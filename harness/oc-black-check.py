"""Does OpenCore's built-in picker draw? Starts it from Charon's device list
and counts colours in the top-left of the screen 12 s later.
  oc-black-check.py CHARON_EFI LABEL [extra prepare args]"""
import subprocess,sys,time
efi,label,*extra=sys.argv[1:];name='ocblack-'+label
def vm(*a):return subprocess.run([sys.executable,'charon-vm.py',*a],capture_output=True,text=True).stdout
vm('prepare',name,efi,'--grub','grub-dl/grubx64.efi','--winboot','--opencore',*extra)
vm('wait',name,'menu announce','120');time.sleep(3)
vm('keys',name,'1.5','spc','ret','down','down');time.sleep(4);vm('keys',name,'0.1','ret');time.sleep(14)
vm('screen',name,'s.ppm');vm('quit',name)
from PIL import Image
im=Image.open(name+'/s.ppm');print(label,im.size,'colours top-left:',len(set(im.crop((0,0,800,300)).get_flattened_data())))
