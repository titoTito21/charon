"""Malformed firmware inventories and exact, atomic one-time selection."""
from pathlib import Path
import struct,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import hak_efi_boot as efi

def option(label='Fixture'):
 return struct.pack('<IH',1,8)+label.encode('utf-16-le')+b'\0\0'+bytes([1,1,4,0,127,255,4,0])+b'options'
def inventory(payload):
 return efi.MAGIC+struct.pack('<HI',7,len(payload))+payload+struct.pack('<HI',65535,0)
data=inventory(option())
target,=efi.decode_inventory(data)
assert target.description=='Fixture'
for invalid in [data[:n] for n in range(len(data))]+[data+b'\0',b'other'+data,
 efi.MAGIC+data[8:-6]*2+data[-6:]]:
 try:efi.decode_inventory(invalid)
 except (ValueError,UnicodeError):pass
 else:raise AssertionError('Invalid inventory accepted')
for label in ['Zażółć gęślą','English','語言','🙂']:
 assert efi.decode_inventory(inventory(option(label)))[0].description==label
bad=bytearray(option());bad[6:8]=b'\0\xdc'
try:efi.decode_inventory(inventory(bad))
except UnicodeError:pass
else:raise AssertionError('Broken UTF-16 accepted')
with tempfile.TemporaryDirectory() as temp:
 esp=Path(temp);area=esp/'EFI/HAK';area.mkdir(parents=True)
 (area/'inventory.bin').write_bytes(data)
 efi.choose_once(esp,target.identifier)
 expected=struct.pack('<HI',7,len(target.data))+target.data
 assert (area/'next.bin').read_bytes()==expected
 (area/'inventory.bin').write_bytes(inventory(option('Replaced')))
 try:efi.choose_once(esp,target.identifier)
 except ValueError:pass
 else:raise AssertionError('Stale option accepted')
 assert (area/'next.bin').read_bytes()==expected
 assert not list(area.glob('HAKNEXT-*'))
print('PASS truncated/duplicate/invalid inventories, multilingual UTF-16, exact option identity and atomic request preservation')
