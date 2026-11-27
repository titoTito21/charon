"""r409: outputs/Charon-Installer-r409.zip - the installer on its own (no components/,
no QEMU): Instalator/ (boot manager / reader / shim modes), Dodatek-UEFI/ (voice
from power-on), Dodatki-syntezy/ (eSpeak NG, RHVoice), licences with the
corresponding source, CZYTAJ. Taken from the verified r409 release stage.

  python make-charon-installer-r409.py
"""
import hashlib,json,zipfile,os
from pathlib import Path
base=Path(__file__).resolve().parent
rel=base/'charon-release-r409-install/Charon'
out=base.parent/'outputs/Charon-Installer-r409.zip'
assert not out.exists(),out
czytaj=(base/'readme-r409/installer-package-README.txt').read_text(encoding='utf-8')
files={}
for d in ('Instalator','Dodatek-UEFI','Dodatki-syntezy','OS-tools','licenses'):
    for p in sorted((rel/d).rglob('*')):
        if p.is_file():files[d+'/'+p.relative_to(rel/d).as_posix()]=p.read_bytes()
import importlib.util as _u
_s=_u.spec_from_file_location('scanprivate',base/'scan-private-r409.py');scanprivate=_u.module_from_spec(_s);_s.loader.exec_module(scanprivate)
if scanprivate.main([str(rel)]):raise SystemExit('private strings in the stage')
files['README.txt']=('﻿'+czytaj.replace('\n','\r\n')).encode('utf-8')
man={'release':'r409','package':'installer','files':{k:hashlib.sha256(v).hexdigest() for k,v in sorted(files.items())}}
files['manifest.json']=(json.dumps(man,indent=2)+'\n').encode()
tmp=out.with_suffix('.zip.tmp')
with zipfile.ZipFile(tmp,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for k,v in sorted(files.items()):z.writestr('Charon-Installer/'+k,v)
with zipfile.ZipFile(tmp) as z:assert z.testzip() is None
os.replace(tmp,out)
print('zip',out,out.stat().st_size,len(files),'files')
