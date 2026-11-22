// Charon's own USB Audio Class output (r405, widened in r407). GPL-2.0-or-later.
//
// Speech through USB sound cards, USB headsets, USB-C headphone adapters and
// docks on xHCI controllers. What is supported, the rules and their sources are
// written down in USB-AUDIO.txt; the pure logic (descriptors, pacing, feedback,
// the rings) is CharonUsbAudioModel.h, the device quirks CharonUsbAudioQuirks.h.
//
// Firmware cannot do this: the isochronous transfer functions of
// EFI_USB_IO_PROTOCOL and of the xHCI, EHCI and UHCI drivers return
// EFI_UNSUPPORTED in edk2 (nothing else is documented for vendor firmware), and
// their prototypes carry no packet lengths anyway. This file
// therefore drives the isochronous endpoints itself, BESIDE the firmware's
// xHCI driver, which keeps owning the controller (the USB keyboard hangs on
// the same one). Everything the firmware can do is left to it: descriptors,
// SET_INTERFACE and the class requests (sampling frequency, clock, mute,
// volume, selectors) go through UsbIo control transfers. Only three things
// are done directly:
//
// 1. One Configure Endpoint command that points the endpoints (data out, and
//    the feedback endpoint of an asynchronous device) at Charon's own transfer
//    rings. The command ring belongs to the firmware driver, so the command is
//    LENT a slot of it: with interrupts off (TPL_HIGH_LEVEL) the command is
//    written at the ring's enqueue position (found from the newest Command
//    Completion Event still in the firmware's event ring memory), run, and
//    then the slot is restored byte for byte and the controller's dequeue
//    pointer is led back to it by a temporary Link TRB in the following slot.
//    Afterwards ring memory, dequeue pointer and cycle state are exactly as
//    found; CRCR is never written and the command ring is never stopped. The
//    completion event stays in the firmware's event ring: it names a TRB
//    inside the firmware's own command ring, which firmware drivers skip.
// 2. A spare interrupter (a second event ring nobody uses: the firmware driver
//    works on interrupter 0) with its interrupt left disabled. Charon's
//    isochronous TDs name it as their target, so their completion events never
//    reach the firmware's event ring. (edk2's XhcCheckUrbResult ASSERTs on an
//    event whose TRB lies outside its own memory pool.)
// 3. The endpoints' doorbells.
//
// Streaming: a normal transfer ring, one isochronous TD (one TRB, "start as
// soon as possible") per service interval. The ring is kept filled with
// silence several seconds ahead, so it does not run dry while a firmware call
// holds the timer back (a ring that runs dry makes the controller report a
// Ring Underrun to the FIRMWARE's event ring). Speech is copied into the
// queued buffers a few milliseconds ahead of the controller's position, which
// is known exactly from the completion events: latency like the AC'97 backend
// (5..85 ms), and Discard simply silences what was not played yet.
// Asynchronous devices: the feedback endpoint is read through a second ring
// and the TDs a few milliseconds ahead get the lengths the device asks for.
//
// Stop: SET_INTERFACE alternate 0 through UsbIo (edk2 then stops and drops the
// endpoint itself); if the endpoint is still there, Charon drops it with a
// second lent command. A vanished UsbIo instance does the same without UsbIo.
// ExitBootServices (r407): no command and no waiting on firmware services any
// more - the rings are cut short behind the TDs in flight (memory writes), the
// controller's frame counter is watched for at most three service intervals and
// the spare interrupter is switched off. Nothing is allocated or freed.
//
// Every step is checked and anything unexpected ends in a clean refusal that
// leaves the controller as found: not an xHCI, controller halted, no spare
// interrupter, device slot not identified unambiguously, command ring
// position unknown or not idle, memory not described by the memory map,
// 64-bit addresses on a 32-bit controller.
//
// Verified in QEMU with its usb-audio device only (USB Audio Class 1.0, full
// speed, adaptive). Everything else - UAC 2.0 and 3.0, high speed and
// SuperSpeed, feedback, 8-bit and float samples - is written from the
// specifications and covered by host tests of the pure logic, NOT by a device.
// Not supported: EHCI/UHCI/OHCI controllers (see USB-AUDIO.txt, section 6).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <efi/boot-services.h>
#include "CharonAc97.h"   // PciIoFull
#include "CharonTrace.h"
#include "CharonUsbAudioModel.h"

