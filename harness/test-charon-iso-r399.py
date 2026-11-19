"""r399: boot outputs/Charon-r399.iso on OVMF with NO hard disk - as a CD (El Torito)
or written raw to a USB stick (hybrid MBR) - record Charon's audio with wavcapture
and screenshots, press Enter/Down to show the menu reacts.

  python test-charon-iso-r399.py cd|usb
"""
import importlib.util,json,shutil,subprocess,sys,time,wave,array
from pathlib import Path
base=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('cvm',base/'charon-vm.py');cvm=importlib.util.module_from_spec(spec)
a=sys.argv;sys.argv=['charon-vm.py'];spec.loader.exec_module(cvm);sys.argv=a
mode=sys.argv[1];iso=(base.parent/'outputs/Charon-r399.iso').resolve()
area=base/f'iso-r399-{mode}';area.mkdir(exist_ok=False)
shutil.copyfile(base/'charon-machine-integrated-r349/firmware-code.fd',area/'firmware-code.fd')
shutil.copyfile(base/'charon-machine-r347/firmware-vars.fd',area/'firmware-vars.fd')
cmd=[cvm.QEMU,'-name',f'Charon-iso-{mode}','-machine','q35,accel=tcg','-cpu','max','-smp','2','-m','2048','-nic','none','-display','sdl',
     '-drive','if=pflash,format=raw,readonly=on,file=firmware-code.fd','-drive','if=pflash,format=raw,file=firmware-vars.fd']
if mode=='cd':cmd+=['-drive',f'if=none,id=cd0,media=cdrom,readonly=on,file={iso}','-device','ide-cd,bus=ide.0,drive=cd0']
else:
    shutil.copyfile(iso,area/'stick.img')
    cmd+=['-device','qemu-xhci,id=xhci','-drive','if=none,id=stick,format=raw,file=stick.img','-device','usb-storage,bus=xhci.0,drive=stick']
cmd+=['-audiodev','dsound,id=charon_audio','-device','ich9-intel-hda','-device','hda-duplex,audiodev=charon_audio',
      '-qmp','tcp:127.0.0.1:4466,server=on,wait=off','-serial','file:serial.log']
p=subprocess.Popen(cmd,cwd=area)
for _ in range(50):
    try:q=cvm.qmp();q('human-monitor-command',{'command-line':'wavcapture '+str(area/'boot.wav')+' charon_audio'});break
    except OSError:time.sleep(0.2)
def shot(n):q('screendump',{'filename':str(area/(n+'.ppm'))})
def key(k):q('send-key',{'keys':[{'type':'qcode','data':k}]});time.sleep(0.3)
time.sleep(70);shot('menu')
key('down');time.sleep(6);key('down');time.sleep(6);shot('down2')
q('human-monitor-command',{'command-line':'stopcapture 0'});time.sleep(1)
try:q('quit')
except OSError:pass
p.wait(30)
w=wave.open(str(area/'boot.wav'));s=array.array('h',w.readframes(w.getnframes()))
sec=w.getframerate()*w.getnchannels()
loud=sum(1 for i in range(0,len(s),sec) if max(abs(x) for x in s[i:i+sec] or [0])>1500)
from PIL import Image
for n in ('menu','down2'):Image.open(area/(n+'.ppm')).save(area/(n+'.png'))
res={'mode':mode,'seconds':len(s)//sec,'peak':max(abs(x) for x in s),'seconds_with_sound':loud}
(area/'result.json').write_text(json.dumps(res,indent=1));print(res)
