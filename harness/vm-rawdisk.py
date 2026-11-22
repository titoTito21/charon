"""vm-rawdisk.py NAME [profile line ...]: for a stopped VMware test machine work/NAME made by dos-me.sh.
Turns its sparse disk.vmdk into disk.raw + disk-flat.vmdk (so the files on it can be edited from the host
with charon-r400-esp.py) and appends the given lines (e.g. debugserial=1) to the Windows ME profile
\\EFI\\CharonDOS\\C\\PC\\WINME\\CHARONPC.CFG on it."""
import subprocess, sys
from pathlib import Path
base = Path(__file__).resolve().parent
name = sys.argv[1]
d = base / name
if (d / 'disk.vmdk').exists():
    subprocess.run([str(base / 'charon-release-r394/Charon/qemu/qemu-img.exe'), 'convert', '-O', 'raw', str(d / 'disk.vmdk'), str(d / 'disk.raw')], check=True)
    (d / 'disk.vmdk').unlink()
    n = (d / 'disk.raw').stat().st_size // 512
    (d / 'disk-flat.vmdk').write_text('# Disk DescriptorFile\nversion=1\nCID=fffffffe\nparentCID=ffffffff\ncreateType="monolithicFlat"\n\n'
                                      'RW %d FLAT "disk.raw" 0\n\nddb.adapterType = "lsilogic"\nddb.geometry.cylinders = "%d"\n'
                                      'ddb.geometry.heads = "255"\nddb.geometry.sectors = "63"\nddb.virtualHWVersion = "21"\n' % (n, n // 16065))
    vmx = d / (name + '.vmx')
    t = vmx.read_text(encoding='cp1250').replace('nvme0:0.fileName = "disk.vmdk"', 'nvme0:0.fileName = "disk-flat.vmdk"')
    vmx.write_text(t, encoding='cp1250')
lines = sys.argv[2:]
if lines:
    cfg = d / 'profile.cfg'
    path = '/EFI/CharonDOS/C/PC/WINME/CHARONPC.CFG'
    esp = [sys.executable, str(base / 'charon-r400-esp.py'), str(d / 'disk.raw')]
    subprocess.run(esp + ['get', path, str(cfg)], check=True, capture_output=True)
    data = cfg.read_bytes().rstrip(b'\r\n') + b'\r\n' + b''.join(x.encode() + b'\r\n' for x in lines)
    cfg.write_bytes(data)
    subprocess.run(esp + ['put', path, str(cfg)], check=True, capture_output=True)
print('raw disk ready', name, lines)
