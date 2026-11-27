// r410 host test: the voice provider when its pump comes late (a busy main thread: Charon copying
// itself to memory while the host's disk is slow). The real VoiceProvider, LiveVoice and TVox against a
// simulated sound card with a queue that plays at 48 kHz on a simulated clock.
// Until r410 one pump added at most 2048 frames (43 ms) to the card, however late it came: with pumps
// 100 or 200 ms apart the card ran empty between them and speech was heard in 40 ms scraps.
// Build as tests/CharonVoiceStallProviderTests.cpp (same objects); see work/r410-host-tests.sh.
#include "../src/efi/CharonVoiceProvider.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace charonaudioefi;
static int failures=0,checks=0;
#define CHECK(x) do {++checks;if(!(x)) {++failures;printf("FAIL line %d: %s\n",__LINE__,#x);}} while(false)
static efi_boot_services boot{};
static Stream stream{};static hakaudioefi::Protocol audio{};static hakaudioefi::Port ports[1];
static char streamHandle,selfToken,eventToken;
// the card: a queue of `capacity` frames that empties at 48 frames per millisecond of the test clock
static const uint64_t ticksPerMs=1000,capacity=32768;
static uint64_t sent=0,played=0,lastClock=0,emptyTicks=0,busyTicks=0;static bool speaking=false;
static void Advance() {
    const uint64_t now=VoiceProvider::TestClock();
    if(now>lastClock) {
        const uint64_t ticks=now-lastClock,could=ticks*48/ticksPerMs,queued=sent-played;
        if(queued>=could) {played+=could;if(speaking)busyTicks+=ticks;}
        else {played=sent;if(speaking) {const uint64_t fullTicks=queued*ticksPerMs/48;busyTicks+=fullTicks;emptyTicks+=ticks-fullTicks;}}
        lastClock=now;
    }
}
static efi_status EFIAPI Begin(Stream*) {Advance();sent=played=0;return EFI_SUCCESS;}
static efi_status EFIAPI Write(Stream*,const int16_t*,size_t frames,size_t* taken) {
    Advance();const uint64_t room=capacity-(sent-played);
    *taken=frames<room?frames:size_t(room);sent+=*taken;return EFI_SUCCESS;
}
static efi_status EFIAPI GetState(Stream*,State* state) {
    Advance();State s{};s.size=sizeof(State);s.submitted=sent;s.consumed=played;s.flags=3;s.error=EFI_SUCCESS;*state=s;return EFI_SUCCESS;
}
static efi_status EFIAPI Stop(Stream*) {return EFI_SUCCESS;}
static efi_status EFIAPI Outputs(hakaudioefi::Protocol*,hakaudioefi::Port** out,size_t* count) {*out=ports;*count=1;return EFI_SUCCESS;}
static efi_status EFIAPI Setup(hakaudioefi::Protocol*,uint64_t,int8_t,uint32_t,uint32_t,uint8_t,size_t) {return EFI_SUCCESS;}
static efi_status EFIAPI Locate(efi_locate_search_type type,efi_guid* guid,void*,size_t* count,efi_handle** out) {
    if(type!=ByProtocol || !guid || memcmp(guid,&streamGuid,sizeof(*guid)))return EFI_NOT_FOUND;
    *out=static_cast<efi_handle*>(malloc(sizeof(efi_handle)));(*out)[0]=&streamHandle;*count=1;return EFI_SUCCESS;
}
static efi_status EFIAPI Protocol(efi_handle handle,efi_guid* guid,void** out) {
    if(handle==&streamHandle && !memcmp(guid,&streamGuid,sizeof(*guid))) {*out=&stream;return EFI_SUCCESS;}
    if(handle==&streamHandle && !memcmp(guid,&hakaudioefi::guid,sizeof(*guid))) {*out=&audio;return EFI_SUCCESS;}
    return EFI_UNSUPPORTED;
}
static efi_status EFIAPI Free(void* value) {if(value!=ports)free(value);return EFI_SUCCESS;}
static efi_status EFIAPI Allocate(efi_memory_type,size_t bytes,void** out) {*out=malloc(bytes);return *out?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;}
static efi_status EFIAPI StallNow(size_t) {return EFI_SUCCESS;}
static unsigned deadlineChecks=0;
static efi_status EFIAPI CreateEventFake(uint32_t,efi_tpl,efi_event_notify,void*,efi_event* out) {*out=&eventToken;return EFI_SUCCESS;}
static efi_status EFIAPI SetTimerFake(efi_event,efi_timer_delay,uint64_t) {deadlineChecks=0;return EFI_SUCCESS;}
static efi_status EFIAPI CheckEventFake(efi_event) {return ++deadlineChecks>32?EFI_SUCCESS:EFI_NOT_READY;}
static efi_status EFIAPI CloseEventFake(efi_event) {return EFI_SUCCESS;}
static efi_tpl EFIAPI RaiseFake(efi_tpl) {return TPL_APPLICATION;}
static void EFIAPI RestoreFake(efi_tpl) {}
static efi_file_protocol root{};
static efi_status EFIAPI OpenFile(efi_file_protocol*,efi_file_protocol**,const char16_t*,uint64_t,uint64_t) {return EFI_NOT_FOUND;}
static const char* longText="Installing a system from the network. First choose a group, then a system. For every system you will hear whether it speaks. "
    "Nothing on a disk changes until you type a confirmation. Escape goes back one step. Installing a system from the network. First choose a group, then a system.";
