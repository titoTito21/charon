import re,numpy as np
t=open('deb2/serial.log','rb').read().decode('utf-8','replace')
for l in t.splitlines():
    if re.search('farewell|Wyłączanie|beep=',l):print(l[:100])
d=np.fromfile('deb2/live2.wav',dtype='<i2',offset=44)
d=d[:len(d)//2*2].reshape(-1,2).astype(np.float32).mean(1);sr=44100
def band(x,f):
    n=len(x);tt=np.arange(n)/sr
    return abs((x*np.exp(-2j*np.pi*f*tt)).sum())*2/n
w=sr//20;ev=[]
for k in range(len(d)//w):
    x=d[k*w:(k+1)*w];a,b=band(x,1175),band(x,784)
    if max(a,b)>250:ev.append((k/20,'1175' if a>b else '784',int(max(a,b))))
print(ev)
n=len(d)//w;e=np.sqrt((d[:n*w].reshape(n,w)**2).mean(1));act=e>150;segs=[];s=None
for i,a in enumerate(act):
    if a and s is None:s=i
    if not a and s is not None:segs.append((s/20,i/20));s=None
print(segs)
