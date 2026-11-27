#!/usr/bin/env python3
"""Host tests of the Charon OS companion programs (tools/charon-os).

Nothing here touches the firmware or the EFI system partition of the computer the tests
run on: the programs work on scratch folders (--esp / -EspPath) and on a folder of
efivarfs-style files that stands in for firmware variables (--firmware-dir / -FirmwareDir).

What is checked
  1. Syntax: every .ps1 with the Windows PowerShell 5.1 parser, every Python program with
     py_compile, the shell scripts with sh -n; the release form of the Windows files
     (UTF-8 with BOM, CRLF); the generated settings table.
  2. The settings record, byte for byte against Charon's own code. CharonOsSettingsHost.cpp
     is compiled with the tree's Clang from src/efi/CharSettings.h (unchanged) and the
     functions SettingsSlot .. SaveCharSettings cut out of src/efi/BootMenu.h, over a file
     protocol that works on a folder. Records written by each program are loaded by
     Charon's LoadCharSettings and compared with Charon's EncodeSettings; records written
     by Charon's SaveCharSettings are read by each program; both write in turns.
  3. Older record formats, refusals (newer format, invalid record), the repair.
     Formats D (r410 V13) and E (r410 V14): the new bits survive a change of an old setting
     byte for byte, the masks are the ones of DecodeSettings, a boot manager image carrying
     CHARS00E makes the programs write format E, format F is refused.
  4. Synthesizer keys against synths::KeyOf, the Continue target against ParseContinueTarget.
  5. The boot manager programs on a firmware stand-in: order, Charon first / second /
     removed, BootNext, firmware setup, reader add-on.
  6. Transcripts of the text interfaces (written to the work folder).

Usage: python run-tests.py [--work DIR] [--only python|powershell] [--quick] [--keep]
Environment: CHARON_LLVM (folder with clang++; default work/llvm-23.1.5/bin next to the tree).
Exit code 0 when every check passed.
"""
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
OS_DIR = HERE.parent
TREE = OS_DIR.parents[1]
sys.path.insert(0, str(HERE))
import charon_fields as spec  # noqa: E402

GUID = '-8be4df61-93ca-11d2-aa0d-00e098032b8c'
ESP_PART = '2,2048,204800,11111111-2222-3333-4444-555555555555'
PASSED = []
FAILED = []


def check(name, ok, detail=''):
    (PASSED if ok else FAILED).append(name)
    print(('PASS ' if ok else 'FAIL ') + name + ((' :: ' + str(detail)) if detail and not ok else ''))
    return ok


def run(command, stdin=None, env=None, timeout=300):
    merged = dict(os.environ)
    merged['CHARON_TEST_ESP_PART'] = ESP_PART
    merged['PYTHONDONTWRITEBYTECODE'] = '1'
    merged['PYTHONIOENCODING'] = 'utf-8'
    if env:
        merged.update(env)
    result = subprocess.run([str(c) for c in command], input=stdin, capture_output=True, env=merged, timeout=timeout)
    out = result.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')
    err = result.stderr.decode('utf-8', 'replace').replace('\r\n', '\n')
    return result.returncode, out, err


def pairs(text):
    values = {}
    for line in text.splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            values[key.strip()] = value.strip()
    return values


# ---- Charon's own code on the host ------------------------------------------------------
def find_llvm():
    candidates = [os.environ.get('CHARON_LLVM'), TREE.parent / 'llvm-23.1.5' / 'bin', TREE.parents[1] / 'work' / 'llvm-23.1.5' / 'bin']
    for candidate in candidates:
        if candidate and (Path(candidate) / 'clang++.exe').exists():
            return Path(candidate) / 'clang++.exe'
        if candidate and (Path(candidate) / 'clang++').exists():
            return Path(candidate) / 'clang++'
    return shutil.which('clang++')


def build_host(work):
    build = work / 'host'
    build.mkdir()
    efi = TREE / 'src' / 'efi'
    shutil.copytree(efi / 'haiku', build / 'include' / 'efi')          # the tree's EFI headers as <efi/...>
    menu = (efi / 'BootMenu.h').read_text(encoding='utf-8')
    start = menu.index('inline const char16_t* SettingsSlot(unsigned slot)')
    end = menu.index('inline void InstallationLocale(')
    cut = menu[start:end]
    # r408: the copy of the record to the start medium (Charon running from its RAM volume) needs
    # the running firmware; the host has one folder, so that function and its one call are left out.
    mirror = re.search(r'// r408: once Charon runs from the RAM copy.*?\ninline void MirrorSettingsToMedium\(.*?\n\}\n', cut, re.S)
    call = re.search(r'\n[ \t]*if\(selfram::Active\(\)\)MirrorSettingsToMedium\([^\n]*', cut)
    if mirror and call:
        cut = cut.replace(mirror.group(0), '').replace(call.group(0), '')
    (build / 'charon-menu-settings.inc').write_text(cut, encoding='utf-8')
    boot = (efi / 'HAKBootEFI.cpp').read_text(encoding='utf-8')
    write = re.search(r'static bool Write\(efi_file_protocol\* file,const void\* data,size_t length\) \{.*?\n\}', boot, re.S).group(0)
    (build / 'charon-write.inc').write_text(write + '\n', encoding='utf-8')
    synth = (efi / 'CharonSynthHost.h').read_text(encoding='utf-8')
    keyof = re.search(r'inline uint8_t KeyOf\(const char16_t\* name\) \{.*?\n\}', synth, re.S).group(0)
    (build / 'charon-keyof.inc').write_text(keyof + '\n', encoding='utf-8')
    exe = build / ('charon-os-host.exe' if os.name == 'nt' else 'charon-os-host')
    clang = find_llvm()
    if not clang:
        raise SystemExit('clang++ not found (set CHARON_LLVM)')
    command = [clang, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-parameter',
               '-I', build / 'include', '-I', build, '-I', efi, HERE / 'CharonOsSettingsHost.cpp', '-o', exe]
    code, out, err = run(command)
    if code:
        print(out + err)
        raise SystemExit('the host harness did not compile')
    return exe


class Host:
    def __init__(self, exe):
        self.exe = exe

    def call(self, *arguments):
        code, out, err = run([self.exe] + list(arguments))
        return code, pairs(out), out + err


# ---- the programs under test ---------------------------------------------------------------
class PythonTools:
    name = 'python'

    def __init__(self):
        self.settings_file = OS_DIR / 'linux' / 'charon-settings'
        self.boot_file = OS_DIR / 'linux' / 'charon-bootmanager'

    def available(self):
        return self.settings_file.exists() and self.boot_file.exists()

    def settings(self, esp, *arguments, firmware=None, stdin=None):
        mode = ['--firmware-dir', firmware] if firmware else ['--no-firmware']
        return run([sys.executable, self.settings_file, '--esp', esp, '--lang', 'en'] + mode + list(arguments), stdin=stdin)

    def boot(self, esp, firmware, *arguments, stdin=None):
        mode = ['--firmware-dir', firmware] if firmware else ['--no-firmware']
        return run([sys.executable, self.boot_file, '--esp', esp, '--lang', 'en'] + mode + list(arguments), stdin=stdin)

    def set_arguments(self, values):
        out = []
        for key, value in values.items():
            out += ['--set', '%s=%d' % (key, value)]
        return out

    option = {'show': '--show', 'defaults': '--defaults', 'repair': '--repair', 'format': '--format', 'synths': '--synths',
              'readeronly': '--reader-only', 'addon': '--addon', 'yes': '--yes', 'list': '--list', 'move': '--move',
              'charon': '--charon', 'continue': '--continue', 'bootnext': '--bootnext-charon', 'setup': '--firmware-setup',
              'restart': '--restart'}


class PowerShellTools:
    name = 'powershell'

    def __init__(self):
        self.settings_file = OS_DIR / 'windows' / 'Charon-Settings.ps1'
        self.boot_file = OS_DIR / 'windows' / 'Charon-BootManager.ps1'
        system = Path(os.environ.get('SystemRoot', r'C:\Windows'))
        self.exe = system / 'System32' / 'WindowsPowerShell' / 'v1.0' / 'powershell.exe'

    def available(self):
        return self.exe.exists() and self.settings_file.exists() and self.boot_file.exists()

    def _run(self, script, esp, firmware, arguments, stdin):
        mode = ['-FirmwareDir', firmware] if firmware else ['-NoFirmware']
        return run([self.exe, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, '-EspPath', esp, '-Language', 'en'] + mode + list(arguments), stdin=stdin)

    def settings(self, esp, *arguments, firmware=None, stdin=None):
        return self._run(self.settings_file, esp, firmware, arguments, stdin)

    def boot(self, esp, firmware, *arguments, stdin=None):
        return self._run(self.boot_file, esp, firmware, arguments, stdin)

    def set_arguments(self, values):
        return ['-Set', ','.join('%s=%d' % item for item in values.items())] if values else []

    option = {'show': '-Show', 'defaults': '-Defaults', 'repair': '-Repair', 'format': '-Format', 'synths': '-Synths',
              'readeronly': '-ReaderOnly', 'addon': '-Addon', 'yes': '-Yes', 'list': '-List', 'move': '-Move',
              'charon': '-Charon', 'continue': '-Continue', 'bootnext': '-BootNextCharon', 'setup': '-FirmwareSetup',
              'restart': '-Restart'}


