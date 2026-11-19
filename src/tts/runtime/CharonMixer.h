#pragma once
#include <stddef.h>
#include <stdint.h>

namespace charon {
// All inputs/output are interleaved signed PCM16, 48000 Hz, two channels.
// Buffers remain caller-owned, readable until their stream stops, and must not
// alias the output. Call on one owner thread. No allocation or device access.
class Mixer {
public:
    static constexpr uint32_t sampleRate=48000;
    struct Gains {uint32_t speech=16384,cue=8192,melody=4096;};
    bool SetGains(const Gains& value) {
        if(value.speech>32768 || value.cue>32768 || value.melody>32768
            || uint64_t(value.speech)+value.cue+value.melody>32768)return false;
        gains_=value;return true;
    }
    bool StartSpeech(const int16_t* samples,size_t frames) {return Set(speech_,samples,frames);}
    bool StartCue(const int16_t* samples,size_t frames,bool always=false) {
        if(!sounds_ && !always)return false;
        return Set(cue_,samples,frames);
    }
    bool SetMelody(const int16_t* samples,size_t frames) {return Set(melody_,samples,frames);}
    void CancelSpeech() {speech_={};}
    void CancelCue() {cue_={};}
    void StopMelody() {melody_={};}
    void StopAll() {CancelSpeech();CancelCue();StopMelody();}
    void SetUiSoundsEnabled(bool enabled) {sounds_=enabled;if(!enabled)CancelCue();}
    bool UiSoundsEnabled() const {return sounds_;}
    bool SpeechActive() const {return Active(speech_);}
    bool CueActive() const {return Active(cue_);}
    bool MelodyActive() const {return melody_.frames!=0;}
    size_t SpeechPosition() const {return speech_.position;}
    size_t CuePosition() const {return cue_.position;}
    size_t MelodyPosition() const {return melody_.position;}
    bool Render(int16_t* output,size_t frames) {
        if((!output && frames) || frames>SIZE_MAX/(2*sizeof(int16_t)))return false;
        for(size_t i=0;i<frames;++i) {
            const bool speech=Active(speech_),cue=Active(cue_),melody=Active(melody_);
            for(size_t channel=0;channel<2;++channel) {
                int64_t sum=0;
                if(speech)sum+=int64_t(speech_.samples[speech_.position*2+channel])*gains_.speech;
                if(cue)sum+=int64_t(cue_.samples[cue_.position*2+channel])*gains_.cue;
                if(melody)sum+=int64_t(melody_.samples[melody_.position*2+channel])*gains_.melody;
                // Total gain <= 1: even three coincident full-scale streams
                // fit PCM16. Division truncates towards zero on every target.
                output[i*2+channel]=int16_t(sum/32768);
            }
            if(speech)++speech_.position;
            if(cue)++cue_.position;
            if(melody && ++melody_.position==melody_.frames)melody_.position=0;
        }
        return true;
    }
private:
    struct Stream {const int16_t* samples=nullptr;size_t frames=0,position=0;};
    static bool Active(const Stream& stream) {return stream.position<stream.frames;}
    static bool Set(Stream& stream,const int16_t* samples,size_t frames) {
        if((!samples && frames) || frames>SIZE_MAX/(2*sizeof(int16_t)))return false;
        stream={samples,frames,0};return true;
    }
    Stream speech_,cue_,melody_;
    Gains gains_;
    bool sounds_=true;
};
}
