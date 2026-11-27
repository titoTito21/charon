"""r409: checks of the finished packages in outputs/.   python verify-release-r409.py
1. Charon-r409.zip and Charon-Installer-r409.zip: every member's SHA-256 against the manifest inside, no member
   outside the manifest; the images against the build folders.
2. Charon-r409.iso: the boot FAT image is read back and every file compared with the stage / the build folders.
3. Charon-testvm-r409-all-managers.zip: members against the stage folder (size and CRC), the Charon on its boot
   disk and the ISO in cd\ are the released ones.
4. charon-github-r409, the two zips (members too) and the ISO: no account name of the build computer.
5. Licence files that must be present."""
import hashlib,importlib.util,io,json,sys,zipfile,zlib
from pathlib import Path
base=Path(__file__).resolve().parent;out=base.parent/'outputs'
sys.path[:0]=[str(base/'iso-deps'),str(base/'fat-image-deps'),str(base/'fat-legacy-deps')]
spec=importlib.util.spec_from_file_location('scanprivate',base/'scan-private-r409.py');scan=importlib.util.module_from_spec(spec);spec.loader.exec_module(scan)
def sha(b):return hashlib.sha256(b).hexdigest()
def fsha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()
stage=base/'charon-release-r409-install/Charon'
B={'CHARON.EFI':fsha(base/'charon-win-r409final/CHARON.EFI'),'CHARONVOICE.EFI':fsha(base/'charon-win-r409final-drv/CHARONVOICE.EFI'),
   'AudioDxe.efi':fsha(base/'charon-audio-driver-r409/AudioDxe-r409.efi'),'CHARONDOS.EFI':fsha(base/'charondos-r409/CHARONDOS.EFI'),
   'CHARONPC.EFI':fsha(base/'charon-pc-build-r407zf/CHARONPC.EFI'),
   'rhvoice.tts':fsha(base/'synth-modules-r409-release/EFI/HAK/synths/rhvoice.tts'),'espeak.tts':fsha(base/'synth-modules-r409-release/EFI/HAK/synths/espeak.tts')}
assert B['AudioDxe.efi']=='ebe6899accbfebd1a77c3b409361eb6091525957052940c178e95019acc1064f'
ok=True
def check(what,cond,detail=''):
    global ok;ok=ok and bool(cond);print('PASS' if cond else 'FAIL',what,detail)
