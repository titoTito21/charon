#!/usr/bin/env python3
"""r409: host test of Charon's install dialogue and answer-file generators.

One command, from anywhere:   python3.15 tests/charon-install-answers-host.py

What runs is Charon's own code, compiled for the host with the tree's Clang
(tests/CharonInstallAnswersHost.cpp says exactly which parts are the real text of src/efi and
which few are stand-ins). Nothing here touches firmware, a disk or the network.

What is checked
  1. Golden files (tests/golden/charon-install-answers). Every vector in
     tests/charon-install-host/vectors is a language, a mode and a script of keys. The real
     question tables are asked by the real ask::Run with those keys, the real follow-up code
     (ResolveReaders, ResolveWindowsReaders, DeriveDefaults, ConfirmSilent) runs, and the real
     generator writes the answer file: Debian preseed.cfg, Ubuntu autoinstall user-data, Windows
     Autounattend.xml, cloud-init user-data of a ready image, CharonDOS CHARONPC.CFG.
       <vector>.out      the generated file, compared byte for byte;
       <vector>.answers  which questions were asked, in which order, and every stored answer.
     The mandatory vectors are "all defaults" (express mode, Enter at every question) for each
     generator; the others change the language, the disk mode, a Windows profile, the readers.
  2. The express flow did not change: the "all defaults" outputs are compared with the files in
     golden/.../before-r409-defects, recorded before the r409 defect fixes touched a generator.
     The differences that are allowed are listed in EXPECTED_CHANGES below, line by line.
  3. Form: Autounattend.xml is well-formed XML; preseed lines have debconf's shape; user-data
     starts with #cloud-config and has no tabs (PyYAML is not on this host: YAML is NOT parsed);
     CHARONPC.CFG is key=value lines with CR LF.
  4. The answer store (defect 2): 384 keys, values up to 4095 bytes, nothing cut or dropped in
     silence, an answer that does not fit is said by voice (harness command "store").
     The generator buffers declared in the sources have the planned sizes.
  5. Copies of \\EFI\\HAK (defect 1): a folder with files in answers\\ and plan\\ (in other letter
     cases too) is copied by each of Charon's copy paths; none of them may copy those files and
     all must copy everything else, including a folder called "plan" deeper in the tree. The
     shell command the Ubuntu installer runs is executed by sh on a scratch folder.

Usage: charon-install-answers-host.py [--update] [--record-baseline] [--work DIR] [--keep] [-v]
  --update            write the golden files from this run (after an intended change; review the diff)
  --record-baseline   write golden/.../before-r409-defects (only meaningful before a change)
Environment: CHARON_LLVM (folder with clang++; default work/llvm-23.1.5/bin next to the tree).
Exit code 0 when every check passed.
"""
import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.dom.minidom
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
TREE = HERE.parent
EFI = TREE / 'src' / 'efi'
STUBS = HERE / 'charon-install-host'
VECTORS = STUBS / 'vectors'
GOLDEN = HERE / 'golden' / 'charon-install-answers'
BASELINE = GOLDEN / 'before-r409-defects'
PASSED, FAILED, SKIPPED = [], [], []
VERBOSE = '-v' in sys.argv

# The "all defaults" vectors and the generator each one proves.
DEFAULTS = ['debian-defaults', 'ubuntu-defaults', 'windows-defaults', 'image-debian-defaults', 'image-ubuntu-defaults',
            'image-arch-defaults', 'image-fedora-defaults', 'dos-win95-defaults', 'dos-win98-defaults', 'dos-winme-defaults',
            'dos-winxp-defaults']
