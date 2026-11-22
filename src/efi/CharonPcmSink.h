// Charon PCM sink (r407): sound of another program mixed under Charon's speech.
// GPL-2.0-or-later.
//
// Charon's voice provider owns the sound card (the HAK stream of AudioDxe, or
// Charon's own AC'97 / ES1371 / USB output); a second program cannot open it.
// A program that makes sound of its own - CharonPC playing the emulated Sound
// Blaster 16 of a guest Windows - writes PCM into this sink instead, and the
// provider mixes it into what it sends to the card: under the speech while
// Charon speaks, alone otherwise.
//
// Published by the provider under pcmSinkGuid, as its own protocol with
// version and size (not part of MenuVoice, AudioEffects or ReaderControl, and
// independent of charonRelease): all functions use the EFI calling convention,
// so images of different builds can use it.
//
//   Open(rate, channels)   only the sink's own format (fields `rate` and
//                          `channels`: 48000 Hz, 2 channels, signed 16 bit)
//                          is accepted in version 1; EFI_UNSUPPORTED otherwise.
//                          One client: a new Open drops what an earlier one left.
//   Write(frames)          never blocks; `accepted` may be less than `count`
//                          (the ring is full: the card takes 48000 frames a
//                          second). Call at TPL_APPLICATION.
//   Queued(frames)         frames accepted and not yet handed to the card.
//   Close()                drops what is still queued.
//
// The sound reaches the card when the voice is pumped (MenuVoice::Pump): the
// client keeps pumping as it does for speech. Playback of a new sound starts
// when `prebuffer` frames are there (protection against an irregular pump),
// so a client that ends a sound appends some silence.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <efi/boot-services.h>
namespace hakefi {
static efi_guid pcmSinkGuid={0x8c4e2f1a,0x3b7d,0x4e65,{0xa1,0x9c,0x52,0xd8,0x07,0x6e,0xb3,0x41}};
struct PcmSink {
    uint32_t version,size;
    uint32_t rate,channels;
    efi_status (EFIAPI *Open)(PcmSink*,uint32_t rate,uint32_t channels);
    efi_status (EFIAPI *Write)(PcmSink*,const int16_t* samples,size_t count,size_t* accepted);
    efi_status (EFIAPI *Queued)(PcmSink*,size_t* frames);
    efi_status (EFIAPI *Close)(PcmSink*);
};
static_assert(sizeof(void*)!=8 || sizeof(PcmSink)==48,"PcmSink v1 ABI");
inline PcmSink* FindPcmSink(efi_boot_services* boot) {
    PcmSink* sink=nullptr;
    if(!boot || boot->LocateProtocol(&pcmSinkGuid,nullptr,(void**)&sink)!=EFI_SUCCESS || !sink)return nullptr;
    if(sink->version<1 || sink->size<sizeof(PcmSink) || !sink->Open || !sink->Write || !sink->Queued || !sink->Close)return nullptr;
    return sink;
}
}
namespace charonaudioefi {
// The ring and the mixing. One writer (the client, at TPL_APPLICATION) and one
// reader (the owner of the card, possibly from a timer notification that
// interrupts the writer): the writer only moves written_, the reader only read_.
// The reader may itself be entered twice: a resident reader pumps the voice
// from a timer notification, and that can interrupt the pump call of an
// application (CharonPC pumps the voice too). Only the outer call moves
// read_ (busy_): the inner one passes the speech through unmixed. Without
// this both advanced read_ past written_ and the ring played for ever.
// `Sink` is charon::AudioEngine::Sink (context, Write, Drained) - a template
// parameter, so a client that only writes needs no audio engine headers.
class PcmMix {
public:
    static constexpr size_t capacity=8192;     // frames (170 ms)
    static constexpr size_t prebuffer=1920;    // 40 ms before a new sound starts
    static constexpr size_t block=128;         // charon::AudioEngine::blockFrames
    PcmMix() {
        api_.sink.version=1;api_.sink.size=sizeof(hakefi::PcmSink);api_.sink.rate=48000;api_.sink.channels=2;
        api_.sink.Open=OpenThunk;api_.sink.Write=WriteThunk;api_.sink.Queued=QueuedThunk;api_.sink.Close=CloseThunk;
        api_.owner=this;
    }
    PcmMix(const PcmMix&)=delete;PcmMix& operator=(const PcmMix&)=delete;
    hakefi::PcmSink* Interface() {return &api_.sink;}
    // ---- publication (the provider that owns the card) ----
    efi_status Publish(efi_boot_services* boot) {
        if(handle_)return EFI_ALREADY_STARTED;
        if(!boot || !boot->InstallProtocolInterface || !boot->UninstallProtocolInterface || !boot->LocateProtocol)return EFI_INVALID_PARAMETER;
        void* existing=nullptr;
        if(boot->LocateProtocol(&hakefi::pcmSinkGuid,nullptr,&existing)==EFI_SUCCESS)return EFI_ALREADY_STARTED;
        efi_handle handle=nullptr;
        const auto status=boot->InstallProtocolInterface(&handle,&hakefi::pcmSinkGuid,EFI_NATIVE_INTERFACE,&api_.sink);
        if(status==EFI_SUCCESS && handle) {boot_=boot;handle_=handle;}
        return status;
    }
    // Failure keeps the interface (and this object must stay): never leave callbacks behind freed storage.
    efi_status Withdraw() {
        if(!handle_)return EFI_SUCCESS;
        const auto status=boot_->UninstallProtocolInterface(handle_,&hakefi::pcmSinkGuid,&api_.sink);
        if(status!=EFI_SUCCESS)return status;
        handle_=nullptr;boot_=nullptr;Close();return EFI_SUCCESS;
    }
    // ---- the client's side ----
    bool Open(uint32_t rate,uint32_t channels) {
        if(rate!=48000 || channels!=2)return false;
        open_=false;read_=written_;streaming_=false;gain_=fullGain;wrote_=0;open_=true;return true;
    }
    size_t Write(const int16_t* samples,size_t frames) {
        if(!open_ || !samples)return 0;
        const uint64_t written=written_;
        const size_t used=size_t(written-read_);
        size_t take=used<capacity?capacity-used:0;if(take>frames)take=frames;
        for(size_t i=0;i<take;++i) {
            const size_t at=size_t((written+i)%capacity)*2;
            ring_[at]=samples[2*i];ring_[at+1]=samples[2*i+1];
        }
        written_=written+take;   // published after the samples are in place
        return take;
    }
    size_t Queued() const {return open_?size_t(written_-read_):0;}
    void Close() {open_=false;read_=written_;streaming_=false;wrote_=0;}
    bool Opened() const {return open_;}
    // ---- the card owner's side ----
    // The sink the speech engine writes to while a client is there: every block
    // of speech, cues or melody gets the client's sound added (at half level,
    // so the speech stays in front). Without a client: the card's own sink.
    template<class Sink> Sink Wrap(const Sink& inner) {
        if(!open_ || !inner.Write)return inner;
        innerContext_=inner.context;innerWrite_=inner.Write;innerDrained_=inner.Drained;
        Sink mixed=inner;mixed.context=this;mixed.Write=MixedWrite;mixed.Drained=MixedDrained;
        return mixed;
    }
    // After the engine was pumped: when it wrote nothing (no speech, or the
    // card is full), the client's sound goes to the card by itself.
    template<class Sink> void Service(const Sink& card) {
        Busy busy(busy_);if(!busy.own)return;
        const size_t wrote=wrote_;wrote_=0;
        if(!open_ || !card.Write)return;
        if(wrote) {streaming_=true;return;}
        size_t fill=Fill();
        if(!fill)return;
        // The card played everything it had: what follows is a new start.
        if(streaming_ && card.Drained && card.Drained(card.context))streaming_=false;
        if(!streaming_) {if(fill<prebuffer)return;streaming_=true;}
        while(fill) {
            const size_t count=fill<block?fill:block;
            int16_t out[block*2];int32_t gain=gain_;
            for(size_t i=0;i<count;++i) {
                const size_t at=size_t((read_+i)%capacity)*2;
                gain=gain+gainStep<fullGain?gain+gainStep:fullGain;
                out[2*i]=int16_t((int32_t(ring_[at])*gain)>>15);out[2*i+1]=int16_t((int32_t(ring_[at+1])*gain)>>15);
            }
            const size_t accepted=card.Write(card.context,out,count);
            if(accepted>count)return;
            gain_=gain_+int32_t(accepted)*gainStep<fullGain?gain_+int32_t(accepted)*gainStep:fullGain;
            read_=read_+accepted;fill-=accepted;
            if(accepted<count)break;
        }
    }
private:
    static constexpr int32_t fullGain=32768,duckedGain=16384,gainStep=17;   // 20 ms between them
    struct Busy {
        volatile bool& flag;const bool own;
        explicit Busy(volatile bool& f):flag(f),own(!f) {if(own)flag=true;}
        ~Busy() {if(own)flag=false;}
    };
    // Frames waiting, for the reader. More than the ring holds cannot be: the reader starts anew.
    size_t Fill() {
        const uint64_t fill=written_-read_;
        if(fill>capacity) {read_=written_;return 0;}
        return size_t(fill);
    }
    struct Api {hakefi::PcmSink sink;PcmMix* owner;};
    static PcmMix* Owner(hakefi::PcmSink* sink) {return reinterpret_cast<Api*>(sink)->owner;}
    static efi_status EFIAPI OpenThunk(hakefi::PcmSink* sink,uint32_t rate,uint32_t channels) {
        if(!sink)return EFI_INVALID_PARAMETER;
        return Owner(sink)->Open(rate,channels)?EFI_SUCCESS:EFI_UNSUPPORTED;
    }
    static efi_status EFIAPI WriteThunk(hakefi::PcmSink* sink,const int16_t* samples,size_t count,size_t* accepted) {
        if(!sink || !accepted || (!samples && count))return EFI_INVALID_PARAMETER;
        if(!Owner(sink)->open_) {*accepted=0;return EFI_NOT_READY;}
        *accepted=Owner(sink)->Write(samples,count);return EFI_SUCCESS;
    }
    static efi_status EFIAPI QueuedThunk(hakefi::PcmSink* sink,size_t* frames) {
        if(!sink || !frames)return EFI_INVALID_PARAMETER;
        *frames=Owner(sink)->Queued();return EFI_SUCCESS;
    }
    static efi_status EFIAPI CloseThunk(hakefi::PcmSink* sink) {
        if(!sink)return EFI_INVALID_PARAMETER;
        Owner(sink)->Close();return EFI_SUCCESS;
    }
    static size_t MixedWrite(void* context,const int16_t* samples,size_t frames) {
        auto* self=static_cast<PcmMix*>(context);
        if(!self->innerWrite_ || (!samples && frames))return 0;
        Busy busy(self->busy_);if(!busy.own)return self->innerWrite_(self->innerContext_,samples,frames);
        if(frames>block)frames=block;
        size_t fill=self->Fill();if(fill>frames)fill=frames;
        int16_t out[block*2];int32_t gain=self->gain_;
        for(size_t i=0;i<frames;++i) {
            int32_t left=samples[2*i],right=samples[2*i+1];
            if(i<fill) {
                const size_t at=size_t((self->read_+i)%capacity)*2;
                gain=gain-gainStep>duckedGain?gain-gainStep:duckedGain;
                left+=(int32_t(self->ring_[at])*gain)>>15;right+=(int32_t(self->ring_[at+1])*gain)>>15;
            }
            out[2*i]=int16_t(left>32767?32767:left<-32768?-32768:left);
            out[2*i+1]=int16_t(right>32767?32767:right<-32768?-32768:right);
        }
        const size_t accepted=self->innerWrite_(self->innerContext_,out,frames);
        if(accepted>frames)return accepted;   // the engine reports the fault
        const size_t used=accepted<fill?accepted:fill;
        self->gain_=self->gain_-int32_t(used)*gainStep>duckedGain?self->gain_-int32_t(used)*gainStep:duckedGain;
        self->read_=self->read_+used;self->wrote_+=accepted;
        return accepted;
    }
    static bool MixedDrained(void* context) {
        auto* self=static_cast<PcmMix*>(context);
        return self->innerDrained_ && self->innerDrained_(self->innerContext_);
    }
    Api api_{};
    efi_boot_services* boot_=nullptr;efi_handle handle_=nullptr;
    void* innerContext_=nullptr;size_t (*innerWrite_)(void*,const int16_t*,size_t)=nullptr;bool (*innerDrained_)(void*)=nullptr;
    volatile uint64_t written_=0,read_=0;
    volatile bool open_=false,busy_=false;
    bool streaming_=false;
    int32_t gain_=fullGain;
    size_t wrote_=0;
    int16_t ring_[capacity*2]={};
};
}
