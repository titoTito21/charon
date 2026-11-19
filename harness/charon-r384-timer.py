"""r384 countdown trial: enable both countdown check boxes in the screen reader
submenu, start systemd-boot (30 s countdown) and analyse speech and tones.

  charon-r384-timer.py CHARON_EFI LABEL
"""
import subprocess,sys,time,re,array,math
from pathlib import Path
base=Path(__file__).resolve().parent
efi,label=sys.argv[1:3]
grub=len(sys.argv)>3 and sys.argv[3]=='grub'
name='r384-timer-'+label
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
def announces():
    t=(base/name/'serial.log').read_bytes().decode('utf-8','replace')
    return [p.strip().split('\n')[0] for p in t.split('menu announce')[1:]]
vm('prepare',name,efi,'--grub','grub-dl/grubx64.efi','--winboot','--sdboot')
vm('wait',name,'spacj','120');time.sleep(1)
# main -> settings -> screen reader submenu (second to last row)
vm('keys',name,'1.5','spc','down','down','ret','end','up','ret','down','down','down','spc','down','spc','esc','esc','up','up','ret','down',*([] if grub else ['down']))
time.sleep(3);print([a[:90] for a in announces()[-9:]])
vm('wait',name,'pozycja 2 z 3' if grub else 'pozycja 3 z 3','20')
wav=base/name/'live.wav';start=wav.stat().st_size
vm('keys',name,'0.1','ret')
time.sleep(30 if grub else 42)
t=(base/name/'serial.log').read_bytes().decode('utf-8','replace')
said=[m.group(1) for m in re.finditer(r'CHARON speak\r?\n(?:(?:pl|en)\r?\n)?([^\r\n]*)',t) if m.group(1).strip() and not m.group(1).startswith('CHARON')]
i=max((k for k,x in enumerate(said) if x.startswith('Uruchamiam')),default=0)
for x in said[i:]:print('  ',x[:160])
data=wav.read_bytes()[start-(start-44)%4:];a=array.array('h');a.frombytes(data[:len(data)//4*4]);left=a[0::2]
# tone detection: Goertzel energy at 1000/880 Hz against total energy, 20 ms windows
def goertzel(seg,f):
    w=2*math.pi*f/48000;c=2*math.cos(w);s1=s2=0.0
    for x in seg:s0=x+c*s1-s2;s2=s1;s1=s0
    return s1*s1+s2*s2-c*s1*s2
win=960;tones=[];cur=None
for w0 in range(0,len(left)-win,win):
    seg=left[w0:w0+win];total=sum(x*x for x in seg)
    if total<win*500*500:cur=None;continue
    e1=goertzel(seg,1000)*2/win;e2=goertzel(seg,880)*2/win
    kind=1000 if e1>0.6*total else 880 if e2>0.6*total else 0
    if kind and cur and cur[0]==kind and cur[2]==w0-win:cur[2]=w0
    elif kind:cur=[kind,w0,w0];tones.append(cur)
    else:cur=None
ticks=[(round(t0/48000,2),k,round((t1-t0)/48+20)) for k,t0,t1 in tones]
print('tones (s, Hz, ms):',ticks[:60])
vm('quit',name)
