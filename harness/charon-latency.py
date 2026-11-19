"""Key-to-speech latency of a running Charon test machine (r382).

  charon-latency.py NAME WAVNAME KEY [KEY ...]

For each key: note the size of the captured wav (QEMU wavcapture writes it
continuously), send the key over QMP, wait, then find the first audible frame
written after that point. Speech must be silent before the key (wait for the
previous utterance to end). Prints milliseconds per key and the median.
"""
from pathlib import Path
import sys,time,struct,statistics,importlib.util
base=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('cvm',base/'charon-vm.py');cvm=importlib.util.module_from_spec(spec)
sys.argv_saved=sys.argv;spec.loader.exec_module(cvm)
name,wav,keys=sys.argv[1],sys.argv[2],sys.argv[3:]
path=base/name/wav
head=path.read_bytes()[:44]
channels,rate,bits=struct.unpack('<H',head[22:24])[0],struct.unpack('<I',head[24:28])[0],struct.unpack('<H',head[34:36])[0]
frame=channels*bits//8
results=[]
cmd=cvm.qmp()
for key in keys:
    # quiet first: the last 300 ms of capture must be silent
    for _ in range(40):
        data=path.read_bytes();tail=data[-int(rate*0.3)*frame:]
        samples=struct.unpack('<%dh'%(len(tail)//2),tail[:len(tail)//2*2])
        if max(abs(s) for s in samples)<300:break
        time.sleep(0.25)
    start=path.stat().st_size
    cmd('send-key',{'keys':[{'type':'qcode','data':k} for k in key.split('+')]})
    time.sleep(3)
    data=path.read_bytes()[start-(start-44)%frame:]
    samples=struct.unpack('<%dh'%(len(data)//2),data[:len(data)//2*2])
    onset=None
    for i in range(0,len(samples),channels):
        if abs(samples[i])>300:onset=i//channels;break
    ms=None if onset is None else onset*1000/rate
    results.append(ms);print(key,'no sound' if ms is None else '%.0f ms'%ms)
    time.sleep(0.5)
ok=[r for r in results if r is not None]
if ok:print('median %.0f ms, max %.0f ms (%d keys)'%(statistics.median(ok),max(ok),len(ok)))
