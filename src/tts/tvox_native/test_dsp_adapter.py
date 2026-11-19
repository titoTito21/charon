"""Differential test of real compiled adapter against original Python + C DSP.

Pass adapter shared library (linked with upstream/tvox_dsp.c). Requires original
TVox native library available to Python; refuses a silent Python substitution.
"""
import ctypes as C
import hashlib,json,math,sys
from pathlib import Path
from generate_pl import HERE,VOICE,load

for path,digest in json.loads((HERE/'DSP-ADAPTER-PROVENANCE.json').read_text()).items():
    assert hashlib.sha256((VOICE/path).read_bytes()).hexdigest()==digest,path
e=load('engine');k=e.klatt
assert k.native_ready(),'Original native TVox DSP required for differential oracle'
assert k._np is not None,'Original native render requires numpy'
lib=C.CDLL(str(Path(sys.argv[1]).resolve()))
fn=lib.tvox_dsp_render_pl
PD=C.POINTER(C.c_double);PS=C.POINTER(C.c_short)
fn.argtypes=[PD,C.c_size_t,C.c_size_t,C.c_size_t,C.c_double,C.c_double,PD,C.c_size_t,PS,C.c_size_t,C.POINTER(C.c_size_t)]
fn.restype=C.c_int
size=lib.tvox_dsp_stream_size;size.restype=C.c_size_t
begin=lib.tvox_dsp_stream_begin
begin.argtypes=[C.c_void_p,PD,C.c_size_t,C.c_size_t,C.c_size_t,C.c_double,C.c_double]
render=lib.tvox_dsp_stream_render
render.argtypes=[C.c_void_p,PS,C.c_size_t,C.POINTER(C.c_size_t)]
cancel=lib.tvox_dsp_stream_cancel;cancel.argtypes=[C.c_void_p]
v=e.klatt_voice(e.get_voice('Adam')[1],'default');eq=e.learned()['eq']
keys=k.FRAME_KEYS+k.TRACK_KEYS+('rd','creak')
checks=0
for leading,trailing,pitch,volume in [(0,0,1,1),(20,40,1,.7),(4,11,.7,1),(8,12,1.7,.3)]:
    frames=[]
    for i in range(leading+35+trailing):
        silent=i<leading or i>=leading+35
        fr=dict(zip(keys,[120*pitch,0 if silent else 58,0 if silent else 20,
            0 if silent or i%3 else 45,45,40,35,30,25,15,
            500,70,1500,100,2500,150,3500,200,4500,250,5500,300,
            250,100,250,100,v['rd'],0]))
        fr['f0']*=1+.08*math.sin(i*.3)
        fr['f1']+=40*math.sin(i*.7)
        fr['f2']+=130*math.cos(i*.2)
        fr['rd']=.5+(i%40)*.05
        fr['creak']=.3 if i%5==0 else 0
        frames.append(fr)
    a=(C.c_double*(len(frames)*28))(*(fr[key] for fr in frames for key in keys))
    needed=C.c_size_t()
    assert fn(a,len(frames),leading,trailing,pitch,volume,None,0,None,0,C.byref(needed))==1
    n=needed.value;scratch=(C.c_double*n)();out=(C.c_short*n)(*([12345]*n))
    assert fn(a,len(frames),leading,trailing,pitch,volume,scratch,n,out,n-1,C.byref(needed))==1
    assert all(x==12345 for x in out)
    assert fn(a,len(frames),leading,trailing,pitch,volume,scratch,n,out,n,C.byref(needed))==0
    skip=max(0,leading-e.EDGE_LEAD);tail=max(0,trailing-e.EDGE_TAIL)
    x=k.render(frames[:len(frames)-tail],rd=v['rd'],tilt=v['tilt'],flutter=v['flutter'],
        jitter=v.get('jitter',.01),shimmer=v.get('shimmer',.03),shelf=v.get('shelf',0),
        src_eq=e._src_eq('pl',v),skip=skip,db1_open=v.get('db1_open'))
    # Scalar reference is an original supported EQ implementation. FFT
    # convolution is an optimization with its own stated <1e-7 difference.
    saved=k._np;k._np=None
    try:x=k.EQ(eq['fc'],[g*.5 for g in eq[v['sex']]],eq['q']).process(list(x))
    finally:k._np=saved
    g=e.TitanVoice('Adam',lang='pl',pitch=pitch)._klatt_gain('pl')*volume
    expected=k.to_pcm16(x,gain=g)+b'\0\0'*(tail*110)
    oracle=(C.c_short*n).from_buffer_copy(expected)
    assert len(expected)==n*2
    assert max(abs(p-q) for p,q in zip(out,oracle))<=1
    for block in (1,17,480,4096):
        state=(C.c_double*((size()+7)//8))()
        assert begin(state,a,len(frames),leading,trailing,pitch,volume)==0
        collected=[];calls=0
        while True:
            packet=(C.c_short*block)();written=C.c_size_t(999)
            # Polling pauses the stream without changing its future samples.
            assert render(state,None,0,C.byref(written))==0 and written.value==0
            status=render(state,packet,block,C.byref(written))
            assert status in (0,1) and written.value<=block
            collected.extend(packet[:written.value]);calls+=1
            assert calls<len(frames)*111+2
            if status==1:break
        assert collected==list(out),(block,len(collected),n)
        # Cancel midway, never emit stale buffered samples; new session starts
        # with the same seed and filter states as a completely fresh stream.
        assert begin(state,a,len(frames),leading,trailing,pitch,volume)==0
        render(state,packet,block,C.byref(written));cancel(state)
        assert render(state,packet,block,C.byref(written))==1 and written.value==0
        assert begin(state,a,len(frames),leading,trailing,pitch,volume)==0
        restarted=[]
        while True:
            status=render(state,packet,block,C.byref(written))
            restarted.extend(packet[:written.value])
            if status==1:break
            assert status==0
        assert restarted==collected
    # Finite validation must fail before modifying output or required count.
    a[0]=math.nan;needed.value=999
    before=bytes(out)
    assert fn(a,len(frames),leading,trailing,pitch,volume,scratch,n,out,n,C.byref(needed))==-1
    assert bytes(out)==before and needed.value==999
    checks+=1
print('PASS',checks,'original DSP/EQ/trim/PCM differential cases; stream blocks1/17/480/4096 exact; pause/cancel/restart; capacity/failure atomicity')
