"""Native whole PL text->phones vs actual engine.text_to_phones AST."""
import ast,ctypes as C,random,sys,types
from pathlib import Path
from generate_pl import VOICE,original as g2p,phonemes
from generate_textproc import original as textproc
source=VOICE/'engine.py'
node=next(n for n in ast.parse(source.read_text(encoding='utf-8-sig')).body if isinstance(n,ast.FunctionDef) and n.name=='text_to_phones')
scope={'textproc':textproc,'g2p_pl':g2p,'langs':types.SimpleNamespace(BUILTIN={'pl','en'},table=lambda lang:phonemes.P_PL)}
exec(compile(ast.Module(body=[node],type_ignores=[]),str(source),'exec'),scope)
fn=C.CDLL(str(Path(sys.argv[1]).resolve())).tvox_pl_text
fn.argtypes=[C.c_char_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
fn.restype=C.c_int
def check(text):
    data=text.encode();needed=C.c_size_t()
    expected=''
    for punct,items,count in scope['text_to_phones'](text,'pl'):
        expected+='@'+punct+'\t'+str(count)+'\n'
        expected+=''.join(f'{phone}\t{mark}\t{index}\t{int(last)}\n' for phone,mark,index,last in items)
    assert fn(data,len(data),None,0,C.byref(needed))==1
    out=C.create_string_buffer(needed.value);assert fn(data,len(data),out,len(out),C.byref(needed))==0
    assert out.value.decode()==expected,(text,out.value.decode(),expected)
    out.raw=b'X'*len(out);assert fn(data,len(data),out,len(out)-1,C.byref(needed))==1
    assert out.raw==b'X'*len(out)
texts=['Menedżer rozruchu Haiku Accessibility Kit.','Dysk NVMe Samsung 980 PRO, 500GB. Partycja 2.',
    'Firmware załadowany. Uruchamianie menedżera rozruchu.','Witaj! Wybierz system: Haiku, Windows, Linux.',
    'poniedziałek 16 listopada 2026, godz. 12:30','W XX wieku, np. 3,14 km, ok. 0,05 GB.',
    'proszę cię, nie wiem. Co to? Chodź pan. Mój dom.','Wi-Fi, e-mail, pl.wikipedia.org, widgetów 12.',
    '', 'Q', '²', '𐐀', 'ΣΟΣ']
# Original invalid numeric tokens are tested separately by test_textproc.
texts.remove('²')
rng=random.Random(346)
pool=list(g2p._lexicon())+['USB','UEFI','Haiku','nie','w','na','mi','cię','123','0,05','2GB','prof.','itd.']
for _ in range(1500):texts.append(' '.join(rng.choices(pool,k=rng.randrange(1,14)))+rng.choice(['.','?','!',',']))
for text in texts:check(text)
print('PASS',len(texts),'whole native PL text->phoneme/stress/word-index/phrase outputs match original engine')
