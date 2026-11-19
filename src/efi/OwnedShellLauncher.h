#pragma once
#include "BootMenuVoice.h"
#include <efi/protocol/file.h>
#include <efi/protocol/loaded-image.h>
namespace hakefi {
class OwnedShellLauncher {
public:
    static constexpr size_t maximumImageBytes=16*1024*1024;
    bool Retained() const {return file_ || image_ || bytes_ || path_ || needsStop_;}
    efi_status Cleanup() {
        if(!boot_)return EFI_SUCCESS;
        if(needsStop_) {const auto status=voice_->Stop(voice_);if(status!=EFI_SUCCESS)return status;needsStop_=false;}
        if(image_) {
            efi_guid guid=EFI_LOADED_IMAGE_PROTOCOL_GUID;void* loaded=nullptr;
            const auto status=boot_->HandleProtocol(image_,&guid,&loaded);
            // EFI applications are normally unloaded by firmware before
            // StartImage returns. Verify that exact handle's retirement.
            if(status==EFI_INVALID_PARAMETER || status==EFI_UNSUPPORTED || status==EFI_NOT_FOUND)image_=nullptr;
            else if(status!=EFI_SUCCESS)return status;
            else {
                if(!identity_)return identityError_;
                // A firmware event may have recycled a returned image handle.
                // Never unload a new image merely because its handle matches.
                const auto* live=static_cast<const efi_loaded_image_protocol*>(loaded);
                if(live!=identity_ || !live || live->ImageBase!=imageBase_ || live->ImageSize!=imageSize_)image_=nullptr;
            }
        }
        if(image_) {const auto status=boot_->UnloadImage(image_);if(status!=EFI_SUCCESS)return status;image_=nullptr;}
        if(file_) {const auto status=file_->Close(file_);if(status!=EFI_SUCCESS)return status;file_=nullptr;}
        if(bytes_) {const auto status=boot_->FreePool(bytes_);if(status!=EFI_SUCCESS)return status;bytes_=nullptr;}
        if(path_) {const auto status=boot_->FreePool(path_);if(status!=EFI_SUCCESS)return status;path_=nullptr;}
        boot_=nullptr;voice_=nullptr;identity_=nullptr;imageBase_=nullptr;imageSize_=0;identityError_=EFI_NOT_READY;return EFI_SUCCESS;
    }
    efi_status Start(efi_boot_services* boot,efi_handle self,efi_file_protocol* root,MenuVoice* voice) {
        if(Retained())return EFI_ALREADY_STARTED;
        if(!boot || !self || !root || !ValidMenuVoice(voice))return EFI_INVALID_PARAMETER;
        MenuVoice* published=nullptr;
        auto status=boot->LocateProtocol(&menuVoiceGuid,nullptr,(void**)&published);
        if(status!=EFI_SUCCESS)return status;
        if(published!=voice)return EFI_ACCESS_DENIED;
        boot_=boot;voice_=voice;
        static const char16_t name[]=u"\\EFI\\HAK\\CharonShell.efi";
        status=root->Open(root,&file_,name,EFI_FILE_MODE_READ,0);
        size_t count=0;
        if(status==EFI_SUCCESS && !file_)status=EFI_DEVICE_ERROR;
        if(status==EFI_SUCCESS) {
            alignas(8) uint8_t buffer[1024]={};size_t length=sizeof(buffer);efi_guid guid=EFI_FILE_INFO_GUID;
            status=file_->GetInfo(file_,&guid,&length,buffer);
            const auto* info=reinterpret_cast<const efi_file_info*>(buffer);
            if(status==EFI_SUCCESS && (length<sizeof(*info) || length>sizeof(buffer) || info->Size<sizeof(*info)
                || info->Size>length || !info->FileSize || info->FileSize>maximumImageBytes || (info->Attribute&EFI_FILE_DIRECTORY)))status=EFI_BAD_BUFFER_SIZE;
            if(status==EFI_SUCCESS) {count=size_t(info->FileSize);status=boot_->AllocatePool(EfiLoaderData,count,&bytes_);if(status==EFI_SUCCESS && !bytes_)status=EFI_OUT_OF_RESOURCES;}
            for(size_t offset=0;status==EFI_SUCCESS && offset<count;) {
                size_t part=count-offset;if(part>65536)part=65536;const size_t requested=part;
                status=file_->Read(file_,&part,static_cast<uint8_t*>(bytes_)+offset);
                if(status==EFI_SUCCESS && (!part || part>requested))status=EFI_DEVICE_ERROR;
                if(status==EFI_SUCCESS)offset+=part;
            }
            if(status==EFI_SUCCESS) {uint8_t extra;size_t trailingBytes=1;status=file_->Read(file_,&trailingBytes,&extra);if(status==EFI_SUCCESS && trailingBytes)status=EFI_MEDIA_CHANGED;}
            const auto closed=file_->Close(file_);if(closed==EFI_SUCCESS)file_=nullptr;
            if(status==EFI_SUCCESS)status=closed;
        }
        if(status==EFI_SUCCESS && !IsApplication(static_cast<const uint8_t*>(bytes_),count))status=EFI_UNSUPPORTED;
        if(status==EFI_SUCCESS)status=MakePath(self,name,sizeof(name));
        if(status==EFI_SUCCESS) {
            status=boot_->LoadImage(false,self,static_cast<efi_device_path_protocol*>(path_),bytes_,count,&image_);
            if(image_) {
                // Failed authentication can still return an owned loaded image.
                // Capture its identity before Stop or any child execution.
                efi_guid guid=EFI_LOADED_IMAGE_PROTOCOL_GUID;efi_loaded_image_protocol* loaded=nullptr;
                identityError_=boot_->HandleProtocol(image_,&guid,(void**)&loaded);
                if(identityError_==EFI_SUCCESS && !loaded)identityError_=EFI_DEVICE_ERROR;
                if(identityError_==EFI_SUCCESS) {identity_=loaded;imageBase_=loaded->ImageBase;imageSize_=loaded->ImageSize;}
                else if(status==EFI_SUCCESS)status=identityError_;
            }
        }
        if(status==EFI_SUCCESS && !image_)status=EFI_DEVICE_ERROR;
        if(status==EFI_SUCCESS) {
            needsStop_=true;status=voice_->Stop(voice_);
            if(status==EFI_SUCCESS) {
                efi_guid guid=EFI_LOADED_IMAGE_PROTOCOL_GUID;efi_loaded_image_protocol* live=nullptr;
                status=boot_->HandleProtocol(image_,&guid,(void**)&live);
                if(status==EFI_SUCCESS && (live!=identity_ || !live || live->ImageBase!=imageBase_ || live->ImageSize!=imageSize_))status=EFI_MEDIA_CHANGED;
            }
            if(status==EFI_SUCCESS)status=boot_->StartImage(image_,nullptr,nullptr);
        }
        const auto cleanup=Cleanup();return cleanup==EFI_SUCCESS?status:cleanup;
    }
private:
    static bool IsApplication(const uint8_t* data,size_t size) {
        if(size<64 || data[0]!='M' || data[1]!='Z')return false;
        const size_t pe=size_t(data[60])|(size_t(data[61])<<8)|(size_t(data[62])<<16)|(size_t(data[63])<<24);
        if(pe>size || size-pe<94)return false;
        const size_t optional=size_t(data[pe+20])|(size_t(data[pe+21])<<8);
        return data[pe]=='P' && data[pe+1]=='E' && !data[pe+2] && !data[pe+3]
            && data[pe+4]==0x64 && data[pe+5]==0x86 && optional>=70 && optional<=size-pe-24
            && data[pe+24]==0x0b && data[pe+25]==2 && data[pe+92]==10 && data[pe+93]==0;
    }
    efi_status MakePath(efi_handle self,const char16_t* name,size_t nameBytes) {
        efi_guid loadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID,pathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
        efi_loaded_image_protocol* loaded=nullptr;
        auto status=boot_->HandleProtocol(self,&loadedGuid,(void**)&loaded);
        if(status!=EFI_SUCCESS)return status;
        if(!loaded)return EFI_DEVICE_ERROR;
        const uint8_t* base=nullptr;size_t prefix=0;
        status=boot_->HandleProtocol(loaded->DeviceHandle,&pathGuid,(void**)&base);
        if(status==EFI_SUCCESS) {
            if(!base)return EFI_DEVICE_ERROR;
            for(;;) {
                if(prefix>4092)return EFI_BAD_BUFFER_SIZE;
                const size_t length=size_t(base[prefix+2])|(size_t(base[prefix+3])<<8);
                if(length<4 || length>4096-prefix)return EFI_INVALID_PARAMETER;
                if(base[prefix]==0x7f) {if(base[prefix+1]!=0xff || length!=4)return EFI_UNSUPPORTED;break;}
                prefix+=length;
            }
        } else if(status!=EFI_UNSUPPORTED && status!=EFI_NOT_FOUND)return status;
        // A file-path-only node retains the own-ESP filename when firmware
        // exposes no device path. Never invent another volume or search disks.
        const size_t nodeBytes=4+nameBytes,total=prefix+nodeBytes+4;
        status=boot_->AllocatePool(EfiLoaderData,total,&path_);if(status!=EFI_SUCCESS)return status;if(!path_)return EFI_OUT_OF_RESOURCES;
        auto* out=static_cast<uint8_t*>(path_);
        for(size_t i=0;i<prefix;++i)out[i]=base[i];
        out[prefix]=4;out[prefix+1]=4;out[prefix+2]=uint8_t(nodeBytes);out[prefix+3]=uint8_t(nodeBytes>>8);
        for(size_t i=0;i<nameBytes/2;++i) {out[prefix+4+2*i]=uint8_t(name[i]);out[prefix+5+2*i]=uint8_t(name[i]>>8);}
        out[total-4]=0x7f;out[total-3]=0xff;out[total-2]=4;out[total-1]=0;return EFI_SUCCESS;
    }
    efi_boot_services* boot_=nullptr;MenuVoice* voice_=nullptr;
    efi_file_protocol* file_=nullptr;efi_handle image_=nullptr;void* bytes_=nullptr;void* path_=nullptr;bool needsStop_=false;
    const efi_loaded_image_protocol* identity_=nullptr;void* imageBase_=nullptr;uint64_t imageSize_=0;efi_status identityError_=EFI_NOT_READY;
};
static OwnedShellLauncher ownedShellLauncher;
inline efi_status StartCharonShell(efi_boot_services* boot,efi_handle self,efi_file_protocol* root,MenuVoice* voice) {
    if(ownedShellLauncher.Retained()) {const auto status=ownedShellLauncher.Cleanup();if(status!=EFI_SUCCESS)return status;}
    return ownedShellLauncher.Start(boot,self,root,voice);
}
inline efi_status CleanupCharonShell() {return ownedShellLauncher.Cleanup();}
}
