// r386: boot media the firmware has no Boot#### entry for. GPL-2.0-or-later.
//
// Charon's device list shows the firmware's BootOrder. Firmware creates
// entries for removable media only while it enumerates devices (power-on),
// so a pendrive plugged in later, a CD put in the drive, or a second disk
// with only the fallback loader \EFI\BOOT\BOOTX64.EFI were invisible. This
// header finds them, builds an in-memory load option for each (never written
// to NVRAM) and recognizes media that start only in BIOS mode (FreeDOS, DOS,
// BIOS GRUB/SYSLINUX, old Windows/MBR systems, CDs without an EFI El Torito
// image): those are listed with "tylko BIOS" and cannot be started from UEFI.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "BootOption.h"
#include "CharonManagerProbe.h"

namespace hakefi {
namespace media {
struct BlockIoMedia {
    uint32_t MediaId;bool RemovableMedia,MediaPresent,LogicalPartition,ReadOnly,WriteCaching;
    uint32_t BlockSize,IoAlign;uint64_t LastBlock;
};
struct BlockIo {
    uint64_t Revision;BlockIoMedia* Media;
    efi_status (*Reset)(BlockIo*,bool) EFIAPI;
    efi_status (*ReadBlocks)(BlockIo*,uint32_t,uint64_t,size_t,void*) EFIAPI;
    void* WriteBlocks;void* FlushBlocks;
};
inline efi_guid blockIoGuid={0x964e5b21,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
inline efi_guid usbIoGuid={0x2b2f68d6,0x0cd2,0x44cf,{0x8e,0x8b,0xbb,0xa2,0x0b,0x1b,0x5b,0x75}};
inline efi_guid pathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
inline efi_guid fsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
inline efi_guid fsInfoGuid=EFI_FILE_SYSTEM_INFO_GUID;
inline efi_guid loadFileGuid={0x56ec3091,0x954c,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
// r387: network boot description from a LoadFile device path: protocol
// (PXE over IPv4/IPv6 or HTTP) and the adapter's MAC address.
inline bool NetworkName(const uint8_t* path,size_t bytes,bool pl,char16_t* out,size_t capacity) {
    const uint8_t* mac=nullptr;bool v4=false,v6=false,uri=false;
    for(size_t at=0;at+4<=bytes;) {
        const size_t n=uint16_t(path[at+2]|path[at+3]<<8);if(n<4)break;
        if(path[at]==3 && path[at+1]==11 && n>=37)mac=path+at+4;
        if(path[at]==3 && path[at+1]==12)v4=true;
        if(path[at]==3 && path[at+1]==13)v6=true;
        if(path[at]==3 && path[at+1]==24)uri=true;
        at+=n;
    }
    if(!mac)return false;
    size_t at=0;auto put=[&](const char16_t* s) {while(*s && at+1<capacity)out[at++]=*s++;out[at]=0;};
    put(pl?u"Sieć, ":u"Network, ");put(uri?u"HTTP":u"PXE");put(v6?u" IPv6":v4?u" IPv4":u"");
    put(u", MAC ");
    // MAC node: 32-byte address, 1-byte interface type; Ethernet uses 6.
    const size_t used=6;
    for(size_t i=0;i<used && at+4<capacity;++i) {
        const char16_t hex[]=u"0123456789ABCDEF";if(i)out[at++]=u':';
        out[at++]=hex[mac[i]>>4];out[at++]=hex[mac[i]&15];out[at]=0;
    }
    return true;
}

inline size_t PathLength(const uint8_t* path,size_t limit=4096) {
    size_t at=0;
    while(at+4<=limit) {
        const size_t n=uint16_t(path[at+2]|path[at+3]<<8);
        if(path[at]==0x7f && path[at+1]==0xff)return at;
        if(n<4)return SIZE_MAX;
        at+=n;
    }
    return SIZE_MAX;
}
inline bool Prefix(const uint8_t* a,size_t an,const uint8_t* b,size_t bn) {
    if(an>bn)return false;
    for(size_t i=0;i<an;++i)if(a[i]!=b[i])return false;
    return true;
}
// Hard-drive media node (4/1) of a path, or nullptr.
inline const uint8_t* HdNode(const uint8_t* path,size_t bytes) {
    for(size_t at=0;at+4<=bytes;) {
        const size_t n=uint16_t(path[at+2]|path[at+3]<<8);if(n<4)return nullptr;
        if(path[at]==4 && path[at+1]==1 && n==42)return path+at;
        at+=n;
    }
    return nullptr;
}
// Device part of an option path (up to the first file node).
inline size_t DevicePart(const uint8_t* path,size_t bytes) {
    for(size_t at=0;at+4<=bytes;) {
        const size_t n=uint16_t(path[at+2]|path[at+3]<<8);if(n<4)return at;
        if(path[at]==0x7f || (path[at]==4 && path[at+1]==4))return at;
        at+=n;
    }
    return bytes;
}
// True when an option of the firmware's BootOrder already starts this
// device: its device path is a prefix (whole-disk / removable entries), or
// both name the same partition (short-form HD(...) entries).
inline bool Covered(const uint8_t* const* paths,const size_t* sizes,size_t count,const uint8_t* own,size_t ownBytes) {
    const uint8_t* ownHd=HdNode(own,ownBytes);
    for(size_t i=0;i<count;++i) {
        const size_t device=DevicePart(paths[i],sizes[i]);
        if(!device)continue;
        if(Prefix(paths[i],device,own,ownBytes))return true;
        const uint8_t* hd=HdNode(paths[i],device);
        if(hd && ownHd) {bool same=true;for(size_t k=4;k<42 && same;++k)same=hd[k]==ownHd[k];if(same)return true;}
    }
    return false;
}
inline bool Has(const uint8_t* data,size_t size,const char* text) {
    size_t n=0;while(text[n])++n;
    for(size_t i=0;i+n<=size;++i) {size_t k=0;while(k<n && data[i+k]==uint8_t(text[k]))++k;if(k==n)return true;}
    return false;
}
// The system a BIOS boot sector starts, from strings in the MBR and the
// boot record of the active partition. Names are product names, not words.
inline const char16_t* LegacySystem(const uint8_t* mbr,const uint8_t* vbr,uint8_t type) {
    const uint8_t* s[2]={vbr,mbr};
    for(const uint8_t* b:s) {
        if(!b)continue;
        if(Has(b,512,"FRDOS") || Has(b,512,"FreeDOS") || Has(b,512,"FDOS") || Has(b,512,"KERNEL  SYS"))return u"FreeDOS";
        if(Has(b,512,"BOOTMGR"))return u"Windows";
        if(Has(b,512,"NTLDR"))return u"Windows XP";
        if(Has(b,512,"IO      SYS") || Has(b,512,"MSDOS") || Has(b,512,"MSWIN4") || Has(b,512,"IBMBIO"))return u"MS-DOS";
        if(Has(b,512,"GRUB"))return u"GRUB";
        if(Has(b,512,"SYSLINUX") || Has(b,512,"ISOLINUX") || Has(b,512,"EXTLINUX"))return u"SYSLINUX";
        if(Has(b,512,"LILO"))return u"LILO";
    }
    if(type==0x83)return u"Linux";
    if(type==0xa5 || type==0xa6 || type==0xa9)return u"BSD";
    if(type==0xeb)return u"BeOS/Haiku";
    if(type==0x07)return u"Windows";
    return nullptr;
}
enum class Kind : uint8_t {none,legacyDisk,legacyCd};
struct LegacyInfo {Kind kind=Kind::none;const char16_t* system=nullptr;char16_t label[33]={};};
// Reads the start of a whole device. A GPT disk, an MBR with an EFI system
// partition or an El Torito catalog with an EFI image is a UEFI medium.
inline LegacyInfo ProbeLegacy(efi_boot_services* boot,BlockIo* io) {
    LegacyInfo info;
    if(!io || !io->Media || !io->Media->MediaPresent || io->Media->LogicalPartition)return info;
    const uint32_t bs=io->Media->BlockSize;
    if(bs!=512 && bs!=2048 && bs!=4096)return info;
    uint8_t* buf=nullptr;
    if(boot->AllocatePool(EfiBootServicesData,4096*2,(void**)&buf)!=EFI_SUCCESS || !buf)return info;
    const uint32_t id=io->Media->MediaId;
    auto read=[&](uint64_t lba,uint8_t* out)->bool {return io->ReadBlocks(io,id,lba,bs,out)==EFI_SUCCESS;};
    if(bs==2048) {
        // ISO 9660: primary descriptor at 32 KiB, boot record right after it.
        if(read(16,buf) && buf[0]==1 && buf[1]=='C' && buf[2]=='D' && buf[3]=='0' && buf[4]=='0' && buf[5]=='1') {
            size_t n=0;for(size_t i=0;i<32;++i)info.label[n++]=buf[40+i]<32||buf[40+i]>126?u' ':char16_t(buf[40+i]);
            while(n && info.label[n-1]==u' ')--n;info.label[n]=0;
            if(read(17,buf) && buf[0]==0 && Has(buf+7,32,"EL TORITO")) {
                const uint32_t catalog=uint32_t(buf[0x47]|buf[0x48]<<8|buf[0x49]<<16|uint32_t(buf[0x4a])<<24);
                bool efi=false,bios=false;
                if(read(catalog,buf+4096)) {
                    const uint8_t* c=buf+4096;
                    if(c[0]==1) {
                        if(c[1]==0xef)efi=true;else if(c[1]==0)bios=c[32]==0x88;
                        // Section headers (0x90/0x91) name further platforms.
                        for(size_t at=64;at+32<=2048;at+=32) {
                            if(c[at]==0x90 || c[at]==0x91) {if(c[at+1]==0xef)efi=true;}
                            else if(c[at]!=0x88 && c[at]!=0 && c[at]!=0x44)break;
                        }
                    }
                }
                if(bios && !efi) {
                    info.kind=Kind::legacyCd;
                    info.system=Has(buf,2048,"FreeDOS") || (info.label[0]==u'F' && info.label[1]==u'D') ? u"FreeDOS" : nullptr;
                }
            }
        }
        boot->FreePool(buf);return info;
    }
    if(!read(0,buf) || buf[510]!=0x55 || buf[511]!=0xaa) {boot->FreePool(buf);return info;}
    bool active=false,esp=false;uint8_t type=0;uint32_t start=0;
    for(int p=0;p<4;++p) {
        const uint8_t* e=buf+446+p*16;
        if(e[4]==0xee || e[4]==0xef)esp=true;
        if(e[0]==0x80 && e[4]) {active=true;if(!type) {type=e[4];start=uint32_t(e[8]|e[9]<<8|e[10]<<16|uint32_t(e[11])<<24);}}
    }
    // A superfloppy (no table, jump + BPB at sector 0) boots too.
    const bool superfloppy=(buf[0]==0xeb || buf[0]==0xe9) && buf[11]==0 && (buf[12]==2 || buf[12]==8 || buf[12]==16);
    if(esp || (!active && !superfloppy)) {boot->FreePool(buf);return info;}
    const uint8_t* vbr=nullptr;
    if(active && start && start<=io->Media->LastBlock && read(start,buf+4096))vbr=buf+4096;
    info.kind=Kind::legacyDisk;info.system=LegacySystem(buf,vbr?vbr:(superfloppy?buf:nullptr),type);
    const uint8_t* bpb=superfloppy?buf:vbr;
    if(bpb && (bpb[0]==0xeb || bpb[0]==0xe9)) {
        // FAT volume label: FAT32 at 71, FAT12/16 at 43.
        const uint8_t* l=bpb[66]==0x29 ? bpb+71 : bpb[38]==0x29 ? bpb+43 : nullptr;
        if(l) {size_t n=0;for(size_t i=0;i<11;++i)info.label[n++]=l[i]<32||l[i]>126?u' ':char16_t(l[i]);
            while(n && info.label[n-1]==u' ')--n;info.label[n]=0;
            if(info.label[0]==u'N' && info.label[1]==u'O' && info.label[2]==u' ')info.label[0]=0;}  // "NO NAME"
    }
    boot->FreePool(buf);return info;
}
inline void Put(char16_t* out,size_t cap,size_t& at,const char16_t* s) {while(s && *s && at+1<cap)out[at++]=*s++;out[at]=0;}
// Load option bytes: attributes ACTIVE, description, device path (+ end).
inline uint8_t* BuildOption(efi_boot_services* boot,const char16_t* description,const uint8_t* path,size_t pathBytes,size_t& size) {
    size_t units=0;while(description[units])++units;
    size=6+(units+1)*2+pathBytes+4;uint8_t* data=nullptr;
    if(boot->AllocatePool(EfiLoaderData,size,(void**)&data)!=EFI_SUCCESS || !data)return nullptr;
    data[0]=1;data[1]=data[2]=data[3]=0;data[4]=uint8_t(pathBytes+4);data[5]=uint8_t((pathBytes+4)>>8);
    for(size_t i=0;i<=units;++i) {data[6+i*2]=uint8_t(description[i]);data[7+i*2]=uint8_t(description[i]>>8);}
    uint8_t* p=data+6+(units+1)*2;for(size_t i=0;i<pathBytes;++i)p[i]=path[i];
    p[pathBytes]=0x7f;p[pathBytes+1]=0xff;p[pathBytes+2]=4;p[pathBytes+3]=0;
    return data;
}
// Volume name of the ISO 9660 image on the whole device above `path`.
inline void IsoVolumeName(efi_boot_services* boot,const uint8_t* path,size_t bytes,char16_t* out,size_t capacity) {
    efi_handle* handles=nullptr;size_t n=0;
    if(boot->LocateHandleBuffer(ByProtocol,&blockIoGuid,nullptr,&n,&handles)!=EFI_SUCCESS || !handles)return;
    for(size_t i=0;i<n && !out[0];++i) {
        BlockIo* io=nullptr;uint8_t* own=nullptr;
        if(boot->HandleProtocol(handles[i],&blockIoGuid,(void**)&io)!=EFI_SUCCESS || !io || !io->Media || io->Media->LogicalPartition || !io->Media->MediaPresent)continue;
        if(boot->HandleProtocol(handles[i],&pathGuid,(void**)&own)!=EFI_SUCCESS || !own)continue;
        const size_t whole=PathLength(own);
        if(whole==SIZE_MAX || !whole || whole>=bytes || !Prefix(own,whole,path,bytes))continue;
        const uint32_t bs=io->Media->BlockSize;
        if(bs!=512 && bs!=2048 && bs!=4096)continue;
        static uint8_t sector[4096];
        if(io->ReadBlocks(io,io->Media->MediaId,32768/bs,bs,sector)!=EFI_SUCCESS)continue;
        const uint8_t* d=sector+(32768%bs);
        if(d[0]!=1 || d[1]!='C' || d[2]!='D' || d[3]!='0' || d[4]!='0' || d[5]!='1')continue;
        size_t k=0;for(size_t j=0;j<32 && k+1<capacity;++j)out[k++]=d[40+j]<32||d[40+j]>126?u' ':char16_t(d[40+j]);
        while(k && out[k-1]==u' ')--k;out[k]=0;
    }
    boot->FreePool(handles);
}
}  // namespace media

// One medium found by Charon. kind: 1 = UEFI-bootable (fallback loader),
// 2 = BIOS-only disk, 3 = BIOS-only CD.
struct FoundMedium {uint8_t kind=0;uint8_t* data=nullptr;size_t size=0;};
inline constexpr uint16_t mediaEntryId=0xffff;

// Finds media. `paths` are the device paths of every BootOrder option
// (Charon's own included). The caller frees each data with FreePool.
template<typename Add>
inline void FindMedia(efi_boot_services* boot,const uint8_t* const* paths,const size_t* sizes,size_t count,efi_handle selfDevice,bool pl,Add add) {
    using namespace media;
    efi_handle* handles=nullptr;size_t n=0;
    // UEFI media: file systems carrying the fallback loader.
    static uint8_t bootable[64][256];static size_t bootableBytes[64];size_t bootables=0;
    if(boot->LocateHandleBuffer(ByProtocol,&fsGuid,nullptr,&n,&handles)==EFI_SUCCESS && handles) {
        for(size_t i=0;i<n && i<512;++i) {
            uint8_t* own=nullptr;
            if(boot->HandleProtocol(handles[i],&pathGuid,(void**)&own)!=EFI_SUCCESS || !own)continue;
            const size_t bytes=PathLength(own);if(bytes==SIZE_MAX || !bytes)continue;
            if(bytes>=4 && own[0]==1 && own[1]==3)continue;   // r408: skip Charon's own RAM volume (memory-mapped device path)
            efi_simple_file_system_protocol* fs=nullptr;efi_file_protocol* root=nullptr;
            if(boot->HandleProtocol(handles[i],&fsGuid,(void**)&fs)!=EFI_SUCCESS || !fs || fs->OpenVolume(fs,&root)!=EFI_SUCCESS || !root)continue;
            efi_file_protocol* file=nullptr;
            const bool has=root->Open(root,&file,u"\\EFI\\BOOT\\BOOTX64.EFI",EFI_FILE_MODE_READ,0)==EFI_SUCCESS && file;
            if(file)file->Close(file);
            char16_t label[64]={};
            if(has) {
                uint8_t info[256]={};size_t infoSize=sizeof(info);
                if(root->GetInfo(root,&fsInfoGuid,&infoSize,info)==EFI_SUCCESS && infoSize>36) {
                    const uint8_t* l=info+36;
                    for(size_t k=0;k<63 && 36+k*2+1<infoSize;++k) {const char16_t c=char16_t(l[k*2]|l[k*2+1]<<8);if(!c)break;label[k]=c<32?u' ':c;}
                }
            }
            root->Close(root);
            if(!has)continue;
            // r386: an ISO image (a hybrid stick or a disc) has no FAT label
            // on its EFI image: its ISO 9660 volume name says what it is.
            if(!label[0])IsoVolumeName(boot,own,bytes,label,64);
            if(bootables<64 && bytes<=256) {for(size_t k=0;k<bytes;++k)bootable[bootables][k]=own[k];bootableBytes[bootables++]=bytes;}
            if(handles[i]==selfDevice || Covered(paths,sizes,count,own,bytes))continue;
            const char16_t* manager=ProbeOptionManager(boot,own,bytes+4);
            char16_t description[160]={};size_t at=0;
            Put(description,160,at,label[0]?label:(pl?u"bez etykiety":u"no label"));
            Put(description,160,at,u" (");
            if(manager) {Put(description,160,at,manager);Put(description,160,at,u", ");}
            Put(description,160,at,pl?u"znalazł Charon)":u"found by Charon)");
            size_t size=0;uint8_t* data=BuildOption(boot,description,own,bytes,size);
            if(data)add(FoundMedium{1,data,size});
        }
        boot->FreePool(handles);handles=nullptr;
    }
    // r387: network boot (PXE/HTTP LoadFile handles) the firmware has no
    // Boot#### entry for, e.g. adapters connected after power-on.
    if(boot->LocateHandleBuffer(ByProtocol,&loadFileGuid,nullptr,&n,&handles)==EFI_SUCCESS && handles) {
        for(size_t i=0;i<n && i<64;++i) {
            uint8_t* own=nullptr;
            if(boot->HandleProtocol(handles[i],&pathGuid,(void**)&own)!=EFI_SUCCESS || !own)continue;
            const size_t bytes=PathLength(own);if(bytes==SIZE_MAX || !bytes || bytes>1024)continue;
            char16_t description[160]={};
            if(!NetworkName(own,bytes,pl,description,120) || Covered(paths,sizes,count,own,bytes))continue;
            size_t at=0;while(description[at])++at;
            Put(description,160,at,pl?u" (znalazł Charon)":u" (found by Charon)");
            size_t size=0;uint8_t* data=BuildOption(boot,description,own,bytes,size);
            if(data)add(FoundMedium{1,data,size});
        }
        boot->FreePool(handles);handles=nullptr;
    }
    // BIOS-only media: whole devices whose boot code is for BIOS and which
    // hold no file system with a UEFI loader.
    if(boot->LocateHandleBuffer(ByProtocol,&blockIoGuid,nullptr,&n,&handles)==EFI_SUCCESS && handles) {
        for(size_t i=0;i<n && i<512;++i) {
            BlockIo* io=nullptr;uint8_t* own=nullptr;
            if(boot->HandleProtocol(handles[i],&blockIoGuid,(void**)&io)!=EFI_SUCCESS || !io || !io->Media || io->Media->LogicalPartition)continue;
            if(boot->HandleProtocol(handles[i],&pathGuid,(void**)&own)!=EFI_SUCCESS || !own)continue;
            const size_t bytes=PathLength(own);if(bytes==SIZE_MAX || !bytes || bytes>1024)continue;
            if(bytes>=4 && own[0]==1 && own[1]==3)continue;   // r408: skip Charon's own RAM volume (memory-mapped device path)
            bool uefi=false;
            for(size_t b=0;b<bootables && !uefi;++b)uefi=Prefix(own,bytes,bootable[b],bootableBytes[b]);
            if(uefi)continue;
            const LegacyInfo info=ProbeLegacy(boot,io);
            if(info.kind==Kind::none)continue;
            char16_t description[160]={};size_t at=0;
            if(info.label[0])Put(description,160,at,info.label);
            else Put(description,160,at,info.kind==Kind::legacyCd?(pl?u"płyta":u"disc"):(pl?u"bez etykiety":u"no label"));
            Put(description,160,at,u" (");
            if(info.system) {Put(description,160,at,info.system);Put(description,160,at,u", ");}
            Put(description,160,at,pl?u"tylko BIOS)":u"BIOS only)");
            size_t size=0;uint8_t* data=BuildOption(boot,description,own,bytes,size);
            if(data)add(FoundMedium{uint8_t(info.kind==Kind::legacyCd?3:2),data,size});
        }
        boot->FreePool(handles);
    }
}
// Hot-plug: a cheap signature of attached storage. USB devices appear as
// UsbIo handles by themselves (the bus driver polls ports); their storage
// stack is bound only on ConnectController. Removable drives report a new
// MediaId or presence after a read.
inline uint64_t MediaSignature(efi_boot_services* boot,bool probeDrives) {
    using namespace media;
    uint64_t sig=0;efi_handle* handles=nullptr;size_t n=0;
    if(boot->LocateHandleBuffer(ByProtocol,&usbIoGuid,nullptr,&n,&handles)==EFI_SUCCESS && handles) {
        sig=n;for(size_t i=0;i<n;++i)sig=sig*1099511628211ull^uint64_t(uintptr_t(handles[i]));
        boot->FreePool(handles);handles=nullptr;
    }
    if(boot->LocateHandleBuffer(ByProtocol,&blockIoGuid,nullptr,&n,&handles)==EFI_SUCCESS && handles) {
        sig=sig*31+n;
        for(size_t i=0;i<n && i<64;++i) {
            BlockIo* io=nullptr;
            if(boot->HandleProtocol(handles[i],&blockIoGuid,(void**)&io)!=EFI_SUCCESS || !io || !io->Media || io->Media->LogicalPartition || !io->Media->RemovableMedia)continue;
            if(probeDrives && io->ReadBlocks && io->Media->BlockSize && io->Media->BlockSize<=4096) {
                // A read on a removable drive makes its driver notice a
                // media change (ATAPI/SCSI report it only then).
                static uint8_t sector[4096];
                io->ReadBlocks(io,io->Media->MediaId,0,io->Media->BlockSize,sector);
            }
            sig=sig*1099511628211ull^(uint64_t(io->Media->MediaId)<<1|io->Media->MediaPresent);
        }
        boot->FreePool(handles);
    }
    return sig;
}
// Binds drivers below USB devices and removable drives (new sticks, a
// changed disc) so their partitions and file systems appear.
inline void ConnectRemovable(efi_boot_services* boot) {
    using namespace media;
    efi_handle* handles=nullptr;size_t n=0;
    if(boot->LocateHandleBuffer(ByProtocol,&usbIoGuid,nullptr,&n,&handles)==EFI_SUCCESS && handles) {
        for(size_t i=0;i<n;++i)boot->ConnectController(handles[i],nullptr,nullptr,true);
        boot->FreePool(handles);handles=nullptr;
    }
    if(boot->LocateHandleBuffer(ByProtocol,&blockIoGuid,nullptr,&n,&handles)==EFI_SUCCESS && handles) {
        for(size_t i=0;i<n;++i) {
            BlockIo* io=nullptr;
            if(boot->HandleProtocol(handles[i],&blockIoGuid,(void**)&io)==EFI_SUCCESS && io && io->Media && io->Media->RemovableMedia && !io->Media->LogicalPartition)
                boot->ConnectController(handles[i],nullptr,nullptr,true);
        }
        boot->FreePool(handles);
    }
}
}  // namespace hakefi