# ---- fixtures ----------------------------------------------------------------------------------
def new_esp(work, name):
    esp = work / name
    (esp / 'EFI' / 'HAK').mkdir(parents=True)
    return esp


def slot(esp, letter):
    return esp / 'EFI' / 'HAK' / ('charon-settings-%s.bin' % letter)


def stored(values):
    """What a record of the newest format holds after the programs wrote `values`: all of it."""
    return dict(values)


def for_format(values, version):
    """The members a record of format `version` stores, cut to the bits that format holds."""
    age = spec.VERSIONS.index
    result = {name: value for name, value in values.items() if age(spec.MEMBER_SINCE[name]) <= age(version)}
    if version in 'AB':
        result['formatFlags'] &= 63
        result['readerExtra'] &= 31
    if version == 'C':
        result['readerExtra'] &= 0x7f              # a new format C record never gets bit 7
    if version == 'D':
        result['profile'] = min(result['profile'], 2)   # the custom profile needs format E
    return result


def sealed(record, version=None):
    """The record with another format character (optional) and a fresh CRC."""
    record = bytearray(record)
    if version:
        record[7] = ord(version)
    at = len(record) - 4
    record[at:] = struct.pack('<I', zlib.crc32(bytes(record[:at])) & 0xffffffff)
    return bytes(record)


def same(decoded, values):
    return all(str(decoded.get(name)) == str(value) for name, value in values.items())


def random_settings(rng):
    return dict(language=rng.randint(0, 2), wrap=rng.randint(0, 1), sounds=rng.randint(0, 1), explicitMessages=rng.randint(0, 1),
                startMelody=rng.randint(0, 4), backgroundMelody=rng.randint(0, 4), rate=rng.randint(5, 20),
                speechVolume=rng.randint(1, 10), effectsVolume=rng.randint(0, 10), verbosity=rng.randint(0, 2),
                progress=rng.randint(0, 3), rememberLast=rng.randint(0, 1), autoSeconds=rng.choice(spec.AUTO_DELAYS),
                welcome=rng.randint(0, 7), pitch=rng.randint(5, 20), readerRate=rng.randint(5, 20), readerPitch=rng.randint(5, 20),
                readerVolume=rng.randint(1, 10), detail=rng.randint(0, 7), timers=rng.randint(0, 3), messageRate=rng.randint(5, 20),
                messagePitch=rng.randint(5, 20), punctuation=rng.randint(0, 3), capitals=rng.randint(0, 7),
                screenLanguage=rng.randint(0, 3), readerFlags=rng.randint(0, 63), formatFlags=rng.randint(0, 255),
                readerExtra=rng.randint(0, 2) | (rng.randint(0, 7) << 2) | (rng.randint(0, 2) << 5) | (rng.randint(0, 1) << 7),
                synthKey=rng.randint(0, 255), synthVoice=rng.randint(0, 15), formatGroups=rng.randint(0, 7),
                profile=rng.randint(0, 3), profileItems=rng.randint(0, 4095), readerBar=rng.randint(0, 1),
                absoluteColours=rng.randint(0, 1))


def members_of(values):
    return {name: int(values[name]) for name in spec.MEMBERS}


def host_arguments(values):
    return ['%s=%d' % (name, values[name]) for name in spec.MEMBERS if name in values]


def load_option(description, file_path, attributes=1, guid_byte=0x11):
    hd = bytes([4, 1, 42, 0]) + struct.pack('<IQQ', 2, 2048, 204800) + bytes([guid_byte] * 16) + bytes([2, 2])
    name = (file_path + '\0').encode('utf-16-le')
    node = bytes([4, 4]) + struct.pack('<H', 4 + len(name)) + name
    path = hd + node + bytes([0x7f, 0xff, 4, 0])
    return struct.pack('<IH', attributes, len(path)) + (description + '\0').encode('utf-16-le') + path


def write_variable(folder, name, data):
    (folder / (name + GUID)).write_bytes(struct.pack('<I', 7) + data)


def read_variable(folder, name):
    path = folder / (name + GUID)
    return path.read_bytes()[4:] if path.exists() else None


def new_firmware(work, name, order=(0x0004, 0x0001, 0x0007, 0x0002), secure=0):
    folder = work / name
    folder.mkdir()
    write_variable(folder, 'Boot0001', load_option('Charon', '\\EFI\\HAK\\HAKBOOT.EFI'))
    write_variable(folder, 'Boot0002', load_option('debian', '\\EFI\\debian\\shimx64.efi'))
    write_variable(folder, 'Boot0004', load_option('Windows Boot Manager', '\\EFI\\Microsoft\\Boot\\bootmgfw.efi'))
    write_variable(folder, 'Boot0007', load_option('UEFI: USB stick', '\\EFI\\BOOT\\BOOTX64.EFI', guid_byte=0x22))
    write_variable(folder, 'BootOrder', b''.join(struct.pack('<H', n) for n in order))
    write_variable(folder, 'BootCurrent', struct.pack('<H', 4))
    write_variable(folder, 'SecureBoot', bytes([secure]))
    write_variable(folder, 'OsIndicationsSupported', struct.pack('<Q', 1))
    return folder


def order_of(folder, name='BootOrder'):
    data = read_variable(folder, name) or b''
    return [struct.unpack_from('<H', data, i)[0] for i in range(0, len(data), 2)]


# ---- tests ---------------------------------------------------------------------------------------
def test_syntax(work):
    ps = PowerShellTools()
    if ps.exe.exists():
        script = work / 'parse-ps1.ps1'
        script.write_text(
            'param([string]$Folder)\n$bad = 0\n'
            'foreach ($f in Get-ChildItem $Folder -Filter *.ps1) {\n'
            '    $tokens = $null; $errors = $null\n'
            '    [void][System.Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$tokens, [ref]$errors)\n'
            '    if ($errors.Count) { $bad++; foreach ($e in $errors) { Write-Output ("{0}:{1}: {2}" -f $f.Name, $e.Extent.StartLineNumber, $e.Message) } }\n'
            '    else { Write-Output ("{0}: parsed by PowerShell {1}, no errors" -f $f.Name, $PSVersionTable.PSVersion) }\n'
            '}\nexit $bad\n', encoding='utf-8')
        code, out, err = run([ps.exe, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, '-Folder', OS_DIR / 'windows'])
        print(out.strip())
        check('syntax: every .ps1 parses with Windows PowerShell 5.1', code == 0, out + err)
    code, out, err = run([sys.executable, HERE / 'normalize-windows.py', '--check'])
    check('form: Windows files are UTF-8 with BOM (.ps1) and CRLF', code == 0, out)
    code, out, err = run([sys.executable, HERE / 'gen-windows-fields.py', '--check'])
    check('form: CharonOs.Fields.ps1 matches charon_fields.py', code == 0)
    import py_compile
    for program in ('charon-settings', 'charon-bootmanager'):
        path = OS_DIR / 'linux' / program
        if not path.exists():
            check('syntax: linux/%s exists' % program, False)
            continue
        try:
            py_compile.compile(str(path), cfile=str(work / (program + '.pyc')), doraise=True)
            ok = True
            detail = ''
        except py_compile.PyCompileError as error:
            ok = False
            detail = str(error)
        check('syntax: linux/%s compiles with Python %d.%d' % (program, sys.version_info[0], sys.version_info[1]), ok, detail)
        raw = path.read_bytes()
        check('form: linux/%s has LF line ends, no BOM, a python3 shebang' % program,
              b'\r' not in raw and raw.startswith(b'#!/usr/bin/env python3'))
    # The Linux program carries its own copy of the settings table: it must be the same table.
    path = OS_DIR / 'linux' / 'charon-settings'
    if path.exists():
        import importlib.machinery
        import importlib.util
        loader = importlib.machinery.SourceFileLoader('charon_settings_linux', str(path))
        module = importlib.util.module_from_spec(importlib.util.spec_from_loader('charon_settings_linux', loader))
        loader.exec_module(module)
        same = all(getattr(module, name, None) == getattr(spec, name) for name in
                   ('VERSIONS', 'NEWEST', 'MEMBERS', 'DEFAULTS', 'DEFAULTS_AB', 'DEFAULTS_C', 'MEMBER_SINCE', 'AUTO_DELAYS',
                    'PAGES', 'ROWS', 'LINGER_NOTE_EN', 'LINGER_NOTE_PL'))
        check('form: the settings table in linux/charon-settings is the one in charon_fields.py (%d rows)' % len(spec.ROWS), same)
    shell = shutil.which('sh')
    for script in (OS_DIR / 'linux' / 'install-charon-tools.sh',):
        if shell and script.exists():
            code, out, err = run([shell, '-n', script])
            check('syntax: sh -n linux/%s' % script.name, code == 0 and b'\r' not in script.read_bytes(), err)


