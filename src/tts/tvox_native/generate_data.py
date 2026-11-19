"""Reproduce constants from bundled TVox source; never synthesize/cache speech."""
import ast
import hashlib
import json
from pathlib import Path

HERE=Path(__file__).resolve().parent
SOURCE=HERE.parents[2]/'dependencies'/'tvox'
VOICE=SOURCE/'lib'/'titan_voice'

def literals(path,names):
    result={}
    for node in ast.parse(path.read_text(encoding='utf-8-sig')).body:
        if not isinstance(node,ast.Assign):continue
        for target in node.targets:
            if isinstance(target,ast.Name) and target.id in names:
                if isinstance(node.value,ast.Dict) and target.id=='VOICES':
                    result[target.id]={ast.literal_eval(key):
                        {kw.arg:ast.literal_eval(kw.value) for kw in value.keywords}
                        for key,value in zip(node.value.keys,node.value.values)}
                else:result[target.id]=ast.literal_eval(node.value)
    assert set(result)==set(names)
    return result

def generate():
    tables=literals(VOICE/'g2p_pl.py',('_ONES','_TEENS','_TENS','_HUND','_BIG'))
    words=[word for key,values in tables.items() for word in
        ([word for row in values for word in row] if key=='_BIG' else values)]
    assert (max(len(word.encode('utf-8')) for word in words)+1)*28<2048
    voices=literals(VOICE/'voices.py',('VOICES',))['VOICES']
    levels=json.loads((VOICE/'data'/'levels.json').read_text())
    lines=['/* Generated from original TVox by generate_data.py; MIT-0. */']
    quote=lambda value:json.dumps(value,ensure_ascii=False)
    for key,values in tables.items():
        name='tvox_'+key[1:].lower()
        if key=='_BIG':
            rows=','.join('{'+','.join(map(quote,row))+'}' for row in values)
            lines.append('static const char* const '+name+'[3][3]={'+rows+'};')
        else:lines.append('static const char* const '+name+'[]={'+','.join(map(quote,values))+'};')
    (HERE/'tvox_number_data.inc').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    lines=['#include "tvox_number_data.inc"','const tvox_voice tvox_voices[8]={']
    for name,v in voices.items():
        fields=[quote(name),"'"+v['sex']+"'"]
        fields.extend(repr(v[key]) for key in ('f0','range','fscale','f1scale','bwscale','rd','tilt','breath','flutter','rate'))
        fields.append('{'+','.join(repr(levels['f0'][name+'_'+lg]) for lg in ('pl','en'))+'}')
        fields.append('{'+','.join(repr(levels['gain'][name+'|'+lg+'|default']['0.50']) for lg in ('pl','en'))+'}')
        lines.append('{'+','.join(fields)+'},')
    lines.append('};')
    (HERE/'tvox_data.inc').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    (HERE/'COPYING').write_bytes((SOURCE/'lib'/'native'/'COPYING').read_bytes())
    upstream=HERE/'upstream';upstream.mkdir(exist_ok=True)
    dsp=SOURCE/'lib'/'native'/'tvox_dsp.c'
    (upstream/'tvox_dsp.c').write_bytes(dsp.read_bytes())
    paths=[VOICE/'g2p_pl.py',VOICE/'voices.py',VOICE/'data'/'levels.json',dsp,SOURCE/'README.md']
    manifest={str(path.relative_to(SOURCE)).replace('\\','/'):hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    (HERE/'PROVENANCE.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':generate()
