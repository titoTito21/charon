#pragma once
// r401: Charon workspace - Windows install media made by Charon itself.
// The Windows 11 disc image is downloaded once, in one pass, and its files
// are written straight onto an existing FAT volume of a USB stick (nothing is
// formatted, other files stay). FAT cannot hold a file of 4 GB, and
// sources\install.wim is larger: it is split on the fly into install.swm,
// install2.swm ... which Windows Setup accepts (CharonWimSplit.h). The image
// is a UDF disc (CharonUdf.h); its directory is read first by ranged requests,
// then the whole image streams through and every byte goes to its file.
// Charon's own start-up program (\EFI\BOOT\BOOTX64.EFI) is never replaced:
// the Windows boot manager goes to \EFI\Microsoft\Boot\bootmgfw.efi.
// The address comes from Microsoft (CharonMsIso.h) or, when the file exists,
// from \EFI\HAK\windows-iso-url.txt on Charon's disk (first line: the address;
// an optional line "split-mb=N" sets the part size, for tests).
// GPL-2.0-or-later.
#include "CharonWsRamDisk.h"
#ifdef HAK_CHARON_HTTPS
#include "CharonUdf.h"
#include "CharonWimSplit.h"
namespace hakefi {
namespace ws {
namespace winusb {
constexpr size_t fileCapacity=4096,extentCapacity=8192,partCapacity=16,cacheBlocks=6;
constexpr uint64_t cacheBlock=uint64_t(1)<<20;
// ---- where to write -------------------------------------------------------------
inline bool OnUsb(efi_handle volume) {
    size_t bytes=0;const uint8_t* p=DevicePathOf(volume,bytes);if(!p)return false;
    for(size_t at=0;at+4<=bytes;) {
        const uint8_t type=p[at],sub=p[at+1];const size_t length=size_t(p[at+2])|(size_t(p[at+3])<<8);
        if(length<4 || type==0x7f)break;
        if(type==3 && (sub==5 || sub==15 || sub==16))return true;   // USB, USB WWID, USB class
        at+=length;
    }
    return false;
}
// r402: is the volume a GPT partition of the type "EFI system partition"? Windows
// gives such a partition on a stick no drive letter, so Windows Setup started from
// it cannot find its own files ("a media driver your computer needs is missing":
// seen with a real Windows 11 image in the test machine).
inline bool EspOnGpt(efi_handle volume) {
    size_t bytes=0;const uint8_t* p=DevicePathOf(volume,bytes);if(!p)return false;
    const uint8_t* unique=nullptr;
    for(size_t at=0;at+4<=bytes;) {
        const uint8_t type=p[at],sub=p[at+1];const size_t length=size_t(p[at+2])|(size_t(p[at+3])<<8);
        if(length<4 || type==0x7f)break;
        if(type==4 && sub==1 && length>=42 && p[at+40]==2 && p[at+41]==2)unique=p+at+24;
        at+=length;
    }
    if(!unique)return false;
    auto& s=disks::D();disks::Load();
    for(size_t i=0;i<s.count;++i)for(size_t k=0;k<s.list[i].partCount;++k) {
        const auto& part=s.list[i].parts[k];bool same=true;
        for(int b=0;b<16 && same;++b)same=part.unique[b]==unique[b];
        if(same)return part.kind==1;
    }
    return false;
}
struct Target {efi_handle volume;uint64_t freeBytes;};
// Writable volumes on USB sticks; `need` = 0 lists them all with their free space.
inline size_t Targets(Target* out,size_t maximum,uint64_t need) {
    efi_handle volumes[32];const size_t n=Volumes(volumes,32);size_t count=0;
    for(size_t i=0;i<n && count<maximum;++i) {
        if(!OnUsb(volumes[i]))continue;
        efi_file_protocol* root=OpenRoot(volumes[i]);if(!root)continue;
        alignas(8) uint8_t info[512]={};size_t size=sizeof(info);uint64_t freeBytes=0;bool readOnly=true;
        if(root->GetInfo(root,&media::fsInfoGuid,&size,info)==EFI_SUCCESS && size>36) {readOnly=info[8]!=0;freeBytes=systools::Le64(info+24);}
        root->Close(root);
        if(readOnly || freeBytes<need)continue;
        out[count].volume=volumes[i];out[count].freeBytes=freeBytes;++count;
    }
    return count;
}
// ---- the image over the network ------------------------------------------------
struct Image {
    https::Client client;char url[https::urlUnits];char validator[160];uint64_t total;
    uint8_t* cache;uint64_t cacheAt[cacheBlocks];unsigned cacheAge[cacheBlocks];unsigned clock;   // 1 MiB blocks of the image read by range
    https::Result lastResult;
};
inline Image& I() {static Image image;return image;}
struct RangeSink {uint8_t* out;size_t capacity,got;};
inline bool RangeTake(void* context,const uint8_t* data,size_t size) {
    auto& s=*static_cast<RangeSink*>(context);if(s.got+size>s.capacity)return false;
    memcpy(s.out+s.got,data,size);s.got+=size;return true;
}
inline bool FetchRange(uint64_t offset,uint8_t* out,size_t size) {
    auto& im=I();static https::Response answer;
    for(unsigned attempt=0;attempt<3;++attempt) {
        https::Request rq;rq.url=im.url;rq.range=true;rq.rangeStart=offset;rq.rangeEnd=offset+size-1;
        RangeSink sink{out,size,0};
        im.lastResult=https::Get(im.client,rq,answer,RangeTake,&sink);Pump();
        if(im.lastResult==https::Result::ok && sink.got==size)return true;
    }
    return false;
}
// udf::Source: reads through a small cache (the disc's directory lies close together).
inline bool ReadImage(void*,uint64_t offset,uint8_t* out,size_t size) {
    auto& im=I();
    while(size) {
        const uint64_t block=offset/cacheBlock*cacheBlock;size_t slot=cacheBlocks;
        for(size_t i=0;i<cacheBlocks;++i)if(im.cacheAge[i] && im.cacheAt[i]==block)slot=i;
        if(slot==cacheBlocks) {
            slot=0;for(size_t i=1;i<cacheBlocks;++i)if(im.cacheAge[i]<im.cacheAge[slot])slot=i;
            uint64_t bytes=cacheBlock;if(block+bytes>im.total)bytes=im.total-block;
            if(block>=im.total || !FetchRange(block,im.cache+slot*cacheBlock,size_t(bytes))) {im.cacheAge[slot]=0;return false;}
            im.cacheAt[slot]=block;
        }
        im.cacheAge[slot]=++im.clock;
        const size_t inside=size_t(offset-block);size_t part=size_t(cacheBlock)-inside;if(part>size)part=size;
        if(offset+part>im.total)return false;
        memcpy(out,im.cache+slot*cacheBlock+inside,part);out+=part;offset+=part;size-=part;
    }
    return true;
}
// ---- the job ---------------------------------------------------------------------
struct Job {
    efi_handle volume;udf::Map map;udf::Router router;
    uint32_t wimFile;bool split;wimsplit::Plan plan;wimsplit::Writer writer;
    uint32_t loaderFile;bool redirectLoader;      // the image's \efi\boot\bootx64.efi written as the Windows boot manager
    uint8_t* started;                              // per file: already created in this run
    uint32_t openFile;efi_file_protocol* handle;
    efi_file_protocol* parts[partCapacity];
    uint64_t done;unsigned rounds;bool failed,cancelled;const char16_t* why;
};
inline Job& J() {static Job job;return job;}
inline bool SamePath(const char16_t* a,const char16_t* b) {
    for(size_t i=0;;++i) {
        char16_t x=a[i],y=b[i];if(x>=u'A' && x<=u'Z')x=char16_t(x+32);if(y>=u'A' && y<=u'Z')y=char16_t(y+32);
        if(x!=y)return false;
        if(!x)return true;
    }
}
inline uint32_t FindFile(const udf::Map& map,const char16_t* path) {
    for(size_t i=0;i<map.fileCount;++i)if(!map.files[i].directory && SamePath(map.files[i].path,path))return uint32_t(i);
    return UINT32_MAX;
}
// Bytes of one file of the image, by ranged reads (headers and tables of install.wim).
inline bool ReadFile(const udf::Map& map,uint32_t file,uint64_t fileOffset,uint8_t* out,size_t size) {
    while(size) {
        bool found=false;
        for(size_t i=0;i<map.extentCount && !found;++i) {
            const auto& e=map.extents[i];
            if(e.file!=file || fileOffset<e.fileOffset || fileOffset>=e.fileOffset+e.length)continue;
            size_t part=size_t(e.fileOffset+e.length-fileOffset);if(part>size)part=size;
            if(e.zero)memset(out,0,part);
            else {
                // Large pieces straight from the network; small ones through the cache.
                if(part>=cacheBlock) {if(part>(size_t(4)<<20))part=size_t(4)<<20;if(!FetchRange(e.offset+(fileOffset-e.fileOffset),out,part))return false;}
                else if(!ReadImage(nullptr,e.offset+(fileOffset-e.fileOffset),out,part))return false;
            }
            out+=part;fileOffset+=part;size-=part;found=true;
        }
        if(!found)return false;
    }
    return true;
}
inline efi_file_protocol* OpenForWrite(efi_handle volume,const char16_t* path,bool fresh) {
    static FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,path);efi_file_protocol* file=nullptr;
    if(fresh && OpenFile(ref,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&file)==EFI_SUCCESS && file) {
        if(file->Delete(file)!=EFI_SUCCESS)return nullptr;
        file=nullptr;
    }
    if(OpenFile(ref,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0,&file)!=EFI_SUCCESS)return nullptr;
    return file;
}
inline bool WriteAt(efi_file_protocol* file,uint64_t offset,const uint8_t* data,size_t size) {
    if(file->SetPosition(file,offset)!=EFI_SUCCESS)return false;
    size_t wrote=size;
    return file->Write(file,&wrote,const_cast<uint8_t*>(data))==EFI_SUCCESS && wrote==size;
}
inline void PartName(uint32_t part,char16_t* out,size_t cap) {
    Set(out,cap,u"\\sources\\install");if(part>1)CatN(out,cap,part);Cat(out,cap,u".swm");
}
inline bool PartSink(void*,uint32_t part,uint64_t partOffset,const uint8_t* data,size_t size) {
    auto& j=J();if(!part || part>partCapacity) {j.failed=true;return false;}
    auto*& file=j.parts[part-1];
    if(!file) {static char16_t name[64];PartName(part,name,64);file=OpenForWrite(j.volume,name,true);}
    if(!file || !WriteAt(file,partOffset,data,size)) {j.failed=true;j.why=T(u"Zapis na nośnik nie powiódł się; może brakuje miejsca.",u"Writing to the media failed; it may be full.");return false;}
    return true;
}
inline bool FileSink(void*,uint32_t file,uint64_t fileOffset,const uint8_t* data,size_t size) {
    auto& j=J();
    if(j.split && file==j.wimFile) {
        if(wimsplit::Feed(j.writer,fileOffset,data,size,PartSink,nullptr))return true;
        if(!j.failed) {j.failed=true;j.why=T(u"Obraz systemu ma układ, którego Charon nie potrafi podzielić na części.",u"The system image has a layout Charon cannot split into parts.");}
        return false;
    }
    if(j.openFile!=file) {
        if(j.handle) {j.handle->Close(j.handle);j.handle=nullptr;}
        static char16_t path[pathUnits];
        if(file==j.loaderFile && j.redirectLoader)Set(path,pathUnits,u"\\efi\\microsoft\\boot\\bootmgfw.efi");
        else {Set(path,pathUnits,u"\\");Cat(path,pathUnits,j.map.files[file].path);}
        const bool fresh=!(j.started[file>>3]&(1u<<(file&7)));j.started[file>>3]|=uint8_t(1u<<(file&7));
        j.handle=OpenForWrite(j.volume,path,fresh);j.openFile=file;
        if(!j.handle) {j.failed=true;j.why=T(u"Nie udało się utworzyć pliku na nośniku.",u"A file could not be created on the media.");return false;}
    }
    if(!WriteAt(j.handle,fileOffset,data,size)) {j.failed=true;j.why=T(u"Zapis na nośnik nie powiódł się; może brakuje miejsca.",u"Writing to the media failed; it may be full.");return false;}
    return true;
}
inline bool StreamSink(void*,const uint8_t* data,size_t size) {
    auto& j=J();
    if(!udf::Feed(j.router,j.done,data,size,FileSink,nullptr)) {
        if(!j.failed) {j.failed=true;j.why=T(u"Obraz ISO jest uszkodzony albo ma nieznany układ.",u"The ISO image is damaged or has an unknown layout.");}
        return false;
    }
    j.done+=size;
    if(++j.rounds%32==0) {
        static const char16_t* const what[2]={u"Windows 11 na nośnik",u"Windows 11 to the media"};
        Progress(what[Pl()?0:1],j.done,I().total);
        if(j.rounds%256==0 && EscapePressed()) {j.cancelled=true;return false;}
    }
    Pump();
    return true;
}
inline void CloseAll() {
    auto& j=J();
    if(j.handle) {if(j.handle->Flush)j.handle->Flush(j.handle);j.handle->Close(j.handle);j.handle=nullptr;}
    for(auto*& p:j.parts)if(p) {if(p->Flush)p->Flush(p);p->Close(p);p=nullptr;}
}
// r402: an evaluation image has a fixed address (Preset); the spoken name of what is fetched (Product).
inline const char*& Preset() {static const char* url=nullptr;return url;}
inline const char16_t*& Product() {static const char16_t* name=u"Windows 11";return name;}
// The address: the override file, else the preset one, else Microsoft. `splitMb`: part size.
inline bool Address(const char* locale,unsigned& splitMb) {
    auto& im=I();im.url[0]=0;splitMb=3800;
    static FileRef ref;ref.volume=U().host->selfDevice;Set(ref.path,pathUnits,u"\\EFI\\HAK\\windows-iso-url.txt");
    uint8_t* data=nullptr;size_t size=0;
    if(ReadWhole(ref,4096,&data,&size)==EFI_SUCCESS && data) {
        size_t n=0;while(n<size && n+1<sizeof(im.url) && data[n]>32 && data[n]<127) {im.url[n]=char(data[n]);++n;}
        im.url[n]=0;
        static const char key[]="split-mb=";
        for(size_t i=n;i+sizeof(key)-1<size;++i) {
            if(udf::BytesDiffer(data+i,key,sizeof(key)-1))continue;
            unsigned value=0;for(size_t k=i+sizeof(key)-1;k<size && data[k]>='0' && data[k]<='9';++k)value=value*10+unsigned(data[k]-'0');
            if(value>=16 && value<=4000)splitMb=value;
        }
        Release(data);
        if(im.url[0]) {Say(T(u"Używam adresu obrazu z pliku windows iso url na dysku Charona.",u"Using the image address from the file windows iso url on Charon's disk."));return true;}
    }
    if(Preset()) {
        size_t n=0;for(;Preset()[n] && n+1<sizeof(im.url);++n)im.url[n]=Preset()[n];
        im.url[n]=0;Say(T(u"Używam obrazu wersji próbnej z serwera Microsoftu.",u"Using the evaluation image from Microsoft's server."));return true;
    }
    static msiso::Link link;
    Say(ramdisk::AskingText());
    const auto result=msiso::Windows11LinkAny(im.client,ramdisk::MicrosoftLanguage(locale),link,ramdisk::LinkOptions());
    charontrace::Text("msiso stage");charontrace::Text(link.stage?link.stage:"-");charontrace::Text(link.error);charontrace::Text(link.errorKey);charontrace::Value("msiso http",uint64_t(link.httpStatus));charontrace::Value("msiso error type",uint64_t(link.errorType));
    if(result!=msiso::Result::ok) {
        Fail(result==msiso::Result::refused?T(u"Microsoft odmówił podania adresu; zdarza się to po kilku próbach z tego samego adresu sieciowego. Spróbuj ponownie za kilka godzin. Możesz też wpisać własny adres obrazu do pliku windows iso url w folderze EFI HAK na dysku Charona.",
                u"Microsoft refused to give the address; this happens after several requests from the same network address. Try again in a few hours. You can also put your own image address into the file windows iso url in the folder EFI HAK on Charon's disk.")
            :result==msiso::Result::noClock?T(u"Firmware nie podaje daty, więc połączenia szyfrowanego nie da się sprawdzić.",u"The firmware gives no date, so the encrypted connection cannot be checked.")
            :T(u"Nie udało się uzyskać adresu obrazu od Microsoftu.",u"The address of the image could not be obtained from Microsoft."));
        return false;}
    size_t n=0;for(;link.url[n] && n+1<sizeof(im.url);++n)im.url[n]=link.url[n];
    im.url[n]=0;return true;
}
// Downloads the image and writes the install media onto `target`. True: the
// volume now is Windows install media.
inline bool Make(efi_handle target,const char* locale) {
    auto& im=I();auto& j=J();unsigned splitMb=3800;
    static Memory cacheMemory,filesMemory,extentsMemory,startedMemory,tableMemory,resourceMemory,xmlMemory,scratchMemory;
    Memory* all[]={&cacheMemory,&filesMemory,&extentsMemory,&startedMemory,&tableMemory,&resourceMemory,&xmlMemory,&scratchMemory};
    auto release=[&]() {CloseAll();for(auto* m:all)if(m->data)Free(*m);};
    for(auto* m:all) {m->data=nullptr;m->capacity=0;m->size=0;m->extra=0;m->reserved=false;}
    memset(&j,0,sizeof(j));j.volume=target;j.openFile=UINT32_MAX;j.wimFile=UINT32_MAX;j.loaderFile=UINT32_MAX;
    memset(&im.cacheAt,0,sizeof(im.cacheAt));memset(&im.cacheAge,0,sizeof(im.cacheAge));im.clock=0;
    im.client.transport=net::tcp::Transport();
    if(!Address(locale,splitMb))return false;
    static https::Response probe;
    auto result=https::Probe(im.client,im.url,probe);
    if(result!=https::Result::ok) {net::HttpsFail(im.client,result);return false;}
    im.total=probe.totalLength!=https::unknownLength?probe.totalLength:probe.contentLength!=https::unknownLength?probe.contentLength:0;
    {size_t k=0;for(;probe.validator[k] && k+1<sizeof(im.validator);++k)im.validator[k]=probe.validator[k];im.validator[k]=0;}
    if(im.total<(uint64_t(1)<<20)) {Fail(T(u"Serwer nie podał rozmiaru obrazu.",u"The server did not give the image's size."));return false;}
    // Room: the whole image and a little for the parts' own tables.
    {Target one[1];bool room=false;Target list[16];const size_t n=Targets(list,16,im.total+(uint64_t(64)<<20));
     for(size_t i=0;i<n;++i)if(list[i].volume==target)room=true;
     (void)one;
     if(!room) {static char16_t text[300];Set(text,300,T(u"Na nośniku jest za mało wolnego miejsca. Potrzeba ",u"The media has too little free space. Needed: "));
        systools::PutSize(text,300,im.total+(uint64_t(64)<<20),Pl());Cat(text,300,u".");Fail(text);return false;}}
    if(!Reserve(cacheMemory,size_t(cacheBlock)*cacheBlocks) || !Reserve(filesMemory,sizeof(udf::File)*fileCapacity)
        || !Reserve(extentsMemory,sizeof(udf::Extent)*extentCapacity) || !Reserve(startedMemory,fileCapacity/8)) {release();Fail(T(u"Za mało pamięci RAM.",u"Not enough RAM."));return false;}
    im.cache=cacheMemory.data;memset(startedMemory.data,0,fileCapacity/8);j.started=startedMemory.data;
    j.map.files=reinterpret_cast<udf::File*>(filesMemory.data);j.map.fileCapacity=fileCapacity;j.map.fileCount=0;
    j.map.extents=reinterpret_cast<udf::Extent*>(extentsMemory.data);j.map.extentCapacity=extentCapacity;j.map.extentCount=0;j.map.overflow=false;
    Say(T(u"Czytam spis plików obrazu.",u"Reading the image's list of files."));
    udf::Source source{nullptr,ReadImage};
    if(!udf::Scan(source,j.map) || j.map.overflow) {
        release();
        if(im.lastResult!=https::Result::ok)net::HttpsFail(im.client,im.lastResult);
        else Fail(T(u"Charon nie potrafi odczytać tego obrazu ISO: nie ma w nim systemu plików UDF.",u"Charon cannot read this ISO image: it has no UDF file system."));
        return false;}
    j.wimFile=FindFile(j.map,u"sources\\install.wim");
    const uint32_t bootWim=FindFile(j.map,u"sources\\boot.wim");
    if(bootWim==UINT32_MAX || (j.wimFile==UINT32_MAX && FindFile(j.map,u"sources\\install.esd")==UINT32_MAX)) {
        release();Fail(T(u"Ten obraz nie jest instalatorem Windows: brak plików sources boot wim i install wim.",u"This image is not a Windows installer: the files sources boot wim and install wim are missing."));return false;}
    // Files of 4 GB and more cannot be written to FAT (except the image that is split).
    for(size_t i=0;i<j.map.fileCount;++i)if(!j.map.files[i].directory && uint32_t(i)!=j.wimFile && j.map.files[i].size>=0xffffffffull) {
        release();Fail(T(u"Obraz zawiera plik większy niż 4 gigabajty, którego nie da się zapisać na FAT.",u"The image contains a file larger than 4 gigabytes, which cannot be written to FAT."));return false;}
    // Charon's own loader stays; the Windows boot manager goes under its own name.
    j.loaderFile=FindFile(j.map,u"efi\\boot\\bootx64.efi");
    // r405: only a stick that already starts something (Charon's own stick) keeps its loader;
    // on any other stick the Windows boot manager must be \EFI\BOOT\BOOTX64.EFI, or the stick
    // starts nowhere else (found with a stick made by Charon and tried in VMware).
    bool hasLoader=false;
    {static FileRef ref;ref.volume=target;Set(ref.path,pathUnits,u"\\efi\\boot\\bootx64.efi");efi_file_protocol* file=nullptr;
     if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)==EFI_SUCCESS && file) {file->Close(file);hasLoader=true;}}
    if(j.loaderFile!=UINT32_MAX && !hasLoader)j.loaderFile=UINT32_MAX;   // written under its own name, like every other file
    else if(j.loaderFile!=UINT32_MAX && FindFile(j.map,u"efi\\microsoft\\boot\\bootmgfw.efi")==UINT32_MAX)j.redirectLoader=true;
    else if(j.loaderFile!=UINT32_MAX) {   // the image has both: only bootx64.efi must not replace an existing loader
        static FileRef ref;ref.volume=target;Set(ref.path,pathUnits,u"\\efi\\boot\\bootx64.efi");efi_file_protocol* file=nullptr;
        if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)==EFI_SUCCESS && file) {file->Close(file);
            // drop its extents: the bytes are skipped
            for(size_t i=0;i<j.map.extentCount;)if(j.map.extents[i].file==j.loaderFile) {j.map.extents[i]=j.map.extents[--j.map.extentCount];} else ++i;
        }
        j.loaderFile=UINT32_MAX;
    }
    // install.wim of 4 GB and more: plan the parts from its header and tables.
    if(j.wimFile!=UINT32_MAX && (j.map.files[j.wimFile].size>=0xffffffffull || splitMb<3800)) {
        static uint8_t head[wimsplit::kHeaderBytes];static wimsplit::Header header;static wimsplit::Part partList[partCapacity];
        Say(T(u"Plik z systemem jest większy niż 4 gigabajty, więc dzielę go na części, które zmieszczą się na FAT.",u"The system file is larger than 4 gigabytes, so I am splitting it into parts that fit on FAT."));
        bool ok=ReadFile(j.map,j.wimFile,0,head,sizeof(head)) && wimsplit::ParseHeader(head,header);
        const size_t tableBytes=ok?size_t(header.blobTable.size):0,entries=tableBytes/wimsplit::kEntryBytes,xmlBytes=ok?size_t(header.xml.size):0;
        ok=ok && tableBytes && xmlBytes && Reserve(tableMemory,tableBytes) && Reserve(resourceMemory,sizeof(wimsplit::Resource)*(entries+1))
            && Reserve(xmlMemory,xmlBytes) && Reserve(scratchMemory,size_t(1)<<20)
            && ReadFile(j.map,j.wimFile,header.blobTable.offset,tableMemory.data,tableBytes) && ReadFile(j.map,j.wimFile,header.xml.offset,xmlMemory.data,xmlBytes);
        xmlMemory.size=xmlBytes;
        ok=ok && wimsplit::MakePlan(header,tableMemory.data,tableBytes,uint64_t(splitMb)<<20,j.plan,reinterpret_cast<wimsplit::Resource*>(resourceMemory.data),entries+1,partList,partCapacity);
        if(!ok) {
            release();
            Fail(T(u"Nie udało się zaplanować podziału pliku z systemem; ten obraz nie nadaje się na nośnik FAT.",u"Splitting the system file could not be planned; this image is not suitable for FAT media."));return false;}
        j.split=true;wimsplit::Begin(j.writer,j.plan);
    }
    // Folders first, then the stream.
    for(size_t i=0;i<j.map.fileCount;++i)if(j.map.files[i].directory) {
        static char16_t path[pathUnits];Set(path,pathUnits,u"\\");Cat(path,pathUnits,j.map.files[i].path);
        if(MakeFolders(target,path)!=EFI_SUCCESS) {release();Fail(T(u"Nie udało się utworzyć folderów na nośniku.",u"The folders could not be created on the media."));return false;}
    }
    MakeFolders(target,u"\\efi\\microsoft\\boot");
    // Empty files exist in the list only.
    for(size_t i=0;i<j.map.fileCount;++i)if(!j.map.files[i].directory && !j.map.files[i].size) {
        static char16_t path[pathUnits];Set(path,pathUnits,u"\\");Cat(path,pathUnits,j.map.files[i].path);
        if(auto* file=OpenForWrite(target,path,true))file->Close(file);
    }
    udf::SortByOffset(j.map);udf::Begin(j.router,j.map);
    if(j.router.failed) {release();Fail(T(u"Obraz ISO ma układ, którego Charon nie potrafi zapisać na nośniku.",u"The ISO image has a layout Charon cannot write to the media."));return false;}
    {static char16_t text[400];Set(text,400,T(u"Pobieram Windows 11 i zapisuję na nośnik: ",u"Downloading Windows 11 and writing it to the media: "));systools::PutSize(text,400,im.total,Pl());
     Cat(text,400,T(u". To potrwa długo, zwykle ponad pół godziny. Escape przerywa.",u". This takes a long time, usually more than half an hour. Escape stops it."));Say(text);install::HearOut(120);}
    uint64_t offset=0;static https::Response answer;
    result=https::Download(im.client,im.url,offset,im.validator[0]?im.validator:nullptr,StreamSink,nullptr,answer);
    bool ok=result==https::Result::ok && !j.failed && !j.cancelled && j.done==im.total;
    if(ok && j.split) {
        // One GUID for the whole set.
        uint8_t guid[16];uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));uint64_t seed=(uint64_t(hi)<<32)|lo;
        for(auto& g:guid) {seed=seed*6364136223846793005ull+1442695040888963407ull;g=uint8_t(seed>>56);}
        ok=wimsplit::Finish(j.writer,xmlMemory.data,xmlMemory.size,guid,scratchMemory.data,size_t(1)<<20,PartSink,nullptr);
        if(!ok && !j.why)j.why=T(u"Nie udało się dokończyć zapisu części pliku z systemem.",u"The parts of the system file could not be completed.");
    }
    const bool cancelled=j.cancelled;const char16_t* why=j.why;const bool failed=j.failed;
    release();
    if(ok) {
        static const char16_t* const what[2]={u"Windows 11 na nośnik",u"Windows 11 to the media"};Progress(what[Pl()?0:1],im.total,im.total);
        Play(Icon::done);Say(T(u"Nośnik instalacyjny Windows 11 jest gotowy.",u"The Windows 11 installation media is ready."));install::HearOut(60);
        return true;
    }
    if(cancelled)Fail(T(u"Przerwano. Na nośniku zostały niepełne pliki instalatora; kolejna próba je zastąpi.",u"Stopped. Incomplete installer files remain on the media; the next attempt replaces them."));
    else if(failed || why)Fail(why?why:T(u"Zapis na nośnik nie powiódł się.",u"Writing to the media failed."));
    else net::HttpsFail(im.client,result);
    return false;
}
// Asks which USB volume to use (when there are several) and confirms.
inline bool ChooseAndMake(const char* locale) {
    // Microsoft's image needs 9 GB; an image named in the address file is measured later, in Make.
    uint64_t need=uint64_t(9)<<30;
    {static FileRef ref;ref.volume=U().host->selfDevice;Set(ref.path,pathUnits,u"\\EFI\\HAK\\windows-iso-url.txt");
     efi_file_protocol* file=nullptr;if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)==EFI_SUCCESS && file) {file->Close(file);need=uint64_t(64)<<20;}}
    static Target list[16];const size_t n=Targets(list,16,need);
    if(!n) {
        Fail(T(u"Nie ma pendrive'a z partycją FAT i co najmniej 9 gigabajtami wolnego miejsca. Podłącz taki pendrive, sformatowany jako FAT32, i wybierz instalację ponownie. Może to być pendrive, z którego uruchomiono Charona.",
            u"There is no USB stick with a FAT partition and at least 9 gigabytes free. Plug in such a USB stick, formatted as FAT32, and choose the installation again. It may be the USB stick Charon was started from."));
        return false;}
    size_t pick=0;
    if(n>1) {
        static char16_t names[16][120];static ask::Choice choices[16];
        for(size_t i=0;i<n;++i) {
            static char16_t label[64];VolumeLabel(list[i].volume,label,64);Set(names[i],120,label);Cat(names[i],120,T(u", wolne ",u", free "));systools::PutSize(names[i],120,list[i].freeBytes,Pl());
            choices[i]={names[i],names[i],""};
        }
        if(!ask::Choose(T(u"Na który pendrive?",u"To which USB stick?"),choices,n,pick))return false;
    }
    static char16_t label[64];static char16_t text[500];VolumeLabel(list[pick].volume,label,64);
    Set(text,500,T(u"Pobrać ",u"Download "));Cat(text,500,Product());Cat(text,500,T(u" i zapisać pliki instalatora na pendrive ",u" and write the installer's files to the USB stick "));Cat(text,500,label);
    Cat(text,500,T(u"? Nic nie zostanie sformatowane, inne pliki zostają; pliki instalatora Windows o tych samych nazwach zostaną zastąpione.",
        u"? Nothing will be formatted and other files are kept; Windows installer files with the same names will be replaced."));
    if(EspOnGpt(list[pick].volume)) {
        Fail(T(u"Partycja na tym pendrivie jest oznaczona jako partycja systemowa EFI. Windows takiej partycji nie pokazuje, więc instalator Windows nie znalazłby na niej swoich plików. Użyj pendrive'a ze zwykłą partycją FAT32, takiego jak po sformatowaniu w Windows albo w Linuksie.",
            u"The partition on this USB stick is marked as an EFI system partition. Windows does not show such a partition, so Windows Setup would not find its files on it. Use a USB stick with an ordinary FAT32 partition, as formatted by Windows or Linux."));
        return false;}
    if(!Confirm(text))return false;
    return Make(list[pick].volume,locale);
}
}  // namespace winusb
}  // namespace ws
}  // namespace hakefi
#endif
