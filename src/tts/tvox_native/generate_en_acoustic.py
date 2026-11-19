"""Original Adam EN acoustic data/calibration, not PL data relabeled English."""
import json,hashlib
from generate_acoustic import engine as e
from generate_pl import HERE,VOICE,literal
def generate():
    learned=e.learned();fm=learned.get('formants',{});correction=fm.get('en_adam') or fm.get('en_m') or {};dur=learned.get('dur',{}).get('en',{})
    def value(v):
        if isinstance(v,str):return literal(v)
        if isinstance(v,bool):return 'true' if v else 'false'
        if isinstance(v,(tuple,list)):return '{'+','.join(map(value,v))+'}'
        return repr(float(v))
    lines=['/* Exact original Adam EN acoustic model; MIT-0. */','static const PhoneData EN_PHONE_DATA[]={']
    for sym,ph in sorted(e.langs.table('en').items()):
        seg=e.Seg(sym,ph,ph['kind'],ph['dur_en'])
        fields=[sym,ph['kind'],seg.start,seg.end,seg.locus or [0]*3,seg.k or [0]*3,seg.locus is not None,seg.k is not None,seg.voiced,seg.rank,seg.ext,seg.int,seg.av,seg.af,seg.ah,seg.par,ph['dur_en'],ph.get('rel',0),ph.get('rel_asp',0),ph.get('frdur',0),ph.get('fric',''),dur.get('scale',{}).get(sym,1),e.P[sym]['f'],False,[0]*3,1,False,[[1]*3,[1]*3],sym in correction,correction.get(sym,[1]*3)]
        values=list(map(value,fields));values[27]='{'+values[27]+'}';lines.append('{'+','.join(values)+'},')
    lines.append('};')
    lines.append('static const S EN_KNOWN_PHONES[]={'+','.join(map(literal,sorted(e.langs.table('en'))))+'};')
    f0=learned.get('f0',{}).get('en',{})
    values={'EN_FINAL_VOWEL':dur.get('final_vowel',1),'EN_FINAL_CONS':dur.get('final_cons',1),'EN_ACCENT_SCALE':f0.get('accent_scale',1),'EN_FINAL_SHIFT_ST':f0.get('final_shift_st',0),'EN_BASE_F0':e._klatt_base_f0(e.get_voice('Adam')[1],'en'),'EN_DUR_VOWEL':e.KLATT_DUR_VOWEL,'EN_DUR_CONS':e.KLATT_DUR_CONS,'EN_UNSTRESSED_DB':e.EN_UNSTRESSED_DB,'EN_STRESS_DB':e.STRESS_DB['en'][0],'EN_WEAK_DB':e.STRESS_DB['en'][1]}
    for key,val in values.items():lines.append('static const double '+key+'='+repr(float(val))+';')
    (HERE/'tvox_en_acoustic_data.inc').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    paths=[VOICE/'engine.py',VOICE/'phonemes.py',VOICE/'data'/'learned.json',VOICE/'data'/'levels.json',VOICE/'voices.py']
    (HERE/'EN-ACOUSTIC-PROVENANCE.json').write_text(json.dumps({str(p.relative_to(VOICE)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2)+'\n')
if __name__=='__main__':generate()
