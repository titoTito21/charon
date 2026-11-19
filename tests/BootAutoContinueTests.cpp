#include "../src/efi/BootAutoContinue.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace hakefi;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
struct Fake {
    unsigned clock=0,deadline=10000,pumps=0,stops=0,reads=0;
    unsigned spaceAt=~0u;
    bool failed=false,failPump=false,failStop=false,spaceDuringStop=false;
    bool raceTimerFirst=false,extraKeys=false;
    bool SpacePending() {++reads;return clock>=spaceAt;}
    bool Failed() const {return failed;}
    bool KeysPending() {if(extraKeys){extraKeys=false;spaceAt=clock;return true;}return false;}
    AutoContinueEvent Wait() {
        clock+=10;
        if(clock>=deadline)return AutoContinueEvent::deadline;
        if(!raceTimerFirst && clock>=spaceAt)return AutoContinueEvent::key;
        return AutoContinueEvent::audio;
    }
    bool Pump() {++pumps;return !(failed=failPump);}
    bool Stop() {++stops;if(spaceDuringStop)spaceAt=clock;return !(failed=failStop);}
};
int main() {
    CHECK(autoContinueDelay100ns==100000000);
    {Fake f;CHECK(WaitAutoContinue(f)==AutoContinueResult::handoff);CHECK(f.clock==10000 && f.pumps==999 && f.stops==1);}
    for(unsigned at: {0u,10u,9990u,10000u}) {
        Fake f;f.spaceAt=at;f.raceTimerFirst=true;
        CHECK(WaitAutoContinue(f)==AutoContinueResult::menu);CHECK(f.stops==0 && f.clock<=10000);
    }
    {Fake f;f.spaceDuringStop=true;CHECK(WaitAutoContinue(f)==AutoContinueResult::menu);CHECK(f.stops==1);}
    {Fake f;f.extraKeys=true;CHECK(WaitAutoContinue(f)==AutoContinueResult::menu);CHECK(f.stops==0);}
    {Fake f;f.failPump=true;CHECK(WaitAutoContinue(f)==AutoContinueResult::failed);CHECK(f.stops==0);}
    {Fake f;f.failStop=true;CHECK(WaitAutoContinue(f)==AutoContinueResult::failed);CHECK(f.stops==1);}
    {Fake f;f.failed=true;CHECK(WaitAutoContinue(f)==AutoContinueResult::failed);CHECK(f.clock==0);}
    std::puts("PASS autoboot actual gate: deadline, Space priority, queue drain, stop race and failures");
}