def test_record(tools, host, work, vectors):
    tag = tools.name
    show = tools.option['show']
    # A. each program writes, Charon's code reads and encodes the same bytes.
    equal = True
    detail = ''
    for index, values in enumerate(vectors):
        esp = new_esp(work, '%s-write-%d' % (tag, index))
        code, out, err = tools.settings(esp, *tools.set_arguments(values))
        expected = stored(values)
        loaded_code, loaded, text = host.call('load', esp)
        ok = code == 0 and loaded_code == 0 and loaded.get('status') == 'ok' and loaded.get('slota') == 'ok' \
            and loaded.get('slotb') == 'notfound' and loaded.get('sequence') == '1' and loaded.get('format') == spec.NEWEST \
            and members_of(loaded) == expected and members_of(pairs(out)) == expected
        if ok:
            _, encoded, _ = host.call('encode', 'sequence=1', *host_arguments(expected))
            ok = slot(esp, 'a').read_bytes().hex() == encoded.get('record')
        if not ok:
            equal = False
            detail = 'vector %d: %s %s %s' % (index, out, err, text)
            break
    check('%s: %d records written by the program are byte-identical to Charon\'s EncodeSettings and load with LoadCharSettings' % (tag, len(vectors)), equal, detail)

    # B. Charon's SaveCharSettings writes, the program reads.
    equal = True
    detail = ''
    for index, values in enumerate(vectors):
        esp = new_esp(work, '%s-read-%d' % (tag, index))
        code, saved, text = host.call('save', esp, *host_arguments(stored(values)))
        shown_code, out, err = tools.settings(esp, show)
        shown = pairs(out)
        if not (code == 0 and shown_code == 0 and members_of(shown) == members_of(saved) and shown.get('sequence') == '1' and shown.get('format') == spec.NEWEST):
            equal = False
            detail = 'vector %d: %s | %s %s' % (index, text, out, err)
            break
    check('%s: %d records written by Charon\'s SaveCharSettings are read identically by the program' % (tag, len(vectors)), equal, detail)

    # C. both write in turns: sequence, slot parity, the other slot untouched.
    esp = new_esp(work, tag + '-turns')
    first, second, third, fourth = vectors[1], vectors[2], vectors[3], vectors[4]
    tools.settings(esp, *tools.set_arguments(first))
    a_before = slot(esp, 'a').read_bytes()
    host.call('save', esp, *host_arguments(stored(second)))
    ok = slot(esp, 'a').read_bytes() == a_before and slot(esp, 'b').exists()
    b_before = slot(esp, 'b').read_bytes()
    code, out, err = tools.settings(esp, *tools.set_arguments(third))
    ok = ok and code == 0 and slot(esp, 'b').read_bytes() == b_before and pairs(out).get('sequence') == '3'
    code, saved, text = host.call('save', esp, *host_arguments(stored(fourth)))
    ok = ok and code == 0 and saved.get('sequence') == '4'
    _, out, _ = tools.settings(esp, show)
    ok = ok and members_of(pairs(out)) == stored(fourth) and pairs(out).get('sequence') == '4'
    _, loaded, _ = host.call('load', esp)
    ok = ok and loaded.get('slota') == 'ok' and loaded.get('slotb') == 'ok'
    check('%s: the program and Charon write in turns (sequences 1-4, slot a odd, slot b even, the other slot untouched)' % tag, ok, out)

    # D. one changed setting keeps every other byte of the record.
    esp = new_esp(work, tag + '-keep')
    host.call('save', esp, *host_arguments(stored(vectors[5])))
    before = slot(esp, 'a').read_bytes()
    new_rate = 5 if vectors[5]['rate'] != 5 else 6
    tools.settings(esp, *tools.set_arguments({'rate': new_rate}))
    after = slot(esp, 'b').read_bytes()
    differing = [i for i in range(44) if before[i] != after[i]]
    check('%s: changing one setting changes only its byte and the sequence (bytes 8 and 22)' % tag, differing == [8, 22], differing)

    # E. the newest valid slot wins; a damaged newest slot falls back like ChooseSettings.
    damaged = bytearray(slot(esp, 'b').read_bytes())
    damaged[20] ^= 0xff
    slot(esp, 'b').write_bytes(bytes(damaged))
    code, out, _ = tools.settings(esp, show)
    _, loaded, _ = host.call('load', esp)
    check('%s: a damaged newest slot falls back to the other one, as LoadCharSettings does' % tag,
          code == 0 and members_of(pairs(out)) == members_of(loaded) and pairs(out).get('sequence') == loaded.get('sequence') == '1', out)
    slot(esp, 'a').write_bytes(b'CHARS00C' + bytes(40))
    code, out, err = tools.settings(esp, show)
    host_code, _, _ = host.call('load', esp)
    check('%s: two unreadable slots are reported as invalid (exit 4), as LoadCharSettings does' % tag, code == 4 and host_code == 4 and 'record=invalid' in out, out + err)
    code, out, err = tools.settings(esp, *tools.set_arguments({'rate': 9}))
    check('%s: nothing is written over invalid settings without a repair' % tag, code != 0 and slot(esp, 'a').read_bytes() == b'CHARS00C' + bytes(40), out + err)

    # F. a newer format on the partition: read-only.
    esp = new_esp(work, tag + '-newer')
    tools.settings(esp, *tools.set_arguments(vectors[1]))
    newer = bytearray(slot(esp, 'a').read_bytes())
    newer[7] = ord('F')
    newer[8] = 2
    newer[44:48] = struct.pack('<I', zlib.crc32(bytes(newer[:44])))
    slot(esp, 'b').write_bytes(bytes(newer))
    a_before = slot(esp, 'a').read_bytes()
    code, out, err = tools.settings(esp, *tools.set_arguments({'rate': 9}))
    host_code, _, _ = host.call('decode', bytes(newer).hex())
    check('%s: a record of a newer format (CHARS00F, which Charon\'s DecodeSettings rejects too) makes the program refuse to write (exit 6), files untouched' % tag,
          code == 6 and host_code == 4 and slot(esp, 'a').read_bytes() == a_before and slot(esp, 'b').read_bytes() == bytes(newer), out + err)
    code, out, err = tools.settings(esp, show)
    check('%s: with a newer record in the other slot the program still shows the record it can read, and writes nothing' % tag,
          code == 0 and pairs(out).get('sequence') == '1' and slot(esp, 'a').read_bytes() == a_before and slot(esp, 'b').read_bytes() == bytes(newer), out + err)
    esp = new_esp(work, tag + '-newer-binary')
    (esp / 'EFI' / 'HAK' / 'HAKBOOT.EFI').write_bytes(b'MZ' + bytes(300) + b'CHARS00F' + bytes(100))
    code, out, err = tools.settings(esp, *tools.set_arguments({'rate': 9}))
    check('%s: a boot manager that writes a newer format makes the program refuse to write (exit 6)' % tag,
          code == 6 and not slot(esp, 'a').exists(), out + err)

    # G. the format follows the installed boot manager, then the record; older formats load in Charon.
    esp = new_esp(work, tag + '-binary-b')
    (esp / 'EFI' / 'HAK' / 'HAKBOOT.EFI').write_bytes(b'MZ' + bytes(300) + b'CHARS00B' + bytes(100))
    code, out, err = tools.settings(esp, *tools.set_arguments(dict(for_format(vectors[6], 'B'), formatFlags=128 | 5)))
    check('%s: a bit format B does not store (formatFlags 128) is refused (exit 5), nothing written' % tag, code == 5 and not slot(esp, 'a').exists(), out + err)
    expected = for_format(vectors[6], 'B')
    code, out, err = tools.settings(esp, *tools.set_arguments(expected))
    raw = slot(esp, 'a').read_bytes() if slot(esp, 'a').exists() else b''
    _, loaded, text = host.call('load', esp)
    check('%s: with a boot manager that writes format B the record is CHARS00B and Charon loads it' % tag,
          code == 0 and raw[:8] == b'CHARS00B' and loaded.get('status') == 'ok' and same(loaded, expected), out + err + text)
    esp = new_esp(work, tag + '-binary-b-defaults')
    (esp / 'EFI' / 'HAK' / 'HAKBOOT.EFI').write_bytes(b'MZ' + bytes(300) + b'CHARS00B' + bytes(100))
    code, out, err = tools.settings(esp, *tools.set_arguments({'rate': 12}))
    raw = slot(esp, 'a').read_bytes() if slot(esp, 'a').exists() else b''
    _, loaded, text = host.call('load', esp)
    check('%s: a new format B record starts from that format\'s defaults (bytes 40 and 41 = 60 and 25, within its bits), and Charon loads the record' % tag,
          code == 0 and raw[:8] == b'CHARS00B' and raw[40] == 60 and raw[41] == 25 and loaded.get('status') == 'ok' and loaded.get('rate') == '12', out + err + text)
    ok = True
    detail = ''
    for version in spec.VERSIONS[:-1]:
        esp = new_esp(work, '%s-format-%s' % (tag, version))
        values = for_format(vectors[7], version)
        code, out, err = tools.settings(esp, tools.option['format'], version, *tools.set_arguments(values))
        raw = slot(esp, 'a').read_bytes() if slot(esp, 'a').exists() else b''
        size = 48 if version in '9ABCDE' else 40 if version in '78' else 32
        _, decoded, text = host.call('decode', raw.hex())
        good = code == 0 and len(raw) == size and raw[:8] == b'CHARS00' + version.encode() and decoded.get('decode') == 'ok'
        good = good and all(int(decoded[name]) == value for name, value in values.items())
        good = good and members_of(pairs(out)) == members_of(decoded)
        if not good:
            ok = False
            detail = 'format %s: %s %s %s' % (version, out, err, text)
            break
    check('%s: records in the older formats 1-9, A, B, C, D have Charon\'s sizes and decode in Charon to the values set' % tag, ok, detail)
    esp = new_esp(work, tag + '-format-refuse')
    code, out, err = tools.settings(esp, tools.option['format'], '8', *tools.set_arguments({'punctuation': 2}))
    check('%s: a setting the target format does not store is refused (exit 5), nothing written' % tag, code == 5 and not slot(esp, 'a').exists(), out + err)
    esp = new_esp(work, tag + '-range')
    code, out, err = tools.settings(esp, *tools.set_arguments({'rate': 21}))
    refused, _, _ = host.call('encode', 'sequence=1', 'rate=21')
    check('%s: a value EncodeSettings rejects (rate 21) is refused (exit 5), nothing written' % tag, code == 5 and refused == 5 and not slot(esp, 'a').exists(), out + err)

    # H. a record with a right frame and a value Charon rejects: shown as invalid, repaired.
    esp = new_esp(work, tag + '-repair')
    tools.settings(esp, *tools.set_arguments(vectors[9]))
    broken = bytearray(slot(esp, 'a').read_bytes())
    broken[22] = 3                                   # rate below 5
    slot(esp, 'a').write_bytes(sealed(broken))
    host_code, _, _ = host.call('load', esp)
    code, out, err = tools.settings(esp, show)
    ok = host_code == 4 and code == 4 and 'record=invalid' in out
    code, out, err = tools.settings(esp, tools.option['repair'])
    _, loaded, text = host.call('load', esp)
    expected = dict(vectors[9], rate=spec.DEFAULTS['rate'])
    check('%s: a record with a value out of range (rate 3) is reported invalid, as LoadCharSettings does, and repaired: the default for that value, every other member kept; Charon loads the repair' % tag,
          ok and code == 0 and loaded.get('status') == 'ok' and loaded.get('sequence') == '2' and members_of(loaded) == expected, out + err + text)

    # J. formats D (V13) and E (V14).
    test_new_formats(tools, host, work, vectors)

    # I. defaults and the reader-only marker.
    esp = new_esp(work, tag + '-defaults')
    tools.settings(esp, *tools.set_arguments(vectors[8]))
    code, out, err = tools.settings(esp, tools.option['defaults'])
    _, loaded, _ = host.call('load', esp)
    raw = slot(esp, 'b').read_bytes() if slot(esp, 'b').exists() else bytes(48)
    _, encoded, _ = host.call('encode', 'sequence=2')
    check('%s: restore defaults writes the record Charon\'s EncodeSettings gives for CharSettings{} (byte 40 = 252, byte 41 = 153)' % tag,
          code == 0 and members_of(loaded) == dict(spec.DEFAULTS) and raw.hex() == encoded.get('record') and raw[40] == 252 and raw[41] == 153, out + err)
    marker = esp / 'EFI' / 'HAK' / 'reader-only'
    tools.settings(esp, tools.option['readeronly'], 'on')
    on = marker.exists() and marker.read_bytes() == b'reader-only\r\n'
    tools.settings(esp, tools.option['readeronly'], 'off')
    check('%s: the reader-only marker is created (as the installers write it) and removed' % tag, on and not marker.exists())
    names = sorted(p.name for p in (esp / 'EFI' / 'HAK').iterdir())
    check('%s: nothing but the two settings slots was created in EFI/HAK' % tag, names == ['charon-settings-a.bin', 'charon-settings-b.bin'], names)


