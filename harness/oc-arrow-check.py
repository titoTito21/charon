"""Does OpenCore's picker react to arrows? Screens before/after Down.
  oc-arrow-check.py CHARON_EFI LABEL [prepare args]"""
import subprocess,sys,time
from PIL import Image,ImageChops
efi,label,*extra=sys.argv[1:];name='ocarrow-'+label
def vm(*a):return subprocess.run([sys.executable,'charon-vm.py',*a],capture_output=True,text=True).stdout
vm('prepare',name,efi,'--grub','grub-dl/grubx64.efi','--winboot','--opencore',*extra)
vm('wait',name,'menu announce','120');time.sleep(3)
vm('keys',name,'1.5','spc','ret','down','down');time.sleep(4);vm('keys',name,'0.1','ret');time.sleep(12)
vm('screen',name,'a.ppm');vm('keys',name,'0.3','down');time.sleep(3);vm('screen',name,'b.ppm')
vm('keys',name,'0.3','2');time.sleep(3);vm('screen',name,'c.ppm');vm('quit',name)
a,b,c=[Image.open(f'{name}/{x}.ppm').convert('RGB') for x in 'abc']
print(label,'down changed:',ImageChops.difference(a,b).getbbox(),'digit changed:',ImageChops.difference(b,c).getbbox())
