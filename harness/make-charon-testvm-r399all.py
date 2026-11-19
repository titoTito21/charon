"""r399: outputs/Charon-testvm-r399-all-managers.zip - ONE test machine with
EVERY boot manager of the test set and Charon r399 (QEMU + OVMF + disks + launcher).
Test only, for this computer: it carries this host's Windows Boot Manager files.

  python make-charon-testvm-r399all.py

Disks (built by charon-vm.py exactly as the regression builds them):
  boot-disk  Charon r399 (BOOTX64), CHARONVOICE, AudioDxe, Shell, Memtest86+,
             eSpeak NG / RHVoice add-ons (ABI 3), a demo manager profile
  grub-disk  GRUB 2.12 (Debian) chain loading systemd-boot, Limine, OpenCore,
             Clover, Memtest86+, Windows Boot Manager, probes; plus the Debian
             starfield theme and a gfxterm menu
  win-disk   GPT ESP: systemd-boot + Windows Boot Manager / Memory Diagnostic
  USB        Syslinux, iPXE, Ventoy (3 ISOs), ZFSBootMenu, rEFInd, HAK demo loader
  CD/DVD     (second AHCI controller) Charon-r399.iso, Haiku (its boot loader),
             FreeBSD loader, NetBSD, OpenBSD, Debian mini (GRUB), rescue tools
"""
import sys,os,json,shutil,time,zipfile,importlib.util,subprocess,socket
from pathlib import Path
base=Path(__file__).resolve().parent;root=base.parent
sys.path[:0]=[str(base/'fat-image-deps'),str(base/'fat-legacy-deps')]
efi=base/'charon-win-r399/CHARON.EFI';voice=base/'charon-win-r399-drv/CHARONVOICE.EFI'
synths=base/'synth-modules-r397'
name='testvm-r399all';area=base/name;stage=base/'charon-testvm-r399all'
out=root/'outputs/Charon-testvm-r399-all-managers.zip'
assert not area.exists() and not out.exists() and not stage.exists()
spec=importlib.util.spec_from_file_location('cvm',base/'charon-vm.py');cvm=importlib.util.module_from_spec(spec)
a=sys.argv;sys.argv=['charon-vm.py'];spec.loader.exec_module(cvm);sys.argv=a
with socket.socket() as probe:assert probe.connect_ex(('127.0.0.1',cvm.PORT))!=0,'Stop the previous Charon QEMU first'
haiku=Path(os.environ.get('HAIKU_ISO',r'<local path>'))
cds=[('charon-r399.iso',root/'outputs/Charon-r399.iso'),('haiku.iso',haiku),('freebsd-bootonly.iso',base/'freebsd-dl/bootonly.iso'),
     ('netbsd-boot.iso',base/'netbsd-dl/boot.iso'),('openbsd-cd.iso',base/'openbsd-dl/cd.iso'),
     ('debian-mini.iso',base/'app-sticks/debian-mini.iso'),('rescue-tools.iso',base/'app-sticks/rescue-tools.iso')]
for n,p in cds:assert p.exists(),p
# --- USB sticks (as r396e) ---
sticks=base/'testvm-r399-sticks';sticks.mkdir(exist_ok=False)
rd=base/'refind-dl/x/refind-bin-0.14.2'
ref={'EFI/BOOT/BOOTX64.EFI':(rd/'refind/refind_x64.efi').read_bytes(),
     'EFI/BOOT/refind.conf':b'timeout 30\nscanfor internal,external,manual\ndont_scan_dirs EFI/BOOT,EFI/HAK\n'}
