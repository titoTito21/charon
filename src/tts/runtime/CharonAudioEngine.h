#pragma once
#include "CharonMixer.h"
namespace charon {
// Single owner thread. Cue/melody buffers remain externally owned until drained
// or cancelled. Sink acceptance is not hardware playback completion.
class AudioEngine {
public:
    // r389: two speech channels, mixed: 0 = screen reader / menu (a newer
    // utterance replaces the old one), 1 = boot messages read before a menu
    // ("GNU GRUB version ...", loader logs), which the reader never cuts.
    static constexpr size_t speechCapacity=2048,cueCapacity=12,blockFrames=128,channels=2;
    struct Sink {
        void* context=nullptr;
        size_t (*Write)(void*,const int16_t*,size_t)=nullptr;
        bool (*Drained)(void*)=nullptr;
    };
    uint64_t BeginSpeech(unsigned c=0) {CancelSpeech(c);auto& v=voice_[c%channels];v.open=!v.exhausted;return v.generation;}
    void CancelSpeech(unsigned c=0) {
        auto& v=voice_[c%channels];
        v.head=v.count=0;v.phase=0;v.ended=false;v.open=false;v.marks=0;v.pushed=v.consumed=0;v.pan=v.lastPan=128;
        if(v.generation!=UINT64_MAX)++v.generation;else v.exhausted=true;
        mixer_.CancelSpeech();   // the mixer holds speech for one frame only
    }
    // r397: pan 0 (left) .. 128 (centre) .. 255 (right): Emacspeak-style stereo place of this speech.
    size_t PushSpeech(uint64_t generation,const int16_t* samples,size_t frames,unsigned c=0,uint8_t pan=128) {
        auto& v=voice_[c%channels];
        if(v.exhausted || generation!=v.generation || !v.open || v.ended || (!samples && frames))return 0;
        size_t take=frames;if(take>speechCapacity-v.count)take=speechCapacity-v.count;
        for(size_t i=0;i<take;++i)v.input[(v.head+v.count+i)%speechCapacity]=samples[i];
        // r397: a new place starts at this sample (a few marks, not a per-sample array: Pump copies the
        // whole engine onto the caller's stack, and loaders such as Limine have small stacks).
        if(take && pan!=v.lastPan) {if(v.marks==Voice::panMarks) {for(size_t k=1;k<Voice::panMarks;++k)v.mark[k-1]=v.mark[k];--v.marks;}v.mark[v.marks++]={v.pushed,pan};v.lastPan=pan;}
        v.pushed+=take;
        v.count+=take;return take;
    }
    bool EndSpeech(uint64_t generation,unsigned c=0) {
        auto& v=voice_[c%channels];
        if(v.exhausted || generation!=v.generation || !v.open)return false;
        v.ended=true;v.open=false;
        // Retire only input positions already consumed by accepted output.
        // This produces no PCM and must not depend on a future nonempty Pump.
        while(v.count && v.phase>=48000) {
            v.phase-=48000;v.Advance();
        }
        if(!v.count)v.phase=0;
        return true;
    }
    // r396e: always=true for formatting icons (their own setting), not the menu-sounds switch.
    bool QueueCue(const int16_t* stereo,size_t frames,bool always=false) {
        if((!always && !mixer_.UiSoundsEnabled()) || !stereo || !frames || frames>SIZE_MAX/(2*sizeof(int16_t)) || queued_==cueCapacity)return false;
        cues_[(cueHead_+queued_)%cueCapacity]={stereo,frames,always};++queued_;return true;
    }
    bool SetMelody(const int16_t* stereo,size_t frames) {return mixer_.SetMelody(stereo,frames);}
    void StopMelody() {mixer_.StopMelody();}
    void SetUiSoundsEnabled(bool value) {
        mixer_.SetUiSoundsEnabled(value);if(!value)cueHead_=queued_=0;
    }
    bool SetGains(const Mixer::Gains& gains) {return mixer_.SetGains(gains);}
    size_t MelodyPosition() const {return mixer_.MelodyPosition();}
    size_t BufferedSpeech(unsigned c=0) const {return voice_[c%channels].count;}
    size_t QueuedCues() const {return queued_;}
    bool SoftwareDrained() const {return !Open() && !voice_[0].count && !voice_[1].count && !queued_ && !mixer_.CueActive() && !mixer_.MelodyActive();}
    // Speech only: true once no TVox PCM of that channel is open or buffered
    // (channel 0 also: none sounding in the mixer). Melody and cues are
    // ignored; device-queued frames are not included.
    bool SpeechIdle(unsigned c=0) const {const auto& v=voice_[c%channels];return !v.open && !v.count && (c%channels || !mixer_.SpeechActive());}
    // UI cues only: none queued and none sounding in the mixer.
    bool CuesIdle() const {return !queued_ && !mixer_.CueActive();}
    bool Drained(const Sink& sink) const {return !sinkFault_ && SoftwareDrained() && sink.Drained && sink.Drained(sink.context);}
    bool SinkFault() const {return sinkFault_;}
    // Drops queued and sounding UI cues only; speech and melody continue.
    void CancelCues() {cueHead_=queued_=0;mixer_.CancelCue();}
    void StopAll() {CancelSpeech(0);CancelSpeech(1);cueHead_=queued_=0;mixer_.StopAll();}
    // Sink returns an accepted prefix. Retained PCM and all stream positions
    // advance only for that prefix; zero acceptance is immediate backpressure.
    size_t Pump(const Sink& sink,size_t budget) {
        if(!sink.Write || sinkFault_)return 0;
        size_t total=0;
        while(total<budget) {
            size_t wanted=budget-total;if(wanted>blockFrames)wanted=blockFrames;
            AudioEngine preview=*this;int16_t output[blockFrames*2];size_t produced=0;
            while(produced<wanted && preview.Frame(output+produced*2))++produced;
            if(!produced)break;
            const size_t accepted=sink.Write(sink.context,output,produced);
            if(accepted>produced) {sinkFault_=true;break;}
            int16_t discard[2];for(size_t i=0;i<accepted;++i)Frame(discard);
            total+=accepted;if(accepted<produced)break;
        }
        return total;
    }
private:
    struct Voice {
        int16_t input[speechCapacity]={};size_t head=0,count=0;
        static constexpr size_t panMarks=6;struct PanMark {uint64_t at;uint8_t pan;};
        PanMark mark[panMarks]={};size_t marks=0;uint64_t pushed=0,consumed=0;uint8_t pan=128,lastPan=128;
        void Advance() {head=(head+1)%speechCapacity;--count;++consumed;}
        uint8_t Pan() {while(marks && mark[0].at<=consumed) {pan=mark[0].pan;for(size_t k=1;k<marks;++k)mark[k-1]=mark[k];--marks;}return pan;}uint32_t phase=0;uint64_t generation=0;
        bool ended=false,open=false,exhausted=false;
    };
    bool Open() const {return voice_[0].open || voice_[1].open;}
    static bool SpeechSample(Voice& v,int16_t& sample) {
        while(v.phase>=48000 && v.count>1) {v.phase-=48000;v.Advance();}
        if(v.count==1 && v.ended && v.phase>=48000) {v.count=0;v.phase=0;}
        if(!v.count || (v.count==1 && !v.ended))return false;
        const int32_t first=v.input[v.head];
        const int32_t second=v.count>1?v.input[(v.head+1)%speechCapacity]:first;
        sample=int16_t((int64_t(first)*(48000-v.phase)+int64_t(second)*v.phase)/48000);
        v.phase+=22050;
        // Retire completed final held sample without requiring a phantom frame.
        if(v.count==1 && v.ended && v.phase>=48000) {v.count=0;v.phase=0;}
        return true;
    }
    bool Frame(int16_t* stereo) {
        if(!mixer_.CueActive() && queued_) {
            const auto cue=cues_[cueHead_];cueHead_=(cueHead_+1)%cueCapacity;--queued_;
            mixer_.StartCue(cue.samples,cue.frames,cue.always);
        }
        int16_t a=0,b=0,pair[2];
        const bool first=SpeechSample(voice_[0],a),second=SpeechSample(voice_[1],b);
        // r389 "dubbing": while the reader speaks, the boot-message voice
        // fades to 12% in 60 ms and comes back in 250 ms after the reader's
        // voice ends (a gap under 150 ms keeps it down between words).
        if(first)duckHold_=7200;else if(duckHold_)--duckHold_;
        const int32_t target=duckHold_?3932:32768;
        if(duck_>target) {duck_-=9;if(duck_<target)duck_=target;}
        else if(duck_<target) {duck_+=3;if(duck_>target)duck_=target;}
        if(second)b=int16_t((int32_t(b)*duck_)>>15);
        if(!first && !second && !mixer_.CueActive() && !mixer_.MelodyActive())return false;
        if(first || second) {
            // Both voices at once: summed, then limited (no wrap-around clicks).
            // r397: the reader's voice placed by its pan (balance: centre keeps both sides full).
            // both voices placed by their own pan (the boot-message voice too)
            auto gains=[](unsigned p,int32_t& left,int32_t& right) {left=p<=128?256:int32_t((255-p)*256/127);right=p>=128?256:int32_t(p*256/128);};
            int32_t la,ra,lb,rb;gains(first?voice_[0].Pan():128,la,ra);gains(second?voice_[1].Pan():128,lb,rb);
            int32_t l=int32_t(a)*la/256+int32_t(b)*lb/256,r=int32_t(a)*ra/256+int32_t(b)*rb/256;
            if(l>32767)l=32767;if(l<-32768)l=-32768;if(r>32767)r=32767;if(r<-32768)r=-32768;
            pair[0]=int16_t(l);pair[1]=int16_t(r);mixer_.StartSpeech(pair,1);
        }
        mixer_.Render(stereo,1);mixer_.CancelSpeech();return true;
    }
    struct Cue {const int16_t* samples=nullptr;size_t frames=0;bool always=false;};
    Mixer mixer_;Voice voice_[channels];Cue cues_[cueCapacity];
    size_t cueHead_=0,queued_=0;
    int32_t duck_=32768;uint32_t duckHold_=0;   // r389 message-voice gain (Q15) and hold after reader speech
    bool sinkFault_=false;
};
}
