"""r382 latency/stutter trial: Charon's resident reader in GRUB's menu.

  charon-r382-trial.py CHARON_EFI LABEL

Fresh machine with the GRUB test disk, starts GRUB from Charon's device list,
stops GRUB's countdown, measures key-to-speech latency (Down/Up) and counts
exact-zero runs of 1.5-150 ms between audible samples afterwards (gaps the
voice did not produce on purpose show up as such runs).
"""
import subprocess,sys,time,array
from pathlib import Path
base=Path(__file__).resolve().parent
efi,label=sys.argv[1:3]
name='r382-trial-'+label
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
import os
print(vm('prepare',name,efi,'--grub','grub-dl/grubx64.efi',*os.environ.get('CHARON_TRIAL_ARGS','').split()).strip().splitlines()[-1:])
print(vm('wait',name,'spacj','120').strip())
time.sleep(1)
vm('keys',name,'1.5','spc','ret','down','ret')
print(vm('wait',name,'GRUB','90').strip())
time.sleep(4)
vm('keys',name,'1','down')   # stop the countdown
time.sleep(4)
wav=base/name/'live.wav';start=wav.stat().st_size
out=subprocess.run([sys.executable,str(base/'charon-latency.py'),name,'live.wav']+['down','up']*5,capture_output=True,text=True)
print(out.stdout.strip() or out.stderr[-500:])
data=wav.read_bytes()[start-(start-44)%4:];a=array.array('h');a.frombytes(data[:len(data)//4*4]);left=a[0::2]
runs=[];i=0;n=len(left)
while i<n:
    if left[i]==0:
        j=i
        while j<n and left[j]==0:j+=1
        if i>0 and j<n and 72<=j-i<=7200:runs.append((j-i)/48)
        i=j
    else:i+=1
sound=sum(1 for x in left if abs(x)>200)/48000
print(f'{label}: zero runs 1.5-150 ms inside sound: {len(runs)}, total {sum(runs):.0f} ms, audible {sound:.1f} s')
def runs_in(seg):
    a=array.array('h');a.frombytes(seg[:len(seg)//4*4]);left=a[0::2];runs=[];i=0;n=len(left)
    while i<n:
        if left[i]==0:
            j=i
            while j<n and left[j]==0:j+=1
            if i>0 and j<n and 72<=j-i<=7200:runs.append((j-i)/48)
            i=j
        else:i+=1
    first=next((k for k,x in enumerate(left) if abs(x)>200),n);last=n-next((k for k,x in enumerate(reversed(left)) if abs(x)>200),n)
    return runs,first/48,(last-first)/48000
for attempt in range(2):
    time.sleep(2);start=wav.stat().st_size
    vm('keys',name,'0.1','ctrl+alt+r');time.sleep(25)
    seg=wav.read_bytes()[start-(start-44)%4:]
    runs,onset,span=runs_in(seg)
    big=[r for r in runs if r>=40]
    print(f'{label} read-all: onset {onset:.0f} ms, speech span {span:.1f} s, zero runs {len(runs)}, >=40 ms: {len(big)} ({", ".join("%.0f"%r for r in big[:12])})')
vm('quit',name)
