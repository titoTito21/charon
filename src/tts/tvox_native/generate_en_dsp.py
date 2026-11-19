"""Exact EN source shelves/gain; shared LF/Adam/EQ data stay source-identical."""
import hashlib,json
from generate_pl import HERE,VOICE,load
def generate():
    e=load('engine');k=e.klatt;v=e.klatt_voice(e.get_voice('Adam')[1],'default')
    shelves=[x for fc,db,low in e._src_eq('en',v) for x in k.shelf_coefs(fc,db,k.SR,low)]
    text='/* Original Adam EN calibration/source EQ; not PL speech relabeled. MIT-0. */\n'
    text+='static const double EN_DSP_SHELVES[]={'+','.join(float(x).hex() for x in shelves)+'};\n'
    text+='static const double EN_DSP_GAIN='+float(e.TitanVoice('Adam',lang='en')._klatt_gain('en')).hex()+';\n'
    (HERE/'tvox_en_dsp_data.inc').write_text(text,encoding='ascii')
    paths=[VOICE/'engine.py',VOICE/'klatt.py',VOICE/'voices.py',VOICE/'data'/'learned.json',VOICE/'data'/'levels.json']
    (HERE/'EN-DSP-PROVENANCE.json').write_text(json.dumps({str(p.relative_to(VOICE)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2)+'\n')
if __name__=='__main__':generate()