def test_new_formats(tools, host, work, vectors):
    """Formats D (r410 V13) and E (r410 V14) against CharSettings.h on the host."""
    tag = tools.name
    show = tools.option['show']
    hak = lambda esp: esp / 'EFI' / 'HAK'
    rich = dict(vectors[2], detail=5, timers=1, punctuation=2, capitals=5, screenLanguage=1, formatGroups=5, profile=3,
                profileItems=0xa5a, readerBar=1, absoluteColours=1, readerExtra=128 | 2 | 8 | (1 << 5), rate=11)

    # 1. a format E record: one old setting changes, every other byte stays.
    esp = new_esp(work, tag + '-e-keep')
    _, encoded, _ = host.call('encode', 'sequence=1', *host_arguments(rich))
    before = bytes.fromhex(encoded.get('record', ''))
    slot(esp, 'a').write_bytes(before)
    code, out, err = tools.settings(esp, show)
    _, decoded, _ = host.call('decode', before.hex())
    check('%s: a format E record with every new bit in use is read as DecodeSettings reads it (detail = byte 32 & 7, timers = byte 33 & 3, '
          'punctuation = byte 36 & 3, capitals = byte 37 & 7, screenLanguage = byte 38 & 3; groups, profile 3, 12 items, bar, colours)' % tag,
          code == 0 and before[:8] == b'CHARS00E' and before[32] == 5 | (0xa5a & 31) << 3 and before[33] == 1 | ((0xa5a >> 5) & 63) << 2
          and before[37] == 5 | 8 | 16 | 32 and before[36] == 2 | 5 << 2 and before[38] == 1 | 3 << 2
          and members_of(pairs(out)) == members_of(decoded) == rich and pairs(out).get('format') == 'E', out + err)
    code, out, err = tools.settings(esp, *tools.set_arguments({'rate': 17}))
    after = slot(esp, 'b').read_bytes() if slot(esp, 'b').exists() else bytes(48)
    differing = [i for i in range(44) if before[i] != after[i]]
    _, decoded, _ = host.call('decode', after.hex())
    check('%s: changing one old setting (rate) in a format E record changes bytes 8 and 22 only; the record stays CHARS00E and the new bits survive' % tag,
          code == 0 and differing == [8, 22] and slot(esp, 'a').read_bytes() == before and decoded.get('decode') == 'ok'
          and same(decoded, dict(rich, rate=17)), '%s %s %s' % (differing, out, err))
    code, out, err = tools.settings(esp, *tools.set_arguments({'detail': 2, 'timers': 2, 'punctuation': 1, 'capitals': 2, 'screenLanguage': 3}))
    third = slot(esp, 'a').read_bytes()
    differing = [i for i in range(44) if after[i] != third[i]]
    ok = code == 0 and differing == [8, 32, 33, 36, 37, 38]
    ok = ok and all(third[i] & ~m & 0xff == after[i] & ~m & 0xff for i, m in ((32, 7), (33, 3), (36, 3), (37, 7), (38, 3)))
    ok = ok and (third[32] & 7, third[33] & 3, third[36] & 3, third[37] & 7, third[38] & 3) == (2, 2, 1, 2, 3)
    _, decoded, _ = host.call('decode', third.hex())
    check('%s: changing the five old settings that share a byte with new bits (bytes 32, 33, 36, 37, 38) changes only their own bits' % tag,
          ok and same(decoded, dict(rich, rate=17, detail=2, timers=2, punctuation=1, capitals=2, screenLanguage=3)), '%s %s %s' % (differing, out, err))
    code, out, err = tools.settings(esp, *tools.set_arguments({'itemHints': 1, 'readerBar': 0, 'groupState': 1, 'profile': 1, 'linger': 0}))
    fourth = slot(esp, 'b').read_bytes()
    _, decoded, _ = host.call('decode', fourth.hex())
    check('%s: the new settings are changed by their rows (a custom profile item, reader bar, a formatting group, the profile, the loading sound) and Charon decodes them' % tag,
          code == 0 and same(decoded, dict(profileItems=0xa5a | 4, readerBar=0, absoluteColours=1, formatGroups=7, profile=1, readerExtra=rich['readerExtra'] & 0x7f,
                                           detail=2, capitals=2)), out + err)
    code, out, err = tools.settings(esp, *tools.set_arguments({'profileItems': 4096}))
    refused, _, _ = host.call('encode', 'sequence=1', 'profileItems=4096')
    check('%s: a value EncodeSettings rejects in a new member (profileItems 4096) is refused (exit 5), nothing written' % tag,
          code == 5 and refused == 5 and slot(esp, 'b').read_bytes() == fourth and slot(esp, 'a').read_bytes() == third, out + err)

    # 2. a format D record (r410 V13): groups and profile in bytes 36 and 38, nothing in bytes 32, 33, 37.
    plain = dict(rich, profile=2, profileItems=0, readerBar=0, absoluteColours=0)
    _, encoded, _ = host.call('encode', 'sequence=1', *host_arguments(plain))
    record_d = sealed(bytes.fromhex(encoded.get('record', '')), 'D')
    _, decoded_d, _ = host.call('decode', record_d.hex())
    esp = new_esp(work, tag + '-d-keep')
    slot(esp, 'a').write_bytes(record_d)
    code, out, err = tools.settings(esp, show)
    shown = pairs(out)
    code2, out2, err2 = tools.settings(esp, *tools.set_arguments({'rate': 17}))
    after = slot(esp, 'b').read_bytes() if slot(esp, 'b').exists() else bytes(48)
    differing = [i for i in range(44) if record_d[i] != after[i]]
    _, decoded, _ = host.call('decode', after.hex())
    check('%s: a format D record is read as DecodeSettings reads it (groups 5, profile 2, items 1083) and stays CHARS00D, bytes 8 and 22 changed only, when rate changes' % tag,
          code == 0 and decoded_d.get('decode') == 'ok' and shown.get('format') == 'D' and members_of(shown) == members_of(decoded_d)
          and shown.get('formatGroups') == '5' and shown.get('profile') == '2' and shown.get('profileItems') == '1083'
          and code2 == 0 and differing == [8, 22] and after[:8] == b'CHARS00D' and decoded.get('decode') == 'ok'
          and same(decoded, dict(formatGroups=5, profile=2, rate=17)), '%s %s %s %s %s' % (differing, out, err, out2, err2))
    code, out, err = tools.settings(esp, *tools.set_arguments({'profile': 3}))
    code2, out2, err2 = tools.settings(esp, *tools.set_arguments({'readerBar': 1}))
    check('%s: what format D does not store (the custom profile 3, the reader bar) is refused (exit 5), nothing written' % tag,
          code == 5 and code2 == 5 and slot(esp, 'b').read_bytes() == after and slot(esp, 'a').read_bytes() == record_d, out + err + out2 + err2)

    # 3. the boot manager image decides: CHARS00E inside HAKBOOT.EFI means format E is written.
    image = b'MZ' + bytes(300) + b'CHARS00\0' + bytes(21) + b'CHARS00E' + bytes(100)
    esp = new_esp(work, tag + '-binary-e')
    (hak(esp) / 'HAKBOOT.EFI').write_bytes(image)
    code, out, err = tools.settings(esp, *tools.set_arguments({'rate': 12}))
    raw = slot(esp, 'a').read_bytes() if slot(esp, 'a').exists() else b''
    _, encoded, _ = host.call('encode', 'sequence=1', 'rate=12')
    check('%s: a boot manager image carrying CHARS00E makes the program write format E; the new record is Charon\'s EncodeSettings of its defaults with that change' % tag,
          code == 0 and raw[:8] == b'CHARS00E' and raw.hex() == encoded.get('record'), out + err)
    esp = new_esp(work, tag + '-binary-e-upgrade')
    copy = new_esp(work, tag + '-binary-e-upgrade-host')
    for folder in (esp, copy):
        (hak(folder) / 'HAKBOOT.EFI').write_bytes(image)
        slot(folder, 'a').write_bytes(record_d)
    code, out, err = tools.settings(esp, *tools.set_arguments({'rate': 17}))
    host_code, _, text = host.call('save', copy, 'rate=17')
    raw = slot(esp, 'b').read_bytes() if slot(esp, 'b').exists() else b''
    check('%s: a format D record under a boot manager that writes format E is rewritten as CHARS00E, byte for byte what Charon\'s SaveCharSettings writes from the same record' % tag,
          code == 0 and host_code == 0 and raw[:8] == b'CHARS00E' and raw == slot(copy, 'b').read_bytes() and slot(esp, 'a').read_bytes() == record_d, out + err + text)

    # 4. format C: bit 7 of byte 41 (r408) is read and kept, never turned on by the programs.
    old = dict(rich, formatGroups=0, profile=0, profileItems=0, readerBar=0, absoluteColours=0)
    _, encoded, _ = host.call('encode', 'sequence=1', *host_arguments(old))
    record_c = sealed(bytes.fromhex(encoded.get('record', '')), 'C')
    _, decoded_c, _ = host.call('decode', record_c.hex())
    esp = new_esp(work, tag + '-c-keep')
    slot(esp, 'a').write_bytes(record_c)
    code, out, err = tools.settings(esp, show)
    shown = pairs(out)
    code2, out2, err2 = tools.settings(esp, *tools.set_arguments({'rate': 17, 'stopCountdown': 1}))
    after = slot(esp, 'b').read_bytes() if slot(esp, 'b').exists() else bytes(48)
    differing = [i for i in range(44) if record_c[i] != after[i]]
    check('%s: a format C record with byte 41 bit 7 set (Charon r408, r409) is valid, as in DecodeSettings, and keeps the bit when other settings change' % tag,
          code == 0 and decoded_c.get('decode') == 'ok' and record_c[41] & 128 and members_of(shown) == members_of(decoded_c) and shown.get('format') == 'C'
          and code2 == 0 and after[:8] == b'CHARS00C' and differing == [8, 22, 41] and after[41] == record_c[41] | 4, '%s %s %s %s %s' % (differing, out, err, out2, err2))
    esp = new_esp(work, tag + '-binary-c')
    (hak(esp) / 'HAKBOOT.EFI').write_bytes(b'MZ' + bytes(300) + b'CHARS00C' + bytes(100))
    code, out, err = tools.settings(esp, *tools.set_arguments({'rate': 12}))
    raw = slot(esp, 'a').read_bytes() if slot(esp, 'a').exists() else bytes(48)
    _, decoded, _ = host.call('decode', raw.hex())
    code2, out2, err2 = tools.settings(esp, *tools.set_arguments({'linger': 1}))
    code3, out3, err3 = tools.settings(esp, *tools.set_arguments({'readerExtra': 153}))
    check('%s: with a boot manager that writes format C a new record is CHARS00C with bytes 40, 41 = 255, 25 and no new bits; turning the loading sound on is refused (exit 5)' % tag,
          code == 0 and raw[:8] == b'CHARS00C' and (raw[40], raw[41]) == (255, 25) and raw[32] <= 7 and raw[33] <= 3 and raw[36] <= 3 and raw[37] <= 7 and raw[38] <= 3
          and decoded.get('decode') == 'ok' and code2 == 5 and code3 == 5 and slot(esp, 'a').read_bytes() == raw and not slot(esp, 'b').exists(),
          out + err + out2 + err2 + out3 + err3)

    # 5. which bits a format accepts: the programs and DecodeSettings give the same verdict.
    _, encoded, _ = host.call('encode', 'sequence=1', *host_arguments(dict(old, readerExtra=old['readerExtra'] & 0x7f)))
    base = bytes.fromhex(encoded.get('record', ''))
    cases = [('E', 32, 0xf8), ('E', 33, 0xfc), ('E', 37, 0x38), ('E', 36, 0x1c), ('E', 38, 0x0c), ('E', 41, 0x80),
             ('E', 37, 0x40), ('E', 36, 0x20), ('E', 38, 0x10), ('E', 39, 0x40),
             ('D', 36, 0x1c), ('D', 38, 0x08), ('D', 38, 0x0c), ('D', 32, 0x08), ('D', 33, 0x04), ('D', 37, 0x08),
             ('C', 41, 0x80), ('C', 36, 0x04), ('C', 38, 0x04), ('C', 32, 0x08), ('B', 41, 0x80)]
    wrong = []
    accepted = 0
    for index, (version, at, bits) in enumerate(cases):
        record = bytearray(base)
        if version == 'B':
            record[40] &= 63
            record[41] &= 31
        record[at] |= bits
        record = sealed(record, version)
        host_code, _, _ = host.call('decode', record.hex())
        esp = new_esp(work, '%s-bits-%d' % (tag, index))
        slot(esp, 'a').write_bytes(record)
        code, out, err = tools.settings(esp, show)
        accepted += 1 if host_code == 0 else 0
        if (code == 0) != (host_code == 0) or code not in (0, 4) or (code == 0 and members_of(pairs(out)) != members_of(host.call('decode', record.hex())[1])):
            wrong.append((version, at, bits, code, host_code))
    check('%s: %d records with single bit fields set in bytes 32, 33, 36-39, 41 of formats B to E are accepted or reported invalid exactly as by DecodeSettings (%d accepted)'
          % (tag, len(cases), accepted), not wrong and accepted == 9, wrong)


