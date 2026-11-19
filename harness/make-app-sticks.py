"""r386: USB stick images with other UEFI boot programs (test only).
  make-app-sticks.py OUTDIR  -> syslinux.img, ipxe.img"""
import sys,importlib.util
from pathlib import Path
base=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(exist_ok=False)
spec=importlib.util.spec_from_file_location('cvm',base/'charon-vm.py');cvm=importlib.util.module_from_spec(spec)
argv=sys.argv;sys.argv=['charon-vm.py'];spec.loader.exec_module(cvm);sys.argv=argv
m=base/'uefi-apps-dl/syslinux-common/x/usr/lib/syslinux/modules/efi64'
cfg=b"UI menu.c32\nMENU TITLE Charon Syslinux test\nTIMEOUT 600\nLABEL one\n  MENU LABEL Debian test one\n  KERNEL vmlinuz1\nLABEL two\n  MENU LABEL Debian test two\n  KERNEL vmlinuz2\nLABEL three\n  MENU LABEL Rescue shell\n  KERNEL vmlinuz3\n"
files={'EFI/BOOT/BOOTX64.EFI':(base/'uefi-apps-dl/syslinux/x/usr/lib/SYSLINUX.EFI/efi64/syslinux.efi').read_bytes(),'EFI/BOOT/syslinux.cfg':cfg}
for n in ['ldlinux.e64','menu.c32','libutil.c32','libcom32.c32']:files['EFI/BOOT/'+n]=(m/n).read_bytes()
cvm.fat_disk(out/'syslinux.img',32,files)
cvm.fat_disk(out/'ipxe.img',32,{'EFI/BOOT/BOOTX64.EFI':(base/'uefi-apps-dl/ipxe/x/usr/lib/ipxe/ipxe.efi').read_bytes()})
print('ok',out)
