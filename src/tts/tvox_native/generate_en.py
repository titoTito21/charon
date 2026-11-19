"""Freeze original EN lexicon/rules. No generated utterances or audio."""
import json,hashlib
from generate_pl import HERE,VOICE,load,literal
original=load('g2p_en')
def generate():
    e=original;lines=['/* Exact TVox English source tables; MIT-0. CMU license accompanies dictionary. */']
    def pairs(name,values):
        lines.append('static const Pair '+name+'[]={')
        for key,value in sorted(values.items()):lines.append('{'+literal(key)+','+literal(value)+'},')
        lines.append('};')
    pairs('EN_DICT',{k:' '.join(v) for k,v in e._load().items()})
    pairs('EN_ARPA',e.ARPA);pairs('EN_SAID',{k:' '.join(v) for k,v in e.SAID.items()})
    for name,items in [('EN_FUNCTION_WORDS',e.FUNCTION_WORDS),('EN_SIBILANT',e._SIBILANT),('EN_VOICELESS',e._VOICELESS)]:
        lines.append('static const S '+name+'[]={'+','.join(map(literal,sorted(items)))+'};')
    for name,values in [('EN_PREFIXES',e.PREFIXES),('EN_SUFFIXES',e.SUFFIXES)]:
        lines.append('static const Pair '+name+'[]={'+','.join('{'+literal(k)+','+literal(v)+'}' for k,v in values)+'};')
    left={'':0,' ':1,e._C:2,e._V+'.':3,' '+e._C+'?':4,e._V:5}
    right={'':0,' ':1,'t':2,e._C+'e ':3,e._V:4,'[eiy]':5}
    lines.append('static const ENRule EN_RULES[]={')
    for l,m,r,out in e.RULES:lines.append('{'+str(left[l])+','+literal(m)+','+str(right[r])+','+literal(out)+'},')
    lines.append('};')
    (HERE/'tvox_en_data.inc').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    license=VOICE/'data'/'CMUDICT_LICENSE.txt'
    if not license.exists():license=next(VOICE.parent.parent.rglob('CMUDICT_LICENSE.txt'))
    (HERE/'CMUDICT_LICENSE.txt').write_bytes(license.read_bytes())
    paths=[VOICE/'g2p_en.py',VOICE/'data'/'cmudict.dict',license]
    (HERE/'EN-PROVENANCE.json').write_text(json.dumps({str(p.relative_to(VOICE)) if p.is_relative_to(VOICE) else str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2)+'\n')
if __name__=='__main__':generate()
