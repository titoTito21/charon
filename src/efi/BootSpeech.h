// Reusable pre-OS cached-speech playback. Caller owns protocol, ESP and buffer.
// Buffer must outlive the driver stream, including a failed Stop operation.
#pragma once
#include "AudioIo.h"
#include "SpeechName.h"
namespace hakaudioefi {
// Caller-owned completion storage must outlive playback and failed Stop calls.
struct Completion {volatile bool done=true;};
inline void EFIAPI Completed(Protocol*,void* context) {static_cast<Completion*>(context)->done=true;}
class Speech {
public:
    Speech(Protocol* audio,efi_file_protocol* root,uint8_t* buffer,size_t capacity,uint64_t resetOutputs=0,Completion* completion=nullptr,size_t preFrames=0,size_t postFrames=0)
        :audio(audio),root(root),buffer(buffer),capacity(capacity),resetOutputs(resetOutputs),completion(completion),preFrames(preFrames),postFrames(postFrames) {}
    Speech(const Speech&)=delete;
    Speech& operator=(const Speech&)=delete;
    efi_status SayText(const char* language,const char* utf8) {
        char16_t path[64]={};
        return SpeechPath(language,utf8,path) ? SayFile(path) : EFI_INVALID_PARAMETER;
    }
    efi_status Stop() {
        if(handedOff)return EFI_SUCCESS;
        if(!audio || audio->revision!=4 || !audio->stop)return EFI_UNSUPPORTED;
        const auto status=audio->stop(audio);
        if(status==EFI_SUCCESS && completion)completion->done=true;
        return status;
    }
    efi_status Handoff() {
        // A late menu event must not restart firmware speech during OS startup.
        // A failed stop prevents handoff, but callers may retry this operation.
        handoffRequested=true;
        if(handedOff)return EFI_SUCCESS;
        const auto status=Stop();
        if(status==EFI_SUCCESS) {
            handedOff=true;audio=nullptr;root=nullptr;buffer=nullptr;capacity=0;
        }
        return status;
    }
    efi_status SayFile(const char16_t* path) {
        if(handoffRequested)return EFI_ACCESS_DENIED;
        if(!root || !buffer || capacity<8 || !path || !audio || audio->revision!=4
           || !audio->playAsync || !audio->stop)return EFI_INVALID_PARAMETER;
        // Never overwrite samples until the driver confirms playback stopped.
        auto status=Stop();if(status!=EFI_SUCCESS)return status;
        efi_file_protocol* file=nullptr;
        status=root->Open(root,&file,path,EFI_FILE_MODE_READ,0);
        if(status!=EFI_SUCCESS)return status;
        size_t length=capacity;
        status=file->Read(file,&length,buffer);
        const auto closed=file->Close(file);
        if(status!=EFI_SUCCESS)return status;
        if(closed!=EFI_SUCCESS)return closed;
        // A full buffer may be truncated: refuse rather than speak a fragment.
        if(!length || length>=capacity || length%4)return EFI_BAD_BUFFER_SIZE;
        // Optional output-start/drain guard frames, never replacing voice data.
        if(preFrames>capacity/4 || postFrames>capacity/4
           || preFrames+postFrames>(capacity-length-1)/4)return EFI_BAD_BUFFER_SIZE;
        const size_t prefix=preFrames*4,suffix=postFrames*4;
        for(size_t i=length;i>0;--i)buffer[prefix+i-1]=buffer[i-1];
        for(size_t i=0;i<prefix;++i)buffer[i]=0;
        for(size_t i=0;i<suffix;++i)buffer[prefix+length+i]=0;
        length+=prefix+suffix;
        // Optional AudioDxe restart workaround: changing stream format forces
        // hardware/DMA accounting reset. No samples play in the interim format.
        // Caller must have checked 44.1kHz and 48kHz support on every output.
        if(resetOutputs) {
            if(!audio->setup)return EFI_UNSUPPORTED;
            status=audio->setup(audio,resetOutputs,-12,32,2,2,0);
            if(status!=EFI_SUCCESS)return status;
            status=audio->setup(audio,resetOutputs,-12,64,2,2,0);
            if(status!=EFI_SUCCESS)return status;
        }
        if(completion)completion->done=false;
        status=audio->playAsync(audio,buffer,length,0,completion ? Completed : nullptr,completion);
        if(status!=EFI_SUCCESS)Stop();
        return status;
    }
private:
    Protocol* audio;
    efi_file_protocol* root;
    uint8_t* buffer;
    size_t capacity;
    uint64_t resetOutputs;
    Completion* completion;
    size_t preFrames,postFrames;
    bool handoffRequested=false,handedOff=false;
};
}