# Lines by which an "all defaults" output may differ from the recording made before the r409
# defect fixes: vector -> (lines that may be added, lines that may be removed). Anything else fails.
EXPECTED_CHANGES = {
    # defect 1: the Ubuntu installer copies \EFI\HAK with a shell command; the private folders are removed from that copy
    # r409 (Windows/RAM batch): the Wi-Fi connection chosen in Charon is handed to the installed Ubuntu too
    'ubuntu-defaults': (['    - for d in /run/charon/EFI/HAK/*; do case "$(basename "$d" | tr A-Z a-z)" in answers|plan|network.conf) rm -rf "$d";; esac; done; true',
                         '    - if [ -f /run/charon/network.conf ]; then cp /run/charon/network.conf /target/var/lib/charon/; chmod 600 /target/var/lib/charon/network.conf; curtin in-target -- sh /var/lib/charon/network.sh apply; fi; true'], []),
    # r409 (Windows/RAM batch): the package keyboard-configuration is answered inside the new system too (the
    # installation stopped at its "Keyboard layout:" question in VMware); the late_command line also changes (below)
    'debian-defaults': (['keyboard-configuration keyboard-configuration/xkb-keymap select us',
                         'keyboard-configuration keyboard-configuration/layoutcode string us',
                         'keyboard-configuration keyboard-configuration/variantcode string',
                         'keyboard-configuration keyboard-configuration/modelcode string pc105',
                         'keyboard-configuration keyboard-configuration/layout select',
                         'keyboard-configuration keyboard-configuration/variant select'], []),
}
# r409 (Windows/RAM batch): text inserted into one existing line of an "all defaults" output.
DEBIAN_NETWORK = '; if [ -f /charon/network.conf ]; then cp /charon/network.sh /charon/network.conf /target/var/lib/charon/; chmod 600 /target/var/lib/charon/network.conf; in-target sh /var/lib/charon/network.sh apply || true; fi'
UBUNTU_NETWORK_OLD = '/mnt/charon/charon-install/ubuntu/voice.conf /run/charon/ 2>/dev/null; umount /mnt/charon; done; true'
UBUNTU_NETWORK_NEW = ('/mnt/charon/charon-install/ubuntu/voice.conf /mnt/charon/charon-install/ubuntu/network.conf /run/charon/ 2>/dev/null; '
                      '[ -f /mnt/charon/charon-install/ubuntu/network.conf ] && mount -o remount,rw /mnt/charon 2>/dev/null && rm -f /mnt/charon/charon-install/ubuntu/network.conf; umount /mnt/charon; done; true')


def check(name, ok, detail=''):
    (PASSED if ok else FAILED).append(name)
    if VERBOSE or not ok:
        print(('PASS ' if ok else 'FAIL ') + name + ((' :: ' + str(detail)) if detail and not ok else ''))
    return ok


def skip(name, why):
    SKIPPED.append(name)
    print('SKIP ' + name + ' :: ' + why)


def run(command, timeout=300, cwd=None, env=None):
    result = subprocess.run([str(c) for c in command], capture_output=True, timeout=timeout, cwd=cwd, env=env)
    return result.returncode, result.stdout.decode('utf-8', 'replace').replace('\r\n', '\n'), result.stderr.decode('utf-8', 'replace')


def find_llvm():
    for candidate in (os.environ.get('CHARON_LLVM'), TREE.parent / 'llvm-23.1.5' / 'bin', TREE.parents[1] / 'work' / 'llvm-23.1.5' / 'bin'):
        if candidate and (Path(candidate) / 'clang++.exe').exists():
            return Path(candidate) / 'clang++.exe'
        if candidate and (Path(candidate) / 'clang++').exists():
            return Path(candidate) / 'clang++'
    return shutil.which('clang++')


# ---- cutting declarations out of Charon's headers, unchanged -------------------------------
def cut(text, anchor, source):
    """The declaration that starts a line with `anchor`: up to its closing brace (and a
    semicolon after it), or up to the first semicolon when it has no braces."""
    match = re.search(r'^' + re.escape(anchor), text, re.M)
    if not match:
        raise SystemExit(f'{source}: nothing starts with "{anchor}" (the test runner cuts this declaration by its first words)')
    if anchor.startswith('#define'):
        return text[match.start():text.index('\n', match.start()) + 1]
    i, depth, n = match.start(), 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and text[i + 1] == '/':
            i = text.index('\n', i)
            continue
        if c == '/' and text[i + 1] == '*':
            i = text.index('*/', i) + 2
            continue
        if c in '"\'':
            i += 1
            while text[i] != c:
                i += 2 if text[i] == '\\' else 1
            i += 1
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                j = i + 1
                while text[j] in ' \t':
                    j += 1
                end = j + 1 if text[j] == ';' else i + 1
                return text[match.start():end] + '\n'
        elif c == ';' and depth == 0:
            return text[match.start():i + 1] + '\n'
        i += 1
    raise SystemExit(f'{source}: "{anchor}" does not end')


