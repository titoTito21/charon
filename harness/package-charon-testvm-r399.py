"""Build outputs/Charon-testvm-r399.zip - the COMPLETE release with the QEMU demo
(launcher + bundled QEMU + disk templates), rebuilt from the r394 tree's qemu/ and
licenses, fresh r399 binaries, and freshly assembled disk templates (the r394
templates were deleted for space). Publishable: the GRUB disk chain-loads only
redistributable managers (systemd-boot, Limine, OpenCore, Clover, Memtest86+,
probes) - no host Windows files.

  python package-charon-testvm-r399.py
"""
import hashlib,json,os,shutil,sys,zipfile,importlib.util
from pathlib import Path
base=Path(__file__).resolve().parent
sys.path[:0]=[str(base/'fat-image-deps'),str(base/'fat-legacy-deps')]
from pyfatfs.PyFatFS import PyFatFS
spec=importlib.util.spec_from_file_location('cvm',base/'charon-vm.py');cvm=importlib.util.module_from_spec(spec)
_argv=sys.argv;sys.argv=['charon-vm.py'];spec.loader.exec_module(cvm);sys.argv=_argv
r394=base/'charon-release-r394/Charon'
efi=base/'charon-win-r399/CHARON.EFI';voice=base/'charon-win-r399-drv/CHARONVOICE.EFI'
audio=base/'charon-audio-driver-r389/AudioDxe-r389.efi';memtest=base/'memtest-dl/x/mt86p_900_x86_64'
addon=base/'charon-src-r395/tools/charon-addon'
stage=base/'charon-testvm-r399';out=base.parent/'outputs/Charon-testvm-r399.zip'
assert not stage.exists() and not out.exists(),(stage,out)
assert r394.exists() and (r394/'qemu/qemu-system-x86_64.exe').exists(),'need the r394 tree with qemu/'
stage.mkdir(parents=True);(stage/'templates').mkdir()

def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()

# --- templates/firmware-vars.fd and firmware-code.fd ---
shutil.copyfile(base/'charon-machine-r347/firmware-vars.fd',stage/'templates/firmware-vars.fd')
shutil.copyfile(base/'charon-machine-integrated-r349/firmware-code.fd',stage/'firmware-code.fd')
# --- templates/boot-disk.img: base disk + fresh r399 binaries + built-in Memtest ---
shutil.copyfile(base/'charon-machine-integrated-r349/boot-disk.img',stage/'templates/boot-disk.img')
with PyFatFS(str(stage/'templates/boot-disk.img')) as fs:
    put={'EFI/BOOT/BOOTX64.EFI':efi.read_bytes(),'EFI/HAK/HAKBOOT.EFI':efi.read_bytes(),
         'EFI/HAK/CHARONVOICE.EFI':voice.read_bytes(),'EFI/HAK/AudioDxe.efi':audio.read_bytes(),
         'EFI/HAK/tools/memtest.efi':memtest.read_bytes()}
    for name,data in put.items():
        parts=name.split('/')
        for i in range(1,len(parts)):
            d='/'.join(parts[:i])
            if not fs.exists(d):fs.makedir(d)
        if fs.exists(name):fs.remove(name)
        with fs.openbin(name,'w') as f:f.write(data)
        with fs.openbin(name,'r') as f:assert f.read()==data
# --- templates/grub-disk.img: GRUB chain-loading the redistributable managers ---
cfg=(b'set timeout=8\nset timeout_style=menu\n'
     b'menuentry "Charon test entry one" {\n  echo "Entry one selected"\n  sleep 3\n}\n'
     b'menuentry "Charon test entry two" {\n  echo "Entry two selected"\n  sleep 3\n}\n'
     b'menuentry "UEFI firmware settings" {\n  fwsetup\n}\n'
     b'menuentry "Probe loader" {\n  chainloader /EFI/probe/PROBE.EFI\n}\n'
     b'menuentry "Graphical probe" {\n  chainloader /EFI/probe/GPROBE.EFI\n}\n')+cvm.MANAGER_ENTRIES
gfiles={'EFI/probe/PROBE.EFI':(base/'charon-probe-r355/PROBE.EFI').read_bytes(),
        'EFI/probe/GPROBE.EFI':(base/'charon-probe-r355/GPROBE.EFI').read_bytes(),
        'EFI/BOOT/BOOTX64.EFI':(base/'grub-dl/grubx64.efi').read_bytes(),
        'EFI/BOOT/grub.cfg':cfg,'boot/grub/grub.cfg':cfg,'EFI/debian/grub.cfg':cfg,
        'boot/grub/fonts/unicode.pf2':(base/'unifont-dl/unicode.pf2').read_bytes()}
