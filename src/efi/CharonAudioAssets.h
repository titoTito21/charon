#pragma once
#include <efi/protocol/file.h>
#include "CharonCues.h"
#include "../tts/runtime/CharonPool.h"

namespace charonaudioefi {
// Reads only the selected ESP. Output is committed after exact-size/EOF and
// close checks; buffers remain pool-owned until explicitly released.
inline efi_status ReadAudioFile(efi_file_protocol* root,CharonPool* pool,
    const char16_t* path,size_t limit,void*& data,size_t& bytes) {
    if(!root || !pool || !path || !limit)return EFI_INVALID_PARAMETER;
    efi_file_protocol* file=nullptr;
    auto status=root->Open(root,&file,path,EFI_FILE_MODE_READ,0);
    if(status!=EFI_SUCCESS)return status;
    if(!file)return EFI_DEVICE_ERROR;
    alignas(8) uint8_t infoBytes[1024]={};size_t infoSize=sizeof(infoBytes);
    efi_guid infoGuid=EFI_FILE_INFO_GUID;
    status=file->GetInfo(file,&infoGuid,&infoSize,infoBytes);
    auto* info=reinterpret_cast<efi_file_info*>(infoBytes);
    void* candidate=nullptr;size_t count=0;
    if(status==EFI_SUCCESS) {
        if(infoSize<sizeof(efi_file_info) || infoSize>sizeof(infoBytes)
            || info->Size<sizeof(efi_file_info) || info->Size>infoSize
            || (info->Attribute&EFI_FILE_DIRECTORY) || !info->FileSize || info->FileSize>limit)
            status=EFI_BAD_BUFFER_SIZE;
        else {
            count=size_t(info->FileSize);
            candidate=charon_pool_malloc(pool,count);
            if(!candidate)status=EFI_OUT_OF_RESOURCES;
        }
    }
    for(size_t offset=0;status==EFI_SUCCESS && offset<count;) {
        size_t part=count-offset;if(part>65536)part=65536;
        const size_t requested=part;
        status=file->Read(file,&part,static_cast<uint8_t*>(candidate)+offset);
        if(status==EFI_SUCCESS && (!part || part>requested))status=EFI_DEVICE_ERROR;
        if(status==EFI_SUCCESS)offset+=part;
    }
    if(status==EFI_SUCCESS) {
        uint8_t extra=0;size_t size=1;
        status=file->Read(file,&size,&extra);
        if(status==EFI_SUCCESS && size)status=EFI_MEDIA_CHANGED;
    }
    const auto closed=file->Close(file);
    if(status==EFI_SUCCESS)status=closed;
    if(status!=EFI_SUCCESS) {charon_pool_free(pool,candidate);return status;}
    data=candidate;bytes=count;return EFI_SUCCESS;
}

// Fixed raw PCM16LE, stereo, 48000 Hz, prepared by prepare-charon-sounds.py
// and export-efi-melodies.cpp. No speech recordings or Haiku startup WAV.
class Assets {
public:
    struct Sound {const int16_t* samples=nullptr;size_t frames=0;};
    Assets()=default;
    Assets(const Assets&)=delete;
    Assets& operator=(const Assets&)=delete;
    efi_status Load(efi_file_protocol* root,CharonPool* pool) {
        if(pool_)return EFI_ALREADY_STARTED;
        if(!root || !pool)return EFI_INVALID_PARAMETER;
        pool_=pool;
        for(unsigned i=1;i<hakefi::menuCueCount;++i) {
            const char* source=hakefi::MenuCueAsset(hakefi::MenuCue(i));
            char16_t path[96];Path(path,source,true);
            auto status=Read(root,path,cues_[i]);
            if(status==EFI_NOT_FOUND && i>=hakefi::requiredMenuCues)continue;  // ESP from before r361
            if(status!=EFI_SUCCESS) {Clear();return status;}
        }
        for(unsigned i=0;i<4;++i) {
            char name[]="start-1.pcm";name[6]=char('1'+i);
            char16_t path[96];Path(path,name,false);
            auto status=Read(root,path,starts_[i]);
            if(status==EFI_SUCCESS) {
                char bed[]="background-1.pcm";bed[11]=char('1'+i);
                Path(path,bed,false);status=Read(root,path,beds_[i]);
            }
            // r385: melodies are optional (the UEFI add-on installs cues only;
            // the boot menu then reports melodies as unavailable).
            if(status==EFI_NOT_FOUND) {Release(starts_[i]);continue;}
            if(status!=EFI_SUCCESS) {Clear();return status;}
        }
        return EFI_SUCCESS;
    }
    // Caller must first stop/drain every consumer borrowing these buffers.
    void Clear() {
        if(pool_) {
            for(auto& sound:cues_)Release(sound);
            for(auto& sound:starts_)Release(sound);
            for(auto& sound:beds_)Release(sound);
        }
        pool_=nullptr;
    }
    Sound Cue(hakefi::MenuCue cue)const {const auto i=unsigned(cue);return i<hakefi::menuCueCount?cues_[i]:Sound{};}
    Sound Start(unsigned choice)const {return choice>=1 && choice<=4?starts_[choice-1]:Sound{};}
    Sound Background(unsigned choice)const {return choice>=1 && choice<=4?beds_[choice-1]:Sound{};}
    bool Ready()const {return pool_!=nullptr;}
private:
    static void Path(char16_t (&path)[96],const char* name,bool wav) {
        const char16_t prefix[]=u"\\EFI\\HAK\\audio\\";
        size_t at=0;while(prefix[at]) {path[at]=prefix[at];++at;}
        for(size_t i=0;name[i] && at+1<96;++i)path[at++]=char16_t(name[i]);
        path[at]=0;
        if(wav && at>=4) {path[at-3]=u'p';path[at-2]=u'c';path[at-1]=u'm';}
    }
    efi_status Read(efi_file_protocol* root,const char16_t* path,Sound& sound) {
        void* data=nullptr;size_t bytes=0;
        auto status=ReadAudioFile(root,pool_,path,4*1024*1024,data,bytes);
        if(status!=EFI_SUCCESS)return status;
        if(bytes%4) {charon_pool_free(pool_,data);return EFI_BAD_BUFFER_SIZE;}
        sound={static_cast<const int16_t*>(data),bytes/4};return EFI_SUCCESS;
    }
    void Release(Sound& sound) {charon_pool_free(pool_,const_cast<int16_t*>(sound.samples));sound={};}
    CharonPool* pool_=nullptr;
    Sound cues_[hakefi::menuCueCount],starts_[4],beds_[4];
};
}
