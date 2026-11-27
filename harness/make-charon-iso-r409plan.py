"""r409: a Charon boot disc for the plan trial runs in VMware - work/r409plan-vmw/charon-plan.iso.
The r408 disc maker (make-charon-iso-r408.py) with other inputs: the plan build of Charon (trace
build: what Charon says goes to the serial port), the reader add-on, the CharonDOS of this tree in
the package, the installers' add-on scripts of this tree and the test switch \\EFI\\HAK\\plan-test.

  python make-charon-iso-r409plan.py CHARON.EFI CHARONVOICE.EFI CHARONDOS.EFI OUT.iso
"""
import sys
from pathlib import Path
base = Path(__file__).resolve().parent
efi, voice, dos, out = (Path(a).resolve() for a in sys.argv[1:5])
source = (base / 'make-charon-iso-r408.py').read_text(encoding='utf-8')
def rep(old, new):
    global source
    assert source.count(old) == 1, old
    source = source.replace(old, new)
rep("efi=base/'charon-win-r408final/CHARON.EFI';voice=base/'charon-win-r408final-drv/CHARONVOICE.EFI'", "efi=Path(r'%s');voice=Path(r'%s')" % (efi, voice))
rep("out=base.parent/'outputs/Charon-r408.iso';fatimg=base/'charon-iso-r408-efiboot.img'", "out=Path(r'%s');fatimg=out.with_suffix('.efiboot.img')" % out)
rep("need=sum(len(v) for v in files.values())",
    "files['EFI/HAK/packages/CharonDOS/CHARONDOS.EFI']=Path(r'%s').read_bytes()\n"
    "files['EFI/HAK/plan-test']=b'test switch: every plan set is shown\\r\\n'\n"
    "import os\n"
    "if os.environ.get('WURL'):files['EFI/HAK/windows-iso-url.txt']=os.environ['WURL'].encode()+bytes([13,10])   # the Windows image from a server on the host\n"
    "if os.environ.get('WLANG'):files['EFI/HAK/audio-language']=os.environ['WLANG'].encode()\n"
    "for p in sorted((base/'charon-src-r395/tools/charon-addon').glob('*')):\n"
    "    if p.is_file():files['EFI/HAK/addon/'+p.name]=p.read_bytes()\n"
    "need=sum(len(v) for v in files.values())" % dos)
sys.argv = ['make-charon-iso-r408.py']
exec(compile(source, 'make-charon-iso-r408.py (r409 plan inputs)', 'exec'), {'__name__': '__main__', '__file__': str(base / 'make-charon-iso-r408.py')})
