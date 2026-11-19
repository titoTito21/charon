"""Build outputs/Charon-r399.zip as an INSTALL/UPDATE package (self-contained).

The r394 release tree's QEMU demo VM (templates/*.img, qemu/ ~1.2 GB) was deleted
to free disk space, so this packages only what installs Charon on a real machine:
binaries, the installer (Instalator/), the UEFI add-on (Dodatek-UEFI/), optional
synthesizers (Dodatki-syntezy/), licenses + r399 corresponding source, and CZYTAJ.
The built-in Memtest86+ is shipped for the installer (Instalator/EFI/HAK/tools).

  python package-charon-r399.py
"""
from pathlib import Path
import hashlib,json,shutil,sys,zipfile,os
base=Path(__file__).resolve().parent
r394=base/'charon-release-r394/Charon'
stage=base/'charon-release-r399-install/Charon'
efi=base/'charon-win-r399/CHARON.EFI';voice=base/'charon-win-r399-drv/CHARONVOICE.EFI'
audio=base/'charon-audio-driver-r389/AudioDxe-r389.efi';memtest=base/'memtest-dl/x/mt86p_900_x86_64'
charonsrc=base/'charon-src-r395';addon=charonsrc/'tools/charon-addon'
out=base.parent/'outputs/Charon-r399.zip'

def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()

assert not stage.exists(),stage
assert not out.exists(),out
stage.mkdir(parents=True)
# Subtrees that install Charon (no qemu/, no templates/, no launcher demo).
for d in ('components','Instalator','Dodatek-UEFI','Dodatki-syntezy','licenses'):
    shutil.copytree(r394/d,stage/d)
# r399: ship the ABI-3 synth modules (eSpeak NG honours SetPersona -> espeakRANGE,
# so the reader's modulation reaches the default external voice, not only TVox).
_syn=stage/'Dodatki-syntezy/EFI/HAK/synths'
if _syn.exists():shutil.rmtree(_syn)
shutil.copytree(base/'synth-modules-r397/EFI/HAK/synths',_syn)
# r399 binaries everywhere they are shipped.
shutil.copyfile(efi,stage/'components/CHARON.EFI')
shutil.copyfile(audio,stage/'components/AudioDxe.efi')
for dst in ('components/CHARONVOICE.EFI','Dodatek-UEFI/EFI/HAK/CHARONVOICE.EFI','Instalator/EFI/HAK/CHARONVOICE.EFI'):
    shutil.copyfile(voice,stage/dst)
shutil.copyfile(efi,stage/'Instalator/EFI/HAK/HAKBOOT.EFI')
shutil.copyfile(audio,stage/'Instalator/EFI/HAK/AudioDxe.efi')
shutil.copyfile(memtest,stage/'components/managers/memtest86plus.efi')
# Built-in Memtest86+ for the installer to deploy (\EFI\HAK\tools\memtest.efi).
(stage/'Instalator/EFI/HAK/tools').mkdir(parents=True,exist_ok=True)
shutil.copyfile(memtest,stage/'Instalator/EFI/HAK/tools/memtest.efi')
# Refresh the installer scripts (r399: installs the Memtest tool in Manager/Shim modes).
for n in ('Install-Charon.ps1','CharonEfiSign.ps1','zainstaluj-charona.sh','CZYTAJ-DODATEK.txt'):
    if (addon/n).exists():shutil.copyfile(addon/n,stage/'Instalator'/n if (stage/'Instalator'/n).exists() or n!='CZYTAJ-DODATEK.txt' else stage/'Instalator'/n)
shutil.copyfile(addon/'Install-Charon.ps1',stage/'Instalator/Install-Charon.ps1')
shutil.copyfile(addon/'CharonEfiSign.ps1',stage/'Instalator/CharonEfiSign.ps1')
shutil.copyfile(addon/'zainstaluj-charona.sh',stage/'Instalator/zainstaluj-charona.sh')
# r399 corresponding source (GPL) + results.
cs=stage/'licenses/corresponding-source'
with zipfile.ZipFile(cs/'charon-source-r399.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for tree in ('src','tools','tests'):
        for p in sorted((charonsrc/tree).rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts:z.write(p,p.relative_to(charonsrc).as_posix())
shutil.copyfile(charonsrc/'tests/charon-r399-results.txt',cs/'charon-r399-results.txt')
# CZYTAJ: r399 changelog + what this package is.
head=(base/'czytaj-r399.txt').read_text(encoding='utf-8')
note=("CHARON r399 - pakiet instalacyjny\n"
      "=================================\n\n"
      "Ten pakiet instaluje Charona na prawdziwym komputerze. Zawiera binaria,\n"
      "instalator (folder Instalator), dodatek UEFI (Dodatek-UEFI), opcjonalne\n"
      "syntezatory (Dodatki-syntezy), licencje i kod zrodlowy r399.\n\n"
      "Instalacja: uruchom Instalator\\Zainstaluj-Charona.bat (jako administrator)\n"
      "albo Instalator\\Install-Charon.ps1. Dodatek samej mowy: Dodatek-UEFI.\n\n"
      "UWAGA: demo w QEMU (Uruchom-Charona) nie jest dolaczone w tym wydaniu -\n"
      "jego obrazy dyskow i QEMU (~1,2 GB) zostaly wczesniej usuniete dla miejsca.\n"
      "Mozna je odtworzyc na zyczenie.\n\n")
(stage/'CZYTAJ.txt').write_text(note+head,encoding='utf-8',newline='\r\n')
# Simple manifest with file hashes.
files=sorted(p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file())
manifest={'release':'r399','package':'install',
          'inputs_sha256':{'charon':sha(efi),'voice_driver':sha(voice),'audio_driver':sha(audio),'memtest':sha(memtest)},
          'files':{f:sha(stage/f) for f in files}}
(stage/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
files=sorted(set(files)|{'manifest.json'})
tmp=out.with_suffix('.zip.tmp')
with zipfile.ZipFile(tmp,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for f in files:z.write(stage/f,'Charon/'+f)
with zipfile.ZipFile(tmp) as z:assert z.testzip() is None
os.replace(tmp,out)
print('zip',out,out.stat().st_size,sha(out)[:16])