def test_synths(tools, host, work):
    tag = tools.name
    esp = new_esp(work, tag + '-synths')
    folder = esp / 'EFI' / 'HAK' / 'synths'
    folder.mkdir()
    (folder / 'espeak.manifest').write_text('abi = 3\nname = eSpeak NG\nmodule = espeak.tts\ndata = espeak\n'
                                            'voice = espeak-pl | eSpeak NG polski | pl\nvoice = espeak-en | eSpeak NG English | en\n', encoding='utf-8')
    (folder / 'RHVoice.MANIFEST').write_text('# comment\r\nname = RHVoice\r\nmodule = rhvoice.tts\r\nvoice = magda | RHVoice Magda | pl\r\n', encoding='utf-8')
    (folder / 'broken.manifest').write_text('name = No module\nvoice = a | b | c\n', encoding='utf-8')
    (folder / 'novoice.manifest').write_text('name = No voice\nmodule = x.tts\n', encoding='utf-8')
    code, out, err = tools.settings(esp, tools.option['synths'])
    found = {}
    for line in out.splitlines():
        match = re.match(r'key=(\d+) file=(\S+) voices=(\d+) name=(.*)', line)
        if match:
            found[match.group(2)] = (int(match.group(1)), int(match.group(3)), match.group(4))
    ok = set(found) == {'espeak.manifest', 'RHVoice.MANIFEST'}
    for name in found:
        _, key, _ = host.call('keyof', name)
        ok = ok and int(key['key']) == found[name][0]
    ok = ok and found.get('espeak.manifest', (0, 0, ''))[1:] == (2, 'eSpeak NG') and found.get('RHVoice.MANIFEST', (0, 0, ''))[1:] == (1, 'RHVoice')
    check('%s: synthesizer add-ons are listed with the keys of Charon\'s synths::KeyOf; manifests without module or voice are left out' % tag, ok, out + err)
    if ok:
        key = found['espeak.manifest'][0]
        code, out, err = tools.settings(esp, *tools.set_arguments({'synthKey': key, 'synthVoice': 1}))
        _, loaded, _ = host.call('load', esp)
        check('%s: the chosen synthesizer and voice are stored in bytes 42 and 43' % tag,
              code == 0 and loaded.get('synthKey') == str(key) and loaded.get('synthVoice') == '1' and slot(esp, 'a').read_bytes()[42:44] == bytes([key, 1]), out + err)