for p in (rd/'refind/icons').glob('*.png'):ref['EFI/BOOT/icons/'+p.name]=p.read_bytes()
for p in (rd/'fonts').glob('*.png'):ref['EFI/BOOT/fonts/'+p.name]=p.read_bytes()
cvm.fat_disk(sticks/'refind.img',32,ref)
usb=['syslinux.img','ipxe.img','ventoy3.img']
for n in usb:shutil.copyfile(base/'app-sticks'/n,sticks/n)
cvm.fat_disk(sticks/'zbm.img',100,{'EFI/BOOT/BOOTX64.EFI':(base/'zbm-dl/zbm.EFI').read_bytes()})
usb+=['zbm.img','refind.img']
shutil.copyfile(base/'a11ydemo-r396b/demo-stick.img',sticks/'a11y-demo.img');usb.append('a11y-demo.img')
# --- prepare the machine with charon-vm.py (boots once, then quit) ---
env=dict(os.environ,CHARON_PROFILES=str(base/'a11y-profiles-r396'),CHARON_VOICE=str(voice),CHARON_SYNTHS=str(synths),CHARON_MEM='2048',
         CHARON_USB=','.join(str(sticks/n) for n in usb))
r=subprocess.run([sys.executable,str(base/'charon-vm.py'),'prepare',name,str(efi),'--grub',str(base/'grub-dl/grubx64.efi'),
                  '--managers','--winboot','--sdboot','--usb'],env=env,capture_output=True,text=True)
assert (area/'process.json').exists(),r.stderr[-2000:]
time.sleep(3)
try:cvm.qmp()('quit')
except OSError:pass
time.sleep(4)
command=json.loads((area/'process.json').read_text())['command']
stage.mkdir()
for n in ('boot-disk.img','grub-disk.img','win-disk.img','firmware-code.fd','firmware-vars.fd'):shutil.copyfile(area/n,stage/n)
# boot-disk: Memtest86+ for Charon's device list (as the release); grub-disk: Debian starfield theme entry
from pyfatfs.PyFatFS import PyFatFS
def put(img,items):
    with PyFatFS(str(img)) as fs:
        for k,data in items.items():
            parts=k.split('/')
            for i in range(1,len(parts)):
                d='/'.join(parts[:i])
                if not fs.exists(d):fs.makedir(d)
            if fs.exists(k):fs.remove(k)
            with fs.openbin(k,'w') as f:f.write(data)
put(stage/'boot-disk.img',{'EFI/HAK/tools/memtest.efi':(base/'memtest-dl/x/mt86p_900_x86_64').read_bytes(),
    'EFI/HAK/audio-language':b'en'})   # Charon speaks English by default
theme=next((base/'grubtheme-dl/x').rglob('theme.txt')).parent
items={'boot/grub/themes/starfield/'+p.name:p.read_bytes() for p in theme.iterdir() if p.is_file()}
themed=(b'insmod all_video\ninsmod gfxterm\ninsmod png\nloadfont /boot/grub/fonts/unicode.pf2\nset gfxmode=auto\nterminal_output gfxterm\n'
        +b''.join(b'loadfont /boot/grub/themes/starfield/'+n+b'.pf2\n' for n in (b'dejavu_10',b'dejavu_12',b'dejavu_14',b'dejavu_16',b'dejavu_bold_14'))
        +b'set theme=/boot/grub/themes/starfield/theme.txt\nconfigfile /boot/grub/plain.cfg\n')
with PyFatFS(str(stage/'grub-disk.img')) as fs:
    with fs.openbin('boot/grub/grub.cfg','r') as f:cfg=f.read()
entry=b'menuentry "Debian starfield theme" {\n  configfile /boot/grub/themed.cfg\n}\n'
items['boot/grub/themed.cfg']=themed;items['boot/grub/plain.cfg']=cfg
for n in ('EFI/BOOT/grub.cfg','boot/grub/grub.cfg','EFI/debian/grub.cfg'):items[n]=cfg+entry
put(stage/'grub-disk.img',items)
(stage/'usb').mkdir();(stage/'cd').mkdir()
for n in usb:shutil.move(str(sticks/n),stage/'usb'/n)
sticks.rmdir()
for n,p in cds:shutil.copyfile(p,stage/'cd'/n)
shutil.copytree(base/'charon-release-r394/Charon/qemu',stage/'qemu')
# --- launcher: the same machine, relative files, no test sockets, CDs on a second AHCI ---
args=[];skip=False
for x in command[1:]:
    if skip:skip=False;continue
    if x in ('-qmp','-serial'):skip=True;continue
    if x.startswith('if=none,id=stick'):x=x.split('file=',1)[0]+'file=usb\\'+Path(x.split('file=',1)[1]).name
    if x.startswith('Charon-'):x='Charon-r399-testy'
    args.append(x)
