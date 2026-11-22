"""Build outputs/Charon-r408.zip: the INSTALL/UPDATE package (no demo VM).

Starts from the released r404 stage (charon-release-r404-install) and overlays
the r408 binaries, installers, the driver pack, the CharonDOS package
(CHARONDOS.EFI r408 with the CHARON command + CHARONPC.EFI r407zf), the
programs for the running system (\\EFI\\HAK\\os), licences, source and READMEs.

  python package-charon-r408.py     (env R408_AUDIO / R408_DRIVERS override the audio driver / driver pack)
"""
from pathlib import Path
import hashlib,json,shutil,zipfile,os
base=Path(__file__).resolve().parent
prev=base/'charon-release-r404-install/Charon'
stage=base/'charon-release-r408-install/Charon'
efi=base/'charon-win-r408final/CHARON.EFI';voice=base/'charon-win-r408final-drv/CHARONVOICE.EFI'
audio=Path(os.environ.get('R408_AUDIO',base/'charon-audio-driver-r408/AudioDxe-r408.efi'))
charonsrc=base/'charon-src-r395';addon=charonsrc/'tools/charon-addon';ostools=charonsrc/'tools/charon-os'
drivers=Path(os.environ.get('R408_DRIVERS',base/'charon-drivers-r408'))
dosefi=base/'charondos-r408/CHARONDOS.EFI';pcefi=base/'charon-pc-build-r407zf/CHARONPC.EFI'
pc=base/'charon-pc-stage-r406i'          # licences of the emulator (Bochs 3.1, musl, libc++: unchanged since r403)
dos=base/'pkg-r408-dos/charon-packages/CharonDOS'
gpl2=base/'charon-release-r394/Charon/licenses/qemu-as-shipped/COPYING'
out=base.parent/'outputs/Charon-r408.zip'

def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()

assert not stage.exists(),stage
assert not out.exists(),out
for p in (prev,efi,voice,audio,drivers,dosefi,pcefi,pc/'LICENCES.txt',gpl2,ostools/'windows',ostools/'linux'):assert p.exists(),p
# The CharonDOS package: CHARONDOS.EFI + the PC emulator and its licences
# (the installer copies the whole folder onto a CharonDOS system).
if not dos.exists():
    shutil.copytree(base/'pkg-r403final-dos/charon-packages/CharonDOS',dos)
    shutil.copyfile(dosefi,dos/'CHARONDOS.EFI')
    shutil.copyfile(pcefi,dos/'CHARONPC.EFI')
    shutil.copyfile(pc/'LICENCES.txt',dos/'CHARONPC-LICENCES.txt')
    shutil.rmtree(dos/'licences');shutil.copytree(pc/'licences',dos/'licences')
    shutil.copyfile(charonsrc/'src/pc/WIN9X-READERS.txt',dos/'WIN9X-READERS.txt')
    shutil.copyfile(charonsrc/'src/dos/CHARON-COMMANDS.txt',dos/'CHARON-COMMANDS.txt')
shutil.copytree(prev,stage)
(stage/'manifest.json').unlink()
# Parts the r404 stage carries are replaced, not added twice.
for old in ('Instalator/EFI/HAK/addon','Instalator/EFI/HAK/drivers','Instalator/EFI/HAK/packages/CharonDOS','Instalator/EFI/HAK/os','OS-tools'):
    if (stage/old).exists():shutil.rmtree(stage/old)
cs=stage/'licenses/corresponding-source'
for old in list(cs.glob('charon-r40*-results.txt'))+list(cs.glob('charon-source-r40*.zip')):old.unlink()
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
# Driver pack: binaries, manifest and documents onto the medium; its source tree
# goes to the corresponding source, its licence texts also to licenses\.
shutil.copytree(drivers,hak/'drivers',ignore=shutil.ignore_patterns('source','__pycache__'))
lic=stage/'licenses'
if (lic/'driver-pack').exists():shutil.rmtree(lic/'driver-pack')
if (drivers/'licences').exists():shutil.copytree(drivers/'licences',lic/'driver-pack')
for n in ('DRIVER-PACK.txt','NETWORK-DRIVERS.txt'):
    if (drivers/n).exists():
        (lic/'driver-pack').mkdir(exist_ok=True);shutil.copyfile(drivers/n,lic/'driver-pack'/n)
