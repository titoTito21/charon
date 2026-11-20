"""CharonPC test machines (QEMU + OVMF) on QMP ports 4480-4489.

  charon-pc-vm.py prepare NAME BOOT.EFI [--file DEST=SOURCE ...] [--mb N] [--show] [--wav] [--tcg]
  charon-pc-vm.py screen NAME FILE.png
  charon-pc-vm.py keys NAME GAP qcode [qcode ...]     ('ctrl+shift+f12': modifiers held)
  charon-pc-vm.py type NAME GAP "text"                 (US layout, Enter as \\n)
  charon-pc-vm.py quit NAME
  charon-pc-vm.py clean NAME                           (deletes the machine's disk images)

prepare makes work/NAME (must not exist): a FAT32 superfloppy boot disk with
BOOT.EFI as EFI/BOOT/BOOTX64.EFI plus the --file entries (DEST is a path on
the disk), the OVMF firmware of the Charon test machines, and starts QEMU on
the Windows hypervisor with the firmware debug port 0x402 written to
work/NAME/debugcon.log (CharonPC mirrors its Bochs log there with
debugcon=1). CHARON_PC_PORT selects the QMP port (default 4480).
"""
from pathlib import Path
import json, os, shutil, socket, subprocess, sys, time
work = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(work / 'fat-image-deps'), str(work / 'fat-legacy-deps')]
QEMU = os.environ.get('CHARON_QEMU') or str(Path(__file__).resolve().parents[5] / '2026-11-02/mo/outputs/haiku-accessibility/qemu/qemu-system-x86_64.exe')  # QEMU 10.x with SDL
PORT = int(os.environ.get('CHARON_PC_PORT', '4480'))
assert 4480 <= PORT <= 4489, 'CharonPC machines use QMP ports 4480-4489'

def qmp():
    s = socket.create_connection(('127.0.0.1', PORT), timeout=20); f = s.makefile('rwb', buffering=0); f.readline()
    def cmd(n, a=None):
        f.write((json.dumps(dict(execute=n, arguments=a or {})) + '\n').encode())
        while True:
            r = json.loads(f.readline())
            if 'return' in r or 'error' in r: return r
    cmd('qmp_capabilities'); return cmd

def fat_disk(path, size_mb, files):
    from pyfatfs.PyFatFS import PyFatFS
    from pyfatfs import PyFat
    with open(path, 'wb') as f: f.truncate(size_mb * 1024 * 1024)
    fat = PyFat.PyFat(); fat.mkfs(str(path), fat_type=PyFat.PyFat.FAT_TYPE_FAT32, size=size_mb * 1024 * 1024, label='CHARONPC')
    fat.close()
    with PyFatFS(str(path)) as fs:
        for name, source in files.items():
            parts = name.split('/')
            for i in range(1, len(parts)):
                d = '/'.join(parts[:i])
                if not fs.exists(d): fs.makedir(d)
            with fs.openbin(name, 'w') as out, open(source, 'rb') as src: shutil.copyfileobj(src, out, 1 << 20)

def prepare(name, efi, extra, size_mb, show, wav, tcg):
    area = work / name; area.mkdir(exist_ok=False)
    with socket.socket() as probe: assert probe.connect_ex(('127.0.0.1', PORT)) != 0, f'QMP port {PORT} is in use'
    files = {'EFI/BOOT/BOOTX64.EFI': efi, **extra}
    need = sum(Path(p).stat().st_size for p in files.values()) // (1 << 20) + 48
    fat_disk(area / 'boot-disk.img', max(size_mb, need, 64), files)
    shutil.copyfile(work / 'charon-machine-integrated-r349/firmware-code.fd', area / 'firmware-code.fd')
    shutil.copyfile(work / 'charon-machine-r347/firmware-vars.fd', area / 'firmware-vars.fd')
    command = [QEMU, '-name', 'CharonPC-' + name, '-machine', 'q35,accel=' + ('tcg' if tcg else 'whpx,kernel-irqchip=off'),
               '-cpu', 'max', '-smp', '2', '-m', os.environ.get('CHARON_PC_MEM', '1024'), '-nic', 'none',
               '-display', 'sdl' if show else 'none',
               '-drive', 'if=pflash,format=raw,readonly=on,file=firmware-code.fd', '-drive', 'if=pflash,format=raw,file=firmware-vars.fd',
               '-drive', 'if=none,id=disk0,format=raw,file=boot-disk.img', '-device', 'ide-hd,drive=disk0,bus=ide.0,bootindex=0',
               '-debugcon', 'file:debugcon.log', '-global', 'isa-debugcon.iobase=0x402',
               '-audiodev', ('wav,id=snd,path=live.wav' if wav else 'dsound,id=snd'), '-device', 'ich9-intel-hda', '-device', 'hda-duplex,audiodev=snd',
               '-qmp', f'tcp:127.0.0.1:{PORT},server=on,wait=off', '-serial', 'file:serial.log']
    out = (area / 'stdout.log').open('xb'); err = (area / 'stderr.log').open('xb')
    child = subprocess.Popen(command, cwd=area, stdin=subprocess.DEVNULL, stdout=out, stderr=err, creationflags=subprocess.DETACHED_PROCESS)
    (area / 'process.json').write_text(json.dumps(dict(pid=child.pid, command=command, files={k: str(v) for k, v in files.items()}), indent=2))
    print('PID', child.pid, area)
    if not wav:
        # Audible on the host speakers; the same stream is recorded for measurement.
        for _ in range(40):
            try: print(qmp()('human-monitor-command', {'command-line': 'wavcapture ' + str(area / 'live.wav') + ' snd'})); break
            except OSError: time.sleep(.5)

