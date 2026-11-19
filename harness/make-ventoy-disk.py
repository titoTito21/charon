"""r386: a Ventoy-layout test disk (MBR: part 1 FAT32 with ISOs, part 2 VTOYEFI).
  make-ventoy-disk.py OUT.img ISO..."""
import sys,lzma,struct,importlib.util
from pathlib import Path
base=Path(__file__).resolve().parent;v=base/'ventoy-dl/ventoy-1.1.18'
out=Path(sys.argv[1]);isos=[Path(p) for p in sys.argv[2:]]
spec=importlib.util.spec_from_file_location('cvm',base/'charon-vm.py');cvm=importlib.util.module_from_spec(spec)
a=sys.argv;sys.argv=['charon-vm.py'];spec.loader.exec_module(cvm);sys.argv=a
size_mb=384;total=size_mb*2048;p2=65536;p1s=2048;p1n=total-p2-p1s
part1=out.with_suffix('.p1');cvm.fat_disk(part1,p1n//2048,{p.name:p.read_bytes() for p in isos})
efi=lzma.open(v/'ventoy/ventoy.disk.img.xz').read();assert len(efi)==p2*512
disk=bytearray(total*512)
core=lzma.open(v/'boot/core.img.xz').read();disk[512:512+len(core)]=core
bootimg=(v/'boot/boot.img').read_bytes();disk[0:446]=bootimg[:446]
def entry(boot,typ,start,n):return struct.pack('<B3sB3sII',boot,b'\xfe\xff\xff',typ,b'\xfe\xff\xff',start,n)
disk[446:462]=entry(0x80,0x0c,p1s,p1n);disk[462:478]=entry(0,0xef,p1s+p1n,p2);disk[510:512]=b'\x55\xaa'
disk[p1s*512:(p1s+p1n)*512]=part1.read_bytes()[:p1n*512];disk[(p1s+p1n)*512:]=efi
out.write_bytes(disk);part1.unlink();print('ok',out)
