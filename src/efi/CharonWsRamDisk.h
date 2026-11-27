#pragma once
// r400: Charon workspace - disc images started from memory. An ISO image (a
// file on any volume, or an http/https address) is loaded whole into reserved
// RAM and handed to the firmware's RAM disk service (EFI_RAM_DISK_PROTOCOL)
// as a virtual CD; the firmware then sees an ordinary disc and Charon starts
// its boot program with the screen reader on. Nothing is written to a disk.
// The memory is of the reserved type, so an operating system that reads the
// firmware's NVDIMM table (NFIT) can still find the disc after it took over.
// Needs: firmware with the RAM disk service, and free memory for the whole
// image besides what the started system needs for itself.
// GPL-2.0-or-later.
#include "CharonWsFetch.h"
#ifdef HAK_CHARON_HTTPS
#include "CharonMsIso.h"
#endif
namespace hakefi {
namespace ws {
void StartProgram(efi_handle volume,const char16_t* path,const char16_t* title,const char16_t* args);   // CharonWorkspace.h
const uint8_t* DevicePathOf(efi_handle volume,size_t& bytes);
namespace ramdisk {
struct Protocol {
    efi_status (EFIAPI *Register)(uint64_t base,uint64_t size,efi_guid* type,void* parentPath,void** devicePath);
    efi_status (EFIAPI *Unregister)(void* devicePath);
};
inline efi_guid protocolGuid={0xab38a0df,0x6873,0x44a9,{0x87,0xe6,0xd4,0xeb,0x56,0x14,0x84,0x49}};
inline efi_guid virtualCdGuid={0x3d5abd30,0x4175,0x87ce,{0x6d,0x64,0xd2,0xad,0xe5,0x23,0xc4,0xbb}};
inline Protocol* Find() {
    Protocol* p=nullptr;
    return U().host->boot->LocateProtocol(&protocolGuid,nullptr,(void**)&p)==EFI_SUCCESS?p:nullptr;
}
// Memory nobody uses yet.
inline uint64_t FreeMemory() {
    auto* boot=U().host->boot;size_t size=0,key=0,descriptor=0;uint32_t version=0;
    if(boot->GetMemoryMap(&size,nullptr,&key,&descriptor,&version)!=EFI_BUFFER_TOO_SMALL || descriptor<sizeof(efi_memory_descriptor))return 0;
    size+=descriptor*16;void* buffer=Allocate(size);if(!buffer)return 0;
    uint64_t pages=0;size_t used=size;
    if(boot->GetMemoryMap(&used,static_cast<efi_memory_descriptor*>(buffer),&key,&descriptor,&version)==EFI_SUCCESS)
        for(size_t at=0;at+descriptor<=used;at+=descriptor) {
            const auto* d=reinterpret_cast<const efi_memory_descriptor*>(static_cast<const uint8_t*>(buffer)+at);
            if(d->Type==EfiConventionalMemory)pages+=d->NumberOfPages;
        }
    Release(buffer);return pages*4096;
}
// Is there room for an image of `bytes` and `spare` more for the system that starts from it?
inline bool Room(uint64_t bytes,uint64_t spare,bool tell=true) {
    const uint64_t have=FreeMemory();
    if(have>=bytes+spare)return true;
    if(tell) {
        static char16_t text[400];Set(text,400,T(u"Za mało pamięci RAM, żeby trzymać ten obraz w pamięci. Potrzeba około ",u"Not enough RAM to keep this image in memory. Needed, about: "));
        systools::PutSize(text,400,bytes+spare,Pl());Cat(text,400,T(u", wolne jest ",u", free: "));systools::PutSize(text,400,have,Pl());Cat(text,400,u".");
        Fail(text);
    }
    return false;
}
// Registers the image as a virtual CD and starts its boot program. Returns
// when that program returns (an operating system never does).
inline bool Boot(Memory& image,const char16_t* title) {
    auto* boot=U().host->boot;Protocol* service=Find();
    if(!service) {Fail(T(u"To firmware nie ma usługi RAM-dysku.",u"This firmware has no RAM disk service."));return false;}
    void* path=nullptr;
    const auto status=service->Register(uint64_t(reinterpret_cast<uintptr_t>(image.data)),image.size,&virtualCdGuid,nullptr,&path);
    if(status!=EFI_SUCCESS || !path) {Fail(T(u"Firmware nie przyjęło obrazu jako dysku w pamięci.",u"The firmware did not accept the image as a disk in memory."),status);return false;}
    const size_t prefix=media::PathLength(static_cast<const uint8_t*>(path));
    {auto* remaining=static_cast<efi_device_path_protocol*>(path);efi_handle disk=nullptr;
     if(boot->LocateDevicePath(&media::blockIoGuid,&remaining,&disk)==EFI_SUCCESS && disk)boot->ConnectController(disk,nullptr,nullptr,true);}
    efi_handle volumes[32];const size_t n=Volumes(volumes,32);efi_handle found=nullptr;
    static const char16_t* const loaders[]={u"\\EFI\\BOOT\\BOOTX64.EFI",u"\\efi\\microsoft\\boot\\bootmgfw.efi"};const char16_t* loader=nullptr;
    for(size_t i=0;i<n && !found && prefix!=SIZE_MAX;++i) {
        size_t bytes=0;const uint8_t* own=DevicePathOf(volumes[i],bytes);
        if(!own || bytes<prefix)continue;
        bool under=true;for(size_t k=0;k<prefix && under;++k)under=own[k]==static_cast<const uint8_t*>(path)[k];
        if(!under)continue;
        for(const auto* l:loaders) {
            FileRef ref;ref.volume=volumes[i];Set(ref.path,pathUnits,l);efi_file_protocol* file=nullptr;
            if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)==EFI_SUCCESS) {file->Close(file);found=volumes[i];loader=l;break;}
        }
    }
    if(!found) {
        service->Unregister(path);
        Fail(T(u"Na obrazie nie ma programu rozruchowego UEFI dla tego komputera.",u"The image holds no UEFI boot program for this computer."));return false;}
    Say(T(u"Obraz jest w pamięci jako dysk. Uruchamiam go.",u"The image is in memory as a disc. Starting it."));
    for(unsigned i=0;i<60;++i) {boot->Stall(100000);Pump();auto* fx=U().host->effects;if(i>5 && fx && fx->SpeechIdle && fx->SpeechIdle(fx->context))break;}
    StartProgram(found,loader,title,nullptr);
    service->Unregister(path);
    return true;
}
// An image file from a volume.
inline void BootFile(const FileRef& ref,const char16_t* name,uint64_t bytes) {
    if(!Find()) {Fail(T(u"To firmware nie ma usługi RAM-dysku, więc obrazu nie da się uruchomić z pamięci.",u"This firmware has no RAM disk service, so the image cannot be started from memory."));return;}
    if(!Room(bytes,uint64_t(256)<<20))return;
    static char16_t text[300];Set(text,300,T(u"Wczytać obraz do pamięci i uruchomić? ",u"Load the image into memory and start it? "));Cat(text,300,name);Cat(text,300,u", ");systools::PutSize(text,300,bytes,Pl());
    if(!Confirm(text))return;
    static Memory image;image.reserved=true;
    if(!Reserve(image,size_t(bytes))) {Fail(T(u"Brak pamięci.",u"Out of memory."));return;}
    efi_file_protocol* file=nullptr;
    if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS) {Free(image);Fail(T(u"Nie otwarto pliku.",u"The file could not be opened."));return;}
    Say(T(u"Wczytuję obraz do pamięci.",u"Loading the image into memory."));
    uint64_t done=0;unsigned rounds=0;efi_status status=EFI_SUCCESS;
    while(done<bytes) {
        size_t part=size_t(bytes-done);if(part>(1u<<20))part=1u<<20;
        status=file->Read(file,&part,image.data+done);
        if(status!=EFI_SUCCESS || !part)break;
        done+=part;Pump();
        if(++rounds%4==0) {
            static const char16_t* const what[2]={u"Wczytywanie",u"Loading"};Progress(what[Pl()?0:1],done,bytes);
        }
    }
    file->Close(file);
    if(done!=bytes) {Free(image);Fail(T(u"Odczyt obrazu nie powiódł się.",u"Reading the image failed."),status);return;}
    image.size=size_t(bytes);
    if(!Boot(image,name))Free(image);   // after a start the memory stays: the started program may still use the disc
}
// An image from the network.
inline void BootUrl(const char16_t* url,const char16_t* title,size_t spareMegabytes=256) {
    if(!Find()) {Fail(T(u"To firmware nie ma usługi RAM-dysku.",u"This firmware has no RAM disk service."));return;}
    static Memory image;image.data=nullptr;image.capacity=0;image.size=0;image.extra=0;image.reserved=true;
    static FileRef none;none.volume=nullptr;none.path[0]=0;uint64_t received=0;
    Say(T(u"Pobieram obraz do pamięci.",u"Downloading the image into memory."));
    if(!net::DownloadAny(url,none,&received,&image,T(u"Pobieranie",u"Downloading")))return;
    if(FreeMemory()<(uint64_t(spareMegabytes)<<20)) {Free(image);Fail(T(u"Obraz zajął prawie całą pamięć; system nie miałby gdzie działać.",u"The image took almost all memory; a system would have no room to run."));return;}
    if(!Boot(image,title))Free(image);
}
// r409: a writable FAT32 volume in reserved memory, registered with the firmware as a virtual DISK
// (EFI_VIRTUAL_DISK_GUID, "raw disk format in volatile memory"). It stands in for a USB stick: the
// Windows installer's files, the answer file and Charon are written to it exactly as to a stick.
// Layout: an MBR with one FAT32 partition from 1 MiB on (a partitioned disk, so that no system has
// to guess a "superfloppy"). EDK2's RAM disk driver publishes a RAM disk in the ACPI NFIT only when
// its memory is EfiReservedMemoryType and only once "ready to boot" has been signalled
// (RamDiskDxe: RamDiskAcpiCheck); Charon is the boot manager here, so it signals that itself.
inline efi_guid virtualDiskGuid={0x77ab535a,0x45fc,0x624b,{0x55,0x60,0xf7,0xb2,0x81,0xd1,0xf9,0x6e}};
struct RamVolume {Memory memory;void* path;efi_handle volume;};
inline RamVolume& V() {static RamVolume v;return v;}
inline void EFIAPI NoNotify(efi_event,void*) {}
inline void SignalReadyToBoot() {
    static bool done=false;if(done)return;
    static efi_guid group={0x7ce88fb3,0x4bd7,0x4679,{0x87,0xa8,0xa8,0xd8,0xde,0xe5,0x0d,0x2b}};
    auto* boot=U().host->boot;efi_event event=nullptr;
    if(!boot->CreateEventEx || boot->CreateEventEx(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,NoNotify,nullptr,&group,&event)!=EFI_SUCCESS || !event)return;
    boot->SignalEvent(event);boot->CloseEvent(event);done=true;
}
// ---- what an operating system is told about the disk (ACPI) ----------------------------------
// An operating system finds a firmware RAM disk only through the ACPI NFIT: one "SPA range structure" whose
// address range type is the disk type GUID (ACPI 6.5, 5.2.26.2; the range must be EfiReservedMemoryType).
// Measured in r409: VMware's firmware has the RAM disk service but publishes no NFIT for a disk registered by
// a boot manager. Charon then publishes the table itself through EFI_ACPI_TABLE_PROTOCOL, exactly as EDK2's
// RamDiskDxe does (RamDiskProtocol.c: RamDiskPublishNfit, RamDiskPublishSsdt): an NFIT with the one range and,
// because no NFIT existed, an SSDT with the NVDIMM root device (ACPI0012) under \_SB. Nothing else is touched;
// an existing NFIT is never replaced (a second NFIT is not allowed: Published() then reports false).
struct AcpiTableProtocol {
    efi_status (EFIAPI *Install)(AcpiTableProtocol*,void* table,size_t size,size_t* key);
    efi_status (EFIAPI *Uninstall)(AcpiTableProtocol*,size_t key);
};
inline efi_guid acpiTableGuid={0xffe06bdd,0x6107,0x46a6,{0x7b,0xb2,0x5a,0x9c,0x7e,0xc5,0x27,0x5c}};
inline uint32_t AcpiLe32(const uint8_t* p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline uint64_t AcpiLe64(const uint8_t* p) {return uint64_t(AcpiLe32(p))|(uint64_t(AcpiLe32(p+4))<<32);}
// Calls `each` for every table of the XSDT; stops when it returns true.
template<class Each> inline bool AcpiTables(Each each) {
    static const efi_guid acpi2={0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
    auto* table=U().host->system;if(!table || !table->ConfigurationTable)return false;
    const uint8_t* rsdp=nullptr;
    for(size_t i=0;i<table->NumberOfTableEntries;++i) {
        const uint8_t* a=reinterpret_cast<const uint8_t*>(&table->ConfigurationTable[i]);const uint8_t* b=reinterpret_cast<const uint8_t*>(&acpi2);
        bool same=true;for(size_t k=0;k<16;++k)if(a[k]!=b[k])same=false;
        if(same)rsdp=*reinterpret_cast<const uint8_t* const*>(a+16);
    }
    if(!rsdp || rsdp[0]!='R' || rsdp[15]<2)return false;
    const uint8_t* xsdt=reinterpret_cast<const uint8_t*>(uintptr_t(AcpiLe64(rsdp+24)));
    if(!xsdt || xsdt[0]!='X')return false;
    const uint32_t length=AcpiLe32(xsdt+4);
    for(uint32_t at=36;at+8<=length && at<36+8*512;at+=8) {
        const uint8_t* t=reinterpret_cast<const uint8_t*>(uintptr_t(AcpiLe64(xsdt+at)));
        if(t && each(t))return true;
    }
    return false;
}
inline bool NfitExists() {return AcpiTables([](const uint8_t* t) {return t[0]=='N' && t[1]=='F' && t[2]=='I' && t[3]=='T';});}
// Is the memory range described in an NFIT as a region of the given type?
inline bool Published(uint64_t base,uint64_t size,const efi_guid& type) {
    return AcpiTables([&](const uint8_t* t) {
        if(!(t[0]=='N' && t[1]=='F' && t[2]=='I' && t[3]=='T'))return false;
        const uint32_t length=AcpiLe32(t+4);
        for(uint32_t at=40;at+4<=length;) {
            const uint32_t kind=uint32_t(t[at])|uint32_t(t[at+1])<<8,bytes=uint32_t(t[at+2])|uint32_t(t[at+3])<<8;
            if(bytes<4 || at+bytes>length)break;
            if(kind==0 && bytes>=56 && AcpiLe64(t+at+32)==base && AcpiLe64(t+at+40)==size) {
                const uint8_t* g=reinterpret_cast<const uint8_t*>(&type);bool same=true;
                for(int k=0;k<16;++k)if(t[at+16+k]!=g[k])same=false;
                if(same)return true;
            }
            at+=bytes;
        }
        return false;
    });
}
inline void AcpiHeader(uint8_t* t,const char* signature,uint32_t length,uint8_t revision,const char* tableId) {
    for(int i=0;i<4;++i)t[i]=uint8_t(signature[i]);
    t[4]=uint8_t(length);t[5]=uint8_t(length>>8);t[6]=uint8_t(length>>16);t[7]=uint8_t(length>>24);t[8]=revision;
    static const char oem[]="CHARON";for(int i=0;i<6;++i)t[10+i]=uint8_t(oem[i]);
    for(int i=0;i<8;++i)t[16+i]=uint8_t(tableId[i]);
    t[24]=1;t[28]='C';t[29]='H';t[30]='R';t[31]='N';t[32]=1;
}
inline void AcpiChecksum(uint8_t* t,uint32_t length) {t[9]=0;uint8_t sum=0;for(uint32_t i=0;i<length;++i)sum=uint8_t(sum+t[i]);t[9]=uint8_t(0-sum);}
// Publishes the range when the firmware did not. True: an NFIT now describes it.
inline bool Publish(uint64_t base,uint64_t size,const efi_guid& type) {
    if(Published(base,size,type))return true;
    if(NfitExists())return false;
    AcpiTableProtocol* acpi=nullptr;
    if(U().host->boot->LocateProtocol(&acpiTableGuid,nullptr,(void**)&acpi)!=EFI_SUCCESS || !acpi)return false;
    static uint8_t nfit[96+56*3];for(auto& b:nfit)b=0;
    uint32_t nfitBytes=96;
    uint8_t* spa=nfit+40;spa[0]=0;spa[2]=56;
    {const uint8_t* g=reinterpret_cast<const uint8_t*>(&type);for(int k=0;k<16;++k)spa[16+k]=g[k];}
    for(int k=0;k<8;++k) {spa[32+k]=uint8_t(base>>(8*k));spa[40+k]=uint8_t(size>>(8*k));}
#ifdef HAK_CHARON_RAMPROBE
    // Test builds only: copies of the same disk under other region types, to measure which one Windows mounts.
    {static const efi_guid kinds[3]={{0x5cea02c9,0x4d07,0x69d3,{0x26,0x9f,0x44,0x96,0xfb,0xe0,0x96,0xf9}},   // persistent virtual disk
        {0x3d5abd30,0x4175,0x87ce,{0x6d,0x64,0xd2,0xad,0xe5,0x23,0xc4,0xbb}},                                 // volatile virtual CD
        {0x66f0d379,0xb4f3,0x4074,{0xac,0x43,0x0d,0x33,0x18,0xb7,0x8c,0xdb}}};                                // persistent memory region
     for(int n=0;n<3;++n) {
        static Memory copy[3];copy[n].reserved=true;if(!Reserve(copy[n],size_t(size)))break;
        __builtin_memcpy(copy[n].data,reinterpret_cast<const void*>(uintptr_t(base)),size_t(size));
        copy[n].data[0x47+0x100000]=uint8_t('1'+n);   // the volume label's first letter tells the copies apart
        uint8_t* s2=nfit+nfitBytes;s2[0]=0;s2[2]=56;s2[4]=uint8_t(n+1);
        const uint8_t* g=reinterpret_cast<const uint8_t*>(&kinds[n]);for(int k=0;k<16;++k)s2[16+k]=g[k];
        const uint64_t b2=uint64_t(reinterpret_cast<uintptr_t>(copy[n].data));
        for(int k=0;k<8;++k) {s2[32+k]=uint8_t(b2>>(8*k));s2[40+k]=uint8_t(size>>(8*k));}
        if(n==2)s2[48]=0x08;   // EFI_MEMORY_WB, as a persistent memory range carries
        nfitBytes+=56;
     }}
#endif
    AcpiHeader(nfit,"NFIT",nfitBytes,1,"RAMDISK ");
    AcpiChecksum(nfit,nfitBytes);
    size_t key=0;
    if(acpi->Install(acpi,nfit,nfitBytes,&key)!=EFI_SUCCESS)return false;
    // Scope(\_SB){Device(NVDR){Name(_HID,"ACPI0012") Method(_STA,0){Return(0x0F)}}}
    static const uint8_t aml[]={0x10,0x26,0x5c,0x5f,0x53,0x42,0x5f,0x5b,0x82,0x1e,'N','V','D','R',
        0x08,'_','H','I','D',0x0d,'A','C','P','I','0','0','1','2',0x00,0x14,0x09,'_','S','T','A',0x00,0xa4,0x0a,0x0f};
    static uint8_t ssdt[36+sizeof(aml)];for(auto& b:ssdt)b=0;
    AcpiHeader(ssdt,"SSDT",sizeof(ssdt),2,"RAMDISK ");
    for(size_t i=0;i<sizeof(aml);++i)ssdt[36+i]=aml[i];
    AcpiChecksum(ssdt,sizeof(ssdt));
    size_t key2=0;const auto root=acpi->Install(acpi,ssdt,sizeof(ssdt),&key2);
    charontrace::Value("CHARON ramdisk own NFIT, SSDT status",uint64_t(root));
    return Published(base,size,type);
}
inline void DropVolume() {
    auto& v=V();
    if(v.path) {if(Protocol* service=Find())service->Unregister(v.path);v.path=nullptr;}
    v.volume=nullptr;
    if(v.memory.data)Free(v.memory);
}
inline bool IsRamVolume(efi_handle volume) {return volume && V().volume==volume;}
// Free bytes on a volume (0 when unknown).
inline uint64_t FreeOn(efi_handle volume) {
    efi_file_protocol* root=OpenRoot(volume);if(!root)return 0;
    alignas(8) uint8_t info[512]={};size_t size=sizeof(info);uint64_t freeBytes=0;
    if(root->GetInfo(root,&media::fsInfoGuid,&size,info)==EFI_SUCCESS && size>36)freeBytes=systools::Le64(info+24);
    root->Close(root);return freeBytes;
}
// Makes the volume (`bytes` of disk, rounded up to whole megabytes). Null: nothing is left behind.
// `why`: 1 no service, 2 no memory, 3 format, 4 firmware refused, 5 no file system appeared.
inline efi_handle MakeVolume(uint64_t bytes,unsigned& why,efi_status& status) {
    auto* boot=U().host->boot;auto& v=V();status=EFI_SUCCESS;
    DropVolume();
    Protocol* service=Find();why=1;if(!service)return nullptr;
    bytes=(bytes+(uint64_t(1)<<20)-1)>>20<<20;
    const uint64_t first=2048,sectors=bytes/512;
    why=2;v.memory.reserved=true;
    if(sectors<first+(uint64_t(40)<<11) || !Reserve(v.memory,size_t(bytes)))return nullptr;
    // Only what the empty file system reads must be zero: the first megabyte, the reserved sectors, both tables, the root.
    {uint64_t zero=(first+64)*512+(sectors-first)+(uint64_t(1)<<20);if(zero>bytes)zero=bytes;
     for(uint64_t at=0;at<zero;) {size_t part=zero-at>(uint64_t(8)<<20)?size_t(8)<<20:size_t(zero-at);boot->SetMem(v.memory.data+at,part,0);at+=part;Pump();}}
    why=3;
    uint32_t lo=0,hi=0;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
    uint8_t* part=v.memory.data+first*512;
    if(!hakefi::selfram::Format(part,sectors-first,lo^hi)) {DropVolume();return nullptr;}
    {static const char label[]="CHARON WIN ";
     const uint32_t fatSectors=uint32_t(part[36])|uint32_t(part[37])<<8|uint32_t(part[38])<<16|uint32_t(part[39])<<24;
     uint8_t* root=part+(uint64_t(32)+2ull*fatSectors)*512;
     for(int copy=0;copy<2;++copy) {uint8_t* b=part+(copy?6*512:0);hakefi::selfram::P32(b+28,uint32_t(first));for(int i=0;i<11;++i)b[71+i]=uint8_t(label[i]);}
     for(int i=0;i<11;++i)root[i]=uint8_t(label[i]);}
    {uint8_t* mbr=v.memory.data;hakefi::selfram::P32(mbr+440,lo*2654435761u^hi);
     uint8_t* e=mbr+446;e[0]=0;e[1]=0xfe;e[2]=0xff;e[3]=0xff;e[4]=0x0c;e[5]=0xfe;e[6]=0xff;e[7]=0xff;
     hakefi::selfram::P32(e+8,uint32_t(first));hakefi::selfram::P32(e+12,uint32_t(sectors-first));mbr[510]=0x55;mbr[511]=0xaa;}
    SignalReadyToBoot();
    why=4;
    status=service->Register(uint64_t(reinterpret_cast<uintptr_t>(v.memory.data)),bytes,&virtualDiskGuid,nullptr,&v.path);
    if(status!=EFI_SUCCESS || !v.path) {v.path=nullptr;DropVolume();return nullptr;}
    why=5;
    const size_t prefix=media::PathLength(static_cast<const uint8_t*>(v.path));
    {auto* remaining=static_cast<efi_device_path_protocol*>(v.path);efi_handle disk=nullptr;
     if(boot->LocateDevicePath(&media::blockIoGuid,&remaining,&disk)==EFI_SUCCESS && disk)boot->ConnectController(disk,nullptr,nullptr,true);}
    efi_handle volumes[32];const size_t n=Volumes(volumes,32);
    for(size_t i=0;i<n && !v.volume && prefix!=SIZE_MAX;++i) {
        size_t own=0;const uint8_t* p=DevicePathOf(volumes[i],own);
        if(!p || own<prefix)continue;
        bool under=true;for(size_t k=0;k<prefix && under;++k)under=p[k]==static_cast<const uint8_t*>(v.path)[k];
        if(under && !VolumeReadOnly(volumes[i]))v.volume=volumes[i];
    }
    if(!v.volume) {status=EFI_NOT_FOUND;DropVolume();return nullptr;}
    why=6;
    {const uint64_t base=uint64_t(reinterpret_cast<uintptr_t>(v.memory.data));
     const bool byFirmware=Published(base,bytes,virtualDiskGuid);
     charontrace::Value("CHARON ramdisk NFIT existed",uint64_t(NfitExists()));charontrace::Value("CHARON ramdisk published by firmware",uint64_t(byFirmware));
     const bool visible=byFirmware || Publish(base,bytes,virtualDiskGuid);
     charontrace::Value("CHARON ramdisk published",uint64_t(visible));
     if(!visible) {status=EFI_UNSUPPORTED;DropVolume();return nullptr;}}
    why=0;
    charontrace::Value("CHARON ramdisk volume bytes",bytes);
    return v.volume;
}
#ifdef HAK_CHARON_HTTPS
// Windows 11 straight from Microsoft, into memory. `locale`: a Windows
// locale name such as pl-PL. The installer is then driven by the user with
// Narrator (Control+Windows+Enter): an answer file cannot be added to a
// read-only disc image.
inline const char* MicrosoftLanguage(const char* locale) {
    struct Row {const char* locale;const char* name;};
    static const Row rows[]={{"pl-PL","Polish"},{"en-US","English"},{"en-GB","English International"},{"de-DE","German"},{"fr-FR","French"},
        {"es-ES","Spanish"},{"it-IT","Italian"},{"uk-UA","Ukrainian"},{"cs-CZ","Czech"}};
    for(const auto& r:rows) {size_t i=0;for(;r.locale[i] && r.locale[i]==locale[i];++i) {}if(!r.locale[i] && !locale[i])return r.name;}
    return "English";
}
// r401: the image is chosen in Charon as in Rufus: the version, then the
// language from the list Microsoft offers for it (Charon's own language comes
// first as the default). The architecture is the one Charon runs on, x64; the
// image holds the Home, Pro and Education editions.
struct Pick {bool windows10;char language[64];char locale[16];};
inline Pick& P() {static Pick pick;return pick;}
inline const char* LocaleOf(const char* name) {
    struct Row {const char* name;const char* locale;};
    static const Row rows[]={{"Arabic","ar-SA"},{"Brazilian Portuguese","pt-BR"},{"Bulgarian","bg-BG"},{"Chinese Simplified","zh-CN"},{"Chinese Traditional","zh-TW"},
        {"Croatian","hr-HR"},{"Czech","cs-CZ"},{"Danish","da-DK"},{"Dutch","nl-NL"},{"English","en-US"},{"English International","en-GB"},{"Estonian","et-EE"},
        {"Finnish","fi-FI"},{"French","fr-FR"},{"French Canadian","fr-CA"},{"German","de-DE"},{"Greek","el-GR"},{"Hebrew","he-IL"},{"Hungarian","hu-HU"},
        {"Italian","it-IT"},{"Japanese","ja-JP"},{"Korean","ko-KR"},{"Latvian","lv-LV"},{"Lithuanian","lt-LT"},{"Norwegian","nb-NO"},{"Polish","pl-PL"},
        {"Portuguese","pt-PT"},{"Romanian","ro-RO"},{"Russian","ru-RU"},{"Serbian Latin","sr-Latn-RS"},{"Slovak","sk-SK"},{"Slovenian","sl-SI"},
        {"Spanish","es-ES"},{"Spanish (Mexico)","es-MX"},{"Swedish","sv-SE"},{"Thai","th-TH"},{"Turkish","tr-TR"},{"Ukrainian","uk-UA"}};
    for(const auto& row:rows)if(msiso::SameNoCase(row.name,name))return row.locale;
    return "en-US";
}
inline bool ChooseLanguage(void*,const char (*names)[64],size_t count,size_t& index) {
    static char16_t wide[msiso::maxLanguages][64];static ask::Choice choices[msiso::maxLanguages];
    if(count>msiso::maxLanguages)count=msiso::maxLanguages;
    for(size_t i=0;i<count;++i) {size_t k=0;for(;names[i][k] && k<63;++k)wide[i][k]=char16_t(uint8_t(names[i][k]));wide[i][k]=0;choices[i]={wide[i],wide[i],""};}
    if(!ask::Choose(T(u"Język Windows, lista od Microsoftu:",u"Windows language, Microsoft's list:"),choices,count,index))return false;
    auto& pick=P();size_t k=0;for(;names[index][k] && k<63;++k)pick.language[k]=names[index][k];
    pick.language[k]=0;const char* locale=LocaleOf(pick.language);k=0;for(;locale[k] && k<15;++k)pick.locale[k]=locale[k];
    pick.locale[k]=0;return true;
}
inline msiso::Options LinkOptions() {
    msiso::Options options;options.choose=ChooseLanguage;
    if(P().windows10) {options.editionId=msiso::windows10EditionId;options.refererLine=msiso::referer10;options.readPage=false;}
    return options;
}
inline const char16_t* AskingText() {
    return P().windows10?T(u"Pytam Microsoft o obraz Windows 10 i listę języków.",u"Asking Microsoft for the Windows 10 image and the list of languages.")
        :T(u"Pytam Microsoft o obraz Windows 11 i listę języków.",u"Asking Microsoft for the Windows 11 image and the list of languages.");
}
inline bool WindowsFromMicrosoft(const char* locale) {
    if(!Find()) {Fail(T(u"To firmware nie ma usługi RAM-dysku, a obraz Windows nie mieści się w pliku na partycji FAT. Użyj pendrive'a z instalatorem.",
        u"This firmware has no RAM disk service, and the Windows image does not fit in a file on a FAT partition. Use a USB stick with the installer."));return false;}
    // The image is about 9 GB; Windows Setup needs some 3 GB for itself.
    const uint64_t spare=uint64_t(3)<<30;
    if(!Room(uint64_t(9)<<30,spare))return false;
    static https::Client client;static msiso::Link link;client.transport=net::tcp::Transport();
    Say(AskingText());
    const auto result=msiso::Windows11LinkAny(client,MicrosoftLanguage(locale),link,LinkOptions());
    if(result!=msiso::Result::ok) {
        static char16_t text[500];
        Set(text,500,result==msiso::Result::refused?T(u"Microsoft odmówił podania adresu; tak bywa po kilku prośbach z jednego adresu sieciowego. Spróbuj za kilka godzin albo użyj pendrive'a z instalatorem.",
                u"Microsoft refused to give the address; that happens after a few requests from one network address. Try again in a few hours or use a USB stick with the installer.")
            :result==msiso::Result::noClock?T(u"Firmware nie podaje daty, więc połączenia szyfrowanego nie da się sprawdzić.",u"The firmware gives no date, so the encrypted connection cannot be checked.")
            :T(u"Nie udało się uzyskać adresu obrazu od Microsoftu.",u"The address of the image could not be got from Microsoft."));
        Fail(text);return false;}
    if(link.hasSize && !Room(link.size,spare))return false;
    static char16_t wide[2048];size_t n=0;for(;link.url[n] && n<2047;++n)wide[n]=char16_t(uint8_t(link.url[n]));
    wide[n]=0;
    {static char16_t text[300];Set(text,300,T(u"Pobieram Windows 11 do pamięci: ",u"Downloading Windows 11 into memory: "));if(link.hasSize)systools::PutSize(text,300,link.size,Pl());
     Cat(text,300,T(u". To potrwa długo.",u". This takes long."));Say(text);}
    static Memory image;image.data=nullptr;image.capacity=0;image.size=0;image.extra=0;image.reserved=true;
    static FileRef none;none.volume=nullptr;none.path[0]=0;uint64_t received=0;
    if(!net::DownloadAny(wide,none,&received,&image,T(u"Windows 11",u"Windows 11")))return false;
    if(link.hasSha256) {
        Say(T(u"Sprawdzam sumę kontrolną obrazu.",u"Checking the image's checksum."));
        br_sha256_context hash;br_sha256_init(&hash);
        for(size_t at=0;at<image.size;) {size_t part=image.size-at;if(part>(4u<<20))part=4u<<20;br_sha256_update(&hash,image.data+at,part);at+=part;Pump();
            if((at>>22)%16==0) {static const char16_t* const what[2]={u"Sprawdzanie",u"Checking"};Progress(what[Pl()?0:1],at,image.size);}}
        uint8_t digest[32];br_sha256_out(&hash,digest);static const char hex[]="0123456789ABCDEF";bool same=true;
        for(int i=0;i<32 && same;++i) {
            char a=link.sha256[i*2],b=link.sha256[i*2+1];if(a>='a' && a<='f')a=char(a-32);if(b>='a' && b<='f')b=char(b-32);
            same=a==hex[digest[i]>>4] && b==hex[digest[i]&15];}
        if(!same) {Free(image);Fail(T(u"Suma kontrolna obrazu nie zgadza się z podaną przez Microsoft. Obraz odrzucony.",u"The image's checksum does not match Microsoft's. The image was rejected."));return false;}
    }
    Say(T(u"Windows 11 jest w pamięci. Uruchamiam instalator. Narratora włącza Control, Windows, Enter. Instalator prowadzisz sam.",
          u"Windows 11 is in memory. Starting Setup. Control, Windows, Enter turns Narrator on. You drive Setup yourself."));
    for(unsigned i=0;i<120;++i) {U().host->boot->Stall(100000);Pump();auto* fx=U().host->effects;if(i>5 && fx && fx->SpeechIdle && fx->SpeechIdle(fx->context))break;}
    if(!Boot(image,u"Windows Setup")) {Free(image);return false;}
    return true;
}
#endif
}  // namespace ramdisk
}  // namespace ws
}  // namespace hakefi
