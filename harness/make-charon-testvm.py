"""r396: one test machine with every boot manager of the regression set, packed
as outputs/Charon-testvm-r396.zip (QEMU + OVMF + disks + launcher), test only.

  python make-charon-testvm.py CHARON.EFI CHARONVOICE.EFI SYNTHS_DIR

Disks (built by charon-vm.py exactly as the regression builds them):
  boot-disk  Charon (BOOTX64), CHARONVOICE, synthesizer add-ons
  grub-disk  GRUB 2.12 (Debian) chain loading systemd-boot, Limine, OpenCore,
             Clover, Memtest86+, Windows Boot Manager and probe loaders
  win-disk   GPT ESP: systemd-boot + Windows Boot Manager (this host's files)
  USB        Syslinux, iPXE, Ventoy (3 ISOs), ZFSBootMenu, rEFInd
The Windows Boot Manager files are copied from this computer's C:\\Windows\\Boot
for local tests only; the archive must not be published.
"""
import sys,os,json,shutil,time,zipfile,importlib.util,subprocess
from pathlib import Path
base=Path(__file__).resolve().parent;root=base.parent
efi,voice,synths=Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve(),Path(sys.argv[3]).resolve()
name='testvm-r396';area=base/name;out=root/'outputs/Charon-testvm-r396.zip';stage=base/'charon-testvm-r396'
assert not area.exists() and not out.exists() and not stage.exists()
spec=importlib.util.spec_from_file_location('cvm',base/'charon-vm.py');cvm=importlib.util.module_from_spec(spec)
a=sys.argv;sys.argv=['charon-vm.py'];spec.loader.exec_module(cvm);sys.argv=a
sticks=base/'testvm-sticks';sticks.mkdir(exist_ok=False)
rd=base/'refind-dl/x/refind-bin-0.14.2'
ref={'EFI/BOOT/BOOTX64.EFI':(rd/'refind/refind_x64.efi').read_bytes(),
     'EFI/BOOT/refind.conf':b'timeout 30\nscanfor internal,external,manual\ndont_scan_dirs EFI/BOOT,EFI/HAK\n'}
for p in (rd/'refind/icons').glob('*.png'):ref['EFI/BOOT/icons/'+p.name]=p.read_bytes()
for p in (rd/'fonts').glob('*.png'):ref['EFI/BOOT/fonts/'+p.name]=p.read_bytes()
cvm.fat_disk(sticks/'refind.img',32,ref)
usb=['syslinux.img','ipxe.img','ventoy3.img','zbm.img']
for n in usb:shutil.copyfile(base/'app-sticks'/n,sticks/n)
usb.append('refind.img')
env=dict(os.environ,CHARON_VOICE=str(voice),CHARON_SYNTHS=str(synths),CHARON_MEM='2048',
         CHARON_USB=','.join(str(sticks/n) for n in usb))
r=subprocess.run([sys.executable,str(base/'charon-vm.py'),'prepare',name,str(efi),'--grub',str(base/'grub-dl/grubx64.efi'),
                  '--managers','--winboot','--sdboot','--usb'],env=env,capture_output=True,text=True)
assert (area/'process.json').exists(),r.stderr[-2000:]
time.sleep(3)
try:cvm.qmp()('quit')
except OSError:pass
time.sleep(3)
command=json.loads((area/'process.json').read_text())['command']
stage.mkdir()
for n in ('boot-disk.img','grub-disk.img','win-disk.img','firmware-code.fd','firmware-vars.fd'):shutil.copyfile(area/n,stage/n)
(stage/'usb').mkdir()
for n in usb:shutil.move(str(sticks/n),stage/'usb'/n)
sticks.rmdir()
shutil.copytree(base/'charon-release-r394/Charon/qemu',stage/'qemu')
# The launcher: the same machine, files relative to this folder, no test sockets.
args=[]
skip=False
for i,x in enumerate(command[1:]):
    if skip:skip=False;continue
    if x in ('-qmp','-serial'):skip=True;continue
    if x.startswith('if=none,id=stick'):
        stick=Path(x.split('file=',1)[1]).name;x=x.split('file=',1)[0]+'file=usb\\'+stick
    args.append(x)
q=lambda s:'"'+s+'"' if ' ' in s else s
bat='@echo off\r\nrem Charon r396 test machine (QEMU + OVMF). Test only.\r\ncd /d "%~dp0"\r\n"%~dp0qemu\\qemu-system-x86_64.exe" '+' '.join(q(x) for x in args)+'\r\n'
(stage/'Uruchom-maszyne-testowa.bat').write_text(bat,encoding='ascii',newline='')
(stage/'CZYTAJ.txt').write_text('''Charon r396 - maszyna testowa ze wszystkimi menedzerami rozruchu
=================================================================

Uruchomienie: Uruchom-maszyne-testowa.bat (Windows, bez instalacji; QEMU w qemu\\).
Dzwiek przez glosniki komputera (dsound). Pierwszy start trwa dluzej.

Dyski:
  boot-disk.img  Charon r396 (EFI\\BOOT\\BOOTX64.EFI), CHARONVOICE.EFI, dodatki syntezy
                 eSpeak NG i RHVoice (EFI\\HAK\\synths)
  grub-disk.img  GRUB 2.12 (Debian); z jego menu: systemd-boot, Limine, OpenCore,
                 Clover, Memtest86+, Windows Boot Manager, programy probne
  win-disk.img   dysk GPT: systemd-boot i Windows Boot Manager
  usb\\           pamieci USB: Syslinux, iPXE, Ventoy (3 obrazy ISO), ZFSBootMenu, rEFInd
W Charonie: Spacja, Enter otwiera liste urzadzen; strzalki wybieraja menedzera.

Pliki Windows Boot Manager pochodza z C:\\Windows\\Boot tego komputera i sluza
tylko do testow lokalnych: tego archiwum nie wolno publikowac.
Licencje skladnikow: qemu\\ (QEMU, edk2) oraz zrodla Charona w outputs\\charon-github.
''',encoding='utf-8')
with zipfile.ZipFile(out,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in sorted(stage.rglob('*')):
        if p.is_file():z.write(p,'Charon-testvm-r396/'+p.relative_to(stage).as_posix())
shutil.rmtree(stage);shutil.rmtree(area)
print('ok',out,out.stat().st_size)
