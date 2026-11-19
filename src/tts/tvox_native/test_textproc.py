"""Compiled built-in PL/EN normalise against actual original Python textproc."""
import ctypes as C,random,sys
from pathlib import Path
from generate_textproc import original as py
fn=C.CDLL(str(Path(sys.argv[1]).resolve())).tvox_normalise
fn.argtypes=[C.c_char_p,C.c_size_t,C.c_int,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
fn.restype=C.c_int
checks=0
def check(text,lang):
    global checks
    data=text.encode();need=C.c_size_t()
    try:expected=''.join(p+'\t'+' '.join(w)+'\n' for w,p in py.normalise(text,lang))
    except (ValueError,IndexError,KeyError):
        assert fn(data,len(data),lang=='en',None,0,C.byref(need))==-1,(text,lang,'original error');checks+=1;return
    status=fn(data,len(data),lang=='en',None,0,C.byref(need));assert status==1,(text,lang,status)
    buffer=C.create_string_buffer(need.value);assert fn(data,len(data),lang=='en',buffer,len(buffer),C.byref(need))==0
    actual=buffer.value.decode();assert actual==expected,(text,lang,actual,expected)
    buffer.raw=b'X'*len(buffer);assert fn(data,len(data),lang=='en',buffer,len(buffer)-1,C.byref(need))==1
    assert buffer.raw==b'X'*len(buffer);checks+=1
texts=['','a','w','i','I','K','Q','Haiku Accessibility Kit','UEFI BIOS USB CD NVMe QEMU',
    'NVMe Samsung 980 PRO 500GB partycja 2','Haiku 64-bit, Windows 11, Linux 6.12',
    '2026-11-16 12:30:01','poniedziałek 16 listopada 2026','3,14 0,05 3.001 0.1234',
    'pl.wikipedia.org https://example.org/user?id=3','białо-czerwony e-book',
    'A & B','x\t&\ny','ΣΟΣ ΟΣ ΑΣΑ','İ ı ſ K ẞ 𐐀','１２３ ١٢٣ ² Ⅳ ½','телефо́н',
    'C:\\Haiku\\system','a!?... b;: c — d – e - f','ok. 12 OK ok. Gotowe',
    'w XX wieku XIX wieku XX w. w XX w. XX wiekiem','12345678901234567890',
    '0001 0000 00002 0000000000001','pięć\x01sześć','e-mail Wi-Fi facebook widgetów launcherem']
texts+=['İtd.','2mİn','2Kg','İnż.','ſtr.','1ſ','ΑΟΣ\u0301','ΣΟΣ\u0301Σ']
for key in py.PL_ABBREV:
    texts.extend([key,key+'.',key.upper()+'. Następny',key+'. 2',key+'.x','x'+key+'.',key+'\t'])
for unit in py.PL_UNITS:
    for num in ('0','1','2','5','12','22','101','1001','0,5','1.20','0001'):
        texts.extend([num+unit,num+' '+unit.upper()+'.',num+'  '+unit])
for stem in ('I','IV','XIX','XX','XXI','XCIX','C','IIIIIIII'):
    for noun in ('wiek','wieku','wiekiem','wiekach','wieki','w.','wiekuabc'):
        texts.extend([stem+' '+noun,'w '+stem+' '+noun,'W '+stem+'\t'+noun])
for text in texts:
    for lang in ('pl','en'):check(text,lang)
rng=random.Random(345)
parts=texts+['\t','\n','\r','\u00a0','\u2003','inż.','prof.','Mr.','Dr.','e.g.','i.e.','etc.']
for _ in range(2500):
    text=' '.join(rng.choices(parts,k=rng.randrange(1,5)))
    for lang in ('pl','en'):check(text,lang)
print('PASS',checks,'native PL/EN normalization cases, exact original units/abbreviations/Unicode/numbers/phrases/error outcomes')
