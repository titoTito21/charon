"""Actual compiled EN G2P against original dictionary + all live fallback rules."""
import ctypes as C,sys,random,json,hashlib
from pathlib import Path
from generate_en import original as e
from generate_pl import HERE,VOICE
for path,digest in json.loads((HERE/'EN-PROVENANCE.json').read_text()).items():
    assert hashlib.sha256((VOICE/path).read_bytes()).hexdigest()==digest,path
fn=C.CDLL(str(Path(sys.argv[1]).resolve())).tvox_en_analyze
fn.argtypes=[C.c_char_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
def check(word):
    ph,st=e.word_to_phones(word);expected=' '.join(ph)+'\t'+''.join(map(str,st))+'\n';data=word.encode();n=C.c_size_t()
    assert fn(data,len(data),None,0,C.byref(n))==1
    out=C.create_string_buffer(n.value);assert fn(data,len(data),out,len(out),C.byref(n))==0
    assert out.value.decode()==expected,(word,out.value.decode(),expected)
words=list(e._load())
rng=random.Random(347)
words+=['Haiku','firmware','NVMe','system','bootloader','accessibility','unmuted','subagents','checkboxes','whitelisted','minimised','organisation','initialised','reinitialization','runnable','gammepad',"Adam's",'ΣΟΣ','İtd','𐐀','',"''"]
for _ in range(3000):words.append(''.join(rng.choices('abcdefghijklmnopqrstuvwxyz',k=rng.randrange(1,22))))
for _ in range(2000):words.append(rng.choice(['un','re','under','inter','multi',''])+rng.choice(list(e._load()))+rng.choice(['s','ed','ing','ers','ness','able','ization']))
for word in words:check(word)
print('PASS',len(words),'original English CMU dictionary/composition/fallback/Unicode/function-word stress cases')
