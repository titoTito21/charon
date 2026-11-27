#include "../src/efi/CharonAudioDevice.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(false)
using namespace charonaudioefi;
static efi_boot_services boot{};static Stream stream{};static hakaudioefi::Protocol audio{};
static efi_handle handles[2];static hakaudioefi::Port ports[1];static State observed{};
static size_t accepted=0,written=0;static int calls=0,setups=0,begins=0,stops=0,frees=0,otherLookups=0;
static bool sameHandle=true,badAccepted=false;static efi_status setupStatus=0,beginStatus=0,writeStatus=0,stateStatus=0,stopStatus=0;
static efi_status EFIAPI Locate(efi_locate_search_type type,efi_guid* guid,void*,size_t* count,efi_handle** out) {
    CHECK(type==ByProtocol);
    // r405: Open also looks for a USB sound card (the mirror). This machine has none: only the HDA stream exists.
    if(memcmp(guid,&streamGuid,sizeof(*guid))) {++otherLookups;return EFI_NOT_FOUND;}
    *count=1;*out=handles;return EFI_SUCCESS;
}
static efi_status EFIAPI Protocol(efi_handle handle,efi_guid* guid,void** out) {
    CHECK(handle==handles[0]);
    if(!memcmp(guid,&streamGuid,sizeof(*guid))) {*out=&stream;return EFI_SUCCESS;}
    CHECK(!memcmp(guid,&hakaudioefi::guid,sizeof(*guid)));
    if(!sameHandle) {return EFI_NOT_FOUND;}
    *out=&audio;return EFI_SUCCESS;
}
static efi_status EFIAPI Free(void* value) {CHECK(value==handles || value==ports);++frees;return EFI_SUCCESS;}
static efi_status EFIAPI Outputs(hakaudioefi::Protocol*,hakaudioefi::Port** out,size_t* count) {*out=ports;*count=1;return EFI_SUCCESS;}
static efi_status EFIAPI Setup(hakaudioefi::Protocol*,uint64_t mask,int8_t gain,uint32_t frequency,uint32_t bits,uint8_t channels,size_t delay) {
    ++setups;CHECK(mask==1 && gain==127 && frequency==64 && bits==2 && channels==2 && delay==0);return setupStatus;
}
static efi_status EFIAPI Begin(Stream*) {CHECK(setups==1);++begins;return beginStatus;}
static efi_status EFIAPI Write(Stream*,const int16_t*,size_t frames,size_t* taken) {
    ++calls;CHECK(frames<=128);*taken=badAccepted?frames+1:(accepted<frames?accepted:frames);written+=*taken;return writeStatus;
}
static efi_status EFIAPI GetState(Stream*,State* state) {CHECK(state->size==sizeof(State));*state=observed;return stateStatus;}
static efi_status EFIAPI Stop(Stream*) {++stops;return stopStatus;}
void Reset() {
    boot={};boot.LocateHandleBuffer=Locate;boot.HandleProtocol=Protocol;boot.FreePool=Free;
    stream={1,uint32_t(offsetof(Stream,discard)),Begin,Write,GetState,Stop,nullptr};/* r397: a revision 1 stream (its size ends before r389's discard) */audio={};audio.revision=4;audio.outputs=Outputs;audio.setup=Setup;
    handles[0]=&audio;ports[0]={0,2,64,0,0,0};observed={};observed.size=sizeof(State);observed.flags=3;
    accepted=written=0;calls=setups=begins=stops=frees=0;sameHandle=true;badAccepted=false;
    setupStatus=beginStatus=writeStatus=stateStatus=stopStatus=0;
}
int main() {
    int16_t pcm[512]={};Reset();Device device;CHECK(device.Open(&boot)==EFI_SUCCESS && begins==1 && frees==2);
    CHECK(otherLookups>=1 && !device.UsbMirror() && !device.UsbPrimary());   // r405: the USB card was looked for and is not there
    CHECK(device.Open(&boot)==EFI_ALREADY_STARTED && device.NeedsStop());CHECK(device.Drained());
    CHECK(device.Write(pcm,128)==0 && calls==1);accepted=3;CHECK(device.Write(pcm,128)==3);
    observed.submitted=3;observed.queued=3;observed.flags=1;CHECK(!device.Drained() && device.LastError()==EFI_SUCCESS);
    observed.queued=0;observed.hardware=3;CHECK(!device.Drained());
    observed.hardware=0;observed.consumed=3;CHECK(!device.Drained()); // Pipeline padding not done yet.
    observed.flags=3;CHECK(device.Drained());
    stopStatus=EFI_DEVICE_ERROR;CHECK(device.Stop()==EFI_DEVICE_ERROR && device.NeedsStop() && device.Handle()==handles[0]);
    CHECK(device.Write(pcm,1)==0);stopStatus=0;CHECK(device.Stop()==0 && !device.NeedsStop());CHECK(device.Stop()==0 && stops==2);
    Reset();sameHandle=false;Device mismatch;CHECK(mismatch.Open(&boot)==EFI_NOT_FOUND && setups==0 && begins==0);
    Reset();setupStatus=EFI_DEVICE_ERROR;Device setupFailure;CHECK(setupFailure.Open(&boot)==EFI_DEVICE_ERROR && begins==0 && setupFailure.NeedsStop());CHECK(setupFailure.Stop()==0);
    Reset();beginStatus=EFI_UNSUPPORTED;Device beginFailure;CHECK(beginFailure.Open(&boot)==EFI_UNSUPPORTED && beginFailure.NeedsStop());CHECK(beginFailure.Stop()==0);
    Reset();Device fault;CHECK(fault.Open(&boot)==0);accepted=2;writeStatus=EFI_DEVICE_ERROR;
    CHECK(fault.Write(pcm,5)==2 && fault.LastError()==EFI_DEVICE_ERROR);CHECK(fault.Write(pcm,5)==0 && calls==1);CHECK(!fault.Drained());CHECK(fault.Stop()==0);
    Reset();Device malformed;CHECK(malformed.Open(&boot)==0);badAccepted=true;CHECK(malformed.Write(pcm,1)==0 && malformed.LastError()==EFI_DEVICE_ERROR);CHECK(malformed.Stop()==0);
    for(unsigned mode=0;mode<5;++mode) {
        Reset();Device invalid;CHECK(invalid.Open(&boot)==0);
        if(mode==0)observed.flags=8;
        if(mode==1)observed.consumed=1;
        if(mode==2)observed.queued=1;
        if(mode==3)observed.size=0;
        if(mode==4)stateStatus=EFI_ACCESS_DENIED;
        CHECK(!invalid.Drained() && invalid.LastError()!=EFI_SUCCESS);CHECK(invalid.Stop()==0);
    }
    Reset();Device reportedFault;CHECK(reportedFault.Open(&boot)==0);
    observed.flags=4;observed.error=EFI_ACCESS_DENIED;
    CHECK(!reportedFault.Drained() && reportedFault.LastError()==EFI_ACCESS_DENIED);CHECK(reportedFault.Stop()==0);
    Reset();Device bounded;CHECK(bounded.Open(&boot)==0);accepted=512;CHECK(bounded.Write(pcm,256)==128);CHECK(bounded.Stop()==0);
    Reset();Device integrated;CHECK(integrated.Open(&boot)==0);charon::AudioEngine engine;const int16_t cue[]={100,200,300,400};
    CHECK(engine.QueueCue(cue,2));accepted=1;CHECK(engine.Pump(integrated.Sink(),128)==1 && !engine.SoftwareDrained());
    accepted=128;CHECK(engine.Pump(integrated.Sink(),128)==1 && engine.SoftwareDrained());
    observed.submitted=2;observed.consumed=2;CHECK(engine.Drained(integrated.Sink()));CHECK(integrated.Stop()==0);
    puts("PASS actual EFI audio adapter ABI/discovery/same-handle/setup, bounded partial sink, real drain state, faults and retained Stop ownership");
}