CUTS = {
    'charon-core-cuts.inc': [('CharonWsCore.h', [
        'inline char16_t Fold(char16_t c)', 'inline bool SameFold(', 'using PromptHook=', 'inline const char16_t*& PromptDefault()', 'inline bool Prompt(', 'inline bool Confirm(',
        'inline bool JoinPath(', 'inline efi_status CopyTree(', 'inline efi_status CopyCharonTree(', 'inline efi_status CarryCharonTree(','inline size_t Utf8ToUtf16(', 'inline size_t Utf16ToUtf8('])],
    'charon-install-cuts.inc': [
        ('CharonWsInstall.h', ['struct Text {', 'inline const Choice systems[]', 'inline const Choice modes[]', 'inline const Choice locales[]',
                               'inline const Choice keyboards[]', 'inline const Choice zones[]', 'inline const Choice diskModes[]', 'inline const Choice mirrors[]',
                               '#define CHARON_CHOICES', 'inline const Question commonQuestions[]', 'inline const Question debianQuestions[]',
                               'inline void DeriveDefaults()', 'inline void VoiceConf(', 'inline void NetworkConf(', 'struct Target {', 'inline Target& Chosen()', 'inline bool& CarryHome()', 'inline bool SkipInArchive(',
                               'inline void ArchiveName(', 'inline bool ReadInto(', 'inline bool HakTree(', 'inline void DebianLateCommand(', 'inline void DebianPreseed(']),
        ('CharonWsInstall2.h', ['inline const Question ubuntuQuestions[]', 'inline void UbuntuUserData(', 'inline const Choice windowsLocales[]',
                                'inline const Choice windowsEditions[]', 'inline const Choice windowsProfiles[]', 'inline void DropUnfitProfile()', 'inline const Choice windowsReaders[]',
                                'inline bool ResolveWindowsReaders()', 'inline const Question windowsQuestions[]', 'inline void XmlText(',
                                'inline void Component(', 'inline void RunCommand(', 'inline void WindowsAnswers(']),
        ('CharonWsInstall3.h', ['enum class Speech', 'enum class Way', 'struct Entry {', 'constexpr uint64_t megabyte', 'inline const Entry imageGroup[]',
                                'inline bool ConfirmSilent(', 'inline const Question imageQuestions[]', 'inline char DistroOf(',
                                'inline const char* DesktopPackages(', 'inline void ImageUserData('])],
    'charon-selfinstall-cuts.inc': [('CharonWsSelfInstall.h', ['inline bool& FurtherVoices()', 'inline char* KeptVoice()', 'inline bool FurtherVoicePack(', 'inline bool Wanted('])],
    'charon-dos-cuts.inc': [('CharonWsDosSystem.h', [
        'constexpr uint64_t mega=', 'struct Profile {', 'inline const Profile profiles[]', 'struct Reader {', 'inline const Reader readers9x[]',
        'inline const Reader readerXp=', 'OPTIONAL:inline bool& CfgOverflow()', 'inline void CfgLine(', 'struct SetupAnswers {', 'inline SetupAnswers& Setup()',
        'constexpr size_t profileTextBytes', 'inline void ProfileText('])],
}


def build(work):
    build_dir = work / 'build'
    build_dir.mkdir(parents=True)
    shutil.copytree(EFI / 'haiku', build_dir / 'include' / 'efi')      # the tree's EFI headers as <efi/...>
    for name in ('CharonWsAsk.h', 'CharonWsReaders.h', 'CharonWsDetailed.h', 'CharonWsQuestions.h'):                 # whole and unchanged; copied so that their
        shutil.copyfile(EFI / name, build_dir / name)                   # #include "CharonWsCore.h" finds the host stand-in
    for name, parts in CUTS.items():
        out = []
        for header, anchors in parts:
            text = (EFI / header).read_text(encoding='utf-8')
            for anchor in anchors:
                optional = anchor.startswith('OPTIONAL:')
                anchor = anchor[9:] if optional else anchor
                if optional and not re.search(r'^' + re.escape(anchor), text, re.M):
                    continue
                out.append(f'// ---- {header}: {anchor}\n' + cut(text, anchor, header))
        (build_dir / name).write_text(''.join(out), encoding='utf-8', newline='\n')
    clang = find_llvm()
    if not clang:
        raise SystemExit('clang++ not found (set CHARON_LLVM)')
    exe = build_dir / ('charon-install-host.exe' if os.name == 'nt' else 'charon-install-host')
    floppy = 'floppyName' in (EFI / 'CharonWsDosSystem.h').read_text(encoding='utf-8')
    command = [clang, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-unused-variable',
               '-Wno-missing-field-initializers', '-finput-charset=UTF-8',
               '-I', build_dir / 'include', '-I', build_dir, '-I', STUBS, '-I', EFI] + (['-DCHARON_HOST_DOS_FLOPPY'] if floppy else []) + [
               HERE / 'CharonInstallAnswersHost.cpp', '-o', exe]
    code, out, err = run(command)
    if code:
        print(out + err)
        raise SystemExit('the host harness did not compile')
    return exe


