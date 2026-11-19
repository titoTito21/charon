"""Resumed whole-utterance frames vs actual original TVox, cancel/OOM recovery.
Uses native host allocator: Python callbacks must not run on an alternate stack.
"""
import ctypes as C,sys,math
from pathlib import Path
from generate_acoustic import engine as e
lib=C.CDLL(str(Path(sys.argv[1]).resolve()))
SZ=C.POINTER(C.c_size_t);PD=C.POINTER(C.c_double)
begin=lib.tvox_frontend_begin_en;begin.argtypes=[C.c_char_p,C.c_size_t,C.c_double,C.c_double]
pump=lib.tvox_frontend_pump;pump.argtypes=[C.c_size_t,SZ]
view=lib.tvox_frontend_frames;view.argtypes=[C.POINTER(PD),SZ,SZ,SZ]
cancel=lib.tvox_frontend_cancel
lib.tvox_test_setup.argtypes=[C.c_size_t,C.c_size_t]
lib.tvox_test_live.restype=C.c_size_t
keys=e.klatt.FRAME_KEYS+e.klatt.TRACK_KEYS+('rd','creak');voice=e.get_voice('Adam')[1]
def run(text,budget):
    assert lib.tvox_test_setup(0,0)==0
    data=text.encode();assert begin(data,len(data),1,1)==0
    assert begin(data,len(data),1,1)==-4
    out=PD();n=C.c_size_t(777);a=C.c_size_t(888);b=C.c_size_t(999)
    assert view(C.byref(out),C.byref(n),C.byref(a),C.byref(b))==0
    assert (n.value,a.value,b.value)==(777,888,999)
    steps=0
    while True:
        used=C.c_size_t(999);status=pump(budget,C.byref(used));steps+=1
        assert used.value<=budget
        if status:break
        assert used.value==budget and steps<2000000
    assert status==1,(text,status)
    assert pump(budget,C.byref(used))==1 and used.value==0
    assert view(C.byref(out),C.byref(n),C.byref(a),C.byref(b))==1
    phrases=e.text_to_phones(text,'en');seg=e.build_segments(phrases,'en',voice['rate'],voice['sex'],rhythm=True,voice='Adam')
    frames=e.build_frames(seg,e._formant_tracks_py(seg),e.pitch_track(seg,phrases,e._klatt_base_f0(voice,'en'),voice['range'],'en'),voice,lang='en')
    assert (n.value,a.value,b.value)==(len(frames),seg[0].n,seg[-1].n if len(seg)>1 else 0)
    for i,frame in enumerate(frames):
        for j,key in enumerate(keys):assert math.isclose(out[i*28+j],frame[key],abs_tol=2e-9,rel_tol=2e-11),(text,budget,i,key)
    assert cancel()==0 and lib.tvox_test_live()==0
    return steps
checks=0
for text in ['a','Haiku boot manager.','USB device, partition 2, Linux.','Do you want to continue?','']:
    for budget in [1,7,64,4096]:
        steps=run(text,budget);checks+=1
        assert steps>1 or budget==4096
for after in [0,1,10,100,1000]:
    assert lib.tvox_test_setup(0,0)==0
    data=b'Menedzer rozruchu Haiku.';assert begin(data,len(data),1,1)==0
    used=C.c_size_t()
    for _ in range(after):assert pump(1,C.byref(used))==0
    assert cancel()==0 and lib.tvox_test_live()==0
for fail,stop,data,expected in [(2,0,b'a',-2),(0,1,b'a',-3),(0,1000,b'Haiku',-3),(0,0,b'\xff',-1)]:
    assert lib.tvox_test_setup(fail,stop)==0
    assert begin(data,len(data),1,1)==0
    while True:
        status=pump(7,C.byref(used))
        if status:break
    assert status==expected,(fail,stop,status)
    assert cancel()==0 and lib.tvox_test_live()==0
run('System initialized.',7)
print('PASS',checks,'original-frame resumable jobs, budgets1/7/64/4096; cancellation, OOM, invalid UTF8 and restart')
