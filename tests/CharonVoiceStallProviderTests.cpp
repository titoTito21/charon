// r409 host test of the voice provider's stall path (src/efi/CharonVoiceProvider.h with
// CharonAudioStall.h): the real VoiceProvider, LiveVoice and TVox against a simulated HDA stream
// that stops taking frames in the middle of an utterance (its write reports EFI_TIMEOUT, as
// AudioDxe's does) and later takes them again.
// The fixture's Stall returns at once, so the provider finds no usable clock and counts its rests in
// calls (256, 512, 1024, 2048): the test needs no real time.
// Build (host Clang, from the tree's root; -I: the EFI headers of a Charon build directory):
//   clang++ -std=c++17 -O1 -w -I../charon-win-r409stall/include -c tests/CharonVoiceStallProviderTests.cpp
//   + src/tts/runtime/CharonLiveVoice.cpp CharonPool.c, src/tts/tvox_native/tvox_freestanding.cpp
//     tvox_dsp_stream.c tvox_dsp_mit.c tvox_numeric.c, tvox_context_msvc.asm (ml64) ; link with /STACK:536870912
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
static bool stalled=false,faulted=false,oneShot=false;
static uint64_t sent=0,written=0;static unsigned begins=0,stops=0,setups=0,writeCalls=0;
static efi_status EFIAPI Begin(Stream*) {++begins;sent=0;faulted=false;return EFI_SUCCESS;}
static efi_status EFIAPI Write(Stream*,const int16_t*,size_t frames,size_t* taken) {
    ++writeCalls;*taken=0;
    if(faulted)return EFI_TIMEOUT;
    if(stalled || oneShot) {oneShot=false;faulted=true;return EFI_TIMEOUT;}   // the driver stopped the stream itself
    *taken=frames;sent+=frames;written+=frames;return EFI_SUCCESS;
}
static efi_status EFIAPI GetState(Stream*,State* state) {
    State s{};s.size=sizeof(State);s.submitted=s.consumed=sent;s.flags=faulted?5:3;s.error=faulted?EFI_TIMEOUT:EFI_SUCCESS;*state=s;return EFI_SUCCESS;
}
static efi_status EFIAPI Stop(Stream*) {++stops;return EFI_SUCCESS;}
static efi_status EFIAPI Outputs(hakaudioefi::Protocol*,hakaudioefi::Port** out,size_t* count) {*out=ports;*count=1;return EFI_SUCCESS;}
static efi_status EFIAPI Setup(hakaudioefi::Protocol*,uint64_t,int8_t,uint32_t,uint32_t,uint8_t,size_t) {++setups;return EFI_SUCCESS;}
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
    "Nothing on a disk changes until you type a confirmation. Escape goes back one step.";