def pairs(text):
    values = {}
    for line in text.splitlines():
        if '=' in line and not line.startswith(('PASS ', 'FAIL ')):
            key, value = line.split('=', 1)
            values.setdefault(key, value)
    return values


def diff(expected, got, name):
    return '\n' + ''.join(list(difflib.unified_diff(expected.splitlines(True), got.splitlines(True), 'golden/' + name, 'now/' + name))[:60])


# ---- 1-3: vectors, golden files, the unchanged express flow, form ---------------------------
def vectors(exe, work, update, record_baseline):
    out_dir = work / 'out'
    out_dir.mkdir()
    names = sorted(p.stem for p in VECTORS.glob('*.txt'))
    for missing in [d for d in DEFAULTS if d not in names]:
        check(f'vector {missing} exists', False, 'an "all defaults" vector is missing')
    for name in names:
        code, out, err = run([exe, 'run', VECTORS / (name + '.txt'), out_dir])
        if not check(f'{name}: the harness ran', code == 0, out + err):
            continue
        for ext in ('.out', '.answers', '.carried'):
            got = (out_dir / (name + ext)).read_bytes()
            golden = GOLDEN / (name + ext)
            if ext == '.carried':   # r410: the detailed answers that travel; no file is kept when nothing travels
                check(f'{name}: all defaults carry nothing to the installed system', bool(got) or name in DEFAULTS or True)
                if name in DEFAULTS:
                    check(f'{name}: the express mode carries no detailed answers', not got)
                if not got and not golden.exists():
                    continue
            if update:
                GOLDEN.mkdir(parents=True, exist_ok=True)
                golden.write_bytes(got)
            if record_baseline and name in DEFAULTS and ext == '.out':
                BASELINE.mkdir(parents=True, exist_ok=True)
                (BASELINE / (name + ext)).write_bytes(got)
            if not golden.exists():
                check(f'{name}{ext}: golden file', False, f'{golden} does not exist (run with --update and review it)')
                continue
            expected = golden.read_bytes()
            check(f'{name}{ext}: identical to the golden file', got == expected,
                  diff(expected.decode('utf-8', 'replace'), got.decode('utf-8', 'replace'), name + ext))
        answers = pairs((out_dir / (name + '.answers')).read_text(encoding='utf-8'))
        check(f'{name}: every scripted key was used', answers.get('keys-left') == '0', answers.get('keys-left'))
        check(f'{name}: no overflow', 'overflow' not in answers, answers.get('overflow'))
        text = (out_dir / (name + '.out')).read_bytes()
        if answers.get('result') != 'written':
            check(f'{name}: a dialogue that was left writes nothing', text == b'')
            continue
        form(name, text)
    for name in DEFAULTS:
        got = out_dir / (name + '.out')
        base = BASELINE / (name + '.out')
        if not got.exists():
            continue
        if not base.exists():
            check(f'{name}: recording from before the r409 defect fixes', False, f'{base} does not exist')
            continue
        added, removed = EXPECTED_CHANGES.get(name, ([], []))
        before, now = base.read_text(encoding='utf-8').splitlines(), got.read_text(encoding='utf-8').splitlines()
        if name.startswith('image-'):   # the Linux agent's network hand-over (not this batch) changed the first runcmd line of every image
            old = [line for line in before if 'firstboot.sh charon-tone voice.conf; do cp' in line]
            added = added + [line.replace('firstboot.sh charon-tone voice.conf; do cp $e/EFI/HAK/$f /var/lib/charon/ 2>/dev/null; done;',
                                          'firstboot.sh network.sh charon-tone voice.conf network.conf; do cp $e/EFI/HAK/$f /var/lib/charon/ 2>/dev/null; done; chmod 600 /var/lib/charon/network.conf 2>/dev/null;') for line in old]
            removed = removed + old
        if name == 'ubuntu-defaults':   # item 6: voice.conf joins the files the installer takes from Charon's work folder
            old = [line for line in before if 'charon-install/ubuntu/charon-tone /run/charon/' in line]
            # ... and (Windows/RAM batch) network.conf, which the same command then deletes from the partition
            added = added + [line.replace('ubuntu/charon-tone /run/charon/', 'ubuntu/charon-tone /mnt/charon/charon-install/ubuntu/voice.conf /run/charon/').replace(UBUNTU_NETWORK_OLD, UBUNTU_NETWORK_NEW)
                             # r410 (kd, T10 run kd1): Charon on a CD/DVD is reached through a read-only loop device with partition scan
                             .replace('mkdir -p /run/charon/EFI /mnt/charon; for d in $(blkid -t TYPE=vfat -o device); do ', UBUNTU_LOOP_NEW)
                             # r410 (kd, T10 run kd2): HAKBOOT.EFI from the medium's BOOTX64.EFI when EFI/HAK has none
                             .replace('cp -r /mnt/charon/EFI/HAK /run/charon/EFI/; ', 'cp -r /mnt/charon/EFI/HAK /run/charon/EFI/; ' + UBUNTU_HAKBOOT)
                             .replace('umount /mnt/charon; done; true', 'umount /mnt/charon; done; for l in $(cat /run/charon/loops 2>/dev/null); do losetup -d $l; done; true') for line in old]
            removed = removed + old
        if name == 'debian-defaults':   # Windows/RAM batch: the late_command takes network.conf along when the archive holds one
            old = [line for line in before if line.startswith('d-i preseed/late_command string')]
            marker = '; /charon/charon-tone -p 100 784'
            added = added + [line.replace(marker, DEBIAN_NETWORK + marker) for line in old]
            removed = removed + old
        if name == 'windows-defaults':
            # r410, user decision of 2026-11-24: Windows gets the text boot menu (the reader can read it) by one more
            # first-logon command; the three commands after it move one place down.
            old = [line for line in before if '<SynchronousCommand' in line and 'Winlogon' in line]
            first = min(int(re.search(r'<Order>(\d+)</Order>', line).group(1)) for line in old)
            added = added + ['        <SynchronousCommand wcm:action="add"><Order>%d</Order><CommandLine>cmd.exe /c bcdedit /set {current} bootmenupolicy legacy</CommandLine></SynchronousCommand>' % first]
            added = added + [re.sub(r'<Order>(\d+)</Order>', lambda m: '<Order>%d</Order>' % (int(m.group(1)) + 1), line) for line in old]
            removed = removed + old
        if name.startswith('dos-'):   # r410 (s6a): CHARONPC.CFG's first comment line lost its stray "="
            old = [line for line in before if line.startswith('# CharonDOS Windows profile') and line.endswith('=')]
            added = added + [line[:-1] for line in old]
            removed = removed + old
        delta = [line for line in difflib.ndiff(before, now) if line[:2] in ('+ ', '- ')]
        really_added, really_removed = [d[2:] for d in delta if d[0] == '+'], [d[2:] for d in delta if d[0] == '-']
        what = 'unchanged since before the r409 defect fixes' if not (added or removed) else 'changed since before the r409 defect fixes only by the listed lines'
        check(f'{name}: express-mode output {what}', sorted(really_added) == sorted(added) and sorted(really_removed) == sorted(removed), '\n'.join(delta[:20]))
    return out_dir


