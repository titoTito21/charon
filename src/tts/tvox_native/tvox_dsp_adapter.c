/* Direct translation of klatt.render, EQ.process (scalar reference),
 * engine._edges and _klatt_gain; unchanged upstream klatt_render/pcm16.
 * MIT-0; see COPYING and DSP-ADAPTER-PROVENANCE.json. */
#include "tvox_dsp_adapter.h"
#include <math.h>
#include <limits.h>
#include "tvox_dsp_adapter_data.inc"
#include "tvox_en_dsp_data.inc"
extern int klatt_render(int,const double*,const double*,const double*,
    const double*,int,const double*,int,int,unsigned long long,double*);
extern void pcm16(int,const double*,double,double,short*);

static int render_language(const double* frames,size_t count,size_t leading,
    size_t trailing,double pitch,double volume,double* scratch,
    size_t scratchCapacity,short* output,size_t outputCapacity,size_t* required,int english)
{
    size_t i,j,skip,lead,tail,end,n;
    int rendered;
    double gain;
    if (!required || (!frames && count) || count>(size_t)INT_MAX/110
        || leading>count || trailing>count-leading || !isfinite(pitch)
        || pitch<=0 || !isfinite(volume) || volume<0) return -1;
    for(i=0;i<count*28;i++) if(!isfinite(frames[i])) return -1;
    lead=leading>DSP_LEAD ? leading-DSP_LEAD : 0;
    tail=trailing>DSP_TAIL ? trailing-DSP_TAIL : 0;
    end=count-tail;
    skip=lead;
    for(i=0;i<lead;i++) {
        const double* f=frames+i*28;
        if(f[1]>0 || f[2]>0 || f[3]>0) {skip=i;break;}
    }
    n=(count-skip)*110;
    gain=(english?EN_DSP_GAIN:DSP_GAIN)*pow(pitch,DSP_PITCH_GAIN_EXP)*volume;
    if(!isfinite(gain)) return -1;
    *required=n;
    if(n && (!scratch || !output || scratchCapacity<n || outputCapacity<n)) return 1;
    if(!n) return 0;
    rendered=klatt_render((int)end,frames,DSP_G,DSP_LF,DSP_TE,49,
        english?EN_DSP_SHELVES:DSP_SHELVES,(int)(english?sizeof(EN_DSP_SHELVES)/sizeof(double)/5:sizeof(DSP_SHELVES)/sizeof(double)/5),(int)skip,1234,scratch);
    if(rendered!=(int)((end-skip)*110)) return -2;
    /* Original scalar EQ: band-major, same arithmetic/state update ordering. */
    for(j=0;j<sizeof(DSP_EQ)/sizeof(double);j+=5) {
        double x1=0,x2=0,y1=0,y2=0;
        for(i=0;i<(size_t)rendered;i++) {
            double s=scratch[i];
            double o=DSP_EQ[j]*s+DSP_EQ[j+1]*x1+DSP_EQ[j+2]*x2
                -DSP_EQ[j+3]*y1-DSP_EQ[j+4]*y2;
            x2=x1;x1=s;y2=y1;y1=o;scratch[i]=o;
        }
    }
    pcm16(rendered,scratch,gain,.9,output);
    /* Trimmed trailing pause is emitted as silence, after EQ, as upstream. */
    for(i=(size_t)rendered;i<n;i++) output[i]=0;
    return 0;
}

int tvox_dsp_render_pl(const double* frames,size_t count,size_t leading,size_t trailing,double pitch,double volume,double* scratch,size_t scratchCapacity,short* output,size_t outputCapacity,size_t* required){return render_language(frames,count,leading,trailing,pitch,volume,scratch,scratchCapacity,output,outputCapacity,required,0);}

int tvox_dsp_render_en(const double* frames,size_t count,size_t leading,size_t trailing,double pitch,double volume,double* scratch,size_t scratchCapacity,short* output,size_t outputCapacity,size_t* required){return render_language(frames,count,leading,trailing,pitch,volume,scratch,scratchCapacity,output,outputCapacity,required,1);}
