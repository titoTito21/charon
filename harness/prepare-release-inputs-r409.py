"""r409: release inputs without the build computer's account name (scrub-private-r409.py).

  python prepare-release-inputs-r409.py

1. The final images, in place: charon-win-r409final/CHARON.EFI, charon-win-r409final-drv/CHARONVOICE.EFI,
   charondos-r409/CHARONDOS.EFI (the ELF's absolute path in the PE debug entry). The hashes as built and
   after the change are written to release-inputs-r409.json.
2. synth-modules-r409-release: a copy of synth-modules-r409 (the tested build) with espeak.tts and
   rhvoice.tts scrubbed (__FILE__ strings and the ELF path) and the sha256 lines of the three manifests
   set to the new files. Nothing else differs from synth-modules-r409.
3. charon-drivers-r409: a copy of charon-drivers-r408 with the seven driver files that held such a path
   scrubbed, MANIFEST.json (sha256, pack name) corrected, and the text files of source/ scrubbed.
Each target directory must not exist (the images of 1. are changed once: a second run changes nothing)."""
from pathlib import Path
import hashlib,importlib.util,json,shutil
base=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('scrub',base/'scrub-private-r409.py');scrub=importlib.util.module_from_spec(spec);spec.loader.exec_module(scrub)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
record={'images':{},'synths':{},'drivers':{}}
for rel in ('charon-win-r409final/CHARON.EFI','charon-win-r409final-drv/CHARONVOICE.EFI','charondos-r409/CHARONDOS.EFI'):
    p=base/rel;before=sha(p);n=scrub.scrub_file(p)
    record['images'][rel]={'as_built':before,'released':sha(p),'replacements':n};print(rel,n)
# 2. synthesizer modules
src=base/'synth-modules-r409';dst=base/'synth-modules-r409-release';assert not dst.exists(),dst
shutil.copytree(src,dst,ignore=shutil.ignore_patterns('test-wav'))
root=dst/'EFI/HAK/synths';change={}
for name in ('espeak.tts','rhvoice.tts'):
    before=sha(root/name);n=scrub.scrub_file(root/name);after=sha(root/name);change[before]=after
    record['synths'][name]={'as_built':before,'released':after,'replacements':n};print(name,n)
for m in (root/'espeak.manifest',root/'rhvoice.manifest',dst/'rhvoice-base/EFI/HAK/synths/rhvoice.manifest'):
    t=m.read_bytes();k=0
    for a,b in change.items():
        if a.encode() in t:t=t.replace(a.encode(),b.encode());k+=1
    assert k==1,(m,k);m.write_bytes(t)
for m,tts in ((root/'espeak.manifest','espeak.tts'),(root/'rhvoice.manifest','rhvoice.tts'),(dst/'rhvoice-base/EFI/HAK/synths/rhvoice.manifest','rhvoice.tts')):
    assert ('sha256 = '+sha(root/tts)).encode() in m.read_bytes(),m
# 3. driver pack
src=base/'charon-drivers-r408';dst=base/'charon-drivers-r409';assert not dst.exists(),dst
shutil.copytree(src,dst,ignore=shutil.ignore_patterns('__pycache__'))
man=json.loads((dst/'MANIFEST.json').read_text(encoding='utf-8'))
for e in man['drivers']:
    p=dst/e['file'];assert sha(p)==e['sha256'],e['file']
    n=scrub.scrub_file(p)
    if n:
        record['drivers'][e['file']]={'as_built':e['sha256'],'released':sha(p),'replacements':n};e['sha256']=sha(p)
        assert p.stat().st_size==e['size']
man['pack']='charon-drivers-r409'
man['note']=man['note']+' r409: the same files as charon-drivers-r408; in %d of them the account name of the build computer inside a debug path was replaced by "user" (same length, PE checksum corrected), nothing else.'%len(record['drivers'])
(dst/'MANIFEST.json').write_text(json.dumps(man,indent=1,ensure_ascii=False)+'\n',encoding='utf-8',newline='\n')
texts=0
for p in sorted((dst/'source').rglob('*')):
    if p.is_file():texts+=1 if scrub.scrub_file(p) else 0
for p in dst.glob('*.txt'):scrub.scrub_file(p)
record['drivers_source_files_scrubbed']=texts
(base/'release-inputs-r409.json').write_text(json.dumps(record,indent=1)+'\n',encoding='utf-8',newline='\n')
print('drivers',len(record['drivers']),'source files',texts)
