#pragma once
#include "BootMenuVoice.h"
#include "CharonAudioAssets.h"
#include "CharonAudioDevice.h"
#include "CharonAudioStall.h"   // r409
#include "CharonVoiceRest.h"   // r409: the resting flag a wait for speech reads
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif
#include "CharonPcmSink.h"   // r407: another program's sound (CharonPC's Sound Blaster) under the speech
#include "CharonPoolEFI.h"
#include "CharonTrace.h"
#include "../tts/runtime/CharonLiveVoice.h"
#include "CharonSynthHost.h"

namespace charonaudioefi {
// Application-owned real TVox provider. Firmware calls have EFI ABI; the
// frontend/context switch remains ordinary x86-64 SysV inside the application.
class VoiceProvider {
public:
    VoiceProvider() {
        bridge_.owner=this;
        bridge_.api={{2,sizeof(hakefi::MenuVoiceV2),SpeakThunk,PumpThunk,StopThunk},PreparingThunk};
        effects_.context=this;effects_.QueueCue=CueThunk;effects_.SetEnabled=EnabledThunk;
        effects_.SetMelodies=MelodiesThunk;effects_.SetRate=RateThunk;
        effects_.SetVolumes=VolumesThunk;effects_.SpeechIdle=SpeechIdleThunk;effects_.CuesIdle=CuesIdleThunk;effects_.Tone=ToneThunk;effects_.SetPitch=PitchThunk;effects_.Beep=BeepThunk;
        effects_.SetSynth=SetSynthThunk;effects_.SynthVoices=SynthVoicesThunk;effects_.UseSynthVoice=UseSynthVoiceThunk;effects_.SynthAt=SynthAtThunk;effects_.ActiveSynthKey=ActiveKeyThunk;effects_.Linger=LingerThunk;effects_.PreloadSynths=PreloadThunk;effects_.SynthLoaded=LoadedThunk;
        effects_.SpeakMessage=MessageThunk;effects_.Silence=SilenceThunk;effects_.SetMessageVoice=MessageVoiceThunk;effects_.MessageIdle=MessageIdleThunk;
    }
    // deferDevice (r356 resident driver, loaded from Driver#### before
    // firmware connects PCI): register AudioDxe without connecting
    // controllers and succeed without a device; EnsureRunning opens the
    // stream once firmware has bound the HDA controller.
    efi_status Initialize(efi_handle self,efi_boot_services* boot,efi_file_protocol* root,bool deferDevice=false) {
        if(initialized_)return EFI_ALREADY_STARTED;
        if(!self || !boot || !root)return EFI_INVALID_PARAMETER;
        boot_=boot;image_=self;
        {efi_guid lg=EFI_LOADED_IMAGE_PROTOCOL_GUID;efi_loaded_image_protocol* li=nullptr;if(boot->HandleProtocol(self,&lg,(void**)&li)==EFI_SUCCESS && li)volume_=li->DeviceHandle;}
        if(!charon::InitializeSpeechPool(pool_,boot,256*1024*1024))return EFI_OUT_OF_RESOURCES;
        initialized_=true;
        auto status=Convert(voice_.Initialize(&pool_));
        if(status!=EFI_SUCCESS)return status;
        // r389: boot messages: a second voice on the second speech channel.
        messagesReady_=intro_.Initialize(&pool_,voice_.Engine(),1)==0;
        status=boot_->CreateEvent(EVT_TIMER,TPL_APPLICATION,nullptr,nullptr,&preparationDeadline_);
        if(status!=EFI_SUCCESS)return status;
        // Finish file I/O before starting DMA; the menu will pump promptly
        // once Open has established the actual device stream.
        assetsStatus_=assets_.Load(root,&pool_);
        // r405: a USB sound card is the last choice: not before AudioDxe had its chance.
        CalibrateClock();   // r409
        status=device_.Open(boot,false);
        charontrace::Value("CHARON audio open",uint64_t(status));
        if(status==EFI_NOT_FOUND && !device_.NeedsStop()) {
            status=LoadDriver(self,root,!deferDevice);
            charontrace::Value("CHARON audio driver",uint64_t(status));
            // r405: also without the driver (no AudioDxe file, load refused): a USB sound card may be there.
            const auto opened=device_.Open(boot);
            if(status==EFI_SUCCESS || opened==EFI_SUCCESS)status=opened;
            charontrace::Value("CHARON audio open2",uint64_t(status));
        }
        if(deferDevice && status!=EFI_SUCCESS && !device_.NeedsStop()) {ready_=true;pcm_.Publish(boot_);return EFI_SUCCESS;}
        if(status!=EFI_SUCCESS)return status;
        ready_=true;pcm_.Publish(boot_);   // r407
        return EFI_SUCCESS; // Speech can work without optional effects assets.
    }
    hakefi::MenuVoice* Voice() {return ready_?&bridge_.api.base:nullptr;}
    hakefi::AudioEffects* Effects() {return ready_&&assets_.Ready()?&effects_:nullptr;}
    efi_status AssetsStatus()const {return assetsStatus_;}
    efi_status DeviceStatus()const {return device_.LastError();}
    // Historical playback failure is independent of current DMA ownership.
    // A successful Stop permits handoff but must not erase this first cause.
    efi_status PlaybackStatus()const {return playbackError_;}
    // Called only after menu has stopped using the interface. Failed Stop
    // retains all storage and the driver binding; callers must not unload us.
    efi_status Shutdown() {
        const auto status=Stop();if(status!=EFI_SUCCESS)return status;
        if(pcm_.Withdraw()!=EFI_SUCCESS)return EFI_ACCESS_DENIED;   // r407: its callbacks point into this object
        if(messagesReady_)intro_.Shutdown();
        messagesReady_=false;voice_.Shutdown();assets_.Clear();
        if(preparationDeadline_)boot_->CloseEvent(preparationDeadline_);
        preparationDeadline_=nullptr;ready_=false;initialized_=false;boot_=nullptr;
        melodiesSet_=false;sounds_=true;start_=background_=0;stall_.Clear();return EFI_SUCCESS;
    }
    efi_status Stop() {
        if(!ready_)return device_.Stop();
        voice_.CancelSpeech();voice_.StopMelody();
        if(messagesReady_)intro_.CancelSpeech();
        if(!device_.NeedsStop())return EFI_SUCCESS;
        // Drain actual queued effects (including Continue) and already accepted
        // device frames. Elapsed time is only a timeout, never playback proof.
        // A fault can be aborted safely only through the driver's real Stop.
        if(device_.LastError()!=EFI_SUCCESS) {RememberPlaybackError(device_.LastError());voice_.StopAll();return device_.Stop();}
        for(unsigned attempt=0;attempt<5000;++attempt) {
            charon::LiveVoice::Progress progress;
            const auto status=Convert(voice_.Pump({0,0,480},device_.Sink(),&progress));
            if(status!=EFI_SUCCESS) {RememberPlaybackError(status);voice_.StopAll();return device_.Stop();}
            if(device_.LastError()!=EFI_SUCCESS) {RememberPlaybackError(device_.LastError());voice_.StopAll();return device_.Stop();}
            if(voice_.Drained(device_.Sink()))return device_.Stop();
            boot_->Stall(1000);
        }
        return EFI_TIMEOUT;
    }
private:
    void RememberPlaybackError(efi_status status) {if(playbackError_==EFI_SUCCESS&&status!=EFI_SUCCESS)playbackError_=status;}
    struct Bridge {hakefi::MenuVoiceV2 api;VoiceProvider* owner;};
    static VoiceProvider* Owner(hakefi::MenuVoice* api) {return reinterpret_cast<Bridge*>(api)->owner;}
    static bool EFIAPI PreparingThunk(hakefi::MenuVoice* api) {return Owner(api)->voice_.Preparing();}
    static efi_status Convert(int value) {
        if(!value)return EFI_SUCCESS;
        if(value==charon::LiveVoice::NoMemory)return EFI_OUT_OF_RESOURCES;
        if(value==charon::LiveVoice::UnsupportedLanguage)return EFI_UNSUPPORTED;
        if(value==charon::LiveVoice::Invalid)return EFI_INVALID_PARAMETER;
        if(value==charon::LiveVoice::Busy)return EFI_ALREADY_STARTED;
        return EFI_DEVICE_ERROR;
    }
    // A timing-ambiguity fault (EFI_TIMEOUT: a delayed firmware poll could hide
    // ring progress) is recovered only by the driver's real Stop and a fresh
    // Begin. Timing limits stay strict; a failed Stop keeps the fault and the
    // device ownership. Bounded: r409, by the rule in CharonAudioStall.h (before
    // r409 by 64 restarts per utterance, after which the voice stayed failed).
    efi_status Recover() {
        const auto fault=device_.LastError();
        if(fault!=EFI_TIMEOUT || !device_.NeedsStop())return fault;
        // r400 (user: the greeting came out as "ha, ha, haron"): only the
        // device is restarted. The speech already synthesized stays queued in
        // the engine and goes on from where the sound card stopped; saying
        // the utterance again from its start made every stall of the firmware
        // (a disk read while a boot option loads) a stutter.
        // r409 (CharonAudioStall.h): a card that stalls again without having
        // played is not restarted a third time in a row; it rests. Every
        // restart is a stop, a codec setup and a start of the stream, and on a
        // host that cannot play each of them may hold the whole machine.
#ifdef HAK_CHARON_STALLTEST_OLD
        if(recoveries_>=maximumRecoveries)return fault;   // test builds only: the rule before r409, to compare
        const bool restart=true;
#else
        const bool restart=stall_.Stalled(device_.Submitted()) && recoveries_<maximumRecoveries;
#endif
        if(device_.Stop()!=EFI_SUCCESS)return fault;
        ++recoveries_;charontrace::Value("CHARON audio recovered",recoveries_);
        if(!restart)return Rest();
        const uint64_t before=Ticks();
        const auto status=EnsureRunning();
        const uint64_t took=Ticks()-before;
        if(stall_.Restarted(took))charontrace::Value("CHARON audio restart slow, ms",took/stall_.TicksPerMs());
        if(status!=EFI_SUCCESS) {
            // The stream would not start again: tried once more after a rest instead of failing the voice for good.
            charontrace::Value("CHARON audio restart status",uint64_t(status));
            if(device_.NeedsStop())device_.Stop();
            if(!device_.NeedsStop())return Rest();
        }
        return status;
    }
    // r409: the processor's time-stamp counter, measured once against the firmware's Stall,
    // in Initialize (12 ms). Never later: the screen reader hooks Stall and changes the
    // synthesizer inside it, which must not happen while the voice is being pumped (tried:
    // measured at the first stall, speech came out in scraps afterwards). The firmware's timer
    // events are no measure here: their time advances by one period per delivered interrupt,
    // so it stands still while the machine itself is held.
    static uint64_t Ticks() {
        if(TestClock())return TestClock();   // r410 host tests
#if defined(__x86_64__) || defined(__i386__)
        return __builtin_ia32_rdtsc();
#elif defined(_M_X64) || defined(_M_IX86)
        return __rdtsc();
#else
        return 0;
#endif
    }
    void CalibrateClock() {
        if(clockKnown_ || !boot_ || !boot_->Stall)return;
        clockKnown_=true;uint64_t best=0;
        // Stall(5 ms) less Stall(1 ms) is 4 ms without the call's own fixed cost (VMware: a 1 ms
        // Stall alone measured 1.4 ms). The smaller of two: a measurement the host interrupted
        // is too long, never too short.
        for(int i=0;i<2;++i) {
            const uint64_t a=Ticks();boot_->Stall(1000);const uint64_t b=Ticks();boot_->Stall(5000);const uint64_t c=Ticks();
            const uint64_t shortOne=b-a,longOne=c-b;
            if(longOne>shortOne) {const uint64_t ticks=(longOne-shortOne)/4;if(ticks && (!best || ticks<best))best=ticks;}
        }
        stall_.SetClock(best>=1000?best:0);   // under 1 MHz: no usable counter, a rest is then counted in calls
        charontrace::Value("CHARON audio clock, ticks per ms",stall_.TicksPerMs());
    }
    // The stream is stopped and stays so; EnsureRunning opens it again when the rest is over.
    // Success for the caller: speech stays queued, the dialogue goes on.
    efi_status Rest() {
        charontrace::Value("CHARON audio stalled, stream rests, s",stall_.Rest(Ticks()));hakefi::VoiceResting()=true;
        return EFI_SUCCESS;
    }
    efi_status EnsureRunning() {
        // r409: a stalled stream rests (CharonAudioStall.h): nothing touches the card meanwhile.
        bool afterRest=false;
        if(stall_.Resting()) {
            if(!stall_.RestOver(Ticks()))return EFI_SUCCESS;
            afterRest=true;hakefi::VoiceResting()=false;charontrace::Value("CHARON audio rest over, level",stall_.Level());
        }
        // r402: firmware took the audio driver away (it reconnects controllers at
        // start-up). Speech stays queued and sound returns when the card is bound again.
        if(device_.Vanished()) {
            // r407: once the tries are used up, look for a card only now and then: every call
            // searched all sound cards and USB devices again (hundreds of times a second).
            // r407: at once when a USB device was plugged in (a USB sound card put back is heard without a wait).
            if(reconnects_>=12 && (++vanishedCalls_&1023) && !device_.UsbChanged(boot_))return EFI_SUCCESS;
            auto reopened=device_.Open(boot_);
            if(reopened!=EFI_SUCCESS) {
                if(reconnects_<12)charontrace::Value("CHARON audio reopen status",reopened);
                // a failed setup leaves the stream held: let go of it before the next try
                if(device_.NeedsStop())device_.Stop();
                // the card may have been left without a driver: bind it again, a few times at most
                if(reconnects_>=12)return EFI_SUCCESS;
                ++reconnects_;if(driver_)DisconnectFirmwareHda(boot_,driver_);ConnectAudioControllers(boot_);   // r404: the firmware's own HDA driver may have taken the card back
                reopened=device_.Open(boot_);
                if(reopened!=EFI_SUCCESS) {if(device_.NeedsStop())device_.Stop();device_.MarkVanished();return EFI_SUCCESS;}
            }
            charontrace::Value("CHARON audio reopened",1);
        }
        if(device_.NeedsStop()) {
            const auto fault=device_.LastError();
            if(fault==EFI_TIMEOUT)return Recover();
            if(fault==EFI_SUCCESS)return EFI_SUCCESS;
            // r406: any other fault of the stream (seen in VMware right after the network
            // came up: the dialogue went on, silent for good). The device is stopped and
            // opened again, a few times in a row at most; speech asked for meanwhile is lost,
            // the next utterance is heard.
            if(faultRestarts_>=maximumFaultRestarts)return fault;
            ++faultRestarts_;charontrace::Value("CHARON audio fault restart",uint64_t(fault));
            device_.Stop();
            if(device_.NeedsStop()) {device_.MarkVanished();return EFI_SUCCESS;}   // the stream would not stop: found again as a vanished one
        }
        const uint64_t before=afterRest?Ticks():0;
        const auto status=device_.Open(boot_);
        if(afterRest && stall_.Restarted(Ticks()-before))charontrace::Value("CHARON audio open after rest slow, ms",(Ticks()-before)/stall_.TicksPerMs());
        if(status!=EFI_SUCCESS && afterRest) {   // r409: still no stream: the next try comes after a longer rest
            charontrace::Value("CHARON audio open after rest",uint64_t(status));
            if(device_.NeedsStop())device_.Stop();
            if(!device_.NeedsStop())return Rest();
        }
        if(status==EFI_SUCCESS && assets_.Ready()) {
            const auto sound=assets_.Background(background_);
            voice_.SetMelody(sound.samples,sound.frames);
            // r385: a cue asked for before firmware bound the audio
            // controller (the resident driver's welcome) plays now.
            if(pendingBeepHz_) {
                charontrace::Value("CHARON beep late",(uint64_t(pendingBeepHz_)<<16)|pendingBeepMs_);
                const unsigned hz=pendingBeepHz_,ms=pendingBeepMs_;pendingBeepHz_=0;
                if(sounds_)Square(hz,ms);
            }
            if(pendingCue_!=hakefi::MenuCue::none) {
                const auto cue=assets_.Cue(pendingCue_);
                charontrace::Value("CHARON cue late",unsigned(pendingCue_));
                pendingCue_=hakefi::MenuCue::none;
                if(cue.samples && sounds_)voice_.QueueCue(cue.samples,cue.frames);
            }
        }
        return status;
    }
    static efi_status EFIAPI SpeakThunk(hakefi::MenuVoice* api,const char* language,const char* text) {
        auto* self=Owner(api);
        if(!language || !text)return EFI_INVALID_PARAMETER;
        charon::LiveVoice::Language selected;
        if(language[0]=='p' && language[1]=='l' && !language[2])selected=charon::LiveVoice::Language::Polish;
        else if(language[0]=='e' && language[1]=='n' && !language[2])selected=charon::LiveVoice::Language::English;
        else return EFI_UNSUPPORTED;
        size_t bytes=0;while(bytes<=charon::LiveVoice::maximumTextBytes && text[bytes])++bytes;
        if(bytes>charon::LiveVoice::maximumTextBytes)return EFI_BAD_BUFFER_SIZE;
        const auto status=self->EnsureRunning();if(status!=EFI_SUCCESS)return status;
        self->faultRestarts_=0;self->recoveries_=0;   // r400: the bound is per utterance, or a long session went mute after 64 stalls
        charontrace::Text("CHARON speak");charontrace::Text(language);charontrace::Text(text);
#ifdef HAK_CHARON_STALLTEST
        self->stallTestStart_=bytes>150?Ticks():0;
#endif
        // r389: an interruption drops the old speech the driver still holds
        // (up to 150 ms before), so the new text is heard at once.
        // Not while a boot message speaks: its sound is mixed into the same frames.
        if(!self->voice_.SpeechIdle() && (!self->messagesReady_ || self->intro_.SpeechIdle()) && self->device_.Discard())charontrace::Value("CHARON discard",1);
        if(self->otherReader_==Other::Pending && self->NeedsOther(selected,text,bytes))self->PrepareOther(self->loaded_[self->activeSynth_].reader,self->otherReader_,self->voice_);
        const auto queued=Convert(self->voice_.QueueText(selected,text,bytes,self->rate_,self->pitch_));
        if(queued==EFI_SUCCESS) {
            for(size_t i=0;i<bytes;++i)self->lastText_[i]=text[i];
            self->lastText_[bytes]=0;self->lastTextBytes_=bytes;self->lastLanguage_=selected;
            self->traceGenerated_=false;self->tracePump_=self->traceCheckpoints_=0;
        }
        charontrace::Value("CHARON speak status",queued);
        return queued;
    }
    static int PreparationDeadline(void* context) {
        auto* self=static_cast<VoiceProvider*>(context);
        // Firmware event services may be expensive or round a short timer to
        // already-signalled. Guarantee useful bounded progress and amortize
        // event polling without changing any synthesis algorithm checkpoints.
        ++self->deadlineCheckpoint_;
        if(self->deadlineCheckpoint_<=4096 || (self->deadlineCheckpoint_-1)%1024)return 0;
        const auto status=self->boot_->CheckEvent(self->preparationDeadline_);
        if(status==EFI_NOT_READY)return 0;
        if(status!=EFI_SUCCESS)self->deadlineError_=status;
        return 1;
    }
    uint64_t lastPumpTicks_=0,catchUpFrames_=0;unsigned latePumps_=0;   // r410
public:
    unsigned LatePumps() const {return latePumps_;}
    uint64_t CatchUpFrames() const {return catchUpFrames_;}
    static uint64_t& TestClock() {static uint64_t ticks=0;return ticks;}   // host tests: a clock they move (0: the processor's counter)
    void TestSetClock(uint64_t ticksPerMs) {stall_.SetClock(ticksPerMs);}
private:
    static efi_status EFIAPI PumpThunk(hakefi::MenuVoice* api,uint32_t maximumFrames) {
        auto* self=Owner(api);
        if(!maximumFrames || maximumFrames>4096)return EFI_INVALID_PARAMETER;
        const auto status=self->EnsureRunning();if(status!=EFI_SUCCESS)return status;
        if(self->stall_.Resting())return EFI_SUCCESS;   // r409: speech stays queued; no synthesis, no device call
        self->deadlineError_=EFI_SUCCESS;
        self->deadlineCheckpoint_=0;
        // Independent timer: never consume the keyboard's WaitForKey signal.
        // Five millisecond target, sampled per1024 checkpoints after the known
        // working4096 minimum. Short labels take~60k-175k checkpoints, so tiny
        // minimum slices would delay speech for many seconds at a10ms UI tick.
        // Firmware rounding cannot prevent all progress. This is cooperative,
        // not a hard wall-clock bound. DSP retains its separate sample budget.
        // r389: while the first chunk of an utterance is prepared nothing of
        // it is audible: the frontend may run 25 ms per call then (5 ms
        // otherwise, so playing speech or the melody never starves).
        // r389: also while a voice is about to run out of PCM (both voices
        // share the frontend; on a busy host 5 ms per call was too little).
        const bool urgent=(self->voice_.Preparing() || self->voice_.Starving() || (self->messagesReady_ && self->intro_.Starving()))
            && (!self->melodiesSet_ || !self->background_);
        const auto armed=self->boot_->SetTimer(self->preparationDeadline_,TimerRelative,urgent?250000:50000);
        if(armed!=EFI_SUCCESS)return armed;
        if(self->device_.Vanished()) {   // r402
            if(!(self->reopenTick_%256))charontrace::Value("CHARON audio vanished",self->reopenTick_);
            if(++self->reopenTick_%32==0)self->EnsureRunning();
            if(self->device_.Vanished())return EFI_SUCCESS;
        }
        charon::LiveVoice::Progress progress;
        // r355 stutter fix. Callers (menu, HII, owned Shell) ask for 480
        // frames per 10 ms tick: exactly real time with no reserve. Firmware
        // timers on emulated or busy hosts tick late (Windows QEMU: ~15.6 ms),
        // so output and TVox both fell behind and the ring underran. Top the
        // device queue up on every call (its backpressure bounds the write
        // and therefore the interruption latency) and allow four DSP blocks,
        // about 46 ms of 22050 Hz speech, per call.
        const size_t sinkFrames=maximumFrames<2048?2048:maximumFrames;
        // r389: the reader's voice first (interactive), then the message voice
        // (it only feeds the second channel; the first voice writes the mix).
        const auto mixed=self->pcm_.Wrap(self->device_.Sink());   // r407: with a PCM client, its sound is added to every block
        const int result=self->voice_.Pump({32768,1024,0,PreparationDeadline,self},mixed,&progress);
        if(self->messagesReady_) {
            charon::LiveVoice::Progress other;
            if(self->intro_.Pump({32768,1024,0,PreparationDeadline,self},mixed,&other))self->intro_.CancelSpeech();
        }
        // r410: a pump that comes late (the main thread was held: Charon copying itself to memory while the host's
        // disk is busy, a slow firmware read) used to add its usual 43 ms of sound and leave: speech came out in
        // 40 ms scraps with silence between them for as long as the pumps stayed slow. The time since the last
        // pump is now the measure: for every 30 ms of it one more round of synthesis and one more top-up of the
        // device queue (12 at most, about half a second of sound, each round ended by the device's own
        // backpressure). No wait, no Stall: only more of the work a pump does anyway.
        unsigned catchUp=0;
        {const uint64_t now=Ticks(),perMs=self->stall_.TicksPerMs();
         if(perMs && self->lastPumpTicks_ && now>self->lastPumpTicks_) {const uint64_t gapMs=(now-self->lastPumpTicks_)/perMs;
            if(gapMs>30) {catchUp=gapMs/30>12?12:unsigned(gapMs/30);++self->latePumps_;
                if(self->latePumps_<=4 || !(self->latePumps_%64))charontrace::Value("CHARON pump late, ms",gapMs);}}
         self->lastPumpTicks_=now;}
        self->device_.LimitLead(!catchUp && self->messagesReady_ && !self->intro_.SpeechIdle()?4096:0);   // r389: 85 ms while a message speaks
        if(!result) {charon::LiveVoice::Progress out;self->voice_.Pump({0,0,sinkFrames},mixed,&out);progress.sinkFrames=out.sinkFrames;}
        for(unsigned round=0;round<catchUp && !result;++round) {
            charon::LiveVoice::Progress more;
            if(self->voice_.Pump({32768,1024,0,PreparationDeadline,self},mixed,&more))break;
            if(self->messagesReady_) {charon::LiveVoice::Progress other;if(self->intro_.Pump({32768,1024,0,PreparationDeadline,self},mixed,&other))self->intro_.CancelSpeech();}
            charon::LiveVoice::Progress out;self->voice_.Pump({0,0,sinkFrames},mixed,&out);
            if(!out.sinkFrames || self->device_.LastError()!=EFI_SUCCESS || self->deadlineError_!=EFI_SUCCESS)break;   // the device queue is full, or a fault: as a single pump
            progress.sinkFrames+=out.sinkFrames;self->catchUpFrames_+=out.sinkFrames;
        }
        self->pcm_.Service(self->device_.Sink());   // r407: no speech was written: the client's sound goes out alone
        self->stall_.Played(self->device_.Submitted());   // r409
#ifdef HAK_CHARON_STALLTEST
        self->StallTest();
#endif
        ++self->tracePump_;self->traceCheckpoints_+=progress.frontend;
        if(!self->traceGenerated_ && (self->tracePump_<=3 || !(self->tracePump_%128))) {
            charontrace::Value("CHARON prep pump",self->tracePump_);
            charontrace::Value("CHARON prep charged",progress.frontend);
            charontrace::Value("CHARON prep total",self->traceCheckpoints_);
        }
        if(progress.frontendReady)charontrace::Value("CHARON frontend frames",progress.readyFrames);
        if(progress.generated&&!self->traceGenerated_) {
            charontrace::Value("CHARON first PCM",progress.generated);self->traceGenerated_=true;
        }
        if(self->deadlineError_!=EFI_SUCCESS) {self->RememberPlaybackError(self->deadlineError_);return self->deadlineError_;}
        if(result || self->device_.LastError()!=EFI_SUCCESS) {
            charontrace::Value("CHARON pump synthesis",uint64_t(result));
            charontrace::Value("CHARON pump device",self->device_.LastError());
        }
        if(self->device_.Vanished())return EFI_SUCCESS;   // r402: reopened by a later pump
        if(self->device_.LastError()==EFI_TIMEOUT && (!result || result==charon::LiveVoice::SinkError)
            && self->Recover()==EFI_SUCCESS)return EFI_SUCCESS;
        const auto playback=self->device_.LastError()!=EFI_SUCCESS?self->device_.LastError():Convert(result);
        self->RememberPlaybackError(playback);return playback;
    }
    static efi_status EFIAPI StopThunk(hakefi::MenuVoice* api) {return Owner(api)->Stop();}
    static void EnabledThunk(void* context,bool enabled) {
        auto* self=static_cast<VoiceProvider*>(context);self->sounds_=enabled;self->voice_.SetUiSoundsEnabled(enabled);
    }
    static void CueThunk(void* context,hakefi::MenuCue cue) {
        auto* self=static_cast<VoiceProvider*>(context);
        if(!self->sounds_)return;
        if(self->EnsureRunning()!=EFI_SUCCESS) {self->pendingCue_=cue;charontrace::Value("CHARON cue deferred",unsigned(cue));return;}
        if(self->stall_.Resting())return;   // r409: a cue heard seconds late says nothing
        // r393 (user): the welcome sound is the add-on's PC-speaker beep (900 Hz, 200 ms).
        if(cue==hakefi::MenuCue::welcome) {charontrace::Value("CHARON welcome beep",900);self->Square(900,200);return;}
        const auto sound=self->assets_.Cue(cue);
        charontrace::Value("CHARON cue",(uint64_t(sound.frames)<<8)|unsigned(cue));
        // A new UI event supersedes the previous cue (as speech does); the
        // final Continue cue is still drained by Stop before handoff.
        if(sound.samples) {self->voice_.CancelCues();self->voice_.QueueCue(sound.samples,sound.frames);}
    }
    static bool VolumesThunk(void* context,unsigned speech,unsigned effects) {
        if(speech<1 || speech>10 || effects>10)return false;
        // Defaults (speech 16384, cue 8192, melody 4096) are the 100% points,
        // so every combination keeps the mixer's total gain <= 1.
        charon::Mixer::Gains gains;gains.speech=16384*speech/10;
        gains.cue=8192*effects/10;gains.melody=4096*effects/10;
        return static_cast<VoiceProvider*>(context)->voice_.SetGains(gains);
    }
    // NVDA-style progress beep: sine, 5 ms linear fade in/out against clicks.
    // Two rotating buffers: a new beep replaces the previous cue (as speech
    // does), and the buffer being cancelled is never the one being written.
    static void ToneThunk(void* context,unsigned hertz,unsigned milliseconds) {
        auto* self=static_cast<VoiceProvider*>(context);
        if(!self->sounds_ || hertz<40 || hertz>8000 || !milliseconds || self->EnsureRunning()!=EFI_SUCCESS || self->stall_.Resting())return;
        if(milliseconds>toneMaxMs)milliseconds=toneMaxMs;
        const size_t frames=size_t(48)*milliseconds;
        int16_t* buffer=self->tone_[self->toneSlot_^=1];
        const double w=2*3.14159265358979323846*hertz/48000.0;
        // cos(w) by series; w<=1.05 rad so eight terms are far below 1/32768.
        double c=1,term=1;for(int n=1;n<=8;++n) {term*=-w*w/((2*n-1)*(2*n));c+=term;}
        double s1=w,t=w;for(int n=1;n<=8;++n) {t*=-w*w/((2*n)*(2*n+1));s1+=t;}
        // sin(n*w) by the recurrence y[n]=2cos(w)y[n-1]-y[n-2], y[0]=0, y[1]=sin(w).
        double before=0,last=0;
        const size_t fade=240;
        for(size_t i=0;i<frames;++i) {
            const double y=i==0?0:i==1?s1:2*c*last-before;
            before=last;last=y;
            double gain=1;if(i<fade)gain=double(i)/fade;if(frames-i<fade)gain=double(frames-i)/fade;
            const int16_t v=int16_t(y*gain*14000);
            buffer[i*2]=v;buffer[i*2+1]=v;
        }
        self->voice_.CancelCues();self->voice_.QueueCue(buffer,frames);
    }
    // r385: PC-speaker style beep: square wave, 1 ms edges against clicks.
    // r400: the add-on's start sound: two soft rising tones (a fairly high
    // one, then a higher one), like an interface switching on.
    static constexpr unsigned chimeHertz=8000;   // Beep(chimeHertz, any) asks for it
    void Chime(bool falling=false) {
        static const unsigned rising[2][3]={{784,0,110},{1175,140,170}};   // hertz, start ms, length ms
        static const unsigned sinking[2][3]={{1175,0,110},{784,140,170}};  // the reverse: switching off
        const auto& tones=falling?sinking:rising;
        const size_t frames=size_t(48)*320;int16_t* buffer=tone_[toneSlot_^=1];
        for(size_t i=0;i<frames*2;++i)buffer[i]=0;
        for(const auto& tone:tones) {
            const size_t start=size_t(48)*tone[1],length=size_t(48)*tone[2];
            for(size_t i=0;i<length;++i) {
                const uint32_t phase=uint32_t((uint64_t(i)*tone[0]*65536/48000)&65535);
                const int64_t u=phase<32768?phase:phase-32768;
                int64_t v=7000*4*u*(32768-u)/(int64_t(32768)*32768);if(phase>=32768)v=-v;
                // quick attack, falling tail
                const int64_t envelope=i<96?int64_t(i)*1024/96:int64_t(length-i)*1024/int64_t(length-96);
                v=v*envelope/1024;
                buffer[(start+i)*2]=int16_t(v);buffer[(start+i)*2+1]=int16_t(v);
            }
        }
        voice_.CancelCues();voice_.QueueCue(buffer,frames);
    }
    void Square(unsigned hertz,unsigned milliseconds) {
        if(hertz==chimeHertz || hertz==chimeHertz-1) {Chime(hertz!=chimeHertz);return;}
        if(milliseconds>toneMaxMs)milliseconds=toneMaxMs;
        const size_t frames=size_t(48)*milliseconds,period=48000/hertz,fade=48;
        int16_t* buffer=tone_[toneSlot_^=1];
        for(size_t i=0;i<frames;++i) {
            int v=(i%period)<period/2?9000:-9000;
            if(i<fade)v=int(v*int(i)/int(fade));else if(frames-i<fade)v=int(v*int(frames-i)/int(fade));
            buffer[i*2]=int16_t(v);buffer[i*2+1]=int16_t(v);
        }
        voice_.CancelCues();voice_.QueueCue(buffer,frames);
    }
    static void BeepThunk(void* context,unsigned hertz,unsigned milliseconds) {
        auto* self=static_cast<VoiceProvider*>(context);
        if(!self->sounds_ || hertz<40 || hertz>8000 || !milliseconds)return;
        if(self->EnsureRunning()!=EFI_SUCCESS) {self->pendingBeepHz_=hertz;self->pendingBeepMs_=milliseconds;charontrace::Value("CHARON beep deferred",hertz);return;}
        if(self->stall_.Resting())return;   // r409
        charontrace::Value("CHARON beep",(uint64_t(hertz)<<16)|milliseconds);
        self->Square(hertz,milliseconds);
    }
    // r410 (helper vr, ag5): with no sound stream at all (no card found, or the card gone and stopped) the queue never
    // drains, so every bounded wait for the end of speech ran to its cap (the plan's restart sentence came 180 s after
    // the report). Nothing can be heard then: idle. A resting stream (CharonAudioStall.h) still counts as speaking.
    static bool SpeechIdleThunk(void* context) {
        auto* self=static_cast<VoiceProvider*>(context);
        return self->voice_.SpeechIdle() || (!self->device_.NeedsStop() && !self->stall_.Resting());
    }
    static bool MessageThunk(void* context,const char* language,const char* text) {
        auto* self=static_cast<VoiceProvider*>(context);
        if(!self->messagesReady_ || !language || !text || self->EnsureRunning()!=EFI_SUCCESS)return false;
        charon::LiveVoice::Language selected;
        if(language[0]=='p' && language[1]=='l' && !language[2])selected=charon::LiveVoice::Language::Polish;
        else if(language[0]=='e' && language[1]=='n' && !language[2])selected=charon::LiveVoice::Language::English;
        else return false;
        size_t bytes=0;while(bytes<=charon::LiveVoice::maximumTextBytes && text[bytes])++bytes;
        if(!bytes || bytes>charon::LiveVoice::maximumTextBytes)return false;
        charontrace::Text("CHARON message");charontrace::Text(text);
        // r396 (user 2026-11-18): a new message interrupts the one still being
        // read; the reader's own speech is not cut. The driver's queued sound
        // is dropped only when the reader is silent (both are mixed in it).
        if(!self->intro_.SpeechIdle()) {
            self->intro_.CancelSpeech();
            if(self->voice_.SpeechIdle() && self->device_.Discard())charontrace::Value("CHARON message discard",1);
            charontrace::Value("CHARON message interrupted",1);
        }
        if(self->otherMessage_==Other::Pending && self->NeedsOther(selected,text,bytes))self->PrepareOther(self->loaded_[self->activeSynth_].message,self->otherMessage_,self->intro_);
        return self->intro_.QueueText(selected,text,bytes,self->messageRate_,self->messagePitch_)==0;
    }
    static void MessageVoiceThunk(void* context,unsigned rate,unsigned pitch) {
        auto* self=static_cast<VoiceProvider*>(context);
        if(rate>=5 && rate<=20)self->messageRate_=rate/10.0;
        if(pitch>=5 && pitch<=20)self->messagePitch_=pitch/10.0;
    }
    static void SilenceThunk(void* context) {
        auto* self=static_cast<VoiceProvider*>(context);
        if(!self->ready_)return;
        const bool speaking=!self->voice_.SpeechIdle() || (self->messagesReady_ && !self->intro_.SpeechIdle());
        self->voice_.CancelSpeech();if(self->messagesReady_)self->intro_.CancelSpeech();
        if(speaking)self->device_.Discard();
        charontrace::Value("CHARON silence",speaking);
    }
    static bool MessageIdleThunk(void* context) {auto* self=static_cast<VoiceProvider*>(context);return !self->messagesReady_ || self->intro_.SpeechIdle();}
    static bool CuesIdleThunk(void* context) {return static_cast<VoiceProvider*>(context)->voice_.CuesIdle();}
    // ---- r393 synthesizer add-ons (CharonSynthHost.h) --------------------
    // The module's functions use EFIAPI; LiveVoice calls these plain thunks.
    static int ExtBegin(void* m,uint32_t v,const char* t,size_t n,double r,double p,double vol) {
        auto* x=static_cast<hakefi::CharonSynthModule*>(m);
        if(r<.5)r=.5;if(r>2)r=2;if(p<.5)p=.5;if(p>2)p=2;if(vol<0)vol=0;if(vol>1)vol=1;
        return x->Begin(x,v,t,n,r,p,vol);
    }
    static int ExtRender(void* m,int16_t* o,size_t n) {auto* x=static_cast<hakefi::CharonSynthModule*>(m);return x->Render(x,o,n);}
    static void ExtCancel(void* m) {auto* x=static_cast<hakefi::CharonSynthModule*>(m);x->Cancel(x);}
    // r397: ABI 3 persona (Emacspeak ACSS) of an add-on.
    static int ExtPersona(void* m,double range,double stress,double richness,double smoothness,uint32_t family) {
        auto* x=static_cast<hakefi::CharonSynthModule*>(m);
        const hakefi::CharonSynthPersona p{sizeof(hakefi::CharonSynthPersona),family,range,stress,richness,smoothness};
        return x->SetPersona(x,&p);
    }
    enum class Other:uint8_t {None,Pending,Ready,Failed};   // r395 other-language voice
    static uint32_t VoiceRate(const hakefi::CharonSynthModule* m,uint32_t voice) {const uint32_t rate=m->voices[voice].sampleRate;return rate>=8000 && rate<=48000?rate:22050;}
    static bool VoiceEnglish(const hakefi::CharonSynthModule* m,uint32_t voice) {return m->voices[voice].language[0]=='e' && m->voices[voice].language[1]=='n';}
    // other == voice: the chosen voice reads all text (only it is prepared).
    static charon::ExternalSynth External(hakefi::CharonSynthModule* m,uint32_t voice,uint32_t other) {
        charon::ExternalSynth e;e.module=m;e.begin=ExtBegin;e.render=ExtRender;e.cancel=ExtCancel;
        if(m->abi>=3 && m->SetPersona)e.persona=ExtPersona;
        const bool english=VoiceEnglish(m,voice);
        e.voicePolish=english?other:voice;e.voiceEnglish=english?voice:other;
        e.ratePolish=VoiceRate(m,e.voicePolish);e.rateEnglish=VoiceRate(m,e.voiceEnglish);
        e.voiceFallback=voice;e.rateFallback=VoiceRate(m,voice);
        return e;
    }
    static charon::ExternalSynth External(hakefi::CharonSynthModule* m,uint32_t voice) {return External(m,voice,voice);}
    void UseTvox() {voice_.SetExternal(nullptr);if(messagesReady_)intro_.SetExternal(nullptr);activeSynth_=SIZE_MAX;otherReader_=otherMessage_=Other::None;}
    bool Activate(size_t slot,uint32_t voice) {
        LoadedSynth& l=loaded_[slot];
        if(!l.reader || voice>=l.reader->voiceCount)return false;
        {const int prepared=l.reader->Prepare(l.reader,voice);if(prepared!=0) {charontrace::Value("CHARON synth prepare failed",uint64_t(uint32_t(prepared)));return false;}}
        if(l.message && l.message->Prepare(l.message,voice)!=0)l.message=nullptr;   // messages keep TVox then
        const auto e=External(l.reader,voice);
        if(voice_.SetExternal(&e)) {charontrace::Value("CHARON synth voice busy",1);return false;}
        if(messagesReady_) {if(l.message) {const auto m=External(l.message,voice);intro_.SetExternal(&m);} else intro_.SetExternal(nullptr);}
        activeSynth_=slot;activeVoice_=voice;
        // r395 (user's choice 2026-11-18): the other language's voice of this
        // add-on is prepared the first time text in that language is spoken.
        otherReader_=otherMessage_=Other::None;
        const bool english=VoiceEnglish(l.reader,voice);
        for(uint32_t i=0;i<l.reader->voiceCount;++i)if(VoiceEnglish(l.reader,i)!=english) {
            const char* g=l.reader->voices[i].language;
            if(!((g[0]=='p' && g[1]=='l') || (g[0]=='e' && g[1]=='n')))continue;
            otherVoice_=i;otherReader_=Other::Pending;otherMessage_=l.message?Other::Pending:Other::None;break;
        }
        return true;
    }
    // Does this text (its language and Charon's ESC p / ESC e switches) need
    // the language the chosen voice does not speak?
    bool NeedsOther(charon::LiveVoice::Language language,const char* text,size_t bytes) const {
        if(activeSynth_==SIZE_MAX)return false;
        const bool otherEnglish=!VoiceEnglish(loaded_[activeSynth_].reader,activeVoice_);
        if((language==charon::LiveVoice::Language::English)==otherEnglish)return true;
        for(size_t i=0;i+1<bytes;) {
            if(text[i]!=0x1b) {++i;continue;}
            const char c=text[i+1];
            if((c=='e' && otherEnglish) || (c=='p' && !otherEnglish))return true;
            i+=c=='f'?16:c=='v'?6:c=='i'?4:2;   // r410: ESC f + 14 payload characters
        }
        return false;
    }
    // File reads and pool allocations are allowed up to TPL_CALLBACK (UEFI 2.10, 7.1).
    bool PrepareTplAllowed() const {const efi_tpl t=boot_->RaiseTPL(TPL_HIGH_LEVEL);boot_->RestoreTPL(t);charontrace::Value("CHARON synth other tpl",t);return t<=TPL_CALLBACK;}
    // Prepares the other voice in module m (it may take seconds; never above
    // TPL_CALLBACK, else the chosen voice reads this text and it stays
    // pending). A module may release the chosen voice for memory: it is
    // prepared again, and a voice released later is covered by the fallback.
    void PrepareOther(hakefi::CharonSynthModule* m,Other& state,charon::LiveVoice& lv) {
        if(state!=Other::Pending || !m || !PrepareTplAllowed())return;
        charontrace::Value("CHARON synth other",otherVoice_);
        const int r=m->Prepare(m,otherVoice_);
        const int keep=m->Prepare(m,activeVoice_);
        charontrace::Value("CHARON synth other status",uint64_t(uint32_t(r))|uint64_t(uint32_t(keep))<<32);
        if(keep!=0) {state=Other::Failed;lv.SetExternal(nullptr);return;}   // the chosen voice is lost: TVox
        state=r==0?Other::Ready:Other::Failed;
        if(state==Other::Ready) {const auto e=External(m,activeVoice_,otherVoice_);lv.SetExternal(&e);}
    }
    static void Put(char16_t* r,size_t cap,size_t& at,const char16_t* t) {if(!r || !cap)return;while(*t && at+1<cap)r[at++]=*t++;r[at]=0;}
    // report: on success "<synth>, <voice>"; on failure '!', a reason digit
    // (synths::Failure, 7 = voice, 8 = volume, 9 = no manifest) and the manifest name.
    static bool SetSynthThunk(void* context,uint8_t key,uint8_t voiceIndex,char16_t* report,size_t cap) {
        auto* self=static_cast<VoiceProvider*>(context);size_t at=0;if(report && cap)report[0]=0;
        if(!key) {self->UseTvox();Put(report,cap,at,u"TVox");return true;}
        using hakefi::synths::Failure;
        efi_guid fsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;efi_simple_file_system_protocol* fs=nullptr;efi_file_protocol* root=nullptr;
        if(!self->volume_ || self->boot_->HandleProtocol(self->volume_,&fsGuid,(void**)&fs)!=EFI_SUCCESS || !fs || fs->OpenVolume(fs,&root)!=EFI_SUCCESS || !root) {
            self->UseTvox();Put(report,cap,at,u"!8");return false;}
        static hakefi::synths::Info list[hakefi::synths::maxSynths];
        const size_t n=hakefi::synths::ScanAll(self->boot_,self->volume_,root,list,hakefi::synths::maxSynths);
        const hakefi::synths::Info* info=hakefi::synths::Find(list,n,key);
        unsigned reason=9;bool ok=false;
        if(info) {
            size_t slot=SIZE_MAX;for(size_t i=0;i<self->loadedCount_;++i)if(self->loaded_[i].key==key)slot=i;
            if(slot==SIZE_MAX && self->loadedCount_<maxLoaded && !self->loading_) {
                // r402: starting a module goes through the reader's StartImage hook, which preloads
                // every add-on: nested in this load it loaded them all again and the table overflowed
                // (eSpeak NG vanished from the list, speech stopped, Charon crashed).
                self->loading_=true;
                hakefi::CharonSynthModule* reader=nullptr;
                const Failure f=hakefi::synths::LoadAny(self->boot_,self->image_,self->volume_,root,*info,&reader);
                reason=unsigned(f);
                if(f==Failure::none) {
                    // a second instance speaks the boot messages (a module speaks one text at a time)
                    hakefi::CharonSynthModule* message=nullptr;
                    if(self->messagesReady_ && hakefi::synths::LoadAny(self->boot_,self->image_,self->volume_,root,*info,&message)!=Failure::none)message=nullptr;
                    if(self->loadedCount_<maxLoaded) {slot=self->loadedCount_++;self->loaded_[slot].key=key;self->loaded_[slot].reader=reader;self->loaded_[slot].message=message;}
                    else reason=unsigned(Failure::load);
                }
                self->loading_=false;
            }
            charontrace::Value("CHARON synth slot",(uint64_t(self->loadedCount_)<<32)|(uint64_t(uint16_t(slot))<<16)|(uint64_t(self->voice_.Pumping())<<8)|uint64_t(self->activeSynth_&0xff));
            if(slot!=SIZE_MAX) {
                const bool listed=voiceIndex<info->voices;
                const uint32_t v=hakefi::synths::VoiceIndex(self->loaded_[slot].reader,listed?info->id[voiceIndex]:nullptr,listed?info->language[voiceIndex]:"pl");
                charontrace::Value("CHARON synth voice",(uint64_t(self->loaded_[slot].reader->voiceCount)<<32)|v);
                ok=v!=UINT32_MAX && self->Activate(slot,v);
                reason=ok?0:7;
                if(ok) {char16_t name[48];hakefi::synths::Wide(info->name,sizeof(info->name),name,48);Put(report,cap,at,name);Put(report,cap,at,u", ");Put(report,cap,at,self->loaded_[slot].reader->voices[v].name);}
            }
            charontrace::Value("CHARON synth",(uint64_t(key)<<16)|(uint64_t(voiceIndex)<<8)|reason);
        }
        root->Close(root);
        if(ok)return true;
        self->UseTvox();
        char16_t code[3]={u'!',char16_t(u'0'+(reason%10)),0};Put(report,cap,at,code);
        if(info)Put(report,cap,at,info->manifest);
        return false;
    }
    static bool SynthAtThunk(void* context,size_t index,uint8_t* key,char16_t* name,size_t cap) {
        auto* self=static_cast<VoiceProvider*>(context);
        static hakefi::synths::Info list[hakefi::synths::maxSynths];static size_t count=0;static bool scanned=false;
        if(!scanned) {
            efi_guid fsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;efi_simple_file_system_protocol* fs=nullptr;efi_file_protocol* root=nullptr;
            if(!self->volume_ || self->boot_->HandleProtocol(self->volume_,&fsGuid,(void**)&fs)!=EFI_SUCCESS || !fs || fs->OpenVolume(fs,&root)!=EFI_SUCCESS || !root)return false;
            count=hakefi::synths::ScanAll(self->boot_,self->volume_,root,list,hakefi::synths::maxSynths);root->Close(root);scanned=true;
        }
        if(index>=count)return false;
        if(key)*key=list[index].key;
        if(name && cap) {hakefi::synths::Wide(list[index].name,sizeof(list[index].name),name,cap);}
        return true;
    }
    static bool LoadedThunk(void* context,uint8_t key) {
        auto* self=static_cast<VoiceProvider*>(context);
        if(!key)return true;
        for(size_t i=0;i<self->loadedCount_;++i)if(self->loaded_[i].key==key)return true;
        return false;
    }
    // r400: the cue the sound card repeats by itself after ExitBootServices:
    // two soft rising blips in 1.2 s (48 kHz stereo), 50 times = one minute.
    static bool LingerThunk(void* context) {
        auto* self=static_cast<VoiceProvider*>(context);
        constexpr uint32_t frames=57600;static int16_t loop[frames*2];
        for(uint32_t i=0;i<frames*2;++i)loop[i]=0;
        auto blip=[&](uint32_t start,double twoCos,double sine,uint32_t length) {
            // y[n]=2cos(w)y[n-1]-y[n-2]: a sine without the math library.
            double y1=0,y2=-sine;
            for(uint32_t n=0;n<length && start+n<frames;++n) {
                const double y=twoCos*y1-y2;y2=y1;y1=y;
                const double attack=n<240?double(n)/240.0:1.0,decay=1.0-double(n)/double(length);
                const int16_t v=int16_t(y*2600.0*attack*decay*decay);
                loop[(start+n)*2]=v;loop[(start+n)*2+1]=v;
            }
        };
        blip(0,1.9925407529858825,0.08628636579792337,5760);        // 660 Hz, 120 ms
        blip(12000,1.986745531200793,0.11493715049286661,5760);     // 880 Hz a quarter second later
        const bool ok=self->device_.Linger(loop,frames,50);
        charontrace::Value("CHARON linger",ok?1:0);
        return ok;
    }
    static void PreloadThunk(void* context) {
        auto* self=static_cast<VoiceProvider*>(context);
        // r406: called when firmware starts a program, at TPL_APPLICATION: the moment at which the reader
        // that came as a firmware driver may take the sound card over from the firmware's own HD Audio
        // driver (never from its timer or a notification, see CharonAc97.h).
        if(self->driver_ && (!self->device_.NeedsStop() || self->device_.Vanished()) && AtApplicationLevel(self->boot_) && self->applicationConnects_<3) {
            ++self->applicationConnects_;
            if(self->device_.NeedsStop())self->device_.Stop();
            const size_t taken=DisconnectFirmwareHda(self->boot_,self->driver_);
            ConnectAudioControllers(self->boot_);
            // r407: VMware's firmware has its own codec driver too; it bound the codec that Charon's
            // controller driver published, so the stream had no codec driver of ours (Open = unsupported).
            charontrace::Value("CHARON audio codecs claimed",uint64_t(Device::ClaimCodecs(self->boot_,self->driver_)));
            const auto opened=self->device_.Open(self->boot_);
            charontrace::Value("CHARON audio taken at program start",uint64_t(taken));
            charontrace::Value("CHARON audio open at program start",uint64_t(opened));
            if(opened==EFI_SUCCESS)self->reconnects_=0;
        }
        if(self->preloaded_ || self->loading_)return;   // r402: never inside a load (see SetSynthThunk)
        self->preloaded_=true;
        const uint8_t before=self->activeSynth_==SIZE_MAX?0:self->loaded_[self->activeSynth_].key;const uint32_t beforeVoice=self->activeVoice_;
        uint8_t key=0;bool any=false;
        for(size_t i=0;i<hakefi::synths::maxSynths && SynthAtThunk(context,i,&key,nullptr,0);++i)
            if(!LoadedThunk(context,key)) {char16_t report[80];SetSynthThunk(context,key,0,report,80);any=true;}
        if(!any)return;
        // back to the voice in use
        if(!before)self->UseTvox();
        else for(size_t i=0;i<self->loadedCount_;++i)if(self->loaded_[i].key==before)self->Activate(i,beforeVoice);
    }
    static uint8_t ActiveKeyThunk(void* context) {
        auto* self=static_cast<VoiceProvider*>(context);
        return self->activeSynth_==SIZE_MAX?0:self->loaded_[self->activeSynth_].key;
    }
    static size_t SynthVoicesThunk(void* context,size_t* current) {
        auto* self=static_cast<VoiceProvider*>(context);
        const size_t slot=self->activeSynth_!=SIZE_MAX?self->activeSynth_:(self->loadedCount_?0:SIZE_MAX);
        if(current)*current=self->activeSynth_==SIZE_MAX?SIZE_MAX:self->activeVoice_;
        return slot==SIZE_MAX?0:self->loaded_[slot].reader->voiceCount;
    }
    static bool UseSynthVoiceThunk(void* context,size_t index,char16_t* name,size_t capacity) {
        auto* self=static_cast<VoiceProvider*>(context);size_t at=0;if(name && capacity)name[0]=0;
        if(index==SIZE_MAX || !self->loadedCount_) {self->UseTvox();Put(name,capacity,at,u"TVox");return true;}
        const size_t slot=self->activeSynth_!=SIZE_MAX?self->activeSynth_:0;
        if(!self->Activate(slot,uint32_t(index))) {self->UseTvox();Put(name,capacity,at,u"TVox");return false;}
        Put(name,capacity,at,self->loaded_[slot].reader->voices[index].name);return true;
    }
    static void PitchThunk(void* context,unsigned tenths) {
        if(tenths>=5 && tenths<=20)static_cast<VoiceProvider*>(context)->pitch_=tenths/10.0;
    }
    static void RateThunk(void* context,unsigned tenths) {
        if(tenths>=5 && tenths<=20)static_cast<VoiceProvider*>(context)->rate_=tenths/10.0;
    }
    static void MelodiesThunk(void* context,unsigned start,unsigned background) {
        auto* self=static_cast<VoiceProvider*>(context);
        if(start>4 || background>4 || self->EnsureRunning()!=EFI_SUCCESS)return;
        if(!self->melodiesSet_ || background!=self->background_) {
            const auto sound=self->assets_.Background(background);self->voice_.SetMelody(sound.samples,sound.frames);
        }
        if((!self->melodiesSet_ || start!=self->start_) && self->sounds_) {
            const auto sound=self->assets_.Start(start);
            if(sound.samples)self->voice_.QueueCue(sound.samples,sound.frames);
        }
        self->start_=start;self->background_=background;self->melodiesSet_=true;
    }
    efi_status LoadDriver(efi_handle self,efi_file_protocol* root,bool connect=true) {
        void* image=nullptr;size_t bytes=0;
        auto status=ReadAudioFile(root,&pool_,u"\\EFI\\HAK\\AudioDxe.efi",8*1024*1024,image,bytes);
        if(status!=EFI_SUCCESS)return status;
        status=boot_->LoadImage(false,self,nullptr,image,bytes,&driver_);
        charon_pool_free(&pool_,image);
        if(status!=EFI_SUCCESS) {if(driver_)boot_->UnloadImage(driver_);driver_=nullptr;return status;}
        {efi_guid lg=EFI_LOADED_IMAGE_PROTOCOL_GUID;efi_loaded_image_protocol* li=nullptr;   // r404: where the driver lies (crash addresses)
         if(boot_->HandleProtocol(driver_,&lg,(void**)&li)==EFI_SUCCESS && li)charontrace::Value("CHARON audio driver base",uint64_t(uintptr_t(li->ImageBase)));
         li=nullptr;if(boot_->HandleProtocol(self,&lg,(void**)&li)==EFI_SUCCESS && li)charontrace::Value("CHARON image base",uint64_t(uintptr_t(li->ImageBase)));}
        status=boot_->StartImage(driver_,nullptr,nullptr);
        charontrace::Value("CHARON audio driver started",uint64_t(status));
        if(status!=EFI_SUCCESS) {boot_->UnloadImage(driver_);driver_=nullptr;return status;}
        audioDriverImage=driver_;   // r406
        // A started driver owns its bindings and must stay resident. Never
        // bypass firmware authentication or unload it beneath connected devices.
        if(!connect)return EFI_SUCCESS;
        charontrace::Value("CHARON audio firmware hda disconnected",uint64_t(DisconnectFirmwareHda(boot_,driver_)));   // r404
        efi_handle* handles=nullptr;size_t count=0;
        status=boot_->LocateHandleBuffer(AllHandles,nullptr,nullptr,&count,&handles);
        if(status!=EFI_SUCCESS)return status;
        if(!handles || count>4096) {if(handles)boot_->FreePool(handles);return EFI_DEVICE_ERROR;}
        for(size_t i=0;i<count;++i)boot_->ConnectController(handles[i],nullptr,nullptr,true);
        charontrace::Value("CHARON audio connected",uint64_t(count));
        boot_->FreePool(handles);return EFI_SUCCESS;
    }
    Bridge bridge_{};hakefi::AudioEffects effects_{};
    // r393 synthesizer add-ons: the loaded module instances (reader, messages).
    efi_handle image_=nullptr,volume_=nullptr;
    bool preloaded_=false;   // r396e
    struct LoadedSynth {uint8_t key=0;hakefi::CharonSynthModule* reader=nullptr;hakefi::CharonSynthModule* message=nullptr;};
    static constexpr size_t maxLoaded=4;bool loading_=false;   // r402
    LoadedSynth loaded_[maxLoaded]={};size_t loadedCount_=0;size_t activeSynth_=SIZE_MAX;uint32_t activeVoice_=0;
    Other otherReader_=Other::None,otherMessage_=Other::None;uint32_t otherVoice_=0;
    efi_boot_services* boot_=nullptr;efi_handle driver_=nullptr;unsigned applicationConnects_=0,vanishedCalls_=0;
    efi_event preparationDeadline_=nullptr;efi_status deadlineError_=EFI_SUCCESS;
    size_t deadlineCheckpoint_=0;uint64_t tracePump_=0,traceCheckpoints_=0;
    CharonPool pool_{};charon::LiveVoice voice_;Device device_;Assets assets_;PcmMix pcm_;
    charon::LiveVoice intro_;bool messagesReady_=false;double messageRate_=1.4,messagePitch_=1;   // r389 boot messages
    efi_status assetsStatus_=EFI_NOT_READY;
    efi_status playbackError_=EFI_SUCCESS;unsigned reopenTick_=0,reconnects_=0;
    bool ready_=false,initialized_=false,sounds_=true,melodiesSet_=false;
    bool traceGenerated_=false;
    hakefi::MenuCue pendingCue_=hakefi::MenuCue::none;  // r385
    unsigned pendingBeepHz_=0,pendingBeepMs_=0;          // r385
    double rate_=1,pitch_=1;
    static constexpr unsigned toneMaxMs=500;  // r384: 500 ms countdown end tone
    int16_t tone_[2][48*toneMaxMs*2]={};unsigned toneSlot_=0;
    static constexpr unsigned maximumRecoveries=64;unsigned recoveries_=0;static constexpr unsigned maximumFaultRestarts=8;unsigned faultRestarts_=0;   // r406
    StallPolicy stall_;bool clockKnown_=false;   // r409
#ifdef HAK_CHARON_STALLTEST
    // Test builds only (-DHAK_CHARON_STALLTEST): for every utterance longer than 150 bytes the
    // stream is treated as stalled from 1.5 s to 9.5 s after it was asked for, on every pump,
    // so the real stream is stopped and restarted as after the driver's EFI_TIMEOUT.
    uint64_t stallTestStart_=0;
    void StallTest() {
        if(!stallTestStart_ || !device_.NeedsStop() || device_.Vanished() || device_.LastError()!=EFI_SUCCESS)return;
        if(!stall_.TicksPerMs())return;
        const uint64_t ms=(Ticks()-stallTestStart_)/stall_.TicksPerMs();
        if(ms>=1500 && ms<9500)device_.InjectTimeout();
    }
#endif
    char lastText_[charon::LiveVoice::maximumTextBytes+1]={};size_t lastTextBytes_=0;
    charon::LiveVoice::Language lastLanguage_=charon::LiveVoice::Language::Polish;
    unsigned start_=0,background_=0;
};
}
