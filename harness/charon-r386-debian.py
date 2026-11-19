"""r386: Debian mini.iso from the CD drive, GRUB arrows, then into Linux.

  charon-r386-debian.py CHARON_EFI LABEL [KEY ...]

Starts the DVD-ROM entry from Charon's device list, waits for Debian's GRUB,
then sends each KEY (4 s apart) and prints what the reader said after it.
KEY 'wait:N' sleeps N seconds. The VM stays up with KEEP=1.
"""
import subprocess,sys,time,re,os
from pathlib import Path
base=Path(__file__).resolve().parent
efi,label,*keys=sys.argv[1:]
name='r386-debian-'+label
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
def said():
    t=(base/name/'serial.log').read_bytes().decode('utf-8','replace')
    return [m.group(1) for m in re.finditer(r'CHARON speak\r?\n(?:(?:pl|en)\r?\n)?([^\r\n]*)',t)
            if m.group(1).strip() and not m.group(1).startswith('CHARON')]
env=dict(os.environ,CHARON_CDROM=os.environ.get('CHARON_ISO',str(base/'debian-dl/mini.iso')))
subprocess.run([sys.executable,str(base/'charon-vm.py'),'prepare',name,efi,'--grub','grub-dl/grubx64.efi',*os.environ.get('CHARON_ARGS','').split()],env=env,capture_output=True,text=True)
vm('wait',name,'spacj','150');time.sleep(1)
vm('keys',name,'1.5','spc','ret');time.sleep(4)
for _ in range(8):
    if 'DVD' in (said() or [''])[-1]:break
    vm('keys',name,'0.1','down');time.sleep(2.5)
mark=len(said());vm('keys',name,'0.1','ret')
vm('wait',name,'Install','60');time.sleep(6)
vm('screen',name,'k-start.ppm') if os.environ.get('SHOTS') else None;print('== start');[print('  ',x[:300]) for x in said()[mark:]]
shot=0
for k in keys:
    mark=len(said())
    if k.startswith('wait:'):time.sleep(float(k[5:]))
    else:vm('keys',name,'0.1',k);time.sleep(4)
    print('==',k);[print('  ',x[:300]) for x in said()[mark:]]
    if os.environ.get('SHOTS'):vm('screen',name,f'k{shot}.ppm');shot+=1
vm('screen',name,'end.ppm')
if not os.environ.get('KEEP'):vm('quit',name)