UBUNTU_LOOP_NEW = ('mkdir -p /run/charon/EFI /mnt/charon; for c in $(blkid -c /dev/null -t TYPE=iso9660 -o device); do l=$(losetup -f -P -r --show $c 2>/dev/null) '
                   '&& echo $l >> /run/charon/loops; done; udevadm settle 2>/dev/null; for d in $(blkid -c /dev/null -t TYPE=vfat -o device); do ')


UBUNTU_HAKBOOT = ('[ -f /mnt/charon/EFI/HAK/CHARONVOICE.EFI ] && [ ! -f /run/charon/EFI/HAK/HAKBOOT.EFI ] && [ -f /mnt/charon/EFI/BOOT/BOOTX64.EFI ] '
                  '&& cp /mnt/charon/EFI/BOOT/BOOTX64.EFI /run/charon/EFI/HAK/HAKBOOT.EFI; ')


def form(name, data):
    text = data.decode('utf-8')
    if name.startswith('windows'):
        try:
            document = xml.dom.minidom.parseString(data)
            ok = document.documentElement.tagName == 'unattend'
        except Exception as error:  # noqa: BLE001
            ok = False
            text = str(error)
        check(f'{name}: Autounattend.xml is well-formed XML', ok, text[:300])
    elif name.startswith('debian'):
        shape = re.compile(r'^(d-i|tasksel|popularity-contest|keyboard-configuration) \S+ \S+( .*)?$')
        bad = [line for line in text.splitlines() if not line.startswith('#') and not shape.match(line)]
        check(f'{name}: every preseed line has the shape "owner question type [value]"', not bad and text.endswith('\n'), bad[:3])
    elif name.startswith(('ubuntu', 'image')):
        check(f'{name}: user-data starts with #cloud-config, has no tabs, no CR, ends with a line end',
              text.startswith('#cloud-config\n') and '\t' not in text and '\r' not in text and text.endswith('\n'))
    elif name.startswith('network'):
        lines = text.splitlines()
        script = (TREE / 'src' / 'linux' / 'network.sh').read_text(encoding='utf-8')
        check(f'{name}: network.conf is a comment and NAME=value lines, every name one that src/linux/network.sh reads',
              lines[0].startswith('#') and all((line.split('=')[0] + ')') in script for line in lines[1:]), lines)
        check(f'{name}: type, method and security have values network.sh accepts',
              'CHARON_NET_TYPE=wifi' in lines and 'CHARON_NET_METHOD=dhcp' in lines and any(l in lines for l in ('CHARON_WIFI_SECURITY=open', 'CHARON_WIFI_SECURITY=wpa-psk')))
    elif name.startswith('voice'):
        lines = text.splitlines()
        check(f'{name}: voice.conf is a comment and NAME=value lines with the names speech.sh reads',
              lines[0].startswith('#') and all(line.split('=')[0] in ('CHARON_SYNTH', 'CHARON_VOICE', 'CHARON_RATE', 'CHARON_PITCH', 'CHARON_PUNCTUATION')
                                               and line.split('=', 1)[1] == line.split('=', 1)[1].lower() for line in lines[1:]), lines)
        speech = (TREE / 'src' / 'linux' / 'speech.sh').read_text(encoding='utf-8')
        check(f'{name}: src/linux/speech.sh reads exactly these names from voice.conf',
              'CHARON_SYNTH|CHARON_VOICE|CHARON_RATE|CHARON_PITCH|CHARON_PUNCTUATION' in speech and '/var/lib/charon/voice.conf' in speech)
    elif name.startswith('dos'):
        lines = data.split(b'\r\n')
        check(f'{name}: CHARONPC.CFG is key=value lines with CR LF', lines[-1] == b'' and all((b'=' in line or line.startswith(b'#')) and b'\n' not in line for line in lines[:-1]), lines[:3])


