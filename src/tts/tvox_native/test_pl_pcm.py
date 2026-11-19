"""Real UTF8 -> native Adam PL frames -> original native DSP -> PCM parity.

Both shared libraries are required. No utterance recordings are input data.
This tests synchronous sentence rendering, not an EFI audio driver or scheduler.
"""
import ctypes as C,sys,wave
from pathlib import Path
from generate_acoustic import engine as e
k=e.klatt
assert k.native_ready() and k._np is not None
PD=C.POINTER(C.c_double);SZ=C.POINTER(C.c_size_t)
frontend=C.CDLL(str(Path(sys.argv[1]).resolve())).tvox_pl_frames_ex
frontend.argtypes=[C.c_char_p,C.c_size_t,C.c_double,C.c_double,PD,C.c_size_t,SZ,SZ,SZ]
render=C.CDLL(str(Path(sys.argv[2]).resolve())).tvox_dsp_render_pl
render.argtypes=[PD,C.c_size_t,C.c_size_t,C.c_size_t,C.c_double,C.c_double,PD,C.c_size_t,C.POINTER(C.c_short),C.c_size_t,SZ]
v=e.klatt_voice(e.get_voice('Adam')[1],'default');eq=e.learned()['eq']
texts=['Menedżer rozruchu Haiku Accessibility Kit.','Dysk USB, partycja druga, Linux.',
       'Czy chcesz kontynuować?','Firmware załadowany.','Ustawienia głosu Adam.']
maximum=0
for text,rate,pitch in [(t,r,p) for t in texts for r,p in [(1.,1.),(.8,.9),(1.4,1.2)]]:
    data=text.encode();count=C.c_size_t();lead=C.c_size_t();tail=C.c_size_t()
    pointers=(C.byref(count),C.byref(lead),C.byref(tail))
    assert frontend(data,len(data),rate,pitch,None,0,*pointers)==1
    frames=(C.c_double*(count.value*28))()
    assert frontend(data,len(data),rate,pitch,frames,count.value,*pointers)==0
    samples=C.c_size_t()
    args=(frames,count.value,lead.value,tail.value,pitch,1.)
    assert render(*args,None,0,None,0,C.byref(samples))==1
    scratch=(C.c_double*samples.value)();out=(C.c_short*samples.value)()
    assert render(*args,scratch,samples.value,out,samples.value,C.byref(samples))==0
    # Entire oracle frontend and DSP operate on original Python source data.
    phrases=e.text_to_phones(text,'pl')
    segs=e.build_segments(phrases,'pl',rate*v['rate'],v['sex'],rhythm=True,voice='Adam')
    f=e.build_frames(segs,e._formant_tracks_py(segs),e.pitch_track(segs,phrases,e._klatt_base_f0(v,'pl')*pitch,v['range'],'pl'),v,lang='pl')
    skip=max(0,segs[0].n-e.EDGE_LEAD);trim=max(0,segs[-1].n-e.EDGE_TAIL)
    x=k.render(f[:len(f)-trim],rd=v['rd'],tilt=v['tilt'],flutter=v['flutter'],jitter=v.get('jitter',.01),shimmer=v.get('shimmer',.03),shelf=v.get('shelf',0),src_eq=e._src_eq('pl',v),skip=skip,db1_open=v.get('db1_open'))
    saved=k._np;k._np=None
    try:x=k.EQ(eq['fc'],[g*.5 for g in eq[v['sex']]],eq['q']).process(list(x))
    finally:k._np=saved
    pcm=k.to_pcm16(x,gain=e.TitanVoice('Adam',lang='pl',pitch=pitch)._klatt_gain('pl'))+b'\0\0'*(trim*110)
    assert len(pcm)==samples.value*2
    oracle=(C.c_short*samples.value).from_buffer_copy(pcm)
    error=max(abs(a-b) for a,b in zip(out,oracle));maximum=max(maximum,error)
    assert error<=1,(text,rate,pitch,error)
    # Optional local evidence artifact only, never shipped as a speech backend.
    if len(sys.argv)>3 and text==texts[0] and rate==pitch==1:
        with wave.open(sys.argv[3],'wb') as wav:
            wav.setparams((1,2,22050,0,'NONE','not compressed'));wav.writeframes(bytes(out))
print('PASS',len(texts)*3,'live text-to-PCM original Adam PL comparisons; maximum PCM LSB error',maximum)