int main() {
    boot.LocateHandleBuffer=Locate;boot.HandleProtocol=Protocol;boot.FreePool=Free;boot.AllocatePool=Allocate;boot.Stall=StallNow;
    boot.CreateEvent=CreateEventFake;boot.SetTimer=SetTimerFake;boot.CheckEvent=CheckEventFake;boot.CloseEvent=CloseEventFake;
    boot.RaiseTPL=RaiseFake;boot.RestoreTPL=RestoreFake;
    stream={1,streamSize1,Begin,Write,GetState,Stop,nullptr,nullptr};audio.revision=4;audio.outputs=Outputs;audio.setup=Setup;
    ports[0]={0,2,64,0,0,0};root.Open=OpenFile;
    static VoiceProvider provider;   // far too large for a stack
    CHECK(provider.Initialize(&selfToken,&boot,&root)==EFI_SUCCESS);
    auto* api=provider.Voice();CHECK(api!=nullptr);if(!api)return 1;
    CHECK(begins==1);

    // 1. An utterance plays; then the card stops taking frames for a long time.
    CHECK(api->SpeakUtf8(api,"en",longText)==EFI_SUCCESS);
    for(unsigned i=0;i<30000 && written<24000;++i)CHECK(api->Pump(api,480)==EFI_SUCCESS);
    CHECK(written>=24000);
    stalled=true;const unsigned beginsBefore=begins;const uint64_t writtenAtStall=written;unsigned errors=0;
    for(unsigned i=0;i<3000;++i)if(api->Pump(api,480)!=EFI_SUCCESS)++errors;
    printf("lasting stall, 3000 pumps: %u restarts of the stream, %u pump errors, %u write calls\n",begins-beginsBefore,errors,writeCalls);
    CHECK(errors==0);                       // before r409: an error from the 65th restart on, and the callers gave the voice up
    CHECK(begins-beginsBefore>=3 && begins-beginsBefore<=6);   // two restarts, then one try after each rest (256, 512, 1024 calls); before r409: 64
    CHECK(written==writtenAtStall);
    // 2. Speech asked for during the stall is accepted (before r409 it was refused for good after 64 restarts).
    CHECK(api->SpeakUtf8(api,"en","Which group? Windows, 1 of 7")==EFI_SUCCESS);
    for(unsigned i=0;i<200;++i)if(api->Pump(api,480)!=EFI_SUCCESS)++errors;
    CHECK(errors==0 && written==writtenAtStall);
    // 3. The card plays again: the stream is opened after the rest and the newest utterance is heard, without a new request.
    stalled=false;unsigned pumps=0;
    for(;pumps<6000 && written<writtenAtStall+24000;++pumps)if(api->Pump(api,480)!=EFI_SUCCESS)++errors;
    printf("card plays again: %llu frames after %u pumps\n",(unsigned long long)(written-writtenAtStall),pumps);
    CHECK(errors==0 && written>=writtenAtStall+24000);
    CHECK(pumps<=2048+1500);                // the longest rest counted in calls, plus the speech itself
    // 4. Stalls with sound in between are restarted at once every time (r400 behaviour), 70 times in one utterance:
    //    more than the old limit of 64, and the voice still works.
    CHECK(api->SpeakUtf8(api,"en",longText)==EFI_SUCCESS);
    unsigned restarts=0,idle=0;uint64_t mark=written,last=written;const unsigned beginsSparse=begins;
    for(unsigned i=0;i<2000000 && restarts<70;++i) {
        if(api->Pump(api,480)!=EFI_SUCCESS)++errors;
        if(written-mark>=20000) {oneShot=true;mark=written;++restarts;}
        if(written!=last) {last=written;idle=0;}
        else if(++idle>=400) {idle=0;CHECK(api->SpeakUtf8(api,"en",longText)==EFI_SUCCESS);}   // the text ended: say it again
    }
    for(unsigned i=0;i<50;++i)if(api->Pump(api,480)!=EFI_SUCCESS)++errors;
    printf("stalls with sound in between: %u stalls, %u restarts\n",restarts,begins-beginsSparse);
    CHECK(errors==0 && restarts==70 && begins-beginsSparse==70);
    // 5. Two stalls back to back, then sound: restarted both times, no rest.
    CHECK(api->SpeakUtf8(api,"en",longText)==EFI_SUCCESS);
    for(unsigned i=0;i<4000 && written<mark+20000;++i)if(api->Pump(api,480)!=EFI_SUCCESS)++errors;
    const unsigned beginsPair=begins;oneShot=true;
    for(unsigned i=0;i<40;++i)if(api->Pump(api,480)!=EFI_SUCCESS)++errors;
    oneShot=true;mark=written;
    for(unsigned i=0;i<4000 && written<mark+20000;++i)if(api->Pump(api,480)!=EFI_SUCCESS)++errors;
    CHECK(errors==0 && begins-beginsPair==2 && written>=mark+20000);
    // 6. Stop and Shutdown work during a rest.
    stalled=true;for(unsigned i=0;i<60;++i)if(api->Pump(api,480)!=EFI_SUCCESS)++errors;
    CHECK(errors==0);
    CHECK(api->Stop(api)==EFI_SUCCESS);
    stalled=false;CHECK(provider.Shutdown()==EFI_SUCCESS);
    printf("%d checks, %d failed\n",checks,failures);
    return failures?1:0;
}
