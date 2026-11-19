"""Actual native Adam EN frames versus original TVox engine, no cached speech."""
import ctypes as C,sys,random,math,json,hashlib
from pathlib import Path
from generate_acoustic import engine as e
from generate_pl import HERE,VOICE
from generate_en import original as g2p
for path,digest in json.loads((HERE/'EN-ACOUSTIC-PROVENANCE.json').read_text()).items():
    assert hashlib.sha256((VOICE/path).read_bytes()).hexdigest()==digest,path
fn=C.CDLL(str(Path(sys.argv[1]).resolve())).tvox_en_frames_ex
fn.argtypes=[C.c_char_p,C.c_size_t,C.c_double,C.c_double,C.POINTER(C.c_double),C.c_size_t]+[C.POINTER(C.c_size_t)]*3
keys=e.klatt.FRAME_KEYS+e.klatt.TRACK_KEYS+('rd','creak')
voice=e.get_voice('Adam')[1]
worst=0
def check(text,rate=1,pitch=1):
    global worst
    phrases=e.text_to_phones(text,'en')
    segs=e.build_segments(phrases,'en',rate*voice['rate'],voice['sex'],rhythm=True,voice='Adam')
    tracks=e._formant_tracks_py(segs)
    f0=e.pitch_track(segs,phrases,e._klatt_base_f0(voice,'en')*pitch,voice['range'],'en')
    frames=e.build_frames(segs,tracks,f0,voice,lang='en')
    data=text.encode();n=C.c_size_t();lead=C.c_size_t();tail=C.c_size_t()
    args=[data,len(data),rate,pitch]
    assert fn(*args,None,0,C.byref(n),C.byref(lead),C.byref(tail))==1
    assert n.value==len(frames),(text,rate,'length',n.value,len(frames))
    assert (lead.value,tail.value)==(segs[0].n,segs[-1].n if len(segs)>1 else 0)
    out=(C.c_double*(n.value*28))()
    assert fn(*args,out,n.value,C.byref(n),C.byref(lead),C.byref(tail))==0
    for i,frame in enumerate(frames):
        for j,key in enumerate(keys):
            actual=out[i*28+j];expected=frame[key];err=abs(actual-expected)
            worst=max(worst,err)
            assert math.isclose(actual,expected,rel_tol=2e-11,abs_tol=2e-9),(text,rate,pitch,i,key,actual,expected)
    before=bytes(out)
    assert fn(*args,out,n.value-1,C.byref(n),C.byref(lead),C.byref(tail))==1
    assert bytes(out)==before
texts=['a','test','Haiku Accessibility Kit boot manager.','Firmware initialized.','USB device, partition 2, Linux.','Do you want to continue?','Peter started getting better water bottles.','unmuted checkboxes, minimised subagents, initialised taskbar','']
for text in texts:
    for rate,pitch in [(1,1),(.7,.8),(1.6,1.3)]:check(text,rate,pitch)
rng=random.Random(346)
pool=list(g2p._load())+['USB','UEFI','Haiku','the','a','123','0.05','2GB','Dr.','etc.']
for _ in range(300):
    check(' '.join(rng.choices(pool,k=rng.randrange(1,11)))+rng.choice(['.','?','!',',',';',':']),rng.choice([.5,.8,1.,1.3,2.]),rng.choice([.6,1.,1.4]))
for data,rate,pitch in [(b'\xff',1,1),(b'a\0b',1,1),(b'a',0,1),(b'a',11,1),(b'a',1,0),(b'a',1,math.nan),(b'a',math.inf,1),(b'a'*65537,1,1)]:
    out=(C.c_double*28)(*([42]*28));n=C.c_size_t(777);lead=C.c_size_t(888);tail=C.c_size_t(999)
    assert fn(data,len(data),rate,pitch,out,1,C.byref(n),C.byref(lead),C.byref(tail))==-1
    assert list(out)==[42]*28 and (n.value,lead.value,tail.value)==(777,888,999)
print('PASS',len(texts)*3+300,'actual native acoustic frame cases; maximum absolute error',worst)
