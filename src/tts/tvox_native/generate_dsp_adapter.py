"""Freeze original synthesis constants/LF waveform tables, never utterances."""
import hashlib,json,math
from generate_pl import HERE,VOICE,load

def generate():
    e=load('engine'); k=e.klatt
    v=e.klatt_voice(e.get_voice('Adam')[1],'default')
    tables=[k.lf_table(i/20) for i in range(6,55)]
    g=[v['tilt'],v['flutter'],v.get('jitter',.01),v.get('shimmer',.03),
       10**(v.get('shelf',0)/20)-1,math.exp(-2*math.pi*k.SHELF_HZ/k.SR),k.SR,
       v.get('db1_open',k.DB1_OPEN),float(k.CASCADE_F6),k.NOISE_LP,k.K_VOICE,k.K_ASP,k.K_FRIC,
       k.FRAME_MS,v['rd'],round(k.SR*k.FRAME_MS/1000),max(1,round(k.SR*k.SUB_MS/1000)),
       0,2,150,5000,1,1,0,.5]
    assert not v.get('harmonic') and v.get('source','lf')=='lf'
    shelves=[k.shelf_coefs(fc,db,k.SR,low) for fc,db,low in e._src_eq('pl',v)]
    eq=e.learned()['eq'];bands=k.EQ(eq['fc'],[db*.5 for db in eq[v['sex']]],eq['q']).bands
    def array(name,values):
        return 'static const double '+name+'[]={\n'+',\n'.join(','.join(float(x).hex() for x in values[i:i+8]) for i in range(0,len(values),8))+'\n};\n'
    text='/* Original TVox model constants, not recorded speech. MIT-0. */\n'
    for name,values in [('DSP_G',g),('DSP_LF',[x for t,te in tables for x in t]),('DSP_TE',[te for t,te in tables]),('DSP_SHELVES',[x for s in shelves for x in s]),('DSP_EQ',[x for band in bands for x in band[:5]])]:text+=array(name,values)
    text+='static const double DSP_GAIN='+float(e.TitanVoice('Adam',lang='pl')._klatt_gain('pl')).hex()+';\n'
    text+='static const double DSP_PITCH_GAIN_EXP='+float(e.PITCH_GAIN_EXP).hex()+';\n'
    text+='enum { DSP_LEAD='+str(e.EDGE_LEAD)+', DSP_TAIL='+str(e.EDGE_TAIL)+' };\n'
    (HERE/'tvox_dsp_adapter_data.inc').write_text(text,encoding='ascii')
    paths=[VOICE/'klatt.py',VOICE/'engine.py',VOICE/'voices.py',VOICE/'data'/'learned.json',VOICE/'data'/'levels.json']
    (HERE/'DSP-ADAPTER-PROVENANCE.json').write_text(json.dumps({str(p.relative_to(VOICE)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2)+'\n')
if __name__=='__main__':generate()
