#include <efi/protocol/simple-file-system.h>
#include "../src/efi/BootAnnouncements.h"
#include <cassert>
#include <cstring>
#include <cstdio>
static hakaudioefi::Completion completion;
static efi_status createStatus,setStatus,waitStatus,closeStatus;
static unsigned waits,closes,creates;
static bool finish;
static efi_status EFIAPI Create(uint32_t,efi_tpl,efi_event_notify,void*,efi_event* event) {
    ++creates;if(createStatus==EFI_SUCCESS)*event=(efi_event)1;return createStatus;
}
static efi_status EFIAPI Set(efi_event,efi_timer_delay,uint64_t time) {assert(time==100000);return setStatus;}
static efi_status EFIAPI Wait(size_t count,efi_event* events,size_t* index) {
    assert(count==1 && events[0]==(efi_event)1);*index=0;++waits;
    if(finish)completion.done=true;
    return waitStatus;
}
static efi_status EFIAPI Close(efi_event event) {assert(event==(efi_event)1);++closes;return closeStatus;}
struct Player {
    unsigned says=0,stops=0;efi_status play=EFI_SUCCESS,stop=EFI_SUCCESS;
    const char* texts[2]={};
    efi_status SayText(const char*,const char* text) {assert(says<2);texts[says++]=text;completion.done=false;return play;}
    efi_status Handoff() {++stops;return stop;}
};
static void Reset() {createStatus=setStatus=waitStatus=closeStatus=EFI_SUCCESS;waits=closes=creates=0;finish=true;completion.done=true;}
int main() {
    (void)hakaudioefi::guid;
    efi_boot_services boot={};boot.CreateEvent=Create;boot.SetTimer=Set;boot.WaitForEvent=Wait;boot.CloseEvent=Close;
    Reset();Player en;assert(hakaudioefi::Announce(&boot,en,completion,"en")==EFI_SUCCESS);
    assert(en.says==2 && en.stops==1 && waits==2 && closes==1);
    assert(!strcmp(en.texts[0],"Firmware initialized.") && !strcmp(en.texts[1],"Starting boot manager."));
    Reset();Player pl;assert(hakaudioefi::Announce(&boot,pl,completion,"pl")==EFI_SUCCESS);
    assert(!strcmp(pl.texts[0],"Firmware załadowany.") && !strcmp(pl.texts[1],"Uruchamianie menedżera rozruchu."));
    Reset();Player bad;assert(hakaudioefi::Announce(&boot,bad,completion,"de")==EFI_INVALID_PARAMETER);
    assert(!creates && !bad.says && bad.stops==1);
    Reset();Player timeout;finish=false;assert(hakaudioefi::Announce(&boot,timeout,completion,"en",3)==EFI_TIMEOUT);
    assert(waits==3 && timeout.says==1 && timeout.stops==1 && closes==1);
    Reset();Player failed;failed.play=EFI_NOT_FOUND;assert(hakaudioefi::Announce(&boot,failed,completion,"en")==EFI_NOT_FOUND);
    assert(!waits && failed.says==1 && failed.stops==1 && closes==1);
    Reset();Player stop;stop.stop=EFI_DEVICE_ERROR;waitStatus=EFI_ABORTED;
    assert(hakaudioefi::Announce(&boot,stop,completion,"en")==EFI_DEVICE_ERROR && stop.says==1 && closes==1);
    Reset();Player create;createStatus=EFI_OUT_OF_RESOURCES;
    assert(hakaudioefi::Announce(&boot,create,completion,"en")==EFI_OUT_OF_RESOURCES && !closes && !create.says && create.stops==1);
    Reset();Player set;setStatus=EFI_DEVICE_ERROR;
    assert(hakaudioefi::Announce(&boot,set,completion,"en")==EFI_DEVICE_ERROR && closes==1 && !set.says && set.stops==1);
    Reset();Player close;closeStatus=EFI_DEVICE_ERROR;
    assert(hakaudioefi::Announce(&boot,close,completion,"en")==EFI_DEVICE_ERROR && close.stops==1);
    puts("PASS: announcement language/order, no key events, bounded timeout and failure handoff");
}
