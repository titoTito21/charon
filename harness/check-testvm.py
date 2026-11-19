"""r396 (check-testvm.py DIR [chain]): open every device-list entry of outputs/Charon-testvm-r396.zip from its own
launcher command (+ QMP only), press Down once in the started manager, take a
screenshot and measure how much speech was heard.  check-testvm.py DIR"""
import sys,os,json,time,socket,subprocess,array
from pathlib import Path
d=Path(sys.argv[1]).resolve();out=Path(__file__).resolve().parent/'testvm-check';out.mkdir(exist_ok=True)
line=(d/'Uruchom-maszyne-testowa.bat').read_text().splitlines()[-1]
args=line.split('.exe" ',1)[1].split()
def q(cmd,a=None):
    s=socket.create_connection(('127.0.0.1',4466));f=s.makefile('rw');f.readline()
    f.write(json.dumps({'execute':'qmp_capabilities'})+'\n');f.flush();f.readline()
    f.write(json.dumps({'execute':cmd,**({'arguments':a} if a else {})})+'\n');f.flush()
    while True:
        r=json.loads(f.readline())
        if 'return' in r or 'error' in r:s.close();return r
def key(k):
    for down in (True,False):q('input-send-event',{'events':[{'type':'key','data':{'down':down,'key':{'type':'qcode','data':k}}}]});time.sleep(.05)
    time.sleep(.6)
names={2:'syslinux',3:'ipxe',4:'ventoy',5:'zfsbootmenu',6:'refind',7:'grub',8:'systemd-boot'}
# r396: managers chain loaded from the GRUB disk's menu (entry number -> name).
chain={8:'limine',9:'opencore',10:'clover',11:'memtest86plus',12:'windows-boot-manager'}
runs=[(p,n,0) for p,n in names.items()] if len(sys.argv)<3 else [(7,n,e) for e,n in chain.items()]
for pos,name,entry in runs:
    for n in ('firmware-vars.fd',):pass
    p=subprocess.Popen([str(d/'qemu/qemu-system-x86_64.exe'),*args,'-qmp','tcp:127.0.0.1:4466,server=on,wait=off'],cwd=d)
    for _ in range(60):
        try:q('query-status');break
        except OSError:time.sleep(.5)
    wav=out/f'{name}.wav';q('human-monitor-command',{'command-line':f'wavcapture {wav} charon_audio'})
    time.sleep(75);key('spc');key('ret');time.sleep(3)
    for _ in range(pos-1):key('down')
    time.sleep(2);key('ret')
    if entry:
        time.sleep(12)
        for _ in range(entry-1):key('down')
        key('ret')
        time.sleep(45 if name in ('opencore','clover') else 30)
    else:time.sleep(25)
    key('down');time.sleep(5)
    q('screendump',{'filename':str(out/f'{name}.ppm')})
    try:q('quit')
    except OSError:pass
    p.wait(30);time.sleep(2)
    b=wav.read_bytes();m=array.array('h',b[44:len(b)-(len(b)-44)%2])[::2]
    voiced=sum(1 for i in range(0,len(m)-2205,2205) if max(abs(x) for x in m[i:i+2205])>1500)
    print(name,'voiced 50ms blocks:',voiced,'of',len(m)//2205,flush=True)
