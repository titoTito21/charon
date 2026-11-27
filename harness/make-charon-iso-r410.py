"""r410: work/charon-release-r410-outputs/Charon-r410.iso - a bootable UEFI CD (El Torito,
platform EFI, no emulation) holding ONLY Charon: the boot FAT image with EFI/BOOT/BOOTX64.EFI
(Charon) and EFI/HAK (voice driver, HDA audio driver, speaking Shell, Memtest86+, cues,
eSpeak NG / RHVoice add-ons), plus the licences of what is on the disc.

r410 adaptation of make-charon-iso-r409.py; staging only (output to work/, not ..\\outputs).

The FAT image is larger than El Torito's 16-bit 512-byte load size can describe
(32 MB), so the catalogue's sector count is set to 1: edk2 PartitionDxe (ElTorito.c,
"When the SectorCount < 2, set the Partition as the whole CD") then exposes the
image from its LBA to the end of the disc; the FAT driver uses its own BPB size.
That range starts at the image and holds all of it (trailing licence files are ignored).
An MBR in the ISO system area (type 0xEF over the same image) makes the file
bootable when written raw to a USB stick (isohybrid-style); CD firmware uses El Torito.

  python make-charon-iso-r410.py
"""
import hashlib,io,os,struct,sys
from pathlib import Path
base=Path(__file__).resolve().parent
sys.path[:0]=[str(base/'iso-deps'),str(base/'fat-image-deps'),str(base/'fat-legacy-deps')]
import pycdlib
from pyfatfs import PyFat
from pyfatfs.PyFatFS import PyFatFS
rel=base/'charon-release-r410-install/Charon'
efi=base/'charon-src-r395/charon-win-r410-rel/CHARON.EFI';voice=base/'charon-src-r395/charon-win-r410-rel-drv/CHARONVOICE.EFI'
syn=base/'synth-modules-r410a/EFI/HAK/synths'   # all 13 RHVoice voices
out=base/'charon-release-r410-outputs/Charon-r410.iso';fatimg=base/'charon-iso-r410-efiboot.img'
assert not out.exists() and not fatimg.exists(),(out,fatimg)

# --- boot FAT: Charon alone (no installer copy HAKBOOT.EFI) ---
files={'EFI/BOOT/BOOTX64.EFI':efi.read_bytes(),'EFI/HAK/CHARONVOICE.EFI':voice.read_bytes(),
       'EFI/HAK/audio-language':b'en'}   # English by default (Settings -> Language changes it)
hak=rel/'Instalator/EFI/HAK'
for p in sorted(hak.rglob('*')):
    r=p.relative_to(hak).as_posix()
    if p.is_file() and r not in ('HAKBOOT.EFI','CHARONVOICE.EFI'):files['EFI/HAK/'+r]=p.read_bytes()
for p in sorted(syn.rglob('*')):
    if p.is_file():files['EFI/HAK/synths/'+p.relative_to(syn).as_posix()]=p.read_bytes()
