// Charon's own Ensoniq AudioPCI output (r406). GPL-2.0-or-later.
//
// The ES1371/ES1373 (Creative CT5880, EV1938) is the DEFAULT sound card of
// VMware virtual machines (sound.virtualDev = "es1371") and of many late-1990s
// PCs. It is neither HDA nor an ICH-style AC'97 controller, so neither AudioDxe
// nor CharonAc97.h finds it. This plays the speech mix through its second
// playback channel (DAC2): 48 kHz stereo PCM16 from one looping DMA buffer of
// 16384 frames; a periodic timer reads the DMA position and clears what was
// played, so nothing old repeats. Same interface as Ac97.
//
// The chip always clocks its AC'97 codec at 48 kHz and converts every channel
// with a sample rate converter (SRC) whose coefficients live in a small RAM
// behind one register; that RAM powers up undefined, so it is initialised and
// DAC2 is given the 1:1 ratio. The codec (master and PCM volume) is reached
// through a second register that must be written while the SRC state machine
// is parked. Register knowledge: the ES1371 data sheet and the description of
// the chip in Linux sound/pci/ens1370.c; the code is Charon's own.
//
// Nothing here uses interrupts: the channel loops by itself (P2_LOOP_SEL = 0)
// and the position is polled.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <efi/boot-services.h>
#include "CharonTrace.h"
#include "CharonAc97.h"   // PciIoFull
// Pure logic (host tests: tests/CharonEs1371Tests.cpp).
namespace charones1371 {
// I/O registers (BAR 0).
constexpr uint32_t regControl=0x00,regStatus=0x04,regPage=0x0c,regSrc=0x10,regCodec=0x14,regLegacy=0x18,
    regSerial=0x20,regDac2Count=0x28,regDac2Frame=0x38,regDac2Size=0x3c;
constexpr uint32_t pageDac=0x0c;                       // regDac2Frame/Size exist on this memory page only
constexpr uint32_t controlSyncReset=1u<<14,controlDac1=1u<<6,controlDac2=1u<<5,controlAdc=1u<<4;
constexpr uint32_t statusAc97Reset=1u<<29,statusDac2=1u<<1;
constexpr uint32_t srcBusy=1u<<23,srcDisable=1u<<22,srcOffP1=1u<<21,srcOffP2=1u<<20,srcOffR1=1u<<19,srcWrite=1u<<24;
constexpr uint32_t srcKeep=srcDisable|srcOffP1|srcOffP2|srcOffR1;
constexpr uint32_t srcStateMask=0x00870000,srcStateParked=0x00010000;
constexpr uint32_t codecReady=1u<<31,codecBusy=1u<<30,codecRead=1u<<23;
// SRC RAM: per channel base + {truncation, integer part, accumulator, fraction}; volumes.
constexpr uint32_t srcDac1=0x70,srcDac2=0x74,srcAdc=0x78,srcVolAdc=0x6c,srcVolDac1=0x7c,srcVolDac2=0x7e;
constexpr uint32_t srcTrunc=0,srcInt=1,srcFraction=3;
// The playback ratio as a 5.15 fixed point multiple of 3000 Hz, rounded to nearest (48000 Hz -> 16.0).
constexpr uint32_t SrcFrequency(uint32_t rate) {return uint32_t(((uint64_t(rate)<<15)+1500)/3000);}
// The integer part sits in bits 10-15 of the channel's integer register; the low byte is the chip's own.
constexpr uint16_t SrcIntegerWord(uint16_t old,uint32_t frequency) {return uint16_t((old&0x00ff)|((frequency>>5)&0xfc00));}
constexpr uint16_t SrcFractionWord(uint32_t frequency) {return uint16_t(frequency&0x7fff);}
constexpr uint32_t SrcRamWrite(uint32_t keep,uint32_t address,uint16_t data) {return (keep&srcKeep)|((address&0x7f)<<25)|srcWrite|data;}
constexpr uint32_t SrcRamRead(uint32_t keep,uint32_t address) {return (keep&srcKeep)|((address&0x7f)<<25);}
constexpr uint32_t CodecWriteWord(uint32_t reg,uint16_t value) {return ((reg&0x7f)<<16)|value;}
constexpr uint32_t CodecReadWord(uint32_t reg) {return ((reg&0x7f)<<16)|codecRead;}
// Serial interface word for DAC2: stereo 16 bit (mode 3 in bits 2-3), loop
// end increment 2 (16-bit samples), start increment 0, no interrupt, not paused.
constexpr uint32_t serialDac2Stereo16=(3u<<2)|(2u<<19);
// The cards this driver knows: Ensoniq ES1371/ES1373, Creative CT5880, Creative EV1938.
constexpr bool Supported(uint16_t vendor,uint16_t device) {
    return (vendor==0x1274 && (device==0x1371 || device==0x5880)) || (vendor==0x1102 && device==0x8938);
}
// Revisions whose codec stays in reset until a bit in the status register is set.
constexpr bool NeedsAc97Reset(uint16_t vendor,uint16_t device,uint8_t revision) {
    return vendor==0x1274 && ((device==0x5880 && (revision==0x02 || revision==0x03 || revision==0x04))
        || (device==0x1371 && (revision==0x07 || revision==0x08)));
}
// The DMA position in frames from the size register (current longword in the
// high half; one longword is one stereo 16-bit frame); garbage stays in the ring.
constexpr uint32_t Position(uint32_t sizeRegister,uint32_t ringFrames) {return ((sizeRegister>>16)&0xffff)%ringFrames;}
constexpr uint64_t Step(uint64_t now,uint64_t last,uint64_t ringFrames) {return (now+ringFrames-last)%ringFrames;}
}
namespace charonaudioefi {
class Es1371 {
public:
    // periodFrames: what an emulation takes at once (see Acknowledge). It has
    // to last clearly longer than the 10 ms service timer, or the card would
    // be fed slower than it plays, and it must not exceed maximumLead, or a
    // period could never be filled.
    static constexpr size_t ringFrames=16384,periodFrames=2048;
    static constexpr uint64_t margin=256,maximumLead=4096;
    efi_status Open(efi_boot_services* boot) {
        if(open_)return EFI_ALREADY_STARTED;
        if(!boot || !boot->LocateHandleBuffer || !boot->HandleProtocol || !boot->FreePool || !boot->Stall)return EFI_INVALID_PARAMETER;
        boot_=boot;
        efi_guid pciIoGuid={0x4cf5b200,0x68b8,0x4ca5,{0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};
        efi_handle* handles=nullptr;size_t count=0;
        if(boot->LocateHandleBuffer(ByProtocol,&pciIoGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return EFI_NOT_FOUND;
        efi_status status=EFI_NOT_FOUND;
        for(size_t i=0;i<count && status!=EFI_SUCCESS;++i) {
            PciIoFull* io=nullptr;
            if(boot->HandleProtocol(handles[i],&pciIoGuid,(void**)&io)!=EFI_SUCCESS || !io)continue;
            uint16_t ids[2]={};uint8_t revision=0;
            if(io->Pci.Read(io,1,0,2,ids)!=EFI_SUCCESS || io->Pci.Read(io,0,8,1,&revision)!=EFI_SUCCESS)continue;
            if(!charones1371::Supported(ids[0],ids[1]))continue;
            charontrace::Value("CHARON es1371 found",uint64_t(ids[0])|(uint64_t(ids[1])<<16)|(uint64_t(revision)<<32));
            handle_=handles[i];status=Start(io,charones1371::NeedsAc97Reset(ids[0],ids[1],revision));
            charontrace::Value("CHARON es1371 start",uint64_t(status)|(uint64_t(stage_)<<56));
            if(status!=EFI_SUCCESS) {Quiet();Release();}
        }
        boot->FreePool(handles);
        return status;
    }
    bool Running() const {return open_;}
    size_t Write(const int16_t* samples,size_t frames) {
        if(!open_ || !samples)return 0;
        const efi_tpl old=boot_->RaiseTPL(TPL_NOTIFY);   // the timer's Service must not run in between
        Service();
        if(!open_) {boot_->RestoreTPL(old);return 0;}         // the controller vanished during Service
        if(write_<played_+margin) {if(write_>played_)++late_;write_=played_+margin;}   // late: continue after a short silence
        const uint64_t limit=played_+maximumLead;
        size_t n=0;
        if(write_<limit) {
            n=frames;if(n>limit-write_)n=size_t(limit-write_);
            for(size_t i=0;i<n;++i) {const size_t at=size_t((write_+i)%ringFrames);ring_[at*2]=samples[i*2];ring_[at*2+1]=samples[i*2+1];}
            write_+=n;written_+=n;
        }
        const bool report=played_>=reported_+48000;
        const uint64_t played=played_,state=(written_&0xffffffff)|(uint64_t(maximumStep_&0xffff)<<32)|((late_&0xffff)<<48);
        if(report)reported_=played_;
        boot_->RestoreTPL(old);
        // About once a second while sound is written: the proof that the DMA
        // moves (frames played; frames written, largest position step, late writes).
        if(report) {charontrace::Value("CHARON es1371 played",played);charontrace::Value("CHARON es1371 written",state);}
        return n;
    }
    bool Drained() {if(!open_)return true;const efi_tpl old=boot_->RaiseTPL(TPL_NOTIFY);Service();const bool done=!open_||write_<=played_;boot_->RestoreTPL(old);return done;}
    void Discard() {
        if(!open_)return;
        const efi_tpl old=boot_->RaiseTPL(TPL_NOTIFY);
        Service();
        if(!open_) {boot_->RestoreTPL(old);return;}
        const uint64_t keep=played_+margin;
        for(uint64_t p=keep;p<write_;++p) {const size_t at=size_t(p%ringFrames);ring_[at*2]=ring_[at*2+1]=0;}
        if(write_>keep)write_=keep;
        boot_->RestoreTPL(old);
    }
    efi_status Stop() {
        if(!open_)return EFI_SUCCESS;
        if(timer_) {boot_->SetTimer(timer_,TimerCancel,0);boot_->CloseEvent(timer_);timer_=nullptr;}
        open_=false;
        charontrace::Value("CHARON es1371 stop played",played_);
        charontrace::Value("CHARON es1371 stop written",(written_&0xffffffff)|(uint64_t(maximumStep_&0xffff)<<32)|((late_&0xffff)<<48));
        if(Live()) {Quiet();Release();}
        else {io_=nullptr;handle_=nullptr;mapping_=nullptr;host_=nullptr;ring_=nullptr;attributesSet_=false;saved_=false;}   // controller gone: never touch the freed driver
        return EFI_SUCCESS;
    }
private:
    // Every register access is counted when it fails; Start refuses on any.
    uint32_t In(uint32_t offset) {uint32_t v=0;if(io_->Io.Read(io_,2,0,offset,1,&v)!=EFI_SUCCESS) {++failed_;return 0;}return v;}
    void Out(uint32_t offset,uint32_t value) {if(io_->Io.Write(io_,2,0,offset,1,&value)!=EFI_SUCCESS)++failed_;}
    // The SRC RAM takes one access at a time; its other bits must be kept.
    bool SrcReady(uint32_t* value) {
        for(int t=0;t<2000;++t) {const uint32_t v=In(charones1371::regSrc);if(!(v&charones1371::srcBusy)) {*value=v;return true;}boot_->Stall(10);}
        ++failed_;*value=0;return false;
    }
    bool SrcWrite(uint32_t address,uint16_t data) {
        uint32_t v=0;if(!SrcReady(&v))return false;
        Out(charones1371::regSrc,charones1371::SrcRamWrite(v,address,data));return true;
    }
    bool SrcRead(uint32_t address,uint16_t* data) {
        uint32_t v=0;if(!SrcReady(&v))return false;
        Out(charones1371::regSrc,charones1371::SrcRamRead(v,address));
        if(!SrcReady(&v))return false;
        *data=uint16_t(v);return true;
    }
    // The codec register is shared with the SRC's serial engine: it is written
    // while that engine is parked (state 1 after state 0). A chip (or an
    // emulation) that never shows these states gets the access anyway after a
    // bounded wait, as the hardware needs the timing, not the handshake.
    bool CodecAccess(uint32_t word) {
        using namespace charones1371;
        bool idle=false;
        for(int t=0;t<2000 && !idle;++t) {idle=!(In(regCodec)&codecBusy);if(!idle)boot_->Stall(10);}
        if(!idle)return false;
        uint32_t saved=0;if(!SrcReady(&saved))return false;
        Out(regSrc,(saved&srcKeep)|srcStateParked);
        for(int t=0;t<1024;++t)if((In(regSrc)&srcStateMask)==0)break;
        for(int t=0;t<1024;++t)if((In(regSrc)&srcStateMask)==srcStateParked)break;
        Out(regCodec,word);
        uint32_t now=0;if(!SrcReady(&now))return false;
        Out(regSrc,saved);
        return true;
    }
    bool CodecWrite(uint32_t reg,uint16_t value) {return CodecAccess(charones1371::CodecWriteWord(reg,value));}
    bool CodecRead(uint32_t reg,uint16_t* value) {
        using namespace charones1371;
        for(int attempt=0;attempt<4;++attempt) {
            if(!CodecAccess(CodecReadWord(reg)))return false;
            bool idle=false;
            for(int t=0;t<2000 && !idle;++t) {idle=!(In(regCodec)&codecBusy);if(!idle)boot_->Stall(10);}
            for(int t=0;t<2000;++t) {
                const uint32_t v=In(regCodec);
                // The answer must be for the register asked: a stale word of another register is not taken.
                if((v&codecReady) && ((v>>16)&0x7f)==reg) {*value=uint16_t(v);return true;}
                boot_->Stall(10);
            }
        }
        return false;
    }
    bool Dac2Rate(uint32_t rate) {
        using namespace charones1371;
        const uint32_t frequency=SrcFrequency(rate);
        uint32_t v=0;if(!SrcReady(&v))return false;
        Out(regSrc,(v&(srcDisable|srcOffP1|srcOffR1))|srcOffP2);          // channel frozen while its ratio changes
        uint16_t old=0;
        if(!SrcRead(srcDac2+srcInt,&old) || !SrcWrite(srcDac2+srcInt,SrcIntegerWord(old,frequency))
            || !SrcWrite(srcDac2+srcFraction,SrcFractionWord(frequency)))return false;
        if(!SrcReady(&v))return false;
        Out(regSrc,v&(srcDisable|srcOffP1|srcOffR1));
        return true;
    }
    // VMware's model of the chip hands the host one period (periodFrames) at a
    // time and then waits with the channel's interrupt flag raised, even with
    // the interrupt disabled, until the serial register is written (measured,
    // r406: one period per write; a plain rewrite of the same value is
    // enough). The flag is therefore looked at on every service and answered
    // that way; the interrupt enable bit itself is never set, so no interrupt
    // line is raised. The real chip never sets the flag with the enable bit
    // clear and simply loops.
    void Acknowledge() {
        if(In(charones1371::regStatus)&charones1371::statusDac2)Out(charones1371::regSerial,charones1371::serialDac2Stereo16);
    }
    uint32_t PositionNow() {
        Out(charones1371::regPage,charones1371::pageDac);
        return charones1371::Position(In(charones1371::regDac2Size),uint32_t(ringFrames));
    }
    // stage_ names the step that refused (traced): 1 attributes, 2 registers,
    // 3 SRC, 4 codec, 5 memory, 6 DMA mapping, 7 programming, 8 position does
    // not move, 9 timer.
    efi_status Start(PciIoFull* io,bool ac97Reset) {
        using namespace charones1371;
        io_=io;failed_=0;stage_=1;
        if(!io->Attributes || !io->AllocateBuffer || !io->FreeBuffer || !io->Map || !io->Unmap || !io->Io.Read || !io->Io.Write)return EFI_UNSUPPORTED;
        uint64_t supported=0;
        if(io->Attributes(io,4,0,&supported)!=EFI_SUCCESS)return EFI_UNSUPPORTED;
        if((supported&(0x100|0x400))!=(0x100|0x400))return EFI_UNSUPPORTED;
        if(io->Attributes(io,2,0x100|0x400,nullptr)!=EFI_SUCCESS)return EFI_UNSUPPORTED;   // I/O decode + bus master
        attributesSet_=true;
        stage_=2;
        control0_=In(regControl);serial0_=In(regSerial);
        if(failed_ || control0_==0xffffffff)return EFI_DEVICE_ERROR;   // no device behind the BAR
        saved_=true;
        // All channels off, no interrupts, no legacy (Sound Blaster) trapping.
        Out(regControl,0);Out(regSerial,0);Out(regLegacy,0);
        if(ac97Reset) {Out(regStatus,statusAc97Reset);boot_->Stall(20000);}
        // A warm reset of the AC'97 link starts the codec's bit clock.
        Out(regControl,controlSyncReset);In(regControl);boot_->Stall(100);Out(regControl,0);
        if(failed_)return EFI_DEVICE_ERROR;
        stage_=3;
        uint32_t v=0;
        if(!SrcReady(&v))return EFI_NOT_READY;
        Out(regSrc,srcDisable);
        for(uint32_t at=0;at<0x80;++at)if(!SrcWrite(at,0))return EFI_DEVICE_ERROR;
        // Both playback channels at 1:1 (16.0) and unity gain; the recorder's volume too.
        if(!SrcWrite(srcDac1+srcTrunc,16<<4) || !SrcWrite(srcDac1+srcInt,16<<10)
            || !SrcWrite(srcDac2+srcTrunc,16<<4) || !SrcWrite(srcDac2+srcInt,16<<10)
            || !SrcWrite(srcVolAdc,1<<12) || !SrcWrite(srcVolAdc+1,1<<12)
            || !SrcWrite(srcVolDac1,1<<12) || !SrcWrite(srcVolDac1+1,1<<12)
            || !SrcWrite(srcVolDac2,1<<12) || !SrcWrite(srcVolDac2+1,1<<12))return EFI_DEVICE_ERROR;
        if(!Dac2Rate(48000))return EFI_DEVICE_ERROR;
        if(!SrcReady(&v))return EFI_NOT_READY;
        Out(regSrc,0);                                                      // the converter runs
        if(failed_)return EFI_DEVICE_ERROR;
        stage_=4;
        // The codec: reset its registers, master, headphone and PCM out at 0 dB.
        // A codec that does not answer, or stays muted, is a refusal: sound
        // would be played into nothing.
        if(!CodecWrite(0x00,0))return EFI_NOT_READY;
        boot_->Stall(10000);
        uint16_t master=0xffff,pcm=0xffff,vendor[2]={};
        if(!CodecWrite(0x02,0x0000) || !CodecWrite(0x04,0x0000) || !CodecWrite(0x18,0x0808))return EFI_NOT_READY;
        if(!CodecRead(0x02,&master) || !CodecRead(0x18,&pcm))return EFI_NOT_READY;
        CodecRead(0x7c,&vendor[0]);CodecRead(0x7e,&vendor[1]);              // only for the trace
        charontrace::Value("CHARON es1371 codec",uint64_t(master)|(uint64_t(pcm)<<16)|(uint64_t(vendor[1])<<32)|(uint64_t(vendor[0])<<48));
        if(failed_ || (master&0x8000) || (pcm&0x8000))return EFI_DEVICE_ERROR;
        stage_=5;
        const size_t bytes=ringFrames*4;pages_=(bytes+4095)/4096;
        if(io->AllocateBuffer(io,0,4,pages_,&host_,0)!=EFI_SUCCESS || !host_) {host_=nullptr;return EFI_OUT_OF_RESOURCES;}
        stage_=6;
        size_t mapped=pages_*4096;uint64_t device=0;
        if(io->Map(io,2,host_,&mapped,&device,&mapping_)!=EFI_SUCCESS) {mapping_=nullptr;return EFI_UNSUPPORTED;}
        if(mapped<bytes || (device>>32) || ((device+bytes)>>32) || (device&3))return EFI_UNSUPPORTED;   // the chip takes 32-bit addresses
        ring_=static_cast<int16_t*>(host_);
        for(size_t i=0;i<ringFrames*2;++i)ring_[i]=0;
        stage_=7;
        Out(regPage,pageDac);
        Out(regDac2Frame,uint32_t(device));
        Out(regDac2Size,uint32_t(ringFrames-1));                             // longwords - 1; the current position restarts at 0
        Out(regSerial,serialDac2Stereo16);
        Out(regDac2Count,uint32_t(periodFrames-1));                         // sample counter (no interrupt is taken from it)
        if(In(regDac2Frame)!=uint32_t(device) || In(regDac2Size)!=ringFrames-1 || failed_)return EFI_DEVICE_ERROR;   // size as written, position 0
        Out(regControl,controlDac2);                                        // run
        if(failed_)return EFI_DEVICE_ERROR;
        stage_=8;
        // The channel must really consume the buffer: the position has to leave
        // the start (0, checked above) within half a second.
        bool moving=false;
        for(int t=0;t<500 && !moving;++t) {boot_->Stall(1000);Acknowledge();moving=PositionNow()!=0;}
        if(!moving || failed_)return EFI_DEVICE_ERROR;
        last_=PositionNow();played_=write_=written_=reported_=late_=0;maximumStep_=0;open_=true;
        stage_=9;
        if(boot_->CreateEvent(EVT_TIMER|EVT_NOTIFY_SIGNAL,TPL_NOTIFY,TimerNotify,this,&timer_)!=EFI_SUCCESS) {timer_=nullptr;open_=false;return EFI_DEVICE_ERROR;}
        if(boot_->SetTimer(timer_,TimerPeriodic,100000)!=EFI_SUCCESS) {boot_->CloseEvent(timer_);timer_=nullptr;open_=false;return EFI_DEVICE_ERROR;}
        stage_=0;
        return EFI_SUCCESS;
    }
    // DMA off and the control and serial registers as they were found (never
    // with a channel running: its buffer is about to be freed).
    void Quiet() {
        using namespace charones1371;
        if(!io_ || !saved_)return;
        Out(regControl,0);
        Out(regSerial,serial0_);
        Out(regControl,control0_&~(controlDac1|controlDac2|controlAdc|controlSyncReset));
        saved_=false;
    }
    void Release() {
        if(io_ && mapping_)io_->Unmap(io_,mapping_);
        if(io_ && host_)io_->FreeBuffer(io_,pages_,host_);
        if(io_ && attributesSet_)io_->Attributes(io_,3,0x400,nullptr);   // bus master off again
        mapping_=nullptr;host_=nullptr;ring_=nullptr;io_=nullptr;open_=false;attributesSet_=false;saved_=false;
    }
    static void EFIAPI TimerNotify(efi_event,void* context) {static_cast<Es1371*>(context)->Service();}
    // Played frames are cleared, so a late writer is followed by silence and
    // not by the sound of 341 ms ago.
    // See CharonAc97::Live/Vanished: the firmware can disconnect and reconnect the controller
    // under us (FORCE_RECONNECT, re-enumeration), freeing the driver; the 10 ms service timer must
    // not then call into the freed PciIo (the crash r402 fixed for HDA, now guarded here too).
    bool Live() const {
        efi_guid pciIoGuid={0x4cf5b200,0x68b8,0x4ca5,{0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};void* now=nullptr;
        return io_ && handle_ && boot_ && boot_->HandleProtocol(handle_,&pciIoGuid,&now)==EFI_SUCCESS && now==static_cast<void*>(io_);
    }
    void Vanished() {
        if(timer_) {boot_->SetTimer(timer_,TimerCancel,0);boot_->CloseEvent(timer_);timer_=nullptr;}
        io_=nullptr;handle_=nullptr;mapping_=nullptr;host_=nullptr;ring_=nullptr;open_=false;attributesSet_=false;saved_=false;serving_=false;
    }
    void Service() {
        if(!open_ || serving_)return;
        if(!Live()) {Vanished();return;}
        serving_=true;
        Acknowledge();
        const uint64_t now=PositionNow();
        const uint64_t step=charones1371::Step(now,last_,ringFrames);
        for(uint64_t p=played_;p<played_+step;++p) {const size_t at=size_t(p%ringFrames);ring_[at*2]=ring_[at*2+1]=0;}
        played_+=step;last_=now;
        if(step>maximumStep_)maximumStep_=uint32_t(step);
        serving_=false;
    }
    efi_boot_services* boot_=nullptr;PciIoFull* io_=nullptr;efi_handle handle_=nullptr;
    void* host_=nullptr;void* mapping_=nullptr;size_t pages_=0;int16_t* ring_=nullptr;
    efi_event timer_=nullptr;
    uint64_t played_=0,write_=0,last_=0,written_=0,reported_=0,late_=0;
    uint32_t control0_=0,serial0_=0,failed_=0,maximumStep_=0,stage_=0;
    bool open_=false,attributesSet_=false,saved_=false;volatile bool serving_=false;
};
}
