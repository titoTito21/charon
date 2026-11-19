#!/usr/bin/env python3
"""Stage a portable Windows Charon VM only from explicitly validated inputs.
No VM is launched and no firmware trust/key store is changed by this tool.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

PROFILE = 'q35-tcg-2cpu-512m-ide-hda-dsound-v1'
CHECKS = ('boot', 'speech_pl', 'speech_en', 'keyboard', 'speech_interrupts',
          'cues', 'background_continues', 'settings_survive_restart',
          'autoboot_saved_target', 'space_opens_menu', 'handoff_stop')
FILES = ('charon', 'audio_driver', 'disk', 'firmware_code', 'firmware_vars')

def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(data)
    return digest.hexdigest()

def tree(path):
    if not path.is_dir() or path.is_symlink():
        raise ValueError('Expected a real directory')
    entries = {}
    for item in sorted(path.rglob('*')):
        if item.is_symlink() or (hasattr(item, "is_junction") and item.is_junction()):
            raise ValueError('Symbolic links are not portable package inputs')
        if item.is_file():
            entries[item.relative_to(path).as_posix()] = sha(item)
    return entries

def tree_hash(entries):
    return hashlib.sha256(json.dumps(entries, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

def validate(record, inputs, assets, qemu):
    if record.get('schema') != 'charon-vm-validation-1' or record.get('profile') != PROFILE:
        raise ValueError('Missing matching explicit VM validation record/profile')
    if any(record.get('checks', {}).get(check) is not True for check in CHECKS):
        raise ValueError('All required real VM/audio/keyboard checks must be explicitly true')
    if record.get('disk_contains_exact_components') is not True:
        raise ValueError('Validation must attest the tested disk contains these exact EFI and audio assets')
    evidence = record.get('evidence')
    if not isinstance(evidence, str) or not evidence.strip():
        raise ValueError('Validation requires a human-readable test evidence identifier')
    hashes = {key: sha(path) for key, path in inputs.items()}
    hashes.update(assets=tree_hash(assets), qemu=tree_hash(qemu))
    if record.get('sha256') != hashes:
        raise ValueError('Validation hashes do not exactly match package inputs')
    return hashes

BAT = r'''@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uruchom-Charona.ps1"
set "result=%errorlevel%"
if not "%result%"=="0" (
  echo.
  echo Nie uruchomiono poprawnie Charona. Kod: %result%
  pause
)
exit /b %result%
'''
PS = r'''$ErrorActionPreference = 'Stop'
$base = $PSScriptRoot
Set-Location -LiteralPath $base
$lock = $null
try {
    New-Item -ItemType Directory -Force -Path (Join-Path $base 'state') | Out-Null
    try {
        $lock = [IO.File]::Open((Join-Path $base 'state\instance.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    } catch {
        throw 'Charon jest juz uruchomiony albo katalog state jest niedostepny. Nie uruchamiaj drugiej kopii.'
    }
    # Templates remain unchanged. Each user keeps firmware variables and disk changes.
    foreach ($name in @('firmware-vars.fd', 'boot-disk.img')) {
        $destination = Join-Path $base ('state\' + $name)
        if (!(Test-Path -LiteralPath $destination)) {
            $pending = $destination + '.new'
            Copy-Item -LiteralPath (Join-Path $base ('templates\' + $name)) -Destination $pending -Force
            Move-Item -LiteralPath $pending -Destination $destination
        }
    }
    $qemu = Join-Path $base 'qemu\qemu-system-x86_64.exe'
    if (!(Test-Path -LiteralPath $qemu)) { throw 'Brak przenosnego QEMU w pakiecie.' }
    $arguments = @('-name','Charon','-machine','q35,accel=tcg','-cpu','max',
        '-smp','2','-m','512','-nic','none','-display','sdl',
        '-drive','if=pflash,format=raw,readonly=on,file=firmware-code.fd',
        '-drive','if=pflash,format=raw,file=state/firmware-vars.fd',
        '-drive','if=ide,format=raw,file=state/boot-disk.img',
        '-audiodev','dsound,id=charon_audio',
        '-device','ich9-intel-hda', '-device','hda-duplex,audiodev=charon_audio')
    & $qemu @arguments
    $result = $LASTEXITCODE
    if ($result -ne 0) { throw "QEMU zakonczyl prace z kodem $result." }
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
} finally {
    if ($null -ne $lock) { $lock.Dispose() }
}
exit 0
'''
README = '''Charon — portable test machine (Windows, QEMU)

Run Uruchom-Charona.bat. QEMU opens its own window; sound goes through
DirectSound. No administrator rights or installation needed. The machine has
no network. The folder must be writable. The first start creates the state
folder (copies of the disk and UEFI variables from templates).

Start: Charon says "Welcome to Charon. Press Space to open the options menu"
and after the countdown starts the Continue target by itself (in this machine:
EFI Internal Shell). Space during the announcement opens the menu.

Keys: Up/Down arrows choose; Enter/Space open or toggle; Escape/Backspace go
back (in the main menu Escape = continue booting); Left arrow goes back, and on
the "Speech rate" slider Left/Right change the rate by 10% (50-200%).
Home/End: start/end of the list. Q R W A in a row: explicit message mode
(Q R W A again turns it off). The next key interrupts speech and effects; the
background melody keeps playing.

Main menu: Boot devices; Charon settings (wrap lists, refresh devices, Secure
Boot, pending changes, Continue target, language, interface sounds, start and
background melody, speech rate); UEFI settings (firmware HII forms with names,
values and editing; F1 help, F5 refresh); UEFI Shell (Charon's speaking Shell:
F9/F10 previous/next output line, F11 current command line, F12 stop speech);
Continue.

Settings are stored on the machine's disk (state). Do not delete state if you
want to keep them; deleting state restores the initial state.
Close QEMU before copying the folder and do not run two copies at once.

In emulation (TCG) the host can be slow; the audio driver then detects a gap
in buffer service, Charon recovers the sound (a real Stop and restart) and
repeats the interrupted sentence. Such a short gap is not a failure.

Not finished in this release: reading firmware messages before Charon starts
(Driver####) and after the handoff; vendor screens that publish no HII forms;
full tests on physical UEFI. The host computer's Secure Boot keys are neither
installed nor changed.

templates: original images; components: the exact EFI and PCM files of the
tested disk (changing components does NOT change the machine's disk);
licenses: licences and full sources; manifest.json: SHA-256 sums;
validation.json: record of the tests run before packaging.
'''

def package(args):
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Output already exists; refusing to replace user state')
    inputs = {key: getattr(args, key).resolve(strict=True) for key in FILES}
    if any(not path.is_file() or path.is_symlink() for path in inputs.values()):
        raise ValueError('Expected regular input files')
    qemu_dir, assets_dir, licenses_dir = (getattr(args, key).resolve(strict=True) for key in ('qemu', 'assets', 'licenses'))
    for source in (qemu_dir, assets_dir, licenses_dir):
        if output == source or source in output.parents:
            raise ValueError('Output must be outside input directories')
    qemu, assets, licenses = tree(qemu_dir), tree(assets_dir), tree(licenses_dir)
    if 'qemu-system-x86_64.exe' not in qemu:
        raise ValueError('QEMU distribution must include qemu-system-x86_64.exe')
    if not licenses:
        raise ValueError('Supply component license notices')
    expected = {'welcome','focus','edge','wrap','submenu_open','submenu_close','popUp','popup_close','dialog_open','dialog_close','select','continue'}
    expected |= {f'{kind}-{index}' for kind in ('start','background') for index in range(1,5)}
    if not {name+'.pcm' for name in expected}.issubset(assets):
        raise ValueError('Incomplete flat audio pack (including exact popUp.pcm case)')
    record = json.loads(args.validation.read_text(encoding='utf-8-sig'))
    hashes = validate(record, inputs, assets, qemu)
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.'+output.name+'-stage-', dir=output.parent)).resolve()
    try:
        shutil.copytree(qemu_dir, staging/'qemu')
        shutil.copytree(assets_dir, staging/'components/audio')
        shutil.copytree(licenses_dir, staging/'licenses')
        (staging/'templates').mkdir()
        for key, relative in {'charon':'components/CHARON.EFI','audio_driver':'components/AudioDxe.efi','disk':'templates/boot-disk.img','firmware_code':'firmware-code.fd','firmware_vars':'templates/firmware-vars.fd'}.items():
            shutil.copy2(inputs[key], staging/relative)
        # Detect inputs changing while copying rather than recording stale validation.
        if tree(staging/'qemu') != qemu or tree(staging/'components/audio') != assets or tree(staging/'licenses') != licenses:
            raise ValueError('An input tree changed while copying')
        for key, relative in {'charon':'components/CHARON.EFI','audio_driver':'components/AudioDxe.efi','disk':'templates/boot-disk.img','firmware_code':'firmware-code.fd','firmware_vars':'templates/firmware-vars.fd'}.items():
            if sha(staging/relative) != hashes[key]:
                raise ValueError('An input file changed while copying')
        (staging/'Uruchom-Charona.bat').write_bytes(BAT.replace('\n','\r\n').encode('ascii'))
        (staging/'Uruchom-Charona.ps1').write_bytes(PS.replace('\n','\r\n').encode('ascii'))
        (staging/'README.txt').write_text(README,encoding='utf-8-sig')
        # Do not copy arbitrary external paths/private commentary from the record.
        public_record = {'schema':record['schema'],'profile':PROFILE,'checks':{key:record['checks'][key] for key in CHECKS},
                         'sha256':hashes,'disk_contains_exact_components':True,'evidence':record['evidence']}
        (staging/'validation.json').write_text(json.dumps(public_record,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
        manifest={'schema':'charon-portable-vm-1','profile':PROFILE,'inputs_sha256':hashes,'files':tree(staging)}
        (staging/'manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
        staging.rename(output)
    finally:
        if staging.exists():
            if staging.parent != output.parent or not staging.name.startswith('.'+output.name+'-stage-'):
                raise ValueError('Refusing unsafe staging cleanup')
            shutil.rmtree(staging)
    return output

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in (*FILES,'qemu','assets','licenses','validation','output'):
        parser.add_argument('--'+key.replace('_','-'),type=Path,required=True)
    args=parser.parse_args()
    try:
        output=package(args)
    except (ValueError,OSError,KeyError,TypeError) as error:
        parser.exit(1,str(error)+'\n')
    print(output)
if __name__=='__main__':main()
