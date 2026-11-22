#pragma once
// r400: Charon workspace - what disks the computer has, read-only: whole
// disks (size, removable, the bus), their GPT partitions (name, kind, size)
// and the largest free space. Used by the install questions so that a disk
// or partition is chosen by what the user hears, and so the choice can be
// passed to an installer by size and by GPT identifiers. Nothing is written.
// GPL-2.0-or-later.
#include "CharonWsCore.h"
namespace hakefi {
namespace ws {
namespace disks {
constexpr size_t maxDisks=16,maxParts=32;
struct Part {
    uint32_t number;          // 1-based GPT entry number
    uint64_t first,last;      // LBA
    uint8_t type[16],unique[16];
    char16_t name[40];
    uint8_t kind;             // 0 other, 1 EFI system, 2 Windows data, 3 Linux, 4 swap, 5 Windows reserved, 6 recovery
};
struct Disk {
    efi_handle handle;media::BlockIo* io;
    uint64_t blocks;uint32_t blockSize;bool removable,readOnly,usb,nvme,gpt,hasCharon,blank;   // blank: first sector all zero, no partition table, nothing to lose
    uint8_t guid[16];
    Part parts[maxParts];size_t partCount;
    uint64_t freeFirst,freeBlocks;     // the largest gap
};
struct State {Disk list[maxDisks];size_t count;};
inline State& D() {static State state;return state;}
inline uint8_t KindOf(const uint8_t* type) {
    struct Known {uint32_t a;uint8_t kind;};
    static const Known known[]={{0xC12A7328,1},{0xEBD0A0A2,2},{0x0FC63DAF,3},{0x0657FD6D,4},{0xE3C9E316,5},{0xDE94BBA4,6},{0x4F68BCE3,3},{0x44479540,3},{0xE6D6D379,3}};
    const uint32_t a=systools::Le32(type);
    for(const auto& k:known)if(k.a==a)return k.kind;
    return 0;
}
inline const char16_t* KindName(uint8_t kind) {
    switch(kind) {
        case 1:return T(u"partycja systemowa EFI",u"EFI system partition");case 2:return T(u"dane Windows",u"Windows data");
        case 3:return T(u"Linux",u"Linux");case 4:return T(u"pamięć wymiany Linuksa",u"Linux swap");
        case 5:return T(u"zastrzeżona przez Microsoft",u"Microsoft reserved");case 6:return T(u"odzyskiwanie Windows",u"Windows recovery");
        default:return T(u"inna",u"other");
    }
}
inline void ReadGpt(Disk& d) {
    d.gpt=false;d.blank=false;d.partCount=0;d.freeFirst=0;d.freeBlocks=0;
    const uint32_t bs=d.blockSize;if(bs<512 || bs>4096 || d.blocks<128)return;
    alignas(16) static uint8_t header[4096];alignas(16) static uint8_t entries[32768];
    if(d.io->ReadBlocks(d.io,d.io->Media->MediaId,0,bs,header)==EFI_SUCCESS) {
        bool zero=true;for(uint32_t i=0;i<bs;++i)if(header[i]) {zero=false;break;}
        d.blank=zero;
    }
    if(d.io->ReadBlocks(d.io,d.io->Media->MediaId,1,bs,header)!=EFI_SUCCESS)return;
    static const char signature[]="EFI PART";for(int i=0;i<8;++i)if(header[i]!=uint8_t(signature[i]))return;
    const uint64_t entryLba=systools::Le64(header+72),firstUsable=systools::Le64(header+40),lastUsable=systools::Le64(header+48);
    const uint32_t count=systools::Le32(header+80),entrySize=systools::Le32(header+84);
    if(entrySize<128 || entrySize>512 || !count || entryLba<2 || entryLba>=d.blocks)return;
    d.gpt=true;for(int i=0;i<16;++i)d.guid[i]=header[56+i];
    size_t bytes=size_t(count>128?128:count)*entrySize;if(bytes>sizeof(entries))bytes=sizeof(entries);
    bytes=(bytes+bs-1)/bs*bs;if(bytes>sizeof(entries))bytes=sizeof(entries)/bs*bs;
    if(d.io->ReadBlocks(d.io,d.io->Media->MediaId,entryLba,bytes,entries)!=EFI_SUCCESS) {d.gpt=false;return;}
    for(size_t i=0;(i+1)*entrySize<=bytes && d.partCount<maxParts;++i) {
        const uint8_t* e=entries+i*entrySize;bool used=false;for(int k=0;k<16;++k)if(e[k])used=true;
        if(!used)continue;
        auto& p=d.parts[d.partCount++];p.number=uint32_t(i+1);p.first=systools::Le64(e+32);p.last=systools::Le64(e+40);
        for(int k=0;k<16;++k) {p.type[k]=e[k];p.unique[k]=e[16+k];}
        size_t n=0;for(;n<36;++n) {const char16_t c=char16_t(e[56+n*2]|e[57+n*2]<<8);if(!c)break;p.name[n]=c<32?u' ':c;}
        p.name[n]=0;p.kind=KindOf(p.type);
    }
    // Sort by position, then find the largest gap inside the usable area.
    for(size_t i=1;i<d.partCount;++i) {Part moving=d.parts[i];size_t j=i;while(j && d.parts[j-1].first>moving.first) {d.parts[j]=d.parts[j-1];--j;}d.parts[j]=moving;}
    uint64_t at=firstUsable;
    for(size_t i=0;i<=d.partCount;++i) {
        const uint64_t end=i<d.partCount?d.parts[i].first:lastUsable+1;
        if(end>at && end-at>d.freeBlocks) {d.freeBlocks=end-at;d.freeFirst=at;}
        if(i<d.partCount && d.parts[i].last+1>at)at=d.parts[i].last+1;
    }
}
// Every whole disk with media (CD/DVD drives and RAM disks are left out).
inline void Load() {
    auto& s=D();s.count=0;auto* boot=U().host->boot;efi_handle* handles=nullptr;size_t n=0;
    if(boot->LocateHandleBuffer(ByProtocol,&media::blockIoGuid,nullptr,&n,&handles)!=EFI_SUCCESS || !handles)return;
    // The disk Charon itself runs from: its volume's device path starts with the disk's.
    uint8_t* self=nullptr;boot->HandleProtocol(U().host->selfDevice,&media::pathGuid,(void**)&self);
    for(size_t i=0;i<n && s.count<maxDisks;++i) {
        media::BlockIo* io=nullptr;uint8_t* path=nullptr;
        if(boot->HandleProtocol(handles[i],&media::blockIoGuid,(void**)&io)!=EFI_SUCCESS || !io || !io->Media || io->Media->LogicalPartition || !io->Media->MediaPresent)continue;
        if(io->Media->BlockSize==2048)continue;   // optical
        if(boot->HandleProtocol(handles[i],&media::pathGuid,(void**)&path)!=EFI_SUCCESS || !path)continue;
        const size_t bytes=media::PathLength(path);if(bytes==SIZE_MAX)continue;
        auto& d=s.list[s.count];memset(&d,0,sizeof(d));d.handle=handles[i];d.io=io;
        d.blocks=io->Media->LastBlock+1;d.blockSize=io->Media->BlockSize;d.removable=io->Media->RemovableMedia;d.readOnly=io->Media->ReadOnly;
        bool memory=false;
        for(size_t at=0;at+4<=bytes;) {
            const uint8_t type=path[at],sub=path[at+1];const size_t len=systools::Le16(path+at+2);
            if(type==0x7f || len<4)break;
            if(type==3 && (sub==5 || sub==15 || sub==16))d.usb=true;
            if(type==3 && sub==0x17)d.nvme=true;
            if(type==1 && sub==3)memory=true;
            if(type==4 && sub==2)memory=true;   // CD-ROM node
            at+=len;
        }
        if(memory)continue;
        // PathLength leaves the end node out: the whole disk path must lead Charon's volume path.
        if(self && bytes) {const size_t own=media::PathLength(self);bool same=own!=SIZE_MAX && own>=bytes;for(size_t k=0;same && k<bytes;++k)if(self[k]!=path[k])same=false;d.hasCharon=same;}
        ReadGpt(d);++s.count;
    }
    boot->FreePool(handles);
}
inline uint64_t Bytes(const Disk& d) {return d.blocks*d.blockSize;}
inline void DiskLabel(const Disk& d,size_t index,char16_t* out,size_t cap) {
    Set(out,cap,T(u"Dysk ",u"Disk "));CatN(out,cap,index+1);Cat(out,cap,u", ");systools::PutSize(out,cap,Bytes(d),Pl());
    Cat(out,cap,d.usb?u", USB":d.nvme?u", NVMe":T(u", wewnętrzny",u", internal"));
    if(d.removable)Cat(out,cap,T(u", wymienny",u", removable"));
    if(d.readOnly)Cat(out,cap,T(u", tylko do odczytu",u", read-only"));
    if(d.hasCharon)Cat(out,cap,T(u", z niego działa Charon",u", Charon runs from it"));
    if(d.blank)Cat(out,cap,T(u", pusty, bez partycji",u", empty, no partitions"));
    else if(!d.gpt)Cat(out,cap,T(u", bez tablicy GPT",u", no GPT table"));
    else {
        Cat(out,cap,T(u", partycji: ",u", partitions: "));CatN(out,cap,d.partCount);
        // What lives there, so the user knows what would be lost.
        bool windows=false,linuxSystem=false;
        for(size_t i=0;i<d.partCount;++i) {if(d.parts[i].kind==2 || d.parts[i].kind==5)windows=true;if(d.parts[i].kind==3)linuxSystem=true;}
        if(windows)Cat(out,cap,T(u", zawiera Windows lub dane Windows",u", contains Windows or Windows data"));
        if(linuxSystem)Cat(out,cap,T(u", zawiera Linuksa",u", contains Linux"));
        if(d.freeBlocks*d.blockSize>=(uint64_t(1)<<30)) {Cat(out,cap,T(u", wolne miejsce ",u", free space "));systools::PutSize(out,cap,d.freeBlocks*d.blockSize,Pl());}
    }
}
inline void PartLabel(const Disk& d,const Part& p,char16_t* out,size_t cap) {
    Set(out,cap,T(u"Partycja ",u"Partition "));CatN(out,cap,p.number);Cat(out,cap,u", ");
    systools::PutSize(out,cap,(p.last-p.first+1)*d.blockSize,Pl());Cat(out,cap,u", ");Cat(out,cap,KindName(p.kind));
    if(p.name[0]) {Cat(out,cap,u", ");Cat(out,cap,p.name);}
}
// "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (lower case) from a GPT GUID.
inline void GuidText(const uint8_t* g,char* out) {
    static const char hex[]="0123456789abcdef";static const int order[16]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};size_t at=0;
    for(int i=0;i<16;++i) {if(i==4 || i==6 || i==8 || i==10)out[at++]='-';out[at++]=hex[g[order[i]]>>4];out[at++]=hex[g[order[i]]&15];}
    out[at]=0;
}
}  // namespace disks
}  // namespace ws
}  // namespace hakefi
