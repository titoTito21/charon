"""Native ABI/Python parity and FAT persistence, against a scratch ESP only."""
from pathlib import Path
import ctypes,struct,subprocess,sys
root=Path(sys.argv[1]);disk=Path(sys.argv[2]);mount=Path(sys.argv[3])
sys.path.insert(0,str(root/'tools'))
import hak_efi_boot as efi
library=ctypes.CDLL(str(root/'lib/libhaiku-accessibility-client.so'))
read=library.hak_efi_boot_devices_read
read.argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_int64),ctypes.POINTER(ctypes.c_int32)]
choose=library.hak_efi_boot_start_once;choose.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
esp=str(mount).encode();expected=None
for phase in ('write','read-only','cleanup'):
 subprocess.run(['mount']+(['-ro'] if phase=='read-only' else [])+[str(disk),str(mount)],check=True)
 try:
  targets=efi.read_targets(mount)
  rows='format\t1\tuefi-firmware-options\n'+''.join('target\t'+t.identifier+'\t'+t.description+'\n' for t in targets)
  output=ctypes.create_string_buffer(65536);used=ctypes.c_int64(-1);count=ctypes.c_int32(-1)
  assert read(esp,output,len(output),ctypes.byref(used),ctypes.byref(count))==0
  assert output.raw[:used.value].decode()==rows and count.value==len(targets)+1
  if phase=='write':
   small=ctypes.create_string_buffer(b'abc');used.value=-7;count.value=-9
   assert read(esp,small,3,ctypes.byref(used),ctypes.byref(count))<0
   assert small.raw==b'abc\0' and used.value==-7 and count.value==-9
   assert read(None,small,3,ctypes.byref(used),ctypes.byref(count))<0
   target=next(t for t in targets if t.number==0xf001)
   assert choose(esp,target.identifier.encode())==0
   expected=struct.pack('<HI',target.number,len(target.data))+target.data
   assert choose(esp,(target.identifier[:-1]+('0' if target.identifier[-1]!='0' else '1')).encode())<0
   assert (mount/'EFI/HAK/next.bin').read_bytes()==expected
   assert not list((mount/'EFI/HAK').glob('HAKNEXT-*'))
  elif phase=='read-only':
   assert (mount/'EFI/HAK/next.bin').read_bytes()==expected
  else:
   (mount/'EFI/HAK/next.bin').unlink(missing_ok=True)
   import os;os.sync()
  print('PASS native EFI ABI',phase,'Python identity parity, FAT persistence/cleanup',flush=True)
 finally:subprocess.run(['unmount',str(mount)],check=True)