def test_addon(tools, work):
    tag = tools.name
    esp = new_esp(work, tag + '-addon')
    firmware = new_firmware(work, tag + '-addon-fw')
    yes = tools.option['yes']
    addon = tools.option['addon']
    code, out, err = tools.settings(esp, addon, 'status', firmware=firmware)
    off = code == 0 and 'addon=off' in out
    code, out, err = tools.settings(esp, addon, 'on', yes, firmware=firmware)
    check('%s: the reader add-on is not turned on when CHARONVOICE.EFI is not on the partition' % tag, off and code != 0 and read_variable(firmware, 'DriverOrder') is None, out + err)
    (esp / 'EFI' / 'HAK' / 'CHARONVOICE.EFI').write_bytes(b'MZ')
    write_variable(firmware, 'Driver0000', load_option('Other driver', '\\EFI\\other\\driver.efi', attributes=1))
    write_variable(firmware, 'DriverOrder', struct.pack('<H', 0))
    code, out, err = tools.settings(esp, addon, 'on', yes, firmware=firmware)
    option = read_variable(firmware, 'Driver0001') or b''
    description = ('Charon voice\0').encode('utf-16-le')
    file_node = ('\\EFI\\HAK\\CHARONVOICE.EFI\0').encode('utf-16-le')
    ok = code == 0 and 'addon=on Driver0001' in out and order_of(firmware, 'DriverOrder') == [1, 0]
    ok = ok and option[:4] == struct.pack('<I', 3) and option[6:6 + len(description)] == description and file_node in option and option.endswith(bytes([0x7f, 0xff, 4, 0]))
    hd = option[6 + len(description):6 + len(description) + 42]
    ok = ok and hd[:4] == bytes([4, 1, 42, 0]) and hd[40:42] == bytes([2, 2])
    check('%s: turning the reader add-on on creates the driver option "Charon voice" (attributes 3, first in DriverOrder) and reads it back' % tag, ok, out + err + option.hex())
    code, out, err = tools.settings(esp, addon, 'off', yes, firmware=firmware)
    check('%s: turning it off removes only that option; the other driver stays' % tag,
          code == 0 and 'addon=off' in out and order_of(firmware, 'DriverOrder') == [0] and read_variable(firmware, 'Driver0001') is None and read_variable(firmware, 'Driver0000') is not None, out + err)
    code, out, err = tools.settings(esp, addon, 'on', firmware=firmware, stdin=b'no\n')
    check('%s: without a yes the firmware variables stay as they are' % tag, order_of(firmware, 'DriverOrder') == [0] and read_variable(firmware, 'Driver0001') is None, out + err)
    code, out, err = tools.settings(esp, addon, 'status')
    check('%s: without firmware access the add-on request is refused (exit 7)' % tag, code == 7, out + err)


def listing(tools, esp, firmware):
    code, out, err = tools.boot(esp, firmware, tools.option['list'])
    rows = []
    for line in out.splitlines():
        match = re.match(r'(\d+) Boot([0-9A-F]{4}) (\S+) (.*)', line)
        if match:
            rows.append((int(match.group(1)), match.group(2), match.group(3), match.group(4)))
    return code, rows, pairs(out), out + err