// ============================================================================
// The device: EFI_USB_IO_PROTOCOL for requests, the xHCI directly for the stream.
// ============================================================================
namespace charonaudioefi {
struct UsbAudioRequest {uint8_t requestType,request;uint16_t value,index,length;};
struct UsbAudioDeviceDescriptor {uint8_t length,type;uint16_t usb;uint8_t cls,subClass,protocol,maxPacket;uint16_t vendor,product,device;uint8_t manufacturer,productName,serial,configurations;};
struct UsbAudioConfigHeader {uint8_t length,type;uint16_t totalLength;uint8_t interfaces,value,name,attributes,maxPower;};
struct UsbAudioInterface {uint8_t length,type,number,alternate,endpoints,interfaceClass,subClass,protocol,name;};
struct UsbIoAudio {
    efi_status (EFIAPI *ControlTransfer)(UsbIoAudio*,UsbAudioRequest*,uint32_t direction,uint32_t timeout,void* data,size_t length,uint32_t* status);
    void* BulkTransfer;void* AsyncInterruptTransfer;void* SyncInterruptTransfer;void* IsochronousTransfer;void* AsyncIsochronousTransfer;
    efi_status (EFIAPI *GetDeviceDescriptor)(UsbIoAudio*,UsbAudioDeviceDescriptor*);
    efi_status (EFIAPI *GetConfigDescriptor)(UsbIoAudio*,UsbAudioConfigHeader*);
    efi_status (EFIAPI *GetInterfaceDescriptor)(UsbIoAudio*,UsbAudioInterface*);
    void* GetEndpointDescriptor;void* GetStringDescriptor;void* GetSupportedLanguages;void* PortReset;
};
class UsbAudio {
public:
    // Like the AC'97 backend: speech lands 5..85 ms ahead of the hardware. As a
    // second sink beside the built-in card (whose driver may take sound in
    // larger pieces) up to 400 ms are held.
    static constexpr uint32_t marginMs=5,leadMs=85,mirrorLeadMs=400,timerMs=10,idlePaceMs=30;
    static constexpr uint32_t eventEntries=4096,controlTimeoutMs=500;
    UsbAudio()=default;
    UsbAudio(const UsbAudio&)=delete;UsbAudio& operator=(const UsbAudio&)=delete;
    struct Info {   // diagnostics of the open device
        uint8_t version=0,slot=0,dci=0,feedbackDci=0,channels=0,subframe=0,speed=0;uint16_t vendor=0,product=0;
        uint32_t rate=0,microframes=0,interrupter=0,slots=0,quirks=0;
        uint64_t events=0,errors=0,rebases=0,feedbackErrors=0;
    };
    const Info& Diagnostics() const {return info_;}
    bool Opened() const {return open_;}            // Stop is needed
    bool Running() const {return open_ && !gone_;}
    bool Gone() const {return open_ && gone_;}     // unplugged or taken away: only Stop is left
    efi_handle Handle() const {return usbHandle_;}
    const void* Io() const {return usb_;}
    uint32_t Rank() const {return charonusbaudio::OutputRank(plan_.outputType);}
    static efi_guid UsbIoGuid() {return {0x2b2f68d6,0x0cd2,0x44cf,{0x8e,0x8b,0xbb,0xa2,0x0b,0x1b,0x5b,0x75}};}
    // The USB I/O instance of a handle when it is an AudioStreaming interface (cheap; no transfers).
    static UsbIoAudio* Streaming(efi_boot_services* boot,efi_handle handle) {
        efi_guid guid=UsbIoGuid();UsbIoAudio* io=nullptr;UsbAudioInterface d{};
        if(boot->HandleProtocol(handle,&guid,(void**)&io)!=EFI_SUCCESS || !io || !io->ControlTransfer || !io->GetDeviceDescriptor
            || !io->GetConfigDescriptor || !io->GetInterfaceDescriptor)return nullptr;
        return io->GetInterfaceDescriptor(io,&d)==EFI_SUCCESS && d.interfaceClass==1 && d.subClass==2?io:nullptr;
    }
    // True when a USB AudioStreaming interface exists.
    static bool Present(efi_boot_services* boot) {
        if(!boot || !boot->LocateHandleBuffer || !boot->HandleProtocol || !boot->FreePool)return false;
        efi_guid guid=UsbIoGuid();efi_handle* handles=nullptr;size_t count=0;bool found=false;
        if(boot->LocateHandleBuffer(ByProtocol,&guid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return false;
        for(size_t i=0;i<count && i<1024 && !found;++i)found=Streaming(boot,handles[i])!=nullptr;
        boot->FreePool(handles);return found;
    }
    // Starts the stream of one AudioStreaming interface (silence until Write).
    // EFI_NOT_FOUND: not such an interface. EFI_NOT_READY: called above
    // TPL_CALLBACK, where UsbIo cannot be used, or after the exit signal. On any
    // failure the device is in alternate setting 0 again and the controller is as found.
    efi_status OpenOn(efi_boot_services* boot,efi_handle handle) {
        if(open_)return EFI_ALREADY_STARTED;
        if(!boot || !handle || !boot->LocateHandleBuffer || !boot->HandleProtocol || !boot->FreePool || !boot->RaiseTPL || !boot->RestoreTPL
            || !boot->LocateDevicePath || !boot->GetMemoryMap || !boot->AllocatePool || !boot->Stall || !boot->CreateEvent || !boot->SetTimer || !boot->CloseEvent)return EFI_INVALID_PARAMETER;
        boot_=boot;
        if(exiting_ || CurrentTpl()>TPL_CALLBACK)return EFI_NOT_READY;
        UsbIoAudio* io=Streaming(boot,handle);
        if(!io)return EFI_NOT_FOUND;
        usb_=io;usbHandle_=handle;
        const efi_status status=Start();
        charontrace::Value("CHARON usb audio start",uint64_t(status));
        if(status!=EFI_SUCCESS) {usb_=nullptr;usbHandle_=nullptr;}
        return status;
    }
    // The first USB audio interface that starts. EFI_NOT_FOUND: there is none.
    efi_status Open(efi_boot_services* boot) {
        if(open_)return EFI_ALREADY_STARTED;
        if(!boot || !boot->LocateHandleBuffer || !boot->HandleProtocol || !boot->FreePool)return EFI_INVALID_PARAMETER;
        efi_guid guid=UsbIoGuid();efi_handle* handles=nullptr;size_t count=0;
        if(boot->LocateHandleBuffer(ByProtocol,&guid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return EFI_NOT_FOUND;
        efi_status status=EFI_NOT_FOUND;
        for(size_t i=0;i<count && i<1024 && status!=EFI_SUCCESS;++i)if(Streaming(boot,handles[i]))status=OpenOn(boot,handles[i]);
        boot->FreePool(handles);
        return status;
    }
    // `mirror`: this is the second sink beside another card: a longer lead, and
    // the caller ignores how much was taken.
    size_t Write(const int16_t* samples,size_t frames,bool mirror=false) {
        if(!Running() || !samples || CurrentTpl()>TPL_NOTIFY)return 0;
        const efi_tpl old=boot_->RaiseTPL(TPL_NOTIFY);   // the timer's Service must not run in between
        Service();
        size_t taken=0;
        if(Running()) {
            bool fed=false;
            taken=ring_.Write(samples,frames,charonusbaudio::Tds(mirror?mirrorLeadMs:leadMs,plan_.microframes),&fed);
            if(fed)Doorbell();
        }
        boot_->RestoreTPL(old);return taken;
    }
    bool Drained() {
        if(!Running() || CurrentTpl()>TPL_NOTIFY)return true;
        const efi_tpl old=boot_->RaiseTPL(TPL_NOTIFY);Service();const bool done=!Running() || ring_.Drained();boot_->RestoreTPL(old);return done;
    }
    void Discard() {
        if(!Running() || CurrentTpl()>TPL_NOTIFY)return;
        const efi_tpl old=boot_->RaiseTPL(TPL_NOTIFY);Service();if(Running())ring_.Discard();boot_->RestoreTPL(old);
    }
    // Ends the stream and gives everything back. Above TPL_CALLBACK the device is
    // not told (no SET_INTERFACE 0); the endpoint is still removed from the controller.
    // After the exit signal nothing is done at all (the memory stays allocated).
    efi_status Stop() {
        if(!open_)return EFI_SUCCESS;
        if(exiting_) {
            // The timer is cancelled (no allocation). Outside a notification (the exit failed and the
            // program went on) the events are closed too, so nothing points into this object any more.
            if(timer_)boot_->SetTimer(timer_,TimerCancel,0);
            if(CurrentTpl()==TPL_APPLICATION) {
                if(timer_) {boot_->CloseEvent(timer_);timer_=nullptr;}
                if(exitEvent_) {boot_->CloseEvent(exitEvent_);exitEvent_=nullptr;}
            }
            open_=false;return EFI_SUCCESS;
        }
        if(timer_) {boot_->SetTimer(timer_,TimerCancel,0);boot_->CloseEvent(timer_);timer_=nullptr;}
        if(exitEvent_) {boot_->CloseEvent(exitEvent_);exitEvent_=nullptr;}
        const efi_tpl tpl=CurrentTpl();
        charontrace::Value("CHARON usb audio events",info_.events);
        charontrace::Value("CHARON usb audio errors",info_.errors|(info_.rebases<<32));
        if(plan_.feedbackEndpoint)charontrace::Value("CHARON usb audio feedback values",feedback_.accepted|(feedback_.rejected<<24)|(info_.feedbackErrors<<44));
        bool keepMemory=false;
        if(!detached_ && ControllerLive()) {
            if(SlotLive() && UsbLive() && tpl<=TPL_CALLBACK) {
                // edk2's xHCI driver stops and drops the endpoint itself on this request.
                const efi_status alt=Control(0x01,11,0,plan_.interface,nullptr,0);
                charontrace::Value("CHARON usb audio alt0",uint64_t(alt));
            }
            keepMemory=!Detach();
        }
        Release(keepMemory);
        charontrace::Value("CHARON usb audio stopped",keepMemory?1:0);
        return EFI_SUCCESS;
    }
private:
    using Trb=charonusbaudio::Trb;
    static efi_guid PciIoGuid() {return {0x4cf5b200,0x68b8,0x4ca5,{0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};}
    efi_tpl CurrentTpl() const {const efi_tpl now=boot_->RaiseTPL(TPL_HIGH_LEVEL);boot_->RestoreTPL(now);return now;}

    // ---- controller registers (PciIo, BAR 0) --------------------------------
    uint32_t Read32(uint32_t offset) {
        uint32_t value=0xffffffffu;
        if(!pci_ || pci_->Mem.Read(pci_,2,0,offset,1,&value)!=EFI_SUCCESS) {mmioFault_=true;return 0xffffffffu;}
        return value;
    }
    void Write32(uint32_t offset,uint32_t value) {if(!pci_ || pci_->Mem.Write(pci_,2,0,offset,1,&value)!=EFI_SUCCESS)mmioFault_=true;}
    uint64_t Read64(uint32_t offset) {const uint64_t low=Read32(offset);return low|uint64_t(Read32(offset+4))<<32;}
    void Write64(uint32_t offset,uint64_t value) {Write32(offset,uint32_t(value));Write32(offset+4,uint32_t(value>>32));}
    uint32_t Interrupter(uint32_t n) const {return runtime_+0x20+32*n;}
    // Running, not halted, no host system error, ready.
    bool ControllerRuns() {
        const uint32_t status=Read32(operational_+4),command=Read32(operational_);
        return !mmioFault_ && status!=0xffffffffu && !(status&((1u<<0)|(1u<<2)|(1u<<11)|(1u<<12))) && (command&1);
    }
    void Doorbell() {Write32(doorbell_+4u*info_.slot,info_.dci);}

    // ---- memory the firmware driver owns: only read where the memory map says RAM
    efi_status SnapshotMemoryMap() {
        if(map_) {boot_->FreePool(map_);map_=nullptr;}
        for(int attempt=0;attempt<4;++attempt) {
            size_t size=0,key=0,each=0;uint32_t version=0;
            const efi_status probe=boot_->GetMemoryMap(&size,nullptr,&key,&each,&version);
            if(probe!=EFI_BUFFER_TOO_SMALL || each<sizeof(efi_memory_descriptor) || !size)return EFI_DEVICE_ERROR;
            size+=16*each;void* buffer=nullptr;
            if(boot_->AllocatePool(EfiBootServicesData,size,&buffer)!=EFI_SUCCESS || !buffer)return EFI_OUT_OF_RESOURCES;
            if(boot_->GetMemoryMap(&size,static_cast<efi_memory_descriptor*>(buffer),&key,&each,&version)==EFI_SUCCESS) {
                map_=static_cast<uint8_t*>(buffer);mapSize_=size;mapEach_=each;return EFI_SUCCESS;
            }
            boot_->FreePool(buffer);
        }
        return EFI_DEVICE_ERROR;
    }
    bool Readable(uint64_t address,uint64_t bytes) const {
        if(!map_ || !address || !bytes || address+bytes<address)return false;
        for(size_t at=0;at+mapEach_<=mapSize_;at+=mapEach_) {
            const auto* d=reinterpret_cast<const efi_memory_descriptor*>(map_+at);
            // Not free memory, not unusable, not memory-mapped I/O.
            const bool ram=d->Type==EfiReservedMemoryType || d->Type==EfiLoaderCode || d->Type==EfiLoaderData || d->Type==EfiBootServicesCode
                || d->Type==EfiBootServicesData || d->Type==EfiRuntimeServicesCode || d->Type==EfiRuntimeServicesData
                || d->Type==EfiACPIReclaimMemory || d->Type==EfiACPIMemoryNVS;
            if(ram && address>=d->PhysicalStart && address+bytes<=d->PhysicalStart+d->NumberOfPages*4096)return true;
        }
        return false;
    }
    // Device addresses of firmware structures are used as host addresses: true
    // on PCs without a DMA remapping translation (checked for our own buffer).
    template<typename T> static T* Host(uint64_t address) {return reinterpret_cast<T*>(uintptr_t(address));}
    bool AddressOk(uint64_t address) const {return address && !(address&0xf) && (wide_ || !(address>>32)) && address==uint64_t(uintptr_t(address));}

    // ---- UsbIo requests ------------------------------------------------------
    efi_status Control(uint8_t type,uint8_t request,uint16_t value,uint16_t index,void* data,uint16_t length) {
        UsbAudioRequest r{type,request,value,index,length};uint32_t result=0;
        const uint32_t direction=!length?2u:(type&0x80)?0u:1u;   // EfiUsbNoData, EfiUsbDataIn, EfiUsbDataOut
        const efi_status status=usb_->ControlTransfer(usb_,&r,direction,controlTimeoutMs,data,length,&result);
        if((type&0x60)==0x20) {   // class requests: the pause some devices need after each one (kernel quirk data)
            using namespace charonusbaudio;
            if(quirks_&quirk::delay20ms)boot_->Stall(20000);
            else if(quirks_&quirk::delay1ms)boot_->Stall(1000);
            else if(quirks_&quirk::delay5ms)boot_->Stall(5000);
        }
        return status;
    }
    efi_status SetInterface(uint8_t alternate) {
        efi_status status=Control(0x01,11,alternate,plan_.interface,nullptr,0);
        if(status!=EFI_SUCCESS) {
            // edk2 reports its own Configure Endpoint failure here although the device took the request.
            uint8_t now=0xff;
            if(Control(0x81,10,0,plan_.interface,&now,1)==EFI_SUCCESS && now==alternate)status=EFI_SUCCESS;
        }
        if(quirks_&charonusbaudio::quirk::interfaceDelay)boot_->Stall(50000);
        return status;
    }
    bool UsbLive() const {
        efi_guid guid=UsbIoGuid();void* now=nullptr;
        return usb_ && usbHandle_ && boot_->HandleProtocol(usbHandle_,&guid,&now)==EFI_SUCCESS && now==static_cast<void*>(usb_);
    }
    bool PciLive() const {
        efi_guid guid=PciIoGuid();void* now=nullptr;
        return pci_ && pciHandle_ && boot_->HandleProtocol(pciHandle_,&guid,&now)==EFI_SUCCESS && now==static_cast<void*>(pci_);
    }
    // The controller is still the running one found at Open.
    bool ControllerLive() {
        if(!PciLive())return false;
        mmioFault_=false;
        return ControllerRuns() && (Read64(operational_+0x30)&~0x3full)==dcbaa_ && !mmioFault_;
    }
    // The device slot still has the device context found at Open.
    bool SlotLive() const {return (*Host<volatile uint64_t>(dcbaa_+8u*info_.slot)&~0x3full)==context_;}
    uint32_t EndpointState(uint32_t dci) const {return *Host<volatile uint32_t>(context_+uint64_t(contextSize_)*dci)&7;}

    // ---- opening ---------------------------------------------------------------
    efi_status Start() {
        using namespace charonusbaudio;
        mmioFault_=false;detached_=false;gone_=false;configured_=false;info_=Info{};quirks_=0;fb_.Clear();fbDevice_=0;
        efi_status status=Identify();
        charontrace::Value("CHARON usb audio identify",uint64_t(status)|(uint64_t(info_.slot)<<8)|(uint64_t(info_.speed)<<16));
        if(status!=EFI_SUCCESS) {Release(false);return status;}
        charontrace::Value("CHARON usb audio device",uint64_t(info_.product)|(uint64_t(info_.vendor)<<16)|(uint64_t(quirks_)<<32));
        status=Describe();
        charontrace::Value("CHARON usb audio format",uint64_t(plan_.rate)|(uint64_t(plan_.channels)<<32)|(uint64_t(plan_.subframe)<<40)|(uint64_t(plan_.version)<<48)|(uint64_t(plan_.alternate)<<56));
        if(status!=EFI_SUCCESS) {Release(false);return status;}
        // endpoint, attributes, packet size, service interval, feedback endpoint, encoding
        charontrace::Value("CHARON usb audio endpoint",uint64_t(plan_.endpoint)|(uint64_t(plan_.attributes)<<8)|(uint64_t(plan_.maxPacket)<<16)|(uint64_t(plan_.microframes)<<32)|(uint64_t(plan_.feedbackEndpoint)<<40)|(uint64_t(plan_.encoding)<<48)|(uint64_t(plan_.interface)<<56));
        // best output terminal type, output terminals, feature units, selector units
        charontrace::Value("CHARON usb audio output",uint64_t(plan_.outputType)|(uint64_t(plan_.outputs)<<16)|(uint64_t(plan_.unitCount)<<24)|(uint64_t(plan_.selectorCount)<<32));
        if(plan_.implicit)charontrace::Value("CHARON usb audio asynchronous without feedback endpoint, nominal rate",plan_.rate);
        Controls();
        if(quirks_&quirk::interfaceReset)SetInterface(0);
        // UAC 2/3: the clock first, then the alternate setting (the other way round for the devices the kernel lists).
        const bool clockFirst=plan_.version>=2 && !(quirks_&quirk::interfaceFirst);
        if(clockFirst && (status=SetClock())!=EFI_SUCCESS) {Release(false);return status;}
        status=SetInterface(plan_.alternate);
        if(status==EFI_SUCCESS && plan_.version>=2 && !clockFirst)status=SetClock();
        if(status==EFI_SUCCESS && plan_.version==1)status=SetEndpointRate();
        if(status!=EFI_SUCCESS) {Control(0x01,11,0,plan_.interface,nullptr,0);Release(false);return status;}
        status=Attach();
        charontrace::Value("CHARON usb audio attach",uint64_t(status)|(uint64_t(info_.interrupter)<<8)|(uint64_t(lastCode_)<<16));
        if(status!=EFI_SUCCESS && plan_.feedbackEndpoint) {
            // Once more without the feedback endpoint (the nominal rate is sent then).
            const bool kept=!Detach();
            FreeDma(kept);
            detached_=false;configured_=false;plan_.feedbackEndpoint=0;fb_.Clear();fbDevice_=0;info_.feedbackDci=0;
            status=Attach();
            charontrace::Value("CHARON usb audio attach without feedback",uint64_t(status)|(uint64_t(lastCode_)<<16));
        }
        if(status!=EFI_SUCCESS) {
            // The endpoint may already point at our ring (the command ran but the result was not as expected).
            const bool keepMemory=!Detach();
            Control(0x01,11,0,plan_.interface,nullptr,0);
            Release(keepMemory);return status;
        }
        open_=true;
        info_.version=plan_.version;info_.channels=plan_.channels;info_.subframe=plan_.subframe;info_.rate=plan_.rate;info_.microframes=plan_.microframes;
        idle_=0;stalls_=0;lastIndex_=Read32(runtime_)&0x3fff;lastDone_=0;tick_=1;fbLastDone_=0;fbStamp_=0;fbValueStamp_=0;
        if(ring_.Feed())Doorbell();
        if(fb_.Feed())FeedbackDoorbell();
        // The stream must really run: the first completion events within 200 ms.
        // (A controller that would not report to the spare interrupter is refused here.)
        bool started=false;
        for(int waited=0;waited<400 && !started && !gone_;++waited) {boot_->Stall(500);Service();started=ring_.Done()>0;}
        charontrace::Value("CHARON usb audio first events",info_.events|(started?0:1ull<<63));
        if(!started) {Stop();return EFI_DEVICE_ERROR;}
        efi_guid exitGroup=EFI_EVENT_GROUP_EXIT_BOOT_SERVICES;
        if(boot_->CreateEvent(EVT_TIMER|EVT_NOTIFY_SIGNAL,TPL_NOTIFY,TimerNotify,this,&timer_)!=EFI_SUCCESS) {timer_=nullptr;Stop();return EFI_DEVICE_ERROR;}
        if(!boot_->CreateEventEx || boot_->CreateEventEx(EVT_NOTIFY_SIGNAL,TPL_NOTIFY,ExitNotify,this,&exitGroup,&exitEvent_)!=EFI_SUCCESS) {exitEvent_=nullptr;Stop();return EFI_DEVICE_ERROR;}
        if(boot_->SetTimer(timer_,TimerPeriodic,timerMs*10000)!=EFI_SUCCESS) {Stop();return EFI_DEVICE_ERROR;}
        return EFI_SUCCESS;
    }
    // The xHCI controller the device hangs on, and the device's slot in it. Reads only.
    efi_status Identify() {
        UsbAudioDeviceDescriptor device{};
        if(usb_->GetDeviceDescriptor(usb_,&device)!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
        info_.vendor=device.vendor;info_.product=device.product;configurations_=device.configurations;
        quirks_=charonusbaudio::QuirksOf(device.vendor,device.product);info_.quirks=quirks_;
        efi_guid pathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,pciGuid=PciIoGuid();
        efi_device_path_protocol* path=nullptr;
        if(boot_->HandleProtocol(usbHandle_,&pathGuid,(void**)&path)!=EFI_SUCCESS || !path)return EFI_UNSUPPORTED;
        efi_device_path_protocol* rest=path;efi_handle controller=nullptr;
        if(boot_->LocateDevicePath(&pciGuid,&rest,&controller)!=EFI_SUCCESS || !controller || !rest)return EFI_UNSUPPORTED;
        // The rest of the path: one USB(port,interface) node per tier (root port, then each hub's port).
        uint8_t ports[6];size_t tiers=0;
        const uint8_t* node=reinterpret_cast<const uint8_t*>(rest);
        for(int guard=0;guard<16;++guard) {
            const size_t length=size_t(node[2])|size_t(node[3])<<8;
            if(node[0]==0x7f || length<4)break;
            if(node[0]!=3 || node[1]!=5 || length<6 || tiers>=6)return EFI_UNSUPPORTED;   // not a plain USB path
            ports[tiers++]=node[4];
            node+=length;
        }
        if(!tiers)return EFI_UNSUPPORTED;
        PciIoFull* io=nullptr;uint8_t classCode[3]={};
        if(boot_->HandleProtocol(controller,&pciGuid,(void**)&io)!=EFI_SUCCESS || !io)return EFI_UNSUPPORTED;
        if(io->Pci.Read(io,0,9,3,classCode)!=EFI_SUCCESS || classCode[2]!=0x0c || classCode[1]!=0x03)return EFI_UNSUPPORTED;
        if(classCode[0]!=0x30) {
            // UHCI 0x00, OHCI 0x10, EHCI 0x20: no isochronous transfers in the firmware's driver and none of ours (USB-AUDIO.txt, 6).
            charontrace::Value("CHARON usb audio on a controller that is not xHCI, programming interface",classCode[0]);
            return EFI_UNSUPPORTED;
        }
        pci_=io;pciHandle_=controller;
        const uint32_t capability=Read32(0),structural=Read32(4),capabilities=Read32(0x10);
        const uint32_t length=capability&0xff;
        doorbell_=Read32(0x14)&~3u;runtime_=Read32(0x18)&~0x1fu;operational_=length;
        if(mmioFault_ || capability==0xffffffffu || length<0x20 || !doorbell_ || !runtime_)return EFI_UNSUPPORTED;
        wide_=(capabilities&1)!=0;contextSize_=(capabilities&4)?64:32;
        interrupters_=(structural>>8)&0x7ff;
        const uint32_t maxSlots=structural&0xff,rootPorts=structural>>24;
        if(!ControllerRuns())return EFI_NOT_READY;
        const uint32_t enabled=Read32(operational_+0x38)&0xff;
        dcbaa_=Read64(operational_+0x30)&~0x3full;
        if(mmioFault_ || !enabled || enabled>maxSlots || !AddressOk(dcbaa_))return EFI_UNSUPPORTED;
        efi_status status=SnapshotMemoryMap();
        if(status!=EFI_SUCCESS)return status;
        if(!Readable(dcbaa_,8ull*(enabled+1)))return EFI_UNSUPPORTED;
        // The slot whose root hub port and route string are this device path.
        // edk2 numbers ParentPortNumber from 0; if nothing matches, from 1 is tried.
        uint32_t found=0,matches=0;
        for(int base=1;base>=0 && !matches;--base) {
            uint32_t route=0;bool valid=uint32_t(ports[0])+base>=1 && uint32_t(ports[0])+base<=rootPorts;
            for(size_t t=1;t<tiers;++t) {
                const uint32_t port=uint32_t(ports[t])+base;
                if(!port)valid=false;
                route|=(port>15?15u:port)<<(4*(t-1));
            }
            if(!valid)continue;
            for(uint32_t slot=1;slot<=enabled;++slot) {
                const uint64_t context=*Host<volatile uint64_t>(dcbaa_+8u*slot)&~0x3full;
                if(!context)continue;
                if(!AddressOk(context) || !Readable(context,uint64_t(contextSize_)*32))continue;
                const volatile uint32_t* s=Host<volatile uint32_t>(context);
                const uint32_t state=s[3]>>27;
                if((s[0]&0xfffff)!=route || ((s[1]>>16)&0xff)!=uint32_t(ports[0])+base || (state!=2 && state!=3))continue;
                // Control endpoint: running, with the device's bMaxPacketSize0 (SuperSpeed: 512, told as exponent 9).
                const volatile uint32_t* ep0=Host<volatile uint32_t>(context+contextSize_);
                const uint32_t speed=(s[0]>>20)&0xf,packet0=speed>=charonusbaudio::speedSuper && device.maxPacket==9?512u:device.maxPacket;
                if((ep0[1]>>16)!=packet0)continue;
                found=slot;++matches;context_=context;info_.speed=uint8_t(speed);
            }
        }
        if(matches!=1)return EFI_NOT_FOUND;
        info_.slot=uint8_t(found);
        // Full speed (1), high speed (3), SuperSpeed (4 and above). Low speed has no isochronous endpoints.
        if(info_.speed!=charonusbaudio::speedFull && info_.speed<charonusbaudio::speedHigh)return EFI_UNSUPPORTED;
        return EFI_SUCCESS;
    }
    // The configuration descriptor and what to play on.
    efi_status Describe() {
        using namespace charonusbaudio;
        UsbAudioConfigHeader header{};UsbAudioInterface own{};
        if(usb_->GetConfigDescriptor(usb_,&header)!=EFI_SUCCESS || header.totalLength<9 || header.totalLength>16384)return EFI_DEVICE_ERROR;
        if(usb_->GetInterfaceDescriptor(usb_,&own)!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
        void* buffer=nullptr;
        if(boot_->AllocatePool(EfiBootServicesData,header.totalLength,&buffer)!=EFI_SUCCESS || !buffer)return EFI_OUT_OF_RESOURCES;
        // The configuration the firmware selected, by its value (edk2 selects the first one; a device
        // with a UAC 3.0 configuration offers UAC 1.0 or 2.0 in the first). The firmware's choice is kept:
        // SET_CONFIGURATION would make the firmware tear down every interface of the device, its HID keys too.
        // (edk2's xHCI driver keeps its own copy of this answer and takes every interface to be in alternate setting 0, which is true here.)
        efi_status status=EFI_NOT_FOUND;
        const uint8_t* bytes=static_cast<const uint8_t*>(buffer);
        for(uint16_t index=0;index<(configurations_?configurations_:1) && index<8 && status!=EFI_SUCCESS;++index) {
            memset(buffer,0,header.totalLength);
            if(Control(0x80,6,uint16_t(0x0200|index),0,buffer,header.totalLength)==EFI_SUCCESS && bytes[1]==2 && bytes[5]==header.value)status=EFI_SUCCESS;
        }
        Plan plan;
        if(status==EFI_SUCCESS && !Parse(bytes,header.totalLength,info_.speed,own.number,&plan))status=EFI_UNSUPPORTED;
        boot_->FreePool(buffer);
        if(status!=EFI_SUCCESS)return status;
        plan_=plan;
        if(quirks_&quirk::lengthHeader)return EFI_UNSUPPORTED;   // packets with a length header (two Zoom recorders): not implemented
        if(plan_.version==3 && !plan_.badd) {
            // UAC 3.0: the number of channels stands in the cluster descriptor (a high capability descriptor).
            uint8_t cluster[7]={};   // wLength, bDescriptorType, bDescriptorSubtype, wDescriptorID, bNrChannels
            if(Control(0xa1,0x06,plan_.cluster,plan_.controlInterface,cluster,7)!=EFI_SUCCESS || !cluster[6] || cluster[6]>32)return EFI_UNSUPPORTED;
            plan_.channels=cluster[6];
        }
        if(plan_.version>=2 && !plan_.badd) {
            ChooseClock();
            uint32_t rate=0;
            if(plan_.clock) {
                // Sampling frequency ranges of the clock source: wNumSubRanges, then (min, max, res) of 32 bits each.
                uint8_t* r=scratch_;memset(scratch_,0,sizeof(scratch_));
                const uint16_t clockIndex=uint16_t(plan_.clock<<8|plan_.controlInterface);
                if(Control(0xa1,0x02,0x0100,clockIndex,r,2)==EFI_SUCCESS && (r[0]|r[1])) {
                    size_t ranges=size_t(r[0])|size_t(r[1])<<8;if(ranges>20)ranges=20;
                    if(Control(0xa1,0x02,0x0100,clockIndex,r,uint16_t(2+12*ranges))!=EFI_SUCCESS)ranges=0;
                    for(size_t k=0;k<ranges && 2+12*k+12<=sizeof(scratch_);++k) {
                        uint32_t low,high;memcpy(&low,r+2+12*k,4);memcpy(&high,r+6+12*k,4);
                        const uint32_t candidate=PreferRate(low,high);
                        Plan fit=plan_;fit.rate=candidate;
                        if(Fits(fit) && (!rate || RateRank(candidate)<RateRank(rate)))rate=candidate;
                    }
                }
                if(!plan_.clockSettable || !rate) {
                    // A fixed clock: what it runs at.
                    uint32_t now=0;
                    if(Control(0xa1,0x01,0x0100,clockIndex,&now,4)==EFI_SUCCESS && now)rate=now;
                    else if(!rate && !(quirks_&quirk::ignoreClockSource))return EFI_DEVICE_ERROR;
                }
            } else if(!(quirks_&quirk::ignoreClockSource))return EFI_UNSUPPORTED;
            if(!rate)rate=48000;   // a hard-wired clock that answers nothing (kernel quirk data)
            plan_.rate=rate;
        }
        if(!Fits(plan_))return EFI_UNSUPPORTED;
        return EFI_SUCCESS;
    }
    bool ClockValid(const charonusbaudio::Clock& source) {
        if(!source.validity)return true;
        uint8_t valid=1;
        return Control(0xa1,0x01,0x0200,uint16_t(source.id<<8|plan_.controlInterface),&valid,1)!=EFI_SUCCESS || valid!=0;
    }
    // UAC 2/3: the clock source the selectors point at. When it is not valid (an absent S/PDIF
    // input, say) and the selector can be written, the first valid input is selected.
    void ChooseClock() {
        using namespace charonusbaudio;
        const uint16_t interface=plan_.controlInterface;
        auto pin=[&](const Clock& selector)->uint8_t {
            uint8_t now=0;
            return Control(0xa1,0x01,0x0100,uint16_t(selector.id<<8|interface),&now,1)==EFI_SUCCESS?now:uint8_t(1);
        };
        const Clock* selector=nullptr;
        const Clock* source=ResolveClock(plan_,pin,&selector);
        bool valid=source && ClockValid(*source);
        if(source && !valid && selector && selector->selectable && !(quirks_&quirk::skipClockSelector)) {
            for(uint8_t candidate=1;candidate<=selector->sourceCount && !valid;++candidate) {
                uint8_t value=candidate;
                if(Control(0x21,0x01,0x0100,uint16_t(selector->id<<8|interface),&value,1)!=EFI_SUCCESS)continue;
                const Clock* tried=ResolveClock(plan_,pin);
                if(tried && ClockValid(*tried)) {source=tried;valid=true;}
            }
        }
        plan_.clock=source?source->id:uint8_t(0);plan_.clockSettable=source && source->settable;
        charontrace::Value("CHARON usb audio clock",uint64_t(plan_.clock)|(plan_.clockSettable?1u<<8:0u)|(valid?1u<<9:0u)|(selector?uint64_t(selector->id)<<16:0));
    }
    // UAC 2/3: the clock source runs at the chosen rate (set when it can be and differs; read
    // back; the rate the device kept is the one played).
    efi_status SetClock() {
        using namespace charonusbaudio;
        if(plan_.badd || !plan_.clock)return EFI_SUCCESS;   // fixed 48 kHz / a hard-wired clock
        const uint16_t index=uint16_t(plan_.clock<<8|plan_.controlInterface);
        uint32_t now=0;
        const bool readable=Control(0xa1,0x01,0x0100,index,&now,4)==EFI_SUCCESS;
        if(plan_.clockSettable && (!readable || now!=plan_.rate)) {
            uint32_t rate=plan_.rate;
            const efi_status set=Control(0x21,0x01,0x0100,index,&rate,4);
            charontrace::Value("CHARON usb audio clock set",uint64_t(plan_.rate)|(set==EFI_SUCCESS?0:1ull<<63));
            if(set!=EFI_SUCCESS && !readable)return set;
        }
        uint32_t actual=0;
        if(Control(0xa1,0x01,0x0100,index,&actual,4)==EFI_SUCCESS && actual && actual!=plan_.rate) {
            Plan kept=plan_;kept.rate=actual;
            if(!Fits(kept))return EFI_UNSUPPORTED;
            plan_.rate=actual;
            charontrace::Value("CHARON usb audio clock kept its rate",actual);
        }
        return EFI_SUCCESS;
    }
    // UAC 1.0: the sampling frequency control of the endpoint.
    efi_status SetEndpointRate() {
        using namespace charonusbaudio;
        if(!plan_.endpointRate)return EFI_SUCCESS;
        if((quirks_&quirk::fixedRate) && plan_.rateCount==1)return EFI_SUCCESS;
        uint8_t rate[3]={uint8_t(plan_.rate),uint8_t(plan_.rate>>8),uint8_t(plan_.rate>>16)},now[3]={0,0,0};
        if(Control(0x22,0x01,0x0100,plan_.endpoint,rate,3)==EFI_SUCCESS)return EFI_SUCCESS;
        // Refused: fine when the endpoint says it runs at that rate anyway, or has no other.
        if(!(quirks_&quirk::noRateReadBack) && Control(0xa2,0x81,0x0100,plan_.endpoint,now,3)==EFI_SUCCESS
            && (uint32_t(now[0])|uint32_t(now[1])<<8|uint32_t(now[2])<<16)==plan_.rate)return EFI_SUCCESS;
        return plan_.rateCount==1?EFI_SUCCESS:EFI_DEVICE_ERROR;
    }
    // Selector units on the way switched to the stream; feature units on the way to every output
    // the stream reaches: not muted, volume at its maximum (0 dB when the range crosses it).
    void Controls() {
        const uint16_t interface=plan_.controlInterface;
        for(size_t i=0;i<plan_.selectorCount;++i) {
            // UAC 1.0 selector units have one control (wValue 0); 2.0 and 3.0 name it (selector control 1).
            uint8_t pin=plan_.selectors[i].pin;
            const efi_status set=Control(0x21,0x01,plan_.version==1?0:0x0100,uint16_t(plan_.selectors[i].id<<8|interface),&pin,1);
            charontrace::Value("CHARON usb audio selector",uint64_t(plan_.selectors[i].id)|(uint64_t(plan_.selectors[i].pin)<<8)|(set==EFI_SUCCESS?0:1ull<<63));
        }
        for(size_t i=0;i<plan_.unitCount;++i) {
            const charonusbaudio::Unit& u=plan_.units[i];
            for(uint16_t channel=0;channel<32;++channel) {
                const uint16_t index=uint16_t(u.id<<8|interface);
                if(u.mute&(1u<<channel)) {uint8_t off=0;Control(0x21,0x01,uint16_t(0x0100|channel),index,&off,1);}
                if(!(u.volume&(1u<<channel)))continue;
                int16_t low=0,high=0;bool range=false;
                if(plan_.version==1)range=Control(0xa1,0x82,uint16_t(0x0200|channel),index,&low,2)==EFI_SUCCESS && Control(0xa1,0x83,uint16_t(0x0200|channel),index,&high,2)==EFI_SUCCESS;
                else {
                    uint8_t r[8]={};
                    if(Control(0xa1,0x02,uint16_t(0x0200|channel),index,r,8)==EFI_SUCCESS && (r[0]|r[1])) {memcpy(&low,r+2,2);memcpy(&high,r+4,2);range=true;}
                }
                if(!range || low>=high)continue;   // no range, or a broken one: the device's own setting stays
                int16_t volume=charonusbaudio::PickVolume(low,high);
                const efi_status set=Control(0x21,0x01,uint16_t(0x0200|channel),index,&volume,2);
                charontrace::Value("CHARON usb audio volume",uint64_t(uint16_t(volume))|(uint64_t(uint16_t(low))<<16)|(uint64_t(uint16_t(high))<<32)|(uint64_t(channel)<<48)|(set==EFI_SUCCESS?0:1ull<<63));
            }
        }
    }
    // Memory, the spare interrupter, and the endpoints in the controller.
    efi_status Attach() {
        using namespace charonusbaudio;
        info_.dci=uint8_t((plan_.endpoint&0x0f)*2);   // OUT endpoint n: device context index 2n
        info_.feedbackDci=plan_.feedbackEndpoint?uint8_t((plan_.feedbackEndpoint&0x0f)*2+1):uint8_t(0);   // IN endpoint n: 2n+1
        if(!ControllerLive() || !SlotLive())return EFI_DEVICE_ERROR;
        // A spare interrupter: the highest one nobody set up.
        uint32_t spare=0;
        if(interrupters_<2)return EFI_UNSUPPORTED;
        for(uint32_t n=interrupters_>1024?1023:interrupters_-1;n>=1 && !spare;--n) {
            const uint32_t at=Interrupter(n);
            if(!(Read32(at)&2) && !(Read32(at+8)&0xffff) && !Read64(at+0x10) && !(Read64(at+0x18)&~0xfull))spare=n;
        }
        if(mmioFault_ || !spare)return EFI_UNSUPPORTED;
        info_.interrupter=spare;
        // One DMA block: segment table, input context, event ring, transfer ring, feedback ring, lengths, data.
        const Pacing pacing{plan_.rate,plan_.microframes};
        const uint32_t frameBytes=uint32_t(plan_.channels)*plan_.subframe;
        const uint32_t stride=StrideFor(TdFrames(plan_)*frameBytes),slots=RingSlots(plan_.microframes,stride);
        const uint32_t fbSlots=info_.feedbackDci?RingSlots(plan_.feedbackMicroframes>8?8:plan_.feedbackMicroframes,8):0;
        const size_t tableAt=0,inputAt=4096,eventsAt=8192,ringAt=eventsAt+eventEntries*sizeof(Trb);
        const size_t fbRingAt=ringAt+(size_t(slots)+1)*sizeof(Trb),fbDataAt=fbRingAt+(fbSlots?(size_t(fbSlots)+1)*sizeof(Trb):0);
        const size_t lengthsAt=(fbDataAt+size_t(fbSlots)*8+15)&~size_t(15);
        const size_t dataAt=(lengthsAt+size_t(slots)*sizeof(uint16_t)+4095)&~size_t(4095),bytes=dataAt+size_t(slots)*stride;
        pages_=(bytes+4095)/4096;
        if(pci_->AllocateBuffer(pci_,0,EfiBootServicesData,pages_,&host_,0)!=EFI_SUCCESS || !host_) {host_=nullptr;return EFI_OUT_OF_RESOURCES;}
        size_t mapped=pages_*4096;uint64_t device=0;
        if(pci_->Map(pci_,2,host_,&mapped,&device,&mapping_)!=EFI_SUCCESS || mapped<bytes) {mapping_=nullptr;return EFI_UNSUPPORTED;}
        // No translation between host and device addresses (firmware structures are read by their device address).
        if(device!=uint64_t(uintptr_t(host_)) || (device&4095) || (!wide_ && ((device+bytes)>>32)))return EFI_UNSUPPORTED;
        device_=device;
        uint8_t* base=static_cast<uint8_t*>(host_);
        memset(base,0,dataAt);
        input_=base+inputAt;events_=reinterpret_cast<Trb*>(base+eventsAt);
        IsoRing::Layout layout;
        layout.trbs=reinterpret_cast<Trb*>(base+ringAt);layout.trbsDevice=device+ringAt;
        layout.data=base+dataAt;layout.dataDevice=device+dataAt;layout.slots=slots;layout.stride=stride;layout.maxFrames=TdFrames(plan_);
        layout.lengths=reinterpret_cast<uint16_t*>(base+lengthsAt);
        layout.pacing=pacing;layout.channels=plan_.channels;layout.subframe=plan_.subframe;layout.encoding=plan_.encoding;
        layout.packet=plan_.maxPacket&0x7ff;layout.burst=plan_.burst;
        layout.interrupter=spare;layout.eventEvery=Tds(1,plan_.microframes);
        layout.margin=Tds(marginMs,plan_.microframes);layout.horizon=slots-64;
        if(!ring_.Init(layout))return EFI_UNSUPPORTED;
        ringDevice_=layout.trbsDevice;
        info_.slots=slots;
        uint64_t fbTrbs=0;
        if(fbSlots) {
            FeedbackRing::Layout f;
            f.trbs=reinterpret_cast<Trb*>(base+fbRingAt);f.trbsDevice=fbTrbs=device+fbRingAt;
            f.data=base+fbDataAt;f.dataDevice=device+fbDataAt;f.slots=fbSlots;f.packet=plan_.feedbackPacket;
            f.interrupter=spare;f.eventEvery=Tds(4,plan_.feedbackMicroframes);f.horizon=fbSlots-64;
            if(!fb_.Init(f))return EFI_UNSUPPORTED;
            fbDevice_=fbTrbs;
            feedback_.Init(plan_.rate,plan_.microframes,ring_.MaxFrames());
        }
        // The event ring of the spare interrupter: one segment, interrupt disabled.
        uint64_t* table=reinterpret_cast<uint64_t*>(base+tableAt);
        table[0]=device+eventsAt;table[1]=eventEntries;
        eventAt_=0;eventCycle_=1;
        const uint32_t at=Interrupter(spare);
        Write32(at,1);                                   // interrupt pending cleared, enable off
        Write32(at+8,1);                                 // one segment
        Write64(at+0x18,(device+eventsAt)|8);            // dequeue pointer
        Write64(at+0x10,device+tableAt);                 // the table: starts the ring
        if(mmioFault_)return EFI_DEVICE_ERROR;
        interrupterSet_=true;
        // The endpoint contexts: isochronous OUT (and the feedback IN) on our rings. If the
        // firmware already enabled an endpoint (edk2 skips isochronous ones on SET_INTERFACE,
        // other firmware may not), it is dropped and added again in the same command.
        const volatile uint32_t* slot=Host<volatile uint32_t>(context_);
        memset(input_,0,size_t(contextSize_)*33);
        uint32_t* control=reinterpret_cast<uint32_t*>(input_);
        control[0]=(EndpointState(info_.dci)?1u<<info_.dci:0)|(info_.feedbackDci && EndpointState(info_.feedbackDci)?1u<<info_.feedbackDci:0);
        control[1]=1u|1u<<info_.dci|(info_.feedbackDci?1u<<info_.feedbackDci:0);
        uint32_t* inputSlot=reinterpret_cast<uint32_t*>(input_+contextSize_);
        for(int i=0;i<8;++i)inputSlot[i]=slot[i];
        uint32_t entries=inputSlot[0]>>27;
        if(entries<info_.dci)entries=info_.dci;
        if(entries<info_.feedbackDci)entries=info_.feedbackDci;
        inputSlot[0]=(inputSlot[0]&0x07ffffffu)|entries<<27;
        uint32_t* ep=reinterpret_cast<uint32_t*>(input_+size_t(contextSize_)*(info_.dci+1));
        uint32_t interval=0;while((1u<<interval)<plan_.microframes)++interval;
        const uint32_t packet=plan_.maxPacket&0x7ff,payload=Capacity(plan_);
        // Interval; Mult (SuperSpeed bursts per interval - 1); CErr 0, isochronous OUT; Max Burst
        // (transactions per burst - 1: high-bandwidth high speed, SuperSpeed); Max ESIT Payload.
        ep[0]=interval<<16|(info_.speed>=speedSuper?uint32_t(plan_.mult)<<8:0u)|((payload>>16)&0xff)<<24;
        ep[1]=(1u<<3)|uint32_t(plan_.burst)<<8|packet<<16;
        ep[2]=uint32_t(layout.trbsDevice)|1;ep[3]=uint32_t(layout.trbsDevice>>32);   // dequeue pointer, cycle state 1
        ep[4]=(pacing.MaxFrames()*frameBytes)|(payload&0xffff)<<16;                  // average TRB length, max ESIT payload
        if(info_.feedbackDci) {
            uint32_t* f=reinterpret_cast<uint32_t*>(input_+size_t(contextSize_)*(info_.feedbackDci+1));
            uint32_t fbInterval=0;while((1u<<fbInterval)<plan_.feedbackMicroframes)++fbInterval;
            f[0]=fbInterval<<16;
            f[1]=(5u<<3)|uint32_t(plan_.feedbackPacket)<<16;                         // isochronous IN
            f[2]=uint32_t(fbTrbs)|1;f[3]=uint32_t(fbTrbs>>32);
            f[4]=uint32_t(plan_.feedbackPacket)|uint32_t(plan_.feedbackPacket)<<16;
        }
        efi_status status=Command(device+inputAt,(trbConfigureEndpoint<<10)|uint32_t(info_.slot)<<24,&lastCode_);
        // From here the endpoint may read our ring (after a timeout: when its context names it).
        const volatile uint32_t* now=Host<volatile uint32_t>(context_+uint64_t(contextSize_)*info_.dci);
        configured_=(status==EFI_SUCCESS && lastCode_==1) || (status==EFI_TIMEOUT && (now[0]&7) && ((now[2]&~0xfu)|uint64_t(now[3])<<32)==layout.trbsDevice);
        if(status!=EFI_SUCCESS)return status;
        if(lastCode_!=1)return EFI_DEVICE_ERROR;         // e.g. bandwidth or resource error
        if(EndpointState(info_.dci)!=1)return EFI_DEVICE_ERROR;   // running
        return EFI_SUCCESS;
    }
    // Removes the endpoints from the controller and gives the interrupter back.
    // False when an endpoint may still use our memory (then it must not be freed).
    bool Detach() {
        using namespace charonusbaudio;
        bool clean=true;
        if(configured_ && input_ && SlotLive() && (EndpointState(info_.dci) || (info_.feedbackDci && EndpointState(info_.feedbackDci)))) {
            memset(input_,0,size_t(contextSize_)*33);
            uint32_t* control=reinterpret_cast<uint32_t*>(input_);
            control[0]=(EndpointState(info_.dci)?1u<<info_.dci:0)|(info_.feedbackDci && EndpointState(info_.feedbackDci)?1u<<info_.feedbackDci:0);
            control[1]=1;
            const uint32_t dropped=control[0];
            const volatile uint32_t* slot=Host<volatile uint32_t>(context_);
            uint32_t* inputSlot=reinterpret_cast<uint32_t*>(input_+contextSize_);
            for(int i=0;i<8;++i)inputSlot[i]=slot[i];
            // Context entries: the last endpoint that stays.
            uint32_t entries=1;
            for(uint32_t dci=31;dci>1;--dci)if(!(dropped&(1u<<dci)) && (*Host<volatile uint32_t>(context_+uint64_t(contextSize_)*dci)&7)) {entries=dci;break;}
            inputSlot[0]=(inputSlot[0]&0x07ffffffu)|entries<<27;
            const efi_status status=Command(device_+4096,(trbConfigureEndpoint<<10)|uint32_t(info_.slot)<<24,&lastCode_);
            charontrace::Value("CHARON usb audio drop",uint64_t(status)|(uint64_t(lastCode_)<<16));
            if(status!=EFI_SUCCESS || lastCode_!=1 || EndpointState(info_.dci) || (info_.feedbackDci && EndpointState(info_.feedbackDci))) {
                // The command could not be run: let the rings run dry instead.
                const efi_tpl old=boot_->RaiseTPL(TPL_HIGH_LEVEL);ring_.Truncate();if(fb_.Active())fb_.Truncate();boot_->RestoreTPL(old);
                boot_->Stall(size_t(plan_.microframes)*125*8+2000);
                clean=false;
            }
        }
        InterrupterOff();
        detached_=true;configured_=false;
        return clean;
    }
    void InterrupterOff() {
        if(!interrupterSet_)return;
        const uint32_t at=Interrupter(info_.interrupter);
        if((Read64(at+0x10)&~0x3full)==device_) {Write32(at+8,0);Write64(at+0x10,0);Write64(at+0x18,8);}
        interrupterSet_=false;
    }
    void FreeDma(bool keepMemory) {
        if(!keepMemory) {
            if(pci_ && mapping_ && PciLive())pci_->Unmap(pci_,mapping_);
            if(pci_ && host_ && PciLive())pci_->FreeBuffer(pci_,pages_,host_);
        }
        mapping_=nullptr;host_=nullptr;input_=nullptr;events_=nullptr;interrupterSet_=false;
    }
    void Release(bool keepMemory) {
        FreeDma(keepMemory);
        if(map_)boot_->FreePool(map_);
        map_=nullptr;pci_=nullptr;pciHandle_=nullptr;usb_=nullptr;usbHandle_=nullptr;
        open_=false;gone_=false;
    }

    // ---- a command on the firmware's command ring ----------------------------
    // Runs one command and puts the ring back exactly as found (see the head of
    // this file). *code: the completion code (1 = success). Any other return
    // than EFI_SUCCESS: the command was not run and nothing was changed, except
    // EFI_TIMEOUT: no completion was seen within 500 ms and the lent slot was
    // taken back.
    efi_status Command(uint64_t parameter,uint32_t control,uint32_t* code) {
        *code=0;
        if(SnapshotMemoryMapIfAllowed()!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
        const efi_tpl old=boot_->RaiseTPL(TPL_HIGH_LEVEL);
        const efi_status status=CommandLocked(parameter,control,code);
        boot_->RestoreTPL(old);
        return status;
    }
    // The memory map is taken again where that is allowed (the firmware may
    // have moved its rings since Open); above TPL_NOTIFY the one from Open stays.
    efi_status SnapshotMemoryMapIfAllowed() {
        if(CurrentTpl()>TPL_NOTIFY || exiting_)return map_?EFI_SUCCESS:EFI_DEVICE_ERROR;
        return SnapshotMemoryMap();
    }
    struct EventRing {uint64_t base[8];uint32_t size[8];uint32_t segments=0,count=0;
        Trb* At(uint32_t index) const {
            for(uint32_t s=0;s<segments;++s) {if(index<size[s])return Host<Trb>(base[s])+index;index-=size[s];}
            return nullptr;
        }
    };
    efi_status CommandLocked(uint64_t parameter,uint32_t control,uint32_t* code) {
        using namespace charonusbaudio;
        mmioFault_=false;
        if(!ControllerRuns())return EFI_NOT_READY;
        // The firmware's event ring (interrupter 0).
        const uint32_t at=Interrupter(0);
        EventRing ring;
        ring.segments=Read32(at+8)&0xffff;
        const uint64_t table=Read64(at+0x10)&~0x3full,dequeue=Read64(at+0x18)&~0xfull;
        if(mmioFault_ || !ring.segments || ring.segments>8 || !AddressOk(table) || !Readable(table,16ull*ring.segments))return EFI_UNSUPPORTED;
        uint32_t dequeueIndex=0xffffffffu;
        for(uint32_t s=0;s<ring.segments;++s) {
            const volatile uint64_t* entry=Host<volatile uint64_t>(table+16u*s);
            ring.base[s]=entry[0]&~0x3full;ring.size[s]=uint32_t(entry[1])&0xffff;
            if(ring.size[s]<16 || ring.size[s]>4096 || !AddressOk(ring.base[s]) || !Readable(ring.base[s],16ull*ring.size[s]))return EFI_UNSUPPORTED;
            if(dequeue>=ring.base[s] && dequeue<ring.base[s]+16ull*ring.size[s])dequeueIndex=ring.count+uint32_t((dequeue-ring.base[s])/16);
            ring.count+=ring.size[s];
        }
        if(dequeueIndex==0xffffffffu)return EFI_UNSUPPORTED;
        uint32_t enqueue=0,cycle=0;
        EventEnqueue([&](uint32_t i)->const Trb& {return *ring.At(i);},ring.count,&enqueue,&cycle);
        // Room for our completion event (the firmware driver does not run while interrupts are off).
        if(ring.count-(enqueue+ring.count-dequeueIndex)%ring.count<16)return EFI_NOT_READY;
        // The newest Command Completion Event names the command the firmware ran last.
        uint64_t last=0;
        for(uint32_t back=1;back<=ring.count && !last;++back) {
            const Trb* e=ring.At((enqueue+ring.count-back)%ring.count);
            if(!e->control && !e->parameter && !e->status)break;      // never written
            if(TrbType(e->control)!=trbCommandCompletion)continue;
            const uint32_t completion=e->status>>24;
            if(completion==24 || completion==25)return EFI_UNSUPPORTED;   // ring stopped or command aborted: position unknown
            last=e->parameter&~0xfull;
            if(!last)return EFI_UNSUPPORTED;
        }
        if(!last)return EFI_NOT_FOUND;
        if(!AddressOk(last) || !Readable(last,32))return EFI_UNSUPPORTED;
        const Trb* ran=Host<Trb>(last);
        if(TrbType(ran->control)==trbLink)return EFI_UNSUPPORTED;
        uint32_t producer=ran->control&1;                 // the cycle bit commands are written with
        // The enqueue position: the slot behind it, through the ring's Link TRB.
        uint64_t slot=last+16;
        if(TrbType(Host<Trb>(slot)->control)==trbLink) {
            if(Host<Trb>(slot)->control&2)producer^=1;
            slot=Host<Trb>(slot)->parameter&~0xfull;
            if(!AddressOk(slot) || !Readable(slot,16) || TrbType(Host<Trb>(slot)->control)==trbLink)return EFI_UNSUPPORTED;
        }
        if(!Readable(slot,32))return EFI_UNSUPPORTED;
        Trb* lent=Host<Trb>(slot);Trb* next=lent+1;       // a ring's last slot is its Link TRB, so `next` exists
        if((lent->control&1)==producer)return EFI_NOT_READY;   // the ring is not idle there (or not understood)
        const Trb savedLent=*lent,savedNext=*next;
        // The command; the slot behind it must stop the controller.
        Store(next,0,0,producer^1);
        Store(lent,parameter,0,(control&~1u)|producer);
        Write32(doorbell_,0);
        // Its completion: read in the event ring's memory; the firmware's dequeue pointer is not moved.
        bool completed=false;uint32_t index=enqueue,state=cycle;
        for(uint32_t waited=0;waited<500000 && !completed;waited+=50) {
            for(uint32_t n=0;n<ring.count;++n) {
                const Trb* e=ring.At(index);
                if((e->control&1)!=state)break;
                if(TrbType(e->control)==trbCommandCompletion && (e->parameter&~0xfull)==slot) {*code=e->status>>24;completed=true;break;}
                if(++index==ring.count) {index=0;state^=1;}
            }
            if(!completed)boot_->Stall(50);
        }
        // The slot as it was; then the controller's dequeue pointer back onto it:
        // a Link TRB (no cycle toggle) in the following slot, followed on the next doorbell.
        Store(lent,savedLent.parameter,savedLent.status,savedLent.control);
        if(completed) {
            Store(next,slot,0,(trbLink<<10)|producer);
            Write32(doorbell_,0);boot_->Stall(1000);
            Write32(doorbell_,0);boot_->Stall(1000);
        }
        { volatile Trb* v=next;v->control=savedNext.control;v->parameter=savedNext.parameter;v->status=savedNext.status; }
        if(mmioFault_)return EFI_DEVICE_ERROR;
        return completed?EFI_SUCCESS:EFI_TIMEOUT;
    }

    // ---- streaming ---------------------------------------------------------------
    static void EFIAPI TimerNotify(efi_event,void* context) {static_cast<UsbAudio*>(context)->Service();}
    // ExitBootServices (r407): the memory becomes the operating system's. No
    // command on the firmware's ring, no Stall, no memory map and no pool call
    // here: the rings are cut short behind the two TDs the controller may already
    // hold (memory writes), the controller's frame counter is watched until those
    // are over (register reads, three service intervals at the most, bounded), and
    // the spare interrupter is switched off, so nothing is written into our event
    // ring any more. The endpoint contexts keep pointing at ring memory the
    // controller no longer reads from (it found the end of the ring); the system
    // resets the controller when it takes it over. From here on this object does
    // nothing: no Open, and Stop frees nothing.
    static void EFIAPI ExitNotify(efi_event,void* context) {
        UsbAudio* self=static_cast<UsbAudio*>(context);
        if(self->exiting_)return;
        self->exiting_=true;
        if(!self->open_ || self->detached_) {self->gone_=true;return;}
        self->gone_=true;
        self->ring_.Truncate();
        if(self->fb_.Active())self->fb_.Truncate();
        self->mmioFault_=false;
        if(self->PciLive() && self->ControllerRuns()) {
            uint32_t longest=self->plan_.microframes;
            if(self->fb_.Active() && self->plan_.feedbackMicroframes>longest)longest=self->plan_.feedbackMicroframes;
            const uint32_t wanted=3*longest+2,start=self->Read32(self->runtime_)&0x3fff;
            for(uint32_t reads=0;reads<20000 && !self->mmioFault_ && (((self->Read32(self->runtime_)&0x3fff)-start)&0x3fff)<wanted;++reads) {}
            self->InterrupterOff();
        }
        self->detached_=true;
    }
    // The device or the controller went away: stop touching them. If only the
    // UsbIo instance went (driver disconnected) the endpoint is still removed.
    void Vanish() {
        gone_=true;
        if(!detached_ && PciLive() && ControllerLive())Detach();
        detached_=true;
        charontrace::Value("CHARON usb audio vanished",info_.events);
    }
    void FeedbackDoorbell() {if(info_.feedbackDci)Write32(doorbell_+4u*info_.slot,info_.feedbackDci);}
    void Service() {
        using namespace charonusbaudio;
        if(!open_ || gone_ || serving_ || exiting_)return;
        serving_=true;
        mmioFault_=false;
        // Liveness: the controller every time, the handles every 16th time (160 ms).
        bool live=true;
        if(!(tick_++&15))live=PciLive() && UsbLive() && ControllerLive() && SlotLive();
        else {const uint32_t status=Read32(operational_+4);live=!mmioFault_ && status!=0xffffffffu && !(status&((1u<<0)|(1u<<2)|(1u<<12)));}
        if(!live) {Vanish();serving_=false;return;}
        // Completion events of our TDs.
        const uint64_t ringDevice=ringDevice_,fbDevice=fbDevice_;
        uint32_t taken=0;
        for(;taken<eventEntries;++taken) {
            const volatile Trb* e=&events_[eventAt_];
            const uint32_t control=e->control;
            if((control&1)!=eventCycle_)break;
            if(TrbType(control)==trbTransferEvent) {
                const uint32_t completion=e->status>>24;const uint64_t pointer=e->parameter;
                const bool ours=pointer>=ringDevice && pointer<ringDevice+16ull*ring_.Slots() && !(pointer&15);
                if(fbDevice && pointer>=fbDevice && pointer<fbDevice+16ull*fb_.Slots() && !(pointer&15)) {
                    // A feedback packet (or a frame in which the device sent none: counted, nothing else).
                    uint32_t raw=0,bytes=0;const bool good=completion==1 || completion==13;
                    if(!good)++info_.feedbackErrors;
                    if(fb_.Completed(uint32_t((pointer-fbDevice)/16),e->status&0xffffffu,good,&raw,&bytes) && feedback_.Take(raw,bytes)) {
                        if(!ring_.FeedbackRate())charontrace::Value("CHARON usb audio feedback",uint64_t(feedback_.current)|(uint64_t(feedback_.nominal)<<32));
                        ring_.SetFeedback(feedback_.current);fbValueStamp_=ring_.Done();
                    }
                } else if(completion==1 || completion==13) {
                    // success, short packet; ring underrun/overrun and "stopped" name no finished TD
                    if(ours)ring_.Completed(uint32_t((pointer-ringDevice)/16));
                } else if(completion!=14 && completion!=15 && completion!=26 && completion!=27) {
                    ++info_.errors;
                    if(ours)ring_.Completed(uint32_t((pointer-ringDevice)/16));
                }
            }
            ++info_.events;
            if(++eventAt_==eventEntries) {eventAt_=0;eventCycle_^=1;}
        }
        if(taken)Write64(Interrupter(info_.interrupter)+0x18,(device_+8192+16ull*eventAt_)|8);
        // Watchdog on the controller's frame counter: TDs are queued but none
        // completes for their whole duration plus 100 ms (lost events): they
        // are counted as played, so the stream cannot stick.
        const uint32_t index=Read32(runtime_)&0x3fff,elapsed=(index-lastIndex_)&0x3fff;
        lastIndex_=index;
        if(ring_.Done()!=lastDone_) {lastDone_=ring_.Done();idle_=0;stalls_=0;}
        else if(ring_.Done()==ring_.Queued())idle_=0;
        else {
            idle_+=elapsed;
            if(idle_>(ring_.Queued()-ring_.Done())*plan_.microframes+800) {
                ring_.Rebase();lastDone_=ring_.Done();idle_=0;++info_.rebases;
                // Twice in a row without a single completion: the endpoint does not run. Reported as gone.
                if(++stalls_>=2) {Vanish();serving_=false;return;}
            }
        }
        if(fb_.Active()) {
            // The feedback ring: no completion for two seconds of the stream: its TDs are counted as
            // done. No value for one second: back to the nominal rate until the device speaks again.
            const uint64_t second=Tds(1000,plan_.microframes);
            if(fb_.Done()!=fbLastDone_) {fbLastDone_=fb_.Done();fbStamp_=ring_.Done();}
            else if(ring_.Done()-fbStamp_>2*second && fb_.Queued()!=fb_.Done()) {fb_.Rebase();fbLastDone_=fb_.Done();fbStamp_=ring_.Done();}
            if(ring_.FeedbackRate() && ring_.Done()-fbValueStamp_>second) {ring_.SetFeedback(0);charontrace::Value("CHARON usb audio feedback lost",feedback_.accepted);}
            if(fb_.Feed())FeedbackDoorbell();
        }
        if(ring_.Feed())Doorbell();
        ring_.Pace(Tds(idlePaceMs,plan_.microframes));
        if(mmioFault_)Vanish();
        serving_=false;
    }

    efi_boot_services* boot_=nullptr;
    UsbIoAudio* usb_=nullptr;efi_handle usbHandle_=nullptr;
    PciIoFull* pci_=nullptr;efi_handle pciHandle_=nullptr;
    uint32_t operational_=0,runtime_=0,doorbell_=0,interrupters_=0,contextSize_=32;bool wide_=false;
    uint64_t dcbaa_=0,context_=0;       // device context base address array, the slot's device context
    uint8_t* map_=nullptr;size_t mapSize_=0,mapEach_=0;   // memory map snapshot
    void* host_=nullptr;void* mapping_=nullptr;size_t pages_=0;uint64_t device_=0;
    uint64_t ringDevice_=0,fbDevice_=0;uint8_t* input_=nullptr;Trb* events_=nullptr;uint32_t eventAt_=0,eventCycle_=1;
    charonusbaudio::Plan plan_;charonusbaudio::IsoRing ring_;charonusbaudio::FeedbackRing fb_;charonusbaudio::Feedback feedback_;Info info_;
    efi_event timer_=nullptr,exitEvent_=nullptr;
    uint32_t lastCode_=0,lastIndex_=0,idle_=0,tick_=0,stalls_=0,quirks_=0;uint64_t lastDone_=0,fbLastDone_=0,fbStamp_=0,fbValueStamp_=0;
    uint8_t configurations_=0;
    uint8_t scratch_[2+12*20]={};
    bool open_=false,gone_=false,detached_=false,configured_=false,interrupterSet_=false,mmioFault_=false;volatile bool exiting_=false,serving_=false;
};

// ============================================================================
// Every USB sound device at once (r407).
//
// Which one of several is at the user's ears cannot be known (a dock's empty
// headphone jack and a headset both say "headphones"), so the rule is: every
// AudioStreaming playback interface that can be started plays, four at the
// most, in the order the firmware lists them. One of them paces the speech (the
// lowest output terminal rank - headphones and headsets, then speakers, then
// line and digital outputs - the first of equals); the others get a copy with
// the longer lead of a mirror. Hot-plug: Poll, called from the sound path,
// lets go of devices that were pulled out and starts ones that were plugged
// in; it costs one handle count per `pollEvery` calls unless something changed.
// ============================================================================
class UsbAudioSet {
public:
    static constexpr size_t maximum=4,failures=8;
    static constexpr uint32_t pollEvery=32;
    UsbAudioSet()=default;
    UsbAudioSet(const UsbAudioSet&)=delete;UsbAudioSet& operator=(const UsbAudioSet&)=delete;
    static bool Present(efi_boot_services* boot) {return UsbAudio::Present(boot);}
    bool Opened() const {for(const UsbAudio& d:devices_)if(d.Opened())return true;return false;}   // Stop is needed
    bool Running() const {for(const UsbAudio& d:devices_)if(d.Running())return true;return false;}
    bool Gone() const {return Opened() && !Running();}
    size_t Count() const {size_t n=0;for(const UsbAudio& d:devices_)if(d.Running())++n;return n;}
    const UsbAudio& Device(size_t index) const {return devices_[index];}
    // Starts every playable USB audio interface. EFI_SUCCESS when at least one plays.
    efi_status Open(efi_boot_services* boot) {
        if(Opened())return EFI_ALREADY_STARTED;
        if(!boot || !boot->LocateHandleBuffer || !boot->HandleProtocol || !boot->FreePool || !boot->RaiseTPL || !boot->RestoreTPL)return EFI_INVALID_PARAMETER;
        boot_=boot;failed_=0;
        const efi_status status=Scan();
        return Running()?EFI_SUCCESS:status;
    }
    // Hot-plug. True when a device plays. Cheap; does its work at TPL_CALLBACK or below only.
    bool Poll(efi_boot_services* boot) {
        if(!boot || !boot->LocateHandleBuffer || !boot->HandleProtocol || !boot->FreePool || !boot->RaiseTPL || !boot->RestoreTPL)return Running();
        boot_=boot;
        if(++calls_<pollEvery)return Running();
        calls_=0;
        if(Tpl()>TPL_CALLBACK)return Running();
        bool changed=false;
        for(UsbAudio& d:devices_)if(d.Gone()) {
            charontrace::Value("CHARON usb audio unplugged",uint64_t(d.Diagnostics().product)|(uint64_t(d.Diagnostics().vendor)<<16));
            d.Stop();changed=true;
        }
        const size_t handles=HandleCount();
        if(changed || handles!=handles_) {
            if(handles!=handles_)failed_=0;   // the bus changed: devices refused before get one more try
            Scan();
        }
        return Running();
    }
    // True when the number of USB I/O handles is not the one of the last scan (something was
    // plugged in or pulled out): the cue to look for sound devices again. One handle count
    // per eight calls, no allocation; the next scan (Open or Poll) makes it false again.
    bool Changed(efi_boot_services* boot) {
        if(!boot || !boot->LocateHandle || (++changeCalls_&7))return false;
        boot_=boot;
        return HandleCount()!=handles_;
    }
    size_t Write(const int16_t* samples,size_t frames,bool mirror=false) {
        UsbAudio* pace=Pace();
        if(!pace)return 0;
        const size_t taken=pace->Write(samples,frames,mirror);
        for(UsbAudio& d:devices_)if(&d!=pace && d.Running())d.Write(samples,taken,true);
        return taken;
    }
    bool Drained() {UsbAudio* pace=Pace();return !pace || pace->Drained();}
    void Discard() {for(UsbAudio& d:devices_)if(d.Running())d.Discard();}
    efi_status Stop() {
        for(UsbAudio& d:devices_)if(d.Opened())d.Stop();
        pace_=maximum;
        return EFI_SUCCESS;
    }
private:
    efi_tpl Tpl() const {const efi_tpl now=boot_->RaiseTPL(TPL_HIGH_LEVEL);boot_->RestoreTPL(now);return now;}
    // USB I/O handles, counted without an allocation.
    size_t HandleCount() const {
        if(!boot_->LocateHandle)return ++unknown_;   // no such service: every poll scans
        efi_guid guid=UsbAudio::UsbIoGuid();size_t bytes=0;
        const efi_status status=boot_->LocateHandle(ByProtocol,&guid,nullptr,&bytes,nullptr);
        return status==EFI_BUFFER_TOO_SMALL?bytes/sizeof(efi_handle):0;
    }
    // The device that paces the speech: the running one with the lowest output rank.
    UsbAudio* Pace() {
        size_t best=maximum;
        for(size_t i=0;i<maximum;++i)if(devices_[i].Running() && (best==maximum || devices_[i].Rank()<devices_[best].Rank()))best=i;
        if(best!=pace_) {
            pace_=best;
            if(best!=maximum)charontrace::Value("CHARON usb audio pacing device",uint64_t(best)|(uint64_t(Count())<<8)|(uint64_t(devices_[best].Diagnostics().product)<<16)|(uint64_t(devices_[best].Diagnostics().vendor)<<32));
        }
        return best==maximum?nullptr:&devices_[best];
    }
    bool Known(efi_handle handle,const void* io) const {
        for(const UsbAudio& d:devices_)if(d.Opened() && d.Handle()==handle && d.Io()==io)return true;
        for(size_t i=0;i<failed_;++i)if(failedHandle_[i]==handle && failedIo_[i]==io)return true;
        return false;
    }
    // Starts every AudioStreaming interface that is not open and was not refused before.
    efi_status Scan() {
        efi_guid guid=UsbAudio::UsbIoGuid();efi_handle* handles=nullptr;size_t count=0;
        handles_=HandleCount();
        if(boot_->LocateHandleBuffer(ByProtocol,&guid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return EFI_NOT_FOUND;
        efi_status result=EFI_NOT_FOUND;
        for(size_t i=0;i<count && i<1024;++i) {
            const void* io=UsbAudio::Streaming(boot_,handles[i]);
            if(!io || Known(handles[i],io))continue;
            UsbAudio* spare=nullptr;
            for(UsbAudio& d:devices_)if(!d.Opened() && !spare)spare=&d;
            if(!spare) {charontrace::Value("CHARON usb audio devices beyond the four in use",uint64_t(i));break;}
            const efi_status status=spare->OpenOn(boot_,handles[i]);
            if(status==EFI_SUCCESS) {
                result=EFI_SUCCESS;
                charontrace::Value("CHARON usb audio plays on",uint64_t(spare->Diagnostics().product)|(uint64_t(spare->Diagnostics().vendor)<<16)|(uint64_t(spare->Rank())<<32)|(uint64_t(spare-devices_)<<40));
            } else {
                if(result!=EFI_SUCCESS)result=status;
                // Not asked again until the bus changes (a recording-only interface, a refused device).
                if(status!=EFI_NOT_READY && failed_<failures) {failedHandle_[failed_]=handles[i];failedIo_[failed_]=io;++failed_;}
            }
        }
        boot_->FreePool(handles);
        return result;
    }
    efi_boot_services* boot_=nullptr;
    UsbAudio devices_[maximum];
    size_t pace_=maximum,handles_=0,failed_=0;mutable size_t unknown_=0;uint32_t calls_=0,changeCalls_=0;
    efi_handle failedHandle_[failures]={};const void* failedIo_[failures]={};
};
}  // namespace charonaudioefi