for c in range((len(cds)+5)//6):args+=['-device',f'ahci,id=cdahci{c}']   # 6 ports per AHCI controller
for i,(n,_) in enumerate(cds):args+=['-drive',f'if=none,id=cd{i},media=cdrom,readonly=on,file=cd\\{n}','-device',f'ide-cd,bus=cdahci{i//6}.{i%6},drive=cd{i}']
q=lambda s:'"'+s+'"' if ' ' in s else s
bat=('@echo off\r\nrem Charon r399 - test machine with every boot manager (QEMU + OVMF). Tests only.\r\n'
     'cd /d "%~dp0"\r\nif not exist state\\firmware-vars.fd (mkdir state 2>nul & copy /y firmware-vars.fd state\\firmware-vars.fd >nul)\r\n'
     '"%~dp0qemu\\qemu-system-x86_64.exe" '+' '.join(q(x) for x in args).replace('file=firmware-vars.fd','file=state\\firmware-vars.fd')+'\r\n')
(stage/'Start-test-machine.bat').write_text(bat,encoding='ascii',newline='')
(stage/'Reset-firmware-settings.bat').write_text('@echo off\r\ncd /d "%~dp0"\r\ndel /q state\\firmware-vars.fd 2>nul\r\necho Firmware variables are back to the state in the archive.\r\npause\r\n',encoding='ascii',newline='')
(stage/'README.txt').write_text(r'''Charon r399 - test machine with EVERY boot manager
==================================================

Start: Start-test-machine.bat (Windows, nothing to install; QEMU is in qemu\).
Unpack the whole archive first. Sound goes to the speakers (DirectSound). The
first start takes longer. Firmware variables (boot order, Driver####) are kept
in state\; Reset-firmware-settings.bat restores the original state.

Charon starts first. Space opens its menu; Enter on "Boot devices" shows the
list; arrows pick a boot manager, Enter starts it, and the reader keeps reading.

Disks:
  boot-disk.img  Charon r399 (EFI\BOOT\BOOTX64.EFI), CHARONVOICE.EFI (speech from
                 power-on: Settings -> Speech from power-on), AudioDxe, Shell,
                 Memtest86+, eSpeak NG and RHVoice add-ons, demo profile demo.txt
  grub-disk.img  GRUB 2.12 (Debian); its menu starts systemd-boot, Limine, OpenCore,
                 Clover, Memtest86+, Windows Boot Manager, Windows Memory Diagnostic,
                 probe programs, a graphical menu (gfxterm), the Debian starfield
                 theme and "UEFI firmware settings" (fwsetup)
  win-disk.img   GPT disk: systemd-boot and Windows Boot Manager
  usb\           Syslinux, iPXE, Ventoy (3 ISO images), ZFSBootMenu, rEFInd,
                 Demo Loader (HAK accessibility protocol)
  cd\            Charon-r399.iso (Charon boot disc), Haiku (Haiku boot loader),
                 FreeBSD (loader), NetBSD, OpenBSD, Debian mini (installer GRUB),
                 rescue tools

The Windows Boot Manager files were copied from C:\Windows\Boot of this computer
and are for local tests only: DO NOT publish this archive.
Licences: qemu\ (QEMU, edk2); Charon sources in charon-github-r399; licences of
the other components in Charon-Installer-r399.zip (licenses\).
''',encoding='utf-8')
with zipfile.ZipFile(out.with_suffix('.zip.tmp'),'x',zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as z:
    for p in sorted(stage.rglob('*')):
        if p.is_file():z.write(p,'Charon-testvm-r399/'+p.relative_to(stage).as_posix())
os.replace(out.with_suffix('.zip.tmp'),out)
print('ok',out,out.stat().st_size)
