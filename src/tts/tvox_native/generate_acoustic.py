"""Freeze original Adam/PL acoustic parameters, never text-dependent frames."""
import json,hashlib
from generate_pl import HERE,VOICE,load,literal
engine=load('engine')
def generate():
    e=engine;learn=e.learned();fm=learn.get('formants',{})
    correction=fm.get('pl_adam') or fm.get('pl_m') or {}
    duration=learn.get('dur',{}).get('pl',{})
    def value(v):
        if isinstance(v,str):return literal(v)
        if isinstance(v,bool):return 'true' if v else 'false'
        if isinstance(v,(tuple,list)):return '{'+','.join(map(value,v))+'}'
        return repr(float(v))
    lines=['/* Exact original TVox acoustic data for Adam/PL; MIT-0. */']
    lines.append('static const PhoneData PHONE_DATA[]={')
    for sym,ph in sorted(e.P_PL.items()):
        seg=e.Seg(sym,ph,ph['kind'],ph['dur'])
        mix=e.PH.PL_VOWEL_MIX.get(sym,1.0) if isinstance(e.PH.PL_VOWEL_MIX,dict) else e.PH.PL_VOWEL_MIX
        fields=[sym,ph['kind'],seg.start,seg.end,seg.locus or [0]*3,seg.k or [0]*3,
            seg.locus is not None,seg.k is not None,seg.voiced,seg.rank,seg.ext,seg.int,
            seg.av,seg.af,seg.ah,seg.par,ph['dur'],ph.get('rel',0),ph.get('rel_asp',0),
            ph.get('frdur',0),ph.get('fric',''),duration.get('scale',{}).get(sym,1),
            e.P[sym]['f'],sym in e.PH.PL_VOWELS,e.PH.PL_VOWELS.get(sym,[0]*3),mix,
            sym in e.PH.PL_SOFT,e.PH.PL_SOFT.get(sym,[[1]*3,[1]*3]),sym in correction,correction.get(sym,[1]*3)]
        values=list(map(value,fields))
        values[27]='{'+values[27]+'}' # std::array of std::array aggregate
        lines.append('{'+','.join(values)+'},')
    lines.append('};')
    for name,items in [('ACOUSTIC_VOWELS',e.VOWELS),('HIGH_VOWELS',e.HIGH_VOWELS),('LOW_VOWELS',e.LOW_VOWELS),('VOICELESS',e.VOICELESS),('SOFT_CONS',e.PH.PL_SOFT_CONS),('SOFT_GLIDE_AFTER',e.PL_SOFT_GLIDE_AFTER)]:
        lines.append('static const S '+name+'[]={'+','.join(map(literal,sorted(items)))+'};')
    f0=learn.get('f0',{}).get('pl',{})
    constants={'DURATION_FINAL_VOWEL':duration.get('final_vowel',1),'DURATION_FINAL_CONS':duration.get('final_cons',1),
        'ACCENT_SCALE':f0.get('accent_scale',1),'FINAL_SHIFT_ST':f0.get('final_shift_st',0),
        'BASE_F0':e._klatt_base_f0(e.get_voice('Adam')[1],'pl'),'FRIC_FSCALE_EXP':e.PL_FRIC_FSCALE_EXP}
    constants.update({'ADAM_'+key.upper():e.get_voice('Adam')[1][key] for key in
        ('fscale','f1scale','bwscale','breath','rd','range','rate')})
    for name,v in constants.items():lines.append('static const double '+name+'='+repr(float(v))+';')
    (HERE/'tvox_acoustic_data.inc').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    paths=[VOICE/'engine.py',VOICE/'phonemes.py',VOICE/'data'/'learned.json',VOICE/'data'/'levels.json',VOICE/'voices.py']
    (HERE/'ACOUSTIC-PROVENANCE.json').write_text(json.dumps({str(p.relative_to(VOICE)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2)+'\n')
if __name__=='__main__':generate()
