from pathlib import Path
from unittest.mock import patch
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import hak_efi_volumes as efi

def encoded(value):return value.encode().hex() or '-'
record='ESP\t42\t1048576\t67108864\t'+'\t'.join(map(encoded,('/dev/disk/virtual/1/raw','/dev/disk/virtual/1/0','/EFI TEST','EFI test')))
text='HAK-ESP-1\n'+record+'\nEND\n'
selected,=efi.decode(text)
assert selected.mount=='/EFI TEST'
for invalid in ('',text.replace('END',''),text+record,text.replace('42','-1'),text.replace(record,record+'\n'+record)):
 try:efi.decode(invalid)
 except ValueError:pass
 else:raise AssertionError('Invalid inventory accepted')
with patch.object(efi,'discover',return_value=[selected]),patch.object(efi.subprocess,'run') as run:
 assert efi.open_volume(Path('helper'),selected)==Path('/EFI TEST')
 run.assert_not_called()
with patch.object(efi,'discover',return_value=[]),patch.object(efi.subprocess,'run') as run:
 try:efi.open_volume(Path('helper'),selected)
 except ValueError:pass
 else:raise AssertionError('Missing partition accepted')
 run.assert_not_called()
print('PASS ESP protocol completeness, duplicate identity, paths and no mount for missing/already-mounted partition')
