"""r389 countdown check with default settings (tones on since r389):
Charon's own countdown, then systemd-boot's 30 s countdown read by the
reader. Counts 'reader countdown tick/end' traces and measures 1000 Hz /
880 Hz tones in the QEMU capture (Goertzel, 20 ms windows).
  charon-r389-timer.py CHARON_EFI NAME"""
import subprocess,sys,time,re,array,math,struct
from pathlib import Path
base=Path(__file__).resolve().parent
efi,name=sys.argv[1:3]
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
def tones(path,start):
    data=path.read_bytes();rate=struct.unpack('<I',data[24:28])[0]
    data=data[start-(start-44)%4:];a=array.array('h');a.frombytes(data[:len(data)//4*4]);left=a[0::2]
    def goertzel(seg,f):
        w=2*math.pi*f/rate;c=2*math.cos(w);s1=s2=0.0
        for x in seg:s0=x+c*s1-s2;s2=s1;s1=s0
        return s1*s1+s2*s2-c*s1*s2
    win=rate//50;found=[];cur=None
    for w0 in range(0,len(left)-win,win):
        seg=left[w0:w0+win];total=sum(x*x for x in seg)
        if total<win*300*300:cur=None;continue
        e1=goertzel(seg,1000)*2/win;e2=goertzel(seg,880)*2/win
        kind=1000 if e1>0.5*total else 880 if e2>0.5*total else 0
        if kind and (not cur or cur[0]!=kind):cur=[kind,w0/rate,0];found.append(cur)
        if kind:cur[2]+=20
        else:cur=None
    return found
vm('prepare',name,efi,'--grub','grub-dl/grubx64.efi','--sdboot')
wav=base/name/'live.wav'
vm('wait',name,'menu announce','150');start=wav.stat().st_size
time.sleep(13)   # Charon's own 10 s countdown runs out; firmware boot order continues
own=tones(wav,start)
print('Charon countdown tones:',[(k,round(t,1),ms) for k,t,ms in own])
t=(base/name/'serial.log').read_bytes().decode('utf-8','replace')
print('serial ticks so far',t.count('reader countdown tick'))
vm('quit',name)
time.sleep(2)
# systemd-boot from the device list: Space, Enter, last entry, Enter
name2=name+'-sd'
vm('prepare',name2,efi,'--grub','grub-dl/grubx64.efi','--sdboot')
vm('wait',name2,'menu announce','150');time.sleep(2)
vm('keys',name2,'1.5','spc','ret','end');time.sleep(3)
wav=base/name2/'live.wav';start=wav.stat().st_size
vm('keys',name2,'0.1','ret');time.sleep(40)
t=(base/name2/'serial.log').read_bytes().decode('utf-8','replace')
print('systemd-boot: ticks',t.count('reader countdown tick'),'ends',t.count('reader countdown end'))
found=tones(wav,start)
print('tones:',len([f for f in found if f[0]==1000]),'x1000 Hz,',[(k,round(s,1),ms) for k,s,ms in found if k==880],'880 Hz')
vm('quit',name2)
