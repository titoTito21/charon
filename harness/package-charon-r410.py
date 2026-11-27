"""Build work/charon-release-r410-outputs/Charon-r410.zip: the INSTALL/UPDATE package (no demo VM).

r410 adaptation of package-charon-r409.py. Output goes to work/ (NOT ..\\outputs - staging only).
Starts from the released r409 stage (charon-release-r409-install) and overlays the r410 reader
pair (CHARON.EFI / CHARONVOICE.EFI), the r410 CharonDOS / CharonPC, and re-states the install media.
REUSED unchanged from r409: the sound driver (charon-audio-driver-r409), the driver pack
(charon-drivers-r409), and the 13-voice synthesizer set (synth-modules-r410a, built for r410,
scanned clean). The two synth .tts files and the four r410 EFIs were scanned for the build
computer's account name (0 hits), so no scrub was needed (release-inputs-r410.json).

  python package-charon-r410.py [--stage-only]
"""
from pathlib import Path
import hashlib,importlib.util,json,shutil,sys,zipfile,os,csv
base=Path(__file__).resolve().parent
os.environ.setdefault('CHARON_SYNTH_STAGE','synth-modules-r410a')   # the r410 synth build (already clean)
def load(name):
    spec=importlib.util.spec_from_file_location(name.replace('-','_'),base/(name+'.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
scrub=load('scrub-private-r409');scan=load('scan-private-r409');synths=load('package-synths-r409')
prev=base/'charon-release-r409-install/Charon'
stage=base/'charon-release-r410-install/Charon'
charonsrc=base/'charon-src-r395';addon=charonsrc/'tools/charon-addon';ostools=charonsrc/'tools/charon-os'
efi=charonsrc/'charon-win-r410-rel/CHARON.EFI';voice=charonsrc/'charon-win-r410-rel-drv/CHARONVOICE.EFI'
audio=base/'charon-audio-driver-r409/AudioDxe-r409.efi'    # REUSED, unchanged since r409
drivers=base/'charon-drivers-r409'                          # REUSED, unchanged since r409
dosefi=charonsrc/'charondos-r410rel/CHARONDOS.EFI';pcefi=charonsrc/'charonpc-r410rel/CHARONPC.EFI'
pc=base/'charon-pc-stage-r406i'          # licences of the emulator (Bochs 3.1, musl, libc++: unchanged since r403)
dos=base/'pkg-r410-dos/charon-packages/CharonDOS'
gpl2=base/'charon-release-r394/Charon/licenses/qemu-as-shipped/COPYING'
out=base/'charon-release-r410-outputs/Charon-r410.zip'
results=charonsrc/'tests/charon-r410-install-results.txt'   # the primary r410 results file (FINAL RELEASE STATE on top)
specs=('charon-multiboot-spec-2026-11-23.md','charon-detailed-questions-spec-2026-11-23.md','charon-windows-profiles-research-2026-11-23.md','charon-older-windows-research-2026-11-23.md')

def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()

assert not stage.exists(),stage
out.parent.mkdir(parents=True,exist_ok=True)
assert not out.exists(),out
for p in (prev,efi,voice,audio,drivers,dosefi,pcefi,pc/'LICENCES.txt',gpl2,ostools/'windows',ostools/'linux',results):assert p.exists(),p
assert os.environ.get('R410_CANDIDATE') or '@@' not in results.read_text(encoding='utf-8'),'the results file is not complete'
for n in ('Charon-Network.ps1','nvdaControllerClient.dll','nvdaControllerClient-LICENSE.txt','Install-Charon.ps1','Install-CharonAddon.ps1','Charon-WindowsProfile.ps1','Charon-Readers.ps1','CHARON-WINDOWS-PROFILES.txt'):assert (addon/n).exists(),n
# The CharonDOS package: CHARONDOS.EFI + the PC emulator and its licences (the installer copies the whole
# folder onto a CharonDOS system). Made from the r409 package; the program and the two documents are
# always taken again from the build and the source tree.
if not dos.exists():
    shutil.copytree(base/'pkg-r409-dos/charon-packages/CharonDOS',dos)
    shutil.copyfile(pcefi,dos/'CHARONPC.EFI')
    shutil.copyfile(pc/'LICENCES.txt',dos/'CHARONPC-LICENCES.txt')
    shutil.rmtree(dos/'licences');shutil.copytree(pc/'licences',dos/'licences')
shutil.copyfile(dosefi,dos/'CHARONDOS.EFI')
shutil.copyfile(charonsrc/'src/pc/WIN9X-READERS.txt',dos/'WIN9X-READERS.txt')
shutil.copyfile(charonsrc/'src/dos/CHARON-COMMANDS.txt',dos/'CHARON-COMMANDS.txt')
assert sha(dos/'CHARONPC.EFI')==sha(pcefi)
shutil.copytree(prev,stage)
(stage/'manifest.json').unlink()
# Parts the r409 stage carries are replaced, not added twice.
for old in ('Instalator/EFI/HAK/addon','Instalator/EFI/HAK/drivers','Instalator/EFI/HAK/packages/CharonDOS','Instalator/EFI/HAK/os','OS-tools'):
    if (stage/old).exists():shutil.rmtree(stage/old)
cs=stage/'licenses/corresponding-source'
for old in list(cs.glob('charon-r40[4-9]*-results.txt'))+list(cs.glob('charon-r41*-results.txt'))+list(cs.glob('charon-source-r40[5-9].zip'))+list(cs.glob('charon-source-r41*.zip')):old.unlink()
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
if (cs/'driver-pack-source.zip').exists():(cs/'driver-pack-source.zip').unlink()
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
# Licences. The sound driver is byte-identical to r409's (REUSED); keep its r409 note and add an r410 note.
ad=lic/'audio-driver'
shutil.copyfile(gpl2,ad/'GPL-2.0.txt')
shutil.copyfile(charonsrc/'src/efi/AUDIO-CODECS.txt',ad/'AUDIO-CODECS.txt')
shutil.copyfile(charonsrc/'src/efi/audio_stream/driver/BUILD-NOTES.md',ad/'BUILD-NOTES.md')
(ad/'R410-LICENCE-NOTE.txt').write_text('''AudioDxe.efi of Charon r410
===========================
The sound driver shipped in r410 is byte-identical to the r409 driver
(the same AudioDxe-r409.efi; it was not rebuilt for r410). Its licence and
history are unchanged: GPL-2.0-or-later as since r408, because it contains
codec and machine tables written from GPL-2.0-or-later files of the Linux
kernel's sound/hda tree (AUDIO-CODECS.txt, section 1). The parts taken from
OpenCorePkg / AUDK keep their BSD and MIT notices (the other files of this
folder). What the driver does and how it is built: R409-LICENCE-NOTE.txt and
BUILD-NOTES.md. It has not been run on a physical sound card.

Corresponding source: corresponding-source\\charon-source-r410.zip -
src/efi/audio_stream/driver and tools/gen-charon-hda-quirks.py (the sound
driver sources are the same as r409; the archive is the full r410 source tree).
Switch for trouble on real hardware: an empty file \\EFI\\HAK\\audio-noquirks
turns the machine table off.
''',encoding='utf-8',newline='\r\n')
nv=lic/'nvda-controller-client';nv.mkdir(exist_ok=True)
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
lines=['RHVoice voices of Charon r410: repositories and commits','======================================================',
       'The voice data in \\EFI\\HAK\\synths\\rhvoice\\*.chsp are packed by tools/build-charon-synths.py from these',
       'repositories at these commits (16 kHz models). The voice set is the same 13 voices as r409. The',
       'complete table with licences, data archives and their SHA-256 is',
       'src/synth/rhvoice/VOICES.tsv in charon-source-r410.zip (licenses\\synth-addons\\rhvoice-voices.tsv).','']
for r in rows:lines.append('%-12s %-8s %-22s %s  commit %s%s'%(r['rhvoice_name'],r['rhvoice_language'],r['licence'],r['repository'],r['commit'],'   NON-COMMERCIAL' if r['non_commercial']=='yes' else ''))
(cs/'synth-upstream/RHVOICE-VOICE-SOURCES.txt').write_text('\r\n'.join(lines)+'\r\n',encoding='utf-8',newline='')
with (lic/'COMPONENTS-AND-SOURCES.txt').open('a',encoding='utf-8',newline='') as f:
    f.write('\r\nAdded in r410\r\n'
            '- Reader control (CHARON.EFI / CHARONVOICE.EFI), CharonDOS and CharonPC rebuilt from the r410 source\r\n'
            '  tree. The multi-boot plan now OFFERS the "Windows, Debian, installer and Charon Hypervisor" preset in\r\n'
            '  the normal list; its full end-to-end install was not run in a VM (honest proof scope: the r410 section\r\n'
            '  of charon-r409-multiboot-results.txt and charon-r410-install-results.txt).\r\n'
            '- AudioDxe.efi: UNCHANGED since r409 (byte-identical); GPL-2.0-or-later. Details: audio-driver\\R410-LICENCE-NOTE.txt\r\n'
            '  and the kept audio-driver\\R409-LICENCE-NOTE.txt.\r\n'
            '- Driver pack and the 13-voice RHVoice add-on: UNCHANGED since r409 (driver-pack\\, synth-addons\\). The\r\n'
            '  voices Cezary, Evgeniy-Eng and Lyubov remain CC BY-NC-ND 4.0 (non-commercial, no changed versions).\r\n'
            '- Corresponding source for r410: corresponding-source\\charon-source-r410.zip; results:\r\n'
            '  corresponding-source\\charon-r410-install-results.txt and the other charon-r410-*-results.txt.\r\n'
            '- The four r410 images (CHARON.EFI, CHARONVOICE.EFI, CHARONDOS.EFI, CHARONPC.EFI) and the two synth\r\n'
            '  modules (espeak.tts, rhvoice.tts) were scanned for the build computer\'s account name: 0 hits, so no\r\n'
            '  scrub was needed (work/release-inputs-r410.json). The reused driver pack had already been scrubbed for r409.\r\n')
# The older source archive of the stage holds files with such paths: rewritten with them scrubbed (idempotent).
old=cs/'charon-source-r394.zip';tmp=cs/'charon-source-r394.zip.tmp'
n394=scrub.scrub_zip(old,tmp);os.replace(tmp,old)
skipped=[]
with zipfile.ZipFile(cs/'charon-source-r410.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for tree in ('src','tools','tests','third_party'):
        if not (charonsrc/tree).exists():continue
        for p in sorted((charonsrc/tree).rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts:
                data,k=scrub.scrub_bytes(p.read_bytes())
                if k:skipped.append(p.relative_to(charonsrc).as_posix())
                z.writestr(p.relative_to(charonsrc).as_posix(),data)
    for n in specs:z.write(base/n,'docs/'+n)
    for n in ('scrub-private-r409.py','scan-private-r409.py','prepare-release-inputs-r409.py'):z.write(base/n,'work/'+n)
for p in sorted((charonsrc/'tests').glob('charon-r40[4-9]*-results.txt'))+sorted((charonsrc/'tests').glob('charon-r41*-results.txt')):shutil.copyfile(p,cs/p.name)
assert (cs/'charon-r410-install-results.txt').exists() and len(list(cs.glob('charon-r410-*-results.txt')))==4
# READMEs (English only).
rd=base/'readme-r410'
def put(src,dst):dst.write_bytes(('﻿'+src.read_text(encoding='utf-8').replace('\r\n','\n').replace('\n','\r\n')).encode('utf-8'))
put(rd/'release-README.txt',stage/'README.txt')
put(rd/'installer-README.txt',stage/'Instalator/README.txt')
put(rd/'addon-README.txt',stage/'Dodatek-UEFI/README.txt')
# Nothing personal may leave this machine: every file, binaries and zip members included.
if scan.main([str(stage)]):raise SystemExit('private strings in the stage; nothing packed')
files=sorted(p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file())
manifest={'release':'r410','package':'install',
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