# 1
for name,prefix,expect in (('Charon-r409.zip','Charon/',{'components/CHARON.EFI':'CHARON.EFI','components/CHARONVOICE.EFI':'CHARONVOICE.EFI','components/AudioDxe.efi':'AudioDxe.efi',
        'Instalator/EFI/HAK/HAKBOOT.EFI':'CHARON.EFI','Instalator/EFI/HAK/CHARONVOICE.EFI':'CHARONVOICE.EFI','Instalator/EFI/HAK/AudioDxe.efi':'AudioDxe.efi','Dodatek-UEFI/EFI/HAK/CHARONVOICE.EFI':'CHARONVOICE.EFI',
        'Instalator/EFI/HAK/packages/CharonDOS/CHARONDOS.EFI':'CHARONDOS.EFI','Instalator/EFI/HAK/packages/CharonDOS/CHARONPC.EFI':'CHARONPC.EFI',
        'Dodatki-syntezy/EFI/HAK/synths/rhvoice.tts':'rhvoice.tts','Dodatki-syntezy/EFI/HAK/synths/espeak.tts':'espeak.tts'}),
      ('Charon-Installer-r409.zip','Charon-Installer/',{'Instalator/EFI/HAK/HAKBOOT.EFI':'CHARON.EFI','Instalator/EFI/HAK/CHARONVOICE.EFI':'CHARONVOICE.EFI','Instalator/EFI/HAK/AudioDxe.efi':'AudioDxe.efi',
        'Dodatek-UEFI/EFI/HAK/CHARONVOICE.EFI':'CHARONVOICE.EFI','Instalator/EFI/HAK/packages/CharonDOS/CHARONDOS.EFI':'CHARONDOS.EFI',
        'Dodatki-syntezy/EFI/HAK/synths/rhvoice.tts':'rhvoice.tts','Dodatki-syntezy/EFI/HAK/synths/espeak.tts':'espeak.tts'})):
    with zipfile.ZipFile(out/name) as z:
        man=json.loads(z.read(prefix+'manifest.json'));names={i.filename[len(prefix):] for i in z.infolist() if not i.is_dir()}-{'manifest.json'}
        bad=[f for f,h in man['files'].items() if f!='manifest.json' and (f not in names or sha(z.read(prefix+f))!=h)]
        check(name+': %d files match the manifest'%len(man['files']),not bad and names<=set(man['files']),str(bad[:5]))
        wrong=[f for f,k in expect.items() if man['files'].get(f)!=B[k]]
        check(name+': the %d images are the build folders\' files'%len(expect),not wrong,str(wrong))
        need=['licenses/audio-driver/GPL-2.0.txt','licenses/audio-driver/R409-LICENCE-NOTE.txt','licenses/nvda-controller-client/LGPL-2.1.txt','licenses/synth-addons/LICENCES.txt',
              'licenses/synth-addons/RHVOICE-VOICES.txt','licenses/synth-addons/rhvoice-voices.tsv','licenses/synth-addons/cc-by-nc-nd-4.0.txt','licenses/COMPONENTS-AND-SOURCES.txt',
              'licenses/corresponding-source/charon-source-r409.zip','licenses/corresponding-source/charon-r409-results.txt','licenses/corresponding-source/driver-pack-source.zip',
              'licenses/corresponding-source/synth-upstream/RHVOICE-VOICE-SOURCES.txt','Instalator/EFI/HAK/addon/Charon-Network.ps1','Instalator/EFI/HAK/addon/nvdaControllerClient.dll',
              'Instalator/EFI/HAK/addon/nvdaControllerClient-LICENSE.txt','Instalator/EFI/HAK/addon/CHARON-WINDOWS-PROFILES.txt']
        check(name+': licences and required files present',all(n in names for n in need),str([n for n in need if n not in names]))
        res=z.read(prefix+'licenses/corresponding-source/charon-r409-results.txt')
        check(name+': the results file is the final one',res==(base/'charon-src-r395/tests/charon-r409-results.txt').read_bytes() and b'@@' not in res)
        src=zipfile.ZipFile(io.BytesIO(z.read(prefix+'licenses/corresponding-source/charon-source-r409.zip')))
        check(name+': source archive has the sound driver and the fixed installer','src/efi/audio_stream/driver/HakAudioStream.c' in src.namelist() and b'cat "$v" | tail -c +5' in src.read('tools/charon-addon/zainstaluj-charona.sh'))
# 2
import pycdlib
from pyfatfs.PyFatFS import PyFatFS
iso=pycdlib.PyCdlib();iso.open(str(out/'Charon-r409.iso'));buf=io.BytesIO();iso.get_file_from_iso_fp(buf,joliet_path='/efiboot.img');iso.close()
tmp=base/'verify-r409-efiboot.img';tmp.write_bytes(buf.getvalue())
want={'EFI/BOOT/BOOTX64.EFI':B['CHARON.EFI'],'EFI/HAK/CHARONVOICE.EFI':B['CHARONVOICE.EFI']}
hak=stage/'Instalator/EFI/HAK'
for p in hak.rglob('*'):
    r=p.relative_to(hak).as_posix()
    if p.is_file() and r not in ('HAKBOOT.EFI','CHARONVOICE.EFI'):want['EFI/HAK/'+r]=fsha(p)
syn=base/'synth-modules-r409-release/EFI/HAK/synths'
for p in syn.rglob('*'):
    if p.is_file():want['EFI/HAK/synths/'+p.relative_to(syn).as_posix()]=fsha(p)
