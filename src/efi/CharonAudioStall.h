#pragma once
#include <stdint.h>
namespace charonaudioefi {
// r409: what to do when the sound card stops taking sound (the stream driver's
// EFI_TIMEOUT: its cursor stood still for 200 ms, or its 1 ms poll came more
// than 20 ms late). Until r409 every such stall restarted the stream at once,
// up to 64 times per utterance. In a virtual machine whose host cannot play
// (several machines sounding at once, a full host disk) the stall does not go
// away, so the restarts came five times a second, and every one of them makes
// the machine's processor thread close and open the host's sound stream
// (measured in VMware: "Stopping/Closing/Creating WAVE stream" on vcpu-0 for
// each restart). While that host call does not return, nothing of Charon runs:
// no key, no speech, no trace. The dialogue looked hung.
//
// Rule now: a stream that stalls again before it has played 300 ms did not
// recover. The third such stall in a row stops the restarts: the stream stays
// stopped ("rest") for 1 s, then 2, 4 and at most 8 s while it keeps
// stalling, and is opened again after the rest. Speech that was asked for
// stays queued and goes on when the card plays again; a newer utterance
// replaces it as always. A restart that itself took longer than 250 ms counts
// as two stalls, and the rest after it is at least four times as long as that
// restart was (60 s at most), so a card whose every restart holds the machine
// for seconds is touched for a fifth of the time at most. Stalls with sound in
// between (a disk read, a late timer) are restarted at once as before.
//
// No EFI types here, so the host test (tests/CharonAudioStallTests.cpp) runs
// this very code against a simulated card.
class StallPolicy {
public:
    static constexpr unsigned maximumStalls=3;
    static constexpr uint64_t progressFrames=14400;    // 300 ms at 48 kHz taken by the stream since it was opened
    static constexpr uint64_t healthyFrames=192000;    // 4 s without a restart: the trouble is over
    static constexpr unsigned slowRestartMs=250;
    static constexpr unsigned firstRestSeconds=1,maximumLevel=3,longestRestSeconds=60;
    static constexpr unsigned restCalls=256;           // without a usable clock a rest is counted in calls
    // ticks of the caller's clock per millisecond; 0 = no usable clock
    void SetClock(uint64_t ticksPerMs) {ticksPerMs_=ticksPerMs;}
    uint64_t TicksPerMs() const {return ticksPerMs_;}
    // The stream stalled after it took `frames` since it was opened. True: restart it now. False: rest.
    bool Stalled(uint64_t frames) {
        stalls_=frames>=progressFrames?1:stalls_+1;
        return stalls_<maximumStalls;
    }
    // A restart took `ticks`. True when that was slow (the next stall without sound rests at once).
    bool Restarted(uint64_t ticks) {
        if(!ticksPerMs_ || ticks<=uint64_t(slowRestartMs)*ticksPerMs_) {slowMs_=0;return false;}
        slowMs_=ticks/ticksPerMs_;
        if(stalls_<maximumStalls-1)stalls_=maximumStalls-1;
        return true;
    }
    // Starts a rest at `now`; returns its length in seconds.
    unsigned Rest(uint64_t now) {
        resting_=true;start_=now;calls_=0;seconds_=firstRestSeconds<<level_;callLimit_=restCalls<<level_;
        const uint64_t afterSlow=slowMs_/250+1;   // four times the slow restart, in seconds
        if(slowMs_ && afterSlow>seconds_)seconds_=afterSlow<longestRestSeconds?unsigned(afterSlow):longestRestSeconds;
        if(level_<maximumLevel)++level_;
        stalls_=maximumStalls-1;   // the stream opened after the rest gets one try
        return seconds_;
    }
    bool Resting() const {return resting_;}
    // True once the rest is over (and from then on, until the next Rest).
    bool RestOver(uint64_t now) {
        if(!resting_)return true;
        const bool over=ticksPerMs_?now-start_>=uint64_t(seconds_)*1000*ticksPerMs_:++calls_>=callLimit_;
        if(over)resting_=false;
        return over;
    }
    // The open stream has taken `frames` so far.
    void Played(uint64_t frames) {if(frames>=healthyFrames) {level_=0;stalls_=0;}}
    unsigned Level() const {return level_;}
    unsigned Stalls() const {return stalls_;}
    void Clear() {const uint64_t clock=ticksPerMs_;*this=StallPolicy();ticksPerMs_=clock;}
private:
    uint64_t ticksPerMs_=0,start_=0,slowMs_=0;
    unsigned stalls_=0,level_=0,seconds_=0,calls_=0,callLimit_=0;
    bool resting_=false;
};
}