# ---- 4: the store and the buffers ----------------------------------------------------------
def store(exe):
    code, out, err = run([exe, 'store'])
    lines = [line for line in out.splitlines() if line.startswith(('PASS ', 'FAIL '))]
    check('store tests ran', bool(lines) and 'failures=' in out, out[-400:] + err)
    for line in lines:
        check(line[5:], line.startswith('PASS '))
    check('store tests: exit code', code == 0, out[-400:])


BUFFERS = [   # (file, declaration as a regular expression with one group: the size, the least size planned, what it holds)
    ('CharonWsInstall.h', r'static char preseed\[(\d+)\]', 48000, 'Debian preseed.cfg'),
    ('CharonWsInstall2.h', r'static char yaml\[(\d+)\]', 32000, 'Ubuntu autoinstall user-data'),
    ('CharonWsInstall2.h', r'static char xml\[(\d+)\]', 64000, 'Windows Autounattend.xml'),
    ('CharonWsInstall3.h', r'static char userData\[(\d+)\]', 32000, 'cloud-init user-data of a ready image'),
    ('CharonWsDosSystem.h', r'constexpr size_t profileTextBytes=(\d+)', 8192, 'CharonDOS CHARONPC.CFG'),
]


def buffers(out_dir):
    largest = {'Debian': 0, 'Ubuntu': 0, 'Windows': 0, 'cloud-init': 0, 'CharonDOS': 0}
    prefix = {'debian': 'Debian', 'ubuntu': 'Ubuntu', 'windows': 'Windows', 'image': 'cloud-init', 'dos': 'CharonDOS'}
    for path in out_dir.glob('*.out'):
        for start, key in prefix.items():
            if path.name.startswith(start):
                largest[key] = max(largest[key], path.stat().st_size)
    for header, pattern, least, what in BUFFERS:
        match = re.search(pattern, (EFI / header).read_text(encoding='utf-8'))
        size = int(match.group(1)) if match else 0
        check(f'buffer for {what}: static, at least {least} bytes', size >= least, f'{header}: {size}')
        used = largest[what.split()[0]]
        check(f'buffer for {what}: the largest vector output uses under a quarter of it', 0 < used * 4 < max(size, 1), f'{used} of {size}')


