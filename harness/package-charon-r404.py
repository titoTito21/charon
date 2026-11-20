"""Build outputs/Charon-r404.zip: the INSTALL/UPDATE package (no demo VM).

Starts from the released r402 stage (charon-release-r402-install) and overlays
the r404 binaries, installers, the CharonDOS package (CHARONDOS.EFI r404 +
CHARONPC.EFI, the Bochs-based PC emulator), licences, source and READMEs.

  python package-charon-r404.py            (env R404_AUDIO=path overrides the audio driver)
"""
from pathlib import Path
import hashlib,json,shutil,zipfile,os
base=Path(__file__).resolve().parent
r403=base/'charon-release-r403-install/Charon'
stage=base/'charon-release-r404-install/Charon'
efi=base/'charon-win-r404final/CHARON.EFI';voice=base/'charon-win-r404final-drv/CHARONVOICE.EFI'
audio=Path(os.environ.get('R404_AUDIO',base/'charon-audio-driver-r404/AudioDxe-r404.efi'))
charonsrc=base/'charon-src-r395';addon=charonsrc/'tools/charon-addon'
drivers=base/'charon-drivers-r400';dos=base/'pkg-r403final-dos/charon-packages/CharonDOS'
pc=base/'charon-pc-stage-r403'
out=base.parent/'outputs/Charon-r404.zip'

def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()

assert not stage.exists(),stage
assert not out.exists(),out
for p in (efi,voice,audio,drivers,base/'charondos-r403b/CHARONDOS.EFI',pc/'CHARONPC.EFI'):assert p.exists(),p
# The CharonDOS package: CHARONDOS.EFI + the PC emulator and its licences
# (the installer copies the whole folder onto a CharonDOS system).
if not dos.exists():
    shutil.copytree(base/'pkg-r403-dos/charon-packages/CharonDOS',dos)
    shutil.copyfile(base/'charondos-r403b/CHARONDOS.EFI',dos/'CHARONDOS.EFI')
    shutil.copyfile(pc/'CHARONPC.EFI',dos/'CHARONPC.EFI')
    shutil.copyfile(pc/'LICENCES.txt',dos/'CHARONPC-LICENCES.txt')
    shutil.copytree(pc/'licences',dos/'licences')
    shutil.copyfile(charonsrc/'src/pc/WIN9X-READERS.txt',dos/'WIN9X-READERS.txt')
shutil.copytree(r403,stage)
(stage/'manifest.json').unlink()
# Parts the r402 stage carries are replaced, not added twice.
for old in ('Instalator/EFI/HAK/addon','Instalator/EFI/HAK/drivers','Instalator/EFI/HAK/packages/CharonDOS'):
    if (stage/old).exists():shutil.rmtree(stage/old)
cs=stage/'licenses/corresponding-source'
for old in list(cs.glob('charon-r403-results.txt'))+list(cs.glob('charon-source-r403.zip')):old.unlink()
# Binaries everywhere they are shipped.
shutil.copyfile(efi,stage/'components/CHARON.EFI')
shutil.copyfile(audio,stage/'components/AudioDxe.efi')
for dst in ('components/CHARONVOICE.EFI','Dodatek-UEFI/EFI/HAK/CHARONVOICE.EFI','Instalator/EFI/HAK/CHARONVOICE.EFI'):
    shutil.copyfile(voice,stage/dst)
shutil.copyfile(efi,stage/'Instalator/EFI/HAK/HAKBOOT.EFI')
for dst in ('Instalator/EFI/HAK/AudioDxe.efi','Dodatek-UEFI/EFI/HAK/AudioDxe.efi'):
    if (stage/dst).exists():shutil.copyfile(audio,stage/dst)
for n in ('Install-Charon.ps1','CharonEfiSign.ps1','zainstaluj-charona.sh','Zainstaluj-Charona.bat','Odinstaluj-Charona.bat'):
    shutil.copyfile(addon/n,stage/'Instalator'/n)
for n in ('Install-CharonAddon.ps1','CharonEfiSign.ps1','zainstaluj-dodatek-uefi.sh','Zainstaluj-dodatek-UEFI.bat','Odinstaluj-dodatek-UEFI.bat'):
    shutil.copyfile(addon/n,stage/'Dodatek-UEFI'/n)
hak=stage/'Instalator/EFI/HAK'
(hak/'addon').mkdir()
for p in sorted(addon.glob('*')):
    if p.is_file():shutil.copyfile(p,hak/'addon'/p.name)
shutil.copytree(drivers,hak/'drivers')
shutil.copytree(dos,hak/'packages/CharonDOS')
shutil.copyfile(charonsrc/'src/efi/WORKSPACE.txt',stage/'WORKSPACE.txt')
# Licences of the new parts.
lic=stage/'licenses'
if (lic/'charonpc').exists():shutil.rmtree(lic/'charonpc')
(lic/'charonpc').mkdir()
shutil.copyfile(pc/'LICENCES.txt',lic/'charonpc/LICENCES.txt')
for p in (pc/'licences').glob('*'):shutil.copyfile(p,lic/'charonpc'/p.name)
with zipfile.ZipFile(cs/'charon-source-r404.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for tree in ('src','tools','tests','third_party'):
        for p in sorted((charonsrc/tree).rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts and b'Users\\tito' not in p.read_bytes():z.write(p,p.relative_to(charonsrc).as_posix())
shutil.copyfile(charonsrc/'tests/charon-r404-results.txt',cs/'charon-r404-results.txt')
# READMEs (English only).
rd=base/'readme-r404'
def put(src,dst):dst.write_bytes(('﻿'+src.read_text(encoding='utf-8').replace('\r\n','\n').replace('\n','\r\n')).encode('utf-8'))
put(rd/'release-README.txt',stage/'README.txt')
put(rd/'installer-README.txt',stage/'Instalator/README.txt')
put(rd/'addon-README.txt',stage/'Dodatek-UEFI/README.txt')
files=sorted(p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file())
manifest={'release':'r404','package':'install',
          'inputs_sha256':{'charon':sha(efi),'voice_driver':sha(voice),'audio_driver':sha(audio),
                           'charondos':sha(dos/'CHARONDOS.EFI'),'charonpc':sha(dos/'CHARONPC.EFI')},
          'files':{f:sha(stage/f) for f in files}}
(stage/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
files=sorted(set(files)|{'manifest.json'})
tmp=out.with_suffix('.zip.tmp')
with zipfile.ZipFile(tmp,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for f in files:z.write(stage/f,'Charon/'+f)
with zipfile.ZipFile(tmp) as z:assert z.testzip() is None
os.replace(tmp,out)
print('zip',out,out.stat().st_size,sha(out)[:16])
