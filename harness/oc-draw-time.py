"""Seconds from Enter on OpenCore's device-list entry until its picker shows text.
  oc-draw-time.py CHARON_EFI LABEL [extra prepare args]"""
import subprocess,sys,time
from PIL import Image
efi,label,*extra=sys.argv[1:];name='ocdraw-'+label
def vm(*a):return subprocess.run([sys.executable,'charon-vm.py',*a],capture_output=True,text=True).stdout
vm('prepare',name,efi,'--grub','grub-dl/grubx64.efi','--winboot','--opencore',*extra)
vm('wait',name,'menu announce','120');time.sleep(3)
vm('keys',name,'1.5','spc','ret','down','down');time.sleep(4);vm('keys',name,'0.1','ret');t0=time.time();result='never'
while time.time()-t0<45:
    vm('screen',name,'s.ppm')
    try:
        im=Image.open(name+'/s.ppm')
        if im.size==(2560,1600) and len(set(im.crop((0,0,800,300)).get_flattened_data()))>1:result=round(time.time()-t0,1);break
    except Exception:pass
    time.sleep(1)
vm('quit',name);print(label,result)
