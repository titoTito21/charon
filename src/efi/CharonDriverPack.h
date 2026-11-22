#pragma once
// r407: Charon driver pack - loading rules for the driver classes that are
// not the network classes of CharonWsNet.h (nic-, wifi-, net- stay there).
// Folder \EFI\HAK\drivers on any volume the firmware can read; a class is the
// beginning of the file name:
//   fs-*.efi     file system drivers. Loaded when a block device with a medium
//                has no file system driver; then only those devices are
//                connected, so new volumes appear. All pack drivers read only.
//   input-*.efi  USB keyboard / mouse drivers. Loaded only when a USB HID boot
//                keyboard or mouse interface exists that nothing drives.
//   disk-*.efi   storage controller and disk drivers. Loaded only when a PCI
//                mass storage controller (class 01) or SD host controller
//                (08/05) exists that nothing drives.
//   usb-*.efi    USB host controller, bus and mass storage drivers. Loaded
//                only when a UHCI/EHCI/xHCI controller exists that nothing
//                drives and that firmware legacy support does not own.
//   virt-*.efi   virtual machine devices (virtio, VMware PVSCSI, emulated LSI).
//                Loaded only when such a PCI device exists that nothing drives.
//   nic-usb-*.efi  USB network drivers that follow the driver model (edk2).
//                Loaded when a USB network interface exists that nothing
//                drives, also when the firmware already drives another
//                adapter (CharonWsNet.h loads nic-* only when there is none).
//   audio-*.efi  RESERVED. No loadable sound driver exists besides AudioDxe
//                (see DRIVER-PACK.txt); nothing loads this class.
//   manual\*.efi (the sub-folder) is never loaded by a rule: LoadManual() on
//                the user's request only.
// What "nothing drives" means, for every rule: the protocol of the handle (PCI
// I/O or USB I/O) has no BY_DRIVER, EXCLUSIVE or BY_CHILD_CONTROLLER opener;
// for PCI also: no other handle has a device path below the controller; for
// USB host controllers also: no USB host controller protocol on the handle and
// the BIOS-owned bit of USB legacy support is clear; for HID interfaces also:
// no text input or pointer protocol on the handle. A check that fails or
// cannot be made counts as "driven": an error never leads to loading.
// Order of work in every hardware rule: (1) ConnectController on the undriven
// handles with the drivers the firmware already has (fast boot leaves
// controllers unconnected); (2) only when some stay undriven the class is
// loaded; (3) ConnectController on those handles ONLY - never on all handles,
// so a pack driver is not offered a controller the firmware drives.
// Every image goes through LoadImage/StartImage of the firmware (Secure Boot
// stays in force; a refused image is counted and reported), is loaded at most
// once (the loaded images of the firmware are searched for the same file path,
// so this holds across restarts of Charon in one boot) and a failure is
// reported, never fatal. Files that failed are not tried again in this run.
// Written against UEFI 2.10 (7.3 protocol handler services, 14.4 PCI I/O, 17.2
// USB I/O). r408: run in QEMU/OVMF for fs-, disk- (SD, UFS), virt- (PVSCSI) and
// nic-usb-; input- and usb- found nothing to do there and were not loaded by
// their rules (DRIVER-PACK.txt, "R408 LOAD TEST"). Never on a physical computer.
// GPL-2.0-or-later.
#include <efi/protocol/loaded-image.h>
#include "CharonWsNet.h"
#include "CharonTrace.h"   // r408: begin/end lines of every rule in trace builds
namespace hakefi {
namespace ws {
namespace drivers {
inline const char16_t* const packFolder=u"\\EFI\\HAK\\drivers";
inline const char16_t* const manualFolder=u"\\EFI\\HAK\\drivers\\manual";
inline const char16_t* const audioPrefix=u"audio-";   // reserved, see the head of this file
inline efi_guid pciGuid={0x4cf5b200,0x68b8,0x4ca5,{0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};
inline efi_guid usb2HcGuid={0x3e745226,0x9818,0x45b6,{0xa2,0xac,0xd7,0xcd,0x0e,0x8b,0xa2,0xbc}};
inline efi_guid usbHcGuid={0xf5089266,0x1aa0,0x4953,{0x97,0xd8,0x56,0x2f,0x8a,0x73,0xb5,0x19}};
inline efi_guid diskIoGuid={0xce345171,0xba0b,0x11d2,{0x8e,0x4f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
inline efi_guid loadedGuid={0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
inline efi_guid textInGuid={0x387477c1,0x69c7,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
inline efi_guid textInExGuid={0xdd9e7534,0x7762,0x4698,{0x8c,0x14,0xf5,0x85,0x17,0xa6,0x25,0xaa}};
inline efi_guid pointerGuid={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
inline efi_guid absolutePointerGuid={0x8d59d32b,0xc655,0x4ae9,{0x9b,0x15,0xf2,0x59,0x04,0x99,0x2a,0x43}};
// EFI_PCI_IO_PROTOCOL and EFI_USB_IO_PROTOCOL: only the members read here.
struct Pci;
using PciMemFn=efi_status (EFIAPI *)(Pci*,uint32_t width,uint8_t bar,uint64_t offset,size_t count,void* buffer);
using PciConfigFn=efi_status (EFIAPI *)(Pci*,uint32_t width,uint32_t offset,size_t count,void* buffer);
struct Pci {void* PollMem;void* PollIo;struct {PciMemFn Read,Write;} Mem,Io;struct {PciConfigFn Read,Write;} Config;};
struct UsbInterface {uint8_t length,type,number,alternate,endpoints,cls,subClass,protocol,name;};
struct UsbDevice {uint8_t length,type;uint16_t usb;uint8_t cls,subClass,protocol,maxPacket;uint16_t vendor,product,device;uint8_t manufacturer,productName,serial,configurations;};
struct Usb {
    void* transfers[6];
    efi_status (EFIAPI *GetDeviceDescriptor)(Usb*,UsbDevice*);
    void* GetConfigDescriptor;
    efi_status (EFIAPI *GetInterfaceDescriptor)(Usb*,UsbInterface*);
};
// ---- who drives a handle ----
// 1: a driver (or its child controllers) has the protocol open, 0: nobody, -1: unknown.
inline int Driven(efi_handle handle,efi_guid& guid) {
    auto* boot=U().host->boot;efi_open_protocol_information_entry* entries=nullptr;size_t n=0;
    if(boot->OpenProtocolInformation(handle,&guid,&entries,&n)!=EFI_SUCCESS)return -1;
    int driven=0;
    for(size_t i=0;entries && i<n;++i)
        if(entries[i].attributes&(EFI_OPEN_PROTOCOL_BY_DRIVER|EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER|EFI_OPEN_PROTOCOL_EXCLUSIVE))driven=1;
    if(entries)boot->FreePool(entries);
    return driven;
}
inline bool Has(efi_handle handle,efi_guid& guid) {
    return U().host->boot->OpenProtocol(handle,&guid,nullptr,U().host->image,nullptr,EFI_OPEN_PROTOCOL_TEST_PROTOCOL)==EFI_SUCCESS;
}
// True when another handle has a device path that continues this handle's path
// (a device below the controller is published, whoever made it), or when the
// handle has no usable device path (unknown: counts as in use).
inline bool HasDescendants(efi_handle handle) {
    auto* boot=U().host->boot;uint8_t* own=nullptr;
    if(boot->HandleProtocol(handle,&media::pathGuid,(void**)&own)!=EFI_SUCCESS || !own)return true;
    const size_t length=media::PathLength(own);if(length==SIZE_MAX || !length)return true;
    efi_handle* handles=nullptr;size_t count=0;bool found=false;
    if(boot->LocateHandleBuffer(ByProtocol,&media::pathGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return true;
    for(size_t i=0;i<count && !found;++i) {
        uint8_t* other=nullptr;
        if(handles[i]==handle || boot->HandleProtocol(handles[i],&media::pathGuid,(void**)&other)!=EFI_SUCCESS || !other)continue;
        const size_t n=media::PathLength(other);
        if(n==SIZE_MAX || n<=length)continue;
        found=true;for(size_t k=0;k<length;++k)if(other[k]!=own[k]) {found=false;break;}
    }
    boot->FreePool(handles);return found;
}
// USB legacy support: true when the firmware (SMM) says it owns the controller,
// or when that cannot be read. xHCI 1.2 section 7.1.1, EHCI 1.0 section 2.1.7 and
// appendix C, UHCI: LEGSUP (configuration offset C0h) bit 4, SMI on USB interrupt.
inline bool FirmwareOwnsUsb(Pci* io,const uint8_t* header) {
    const uint8_t kind=header[9];
    if(kind==0x30) {
        uint32_t parameters=0;if(io->Mem.Read(io,2,0,0x10,1,&parameters)!=EFI_SUCCESS)return true;
        if(parameters==0xffffffffu)return false;        // memory decoding is off: nobody runs it
        uint32_t at=(parameters>>16)*4;
        for(unsigned guard=0;at && guard<64;++guard) {
            uint32_t capability=0;if(io->Mem.Read(io,2,0,at,1,&capability)!=EFI_SUCCESS || capability==0xffffffffu)return true;
            if((capability&0xff)==1)return (capability&(1u<<16))!=0;
            const uint32_t next=(capability>>8)&0xff;if(!next)break;
            at+=next*4;
        }
        return false;
    }
    if(kind==0x20) {
        uint32_t parameters=0;if(io->Mem.Read(io,2,0,8,1,&parameters)!=EFI_SUCCESS)return true;
        if(parameters==0xffffffffu)return false;
        uint32_t at=(parameters>>8)&0xff;
        for(unsigned guard=0;at>=0x40 && guard<16;++guard) {
            uint32_t capability=0;if(io->Config.Read(io,2,at&0xfc,1,&capability)!=EFI_SUCCESS)return true;
            if((capability&0xff)==1)return (capability&(1u<<16))!=0;
            at=(capability>>8)&0xff;
        }
        return false;
    }
    if(kind==0x00) {uint16_t legacy=0;return io->Config.Read(io,1,0xc0,1,&legacy)!=EFI_SUCCESS || (legacy&0x0010)!=0;}
    return true;
}
// ---- targets: the handles a rule may serve ----
constexpr size_t maxTargets=48;
struct Targets {efi_handle handle[maxTargets];size_t count;efi_guid* protocol;};
inline void Add(Targets& t,efi_handle handle) {if(t.count<maxTargets)t.handle[t.count++]=handle;}
enum class PciKind : uint8_t {disk,usbHost,virtualMachine};
inline bool PciWanted(PciKind kind,efi_handle handle,Pci* io,const uint8_t* h) {
    const uint16_t vendor=uint16_t(h[0]|h[1]<<8),device=uint16_t(h[2]|h[3]<<8),subVendor=uint16_t(h[0x2c]|h[0x2d]<<8);
    const uint8_t progIf=h[9],subClass=h[10],baseClass=h[11];
    if(vendor==0xffff || (h[0x0e]&0x7f)!=0)return false;      // nothing there, or a bridge
    const bool virtualDevice=(vendor==0x1af4 && device>=0x1000 && device<=0x107f) || (vendor==0x15ad && device==0x07c0)
        // LSI 53C895A / Fusion-MPT as virtual machines emulate them (subsystem of QEMU or VMware), never the real cards.
        || (vendor==0x1000 && (device==0x0012 || device==0x0030 || device==0x0054 || device==0x0058) && (subVendor==0x1af4 || subVendor==0x15ad));
    switch(kind) {
    case PciKind::virtualMachine:return virtualDevice;
    case PciKind::disk:return !virtualDevice && (baseClass==0x01 || (baseClass==0x08 && subClass==0x05));
    case PciKind::usbHost:
        return baseClass==0x0c && subClass==0x03 && (progIf==0x00 || progIf==0x20 || progIf==0x30)
            && !Has(handle,usb2HcGuid) && !Has(handle,usbHcGuid) && !FirmwareOwnsUsb(io,h);
    }
    return false;
}
inline void PciTargets(PciKind kind,Targets& out) {
    auto* boot=U().host->boot;out.count=0;out.protocol=&pciGuid;efi_handle* handles=nullptr;size_t count=0;
    if(boot->LocateHandleBuffer(ByProtocol,&pciGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return;
    for(size_t i=0;i<count;++i) {
        Pci* io=nullptr;uint8_t header[64];
        if(boot->HandleProtocol(handles[i],&pciGuid,(void**)&io)!=EFI_SUCCESS || !io)continue;
        if(io->Config.Read(io,0,0,sizeof(header),header)!=EFI_SUCCESS)continue;
        if(Driven(handles[i],pciGuid)!=0)continue;          // cheap test first
        if(!PciWanted(kind,handles[i],io,header) || HasDescendants(handles[i]))continue;
        Add(out,handles[i]);
    }
    boot->FreePool(handles);
}
enum class UsbKind : uint8_t {input,network};
inline void UsbTargets(UsbKind kind,Targets& out) {
    auto* boot=U().host->boot;out.count=0;out.protocol=&media::usbIoGuid;efi_handle* handles=nullptr;size_t count=0;
    if(boot->LocateHandleBuffer(ByProtocol,&media::usbIoGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return;
    for(size_t i=0;i<count;++i) {
        Usb* io=nullptr;UsbInterface d{};
        if(boot->HandleProtocol(handles[i],&media::usbIoGuid,(void**)&io)!=EFI_SUCCESS || !io || io->GetInterfaceDescriptor(io,&d)!=EFI_SUCCESS)continue;
        bool wanted=false;
        if(kind==UsbKind::input) {
            wanted=d.cls==3 && d.subClass==1 && (d.protocol==1 || d.protocol==2)
                && !Has(handles[i],textInGuid) && !Has(handles[i],textInExGuid) && !Has(handles[i],pointerGuid) && !Has(handles[i],absolutePointerGuid);
        } else {
            // What the nic-usb-* drivers of the pack bind: CDC-ECM, CDC-NCM, RNDIS and the ASIX adapters.
            wanted=(d.cls==2 && (d.subClass==6 || d.subClass==0x0d)) || (d.cls==2 && d.subClass==2 && d.protocol==0xff) || (d.cls==0xef && d.subClass==4 && d.protocol==1);
            UsbDevice device{};
            if(!wanted && d.cls==0xff && io->GetDeviceDescriptor(io,&device)==EFI_SUCCESS)
                wanted=(device.vendor==0x0b95 && (device.product==0x7720 || device.product==0x772a || device.product==0x772b || device.product==0x7e2b
                    || device.product==0x1790 || device.product==0x178a)) || (device.vendor==0x17ef && device.product==0x7203);
            if(wanted && Has(handles[i],net::snpGuid))wanted=false;
        }
        if(wanted && Driven(handles[i],media::usbIoGuid)==0)Add(out,handles[i]);
    }
    boot->FreePool(handles);
}
// Block devices with a medium and without any file system or partition driver on them.
inline void VolumeTargets(Targets& out) {
    auto* boot=U().host->boot;out.count=0;out.protocol=&diskIoGuid;efi_handle* handles=nullptr;size_t count=0;
    if(boot->LocateHandleBuffer(ByProtocol,&media::blockIoGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return;
    for(size_t i=0;i<count;++i) {
        media::BlockIo* io=nullptr;
        if(boot->HandleProtocol(handles[i],&media::blockIoGuid,(void**)&io)!=EFI_SUCCESS || !io || !io->Media || !io->Media->MediaPresent)continue;
        // A partition driver or a file system driver holds Disk I/O BY_DRIVER (Block I/O is held by the Disk I/O driver on every disk).
        if(Has(handles[i],media::fsGuid) || Driven(handles[i],diskIoGuid)==1)continue;
        Add(out,handles[i]);
    }
    boot->FreePool(handles);
}
// A handle is offered to the drivers once per run (the second look at a device nothing can drive costs time only).
inline bool Offered(efi_handle handle,bool remember) {
    static efi_handle seen[160];static size_t count=0;
    for(size_t i=0;i<count;++i)if(seen[i]==handle)return true;
    if(remember && count<160)seen[count++]=handle;
    return false;
}
inline void Fresh(Targets& t,bool again) {
    size_t kept=0;
    for(size_t i=0;i<t.count;++i)if(again || !Offered(t.handle[i],false))t.handle[kept++]=t.handle[i];
    t.count=kept;
}
inline void Connect(const Targets& t) {
    for(size_t i=0;i<t.count;++i) {Offered(t.handle[i],true);U().host->boot->ConnectController(t.handle[i],nullptr,nullptr,true);Pump();}
}
inline size_t Served(const Targets& t) {size_t n=0;for(size_t i=0;i<t.count;++i)if(Driven(t.handle[i],*t.protocol)==1)++n;return n;}
// ---- files ----
constexpr size_t maxFiles=96,nameCap=72,pathCap=160;
struct Files {char16_t name[maxFiles][nameCap];efi_handle volume[maxFiles];size_t count;};
// <folder>\<prefix>*.efi of every volume, every name once (Charon's own volume wins), in name order.
inline void ListFiles(const char16_t* folder,const char16_t* prefix,Files& out) {
    out.count=0;efi_handle volumes[32];const size_t n=Volumes(volumes,32);
    for(size_t v=0;v<n;++v) {
        static FileRef ref;ref.volume=volumes[v];Set(ref.path,pathUnits,folder);efi_file_protocol* dir=nullptr;
        if(OpenFile(ref,EFI_FILE_MODE_READ,0,&dir)!=EFI_SUCCESS || !dir)continue;
        alignas(8) static uint8_t buffer[1024];
        for(unsigned guard=0;guard<4096;++guard) {
            size_t size=sizeof(buffer);
            if(dir->Read(dir,&size,buffer)!=EFI_SUCCESS || !size)break;
            const auto* info=reinterpret_cast<const efi_file_info*>(buffer);const char16_t* name=info->FileName;
            if((info->Attribute&EFI_FILE_DIRECTORY) || Len(name)>=nameCap || !EndsFold(name,u".efi") || (prefix && !StartsFold(name,prefix)))continue;
            bool known=false;for(size_t k=0;k<out.count && !known;++k)known=SameFold(out.name[k],name);
            if(known || out.count>=maxFiles)continue;
            Set(out.name[out.count],nameCap,name);out.volume[out.count++]=volumes[v];
        }
        dir->Close(dir);
    }
    for(size_t i=1;i<out.count;++i) {
        char16_t moving[nameCap];Set(moving,nameCap,out.name[i]);const efi_handle volume=out.volume[i];size_t j=i;
        while(j && CompareFold(moving,out.name[j-1])<0) {Set(out.name[j],nameCap,out.name[j-1]);out.volume[j]=out.volume[j-1];--j;}
        Set(out.name[j],nameCap,moving);out.volume[j]=volume;
    }
}
// True when an image with this file path is loaded already (by this or an earlier run of Charon in the same boot).
inline bool ImageLoaded(const char16_t* path) {
    auto* boot=U().host->boot;efi_handle* handles=nullptr;size_t count=0;bool found=false;const size_t want=Len(path);
    if(boot->LocateHandleBuffer(ByProtocol,&loadedGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return false;
    for(size_t i=0;i<count && !found;++i) {
        efi_loaded_image_protocol* image=nullptr;
        if(boot->HandleProtocol(handles[i],&loadedGuid,(void**)&image)!=EFI_SUCCESS || !image || !image->FilePath)continue;
        const uint8_t* p=reinterpret_cast<const uint8_t*>(image->FilePath);
        for(unsigned guard=0;guard<16 && !found;++guard) {
            const size_t n=size_t(p[2])|size_t(p[3])<<8;
            if(n<4 || p[0]==0x7f)break;
            if(p[0]==4 && p[1]==4 && n>=4+(want+1)*2) {
                found=true;
                for(size_t k=0;k<=want && found;++k)found=Fold(char16_t(p[4+k*2]|p[5+k*2]<<8))==Fold(k<want?path[k]:char16_t(0));
            }
            p+=n;
        }
    }
    boot->FreePool(handles);return found;
}
// Files that did not load are not tried again in this run (and not reported twice).
inline bool Failed(const char16_t* name,bool remember) {
    static char16_t names[64][nameCap];static size_t count=0;
    for(size_t i=0;i<count;++i)if(SameFold(names[i],name))return true;
    if(remember && count<64)Set(names[count++],nameCap,name);
    return false;
}
inline efi_status LoadOne(efi_handle volume,const char16_t* path) {
    auto* boot=U().host->boot;uint8_t* own=nullptr;size_t device=0;
    if(boot->HandleProtocol(volume,&media::pathGuid,(void**)&own)!=EFI_SUCCESS || !own || (device=media::PathLength(own))==SIZE_MAX || device>1500)return EFI_NOT_FOUND;
    const size_t units=Len(path)+1,node=4+units*2;
    uint8_t* full=static_cast<uint8_t*>(Allocate(device+node+4));if(!full)return EFI_OUT_OF_RESOURCES;
    memcpy(full,own,device);uint8_t* f=full+device;f[0]=4;f[1]=4;f[2]=uint8_t(node);f[3]=uint8_t(node>>8);
    for(size_t k=0;k<units;++k) {f[4+k*2]=uint8_t(path[k]);f[5+k*2]=uint8_t(path[k]>>8);}
    f[node]=0x7f;f[node+1]=0xff;f[node+2]=4;f[node+3]=0;
    efi_handle image=nullptr;
    auto status=boot->LoadImage(false,U().host->image,reinterpret_cast<efi_device_path_protocol*>(full),nullptr,0,&image);
    Release(full);
    if(status!=EFI_SUCCESS) {if(image)boot->UnloadImage(image);return status;}   // a refused image may leave a handle: it is never started
    if(!image)return EFI_LOAD_ERROR;
    size_t exitSize=0;char16_t* exitData=nullptr;status=boot->StartImage(image,&exitSize,&exitData);
    if(exitData)boot->FreePool(exitData);
    return status;
}
struct Result {size_t loaded,failed,refused,present,files;};   // present: loaded before this call
inline void LoadClass(const char16_t* folder,const char16_t* prefix,Result& r) {
    static Files files;ListFiles(folder,prefix,files);r.files+=files.count;
    for(size_t i=0;i<files.count;++i) {
        static char16_t path[pathCap];Set(path,pathCap,folder);Cat(path,pathCap,u"\\");Cat(path,pathCap,files.name[i]);
        if(ImageLoaded(path)) {++r.present;continue;}
        if(Failed(files.name[i],false))continue;
        {char narrow[nameCap];size_t k=0;for(;files.name[i][k] && k+1<nameCap;++k)narrow[k]=char(files.name[i][k]);narrow[k]=0;charontrace::Text("CHARON drivers load file");charontrace::Text(narrow);}
        const auto status=LoadOne(files.volume[i],path);
        charontrace::Value("CHARON drivers load status",uint64_t(status));
        if(status==EFI_SUCCESS)++r.loaded;
        else {Failed(files.name[i],true);if(status==EFI_SECURITY_VIOLATION || status==EFI_ACCESS_DENIED)++r.refused;else ++r.failed;}
        Tone(700+unsigned(r.loaded)*20,20);Pump();
    }
}
// ---- the rules ----
struct Outcome {Result files;size_t targets,byFirmware,byPack;};
inline void Sum(Outcome& total,const Outcome& o) {
    total.files.loaded+=o.files.loaded;total.files.failed+=o.files.failed;total.files.refused+=o.files.refused;total.files.present+=o.files.present;
    total.files.files+=o.files.files;total.targets+=o.targets;total.byFirmware+=o.byFirmware;total.byPack+=o.byPack;
}
inline Outcome Serve(Targets& t,const char16_t* prefix,bool again) {
    Outcome o{};Fresh(t,again);o.targets=t.count;if(!t.count)return o;
    charontrace::Value("CHARON drivers undriven targets",t.count);
    Connect(t);                                             // (1) the firmware's own drivers
    Targets left;left.count=0;left.protocol=t.protocol;
    for(size_t i=0;i<t.count;++i) {const int driven=Driven(t.handle[i],*t.protocol);if(driven==0)Add(left,t.handle[i]);else if(driven==1)++o.byFirmware;}
    charontrace::Value("CHARON drivers left after firmware connect",left.count);
    if(!left.count)return o;
    LoadClass(packFolder,prefix,o.files);                   // (2)
    if(o.files.loaded || o.files.present) {Connect(left);o.byPack=Served(left);}   // (3) these handles only
    charontrace::Value("CHARON drivers served by pack",o.byPack);
    return o;
}
inline void Report(const char16_t* pl,const char16_t* en,const Outcome& o,bool announce) {
    if(!announce && !o.files.loaded && !o.files.failed && !o.files.refused && !o.byFirmware && !o.byPack)return;
    static char16_t text[500];Set(text,500,T(pl,en));
    if(!o.targets)Cat(text,500,T(u": nie ma urządzeń bez sterownika.",u": no device without a driver."));
    else {
        Cat(text,500,T(u": urządzeń bez sterownika ",u": devices without a driver "));CatN(text,500,o.targets);
        if(o.byFirmware) {Cat(text,500,T(u", obsłużonych przez firmware ",u", now driven by the firmware "));CatN(text,500,o.byFirmware);}
        if(o.targets>o.byFirmware) {
            if(!o.files.files)Cat(text,500,T(u", brak pasujących plików w folderze EFI, HAK, drivers",u", no matching files in the folder EFI, HAK, drivers"));
            else {
                Cat(text,500,T(u", wczytano sterowników ",u", drivers loaded "));CatN(text,500,o.files.loaded);
                if(o.files.refused) {Cat(text,500,T(u", odrzuconych przez Secure Boot ",u", refused by Secure Boot "));CatN(text,500,o.files.refused);}
                if(o.files.failed) {Cat(text,500,T(u", nieudanych ",u", failed "));CatN(text,500,o.files.failed);}
                Cat(text,500,T(u", obsłużonych urządzeń ",u", devices now driven "));CatN(text,500,o.byPack);
            }
        }
        Cat(text,500,u".");
    }
    Say(text);
}
inline Outcome ServePci(PciKind kind,const char16_t* prefix,bool again) {Targets t;PciTargets(kind,t);return Serve(t,prefix,again);}
inline Outcome ServeUsb(UsbKind kind,const char16_t* prefix,bool again) {Targets t;UsbTargets(kind,t);return Serve(t,prefix,again);}
// Each returns the number of devices that got a driver. `announce` speaks also when there was nothing to do;
// `again` (the user asked) offers handles once more that were offered before.
inline size_t LoadUsbControllers(bool announce,bool again=false) {
    if(!U().host)return 0;
    charontrace::Text("CHARON drivers LoadUsbControllers begin");
    const auto o=ServePci(PciKind::usbHost,u"usb-",again);charontrace::Text("CHARON drivers LoadUsbControllers end");Report(u"Kontrolery USB",u"USB controllers",o,announce);return o.byFirmware+o.byPack;
}
inline size_t LoadDisks(bool announce,bool again=false) {
    if(!U().host)return 0;
    charontrace::Text("CHARON drivers LoadDisks begin");
    const auto o=ServePci(PciKind::disk,u"disk-",again);charontrace::Text("CHARON drivers LoadDisks end");Report(u"Kontrolery dysków",u"Disk controllers",o,announce);return o.byFirmware+o.byPack;
}
inline size_t LoadVirtual(bool announce,bool again=false) {
    if(!U().host)return 0;
    charontrace::Text("CHARON drivers LoadVirtual begin");
    const auto o=ServePci(PciKind::virtualMachine,u"virt-",again);charontrace::Text("CHARON drivers LoadVirtual end");Report(u"Urządzenia maszyny wirtualnej",u"Virtual machine devices",o,announce);return o.byFirmware+o.byPack;
}
inline size_t LoadInput(bool announce,bool again=false) {
    if(!U().host)return 0;
    charontrace::Text("CHARON drivers LoadInput begin");
    const auto o=ServeUsb(UsbKind::input,u"input-",again);charontrace::Text("CHARON drivers LoadInput end");Report(u"Klawiatury i myszy USB",u"USB keyboards and mice",o,announce);return o.byFirmware+o.byPack;
}
inline size_t LoadUsbNetwork(bool announce,bool again=false) {
    if(!U().host)return 0;
    charontrace::Text("CHARON drivers LoadUsbNetwork begin");
    const auto o=ServeUsb(UsbKind::network,u"nic-usb-",again);charontrace::Text("CHARON drivers LoadUsbNetwork end");Report(u"Karty sieciowe USB",u"USB network adapters",o,announce);return o.byFirmware+o.byPack;
}
// File systems: returns the number of new volumes.
inline size_t LoadFileSystems(bool announce,bool again=false) {
    if(!U().host)return 0;
    Targets t;VolumeTargets(t);Fresh(t,again);Result r{};charontrace::Value("CHARON drivers LoadFileSystems begin, volumes without a driver",t.count);efi_handle before[32];const size_t had=Volumes(before,32);size_t now=had;
    if(t.count) {
        LoadClass(packFolder,u"fs-",r);
        if(r.loaded || r.present) {Connect(t);now=Volumes(before,32);}
    }
    const size_t gained=now>had?now-had:0;charontrace::Value("CHARON drivers LoadFileSystems end, new volumes",gained);
    if(announce || r.loaded || r.failed || r.refused) {
        static char16_t text[400];
        if(!t.count)Set(text,400,T(u"Systemy plików: nie ma partycji bez sterownika.",u"File systems: no partition without a driver."));
        else if(!r.files)Set(text,400,T(u"Systemy plików: brak plików fs w folderze EFI, HAK, drivers.",u"File systems: no fs files in the folder EFI, HAK, drivers."));
        else {
            Set(text,400,T(u"Systemy plików: wczytano sterowników ",u"File systems: drivers loaded "));CatN(text,400,r.loaded);
            if(r.refused) {Cat(text,400,T(u", odrzuconych przez Secure Boot ",u", refused by Secure Boot "));CatN(text,400,r.refused);}
            if(r.failed) {Cat(text,400,T(u", nieudanych ",u", failed "));CatN(text,400,r.failed);}
            Cat(text,400,T(u", nowych woluminów ",u", new volumes "));CatN(text,400,gained);Cat(text,400,u".");
        }
        Say(text);
    }
    return gained;
}
// Everything that makes hardware appear, in the order that lets one step feed the next: USB controllers (their
// devices appear), storage, virtual machine devices, then input devices and USB network adapters.
inline size_t LoadHardware(bool announce,bool again=false) {
    if(!U().host)return 0;
    Outcome total{};
    Sum(total,ServePci(PciKind::usbHost,u"usb-",again));Sum(total,ServePci(PciKind::disk,u"disk-",again));
    Sum(total,ServePci(PciKind::virtualMachine,u"virt-",again));Sum(total,ServeUsb(UsbKind::input,u"input-",again));
    Sum(total,ServeUsb(UsbKind::network,u"nic-usb-",again));
    Report(u"Sterowniki sprzętu",u"Hardware drivers",total,announce);
    return total.byFirmware+total.byPack;
}
// Storage and file systems together: what the Files view and a search for systems on disks need.
inline size_t LoadStorage(bool announce,bool again=false) {
    if(!U().host)return 0;
    Outcome total{};charontrace::Text("CHARON drivers LoadStorage begin");
    Sum(total,ServePci(PciKind::usbHost,u"usb-",again));Sum(total,ServePci(PciKind::disk,u"disk-",again));Sum(total,ServePci(PciKind::virtualMachine,u"virt-",again));
    charontrace::Text("CHARON drivers LoadStorage hardware end");
    Report(u"Sterowniki dysków",u"Storage drivers",total,false);
    return LoadFileSystems(announce,again)+total.byFirmware+total.byPack;
}
// ---- by hand: \EFI\HAK\drivers\manual ----
// Drivers no rule can load safely (PS/2 and Super I/O, serial ports, a writing NTFS driver, rare file systems).
// The user picks one by name; after it starts, every controller is connected, as the firmware's own "load" does.
inline Files& Manual() {static Files files;return files;}
inline size_t ManualRefresh() {if(!U().host)return 0;ListFiles(manualFolder,nullptr,Manual());return Manual().count;}
inline const char16_t* ManualName(size_t index) {return index<Manual().count?Manual().name[index]:u"";}
inline bool LoadManual(size_t index) {
    if(!U().host || index>=Manual().count)return false;
    static char16_t path[pathCap];Set(path,pathCap,manualFolder);Cat(path,pathCap,u"\\");Cat(path,pathCap,Manual().name[index]);
    if(ImageLoaded(path)) {Say(T(u"Ten sterownik jest już wczytany.",u"This driver is loaded already."));return true;}
    const auto status=LoadOne(Manual().volume[index],path);
    if(status==EFI_SECURITY_VIOLATION || status==EFI_ACCESS_DENIED) {Fail(T(u"Secure Boot odrzucił ten sterownik: nie jest podpisany zaufanym kluczem.",u"Secure Boot refused this driver: it is not signed with a trusted key."));return false;}
    if(status!=EFI_SUCCESS) {Fail(T(u"Sterownik nie uruchomił się.",u"The driver did not start."),status);return false;}
    net::ConnectEverything();
    Play(Icon::done);Say(T(u"Sterownik wczytany, urządzenia połączone ponownie.",u"Driver loaded, devices connected again."));
    return true;
}
}
}
}