def test_bootmanager(tools, host, work):
    tag = tools.name
    yes = tools.option['yes']
    esp = new_esp(work, tag + '-boot')
    firmware = new_firmware(work, tag + '-boot-fw', secure=1)
    code, rows, info, text = listing(tools, esp, firmware)
    check('%s boot manager: lists BootOrder with Charon marked, Secure Boot state, no Continue target' % tag,
          code == 0 and [r[1] for r in rows] == ['0004', '0001', '0007', '0002'] and 'C' in rows[1][2] and rows[0][3].startswith('Windows Boot Manager')
          and info.get('secureboot') == 'on' and info.get('bootnext') == 'none' and info.get('continue') == 'none', text)
    before = dict((p.name, p.read_bytes()) for p in firmware.iterdir())
    code, out, err = tools.boot(esp, firmware, tools.option['move'], '0007', 'up', stdin=b'no\n')
    check('%s boot manager: a change that is not confirmed changes nothing' % tag, dict((p.name, p.read_bytes()) for p in firmware.iterdir()) == before, out + err)
    code, out, err = tools.boot(esp, firmware, tools.option['move'], '0007', 'up', yes)
    ok = code == 0 and order_of(firmware) == [4, 7, 1, 2]
    code, out2, err2 = tools.boot(esp, firmware, tools.option['move'], '0004', 'down', yes)
    ok = ok and code == 0 and order_of(firmware) == [7, 4, 1, 2]
    code, out3, err3 = tools.boot(esp, firmware, tools.option['move'], '0007', 'up', yes)
    check('%s boot manager: entries move up and down; moving the first one up is refused' % tag, ok and code == 5 and order_of(firmware) == [7, 4, 1, 2], out + err + out2 + err2 + out3 + err3)
    code, out, err = tools.boot(esp, firmware, tools.option['charon'], 'first', yes)
    ok = code == 0 and order_of(firmware) == [1, 7, 4, 2]
    code, out2, err2 = tools.boot(esp, firmware, tools.option['charon'], 'second', yes)
    ok = ok and code == 0 and order_of(firmware) == [7, 1, 4, 2]
    code, out3, err3 = tools.boot(esp, firmware, tools.option['charon'], 'remove', yes)
    ok = ok and code == 0 and order_of(firmware) == [7, 4, 2] and read_variable(firmware, 'Boot0001') is not None
    check('%s boot manager: Charon first, second, removed from the order (its Boot0001 entry stays)' % tag, ok, out + err + out2 + err2 + out3 + err3)
    code, out, err = tools.boot(esp, firmware, tools.option['charon'], 'restore', yes)
    check('%s boot manager: Charon is put back into the order' % tag, code == 0 and sorted(order_of(firmware)) == [1, 2, 4, 7] and 1 in order_of(firmware), out + err)

    # Continue target, checked with Charon's ParseContinueTarget.
    hak = esp / 'EFI' / 'HAK'
    code, out, err = tools.boot(esp, firmware, tools.option['continue'], '0004', yes)
    record = (hak / 'continue.bin').read_bytes() if (hak / 'continue.bin').exists() else b''
    option = read_variable(firmware, 'Boot0004')
    expected = b'HAKCONT1' + struct.pack('<HI', 4, len(option)) + option + b'HEND'
    _, parsed, text = host.call('continue', hak / 'continue.bin')
    marker = (hak / 'continue-set').read_bytes() if (hak / 'continue-set').exists() else b''
    check('%s boot manager: the Continue target file is what Charon\'s SaveContinue writes and ParseContinueTarget accepts it' % tag,
          code == 0 and record == expected and parsed.get('continue') == 'ok' and parsed.get('id') == '0004' and parsed.get('size') == str(len(option)) and marker[:1] == b'\x01',
          out + err + text)
    _, rows, info, text = listing(tools, esp, firmware)
    check('%s boot manager: the list shows the Continue target' % tag, info.get('continue') == '0004' and any('T' in r[2] and r[1] == '0004' for r in rows), text)
    code, out, err = tools.boot(esp, firmware, tools.option['continue'], '0001', yes)
    code2, out2, err2 = tools.boot(esp, firmware, tools.option['continue'], '0009', yes)
    check('%s boot manager: Charon itself or an entry outside BootOrder is refused as Continue target' % tag,
          code == 5 and code2 == 5 and (hak / 'continue.bin').read_bytes() == expected, out + err + out2 + err2)

    # BootNext, firmware setup, restart.
    code, out, err = tools.boot(esp, firmware, tools.option['bootnext'], yes)
    _, rows, info, text = listing(tools, esp, firmware)
    check('%s boot manager: "Charon\'s menu at the next restart only" sets BootNext to Charon\'s entry and reads it back' % tag,
          code == 0 and read_variable(firmware, 'BootNext') == struct.pack('<H', 1) and info.get('bootnext') == '0001', out + err + text)
    code, out, err = tools.boot(esp, firmware, tools.option['setup'], yes)
    indications = read_variable(firmware, 'OsIndications') or bytes(8)
    check('%s boot manager: "restart into firmware setup" sets bit 0 of OsIndications' % tag, code == 0 and struct.unpack('<Q', indications)[0] & 1 == 1, out + err)
    code, out, err = tools.boot(esp, firmware, tools.option['restart'], yes)
    check('%s boot manager: restart on the stand-in only says what it would do' % tag, code == 0 and 'would restart' in out.lower(), out + err)
    code, out, err = tools.boot(esp, None, tools.option['list'])
    check('%s boot manager: without firmware access it says so (exit 7)' % tag, code == 7, out + err)
    off = new_firmware(work, tag + '-boot-fw-off', order=(0, 4), secure=0)
    write_variable(off, 'Boot0000', load_option('Linux Boot Manager', '\\EFI\\systemd\\systemd-bootx64.efi'))
    write_variable(off, 'BootNext', struct.pack('<H', 0))
    code, rows, info, text = listing(tools, esp, off)
    check('%s boot manager: Secure Boot off and the entry Boot0000 (BootOrder and BootNext of zero bytes) are read correctly' % tag,
          code == 0 and info.get('secureboot') == 'off' and info.get('bootnext') == '0000' and [r[1] for r in rows] == ['0000', '0004'] and 'N' in rows[0][2], text)
    names = sorted(p.name for p in hak.iterdir())
    check('%s boot manager: only continue.bin and continue-set were written to EFI/HAK' % tag, names == ['continue-set', 'continue.bin'], names)


def transcripts(tools, work):
    tag = tools.name
    esp = new_esp(work, tag + '-session')
    firmware = new_firmware(work, tag + '-session-fw')
    (esp / 'EFI' / 'HAK' / 'CHARONVOICE.EFI').write_bytes(b'MZ')
    (esp / 'EFI' / 'HAK' / 'installed-by.txt').write_text('Charon r391; installed by the test\r\n')
    # rate to 80 %, language Polish, symbol level all, information, save, quit
    session = '1\n6\n80\n2\n3\n0\n4\n8\n4\n0\n10\n12\n0\n'
    code, out, err = tools.settings(esp, firmware=firmware, stdin=session.encode()) if tools.name == 'python' else \
        tools.settings(esp, '-Console', firmware=firmware, stdin=session.encode())
    (work / ('transcript-%s-settings.txt' % tag)).write_text(out + err, encoding='utf-8')
    shown = pairs(tools.settings(esp, tools.option['show'])[1])
    check('%s: a text-menu session (rate 80%%, language Polish, symbol level all, save) is stored' % tag,
          shown.get('rate') == '16' and shown.get('language') == '2' and shown.get('punctuation') == '3' and shown.get('sequence') == '1', out[-600:] + err)
    escapes = '\x1b' in out
    check('%s: the text menus use no escape sequences or cursor addressing' % tag, not escapes)
    session = '1\n4\nyes\n1\n0\n'
    code, out, err = tools.boot(esp, firmware, stdin=session.encode()) if tools.name == 'python' else \
        tools.boot(esp, firmware, '-Console', stdin=session.encode())
    (work / ('transcript-%s-bootmanager.txt' % tag)).write_text(out + err, encoding='utf-8')
    check('%s boot manager: a text-menu session (list, Charon first, yes, list) changes BootOrder' % tag, order_of(firmware)[0] == 1 and '\x1b' not in out, out[-600:] + err)


def test_installers(work):
    ps = PowerShellTools()
    script = OS_DIR / 'windows' / 'Install-CharonTools.ps1'
    if ps.exe.exists() and script.exists():
        target = work / 'Program Files' / 'Charon'
        menu = work / 'Start Menu'
        base = [ps.exe, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, '-Destination', target, '-StartMenu', menu, '-Language', 'en']
        code, out, err = run(base)
        code2, out2, err2 = run(base)
        files = sorted(p.name for p in target.iterdir()) if target.exists() else []
        links = sorted(p.name for p in menu.iterdir()) if menu.exists() else []
        check('Install-CharonTools.ps1: copies the programs and creates the two shortcuts, twice with the same result',
              code == 0 and code2 == 0 and 'Charon-Settings.ps1' in files and 'Charon-BootManager.ps1' in files and 'CharonOs.Common.ps1' in files
              and links == ['Charon boot manager.lnk', 'Charon screen reader settings.lnk'], out + err + out2 + err2 + str(files) + str(links))
        code, out, err = run(base + ['-Uninstall'])
        check('Install-CharonTools.ps1 -Uninstall: removes them again', code == 0 and not target.exists() and not any(menu.iterdir()), out + err)
    script = OS_DIR / 'linux' / 'install-charon-tools.sh'
    shell = shutil.which('sh')
    if shell and script.exists():
        dest = work / 'destdir'
        dest.mkdir()
        env = {'DESTDIR': str(dest).replace('\\', '/'), 'LANG': 'C'}
        code, out, err = run([shell, script], env=env)
        code2, out2, err2 = run([shell, script], env=env)
        wanted = ['usr/local/bin/charon-bootmanager', 'usr/local/bin/charon-settings',
                  'usr/local/share/applications/charon-bootmanager.desktop', 'usr/local/share/applications/charon-settings.desktop']
        present = [w for w in wanted if (dest / w).exists()]
        check('install-charon-tools.sh: installs the two programs and the two .desktop files under DESTDIR, twice with the same result',
              code == 0 and code2 == 0 and present == wanted, out + err + out2 + err2)
        code, out, err = run([shell, script, '--uninstall'], env=env)
        check('install-charon-tools.sh --uninstall: removes them again', code == 0 and not any((dest / w).exists() for w in wanted), out + err)
    for name in ('charon-settings.desktop', 'charon-bootmanager.desktop'):
        path = OS_DIR / 'linux' / name
        if path.exists():
            text = path.read_text(encoding='utf-8')
            check('linux/%s: Terminal=true, names in English and Polish' % name, 'Terminal=true' in text and 'Name=' in text and 'Name[pl]=' in text and 'Type=Application' in text)