struct Run {double emptyShare;uint64_t frames;unsigned late;};
// Speaks the long text and pumps every `periodMs` for `seconds` of simulated time, after a second of ordinary pumps.
static Run Drive(VoiceProvider& provider,hakefi::MenuVoice* api,unsigned periodMs,unsigned seconds) {
    CHECK(api->SpeakUtf8(api,"en",longText)==EFI_SUCCESS);
    unsigned errors=0;
    // ordinary pumps until sound flows (the frontend prepares the utterance first)
    for(unsigned i=0;i<4000 && sent<4800;++i) {VoiceProvider::TestClock()+=10*ticksPerMs;if(api->Pump(api,480)!=EFI_SUCCESS)++errors;}
    CHECK(sent>=4800);
    Advance();speaking=true;emptyTicks=busyTicks=0;const uint64_t before=sent;const unsigned lateBefore=provider.LatePumps();
    for(unsigned t=0;t<seconds*1000;t+=periodMs) {VoiceProvider::TestClock()+=uint64_t(periodMs)*ticksPerMs;if(api->Pump(api,480)!=EFI_SUCCESS)++errors;}
    Advance();speaking=false;
    CHECK(errors==0);
    Run r;r.emptyShare=double(emptyTicks)/double(emptyTicks+busyTicks);r.frames=sent-before;r.late=provider.LatePumps()-lateBefore;
    CHECK(api->Stop(api)==EFI_SUCCESS);
    for(unsigned i=0;i<50;++i) {VoiceProvider::TestClock()+=10*ticksPerMs;api->Pump(api,480);}
    return r;
}
int main() {
    boot.LocateHandleBuffer=Locate;boot.HandleProtocol=Protocol;boot.FreePool=Free;boot.AllocatePool=Allocate;boot.Stall=StallNow;
    boot.CreateEvent=CreateEventFake;boot.SetTimer=SetTimerFake;boot.CheckEvent=CheckEventFake;boot.CloseEvent=CloseEventFake;
    boot.RaiseTPL=RaiseFake;boot.RestoreTPL=RestoreFake;
    stream={1,streamSize1,Begin,Write,GetState,Stop,nullptr,nullptr};audio.revision=4;audio.outputs=Outputs;audio.setup=Setup;
    ports[0]={0,2,64,0,0,0};root.Open=OpenFile;
    VoiceProvider::TestClock()=1000000;lastClock=VoiceProvider::TestClock();
    static VoiceProvider provider;   // far too large for a stack
    CHECK(provider.Initialize(&selfToken,&boot,&root)==EFI_SUCCESS);
    provider.TestSetClock(ticksPerMs);
    auto* api=provider.Voice();CHECK(api!=nullptr);if(!api)return 1;
    // 1. ordinary pumps, every 10 ms: the card never runs empty, no pump counts as late
    const Run normal=Drive(provider,api,10,3);
    printf("pump every  10 ms: card empty %5.1f%% of the time, %llu frames in 3 s, late pumps %u\n",normal.emptyShare*100,(unsigned long long)normal.frames,normal.late);
    CHECK(normal.emptyShare<0.02 && normal.late==0);
    // 2. a pump every 100 ms (the menu's own timer while the main thread is held between its events)
    const Run slow=Drive(provider,api,100,3);
    printf("pump every 100 ms: card empty %5.1f%% of the time, %llu frames in 3 s, late pumps %u\n",slow.emptyShare*100,(unsigned long long)slow.frames,slow.late);
    CHECK(slow.late>=25);
    CHECK(slow.emptyShare<0.10);            // before r410: 2048 frames per 100 ms = 57% of the time empty
    CHECK(slow.frames>=3*48000*85/100);
    // 3. a pump every 200 ms
    const Run slower=Drive(provider,api,200,3);
    printf("pump every 200 ms: card empty %5.1f%% of the time, %llu frames in 3 s, late pumps %u\n",slower.emptyShare*100,(unsigned long long)slower.frames,slower.late);
    CHECK(slower.emptyShare<0.15);          // before r410: 79% of the time empty
    CHECK(slower.frames>=3*48000*80/100);
    // 4. a pump every 400 ms: more than the catch-up's bound of twelve rounds can cover; still most of the time sound
    const Run slowest=Drive(provider,api,400,4);
    printf("pump every 400 ms: card empty %5.1f%% of the time, %llu frames in 4 s, late pumps %u\n",slowest.emptyShare*100,(unsigned long long)slowest.frames,slowest.late);
    CHECK(slowest.emptyShare<0.60);         // before r410: 89%
    // 5. back to ordinary pumps: nothing is left over (no backlog played late, no error)
    const Run again=Drive(provider,api,10,2);
    printf("pump every  10 ms again: card empty %5.1f%% of the time, late pumps %u\n",again.emptyShare*100,again.late);
    CHECK(again.emptyShare<0.02 && again.late==0);
    CHECK(provider.CatchUpFrames()>0);
    CHECK(provider.Shutdown()==EFI_SUCCESS);
    printf("%d checks, %d failed\n",checks,failures);
    return failures?1:0;
}
