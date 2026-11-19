"""Instrumented arena, cancellation, no leaked allocations and actual parity.

Pass runtime DLL/SO and optional test script name (default test_acoustic.py).
The firmware runtime is identical except GCC built-in stack jump vs host CRT.
"""
import ctypes as C,sys,runpy
from pathlib import Path
lib=C.CDLL(str(Path(sys.argv[1]).resolve()))
Alloc=C.CFUNCTYPE(C.c_void_p,C.c_void_p,C.c_size_t)
Free=C.CFUNCTYPE(None,C.c_void_p,C.c_void_p)
Cancel=C.CFUNCTYPE(C.c_int,C.c_void_p)
class Pool(C.Structure):
    _fields_=[('context',C.c_void_p),('allocate',Alloc),('release',Free),('limit',C.c_size_t),('used',C.c_size_t),('allocations',C.c_size_t)]
memory={};peak=0;used=0;cancelAfter=None;calls=0;failAfter=None
@Alloc
def allocate(context,n):
    global peak,used
    if failAfter is not None and len(memory)>=failAfter:return None
    data=C.create_string_buffer(n);address=C.addressof(data);memory[address]=data
    used+=n;peak=max(peak,used)
    return address
@Free
def release(context,address):
    global used
    used-=C.sizeof(memory[address]);del memory[address]
@Cancel
def cancel(context):
    global calls
    calls+=1
    return cancelAfter is not None and calls>=cancelAfter
pool=Pool()
lib.charon_pool_init.argtypes=[C.POINTER(Pool),C.c_void_p,Alloc,Free,C.c_size_t]
lib.tvox_frontend_configure.argtypes=[C.POINTER(Pool),Cancel,C.c_void_p]
assert lib.charon_pool_init(C.byref(pool),None,allocate,release,256*1024*1024)==1
assert lib.tvox_frontend_configure(C.byref(pool),cancel,None)==0
fn=lib.tvox_pl_frames_ex
fn.argtypes=[C.c_char_p,C.c_size_t,C.c_double,C.c_double,C.POINTER(C.c_double),C.c_size_t]+[C.POINTER(C.c_size_t)]*3
text='Menedżer rozruchu Haiku.'.encode()
for limit in [0,1,2]:
    failAfter=limit;n=C.c_size_t(777);lead=C.c_size_t(888);tail=C.c_size_t(999);out=(C.c_double*28)(*([42]*28))
    assert fn(text,len(text),1,1,out,1,C.byref(n),C.byref(lead),C.byref(tail))==-2
    assert not memory and pool.used==pool.allocations==0
    assert list(out)==[42]*28 and (n.value,lead.value,tail.value)==(777,888,999)
failAfter=None
for limit in [1,4,17,100,1000]:
    cancelAfter=limit;calls=0
    assert fn(text,len(text),1,1,out,1,C.byref(n),C.byref(lead),C.byref(tail))==-3
    assert not memory and pool.used==pool.allocations==0
    assert list(out)==[42]*28 and (n.value,lead.value,tail.value)==(777,888,999)
cancelAfter=None
calls=0
assert fn(text,len(text),1,1,None,0,C.byref(n),C.byref(lead),C.byref(tail))==1
frameCheckpoints=calls
for point in [frameCheckpoints//2,frameCheckpoints-1,frameCheckpoints]:
    cancelAfter=point;calls=0;n.value=777;lead.value=888;tail.value=999
    assert fn(text,len(text),1,1,out,1,C.byref(n),C.byref(lead),C.byref(tail))==-3
    assert not memory and pool.used==pool.allocations==0
    assert list(out)==[42]*28 and (n.value,lead.value,tail.value)==(777,888,999)
cancelAfter=None
for name,extra in [('tvox_pl_analyze',[0]),('tvox_normalise',[0]),('tvox_pl_text',[])]:
    textfn=getattr(lib,name)
    textfn.argtypes=[C.c_char_p,C.c_size_t]+[C.c_int]*len(extra)+[C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
    n=C.c_size_t()
    assert textfn(text,len(text),*extra,None,0,C.byref(n))==1
    output=C.create_string_buffer(n.value)
    calls=0
    assert textfn(text,len(text),*extra,output,len(output),C.byref(n))==0
    cancelAfter=calls;calls=0;n.value=777;output.raw=b'X'*len(output)
    assert textfn(text,len(text),*extra,output,len(output),C.byref(n))==-3
    assert n.value==777 and output.raw==b'X'*len(output)
    assert not memory and pool.used==pool.allocations==0
    cancelAfter=None
script=Path(__file__).with_name(sys.argv[2] if len(sys.argv)>2 else 'test_acoustic.py')
sys.argv=[str(script),str(Path(sys.argv[1]).resolve()),*sys.argv[3:]]
runpy.run_path(str(script),run_name='__main__')
assert not memory and pool.used==pool.allocations==0
print('PASS arena allocation failures, cancellation and recovery; peak bytes',peak)
