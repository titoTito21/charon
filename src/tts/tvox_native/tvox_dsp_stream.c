/* MIT-0. See DSP-STREAM-PROVENANCE.json. */
#include "tvox_dsp_stream.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include "tvox_dsp_adapter_data.inc"
#include "tvox_en_dsp_data.inc"
#include "tvox_dsp_stream_core.inc"
extern void pcm16(int,const double*,double,double,short*);
enum { EQ_BANDS=sizeof(DSP_EQ)/sizeof(double)/5 };
typedef struct {
    RenderState core;
    const double* frames;
    size_t end,skip,tail,pending,offset;
    int active,english;
    double gain,eq[EQ_BANDS][4];
    short pcm[110];
} Stream;
static int aligned(const void* p) {return p && (uintptr_t)p%sizeof(double)==0;}
size_t tvox_dsp_stream_size(void) {return sizeof(Stream);}
void tvox_dsp_stream_cancel(void* storage) {
    if(aligned(storage)) memset(storage,0,sizeof(Stream));
}
static int begin_language(void* storage,const double* frames,size_t count,
    size_t leading,size_t trailing,double pitch,double volume,int english)
{
    Stream* s=(Stream*)storage;
    double gain;
    if(!aligned(storage)) return -1;
    /* A failed replacement cannot leave old speech alive. */
    tvox_dsp_stream_cancel(s);
    if((!frames && count) || count>(size_t)INT_MAX/110 || leading>count
        || trailing>count-leading || !isfinite(pitch) || pitch<=0
        || !isfinite(volume) || volume<0) return -1;
    gain=(english?EN_DSP_GAIN:DSP_GAIN)*pow(pitch,DSP_PITCH_GAIN_EXP)*volume;
    if(!isfinite(gain)) return -1;
    s->frames=frames;s->gain=gain;s->english=english;
    s->skip=leading>DSP_LEAD?leading-DSP_LEAD:0;
    s->tail=(trailing>DSP_TAIL?trailing-DSP_TAIL:0)*110;
    s->end=count-s->tail/110;s->active=count!=0;
    return 0;
}
int tvox_dsp_stream_render(void* storage,short* output,size_t maximum,size_t* written)
{
    Stream* s=(Stream*)storage;
    size_t made=0,work=0,budget=maximum/110+(maximum%110!=0),i,j;
    if(!aligned(storage) || !written || (!output && maximum)) return -1;
    *written=0;
    if(!s->active) return 1;
    while(made<maximum) {
        while(s->offset<s->pending && made<maximum) output[made++]=s->pcm[s->offset++];
        if(made==maximum) break;
        s->pending=s->offset=0;
        if((size_t)s->core.frame>=s->end) {
            while(s->tail && made<maximum) {output[made++]=0;--s->tail;}
            if(!s->tail) {s->active=0;s->frames=0;}
            break;
        }
        if(work==budget) break;
        {
            double samples[110];int n;
            size_t f=(size_t)s->core.frame;
            const double* fr=s->frames+f*28;
            /* Validate the current and lookahead row before native indexing. */
            for(i=0;i<(f+1<s->end?56u:28u);i++)
                if(!isfinite(fr[i]) || fabs(fr[i])>1000000) goto invalid;
            for(i=0;i<(f+1<s->end?2u:1u);i++)
                if(fr[i*28]<0 || fr[i*28+26]<.3 || fr[i*28+26]>2.7) goto invalid;
            /* Exact original 'first audible before fa' skip decision, evaluated
             * incrementally instead of scanning the entire leading pause. */
            if(f<s->skip && (fr[1]>0 || fr[2]>0 || fr[3]>0)) s->skip=f;
            n=render_frame(&s->core,(int)s->end,s->frames,DSP_G,DSP_LF,DSP_TE,49,
                s->english?EN_DSP_SHELVES:DSP_SHELVES,s->english?sizeof(EN_DSP_SHELVES)/sizeof(double)/5:sizeof(DSP_SHELVES)/sizeof(double)/5,(int)s->skip,1234,samples);
            ++work;
            if(n<0 || n>110) goto invalid;
            for(j=0;j<EQ_BANDS;j++) {
                double* st=s->eq[j];const double* c=DSP_EQ+j*5;
                for(i=0;i<(size_t)n;i++) {
                    double x=samples[i],y=c[0]*x+c[1]*st[0]+c[2]*st[1]-c[3]*st[2]-c[4]*st[3];
                    st[1]=st[0];st[0]=x;st[3]=st[2];st[2]=y;samples[i]=y;
                }
            }
            pcm16(n,samples,s->gain,.9,s->pcm);s->pending=(size_t)n;
        }
    }
    if(s->offset==s->pending && (size_t)s->core.frame>=s->end && !s->tail) {
        s->active=0;s->frames=0;
    }
    *written=made;return s->active?0:1;
invalid:
    tvox_dsp_stream_cancel(s);*written=made;return -1;
}

int tvox_dsp_stream_begin(void* storage,const double* frames,size_t count,size_t leading,size_t trailing,double pitch,double volume){return begin_language(storage,frames,count,leading,trailing,pitch,volume,0);}
int tvox_dsp_stream_begin_en(void* storage,const double* frames,size_t count,size_t leading,size_t trailing,double pitch,double volume){return begin_language(storage,frames,count,leading,trailing,pitch,volume,1);}