need=sum(len(v) for v in files.values())
size_mb=max(64,(need*115//100)//(1<<20)+4)
with open(fatimg,'wb') as f:f.truncate(size_mb<<20)
fat=PyFat.PyFat();fat.mkfs(str(fatimg),fat_type=PyFat.PyFat.FAT_TYPE_FAT32,size=size_mb<<20,label='CHARON');fat.close()
with PyFatFS(str(fatimg)) as fs:
    for name,data in files.items():
        parts=name.split('/')
        for i in range(1,len(parts)):
            d='/'.join(parts[:i])
            if not fs.exists(d):fs.makedir(d)
        with fs.openbin(name,'w') as o:o.write(data)
    for name,data in files.items():
        with fs.openbin(name,'r') as o:assert o.read()==data,name
print('fat',size_mb,'MB,',len(files),'files,',need,'bytes')

# --- ISO9660 + Joliet: licences + the boot image ---
iso=pycdlib.PyCdlib();iso.new(interchange_level=3,joliet=3,vol_ident='CHARON_R400',app_ident_str='Charon r410 speaking UEFI boot manager')
seq=[0]
def short(name):
    stem,_,ext=name.upper().rpartition('.') if '.' in name else (name.upper(),'','')
    import re
    stem=re.sub(r'[^A-Z0-9_]','_',stem)[:24] or 'F';ext=re.sub(r'[^A-Z0-9_]','_',ext)[:3]
    seq[0]+=1;return f'{stem}_{seq[0]}'+('.'+ext if ext else '')
made=set()
def mkdir(jpath):
    if jpath in made or jpath=='/':return isomap.get(jpath,'/')
    parent='/'.join(jpath.split('/')[:-1]) or '/'
    ip=mkdir(parent);name=jpath.split('/')[-1]
    iname=(ip.rstrip('/')+'/'+short(name).split('.')[0][:31])
    iso.add_directory(iname,joliet_path=jpath);made.add(jpath);isomap[jpath]=iname;return iname
isomap={'/':'/'}
def addfile(jpath,data):
    parent='/'.join(jpath.split('/')[:-1]) or '/'
    ip=mkdir(parent);name=jpath.split('/')[-1]
    iso.add_fp(io.BytesIO(data),len(data),(ip.rstrip('/')+'/'+short(name)+';1'),joliet_path=jpath)
keep=('audio-driver','bearssl','charondos','charonpc','driver-pack','nvda-controller-client','network-drivers','charon-shell','dejavu-fonts','hak-and-nvda','liberation-fonts','memtest86plus','musl','stb_truetype','tvox','unifont')
for d in keep:
    if not (rel/'licenses'/d).exists():continue
    for p in sorted((rel/'licenses'/d).rglob('*')):
        if p.is_file():addfile('/licenses/'+d+'/'+p.relative_to(rel/'licenses'/d).as_posix(),p.read_bytes())
addfile('/licenses/COMPONENTS-AND-SOURCES.txt',(rel/'licenses/COMPONENTS-AND-SOURCES.txt').read_bytes())
for p in sorted((syn/'licences').rglob('*')):
    if p.is_file():addfile('/licenses/synth-addons/'+p.relative_to(syn/'licences').as_posix(),p.read_bytes())
for n in ('LICENCES.txt','RHVOICE-VOICES.txt','rhvoice-voices.tsv'):addfile('/licenses/synth-addons/'+n,(syn/n).read_bytes())
readme='''Charon r410 - speaking UEFI boot manager (boot disc)

Boot this disc in UEFI mode (firmware boot menu, e.g. F12, F8 or Esc).
The ISO file can also be written raw to a USB stick (e.g. Rufus in DD mode).

Read-only disc: changed Charon settings are not stored on a CD (they are on a USB stick).
Secure Boot: Charon is not signed by Microsoft - turn Secure Boot off, or install
Charon with Charon-Installer-r410.zip (signs it for this computer).

Licences: licenses\\. Source code: the Charon repository (charon-github-r410).
'''.replace('\n','\r\n').encode('ascii')
addfile('/README.TXT',readme)
fb=fatimg.read_bytes()
iso.add_fp(io.BytesIO(fb),len(fb),'/EFIBOOT.IMG;1',joliet_path='/efiboot.img')
iso.add_eltorito('/EFIBOOT.IMG;1',bootcatfile='/BOOT.CAT;1',efi=True,media_name='noemul',joliet_bootcatfile='/boot.cat')
iso.eltorito_boot_catalog.initial_entry.sector_count=1
_v=iso.eltorito_boot_catalog.validation_entry;_v._initialized=False;_v.new(0xEF)   # UEFI platform (pycdlib defaults to x86); recomputes the checksum
tmp=out.with_suffix('.iso.tmp');iso.write(str(tmp));iso.close()

# --- patch: catalogue sector count 1 (whole remaining disc), image last, hybrid MBR ---
with open(tmp,'r+b') as f:
    data=bytearray(f.read())
    cat=None
    for s in range(16,40):
        vd=data[s*2048:(s+1)*2048]
        if vd[0]==0 and vd[1:6]==b'CD001' and vd[7:30]==b'EL TORITO SPECIFICATION':cat=struct.unpack_from('<I',vd,0x47)[0];break
    assert cat is not None,'no boot record'
    c=cat*2048
    assert data[c]==1 and data[c+1]==0xEF and data[c+30:c+32]==b'\x55\xaa',data[c:c+32].hex()
    e=c+32;assert data[e]==0x88,data[e:e+32].hex()
    img_lba=struct.unpack_from('<I',data,e+8)[0]
    assert data[img_lba*2048:img_lba*2048+len(fb)]==fb,'boot image not at its LBA'
    total=len(data)//2048
    tail=total-img_lba-(len(fb)+2047)//2048
    print('boot image LBA',img_lba,'of',total,'(whole-disc partition holds the full FAT; BPB sets its size)')
    struct.pack_into('<H',data,e+6,1)
    start=img_lba*4;count=len(fb)//512
    mbr=bytearray(512)
    mbr[446:462]=struct.pack('<B3sB3sII',0x80,b'\xfe\xff\xff',0xEF,b'\xfe\xff\xff',start,count)
    mbr[440:444]=hashlib.sha256(fb).digest()[:4];mbr[510:512]=b'\x55\xaa'
    assert data[:512]==bytes(512);data[:512]=mbr
    f.seek(0);f.write(data)
os.replace(tmp,out)
h=hashlib.sha256(out.read_bytes()).hexdigest()
(out.with_name(out.name+'.sha256')).write_text(f'{h}  {out.name}\n',encoding='ascii')
fatimg.unlink()
import importlib.util as _u
_s=_u.spec_from_file_location('scanprivate',base/'scan-private-r409.py');scanprivate=_u.module_from_spec(_s);_s.loader.exec_module(scanprivate)
if scanprivate.main([str(out)]):raise SystemExit('private strings in the ISO')
print('iso',out,out.stat().st_size,h[:16])
