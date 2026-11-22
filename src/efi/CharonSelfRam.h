#pragma once
// r407: Charon running from memory.
//
// Charon keeps a copy of its own files in memory (CharonSelfCopy.h). This
// file turns that copy into a volume: a FAT32 image in memory, shown to the
// firmware as a disk (a Block I/O protocol on a memory-mapped device path),
// which the firmware's own FAT driver then mounts like any other volume. Charon
// moves onto it - its root folder, its "own device" and the device of its
// loaded image all point at the volume in memory from then on - when
//   - an installation is about to erase or re-partition the disk Charon runs
//     from (so that disk can be the target of a clean installation), or
//   - the medium Charon started from is gone (the stick was pulled out, the
//     disc ejected, or the firmware read the disk's partitions again).
// Until then nothing changes: Charon reads and writes its real volume, so
// settings are kept. After the move, what Charon writes (settings, notes) goes
// to the volume in memory and is lost when the computer restarts; an
// installer copies Charon from there onto the new system's EFI partition.
//
// Root: the rest of the program holds Charon's root folder as one
// efi_file_protocol pointer handed down from efi_main. Wrap() puts a small
// forwarding object in front of the real root, so that every holder follows
// the move without knowing about it.
//
// The volume does not appear in the disk lists of the installer (a
// memory-mapped device path is skipped there) nor among bootable media
// (IsVolume). Boot services only; nothing here survives ExitBootServices.
// GPL-2.0-or-later.
#include "CharonSelfCopy.h"
#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>
namespace hakefi {
namespace selfram {
struct Media {
    uint32_t MediaId;bool RemovableMedia,MediaPresent,LogicalPartition,ReadOnly,WriteCaching;
    uint32_t BlockSize,IoAlign;uint64_t LastBlock;
    uint64_t LowestAlignedLba;uint32_t LogicalBlocksPerPhysicalBlock,OptimalTransferLengthGranularity;
};
struct Block {
    uint64_t Revision;Media* media;
    efi_status (*Reset)(Block*,bool) EFIAPI;
    efi_status (*ReadBlocks)(Block*,uint32_t,uint64_t,size_t,void*) EFIAPI;
    efi_status (*WriteBlocks)(Block*,uint32_t,uint64_t,size_t,void*) EFIAPI;
    efi_status (*FlushBlocks)(Block*) EFIAPI;
};
struct Root {efi_file_protocol face;efi_file_protocol* backing;};
struct State {
    Root root;bool wrapped;
    bool built,active;
    efi_handle handle;            // the volume in memory
    efi_handle oldDevice;         // where Charon ran from before the move (only its device path is used afterwards)
    efi_boot_services* boot;      // r408a: for OriginalDevice()
    alignas(8) uint8_t oldPath[512];size_t oldPathBytes;   // r408a: the device path of that volume, end node included
    efi_file_protocol* ramRoot;
    uint8_t* image;uint64_t sectors;size_t pages;
    Block block;Media media;
    alignas(8) uint8_t path[28];
    efi_status status;unsigned stage;   // why Build failed: 1 no copy, 2 memory, 3 format, 4 protocols, 5 no file system, 6 folders, 7 files
};
inline State& S() {static State s;return s;}
inline efi_guid& BlockGuid() {static efi_guid g={0x964e5b21,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};return g;}
inline efi_guid& PathGuid() {static efi_guid g=EFI_DEVICE_PATH_PROTOCOL_GUID;return g;}
inline efi_guid& FsGuid() {static efi_guid g=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;return g;}
inline efi_guid& ImageGuid() {static efi_guid g=EFI_LOADED_IMAGE_PROTOCOL_GUID;return g;}
inline bool Active() {return S().active;}
// The volume Charon booted from, after the switch - so writes (settings) can be mirrored back to
// the real medium while it is still present. Null before the switch or once it is gone.
// r408a: looked up by device path at every call - the handle kept at the switch is stale once the
// medium was pulled out, the disk re-partitioned or its partitions read again (a freed handle must
// never be given to the firmware). Null when no volume with exactly that path is mounted now.
inline efi_handle OriginalDevice() {
    auto& s=S();
    if(!s.active || !s.boot || !s.oldPathBytes)return nullptr;
    efi_device_path_protocol* rest=reinterpret_cast<efi_device_path_protocol*>(s.oldPath);efi_handle found=nullptr;
    if(s.boot->LocateDevicePath(&FsGuid(),&rest,&found)!=EFI_SUCCESS || !found || !rest)return nullptr;
    const uint8_t* end=reinterpret_cast<const uint8_t*>(rest);
    if(end!=s.oldPath+s.oldPathBytes-4 || end[0]!=0x7f)return nullptr;   // a parent matched, not the volume itself
    return found==s.handle?nullptr:found;
}
inline efi_handle Handle() {return S().handle;}
inline bool IsVolume(efi_handle handle) {return handle && S().handle==handle;}
inline uint64_t Bytes() {return S().sectors*512;}

// ---- the root folder the program holds ---------------------------------------
inline efi_file_protocol* Real(efi_file_protocol* self) {return reinterpret_cast<Root*>(self)->backing;}
inline efi_status EFIAPI RootOpen(efi_file_protocol* self,efi_file_protocol** out,const char16_t* name,uint64_t mode,uint64_t attributes) {
    auto* r=Real(self);return r?r->Open(r,out,name,mode,attributes):EFI_NO_MEDIA;}
inline efi_status EFIAPI RootClose(efi_file_protocol* self) {
    auto* root=reinterpret_cast<Root*>(self);auto* r=root->backing;root->backing=nullptr;
    if(r && r==S().ramRoot)S().ramRoot=nullptr;   // after Switch() the wrapper owns the RAM root; don't let Drop() close it twice
    if(r)r->Close(r);
    return EFI_SUCCESS;}
inline efi_status EFIAPI RootDelete(efi_file_protocol*) {return EFI_UNSUPPORTED;}   // the root is never deleted through the wrapper
inline efi_status EFIAPI RootRead(efi_file_protocol* self,size_t* size,void* buffer) {auto* r=Real(self);return r?r->Read(r,size,buffer):EFI_NO_MEDIA;}
inline efi_status EFIAPI RootWrite(efi_file_protocol* self,size_t* size,void* buffer) {auto* r=Real(self);return r?r->Write(r,size,buffer):EFI_NO_MEDIA;}
inline efi_status EFIAPI RootGetPosition(efi_file_protocol* self,uint64_t* position) {auto* r=Real(self);return r?r->GetPosition(r,position):EFI_NO_MEDIA;}
inline efi_status EFIAPI RootSetPosition(efi_file_protocol* self,uint64_t position) {auto* r=Real(self);return r?r->SetPosition(r,position):EFI_NO_MEDIA;}
inline efi_status EFIAPI RootGetInfo(efi_file_protocol* self,efi_guid* type,size_t* size,void* buffer) {auto* r=Real(self);return r?r->GetInfo(r,type,size,buffer):EFI_NO_MEDIA;}
inline efi_status EFIAPI RootSetInfo(efi_file_protocol* self,efi_guid* type,size_t size,void* buffer) {auto* r=Real(self);return r?r->SetInfo(r,type,size,buffer):EFI_NO_MEDIA;}
inline efi_status EFIAPI RootFlush(efi_file_protocol* self) {auto* r=Real(self);return r?r->Flush(r):EFI_NO_MEDIA;}
// The root the program is to use instead of `real` (opened by efi_main).
inline efi_file_protocol* Wrap(efi_file_protocol* real) {
    auto& s=S();
    if(!real || s.wrapped)return real;
    s.root.face={};s.root.face.Revision=EFI_FILE_PROTOCOL_REVISION;   // revision 1: nobody asks for OpenEx
    s.root.face.Open=RootOpen;s.root.face.Close=RootClose;s.root.face.Delete=RootDelete;s.root.face.Read=RootRead;s.root.face.Write=RootWrite;
    s.root.face.GetPosition=RootGetPosition;s.root.face.SetPosition=RootSetPosition;s.root.face.GetInfo=RootGetInfo;s.root.face.SetInfo=RootSetInfo;
    s.root.face.Flush=RootFlush;
    s.root.backing=real;s.wrapped=true;
    return &s.root.face;
}

// ---- the disk in memory --------------------------------------------------------
inline efi_status EFIAPI BlockReset(Block*,bool) {return EFI_SUCCESS;}
inline efi_status Range(uint32_t mediaId,uint64_t lba,size_t bytes,const void* buffer) {
    const auto& s=S();
    if(mediaId!=s.media.MediaId)return EFI_MEDIA_CHANGED;
    if(bytes%512)return EFI_BAD_BUFFER_SIZE;
    if(!buffer || lba>s.sectors || bytes/512>s.sectors-lba)return EFI_INVALID_PARAMETER;
    return EFI_SUCCESS;
}
inline efi_status EFIAPI BlockRead(Block*,uint32_t mediaId,uint64_t lba,size_t bytes,void* buffer) {
    const auto status=Range(mediaId,lba,bytes,buffer);if(status!=EFI_SUCCESS)return status;
    __builtin_memcpy(buffer,S().image+lba*512,bytes);return EFI_SUCCESS;
}
inline efi_status EFIAPI BlockWrite(Block*,uint32_t mediaId,uint64_t lba,size_t bytes,void* buffer) {
    const auto status=Range(mediaId,lba,bytes,buffer);if(status!=EFI_SUCCESS)return status;
    __builtin_memcpy(S().image+lba*512,buffer,bytes);return EFI_SUCCESS;
}
inline efi_status EFIAPI BlockFlush(Block*) {return EFI_SUCCESS;}
inline void P16(uint8_t* p,uint32_t v) {p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
inline void P32(uint8_t* p,uint32_t v) {for(int i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
// An empty FAT32 volume over the whole image (512-byte sectors, no partition
// table: a "superfloppy", as on many USB sticks). The cluster size is the
// largest that still leaves the 65525 clusters FAT32 must have.
inline bool Format(uint8_t* image,uint64_t sectors,uint32_t serial) {
    if(sectors>0xFFFFFFFFull)return false;
    constexpr uint32_t reserved=32;
    uint32_t spc=8,fatSectors=0,clusters=0;uint64_t data=0;
    for(;;) {
        fatSectors=uint32_t((((sectors-reserved)/spc+2)*4+511)/512);
        data=reserved+2ull*fatSectors;
        if(data+spc>sectors)return false;
        clusters=uint32_t((sectors-data)/spc);
        if(clusters>=66000 || spc==1)break;
        spc/=2;
    }
    if(clusters<65525)return false;
    static const char label[]="CHARON RAM ";
    uint8_t* boot=image;
    boot[0]=0xEB;boot[1]=0x58;boot[2]=0x90;
    static const char oem[]="CHARON  ";for(int i=0;i<8;++i)boot[3+i]=uint8_t(oem[i]);
    P16(boot+11,512);boot[13]=uint8_t(spc);P16(boot+14,reserved);boot[16]=2;
    boot[21]=0xF8;P16(boot+24,63);P16(boot+26,255);
    P32(boot+32,uint32_t(sectors));P32(boot+36,fatSectors);P32(boot+44,2);P16(boot+48,1);P16(boot+50,6);
    boot[64]=0x80;boot[66]=0x29;P32(boot+67,serial);
    for(int i=0;i<11;++i)boot[71+i]=uint8_t(label[i]);
    static const char fs[]="FAT32   ";for(int i=0;i<8;++i)boot[82+i]=uint8_t(fs[i]);
    boot[510]=0x55;boot[511]=0xAA;
    uint8_t* info=image+512;
    P32(info,0x41615252);P32(info+484,0x61417272);P32(info+488,clusters-1);P32(info+492,3);info[510]=0x55;info[511]=0xAA;
    __builtin_memcpy(image+6*512,boot,512);__builtin_memcpy(image+7*512,info,512);
    for(int copy=0;copy<2;++copy) {
        uint8_t* fat=image+(uint64_t(reserved)+uint64_t(copy)*fatSectors)*512;
        P32(fat,0x0FFFFFF8);P32(fat+4,0x0FFFFFFF);P32(fat+8,0x0FFFFFFF);   // media, reserved, the root folder (end of chain)
    }
    uint8_t* root=image+data*512;
    for(int i=0;i<11;++i)root[i]=uint8_t(label[i]);
    root[11]=0x08;
    return true;
}
inline void Drop(efi_boot_services* boot) {
    auto& s=S();
    if(s.ramRoot) {s.ramRoot->Close(s.ramRoot);s.ramRoot=nullptr;}
    if(s.handle) {
        boot->DisconnectController(s.handle,nullptr,nullptr);
        boot->UninstallProtocolInterface(s.handle,&BlockGuid(),&s.block);
        boot->UninstallProtocolInterface(s.handle,&PathGuid(),s.path);
        s.handle=nullptr;
    }
    if(s.image) {boot->FreePages(efi_physical_addr(reinterpret_cast<uintptr_t>(s.image)),s.pages);s.image=nullptr;}
    s.built=false;
}
// Every folder on the way to the file `path` ("\EFI\HAK\audio\x.wav").
inline bool Folders(efi_file_protocol* root,const char16_t* path) {
    static char16_t part[selfcopy::pathUnits];
    for(size_t i=1;path[i] && i<selfcopy::pathUnits;++i) {
        if(path[i]!=u'\\')continue;
        for(size_t k=0;k<i;++k)part[k]=path[k];
        part[i]=0;
        efi_file_protocol* folder=nullptr;
        if(root->Open(root,&folder,part,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY)!=EFI_SUCCESS || !folder)return false;
        folder->Close(folder);
    }
    return true;
}
// Builds the volume from the finished copy and has the firmware mount it.
// `turn` is called between pieces of work (the caller keeps speech fed).
inline efi_status Build(efi_boot_services* boot,void (*turn)(void*),void* context) {
    auto& s=S();
    if(s.built)return EFI_SUCCESS;
    auto& copy=selfcopy::S();
    s.stage=1;s.status=EFI_NOT_READY;
    if(!boot || !selfcopy::Ready() || !copy.count)return s.status;
    // room: the files, their folders and long names, and space for what Charon writes later
    uint64_t bytes=copy.total+copy.total/16+copy.count*4096+(uint64_t(12)<<20);
    if(bytes<(uint64_t(72)<<20))bytes=uint64_t(72)<<20;
    bytes=(bytes+(uint64_t(1)<<20)-1)>>20<<20;
    s.stage=2;s.status=EFI_OUT_OF_RESOURCES;
    efi_physical_addr address=0;const size_t pages=size_t(bytes>>12);
    if(boot->AllocatePages(AllocateAnyPages,EfiLoaderData,pages,&address)!=EFI_SUCCESS || !address)return s.status;
    s.image=reinterpret_cast<uint8_t*>(uintptr_t(address));s.pages=pages;s.sectors=bytes/512;
    boot->SetMem(s.image,size_t(bytes),0);
    if(turn)turn(context);
    s.stage=3;s.status=EFI_VOLUME_CORRUPTED;
    uint32_t lo=0,hi=0;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
    if(!Format(s.image,s.sectors,lo^hi)) {Drop(boot);return s.status;}
    s.media={};s.media.MediaId=0x43484152;s.media.MediaPresent=true;s.media.BlockSize=512;s.media.LastBlock=s.sectors-1;s.media.LogicalBlocksPerPhysicalBlock=1;
    s.block.Revision=0x00010000;s.block.media=&s.media;s.block.Reset=BlockReset;s.block.ReadBlocks=BlockRead;s.block.WriteBlocks=BlockWrite;s.block.FlushBlocks=BlockFlush;
    // MemMap(EfiLoaderData,start,end), end of path
    for(auto& b:s.path)b=0;
    s.path[0]=1;s.path[1]=3;s.path[2]=24;s.path[3]=0;P32(s.path+4,uint32_t(EfiLoaderData));
    for(int i=0;i<8;++i) {s.path[8+i]=uint8_t(uint64_t(address)>>(8*i));s.path[16+i]=uint8_t((uint64_t(address)+bytes-1)>>(8*i));}
    s.path[24]=0x7f;s.path[25]=0xff;s.path[26]=4;s.path[27]=0;
    s.stage=4;s.handle=nullptr;
    s.status=boot->InstallProtocolInterface(&s.handle,&PathGuid(),EFI_NATIVE_INTERFACE,s.path);
    if(s.status!=EFI_SUCCESS) {s.handle=nullptr;Drop(boot);return s.status;}
    s.status=boot->InstallProtocolInterface(&s.handle,&BlockGuid(),EFI_NATIVE_INTERFACE,&s.block);
    if(s.status!=EFI_SUCCESS) {boot->UninstallProtocolInterface(s.handle,&PathGuid(),s.path);s.handle=nullptr;Drop(boot);return s.status;}
    boot->ConnectController(s.handle,nullptr,nullptr,true);
    s.stage=5;
    efi_simple_file_system_protocol* fs=nullptr;
    s.status=boot->HandleProtocol(s.handle,&FsGuid(),(void**)&fs);
    if(s.status==EFI_SUCCESS && fs)s.status=fs->OpenVolume(fs,&s.ramRoot);
    if(s.status!=EFI_SUCCESS || !fs || !s.ramRoot) {s.ramRoot=nullptr;if(s.status==EFI_SUCCESS)s.status=EFI_UNSUPPORTED;const auto why=s.status;Drop(boot);s.status=why;return why;}
    for(size_t i=0;i<copy.count;++i) {
        const auto& item=copy.items[i];
        s.stage=6;
        if(!Folders(s.ramRoot,item.path)) {Drop(boot);s.status=EFI_DEVICE_ERROR;return s.status;}
        s.stage=7;
        efi_file_protocol* file=nullptr;
        s.status=s.ramRoot->Open(s.ramRoot,&file,item.path,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
        if(s.status!=EFI_SUCCESS || !file) {const auto why=s.status==EFI_SUCCESS?EFI_DEVICE_ERROR:s.status;Drop(boot);s.status=why;return why;}
        for(uint64_t done=0;done<item.size && item.data;) {
            size_t part=item.size-done>(uint64_t(1)<<20)?size_t(1)<<20:size_t(item.size-done);const size_t asked=part;
            s.status=file->Write(file,&part,item.data+done);
            if(s.status!=EFI_SUCCESS || part!=asked) {file->Close(file);const auto why=s.status==EFI_SUCCESS?EFI_VOLUME_FULL:s.status;Drop(boot);s.status=why;return why;}
            done+=part;
            if(turn)turn(context);
        }
        file->Close(file);
        if(turn && i%8==0)turn(context);
    }
    s.ramRoot->Flush(s.ramRoot);
    s.built=true;s.stage=0;s.status=EFI_SUCCESS;
    return EFI_SUCCESS;
}
// Charon moves onto the built volume: the wrapped root, the device of its
// loaded image. The caller updates its own copies of "Charon's device"
// (Handle()).
inline bool Switch(efi_boot_services* boot,efi_handle image) {
    auto& s=S();
    if(s.active)return true;
    if(!s.built || !s.ramRoot || !s.wrapped)return false;   // only switch when the root was wrapped, so every holder follows
    if(s.wrapped) {
        efi_file_protocol* old=s.root.backing;s.root.backing=s.ramRoot;
        if(old)old->Close(old);
    }
    efi_loaded_image_protocol* own=nullptr;
    if(image && boot->HandleProtocol(image,&ImageGuid(),(void**)&own)==EFI_SUCCESS && own) {
        s.oldDevice=own->DeviceHandle;s.boot=boot;s.oldPathBytes=0;
        uint8_t* path=nullptr;
        if(s.oldDevice && boot->HandleProtocol(s.oldDevice,&PathGuid(),(void**)&path)==EFI_SUCCESS && path) {
            size_t at=0;bool ok=false;
            while(at+4<=sizeof(s.oldPath)) {
                const size_t length=size_t(path[at+2])|(size_t(path[at+3])<<8);
                if(length<4 || at+length>sizeof(s.oldPath))break;
                if(path[at]==0x7f && path[at+1]==0xff) {at+=4;ok=true;break;}
                at+=length;
            }
            if(ok) {__builtin_memcpy(s.oldPath,path,at);s.oldPath[at-2]=4;s.oldPath[at-1]=0;s.oldPathBytes=at;}
        }
        own->DeviceHandle=s.handle;
    }
    s.active=true;
    return true;
}
inline efi_status Move(efi_boot_services* boot,efi_handle image,void (*turn)(void*),void* context) {
    const auto status=Build(boot,turn,context);
    if(status!=EFI_SUCCESS)return status;
    return Switch(boot,image)?EFI_SUCCESS:EFI_NOT_READY;
}
// Is the volume Charon runs from no longer there? (No file system on the
// handle any more, or its drive reports no medium.)
inline bool Gone(efi_boot_services* boot,efi_handle device) {
    if(!boot || !device || S().active)return false;
    efi_simple_file_system_protocol* fs=nullptr;
    if(boot->HandleProtocol(device,&FsGuid(),(void**)&fs)!=EFI_SUCCESS || !fs)return true;
    Block* io=nullptr;
    if(boot->HandleProtocol(device,&BlockGuid(),(void**)&io)==EFI_SUCCESS && io && io->media && !io->media->MediaPresent)return true;
    return false;
}
}  // namespace selfram
}  // namespace hakefi
