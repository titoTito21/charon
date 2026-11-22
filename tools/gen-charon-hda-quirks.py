#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate Charon's HD Audio machine-quirk table from the Linux kernel sources.

    python3 gen-charon-hda-quirks.py <linux tree> <output .h> [--commit SHA] [--tag TAG] [--report FILE]

<linux tree> is a checkout of torvalds/linux (v6.17 or newer: the HD Audio
codec drivers live in sound/hda/codecs/). Only data is taken:

  * the per-driver fixup arrays (struct hda_fixup): the data-only kinds
    HDA_FIXUP_PINS (pin default configuration overrides), HDA_FIXUP_VERBS
    (verb lists) and HDA_FIXUP_PINCTLS, with their chains (.chained,
    .chain_id, .chained_before);
  * the subsystem-ID quirk lists (struct hda_quirk: SND_PCI_QUIRK,
    SND_PCI_QUIRK_MASK, SND_PCI_QUIRK_VENDOR, HDA_CODEC_QUIRK), in the
    kernel's order, because the first match wins;
  * the codec IDs each driver binds to (struct hda_device_id), so that a
    table is only ever applied to the codecs the kernel applies it to.

HDA_FIXUP_FUNC entries are C functions. Those listed in FUNC_PORTS below were
ported by hand into HakHdaQuirks.c and are referenced by number; every other
function is skipped (its chain still continues, as in the kernel) and is
listed in the generated header and in the report.

Every input file must carry "SPDX-License-Identifier: GPL-2.0-or-later"; a
file with another licence stops the generator. The output is therefore
GPL-2.0-or-later and says so.

