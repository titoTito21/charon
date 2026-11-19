"""Actual compiled PL word/phrase rules vs the complete original g2p_pl module."""
import ctypes as C
import hashlib,json,random,sys
from pathlib import Path
from generate_pl import HERE,VOICE,original as py

manifest=json.loads((HERE/'PL-PROVENANCE.json').read_text())
for path,digest in manifest['sha256'].items():
    assert hashlib.sha256((VOICE/path).read_bytes()).hexdigest()==digest,path
lib=C.CDLL(str(Path(sys.argv[1]).resolve()))
fn=lib.tvox_pl_analyze
fn.argtypes=[C.c_char_p,C.c_size_t,C.c_int,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
fn.restype=C.c_int
checks=0
def native(text,mode):
    data=text.encode();needed=C.c_size_t();assert fn(data,len(data),mode,None,0,C.byref(needed))==1
    buffer=C.create_string_buffer(needed.value)
    assert fn(data,len(data),mode,buffer,len(buffer),C.byref(needed))==0
    result=buffer.value.decode()
    buffer.raw=b'X'*len(buffer)
    assert fn(data,len(data),mode,buffer,len(buffer)-1,C.byref(needed))==1
    assert buffer.raw==b'X'*len(buffer)
    return result
def compare(words,mode):
    global checks
    text=words if mode==0 else '\n'.join(words)
    expected=[words] if mode==0 else py.merge_enclitics(py.merge_proclitics(words))
    phones=[py.word_to_phones(word) for word in expected]
    marks=[py.stress(p,w) for p,w in zip(phones,expected)]
    if mode:marks=py.phrase_stress(expected,phones,marks)
    result=''.join(w+'\t'+' '.join(p)+'\t'+''.join(map(str,m))+'\n' for w,p,m in zip(expected,phones,marks))
    actual=native(text,mode)
    assert actual==result,(text,actual,result)
    checks+=1

words=list(py._lexicon())
words+=['','ĄĘĆŁŃÓŚŹŻ','zażółć','gęślą','jaźń','bezinformacyjny','dezinformacja','marznąć',
    'mierzwa','tarzan','nauka','nieudane','Europa','auto','Paweł','mógł','padł','pomysł',
    'krwawy','trwać','kwiat','trzy','koncie','dysk','partycja','Haiku','UEFI','USB','NVMe',
    'QEMU','ThinkPad','Windows','Linux','SecureBoot','Bluetooth','İ','K','ẞ','𐐀','ΣΟΣ',
    'nie|wiem','na|chuj','proszę+cię','co+to','zrobilibyśmy','uniwersytet','papryka','w|domu']
for n in list(range(0,1000,7))+[1000,1012,2024,999999,2**64-1]:words.extend(py.number_to_words(n).split())
words+=list(py.YKA_PENULT|py.FUNCTION_WORDS|py.ANTEPENULT_WORDS)
words+=['prefix'+ending for ending in py.VERB_4TH+py.VERB_ANTEPENULT+py.YKA_ENDINGS+py.YKA_GENITIVE]
for word in list(words):compare(word,0)
for word in words[::3]:compare(word.upper(),0)
rng=random.Random(345)
alphabet='aąbcćdeęfghijklłmnńoóprsśtuwyzźżqvx|+01239!?'
for _ in range(3000):compare(''.join(rng.choices(alphabet,k=rng.randrange(1,30))),0)
phrases=['nie wiem','w domu','w polu','proszę cię','wierzą nam','na chuj mi to','co to',
    'chodź pan','chuj tam','mój dom','ten pan','sklep pitny','dzień dobry','kot pije',
    'wiem że nie','że to','a to','z nie w domu','daj mi go to by no',
    'dysk USB dwa partycja Haiku','menedżer rozruchu Haiku Accessibility Kit']
for p in phrases:compare(p.split(),1)
pool=list(py.PROCLITICS|py.ENCLITICS|py.WEAK_AFTER_MONO|py.NOT_HOSTS)+words[:100]
for _ in range(1500):compare(rng.choices(pool,k=rng.randrange(0,12)),1)
for data in (b'\xff',b'\xc0\x80',b'\xed\xa0\x80',b'\xf4\x90\x80\x80',b'\xe2\x82',b'a\0b'):
    output=C.create_string_buffer(b'untouched');before=output.raw
    assert fn(data,len(data),0,output,len(output),C.byref(C.c_size_t()))==-1
    assert output.raw==before
for data,mode in ((b'x'*1025,0),(b'a\n'*1025,1),(b'a\tb',0),(b'x',2)):
    assert fn(data,len(data),mode,None,0,C.byref(C.c_size_t()))==-1
print('PASS',checks,'compiled PL lexicon/rules/Unicode/stress/clitic/phrase differential cases; strict UTF8, bounds, failure atomicity')
