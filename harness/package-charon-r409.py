"""Build outputs/Charon-r409.zip: the INSTALL/UPDATE package (no demo VM).

Starts from the released r408 stage (charon-release-r408-install) and overlays the r409
binaries, installers, the Windows add-on folder, the driver pack, the CharonDOS package
(CHARONDOS.EFI r409 with LINUX / WINNT + CHARONPC.EFI r407zf), the programs for the running
system, the synthesizer add-ons with every RHVoice voice (package-synths-r409.py), licences,
source and READMEs. Inputs are the files prepare-release-inputs-r409.py left (no account name
of the build computer in any of them); the stage is scanned whole before the zip is written.

  python package-charon-r409.py [--stage-only]
"""
from pathlib import Path
import hashlib,importlib.util,json,shutil,sys,zipfile,os,csv
base=Path(__file__).resolve().parent
def load(name):
    spec=importlib.util.spec_from_file_location(name.replace('-','_'),base/(name+'.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
scrub=load('scrub-private-r409');scan=load('scan-private-r409');synths=load('package-synths-r409')
prev=base/'charon-release-r408-install/Charon'
stage=base/'charon-release-r409-install/Charon'
efi=base/'charon-win-r409final/CHARON.EFI';voice=base/'charon-win-r409final-drv/CHARONVOICE.EFI'
audio=base/'charon-audio-driver-r409/AudioDxe-r409.efi'
charonsrc=base/'charon-src-r395';addon=charonsrc/'tools/charon-addon';ostools=charonsrc/'tools/charon-os'
drivers=base/'charon-drivers-r409'
dosefi=base/'charondos-r409/CHARONDOS.EFI';pcefi=base/'charon-pc-build-r407zf/CHARONPC.EFI'
pc=base/'charon-pc-stage-r406i'          # licences of the emulator (Bochs 3.1, musl, libc++: unchanged since r403)
dos=base/'pkg-r409-dos/charon-packages/CharonDOS'
gpl2=base/'charon-release-r394/Charon/licenses/qemu-as-shipped/COPYING'
out=base.parent/'outputs/Charon-r409.zip'
specs=('charon-multiboot-spec-2026-11-23.md','charon-detailed-questions-spec-2026-11-23.md','charon-windows-profiles-research-2026-11-23.md','charon-older-windows-research-2026-11-23.md')

def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()

assert not stage.exists(),stage
assert not out.exists(),out
for p in (prev,efi,voice,audio,drivers,dosefi,pcefi,pc/'LICENCES.txt',gpl2,ostools/'windows',ostools/'linux',charonsrc/'tests/charon-r409-results.txt'):assert p.exists(),p
assert os.environ.get('R409_CANDIDATE') or '@@' not in (charonsrc/'tests/charon-r409-results.txt').read_text(encoding='utf-8'),'the results file is not complete'
for n in ('Charon-Network.ps1','nvdaControllerClient.dll','nvdaControllerClient-LICENSE.txt','Install-Charon.ps1','Install-CharonAddon.ps1','Charon-WindowsProfile.ps1','Charon-Readers.ps1','CHARON-WINDOWS-PROFILES.txt'):assert (addon/n).exists(),n
# The CharonDOS package: CHARONDOS.EFI + the PC emulator and its licences (the installer copies the whole
# folder onto a CharonDOS system). Made from the r408 package; the program and the two documents are
# always taken again from the build and the source tree.
if not dos.exists():
    shutil.copytree(base/'pkg-r408-dos/charon-packages/CharonDOS',dos)
    shutil.copyfile(pcefi,dos/'CHARONPC.EFI')
    shutil.copyfile(pc/'LICENCES.txt',dos/'CHARONPC-LICENCES.txt')
    shutil.rmtree(dos/'licences');shutil.copytree(pc/'licences',dos/'licences')
shutil.copyfile(dosefi,dos/'CHARONDOS.EFI')
shutil.copyfile(charonsrc/'src/pc/WIN9X-READERS.txt',dos/'WIN9X-READERS.txt')
shutil.copyfile(charonsrc/'src/dos/CHARON-COMMANDS.txt',dos/'CHARON-COMMANDS.txt')
assert sha(dos/'CHARONPC.EFI')==sha(pcefi)
shutil.copytree(prev,stage)
(stage/'manifest.json').unlink()
# Parts the r408 stage carries are replaced, not added twice.
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
shutil.copytree(drivers/'licences',lic/'driver-pack')
for n in ('DRIVER-PACK.txt','NETWORK-DRIVERS.txt'):shutil.copyfile(drivers/n,lic/'driver-pack'/n)
(cs/'driver-pack-source.zip').unlink()
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
# Synthesizer add-ons: eSpeak NG and RHVoice with all 13 voices, and their licence set.
addon_bytes=synths.put_addons(stage)
# Licences of the changed parts.
ad=lic/'audio-driver'
(ad/'R408-LICENCE-NOTE.txt').unlink()
shutil.copyfile(gpl2,ad/'GPL-2.0.txt')
shutil.copyfile(charonsrc/'src/efi/AUDIO-CODECS.txt',ad/'AUDIO-CODECS.txt')
shutil.copyfile(charonsrc/'src/efi/audio_stream/driver/BUILD-NOTES.md',ad/'BUILD-NOTES.md')
(ad/'R409-LICENCE-NOTE.txt').write_text('''AudioDxe.efi of Charon r409
===========================
Licence. Since r408 the sound driver contains codec and machine tables written
from files of the Linux kernel's sound/hda tree that are licensed
GPL-2.0-or-later (list: AUDIO-CODECS.txt, section 1). The driver binary as a
whole is therefore distributed under the GNU General Public License,
version 2 or (at your option) any later version (GPL-2.0.txt). The parts
taken from OpenCorePkg / AUDK keep their BSD and MIT notices (the other
files of this folder). Up to r407 the binary was BSD/MIT only.

Changed in r409. After the stream is started the driver reads the hardware
cursor once and, when it has not moved yet, takes its poll clock there
instead of before the start (HakAudioStream.c). The time a virtual machine
needs to create its host stream no longer counts against the driver's 70 ms
limit. Nothing else differs from the r408 driver. It has not been run on a
physical sound card.

The r408 driver is not part of this package. It is the state of
src/efi/audio_stream/driver before that change (the source of r408:
charon-source-r408.zip of the r408 release, or the history of the source
repository); BUILD-NOTES.md says how the driver is built.

Corresponding source: corresponding-source\\charon-source-r409.zip -
src/efi/audio_stream/driver (the added files and apply-driver-extension.py,
which lists every change to the upstream driver), tools/gen-charon-hda-quirks.py
(generates the tables), and the pinned upstream archives named in
source-downloads.json and BUILD-NOTES.md.
Switch for trouble on real hardware: an empty file \\EFI\\HAK\\audio-noquirks
turns the machine table off.
''',encoding='utf-8',newline='\r\n')
nv=lic/'nvda-controller-client';nv.mkdir()
shutil.copyfile(addon/'nvdaControllerClient-LICENSE.txt',nv/'LGPL-2.1.txt')
(nv/'README.txt').write_text('''nvdaControllerClient.dll (\\EFI\\HAK\\addon)
==========================================
NV Access's controller client for NVDA, x64, unchanged, from
https://download.nvaccess.org/releases/2026.3/nvda_2026.3_controllerClient.zip
SHA-256 of the DLL: %s
Licence: GNU Lesser General Public License, version 2.1 (LGPL-2.1.txt).
Source: the NVDA repository, https://github.com/nvaccess/nvda , release 2026.3,
folder nvdaHelper (the client is built from it); the archive above also holds the
header and the licence. Charon's scripts load the library to hand their messages to
a running NVDA; it may be replaced by any other build of the same interface.
NVDA itself is not shipped.
'''%sha(addon/'nvdaControllerClient.dll'),encoding='utf-8',newline='\r\n')
# The RHVoice voices: where each one comes from (the same data as src/synth/rhvoice/VOICES.tsv).
rows=[r for r in csv.DictReader((l for l in (charonsrc/'src/synth/rhvoice/VOICES.tsv').read_text(encoding='utf-8').splitlines() if l and not l.startswith('#')),delimiter='\t')]
assert len(rows)==13,len(rows)
lines=['RHVoice voices of Charon r409: repositories and commits','======================================================',
       'The voice data in \\EFI\\HAK\\synths\\rhvoice\\*.chsp are packed by tools/build-charon-synths.py from these',
       'repositories at these commits (16 kHz models). Natan and Slt were already in r397-r408; the other eleven',
       'are new in r409. The complete table with licences, data archives and their SHA-256 is',
       'src/synth/rhvoice/VOICES.tsv in charon-source-r409.zip (licenses\\synth-addons\\rhvoice-voices.tsv).','']
for r in rows:lines.append('%-12s %-8s %-22s %s  commit %s%s'%(r['rhvoice_name'],r['rhvoice_language'],r['licence'],r['repository'],r['commit'],'   NON-COMMERCIAL' if r['non_commercial']=='yes' else ''))
(cs/'synth-upstream/RHVOICE-VOICE-SOURCES.txt').write_text('\r\n'.join(lines)+'\r\n',encoding='utf-8',newline='')
with (lic/'COMPONENTS-AND-SOURCES.txt').open('a',encoding='utf-8',newline='') as f:
    f.write('\r\nAdded in r409\r\n'
            '- AudioDxe.efi r409: GPL-2.0-or-later as since r408; what changed and where the r408 driver is:\r\n'
            '  audio-driver\\R409-LICENCE-NOTE.txt.\r\n'
            '- RHVoice add-on: 13 voices. The engine as before (synth-addons\\LICENCES.txt); each voice has its own\r\n'
            '  licence: synth-addons\\LICENCES.txt, RHVOICE-VOICES.txt, rhvoice-voices.tsv. Cezary, Evgeniy-Eng and Lyubov\r\n'
            '  are CC BY-NC-ND 4.0 (non-commercial use only, no changed versions). Alan, Bdl and Clb have no licence file\r\n'
            '  upstream and are included as Debian distributes them. Repositories and commits:\r\n'
            '  corresponding-source\\synth-upstream\\RHVOICE-VOICE-SOURCES.txt.\r\n'
            '- nvdaControllerClient.dll (\\EFI\\HAK\\addon): NV Access, LGPL-2.1, unchanged: nvda-controller-client\\.\r\n'
            '- Windows profiles: Win11Debloat, WinUtil, ReviOS and AtlasOS are NOT shipped; Charon-WindowsProfile.ps1\r\n'
            '  downloads the pinned release the user chose and checks its SHA-256. Tiny11 is NOT shipped either.\r\n'
            '- Stormux, Linda, Nix, RHVoice packages of the distributions: downloaded by the installed system, not shipped.\r\n'
            '- In the images built on the build computer (CHARON.EFI, CHARONVOICE.EFI, CHARONDOS.EFI, espeak.tts,\r\n'
            '  rhvoice.tts, seven files of the driver pack) the account name inside debug paths was replaced by "user"\r\n'
            '  after the build (same length; work/scrub-private-r409.py in the source repository). Nothing else differs\r\n'
            '  from the build output.\r\n')
# The older source archive of the stage holds four files with such paths: rewritten with them scrubbed.
old=cs/'charon-source-r394.zip';tmp=cs/'charon-source-r394.zip.tmp'
n394=scrub.scrub_zip(old,tmp);os.replace(tmp,old)
skipped=[]
with zipfile.ZipFile(cs/'charon-source-r409.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for tree in ('src','tools','tests','third_party'):
        for p in sorted((charonsrc/tree).rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts:
                data,k=scrub.scrub_bytes(p.read_bytes())
                if k:skipped.append(p.relative_to(charonsrc).as_posix())
                z.writestr(p.relative_to(charonsrc).as_posix(),data)
    for n in specs:z.write(base/n,'docs/'+n)
    for n in ('scrub-private-r409.py','scan-private-r409.py','prepare-release-inputs-r409.py'):z.write(base/n,'work/'+n)
for p in sorted((charonsrc/'tests').glob('charon-r40[4-9]*-results.txt')):shutil.copyfile(p,cs/p.name)
assert (cs/'charon-r409-results.txt').exists() and len(list(cs.glob('charon-r409-*-results.txt')))==7
# READMEs (English only).
rd=base/'readme-r409'
def put(src,dst):dst.write_bytes(('﻿'+src.read_text(encoding='utf-8').replace('\r\n','\n').replace('\n','\r\n')).encode('utf-8'))
put(rd/'release-README.txt',stage/'README.txt')
put(rd/'installer-README.txt',stage/'Instalator/README.txt')
put(rd/'addon-README.txt',stage/'Dodatek-UEFI/README.txt')
# Nothing personal may leave this machine: every file, binaries and zip members included.
if scan.main([str(stage)]):raise SystemExit('private strings in the stage; nothing packed')
files=sorted(p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file())
manifest={'release':'r409','package':'install',
          'inputs_sha256':{'charon':sha(efi),'voice_driver':sha(voice),'audio_driver':sha(audio),
                           'charondos':sha(dos/'CHARONDOS.EFI'),'charonpc':sha(dos/'CHARONPC.EFI'),
                           'rhvoice_module':sha(stage/'Dodatki-syntezy/EFI/HAK/synths/rhvoice.tts'),
                           'espeak_module':sha(stage/'Dodatki-syntezy/EFI/HAK/synths/espeak.tts')},
          'files':{f:sha(stage/f) for f in files}}
(stage/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
files=sorted(set(files)|{'manifest.json'})
print('stage',len(files),'files; synth add-ons',addon_bytes,'bytes; r394 archive replacements',n394,'; source files scrubbed',skipped)
if '--stage-only' in sys.argv:raise SystemExit(0)
tmp=out.with_suffix('.zip.tmp')
with zipfile.ZipFile(tmp,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for f in files:z.write(stage/f,'Charon/'+f)
with zipfile.ZipFile(tmp) as z:assert z.testzip() is None
os.replace(tmp,out)
print('zip',out,out.stat().st_size,sha(out)[:16])
