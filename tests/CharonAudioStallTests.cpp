// r409 host test of src/efi/CharonAudioStall.h: the rule Charon's voice provider follows when the
// HDA stream stalls (EFI_TIMEOUT), run against a simulated sound card on a simulated clock.
// The card: takes 48 frames per millisecond while it plays; takes nothing while "stalled", and the
// stream driver then reports a timeout 200 ms after the last progress (as HakAudioStream.c does).
// A restart of the stream holds everything for `restartMs` (in a virtual machine that is the time the
// host needs to close and open its sound stream). Charon pumps every 10 ms.
// Build and run:  clang++ -std=c++17 -O1 tests/CharonAudioStallTests.cpp -o stall-tests.exe && stall-tests.exe
#include "../src/efi/CharonAudioStall.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
using charonaudioefi::StallPolicy;
static int failures=0,checks=0;
#define CHECK(x) do {++checks;if(!(x)) {++failures;printf("FAIL line %d: %s\n",__LINE__,#x);}} while(false)

struct Result {
    unsigned restarts=0,rests=0,pumpsAnswered=0,pumps=0;
    uint64_t heldMs=0,longestSilentPumpGapMs=0,playedFrames=0,firstSoundAfterStallMs=UINT64_MAX,endMs=0;
    bool gaveUp=false;   // the old rule: the 64th restart of an utterance is a failed voice
};
// stalled(t): is the card unable to play at time t (ms)?  policyOn=false: the rule before r409.
static Result Run(uint64_t totalMs,const std::function<bool(uint64_t)>& stalled,uint64_t restartMs,bool policyOn,uint64_t ticksPerMs=1000) {
    Result r;StallPolicy policy;policy.SetClock(ticksPerMs);
    uint64_t now=0,lastProgress=0,submitted=0,lastPump=0,stallEnd=UINT64_MAX;unsigned oldRecoveries=0;
    bool open=true,fault=false,wasStalled=false;
    auto ticks=[&] {return now*(ticksPerMs?ticksPerMs:1);};
    for(;now<totalMs;now+=10) {
        const bool s=stalled(now);
        if(wasStalled && !s)stallEnd=now;
        wasStalled=s;
        // the card and its driver, 10 ms of them
        if(open && !fault) {
            if(!s) {submitted+=480;r.playedFrames+=480;lastProgress=now;if(stallEnd!=UINT64_MAX && r.firstSoundAfterStallMs==UINT64_MAX)r.firstSoundAfterStallMs=now-stallEnd;}
            else if(now-lastProgress>=200)fault=true;
        }
        // Charon's pump
        ++r.pumps;
        if(now-lastPump>r.longestSilentPumpGapMs)r.longestSilentPumpGapMs=now-lastPump;
        lastPump=now;
        if(r.gaveUp)continue;
        if(policyOn && policy.Resting()) {
            if(!policy.RestOver(ticks())) {++r.pumpsAnswered;continue;}
            const uint64_t before=ticks();
            now+=restartMs;r.heldMs+=restartMs;++r.restarts;open=true;fault=false;submitted=0;lastProgress=now;   // opened again after the rest
            policy.Restarted(ticks()-before);
            ++r.pumpsAnswered;continue;
        }
        if(policyOn)policy.Played(submitted);
        if(fault) {
            const bool restart=policyOn?policy.Stalled(submitted):++oldRecoveries<=64;
            if(!policyOn && !restart) {r.gaveUp=true;continue;}
            if(!restart) {open=false;fault=false;++r.rests;policy.Rest(ticks());++r.pumpsAnswered;continue;}
            const uint64_t before=ticks();
            now+=restartMs;r.heldMs+=restartMs;++r.restarts;fault=false;submitted=0;lastProgress=now;
            if(policyOn)policy.Restarted(ticks()-before);
        }
        ++r.pumpsAnswered;
    }
    r.endMs=now;return r;
}
static void Show(const char* name,const Result& r) {
    printf("%-58s restarts %3u  rests %2u  held %6llu ms  longest gap between pumps %5llu ms  sound again after %lld ms%s\n",name,r.restarts,r.rests,
        (unsigned long long)r.heldMs,(unsigned long long)r.longestSilentPumpGapMs,r.firstSoundAfterStallMs==UINT64_MAX?-1LL:(long long)r.firstSoundAfterStallMs,r.gaveUp?"  VOICE FAILED":"");
}
int main() {
    // 1. The card stops taking frames 2 s into an utterance and resumes 40 s later; a restart costs 30 ms (measured in VMware).
    auto stall40=[](uint64_t t) {return t>=2000 && t<42000;};
    Result before=Run(70000,stall40,30,false),after=Run(70000,stall40,30,true);
    Show("40 s stall, restart 30 ms, rule before r409",before);Show("40 s stall, restart 30 ms, r409",after);
    CHECK(before.gaveUp && before.restarts==64);            // the old rule: 64 restarts in 15 s, then the voice reported failed
    CHECK(!after.gaveUp && after.rests>=3 && after.restarts<=10);
    CHECK(after.firstSoundAfterStallMs<=8000+30);            // the longest rest
    CHECK(after.playedFrames>48u*(70000-40000-8100));        // it played all the time the card could, less one rest at most
    // 2. The same stall when every restart holds the machine for 8 s (a host that cannot open its sound stream).
    Result slowBefore=Run(120000,[](uint64_t t) {return t>=2000 && t<62000;},8000,false),slowAfter=Run(120000,[](uint64_t t) {return t>=2000 && t<62000;},8000,true);
    Show("60 s stall, restart 8 s, rule before r409",slowBefore);Show("60 s stall, restart 8 s, r409",slowAfter);
    CHECK(slowBefore.heldMs>=56000);                          // held for nearly the whole stall: the dialogue answers no key
    CHECK(slowAfter.restarts<=3 && slowAfter.heldMs<=3*8000);   // one restart, then one try after each 32 s rest
    CHECK(slowAfter.heldMs*2<slowBefore.heldMs && slowAfter.pumpsAnswered>slowBefore.pumpsAnswered);   // the pumps in between return at once: the dialogue goes on
    {StallPolicy p;p.SetClock(1000);p.Stalled(0);p.Restarted(8000*1000);CHECK(p.Rest(0)==33);p.Restarted(100*1000*1000);CHECK(p.Rest(0)==60);p.Restarted(1000);CHECK(p.Rest(0)==4);}
    // The first slow restart is counted double: after it one more stall is enough to rest.
    {StallPolicy p;p.SetClock(1000);CHECK(p.Stalled(0));CHECK(p.Restarted(8000*1000));CHECK(!p.Stalled(0));}
    {StallPolicy p;p.SetClock(1000);CHECK(p.Stalled(0));CHECK(!p.Restarted(30*1000));CHECK(p.Stalled(0));CHECK(!p.Stalled(0));}
    // 3. Short stalls with sound in between (a disk read every 1.5 s): restarted at once every time, never a rest (r400 behaviour kept).
    Result sparse=Run(60000,[](uint64_t t) {return t%1500<250;},30,true);
    Show("250 ms stall every 1.5 s, r409",sparse);
    CHECK(sparse.rests==0 && sparse.restarts>=35 && !sparse.gaveUp);
    // 4. Two stalls back to back (seen after a paused machine goes on), then sound: no rest.
    {StallPolicy p;p.SetClock(1000);CHECK(p.Stalled(100000));CHECK(p.Stalled(1200));CHECK(p.Stalls()==2);p.Played(StallPolicy::healthyFrames);CHECK(p.Stalls()==0);}
    // 5. Rests grow 1, 2, 4, 8, 8 s while the card keeps stalling and start at 1 s again after 4 s of sound.
    {StallPolicy p;p.SetClock(1000);uint64_t t=0;const unsigned want[]={1,2,4,8,8};
     for(unsigned w:want) {
        while(p.Stalled(0)) {}
        const unsigned s=p.Rest(t);CHECK(s==w);CHECK(p.Resting());
        CHECK(!p.RestOver(t+uint64_t(s)*1000*1000-1));CHECK(p.RestOver(t+uint64_t(s)*1000*1000));CHECK(!p.Resting());t+=uint64_t(s)*1000*1000;
        CHECK(!p.Stalled(0));   // the stream opened after a rest gets one try
     }
     p.Rest(t);CHECK(p.RestOver(t+8000000));p.Played(StallPolicy::healthyFrames);CHECK(p.Level()==0);
     CHECK(p.Stalled(0) && p.Stalled(0) && !p.Stalled(0));CHECK(p.Rest(t)==1);}
    // 6. A rest always ends: without a usable clock it is counted in calls, and a clock that wraps does not hold it.
    {StallPolicy p;p.SetClock(0);p.Rest(123);unsigned calls=0;while(!p.RestOver(123)) {++calls;CHECK(calls<100000);if(calls>=100000)break;}CHECK(calls==StallPolicy::restCalls-1);}
    {StallPolicy p;p.SetClock(1000);p.Rest(UINT64_MAX-5);CHECK(!p.RestOver(UINT64_MAX));CHECK(p.RestOver(uint64_t(1000000)));}
    {Result noClock=Run(70000,stall40,30,true,0);Show("40 s stall, no clock, r409",noClock);CHECK(!noClock.gaveUp && noClock.restarts<=12 && noClock.firstSoundAfterStallMs!=UINT64_MAX);}
    // 7. Clear keeps the clock and forgets the rest.
    {StallPolicy p;p.SetClock(777);p.Rest(1);p.Clear();CHECK(!p.Resting() && p.TicksPerMs()==777 && p.Level()==0);}
    printf("%d checks, %d failed\n",checks,failures);
    return failures?1:0;
}
