#pragma once
// r402: Charon workspace - ready system images, the parts after the bytes:
//  - a qcow2 "cloud image" is converted to raw while it is fetched by range
//    requests (CharonQcow2.h) and written to the disk;
//  - the partition table of an image smaller than its disk is repaired, and a
//    small FAT partition CIDATA with cloud-init's user-data and meta-data is
//    added at the end of the disk (CharonSeedDisk.h), so the system makes its
//    user, turns speech on and registers Charon at its first start.
// Included from CharonWsInstall3.h after CharonWsImages.h. GPL-2.0-or-later.
#include "CharonInflate.h"
#include "CharonQcow2.h"
#include "CharonSeedDisk.h"
namespace hakefi {
namespace ws {
namespace images {
// ---- block access for CharonSeedDisk.h ------------------------------------------------
inline bool SeedRead(void* context,uint64_t lba,uint8_t* out,size_t blockCount) {
    auto* io=static_cast<media::BlockIo*>(context);
    return io->ReadBlocks(io,io->Media->MediaId,lba,blockCount*io->Media->BlockSize,out)==EFI_SUCCESS;
}
inline bool SeedWrite(void* context,uint64_t lba,const uint8_t* data,size_t blockCount) {
    auto* io=static_cast<media::BlockIo*>(context);
    return reinterpret_cast<BlockWrite>(io->WriteBlocks)(io,io->Media->MediaId,lba,blockCount*io->Media->BlockSize,const_cast<uint8_t*>(data))==EFI_SUCCESS;
}
inline void RandomGuid(seeddisk::Guid& g,uint64_t salt) {
    uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
    uint64_t seed=((uint64_t(hi)<<32)|lo)^(salt*0x9e3779b97f4a7c15ull);efi_time now{};
    auto* runtime=U().host->system->RuntimeServices;
    if(runtime && runtime->GetTime && runtime->GetTime(&now,nullptr)==EFI_SUCCESS)seed^=(uint64_t(now.Nanosecond)<<24)^(uint64_t(now.Second)<<8)^now.Minute^(uint64_t(now.Day)<<44);
    for(auto& b:g.bytes) {seed=seed*6364136223846793005ull+1442695040888963407ull;b=uint8_t(seed>>56);}
    g.bytes[7]=uint8_t((g.bytes[7]&0x0f)|0x40);g.bytes[8]=uint8_t((g.bytes[8]&0x3f)|0x80);   // version 4, variant 1
}
inline uint8_t* SeedWork() {static uint8_t work[seeddisk::kWorkBytes];return work;}
// r402 dual boot: the image's partition table has arrived (the first bytes of
// the image). A partition is made in the free space for every partition of
// the image except a BIOS boot partition, with the image's own type, unique
// GUID and name, in the image's order; 4 MiB at the end of the free space are
// kept for the configuration (CIDATA) partition.
inline bool PlanRemap(const uint8_t* head,size_t size) {
    auto& r=R();const disks::Disk& d=*r.disk;
    if(d.blockSize!=512 || size<1024)return false;
    static const char signature[]="EFI PART";for(int i=0;i<8;++i)if(head[512+i]!=uint8_t(signature[i]))return false;
    const uint64_t entryLba=systools::Le64(head+512+72);const uint32_t count=systools::Le32(head+512+80),entrySize=systools::Le32(head+512+84);
    if(entrySize<128 || entrySize>512 || !count || count>128 || entryLba<2 || entryLba*512+uint64_t(count)*entrySize>size)return false;
    static const uint8_t biosBoot[16]={0x48,0x61,0x68,0x21,0x49,0x64,0x6f,0x6e,0x74,0x4e,0x65,0x65,0x64,0x45,0x46,0x49};
    static const uint8_t espType[16]={0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b};
    const uint64_t mega=uint64_t(1)<<20;
    auto same=[](const uint8_t* a,const uint8_t* b) {for(int i=0;i<16;++i)if(a[i]!=b[i])return false;return true;};
    // Does it fit?
    uint64_t cursor=(r.gapFirst+mega-1)/mega*mega;const uint64_t seedAt=(r.gapEnd-4*mega)/mega*mega;uint64_t need=0;size_t wanted=0;
    for(uint32_t i=0;i<count;++i) {
        const uint8_t* e=head+entryLba*512+size_t(i)*entrySize;bool used=false;for(int k=0;k<16;++k)if(e[k])used=true;
        if(!used || same(e,biosBoot))continue;
        const uint64_t first=systools::Le64(e+32),last=systools::Le64(e+40);if(last<first)return false;
        need+=((last-first+1)*512+mega-1)/mega*mega;++wanted;
    }
    r.need=need+4*mega;
    if(!wanted || wanted>8 || r.gapEnd<r.gapFirst+8*mega || cursor+need>seedAt)return false;
    seeddisk::Disk disk{d.io,d.blockSize,d.blocks,SeedRead,SeedWrite};
    r.count=0;r.hasEsp=false;
    // The largest partition (the system itself) goes last, so that it can grow
    // into the free space behind it at the first start (a Debian placed before
    // its own boot partition stayed at 2.9 GB in the first test).
    uint32_t largest=0;uint64_t largestBlocks=0;
    for(uint32_t i=0;i<count;++i) {
        const uint8_t* e=head+entryLba*512+size_t(i)*entrySize;bool used=false;for(int k=0;k<16;++k)if(e[k])used=true;
        if(!used || same(e,biosBoot))continue;
        const uint64_t blocks=systools::Le64(e+40)-systools::Le64(e+32)+1;
        if(blocks>largestBlocks) {largestBlocks=blocks;largest=i;}
    }
    for(int pass=0;pass<2;++pass)
    for(uint32_t i=0;i<count;++i) {
        const uint8_t* e=head+entryLba*512+size_t(i)*entrySize;bool used=false;for(int k=0;k<16;++k)if(e[k])used=true;
        if(!used || same(e,biosBoot) || (i==largest)!=(pass==1))continue;
        const uint64_t first=systools::Le64(e+32),last=systools::Le64(e+40),bytes=(last-first+1)*512;
        seeddisk::AddRequest request{};static char16_t name[40];size_t n=0;
        for(;n<36;++n) {name[n]=char16_t(e[56+n*2]|e[57+n*2]<<8);if(!name[n])break;}
        name[n]=0;
        for(int k=0;k<16;++k) {request.type.bytes[k]=e[k];request.unique.bytes[k]=e[16+k];}
        request.name=name;request.sizeBytes=bytes;request.alignmentBytes=0;request.placement=seeddisk::Placement::At;request.firstLba=cursor/512;
        request.attributes=systools::Le64(e+48);
        seeddisk::AddResult added{};
        const auto error=seeddisk::AddPartition(disk,SeedWork(),seeddisk::kWorkBytes,request,added);
        charontrace::Value("CHARON dual partition",(uint64_t(error)<<56)|added.firstLba);
        if(error!=seeddisk::Error::None)return false;
        r.segments[r.count++]={first*512,first*512+bytes,cursor};
        if(same(e,espType) && !r.hasEsp) {r.hasEsp=true;for(int k=0;k<16;++k)r.espUnique[k]=e[16+k];}
        cursor=(cursor+bytes+mega-1)/mega*mega;
    }
    r.seedLba=seedAt/512;r.ready=true;
    return true;
}
// Repairs the table for the real size of the disk and adds the CIDATA
// partition with the two files. False: the disk is as the image left it.
// Dual boot (R().on): the table is the disk's own; the partition goes to the
// place kept for it at the end of the free space.
inline bool SeedOnto(const disks::Disk& d,const char* userData,size_t userSize,const char* metaData,size_t metaSize) {
    uint8_t* work=SeedWork();const size_t workSize=seeddisk::kWorkBytes;
    seeddisk::Disk disk{d.io,d.blockSize,d.blocks,SeedRead,SeedWrite};
    auto error=seeddisk::Error::None;
    if(!R().on) {
        error=seeddisk::Relocate(disk,work,workSize);
        charontrace::Value("CHARON seed relocate",uint64_t(error));
        if(error!=seeddisk::Error::None)return false;
    }
    seeddisk::AddRequest request{};request.type=seeddisk::kTypeBasicData;RandomGuid(request.unique,1);
    request.name=u"cidata";request.sizeBytes=uint64_t(4)<<20;request.alignmentBytes=0;request.placement=seeddisk::Placement::End;request.attributes=0;
    if(R().on) {request.placement=seeddisk::Placement::At;request.firstLba=R().seedLba;}
    seeddisk::AddResult added{};
    error=seeddisk::AddPartition(disk,work,workSize,request,added);
    charontrace::Value("CHARON seed partition",(uint64_t(error)<<56)|added.firstLba);
    if(error!=seeddisk::Error::None)return false;
    static Memory memory;memory.reserved=false;memory.extra=0;
    const size_t bytes=size_t(added.lbaCount*d.blockSize);
    if(!Reserve(memory,bytes))return false;
    memset(memory.data,0,bytes);
    const seeddisk::File files[2]={{"user-data",reinterpret_cast<const uint8_t*>(userData),userSize},{"meta-data",reinterpret_cast<const uint8_t*>(metaData),metaSize}};
    seeddisk::FatOptions options{};options.sectorSize=d.blockSize;options.hiddenSectors=uint32_t(added.firstLba);
    {seeddisk::Guid serial;RandomGuid(serial,2);options.serial=uint32_t(serial.bytes[0])|uint32_t(serial.bytes[1])<<8|uint32_t(serial.bytes[2])<<16|uint32_t(serial.bytes[3])<<24;}
    efi_time now{};auto* runtime=U().host->system->RuntimeServices;
    if(runtime && runtime->GetTime && runtime->GetTime(&now,nullptr)==EFI_SUCCESS) {
        options.time.year=now.Year;options.time.month=now.Month;options.time.day=now.Day;options.time.hour=now.Hour;options.time.minute=now.Minute;options.time.second=now.Second;}
    error=seeddisk::BuildFat(memory.data,bytes,"CIDATA",files,2,options);
    charontrace::Value("CHARON seed fat",uint64_t(error));
    if(error==seeddisk::Error::None)error=seeddisk::WriteImage(disk,added.firstLba,added.lbaCount,memory.data,bytes);
    memset(memory.data,0,bytes);Free(memory);   // the password hash was in there
    if(d.io->FlushBlocks)reinterpret_cast<BlockFlush>(d.io->FlushBlocks)(d.io);
    charontrace::Value("CHARON seed written",uint64_t(error));
    return error==seeddisk::Error::None;
}
// ---- qcow2 over HTTP ranges ---------------------------------------------------------------
constexpr size_t windowBytes=size_t(16)<<20;   // one range request (a new connection each) per 16 MiB: 4 MiB gave about 1 MB/s
struct Qcow {
    https::Client client;char url[1024];uint64_t fileSize;
    uint8_t* window;uint64_t windowAt;size_t windowFill;      // one read-ahead window of the image file
    media::BlockIo* io;uint32_t blockSize;uint64_t blocks;
    uint8_t* zeros;bool zeroFill;
    const char16_t* label;unsigned rounds;bool failed,cancelled;https::Result last;efi_status status;
};
inline Qcow& Q() {static Qcow state;return state;}
struct WindowSink {uint8_t* out;size_t capacity,got;};
inline bool WindowTake(void* context,const uint8_t* data,size_t size) {
    auto& s=*static_cast<WindowSink*>(context);if(s.got+size>s.capacity)return false;
    memcpy(s.out+s.got,data,size);s.got+=size;return true;
}
inline bool QcowFetch(uint64_t offset,uint8_t* out,size_t size) {
    auto& q=Q();static https::Response answer;
    for(unsigned attempt=0;attempt<4;++attempt) {
        https::Request rq;rq.url=q.url;rq.range=true;rq.rangeStart=offset;rq.rangeEnd=offset+size-1;
        WindowSink sink{out,size,0};
        q.last=https::Get(q.client,rq,answer,WindowTake,&sink);Pump();
        if(q.last==https::Result::ok && sink.got==size)return true;
    }
    return false;
}
inline bool QcowRead(void*,uint64_t offset,uint8_t* out,size_t size) {
    auto& q=Q();
    while(size) {
        if(offset>=q.fileSize)return false;
        if(!q.windowFill || offset<q.windowAt || offset>=q.windowAt+q.windowFill) {
            size_t want=windowBytes;if(offset+want>q.fileSize)want=size_t(q.fileSize-offset);
            if(!QcowFetch(offset,q.window,want)) {q.failed=true;return false;}
            q.windowAt=offset;q.windowFill=want;
        }
        size_t part=size_t(q.windowAt+q.windowFill-offset);if(part>size)part=size;
        memcpy(out,q.window+(offset-q.windowAt),part);out+=part;offset+=part;size-=part;
    }
    return true;
}
inline bool QcowPut(uint64_t guestOffset,const uint8_t* data,size_t size) {
    auto& q=Q();
    if(R().on) {if(PutMapped(q.io,guestOffset,data,size))return true;q.failed=true;q.status=EFI_DEVICE_ERROR;return false;}
    if(guestOffset%q.blockSize || size%q.blockSize || guestOffset/q.blockSize+size/q.blockSize>q.blocks) {q.failed=true;return false;}
    q.status=reinterpret_cast<BlockWrite>(q.io->WriteBlocks)(q.io,q.io->Media->MediaId,guestOffset/q.blockSize,size,const_cast<uint8_t*>(data));
    if(q.status!=EFI_SUCCESS) {q.failed=true;return false;}
    return true;
}
inline bool QcowWrite(void*,uint64_t guestOffset,const uint8_t* data,size_t size) {return QcowPut(guestOffset,data,size);}
// Space the image leaves empty must read as zero: on a disk that held data it is written out.
inline bool QcowZero(void*,uint64_t guestOffset,uint64_t size,bool) {
    auto& q=Q();if(!q.zeroFill)return true;
    while(size) {
        size_t part=size>chunkBytes?chunkBytes:size_t(size);
        if(!QcowPut(guestOffset,q.zeros,part))return false;
        guestOffset+=part;size-=part;
        if(++q.rounds%8==0)Pump();
    }
    return true;
}
inline bool QcowProgress(void*,uint64_t done,uint64_t total) {
    auto& q=Q();
    if(++q.rounds%8==0)Progress(q.label,done,total);
    if(q.rounds%32==0 && EscapePressed()) {q.cancelled=true;return false;}
    Pump();return true;
}
// Fetches the qcow2 image at `url`, converts it and writes the raw disk onto `d`.
inline bool QcowToDisk(const char* url,const disks::Disk& d,const char16_t* what,uint64_t* written) {
    auto& q=Q();static https::Response probe;static inflate::Decoder decoder;
    if(written)*written=0;
    if(d.hasCharon && !R().on) {Fail(T(u"To jest dysk, z którego działa Charon. Obrazu nie zapisuje się na własny dysk Charona.",u"This is the disk Charon runs from. An image is not written onto Charon's own disk."));return false;}
    if(d.readOnly || !d.io || !d.io->WriteBlocks) {Fail(T(u"Tego dysku nie da się zapisać.",u"This disk cannot be written."));return false;}
    q.client.transport=net::tcp::Transport();
    auto result=https::Probe(q.client,url,probe);
    if(result!=https::Result::ok) {net::HttpsFail(q.client,result);return false;}
    q.fileSize=probe.totalLength!=https::unknownLength?probe.totalLength:probe.contentLength!=https::unknownLength?probe.contentLength:0;
    if(!q.fileSize) {Fail(T(u"Serwer nie podał rozmiaru obrazu.",u"The server did not give the image's size."));return false;}
    // The address after redirects: every range request then goes straight to the mirror.
    {const char* final=q.client.url[0]?q.client.url:url;size_t n=0;for(;final[n] && n+1<sizeof(q.url);++n)q.url[n]=final[n];q.url[n]=0;}
    q.windowFill=0;q.windowAt=0;q.failed=q.cancelled=false;q.rounds=0;q.label=what;q.io=d.io;q.blockSize=d.blockSize;q.blocks=d.blocks;q.zeroFill=!d.blank || R().on;
    static Memory windowMemory,l1Memory,l2Memory,clusterMemory,compressedMemory,zeroMemory;
    Memory* all[]={&windowMemory,&l1Memory,&l2Memory,&clusterMemory,&compressedMemory,&zeroMemory};
    for(auto* m:all) {m->data=nullptr;m->capacity=0;m->size=0;m->extra=0;m->reserved=false;}
    auto release=[&]() {for(auto* m:all)if(m->data)Free(*m);};
    if(!Reserve(windowMemory,windowBytes)) {Fail(T(u"Brak pamięci.",u"Out of memory."));return false;}
    q.window=windowMemory.data;
    qcow2::Source source{nullptr,QcowRead};qcow2::Image image{};
    auto error=qcow2::Open(source,image);
    if(error!=qcow2::Error::None) {
        release();
        if(q.failed)net::HttpsFail(q.client,q.last);
        else Fail(T(u"Tego obrazu Charon nie umie odczytać: to nie jest zwykły obraz qcow2 ze zwykłą kompresją.",u"Charon cannot read this image: it is not a plain qcow2 image with plain compression."));
        return false;}
    if(!R().on && image.virtualSize>disks::Bytes(d)) {
        release();static char16_t text[300];Set(text,300,T(u"System z tego obrazu potrzebuje większego dysku: ",u"The system in this image needs a larger disk: "));systools::PutSize(text,300,image.virtualSize,Pl());Fail(text);return false;}
    const size_t clusterBytes=image.clusterSize>chunkBytes?size_t(image.clusterSize):chunkBytes;
    if(!Reserve(l1Memory,size_t(qcow2::L1Bytes(image))+4096) || !Reserve(l2Memory,size_t(image.clusterSize)) || !Reserve(clusterMemory,clusterBytes)
        || !Reserve(compressedMemory,clusterBytes*2) || !Reserve(zeroMemory,chunkBytes)) {release();Fail(T(u"Brak pamięci.",u"Out of memory."));return false;}
    memset(zeroMemory.data,0,chunkBytes);q.zeros=zeroMemory.data;
    {static char16_t text[400];Set(text,400,T(u"Pobieram obraz, rozpakowuję go i zapisuję na dysk. System zajmie ",u"Downloading the image, unpacking it and writing it to the disk. The system takes "));systools::PutSize(text,400,image.virtualSize,Pl());
     Cat(text,400,T(u". Escape przerywa; dysk zostaje wtedy z niepełnym obrazem.",u". Escape stops it; the disk is then left with an incomplete image."));Say(text);install::HearOut(100);}
    qcow2::Work work{};work.l1=l1Memory.data;work.l1Bytes=size_t(qcow2::L1Bytes(image));work.l2=l2Memory.data;work.l2Bytes=size_t(image.clusterSize);
    work.cluster=clusterMemory.data;work.clusterBytes=clusterBytes;work.compressed=compressedMemory.data;work.compressedBytes=clusterBytes*2;work.decoder=&decoder;work.fileSize=q.fileSize;
    qcow2::Sink sink{nullptr,QcowWrite};qcow2::Hooks hooks{nullptr,QcowZero,QcowProgress};
    error=qcow2::Convert(image,source,work,sink,&hooks);
    charontrace::Value("CHARON qcow2 result",uint64_t(error));
    if(error==qcow2::Error::None && d.io->FlushBlocks)reinterpret_cast<BlockFlush>(d.io->FlushBlocks)(d.io);
    release();
    if(error==qcow2::Error::None) {Progress(what,image.virtualSize,image.virtualSize);if(written)*written=image.virtualSize;return true;}
    if(q.cancelled)Fail(T(u"Przerwano. Na dysku jest niepełny obraz; nie da się z niego uruchomić systemu.",u"Stopped. The disk holds an incomplete image; no system can start from it."));
    else if(R().on && R().failed)Fail(T(u"Tego obrazu nie da się wpisać w wolne miejsce: nie ma zwykłej tablicy partycji GPT z sektorami 512 bajtów albo jego partycje nie mieszczą się w wolnym miejscu.",
        u"This image cannot be put into free space: it has no ordinary GPT partition table with 512-byte sectors, or its partitions do not fit the free space."));
    else if(q.failed && q.status!=EFI_SUCCESS)Fail(T(u"Zapis na dysk nie powiódł się.",u"Writing to the disk failed."),q.status);
    else if(q.failed)net::HttpsFail(q.client,q.last);
    else Fail(T(u"Obraz jest uszkodzony albo ma budowę, której Charon nie zna.",u"The image is damaged or built in a way Charon does not know."));
    return false;
}
}  // namespace images
}  // namespace ws
}  // namespace hakefi