This script reads untrusted downloaded text: it only parses, never executes
it (expressions are evaluated by a small arithmetic evaluator, not eval()).
"""
import ast, hashlib, operator, re, sys
from pathlib import Path

# ---------------------------------------------------------------------------
# What is read. 'ids': how the kernel driver decides that a codec uses this
# fixup table: ('table', device-id array[, model enum names]) or ('list', [...]).
# ---------------------------------------------------------------------------
REALTEK = 'sound/hda/codecs/realtek/'
FAMILIES = [
    dict(name='alc269', file=REALTEK + 'alc269.c', fixups='alc269_fixups', quirks='alc269_fixup_tbl', ids=('table', 'snd_hda_id_alc269')),
    dict(name='alc882', file=REALTEK + 'alc882.c', fixups='alc882_fixups', quirks='alc882_fixup_tbl', ids=('table', 'snd_hda_id_alc882')),
    dict(name='alc662', file=REALTEK + 'alc662.c', fixups='alc662_fixups', quirks='alc662_fixup_tbl', ids=('table', 'snd_hda_id_alc662')),
    dict(name='alc880', file=REALTEK + 'alc880.c', fixups='alc880_fixups', quirks='alc880_fixup_tbl', ids=('table', 'snd_hda_id_alc880')),
    dict(name='alc260', file=REALTEK + 'alc260.c', fixups='alc260_fixups', quirks='alc260_fixup_tbl', ids=('table', 'snd_hda_id_alc260')),
    dict(name='alc262', file=REALTEK + 'alc262.c', fixups='alc262_fixups', quirks='alc262_fixup_tbl', ids=('table', 'snd_hda_id_alc262')),
    dict(name='alc268', file=REALTEK + 'alc268.c', fixups='alc268_fixups', quirks='alc268_fixup_tbl', ids=('table', 'snd_hda_id_alc268')),
    dict(name='alc861', file=REALTEK + 'alc861.c', fixups='alc861_fixups', quirks='alc861_fixup_tbl', ids=('table', 'snd_hda_id_alc861')),
    dict(name='alc861vd', file=REALTEK + 'alc861vd.c', fixups='alc861vd_fixups', quirks='alc861vd_fixup_tbl', ids=('table', 'snd_hda_id_alc861vd')),
    # conexant.c cx_probe(): switch (codec->core.vendor_id) picks the quirk list.
    dict(name='cxt5045', file='sound/hda/codecs/conexant.c', fixups='cxt_fixups', quirks='cxt5045_fixups', ids=('list', [0x14f15045]),
         check=['case 0x14f15045:', 'cxt5045_fixups, cxt_fixups']),
    dict(name='cxt5047', file='sound/hda/codecs/conexant.c', fixups='cxt_fixups', quirks='cxt5047_fixups', ids=('list', [0x14f15047]),
         check=['case 0x14f15047:', 'cxt5047_fixups, cxt_fixups']),
    dict(name='cxt5051', file='sound/hda/codecs/conexant.c', fixups='cxt_fixups', quirks='cxt5051_fixups', ids=('list', [0x14f15051]),
         check=['case 0x14f15051:', 'cxt5051_fixups, cxt_fixups']),
    dict(name='cxt5066', file='sound/hda/codecs/conexant.c', fixups='cxt_fixups', quirks='cxt5066_fixups',
         ids=('table-except', 'snd_hda_id_conexant', [0x14f15045, 0x14f15047, 0x14f15051]),
         check=['cxt5066_fixups, cxt_fixups']),
    dict(name='sn6186', file='sound/hda/codecs/senarytech.c', fixups='senary_fixups', quirks='sn6186_fixups', ids=('table', 'snd_hda_id_senary')),
    dict(name='cs420x', file='sound/hda/codecs/cirrus/cs420x.c', fixups='cs420x_fixups', quirks='cs420x_fixup_tbl',
         ids=('table', 'snd_hda_id_cs420x', ['CS420X_VENDOR_NID'])),
    dict(name='cs4208', file='sound/hda/codecs/cirrus/cs420x.c', fixups='cs4208_fixups', quirks='cs4208_fixup_tbl',
         ids=('table', 'snd_hda_id_cs420x', ['CS4208_VENDOR_NID'])),
    dict(name='cs421x', file='sound/hda/codecs/cirrus/cs421x.c', fixups='cs421x_fixups', quirks='cs421x_fixup_tbl',
         ids=('table', 'snd_hda_id_cs421x', ['CS4210_VENDOR_NID'])),
    dict(name='stac9200', file='sound/hda/codecs/sigmatel.c', fixups='stac9200_fixups', quirks='stac9200_fixup_tbl', ids=('table', 'snd_hda_id_sigmatel', ['MODEL_STAC9200'])),
    dict(name='stac925x', file='sound/hda/codecs/sigmatel.c', fixups='stac925x_fixups', quirks='stac925x_fixup_tbl', ids=('table', 'snd_hda_id_sigmatel', ['MODEL_STAC925X'])),
    dict(name='stac92hd73xx', file='sound/hda/codecs/sigmatel.c', fixups='stac92hd73xx_fixups', quirks='stac92hd73xx_fixup_tbl', ids=('table', 'snd_hda_id_sigmatel', ['MODEL_STAC92HD73XX']), flags='HAK_HDA_FAM_IDT_GPIO0_EAPD'),
    dict(name='stac92hd83xxx', file='sound/hda/codecs/sigmatel.c', fixups='stac92hd83xxx_fixups', quirks='stac92hd83xxx_fixup_tbl', ids=('table', 'snd_hda_id_sigmatel', ['MODEL_STAC92HD83XXX'])),
    dict(name='stac92hd71bxx', file='sound/hda/codecs/sigmatel.c', fixups='stac92hd71bxx_fixups', quirks='stac92hd71bxx_fixup_tbl', ids=('table', 'snd_hda_id_sigmatel', ['MODEL_STAC92HD71BXX']), flags='HAK_HDA_FAM_IDT_GPIO0_EAPD'),
    dict(name='stac922x', file='sound/hda/codecs/sigmatel.c', fixups='stac922x_fixups', quirks='stac922x_fixup_tbl', ids=('table', 'snd_hda_id_sigmatel', ['MODEL_STAC922X'])),
    dict(name='stac927x', file='sound/hda/codecs/sigmatel.c', fixups='stac927x_fixups', quirks='stac927x_fixup_tbl', ids=('table', 'snd_hda_id_sigmatel', ['MODEL_STAC927X']), flags='HAK_HDA_FAM_IDT_GPIO0_EAPD'),
    dict(name='stac9205', file='sound/hda/codecs/sigmatel.c', fixups='stac9205_fixups', quirks='stac9205_fixup_tbl', ids=('table', 'snd_hda_id_sigmatel', ['MODEL_STAC9205']), flags='HAK_HDA_FAM_IDT_GPIO0_EAPD'),
    dict(name='stac92hd95', file='sound/hda/codecs/sigmatel.c', fixups='stac92hd95_fixups', quirks='stac92hd95_fixup_tbl', ids=('table', 'snd_hda_id_sigmatel', ['MODEL_STAC92HD95'])),
    dict(name='stac9872', file='sound/hda/codecs/sigmatel.c', fixups='stac9872_fixups', quirks='stac9872_fixup_tbl', ids=('table', 'snd_hda_id_sigmatel', ['MODEL_STAC9872'])),
    dict(name='vt2002p', file='sound/hda/codecs/via.c', fixups='via_fixups', quirks='vt2002p_fixups', ids=('table', 'snd_hda_id_via', ['VT2002P'])),
    dict(name='ad1986a', file='sound/hda/codecs/analog.c', fixups='ad1986a_fixups', quirks='ad1986a_fixup_tbl', ids=('table', 'snd_hda_id_analog', ['MODEL_AD1986A']), flags='HAK_HDA_FAM_EAPD_INVERTED'),
    dict(name='ad1981', file='sound/hda/codecs/analog.c', fixups='ad1981_fixups', quirks='ad1981_fixup_tbl', ids=('table', 'snd_hda_id_analog', ['MODEL_AD1981'])),
    dict(name='ad1884', file='sound/hda/codecs/analog.c', fixups='ad1884_fixups', quirks='ad1884_fixup_tbl', ids=('table', 'snd_hda_id_analog', ['MODEL_AD1884'])),
]

# Family flags (hand-written, from the probe functions of the kernel drivers):
#   HAK_HDA_FAM_IDT_GPIO0_EAPD  sigmatel.c probe_stac92hd73xx/92hd71bxx/927x/9205(): "GPIO0 = EAPD": mask, direction
#                               and data 0x01; stac_init() drives it high (statically when there is no jack switching).
#   HAK_HDA_FAM_EAPD_INVERTED   analog.c ad1986a_probe(): "AD1986A has the inverted EAPD implementation".

# Header files that only supply constants (verb numbers, pin control bits).
CONSTANT_FILES = ['sound/hda/common/hda_local.h', 'include/sound/hda_codec.h',
                  REALTEK + 'realtek.h', 'sound/hda/codecs/generic.h', 'sound/hda/common/hda_auto_parser.h']

# include/linux/pci_ids.h is not parsed; the few names the quirk lists use.
PCI_VENDORS = {'PCI_VENDOR_ID_INTEL': 0x8086, 'PCI_VENDOR_ID_DELL': 0x1028, 'PCI_VENDOR_ID_HP': 0x103c,
               'PCI_VENDOR_ID_ASUSTEK': 0x1043, 'PCI_VENDOR_ID_SONY': 0x104d, 'PCI_VENDOR_ID_APPLE': 0x106b,
               'PCI_VENDOR_ID_LENOVO': 0x17aa, 'PCI_VENDOR_ID_IBM': 0x1014, 'PCI_VENDOR_ID_SAMSUNG': 0x144d,
               'PCI_VENDOR_ID_TOSHIBA': 0x1179, 'PCI_VENDOR_ID_ATI': 0x1002, 'PCI_VENDOR_ID_NVIDIA': 0x10de,
               'PCI_VENDOR_ID_AMD': 0x1022, 'PCI_VENDOR_ID_VIA': 0x1106, 'PCI_VENDOR_ID_GATEWAY': 0x107b,
               'PCI_VENDOR_ID_FUJITSU_SIEMENS': 0x1734, 'PCI_VENDOR_ID_CREATIVE': 0x1102, 'PCI_VENDOR_ID_NEC': 0x1033, 'PCI_VENDOR_ID_DFI': 0x15bd}

# Hand-ported function fixups: kernel function -> (HAK_HDA_FN_* name, argument).
# The C side is HakHdaQuirks.c HakQuirkFunction(). Keep both in step.
FUNC_PORTS = {
    # realtek.c alc_fixup_gpioN(): alc_setup_gpio(codec, mask) -> enable, output, high.
    'alc_fixup_gpio1': ('GPIO_HIGH', 0x01), 'alc_fixup_gpio2': ('GPIO_HIGH', 0x02),
    'alc_fixup_gpio3': ('GPIO_HIGH', 0x03), 'alc_fixup_gpio4': ('GPIO_HIGH', 0x04),
    # alc882.c alc885_fixup_macpro_gpio(): alc_fixup_gpio3 with the 1 ms write delay (this driver always waits).
    'alc885_fixup_macpro_gpio': ('GPIO_HIGH', 0x03),
    # realtek.c alc_fixup_sku_ignore(): the codec's "assembly ID" must not select an amplifier GPIO.
    'alc_fixup_sku_ignore': ('SKU_IGNORE', 0),
    # alc269.c alc245_fixup_hp_x360_amp(): GPIO0 enabled as output, pulsed high for 100 ms at init.
    'alc245_fixup_hp_x360_amp': ('GPIO_PULSE', 0x01),
    # alc269.c alc285_fixup_hp_gpio_amp_init(): at init, 100 ms, then COEF 0x65 = 0.   arg = index << 16 | value
    'alc285_fixup_hp_gpio_amp_init': ('ALC_COEF_WRITE_100MS', (0x65 << 16) | 0x0000),
    # alc882.c alc1220_fixup_clevo_p950(): alc_update_coef_idx(codec, 0x7, 0, 0x3c3) (the DAC preference is not needed:
    # every output gets the same stream). ..._pb51ed() calls it and adds a headset-microphone mode.
    'alc1220_fixup_clevo_p950': ('ALC_COEF_SET_BITS', (0x07 << 16) | 0x03c3),
    'alc1220_fixup_clevo_pb51ed': ('ALC_COEF_SET_BITS', (0x07 << 16) | 0x03c3),
    # alc269.c alc287_fixup_yoga9_14iap7_bass_spk_pin(): pin 0x17 := 0x90170121 (bass speakers reported unconnected).
    'alc287_fixup_yoga9_14iap7_bass_spk_pin': ('PIN17_BASS_SPEAKER', 0),
    # cirrus/cs420x.c: GPIO1 or GPIO2 = headphone amplifier, GPIO3 = speaker amplifier (Apple). The kernel switches
    # between them on jack detection; this driver has none and enables both.
    'cs420x_fixup_gpio_13': ('GPIO_HIGH', 0x0a), 'cs420x_fixup_gpio_23': ('GPIO_HIGH', 0x0c),
    'cs4208_fixup_gpio0': ('GPIO_HIGH', 0x01),
    # cs4208_fixup_mac(): picks a Mac model by codec SSID, default CS4208_GPIO0; every model chains to GPIO0.
    # Only the GPIO is ported (the MacBook Air 6 pin list of the sub-table is not applied).
    'cs4208_fixup_mac': ('GPIO_HIGH', 0x01),
    # analog.c ad1986a_fixup_eapd(): this board's EAPD is NOT inverted (AD1986A is otherwise, see family flags).
    'ad1986a_fixup_eapd': ('EAPD_NOT_INVERTED', 0),
    # analog.c ad1884_fixup_hp_eapd(): GPIO1 is a mute line: enabled, output, LOW while playing.
    'ad1884_fixup_hp_eapd': ('GPIO_SET', (0x02 << 16) | (0x02 << 8) | 0x00),
    # sigmatel.c: fixups that replace the sub-family's default amplifier GPIO.  arg = mask << 16 | direction << 8 | data
    'stac92hd83xxx_fixup_gpio10_eapd': ('GPIO_SET', (0x10 << 16) | (0x10 << 8) | 0x10),
    'stac922x_fixup_intel_mac_gpio': ('GPIO_SET', (0x03 << 16) | (0x03 << 8) | 0x03),
    'stac9205_fixup_dell_m43': ('GPIO_SET', (0x1b << 16) | (0x0b << 8) | 0x01),
    'stac9200_fixup_panasonic': ('GPIO_SET', (0x09 << 16) | (0x09 << 8) | 0x00),
    'stac927x_fixup_dell_dmic': ('IDT_DELL_DMIC_GPIO2', 0),     # GPIO2 high unless codec SSID 1028:022f
    'stac92hd73xx_fixup_ref': ('GPIO_SET', 0), 'stac92hd71bxx_fixup_ref': ('GPIO_SET', 0),   # reference boards: no GPIO
    'stac927x_fixup_ref': ('GPIO_SET', 0), 'stac9205_fixup_ref': ('GPIO_SET', 0),            # (their pin lists are not applied)
}
# More ports are merged in from the driver-specific list file next to this script (optional).

# Numbers of the High Definition Audio Specification rev. 1.0a (verbs: section 7.3.3; pin widget control, amplifier
# and EAPD bits: sections 7.3.3.7, 7.3.3.13, 7.3.3.16). The names are the ones the kernel sources use; the kernel's
# own list, include/sound/hda_verbs.h, is GPL-2.0-only and is therefore NOT read by this script.
SPEC_CONSTANTS = {
    'AC_VERB_SET_STREAM_FORMAT': 0x200, 'AC_VERB_SET_AMP_GAIN_MUTE': 0x300, 'AC_VERB_SET_PROC_COEF': 0x400,
    'AC_VERB_SET_COEF_INDEX': 0x500, 'AC_VERB_SET_CONNECT_SEL': 0x701, 'AC_VERB_SET_PROC_STATE': 0x703,
    'AC_VERB_SET_SDI_SELECT': 0x704, 'AC_VERB_SET_POWER_STATE': 0x705, 'AC_VERB_SET_CHANNEL_STREAMID': 0x706,
    'AC_VERB_SET_PIN_WIDGET_CONTROL': 0x707, 'AC_VERB_SET_UNSOLICITED_ENABLE': 0x708, 'AC_VERB_SET_PIN_SENSE': 0x709,
    'AC_VERB_SET_BEEP_CONTROL': 0x70a, 'AC_VERB_SET_EAPD_BTLENABLE': 0x70c, 'AC_VERB_SET_DIGI_CONVERT_1': 0x70d,
    'AC_VERB_SET_DIGI_CONVERT_2': 0x70e, 'AC_VERB_SET_VOLUME_KNOB_CONTROL': 0x70f, 'AC_VERB_SET_GPIO_DATA': 0x715,
    'AC_VERB_SET_GPIO_MASK': 0x716, 'AC_VERB_SET_GPIO_DIRECTION': 0x717, 'AC_VERB_SET_GPIO_WAKE_MASK': 0x718,
    'AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK': 0x719, 'AC_VERB_SET_GPIO_STICKY_MASK': 0x71a,
    'AC_VERB_SET_CONFIG_DEFAULT_BYTES_0': 0x71c, 'AC_VERB_SET_CONFIG_DEFAULT_BYTES_1': 0x71d,
    'AC_VERB_SET_CONFIG_DEFAULT_BYTES_2': 0x71e, 'AC_VERB_SET_CONFIG_DEFAULT_BYTES_3': 0x71f,
    'AC_VERB_SET_CVT_CHAN_COUNT': 0x72d, 'AC_VERB_SET_CODEC_RESET': 0x7ff,
    'AC_PINCTL_EPT': 0x3, 'AC_PINCTL_VREFEN': 0x7, 'AC_PINCTL_VREF_HIZ': 0, 'AC_PINCTL_VREF_50': 1,
    'AC_PINCTL_VREF_GRD': 2, 'AC_PINCTL_VREF_80': 4, 'AC_PINCTL_VREF_100': 5, 'AC_PINCTL_IN_EN': 1 << 5,
    'AC_PINCTL_OUT_EN': 1 << 6, 'AC_PINCTL_HP_EN': 1 << 7,
    'AC_AMP_MUTE': 1 << 7, 'AC_AMP_GAIN': 0x7f, 'AC_AMP_SET_INDEX': 0xf << 8, 'AC_AMP_SET_INDEX_SHIFT': 8,
    'AC_AMP_SET_RIGHT': 1 << 12, 'AC_AMP_SET_LEFT': 1 << 13, 'AC_AMP_SET_INPUT': 1 << 14, 'AC_AMP_SET_OUTPUT': 1 << 15,
    'AC_PWRST_D0': 0, 'AC_PWRST_D1': 1, 'AC_PWRST_D2': 2, 'AC_PWRST_D3': 3, 'AC_USRSP_EN': 1 << 7,
    'AC_EAPDBTL_BALANCED': 1 << 0, 'AC_EAPDBTL_EAPD': 1 << 1, 'AC_EAPDBTL_LR_SWAP': 1 << 2,
    'AC_DIG1_ENABLE': 1 << 0,
}
FOUR_BIT_VERBS = (0x200, 0x300, 0x400, 0x500)

HDA_FIXUP_TYPES = {'HDA_FIXUP_INVALID': 0, 'HDA_FIXUP_PINS': 1, 'HDA_FIXUP_VERBS': 2, 'HDA_FIXUP_FUNC': 3, 'HDA_FIXUP_PINCTLS': 4}
REQUIRED_SPDX = 'GPL-2.0-or-later'


class ParseError(Exception):
    pass


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def spdx_of(path):
    head = path.read_text(encoding='utf-8', errors='replace')[:400]
    m = re.search(r'SPDX-License-Identifier:\s*([^\s*]+(?:\s+(?:WITH|OR|AND)\s+[^\s*]+)*)', head)
    return m.group(1).strip() if m else None


_token = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*', re.S)


def strip_comments(text):
    return _token.sub(lambda m: m.group(0) if m.group(0)[0] in '"\'' else ' ', text)


def strip_if0(text):
    out, depth, skip = [], 0, None
    for line in text.split('\n'):
        s = line.strip()
        if re.match(r'#\s*if', s):
            depth += 1
            if skip is None and re.match(r'#\s*if\s+0\b', s):
                skip = depth
            continue_line = skip is not None
        elif re.match(r'#\s*endif', s):
            continue_line = skip is not None
            if skip == depth:
                skip = None
            depth -= 1
        elif re.match(r'#\s*else', s) and skip == depth:
            skip = None            # "#if 0 ... #else": the else branch is live
            continue_line = True
        else:
            continue_line = skip is not None
        out.append('' if continue_line else line)
    return '\n'.join(out)


def load(path):
    return strip_if0(strip_comments(path.read_text(encoding='utf-8', errors='replace')))


def match_brace(text, start, open_ch='{', close_ch='}'):
    """text[start] is open_ch; return the index after its partner. Skips string literals."""
    depth, i, n = 0, start, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == '\\' else 1
        elif c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ParseError('unbalanced ' + open_ch)


def split_top(text):
    """Split at top-level commas (not inside (), {}, [] or strings)."""
    parts, depth, cur, i, n = [], 0, [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            cur.append(text[i:j + 1]); i = j + 1; continue
        if c in '({[':
            depth += 1
        elif c in ')}]':
            depth -= 1
        if c == ',' and depth == 0:
            parts.append(''.join(cur).strip()); cur = []
        else:
            cur.append(c)
        i += 1
    tail = ''.join(cur).strip()
    if tail:
        parts.append(tail)
    return parts


def find_array(text, decl_regex, name):
    m = re.search(decl_regex.replace('NAME', re.escape(name)) + r'\s*\[[^\]]*\]\s*=\s*\{', text)
    if not m:
        return None
    start = m.end() - 1
    return text[start + 1:match_brace(text, start) - 1]


# --- constants -------------------------------------------------------------
class Constants:
    def __init__(self):
        self.defines, self.values = {}, dict(PCI_VENDORS)
        self.values.update({'true': 1, 'false': 0})
        self.values.update(SPEC_CONSTANTS)
        self.macros = {}

    def add_text(self, text):
        joined = text.replace('\\\n', ' ')
        for m in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+([^\n]+)$', joined, re.M):
            self.defines.setdefault(m.group(1), m.group(2).strip())
        # one-parameter function-like macros, e.g. AMP_IN_UNMUTE(idx)
        for m in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+(\w+)\((\w+)\)[ \t]+([^\n]+)$', joined, re.M):
            self.macros.setdefault(m.group(1), (m.group(2), m.group(3).strip()))
        for m in re.finditer(r'\benum\b[^{;]*\{', joined):
            body = joined[m.end():match_brace(joined, m.end() - 1) - 1]
            value = 0
            for item in split_top(body):
                if not item:
                    continue
                em = re.match(r'^(\w+)\s*(?:=\s*(.+))?$', item, re.S)
                if not em:
                    continue
                if em.group(2) is not None:
                    try:
                        value = self.eval(em.group(2))
                    except ParseError:
                        break                      # unknown expression: stop numbering this enum
                self.values.setdefault(em.group(1), value)
                value += 1

    _ops = {ast.BitOr: operator.or_, ast.BitAnd: operator.and_, ast.BitXor: operator.xor, ast.LShift: operator.lshift,
            ast.RShift: operator.rshift, ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul}

    def eval(self, expr, depth=0):
        if depth > 20:
            raise ParseError('recursive constant ' + expr)
        expr = re.sub(r'\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b', r'\1', expr.strip())
        expr = re.sub(r'\(\s*(?:unsigned\s+)?(?:int|short|char|long|u8|u16|u32|hda_nid_t)\s*\)', '', expr)

        for _ in range(8):                          # expand NAME(arg) macros, innermost first
            mm = None
            for cand in re.finditer(r'\b(\w+)\(([^()]*)\)', expr):
                if cand.group(1) in self.macros:
                    mm = cand
                    break
            if mm is None:
                break
            param, body = self.macros[mm.group(1)]
            expr = expr[:mm.start()] + '(' + re.sub(r'\b' + param + r'\b', '(' + mm.group(2) + ')', body) + ')' + expr[mm.end():]

        def name(m):
            n = m.group(0)
            if re.match(r'0[xX]', n) or n.isdigit():
                return n
            if n in self.values:
                return str(self.values[n])
            if n in self.defines:
                v = self.eval(self.defines[n], depth + 1)
                self.values[n] = v
                return str(v)
            raise ParseError('unknown constant ' + n)
        flat = re.sub(r'0[xX][0-9a-fA-F]+|\b[A-Za-z_]\w*\b|\b\d+\b', name, expr)
        try:
            tree = ast.parse(flat, mode='eval')
        except SyntaxError:
            raise ParseError('cannot parse expression ' + expr)

        def walk(node):
            if isinstance(node, ast.Expression):
                return walk(node.body)
            if isinstance(node, ast.Constant) and isinstance(node.value, int):
                return node.value
            if isinstance(node, ast.BinOp) and type(node.op) in self._ops:
                return self._ops[type(node.op)](walk(node.left), walk(node.right))
            if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Invert):
                return ~walk(node.operand)
            if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
                return -walk(node.operand)
            raise ParseError('unsupported expression ' + expr)
        return walk(tree)


# --- tables ----------------------------------------------------------------
def parse_rows(body, width, consts):
    """Rows '{a, b[, c]}' up to the empty terminator. Returns list of tuples."""
    rows, i = [], 0
    while True:
        j = body.find('{', i)
        if j < 0:
            break
        end = match_brace(body, j)
        fields = [f for f in split_top(body[j + 1:end - 1]) if f]
        i = end
        if not fields:
            break                                  # {} terminator
        if len(fields) != width:
            raise ParseError('row with %d fields, expected %d: %s' % (len(fields), width, body[j:end]))
        rows.append(tuple(consts.eval(f) for f in fields))
    return rows


def parse_data(entry, key, struct, width, text, consts):
    m = re.search(r'\.v\.' + key + r'\s*=\s*', entry)
    if not m:
        return None
    rest = entry[m.end():]
    cm = re.match(r'\(\s*const\s+struct\s+' + struct + r'\s*\[\s*\]\s*\)\s*\{', rest)
    if cm:
        return parse_rows(rest[cm.end():match_brace(rest, cm.end() - 1) - 1], width, consts)
    nm = re.match(r'(\w+)', rest)
    if not nm:
        raise ParseError('cannot read .v.' + key)
    body = find_array(text, r'\bstruct\s+' + struct + r'\s+NAME', nm.group(1))
    if body is None:
        raise ParseError('table %s not found' % nm.group(1))
    return parse_rows(body, width, consts)


def parse_fixups(text, name, consts):
    body = find_array(text, r'\bstruct\s+hda_fixup\s+NAME', name)
    if body is None:
        raise ParseError('fixup array %s not found' % name)
    fixups, i = {}, 0
    for m in re.finditer(r'\[\s*(\w+)\s*\]\s*=\s*\{', body):
        if m.start() < i:
            continue
        end = match_brace(body, m.end() - 1)
        entry, i = body[m.end():end - 1], end
        fid = m.group(1)
        tm = re.search(r'\.type\s*=\s*(\w+)', entry)
        fix = dict(id=fid, type=HDA_FIXUP_TYPES[tm.group(1)] if tm else 0, pins=None, verbs=None, func=None,
                   chained=bool(re.search(r'\.chained\s*=\s*true', entry)),
                   chained_before=bool(re.search(r'\.chained_before\s*=\s*true', entry)), chain=None, error=None)
        cm = re.search(r'\.chain_id\s*=\s*(\w+)', entry)
        if cm:
            fix['chain'] = cm.group(1)
        try:
            if fix['type'] in (1, 4):
                fix['pins'] = parse_data(entry, 'pins', 'hda_pintbl', 2, text, consts)
            elif fix['type'] == 2:
                fix['verbs'] = parse_data(entry, 'verbs', 'hda_verb', 3, text, consts)
            elif fix['type'] == 3:
                fm = re.search(r'\.v\.func\s*=\s*(\w+)', entry)
                fix['func'] = fm.group(1) if fm else None
        except ParseError as e:
            fix['error'] = str(e)
        fixups[fid] = fix
    return fixups


QUIRK_MACRO = re.compile(r'\b(SND_PCI_QUIRK_MASK|SND_PCI_QUIRK_VENDOR|SND_PCI_QUIRK|HDA_CODEC_QUIRK)\s*\(')


def parse_quirks(text, name, consts):
    body = find_array(text, r'\bstruct\s+hda_quirk\s+NAME', name)
    if body is None:
        raise ParseError('quirk list %s not found' % name)
    quirks, i = [], 0
    while True:
        m = QUIRK_MACRO.search(body, i)
        if not m:
            break
        end = match_brace(body, m.end() - 1, '(', ')')
        a = split_top(body[m.end():end - 1])
        i = end
        kind = m.group(1)
        if kind == 'SND_PCI_QUIRK_MASK':
            vend, mask, dev, label, fid = a
            q = dict(vendor=consts.eval(vend), device=consts.eval(dev), mask=consts.eval(mask), codec=False)
        elif kind == 'SND_PCI_QUIRK_VENDOR':
            vend, label, fid = a
            q = dict(vendor=consts.eval(vend), device=0, mask=0, codec=False)
        else:
            vend, dev, label, fid = a
            q = dict(vendor=consts.eval(vend), device=consts.eval(dev), mask=0xffff, codec=(kind == 'HDA_CODEC_QUIRK'))
        # the label only goes into a C comment: plain ASCII, no comment delimiters
        label = re.sub(r'[^ -~]', '?', label.strip('"')).replace('/*', '/ *').replace('*/', '* /').replace('\\', '/')
        q['label'], q['fixup'] = label, fid.strip()
        quirks.append(q)
    return quirks


ID_MACRO = re.compile(r'\b(HDA_CODEC_ID_REV_MODEL|HDA_CODEC_ID_MODEL|HDA_CODEC_ID_REV|HDA_CODEC_ID)\s*\(')


def parse_ids(text, name, consts):
    body = find_array(text, r'\bstruct\s+hda_device_id\s+NAME', name)
    if body is None:
        raise ParseError('device id table %s not found' % name)
    ids, i = [], 0
    while True:
        m = ID_MACRO.search(body, i)
        if not m:
            break
        end = match_brace(body, m.end() - 1, '(', ')')
        a = split_top(body[m.end():end - 1])
        i = end
        kind = m.group(1)
        rev, model = 0, None
        if kind == 'HDA_CODEC_ID':
            vid, label = a
        elif kind == 'HDA_CODEC_ID_REV':
            vid, rev, label = a; rev = consts.eval(rev)
        elif kind == 'HDA_CODEC_ID_MODEL':
            vid, label, model = a
        else:
            vid, rev, label, model = a; rev = consts.eval(rev)
        ids.append(dict(id=consts.eval(vid), rev=rev, label=label.strip('"'), model=model.strip() if model else None))
    return ids


# --- assembling ------------------------------------------------------------
def chain_of(fixups, start):
    """The fixups the kernel's __snd_hda_apply_fixup() visits for 'start', in order."""
    order = []

    def apply(fid, depth):
        while fid is not None and fid in fixups:
            depth += 1
            if depth > 10:
                break
            fix = fixups[fid]
            if fix['chained_before'] and fix['chain']:
                apply(fix['chain'], depth + 1)
            order.append(fid)
            if not fix['chained'] or fix['chained_before']:
                break
            fid = fix['chain']
    apply(start, 0)
    return order