if (cs/'driver-pack-source.zip').exists():(cs/'driver-pack-source.zip').unlink()
if (drivers/'source').exists():
    with zipfile.ZipFile(cs/'driver-pack-source.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted((drivers/'source').rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts:z.write(p,p.relative_to(drivers/'source').as_posix())
shutil.copytree(dos,hak/'packages/CharonDOS')
# Programs for the running system: on the medium (the installers copy \EFI\HAK) and as a folder.
for sub in ('windows','linux'):
    shutil.copytree(ostools/sub,hak/'os'/sub,ignore=shutil.ignore_patterns('__pycache__'))
    shutil.copytree(ostools/sub,stage/'OS-tools'/sub,ignore=shutil.ignore_patterns('__pycache__'))
shutil.copyfile(ostools/'README.txt',stage/'OS-tools/README.txt')
shutil.copyfile(charonsrc/'src/efi/WORKSPACE.txt',stage/'WORKSPACE.txt')
# Licences of the changed parts.
if (lic/'charonpc').exists():shutil.rmtree(lic/'charonpc')
(lic/'charonpc').mkdir()
shutil.copyfile(pc/'LICENCES.txt',lic/'charonpc/LICENCES.txt')
for p in (pc/'licences').glob('*'):shutil.copyfile(p,lic/'charonpc'/p.name)
shutil.copyfile(gpl2,lic/'audio-driver/GPL-2.0.txt')
shutil.copyfile(charonsrc/'src/efi/AUDIO-CODECS.txt',lic/'audio-driver/AUDIO-CODECS.txt')
(lic/'audio-driver/R408-LICENCE-NOTE.txt').write_text('''AudioDxe.efi of Charon r408
===========================
Since r408 the sound driver contains codec and machine tables written from
files of the Linux kernel's sound/hda tree that are licensed
GPL-2.0-or-later (list: AUDIO-CODECS.txt, section 1). The driver binary as a
whole is therefore distributed under the GNU General Public License,
version 2 or (at your option) any later version (GPL-2.0.txt). The parts
taken from OpenCorePkg / AUDK keep their BSD and MIT notices (the other
files of this folder). Up to r407 the binary was BSD/MIT only.

Corresponding source: corresponding-source\\charon-source-r408.zip -
src/efi/audio_stream/driver (the added files and apply-driver-extension.py,
which lists every change to the upstream driver), tools/gen-charon-hda-quirks.py
(generates the tables), and the pinned upstream archives named in
source-downloads.json and BUILD-NOTES.md.
Switch for trouble on real hardware: an empty file \\EFI\\HAK\\audio-noquirks
turns the machine table off.
''',encoding='utf-8',newline='\r\n')
with (lic/'COMPONENTS-AND-SOURCES.txt').open('a',encoding='utf-8',newline='') as f:
    f.write('\r\nAdded in r408\r\n'
            '- AudioDxe.efi: GPL-2.0-or-later as a whole since r408 (audio-driver\\R408-LICENCE-NOTE.txt).\r\n'
            '- UEFI driver pack (\\EFI\\HAK\\drivers): licence texts in driver-pack\\, list of every driver, its origin\r\n'
            '  and licence in driver-pack\\DRIVER-PACK.txt and NETWORK-DRIVERS.txt, sources and build scripts in\r\n'
            '  corresponding-source\\driver-pack-source.zip.\r\n'
            '- Programs for the running system (OS-tools\\, \\EFI\\HAK\\os): GPL-2.0-or-later, source = the files themselves.\r\n'
            '- NVDA inside Windows Setup: Charon does not ship NVDA; it downloads the installer from nvaccess.org. The\r\n'
            '  helper programs Charon writes to \\charon\\pe are built from src/windows (GPL-2.0-or-later).\r\n')
with zipfile.ZipFile(cs/'charon-source-r408.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for tree in ('src','tools','tests','third_party'):
        for p in sorted((charonsrc/tree).rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts and b'Users\\tito' not in p.read_bytes():z.write(p,p.relative_to(charonsrc).as_posix())
for p in sorted((charonsrc/'tests').glob('charon-r40[4-8]*-results.txt')):shutil.copyfile(p,cs/p.name)
assert (cs/'charon-r408-results.txt').exists()
# READMEs (English only).
rd=base/'readme-r408'
def put(src,dst):dst.write_bytes(('﻿'+src.read_text(encoding='utf-8').replace('\r\n','\n').replace('\n','\r\n')).encode('utf-8'))
put(rd/'release-README.txt',stage/'README.txt')
put(rd/'installer-README.txt',stage/'Instalator/README.txt')
put(rd/'addon-README.txt',stage/'Dodatek-UEFI/README.txt')
bad=[str(p.relative_to(stage)) for p in stage.rglob('*') if p.is_file() and p.suffix.lower() in ('.txt','.ps1','.sh','.py','.cmd','.bat','.json','.md','') and p.stat().st_size<4<<20 and (b'Users\\tito' in p.read_bytes() or (b'Users/'+b'tito') in p.read_bytes())]
assert not bad,bad[:10]
files=sorted(p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file())
manifest={'release':'r408','package':'install',
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