bad=[];n=0
with PyFatFS(str(tmp),read_only=True) as fs:
    for k,h in want.items():
        with fs.openbin(k,'r') as f:
            n+=1
            if sha(f.read())!=h:bad.append(k)
    lang=fs.openbin('EFI/HAK/audio-language','r').read()
tmp.unlink()
check('Charon-r409.iso: %d files of the boot image are the stage\'s and the build folders\' files'%n,not bad,str(bad[:5]))
check('Charon-r409.iso: English by default',lang==b'en')
check('Charon-r409.iso: .sha256 file',(out/'Charon-r409.iso.sha256').read_text().split()[0]==fsha(out/'Charon-r409.iso'))
# 3
tv=base/'charon-testvm-r409all'
with zipfile.ZipFile(out/'Charon-testvm-r409-all-managers.zip') as z:
    members={i.filename:i for i in z.infolist() if not i.is_dir()};bad=[]
    files=[p for p in tv.rglob('*') if p.is_file()]
    for p in files:
        i=members.get('Charon-testvm-r409/'+p.relative_to(tv).as_posix());crc=0
        if i is None or i.file_size!=p.stat().st_size:bad.append(p.name);continue
        with open(p,'rb') as f:
            for b in iter(lambda:f.read(1<<20),b''):crc=zlib.crc32(b,crc)
        if crc!=i.CRC:bad.append(p.name)
    check('test VM zip: %d members equal the checked stage folder'%len(files),not bad and len(files)==len(members),str(bad[:5]))
check('test VM: cd\\charon-r409.iso is the released ISO',fsha(tv/'cd/charon-r409.iso')==fsha(out/'Charon-r409.iso'))
tw={'EFI/BOOT/BOOTX64.EFI':'CHARON.EFI','EFI/HAK/CHARONVOICE.EFI':'CHARONVOICE.EFI','EFI/HAK/AudioDxe.efi':'AudioDxe.efi','EFI/HAK/synths/rhvoice.tts':'rhvoice.tts',
    'EFI/HAK/synths/espeak.tts':'espeak.tts','charon-packages/CharonDOS/CHARONDOS.EFI':'CHARONDOS.EFI'}
with PyFatFS(str(tv/'boot-disk.img'),read_only=True) as fs:
    bad=[k for k,v in tw.items() if sha(fs.openbin(k,'r').read())!=B[v]]
check('test VM: its boot disk holds the released Charon, reader, sound driver, synthesizer modules and CharonDOS',not bad,str(bad))
# 4
print('scan: source tree, zips with their members, ISO')
check('no account name in charon-github-r409, Charon-r409.zip, Charon-Installer-r409.zip, Charon-r409.iso',
      scan.main([str(out/'charon-github-r409'),str(out/'Charon-r409.zip'),str(out/'Charon-Installer-r409.zip'),str(out/'Charon-r409.iso')])==0)
check('no account name in the test VM folder',scan.main([str(tv)])==0)
gh=out/'charon-github-r409'
check('source tree: results, design documents, licences',all((gh/x).exists() for x in ('docs/results/charon-r409-results.txt','docs/results/charon-r409-multiboot-results.txt','docs/charon-multiboot-spec-2026-11-23.md','docs/charon-detailed-questions-spec-2026-11-23.md','LICENSE','licenses/audio-driver/R409-LICENCE-NOTE.txt','licenses/synth-addons/LICENCES.txt','src/efi/CharonWsPlan.h','src/dos/launch_select.h','src/linux/network.sh','tests/golden/charon-r409-plan/windows-partition-number.txt')) and (gh/'docs/results/charon-r409-results.txt').read_bytes()==(base/'charon-src-r395/tests/charon-r409-results.txt').read_bytes())
print('ALL PASS' if ok else 'FAILED');sys.exit(0 if ok else 1)