gfiles.update(cvm.manager_files())
cvm.fat_disk(stage/'templates/grub-disk.img',96,gfiles)

# --- the zip: r399 binaries, r394's qemu/licenses/addons verbatim, templates, launcher ---
# zip-path -> source (Path to read, or bytes). qemu/ and the big trees are read
# straight from the r394 tree (no second copy on disk).
files={}
def add_tree(src_root,prefix):
    for p in src_root.rglob('*'):
        if p.is_file():files[prefix+'/'+p.relative_to(src_root).as_posix()]=p
for sub in ('qemu','licenses','components','Instalator','Dodatek-UEFI','Dodatki-syntezy'):
    add_tree(r394/sub,sub)
# r399: overlay the ABI-3 synth modules (eSpeak NG SetPersona -> espeakRANGE modulation)
add_tree(base/'synth-modules-r397/EFI/HAK/synths','Dodatki-syntezy/EFI/HAK/synths')
# swap in the r399 binaries
files['components/CHARON.EFI']=efi;files['components/CHARONVOICE.EFI']=voice;files['components/AudioDxe.efi']=audio
files['components/managers/memtest86plus.efi']=memtest
files['Dodatek-UEFI/EFI/HAK/CHARONVOICE.EFI']=voice
files['Instalator/EFI/HAK/CHARONVOICE.EFI']=voice;files['Instalator/EFI/HAK/HAKBOOT.EFI']=efi;files['Instalator/EFI/HAK/AudioDxe.efi']=audio
files['Instalator/EFI/HAK/tools/memtest.efi']=memtest
files['Instalator/Install-Charon.ps1']=addon/'Install-Charon.ps1'
files['Instalator/CharonEfiSign.ps1']=addon/'CharonEfiSign.ps1'
files['Instalator/zainstaluj-charona.sh']=addon/'zainstaluj-charona.sh'
# r399 corresponding source
csrc=base/'charon-src-r395';src_zip=stage/'charon-source-r399.zip'
with zipfile.ZipFile(src_zip,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for tree in ('src','tools','tests'):
        for p in sorted((csrc/tree).rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts:z.write(p,p.relative_to(csrc).as_posix())
files['licenses/corresponding-source/charon-source-r399.zip']=src_zip
files['licenses/corresponding-source/charon-r399-results.txt']=csrc/'tests/charon-r399-results.txt'
# templates + firmware-code
for t in ('templates/boot-disk.img','templates/grub-disk.img','templates/firmware-vars.fd','firmware-code.fd'):
    files[t]=stage/t
# launcher from r394 with the r399 release string
ps=(r394/'Uruchom-Charona.ps1').read_bytes().decode('utf-8-sig')
assert ps.count("$release = 'r394'")==1
ps_r399=bytes([0xef,0xbb,0xbf])+ps.replace("$release = 'r394'","$release = 'r399'").encode('utf-8')
(stage/'Uruchom-Charona.ps1').write_bytes(ps_r399);files['Uruchom-Charona.ps1']=stage/'Uruchom-Charona.ps1'
files['Uruchom-Charona.bat']=r394/'Uruchom-Charona.bat'
# CZYTAJ
czytaj=(r394/'CZYTAJ.txt').read_text(encoding='utf-8-sig')
new=(base/'czytaj-r399.txt').read_text(encoding='utf-8').rstrip()+'\n\n'
assert 'NOWE W r394' in czytaj
czytaj=czytaj.replace('Charon r394','Charon r399',1).replace('NOWE W r394',new+'NOWE W r394',1)
(stage/'CZYTAJ.txt').write_text(czytaj,encoding='utf-8-sig',newline='\r\n')
files['CZYTAJ.txt']=stage/'CZYTAJ.txt'

# manifest.json over the final content
def content_sha(src):
    if isinstance(src,(bytes,bytearray)):return hashlib.sha256(src).hexdigest()
    return sha(src)
manifest={'release':'r399','package':'testvm','files':{k:content_sha(v) for k,v in sorted(files.items())}}
man_bytes=(json.dumps(manifest,indent=2)+'\n').encode('utf-8')
# write the zip
tmp=out.with_suffix('.zip.tmp')
with zipfile.ZipFile(tmp,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for k,v in sorted(files.items()):
        if isinstance(v,(bytes,bytearray)):z.writestr('Charon/'+k,bytes(v))
        else:z.write(v,'Charon/'+k)
    z.writestr('Charon/manifest.json',man_bytes)
with zipfile.ZipFile(tmp) as z:assert z.testzip() is None
os.replace(tmp,out)
print('zip',out,out.stat().st_size,sha(out)[:16])
