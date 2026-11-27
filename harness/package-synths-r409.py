"""r409: the synthesizer add-ons (eSpeak NG, RHVoice with every Polish and English voice) for the
release packagers. Not a packager itself: the r409 packagers import it (or run it on a new folder).

  import importlib.util  ->  synths = load('package-synths-r409.py')
  synths.medium_files()                 {'EFI/HAK/synths/...': Path}  for a boot medium (ISO FAT, test VM, GitHub tree)
  synths.medium_files(all_voices=False) the same without the further RHVoice voices (manifest with Natan and Slt only)
  synths.put_addons(stage)              <stage>/Dodatki-syntezy/EFI/HAK/synths (all voices) and <stage>/licenses/synth-addons
  python package-synths-r409.py NEW_DIRECTORY [--base-only]      writes the medium tree there (to look at or test)

Source: work/synth-modules-r409, built by charon-src-r395/tools/build-charon-synths.py (host tests passed):
  EFI/HAK/synths/                       espeak.tts, rhvoice.tts, manifests, data packs, LICENCES.txt, licences/,
                                        RHVOICE-VOICES.txt, rhvoice-voices.tsv
  rhvoice-base/EFI/HAK/synths/rhvoice.manifest   manifest of a package without the further voices
  rhvoice-voice-pack.json               which files are the further voices, which are non-commercial

Sizes (bytes): whole add-on folder 22 265 251 (r397: 6 845 366); the further voices 15 306 347 in 11 files;
without them 6 957 931. A medium that carries all voices grows by about 15.4 MB; Charon's own installer
(CharonWsSelfInstall.h) copies \\EFI\\HAK\\synths to the EFI partition only when everything fits, so a
package meant for small EFI partitions (100 MB with Windows on it) should use all_voices=False and offer
the further voices as files to copy later (they are picked up without any other change).
"""
from pathlib import Path
import json,shutil,sys
base=Path(__file__).resolve().parent
# The release takes synth-modules-r409-release: the same build with the account name of the build computer
# removed from the debug strings of the two .tts files and the manifests' sha256 set to them
# (prepare-release-inputs-r409.py). CHARON_SYNTH_STAGE=synth-modules-r409 gives the files as built.
import os
SYN=base/os.environ.get('CHARON_SYNTH_STAGE','synth-modules-r409-release')
root=SYN/'EFI/HAK/synths'
pack=json.loads((SYN/'rhvoice-voice-pack.json').read_text(encoding='utf-8'))
EXTRA=set(pack['extra_files']);NON_COMMERCIAL=set(pack['non_commercial_files'])

def medium_files(all_voices=True,non_commercial=True):
    """Every file of \\EFI\\HAK\\synths as {'EFI/HAK/synths/<path>': source Path}.
    all_voices=False: Natan and Slt only (the manifest then lists these two).
    non_commercial=False: all voices but Cezary, Evgeniy-Eng and Lyubov (CC BY-NC-ND 4.0); the full
    manifest stays, Charon speaks with the language's default voice where a listed voice has no file."""
    files={}
    for p in sorted(root.rglob('*')):
        if not p.is_file():continue
        r=p.relative_to(root).as_posix()
        if r in EXTRA and (not all_voices or (not non_commercial and r in NON_COMMERCIAL)):continue
        files['EFI/HAK/synths/'+r]=p
    if not all_voices:files['EFI/HAK/synths/rhvoice.manifest']=SYN/'rhvoice-base/EFI/HAK/synths/rhvoice.manifest'
    return files

def put_addons(stage,all_voices=True):
    """The release folder: Dodatki-syntezy (copied by hand onto the EFI partition) and the licence texts."""
    stage=Path(stage);target=stage/'Dodatki-syntezy'
    if target.exists():shutil.rmtree(target)
    for name,src in medium_files(all_voices).items():
        dst=target/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
    lic=stage/'licenses/synth-addons'
    if lic.exists():shutil.rmtree(lic)
    lic.mkdir(parents=True)
    for n in ('LICENCES.txt','RHVOICE-VOICES.txt','rhvoice-voices.tsv'):shutil.copyfile(root/n,lic/n)
    for p in sorted((root/'licences').iterdir()):shutil.copyfile(p,lic/p.name)
    return sum(p.stat().st_size for p in target.rglob('*') if p.is_file())

if __name__=='__main__':
    a=[x for x in sys.argv[1:] if not x.startswith('--')]
    if len(a)!=1:sys.exit(__doc__)
    out=Path(a[0]);out.mkdir(parents=True,exist_ok=False)
    files=medium_files(all_voices='--base-only' not in sys.argv)
    for name,src in files.items():
        dst=out/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
    print(len(files),'files,',sum(p.stat().st_size for p in files.values()),'bytes ->',out)