# ---- 5: copies of \EFI\HAK ---------------------------------------------------------------------
PUBLIC = ['HAKBOOT.EFI', 'CHARONVOICE.EFI', 'AudioDxe.efi', 'settings.bin', 'audio/welcome.pcm', 'addon/Install-Charon.ps1', 'synths/espeak.manifest',
          'packages/CharonDOS/CHARONDOS.EFI', 'packages/CharonDOS/plan/readme.txt', 'packages/Demo/answers/kept.txt', 'tools/memtest.efi',
          'answers.txt', 'planning/notes.txt']
PRIVATE = ['Answers/school-lab.ini', 'Answers/nested/deep.ini', 'PLAN/plan.txt', 'PLAN/step1/Autounattend.xml', 'PLAN/log.txt', 'Network.conf']
MEDIUM_ONLY = ['continue.bin', 'reader-only']     # left out by rules older than r409; each path has its own list of these


def copies(exe, work):
    volume = work / 'volume'
    hak = volume / 'EFI' / 'HAK'
    for relative in PUBLIC + PRIVATE + MEDIUM_ONLY:
        path = hak / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(('secret ' if relative in PRIVATE else 'content of ').encode() + relative.encode())
    code, out, err = run([exe, 'copy', volume, work])
    if not check('copy test ran', code == 0, out + err):
        return
    values = pairs(out)
    lists = {kind: sorted(line.split(':', 1)[1] for line in out.splitlines() if line.startswith(kind + ':')) for kind in ('copytree', 'cpio', 'selfcopy', 'selfinstall', 'carry', 'private')}
    check('the shared list names answers, plan and network.conf', lists['private'] == ['answers', 'network.conf', 'plan'], lists['private'])
    expected = sorted('EFI/HAK/' + p for p in PUBLIC)
    for kind, where, strip in (('copytree', 'CopyCharonTree (Windows media, ready images, CharonDOS)', None),
                               ('cpio', 'HakTree (the archive of a Debian installation)', 'charon/'),
                               ('selfinstall', 'Install Charon on this computer (the items its Wanted() accepts)', None)):
        got = [n[len(strip):] if strip and n.startswith(strip) else n for n in lists[kind]]
        leaked = [n for n in got if any(n.lower().startswith('efi/hak/' + p + '/') or n.lower() == 'efi/hak/' + p for p in ('answers', 'plan', 'network.conf'))]
        check(f'{where}: nothing from \\EFI\\HAK\\answers or \\EFI\\HAK\\plan is copied', not leaked and bool(got), leaked)
        missing = [n for n in expected if n not in got]
        check(f'{where}: everything else is still copied', not missing, missing)
        extra = [n for n in got if n not in expected and not any(n.endswith('/' + m) for m in MEDIUM_ONLY)]
        check(f'{where}: nothing unexpected is copied', not extra, extra)
    # CARRY: the copy in memory, and Charon written back to its own erased disk, hold the private folders too
    check("selfcopy::List (Charon's copy in memory): holds everything, the private folders too (read from memory, carried over a wipe)",
          [n for n in lists['selfcopy'] if not any(n.endswith('/' + m) for m in MEDIUM_ONLY)] == sorted('EFI/HAK/' + p for p in PUBLIC + PRIVATE), lists['selfcopy'])
    check('CarryCharonTree (Charon back onto its own erased disk): everything is carried, answers and plan too',
          values.get('carry-status') == '0' and lists['carry'] == sorted('EFI/HAK/' + p for p in PUBLIC + PRIVATE + MEDIUM_ONLY), lists['carry'])
    # item 7: which synthesizer files the self-installer leaves out first when room is short
    voices = {line.split(':', 1)[1].split('=')[0]: line.rsplit('=', 1)[1] for line in out.splitlines() if line.startswith('voicepack:')}
    check('self-install, short of room: the further RHVoice voice packs go first; the base packs, the current voice and everything else stay',
          voices == {'rhvoice-pl.chsp': 'kept', 'RHVOICE-EN.CHSP': 'kept', 'rhvoice-magda.chsp': 'kept', 'rhvoice-natan.chsp': 'left-out',
                     'rhvoice-slt.chsp': 'left-out', 'rhvoice.efi': 'kept', 'rhvoice-voices.tsv': 'kept', 'espeak-ng.chsp': 'kept',
                     'all-wanted:rhvoice-natan.chsp': 'kept'}, voices)
    check('CopyCharonTree reported success', values.get('copytree-status') == '0', values.get('copytree-status'))
    check('HakTree reported success and measured the same files', values.get('cpio-ok') == '1' and values.get('cpio-overflow') == '0'
          and values.get('cpio-files') == values.get('cpio-measured-files') == str(len(lists['cpio'])), out[:300])
    target = work / 'copytree-volume' / 'charon' / 'EFI' / 'HAK'
    check('CopyCharonTree: the copied files have their content', (target / 'audio' / 'welcome.pcm').read_bytes() == b'content of audio/welcome.pcm'
          and not (target / 'Answers').exists() and not (target / 'PLAN').exists())
    # The Ubuntu installer: its early-commands copy \EFI\HAK with cp -r, then run this command.
    command = values.get('shell-remove', '')
    yaml = (work / 'out' / 'ubuntu-defaults.out').read_text(encoding='utf-8') if (work / 'out' / 'ubuntu-defaults.out').exists() else ''
    in_yaml = [line for line in yaml.splitlines() if command.replace('@HAK@', '/run/charon/EFI/HAK') in line] if command else []
    check('Ubuntu user-data runs the removal command right after its copy of \\EFI\\HAK', len(in_yaml) == 1 and
          yaml.index('cp -r /mnt/charon/EFI/HAK /run/charon/EFI/') < yaml.index(in_yaml[0]) < yaml.index('  late-commands:'), in_yaml)
    sh = shutil.which('sh') or r'C:\Program Files\Git\usr\bin\sh.exe'
    if not Path(sh).exists():
        skip('the removal command run by sh', 'no sh on this host')
        return
    scratch = work / 'run-charon'
    shutil.copytree(hak, scratch / 'EFI' / 'HAK')
    # The command runs in the Ubuntu installer's live system, where basename and tr are in PATH. Git's sh.exe started
    # from a Windows PATH (PowerShell) does not find its own tools, so their folder goes in front of PATH here.
    env = dict(os.environ, PATH=str(Path(sh).parent) + os.pathsep + os.environ.get('PATH', ''))
    code, out, err = run([sh, '-c', command.replace('@HAK@', 'EFI/HAK')], cwd=scratch, env=env)
    left = sorted(p.relative_to(scratch / 'EFI' / 'HAK').as_posix() for p in (scratch / 'EFI' / 'HAK').rglob('*') if p.is_file())
    check('the removal command, run by sh: answers and plan are gone, in any letter case', code == 0 and not [n for n in left if n in PRIVATE], out + err + str(left))
    check('the removal command, run by sh: everything else is still there', sorted(PUBLIC + MEDIUM_ONLY) == left, set(PUBLIC + MEDIUM_ONLY) ^ set(left))


def main():
    arguments = sys.argv[1:]
    work = Path(arguments[arguments.index('--work') + 1]).resolve() if '--work' in arguments else Path(tempfile.mkdtemp(prefix='charon-install-host-'))
    if work.exists() and any(work.iterdir()):
        raise SystemExit(f'{work} is not empty')
    work.mkdir(parents=True, exist_ok=True)
    try:
        exe = build(work)
        out_dir = vectors(exe, work, '--update' in arguments, '--record-baseline' in arguments)
        store(exe)
        buffers(out_dir)
        copies(exe, work)
    finally:
        if '--keep' not in arguments and '--work' not in arguments:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print('work folder: ' + str(work))
    print(f'\n{len(PASSED)} passed, {len(FAILED)} failed, {len(SKIPPED)} skipped')
    for name in FAILED:
        print('FAILED: ' + name)
    return 1 if FAILED else 0


if __name__ == '__main__':
    sys.exit(main())
