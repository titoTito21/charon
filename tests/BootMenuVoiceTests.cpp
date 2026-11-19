#include "../src/efi/BootMenuVoice.h"
#include "../src/efi/BootAutoContinue.h"
#include <cstdio>
#include <cstdlib>
using namespace hakefi;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
static unsigned waits=0,checks=0,queries=0,pumps=0,slices=0;
static bool key=false,expired=false;
static efi_status checkError=EFI_SUCCESS,waitError=EFI_SUCCESS;
static efi_event events[3]={(efi_event)1,(efi_event)2,(efi_event)3};
static efi_status EFIAPI Speak(MenuVoice*,const char*,const char*) {return EFI_SUCCESS;}
static efi_status EFIAPI Stop(MenuVoice*) {return EFI_SUCCESS;}
static efi_status EFIAPI Pump(MenuVoice*,uint32_t maximum) {CHECK(maximum==480);++pumps;if(slices)--slices;return EFI_SUCCESS;}
static bool EFIAPI Preparing(MenuVoice*) {++queries;return slices!=0;}
static efi_status EFIAPI Check(efi_event event) {
    ++checks;if(checkError!=EFI_SUCCESS)return checkError;
    if(event==events[0] && key) {key=false;return EFI_SUCCESS;}
    if(event==events[1] && expired) {expired=false;return EFI_SUCCESS;}
    return EFI_NOT_READY;
}
static efi_status EFIAPI Wait(size_t,efi_event*,size_t* out) {++waits;*out=2;return waitError;}
static MenuVoice* located=nullptr;
static efi_status EFIAPI Locate(efi_guid*,void*,void** out) {*out=located;return EFI_SUCCESS;}
int main() {
    efi_boot_services boot{};boot.CheckEvent=Check;boot.WaitForEvent=Wait;boot.LocateProtocol=Locate;
    MenuVoice legacy{1,sizeof(MenuVoice),Speak,Pump,Stop};
    MenuVoiceV2 current{{2,sizeof(MenuVoiceV2),Speak,Pump,Stop},Preparing};
    CHECK(sizeof(MenuVoice)==32 && sizeof(MenuVoiceV2)==40);
    located=&legacy;CHECK(FindMenuVoice(&boot)==&legacy && !PendingPreparation(&legacy));
    located=&current.base;CHECK(FindMenuVoice(&boot)==&current.base);
    current.base.size=sizeof(MenuVoice);CHECK(!FindMenuVoice(&boot) && !PendingPreparation(&current.base));
    current.base.size=sizeof(MenuVoiceV2);current.Preparing=nullptr;CHECK(!FindMenuVoice(&boot));
    current.Preparing=Preparing;current.base.version=3;CHECK(!FindMenuVoice(&boot));current.base.version=2;
    legacy.size=sizeof(MenuVoiceV2);CHECK(!ValidMenuVoice(&legacy));legacy.size=sizeof(MenuVoice);
    size_t selected=99;slices=31;
    CHECK(WaitMenuVoiceEvent(&boot,&legacy,events,3,2,selected,1)==EFI_SUCCESS && waits==1);
    waits=0;
    for(unsigned i=0;i<31;++i) {
        CHECK(WaitMenuVoiceEvent(&boot,&current.base,events,3,2,selected,1)==EFI_SUCCESS && selected==2);
        CHECK(current.base.Pump(&current.base,480)==EFI_SUCCESS);
    }
    CHECK(pumps==31 && waits==0 && checks==62); // Keyboard/deadline checked every slice.
    CHECK(WaitMenuVoiceEvent(&boot,&current.base,events,3,2,selected,1)==EFI_SUCCESS && waits==1);
    slices=5;key=true;expired=true;waits=0;
    CHECK(WaitMenuVoiceEvent(&boot,&current.base,events,3,2,selected,1)==EFI_SUCCESS && selected==0 && !key && expired && waits==0);
    CHECK(WaitMenuVoiceEvent(&boot,&current.base,events,3,2,selected,1)==EFI_SUCCESS && selected==1 && !expired && waits==0);
    checkError=EFI_DEVICE_ERROR;selected=99;
    CHECK(WaitMenuVoiceEvent(&boot,&current.base,events,3,2,selected,1)==EFI_DEVICE_ERROR && selected==99 && waits==0);
    checkError=EFI_SUCCESS;slices=0;waitError=EFI_ABORTED;
    CHECK(WaitMenuVoiceEvent(&boot,&current.base,events,3,2,selected,1)==EFI_ABORTED);
    struct Gate {
        efi_boot_services* boot;MenuVoice* voice;unsigned made=0,stops=0;bool cancel=false;
        efi_status error=EFI_SUCCESS;
        bool SpacePending() {if(key){key=false;return true;}return false;}
        bool Failed() const {return error!=EFI_SUCCESS;}
        bool KeysPending() const {return false;}
        AutoContinueEvent Wait() {
            size_t index=99;error=WaitMenuVoiceEvent(boot,voice,events,3,2,index,1);
            return Failed()?AutoContinueEvent::failed:index==0?AutoContinueEvent::key:
                index==1?AutoContinueEvent::deadline:AutoContinueEvent::audio;
        }
        bool Pump() {
            error=voice->Pump(voice,480);++made;
            if(made==3) {expired=true;if(cancel)key=true;}
            return !Failed();
        }
        bool Stop() {++stops;return true;}
    };
    waitError=EFI_SUCCESS;slices=30;key=false;expired=false;waits=0;
    Gate countdown{&boot,&current.base};
    CHECK(WaitAutoContinue(countdown)==AutoContinueResult::handoff);
    CHECK(countdown.made==3 && countdown.stops==1 && waits==0 && slices==27);
    slices=30;key=false;expired=false;Gate cancellation{&boot,&current.base};cancellation.cancel=true;
    CHECK(WaitAutoContinue(cancellation)==AutoContinueResult::menu);
    CHECK(cancellation.made==3 && cancellation.stops==0 && waits==0);
    std::puts("PASS v1/v2 ABI validation, 31 cooperative slices without idle waits, consumed-key dispatch, deadline priority and error propagation");
}
