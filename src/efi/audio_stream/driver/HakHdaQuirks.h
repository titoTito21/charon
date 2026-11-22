/* HAK HDA codec families and machine quirks (Charon r408).
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file, HakHdaQuirkMatch.h, HakHdaQuirks.c and the generated
 * HakHdaQuirkTable.h contain logic and data ported from the Linux kernel
 * (sound/hda/codecs/ and sound/hda/common/ of tag v7.3, files under
 * GPL-2.0-or-later only). An AudioDxe built with them is, as a whole,
 * distributed under GPL-2.0-or-later; the unchanged AudioDxe files keep
 * their MIT notices. See ../../AUDIO-CODECS.txt.
 *
 * Call order in the codec driver (apply-driver-extension.py):
 *   HdaCodecProbeCodec()
 *   HakHdaQuirkProbe()        pin configuration overrides (software only),
 *                             Realtek EAPD-by-verb coefficients, quirk pick
 *   HdaCodecParsePorts()
 *   ... per playback set-up:
 *   HakHdaCodecPrepareOutputs()   (r406: D0, EAPD, Realtek SKU GPIO)
 *   HdaCodecEnableWidgetPath() for every output
 *   HakHdaQuirkFinish()       amplifier GPIOs, family init, verb lists
 *
 * Nothing here returns an error to the caller: a failure in a quirk never
 * stops playback through the generic path. */
#ifndef HAK_HDA_QUIRKS_H
#define HAK_HDA_QUIRKS_H

#include <Uefi.h>

struct _HDA_CODEC_DEV;

/* --- table format (HakHdaQuirkTable.h is generated in this format) --- */

typedef struct {
  UINT32    Id;    /* codec vendor/device ID */
  UINT32    Rev;   /* 0 = any revision */
} HAK_HDA_CODEC_ID;

typedef struct {
  UINT8     Nid;   /* 0 terminates a list */
  UINT32    Config;
} HAK_HDA_PINCFG;

#define HAK_HDA_FIX_NONE     0
#define HAK_HDA_FIX_PINS     1   /* Data: index into the family's pin pool */
#define HAK_HDA_FIX_VERBS    2   /* Data: index into the family's verb pool */
#define HAK_HDA_FIX_FUNC     3   /* Data: HAK_HDA_FN_*, Arg: its argument */
#define HAK_HDA_FIX_PINCTLS  4   /* Data: index into the pin pool; Config = pin widget control */

#define HAK_HDA_FIX_CHAINED         1   /* continue with Chain afterwards */
#define HAK_HDA_FIX_CHAINED_BEFORE  2   /* apply Chain first, then stop */

typedef struct {
  UINT8     Type;
  UINT8     Flags;
  UINT16    Chain;
  UINT16    Data;
  UINT32    Arg;
} HAK_HDA_FIXUP;

#define HAK_HDA_QUIRK_CODEC_SSID  0x8000   /* row is matched against the codec SSID only */
#define HAK_HDA_QUIRK_NO_ACTION   0x7fff   /* row exists in the kernel, nothing this driver does */

typedef struct {
  UINT16    SubVendor;
  UINT16    SubDevice;   /* 0 = any device of the vendor */
  UINT16    Mask;
  UINT16    Fixup;       /* index | HAK_HDA_QUIRK_CODEC_SSID */
} HAK_HDA_QUIRK;

#define HAK_HDA_FAM_IDT_GPIO0_EAPD  0x1   /* IDT 92HD73xx/71Bxx, STAC927x/9205: GPIO0 is the amplifier enable */
#define HAK_HDA_FAM_EAPD_INVERTED   0x2   /* AD1986A: EAPD bit 0 = amplifier on */

typedef struct {
  CONST CHAR8               *Name;
  CONST HAK_HDA_CODEC_ID    *Ids;
  UINT16                    IdCount;
  CONST HAK_HDA_FIXUP       *Fixups;
  UINT16                    FixupCount;
  CONST HAK_HDA_QUIRK       *Quirks;
  UINT16                    QuirkCount;
  CONST HAK_HDA_PINCFG      *Pins;
  CONST UINT32              *Verbs;    /* (nid << 24) | 20-bit verb; 0 terminates a list */
  UINT32                    Flags;
} HAK_HDA_FAMILY;

/* Hand-ported function fixups. tools/gen-charon-hda-quirks.py FUNC_PORTS maps
 * kernel function names to these; HakHdaQuirks.c implements them. */
enum {
  HAK_HDA_FN_NONE = 0,
  HAK_HDA_FN_GPIO_HIGH,             /* Arg = mask: enable, output, high */
  HAK_HDA_FN_GPIO_SET,              /* Arg = mask << 16 | direction << 8 | data: replaces the family default */
  HAK_HDA_FN_GPIO_PULSE,            /* Arg = mask: enable, output; high for 100 ms after the paths are up, then low */
  HAK_HDA_FN_SKU_IGNORE,            /* Realtek: the assembly ID must not select an amplifier GPIO */
  HAK_HDA_FN_ALC_COEF_SET_BITS,     /* Arg = index << 16 | bits: Realtek COEF |= bits, when the codec is bound */
  HAK_HDA_FN_ALC_COEF_WRITE_100MS,  /* Arg = index << 16 | value: after the paths are up, 100 ms, COEF = value */
  HAK_HDA_FN_PIN17_BASS_SPEAKER,    /* pin 0x17 := 0x90170121 */
  HAK_HDA_FN_EAPD_NOT_INVERTED,     /* cancels HAK_HDA_FAM_EAPD_INVERTED for this board */
  HAK_HDA_FN_IDT_DELL_DMIC_GPIO2    /* IDT STAC927x Dell: GPIO2 = amplifier unless codec SSID 1028:022f */
};

/* --- driver interface --- */

/* After the codec was probed, before its ports are parsed. */
VOID
HakHdaQuirkProbe (
  IN struct _HDA_CODEC_DEV  *HdaCodecDev
  );

/* After the output paths of a playback set-up are enabled. */
VOID
HakHdaQuirkFinish (
  IN struct _HDA_CODEC_DEV  *HdaCodecDev
  );

/* TRUE when EAPD = 0 switches this codec's amplifier on. */
BOOLEAN
HakHdaQuirkEapdInverted (
  IN struct _HDA_CODEC_DEV  *HdaCodecDev
  );

/* TRUE when the Realtek assembly-ID GPIO rule must not be applied. */
BOOLEAN
HakHdaQuirkSkuIgnored (
  IN struct _HDA_CODEC_DEV  *HdaCodecDev
  );

/* Realtek amplifier GPIO mask of the assembly ID (HakHdaCodecCompat.c), 0 = EAPD only. */
UINT8
HakHdaCodecRealtekSkuGpio (
  IN struct _HDA_CODEC_DEV  *HdaCodecDev
  );

#endif
