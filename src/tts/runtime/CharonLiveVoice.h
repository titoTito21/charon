#pragma once
#include "CharonAudioEngine.h"
#include "CharonPool.h"
namespace charon {
// The TVox frontend has one global preparation context: up to two voices
// (r389: reader and boot messages) take turns with it, chunk by chunk; each
// has its own DSP. A second voice mixes into the first voice's AudioEngine
// (another speech channel) and never writes to a sink itself.
// Explicit Shutdown is required before destroying this object or its pool.
// r393: an external synthesizer add-on (\EFI\HAK\synths\*.tts, CharonSynthModule.h)
// that replaces TVox for this voice. Charon splits the text at its switches
// (language, formatting) and starts each piece with the voice of its language.
struct ExternalSynth {
    void* module=nullptr;
    int (*begin)(void* module,uint32_t voice,const char* text,size_t bytes,double rate,double pitch,double volume)=nullptr;
    int (*render)(void* module,int16_t* out,size_t maximum)=nullptr;
    void (*cancel)(void* module)=nullptr;
    uint32_t voicePolish=0,voiceEnglish=0,ratePolish=22050,rateEnglish=22050;
    // r395: the chosen voice. A language voice the module no longer holds
    // (Begin -3: released for memory) reads its piece with this one instead.
    uint32_t voiceFallback=0,rateFallback=22050;
    // r397: an ABI 3 module's persona call (CharonSynthModule.h SetPersona):
    // range/stress/richness/smoothness as factors (1 = the voice's own),
    // family 0..7. Returns the dimensions it honoured (personaRange...), or
    // < 0. nullptr (ABI 2): Charon does what it can after the synthesizer.
    int (*persona)(void* module,double range,double stress,double richness,double smoothness,uint32_t family)=nullptr;
};
constexpr unsigned personaRange=1,personaStress=2,personaRichness=4,personaSmoothness=8,personaFamily=16;
class LiveVoice {
public:
    // r393: nullptr = TVox (built in). Takes effect for the next utterance.
    int SetExternal(const ExternalSynth* synth){if(pumping_)return Busy;CancelSpeech();if(synth&&synth->module&&synth->begin&&synth->render&&synth->cancel){external_=*synth;useExternal_=true;}else useExternal_=false;return 0;}
    bool External()const{return useExternal_;}
    bool Pumping()const{return pumping_;}
    enum class Language {Polish,English};
    enum Error {Invalid=-1,NoMemory=-2,Busy=-4,UnsupportedLanguage=-10,
        NotInitialized=-11,SinkError=-12,SynthesisError=-13,GenerationExhausted=-14};
    struct Budget {size_t frontend=64,dspSamples=256,sinkFrames=480;
        int (*yieldFrontend)(void*)=nullptr;void* yieldContext=nullptr;};
    struct Progress {size_t frontend=0,dspAllowance=0,generated=0,sinkFrames=0;
        bool frontendReady=false;size_t readyFrames=0;};
    static constexpr size_t maximumTextBytes=4096,pcmBlock=256;
    LiveVoice()=default;
    LiveVoice(const LiveVoice&)=delete;
    LiveVoice& operator=(const LiveVoice&)=delete;
    int Initialize(CharonPool*,AudioEngine* shared=nullptr,unsigned channel=0);
    // Copies valid UTF8 before returning; never retains the caller's text.
    // Invalid/unsupported replacements leave current speech untouched.
    int QueueText(Language,const char*,size_t,double rate=1,double pitch=1,double volume=1);
    // r389: adds text after what is being spoken (same voice settings), or
    // starts it when the voice is idle. Busy when it does not fit.
    int AppendText(Language,const char*,size_t,double rate=1,double pitch=1,double volume=1);
    // Work units are checkpoints / DSP sample allowance / accepted sink frames.
    // Each DSP call receives at most pcmBlock samples and may evaluate
    // ceil(that allowance/110) source frames despite writing zero samples
    // during leading-pause skipping. No wall-clock deadline is implied.
    int Pump(const Budget&,const AudioEngine::Sink&,Progress*);
    int CancelSpeech(); // Does not stop queued cues or background melody.
    int StopAll(); // Software stop only: hardware drain/abort remains external.
    int Shutdown();
    bool Preparing()const{return stage_==Stage::Preparing;}
    AudioEngine* Engine(){return engine_;}
    // r389: preparation is running or waiting while little PCM is left to play.
    bool Starving()const{return (front_==Front::Running||front_==Front::Waiting)&&engine_->BufferedSpeech(channel_)<1024&&stage_!=Stage::Idle;}   // r389: a second voice mixes into this one
    bool SoftwareDrained()const;
    bool Drained(const AudioEngine::Sink& sink)const;
    int LastError()const{return error_;}
    bool QueueCue(const int16_t* stereo,size_t frames){return !pumping_&&engine_->QueueCue(stereo,frames);}
    bool SetMelody(const int16_t* stereo,size_t frames){return !pumping_&&engine_->SetMelody(stereo,frames);}
    bool CancelCues(){if(pumping_)return false;engine_->CancelCues();return true;}
    bool StopMelody(){if(pumping_)return false;engine_->StopMelody();return true;}
    bool SetUiSoundsEnabled(bool enabled){if(pumping_)return false;engine_->SetUiSoundsEnabled(enabled);return true;}
    bool SetGains(const Mixer::Gains& value){return !pumping_&&engine_->SetGains(value);}
    size_t MelodyPosition()const{return engine_->MelodyPosition();}
    size_t BufferedSpeech()const{return engine_->BufferedSpeech(channel_);}
    // No utterance is preparing, synthesizing or audible in the mixer.
    bool SpeechIdle()const{return stage_==Stage::Idle&&!pendingCount_&&!queuedBytes_&&engine_->SpeechIdle(channel_);}
    bool CuesIdle()const{return engine_->CuesIdle();}
    size_t QueuedCues()const{return engine_->QueuedCues();}
private:
    enum class Stage {Idle,Preparing,Synthesizing,Finishing};
    void ReleaseWork();
    void Fail(int);
    void PushPending();
    // r382: an utterance is spoken in chunks (a short first phrase, then
    // sentences), so sound starts before the whole text is prepared; the
    // next chunk is prepared while the previous one plays.
    size_t ChunkEnd(size_t start,bool first)const;
    int BeginChunk();
    int CollectFrontend(Progress*);
    int StartDsp();
    enum class Front {None,Running,Ready,Waiting};   // Waiting: the other voice holds the frontend
    static constexpr size_t frameDoubles=28;
    Front front_=Front::None;
    bool dspActive_=false;
    double* current_=nullptr;double* next_=nullptr;
    size_t nextCount_=0,nextLeading_=0,nextTrailing_=0;
    AudioEngine own_;AudioEngine* engine_=&own_;unsigned channel_=0;
    char queued_[maximumTextBytes+1]={};size_t queuedBytes_=0;Language queuedLanguage_=Language::Polish,queuedEndLanguage_=Language::Polish;double queuedRate_=1,queuedPitch_=1,queuedVolume_=1;
    CharonPool* pool_=nullptr;
    void* dsp_=nullptr;
    Stage stage_=Stage::Idle;
    Language language_=Language::Polish;
    // r390: ESC 'p' / ESC 'e' in the text switches the language; a chunk ends
    // there. frontLanguage_: the chunk in the frontend, nextLanguage_: the
    // prepared one, endLanguage_: in effect at the end of the text (AppendText).
    Language frontLanguage_=Language::Polish,nextLanguage_=Language::Polish,endLanguage_=Language::Polish;
    // r393: ESC 'f' + payload (Charon formatting, CharonFormatting.h): a
    // voice personality (rate/pitch/volume steps, timbre) for the text after
    // it, and a sound icon played with that text's first chunk.
    // r397: + Emacspeak ACSS pitch range, stress, richness, voice family,
    // an event icon and a stereo place (pan 0..20, -1 none; panSpeech: the
    // speech is placed too, not only the icons).
    static constexpr size_t formatPayload=14;   // r410: + place tone, lead silence (CharonFormatting.h)
    struct Persona {double rate=1,pitch=1,volume=1;unsigned timbre=0;char colour='-';unsigned icons=0;bool icon=false;
        double range=1,stress=1,richness=1;unsigned family=0;char event='-';int pan=-1;bool panSpeech=false;char place='-';unsigned lead=0;};
    size_t leadLeft_=0;   // r410: samples of silence still to send before the chunk's speech
    void ApplyAcss(double* frames,size_t count,const Persona& p) const;   // TVox frames before the DSP
    double extGain_=1;unsigned extHonoured_=0;
    // r393 external synthesizer state
    ExternalSynth external_{};bool useExternal_=false,extActive_=false,extEnded_=false;
    int16_t extBuffer_[512]={};size_t extCount_=0,extAt_=0;double extPhase_=0,extStep_=1;
    int BeginExternalChunk();
    size_t RenderExternal(int16_t* out,size_t maximum);
    int PumpExternal(const Budget&,const AudioEngine::Sink&,Progress*);
    Persona frontPersona_{},nextPersona_{},dspPersona_{};
    bool ParseSwitch(size_t at,size_t* length);
    void QueueIcon(const Persona&);
    void Timbre(int16_t* samples,size_t count);
    static constexpr size_t iconFrames=9600,iconSlots=6,echoLength=2400;   // r396e: 100 ms icons; r397: 200 ms (event + style)
    int16_t icons_[iconSlots][iconFrames*2]={};size_t iconSlot_=0;
    int16_t echo_[echoLength]={};size_t echoAt_=0;int32_t lowpass_=0;int16_t previous_=0;unsigned appliedTimbre_=0;
    // r397: speech is placed at most 128 +- 90 (the far side keeps about 30%), never in one ear only.
    uint8_t SpeechPan() const {return dspPersona_.pan>=0 && dspPersona_.panSpeech?uint8_t(128+(dspPersona_.pan-10)*9):128;}
    bool frontendOwned_=false,pumping_=false;
    uint64_t generation_=0;
    int error_=0;
    char text_[maximumTextBytes+1]={},chunk_[maximumTextBytes+1]={};
    size_t textBytes_=0,chunkStart_=0,chunkEnd_=0;
    double rate_=1;
    int16_t pending_[pcmBlock]={};
    size_t pendingCount_=0,pendingOffset_=0;
    double pitch_=1,volume_=1;
};
}
