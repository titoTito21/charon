"""r389: key-to-speech latency in Charon's own menu and in GRUB's text menu.
  charon-r389-latency.py CHARON_EFI NAME [N]
Starts the GRUB disk (device list position 2), stops its countdown, then
measures N Down/Up keys with charon-latency.py (first audible sample after
the key in the QEMU wavcapture). Env KEEP=1 leaves the machine running."""
import subprocess,sys,time,os,re
from pathlib import Path
base=Path(__file__).resolve().parent
efi,name=sys.argv[1],sys.argv[2];n=int(sys.argv[3]) if len(sys.argv)>3 else 8
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
def lat(*keys):
    out=subprocess.run([sys.executable,str(base/'charon-latency.py'),name,'live.wav',*keys],capture_output=True,text=True).stdout
    print(out.strip());sys.stdout.flush()
vm('prepare',name,efi,'--grub','grub-dl/grubx64.efi',*sys.argv[4:])
vm('wait',name,'menu announce','150');time.sleep(3)
vm('keys',name,'1.5','spc');time.sleep(5)   # stops Charon's own countdown
print('== Charon menu');lat(*(['down','up']*(n//2)))
vm('keys',name,'1.5','ret');time.sleep(5)
print('== Charon device list');lat(*(['down','up']*(n//2)))
vm('keys',name,'0.15','down','ret');time.sleep(6)
vm('keys',name,'0.3','down');time.sleep(3)   # stops GRUB's countdown
print('== GRUB text menu');lat(*(['down','up']*(n//2)))
vm('screen',name,'end.ppm')
if not os.environ.get('KEEP'):vm('quit',name)
