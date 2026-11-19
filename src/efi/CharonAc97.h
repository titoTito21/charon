// Charon's own AC'97 output (r389). GPL-2.0-or-later.
//
// Older PCs, VirtualBox and QEMU "-device AC97" have an Intel ICH-style AC'97
// controller (PCI class 04/01) and no HDA, so AudioDxe finds nothing. This
// plays the speech mix through it: 48 kHz stereo PCM16, a ring of 32 DMA
// buffers of 512 frames, kept running by moving the last valid index ahead
// of the hardware; a periodic timer clears what was played so nothing old
// repeats. Writes land at least 256 frames ahead of the DMA position and at
// most 4096 (85 ms) ahead, so a newer utterance is heard within ~6-85 ms;
// Discard drops what is queued at once. DMA memory comes from PciIo
// (AllocateBuffer + Map, below 4 GiB). Only controllers known to use the ICH
// register layout are touched: Intel, NVIDIA nForce, AMD, SiS 7012.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <efi/boot-services.h>
namespace charonaudioefi {
struct PciIoFull;
using PciIoAccessFn=efi_status (EFIAPI *)(PciIoFull*,uint32_t width,uint8_t bar,uint64_t offset,size_t count,void* buffer);
using PciIoConfigAccessFn=efi_status (EFIAPI *)(PciIoFull*,uint32_t width,uint32_t offset,size_t count,void* buffer);
struct PciIoFull {
    void* PollMem;void* PollIo;
    struct {PciIoAccessFn Read,Write;} Mem,Io;
    struct {PciIoConfigAccessFn Read,Write;} Pci;
    void* CopyMem;
    efi_status (EFIAPI *Map)(PciIoFull*,uint32_t operation,void* host,size_t* bytes,uint64_t* device,void** mapping);
    efi_status (EFIAPI *Unmap)(PciIoFull*,void* mapping);
    efi_status (EFIAPI *AllocateBuffer)(PciIoFull*,uint32_t type,uint32_t memoryType,size_t pages,void** host,uint64_t attributes);
    efi_status (EFIAPI *FreeBuffer)(PciIoFull*,size_t pages,void* host);
    void* Flush;void* GetLocation;
    efi_status (EFIAPI *Attributes)(PciIoFull*,uint32_t operation,uint64_t attributes,uint64_t* result);
};
class Ac97 {
public:
    static constexpr size_t entries=32,bufferFrames=512,ringFrames=entries*bufferFrames;
    static constexpr uint64_t margin=256,maximumLead=4096;
    efi_status Open(efi_boot_services* boot) {
        if(open_)return EFI_ALREADY_STARTED;
        boot_=boot;
        efi_guid pciIoGuid={0x4cf5b200,0x68b8,0x4ca5,{0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};
        efi_handle* handles=nullptr;size_t count=0;
        if(boot->LocateHandleBuffer(ByProtocol,&pciIoGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return EFI_NOT_FOUND;
        efi_status status=EFI_NOT_FOUND;
        for(size_t i=0;i<count && status!=EFI_SUCCESS;++i) {
            PciIoFull* io=nullptr;
            if(boot->HandleProtocol(handles[i],&pciIoGuid,(void**)&io)!=EFI_SUCCESS || !io)continue;
            uint16_t ids[2]={};uint8_t classCode[3]={};
            if(io->Pci.Read(io,1,0,2,ids)!=EFI_SUCCESS || io->Pci.Read(io,0,9,3,classCode)!=EFI_SUCCESS)continue;
            if(classCode[2]!=0x04 || classCode[1]!=0x01)continue;   // multimedia / audio (AC'97)
            if(ids[0]!=0x8086 && ids[0]!=0x10de && ids[0]!=0x1022 && !(ids[0]==0x1039 && ids[1]==0x7012))continue;
            status=Start(io);
            if(status!=EFI_SUCCESS)Release();
        }
        boot->FreePool(handles);
        return status;
    }
    bool Running() const {return open_;}
    size_t Write(const int16_t* samples,size_t frames) {
        if(!open_ || !samples)return 0;
        const efi_tpl old=boot_->RaiseTPL(TPL_NOTIFY);   // the timer's Service must not run in between
        Service();
        if(write_<played_+margin)write_=played_+margin;      // late: continue after a short silence
        const uint64_t limit=played_+maximumLead;
        if(write_>=limit) {boot_->RestoreTPL(old);return 0;}
        size_t n=frames;if(n>limit-write_)n=size_t(limit-write_);
        for(size_t i=0;i<n;++i) {const size_t at=size_t((write_+i)%ringFrames);ring_[at*2]=samples[i*2];ring_[at*2+1]=samples[i*2+1];}
        write_+=n;boot_->RestoreTPL(old);return n;
    }
    bool Drained() {if(!open_)return true;const efi_tpl old=boot_->RaiseTPL(TPL_NOTIFY);Service();const bool done=write_<=played_;boot_->RestoreTPL(old);return done;}
    void Discard() {
        if(!open_)return;
        const efi_tpl old=boot_->RaiseTPL(TPL_NOTIFY);
        Service();
        const uint64_t keep=played_+margin;
        for(uint64_t p=keep;p<write_;++p) {const size_t at=size_t(p%ringFrames);ring_[at*2]=ring_[at*2+1]=0;}
        if(write_>keep)write_=keep;
        boot_->RestoreTPL(old);
    }
    efi_status Stop() {
        if(!open_)return EFI_SUCCESS;
        if(timer_) {boot_->SetTimer(timer_,TimerCancel,0);boot_->CloseEvent(timer_);timer_=nullptr;}
        uint8_t cr=0;io_->Io.Write(io_,0,1,0x1b,1,&cr);       // stop the PCM out DMA
        cr=2;io_->Io.Write(io_,0,1,0x1b,1,&cr);               // and reset its registers
        Release();return EFI_SUCCESS;
    }
private:
    struct Bdl {uint32_t address;uint16_t samples,flags;};
    efi_status Start(PciIoFull* io) {
        io_=io;
        uint64_t supported=0;
        if(io->Attributes(io,4,0,&supported)!=EFI_SUCCESS)return EFI_UNSUPPORTED;
        if(io->Attributes(io,2,supported&(0x100|0x400),nullptr)!=EFI_SUCCESS)return EFI_UNSUPPORTED;   // I/O decode + bus master
        attributesSet_=true;
        // Cold reset released, wait for the primary codec (up to 1 s).
        uint32_t global=2;if(io->Io.Write(io,2,1,0x2c,1,&global)!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
        bool ready=false;
        for(int t=0;t<1000 && !ready;++t) {uint32_t sta=0;io->Io.Read(io,2,1,0x30,1,&sta);ready=(sta&0x100)!=0;if(!ready)boot_->Stall(1000);}
        if(!ready)return EFI_NOT_READY;
        auto mixer=[&](uint32_t reg,uint16_t value) {io->Io.Write(io,1,0,reg,1,&value);};
        mixer(0x00,0);boot_->Stall(10000);           // codec register reset
        mixer(0x02,0x0000);mixer(0x04,0x0000);       // master and headphone: 0 dB, not muted
        mixer(0x18,0x0808);                          // PCM out: 0 dB
        uint16_t extended=0;io->Io.Read(io,1,0,0x28,1,&extended);
        if(extended&1) {uint16_t vra=1;io->Io.Write(io,1,0,0x2a,1,&vra);uint16_t rate=48000;io->Io.Write(io,1,0,0x2c,1,&rate);}
        // DMA memory: the BDL and the ring in one common buffer.
        const size_t bytes=entries*sizeof(Bdl)+ringFrames*4;pages_=(bytes+4095)/4096;
        if(io->AllocateBuffer(io,0,4,pages_,&host_,0)!=EFI_SUCCESS || !host_) {host_=nullptr;return EFI_OUT_OF_RESOURCES;}
        size_t mapped=pages_*4096;uint64_t device=0;
        if(io->Map(io,2,host_,&mapped,&device,&mapping_)!=EFI_SUCCESS || mapped<bytes || (device>>32) || ((device+bytes)>>32)) {mapping_=nullptr;return EFI_UNSUPPORTED;}
        auto* bdl=static_cast<Bdl*>(host_);ring_=reinterpret_cast<int16_t*>(static_cast<uint8_t*>(host_)+entries*sizeof(Bdl));
        for(size_t i=0;i<ringFrames*2;++i)ring_[i]=0;
        const uint32_t ringDevice=uint32_t(device)+uint32_t(entries*sizeof(Bdl));
        for(size_t i=0;i<entries;++i)bdl[i]={uint32_t(ringDevice+i*bufferFrames*4),uint16_t(bufferFrames*2),0};
        uint8_t cr=2;io->Io.Write(io,0,1,0x1b,1,&cr);   // reset PCM out
        for(int t=0;t<100;++t) {io->Io.Read(io,0,1,0x1b,1,&cr);if(!(cr&2))break;boot_->Stall(100);}
        uint32_t base=uint32_t(device);io->Io.Write(io,2,1,0x10,1,&base);
        uint8_t last=entries-1;io->Io.Write(io,0,1,0x15,1,&last);
        uint16_t clear=0x1c;io->Io.Write(io,1,1,0x16,1,&clear);
        cr=1;if(io->Io.Write(io,0,1,0x1b,1,&cr)!=EFI_SUCCESS)return EFI_DEVICE_ERROR;   // run
        played_=write_=0;last_=0;open_=true;
        if(boot_->CreateEvent(EVT_TIMER|EVT_NOTIFY_SIGNAL,TPL_NOTIFY,TimerNotify,this,&timer_)!=EFI_SUCCESS
            || boot_->SetTimer(timer_,TimerPeriodic,20000)!=EFI_SUCCESS) {timer_=nullptr;Stop();return EFI_DEVICE_ERROR;}
        return EFI_SUCCESS;
    }
    void Release() {
        if(io_ && mapping_)io_->Unmap(io_,mapping_);
        if(io_ && host_)io_->FreeBuffer(io_,pages_,host_);
        if(io_ && attributesSet_)io_->Attributes(io_,3,0x400,nullptr);   // bus master off again
        mapping_=nullptr;host_=nullptr;ring_=nullptr;io_=nullptr;open_=false;attributesSet_=false;
    }
    static void EFIAPI TimerNotify(efi_event,void* context) {static_cast<Ac97*>(context)->Service();}
    // The DMA position (frames, monotonic) from the current index and the
    // samples left in it; played frames are cleared; the last valid index
    // stays one buffer behind the current one, so the DMA never halts.
    void Service() {
        if(!open_ || serving_)return;
        serving_=true;
        uint8_t civ=0;uint16_t picb=0;
        io_->Io.Read(io_,0,1,0x14,1,&civ);io_->Io.Read(io_,1,1,0x18,1,&picb);
        civ&=entries-1;if(picb>bufferFrames*2)picb=bufferFrames*2;
        const uint64_t position=uint64_t(civ)*bufferFrames+(bufferFrames-picb/2);
        const uint64_t now=position%ringFrames;
        const uint64_t step=(now+ringFrames-last_)%ringFrames;
        for(uint64_t p=played_;p<played_+step;++p) {const size_t at=size_t(p%ringFrames);ring_[at*2]=ring_[at*2+1]=0;}
        played_+=step;last_=now;
        uint8_t lvi=uint8_t((civ+entries-1)%entries);io_->Io.Write(io_,0,1,0x15,1,&lvi);
        uint16_t sr=0;io_->Io.Read(io_,1,1,0x16,1,&sr);
        if(sr&1) {uint8_t cr=1;io_->Io.Write(io_,0,1,0x1b,1,&cr);}   // halted anyway: run again
        if(sr&0x1c)io_->Io.Write(io_,1,1,0x16,1,&sr);              // acknowledge status bits
        serving_=false;
    }
    efi_boot_services* boot_=nullptr;PciIoFull* io_=nullptr;
    void* host_=nullptr;void* mapping_=nullptr;size_t pages_=0;int16_t* ring_=nullptr;
    efi_event timer_=nullptr;
    uint64_t played_=0,write_=0,last_=0;
    bool open_=false,attributesSet_=false;volatile bool serving_=false;
};
}
