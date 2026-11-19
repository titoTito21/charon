"""Differential tests against actual bundled Python source; pass compiled DLL/SO."""
import ast
import ctypes as C
import hashlib
import json
import random
import sys
from pathlib import Path
from generate_data import HERE,SOURCE,VOICE,literals

for relative,digest in json.loads((HERE/'PROVENANCE.json').read_text()).items():
    assert hashlib.sha256((SOURCE/relative).read_bytes()).hexdigest()==digest,relative
assert (HERE/'upstream'/'tvox_dsp.c').read_bytes()==(SOURCE/'lib'/'native'/'tvox_dsp.c').read_bytes()

source=VOICE/'g2p_pl.py'
scope=literals(source,('_ONES','_TEENS','_TENS','_HUND','_BIG'))
names={'_below1000','_form','number_to_words'}
nodes=[node for node in ast.parse(source.read_text(encoding='utf-8-sig')).body
       if isinstance(node,ast.FunctionDef) and node.name in names]
assert len(nodes)==len(names)
exec(compile(ast.Module(body=nodes,type_ignores=[]),str(source),'exec'),scope)
lib=C.CDLL(str(Path(sys.argv[1]).resolve()))
fn=lib.tvox_pl_number
fn.argtypes=[C.c_uint64,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
fn.restype=C.c_int
randomizer=random.Random(345)
numbers=list(range(10000))+[randomizer.getrandbits(64) for _ in range(10000)]
numbers += [2**64-1]+[10**i+d for i in range(1,20) for d in (-1,0,1)]
for number in numbers:
    expected=scope['number_to_words'](number).encode('utf-8')
    needed=C.c_size_t()
    assert fn(number,None,0,C.byref(needed))==1
    assert needed.value==len(expected)+1
    output=C.create_string_buffer(needed.value)
    assert fn(number,output,len(output),C.byref(needed))==0
    assert output.value==expected,(number,output.value,expected)
    output.raw=b'X'*len(output)
    assert fn(number,output,len(output)-1,C.byref(needed))==1
    assert output.raw==b'X'*len(output)
assert fn(0,None,1,C.byref(C.c_size_t()))==-1
assert fn(0,None,0,None)==-1

class Voice(C.Structure):
    _fields_=[('name',C.c_char_p),('sex',C.c_char)]+[(key,C.c_double) for key in
        ('f0','range','fscale','f1scale','bwscale','rd','tilt','breath','flutter','rate')]+[
        ('calibrated_f0',C.c_double*2),('calibrated_gain',C.c_double*2)]
original=literals(VOICE/'voices.py',('VOICES',))['VOICES']
levels=json.loads((VOICE/'data'/'levels.json').read_text())
for voice,(name,expected) in zip((Voice*8).in_dll(lib,'tvox_voices'),original.items()):
    assert voice.name.decode()==name and voice.sex.decode()==expected['sex']
    for key,_ in Voice._fields_[2:-2]:assert getattr(voice,key)==expected[key]
    for i,lang in enumerate(('pl','en')):
        assert voice.calibrated_f0[i]==levels['f0'][name+'_'+lang]
        assert voice.calibrated_gain[i]==levels['gain'][name+'|'+lang+'|default']['0.50']
print('PASS',len(numbers),'exact Polish numeric vectors; sizing/failure atomicity; 8 original voices and PL/EN calibration')
