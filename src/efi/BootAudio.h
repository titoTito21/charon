// Optional pre-OS announcements from this HAK ESP. No firmware trust changes.
#pragma once
#include <efi/protocol/simple-file-system.h>
#include "BootAnnouncements.h"
namespace hakaudioefi {
static uint8_t bootPcm[4*1024*1024];
static Completion bootCompletion;
inline efi_status ReadLanguage(efi_file_protocol* root,char (&language)[3]) {
    efi_file_protocol* file=nullptr;
    auto status=root->Open(root,&file,u"\\EFI\\HAK\\audio-language",EFI_FILE_MODE_READ,0);
    if(status!=EFI_SUCCESS)return status;
    size_t size=sizeof(language);
    status=file->Read(file,&size,language);
    const auto closed=file->Close(file);
    if(status!=EFI_SUCCESS)return status;
    if(closed!=EFI_SUCCESS)return closed;
    if(size!=2 || !((language[0]=='p' && language[1]=='l') || (language[0]=='e' && language[1]=='n')))
        return EFI_INVALID_PARAMETER;
    language[2]=0;return EFI_SUCCESS;
}
inline efi_status InitializeAudio(efi_handle self,efi_boot_services* boot,
    efi_file_protocol* root,Protocol*& audio,uint64_t& mask) {
    // Prefer an already initialized firmware protocol. Otherwise let LoadImage
    // enforce firmware image authentication for the driver on the HAK ESP.
    auto status=boot->LocateProtocol(&guid,nullptr,(void**)&audio);
    if(status!=EFI_SUCCESS) {
        efi_file_protocol* file=nullptr;
        status=root->Open(root,&file,u"\\EFI\\HAK\\AudioDxe.efi",EFI_FILE_MODE_READ,0);
        if(status!=EFI_SUCCESS)return status;
        size_t size=sizeof(bootPcm);
        status=file->Read(file,&size,bootPcm);
        const auto closed=file->Close(file);
        if(status!=EFI_SUCCESS)return status;
        if(closed!=EFI_SUCCESS)return closed;
        if(!size || size>=sizeof(bootPcm))return EFI_BAD_BUFFER_SIZE;
        efi_handle driver=nullptr;
        status=boot->LoadImage(false,self,nullptr,bootPcm,size,&driver);
        if(status!=EFI_SUCCESS) {if(driver)boot->UnloadImage(driver);return status;}
        status=boot->StartImage(driver,nullptr,nullptr);
        if(status!=EFI_SUCCESS) {boot->UnloadImage(driver);return status;}
        size_t count=0;efi_handle* handles=nullptr;
        if(boot->LocateHandleBuffer(AllHandles,nullptr,nullptr,&count,&handles)==EFI_SUCCESS) {
            for(size_t i=0;i<count;++i)boot->ConnectController(handles[i],nullptr,nullptr,true);
            boot->FreePool(handles);
        }
        status=boot->LocateProtocol(&guid,nullptr,(void**)&audio);
        if(status!=EFI_SUCCESS)return status;
    }
    if(!audio || audio->revision!=4 || !audio->outputs || !audio->setup || !audio->playAsync || !audio->stop)
        return EFI_UNSUPPORTED;
    Port* ports=nullptr;size_t count=0;mask=0;
    status=audio->outputs(audio,&ports,&count);
    if(status!=EFI_SUCCESS)return status;
    if(count && !ports)return EFI_DEVICE_ERROR;
    for(size_t i=0;i<count && i<64;++i)
        if(ports[i].type==0 && (ports[i].bits&2) && (ports[i].frequencies&96)==96)mask|=uint64_t(1)<<i;
    if(ports)boot->FreePool(ports);
    if(!mask)return EFI_UNSUPPORTED;
    // AudioDxe requires an initialized stream before the first StopPlayback.
    return audio->setup(audio,mask,-12,64,2,2,0);
}
inline efi_status BootAudio(efi_handle self,efi_system_table* table,efi_file_protocol* root) {
    char language[3]={};
    auto status=ReadLanguage(root,language);
    if(status!=EFI_SUCCESS)return status;
    Protocol* audio=nullptr;uint64_t mask=0;
    status=InitializeAudio(self,table->BootServices,root,audio,mask);
    if(status!=EFI_SUCCESS)return status;
    Speech speech(audio,root,bootPcm,sizeof(bootPcm),mask,&bootCompletion,4800,7200);
    status=Announce(table->BootServices,speech,bootCompletion,language);
    if(speech.Handoff()!=EFI_SUCCESS) {
        // Returning could unload this callback while the driver still owns it.
        // Retain the image and samples until the driver confirms a stopped stream.
        if(table->ConOut)table->ConOut->OutputString(table->ConOut,(char16_t*)u"HAK EFI: audio stop failed; waiting for safe handoff.\r\n");
        while(speech.Handoff()!=EFI_SUCCESS)table->BootServices->Stall(100000);
    }
    return status;
}
}
