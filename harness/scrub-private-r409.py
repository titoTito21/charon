"""r409: remove the build computer's account name from files that are released.

The linkers and ImageTool write absolute paths of the build tree into the images (the ELF's
path in the PE debug entry, __FILE__ strings of assertions); logs and scripts hold them too.
This tool replaces the account name inside such paths by the word "user" - the same number of
bytes, so nothing moves in a binary - and corrects the PE checksum when the image has one.
ASCII and UTF-16LE, any letter case. Nothing else is changed.

  python scrub-private-r409.py FILE [...]        in place; prints what changed
  import: scrub_bytes(data) -> (data, count);  scrub_file(path) -> count;  scrub_zip(src, dst) -> count
The account name is assembled from pieces so that this file is not changed by itself."""
import re,struct,sys,zipfile,io
from pathlib import Path
N='ti'+'to';R='user'
def _pats(enc):
    e=lambda s:re.escape(s.encode(enc))
    sep=b'(?:'+e('\\')+b'|'+e('/')+b')+'
    def ci(s):return b''.join(b'(?:'+re.escape(c.lower().encode(enc))+b'|'+re.escape(c.upper().encode(enc))+b')' for c in s)
    n=ci(N);r=R.encode(enc)
    return [(re.compile(b'('+ci('users')+sep+b')'+n),lambda m:m.group(1)+r),
            (re.compile(b'('+ci('home')+sep+b')'+n),lambda m:m.group(1)+r),
            (re.compile(n+b'('+sep+ci('documents')+b')'),lambda m:r+m.group(1)),
            (re.compile(n+b'('+sep+ci('appdata')+b')'),lambda m:r+m.group(1))]
PATS=_pats('ascii')+_pats('utf-16le')
def pe_checksum(data):
    """The PE optional header's CheckSum of data (bytes), computed with the field taken as zero."""
    pe=struct.unpack_from('<I',data,0x3c)[0];field=pe+24+64
    b=bytearray(data);b[field:field+4]=b'\0\0\0\0'
    if len(b)&1:b+=b'\0'
    total=0
    for (w,) in struct.iter_unpack('<H',bytes(b)):
        total+=w;total=(total&0xffff)+(total>>16)
    total=(total&0xffff)+(total>>16)
    return (total+len(data))&0xffffffff
def scrub_bytes(data):
    count=0;out=data
    for pat,rep in PATS:
        out,k=pat.subn(rep,out);count+=k
    assert len(out)==len(data)
    if count and out[:2]==b'MZ' and len(out)>0x40:
        pe=struct.unpack_from('<I',out,0x3c)[0]
        if pe+92<=len(out) and out[pe:pe+4]==b'PE\0\0':
            field=pe+24+64
            if struct.unpack_from('<I',out,field)[0]:
                b=bytearray(out);struct.pack_into('<I',b,field,pe_checksum(out));out=bytes(b)
    return out,count
def scrub_file(path):
    path=Path(path);data=path.read_bytes();out,count=scrub_bytes(data)
    if count:path.write_bytes(out)
    return count
def scrub_zip(src,dst,level=6):
    """Copy a zip member by member with every member scrubbed (zips inside are scrubbed too)."""
    count=0
    with zipfile.ZipFile(src) as zin,zipfile.ZipFile(dst,'w',zipfile.ZIP_DEFLATED,compresslevel=level) as zout:
        for i in zin.infolist():
            data=zin.read(i)
            if not i.is_dir():
                if data[:4]==b'PK\x03\x04':
                    a=io.BytesIO();k=scrub_zip(io.BytesIO(data),a,level)
                    if k:data=a.getvalue();count+=k
                else:
                    data,k=scrub_bytes(data);count+=k
            zout.writestr(i,data)
    return count
if __name__=='__main__':
    if len(sys.argv)<2:sys.exit(__doc__)
    for a in sys.argv[1:]:print(scrub_file(a),'replacements in',a)
