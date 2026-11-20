#pragma once
#include <efi/system-table.h>
// Live synthesis interface; cached utterances are not an implementation.
// The provider owns synthesis/mixing. SpeakUtf8 only queues text and cancels
// previous speech; it must not block until an utterance completes. Pump renders
// bounded work so the menu can service interruption between synthesis blocks.
// Dynamic boot descriptions must be synthesized as supplied.
// SpeakUtf8 must copy its NUL-terminated UTF-8 text and two-letter pl/en language
// before returning. Pump's budget bounds frontend work as well as PCM work;
// normalization/G2P for a whole utterance must not block the keyboard loop.
// A provider converts its native rate (TVox: 22050 Hz mono PCM16) to its output
// stream internally. It must not play Haiku's hk_startup.wav in this preOS stage.
namespace hakefi {
static efi_guid menuVoiceGuid={0x99e5faac,0x6c86,0x45db,{0xb0,0x9d,0x3c,0xba,0x80,0x4d,0x0e,0x51}};
struct MenuVoice {
    uint32_t version,size;
    efi_status (EFIAPI *SpeakUtf8)(MenuVoice*,const char* language,const char* text);
    efi_status (EFIAPI *Pump)(MenuVoice*,uint32_t maximumFrames);
    // Stop all provider audio before OS handoff. Failed Stop forbids transfer.
    efi_status (EFIAPI *Stop)(MenuVoice*);
};
// Additive work hint. The v1 prefix and its calling conventions never change.
// Preparing is a nonblocking, side-effect-free query, true only while Pump
// has cooperative frontend work available. It does not mean device drain.
struct MenuVoiceV2 {
    MenuVoice base;
    bool (EFIAPI *Preparing)(MenuVoice*);
};
static_assert(sizeof(void*)!=8 || sizeof(MenuVoice)==32,"MenuVoice v1 ABI");
// r401: set while a synthesizer is being changed at application level (a module is loaded or released):
// nothing may feed the voice from a timer meanwhile - a pump in the middle of the change crashed Charon.
inline volatile int& VoiceSwitching() {static volatile int switching=0;return switching;}
inline bool ValidMenuVoice(MenuVoice* voice) {
    return voice && voice->SpeakUtf8 && voice->Pump && voice->Stop
        && ((voice->version==1 && voice->size==sizeof(MenuVoice))
            || (voice->version==2 && voice->size>=sizeof(MenuVoiceV2)
                && reinterpret_cast<MenuVoiceV2*>(voice)->Preparing));
}
inline bool PendingPreparation(MenuVoice* voice) {
    return ValidMenuVoice(voice) && voice->version==2
        && reinterpret_cast<MenuVoiceV2*>(voice)->Preparing(voice);
}
// events[0] is keyboard. CheckEvent consumes a signalled event: dispatch that
// key directly, never call WaitForEvent again and lose the notification.
// The optional deadline remains higher priority than another preparation slice.
inline efi_status WaitMenuVoiceEvent(efi_boot_services* boot,MenuVoice* voice,
    efi_event* events,size_t count,size_t workIndex,size_t& selected,size_t deadlineIndex=SIZE_MAX) {
    if(PendingPreparation(voice)) {
        auto status=boot->CheckEvent(events[0]);
        if(status==EFI_SUCCESS) {selected=0;return EFI_SUCCESS;}
        if(status!=EFI_NOT_READY)return status;
        if(deadlineIndex<count) {
            status=boot->CheckEvent(events[deadlineIndex]);
            if(status==EFI_SUCCESS) {selected=deadlineIndex;return EFI_SUCCESS;}
            if(status!=EFI_NOT_READY)return status;
        }
        selected=workIndex;return EFI_SUCCESS;
    }
    return boot->WaitForEvent(count,events,&selected);
}
inline MenuVoice* FindMenuVoice(efi_boot_services* boot) {
    MenuVoice* voice=nullptr;
    if(!boot || boot->LocateProtocol(&menuVoiceGuid,nullptr,(void**)&voice)!=EFI_SUCCESS
        || !ValidMenuVoice(voice))return nullptr;
    return voice;
}
}
