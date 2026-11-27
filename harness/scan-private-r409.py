"""r409: look for the build computer's account name / home path in EVERY file of a tree, binaries included
(ASCII and UTF-16LE, any letter case), and inside .zip members one level deep.
  python scan-private-r409.py TREE_OR_FILE [...]      exit code 1 when something was found
The needles are assembled from pieces so that this file never matches itself."""
import sys,zipfile,io
from pathlib import Path
A='ti'+'to';B='klal'+'dek'
NEEDLES=[('users\\'+A).encode(),('users/'+A).encode(),(A+'\\documents').encode(),(A+'/documents').encode(),B.encode(),
         ('users\\'+A).encode('utf-16le'),('users/'+A).encode('utf-16le'),B.encode('utf-16le'),('home/'+A).encode(),(A+'@').encode()]
def hits(data):
    low=data.lower();return [n.decode('latin-1').replace('\x00','') for n in NEEDLES if n in low]
def scan(name,data,out,depth=0):
    h=hits(data)
    if h:out.append((name,sorted(set(h))))
    if depth<2 and data[:2]==b'PK':
        try:
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                for i in z.infolist():
                    if not i.is_dir():scan(name+'!'+i.filename,z.read(i),out,depth+1)
        except zipfile.BadZipFile:pass
def main(args):
    out=[];count=0
    for a in args:
        p=Path(a);files=[p] if p.is_file() else sorted(x for x in p.rglob('*') if x.is_file())
        for f in files:
            count+=1;scan(str(f.relative_to(p) if p.is_dir() else f.name),f.read_bytes(),out)
    for name,h in out:print('PRIVATE',name,h)
    print(count,'files scanned,',len(out),'with private strings')
    return 1 if out else 0
if __name__=='__main__':sys.exit(main(sys.argv[1:]))