def test_secure_boot_helper(work):
    """The boot manager program with a stand-in Charon-SecureBoot.ps1 beside it (the real one is never run here)."""
    ps = PowerShellTools()
    if not ps.available():
        return
    copy = work / 'helper-copy'
    copy.mkdir()
    for name in ('Charon-BootManager.ps1', 'CharonOs.Common.ps1'):
        shutil.copy(OS_DIR / 'windows' / name, copy / name)
    (copy / 'Charon-SecureBoot.ps1').write_text(
        "function Get-CharonSecureBootState { [pscustomobject]@{ Vendor = 'Stand-in'; Model = 'Stand-in 1'; SecureBoot = $true; Supported = $true; ReasonEn = ''; ReasonPl = '' } }\n"
        "function Disable-CharonSecureBoot { param([switch]$Polish, [switch]$NoFirmware, $State, [scriptblock]$Say, [scriptblock]$Ask, [scriptblock]$AskSecret)\n"
        "    & $Say 'PL' 'Stand-in explains the risks.'\n"
        "    $word = & $Ask 'Type DISABLE'\n"
        "    $secret = & $AskSecret 'Password'\n"
        "    $ok = ($word -eq 'DISABLE' -and $secret -is [Security.SecureString] -and $secret.Length -eq 6 -and $NoFirmware -and -not $Polish)\n"
        "    [pscustomobject]@{ Status = $(if ($ok) { 'NotPerformed' } else { 'Wrong' }); Changed = $false; Detail = 'stand-in' } }\n", encoding='utf-8-sig')
    esp = new_esp(work, 'helper')
    firmware = new_firmware(work, 'helper-fw', secure=1)
    before = dict((p.name, p.read_bytes()) for p in firmware.iterdir())
    code, out, err = run([ps.exe, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', copy / 'Charon-BootManager.ps1', '-EspPath', esp, '-FirmwareDir', firmware,
                          '-Language', 'en', '-Console'], stdin=b'12\n1\nDISABLE\nsecret\n0\n')
    (work / 'transcript-powershell-secureboot-helper.txt').write_text(out + err, encoding='utf-8')
    check('powershell boot manager: with Charon-SecureBoot.ps1 beside it the Secure Boot item calls Get-CharonSecureBootState and '
          'Disable-CharonSecureBoot (trial mode on the stand-in), passing its questions through; firmware files untouched',
          code == 0 and 'can be turned off from Windows on this computer (Stand-in 1)' in out and 'Stand-in explains the risks.' in out
          and 'NotPerformed' in out and dict((p.name, p.read_bytes()) for p in firmware.iterdir()) == before, out[-900:] + err)
    (copy / 'Charon-SecureBoot.ps1').unlink()
    code, out, err = run([ps.exe, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', copy / 'Charon-BootManager.ps1', '-EspPath', esp, '-FirmwareDir', firmware,
                          '-Language', 'en', '-Console'], stdin=b'12\n0\n')
    check('powershell boot manager: without Charon-SecureBoot.ps1 the Secure Boot item only shows the state',
          code == 0 and 'Secure Boot: on.' in out and 'shows the Secure Boot state only' in out and 'maker' not in out.split('Secure Boot: on.')[-1].split('Number')[0], out[-600:] + err)


def test_dialogs(work, host):
    """Builds both Windows dialogs without showing them and checks what a screen reader gets."""
    ps = PowerShellTools()
    if not ps.available():
        return
    # The controls of the settings dialog are operated (check box, drop-down list, track bar) and saved.
    esp = new_esp(work, 'dialog-save')
    code, out, err = run([ps.exe, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ps.settings_file, '-EspPath', esp, '-NoFirmware', '-Language', 'en',
                          '-GuiSelfTest', '-Set', 'rate=16,wrap=1,punctuation=3,stereo=2,flagWords=1,welcomeSound=0,readerVolume=4'])
    _, loaded, text = host.call('load', esp)
    expected = dict(spec.DEFAULTS, rate=16, wrap=1, punctuation=3, readerExtra=spec.DEFAULTS['readerExtra'] | (2 << 5), readerFlags=47 | 16, welcome=5, readerVolume=4)
    check('dialog settings: operating its check boxes, drop-down lists and track bars and saving gives the record Charon loads',
          code == 0 and 'dialog-saved' in out and loaded.get('status') == 'ok' and members_of(loaded) == expected, out[-400:] + err + text)
    esp = new_esp(work, 'dialog')
    firmware = new_firmware(work, 'dialog-fw')
    for language in ('en', 'pl'):
        for script, title in ((ps.settings_file, 'settings'), (ps.boot_file, 'bootmanager')):
            code, out, err = run([ps.exe, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, '-EspPath', esp, '-FirmwareDir', firmware,
                                  '-Language', language, '-GuiSelfTest'])
            (work / ('dialog-%s-%s.txt' % (title, language))).write_text(out + err, encoding='utf-8')
            controls = []
            for line in out.splitlines():
                match = re.match(r'\s*(\w+) tab=(\d+) (?:enabled=\w+ )?name="(.*)" value="(.*)"$', line)
                if match:
                    controls.append((match.group(1), int(match.group(2)), match.group(3), match.group(4)))
            named = all(name for kind, _, name, _ in controls)
            labelled = True
            for index, (kind, tab, name, value) in enumerate(controls):
                if kind in ('ComboBox', 'ValueTrackBar', 'TrackBar', 'ListBox', 'TextBox'):
                    before = controls[index - 1] if index else ('', -1, '', '')
                    if before[0] != 'Label' or before[1] != tab - 1 or before[2].replace('&', '').rstrip(':').split(' (')[0] not in name + ' (':
                        labelled = False
            bars = [c for c in controls if 'TrackBar' in c[0]]
            spoken = all(re.match(r'\d+% \(role Slider\)$', value) for _, _, _, value in bars)
            keys = re.findall(r'&(\w)', ' '.join(name for kind, _, name, _ in controls if kind in ('Button', 'Label')))
            unique = len(keys) == len(set(k.lower() for k in keys))
            first = out.splitlines()[0] if out else ''
            ok = code == 0 and controls and named and labelled and spoken and unique and 'accept=' in first and 'cancel=' in first
            check('dialog %s (%s): %d controls, every one named, each field directly after its label in the tab order, '
                  '%d track bars with spoken values, access keys unique, default and cancel button' % (title, language, len(controls), len(bars)),
                  ok, 'named=%s labelled=%s spoken=%s unique=%s %s %s' % (named, labelled, spoken, unique, first, err))


def main():
    arguments = sys.argv[1:]
    only = arguments[arguments.index('--only') + 1] if '--only' in arguments else None
    quick = '--quick' in arguments
    work = Path(arguments[arguments.index('--work') + 1]).resolve() if '--work' in arguments else Path(tempfile.mkdtemp(prefix='charon-os-tests-'))
    if work.exists() and any(work.iterdir()):
        raise SystemExit('the work folder must be new or empty: %s' % work)
    work.mkdir(parents=True, exist_ok=True)
    print('work folder:', work)
    test_syntax(work)
    host = Host(build_host(work))
    print('host harness built from src/efi with', find_llvm())
    rng = random.Random(408)
    low = dict(spec.DEFAULTS, rate=5, pitch=5, readerRate=5, readerPitch=5, messageRate=5, messagePitch=5, speechVolume=1, readerVolume=1,
               effectsVolume=0, welcome=0, detail=0, timers=0, capitals=0, readerFlags=0, formatFlags=0, readerExtra=0, autoSeconds=0,
               startMelody=0, sounds=0, verbosity=0, progress=0, punctuation=0, formatGroups=0, profileItems=0)
    high = dict(spec.DEFAULTS, language=2, wrap=1, explicitMessages=1, startMelody=4, backgroundMelody=4, rate=20, pitch=20, readerRate=20,
                readerPitch=20, messageRate=20, messagePitch=20, verbosity=2, progress=3, rememberLast=1, autoSeconds=255, punctuation=3,
                capitals=7, screenLanguage=3, readerFlags=63, formatFlags=255, readerExtra=2 | 4 | 8 | 16 | (2 << 5) | 128, synthKey=255, synthVoice=15,
                formatGroups=7, profile=3, profileItems=4095, readerBar=1, absoluteColours=1)
    vectors = [dict(spec.DEFAULTS), low, high] + [random_settings(rng) for _ in range(9 if quick else 37)]
    for tools in (PythonTools(), PowerShellTools()):
        if only and tools.name != only:
            continue
        if not tools.available():
            check('%s: programs present' % tools.name, False, 'missing files')
            continue
        count = len(vectors) if tools.name == 'python' else min(len(vectors), 14)     # PowerShell starts slowly
        test_record(tools, host, work, vectors[:count])
        test_synths(tools, host, work)
        test_addon(tools, work)
        test_bootmanager(tools, host, work)
        transcripts(tools, work)
    if only != 'python':
        test_dialogs(work, host)
        test_secure_boot_helper(work)
    test_installers(work)
    for cache in (HERE / '__pycache__', OS_DIR / 'linux' / '__pycache__'):
        shutil.rmtree(cache, ignore_errors=True)
    print()
    print('%d passed, %d failed' % (len(PASSED), len(FAILED)))
    for name in FAILED:
        print('FAILED:', name)
    if '--keep' not in arguments and '--work' not in arguments and not FAILED:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if FAILED else 0


if __name__ == '__main__':
    sys.exit(main())