def effective(fix, ports):
    if fix['error']:
        return False
    if fix['type'] in (1, 4):
        return bool(fix['pins'])
    if fix['type'] == 2:
        return bool(fix['verbs'])
    if fix['type'] == 3:
        return fix['func'] in ports
    return False


def main():
    args = sys.argv[1:]
    opts = {'--commit': 'unknown', '--tag': 'unknown', '--report': None}
    pos = []
    while args:
        a = args.pop(0)
        if a in opts:
            opts[a] = args.pop(0)
        else:
            pos.append(a)
    if len(pos) != 2:
        sys.exit(__doc__)
    tree, out_path = Path(pos[0]), Path(pos[1])
    ports = dict(FUNC_PORTS)
    extra = Path(__file__).with_name('gen-charon-hda-quirks-ports.txt')
    if extra.exists():
        for line in extra.read_text().splitlines():
            line = line.split('#')[0].split()
            if len(line) == 3:
                ports[line[0]] = (line[1], int(line[2], 0))

    consts = Constants()
    inputs = {}

    def use(rel):
        path = tree / rel
        if rel not in inputs:
            spdx = spdx_of(path)
            if spdx != REQUIRED_SPDX:
                raise SystemExit('%s: licence %r, only %s is accepted' % (rel, spdx, REQUIRED_SPDX))
            inputs[rel] = (sha256(path), spdx)
        return load(path)

    for rel in CONSTANT_FILES:
        consts.add_text(use(rel))
    texts = {}
    for fam in FAMILIES:
        if fam['file'] not in texts:
            texts[fam['file']] = use(fam['file'])
            consts.add_text(texts[fam['file']])

    out = []
    report = []
    unported = {}          # function -> number of quirk rows whose chain reaches it
    totals = dict(quirks=0, quirks_effective=0, fixups=0, pins=0, verbs=0, parse_errors=0)
    fam_rows = []
    for fam in FAMILIES:
        text = texts[fam['file']]
        for needle in fam.get('check', []):
            if needle not in text:
                raise SystemExit('%s: expected %r in %s (kernel layout changed)' % (fam['name'], needle, fam['file']))
        if not re.search(r'snd_hda_pick_fixup\s*\(\s*codec\s*,\s*\w+\s*,\s*' + fam['quirks'] + r'\s*,\s*' + fam['fixups'] + r'\s*\)', text):
            raise SystemExit('%s: the kernel does not pair %s with %s' % (fam['name'], fam['quirks'], fam['fixups']))
        fixups = parse_fixups(text, fam['fixups'], consts)
        quirks = parse_quirks(text, fam['quirks'], consts)

        def alias(name):
            # "CS420X_IMAC27_122 = CS420X_GPIO_23": an enumerator that only renames another entry.
            if name is None or name in fixups:
                return name
            try:
                value = consts.eval(name)
            except ParseError:
                return name
            same = [k for k in fixups if consts.values.get(k) == value]
            return same[0] if len(same) == 1 else name
        for q in quirks:
            q['fixup'] = alias(q['fixup'])
        for fx in fixups.values():
            fx['chain'] = alias(fx['chain'])
        kind = fam['ids'][0]
        if kind == 'list':
            ids = [dict(id=i, rev=0, label='', model=None) for i in fam['ids'][1]]
        else:
            ids = parse_ids(text, fam['ids'][1], consts)
            if kind == 'table-except':
                ids = [i for i in ids if i['id'] not in fam['ids'][2]]
            elif len(fam['ids']) > 2:
                ids = [i for i in ids if i['model'] in fam['ids'][2]]
        if not ids:
            raise SystemExit('%s: no codec IDs' % fam['name'])

        # Which fixups are reached from the quirk list, and do they do anything?
        used, index = [], {}
        rows = []
        for q in quirks:
            if q['fixup'] not in fixups:
                raise SystemExit('%s: quirk %s names unknown fixup %s' % (fam['name'], q['label'], q['fixup']))
            chain = chain_of(fixups, q['fixup'])
            acts = [f for f in chain if effective(fixups[f], ports)]
            for f in chain:
                fx = fixups[f]
                if fx['type'] == 3 and fx['func'] not in ports:
                    unported[fx['func']] = unported.get(fx['func'], 0) + 1
            rows.append((q, bool(acts)))
            if acts:
                for f in chain:
                    if f not in index:
                        index[f] = len(used); used.append(f)
        pins_pool, verbs_pool = [], []
        fix_c = []
        for f in used:
            fx = fixups[f]
            typ, data, arg = 0, 0, 0
            if fx['error']:
                totals['parse_errors'] += 1
                report.append('PARSE %s %s: %s' % (fam['name'], f, fx['error']))
            elif fx['type'] in (1, 4) and fx['pins']:
                typ, data = fx['type'], len(pins_pool)
                for nid, val in fx['pins']:
                    if not 0 < nid < 0x100:
                        raise SystemExit('%s %s: node %#x' % (fam['name'], f, nid))
                    pins_pool.append((nid, val & 0xffffffff))
                pins_pool.append((0, 0))
                totals['pins'] += 1
            elif fx['type'] == 2 and fx['verbs']:
                typ, data = 2, len(verbs_pool)
                for nid, verb, param in fx['verbs']:
                    if not (0 < nid < 0x100 and 0 <= verb < 0x1000 and 0 <= param < 0x10000):
                        raise SystemExit('%s %s: verb %#x %#x %#x' % (fam['name'], f, nid, verb, param))
                    # The 20-bit verb field of the HD Audio command: 12-bit verbs carry
                    # 8 bits of payload, 4-bit verbs (0x200..0x500) carry 16 bits.
                    if verb not in FOUR_BIT_VERBS and (param > 0xff or verb < 0x700):
                        raise SystemExit('%s %s: verb %#x with payload %#x' % (fam['name'], f, verb, param))
                    word = (verb << 8) | param
                    verbs_pool.append((nid << 24) | word)
                verbs_pool.append(0)
                totals['verbs'] += 1
            elif fx['type'] == 3 and fx['func'] in ports:
                typ = 3
                fn, arg = ports[fx['func']]
                data = 'HAK_HDA_FN_' + fn
            flags = (1 if fx['chained'] and fx['chain'] in index else 0) | (2 if fx['chained_before'] and fx['chain'] in index else 0)
            chain = index.get(fx['chain'], 0) if fx['chain'] else 0
            fix_c.append((typ, flags, chain, data, arg, f, fx['func']))
        if len(used) >= 0x7fff or len(pins_pool) > 0xffff or len(verbs_pool) > 0xffff:
            raise SystemExit('%s: table too large' % fam['name'])
        n = fam['name']
        out.append('/* %s: %s  (%s + %s) */' % (n, fam['file'], fam['fixups'], fam['quirks']))
        out.append('static CONST HAK_HDA_CODEC_ID mHakIds_%s[] = {' % n)
        out += ['  { 0x%08x, 0x%06x },  /* %s */' % (i['id'], i['rev'], i['label']) for i in ids]
        out.append('};')
        if pins_pool:
            out.append('static CONST HAK_HDA_PINCFG mHakPins_%s[] = {' % n)
            out += ['  { 0x%02x, 0x%08x },' % p for p in pins_pool]
            out.append('};')
        if verbs_pool:
            out.append('static CONST UINT32 mHakVerbs_%s[] = {' % n)
            out += ['  0x%08x,' % v for v in verbs_pool]
            out.append('};')
        if fix_c:
            out.append('static CONST HAK_HDA_FIXUP mHakFixups_%s[] = {' % n)
            for k, (typ, flags, chain, data, arg, fid, func) in enumerate(fix_c):
                out.append('  { %d, %d, %d, %s, 0x%x },  /* %d %s%s */' % (typ, flags, chain, data, arg, k, fid,
                           (' ' + func + '()' + ('' if typ == 3 else ' NOT PORTED')) if func else ''))
            out.append('};')
        if rows:
            out.append('static CONST HAK_HDA_QUIRK mHakQuirks_%s[] = {' % n)
        eff = 0
        for q, has in rows:
            fidx = index[q['fixup']] if has else 0x7fff
            eff += has
            out.append('  { 0x%04x, 0x%04x, 0x%04x, 0x%04x },  /* %s%s -> %s%s */' % (
                q['vendor'], q['device'], q['mask'], fidx | (0x8000 if q['codec'] else 0),
                q['label'].replace('*/', '* /'), ' [codec SSID]' if q['codec'] else '', q['fixup'], '' if has else ' (nothing to do)'))
        if rows:
            out.append('};')
        out.append('')
        fam_rows.append((n, len(ids), len(fix_c), len(rows), bool(pins_pool), bool(verbs_pool)))
        totals['quirks'] += len(rows); totals['quirks_effective'] += eff; totals['fixups'] += len(fix_c)
        report.append('FAMILY %-14s codec IDs %3d  quirk rows %4d  with an action %4d  fixups kept %3d  (of %d in the kernel array)' % (
            n, len(ids), len(rows), eff, len(fix_c), len(fixups)))

    head = ['/* GENERATED FILE - do not edit. Made by tools/gen-charon-hda-quirks.py.',
            ' * SPDX-License-Identifier: GPL-2.0-or-later',
            ' *',
            ' * HD Audio machine quirks (pin configuration overrides, verb lists and their',
            ' * chains, subsystem-ID lists) taken as data from the Linux kernel:',
            ' *   https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git',
            ' *   tag %s, commit %s' % (opts['--tag'], opts['--commit']),
            ' * Copyright of that data: the Linux kernel contributors (see each input file).',
            ' *',
            ' * Input files (SHA-256, licence):']
    head += [' *   %s  %s  %s' % (h, s, rel) for rel, (h, s) in sorted(inputs.items())]
    head += [' *',
             ' * Totals: %(quirks)d subsystem-ID rows, %(quirks_effective)d of them with at least one action this' % totals,
             ' * driver performs; %(fixups)d fixups kept (%(pins)d pin lists, %(verbs)d verb lists); %(parse_errors)d unreadable.' % totals,
             ' * Function fixups ported by hand: ' + ', '.join(sorted(ports)) + '.',
             ' * Function fixups NOT ported (name: quirk rows whose chain reaches it):']
    line = ' *  '
    for fn, cnt in sorted(unported.items(), key=lambda kv: (-kv[1], kv[0])):
        item = ' %s:%d' % (fn, cnt)
        if len(line) + len(item) > 100:
            head.append(line); line = ' *  '
        line += item
    head += [line, ' */', '#ifndef HAK_HDA_QUIRK_TABLE_H', '#define HAK_HDA_QUIRK_TABLE_H', '',
             '#define HAK_HDA_QUIRK_KERNEL_TAG     "%s"' % opts['--tag'],
             '#define HAK_HDA_QUIRK_KERNEL_COMMIT  "%s"' % opts['--commit'],
             '#define HAK_HDA_QUIRK_ROWS           %d' % totals['quirks'],
             '#define HAK_HDA_QUIRK_ROWS_ACTIVE    %d' % totals['quirks_effective'], '']
    tail = ['static CONST HAK_HDA_FAMILY mHakFamilies[] = {']
    for n, nids, nfix, nrows, has_pins, has_verbs in fam_rows:
        flags = [f.get('flags', '0') for f in FAMILIES if f['name'] == n][0]
        tail.append('  { "%s", mHakIds_%s, %d, %s, %d, %s, %d, %s, %s, %s },' % (
            n, n, nids, ('mHakFixups_' + n) if nfix else 'NULL', nfix, ('mHakQuirks_' + n) if nrows else 'NULL', nrows,
            ('mHakPins_' + n) if has_pins else 'NULL', ('mHakVerbs_' + n) if has_verbs else 'NULL', flags))
    tail += ['};', '', '#endif']
    out_path.write_text('\n'.join(head + out + tail) + '\n', encoding='utf-8', newline='\n')
    report.append('TOTAL quirk rows %(quirks)d, with an action %(quirks_effective)d, fixups %(fixups)d (pin lists %(pins)d, verb lists %(verbs)d), unreadable %(parse_errors)d' % totals)
    report.append('PORTED ' + ' '.join(sorted(ports)))
    report.append('UNPORTED ' + ' '.join('%s:%d' % kv for kv in sorted(unported.items(), key=lambda kv: (-kv[1], kv[0]))))
    text = '\n'.join(report) + '\n'
    if opts['--report']:
        Path(opts['--report']).write_text(text, encoding='utf-8', newline='\n')
    sys.stdout.write(text)


if __name__ == '__main__':
    main()
