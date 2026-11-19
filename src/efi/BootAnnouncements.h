// Announcement-only pre-OS lifecycle. Never consumes keyboard input.
#pragma once
#include <efi/system-table.h>
#include "BootSpeech.h"
namespace hakaudioefi {
template<class Player>
efi_status Announce(efi_boot_services* boot,Player& speech,Completion& completion,
    const char* language,unsigned timeoutTicks=6000) {
    const bool polish=language && language[0]=='p' && language[1]=='l' && !language[2];
    const bool english=language && language[0]=='e' && language[1]=='n' && !language[2];
    efi_status status=EFI_SUCCESS;
    efi_event timer=nullptr;
    if(!boot || (!polish && !english) || !timeoutTicks)status=EFI_INVALID_PARAMETER;
    else status=boot->CreateEvent(EVT_TIMER,TPL_APPLICATION,nullptr,nullptr,&timer);
    if(status==EFI_SUCCESS)status=boot->SetTimer(timer,TimerPeriodic,100000);
    const char* phrases[]={polish ? "Firmware załadowany." : "Firmware initialized.",
        polish ? "Uruchamianie menedżera rozruchu." : "Starting boot manager."};
    unsigned remaining=timeoutTicks;
    for(unsigned phrase=0;status==EFI_SUCCESS && phrase<2;++phrase) {
        status=speech.SayText(language,phrases[phrase]);
        while(status==EFI_SUCCESS && !completion.done) {
            if(!remaining) {status=EFI_TIMEOUT;break;}
            --remaining;
            size_t index=0;
            status=boot->WaitForEvent(1,&timer,&index);
        }
    }
    if(timer) {
        const auto closed=boot->CloseEvent(timer);
        if(status==EFI_SUCCESS)status=closed;
    }
    // Stop failures take precedence: caller must not unload callback/sample
    // storage or transfer control until a later Handoff succeeds.
    const auto stopped=speech.Handoff();
    return stopped==EFI_SUCCESS ? status : stopped;
}
}
