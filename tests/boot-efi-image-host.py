"""Verify the relocatable x86-64 EFI artifact, including relocation width."""
from pathlib import Path
import struct,sys

data=Path(sys.argv[1]).read_bytes()
assert data[:2]==b'MZ'
pe=struct.unpack_from('<I',data,0x3c)[0]
assert data[pe:pe+4]==b'PE\0\0'
machine,sections,_,_,_,optional_size,flags=struct.unpack_from('<HHIIIHH',data,pe+4)
assert machine==0x8664 and flags&2
optional=pe+24
assert struct.unpack_from('<H',data,optional)[0]==0x20b
assert struct.unpack_from('<H',data,optional+68)[0]==10
entry=struct.unpack_from('<I',data,optional+16)[0]
rows=[]
for i in range(sections):
 row=struct.unpack_from('<8sIIIIIIHHI',data,optional+optional_size+i*40)
 _,size,address,raw_size,raw_offset,_,_,_,_,attributes=row
 assert raw_offset+raw_size<=len(data) if raw_size else True
 rows.append((address,size,raw_offset,raw_size,attributes))
assert any(address<=entry<address+size and attributes&0x20000000 for address,size,_,_,attributes in rows)
def offset(rva,size):
 for address,_,raw,raw_size,_ in rows:
  if address<=rva and rva+size<=address+raw_size:return raw+rva-address
 raise AssertionError(('RVA outside file-backed image',rva,size))
reloc_rva,reloc_size=struct.unpack_from('<II',data,optional+112+5*8)
assert reloc_size>=8
at=offset(reloc_rva,reloc_size);end=at+reloc_size;count=0
while at<end:
 page,length=struct.unpack_from('<II',data,at)
 assert length>=8 and length%2==0 and at+length<=end
 for p in range(at+8,at+length,2):
  entry=struct.unpack_from('<H',data,p)[0];kind=entry>>12
  assert kind in (0,10),('non-64-bit relocation',kind)
  if kind:
   offset(page+(entry&4095),8);count+=1
 at+=length
assert count>0 and at==end
print('PASS x86-64 EFI subsystem, executable entry, section bounds and',count,'DIR64 relocations')
