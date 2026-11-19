"""Start device-list entry POS (1-based) and print what the reader says.
  charon-r386-entry.py CHARON_EFI LABEL POS [prepare flags...]
Env: KEYS="right right" (sent 4 s apart after WAIT seconds, default 40)."""
import subprocess,sys,time,re,os
from pathlib import Path
base=Path(__file__).resolve().parent
efi,label,pos,*flags=sys.argv[1:];pos=int(pos);name='r386-entry-'+label
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
def said():
    t=(base/name/'serial.log').read_bytes().decode('utf-8','replace')
    return [m.group(1) for m in re.finditer(r'CHARON speak\r?\n(?:(?:pl|en)\r?\n)?([^\r\n]*)',t) if m.group(1).strip() and not m.group(1).startswith('CHARON')]
vm('prepare',name,efi,'--grub','grub-dl/grubx64.efi',*flags)
vm('wait',name,'menu announce','150');time.sleep(3)
vm('keys',name,'1.5','spc','ret',*(['down']*(pos-1)));time.sleep(4)
mark=len(said());vm('keys',name,'0.15','ret',*os.environ.get('BURST','').split());time.sleep(float(os.environ.get('WAIT','40')))
print('==',label);[print('  ',x[:220]) for x in said()[mark:]]
for k in os.environ.get('KEYS','').split():
    m=len(said());vm('keys',name,'0.1',k);time.sleep(4);print('==',k);[print('  ',x[:220]) for x in said()[m:]]
vm('screen',name,'end.ppm')
if not os.environ.get('KEEP'):vm('quit',name)
