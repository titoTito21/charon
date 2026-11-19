#pragma once
#include <efi/boot-services.h>
#include "AudioIo.h"
#include "CharonAc97.h"
#include "../tts/runtime/CharonAudioEngine.h"
namespace charonaudioefi {
// ABI mirror of audio_stream/driver/HAKAudioStream.h revisions 1 and 2. Keep independent
// of EDK2 headers; this application uses the existing Haiku EFI declarations.
struct Stream;
struct State {uint32_t size,flags;uint64_t submitted,consumed,underrun;uint32_t queued,hardware;efi_status error;};
struct Stream {
    uint32_t revision,size;
    efi_status (EFIAPI *begin)(Stream*);
    efi_status (EFIAPI *write)(Stream*,const int16_t*,size_t,size_t*);
    efi_status (EFIAPI *state)(Stream*,State*);
    efi_status (EFIAPI *stop)(Stream*);
    efi_status (EFIAPI *discard)(Stream*,uint64_t*);   // revision 2 (r389) only
};
static efi_guid streamGuid={0xa5e7cf46,0x421c,0x4ea9,{0x93,0x1e,0x16,0x4b,0xf3,0xa8,0xb2,0x70}};
static_assert(offsetof(Stream,begin)==8 && offsetof(Stream,write)==8+sizeof(void*)
    && offsetof(Stream,state)==8+2*sizeof(void*) && offsetof(Stream,stop)==8+3*sizeof(void*)
    && offsetof(Stream,discard)==8+4*sizeof(void*) && sizeof(Stream)==8+5*sizeof(void*),"HAK stream rev1/2 ABI");
static constexpr uint32_t streamSize1=8+4*sizeof(void*),streamSize2=8+5*sizeof(void*);
static_assert(offsetof(State,submitted)==8 && offsetof(State,consumed)==16
    && offsetof(State,underrun)==24 && offsetof(State,queued)==32
    && offsetof(State,hardware)==36 && offsetof(State,error)==40
    && sizeof(State)==40+sizeof(efi_status),"HAK stream state rev1 ABI");
class Device {
public:
    Device()=default;
    Device(const Device&)=delete;Device& operator=(const Device&)=delete;
    // Discovery only. Caller may load a firmware-authenticated AudioDxe before
    // this call; this adapter never loads/unloads images or changes trust.
    efi_status Open(efi_boot_services* boot) {
        if(bound_)return EFI_ALREADY_STARTED;
        if(!boot || !boot->LocateHandleBuffer || !boot->HandleProtocol || !boot->FreePool)return EFI_INVALID_PARAMETER;
        error_=EFI_SUCCESS;submitted_=lastConsumed_=0;
        efi_handle* handles=nullptr;size_t count=0;
        auto status=boot->LocateHandleBuffer(ByProtocol,&streamGuid,nullptr,&count,&handles);
        if(status!=EFI_SUCCESS || !handles || !count || count>1024) {
            if(handles)boot->FreePool(handles);
            // r389: no HDA stream (yet): Charon's own AC'97 output, if the machine has one.
            if(ac97_.Open(boot)==EFI_SUCCESS) {internal_=bound_=running_=true;return EFI_SUCCESS;}
            return Fail(status!=EFI_SUCCESS?status:EFI_DEVICE_ERROR);}
        status=EFI_NOT_FOUND;
        // r389: first a codec with analog outputs (speakers, headphones, line
        // out), then one with only digital ones (a graphics card's HDMI codec
        // is often found first).
        for(int pass=0;pass<2;++pass)
        for(size_t i=0;i<count;++i) {
            Stream* stream=nullptr;hakaudioefi::Protocol* audio=nullptr;
            auto found=boot->HandleProtocol(handles[i],&streamGuid,(void**)&stream);
            if(found!=EFI_SUCCESS) {status=found;continue;}
            found=boot->HandleProtocol(handles[i],&hakaudioefi::guid,(void**)&audio);
            if(found!=EFI_SUCCESS) {status=found;continue;}
            // r389: revision 2 adds discard; revision 1 drivers keep working without it.
            if(!stream || !((stream->revision==1 && stream->size==streamSize1) || (stream->revision>=2 && stream->size>=streamSize2 && stream->discard))
                || !stream->begin || !stream->write || !stream->state || !stream->stop
                || !audio || audio->revision!=4 || !audio->outputs || !audio->setup) {status=EFI_UNSUPPORTED;continue;}
            hakaudioefi::Port* ports=nullptr;size_t portCount=0;uint64_t mask=0;
            found=audio->outputs(audio,&ports,&portCount);
            if(found==EFI_SUCCESS && portCount<=64 && (!portCount || ports)) {
                // r389: every analog output at once: line out (0), built-in
                // speakers (1, laptops) and headphones (2); before r389 only
                // line out, so many laptops stayed silent. Without analog
                // outputs: HDMI/DisplayPort (5), S/PDIF (3), other (6).
                for(size_t at=0;at<portCount && at<64;++at)
                    if(ports[at].type<=2 && (ports[at].bits&2) && (ports[at].frequencies&64))mask|=uint64_t(1)<<at;
                if(pass==1 && mask)mask=0;   // tried in the first pass
                else if(pass==1)for(size_t at=0;at<portCount && at<64;++at)
                    if((ports[at].type==3 || ports[at].type==5 || ports[at].type==6) && (ports[at].bits&2) && (ports[at].frequencies&64))mask|=uint64_t(1)<<at;
            } else if(found==EFI_SUCCESS)found=EFI_DEVICE_ERROR;
            if(ports)boot->FreePool(ports);
            if(found!=EFI_SUCCESS || !mask) {status=found!=EFI_SUCCESS?found:EFI_UNSUPPORTED;continue;}
            // Retain the provider before a state-changing call. Even failed
            // setup/begin requires an explicit successful Stop before release.
            stream_=stream;handle_=handles[i];bound_=true;
            status=audio->setup(audio,mask,-12,64,2,2,0);
            if(status==EFI_SUCCESS)status=stream_->begin(stream_);
            if(status==EFI_SUCCESS)running_=true;
            boot->FreePool(handles);return status==EFI_SUCCESS?status:Fail(status);
        }
        boot->FreePool(handles);return Fail(status);
    }
    charon::AudioEngine::Sink Sink() {return {this,WriteThunk,DrainedThunk};}
    efi_status LastError() const {return error_;}
    bool NeedsStop() const {return bound_;}
    efi_handle Handle() const {return handle_;}
    efi_status Stop() {
        if(!bound_)return EFI_SUCCESS;
        if(internal_) {ac97_.Stop();internal_=bound_=running_=false;return EFI_SUCCESS;}
        const auto status=stream_->stop(stream_);
        if(status!=EFI_SUCCESS)return Fail(status);
        bound_=running_=false;stream_=nullptr;handle_=nullptr;return EFI_SUCCESS;
    }
    size_t Write(const int16_t* samples,size_t frames) {
        if(!running_ || error_!=EFI_SUCCESS)return 0;
        if(internal_)return ac97_.Write(samples,frames);
        // r389: while a boot message speaks (its sound cannot be discarded
        // without cutting it) the driver holds at most leadLimit_ frames, so
        // the reader interrupting itself is still heard within ~64 ms.
        if(leadLimit_) {
            State state{};state.size=sizeof(state);
            if(stream_->state(stream_,&state)==EFI_SUCCESS && state.size==sizeof(state)) {
                const uint64_t ahead=state.submitted-state.consumed;
                if(ahead>=leadLimit_)return 0;
                if(frames>leadLimit_-ahead)frames=size_t(leadLimit_-ahead);
            }
        }
        if(!samples && frames) {Fail(EFI_INVALID_PARAMETER);return 0;}
        if(!frames)return 0;
        if(frames>charon::AudioEngine::blockFrames)frames=charon::AudioEngine::blockFrames;
        if(submitted_>UINT64_MAX-frames) {Fail(EFI_BAD_BUFFER_SIZE);return 0;}
        size_t accepted=0;const auto status=stream_->write(stream_,samples,frames,&accepted);
        if(accepted>frames) {Fail(EFI_DEVICE_ERROR);return 0;}
        submitted_+=accepted;
        if(status!=EFI_SUCCESS)Fail(status);
        return accepted;
    }
    // r389: a newer utterance replaces the old one at once: the driver drops
    // what the hardware has not come near (revision 2). False when unsupported.
    void LimitLead(uint64_t frames) {leadLimit_=frames;}
    bool CanDiscard() const {return running_ && error_==EFI_SUCCESS && (internal_ || (stream_ && stream_->revision>=2));}
    bool Discard() {
        if(!CanDiscard())return false;
        if(internal_) {ac97_.Discard();return true;}
        uint64_t dropped=0;const auto status=stream_->discard(stream_,&dropped);
        if(status!=EFI_SUCCESS) {Fail(status);return false;}
        if(dropped>submitted_-lastConsumed_) {Fail(EFI_DEVICE_ERROR);return false;}
        submitted_-=dropped;return true;
    }
    bool Drained() {
        if(!running_ || error_!=EFI_SUCCESS)return false;
        if(internal_)return ac97_.Drained();
        State state{};state.size=sizeof(state);
        const auto status=stream_->state(stream_,&state);
        if(status!=EFI_SUCCESS) {Fail(status);return false;}
        if(state.size!=sizeof(state) || (state.flags&~7U)) {Fail(EFI_DEVICE_ERROR);return false;}
        if((state.flags&4) || state.error!=EFI_SUCCESS) {Fail(state.error!=EFI_SUCCESS?state.error:EFI_DEVICE_ERROR);return false;}
        if(!(state.flags&1) || state.submitted!=submitted_ || state.consumed>state.submitted || state.consumed<lastConsumed_
            || uint64_t(state.queued)+state.hardware!=state.submitted-state.consumed) {Fail(EFI_DEVICE_ERROR);return false;}
        lastConsumed_=state.consumed;
        if(!(state.flags&2))return false;
        if(state.queued || state.hardware || state.consumed!=state.submitted) {Fail(EFI_DEVICE_ERROR);return false;}
        return true;
    }
private:
    static size_t WriteThunk(void* self,const int16_t* samples,size_t frames) {return static_cast<Device*>(self)->Write(samples,frames);}
    static bool DrainedThunk(void* self) {return static_cast<Device*>(self)->Drained();}
    efi_status Fail(efi_status status) {if(error_==EFI_SUCCESS)error_=status;return status;}
    Stream* stream_=nullptr;efi_handle handle_=nullptr;
    uint64_t submitted_=0,lastConsumed_=0;efi_status error_=EFI_SUCCESS;
    bool bound_=false,running_=false,internal_=false;
    uint64_t leadLimit_=0;
    Ac97 ac97_;   // r389
};
}
