#pragma once
#include <stdint.h>

namespace hakefi {
constexpr uint64_t autoContinueDelay100ns=100000000; // Ten seconds.
enum class AutoContinueEvent { key, audio, deadline, failed };
enum class AutoContinueResult { menu, handoff, failed };
// Adapter owns EFI events and reports errors separately. No elapsed-tick
// approximation: deadline is a real one-shot timer. All calls run at application
// TPL; no synthesis, allocation or boot operation runs in a timer callback.
template<class Adapter> AutoContinueResult WaitAutoContinue(Adapter& io) {
    bool expired=false;
    for(;;) {
        if(io.SpacePending())return AutoContinueResult::menu;
        if(io.Failed())return AutoContinueResult::failed;
        const auto event=expired?AutoContinueEvent::deadline:io.Wait();
        if(event==AutoContinueEvent::failed)return AutoContinueResult::failed;
        if(event==AutoContinueEvent::key)continue;
        // Prefer cancellation even when firmware returned the timer first.
        if(io.SpacePending())return AutoContinueResult::menu;
        if(io.Failed())return AutoContinueResult::failed;
        if(event==AutoContinueEvent::audio) {
            if(!io.Pump())return AutoContinueResult::failed;
        } else if(event==AutoContinueEvent::deadline) {
            expired=true;
            // A bounded keyboard drain may leave more input queued. Defer the
            // decision until it has been examined instead of dropping Space.
            if(io.KeysPending())continue;
            if(!io.Stop())return AutoContinueResult::failed;
            if(io.SpacePending())return AutoContinueResult::menu;
            if(io.Failed())return AutoContinueResult::failed;
            if(io.KeysPending())continue;
            return AutoContinueResult::handoff;
        }
    }
}
}
