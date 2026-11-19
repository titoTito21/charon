// Allocation-free 48kHz stereo mixer for a future continuous EFI audio backend.
// Clips are caller-owned and must remain valid until consumed or cleared.
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace hakaudioefi {
struct Clip {const int16_t* samples=nullptr;size_t frames=0;};
class Mix {
public:
    bool Background(Clip clip) {
        if(ended || (!clip.samples && clip.frames))return false;
        bed=clip;bedAt=0;return true;
    }
    bool Say(Clip clip) {
        if(ended || !clip.samples || !clip.frames)return false;
        voice=clip;voiceAt=0;return true;
    }
    void Hush() {voice={};voiceAt=0;}
    bool Chime(Clip clip) {
        if(ended || !clip.samples || !clip.frames || chimeCount==4)return false;
        chimes[(chimeHead+chimeCount)%4]=clip;++chimeCount;return true;
    }
    void Handoff() {ended=true;Hush();bed={};chimeCount=0;}
    size_t BedPosition() const {return bedAt;}
    bool Speaking() const {return voice.samples && voiceAt<voice.frames;}
    void Fill(int16_t* output,size_t frames) {
        for(size_t frame=0;frame<frames;++frame) {
            if(ended) {output[2*frame]=output[2*frame+1]=0;continue;}
            const bool speaking=Speaking();
            const int target=speaking ? 13107 : 32768;
            if(bedGain<target)bedGain=bedGain+82<target ? bedGain+82 : target;
            if(bedGain>target)bedGain=bedGain-82>target ? bedGain-82 : target;
            for(unsigned channel=0;channel<2;++channel) {
                int32_t value=0;
                if(bed.samples && bed.frames)value=int32_t(bed.samples[2*bedAt+channel])*bedGain/32768;
                if(chimeCount)value+=chimes[chimeHead].samples[2*chimeAt+channel];
                if(speaking)value+=voice.samples[2*voiceAt+channel];
                output[2*frame+channel]=int16_t(value>32767 ? 32767 : value<-32768 ? -32768 : value);
            }
            if(bed.frames)bedAt=(bedAt+1)%bed.frames;
            if(speaking)++voiceAt;
            if(chimeCount && ++chimeAt==chimes[chimeHead].frames) {
                chimeAt=0;chimeHead=(chimeHead+1)%4;--chimeCount;
            }
        }
    }
private:
    Clip bed,voice,chimes[4];
    size_t bedAt=0,voiceAt=0,chimeAt=0;
    unsigned chimeHead=0,chimeCount=0;
    int bedGain=32768;
    bool ended=false;
};
}
