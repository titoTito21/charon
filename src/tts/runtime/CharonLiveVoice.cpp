#include "CharonLiveVoice.h"
#include "../tvox_native/tvox_freestanding.h"
#include "../tvox_native/tvox_dsp_stream.h"
namespace charon {
static LiveVoice* owners[2];static LiveVoice* frontendHolder;static bool frontendConfigured;
static bool finite(double v){uint64_t bits=0;auto a=(unsigned char*)&bits;auto b=(const unsigned char*)&v;for(size_t i=0;i<8;++i)a[i]=b[i];return (bits&UINT64_C(0x7ff0000000000000))!=UINT64_C(0x7ff0000000000000);}
static bool utf8(const char* p,size_t n){
    if(!p||n>LiveVoice::maximumTextBytes)return false;
    for(size_t i=0;i<n;){uint32_t c=(unsigned char)p[i++],minimum=0;unsigned extra=0;
        if(c<128){}else if(c>=0xc2&&c<=0xdf){c&=31;extra=1;minimum=0x80;}
        else if(c>=0xe0&&c<=0xef){c&=15;extra=2;minimum=0x800;}
        else if(c>=0xf0&&c<=0xf4){c&=7;extra=3;minimum=0x10000;}else return false;
        while(extra--){if(i==n||((unsigned char)p[i]&0xc0)!=0x80)return false;c=(c<<6)|((unsigned char)p[i++]&63);}
        if(!c||c<minimum||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;
    }return true;
}
int LiveVoice::Initialize(CharonPool* pool,AudioEngine* shared,unsigned channel){
    if(pool_||pumping_||owners[0]==this||owners[1]==this)return Busy;
    if(!pool||!pool->allocate||!pool->release||channel>=AudioEngine::channels||(!shared&&channel))return Invalid;
    const size_t slot=owners[0]?1:0;if(owners[slot])return Busy;
    void* dsp=charon_pool_malloc(pool,tvox_dsp_stream_size());if(!dsp)return NoMemory;
    if(!frontendConfigured){
        int status=tvox_frontend_configure(pool,nullptr,nullptr);
        if(status){charon_pool_free(pool,dsp);return status;}
        frontendConfigured=true;
    }
    if(shared){engine_=shared;channel_=channel;}else{own_=AudioEngine{};engine_=&own_;channel_=0;}
    error_=0;pool_=pool;dsp_=dsp;owners[slot]=this;tvox_dsp_stream_cancel(dsp_);return 0;
}
void LiveVoice::ReleaseWork(){
    if(useExternal_&&extActive_)external_.cancel(external_.module);
    extActive_=false;extEnded_=false;extCount_=extAt_=0;extPhase_=0;
    if(dsp_)tvox_dsp_stream_cancel(dsp_);
    if(frontendOwned_){tvox_frontend_cancel();frontendOwned_=false;if(frontendHolder==this)frontendHolder=nullptr;}
    if(pool_&&current_)charon_pool_free(pool_,current_);
    if(pool_&&next_)charon_pool_free(pool_,next_);
    current_=next_=nullptr;dspActive_=false;front_=Front::None;
    pendingCount_=pendingOffset_=0;stage_=Stage::Idle;textBytes_=chunkStart_=chunkEnd_=0;
}
size_t LiveVoice::ChunkEnd(size_t start,bool first)const{
    // Break after punctuation followed by a space. The first chunk ends at
    // the first such break (8 bytes or more); later ones at the last break
    // within 160 bytes. Long text without breaks is cut at a space.
    // r382: later chunks grow (100, then 300 bytes): fewer boundaries, and
    // each is prepared while the previous, shorter one plays.
    // r389: the boot-message voice (channel 1) keeps chunks short, so the reader never waits long for the shared frontend.
    const size_t limit=channel_?48:(first?48:(start<120?72:300));size_t best=0;
    // r390: a language switch ends the chunk.
    size_t end=textBytes_;for(size_t i=start;i<textBytes_;++i)if(text_[i]==0x1b){end=i;break;}
    for(size_t i=start;i+1<end;++i){
        const char c=text_[i];
        if((c!='.'&&c!=','&&c!='!'&&c!='?'&&c!=';'&&c!=':')||text_[i+1]!=' '||i+1-start<8)continue;
        if(i+1-start<=limit){best=i+2;if(first)break;}
        else {if(!best)best=i+2;break;}
    }
    // r389: the first chunk without punctuation ends at a word after 20
    // bytes (was: the whole text up to 96 bytes), so sound starts sooner.
    if(!best&&first&&end-start>40)for(size_t i=start+20;i<end;++i)if(text_[i]==' '){best=i+1;break;}
    if(!best&&end-start>limit*2)for(size_t i=start+limit;i<end;++i)if(text_[i]==' '){best=i+1;break;}
    return best&&best<end?best:end;
}
int LiveVoice::BeginChunk(){
    if(frontendHolder&&frontendHolder!=this){
        // r389: the reader (channel 0) never waits for a boot message: the
        // message's chunk in preparation is dropped and prepared again later.
        // r389: no pre-emption (dropping a message chunk's work made the
        // message voice stutter); message chunks are short instead.
        front_=Front::Waiting;return 0;
    }
    // r390: language switches at the chunk's start set its language.
    frontPersona_.icon=false;
    while(chunkStart_+1<textBytes_&&text_[chunkStart_]==0x1b){
        size_t length=2;
        if(!ParseSwitch(chunkStart_,&length))frontLanguage_=text_[chunkStart_+1]=='e'?Language::English:Language::Polish;
        chunkStart_+=length;
        while(chunkStart_<textBytes_&&text_[chunkStart_]==' ')++chunkStart_;
    }
    if(chunkStart_>=textBytes_){front_=Front::None;return 0;}   // nothing after the switch
    chunkEnd_=ChunkEnd(chunkStart_,chunkStart_==0);
    const size_t bytes=chunkEnd_-chunkStart_;
    for(size_t i=0;i<bytes;++i)chunk_[i]=text_[chunkStart_+i];chunk_[bytes]=0;
    const double rate=rate_*frontPersona_.rate,pitch=pitch_*frontPersona_.pitch;
    int status=frontLanguage_==Language::English?tvox_frontend_begin_en(chunk_,bytes,rate,pitch):tvox_frontend_begin_pl(chunk_,bytes,rate,pitch);
    if(status)return status;
    frontendOwned_=true;frontendHolder=this;front_=Front::Running;return 0;
}
// The prepared frames are copied, so the frontend is free for the next chunk
// while the DSP renders this one (frames: count x 28 doubles).
int LiveVoice::CollectFrontend(Progress* progress){
    const double* frames=nullptr;size_t count=0,leading=0,trailing=0;
    if(tvox_frontend_frames(&frames,&count,&leading,&trailing)!=1)return SynthesisError;
    double* copy=nullptr;
    if(count) {
        copy=static_cast<double*>(charon_pool_malloc(pool_,count*frameDoubles*sizeof(double)));
        if(!copy)return NoMemory;
        for(size_t i=0;i<count*frameDoubles;++i)copy[i]=frames[i];
    }
    tvox_frontend_cancel();frontendOwned_=false;if(frontendHolder==this)frontendHolder=nullptr;
    if(next_)charon_pool_free(pool_,next_);
    // More text follows: keep only a natural pause of the chunk's end (the
    // full utterance-final silence would be heard as a cut).
    if(chunkEnd_<textBytes_&&trailing<=count){
        const char end=chunkEnd_>=2?text_[chunkEnd_-2]:' ';
        const size_t keep=(end=='.'||end=='!'||end=='?')?50:(end==','||end==';'||end==':')?20:4;
        if(trailing>keep){count-=trailing-keep;trailing=keep;}
    }
    next_=copy;nextCount_=count;nextLeading_=leading;nextTrailing_=trailing;nextLanguage_=frontLanguage_;nextPersona_=frontPersona_;frontPersona_.icon=false;front_=Front::Ready;
    progress->frontendReady=true;progress->readyFrames=count;
    return 0;
}
int LiveVoice::StartDsp(){
    ApplyAcss(next_,nextCount_,nextPersona_);   // r397
    const double pitch=pitch_*nextPersona_.pitch;double volume=volume_*nextPersona_.volume;if(volume>4)volume=4;
    if(nextLanguage_==Language::English?tvox_dsp_stream_begin_en(dsp_,next_,nextCount_,nextLeading_,nextTrailing_,pitch,volume)
        :tvox_dsp_stream_begin(dsp_,next_,nextCount_,nextLeading_,nextTrailing_,pitch,volume))return SynthesisError;
    dspPersona_=nextPersona_;
    if(dspPersona_.icon)QueueIcon(dspPersona_);
    if(current_)charon_pool_free(pool_,current_);
    current_=next_;next_=nullptr;front_=Front::None;dspActive_=true;stage_=Stage::Synthesizing;
    if(chunkEnd_<textBytes_) {chunkStart_=chunkEnd_;return BeginChunk();}
    return 0;
}
void LiveVoice::Fail(int error){engine_->CancelSpeech(channel_);ReleaseWork();error_=error;}
int LiveVoice::CancelSpeech(){if(pumping_)return Busy;engine_->CancelSpeech(channel_);ReleaseWork();queuedBytes_=0;error_=0;return 0;}
int LiveVoice::StopAll(){if(pumping_)return Busy;if(engine_==&own_)engine_->StopAll();else engine_->CancelSpeech(channel_);ReleaseWork();queuedBytes_=0;error_=0;return 0;}
int LiveVoice::Shutdown(){if(pumping_)return Busy;StopAll();if(pool_&&dsp_)charon_pool_free(pool_,dsp_);pool_=nullptr;dsp_=nullptr;for(auto& o:owners)if(o==this)o=nullptr;return 0;}
int LiveVoice::QueueText(Language language,const char* text,size_t bytes,double rate,double pitch,double volume){
    if(pumping_)return Busy;if(!pool_)return NotInitialized;
    if(language!=Language::Polish&&language!=Language::English)return UnsupportedLanguage;
    if(!utf8(text,bytes)||!finite(rate)||!finite(pitch)||!finite(volume)||rate<.1||rate>10||pitch<=0||pitch>10||volume<0||volume>4)return Invalid;
    generation_=engine_->BeginSpeech(channel_);ReleaseWork();error_=0;queuedBytes_=0;
    if(generation_==UINT64_MAX){Fail(GenerationExhausted);return error_;}
    for(size_t i=0;i<bytes;++i)text_[i]=text[i];text_[bytes]=0;pitch_=pitch;volume_=volume;language_=language;rate_=rate;
    frontLanguage_=nextLanguage_=language;endLanguage_=language;
    for(size_t i=0;i+1<bytes;++i)if(text[i]==0x1b&&(text[i+1]=='e'||text[i+1]=='p'))endLanguage_=text[i+1]=='e'?Language::English:Language::Polish;
    textBytes_=bytes;chunkStart_=0;frontPersona_=Persona{};
    if(useExternal_){const int s=BeginExternalChunk();if(s){Fail(s);return s;}stage_=Stage::Synthesizing;return 0;}
    int status=BeginChunk();
    if(status){Fail(status);return status;}
    stage_=Stage::Preparing;return 0;
}
int LiveVoice::AppendText(Language language,const char* text,size_t bytes,double rate,double pitch,double volume){
    if(pumping_)return Busy;if(!pool_)return NotInitialized;
    if(!utf8(text,bytes))return Invalid;
    if(SpeechIdle())return QueueText(language,text,bytes,rate,pitch,volume);
    // Into the running utterance while its last chunk is not yet prepared.
    // r390: another language is joined with a switch (ESC p/e).
    const bool other=language!=endLanguage_;
    if((stage_==Stage::Preparing||stage_==Stage::Synthesizing)&&!queuedBytes_
        &&textBytes_+4+bytes<=maximumTextBytes){
        text_[textBytes_++]=' ';
        if(other){text_[textBytes_++]=0x1b;text_[textBytes_++]=language==Language::English?'e':'p';text_[textBytes_++]=' ';}
        for(size_t i=0;i<bytes;++i)text_[textBytes_+i]=text[i];textBytes_+=bytes;text_[textBytes_]=0;
        endLanguage_=language;for(size_t i=0;i+1<bytes;++i)if(text[i]==0x1b&&(text[i+1]=='e'||text[i+1]=='p'))endLanguage_=text[i+1]=='e'?Language::English:Language::Polish;
        // The last chunk already went to the DSP: prepare the new text now.
        if(!useExternal_&&front_==Front::None&&chunkEnd_<textBytes_){chunkStart_=chunkEnd_;const int s=BeginChunk();if(s){Fail(s);return s;}}
        return 0;
    }
    // Otherwise after it (the voice finishes its PCM first).
    if(queuedBytes_+(queuedBytes_?1:0)+bytes>maximumTextBytes)return Busy;
    if(queuedBytes_+4+bytes>maximumTextBytes)return Busy;
    if(queuedBytes_)queued_[queuedBytes_++]=' ';
    if(queuedBytes_&&language!=queuedLanguage_){queued_[queuedBytes_++]=0x1b;queued_[queuedBytes_++]=language==Language::English?'e':'p';queued_[queuedBytes_++]=' ';}
    for(size_t i=0;i<bytes;++i)queued_[queuedBytes_+i]=text[i];queuedBytes_+=bytes;queued_[queuedBytes_]=0;
    if(queuedBytes_==bytes)queuedLanguage_=language;queuedRate_=rate;queuedPitch_=pitch;queuedVolume_=volume;return 0;
}
void LiveVoice::PushPending(){
    if(pendingOffset_<pendingCount_){size_t accepted=engine_->PushSpeech(generation_,pending_+pendingOffset_,pendingCount_-pendingOffset_,channel_,SpeechPan());pendingOffset_+=accepted;}
    if(pendingOffset_==pendingCount_)pendingOffset_=pendingCount_=0;
    if(stage_==Stage::Finishing&&!pendingCount_){
        if(!engine_->EndSpeech(generation_,channel_)){Fail(SynthesisError);return;}stage_=Stage::Idle;
    }
}
int LiveVoice::Pump(const Budget& budget,const AudioEngine::Sink& sink,Progress* progress){
    if(useExternal_&&pool_&&!pumping_&&progress)return PumpExternal(budget,sink,progress);
    if(pumping_)return Busy;if(!pool_)return NotInitialized;
    if(!progress||budget.frontend>1000000||budget.dspSamples>1000000||budget.sinkFrames>1000000)return Invalid;
    if(budget.sinkFrames&&!sink.Write)return SinkError;
    *progress=Progress{};pumping_=true;
    // r382: chunked pipeline. Audio first (keep the speech buffer full), then
    // the frontend prepares the next chunk within its time slice.
    auto render=[&]() {
        PushPending();
        while(!error_&&(stage_==Stage::Synthesizing)&&!pendingCount_&&progress->dspAllowance<budget.dspSamples){
            if(!dspActive_) {
                if(front_==Front::Ready) {const int started=StartDsp();if(started){Fail(started);break;}continue;}
                if(front_==Front::None)stage_=Stage::Finishing;
                break;  // the next chunk is still being prepared
            }
            size_t capacity=AudioEngine::speechCapacity-engine_->BufferedSpeech(channel_);if(!capacity)break;
            size_t amount=budget.dspSamples-progress->dspAllowance;if(amount>pcmBlock)amount=pcmBlock;if(amount>capacity)amount=capacity;
            size_t made=0;int status=tvox_dsp_stream_render(dsp_,pending_,amount,&made);
            progress->dspAllowance+=amount;
            if(status<0||made>amount){Fail(SynthesisError);break;}
            if(made)Timbre(pending_,made);
            pendingCount_=made;pendingOffset_=0;progress->generated+=made;
            if(status==1){tvox_dsp_stream_cancel(dsp_);dspActive_=false;}
            PushPending();
        }
        if(!error_&&stage_==Stage::Synthesizing&&!dspActive_&&front_==Front::None&&!pendingCount_)stage_=Stage::Finishing;
        PushPending();
    };
    // The frontend runs first in its own time slice (the caller's deadline
    // starts with this call); the DSP then refills the speech buffer, which
    // holds far more than one slice of audio.
    // r389: text queued behind a finished utterance starts once its PCM is out;
    // a chunk that waited for the other voice starts when the frontend is free.
    if(!error_&&queuedBytes_&&stage_==Stage::Idle&&!pendingCount_&&engine_->SpeechIdle(channel_)){
        char copy[maximumTextBytes+1];const size_t n=queuedBytes_;for(size_t i=0;i<n;++i)copy[i]=queued_[i];
        pumping_=false;const int s=QueueText(queuedLanguage_,copy,n,queuedRate_,queuedPitch_,queuedVolume_);if(s)queuedBytes_=0;
        pumping_=true;
    }
    if(!error_&&front_==Front::Waiting&&!frontendHolder){const int s=BeginChunk();if(s)Fail(s);}
    if(!error_&&front_==Front::Running&&budget.frontend){
        int status=tvox_frontend_pump_until(budget.frontend,&progress->frontend,budget.yieldFrontend,budget.yieldContext);
        if(status<0)Fail(status);
        else if(status==1){
            const int collected=CollectFrontend(progress);
            if(collected)Fail(collected);
            else if(stage_==Stage::Preparing){stage_=Stage::Synthesizing;}
        }
    }
    if(!error_)render();
    progress->sinkFrames=budget.sinkFrames?engine_->Pump(sink,budget.sinkFrames):0;
    if(engine_==&own_&&engine_->SinkFault())Fail(SinkError);
    pumping_=false;return error_;
}

bool LiveVoice::SoftwareDrained()const{return stage_==Stage::Idle&&!pendingCount_&&!queuedBytes_&&engine_->SoftwareDrained();}
bool LiveVoice::Drained(const AudioEngine::Sink& sink)const{return SoftwareDrained()&&engine_->Drained(sink);}
// ---- r393 formatting (Emacspeak-style voice-lock and auditory icons) ----
static double Steps(char c,double per){int n=int(c)-'m';if(n<-12)n=-12;if(n>12)n=12;double v=1+n*per*1.8;return v<.3?.3:v;}   // r396e: steps 1.8x wider (were barely heard)
// r397 Emacspeak ACSS steps: pitch range 12% ('a' = monotone), stress 10%, richness 6% per step.
static double AcssSteps(char c,double per,double low,double high){int n=int(c)-'m';if(n<-12)n=-12;if(n>12)n=12;double v=1+n*per;return v<low?low:v>high?high:v;}
// r397 voice families (head size and voice source), as Emacspeak's DECtalk
// voices: formant scale, bandwidth scale, Rd scale, pitch factor, breath (dB).
struct Family {double formants,bandwidths,rd,pitch,breath;};
static const Family families[8]={
    {1,1,1,1,0},          // the voice itself
    {.93,1,.9,.92,0},     // bold: a bigger head, firmer (Paul -> Harry)
    {1.08,1,1,1.12,0},    // selection: a smaller head, higher (Betty)
    {1.04,1,.95,1.05,0},  // warm colours: a little smaller, tense
    {.88,1.1,.9,.85,0},   // titles: deep (Harry)
    {1.14,.9,1,1.2,0},    // small print: a child's head (Kit)
    {.97,1.35,1.05,1,0},  // another font face: muffled
    {1,1.1,1.3,1.02,6}};  // dim, greyed out: breathy (Wendy)
bool LiveVoice::ParseSwitch(size_t at,size_t* length){
    if(at+1>=textBytes_||text_[at+1]!='f')return false;
    if(at+2+formatPayload>textBytes_){*length=textBytes_-at;return true;}
    const char* p=text_+at+2;
    Persona v;v.rate=Steps(p[0],.04);v.pitch=Steps(p[1],.03);v.volume=Steps(p[2],.08);
    v.timbre=(p[3]>='0'&&p[3]<='7')?unsigned(p[3]-'0'):0;
    v.colour=p[4];v.icons=(p[5]>='a'&&p[5]<='p')?unsigned(p[5]-'a'):0;
    v.range=AcssSteps(p[6],.12,.05,2.5);v.stress=AcssSteps(p[7],.10,.3,2.2);v.richness=AcssSteps(p[8],.06,.4,1.8);
    v.family=(p[9]>='0'&&p[9]<='7')?unsigned(p[9]-'0'):0;v.pitch*=families[v.family].pitch;
    v.event=(p[10]>='a'&&p[10]<='z')?p[10]:'-';
    if(p[11]>='a'&&p[11]<='u'){v.pan=p[11]-'a';v.panSpeech=true;}else if(p[11]>='A'&&p[11]<='U')v.pan=p[11]-'A';
    v.icon=v.colour!='-'||v.icons||v.event!='-';
    frontPersona_=v;*length=2+formatPayload;return true;
}
// r397: Emacspeak ACSS on TVox's prepared frames (28 doubles: F0, AV, AH,
// AF, A2..A6, AB, 8 x (formant, bandwidth), Rd, creak), so every dimension
// is TVox's own synthesis, not an effect: pitch range around the chunk's
// mean F0, stress as the contrast of the voicing amplitude, richness as the
// glottal Rd (tense .. breathy, with aspiration), the family's head size
// as a formant scale.
void LiveVoice::ApplyAcss(double* frames,size_t count,const Persona& p) const{
    const Family& fam=families[p.family<8?p.family:0];
    if(!frames||!count||(p.range==1&&p.stress==1&&p.richness==1&&!p.family))return;
    double f0Sum=0,avSum=0;size_t voiced=0;
    for(size_t i=0;i<count;++i){const double* fr=frames+i*frameDoubles;if(fr[0]>0&&fr[1]>0){f0Sum+=fr[0];avSum+=fr[1];++voiced;}}
    const double meanF0=voiced?f0Sum/double(voiced):0,meanAv=voiced?avSum/double(voiced):0;
    for(size_t i=0;i<count;++i){
        double* fr=frames+i*frameDoubles;
        if(voiced&&fr[0]>0&&p.range!=1){double f=meanF0+(fr[0]-meanF0)*p.range;fr[0]=f<40?40:f>600?600:f;}
        if(voiced&&fr[1]>0&&p.stress!=1){double a=meanAv+(fr[1]-meanAv)*p.stress;fr[1]=a<1?1:a>90?90:a;}
        double rd=fr[26]*fam.rd/p.richness;fr[26]=rd<.32?.32:rd>2.65?2.65:rd;
        const double breath=(p.richness<1?(1-p.richness)*40:0)+fam.breath;
        if(breath>0&&fr[1]>0){const double ah=fr[1]-30+breath;if(ah>fr[2])fr[2]=ah;}
        if(fam.formants!=1||fam.bandwidths!=1)for(size_t k=0;k<6;++k){
            double f=fr[10+2*k]*fam.formants;fr[10+2*k]=f>10400?10400:f;fr[11+2*k]*=fam.bandwidths;}
    }
}
// sin(2*pi*phase) without libm: parabola with one correction step (< 0.1%).
static double Sine(double phase){
    double x=phase-double(int64_t(phase));if(x<0)x+=1;
    const double y=x<.5?16*x*(.5-x):-16*(x-.5)*(1-x);
    return .225*(y*(y<0?-y:y)-y)+y;
}
// One oscillator sound (48 kHz stereo) played with the speech of the text it
// marks: a sine whose pitch names the colour, and short style sounds.
void LiveVoice::QueueIcon(const Persona& p){
    int16_t* out=icons_[iconSlot_];iconSlot_=(iconSlot_+1)%iconSlots;
    for(size_t i=0;i<iconFrames*2;++i)out[i]=0;
    size_t used=0;
    const size_t shift=(p.event>='a'&&p.event<='z'&&(p.colour!='-'||p.icons))?4320:0;   // r397: after the event sound
    auto tone=[&](size_t start,size_t frames,double f0,double f1,double gain,bool square){
        double phase=0;
        for(size_t i=0;i<frames&&start+i<iconFrames;++i){
            const double t=double(i)/double(frames);const double f=f0+(f1-f0)*t;
            phase+=f/48000.0;
            double env=1;const size_t fade=96;if(i<fade)env=double(i)/fade;if(frames-i<fade)env=double(frames-i)/fade;
            double s=Sine(phase);if(square)s=s>0?.6:-.6;
            int v=out[(start+i)*2]+int(s*env*gain);if(v>32767)v=32767;if(v<-32768)v=-32768;
            out[(start+i)*2]=out[(start+i)*2+1]=int16_t(v);
        }
        if(start+frames>used)used=start+frames<iconFrames?start+frames:iconFrames;
    };
    // 16 colours on a ladder from C5 (black) to E7 (bright white).
    static const double colourHz[16]={523.3,587.3,659.3,784.0,880.0,1046.5,1174.7,1318.5,
                                      698.5,1396.9,1568.0,1760.0,2093.0,2349.3,2637.0,1975.5};
    if(p.colour>='a'&&p.colour<='p')tone(shift,3360,colourHz[p.colour-'a'],colourHz[p.colour-'a'],9000,false);   // 70 ms (r396e: louder, longer)
    if(p.icons&1){tone(shift+240,480,1500,1500,6500,true);tone(shift+1200,480,1500,1500,6500,true);}   // bold/bright: two clicks
    if(p.icons&2)tone(shift,2880,700,1400,7500,false);                                        // highlight: rising chirp
    if(p.icons&4)tone(shift+1200,720,2600,2200,6500,false);                                     // font: high tick
    if(p.icons&8)tone(shift,1440,420,300,6500,false);                                          // plain again: low falling blip
    // r397 Emacspeak-style auditory icons for events (CharonFormatting.h
    // Event order); the style and colour sounds above follow 90 ms later.
    if(p.event>='a'&&p.event<='z'){
        const int e=p.event-'a';const size_t ms=48;   // frames per ms
        auto notes=[&](const double* hz,size_t n,size_t each,double gain,bool square){for(size_t k=0;k<n;++k)tone(k*each*ms,each*ms,hz[k],hz[k],gain,square);};
        switch(e){
            case 1:tone(0,60*ms,600,900,8000,false);break;                                   // open-object: rising
            case 2:tone(0,60*ms,900,600,8000,false);break;                                   // close-object: falling
            case 3:tone(0,14*ms,2000,2000,6000,false);break;                                 // select-object: a soft click
            case 4:tone(0,80*ms,300,1500,7000,false);break;                                  // large-movement: a fast sweep
            case 5:{const double hz[]={400,1600};notes(hz,2,30,7000,false);break;}            // wrap: low, high
            case 6:{const double hz[]={3000,2600,2200};notes(hz,3,12,4500,false);break;}      // scroll: three ticks
            case 7:{const double hz[]={440,415,440,415};notes(hz,4,20,6000,true);break;}      // warn-user: square warble
            case 8:tone(0,40*ms,500,1000,7000,false);tone(45*ms,20*ms,1000,1000,7000,false);break;   // ask-question
            case 9:tone(0,40*ms,1200,300,6000,true);break;                                   // delete-object: falling square
            case 10:tone(0,20*ms,1000,1000,5500,true);break;                                 // button
            case 11:{const double hz[]={1200,1200};notes(hz,2,15,5500,false);break;}         // tab: two clicks
            case 12:{const double hz[]={523.3,659.3,784};notes(hz,3,25,7000,false);break;}   // open submenu: up
            case 13:{const double hz[]={784,659.3,523.3};notes(hz,3,25,7000,false);break;}   // close submenu: down
            case 14:tone(0,30*ms,1500,2000,7000,false);break;                                // toggle
            case 15:{const double hz[]={880,1320};notes(hz,2,20,6500,false);break;}           // value changed
            case 16:tone(0,60*ms,220,200,6000,false);break;                                  // disabled: low and dull
            case 17:{const double hz[]={700,700};notes(hz,2,25,5500,true);break;}             // editor / command line
            case 18:{const double hz[]={784,987.8,1174.7};notes(hz,3,22,7000,false);break;}  // help: arpeggio
            case 19:tone(0,80*ms,300,300,5500,true);break;                                   // password
            case 20:tone(0,80*ms,523.3,523.3,6000,false);tone(0,80*ms,784,784,5000,false);break;   // new program: a fifth
            case 21:{const double hz[]={784,1046.5};notes(hz,2,35,7000,false);break;}        // task done
            case 22:tone(0,50*ms,1760,1760,7000,false);break;                                // countdown end
            default:break;
        }
    }
    // r397 stereo: the icon where its text is on the screen (balance, centre = both full).
    if(used&&p.pan>=0&&p.pan<=20){
        // the far side keeps 20% (an icon is placed, not cut from one ear)
        const int left=p.pan<=10?256:51+(20-p.pan)*205/10,right=p.pan>=10?256:51+p.pan*205/10;
        for(size_t i=0;i<used;++i){out[i*2]=int16_t(out[i*2]*left/256);out[i*2+1]=int16_t(out[i*2+1]*right/256);}
    }
    if(used)engine_->QueueCue(out,used,true);
}
// Timbre on 22.05 kHz mono speech: bright (high shelf), smoothed (one-pole
// low pass), echo (60 ms and 100 ms taps, the "reverb" of highlighted text).
void LiveVoice::Timbre(int16_t* s,size_t n){
    const unsigned t=dspPersona_.timbre;
    if(t!=appliedTimbre_){if(!(t&1))for(auto& e:echo_)e=0;appliedTimbre_=t;}
    if(!t)return;
    for(size_t i=0;i<n;++i){
        int32_t x=s[i];
        if(t&4){const int32_t d=x-previous_;previous_=int16_t(x);x+=(d*3)/5;}
        if(t&2){lowpass_+=((x*256)-lowpass_)*90/256;x=lowpass_/256;}
        if(t&1){
            const int32_t a=echo_[(echoAt_+echoLength-1323)%echoLength],b=echo_[(echoAt_+echoLength-2205)%echoLength];
            const int32_t dry=x;x=(dry*80)/100+(a*50)/100+(b*35)/100;   // r396e: stronger echo
            echo_[echoAt_]=int16_t(dry>32767?32767:dry<-32768?-32768:dry);echoAt_=(echoAt_+1)%echoLength;
        }
        if(x>32767)x=32767;if(x<-32768)x=-32768;s[i]=int16_t(x);
    }
}
// ---- r393 external synthesizer add-ons ----------------------------------
// A piece of text between Charon's switches goes to the module whole; the
// module streams it in small Render calls. Its PCM (voice rate) is resampled
// linearly to 22.05 kHz, then gets the persona's timbre like TVox speech.
int LiveVoice::BeginExternalChunk(){
    frontPersona_.icon=false;
    for(;;){
        while(chunkStart_<textBytes_&&text_[chunkStart_]==' ')++chunkStart_;
        while(chunkStart_+1<textBytes_&&text_[chunkStart_]==0x1b){
            size_t length=2;
            if(!ParseSwitch(chunkStart_,&length))frontLanguage_=text_[chunkStart_+1]=='e'?Language::English:Language::Polish;
            chunkStart_+=length;
            while(chunkStart_<textBytes_&&text_[chunkStart_]==' ')++chunkStart_;
        }
        if(chunkStart_>=textBytes_){chunkEnd_=textBytes_;extActive_=false;return 0;}
        size_t end=chunkStart_;while(end<textBytes_&&text_[end]!=0x1b)++end;
        chunkEnd_=end;
        size_t trimmed=end;while(trimmed>chunkStart_&&text_[trimmed-1]==' ')--trimmed;
        if(trimmed==chunkStart_){chunkStart_=end;continue;}   // switches only
        const bool english=frontLanguage_==Language::English;
        const uint32_t voice=english?external_.voiceEnglish:external_.voicePolish;
        uint32_t rate=english?external_.rateEnglish:external_.ratePolish;
        // r397: the persona's gain is Charon's (any synthesizer, above 100% too);
        // the module gets the user's volume only.
        double volume=volume_>1?1:volume_;
        extGain_=frontPersona_.volume*(volume_>1?volume_:1);if(extGain_>4)extGain_=4;
        // r397: ACSS dimensions an ABI 3 module takes itself; the rest Charon
        // approximates after it (richness: brighter / smoother timbre).
        extHonoured_=0;
        if(external_.persona){const int h=external_.persona(external_.module,frontPersona_.range,frontPersona_.stress,frontPersona_.richness,(frontPersona_.timbre&2)?.6:1,frontPersona_.family);extHonoured_=h>0?unsigned(h):0;}
        // a module that took the family brings its own pitch: Charon's family factor is left out
        double pitchArg=pitch_*frontPersona_.pitch;if((extHonoured_&personaFamily)&&frontPersona_.family<8)pitchArg/=families[frontPersona_.family].pitch;
        int status=external_.begin(external_.module,voice,text_+chunkStart_,trimmed-chunkStart_,rate_*frontPersona_.rate,pitchArg,volume);
        if(status==-3&&voice!=external_.voiceFallback){
            rate=external_.rateFallback;
            status=external_.begin(external_.module,external_.voiceFallback,text_+chunkStart_,trimmed-chunkStart_,rate_*frontPersona_.rate,pitchArg,volume);
        }
        if(status)return SynthesisError;
        dspPersona_=frontPersona_;
        if(!(extHonoured_&personaRichness)){if(dspPersona_.richness>1.15)dspPersona_.timbre|=4;else if(dspPersona_.richness<.85)dspPersona_.timbre|=2;}
        if(extHonoured_&personaSmoothness)dspPersona_.timbre&=~2u;   // the module smoothed it itself
        if(dspPersona_.icon)QueueIcon(dspPersona_);
        extActive_=true;extEnded_=false;extCount_=extAt_=0;extPhase_=0;extStep_=double(rate?rate:22050)/22050.0;
        return 0;
    }
}
size_t LiveVoice::RenderExternal(int16_t* out,size_t maximum){
    size_t made=0;
    while(made<maximum){
        if(extAt_+1>=extCount_){   // two source samples around the phase are needed
            if(extEnded_){extActive_=false;break;}
            size_t start=0;if(extCount_){extBuffer_[0]=extBuffer_[extCount_-1];start=1;}
            const int got=external_.render(external_.module,extBuffer_+start,sizeof(extBuffer_)/sizeof(extBuffer_[0])-start);
            extAt_=0;
            if(got<-1){Fail(SynthesisError);break;}
            if(got==-1){extEnded_=true;extActive_=false;extCount_=start;break;}
            extCount_=start+size_t(got);
            if(!got)break;   // the module is preparing
            continue;
        }
        const int32_t a=extBuffer_[extAt_],b=extBuffer_[extAt_+1];
        out[made++]=int16_t(a+int32_t((b-a)*extPhase_));
        extPhase_+=extStep_;
        while(extPhase_>=1&&extAt_+1<extCount_){extPhase_-=1;++extAt_;}
    }
    return made;
}
int LiveVoice::PumpExternal(const Budget& budget,const AudioEngine::Sink& sink,Progress* progress){
    if(budget.sinkFrames&&!sink.Write)return SinkError;
    *progress=Progress{};
    if(!error_&&queuedBytes_&&stage_==Stage::Idle&&!pendingCount_&&engine_->SpeechIdle(channel_)){
        char copy[maximumTextBytes+1];const size_t n=queuedBytes_;for(size_t i=0;i<n;++i)copy[i]=queued_[i];
        const int s=QueueText(queuedLanguage_,copy,n,queuedRate_,queuedPitch_,queuedVolume_);if(s)queuedBytes_=0;
    }
    pumping_=true;
    if(!error_&&(stage_==Stage::Synthesizing||stage_==Stage::Finishing)){
        PushPending();
        while(!error_&&stage_==Stage::Synthesizing&&!pendingCount_&&progress->dspAllowance<budget.dspSamples){
            if(!extActive_){
                if(chunkEnd_<textBytes_){chunkStart_=chunkEnd_;const int s=BeginExternalChunk();if(s){Fail(s);break;}continue;}
                stage_=Stage::Finishing;break;
            }
            size_t capacity=AudioEngine::speechCapacity-engine_->BufferedSpeech(channel_);if(!capacity)break;
            size_t amount=budget.dspSamples-progress->dspAllowance;if(amount>pcmBlock)amount=pcmBlock;if(amount>capacity)amount=capacity;
            const size_t made=RenderExternal(pending_,amount);
            progress->dspAllowance+=amount;
            if(made){Timbre(pending_,made);
                if(extGain_!=1)for(size_t i=0;i<made;++i){const double x=pending_[i]*extGain_;pending_[i]=int16_t(x>32767?32767:x<-32768?-32768:x);}
                pendingCount_=made;pendingOffset_=0;progress->generated+=made;PushPending();}
            else if(extActive_)break;   // preparing: try again next pump
        }
        PushPending();
    }
    progress->sinkFrames=budget.sinkFrames?engine_->Pump(sink,budget.sinkFrames):0;
    if(engine_==&own_&&engine_->SinkFault())Fail(SinkError);
    pumping_=false;return error_;
}
}
