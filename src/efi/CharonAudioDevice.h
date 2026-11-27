#pragma once
#include <efi/boot-services.h>
#include "AudioIo.h"
#include "CharonTrace.h"
#include "CharonAc97.h"
#include "CharonEs1371.h"   // r406
#include "CharonUsbAudio.h"
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
    efi_status (EFIAPI *linger)(Stream*,const int16_t*,uint32_t,uint32_t);   // revision 3 (r400) only
};
static efi_guid streamGuid={0xa5e7cf46,0x421c,0x4ea9,{0x93,0x1e,0x16,0x4b,0xf3,0xa8,0xb2,0x70}};
static_assert(offsetof(Stream,begin)==8 && offsetof(Stream,write)==8+sizeof(void*)
    && offsetof(Stream,state)==8+2*sizeof(void*) && offsetof(Stream,stop)==8+3*sizeof(void*)
    && offsetof(Stream,discard)==8+4*sizeof(void*) && sizeof(Stream)==8+6*sizeof(void*),"HAK stream rev1-3 ABI");
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
    // r405: the order is the HDA stream (AudioDxe), Charon's AC'97 output, then
    // a USB sound card (CharonUsbAudio.h) when `usbFallback` (false for the call
    // made before the caller has loaded AudioDxe: the built-in card comes first).
    // Whichever built-in card plays, a USB sound card that is also present gets
    // the same sound as a second sink (see Mirror).
    // r407: every stream (codec handle of Charon's controller driver) that lacks Charon's codec driver
    // is freed from whatever driver holds it and connected with `image` asked first. Only at
    // TPL_APPLICATION. Returns the number of codec handles reconnected.
    static size_t ClaimCodecs(efi_boot_services* boot,efi_handle image) {
        if(!boot || !image || !boot->LocateHandleBuffer || !boot->HandleProtocol || !boot->DisconnectController || !boot->ConnectController || !boot->FreePool)return 0;
        if(!charonaudioefi::AtApplicationLevel(boot))return 0;
        efi_handle* handles=nullptr;size_t count=0,done=0;
        if(boot->LocateHandleBuffer(ByProtocol,&streamGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return 0;
        for(size_t i=0;i<count && i<64;++i) {
            hakaudioefi::Protocol* audio=nullptr;
            if(boot->HandleProtocol(handles[i],&hakaudioefi::guid,(void**)&audio)==EFI_SUCCESS && audio)continue;
            boot->DisconnectController(handles[i],nullptr,nullptr);
            efi_handle first[2]={image,nullptr};
            boot->ConnectController(handles[i],first,nullptr,true);
            ++done;
        }
        boot->FreePool(handles);
        return done;
    }
    efi_status Open(efi_boot_services* boot,bool usbFallback=true) {
        if(bound_)return EFI_ALREADY_STARTED;
        if(!boot || !boot->LocateHandleBuffer || !boot->HandleProtocol || !boot->FreePool)return EFI_INVALID_PARAMETER;
        error_=EFI_SUCCESS;submitted_=lastConsumed_=0;boot_=boot;
        // A USB card that was unplugged (or was the only card of a failed stream) is let go first.
        if(usb_.Opened() && (usb_.Gone() || usbPrimary_)) {usb_.Stop();usbPrimary_=mirror_=false;}
        efi_handle* handles=nullptr;size_t count=0;
        auto status=boot->LocateHandleBuffer(ByProtocol,&streamGuid,nullptr,&count,&handles);
        if(status!=EFI_SUCCESS || !handles || !count || count>1024) {
            if(handles)boot->FreePool(handles);
            // r389: no HDA stream (yet): Charon's own AC'97 output, if the machine has one.
            if(ac97_.Open(boot)==EFI_SUCCESS) {internal_=bound_=running_=true;vanished_=false;AttachMirror();return EFI_SUCCESS;}
            // r406: or an Ensoniq ES1371/ES1373 (VMware's default card), see CharonEs1371.h.
            if(es1371_.Open(boot)==EFI_SUCCESS) {ensoniq_=internal_=bound_=running_=true;vanished_=false;AttachMirror();return EFI_SUCCESS;}
            if(usbFallback && OpenUsbPrimary())return EFI_SUCCESS;
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
            if(found!=EFI_SUCCESS) {if(!pass)charontrace::Value("CHARON audio stream without codec driver",uint64_t(i));status=found;continue;}
            // r389: revision 2 adds discard; revision 1 drivers keep working without it.
            if(!stream || !((stream->revision==1 && stream->size==streamSize1) || (stream->revision>=2 && stream->size>=streamSize2 && stream->discard))
                || !stream->begin || !stream->write || !stream->state || !stream->stop
                || !audio || audio->revision!=4 || !audio->outputs || !audio->setup) {status=EFI_UNSUPPORTED;continue;}
            hakaudioefi::Port* ports=nullptr;size_t portCount=0;uint64_t mask=0;
            charontrace::Value("CHARON audio outputs",uint64_t(i));
            found=audio->outputs(audio,&ports,&portCount);
            charontrace::Value("CHARON audio ports",uint64_t(portCount)|(uint64_t(found)<<8));
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
            charontrace::Value("CHARON audio setup mask",mask);
            // r404: the codec at its maximum gain (AudioDxe clamps 127 dB to the top amp step); was -12 dB.
            status=audio->setup(audio,mask,127,64,2,2,0);
            charontrace::Value("CHARON audio setup",uint64_t(status));
            if(status==EFI_SUCCESS)status=stream_->begin(stream_);
            charontrace::Value("CHARON audio begin",uint64_t(status));
            if(status==EFI_SUCCESS) {running_=true;vanished_=false;AttachMirror();}
            boot->FreePool(handles);return status==EFI_SUCCESS?status:Fail(status);
        }
        boot->FreePool(handles);
        // r405: stream drivers are there but none has a usable output (nothing was bound).
        if(usbFallback && OpenUsbPrimary())return EFI_SUCCESS;
        return Fail(status);
    }
    charon::AudioEngine::Sink Sink() {return {this,WriteThunk,DrainedThunk};}
    efi_status LastError() const {return error_;}
    uint64_t Submitted() const {return submitted_;}   // r409: frames the HDA stream took since Open (CharonAudioStall.h)
#ifdef HAK_CHARON_STALLTEST
    void InjectTimeout() {Fail(EFI_TIMEOUT);}         // test builds only: the stream is treated as stalled
#endif
    bool Vanished() const {return vanished_;}
    void MarkVanished() {vanished_=true;}   // r402: the driver's stream went away; Open may find it again
    bool NeedsStop() const {return bound_ || usb_.Opened();}   // r405: a USB mirror alone also has to be stopped
    bool UsbPrimary() const {return usbPrimary_;}
    bool UsbMirror() const {return mirror_;}
    bool UsbChanged(efi_boot_services* boot) {return usb_.Changed(boot);}   // r407: USB devices came or went since the last look for sound devices
    efi_handle Handle() const {return handle_;}
    efi_status Stop() {
        // r405: the USB card first, whatever it was (mirror or the only card).
        if(usb_.Opened()) {
            usb_.Stop();mirror_=false;
            if(usbPrimary_) {usbPrimary_=bound_=running_=false;return EFI_SUCCESS;}
        }
        if(!bound_)return EFI_SUCCESS;
        if(internal_) {if(ensoniq_)es1371_.Stop();else ac97_.Stop();ensoniq_=internal_=bound_=running_=false;return EFI_SUCCESS;}   // r406: ensoniq_
        if(!Alive())return EFI_SUCCESS;   // the driver is gone: nothing to stop
        const auto status=stream_->stop(stream_);
        if(status!=EFI_SUCCESS)return Fail(status);
        bound_=running_=false;lingerStream_=stream_;lingerHandle_=handle_;stream_=nullptr;handle_=nullptr;return EFI_SUCCESS;
    }
    // r407: the voice is pumped from two levels at once when a program with sound of its own runs
    // (CharonPC at TPL_APPLICATION, the resident reader from its timer). A timer pump that lands
    // between the driver's write and the bookkeeping here saw "submitted" disagree and declared a
    // device fault: the stream was restarted and the guest's sound was lost. A call nested into
    // another one now does nothing (nothing accepted, not drained, nothing discarded).
    struct Busy {
        volatile bool& flag;const bool own;
        explicit Busy(volatile bool& f):flag(f),own(!f) {if(own)flag=true;}
        ~Busy() {if(own)flag=false;}
    };
    size_t Write(const int16_t* samples,size_t frames) {
        Busy busy(busy_);if(!busy.own)return 0;
        if(!running_ || error_!=EFI_SUCCESS)return 0;
        PollUsb();
        if(usbPrimary_) {
            if(!UsbAlive())return 0;
            if(frames>charon::AudioEngine::blockFrames)frames=charon::AudioEngine::blockFrames;
            return usb_.Write(samples,frames);
        }
        if(internal_) {const size_t taken=ensoniq_?es1371_.Write(samples,frames):ac97_.Write(samples,frames);Mirror(samples,taken);return taken;}   // r406: ensoniq_
        if(!Alive())return 0;
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
        if(accepted>frames) {charontrace::Value("CHARON audio accepted too much",accepted);Fail(EFI_DEVICE_ERROR);return 0;}
        submitted_+=accepted;
        if(status!=EFI_SUCCESS)Fail(status);
        Mirror(samples,accepted);
        return accepted;
    }
    // r389: a newer utterance replaces the old one at once: the driver drops
    // what the hardware has not come near (revision 2). False when unsupported.
    // r400: the stopped stream is handed to the hardware (see HAKAudioStream.h Linger).
    bool Linger(const int16_t* loop,uint32_t frames,uint32_t repeats) {
        Stream* s=lingerStream_;
        if(s && !StreamOn(lingerHandle_,s)) {lingerStream_=nullptr;return false;}
        if(!s || s->revision<3 || s->size<8+6*sizeof(void*) || !s->linger)return false;
        return s->linger(s,loop,frames,repeats)==EFI_SUCCESS;
    }
    void LimitLead(uint64_t frames) {leadLimit_=frames;}
    bool CanDiscard() const {return running_ && error_==EFI_SUCCESS && (internal_ || usbPrimary_ || (stream_ && stream_->revision>=2));}
    bool Discard() {
        Busy busy(busy_);if(!busy.own)return false;
        if(!CanDiscard())return false;
        if(usbPrimary_) {if(!UsbAlive())return false;usb_.Discard();return true;}
        if(mirror_)usb_.Discard();
        if(internal_) {if(ensoniq_)es1371_.Discard();else ac97_.Discard();return true;}   // r406: ensoniq_
        if(!Alive())return false;
        uint64_t dropped=0;const auto status=stream_->discard(stream_,&dropped);
        if(status!=EFI_SUCCESS) {Fail(status);return false;}
        if(dropped>submitted_-lastConsumed_) {charontrace::Value("CHARON audio discard too much",dropped);charontrace::Value("CHARON audio discard window",submitted_-lastConsumed_);Fail(EFI_DEVICE_ERROR);return false;}
        submitted_-=dropped;return true;
    }
    bool Drained() {
        Busy busy(busy_);if(!busy.own)return false;
        if(!running_ || error_!=EFI_SUCCESS)return false;
        PollUsb();
        if(usbPrimary_)return UsbAlive() && usb_.Drained();
        if(internal_)return ensoniq_?es1371_.Drained():ac97_.Drained();   // r406: ensoniq_
        if(!Alive())return false;
        State state{};state.size=sizeof(state);
        const auto status=stream_->state(stream_,&state);
        if(status!=EFI_SUCCESS) {Fail(status);return false;}
        if(state.size!=sizeof(state) || (state.flags&~7U)) {Fail(EFI_DEVICE_ERROR);return false;}
        if((state.flags&4) || state.error!=EFI_SUCCESS) {Fail(state.error!=EFI_SUCCESS?state.error:EFI_DEVICE_ERROR);return false;}
        if(!(state.flags&1) || state.submitted!=submitted_ || state.consumed>state.submitted || state.consumed<lastConsumed_
            || uint64_t(state.queued)+state.hardware!=state.submitted-state.consumed) {
            charontrace::Value("CHARON audio state flags",state.flags);charontrace::Value("CHARON audio state submitted",state.submitted);
            charontrace::Value("CHARON audio own submitted",submitted_);charontrace::Value("CHARON audio state consumed",state.consumed);
            charontrace::Value("CHARON audio own consumed",lastConsumed_);charontrace::Value("CHARON audio state queued",uint64_t(state.queued)+state.hardware);
            Fail(EFI_DEVICE_ERROR);return false;}
        lastConsumed_=state.consumed;
        if(!(state.flags&2))return false;
        if(state.queued || state.hardware || state.consumed!=state.submitted) {charontrace::Value("CHARON audio drained but queued",state.queued);Fail(EFI_DEVICE_ERROR);return false;}
        return true;
    }
private:
    // r402: firmware may stop the audio driver under us (it reconnects controllers
    // before a boot option; the stream then is freed memory, and calling into it
    // crashed the firmware at start-up with the voice driver loaded). The stream is
    // used only while its handle still carries this very interface.
    bool StreamOn(efi_handle handle,Stream* stream) const {
        if(!boot_ || !handle || !stream)return false;
        Stream* now=nullptr;
        return boot_->HandleProtocol(handle,&streamGuid,(void**)&now)==EFI_SUCCESS && now==stream;
    }
    bool Alive() {
        if(StreamOn(handle_,stream_))return true;
        stream_=nullptr;handle_=nullptr;lingerStream_=nullptr;bound_=running_=false;vanished_=true;Fail(EFI_DEVICE_ERROR);
        return false;
    }
    // r405: a USB sound card as the only output. Unplugged: reported like a
    // vanished stream driver (the caller's Open looks for a card again).
    bool OpenUsbPrimary() {
        if(usb_.Opened()) {usb_.Stop();mirror_=false;}
        const efi_status status=usb_.Open(boot_);
        charontrace::Value("CHARON usb audio primary",uint64_t(status));
        if(status!=EFI_SUCCESS)return false;
        usbPrimary_=bound_=running_=true;vanished_=false;return true;
    }
    bool UsbAlive() {
        if(usb_.Running())return true;
        bound_=running_=false;vanished_=true;Fail(EFI_DEVICE_ERROR);   // usb_ stays open until Stop or the next Open
        return false;
    }
    // r405: the built-in card plays and a USB sound card is there too: it gets
    // a copy of everything the built-in card accepted. Best effort: the
    // built-in card keeps the timing and the drain accounting; the copy is
    // dropped where the USB card is more than 400 ms behind and padded with
    // silence where it is ahead. A failure here never fails the built-in card.
    void AttachMirror() {
        if(usb_.Opened()) {mirror_=usb_.Running();return;}   // still attached (the built-in card was reopened)
        mirror_=false;
        if(!UsbAudio::Present(boot_))return;
        const efi_status status=usb_.Open(boot_);
        charontrace::Value("CHARON usb audio mirror",uint64_t(status));
        mirror_=status==EFI_SUCCESS;
    }
    // r407: hot-plug of USB sound devices while a card plays (UsbAudioSet::Poll: devices pulled out are
    // let go, ones plugged in are started). Beside a built-in card a new device becomes the mirror.
    void PollUsb() {if(boot_ && usb_.Poll(boot_) && !usbPrimary_)mirror_=true;}
    void Mirror(const int16_t* samples,size_t frames) {
        if(!mirror_ || !frames)return;
        if(!usb_.Running()) {mirror_=false;return;}   // unplugged: Stop gives it back
        usb_.Write(samples,frames,true);
    }
    efi_boot_services* boot_=nullptr;efi_handle lingerHandle_=nullptr;bool vanished_=false;
    static size_t WriteThunk(void* self,const int16_t* samples,size_t frames) {return static_cast<Device*>(self)->Write(samples,frames);}
    static bool DrainedThunk(void* self) {return static_cast<Device*>(self)->Drained();}
    efi_status Fail(efi_status status) {if(error_==EFI_SUCCESS)error_=status;return status;}
    Stream* stream_=nullptr;efi_handle handle_=nullptr;
    Stream* lingerStream_=nullptr;   // the stream stopped last (r400 Linger)
    uint64_t submitted_=0,lastConsumed_=0;efi_status error_=EFI_SUCCESS;volatile bool busy_=false;
    bool bound_=false,running_=false,internal_=false;
    uint64_t leadLimit_=0;
    Ac97 ac97_;   // r389
    Es1371 es1371_;bool ensoniq_=false;   // r406: internal_ with ensoniq_ = the ES1371 plays, without = AC'97
    UsbAudioSet usb_;bool usbPrimary_=false,mirror_=false;   // r405; r407: every USB sound device at once (CharonUsbAudio.h)
};
}
