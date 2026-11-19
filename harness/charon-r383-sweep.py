"""r383 regression sweep: start a boot manager from Charon's device list
(third entry: the GPT disk) and record what the reader says.

  charon-r383-sweep.py CHARON_EFI LABEL FLAG [KEY ...]
FLAG: --sdboot | --limine | --opencore | --memtest | --clover | none (Windows Boot Manager)
Speech is taken from the serial trace; the language line after "CHARON speak"
may be glued to the previous line, so it is optional here.
"""
import subprocess,sys,time,re,os
from pathlib import Path
base=Path(__file__).resolve().parent
efi,label,flag,*keys=sys.argv[1:]
name='r383-sweep-'+label
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
extra=[] if flag=='none' else [flag]
vm('prepare',name,efi,'--grub','grub-dl/grubx64.efi','--winboot',*extra,*os.environ.get('CHARON_SWEEP_ARGS','').split())
vm('wait',name,'spacj','120');time.sleep(1)
vm('keys',name,'1.5','spc','ret','down','down');time.sleep(3)
vm('wait',name,'pozycja 3 z 3','20');vm('keys',name,'0.1','ret')
if os.environ.get('CHARON_SWEEP_UNTIL'):
    vm('wait',name,os.environ['CHARON_SWEEP_UNTIL'],'120');time.sleep(2)
else:time.sleep(float(os.environ.get('CHARON_SWEEP_WAIT','45')))
for k in keys:
    vm('keys',name,'0.1',k);time.sleep(4)
t=(base/name/'serial.log').read_bytes().decode('utf-8','replace')
said=[m.group(1) for m in re.finditer(r'CHARON speak\r?\n(?:(?:pl|en)\r?\n)?([^\r\n]*)',t)
      if m.group(1).strip() and not m.group(1).startswith('CHARON')]
i=max((k for k,x in enumerate(said) if x.startswith('Uruchamiam')),default=0)
print('==',label)
for x in said[i:]:print('  ',x[:230])
vm('screen',name,'end.ppm')
if not os.environ.get('CHARON_SWEEP_KEEP'):vm('quit',name)