def screen(name, png):
    from PIL import Image
    ppm = (work / name / 'screen.ppm').resolve()
    r = qmp()('screendump', {'filename': str(ppm)})
    assert 'return' in r, r
    for _ in range(50):
        if ppm.exists() and ppm.stat().st_size > 100: break
        time.sleep(.1)
    time.sleep(.3)
    Image.open(ppm).save(png); print('saved', png)

def keys(gap, codes):
    cmd = qmp()
    for k in codes:
        parts = k.split('+')
        for m in parts[:-1]: cmd('input-send-event', {'events': [{'type': 'key', 'data': {'down': True, 'key': {'type': 'qcode', 'data': m}}}]}); time.sleep(.05)
        for down in (True, False):
            cmd('input-send-event', {'events': [{'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': parts[-1]}}}]}); time.sleep(.05)
        for m in reversed(parts[:-1]): cmd('input-send-event', {'events': [{'type': 'key', 'data': {'down': False, 'key': {'type': 'qcode', 'data': m}}}]}); time.sleep(.05)
        time.sleep(gap)

SHIFTED = {'!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6', '&': '7', '*': '8', '(': '9', ')': '0', '_': 'minus',
           '+': 'equal', '{': 'bracket_left', '}': 'bracket_right', ':': 'semicolon', '"': 'apostrophe', '~': 'grave_accent',
           '|': 'backslash', '<': 'comma', '>': 'dot', '?': 'slash'}
PLAIN = {' ': 'spc', '-': 'minus', '=': 'equal', '[': 'bracket_left', ']': 'bracket_right', ';': 'semicolon', "'": 'apostrophe',
         '`': 'grave_accent', '\\': 'backslash', ',': 'comma', '.': 'dot', '/': 'slash', '\n': 'ret', '\t': 'tab'}
def text_codes(text):
    out = []
    for c in text:
        if c.isalpha() and c.isascii(): out.append(('shift+' if c.isupper() else '') + c.lower())
        elif c.isdigit(): out.append(c)
        elif c in PLAIN: out.append(PLAIN[c])
        elif c in SHIFTED: out.append('shift+' + SHIFTED[c])
        else: raise SystemExit(f'no key for {c!r}')
    return out

if __name__ == '__main__':
    op = sys.argv[1]
    if op == 'prepare':
        a = sys.argv[2:]; name, efi = a[0], a[1]; extra = {}; size = 64
        i = 2
        while i < len(a):
            if a[i] == '--file': dest, source = a[i + 1].split('=', 1); extra[dest] = source; i += 2
            elif a[i] == '--mb': size = int(a[i + 1]); i += 2
            else: i += 1
        prepare(name, efi, extra, size, '--show' in a, '--wav' in a, '--tcg' in a)
    elif op == 'screen': screen(sys.argv[2], sys.argv[3])
    elif op == 'keys': keys(float(sys.argv[3]), sys.argv[4:])
    elif op == 'type': keys(float(sys.argv[3]), text_codes(sys.argv[4].replace('\\n', '\n')))
    elif op == 'quit': print(qmp()('quit'))
    elif op == 'clean':
        for p in (work / sys.argv[2]).glob('*.img'): p.unlink(); print('deleted', p)
    else: sys.exit(__doc__)
