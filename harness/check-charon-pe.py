"""Reject a non-relocatable/import-dependent PE, independent of objdump wording."""
import struct,json,sys,hashlib
from pathlib import Path
p=Path(sys.argv[1]);data=p.read_bytes()
def u16(at):return struct.unpack_from('<H',data,at)[0]
def u32(at):return struct.unpack_from('<I',data,at)[0]
assert data[:2]==b'MZ';pe=u32(0x3c);assert data[pe:pe+4]==b'PE\0\0'
assert u16(pe+4)==0x8664
count=u16(pe+6);optional=pe+24;optionalSize=u16(pe+20)
assert u16(optional)==0x20b and u16(optional+68) in (10,11)
assert not u16(pe+22)&1,'Relocations stripped'
entry=u32(optional+16);base=struct.unpack_from('<Q',data,optional+24)[0];imageSize=u32(optional+56)
assert u32(optional+108)>=6 and entry>0
importRva,importSize=u32(optional+120),u32(optional+124)
sections=[]
for i in range(count):
 at=optional+optionalSize+i*40;name=data[at:at+8].rstrip(b'\0').decode('ascii','replace')
 sections.append(dict(name=name,size=u32(at+8),rva=u32(at+12),rawSize=u32(at+16),raw=u32(at+20),flags=u32(at+36)))
assert any(s['rva']<=entry<s['rva']+s['size'] and s['flags']&0x20000000 for s in sections),'Entry outside executable section'
def offset(rva,n):
 for s in sections:
  if s['rva']<=rva and rva+n<=s['rva']+s['rawSize']:
   at=s['raw']+rva-s['rva'];assert at+n<=len(data);return at
 raise AssertionError(('RVA has no file data',hex(rva),n))
if importRva or importSize:
 assert importRva and importSize>=20
 assert not any(data[offset(importRva,importSize):offset(importRva,importSize)+importSize]),'OS import descriptors present'
relocRva=u32(optional+152);relocSize=u32(optional+156)
assert relocRva and relocSize,'No base relocations: image cannot safely move'
at=offset(relocRva,relocSize);end=at+relocSize;relocations=0
while at<end:
 page=u32(at);size=u32(at+4);assert size>=8 and not size%4 and at+size<=end
 for item in range(at+8,at+size,2):
  raw=u16(item);kind=raw>>12
  if not kind:continue
  assert kind==10,('Non-DIR64 relocation',kind)
  target=page+(raw&0xfff);assert target+8<=imageSize
  original=struct.unpack_from('<Q',data,offset(target,8))[0]
  assert base<=original<base+imageSize,('Relocated pointer outside closed image',hex(original))
  relocations+=1
 at+=size
assert relocations
print(json.dumps(dict(file=str(p),bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),sections=count,entryRva=entry,imageBase=base,dir64Relocations=relocations,osImports=0),indent=2))

