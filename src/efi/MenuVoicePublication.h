#pragma once
#include "BootMenuVoice.h"
namespace hakefi {
// Application-owned publication. No destructor silently uninstalls callbacks:
// the provider and all its storage must outlive successful Stop + Uninstall.
class MenuVoicePublication {
public:
    bool Retained() const {return handle_!=nullptr || unknownHandle_;}
    efi_status Install(efi_boot_services* boot,MenuVoice* voice) {
        if(Retained())return EFI_ALREADY_STARTED;
        if(!boot || !ValidMenuVoice(voice) || !boot->InstallProtocolInterface ||
            !boot->UninstallProtocolInterface || !boot->LocateProtocol || !boot->HandleProtocol)return EFI_INVALID_PARAMETER;
        MenuVoice* existing=nullptr;
        auto status=boot->LocateProtocol(&menuVoiceGuid,nullptr,(void**)&existing);
        if(status==EFI_SUCCESS)return EFI_ALREADY_STARTED;
        if(status!=EFI_NOT_FOUND)return status;
        boot_=boot;voice_=voice;
        status=boot_->InstallProtocolInterface(&handle_,&menuVoiceGuid,EFI_NATIVE_INTERFACE,voice_);
        if(status==EFI_SUCCESS) {unknownHandle_=!handle_;return handle_?EFI_SUCCESS:EFI_DEVICE_ERROR;}
        if(handle_) {
            void* actual=nullptr;const auto found=boot_->HandleProtocol(handle_,&menuVoiceGuid,&actual);
            if(found==EFI_SUCCESS && actual==voice_)return status;
            // Unknown failure is retained, never evidence that callbacks are gone.
            if(found!=EFI_SUCCESS && found!=EFI_NOT_FOUND && found!=EFI_UNSUPPORTED)return status;
        }
        handle_=nullptr;voice_=nullptr;boot_=nullptr;return status;
    }
    efi_status StopAndUninstall() {
        if(!Retained())return EFI_SUCCESS;
        const auto stopped=voice_->Stop(voice_);if(stopped!=EFI_SUCCESS)return stopped;
        if(unknownHandle_)return EFI_DEVICE_ERROR;
        const auto status=boot_->UninstallProtocolInterface(handle_,&menuVoiceGuid,voice_);
        if(status!=EFI_SUCCESS)return status;
        handle_=nullptr;voice_=nullptr;boot_=nullptr;return EFI_SUCCESS;
    }
private:
    efi_boot_services* boot_=nullptr;MenuVoice* voice_=nullptr;efi_handle handle_=nullptr;bool unknownHandle_=false;
};
}
