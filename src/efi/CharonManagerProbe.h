// What a boot option starts, named in Charon's device list (r368).
// GPL-2.0-or-later.
//
// Firmware names disk entries after the drive ("UEFI QEMU HARDDISK
// QM00007"). Charon reads the program such an entry would start — the file in
// the option, or \EFI\BOOT\BOOTX64.EFI on that disk — and recognizes the boot
// manager in it (CharonBootManagers.h), so the list says "Disk — … (Windows
// Boot Manager)". Read only, at most 8 MiB per entry, once per inventory.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <efi/protocol/simple-file-system.h>
#include <efi/protocol/file.h>
#include <efi/protocol/device-path.h>
#include "CharonBootManagers.h"
#include "CharonBcd.h"
#include "CharonBcdList.h"
#include "CharonManagerProfiles.h"
namespace hakefi {
struct ManagerNames {
    uint16_t id[256];const char16_t* name[256];size_t count=0;
    // r389: what the BCD store of a Windows Boot Manager disk lists.
    static constexpr size_t maxBcd=16;
    uint16_t bcdId[maxBcd];BcdSummary bcd[maxBcd];size_t bcdCount=0;
    const char16_t* Find(uint16_t option) const {for(size_t i=0;i<count;++i)if(id[i]==option)return name[i];return nullptr;}
    const BcdSummary* FindBcd(uint16_t option) const {for(size_t i=0;i<bcdCount;++i)if(bcdId[i]==option)return &bcd[i];return nullptr;}
};
inline ManagerNames managerNames{};
namespace managerprobe {
inline uint16_t Le16(const uint8_t* p) {return uint16_t(p[0]|p[1]<<8);}
// The device part of a path (before the first file node) and its file name.
inline void Split(const uint8_t* path,size_t bytes,size_t& deviceBytes,char16_t* file,size_t capacity) {
    deviceBytes=bytes;file[0]=0;size_t at=0,length=0;bool first=true;
    while(at+4<=bytes) {
        const uint8_t type=path[at],sub=path[at+1];const size_t n=Le16(path+at+2);
        if(n<4 || at+n>bytes || type==0x7f) {if(first)deviceBytes=at;break;}  // the end node is no device
        if(type==4 && sub==4) {
            if(first) {deviceBytes=at;first=false;}
            for(size_t k=at+4;k+1<at+n && length+1<capacity;k+=2) {
                const char16_t c=char16_t(Le16(path+k));if(!c)break;
                if(length && file[length-1]==u'\\' && c==u'\\')continue;
                file[length++]=c;
            }
        }
        at+=n;
    }
    file[length]=0;
}
// A file system whose device path matches the option's device part: equal
// prefix, or the option's short-form HD() node found in it.
inline efi_handle FindVolume(efi_boot_services* boot,const uint8_t* device,size_t deviceBytes) {
    efi_guid fsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,pathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
    efi_handle* handles=nullptr;size_t count=0;
    if(!deviceBytes || boot->LocateHandleBuffer(ByProtocol,&fsGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return nullptr;
    const bool shortForm=deviceBytes>=4 && device[0]==4 && device[1]==1;
    const size_t hdBytes=shortForm?Le16(device+2):0;
    efi_handle found=nullptr;
    for(size_t i=0;i<count && !found;++i) {
        const uint8_t* path=nullptr;
        if(boot->HandleProtocol(handles[i],&pathGuid,(void**)&path)!=EFI_SUCCESS || !path)continue;
        size_t total=0;while(total<4096) {const size_t n=Le16(path+total+2);if(path[total]==0x7f || n<4)break;total+=n;}
        if(shortForm) {
            for(size_t at=0;at+hdBytes<=total;) {
                const size_t n=Le16(path+at+2);
                if(n==hdBytes) {bool same=true;for(size_t k=0;k<n && same;++k)same=path[at+k]==device[k];if(same) {found=handles[i];break;}}
                if(n<4)break;at+=n;
            }
        } else if(total>=deviceBytes) {
            bool same=true;for(size_t k=0;k<deviceBytes && same;++k)same=path[k]==device[k];
            if(same)found=handles[i];
        }
    }
    boot->FreePool(handles);
    return found;
}
}  // namespace managerprobe
// Names the boot manager an option would start; nullptr when unknown or
// unreadable (network, firmware applications, missing file).
inline bool ReadBcdFile(efi_boot_services* boot,efi_file_protocol* root,BcdSummary& summary) {
    efi_file_protocol* file=nullptr;bool found=false;
    if(root->Open(root,&file,(char16_t*)u"\\EFI\\Microsoft\\Boot\\BCD",EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !file)return false;
    uint64_t end=0;
    if(file->SetPosition(file,UINT64_MAX)==EFI_SUCCESS && file->GetPosition(file,&end)==EFI_SUCCESS && end>=8192 && end<=4u*1024*1024 && file->SetPosition(file,0)==EFI_SUCCESS) {
        uint8_t* data=nullptr;size_t got=size_t(end);
        if(boot->AllocatePool(EfiBootServicesData,got,(void**)&data)==EFI_SUCCESS && data) {
            if(file->Read(file,&got,data)==EFI_SUCCESS)found=ReadBcd(data,got,summary);
            if(found) {   // r410: tools and the menu policy (read only)
                static bcdlist::Entry entries[10];bcdlist::Store store;
                const size_t n=bcdlist::ListEntries(data,got,entries,10,&store);
                for(size_t i=0;i<n;++i) {
                    if(entries[i].tool) {if(!summary.tools) {size_t k=0;for(;entries[i].description[k] && k<63;++k)summary.tool[k]=entries[i].description[k];summary.tool[k]=0;}++summary.tools;}
                    else if(entries[i].isDefault && entries[i].menuPolicy==bcdlist::policyStandard && store.displayBootMenu!=1)summary.tileMenu=true;
                }
            }
            boot->FreePool(data);
        }
    }
    file->Close(file);return found;
}
inline const char16_t* ProbeOptionManager(efi_boot_services* boot,const uint8_t* path,size_t pathBytes,BcdSummary* bcd=nullptr,bool* bcdFound=nullptr) {
    using namespace managerprobe;
    char16_t file[260];size_t deviceBytes=0;
    Split(path,pathBytes,deviceBytes,file,260);
    if(!file[0]) {const char16_t fallback[]=u"\\EFI\\BOOT\\BOOTX64.EFI";for(size_t i=0;i<sizeof(fallback)/2;++i)file[i]=fallback[i];}
    const efi_handle volume=FindVolume(boot,path,deviceBytes);
    if(!volume)return nullptr;
    efi_guid fsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;efi_simple_file_system_protocol* fs=nullptr;
    if(boot->HandleProtocol(volume,&fsGuid,(void**)&fs)!=EFI_SUCCESS || !fs)return nullptr;
    efi_file_protocol* root=nullptr;
    if(fs->OpenVolume(fs,&root)!=EFI_SUCCESS || !root)return nullptr;
    efi_file_protocol* image=nullptr;const char16_t* name=nullptr;
    if(root->Open(root,&image,file,EFI_FILE_MODE_READ,0)==EFI_SUCCESS && image) {
        uint64_t end=0;
        if(image->SetPosition(image,UINT64_MAX)==EFI_SUCCESS && image->GetPosition(image,&end)==EFI_SUCCESS && end>=64 && image->SetPosition(image,0)==EFI_SUCCESS) {
            size_t size=end>8u*1024*1024?8u*1024*1024:size_t(end);
            uint8_t* data=nullptr;
            if(boot->AllocatePool(EfiBootServicesData,size,(void**)&data)==EFI_SUCCESS && data) {
                size_t got=size;
                if(image->Read(image,&got,data)==EFI_SUCCESS && got>=64 && data[0]=='M' && data[1]=='Z')
                    name=DetectBootManager(data,got,[]() {});
                if(name==nullptr && !lastImageWasCharon && got>=64 && data[0]=='M')name=managers::DetectUserManager(data,got,[]() {});   // r396 profile files
                boot->FreePool(data);
            }
        }
        image->Close(image);
    }
    // r389: Windows Boot Manager's own menu, read from its BCD store (read only).
    if(bcd && bcdFound) {*bcdFound=false;if(name && name[0]==u'W' && name[1]==u'i' && name[2]==u'n')*bcdFound=ReadBcdFile(boot,root,*bcd);
        // r393: other boot managers' own configuration files (read only).
        else if(name) {
            char16_t dir[260];size_t n=0,cut=0;for(;file[n] && n<259;++n) {dir[n]=file[n];if(file[n]==u'\\')cut=n;}
            dir[cut]=0;*bcdFound=managers::ReadConfig(boot,root,FirstStage(name)?u"GRUB":name,dir,*bcd);   // shim starts GRUB beside it
        }}
    root->Close(root);
    return name;
}
}  // namespace hakefi
