#pragma once
#include "BootMenuModel.h"
#include "BootMenuVoice.h"
#include "BootAutoContinue.h"
#include "ContinueTarget.h"
#include "CharSettings.h"
#include "CharonKeyHelp.h"
#include "ConsoleMenu.h"
#include "CharonCues.h"
#include "BootMessages.h"
#include "CharonMessageMode.h"
#include "CharonMenuLabels.h"
#include "CharonTrace.h"
#include "HiiMenu.h"
#include "OwnedShellLauncher.h"
#include "CharonResident.h"
#include "CharonReaderControl.h"
#include "CharonManagerProbe.h"
#include "CharonFirmwareBrowser.h"
#include "CharonFileBrowser.h"
#include "CharonMedia.h"
#include "CharonSynthHost.h"

namespace hakefi {
inline const char16_t* SettingsSlot(unsigned slot) {return slot?u"\\EFI\\HAK\\charon-settings-b.bin":u"\\EFI\\HAK\\charon-settings-a.bin";}
inline efi_status ReadSettingsSlot(efi_file_protocol* root,unsigned slot,uint8_t* bytes,size_t& length) {
    efi_file_protocol* file=nullptr;auto status=root->Open(root,&file,SettingsSlot(slot),EFI_FILE_MODE_READ,0);
    if(status!=EFI_SUCCESS) {length=0;return status;}
    length=charSettingsBytes+1;status=file->Read(file,&length,bytes);const auto closed=file->Close(file);
    if(status!=EFI_SUCCESS || closed!=EFI_SUCCESS) {length=0;return EFI_DEVICE_ERROR;}
    CharSettings parsed;if(!DecodeSettings(bytes,length,parsed) || (parsed.sequence&1)!=(slot?0:1)) {length=0;return EFI_INVALID_PARAMETER;}
    return EFI_SUCCESS;
}
inline efi_status LoadCharSettings(efi_file_protocol* root,CharSettings& settings) {
    uint8_t a[charSettingsBytes+1],b[charSettingsBytes+1];size_t aSize=0,bSize=0;
    const auto first=ReadSettingsSlot(root,0,a,aSize),second=ReadSettingsSlot(root,1,b,bSize);
    if(first==EFI_NOT_FOUND && second==EFI_NOT_FOUND)return EFI_NOT_FOUND;
    return ChooseSettings(a,aSize,b,bSize,settings)?EFI_SUCCESS:EFI_INVALID_PARAMETER;
}
inline efi_status SaveCharSettings(efi_file_protocol* root,CharSettings& value) {
    if(value.sequence==UINT64_MAX)return EFI_INVALID_PARAMETER;
    CharSettings next=value;++next.sequence;uint8_t bytes[charSettingsBytes];
    if(!EncodeSettings(next,bytes,sizeof(bytes)))return EFI_INVALID_PARAMETER;
    const unsigned slot=(next.sequence&1)?0:1;efi_file_protocol* file=nullptr;
    // Replace only the inactive slot. The last complete record remains valid
    // until a newer record is flushed, closed and independently read back.
    auto status=root->Open(root,&file,SettingsSlot(slot),EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(status==EFI_SUCCESS) {status=file->Delete(file);if(status!=EFI_SUCCESS)return status;}
    else if(status!=EFI_NOT_FOUND)return status;
    status=root->Open(root,&file,SettingsSlot(slot),EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(status!=EFI_SUCCESS)return status;
    const bool written=Write(file,bytes,sizeof(bytes)) && file->Flush(file)==EFI_SUCCESS;
    const auto closed=file->Close(file);if(!written || closed!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    uint8_t verified[charSettingsBytes+1];size_t length=0;
    status=ReadSettingsSlot(root,slot,verified,length);
    if(status!=EFI_SUCCESS || length!=sizeof(bytes) || !Same(bytes,verified,length))return EFI_DEVICE_ERROR;
    value=next;return EFI_SUCCESS;
}
inline void InstallationLocale(efi_file_protocol* root,char (&locale)[64]) {
    locale[0]='e';locale[1]='n';locale[2]=0;efi_file_protocol* file=nullptr;
    const auto status=root->Open(root,&file,u"\\EFI\\HAK\\audio-language",EFI_FILE_MODE_READ,0);
    if(status==EFI_SUCCESS) {
        char value[64]={};size_t size=63;
        if(file->Read(file,&size,value)==EFI_SUCCESS && size>0 && size<64)memcpy(locale,value,size+1);
        file->Close(file);return;
    }
    // Installed locale takes precedence; consult firmware only if absent.
    if(status!=EFI_NOT_FOUND)return;
    char value[64]={};size_t size=63;
    if(systemTable->RuntimeServices->GetVariable((char16_t*)u"PlatformLang",&globalGuid,nullptr,&size,value)==EFI_SUCCESS
        && size>0 && size<64) {value[size]=0;memcpy(locale,value,size+1);}
}
// source: 0 firmware BootOrder, 1 UEFI medium found by Charon (r386),
// 2 BIOS-only disk, 3 BIOS-only CD (listed, never started).
struct MenuEntry {uint16_t id=0;uint8_t* data=nullptr;size_t size=0;uint8_t source=0;};
struct MenuInventory {
    MenuEntry entries[256];size_t count=0,orderSize=0;
    uint16_t order[256];
    efi_handle selfDevice=nullptr;bool pl=true;size_t firmwareCount=0;
    void Clear() {
        for(size_t i=0;i<count;++i)systemTable->BootServices->FreePool(entries[i].data);
        count=0;orderSize=0;
    }
    efi_status Refresh(uint16_t self) {
        Clear();orderSize=sizeof(order);firmwareCount=0;
        auto status=systemTable->RuntimeServices->GetVariable((char16_t*)u"BootOrder",&globalGuid,nullptr,&orderSize,order);
        if(status==EFI_NOT_FOUND) {Clear();return EFI_SUCCESS;}
        if(status!=EFI_SUCCESS) {Clear();return status;}
        if(orderSize%2 || orderSize>sizeof(order)) {Clear();return EFI_INVALID_PARAMETER;}
        for(size_t i=0;i<orderSize/2;++i) {
            for(size_t j=0;j<i;++j)if(order[j]==order[i]) {Clear();return EFI_INVALID_PARAMETER;}
            if(order[i]==self)continue;
            size_t size;BootOption parsed;
            status=ReadOption(order[i],size);
            if(status!=EFI_SUCCESS || !Parse(optionBytes,size,parsed))continue;
            uint8_t* copy=nullptr;
            status=systemTable->BootServices->AllocatePool(EfiLoaderData,size,(void**)&copy);
            if(status!=EFI_SUCCESS) {Clear();return status;}
            memcpy(copy,optionBytes,size);entries[count++]={order[i],copy,size};
        }
        firmwareCount=count;
        // r386: media without a firmware entry (sticks plugged in later, a
        // disc put in, BIOS-only systems). Charon's own option counts as
        // covered, so its disk is not offered again.
        {
            static const uint8_t* paths[257];static size_t sizes[257];size_t known=0;
            for(size_t i=0;i<count;++i) {BootOption parsed;if(Parse(entries[i].data,entries[i].size,parsed)) {paths[known]=entries[i].data+parsed.pathOffset;sizes[known++]=parsed.pathBytes;}}
            static uint8_t selfCopy[4096];size_t selfSize=0;BootOption selfParsed;
            if(ReadOption(self,selfSize)==EFI_SUCCESS && selfSize<=sizeof(selfCopy) && Parse(optionBytes,selfSize,selfParsed)) {
                memcpy(selfCopy,optionBytes,selfSize);paths[known]=selfCopy+selfParsed.pathOffset;sizes[known++]=selfParsed.pathBytes;}
            bool idFree=true;for(size_t i=0;i<orderSize/2;++i)if(order[i]==mediaEntryId)idFree=false;
#if defined(HAK_CHARON_NO_MEDIA) || defined(HAK_CHARON_NO_MEDIA_SCAN)
            idFree=false;
#endif
            if(idFree)FindMedia(systemTable->BootServices,paths,sizes,known,selfDevice,pl,[&](const FoundMedium& found) {
                if(count<256)entries[count++]={mediaEntryId,found.data,found.size,found.kind};
                else systemTable->BootServices->FreePool(found.data);
            });
        }
        // r397 (user): Memtest86+ shipped with Charon, always startable, no option.
        AddBuiltinTool(u"\\EFI\\HAK\\tools\\memtest.efi",pl?u"Memtest86+ (wbudowany, test pamięci)":u"Memtest86+ (built in, memory test)");
        // r368: which boot manager each entry starts (read from its disk).
        managerNames.count=0;managerNames.bcdCount=0;
        for(size_t i=0;i<firmwareCount;++i) {
            BootOption parsed;
            if(!Parse(entries[i].data,entries[i].size,parsed))continue;
            bool bcdFound=false;BcdSummary* slot=managerNames.bcdCount<ManagerNames::maxBcd?&managerNames.bcd[managerNames.bcdCount]:nullptr;
            const char16_t* name=ProbeOptionManager(systemTable->BootServices,entries[i].data+parsed.pathOffset,parsed.pathBytes,slot,&bcdFound);
            if(name && managerNames.count<256) {managerNames.id[managerNames.count]=entries[i].id;managerNames.name[managerNames.count]=name;++managerNames.count;}
            if(bcdFound && slot)managerNames.bcdId[managerNames.bcdCount++]=entries[i].id;
        }
        return EFI_SUCCESS;
    }
    bool Unchanged() const {
        uint16_t live[256];size_t size=sizeof(live);
        return systemTable->RuntimeServices->GetVariable((char16_t*)u"BootOrder",&globalGuid,nullptr,&size,live)==EFI_SUCCESS
            && size==orderSize && Same((const uint8_t*)live,(const uint8_t*)order,size);
    }
    // r397 (user): a tool shipped on Charon's own volume (Memtest86+) is always
    // offered in the device list, with no setting to turn on. source=4 marks it:
    // startable like a found medium, but never a Continue target or "remembered".
    void AddBuiltinTool(const char16_t* path,const char16_t* label) {
        if(count>=256 || !selfDevice)return;
        auto* boot=systemTable->BootServices;
        efi_simple_file_system_protocol* fs=nullptr;efi_file_protocol* vol=nullptr,*file=nullptr;
        if(boot->HandleProtocol(selfDevice,&media::fsGuid,(void**)&fs)!=EFI_SUCCESS || !fs
            || fs->OpenVolume(fs,&vol)!=EFI_SUCCESS || !vol)return;
        const bool present=vol->Open(vol,&file,(char16_t*)path,EFI_FILE_MODE_READ,0)==EFI_SUCCESS && file;
        if(file)file->Close(file);
        vol->Close(vol);
        if(!present)return;   // the tool is offered only when it is actually shipped
        uint8_t* own=nullptr;
        if(boot->HandleProtocol(selfDevice,&media::pathGuid,(void**)&own)!=EFI_SUCCESS || !own)return;
        const size_t device=media::PathLength(own);
        if(device==SIZE_MAX || !device || device>2048)return;
        size_t plen=0;while(path[plen])++plen;
        const size_t node=4+(plen+1)*2;
        static uint8_t buf[2048+4+520*2];
        if(device+node>sizeof(buf))return;
        for(size_t i=0;i<device;++i)buf[i]=own[i];
        uint8_t* f=buf+device;f[0]=4;f[1]=4;f[2]=uint8_t(node);f[3]=uint8_t(node>>8);
        for(size_t i=0;i<=plen;++i) {f[4+i*2]=uint8_t(path[i]);f[5+i*2]=uint8_t(path[i]>>8);}
        size_t size=0;uint8_t* data=media::BuildOption(boot,label,buf,device+node,size);
        if(data)entries[count++]={mediaEntryId,data,size,4};
    }
};
static uint8_t continueBytes[continueCapacity];
static const uint8_t continueMagic[8]={'H','A','K','C','O','N','T','1'};
// r391: \EFI\HAK\reader-only (the installer's "screen reader through shim"
// mode): Charon starts its Continue target at once and silently, with the
// resident reader; Space while it starts still opens the menu.
inline bool ReaderOnly(efi_file_protocol* root) {
    efi_file_protocol* file=nullptr;
    if(root->Open(root,&file,u"\\EFI\\HAK\\reader-only",EFI_FILE_MODE_READ,0)!=EFI_SUCCESS)return false;
    file->Close(file);return true;
}
inline bool ContinueWasConfigured(efi_file_protocol* root) {
    efi_file_protocol* file=nullptr;
    const auto status=root->Open(root,&file,u"\\EFI\\HAK\\continue-set",EFI_FILE_MODE_READ,0);
    if(status==EFI_SUCCESS)file->Close(file);
    // Unreadable marker is uncertainty, never permission to boot another OS.
    return status!=EFI_NOT_FOUND;
}
inline bool InBootOrder(uint16_t id) {
    uint16_t order[256];size_t size=sizeof(order);
    if(systemTable->RuntimeServices->GetVariable((char16_t*)u"BootOrder",&globalGuid,nullptr,&size,order)!=EFI_SUCCESS
        || size%2 || size>sizeof(order))return false;
    for(size_t i=0;i<size/2;++i)if(order[i]==id)return true;
    return false;
}
inline efi_status ValidateContinueTarget(uint16_t current,uint16_t id,const uint8_t* expected,size_t expectedSize) {
    if(id==current || !InBootOrder(id))return EFI_ACCESS_DENIED;
    size_t size;BootOption parsed;
    if(ReadOption(id,size)!=EFI_SUCCESS || size!=expectedSize
        || !Same(optionBytes,expected,size) || !Parse(optionBytes,size,parsed))return EFI_NOT_FOUND;
    return EFI_SUCCESS;
}
// UEFI 3.1.2 / 3.5.1.1: a boot option whose device path names only a device
// (removable media, a whole disk, firmware-created "UEFI <disk>" entries)
// boots \EFI\BOOT\BOOTX64.EFI from a file system on that device. Firmware's
// own boot manager expands such short paths; LoadImage alone cannot.
static efi_guid devicePathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
static uint8_t expandedPath[2048];
inline size_t PathBytesNoEnd(const uint8_t* path,size_t limit) {
    size_t at=0;
    while(at+4<=limit) {
        const size_t length=U16(path+at+2);
        if(path[at]==0x7f)return at;
        if(length<4 || at+length>limit)return SIZE_MAX;
        at+=length;
    }
    return SIZE_MAX;
}
inline bool PathNamesFile(const uint8_t* path,size_t bytes) {
    for(size_t at=0;at+4<=bytes && path[at]!=0x7f;at+=U16(path+at+2)) {
        if(U16(path+at+2)<4)return true; // malformed: never expand
        if(path[at]==4 && path[at+1]==4)return true;
    }
    return false;
}
inline efi_status LoadRemovableDefault(efi_handle self,const uint8_t* path,size_t pathBytes,efi_handle& image) {
    auto* boot=systemTable->BootServices;image=nullptr;
    const size_t prefix=PathBytesNoEnd(path,pathBytes);
    if(prefix==SIZE_MAX || !prefix)return EFI_NOT_FOUND;
    // Bind partition and file system drivers below the device first.
    auto* remaining=(efi_device_path_protocol*)path;efi_handle device=nullptr;
    if(boot->LocateDevicePath(&devicePathGuid,&remaining,&device)==EFI_SUCCESS && device)
        boot->ConnectController(device,nullptr,nullptr,true);
    efi_handle* handles=nullptr;size_t count=0;
    if(boot->LocateHandleBuffer(ByProtocol,&fsGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return EFI_NOT_FOUND;
    static const char16_t file[]=u"\\EFI\\BOOT\\BOOTX64.EFI";
    const size_t fileNode=4+sizeof(file);
    efi_status status=EFI_NOT_FOUND;
    for(size_t i=0;i<count && i<4096;++i) {
        uint8_t* own=nullptr;
        if(boot->HandleProtocol(handles[i],&devicePathGuid,(void**)&own)!=EFI_SUCCESS || !own)continue;
        const size_t length=PathBytesNoEnd(own,sizeof(expandedPath));
        if(length==SIZE_MAX || length<prefix || !Same(own,path,prefix) || length+fileNode+4>sizeof(expandedPath))continue;
        memcpy(expandedPath,own,length);
        uint8_t* node=expandedPath+length;node[0]=4;node[1]=4;node[2]=uint8_t(fileNode);node[3]=uint8_t(fileNode>>8);
        memcpy(node+4,file,sizeof(file));
        uint8_t* end=node+fileNode;end[0]=0x7f;end[1]=0xff;end[2]=4;end[3]=0;
        status=boot->LoadImage(true,self,(efi_device_path_protocol*)expandedPath,nullptr,0,&image);
        if(status==EFI_SUCCESS)break;
        if(image) {boot->UnloadImage(image);image=nullptr;}
    }
    boot->FreePool(handles);return status;
}
// Optional preboot/postboot narration around one verified start. AfterLoad
// sees the loaded image before any transfer decision; BeforeStart runs last
// (after the cancellation gate) and either installs the resident speech layer
// or silences audio. A failing BeforeStart prevents the transfer.
struct LaunchNarration {
    void* context=nullptr;
    void (*AfterLoad)(void*,efi_loaded_image_protocol*)=nullptr;
    efi_status (*BeforeStart)(void*)=nullptr;
};
inline bool launchWatchdog=true;   // r387: false for programs from the file browser
inline efi_status StartVerified(efi_handle self,uint16_t current,uint16_t id,const uint8_t* expected,size_t expectedSize,BootAttemptStage& stage,
    efi_status (*beforeTransfer)(void*)=nullptr,void* transferContext=nullptr,LaunchNarration* narration=nullptr) {
    stage=BootAttemptStage::validation;
    if(id==mediaEntryId) {
        // r386: a medium found by Charon: the option exists only in memory.
        if(!expected || expectedSize>sizeof(optionBytes))return EFI_NOT_FOUND;
        memcpy(optionBytes,expected,expectedSize);
    } else {
        auto validation=ValidateContinueTarget(current,id,expected,expectedSize);
        if(validation!=EFI_SUCCESS)return validation;
    }
    const auto size=expectedSize;BootOption parsed;
    if(!Parse(optionBytes,size,parsed))return EFI_NOT_FOUND;
    if(beforeTransfer) {const auto guard=beforeTransfer(transferContext);if(guard!=EFI_SUCCESS)return guard;}
    auto* boot=systemTable->BootServices;efi_handle image=nullptr;
    stage=BootAttemptStage::load;
    auto status=boot->LoadImage(true,self,(efi_device_path_protocol*)(optionBytes+parsed.pathOffset),nullptr,0,&image);
    if(status!=EFI_SUCCESS && image) {boot->UnloadImage(image);image=nullptr;}
    if(status==EFI_NOT_FOUND && !PathNamesFile(optionBytes+parsed.pathOffset,parsed.pathBytes))
        status=LoadRemovableDefault(self,optionBytes+parsed.pathOffset,parsed.pathBytes,image);
    if(status!=EFI_SUCCESS) {if(image)boot->UnloadImage(image);return status;}
    stage=BootAttemptStage::protocol;
    efi_loaded_image_protocol* loaded=nullptr;
    status=boot->HandleProtocol(image,&imageGuid,(void**)&loaded);
    if(status==EFI_SUCCESS) {
        loaded->LoadOptionsSize=(uint32_t)(size-parsed.optionsOffset);
        loaded->LoadOptions=loaded->LoadOptionsSize ? optionBytes+parsed.optionsOffset : nullptr;
        if(narration && narration->AfterLoad)narration->AfterLoad(narration->context,loaded);
        stage=BootAttemptStage::returned;
        status=beforeTransfer?beforeTransfer(transferContext):EFI_SUCCESS;
        if(status==EFI_SUCCESS && narration && narration->BeforeStart)status=narration->BeforeStart(narration->context);
        // r387: UEFI 2.11 section 3.1.2: a boot manager arms the 5-minute
        // watchdog before it starts a boot option, and disarms it when the
        // option returns. Loaders that run longer stop it themselves (GRUB,
        // Windows Boot Manager, shells) or end boot services. Programs Charon
        // starts from its file browser run as from a shell, without it.
        const bool watchdog=launchWatchdog && boot->SetWatchdogTimer;
        if(status==EFI_SUCCESS && watchdog)boot->SetWatchdogTimer(300,0,0,nullptr);
        if(status==EFI_SUCCESS)status=boot->StartImage(image,nullptr,nullptr);
        if(watchdog)boot->SetWatchdogTimer(0,0,0,nullptr);
    }
    boot->UnloadImage(image);return status;
}
inline efi_status ReadContinue(efi_file_protocol* root,uint16_t& id,size_t& size,const char16_t* name=u"\\EFI\\HAK\\continue.bin") {
    efi_file_protocol* file=nullptr;
    auto status=root->Open(root,&file,name,EFI_FILE_MODE_READ,0);
    if(status!=EFI_SUCCESS)return status;
    size_t length=sizeof(continueBytes);status=file->Read(file,&length,continueBytes);
    auto closed=file->Close(file);if(status!=EFI_SUCCESS)return status;if(closed!=EFI_SUCCESS)return closed;
    return ParseContinueTarget(continueBytes,length,id,size) ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}
// r396 (user): without a saved Continue target Charon continues with the
// first boot option of the firmware's own BootOrder that is neither Charon
// nor a firmware application (setup page, built-in shell) - what firmware
// would start without Charon. The record is built in memory, never written.
inline bool PathHasFirmwareFile(const uint8_t* path,size_t bytes) {
    for(size_t at=0;at+4<=bytes && path[at]!=0x7f;at+=U16(path+at+2)) {
        if(U16(path+at+2)<4)return true;
        if(path[at]==4 && path[at+1]==6)return true;   // PIWG firmware file (UiApp, Shell)
    }
    return false;
}
inline efi_status FallbackContinue(uint16_t self,uint16_t& id,size_t& size,const MenuInventory* media=nullptr) {
    uint16_t order[256];size_t bytes=sizeof(order);
    if(systemTable->RuntimeServices->GetVariable((char16_t*)u"BootOrder",&globalGuid,nullptr,&bytes,order)!=EFI_SUCCESS
        || bytes%2 || bytes>sizeof(order))return EFI_NOT_FOUND;
    for(size_t i=0;i<bytes/2;++i) {
        if(order[i]==self)continue;
        size_t s=0;BootOption parsed;
        if(ReadOption(order[i],s)!=EFI_SUCCESS || s+18>continueCapacity || !Parse(optionBytes,s,parsed))continue;
        if(PathHasFirmwareFile(optionBytes+parsed.pathOffset,parsed.pathBytes))continue;
        memcpy(continueBytes+14,optionBytes,s);
        lastImageWasCharon=false;ProbeOptionManager(systemTable->BootServices,continueBytes+14+parsed.pathOffset,parsed.pathBytes);
        if(lastImageWasCharon)continue;   // another Charon
        memcpy(continueBytes,continueMagic,8);continueBytes[8]=uint8_t(order[i]);continueBytes[9]=uint8_t(order[i]>>8);
        for(unsigned k=0;k<4;++k)continueBytes[10+k]=uint8_t(s>>(8*k));
        const uint8_t end[4]={'H','E','N','D'};memcpy(continueBytes+14+s,end,4);
        id=order[i];size=s;return EFI_SUCCESS;
    }
    // Then a UEFI medium Charon found without a firmware entry (a disk or a
    // stick firmware did not register): what firmware's removable-media
    // fallback would start.
    if(media)for(size_t i=media->firmwareCount;i<media->count;++i) {
        const MenuEntry& e=media->entries[i];BootOption parsed;
        if(e.source!=1 || e.size+18>continueCapacity || !Parse(e.data,e.size,parsed))continue;
        lastImageWasCharon=false;ProbeOptionManager(systemTable->BootServices,e.data+parsed.pathOffset,parsed.pathBytes);
        if(lastImageWasCharon)continue;
        memcpy(continueBytes,continueMagic,8);continueBytes[8]=uint8_t(e.id);continueBytes[9]=uint8_t(e.id>>8);
        for(unsigned k=0;k<4;++k)continueBytes[10+k]=uint8_t(e.size>>(8*k));
        memcpy(continueBytes+14,e.data,e.size);
        const uint8_t end[4]={'H','E','N','D'};memcpy(continueBytes+14+e.size,end,4);
        id=e.id;size=e.size;return EFI_SUCCESS;
    }
    // Last, a firmware program in BootOrder (OVMF's "EFI Internal Shell"):
    // firmware would start it next. Charon starts it itself so the screen
    // reader keeps reading it; returning to firmware ends Charon and its reader.
    for(size_t i=0;i<bytes/2;++i) {
        if(order[i]==self)continue;
        size_t s=0;BootOption parsed;
        if(ReadOption(order[i],s)!=EFI_SUCCESS || s+18>continueCapacity || !Parse(optionBytes,s,parsed))continue;
        if(!PathHasFirmwareFile(optionBytes+parsed.pathOffset,parsed.pathBytes))continue;
        memcpy(continueBytes+14,optionBytes,s);
        memcpy(continueBytes,continueMagic,8);continueBytes[8]=uint8_t(order[i]);continueBytes[9]=uint8_t(order[i]>>8);
        for(unsigned k=0;k<4;++k)continueBytes[10+k]=uint8_t(s>>(8*k));
        const uint8_t end[4]={'H','E','N','D'};memcpy(continueBytes+14+s,end,4);
        id=order[i];size=s;return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}
// r396d: an in-memory boot option for the firmware's setup application
// (edk2's UiApp, file GUID 462caa21-7614-4503-836e-8ab6f4662331, or a
// Boot#### described "UiApp"/"Enter Setup"), found among Boot0000-Boot00FF.
// Charon starts it like a found medium (never written to NVRAM).
inline efi_status FirmwareMenuOption(uint8_t* out,size_t capacity,size_t& size) {
#ifdef HAK_CHARON_NO_UIAPP
    (void)out;(void)capacity;(void)size;return EFI_NOT_FOUND;   // test only: pretend the firmware has no UiApp, to exercise the in-session HII fallback
#endif
    static const uint8_t uiApp[16]={0x21,0xaa,0x2c,0x46,0x14,0x76,0x03,0x45,0x83,0x6e,0x8a,0xb6,0xf4,0x66,0x23,0x31};
    for(unsigned id=0;id<0x100;++id) {
        size_t s=0;if(ReadOption(uint16_t(id),s)!=EFI_SUCCESS || s<8)continue;
        const uint8_t* d=optionBytes;const size_t pathBytes=U16(d+4);
        size_t at=6;while(at+1<s && (d[at] || d[at+1]))at+=2;at+=2;   // description
        if(at+pathBytes>s)continue;
        const uint8_t* path=d+at;bool match=false;
        for(size_t p=0;p+4<=pathBytes && path[p]!=0x7f;) {
            const size_t n=U16(path+p+2);if(n<4)break;
            if(path[p]==4 && path[p+1]==6 && n>=20) {bool same=true;for(size_t k=0;k<16 && same;++k)same=path[p+4+k]==uiApp[k];match=match||same;}
            p+=n;
        }
        if(!match) {
            static const char16_t* names[]={u"UiApp",u"Enter Setup",u"Setup"};
            for(const char16_t* name:names) {size_t k=0;while(name[k] && 6+2*k+1<s && d[6+2*k]==uint8_t(name[k]) && !d[6+2*k+1])++k;if(!name[k] && !d[6+2*k] && !d[6+2*k+1])match=true;}
        }
        if(!match)continue;
        static const char16_t desc[]=u"UiApp";const size_t descBytes=sizeof(desc);
        if(6+descBytes+pathBytes>capacity)return EFI_BUFFER_TOO_SMALL;
        out[0]=1;out[1]=out[2]=out[3]=0;out[4]=uint8_t(pathBytes);out[5]=uint8_t(pathBytes>>8);   // active, boot category
        memcpy(out+6,desc,descBytes);memcpy(out+6+descBytes,path,pathBytes);
        size=6+descBytes+pathBytes;return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}
inline efi_status WriteTargetFile(efi_file_protocol* root,const char16_t* name,size_t length) {
    efi_file_protocol* file=nullptr;
    auto status=root->Open(root,&file,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(status==EFI_SUCCESS) {status=file->Delete(file);if(status!=EFI_SUCCESS)return status;}
    else if(status!=EFI_NOT_FOUND)return status;
    status=root->Open(root,&file,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(status!=EFI_SUCCESS)return status;
    bool ok=Write(file,continueBytes,length) && file->Flush(file)==EFI_SUCCESS;
    auto closed=file->Close(file);return ok && closed==EFI_SUCCESS ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
inline size_t BuildTargetRecord(const MenuEntry& entry) {
    size_t fresh;BootOption parsed;
    if(!InBootOrder(entry.id) || ReadOption(entry.id,fresh)!=EFI_SUCCESS || fresh!=entry.size
        || !Same(entry.data,optionBytes,fresh) || !Parse(entry.data,fresh,parsed))return 0;
    memcpy(continueBytes,continueMagic,8);continueBytes[8]=uint8_t(entry.id);continueBytes[9]=uint8_t(entry.id>>8);
    for(unsigned i=0;i<4;++i)continueBytes[10+i]=uint8_t(fresh>>(8*i));
    memcpy(continueBytes+14,entry.data,fresh);
    const uint8_t end[4]={'H','E','N','D'};memcpy(continueBytes+14+fresh,end,4);
    return 18+fresh;
}
// One-time target: consumed (deleted) before it is used, so a failing or
// looping loader can never become a permanent automatic boot.
inline const char16_t* onceTargetName=u"\\EFI\\HAK\\once.bin";
inline efi_status SaveOnce(efi_file_protocol* root,const MenuEntry& entry) {
    const size_t length=BuildTargetRecord(entry);if(!length)return EFI_NOT_FOUND;
    return WriteTargetFile(root,onceTargetName,length);
}
inline efi_status ConsumeOnce(efi_file_protocol* root) {
    efi_file_protocol* file=nullptr;
    const auto status=root->Open(root,&file,onceTargetName,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(status!=EFI_SUCCESS)return status;
    return file->Delete(file);
}
inline efi_status SaveContinue(efi_file_protocol* root,const MenuEntry& entry) {
    size_t fresh;BootOption parsed;
    if(!InBootOrder(entry.id) || ReadOption(entry.id,fresh)!=EFI_SUCCESS || fresh!=entry.size
        || !Same(entry.data,optionBytes,fresh) || !Parse(entry.data,fresh,parsed))return EFI_NOT_FOUND;
    memcpy(continueBytes,continueMagic,8);continueBytes[8]=uint8_t(entry.id);continueBytes[9]=uint8_t(entry.id>>8);
    for(unsigned i=0;i<4;++i)continueBytes[10+i]=uint8_t(fresh>>(8*i));
    memcpy(continueBytes+14,entry.data,fresh);
    const uint8_t end[4]={'H','E','N','D'};memcpy(continueBytes+14+fresh,end,4);
    efi_file_protocol* file=nullptr;
    // Durable intent first: interrupted writes/deleted configuration must not
    // silently change a configured Continue target to firmware's next OS.
    auto status=root->Open(root,&file,u"\\EFI\\HAK\\continue-set",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(status!=EFI_SUCCESS)return status;
    const uint8_t marker=1;
    const bool marked=Write(file,&marker,1) && file->Flush(file)==EFI_SUCCESS;
    const auto markerClosed=file->Close(file);
    if(!marked || markerClosed!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    file=nullptr;
    status=root->Open(root,&file,u"\\EFI\\HAK\\continue.bin",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(status==EFI_SUCCESS) {status=file->Delete(file);if(status!=EFI_SUCCESS)return status;}
    else if(status!=EFI_NOT_FOUND)return status;
    status=root->Open(root,&file,u"\\EFI\\HAK\\continue.bin",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(status!=EFI_SUCCESS)return status;
    bool ok=Write(file,continueBytes,18+fresh) && file->Flush(file)==EFI_SUCCESS;
    auto closed=file->Close(file);return ok && closed==EFI_SUCCESS ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
inline void Append(char16_t* out,size_t capacity,const char16_t* text) {
    size_t at=0;while(at+1<capacity && out[at])++at;
    while(at+1<capacity && *text)out[at++]=*text++;
    if(at && out[at-1]>=0xd800 && out[at-1]<=0xdbff)--at;
    out[at]=0;
}
// r382: Detail settings (bit0 disk name, bit1 boot manager name, bit2 positions).
inline uint8_t entryDetail=7;
inline void EntryLabel(const MenuEntry& entry,bool pl,char16_t* output,size_t capacity) {
    BootOption parsed;output[0]=0;if(!Parse(entry.data,entry.size,parsed))return;
    const char16_t* device=pl ? u"Urządzenie UEFI" : u"UEFI device";
    for(size_t at=parsed.pathOffset;at<parsed.pathOffset+parsed.pathBytes;at+=U16(entry.data+at+2)) {
        const auto* node=entry.data+at;
        if(node[0]==3 && (node[1]==5 || node[1]==15 || node[1]==16)) {device=u"USB";break;}
        if(node[0]==3 && node[1]==11) {device=pl?u"Sieć":u"Network";break;}   // r387: MAC node = network boot
        // ATAPI, SCSI, SATA, NVMe, eMMC/UFS bus nodes: a disk even without a
        // partition node (whole-disk and removable-media entries).
        if(node[0]==3 && (node[1]==1 || node[1]==2 || node[1]==0x12 || node[1]==0x17 || node[1]==0x1d || node[1]==0x19))
            device=pl ? u"Dysk" : u"Disk";
        if(node[0]==4 && node[1]==2)device=u"CD/DVD";
        else if(node[0]==4 && node[1]==1)device=pl ? u"Dysk lub partycja" : u"Disk or partition";
    }
    // The name is firmware metadata, not an inferred or trusted OS identity.
    char16_t description[257]={};size_t units=parsed.descriptionBytes/2;
    if(units>256)units=256;
    for(size_t i=0;i<units;++i) {
        const auto unit=U16(entry.data+6+i*2);
        description[i]=unit<32 || unit==127 ? u' ' : (char16_t)unit;
    }
    if(entry.source==3)device=u"CD/DVD";
    // r386: firmware entries of an optical drive name only the bus (ATAPI/SATA);
    // the firmware's own name says DVD-ROM or CD-ROM.
    for(size_t i=0;i+2<units;++i)if((description[i]==u'D' && description[i+1]==u'V' && description[i+2]==u'D') || (description[i]==u'C' && description[i+1]==u'D' && (description[i+2]==u'-' || description[i+2]==u'R')))device=u"CD/DVD";
    while(units && description[units-1]==u' ')description[--units]=0;
    const char16_t* known=entry.source?nullptr:managerNames.Find(entry.id);
    // r382: without the disk name only the boot manager is said ("GRUB");
    // an entry with no recognized manager keeps its firmware name.
    if(!(entryDetail&1) && known && (entryDetail&2)) {Append(output,capacity,known);return;}
    if(entryDetail&1) {Append(output,capacity,device);Append(output,capacity,u" — ");}
    Append(output,capacity,description);
    if(!(entryDetail&2))return;
    // r368: the boot manager found on that disk, unless the firmware's name
    // already says it ("Windows Boot Manager" entries of installed systems).
    if(const char16_t* manager=entry.source?nullptr:managerNames.Find(entry.id)) {
        bool named=false;
        for(size_t i=0;description[i] && !named;++i) {
            size_t k=0;
            while(manager[k] && description[i+k] && (description[i+k]|0x20)==(manager[k]|0x20))++k;
            named=!manager[k];
        }
        // r389: and what its BCD store lists ("Windows 11 (domyślny), Windows 10; 30 s").
        char16_t listed[200]={};
        if(const BcdSummary* bcd=managerNames.FindBcd(entry.id))DescribeBcd(*bcd,pl,listed,200);
        size_t end=0;while(output[end])++end;
        while(end && output[end-1]==u' ')output[--end]=0;
        if(!named) {
            Append(output,capacity,u" (");Append(output,capacity,manager);
            if(listed[0]) {Append(output,capacity,u": ");Append(output,capacity,listed);}
            Append(output,capacity,u")");}
        else if(listed[0]) {Append(output,capacity,u": ");Append(output,capacity,listed);}
    }
}
// Console text: spoken control roles become icons (glyphs present in the
// EDK2 GraphicsConsole font). Speech keeps the words.
inline void DisplayRoles(const char16_t* text,char16_t* out,size_t capacity) {
    struct Role {const char16_t* word;const char16_t* icon;};
    static const Role roles[]={{u" — podmenu",u"►"},{u", podmenu",u"►"},{u" — submenu",u"►"},{u", submenu",u"►"},
        {u", okno wyboru",u"▼"},{u" Okno wyboru",u"▼"},{u", popup",u"▼"},{u" Popup",u"▼"},
        {u", suwak",u"◄►"},{u" Suwak",u"◄►"},{u", slider",u"◄►"},{u" Slider",u"◄►"},
        {u", pole wyboru, zaznaczone",u"[X]"},{u", pole wyboru, niezaznaczone",u"[ ]"},{u", check box, checked",u"[X]"},{u", check box, not checked",u"[ ]"},
        {u", lista",u"►"},{u", list",u"►"}};
    size_t at=0;const char16_t* icon=nullptr;
    for(size_t i=0;text[i] && at+1<capacity;) {
        // r382: the spoken list position is shown by the counter row instead.
        {const char16_t* positions[]={u", pozycja ",u", position "};bool cut=false;
         for(auto* p:positions) {size_t k=0;while(p[k] && text[i+k]==p[k])++k;if(!p[k] && text[i+k]>=u'0' && text[i+k]<=u'9')cut=true;}
         if(cut)break;}
        bool matched=false;
        for(const auto& role:roles) {
            size_t k=0;while(role.word[k] && text[i+k]==role.word[k])++k;
            if(!role.word[k]) {icon=role.icon;i+=k;matched=true;break;}
        }
        if(!matched)out[at++]=text[i++];
    }
    while(at && out[at-1]==u' ')--at;
    out[at]=0;
    if(icon && icon[0]==u'[') {
        // r382: a check box is drawn in front of its label, as firmware forms do.
        size_t length=0;while(out[length])++length;
        if(length+5<capacity) {for(size_t i=length+1;i>0;--i)out[i+3]=out[i-1];for(unsigned i=0;i<3;++i)out[i]=icon[i];out[3]=u' ';}
    } else if(icon) {Append(out,capacity,u" ");Append(out,capacity,icon);}
}
inline void Utf8(const char16_t* text,char* out,size_t capacity) {
    size_t at=0;
    while(*text && at+4<capacity) {
        uint32_t point=*text++;
        if(point>=0xd800 && point<=0xdbff && *text>=0xdc00 && *text<=0xdfff)
            point=0x10000+((point-0xd800)<<10)+(*text++-0xdc00);
        if(point<0x80)out[at++]=char(point);
        else if(point<0x800) {out[at++]=char(0xc0|(point>>6));out[at++]=char(0x80|(point&63));}
        else if(point<0x10000) {out[at++]=char(0xe0|(point>>12));out[at++]=char(0x80|((point>>6)&63));out[at++]=char(0x80|(point&63));}
        else {out[at++]=char(0xf0|(point>>18));out[at++]=char(0x80|((point>>12)&63));out[at++]=char(0x80|((point>>6)&63));out[at++]=char(0x80|(point&63));}
    }
    out[at]=0;
}
// r356: resident CHARONVOICE.EFI (Driver####) found at start, and the menu's
// current reading preferences handed to it whenever it resumes.
inline ReaderControl* residentDriver=nullptr;
inline char residentLanguage[3]={'e','n',0};
inline uint8_t residentPolish=0,residentVerbosity=1,residentProgress=1;
// r382: the screen reader's voice: rate | pitch<<8 | volume<<16 | effects volume<<24.
inline uint32_t residentVoice=0;
inline uint16_t residentMessageVoice=14|(10<<8);   // r389: boot-message voice rate | pitch<<8 (tenths)
inline uint32_t residentReading=1|(1<<2)|(47<<8)|(60u<<14)|(25u<<20);  // r390/r393: PackReading of the reader settings
// r390: one place for the settings <-> menu model copies (were written out four times).
inline void ModelFromSettings(MenuModel& model,const CharSettings& settings) {
    model.wrap=settings.wrap;model.language=settings.language;model.sounds=settings.sounds;
    model.startMelody=settings.startMelody;model.backgroundMelody=settings.backgroundMelody;model.rate=settings.rate;
    model.speechVolume=settings.speechVolume;model.effectsVolume=settings.effectsVolume;model.verbosity=settings.verbosity;
    model.rememberLast=settings.rememberLast;model.autoSeconds=settings.autoSeconds;model.progress=settings.progress;model.welcome=settings.welcome;
    model.pitch=settings.pitch;model.readerRate=settings.readerRate;model.readerPitch=settings.readerPitch;model.readerVolume=settings.readerVolume;
    model.detail=settings.detail;model.timers=settings.timers;model.messageRate=settings.messageRate;model.messagePitch=settings.messagePitch;
    model.punctuation=settings.punctuation;model.capitals=settings.capitals;model.screenLanguage=settings.screenLanguage;model.readerFlags=settings.readerFlags;
    model.formatFlags=settings.formatFlags;model.readerExtra=settings.readerExtra;model.synthKey=settings.synthKey;model.synthVoice=settings.synthVoice;
}
inline void SettingsFromModel(CharSettings& settings,const MenuModel& model) {
    settings.wrap=model.wrap;settings.language=model.language;settings.sounds=model.sounds;
    settings.startMelody=model.startMelody;settings.backgroundMelody=model.backgroundMelody;settings.rate=model.rate;
    settings.speechVolume=model.speechVolume;settings.effectsVolume=model.effectsVolume;settings.verbosity=model.verbosity;
    settings.rememberLast=model.rememberLast;settings.autoSeconds=model.autoSeconds;settings.progress=model.progress;settings.welcome=model.welcome;
    settings.pitch=model.pitch;settings.readerRate=model.readerRate;settings.readerPitch=model.readerPitch;settings.readerVolume=model.readerVolume;
    settings.detail=model.detail;settings.timers=model.timers;settings.messageRate=model.messageRate;settings.messagePitch=model.messagePitch;
    settings.punctuation=model.punctuation;settings.capitals=model.capitals;settings.screenLanguage=model.screenLanguage;settings.readerFlags=model.readerFlags;
    settings.formatFlags=model.formatFlags;settings.readerExtra=model.readerExtra;settings.synthKey=model.synthKey;settings.synthVoice=model.synthVoice;
}
struct LaunchSpeech {
    MenuVoice* voice=nullptr;AudioEffects* effects=nullptr;const char* language="en";
    bool pl=false,explicitMessages=false,speechOk=false,resident=false,driverResumed=false;
    uint8_t verbosity=1,progress=1;
};
inline void LaunchSay(LaunchSpeech& speech,const char16_t* text,unsigned maximumMs) {
    if(!speech.speechOk || !speech.voice)return;
    char16_t styled[1024]={};FormatCharonMessage(text,styled,1024,speech.explicitMessages,speech.pl,false);
    char utf8[4096];Utf8(styled,utf8,sizeof(utf8));
    charontrace::Text("launch announce");charontrace::Text(utf8);
    if(speech.voice->SpeakUtf8(speech.voice,speech.language,utf8)!=EFI_SUCCESS) {speech.speechOk=false;return;}
    for(unsigned waited=0;waited<maximumMs;waited+=5) {
        if(speech.voice->Pump(speech.voice,2048)!=EFI_SUCCESS) {speech.speechOk=false;return;}
        if(waited>=60 && speech.effects && speech.effects->SpeechIdle && speech.effects->SpeechIdle(speech.effects->context))return;
        systemTable->BootServices->Stall(5000);
    }
}
inline void LaunchAfterLoad(void* context,efi_loaded_image_protocol* loaded) {
    auto& speech=*static_cast<LaunchSpeech*>(context);
    if(speech.verbosity<2 || !loaded)return;
    const bool pl=speech.pl;char16_t text[400]={};
    Append(text,400,pl?u"Program załadowany, ":u"Program loaded, ");
    ConsoleNumber(text,400,size_t((loaded->ImageSize+1023)/1024));
    Append(text,400,pl?u" kilobajtów. ":u" kilobytes. ");
    uint8_t secure=255;size_t size=1;
    if(systemTable->RuntimeServices->GetVariable((char16_t*)u"SecureBoot",&globalGuid,nullptr,&size,&secure)!=EFI_SUCCESS || size!=1)secure=255;
    Append(text,400,secure==1?(pl?u"Secure Boot włączony; firmware zweryfikowało podpis.":u"Secure Boot on; firmware verified the signature.")
        :secure==0?(pl?u"Secure Boot wyłączony.":u"Secure Boot off.") :(pl?u"Stan Secure Boot nieznany.":u"Secure Boot state unknown."));
    LaunchSay(speech,text,4000);
}
// The resident reader (driver or in-image) takes over speech and audio pumping.
// r393 (user): Continue (chosen or automatic) boots as fast as usual: no
// hidden-menu prompt, no stopped countdown. The device list keeps them.
inline bool residentQuickBoot=false;
// r393: the synthesizer add-ons on Charon's disk (manifests), for the reader settings.
inline synths::Info menuSynths[synths::maxSynths];inline size_t menuSynthCount=0;
// What SetSynth reported, said in the menu's language.
inline void SynthMessage(bool ok,const char16_t* report,bool pl,char16_t* out,size_t capacity) {
    out[0]=0;
    if(ok) {Append(out,capacity,pl?u"Syntezator: ":u"Synthesizer: ");Append(out,capacity,report);Append(out,capacity,u".");return;}
    static const char16_t* whyPl[]={u"",u"brak pliku modułu",u"suma kontrolna nie zgadza się z manifestem",u"inna wersja interfejsu",u"firmware nie wczytało modułu",u"moduł nie wystartował",u"moduł nie udostępnił syntezatora",u"nie można przygotować głosu",u"nie można otworzyć dysku Charona",u"brak manifestu"};
    static const char16_t* whyEn[]={u"",u"the module file is missing",u"the checksum does not match the manifest",u"another interface version",u"firmware did not load the module",u"the module did not start",u"the module offered no synthesizer",u"the voice cannot be prepared",u"Charon's disk cannot be opened",u"no manifest"};
    const unsigned why=report && report[0]==u'!' && report[1]>=u'0' && report[1]<=u'9'?unsigned(report[1]-u'0'):9;
    Append(out,capacity,pl?u"Dodatek syntezy niedostępny":u"Synthesizer add-on unavailable");
    if(report && report[0]==u'!' && report[2]) {Append(out,capacity,u" (");Append(out,capacity,report+2);Append(out,capacity,u")");}
    Append(out,capacity,u": ");Append(out,capacity,(pl?whyPl:whyEn)[why]);Append(out,capacity,pl?u". Mówi TVox.":u". TVox speaks.");
}
// The key that chose Continue (and its repeats) must not reach the started
// boot manager: GRUB takes any key as "show the hidden menu".
inline void DrainKeys(efi_system_table* table) {
    if(!table || !table->ConIn)return;
    table->BootServices->Stall(150000);   // the key's release and repeats
    for(unsigned i=0;i<64;++i) {efi_input_key k{};if(table->ConIn->ReadKeyStroke(table->ConIn,&k)!=EFI_SUCCESS)break;}
    efi_guid guid={0xdd9e7534,0x7762,0x4698,{0x8c,0x14,0xf5,0x85,0x17,0xa6,0x25,0xaa}};TextInputEx* ex=nullptr;
    if(table->ConsoleInHandle && table->BootServices->HandleProtocol(table->ConsoleInHandle,&guid,(void**)&ex)==EFI_SUCCESS && ex && ex->ReadKeyStrokeEx)
        for(unsigned i=0;i<64;++i) {KeyData d{};if(ex->ReadKeyStrokeEx(ex,&d)!=EFI_SUCCESS)break;}
}
inline uint32_t LaunchReading() {return residentQuickBoot?(residentReading&~(uint32_t(7)<<20)):residentReading;}
// r396: while a program started from the menu runs with the resident reader,
// publish the reader's control as CHARONVOICE.EFI does. A Charon started by
// that program (OpenCore/rEFInd list Charon's own disk) then speaks with this
// voice and pauses this reader instead of opening the audio device again
// (which answered NOT_READY and left the second Charon silent).
inline ReaderControl appReaderControl{};
inline efi_handle appReaderControlHandle=nullptr;
inline efi_handle charonSelfImage=nullptr;   // r396: set by RunBootMenu
inline efi_status EFIAPI AppReaderSuspend(ReaderControl*) {return residentReader.Suspend();}
inline efi_status EFIAPI AppReaderResume(ReaderControl*,const char* language,uint8_t polish,uint8_t verbosity,uint8_t progress,uint32_t voice,uint32_t reading) {
    ResidentOptions u;
    if(language && language[0] && language[1]) {u.language[0]=language[0];u.language[1]=language[1];}
    u.polish=(polish&1)!=0;u.explicitMessages=(polish&2)!=0;u.positions=(polish&4)==0;u.timers=uint8_t((polish>>3)&3);u.verbosity=verbosity;u.progress=progress;
    u.rate=uint8_t(voice);u.pitch=uint8_t(voice>>8);u.volume=uint8_t(voice>>16);u.effectsVolume=uint8_t(voice>>24);
    u.punctuation=uint8_t(reading&3);u.capitals=uint8_t((reading>>2)&7);u.screenLanguage=uint8_t((reading>>5)&3);u.readerFlags=uint8_t((reading>>8)&63);
    u.formatFlags=ReadingFormat(reading);u.stereo=ReadingStereo(reading);const uint8_t extra=uint8_t((reading>>20)&31);u.managerFlags=uint8_t(extra&7);u.brailleAttributes=(extra&8)!=0;u.dictionary=(extra&16)!=0;
    return residentReader.Resume(&u);
}
inline void PublishAppReaderControl(MenuVoice* voice,AudioEffects* effects) {
    auto* boot=systemTable->BootServices;
    if(appReaderControlHandle || !voice || FindReaderControl(boot))return;
    appReaderControl={1,sizeof(ReaderControl),charonRelease,0,voice,effects,AppReaderSuspend,AppReaderResume};
    efi_handle handle=nullptr;
    if(boot->InstallProtocolInterface(&handle,&readerControlGuid,EFI_NATIVE_INTERFACE,&appReaderControl)==EFI_SUCCESS)appReaderControlHandle=handle;
}
inline void WithdrawAppReaderControl() {
    if(!appReaderControlHandle)return;
    if(systemTable->BootServices->UninstallProtocolInterface(appReaderControlHandle,&readerControlGuid,&appReaderControl)==EFI_SUCCESS)appReaderControlHandle=nullptr;
}
inline bool LaunchActivateReader(LaunchSpeech& speech) {
    if(speech.resident || speech.driverResumed)return true;
    // r389: the boot-message voice (driver or in-image provider).
    {AudioEffects* fx=residentDriver?residentDriver->effects:speech.effects;
     if(fx && fx->SetMessageVoice)fx->SetMessageVoice(fx->context,residentMessageVoice&0xff,residentMessageVoice>>8);}
    if(speech.speechOk && residentDriver) {
        if(residentDriver->Resume(residentDriver,speech.language,uint8_t((speech.pl?1:0)|(speech.explicitMessages?2:0)|((entryDetail&4)?0:4)|(residentPolish&0x18)),
            speech.verbosity,speech.progress,residentVoice,LaunchReading())==EFI_SUCCESS) {speech.driverResumed=true;return true;}
    }
    if(speech.speechOk && !residentDriver) {
        ResidentOptions options;options.voice=speech.voice;options.effects=speech.effects;
        options.language[0]=speech.language[0];options.language[1]=speech.language[1];
        options.polish=speech.pl;options.verbosity=speech.verbosity;options.progress=speech.progress;options.explicitMessages=speech.explicitMessages;
        options.rate=uint8_t(residentVoice);options.pitch=uint8_t(residentVoice>>8);options.volume=uint8_t(residentVoice>>16);options.effectsVolume=uint8_t(residentVoice>>24);
        options.positions=(entryDetail&4)!=0;options.timers=uint8_t((residentPolish>>3)&3);options.selfImage=charonSelfImage;
        options.punctuation=uint8_t(residentReading&3);options.capitals=uint8_t((residentReading>>2)&7);options.screenLanguage=uint8_t((residentReading>>5)&3);options.readerFlags=uint8_t((residentReading>>8)&63);
        options.formatFlags=ReadingFormat(residentReading);options.stereo=ReadingStereo(LaunchReading());{const uint8_t extra=uint8_t((LaunchReading()>>20)&31);options.managerFlags=uint8_t(extra&7);options.brailleAttributes=(extra&8)!=0;options.dictionary=(extra&16)!=0;}
        if(residentReader.Install(systemTable,options)==EFI_SUCCESS) {speech.resident=true;PublishAppReaderControl(speech.voice,speech.effects);return true;}
    }
    return false;
}
inline efi_status LaunchBeforeStart(void* context) {
    auto& speech=*static_cast<LaunchSpeech*>(context);
    if(speech.verbosity>=2 && !speech.resident && !speech.driverResumed)
        LaunchSay(speech,speech.pl?u"Przekazuję sterowanie. Czytam ekran programu.":u"Handing over. Reading the program's screen.",0);
    if(LaunchActivateReader(speech))return EFI_SUCCESS;
    // No resident layer: never transfer while audio may still play.
    return speech.voice?speech.voice->Stop(speech.voice):EFI_SUCCESS;
}
// Loader file named by the boot option's device path (MEDIA_FILEPATH nodes),
// or the removable-media default when the option names only a device.
inline void AppendLoaderFile(const MenuEntry& entry,bool pl,char16_t* out,size_t capacity) {
    BootOption parsed;if(!Parse(entry.data,entry.size,parsed))return;
    char16_t file[260]={};size_t at=0;
    for(size_t offset=parsed.pathOffset;offset<parsed.pathOffset+parsed.pathBytes;offset+=U16(entry.data+offset+2)) {
        const auto* node=entry.data+offset;const size_t length=U16(node+2);
        if(node[0]!=4 || node[1]!=4 || length<6)continue;
        for(size_t i=4;i+1<length && at+1<260;i+=2) {const auto unit=U16(node+i);if(!unit)break;file[at++]=unit<32?u' ':char16_t(unit);}
    }
    file[at]=0;
    Append(out,capacity,pl?u" Plik: ":u" File: ");
    Append(out,capacity,at?file:(pl?u"domyślny program nośnika, EFI BOOT BOOTX64.EFI":u"the media default, EFI BOOT BOOTX64.EFI"));
    Append(out,capacity,u".");
}
inline efi_status FindFirmwareVoice(uint16_t& id); // defined with the Driver#### helpers
inline void ConfirmationMessage(MenuAction action,bool pl,const MenuEntry* entry,char16_t* text,size_t capacity) {
    text[0]=0;
    if(action==MenuAction::setContinue) {
        Append(text,capacity,pl?u"Ustawić jako automatyczny cel Kontynuuj: ":u"Set as the automatic Continue target: ");
        if(entry) {char16_t label[300]={};EntryLabel(*entry,pl,label,300);Append(text,capacity,label);}
    } else if(action==MenuAction::setBootNext) {
        Append(text,capacity,pl?u"Uruchomić raz przy następnym starcie Charona: ":u"Start once on the next Charon start: ");
        if(entry) {char16_t label[300]={};EntryLabel(*entry,pl,label,300);Append(text,capacity,label);}
        Append(text,capacity,pl?u". Potem wraca zwykły cel Kontynuuj.":u". The usual Continue target applies afterwards.");
    } else if(action==MenuAction::toggleFirmwareVoice) {
        uint16_t id;
        Append(text,capacity,FindFirmwareVoice(id)==EFI_SUCCESS
            ?(pl?u"Wyłączyć mowę od włączenia komputera? Usuwa wpis sterownika Charon voice z firmware.":u"Turn off speech from power-on? Removes the Charon voice driver entry from firmware.")
            :(pl?u"Włączyć mowę od włączenia komputera? Od następnego startu firmware załaduje sterownik głosu Charona przed swoim menu i Charon będzie czytał ekrany firmware.":u"Turn on speech from power-on? From the next start firmware loads Charon's voice driver before its own menu and Charon reads firmware screens."));
    } else if(action==MenuAction::restartSystem)
        Append(text,capacity,pl?u"Uruchomić ponownie komputer teraz?":u"Restart the computer now?");
    else if(action==MenuAction::shutdownSystem)
        Append(text,capacity,pl?u"Wyłączyć komputer teraz?":u"Shut down the computer now?");
    else if(action==MenuAction::restartFirmware)
        // r399 (user): after a real restart only the voice driver can read, and
        // only if THIS firmware loads it in its setup - many computers draw
        // their setup with firmware code no UEFI driver can read, so the reader
        // then stays silent. Say so honestly and point to "Menu firmware" in
        // System tools, which reads the setup in this session without a restart.
        Append(text,capacity,pl?u"Uruchomić ponownie komputer i otworzyć ustawienia firmware? Po restarcie czyta tylko sterownik głosu i tylko jeśli to firmware go wczyta w swoim menu. Wiele komputerów rysuje ustawienia tak, że żaden czytnik ich nie odczyta i wtedy będzie cisza. Pewniejsze jest „Menu firmware” w Narzędziach systemowych - Charon czyta ustawienia bez restartu.":u"Restart and open firmware setup? After the restart only the voice driver reads, and only if this firmware loads it in its menu. Many computers draw their setup so that no reader can read it, and then it stays silent. Safer is “Firmware menu” in System tools - Charon reads the setup with no restart.");
    else if(action==MenuAction::saveOrder)
        Append(text,capacity,pl?u"Zapisać nową kolejność rozruchu w firmware? Charon pozostaje pierwszy.":u"Save the new boot order to firmware? Charon stays first.");
    else if(action==MenuAction::pendingBoot)
        Append(text,capacity,pl?u"Uruchomić oczekujący wybór rozruchu?":u"Run the pending boot selection?");
    else if(action==MenuAction::pendingSettings)
        Append(text,capacity,pl?u"Zastosować oczekujące ustawienia firmware?":u"Apply the pending firmware settings?");
    else if(action==MenuAction::pendingKeys)
        Append(text,capacity,pl?u"Zastosować oczekującą zmianę kluczy Secure Boot?":u"Apply the pending Secure Boot key update?");
    Append(text,capacity,pl?u" Domyślnie wybrano Anuluj.":u" Cancel is selected by default.");
}
inline MenuEvent DecodeMenuKey(const efi_input_key& key) {
    switch(key.ScanCode) {
        case 1:return MenuEvent::up;case 2:return MenuEvent::down;case 3:return MenuEvent::right;
        case 4:return MenuEvent::left;case 23:return MenuEvent::back;case 5:return MenuEvent::home;case 6:return MenuEvent::end;
        case 9:return MenuEvent::pageUp;case 10:return MenuEvent::pageDown;
    }
    return key.UnicodeChar==13 ? MenuEvent::enter : key.UnicodeChar==8 ? MenuEvent::back
        : key.UnicodeChar==32 ? MenuEvent::space : MenuEvent::none;
}
// ---- r356: firmware loads CHARONVOICE.EFI from Driver#### ---------------
// One active driver option named "Charon voice" pointing at
// \EFI\HAK\CHARONVOICE.EFI on Charon's own volume, first in DriverOrder,
// with LOAD_OPTION_FORCE_RECONNECT so firmware rebinds controllers after
// loading it (the audio driver then binds to the HDA controller).
static const char16_t firmwareVoiceFile[]=u"\\EFI\\HAK\\CHARONVOICE.EFI";
static const char16_t firmwareVoiceName[]=u"Charon voice";
static uint8_t driverOption[2048];
inline void DriverVariable(uint16_t id,char16_t* name) {
    const char16_t hex[]=u"0123456789ABCDEF";const char16_t prefix[]=u"Driver";
    for(int i=0;i<6;++i)name[i]=prefix[i];
    for(int i=0;i<4;++i)name[6+i]=hex[(id>>(12-4*i))&15];
    name[10]=0;
}
inline efi_status FindFirmwareVoice(uint16_t& id) {
    uint16_t order[256];size_t size=sizeof(order);
    auto status=systemTable->RuntimeServices->GetVariable((char16_t*)u"DriverOrder",&globalGuid,nullptr,&size,order);
    if(status!=EFI_SUCCESS)return status==EFI_NOT_FOUND?EFI_NOT_FOUND:status;
    for(size_t i=0;i<size/2;++i) {
        char16_t name[11];DriverVariable(order[i],name);size_t bytes=sizeof(driverOption);
        if(systemTable->RuntimeServices->GetVariable(name,&globalGuid,nullptr,&bytes,driverOption)!=EFI_SUCCESS || bytes<6+sizeof(firmwareVoiceName))continue;
        if(Same(driverOption+6,(const uint8_t*)firmwareVoiceName,sizeof(firmwareVoiceName))) {id=order[i];return EFI_SUCCESS;}
    }
    return EFI_NOT_FOUND;
}
inline efi_status InstallFirmwareVoice(efi_handle self,efi_file_protocol* root) {
    uint16_t existing;if(FindFirmwareVoice(existing)==EFI_SUCCESS)return EFI_SUCCESS;
    efi_file_protocol* file=nullptr;
    auto status=root->Open(root,&file,firmwareVoiceFile,EFI_FILE_MODE_READ,0);
    if(status!=EFI_SUCCESS)return status;
    file->Close(file);
    auto* boot=systemTable->BootServices;efi_loaded_image_protocol* loaded=nullptr;uint8_t* device=nullptr;
    if(boot->HandleProtocol(self,&imageGuid,(void**)&loaded)!=EFI_SUCCESS || !loaded
        || boot->HandleProtocol(loaded->DeviceHandle,&devicePathGuid,(void**)&device)!=EFI_SUCCESS || !device)return EFI_NOT_FOUND;
    const size_t deviceBytes=PathBytesNoEnd(device,1024);
    const size_t fileNode=4+sizeof(firmwareVoiceFile),pathBytes=deviceBytes+fileNode+4;
    const size_t total=6+sizeof(firmwareVoiceName)+pathBytes;
    if(deviceBytes==SIZE_MAX || total>sizeof(driverOption))return EFI_BAD_BUFFER_SIZE;
    uint8_t* at=driverOption;
    const uint32_t attributes=1|2; // LOAD_OPTION_ACTIVE | LOAD_OPTION_FORCE_RECONNECT
    for(int i=0;i<4;++i)*at++=uint8_t(attributes>>(8*i));
    *at++=uint8_t(pathBytes);*at++=uint8_t(pathBytes>>8);
    memcpy(at,firmwareVoiceName,sizeof(firmwareVoiceName));at+=sizeof(firmwareVoiceName);
    memcpy(at,device,deviceBytes);at+=deviceBytes;
    at[0]=4;at[1]=4;at[2]=uint8_t(fileNode);at[3]=uint8_t(fileNode>>8);memcpy(at+4,firmwareVoiceFile,sizeof(firmwareVoiceFile));at+=fileNode;
    at[0]=0x7f;at[1]=0xff;at[2]=4;at[3]=0;
    uint16_t id=0xffff;
    for(uint16_t candidate=0;candidate<0x1000;++candidate) {
        char16_t name[11];DriverVariable(candidate,name);uint8_t probe[8];size_t bytes=sizeof(probe);
        if(systemTable->RuntimeServices->GetVariable(name,&globalGuid,nullptr,&bytes,probe)==EFI_NOT_FOUND) {id=candidate;break;}
    }
    if(id==0xffff)return EFI_OUT_OF_RESOURCES;
    char16_t name[11];DriverVariable(id,name);
    const uint32_t nv=7; // NON_VOLATILE | BOOTSERVICE_ACCESS | RUNTIME_ACCESS
    status=systemTable->RuntimeServices->SetVariable(name,&globalGuid,nv,total,driverOption);
    if(status!=EFI_SUCCESS)return status;
    uint16_t order[257];size_t size=sizeof(order)-2;
    status=systemTable->RuntimeServices->GetVariable((char16_t*)u"DriverOrder",&globalGuid,nullptr,&size,order+1);
    if(status==EFI_NOT_FOUND)size=0;
    else if(status!=EFI_SUCCESS || size%2) {systemTable->RuntimeServices->SetVariable(name,&globalGuid,nv,0,nullptr);return status==EFI_SUCCESS?EFI_INVALID_PARAMETER:status;}
    order[0]=id;
    status=systemTable->RuntimeServices->SetVariable((char16_t*)u"DriverOrder",&globalGuid,nv,size+2,order);
    if(status!=EFI_SUCCESS) {systemTable->RuntimeServices->SetVariable(name,&globalGuid,nv,0,nullptr);return status;}
    uint16_t check;return FindFirmwareVoice(check)==EFI_SUCCESS && check==id?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
inline efi_status RemoveFirmwareVoice() {
    uint16_t id;auto status=FindFirmwareVoice(id);
    if(status!=EFI_SUCCESS)return status==EFI_NOT_FOUND?EFI_SUCCESS:status;
    uint16_t order[256],kept[256];size_t size=sizeof(order),count=0;
    status=systemTable->RuntimeServices->GetVariable((char16_t*)u"DriverOrder",&globalGuid,nullptr,&size,order);
    if(status!=EFI_SUCCESS)return status;
    for(size_t i=0;i<size/2;++i)if(order[i]!=id)kept[count++]=order[i];
    const uint32_t nv=7;
    status=systemTable->RuntimeServices->SetVariable((char16_t*)u"DriverOrder",&globalGuid,nv,count*2,count?kept:nullptr);
    if(status!=EFI_SUCCESS)return status;
    char16_t name[11];DriverVariable(id,name);
    return systemTable->RuntimeServices->SetVariable(name,&globalGuid,nv,0,nullptr);
}
// r385: an entry firmware starts in a normal boot (active, boot category;
// firmware applications such as OVMF's UiApp are skipped by BDS).
inline bool StartsNormally(uint16_t id) {
    size_t size=0;if(ReadOption(id,size)!=EFI_SUCCESS || size<6)return false;
    const uint32_t attributes=U32(optionBytes);return (attributes&1) && !(attributes&0x1f00);
}
// Index of the first such entry in an order, or count when there is none.
inline size_t FirstNormal(const uint16_t* ids,size_t count) {
    for(size_t i=0;i<count;++i)if(StartsNormally(ids[i]))return i;
    return count;
}
// Label for any BootOrder id, including entries the device list hides.
inline void OrderLabel(uint16_t id,bool pl,char16_t* out,size_t capacity) {
    out[0]=0;size_t size=0;
    if(ReadOption(id,size)!=EFI_SUCCESS || size<8) {
        Append(out,capacity,pl?u"Wpis firmware bez danych":u"Firmware entry without data");return;}
    BootOption parsed;
    if(Parse(optionBytes,size,parsed)) {MenuEntry entry{id,optionBytes,size};EntryLabel(entry,pl,out,capacity);return;}
    char16_t description[200]={};size_t units=0;
    for(size_t at=6;at+1<size && units+1<200;at+=2) {const auto unit=U16(optionBytes+at);if(!unit)break;description[units++]=unit<32?u' ':char16_t(unit);}
    description[units]=0;
    Append(out,capacity,units?description:(pl?u"Wpis bez nazwy":u"Unnamed entry"));
    Append(out,capacity,(U32(optionBytes)&1)?(pl?u", aplikacja firmware":u", firmware application"):(pl?u", nieaktywny":u", inactive"));
}
struct EfiAutoContinue {
    efi_boot_services* boot;
    efi_simple_text_input_protocol* input;
    MenuVoice* voice;
    efi_event audio,deadline;
    efi_status error=EFI_SUCCESS;
    bool cancelled=false;
    AudioEffects* effects=nullptr;
    bool sounds=false,cued=false;
    // r389: Charon's own countdown ticks like the readers' (settings bit 1):
    // 1000 Hz / 40 ms every second, 880 Hz / 500 ms when it ends.
    efi_event second=nullptr;unsigned timers=0;
    bool SpacePending() {
        for(unsigned count=0;count<32;++count) {
            efi_input_key key{};const auto status=input->ReadKeyStroke(input,&key);
            if(status==EFI_NOT_READY)return false;
            if(status!=EFI_SUCCESS) {error=status;return false;}
            if(!key.ScanCode && key.UnicodeChar==32)return true;
        }
        return false;
    }
    bool Failed() const {return error!=EFI_SUCCESS;}
    bool KeysPending() {
        const auto status=boot->CheckEvent(input->WaitForKey);
        if(status!=EFI_SUCCESS && status!=EFI_NOT_READY)error=status;
        return status==EFI_SUCCESS || Failed();
    }
    AutoContinueEvent Wait() {
        efi_event events[3]={input->WaitForKey,deadline,audio};size_t index=0;
        error=WaitMenuVoiceEvent(boot,audio?voice:nullptr,events,audio?3:2,2,index,1);
        return Failed()?AutoContinueEvent::failed:index==0?AutoContinueEvent::key:
            index==1?AutoContinueEvent::deadline:AutoContinueEvent::audio;
    }
    bool Pump() {
        if(second && (timers&2) && effects && effects->Tone && boot->CheckEvent(second)==EFI_SUCCESS)effects->Tone(effects->context,1000,40);
        error=voice?voice->Pump(voice,480):EFI_SUCCESS;return !Failed();}
    bool Stop() {
        if(second && (timers&2) && effects && effects->Tone && !cued)effects->Tone(effects->context,880,500);
        if(!cued && sounds && effects && effects->QueueCue) {
            effects->QueueCue(effects->context,MenuCue::continueBoot);cued=true;
        }
        error=voice?voice->Stop(voice):EFI_SUCCESS;return !Failed();
    }
    static efi_status BeforeTransfer(void* context) {
        auto& gate=*(EfiAutoContinue*)context;
        for(unsigned batch=0;batch<4;++batch) {
            if(gate.SpacePending()) {gate.cancelled=true;return EFI_ABORTED;}
            if(gate.Failed())return gate.error;
            if(!gate.KeysPending())return gate.error;
        }
        return gate.Failed()?gate.error:EFI_NOT_READY;
    }
};
inline efi_status RunBootMenu(efi_handle self,efi_file_protocol* root,uint16_t current,bool& returnToFirmware,AudioEffects* effects=nullptr,MenuVoice* suppliedVoice=nullptr) {
    returnToFirmware=false;
    auto* boot=systemTable->BootServices;auto* input=systemTable->ConIn;
    if(!input || !input->ReadKeyStroke || !input->WaitForKey)return EFI_UNSUPPORTED;
    charonSelfImage=self;
    MenuInventory inventory;
    {efi_loaded_image_protocol* own=nullptr;
     if(systemTable->BootServices->HandleProtocol(self,&imageGuid,(void**)&own)==EFI_SUCCESS && own)inventory.selfDevice=own->DeviceHandle;}
    managers::LoadUserProfiles(root);   // r396: \EFI\HAK\profiles before the device list names managers
    auto status=inventory.Refresh(current);
    auto inventoryStatus=status;
    MenuModel model;model.devices=inventory.count;size_t selected=0;
    CharSettings settings;const auto settingsStatus=LoadCharSettings(root,settings);
    const bool settingsInvalid=settingsStatus!=EFI_SUCCESS && settingsStatus!=EFI_NOT_FOUND;
    ModelFromSettings(model,settings);
    // r393: synthesizer add-ons on the disk; the chosen one is loaded (TVox otherwise).
    menuSynthCount=synths::Scan(boot,root,menuSynths,synths::maxSynths);model.synthCount=menuSynthCount;
    for(size_t i=0;i<menuSynthCount;++i) {model.synthKeys[i]=menuSynths[i].key;model.synthVoiceCounts[i]=uint8_t(menuSynths[i].voices);}
    char16_t synthReport[200]={};bool synthFailed=false;
    if(settings.synthKey && effects && effects->SetSynth)synthFailed=!effects->SetSynth(effects->context,settings.synthKey,settings.synthVoice,synthReport,200);
    ApplyVoiceProfile(effects,settings.rate,settings.pitch,settings.speechVolume,settings.effectsVolume);
    const bool effectsAvailable=effects && effects->QueueCue && effects->SetEnabled;
    if(effectsAvailable)effects->SetEnabled(effects->context,settings.sounds);
    const bool melodiesAvailable=effects && effects->SetMelodies;
    if(melodiesAvailable)effects->SetMelodies(effects->context,settings.startMelody,settings.backgroundMelody);
    auto cue=[&](MenuCue event) {if(effectsAvailable && settings.sounds && event!=MenuCue::none)effects->QueueCue(effects->context,event);};
    char locale[64]={};InstallationLocale(root,locale);
    bool pl=PolishSettings(settings,locale);char language[3]={pl?'p':'e',pl?'l':'n',0};
    if(!pl) {inventory.pl=false;if(inventory.count>inventory.firmwareCount)status=inventory.Refresh(current);}
    auto shareReading=[&]() {
        residentLanguage[0]=language[0];residentLanguage[1]=language[1];residentPolish=uint8_t((pl?1:0)|(settings.explicitMessages?2:0)|((settings.detail&4)?0:4)|((settings.timers&3)<<3));
        residentVerbosity=settings.verbosity?settings.verbosity:1;residentProgress=settings.progress;
        residentVoice=uint32_t(settings.readerRate)|(uint32_t(settings.readerPitch)<<8)|(uint32_t(settings.readerVolume)<<16)|(uint32_t(settings.effectsVolume)<<24);
        residentMessageVoice=uint16_t(settings.messageRate|(settings.messagePitch<<8));
        residentReading=PackReading(settings.punctuation,settings.capitals,settings.screenLanguage,settings.readerFlags,settings.formatFlags,settings.readerExtra);
        entryDetail=settings.detail;
    };
    shareReading();
    auto* voice=suppliedVoice?suppliedVoice:FindMenuVoice(boot);
    efi_event timer=nullptr;bool speechFailed=false;
    if(voice && (boot->CreateEvent(EVT_TIMER,TPL_APPLICATION,nullptr,nullptr,&timer)!=EFI_SUCCESS
        || boot->SetTimer(timer,TimerPeriodic,100000)!=EFI_SUCCESS)) {
        if(timer)boot->CloseEvent(timer);
        timer=nullptr;speechFailed=true;
    }
    bool render=true,done=false,sayFocus=true;
    char16_t statusText[512]={};
    char16_t popupText[1024]={};
    if(inventoryStatus!=EFI_SUCCESS) {Append(statusText,512,BootMessage(BootProblem::inventoryRead,pl,settings.explicitMessages));AppendBootStatus(statusText,512,inventoryStatus);}
    else if(!inventory.count)Append(statusText,512,BootMessage(BootProblem::emptyInventory,pl,settings.explicitMessages));
    if(settingsInvalid)Append(statusText,512,pl?u"Nieprawidłowe ustawienia; użyto domyślnych. Zapis zablokowany.":u"Invalid settings; defaults loaded. Saving blocked.");
    auto announce=[&](const char16_t* text,bool authored=false) {
        if(voice && !speechFailed) {char16_t styled[1400]={};
            // r382: on the screen reader page Charon speaks with the reader's voice.
            if(model.page==MenuPage::reader)ApplyVoiceProfile(effects,model.readerRate,model.readerPitch,model.readerVolume,settings.effectsVolume);
            else ApplyVoiceProfile(effects,settings.rate,settings.pitch,settings.speechVolume,settings.effectsVolume);
            FormatCharonMessage(text,styled,1400,settings.explicitMessages,pl,authored);
            char utf8[4400];Utf8(styled,utf8,sizeof(utf8));
            charontrace::Text("menu announce");charontrace::Text(utf8);
            if(voice->SpeakUtf8(voice,language,utf8)!=EFI_SUCCESS) {
                speechFailed=true;render=true;}}
    };
    auto stop=[&]() {
        const auto stopped=voice?voice->Stop(voice):EFI_SUCCESS;
        return stopped==EFI_SUCCESS?CleanupCharonShell():stopped;
    };
    auto message=[&](const char16_t* text) {statusText[0]=0;Append(statusText,512,text);announce(text);render=true;sayFocus=false;};
    auto popup=[&](const char16_t* text) {
        popupText[0]=0;Append(popupText,1024,text);statusText[0]=0;Append(statusText,512,text);
        char16_t spoken[1200]={};Append(spoken,1200,text);
        if(settings.explicitMessages) {
            static const char16_t* plClose[]={u" Enter albo Escape i wypierdalaj z tym komunikatem.",u" Zamknij to Enterem albo Escape’em, łamago.",u" Enter lub Escape, jak już przeczytasz, ślimaku, kurwa mać."};
            static const char16_t* enClose[]={u" Enter or Escape and fuck off out of this message.",u" Close it with Enter or Escape, numpty.",u" Enter or Escape once you’ve finished, slowpoke, for fuck’s sake."};
            Append(spoken,1200,(pl?plClose:enClose)[ExplicitVariety()%3]);
        } else Append(spoken,1200,pl?u" Enter lub Escape zamyka komunikat.":u" Enter or Escape closes this message.");
        model.Notice();announce(spoken);render=true;sayFocus=false;
    };
    auto problem=[&](BootProblem value,efi_status code,bool includeCode=true) {
        char16_t text[512]={};Append(text,512,BootMessage(value,pl,settings.explicitMessages));
        if(includeCode)AppendBootStatus(text,512,uint64_t(code));
        popup(text);
    };
    auto labelFor=[&](size_t row,char16_t* label,size_t capacity) {
        label[0]=0;
        auto pick=[&](const char16_t* normal) {
            const char16_t* rude=settings.explicitMessages?ExplicitMenuLabel(model.page,row,pl):nullptr;
            Append(label,capacity,rude?rude:normal);
        };
        auto position=[&](size_t at,size_t count) {
            Append(label,capacity,u", ");ConsoleNumber(label,capacity,at+1);   // r390 (user): "9 z 19", without "pozycja"
            Append(label,capacity,pl?u" z ":u" of ");ConsoleNumber(label,capacity,count);
        };
            if(model.page==MenuPage::main) {
                const char16_t* labels[]={pl ? u"Urządzenia rozruchowe, podmenu" : u"Boot devices, submenu",
                    pl ? u"Kolejność rozruchu, podmenu" : u"Boot order, submenu",
                    pl ? u"Ustawienia Charona, podmenu" : u"Charon settings, submenu",
                    pl ? u"Ustawienia UEFI, podmenu" : u"UEFI settings, submenu",
                    u"UEFI Shell",pl ? u"Narzędzia systemowe, podmenu" : u"System tools, submenu",pl ? u"Kontynuuj" : u"Continue"};
                Append(label,capacity,settings.explicitMessages?ExplicitMenuLabel(model.page,row,pl):labels[row]);
                if(row==mainContinue) {uint16_t id;size_t size;
                    auto target=ReadContinue(root,id,size);
                    if(target==EFI_NOT_FOUND && FallbackContinue(current,id,size,&inventory)==EFI_SUCCESS)target=EFI_SUCCESS;   // r396
                    if(target==EFI_NOT_FOUND && !ContinueWasConfigured(root))Append(label,capacity,pl ? u" — powrót do kolejności firmware" : u" — return to firmware boot order");
                    else if(target==EFI_SUCCESS) {MenuEntry entry{id,continueBytes+14,size};char16_t name[300];EntryLabel(entry,pl,name,300);Append(label,capacity,u" — ");Append(label,capacity,name);}
                    else Append(label,capacity,pl ? u" — nieprawidłowy zapisany cel" : u" — invalid saved target");}
            } else if(model.page==MenuPage::devices) {
                if(inventory.count) {EntryLabel(inventory.entries[row],pl,label,capacity);if(settings.detail&4)position(row,inventory.count);}
                else Append(label,capacity,settings.explicitMessages?(pl?u"Brak tych cholernych poprawnych wpisów rozruchowych":u"No damn valid boot entries"):
                    (pl ? u"Brak prawidłowych wpisów rozruchowych" : u"No valid boot entries"));
            } else if(model.page==MenuPage::settings) {
                const char16_t* labels[]={model.wrap ? (pl ? u"Zawijanie list, włączone" : u"Wrap lists, enabled") : (pl ? u"Zawijanie list, wyłączone" : u"Wrap lists, disabled"),
                    pl ? u"Odśwież urządzenia" : u"Refresh devices",pl ? u"Stan Secure Boot" : u"Secure Boot status",
                    pl ? u"Uruchom oczekujący wybór rozruchu" : u"Run pending boot selection",
                    pl ? u"Zastosuj oczekujące ustawienia firmware" : u"Apply pending firmware settings",
                    pl ? u"Zastosuj oczekującą zmianę kluczy" : u"Apply pending key update",
                    pl ? u"Ustaw wybrane urządzenie jako cel Kontynuuj" : u"Set selected device as Continue target",
                    settings.language==0 ? (pl?u"Język, automatyczny":u"Language, automatic") : settings.language==1 ? u"Language, English" : u"Język, polski",
                    settings.sounds ? (pl?u"Dźwięki interfejsu, włączone":u"UI sounds, enabled") : (pl?u"Dźwięki interfejsu, wyłączone":u"UI sounds, disabled"),
                    pl?u"Melodia startu, okno wyboru":u"Start melody, popup",
                    pl?u"Melodia w tle, okno wyboru":u"Background melody, popup",
                    pl?u"Tempo mowy, suwak":u"Speech rate, slider",
                    pl?u"Wysokość głosu, suwak":u"Voice pitch, slider",
                    pl?u"Głośność mowy, suwak":u"Speech volume, slider",
                    pl?u"Głośność efektów i melodii, suwak":u"Effects and melody volume, slider",
                    pl?u"Komunikaty rozruchu, okno wyboru":u"Boot messages, popup",
                    pl?u"Zapamiętaj ostatni wybór":u"Remember last choice",
                    pl?u"Czas autostartu, okno wyboru":u"Auto-continue time, popup",
                    pl?u"Uruchom wybrane urządzenie raz przy następnym starcie":u"Start selected device once on next start",
                    pl?u"Paski postępu, okno wyboru":u"Progress bars, popup",
                    pl?u"Mowa od włączenia komputera":u"Speech from power-on",
                    pl?u"Powitanie, lista":u"Welcome, list",
                    pl?u"Ustawienia czytnika ekranu, podmenu":u"Screen reader settings, submenu",
                    pl?u"Szczegółowość, podmenu":u"Detail, submenu",
                    pl?u"Syntezator":u"Synthesizer",pl?u"Głos syntezatora":u"Synthesizer voice"};
                pick(labels[row]);
                if(row==setSynth || row==setSynthVoice) {   // r396
                    Append(label,capacity,pl?u", okno wyboru, ":u", combo box, ");
                    const synths::Info* info=synths::Find(menuSynths,menuSynthCount,model.synthKey);
                    if(!info)Append(label,capacity,row==setSynth?(pl?u"TVox, wbudowany":u"TVox, built in"):u"TVox");
                    else if(row==setSynth) {char16_t name[48];synths::Wide(info->name,sizeof(info->name),name,48);Append(label,capacity,name);Append(label,capacity,pl?u", dodatek":u", add-on");}
                    else Append(label,capacity,info->voiceName[model.synthVoice<info->voices?model.synthVoice:0]);
                }
                if(settings.explicitMessages) {
                    if(row==0 || row==8)Append(label,capacity,(row==0?model.wrap:settings.sounds)?
                        (pl?u", włączone":u", enabled"):(pl?u", wyłączone":u", disabled"));
                    if(row==7)Append(label,capacity,settings.language==0?(pl?u", automatyczny":u", automatic"):
                        settings.language==1?u", English":u", Polski");
                }
                if(row==9 || row==10) {
                    const auto choice=row==9?settings.startMelody:settings.backgroundMelody;
                    if(!melodiesAvailable)Append(label,capacity,pl?u", odtwarzanie niedostępne":u", playback unavailable");
                    else if(!choice)Append(label,capacity,pl?u", wyłączone":u", off");
                    else {Append(label,capacity,pl?u", melodia ":u", melody ");ConsoleNumber(label,capacity,choice);}
                }
                if(row==11) {Append(label,capacity,u", ");ConsoleNumber(label,capacity,VoicePercent(model.rate));Append(label,capacity,u"%");}
                if(row==setPitch) {Append(label,capacity,u", ");ConsoleNumber(label,capacity,VoicePercent(model.pitch));Append(label,capacity,u"%");}
                if(row==setReaderVoice) {Append(label,capacity,pl?u": tempo ":u": rate ");ConsoleNumber(label,capacity,VoicePercent(settings.readerRate));
                    Append(label,capacity,pl?u"%, wysokość ":u"%, pitch ");ConsoleNumber(label,capacity,VoicePercent(settings.readerPitch));
                    Append(label,capacity,pl?u"%, głośność ":u"%, volume ");ConsoleNumber(label,capacity,size_t(settings.readerVolume)*10);Append(label,capacity,u"%");}
                if(row==setDetail) {const char16_t* parts[]={pl?u"nazwa dysku":u"disk name",pl?u"menedżer rozruchu":u"boot manager",pl?u"pozycja":u"position"};bool any=false;
                    for(unsigned bit=0;bit<3;++bit)if(settings.detail&(1u<<bit)) {Append(label,capacity,any?u", ":u": ");Append(label,capacity,parts[bit]);any=true;}
                    if(!any)Append(label,capacity,pl?u": tylko nazwa wpisu":u": entry name only");}
                if(row==setSpeechVolume || row==setEffectsVolume) {Append(label,capacity,u", ");
                    ConsoleNumber(label,capacity,size_t(row==setSpeechVolume?model.speechVolume:model.effectsVolume)*10);Append(label,capacity,u"%");}
                if(row==setVerbosity)Append(label,capacity,settings.verbosity==0?(pl?u", wyłączone":u", off"):settings.verbosity==1?(pl?u", krótkie":u", short"):(pl?u", szczegółowe":u", detailed"));
                if(row==setRemember)Append(label,capacity,settings.rememberLast?(pl?u", włączone":u", enabled"):(pl?u", wyłączone":u", disabled"));
                if(row==setAutoDelay) {
                    if(settings.autoSeconds==autoNever)Append(label,capacity,pl?u", nigdy":u", never");
                    else if(!settings.autoSeconds)Append(label,capacity,pl?u", natychmiast":u", immediately");
                    else {Append(label,capacity,u", ");ConsoleNumber(label,capacity,settings.autoSeconds);Append(label,capacity,u" s");}
                }
                if(row==setFirmwareVoice) {uint16_t id;const bool on=FindFirmwareVoice(id)==EFI_SUCCESS;
                    Append(label,capacity,on?(pl?u", włączona":u", enabled"):(pl?u", wyłączona":u", disabled"));
                    if(residentDriver)Append(label,capacity,pl?u", działa teraz":u", running now");}
                if(row==setWelcome) {
                    // r382: which of the three welcome parts are on.
                    const char16_t* parts[]={pl?u"porada":u"hint",pl?u"dźwięk":u"sound",pl?u"mowa":u"speech"};bool any=false;
                    for(unsigned bit=0;bit<3;++bit)if(settings.welcome&(1u<<bit)) {Append(label,capacity,any?u", ":u": ");Append(label,capacity,parts[bit]);any=true;}
                    if(!any)Append(label,capacity,pl?u": wszystko wyłączone":u": all off");
                }
                if(row==setProgress) {const char16_t* modes[]={pl?u", wyłączone":u", off",pl?u", sygnały":u", beeps",pl?u", mowa co 10%":u", speech every 10%",pl?u", sygnały i mowa":u", beeps and speech"};
                    Append(label,capacity,modes[settings.progress&3]);}
                if((row==6 || row==setBootNextRow) && selected<inventory.count) {char16_t name[300];EntryLabel(inventory.entries[selected],pl,name,300);Append(label,capacity,u" — ");Append(label,capacity,name);}
            } else if(model.page==MenuPage::startMelody || model.page==MenuPage::backgroundMelody) {
                if(settings.explicitMessages)Append(label,capacity,ExplicitMenuLabel(model.page,row,pl));
                else if(!row)Append(label,capacity,pl?u"Wyłączone":u"Off");
                else {Append(label,capacity,pl?u"Melodia ":u"Melody ");ConsoleNumber(label,capacity,row);}
            } else if(model.page==MenuPage::language) {
                const char16_t* labels[]={pl?u"Automatyczny":u"Automatic",u"English",u"Polski"};
                pick(labels[row]);
            } else if(model.page==MenuPage::verbosity) {
                const char16_t* labels[]={pl?u"Wyłączone: bez komunikatów i bez czytania programu":u"Off: no messages, no screen reading",
                    pl?u"Krótkie: co uruchamiam, czytanie ekranu programu":u"Short: what starts, program screen reading",
                    pl?u"Szczegółowe: także plik, rozmiar, Secure Boot i liczniki":u"Detailed: also file, size, Secure Boot and counters"};
                pick(labels[row]);
            } else if(model.page==MenuPage::welcome) {
                const char16_t* labels[]={pl?u"Porada, jak przejść do menu opcji":u"Hint how to open the options menu",
                    pl?u"Pisk powitania, jak przy starcie dodatku":u"Welcome beep, as when the add-on starts",pl?u"Mowa: Witaj w Charon":u"Speech: Welcome to Charon"};
                pick(labels[row]);
                Append(label,capacity,(model.welcome&(1u<<row))?(pl?u", pole wyboru, zaznaczone":u", check box, checked")
                    :(pl?u", pole wyboru, niezaznaczone":u", check box, not checked"));
                if(settings.detail&4)position(row,3);
            } else if(model.page==MenuPage::detail) {
                const char16_t* labels[]={pl?u"Nazwa dysku":u"Disk name",pl?u"Nazwa menedżera rozruchu, na przykład GRUB":u"Boot manager name, for example GRUB",
                    pl?u"Pozycja na liście, na przykład 2 z 5":u"Position in lists, for example 2 of 5"};
                pick(labels[row]);
                Append(label,capacity,(model.detail&(1u<<row))?(pl?u", pole wyboru, zaznaczone":u", check box, checked")
                    :(pl?u", pole wyboru, niezaznaczone":u", check box, not checked"));
                if(model.detail&4)position(row,3);
            } else if(model.page==MenuPage::reader) {
                const char16_t* labels[]={pl?u"Tempo czytnika, suwak":u"Reader rate, slider",pl?u"Wysokość głosu czytnika, suwak":u"Reader pitch, slider",
                    pl?u"Głośność czytnika, suwak":u"Reader volume, slider",
                    pl?u"Odliczanie: mowa co 10 sekund":u"Countdowns: speak every 10 seconds",
                    pl?u"Odliczanie: dźwięk co sekundę, długi na końcu":u"Countdowns: tick every second, long tone at the end",
                    pl?u"Tempo komunikatów startowych, suwak":u"Boot message rate, slider",pl?u"Wysokość głosu komunikatów startowych, suwak":u"Boot message pitch, slider",
                    pl?u"Szczegółowość czytnika":u"Reader verbosity",pl?u"Interpunkcja":u"Symbol level",pl?u"Język tekstu ekranu":u"Screen text language",
                    pl?u"Wielkie litery: mów duże":u"Capitals: say cap",pl?u"Wielkie litery: dźwięk":u"Capitals: beep",pl?u"Wielkie litery: wyższy głos":u"Capitals: higher pitch",
                    pl?u"Tekst poza zaznaczeniem drugim głosem":u"Text outside the selection on the second voice",pl?u"Czytaj wpisywane znaki":u"Speak typed characters",
                    pl?u"Czytaj wpisywane słowa":u"Speak typed words",pl?u"Czytaj znak pod kursorem po strzałkach":u"Speak the character at the cursor after arrows",
                    pl?u"Automatyczne czytanie zmian ekranu":u"Automatic reading of screen changes",pl?u"Porady w menu Charona":u"Hints in Charon's menu",
                    pl?u"Ukryte menu menedżerów rozruchu":u"Hidden boot-manager menus",pl?u"Zatrzymuj odliczanie menedżerów rozruchu":u"Stop boot-manager countdowns",
                    pl?u"Formatowanie tekstu, podmenu":u"Text formatting, submenu",
                    pl?u"Syntezator":u"Synthesizer",pl?u"Głos syntezatora":u"Synthesizer voice"};
                pick(labels[row]);
                if(row==readerSynthRow || row==readerSynthVoiceRow) {
                    // r393 synthesizer add-ons (\EFI\HAK\synths)
                    Append(label,capacity,pl?u", okno wyboru, ":u", combo box, ");
                    const synths::Info* info=synths::Find(menuSynths,menuSynthCount,model.synthKey);
                    if(!info)Append(label,capacity,row==readerSynthRow?(pl?u"TVox, wbudowany":u"TVox, built in"):u"TVox");
                    else if(row==readerSynthRow) {char16_t name[48];synths::Wide(info->name,sizeof(info->name),name,48);Append(label,capacity,name);Append(label,capacity,pl?u", dodatek":u", add-on");}
                    else Append(label,capacity,info->voiceName[model.synthVoice<info->voices?model.synthVoice:0]);
                } else if(row==readerHiddenMenuRow) {
                    static const char16_t* hiddenPl[]={u"wyłączone",u"ogłoś i czekaj 2 sekundy",u"zawsze odsłaniaj"};static const char16_t* hiddenEn[]={u"off",u"announce and wait 2 seconds",u"always reveal"};
                    Append(label,capacity,pl?u", okno wyboru, ":u", combo box, ");Append(label,capacity,(pl?hiddenPl:hiddenEn)[(model.readerExtra&3)<=2?(model.readerExtra&3):1]);
                } else if(row==readerStopCountdownRow) {
                    Append(label,capacity,(model.readerExtra&4)?(pl?u", pole wyboru, zaznaczone":u", check box, checked"):(pl?u", pole wyboru, niezaznaczone":u", check box, not checked"));
                } else if(row==readerFormattingRow) {
                } else if(row==readerVerbosityRow || row==readerPunctuationRow || row==readerLanguageRow) {
                    static const char16_t* verbosityPl[]={u"wyłączony",u"krótko",u"szczegółowo"};static const char16_t* verbosityEn[]={u"off",u"short",u"detailed"};
                    static const char16_t* levelPl[]={u"brak",u"część",u"większość",u"wszystko"};static const char16_t* levelEn[]={u"none",u"some",u"most",u"all"};
                    static const char16_t* langPl[]={u"automatycznie",u"jak menu",u"angielski",u"polski"};static const char16_t* langEn[]={u"automatic",u"as the menu",u"English",u"Polish"};
                    const char16_t* value=row==readerVerbosityRow?(pl?verbosityPl:verbosityEn)[model.verbosity<=2?model.verbosity:1]
                        :row==readerPunctuationRow?(pl?levelPl:levelEn)[model.punctuation&3]:(pl?langPl:langEn)[model.screenLanguage&3];
                    Append(label,capacity,pl?u", okno wyboru, ":u", combo box, ");Append(label,capacity,value);
                } else if(ReaderCheckRow(row)) {
                    static const uint8_t bits[]={1,2,16,8,4,32};
                    const bool on=row<=readerCapPitchRow?(model.capitals&(1u<<(row-readerCapSayRow)))!=0:(model.readerFlags&bits[row-readerMessagesRow])!=0;
                    Append(label,capacity,on?(pl?u", pole wyboru, zaznaczone":u", check box, checked"):(pl?u", pole wyboru, niezaznaczone":u", check box, not checked"));
                } else if(row==readerMessageRateRow || row==readerMessagePitchRow) {
                    Append(label,capacity,u", ");ConsoleNumber(label,capacity,VoicePercent(row==readerMessageRateRow?model.messageRate:model.messagePitch));Append(label,capacity,u"%");
                } else if(row<readerTimerSpeechRow) {
                    const uint8_t value=row==readerRateRow?model.readerRate:row==readerPitchRow?model.readerPitch:model.readerVolume;
                    Append(label,capacity,u", ");ConsoleNumber(label,capacity,row==readerVolumeRow?size_t(value)*10:VoicePercent(value));Append(label,capacity,u"%");
                } else {
                    const bool on=(model.timers&(row==readerTimerSpeechRow?1:2))!=0;
                    Append(label,capacity,on?(pl?u", pole wyboru, zaznaczone":u", check box, checked"):(pl?u", pole wyboru, niezaznaczone":u", check box, not checked"));
                }
                if(model.detail&4)position(row,readerCount);
            } else if(model.page==MenuPage::formatting) {
                // r393 (user): Emacspeak-style formatting as named check boxes.
                const char16_t* labels[]={pl?u"Formatowanie głosem: wysokość, tempo, głośność i barwa jak w Emacspeaku":u"Voice formatting: pitch, rate, volume and timbre, as in Emacspeak",
                    pl?u"Ikony dźwiękowe: dźwięki oscylatora razem z mową":u"Sound icons: oscillator sounds together with speech",
                    pl?u"Kolory tekstu i tła":u"Text and background colours",pl?u"Jasny i pogrubiony tekst":u"Bright and bold text",
                    pl?u"Czcionka i rozmiar":u"Font and size",pl?u"Odwrócenie i podświetlenie":u"Inverse and highlight",
                    pl?u"Ikony zdarzeń jak w Emacspeaku: otwarcie, zamknięcie, wybór, przewinięcie, ostrzeżenie":u"Event icons, as in Emacspeak: open, close, select, scroll, warning",
                    pl?u"Rodziny głosów: inny głos dla innego stylu":u"Voice families: another voice for another style",
                    pl?u"Położenie w stereo":u"Stereo position",
                    pl?u"Atrybuty na brajlu, punkty 7 i 8":u"Attributes in braille, dots 7 and 8",pl?u"Słownik wymowy firmware, na przykład TPM, XHCI":u"Firmware pronunciation dictionary, for example TPM, XHCI"};
                pick(labels[row<formattingCount?row:0]);
                if(row==formatStereoRow) {
                    static const char16_t* stereoPl[]={u", wyłączone",u", ikony",u", mowa i ikony"};static const char16_t* stereoEn[]={u", off",u", icons",u", speech and icons"};
                    const unsigned s=(model.readerExtra>>5)&3;Append(label,capacity,(pl?stereoPl:stereoEn)[s<3?s:0]);
                    Append(label,capacity,pl?u", lista rozwijana":u", combo box");
                } else {
                const bool on=row<=formatFamiliesRow?(model.formatFlags&(1u<<row))!=0:(model.readerExtra&(row==formatBrailleRow?8:16))!=0;
                Append(label,capacity,on?(pl?u", pole wyboru, zaznaczone":u", check box, checked"):(pl?u", pole wyboru, niezaznaczone":u", check box, not checked"));
                }
                if(model.detail&4)position(row,formattingCount);
            } else if(model.page==MenuPage::keyList) {
                const auto& e=keyhelp::entries[row<keyhelp::count?row:0];pick(pl?e.pl:e.en);
                if(model.detail&4)position(row,keyhelp::count);
            } else if(model.page==MenuPage::progress) {
                const char16_t* labels[]={pl?u"Wyłączone":u"Off",pl?u"Sygnały dźwiękowe jak w NVDA":u"Beeps, as in NVDA",
                    pl?u"Mowa co 10 procent":u"Speech every 10 percent",pl?u"Sygnały i mowa":u"Beeps and speech"};
                pick(labels[row]);
            } else if(model.page==MenuPage::autoDelay) {
                const auto value=MenuModel::AutoValue(row);
                if(value==autoNever)pick(pl?u"Nigdy: zawsze otwórz menu":u"Never: always open the menu");
                else if(!value)pick(pl?u"Natychmiast":u"Immediately");
                else {ConsoleNumber(label,capacity,value);Append(label,capacity,pl?u" sekund":u" seconds");}
            } else if(model.page==MenuPage::bootOrder) {
                if(row<model.orderCount) {
                    ConsoleNumber(label,capacity,row+1);Append(label,capacity,u". ");
                    char16_t name[300]={};OrderLabel(model.order[row],pl,name,300);Append(label,capacity,name);
                    if(model.grabbed && row==model.index)Append(label,capacity,pl?u", przenoszony":u", moving");
                } else if(row==model.OrderSaveRow()) {
                    Append(label,capacity,pl?u"Zapisz kolejność":u"Save order");
                    if(model.orderDirty)Append(label,capacity,pl?u", niezapisane zmiany":u", unsaved changes");
                } else if(row==model.OrderRevertRow())Append(label,capacity,pl?u"Przywróć kolejność z firmware":u"Revert to firmware order");
                else if(row==model.OrderCharonRow()) {
                    Append(label,capacity,pl?u"Menu Charona przy starcie":u"Charon menu at start");
                    Append(label,capacity,model.charonFirst?(pl?u", pole wyboru, zaznaczone":u", check box, checked"):(pl?u", pole wyboru, niezaznaczone":u", check box, not checked"));
                    if(!model.charonFirst)Append(label,capacity,pl?u". Firmware uruchamia od razu pierwszy wpis, a ekran czyta tylko sterownik głosu Charona":u". Firmware starts the first entry directly and only Charon's voice driver reads the screen");
                }
                else {
                    Append(label,capacity,pl?u"Limit czasu menu firmware, suwak":u"Firmware menu timeout, slider");
                    if(!model.timeoutKnown)Append(label,capacity,pl?u", niedostępny":u", unavailable");
                    else {Append(label,capacity,u", ");ConsoleNumber(label,capacity,model.firmwareTimeout);Append(label,capacity,u" s");}
                }
                (void)position;
            } else if(model.page==MenuPage::tools) {
                const char16_t* labels[]={pl?u"Informacje o komputerze":u"Computer information",pl?u"Data i godzina":u"Date and time",
                    pl?u"Ustaw datę i godzinę, podmenu":u"Set date and time, submenu",pl?u"Budzenie komputera, podmenu":u"Wake-up alarm, submenu",
                    pl?u"Zmienne firmware, lista":u"Firmware variables, list",pl?u"Sterowniki UEFI, lista":u"UEFI drivers, list",
                    pl?u"Aktualizacje firmware (ESRT)":u"Firmware updates (ESRT)",
                    pl?u"Połącz sterowniki ze wszystkimi urządzeniami":u"Connect drivers to all devices",
                    pl?u"Uruchom program EFI z pliku, lista":u"Start an EFI program from a file, list",
                    pl?u"Uruchom ponownie komputer":u"Restart the computer",pl?u"Wyłącz komputer":u"Shut down the computer",
                    pl?u"Uruchom ponownie do ustawień firmware":u"Restart into firmware setup",
                    pl?u"Menu ustawień firmware z czytnikiem, bez restartu":u"Firmware setup menu with the screen reader, without a restart"};
                pick(labels[row]);
                if(row==toolFirmware && !systools::FirmwareSetupSupported(systemTable->RuntimeServices))
                    Append(label,capacity,pl?u", firmware tego nie obsługuje":u", not supported by this firmware");
            } else if(model.page==MenuPage::clockEdit) {
                const char16_t* names[]={pl?u"Rok":u"Year",pl?u"Miesiąc":u"Month",pl?u"Dzień":u"Day",pl?u"Godzina":u"Hour",pl?u"Minuta":u"Minute"};
                if(row<5) {
                    Append(label,capacity,names[row]);Append(label,capacity,u", ");
                    const unsigned values[]={model.clockYear,model.clockMonth,model.clockDay,model.clockHour,model.clockMinute};
                    ConsoleNumber(label,capacity,values[row]);Append(label,capacity,pl?u", suwak":u", slider");
                } else if(row==6)Append(label,capacity,pl?u"Wyłącz budzenie":u"Turn the alarm off");
                else {
                    Append(label,capacity,model.clockWake?(pl?u"Ustaw budzenie na: ":u"Set the alarm to: "):(pl?u"Zapisz w zegarze firmware: ":u"Save to the firmware clock: "));
                    char16_t when[160]={};systools::DateWords(pl,model.clockYear,model.clockMonth,model.clockDay,model.clockHour,model.clockMinute,when,160);
                    Append(label,capacity,when);
                }
            } else if(model.page==MenuPage::infoList) {
                const auto& list=systools::SharedList();
                if(!list.count)Append(label,capacity,pl?u"Lista jest pusta":u"The list is empty");
                else if(row<list.count)Append(label,capacity,list.rows[row]);
            } else if(model.page==MenuPage::notice)Append(label,capacity,settings.explicitMessages?ExplicitMenuLabel(model.page,row,pl):u"OK");
            else Append(label,capacity,settings.explicitMessages?ExplicitMenuLabel(model.page,row,pl):
                row==0 ? (pl ? u"Anuluj" : u"Cancel") : (pl ? u"Potwierdź zmianę" : u"Confirm action"));
        // r374: options without an authored explicit wording get a tag.
        if(settings.explicitMessages && label[0] && !ExplicitMenuLabel(model.page,row,pl))Append(label,capacity,ExplicitLabelTag(label,row,pl));
    };
    // Boot order page state: the exact BootOrder the page was built from.
    uint16_t orderSnapshot[maximumOrder]={};size_t orderSnapshotBytes=0;uint32_t orderAttributes=7;
    bool currentInOrder=false;uint32_t timeoutAttributes=7;uint16_t firmwareTimeoutSaved=0;
    auto loadOrder=[&]()->efi_status {
        orderSnapshotBytes=sizeof(orderSnapshot);
        auto result=systemTable->RuntimeServices->GetVariable((char16_t*)u"BootOrder",&globalGuid,&orderAttributes,&orderSnapshotBytes,orderSnapshot);
        if(result==EFI_NOT_FOUND) {orderSnapshotBytes=0;orderAttributes=7;}
        else if(result!=EFI_SUCCESS)return result;
        if(orderSnapshotBytes%2)return EFI_INVALID_PARAMETER;
        uint16_t movable[maximumOrder];size_t count=0;currentInOrder=false;
        for(size_t i=0;i<orderSnapshotBytes/2;++i) {
            if(orderSnapshot[i]==current) {currentInOrder=true;continue;}
            movable[count++]=orderSnapshot[i];
        }
        model.LoadOrder(movable,count);
        {const size_t first=FirstNormal(orderSnapshot,orderSnapshotBytes/2);
        model.charonFirst=!currentInOrder || (first<orderSnapshotBytes/2 && orderSnapshot[first]==current);}
        uint16_t value=0;size_t size=2;
        result=systemTable->RuntimeServices->GetVariable((char16_t*)u"Timeout",&globalGuid,&timeoutAttributes,&size,&value);
        if(result==EFI_SUCCESS && size==2) {model.timeoutKnown=true;model.firmwareTimeout=value;}
        else if(result==EFI_NOT_FOUND) {model.timeoutKnown=true;model.firmwareTimeout=0;timeoutAttributes=7;}
        else model.timeoutKnown=false;
        firmwareTimeoutSaved=model.firmwareTimeout;
        return EFI_SUCCESS;
    };
    // Writes only when BootOrder still equals the page's snapshot; Charon's
    // own entry first, or (r385, check box off) last, so firmware starts the
    // first other entry and only the resident voice driver reads the screen.
    // Read back before reporting success.
    auto saveOrder=[&]()->efi_status {
        uint16_t live[maximumOrder];size_t size=sizeof(live);
        auto result=systemTable->RuntimeServices->GetVariable((char16_t*)u"BootOrder",&globalGuid,nullptr,&size,live);
        if(result==EFI_NOT_FOUND)size=0;
        else if(result!=EFI_SUCCESS)return result;
        if(size!=orderSnapshotBytes || !Same((const uint8_t*)live,(const uint8_t*)orderSnapshot,size))return EFI_ACCESS_DENIED;
        uint16_t next[maximumOrder];size_t count=0;
        if(model.charonFirst && currentInOrder)next[count++]=current;
        for(size_t i=0;i<model.orderCount && count<maximumOrder;++i)next[count++]=model.order[i];
        if(!model.charonFirst) {
            // Without a resident reader the next start would be silent.
            uint16_t voiceId;if(FindFirmwareVoice(voiceId)!=EFI_SUCCESS)return EFI_NOT_STARTED;
            if(FirstNormal(model.order,model.orderCount)>=model.orderCount)return EFI_NOT_FOUND;   // nothing else to start
            if(currentInOrder && count<maximumOrder)next[count++]=current;
        }
        if(!count)return EFI_NOT_FOUND;
        result=systemTable->RuntimeServices->SetVariable((char16_t*)u"BootOrder",&globalGuid,orderAttributes,count*2,next);
        if(result!=EFI_SUCCESS)return result;
        size=sizeof(live);
        if(systemTable->RuntimeServices->GetVariable((char16_t*)u"BootOrder",&globalGuid,nullptr,&size,live)!=EFI_SUCCESS
            || size!=count*2 || !Same((const uint8_t*)live,(const uint8_t*)next,size))return EFI_DEVICE_ERROR;
        return loadOrder();
    };
    // Speak the farewell, let it finish (bounded), stop audio, then reset.
    // ResetSystem does not return on success; a return is reported.
    auto powerOff=[&](efi_reset_type type,const char16_t* text)->efi_status {
        announce(text);
        if(voice && !speechFailed)for(unsigned i=0;i<800;++i) {
            if(effects && effects->SpeechIdle) {if(i>20 && effects->SpeechIdle(effects->context))break;}
            else if(i>=300)break;
            if(voice->Pump(voice,480)!=EFI_SUCCESS)break;
            boot->Stall(10000);
        }
        const auto stopped=stop();
        if(stopped!=EFI_SUCCESS)return stopped;
        if(!systemTable->RuntimeServices->ResetSystem)return EFI_UNSUPPORTED;
        systemTable->RuntimeServices->ResetSystem(type,EFI_SUCCESS,0,nullptr);
        return EFI_DEVICE_ERROR;
    };
    // Preboot narration, resident screen reading while the program runs, and
    // the postboot result once it returns. Verbosity off keeps the r354 path:
    // audio is stopped before the transfer and nothing reads the screen.
    auto launch=[&](uint16_t id,const uint8_t* bytes,size_t size,efi_status (*gate)(void*),void* gateContext) {
        MenuEntry entry{id,const_cast<uint8_t*>(bytes),size};char16_t name[300]={};EntryLabel(entry,pl,name,300);
        // r386: programs start on a clear screen with the cursor at the top
        // left, as the firmware's boot manager leaves it. Charon's menu lines
        // fill whole rows, so the cursor sat in the last column and the
        // program's first letter ended a row alone ("i" / "PXE initialising").
        if(systemTable->ConOut && systemTable->ConOut->ClearScreen)systemTable->ConOut->ClearScreen(systemTable->ConOut);
        BootAttemptStage stage=BootAttemptStage::validation;efi_status result;
        LaunchSpeech speech;speech.voice=voice;speech.effects=effects;speech.language=language;speech.pl=pl;
        speech.explicitMessages=settings.explicitMessages;speech.verbosity=settings.verbosity;speech.progress=settings.progress;
        speech.speechOk=voice && !speechFailed && settings.verbosity;
        if(!speech.speechOk) {
            result=stop();
            if(result!=EFI_SUCCESS) {problem(BootProblem::audioStop,result);return;}
            result=StartVerified(self,current,id,bytes,size,stage,gate,gateContext);
        } else {
            result=CleanupCharonShell();
            if(result!=EFI_SUCCESS) {problem(BootProblem::audioStop,result);return;}
            char16_t text[900]={};
            Append(text,900,pl?u"Uruchamiam: ":u"Starting: ");Append(text,900,name);Append(text,900,u".");
            if(settings.verbosity>=2)AppendLoaderFile(entry,pl,text,900);
            // r382: no waiting for "Starting: ..." to end. With short messages
            // the reader takes the audio at once and the program's own screen
            // interrupts the announcement; detailed messages replace it as
            // soon as the program has loaded.
            // r387: with normal messages the program's own screen speaks first
            // (the user: no "Starting: ..."); detailed messages keep it.
            if(settings.verbosity>=2)LaunchSay(speech,text,0);
            if(settings.verbosity<2)LaunchActivateReader(speech);
            LaunchNarration narration{&speech,LaunchAfterLoad,LaunchBeforeStart};
            result=StartVerified(self,current,id,bytes,size,stage,gate,gateContext,&narration);
            // Keys the program read through another input interface may still
            // be queued here; they belonged to the program, not to Charon.
            if(input->Reset)input->Reset(input,false);
            if(speech.driverResumed)residentDriver->Suspend(residentDriver);
            ApplyVoiceProfile(effects,settings.rate,settings.pitch,settings.speechVolume,settings.effectsVolume);  // back to the menu's voice
            if(speech.resident)WithdrawAppReaderControl();
            if(speech.resident && residentReader.Uninstall()!=EFI_SUCCESS)
                Say(settings.explicitMessages?(pl?u"Charon: ten szajs podmienił mi konsolę, kurwa; zostawiam jego wskaźnik.\r\n":u"Charon: this crap swapped my fucking console; leaving its pointer.\r\n")
                    :(pl?u"Charon: program podmienił konsolę; jej wskaźnik pozostawiono.\r\n":u"Charon: the program replaced the console; its pointer was kept.\r\n"));
        }
        auto* gateState=static_cast<EfiAutoContinue*>(gateContext);
        if(gate && gateState && gateState->cancelled)return;
        if(stage==BootAttemptStage::returned && result==EFI_SUCCESS) {
            char16_t text[400]={};Append(text,400,pl?u"Program „":u"The program “");Append(text,400,name);
            Append(text,400,pl?u"” zakończył działanie i oddał sterowanie Charonowi.":u"” finished and returned control to Charon.");
            popup(text);
        } else problem(AttemptProblem(stage,uint64_t(result)),result);
        render=true;
    };
    // Read the durable Continue snapshot once. No menu rendering or settings
    // action can overwrite continueBytes before StartVerified checks it again.
    PresentedMenuCues presented;
    uint16_t autoId=0;size_t autoSize=0;
    // A one-time target wins once and is consumed before use; an undeletable
    // file is ignored so it can never repeat.
    bool once=false;
    if(ReadContinue(root,autoId,autoSize,onceTargetName)==EFI_SUCCESS && ConsumeOnce(root)==EFI_SUCCESS)once=true;
    status=once?EFI_SUCCESS:ReadContinue(root,autoId,autoSize);
    bool fallback=false;   // r396: no saved target - the firmware's next boot option
    if(status==EFI_NOT_FOUND && !once && FallbackContinue(current,autoId,autoSize,&inventory)==EFI_SUCCESS) {status=EFI_SUCCESS;fallback=true;}
    if(status==EFI_SUCCESS && !(fallback && autoId==mediaEntryId))status=ValidateContinueTarget(current,autoId,continueBytes+14,autoSize);
    const bool readerOnly=ReaderOnly(root);   // r391
    const bool neverAuto=settings.autoSeconds==autoNever && !once && !readerOnly;
    const uint8_t welcome=readerOnly?0:settings.welcome;
    presented.welcomeSound=(welcome&welcomeSound)!=0;
    if(neverAuto && status==EFI_SUCCESS) {
        announce((welcome&welcomeSpeech)?(pl?u"Witaj w Charon. Menu opcji.":u"Welcome to Charon. Options menu."):(pl?u"Menu opcji.":u"Options menu."));
        presented.Present(MenuPage::main,settings.sounds,cue);presented.RequestMainMenu();sayFocus=true;
    } else if(status!=EFI_SUCCESS) {
        problem(status==EFI_NOT_FOUND?(ContinueWasConfigured(root)?BootProblem::continueMissing:BootProblem::nothingToStart):
            status==EFI_INVALID_PARAMETER?BootProblem::continueInvalid:BootProblem::continueRead,status);
    } else if(speechFailed) {
        problem(BootProblem::audioStop,EFI_DEVICE_ERROR);
    } else {
        efi_event deadline=nullptr;
        status=boot->CreateEvent(EVT_TIMER,TPL_APPLICATION,nullptr,nullptr,&deadline);
        const uint64_t seconds=readerOnly?0:settings.autoSeconds==autoNever?10:settings.autoSeconds;
        if(status==EFI_SUCCESS)status=boot->SetTimer(deadline,TimerRelative,seconds?seconds*10000000ull:1);
        if(status!=EFI_SUCCESS)problem(BootProblem::continueRead,status);
        else {
            char16_t prompt[512]={};
            Append(prompt,512,pl?u"Witaj w Charon. ":u"Welcome to Charon. ");
            if(once) {MenuEntry entry{autoId,continueBytes+14,autoSize};char16_t name[300]={};EntryLabel(entry,pl,name,300);
                Append(prompt,512,pl?u"Jednorazowy rozruch: ":u"One-time start: ");Append(prompt,512,name);Append(prompt,512,u". ");}
            char16_t fallbackText[360]={};
            if(fallback) {MenuEntry entry{autoId,continueBytes+14,autoSize};char16_t name[300]={};EntryLabel(entry,pl,name,300);
                Append(fallbackText,360,pl?u"Uruchamiam: ":u"Starting: ");Append(fallbackText,360,name);Append(fallbackText,360,u". ");
                Append(prompt,512,fallbackText);}
            Append(prompt,512,pl?u"Naciśnij spację, aby otworzyć menu opcji":u"Press Space to open the options menu");
            // r382: the Welcome setting decides what is spoken; the console
            // line keeps the whole prompt.
            char16_t spoken[512]={};
            if(welcome&welcomeSpeech)Append(spoken,512,pl?u"Witaj w Charon. ":u"Welcome to Charon. ");
            if(once) {const char16_t* rest=prompt;const char16_t* greet=pl?u"Witaj w Charon. ":u"Welcome to Charon. ";
                size_t k=0;while(greet[k] && rest[k]==greet[k])++k;rest+=k;
                const char16_t* hint=pl?u"Naciśnij spację":u"Press Space";size_t cut=0;
                for(size_t i=0;rest[i];++i) {size_t m=0;while(hint[m] && rest[i+m]==hint[m])++m;if(!hint[m]) {cut=i;break;}}
                char16_t part[400]={};for(size_t i=0;i<cut && i<399;++i)part[i]=rest[i];Append(spoken,512,part);}
            if(fallback)Append(spoken,512,fallbackText);
            if(welcome&welcomeHint)Append(spoken,512,pl?u"Naciśnij spację, aby otworzyć menu opcji":u"Press Space to open the options menu");
            if(synthFailed) {char16_t why[400]={};SynthMessage(false,synthReport,pl,why,400);Append(spoken,512,u" ");Append(spoken,512,why);}   // r393
            {size_t end=0;while(spoken[end])++end;while(end && spoken[end-1]==u' ')spoken[--end]=0;}
            if((settings.autoSeconds || once) && spoken[0] && !readerOnly)announce(spoken);
            auto* console=systemTable->ConOut;
            if(console && console->OutputString && console->OutputString(console,(char16_t*)prompt)==EFI_SUCCESS) {
                presented.Present(MenuPage::main,settings.sounds,cue);
                Say(u"\r\n");
            }
            if(speechFailed)problem(BootProblem::audioStop,EFI_DEVICE_ERROR);
            else {
                EfiAutoContinue gate{boot,input,voice,timer,deadline};
                gate.effects=effects;gate.sounds=settings.sounds;gate.timers=settings.timers;
                if(boot->CreateEvent(EVT_TIMER,TPL_APPLICATION,nullptr,nullptr,&gate.second)!=EFI_SUCCESS
                    || boot->SetTimer(gate.second,TimerPeriodic,10000000)!=EFI_SUCCESS) {if(gate.second)boot->CloseEvent(gate.second);gate.second=nullptr;}
                const auto result=WaitAutoContinue(gate);
                if(gate.second) {boot->CloseEvent(gate.second);gate.second=nullptr;}
                if(result==AutoContinueResult::handoff) {
                    residentQuickBoot=true;DrainKeys(systemTable);launch(autoId,continueBytes+14,autoSize,EfiAutoContinue::BeforeTransfer,&gate);residentQuickBoot=false;
                    if(gate.cancelled) {presented.RequestMainMenu();sayFocus=true;}
                } else if(result==AutoContinueResult::failed)problem(BootProblem::audioStop,gate.error);
                else {presented.RequestMainMenu();sayFocus=true;}
            }
        }
        if(deadline)boot->CloseEvent(deadline);
    }
    MessageModeSequence messageSequence;
    [[maybe_unused]] size_t mediaTicks=0,mediaRounds=0;[[maybe_unused]] uint64_t mediaSig=MediaSignature(boot,false);
    // r374: idle taunts in explicit mode, every 20 s without a key.
    // r390: hints after 10 s without a key (each once per run, message voice); taunts every second time (20 s).
    efi_event idle=nullptr;constexpr uint64_t idlePeriod=100000000ull; // 10 s in 100 ns units
    size_t hintNext=0,idleRounds=0;
    static const keyhelp::Entry hints[]={
        {u"Porada: aby otworzyć listę klawiszy, naciśnij Insert F1 albo Alt F1.",u"Hint: to open the key list, press Insert F1 or Alt F1."},
        {u"Porada: w ustawieniach Charona, w ustawieniach czytnika ekranu, zmienisz interpunkcję, język tekstu ekranu, wielkie litery i echo klawiszy.",
         u"Hint: Charon settings, Screen reader settings, change the symbol level, screen text language, capitals and keyboard echo."},
        {u"Porada: po uruchomieniu programu Alt ukośnik wsteczny wstrzymuje rozruch, a strzałki czytają ekran.",u"Hint: once a program runs, Alt backslash pauses the boot and the arrows read the screen."},
        {u"Porada: sam numpad przy wyłączonym NumLocku przegląda ekran, tak jak w NVDA.",u"Hint: the numpad alone with NumLock off reviews the screen, as in NVDA."},
        {u"Porada: Control Alt H mówi wszystkie skróty czytnika jednym ciągiem.",u"Hint: Control Alt H says all reader shortcuts in one go."}};
    if(timer && (boot->CreateEvent(EVT_TIMER,TPL_APPLICATION,nullptr,nullptr,&idle)!=EFI_SUCCESS || boot->SetTimer(idle,TimerRelative,idlePeriod)!=EFI_SUCCESS)) {
        if(idle)boot->CloseEvent(idle);
        idle=nullptr;}
    while(!done) {
        if(render) {
            const bool failedBeforeSpeech=speechFailed;
            if(model.page==MenuPage::devices && inventory.count)selected=model.index;
            char16_t label[512]={};
            // r399 (user): Charon's OWN menu also gets voice formatting - the
            // position in the list maps to modulation (pitch range), like screen
            // rows do for the resident reader, so navigating the menu is not
            // monotone. Guarded by flagVoice (formatFlags bit 0); the marker is
            // the reader's ESC 'f' + 12-byte payload, parsed by the voice engine.
            if((settings.formatFlags&1u) && model.page!=MenuPage::reader) {
                int rangeStep=4-int(model.index)*8/12;if(rangeStep<-4)rangeStep=-4;if(rangeStep>4)rangeStep=4;
                char16_t m[16];size_t w=0;
                m[w++]=0x001b;m[w++]=u'f';
                m[w++]=u'm';m[w++]=u'm';m[w++]=u'm';m[w++]=u'0';m[w++]=u'-';m[w++]=u'a';
                m[w++]=char16_t(u'm'+rangeStep);m[w++]=u'm';m[w++]=u'm';m[w++]=u'0';m[w++]=u'-';m[w++]=u'-';m[w++]=u' ';
                for(size_t i=0;i<w;++i)label[i]=m[i];label[w]=0;
                labelFor(model.index,label+w,512-w);
            } else labelFor(model.index,label,512);
            // Replace speech before potentially slow firmware console painting.
            // The model has already accepted the key; an old utterance must
            // not continue merely because the GOP console redraw is expensive.
            if(sayFocus)announce(label,settings.explicitMessages);  // labelFor already made every option explicit
            // r387: the file browser names the volume and folder it shows.
            char16_t filesTitle[200]={};
            if(model.page==MenuPage::infoList && systools::SharedList().kind==systools::ListKind::files) {
                const auto& browser=files::Shared();
                Append(filesTitle,200,pl?u"Uruchom program z pliku":u"Start a program from a file");
                if(browser.volume<browser.volumeCount) {
                    Append(filesTitle,200,u": ");ConsoleNumber(filesTitle,200,browser.volume+1);Append(filesTitle,200,u" ");Append(filesTitle,200,browser.path);}
                else Append(filesTitle,200,pl?u": woluminy":u": volumes");
            }
            const char16_t* page=model.page==MenuPage::main ? (pl?u"Menu główne":u"Main menu")
                : model.page==MenuPage::devices ? (pl?u"Urządzenia rozruchowe":u"Boot devices")
                : model.page==MenuPage::settings ? (pl?u"Ustawienia":u"Settings")
                : model.page==MenuPage::language ? (pl?u"Język, okno wyboru":u"Language popup")
                : model.page==MenuPage::startMelody ? (pl?u"Melodia startu":u"Start melody")
                : model.page==MenuPage::backgroundMelody ? (pl?u"Melodia w tle":u"Background melody")
                : model.page==MenuPage::bootOrder ? (pl?u"Kolejność rozruchu (Charon zawsze pierwszy)":u"Boot order (Charon always first)")
                : model.page==MenuPage::verbosity ? (pl?u"Komunikaty rozruchu, okno wyboru":u"Boot messages popup")
                : model.page==MenuPage::autoDelay ? (pl?u"Czas autostartu, okno wyboru":u"Auto-continue time popup")
                : model.page==MenuPage::progress ? (pl?u"Paski postępu, okno wyboru":u"Progress bars popup")
                : model.page==MenuPage::welcome ? (pl?u"Powitanie, lista":u"Welcome, list")
                : model.page==MenuPage::reader ? (pl?u"Ustawienia czytnika ekranu (mowa po starcie programu)":u"Screen reader settings (speech once a program starts)")
                : model.page==MenuPage::detail ? (pl?u"Szczegółowość":u"Detail")
                : model.page==MenuPage::formatting ? (pl?u"Formatowanie tekstu":u"Text formatting")
                : model.page==MenuPage::keyList ? (pl?u"Lista klawiszy czytnika":u"Reader key list")
                : model.page==MenuPage::tools ? (pl?u"Narzędzia systemowe":u"System tools")
                : model.page==MenuPage::clockEdit ? (model.clockWake?(pl?u"Budzenie komputera":u"Wake-up alarm"):(pl?u"Ustaw datę i godzinę":u"Set date and time"))
                : model.page==MenuPage::infoList && systools::SharedList().kind==systools::ListKind::files ? filesTitle
                : model.page==MenuPage::infoList ? (systools::SharedList().kind==systools::ListKind::drivers?(pl?u"Sterowniki UEFI (tylko odczyt)":u"UEFI drivers (read only)"):(pl?u"Zmienne firmware (tylko odczyt)":u"Firmware variables (read only)"))
                : model.page==MenuPage::notice ? (pl?u"Komunikat":u"Message") : (pl?u"Potwierdzenie":u"Confirmation");
            const auto* audio=(!voice || speechFailed) ? (pl?u"Brak działającego syntezatora; tylko konsola.":u"Live speech unavailable; console only.")
                : !effectsAvailable ? (pl?u"Odtwarzanie dźwięków interfejsu niedostępne.":u"UI sound playback unavailable.") : u"";
            const auto* keys=model.page==MenuPage::notice ? (pl?u"Enter / Spacja / Esc: zamknij":u"Enter / Space / Esc: close")
                : (model.page==MenuPage::welcome || model.page==MenuPage::detail || model.page==MenuPage::formatting) ? (pl?u"Strzałki: wybór  Spacja: zaznacz / odznacz  Esc: wstecz":u"Arrows: select  Space: check / uncheck  Esc: back")
                : model.page==MenuPage::reader ? (pl?u"Góra / dół: wybór  Lewo / prawo: zmień  Spacja: zaznacz  Esc: wstecz":u"Up / Down: select  Left / Right: change  Space: check  Esc: back")
                : model.page==MenuPage::keyList ? (pl?u"Góra / dół: następny klawisz  Esc: zamknij":u"Up / Down: next key  Esc: close")
                : model.page==MenuPage::bootOrder ? (model.grabbed?(pl?u"Strzałki: przenieś  Enter: upuść  Esc: anuluj":u"Arrows: move  Enter: drop  Esc: cancel")
                    :(pl?u"Enter: chwyć  Strzałki: wybór  Esc: wstecz":u"Enter: grab  Arrows: select  Esc: back"))
                : (pl?u"Strzałki: wybór  Enter: otwórz  Esc: wstecz":u"Arrows: select  Enter: open  Esc: back");
            char16_t displayedStatus[544]={},displayedPopup[1100]={};
            char16_t displayedPage[160]={},displayedAudio[256]={},displayedKeys[256]={};
            {char16_t plainPage[160]={};DisplayRoles(page,plainPage,160);FormatCharonMessage(plainPage,displayedPage,160,settings.explicitMessages,pl);}
            FormatCharonMessage(audio,displayedAudio,256,settings.explicitMessages,pl);
            FormatCharonMessage(keys,displayedKeys,256,settings.explicitMessages,pl);
            FormatCharonMessage(statusText,displayedStatus,544,settings.explicitMessages,pl);
            FormatCharonMessage(popupText,displayedPopup,1100,settings.explicitMessages,pl);
            auto displayLabel=[&](size_t row,char16_t* out,size_t capacity) {
                char16_t spoken[512]={};labelFor(row,spoken,512);DisplayRoles(spoken,out,capacity);
            };
            if(!DrawConsoleMenu(systemTable->ConOut,displayedPage,model.Count(),model.index,displayLabel,displayedStatus,displayedAudio,displayedKeys,
                model.page==MenuPage::notice?displayedPopup:nullptr))
                Say(settings.explicitMessages?(pl?u"Nie da się narysować tego pieprzonego menu.\r\n":u"Cannot render this fucking menu.\r\n"):
                    (pl?u"Nie można narysować menu.\r\n":u"Cannot render console menu.\r\n"));
            else presented.Present(model.page,settings.sounds,cue);
            render=speechFailed!=failedBeforeSpeech;sayFocus=!render;
        }
        efi_event events[3]={input->WaitForKey,timer,idle};size_t event=0;
        status=WaitMenuVoiceEvent(boot,speechFailed?nullptr:voice,events,timer?(idle?3:2):1,1,event,idle?2:SIZE_MAX);
        if(status!=EFI_SUCCESS)break;
        if(event==2) {
            ++idleRounds;
            if((settings.readerFlags&readerFlagHints) && voice && !speechFailed && hintNext<sizeof(hints)/sizeof(hints[0]) && !(settings.explicitMessages && !(idleRounds&1))) {
                const auto& h=hints[hintNext++];char utf8[600];Utf8(pl?h.pl:h.en,utf8,sizeof(utf8));
                if(!(effects && effects->SpeakMessage && effects->SpeakMessage(effects->context,language,utf8)))announce(pl?h.pl:h.en,true);
            }
            else if(settings.explicitMessages && voice && !speechFailed && !(idleRounds&1))announce(IdleTaunt(pl),true);
            boot->SetTimer(idle,TimerRelative,idlePeriod);
            continue;
        }
        if(event==0 && idle)boot->SetTimer(idle,TimerRelative,idlePeriod);
#if !defined(HAK_CHARON_NO_MEDIA) && !defined(HAK_CHARON_NO_MEDIA_TICK)
        if(event==1 && ++mediaTicks>=200 && (model.page==MenuPage::main || model.page==MenuPage::devices)) {
            // r386: announce media plugged in or taken out while the menu is open.
            mediaTicks=0;const uint64_t sig=MediaSignature(boot,++mediaRounds%2==0);
            if(sig!=mediaSig) {
                mediaSig=sig;ConnectRemovable(boot);
                static uint8_t before[32][640];size_t beforeCount=0;
                for(size_t i=inventory.firmwareCount;i<inventory.count && beforeCount<32;++i) {size_t n=inventory.entries[i].size<640?inventory.entries[i].size:640;memset(before[beforeCount],0,640);memcpy(before[beforeCount++],inventory.entries[i].data,n);}
                if(inventory.Refresh(current)==EFI_SUCCESS) {
                    char16_t text[900]={};
                    auto known=[&](const uint8_t* a,size_t n,const uint8_t* const* list,size_t listCount)->bool {
                        for(size_t k=0;k<listCount;++k)if(Same(a,list[k],n<640?n:640))return true;return false;};
                    const uint8_t* nowList[256];size_t nowCount=0;
                    for(size_t i=inventory.firmwareCount;i<inventory.count;++i)nowList[nowCount++]=inventory.entries[i].data;
                    const uint8_t* beforeList[32];for(size_t k=0;k<beforeCount;++k)beforeList[k]=before[k];
                    for(size_t i=inventory.firmwareCount;i<inventory.count;++i)
                        if(!known(inventory.entries[i].data,inventory.entries[i].size,beforeList,beforeCount)) {
                            char16_t name[300]={};EntryLabel(inventory.entries[i],pl,name,300);
                            Append(text,900,pl?u"Nowy nośnik: ":u"New medium: ");Append(text,900,name);Append(text,900,u". ");}
                    for(size_t k=0;k<beforeCount;++k) {
                        bool still=false;for(size_t i=0;i<nowCount && !still;++i)still=Same(before[k],nowList[i],640<inventory.entries[inventory.firmwareCount+i].size?640:inventory.entries[inventory.firmwareCount+i].size);
                        if(!still) {MenuEntry gone{mediaEntryId,before[k],640,0};char16_t name[300]={};EntryLabel(gone,pl,name,300);
                            Append(text,900,pl?u"Odłączono nośnik: ":u"Medium removed: ");Append(text,900,name);Append(text,900,u". ");}
                    }
                    model.devices=inventory.count;if(model.page==MenuPage::devices && model.index>=inventory.count)model.index=inventory.count?inventory.count-1:0;
                    if(text[0]) {charontrace::Value("media change",inventory.count);if(model.page==MenuPage::devices)render=true;announce(text,true);}
                }
            }
        }
#endif
        if(event==1) {
            const auto audioStatus=speechFailed?EFI_SUCCESS:voice->Pump(voice,480);
            if(audioStatus!=EFI_SUCCESS) {
                char16_t detail[192]={};
                Append(detail,192,pl ? u"Błąd syntezy lub urządzenia audio" : u"Speech or audio device error");
                AppendBootStatus(detail,192,audioStatus);
                speechFailed=true;popup(detail);
            }
            continue;
        }
        efi_input_key key{};if(input->ReadKeyStroke(input,&key)!=EFI_SUCCESS)continue;
        ++ExplicitVariety();  // one wording per key press (explicit mode)
        charontrace::Value("menu key",(uint64_t(key.ScanCode)<<32)|key.UnicodeChar);
        if(messageSequence.Feed(key.ScanCode?0:key.UnicodeChar)) {
            CharSettings candidate=settings;candidate.explicitMessages=!settings.explicitMessages;
            status=settingsInvalid?EFI_INVALID_PARAMETER:SaveCharSettings(root,candidate);
            if(status==EFI_SUCCESS) {
                settings=candidate;shareReading();
                if(model.page==MenuPage::notice)model.Key(MenuEvent::back);
                message(settings.explicitMessages?(pl?u"Tryb dosadnych komunikatów włączony.":u"Explicit messages enabled.")
                    :(pl?u"Tryb wulgarny wyłączony.":u"Profanity mode disabled."));
            } else popup(pl?u"Nie potwierdzono zapisu; tryb komunikatów niezmieniony.":u"Save not confirmed; message mode unchanged.");
            continue;
        }
        // r390: F1 (also Insert+F1 and Alt+F1, as in the reader) opens the key list.
        if(key.ScanCode==7 && !key.UnicodeChar)continue;   // Insert: the reader key, F1 follows
        if(key.ScanCode==0x0b && !key.UnicodeChar) {
            if(model.page!=MenuPage::keyList) {model.OpenKeyList(keyhelp::count);render=true;sayFocus=true;}
            else {model.Key(MenuEvent::back);render=true;sayFocus=true;}
            continue;
        }
        const auto before=model;const auto eventKey=DecodeMenuKey(key);
        const auto action=model.Key(eventKey);
        if(action==MenuAction::none)continue;
        const bool settingAction=action==MenuAction::toggleWrap || action==MenuAction::selectLanguage || action==MenuAction::toggleSounds || action==MenuAction::selectMelody || action==MenuAction::changeRate
            || action==MenuAction::changeVolume || action==MenuAction::selectVerbosity || action==MenuAction::toggleRemember || action==MenuAction::selectAutoDelay || action==MenuAction::selectProgress
            || action==MenuAction::toggleWelcome || action==MenuAction::changeReaderVoice || action==MenuAction::toggleDetail || action==MenuAction::toggleTimer;
        if(action==MenuAction::openMelody && !melodiesAvailable) {
            model=before;popup(pl?u"Odtwarzanie melodii niedostępne. Ustawienia niezmienione.":u"Melody playback unavailable. Settings unchanged.");continue;
        }
        if(!settingAction)cue(TransitionCue(before,eventKey,action,model));
        if(action==MenuAction::edge && before.page==MenuPage::settings && before.index==11
            && (eventKey==MenuEvent::left || eventKey==MenuEvent::right)) {
            message(eventKey==MenuEvent::left?(pl?u"Tempo minimalne, 0%":u"Minimum rate, 0%"):(pl?u"Tempo maksymalne, 100%":u"Maximum rate, 100%"));continue;}
        if(action==MenuAction::edge && before.page==MenuPage::settings && (before.index==setSpeechVolume || before.index==setEffectsVolume)
            && (eventKey==MenuEvent::left || eventKey==MenuEvent::right)) {
            message(eventKey==MenuEvent::left?(pl?u"Głośność minimalna":u"Minimum volume"):(pl?u"Głośność maksymalna, 100%":u"Maximum volume, 100%"));continue;}
        if(action==MenuAction::edge && ((before.page==MenuPage::reader && (before.index==readerSynthRow || before.index==readerSynthVoiceRow)) || (before.page==MenuPage::settings && (before.index==setSynth || before.index==setSynthVoice)))) {   // r393, r396
            message((before.page==MenuPage::reader?before.index==readerSynthRow:before.index==setSynth)?(pl?u"Brak innych syntezatorów; dodatki leżą w EFI, HAK, synths":u"No other synthesizers; add-ons go in EFI, HAK, synths")
                :(pl?u"Ten syntezator ma jeden głos":u"This synthesizer has one voice"));continue;}
        if(action==MenuAction::edge && (before.page==MenuPage::reader || (before.page==MenuPage::settings && before.index==setPitch))
            && (eventKey==MenuEvent::left || eventKey==MenuEvent::right)) {
            const bool volume=before.page==MenuPage::reader && before.index==readerVolumeRow;
            message(eventKey==MenuEvent::left?(volume?(pl?u"Głośność minimalna":u"Minimum volume"):(pl?u"Minimum, 0%":u"Minimum, 0%"))
                :(volume?(pl?u"Głośność maksymalna, 100%":u"Maximum volume, 100%"):(pl?u"Maksimum, 100%":u"Maximum, 100%")));continue;}
        if(action==MenuAction::timeoutEdge) {
            message(!model.timeoutKnown?(pl?u"Limit czasu firmware niedostępny":u"Firmware timeout unavailable")
                :eventKey==MenuEvent::left?(pl?u"Limit czasu minimalny, 0 s":u"Minimum timeout, 0 s"):(pl?u"Limit czasu maksymalny, 30 s":u"Maximum timeout, 30 s"));continue;}
        if(action==MenuAction::edge) {message(model.grabbed?(pl?u"Wpis jest już na skraju listy":u"The entry is already at the end of the list"):(pl ? u"Koniec listy" : u"List boundary"));continue;}
        render=true;
        if(settingAction) {
            CharSettings candidate=settings;SettingsFromModel(candidate,model);
            status=settingsInvalid?EFI_INVALID_PARAMETER:SaveCharSettings(root,candidate);
            if(status==EFI_SUCCESS) {
                settings=candidate;pl=PolishSettings(settings,locale);language[0]=pl?'p':'e';language[1]=pl?'l':'n';shareReading();inventory.pl=pl;
                if(effectsAvailable)effects->SetEnabled(effects->context,settings.sounds);
                ApplyVoiceProfile(effects,settings.rate,settings.pitch,settings.speechVolume,settings.effectsVolume);
                if(action==MenuAction::selectMelody && melodiesAvailable)
                    effects->SetMelodies(effects->context,settings.startMelody,settings.backgroundMelody);
                if((before.synthKey!=model.synthKey || before.synthVoice!=model.synthVoice) && effects && effects->SetSynth) {
                    char16_t report[200]={};const bool ok=effects->SetSynth(effects->context,model.synthKey,model.synthVoice,report,200);
                    char16_t text[400]={};SynthMessage(ok,report,pl,text,400);if(ok)message(text);else popup(text);   // r396: no modal window on success
                }
                cue(TransitionCue(before,eventKey,action,model));
                statusText[0]=0;Append(statusText,512,pl?u"Ustawienia zapisane":u"Settings saved");
            } else {ModelFromSettings(model,settings);
                popup(pl?u"Nie potwierdzono zapisu; bieżące ustawienia niezmienione.":u"Save not confirmed; current settings unchanged.");}
        } else if(action==MenuAction::openDevices && !inventory.count) {
            problem(inventoryStatus==EFI_SUCCESS?BootProblem::emptyInventory:BootProblem::inventoryRead,
                inventoryStatus,inventoryStatus!=EFI_SUCCESS);
        } else if(action==MenuAction::openFirmware) {
            status=RunHiiMenu(systemTable,speechFailed?nullptr:voice,effects,pl,settings.sounds,settings.explicitMessages);
            render=true;sayFocus=true;
            if(status!=EFI_SUCCESS)popup(pl?u"Przeglądarka ustawień UEFI zakończyła się błędem.":u"UEFI settings browser returned an error.");
        } else if(action==MenuAction::openShell) {
            status=StartCharonShell(boot,self,root,voice);
            char16_t result[256]={};Append(result,256,status==EFI_SUCCESS?(pl?u"Powrót z powłoki UEFI.":u"Returned from UEFI Shell."):(pl?u"Nie udało się uruchomić lub zamknąć powłoki UEFI.":u"UEFI Shell launch or cleanup failed."));
            if(status!=EFI_SUCCESS)AppendBootStatus(result,256,status);
            popup(result);
        } else if(action==MenuAction::continueBoot) {
            uint16_t id;size_t size;status=ReadContinue(root,id,size);
            if(status==EFI_NOT_FOUND && FallbackContinue(current,id,size,&inventory)==EFI_SUCCESS)status=EFI_SUCCESS;   // r396
            if(status==EFI_NOT_FOUND && !ContinueWasConfigured(root)) {
                status=stop();if(status==EFI_SUCCESS) {done=true;returnToFirmware=true;}
                else problem(BootProblem::audioStop,status);
            } else if(status!=EFI_SUCCESS) {
                problem(status==EFI_NOT_FOUND?BootProblem::continueMissing:
                    status==EFI_INVALID_PARAMETER?BootProblem::continueInvalid:BootProblem::continueRead,status);
            } else {
                residentQuickBoot=true;DrainKeys(systemTable);launch(id,continueBytes+14,size,nullptr,nullptr);residentQuickBoot=false;
            }
        } else if(action==MenuAction::startDevice) {
            if(model.index>=inventory.count)problem(BootProblem::noSelection,EFI_NOT_FOUND,false);
            else if(!inventory.Unchanged())problem(BootProblem::targetChanged,EFI_NOT_FOUND,false);
            else {
                auto& entry=inventory.entries[model.index];
                if(entry.source>=2 && entry.source!=4) {
                    // r386: BIOS-only media cannot be started from UEFI.
                    char16_t name[300]={};EntryLabel(entry,pl,name,300);char16_t text[900]={};
                    Append(text,900,name);
                    Append(text,900,settings.explicitMessages
                        ?(pl?u". Ten złom startuje tylko w trybie BIOS, a ja siedzę w UEFI, więc chuja z tego. Włącz CSM (tryb zgodności) w ustawieniach firmware albo weź wersję UEFI tego systemu, geniuszu."
                            :u". This junk boots only in BIOS mode and I live in UEFI, so fuck all. Turn on CSM (compatibility mode) in firmware setup or get the UEFI version, genius.")
                        :(pl?u". Ten nośnik uruchamia się tylko w trybie BIOS. Charon działa w UEFI i nie może go uruchomić. Włącz tryb zgodności CSM w ustawieniach firmware albo użyj wersji systemu dla UEFI."
                            :u". This medium starts only in BIOS mode. Charon runs in UEFI and cannot start it. Turn on CSM (compatibility mode) in firmware setup or use the UEFI version of the system."));
                    popup(text);continue;
                }
                // Remember: the same verified Continue record the menu writes.
                if(entry.source==0 && settings.rememberLast && SaveContinue(root,entry)!=EFI_SUCCESS)
                    Say(settings.explicitMessages?(pl?u"Charon: chuja, nie zapamiętałem wyboru, sklerotyku.\r\n":u"Charon: fuck, didn’t remember your choice, goldfish.\r\n")
                        :(pl?u"Charon: nie zapamiętano wyboru.\r\n":u"Charon: the choice was not remembered.\r\n"));
                launch(entry.id,entry.data,entry.size,nullptr,nullptr);
            }
        } else if(action==MenuAction::refresh) {
            status=inventory.Refresh(current);inventoryStatus=status;model.devices=inventory.count;selected=0;
            if(status!=EFI_SUCCESS)problem(BootProblem::inventoryRead,status);
            else if(!inventory.count)problem(BootProblem::emptyInventory,status,false);
            else message(pl ? u"Urządzenia odświeżone" : u"Devices refreshed");
        } else if(action==MenuAction::firmwareInfo) {
            uint8_t secure=0;size_t size=1;
            status=systemTable->RuntimeServices->GetVariable((char16_t*)u"SecureBoot",&globalGuid,nullptr,&size,&secure);
            popup(status!=EFI_SUCCESS || size!=1 || secure>1 ? (pl ? u"Stan Secure Boot nieznany" : u"Secure Boot status unknown")
                : secure ? (pl ? u"Secure Boot włączony" : u"Secure Boot enabled") : (pl ? u"Secure Boot wyłączony" : u"Secure Boot disabled"));
        } else if(action==MenuAction::openDevices) {
            // Focus starts on the Continue target, e.g. the remembered choice.
            uint16_t id;size_t size;
            if(ReadContinue(root,id,size)==EFI_SUCCESS)
                for(size_t i=0;i<inventory.count;++i)if(inventory.entries[i].id==id) {model.index=i;break;}
        } else if(action==MenuAction::openBootOrder) {
            status=loadOrder();
            if(status!=EFI_SUCCESS) {model.page=MenuPage::main;model.index=mainOrder;problem(BootProblem::inventoryRead,status);}
            else {char16_t intro[700]={};
                Append(intro,700,model.charonFirst?(pl?u"Kolejność rozruchu. Charon pierwszy. ":u"Boot order. Charon first. "):(pl?u"Kolejność rozruchu. Charon nie jest pierwszy; ekran czyta sterownik głosu. ":u"Boot order. Charon is not first; the voice driver reads the screen. "));
                Append(intro,700,pl?u"Enter chwyta wpis, strzałki go przenoszą, Enter upuszcza. ":u"Enter grabs an entry, arrows move it, Enter drops it. ");
                char16_t first[512]={};labelFor(model.index,first,512);Append(intro,700,first);
                message(intro);}
        } else if(action==MenuAction::grabEntry || action==MenuAction::moveEntry || action==MenuAction::dropEntry || action==MenuAction::grabCancelled) {
            char16_t text[600]={},name[300]={};OrderLabel(model.order[model.index],pl,name,300);
            Append(text,600,action==MenuAction::grabEntry?(pl?u"Chwycono: ":u"Grabbed: "):action==MenuAction::dropEntry?(pl?u"Upuszczono: ":u"Dropped: ")
                :action==MenuAction::grabCancelled?(pl?u"Anulowano przenoszenie: ":u"Move cancelled: "):u"");
            Append(text,600,name);
            Append(text,600,pl?u", pozycja ":u", position ");ConsoleNumber(text,600,model.index+1);
            Append(text,600,pl?u" z ":u" of ");ConsoleNumber(text,600,model.orderCount);
            if(action==MenuAction::grabEntry)Append(text,600,pl?u". Strzałki przenoszą, Enter upuszcza, Escape anuluje.":u". Arrows move, Enter drops, Escape cancels.");
            if(action==MenuAction::dropEntry && model.orderDirty)Append(text,600,pl?u". Wybierz Zapisz kolejność, aby zachować.":u". Choose Save order to keep it.");
            message(text);
        } else if(action==MenuAction::revertOrder) {
            status=loadOrder();
            if(status!=EFI_SUCCESS)problem(BootProblem::inventoryRead,status);
            else message(pl?u"Przywrócono kolejność z firmware.":u"Firmware order restored.");
        } else if(action==MenuAction::leaveOrder) {
            message(pl?u"Niezapisane zmiany kolejności odrzucone.":u"Unsaved order changes discarded.");
        } else if(action==MenuAction::changeTimeout) {
            uint16_t value=model.firmwareTimeout;
            status=systemTable->RuntimeServices->SetVariable((char16_t*)u"Timeout",&globalGuid,timeoutAttributes,2,&value);
            uint16_t check=0;size_t size=2;
            if(status==EFI_SUCCESS && (systemTable->RuntimeServices->GetVariable((char16_t*)u"Timeout",&globalGuid,nullptr,&size,&check)!=EFI_SUCCESS || size!=2 || check!=value))status=EFI_DEVICE_ERROR;
            if(status!=EFI_SUCCESS) {model.firmwareTimeout=firmwareTimeoutSaved;popup(pl?u"Firmware nie przyjęło limitu czasu; wartość niezmieniona.":u"Firmware rejected the timeout; value unchanged.");}
            else firmwareTimeoutSaved=value;
        } else if(action==MenuAction::systemInfo) {
            char16_t text[1024]={};systools::ComputerInfo(systemTable,pl,settings.verbosity>=2,text,1024);popup(text);
        } else if(action==MenuAction::openClock) {
            status=systools::ReadClockFields(systemTable->RuntimeServices,model.clockYear,model.clockMonth,model.clockDay,model.clockHour,model.clockMinute);
            if(status!=EFI_SUCCESS) {model.page=MenuPage::tools;model.index=toolSetClock;
                char16_t text[256]={};Append(text,256,pl?u"Firmware nie podało czasu zegara":u"Firmware did not return the clock time");AppendBootStatus(text,256,status);popup(text);}
            else {char16_t intro[400]={};
                Append(intro,400,pl?u"Ustaw datę i godzinę zegara firmware. Strzałki w lewo i w prawo zmieniają wartość, Zapisz na końcu listy. ":u"Set the firmware clock. Left and Right change a value, Save is at the end of the list. ");
                char16_t first[200]={};labelFor(0,first,200);Append(intro,400,first);message(intro);}
        } else if(action==MenuAction::openWake) {
            char16_t state[300]={};status=systools::WakeText(systemTable->RuntimeServices,pl,state,300);
            if(status!=EFI_SUCCESS) {model.page=MenuPage::tools;model.index=toolWake;if(!state[0]) {Append(state,300,pl?u"Nie odczytano budzenia":u"Wake-up alarm not read");AppendBootStatus(state,300,status);}popup(state);}
            else {
                bool enabled,pending;efi_time wake;systools::WakeState(systemTable->RuntimeServices,enabled,pending,wake);
                if(enabled && wake.Month>=1 && wake.Month<=12 && wake.Day>=1 && wake.Hour<24 && wake.Minute<60) {model.clockYear=wake.Year<2000?2000:wake.Year>2099?2099:wake.Year;model.clockMonth=wake.Month;model.clockDay=wake.Day;model.clockHour=wake.Hour;model.clockMinute=wake.Minute;}
                else if(systools::ReadClockFields(systemTable->RuntimeServices,model.clockYear,model.clockMonth,model.clockDay,model.clockHour,model.clockMinute)==EFI_SUCCESS) {
                    // Suggest five minutes from now (firmware usually accepts only the next 24 hours).
                    unsigned total=unsigned(model.clockHour)*60+model.clockMinute+5;if(total>23*60+59)total=23*60+59;
                    model.clockHour=uint8_t(total/60);model.clockMinute=uint8_t(total%60);
                }
                char16_t intro[600]={};Append(intro,600,state);
                Append(intro,600,pl?u" Strzałki w lewo i w prawo zmieniają wartość; na końcu Ustaw budzenie i Wyłącz budzenie. ":u" Left and Right change a value; Set alarm and Turn the alarm off are at the end. ");
                char16_t first[200]={};labelFor(0,first,200);Append(intro,600,first);message(intro);}
        } else if(action==MenuAction::saveWake || action==MenuAction::disableWake) {
            model.Confirm(action);char16_t confirmation[512]={};
            if(action==MenuAction::disableWake)Append(confirmation,512,pl?u"Wyłączyć budzenie komputera?":u"Turn the wake-up alarm off?");
            else {Append(confirmation,512,pl?u"Ustawić budzenie komputera na: ":u"Set the wake-up alarm to: ");
                systools::DateWords(pl,model.clockYear,model.clockMonth,model.clockDay,model.clockHour,model.clockMinute,confirmation,512);
                Append(confirmation,512,pl?u"? Komputer wyłączony przez system może się wtedy włączyć sam.":u"? A computer switched off by the system may then turn itself on.");}
            Append(confirmation,512,pl?u" Domyślnie wybrano Anuluj.":u" Cancel is selected by default.");message(confirmation);
        } else if(action==MenuAction::connectAll) {
            size_t connected=0;status=systools::ConnectAll(boot,connected);
            if(status==EFI_SUCCESS) {const auto refreshed=inventory.Refresh(current);inventoryStatus=refreshed;model.devices=inventory.count;selected=0;
                char16_t text[300]={};Append(text,300,pl?u"Sterowniki połączone. Urządzenia rozruchowe: ":u"Drivers connected. Boot devices: ");ConsoleNumber(text,300,inventory.count);Append(text,300,u".");
                if(refreshed!=EFI_SUCCESS)AppendBootStatus(text,300,refreshed);popup(text);}
            else {char16_t text[200]={};Append(text,200,pl?u"Nie połączono sterowników":u"Drivers not connected");AppendBootStatus(text,200,status);popup(text);}
        } else if(action==MenuAction::clockEdge) {
            message(pl?u"Wartość graniczna":u"Limit reached");
        } else if(action==MenuAction::saveClock) {
            model.Confirm(MenuAction::saveClock);char16_t confirmation[512]={};
            Append(confirmation,512,pl?u"Ustawić zegar firmware na: ":u"Set the firmware clock to: ");
            systools::DateWords(pl,model.clockYear,model.clockMonth,model.clockDay,model.clockHour,model.clockMinute,confirmation,512);
            Append(confirmation,512,pl?u"? Systemy operacyjne mogą traktować ten zegar jako czas UTC albo lokalny. Domyślnie wybrano Anuluj.":u"? Operating systems may treat this clock as UTC or local time. Cancel is selected by default.");
            message(confirmation);
        } else if(action==MenuAction::openVariables || action==MenuAction::openDrivers) {
            auto& list=systools::SharedList();const bool variables=action==MenuAction::openVariables;
            status=variables?systools::LoadVariables(systemTable->RuntimeServices,pl,list):systools::LoadDrivers(boot,pl,list);
            if(status!=EFI_SUCCESS) {model.page=MenuPage::tools;model.index=model.listReturn;
                char16_t text[256]={};Append(text,256,variables?(pl?u"Nie odczytano zmiennych firmware":u"Firmware variables not read"):(pl?u"Nie odczytano sterowników":u"Drivers not read"));
                AppendBootStatus(text,256,status);popup(text);}
            else {model.listCount=list.count;model.index=0;
                char16_t intro[600]={};
                Append(intro,600,variables?(pl?u"Zmienne firmware, tylko odczyt. ":u"Firmware variables, read only. "):(pl?u"Sterowniki UEFI, tylko odczyt. ":u"UEFI drivers, read only. "));
                ConsoleNumber(intro,600,list.count);{const size_t n=list.count;Append(intro,600,!pl?(n==1?u" item":u" items"):n==1?u" pozycja":(n%10>=2 && n%10<=4 && (n%100<12 || n%100>14))?u" pozycje":u" pozycji");}
                if(list.truncated)Append(intro,600,pl?u", lista niepełna":u", list incomplete");
                Append(intro,600,pl?u". Enter czyta szczegóły, PageUp i PageDown przeskakują o 10. ":u". Enter reads details, Page Up and Page Down jump by 10. ");
                char16_t first[512]={};labelFor(0,first,512);Append(intro,600,first);message(intro);}
        } else if(action==MenuAction::openFiles || action==MenuAction::fileUp
            || (action==MenuAction::listDetail && systools::SharedList().kind==systools::ListKind::files)) {
            // r387: file browser. Volumes -> folders -> *.efi programs.
            auto& list=systools::SharedList();auto& browser=files::Shared();
            char16_t focus[files::nameUnits]={};bool opened=false,started=false;
            if(action==MenuAction::openFiles) {
                status=files::LoadVolumes(boot,inventory.selfDevice,pl,list,browser);opened=status==EFI_SUCCESS;
                if(!opened) {model.page=MenuPage::tools;model.index=model.listReturn;model.fileList=false;
                    char16_t text[256]={};Append(text,256,pl?u"Nie odczytano woluminów":u"Volumes not read");AppendBootStatus(text,256,status);popup(text);continue;}
            } else if(action==MenuAction::fileUp) {
                const size_t volume=browser.volume;
                files::Ascend(browser,focus,files::nameUnits);
                if(browser.volume==SIZE_MAX) {status=files::LoadVolumes(boot,inventory.selfDevice,pl,list,browser);model.fileDepth=0;
                    model.listCount=list.count;model.index=volume<list.count?volume:0;}
                else {status=files::LoadFolder(boot,pl,list,browser);model.fileDepth=model.fileDepth?model.fileDepth-1:0;
                    model.listCount=list.count;model.index=0;
                    for(size_t i=0;i<browser.count;++i)if(browser.entries[i].directory) {
                        size_t k=0;while(focus[k] && browser.entries[i].name[k]==focus[k])++k;
                        if(!focus[k] && !browser.entries[i].name[k]) {model.index=i;break;}}}
                if(status!=EFI_SUCCESS) {char16_t text[256]={};Append(text,256,pl?u"Nie odczytano folderu":u"Folder not read");AppendBootStatus(text,256,status);popup(text);continue;}
                char16_t text[600]={};labelFor(model.index,text,600);message(text);continue;
            } else if(eventKey==MenuEvent::space || (eventKey==MenuEvent::right && browser.volume!=SIZE_MAX && model.index<browser.count && !browser.entries[model.index].directory)) {
                char16_t text[1024]={};files::Detail(boot,browser,list,model.index,pl,text,1024);popup(text);continue;
            } else if(model.index<list.count) {
                if(browser.volume==SIZE_MAX) {
                    browser.volume=model.index;browser.path[0]=u'\\';browser.path[1]=0;
                    status=files::LoadFolder(boot,pl,list,browser);
                    if(status!=EFI_SUCCESS) {browser.volume=SIZE_MAX;files::LoadVolumes(boot,inventory.selfDevice,pl,list,browser);
                        char16_t text[256]={};Append(text,256,pl?u"Nie otwarto woluminu":u"Volume not opened");AppendBootStatus(text,256,status);popup(text);continue;}
                    model.fileDepth=1;opened=true;
                } else if(browser.entries[model.index].directory) {
                    if(!files::Descend(browser,browser.entries[model.index].name)) {popup(pl?u"Ścieżka jest zbyt długa.":u"The path is too long.");continue;}
                    status=files::LoadFolder(boot,pl,list,browser);
                    if(status!=EFI_SUCCESS) {char16_t back[files::nameUnits];files::Ascend(browser,back,files::nameUnits);files::LoadFolder(boot,pl,list,browser);
                        char16_t text[256]={};Append(text,256,pl?u"Nie otwarto folderu":u"Folder not opened");AppendBootStatus(text,256,status);popup(text);continue;}
                    ++model.fileDepth;opened=true;
                } else {
                    // Start the program; the list stays where it was.
                    size_t size=0;uint8_t* data=files::BuildFileOption(boot,browser,browser.entries[model.index].name,size);
                    if(!data) {popup(pl?u"Nie zbudowano ścieżki programu.":u"The program path could not be built.");continue;}
                    const size_t keep=model.index;
                    launchWatchdog=false;launch(mediaEntryId,data,size,nullptr,nullptr);launchWatchdog=true;
                    boot->FreePool(data);started=true;
                    // The program may have changed the disk: read the folder again.
                    files::LoadFolder(boot,pl,list,browser);model.listCount=list.count;model.index=keep<list.count?keep:0;
                }
            }
            if(started)continue;
            if(opened) {
                model.listCount=list.count;model.index=0;
                char16_t intro[700]={};
                if(browser.volume==SIZE_MAX) {
                    Append(intro,700,pl?u"Uruchom program EFI z pliku. Woluminy: ":u"Start an EFI program from a file. Volumes: ");ConsoleNumber(intro,700,list.count);
                    Append(intro,700,pl?u". Enter otwiera wolumin lub folder i uruchamia program, spacja czyta szczegóły, strzałka w lewo wraca wyżej. ":u". Enter opens a volume or folder and starts a program, Space reads details, Left goes up. ");
                } else {
                    // Spoken path: words instead of backslashes.
                    if(!browser.path[1])Append(intro,700,pl?u"Folder główny woluminu. ":u"Volume root folder. ");
                    else {Append(intro,700,pl?u"Folder ":u"Folder ");
                        for(size_t i=1;browser.path[i];++i) {const char16_t c[2]={browser.path[i]==u'\\'?u'/':browser.path[i],0};
                            if(c[0]==u'/')Append(intro,700,u" / ");else Append(intro,700,c);}
                        Append(intro,700,u". ");}
                    size_t folders=0;for(size_t i=0;i<browser.count;++i)folders+=browser.entries[i].directory;
                    Append(intro,700,pl?u"Foldery: ":u"Folders: ");ConsoleNumber(intro,700,folders);
                    Append(intro,700,pl?u", programy EFI: ":u", EFI programs: ");ConsoleNumber(intro,700,browser.count-folders);Append(intro,700,u". ");
                    if(browser.truncated)Append(intro,700,pl?u"Lista niepełna. ":u"List incomplete. ");
                }
                char16_t first[512]={};labelFor(0,first,512);Append(intro,700,first);message(intro);
            }
        } else if(action==MenuAction::listDetail) {
            const auto& list=systools::SharedList();char16_t text[1024]={};
            status=list.kind==systools::ListKind::variables?systools::VariableDetail(systemTable->RuntimeServices,list,model.index,pl,text,1024)
                :systools::DriverDetail(boot,list,model.index,pl,text,1024);
            if(status!=EFI_SUCCESS && status!=EFI_NOT_FOUND)AppendBootStatus(text,1024,status);
            popup(text);
        } else if(action==MenuAction::updatesInfo) {
            char16_t text[1024]={};systools::EsrtText(systemTable,pl,text,1024);popup(text);
        } else if(action==MenuAction::clockInfo) {
            char16_t text[400]={};status=systools::ClockText(systemTable->RuntimeServices,pl,text,400);
            if(status!=EFI_SUCCESS) {text[0]=0;Append(text,400,pl?u"Firmware nie podało czasu zegara":u"Firmware did not return the clock time");AppendBootStatus(text,400,status);}
            popup(text);
        } else if(action==MenuAction::startFirmwareMenu) {
            // r396d (user): the firmware's own setup menu (edk2 UiApp: Boot
            // Manager, Device Manager, Boot Maintenance) started by Charon,
            // so the screen reader keeps reading it; no restart.
            static uint8_t option[2048];size_t optionSize=0;
            if(FirmwareMenuOption(option,sizeof(option),optionSize)==EFI_SUCCESS) {
                residentQuickBoot=true;DrainKeys(systemTable);launch(mediaEntryId,option,optionSize,nullptr,nullptr);residentQuickBoot=false;
            } else popup(pl?u"To firmware nie ma menu ustawień jako programu UEFI (UiApp). Użyj: Uruchom ponownie do ustawień firmware (wtedy czyta je tylko dodatek Mowa od włączenia komputera).":
                u"This firmware has no setup menu as a UEFI program (UiApp). Use: Restart into firmware setup (only the Speech from power-on add-on reads it then).");
        } else if(action==MenuAction::restartFirmware) {
            // r399 (user "czytnik ekranu się nie włącza" after a restart into
            // firmware): a real reboot into the firmware's own setup is read
            // only by the voice driver, and only if the firmware loads it there
            // - many OEM firmwares do not, so it stays silent. When the firmware
            // exposes its setup as a UEFI program (UiApp - edk2/OVMF and modern
            // UEFI), open THAT in this session: Charon reads it reliably, with no
            // restart and no silence. Only firmware without a UiApp falls back
            // to the reboot (read then only by the voice driver, if it loads).
            static uint8_t option[2048];size_t optionSize=0;
            if(FirmwareMenuOption(option,sizeof(option),optionSize)==EFI_SUCCESS) {
                message(pl?u"Otwieram ustawienia firmware bez ponownego uruchamiania, bo tak czytnik na pewno je przeczyta.":u"Opening firmware settings without a restart, so the reader can read them for certain.");
                residentQuickBoot=true;DrainKeys(systemTable);launch(mediaEntryId,option,optionSize,nullptr,nullptr);residentQuickBoot=false;
            } else {
                // r399: no UiApp - read the firmware's HII settings forms in this
                // session (nearly all UEFI, incl. OEM firmware that does not load
                // Driver#### in its own setup after a reboot: the user's "czytnik
                // się nie włącza" case). Only firmware exposing neither a UiApp
                // nor HII forms, but supporting on-request setup, falls back to
                // the reboot (read then only by the voice driver, if it loads).
                message(pl?u"Otwieram ustawienia firmware bez ponownego uruchamiania, bo tak czytnik na pewno je przeczyta.":u"Opening firmware settings without a restart, so the reader can read them for certain.");
                status=RunHiiMenu(systemTable,speechFailed?nullptr:voice,effects,pl,settings.sounds,settings.explicitMessages);
                render=true;sayFocus=true;
                if(status!=EFI_SUCCESS) {
                    if(systools::FirmwareSetupSupported(systemTable->RuntimeServices)) {model.Confirm(action);char16_t confirmation[512]={};ConfirmationMessage(action,pl,nullptr,confirmation,512);message(confirmation);}
                    else popup(pl?u"To firmware nie udostępnia ustawień do odczytu (brak programu UEFI i formularzy HII) ani nie zgłasza otwierania ich na żądanie. Nic nie zmieniono.":u"This firmware offers no readable settings (no UEFI program and no HII forms) and does not support opening them on request. Nothing was changed.");
                }
            }
        } else if(action==MenuAction::restartSystem || action==MenuAction::shutdownSystem) {
            model.Confirm(action);char16_t confirmation[512]={};ConfirmationMessage(action,pl,nullptr,confirmation,512);message(confirmation);
        } else if(action==MenuAction::toggleFirmwareVoice) {
            // r385: the voice driver is the only speech while Charon is not
            // first in BootOrder; never remove it then.
            uint16_t voiceId,live[maximumOrder];size_t liveSize=sizeof(live);
            if(FindFirmwareVoice(voiceId)==EFI_SUCCESS
                && systemTable->RuntimeServices->GetVariable((char16_t*)u"BootOrder",&globalGuid,nullptr,&liveSize,live)==EFI_SUCCESS
                && liveSize>=2 && FirstNormal(live,liveSize/2)<liveSize/2 && live[FirstNormal(live,liveSize/2)]!=current) {
                popup(pl?u"Charon nie jest pierwszy w kolejności rozruchu, więc ten sterownik to jedyna mowa przy starcie. Najpierw zaznacz Menu Charona przy starcie w Kolejności rozruchu. Nic nie zmieniono.":u"Charon is not first in the boot order, so this driver is the only speech at start. First check Charon menu at start in Boot order. Nothing was changed.");
                continue;
            }
            model.Confirm(MenuAction::toggleFirmwareVoice);char16_t confirmation[512]={};
            ConfirmationMessage(MenuAction::toggleFirmwareVoice,pl,nullptr,confirmation,512);message(confirmation);
        } else if(action==MenuAction::saveOrder) {
            uint16_t voiceId;
            if(!model.charonFirst && FindFirmwareVoice(voiceId)!=EFI_SUCCESS) {
                popup(pl?u"Bez menu Charona komputer startowałby bez mowy. Najpierw włącz w Ustawieniach mowę od włączenia komputera (sterownik głosu Charona). Nic nie zapisano.":u"Without Charon's menu the computer would start without speech. First turn on speech from power-on (Charon's voice driver) in Settings. Nothing was saved.");
                continue;
            }
            if(!model.charonFirst && FirstNormal(model.order,model.orderCount)>=model.orderCount) {popup(pl?u"Poza Charonem nie ma innego wpisu do uruchomienia. Nic nie zapisano.":u"There is no entry other than Charon to start. Nothing was saved.");continue;}
            model.Confirm(MenuAction::saveOrder);char16_t confirmation[512]={};
            if(model.charonFirst)ConfirmationMessage(MenuAction::saveOrder,pl,nullptr,confirmation,512);
            else {
                char16_t name[300]={};OrderLabel(model.order[FirstNormal(model.order,model.orderCount)],pl,name,300);
                Append(confirmation,512,pl?u"Zapisać kolejność bez menu Charona przy starcie? Firmware uruchomi od razu: ":u"Save the order without Charon's menu at start? Firmware will start directly: ");
                Append(confirmation,512,name);
                Append(confirmation,512,pl?u". Ekran będzie czytał sterownik głosu. Charon zostaje na końcu listy, w menu rozruchu firmware. Domyślnie wybrano Anuluj.":u". The voice driver reads the screen. Charon stays last, in the firmware boot menu. Cancel is selected by default.");
            }
            message(confirmation);
        } else if(action==MenuAction::pendingBoot || action==MenuAction::pendingSettings || action==MenuAction::pendingKeys || action==MenuAction::setContinue || action==MenuAction::setBootNext) {
            if((action==MenuAction::setContinue || action==MenuAction::setBootNext) && selected>=inventory.count) {problem(BootProblem::noSelection,EFI_NOT_FOUND,false);continue;}
            if((action==MenuAction::setContinue || action==MenuAction::setBootNext) && inventory.entries[selected].source) {
                popup(pl?u"Nośnik znaleziony przez Charona nie ma wpisu w firmware; można go tylko uruchomić z listy urządzeń."
                        :u"A medium found by Charon has no firmware entry; it can only be started from the device list.");continue;}
            char16_t confirmation[512]={};
            ConfirmationMessage(action,pl,(action==MenuAction::setContinue || action==MenuAction::setBootNext)?&inventory.entries[selected]:nullptr,confirmation,512);
            model.Confirm(action);message(confirmation);
        } else if(action==MenuAction::confirmed) {
            const auto pending=model.pending;model.LeaveConfirm();
            if(pending==MenuAction::pendingBoot) {status=stop();if(status==EFI_SUCCESS)status=StartRequested(self,root,current);}
            else if(pending==MenuAction::pendingSettings)status=ProcessSettings(root,current);
            else if(pending==MenuAction::pendingKeys)status=ProcessKeyRequest(root);
            else if(pending==MenuAction::setContinue)status=selected<inventory.count && inventory.Unchanged() ? SaveContinue(root,inventory.entries[selected]) : EFI_NOT_FOUND;
            else if(pending==MenuAction::setBootNext)status=selected<inventory.count && inventory.Unchanged() ? SaveOnce(root,inventory.entries[selected]) : EFI_NOT_FOUND;
            else if(pending==MenuAction::toggleFirmwareVoice) {uint16_t id;
                status=FindFirmwareVoice(id)==EFI_SUCCESS?RemoveFirmwareVoice():InstallFirmwareVoice(self,root);}
            else if(pending==MenuAction::restartSystem || pending==MenuAction::shutdownSystem || pending==MenuAction::restartFirmware) {
                status=pending==MenuAction::restartFirmware?systools::RequestFirmwareSetup(systemTable->RuntimeServices):EFI_SUCCESS;
                // r396d (user): the screen reader must work in the firmware
                // setup after the restart: only the voice driver (Driver####)
                // runs there, so it is turned on first when it is off.
                if(status==EFI_SUCCESS && pending==MenuAction::restartFirmware) {uint16_t id;
                    if(FindFirmwareVoice(id)!=EFI_SUCCESS) {
                        const auto voice=InstallFirmwareVoice(self,root);
                        if(voice!=EFI_SUCCESS) {char16_t text[400]={};Append(text,400,pl?u"Nie udało się włączyć mowy od włączenia komputera, więc czytnik nie zadziała w ustawieniach firmware. Nie uruchomiono ponownie.":u"Speech from power-on could not be turned on, so the reader would not work in firmware setup. Not restarted.");
                            AppendBootStatus(text,400,voice);popup(text);continue;}
                    }}
                // powerOff returns only when audio did not stop or ResetSystem returned.
                if(status==EFI_SUCCESS)status=powerOff(pending==MenuAction::shutdownSystem?EfiResetShutdown:EfiResetCold,
                    pending==MenuAction::shutdownSystem?(pl?u"Wyłączam komputer.":u"Shutting down."):
                    pending==MenuAction::restartFirmware?(pl?u"Uruchamiam ponownie do ustawień firmware.":u"Restarting into firmware setup."):(pl?u"Uruchamiam ponownie.":u"Restarting."));
            }
            else if(pending==MenuAction::saveWake || pending==MenuAction::disableWake) {
                status=pending==MenuAction::saveWake?systools::SetWake(systemTable->RuntimeServices,model.clockYear,model.clockMonth,model.clockDay,model.clockHour,model.clockMinute)
                    :systools::DisableWake(systemTable->RuntimeServices);
                if(status==EFI_SUCCESS) {model.page=MenuPage::tools;model.index=toolWake;}
                else if(pending==MenuAction::saveWake && (status==EFI_UNSUPPORTED || status==EFI_INVALID_PARAMETER)) {
                    char16_t text[400]={};Append(text,400,pl?u"Firmware nie przyjęło tej godziny budzenia. Wiele komputerów przyjmuje tylko czas w ciągu najbliższych 24 godzin, liczony według zegara firmware.":u"Firmware did not accept this alarm time. Many computers accept only a time within the next 24 hours by the firmware clock.");
                    AppendBootStatus(text,400,status);popup(text);continue;
                }
            }
            else if(pending==MenuAction::saveClock) {
                status=systools::SetClock(systemTable->RuntimeServices,model.clockYear,model.clockMonth,model.clockDay,model.clockHour,model.clockMinute);
                if(status==EFI_SUCCESS) {model.page=MenuPage::tools;model.index=toolSetClock;}
            }
            else if(pending==MenuAction::saveOrder) {
                status=saveOrder();
                if(status==EFI_SUCCESS) {const auto refreshed=inventory.Refresh(current);inventoryStatus=refreshed;model.devices=inventory.count;selected=0;}
            }
            else status=EFI_INVALID_PARAMETER;
            {char16_t result[200]={};Append(result,200,status==EFI_SUCCESS ? (pl ? u"Operacja zakończona" : u"Operation completed") : (pl ? u"Operacja odrzucona lub niedostępna" : u"Operation rejected or unavailable"));
            if(status!=EFI_SUCCESS)AppendBootStatus(result,200,status);
            popup(result);}
        } else if(action==MenuAction::cancelled && model.page==MenuPage::confirm) {model.LeaveConfirm();}
    }
    // Never unload callbacks or transfer while a provider may still play audio.
    while(stop()!=EFI_SUCCESS) {Say(settings.explicitMessages?(pl?u"Cholerny dźwięk nie zatrzymał się; rozruch zablokowany.\r\n":u"Damn audio would not stop; handoff blocked.\r\n"):
        (pl?u"Nie zatrzymano dźwięku; przekazanie sterowania zablokowane.\r\n":u"Audio stop failed; handoff blocked.\r\n"));boot->Stall(100000);}
    if(idle)boot->CloseEvent(idle);
    if(timer)boot->CloseEvent(timer);
    inventory.Clear();return status;
}
}
