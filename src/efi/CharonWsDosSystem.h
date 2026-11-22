#pragma once
// r403: Charon workspace - CharonDOS installed as a SEPARATE SYSTEM.
//
// CharonDOS gets its own FAT32 partition with its own boot entry "CharonDOS"
// (first in BootOrder: CharonDOS is the default system), the Charon boot
// manager ("Charon", second) and the Charon screen reader from power-on
// ("Charon voice", Driver####), as every install path of Charon leaves.
//   whole disk:  new GPT, one EFI system partition at 1 MiB (CharonDOS,
//                its programs, its Windows profiles); the rest of the disk
//                stays free, so Windows 11 can be installed beside it
//                (Windows Setup reuses that EFI partition and installs into
//                the free space; at its first logon Install-Charon.ps1
//                -FirstEntry CharonDOS puts CharonDOS first again).
//   free space:  a disk that already holds a system (e.g. Windows 11): a new
//                basic-data FAT32 partition in the largest gap; nothing else
//                on the disk is touched.
// CharonDOS's command WINNT starts the installed Windows; WIN starts a
// Windows profile (95 / 98 / ME / XP in the PC emulator CHARONPC.EFI) that
// lives in C:\PC\<NAME> (CHARONPC.CFG, written here; see src/pc/PLAN.txt).
// The systems themselves are never downloaded by Charon: the profile takes
// the user's own media (a file found on any volume, or an entry of the
// user's own list \EFI\HAK\pc-sources.txt).
// Included from CharonWsInstall3.h (HAK_CHARON_HTTPS). GPL-2.0-or-later.
namespace hakefi {
namespace ws {
namespace dossys {
using ask::Choice;using dosapps::CatAscii;using install::HearOut;
constexpr uint64_t mega=uint64_t(1)<<20;
typedef efi_status (*BlockWrite)(media::BlockIo*,uint32_t,uint64_t,size_t,void*) EFIAPI;
typedef efi_status (*BlockRead)(media::BlockIo*,uint32_t,uint64_t,size_t,void*) EFIAPI;
inline void P32(uint8_t* p,uint32_t v) {for(int i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
inline void P64(uint8_t* p,uint64_t v) {for(int i=0;i<8;++i)p[i]=uint8_t(v>>(8*i));}
inline void P16(uint8_t* p,uint32_t v) {p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
inline bool Write(media::BlockIo* io,uint64_t lba,const uint8_t* data,size_t bytes) {
    return reinterpret_cast<BlockWrite>(io->WriteBlocks)(io,io->Media->MediaId,lba,bytes,const_cast<uint8_t*>(data))==EFI_SUCCESS;
}
// One zero-filled megabyte for the long writes.
inline uint8_t* Zeros() {alignas(16) static uint8_t zeros[mega];return zeros;}
inline bool Zero(media::BlockIo* io,uint64_t lba,uint64_t blocks) {
    uint8_t* z=Zeros();for(size_t i=0;i<mega;++i)z[i]=0;
    while(blocks) {const uint64_t n=blocks>mega/512?mega/512:blocks;if(!Write(io,lba,z,size_t(n*512)))return false;lba+=n;blocks-=n;Pump();}
    return true;
}

// ---- a new, empty GPT over the whole disk (512-byte sectors) -----------------
// LBA 0 protective MBR, 1 header, 2..33 128 entries of 128 bytes; the backup
// entries at the end of the disk, the backup header in the last LBA.
inline bool EmptyGpt(media::BlockIo* io,uint64_t blocks,const seeddisk::Guid& diskGuid) {
    if(blocks<4096)return false;
    alignas(16) static uint8_t sector[512];
    // entries: all zero, written first (both copies), CRC of 16384 zero bytes
    if(!Zero(io,2,32) || !Zero(io,blocks-33,32))return false;
    uint8_t* z=Zeros();for(size_t i=0;i<16384;++i)z[i]=0;
    const uint32_t entriesCrc=seeddisk::Crc32(z,16384);
    for(int copy=1;copy>=0;--copy) {   // backup first, primary last
        for(auto& b:sector)b=0;
        static const char sig[]="EFI PART";for(int i=0;i<8;++i)sector[i]=uint8_t(sig[i]);
        P32(sector+8,0x00010000);P32(sector+12,92);
        const uint64_t my=copy?blocks-1:1,alt=copy?1:blocks-1;
        P64(sector+24,my);P64(sector+32,alt);P64(sector+40,34);P64(sector+48,blocks-34);
        for(int i=0;i<16;++i)sector[56+i]=diskGuid.bytes[i];
        P64(sector+72,copy?blocks-33:2);P32(sector+80,128);P32(sector+84,128);P32(sector+88,entriesCrc);
        P32(sector+16,seeddisk::Crc32(sector,92));
        if(!Write(io,my,sector,512))return false;
    }
    for(auto& b:sector)b=0;
    uint8_t* e=sector+446;e[0]=0;e[1]=0;e[2]=2;e[3]=0;e[4]=0xEE;e[5]=0xFF;e[6]=0xFF;e[7]=0xFF;
    P32(e+8,1);P32(e+12,blocks-1>0xFFFFFFFFull?0xFFFFFFFFu:uint32_t(blocks-1));
    sector[510]=0x55;sector[511]=0xAA;
    return Write(io,0,sector,512);
}

// ---- FAT32 written in place onto a partition (512-byte sectors) ---------------
inline bool FormatFat32(media::BlockIo* io,uint64_t first,uint64_t sectors,const char* label,uint32_t serial) {
    if(sectors<(uint64_t(66)<<11) || sectors>0xFFFFFFFFull)return false;   // FAT32 needs >= 65525 clusters
    uint32_t spc=sectors<=(uint64_t(16)<<21)?8:sectors<=(uint64_t(32)<<21)?16:32;   // 4K to 8 GB, 8K to 16 GB, 16K above
    while(spc>1 && (sectors-32)/spc<66600)spc/=2;   // r408a: a small partition needs smaller clusters to stay FAT32 (>= 65525 clusters after the FATs)
    const uint32_t reserved=32;
    const uint64_t clustersMax=(sectors-reserved)/spc;
    const uint32_t fatSectors=uint32_t(((clustersMax+2)*4+511)/512);
    const uint64_t data=reserved+2ull*fatSectors;
    if(data+spc>sectors)return false;
    const uint32_t clusters=uint32_t((sectors-data)/spc);
    if(clusters<65525)return false;   // r408a: would not be FAT32; nothing written
    alignas(16) static uint8_t boot[512],info[512];
    for(auto& b:boot)b=0;for(auto& b:info)b=0;
    boot[0]=0xEB;boot[1]=0x58;boot[2]=0x90;
    static const char oem[]="CHARON  ";for(int i=0;i<8;++i)boot[3+i]=uint8_t(oem[i]);
    P16(boot+11,512);boot[13]=uint8_t(spc);P16(boot+14,reserved);boot[16]=2;
    boot[21]=0xF8;P16(boot+24,63);P16(boot+26,255);P32(boot+28,uint32_t(first>0xFFFFFFFFull?0xFFFFFFFFu:first));
    P32(boot+32,uint32_t(sectors));P32(boot+36,fatSectors);P32(boot+44,2);P16(boot+48,1);P16(boot+50,6);
    boot[64]=0x80;boot[66]=0x29;P32(boot+67,serial);
    {bool end=false;for(int i=0;i<11;++i) {if(!label[i])end=true;boot[71+i]=end?uint8_t(' '):uint8_t(label[i]);}}
    static const char fs[]="FAT32   ";for(int i=0;i<8;++i)boot[82+i]=uint8_t(fs[i]);
    boot[510]=0x55;boot[511]=0xAA;
    P32(info,0x41615252);P32(info+484,0x61417272);P32(info+488,clusters-1);P32(info+492,3);info[510]=0x55;info[511]=0xAA;
    // reserved area, both FATs and the root cluster: zero first
    if(!Zero(io,first,data+spc))return false;
    alignas(16) static uint8_t fat[512];for(auto& b:fat)b=0;
    P32(fat,0x0FFFFFF8);P32(fat+4,0x0FFFFFFF);P32(fat+8,0x0FFFFFFF);   // media, reserved, root directory (end of chain)
    if(!Write(io,first+reserved,fat,512) || !Write(io,first+reserved+fatSectors,fat,512))return false;
    // the root directory starts with the volume label
    alignas(16) static uint8_t root[512];for(auto& b:root)b=0;
    {bool end=false;for(int i=0;i<11;++i) {if(!label[i])end=true;root[i]=end?uint8_t(' '):uint8_t(label[i]);}}
    root[11]=0x08;
    if(!Write(io,first+data,root,512))return false;
    // boot sector and FSInfo last (a half-written volume is not recognised)
    return Write(io,first+1,info,512) && Write(io,first+7,info,512) && Write(io,first+6,boot,512) && Write(io,first,boot,512);
}

// ---- load options (Boot#### / Driver####) for a file on a volume --------------
inline efi_runtime_services* Rt() {return U().host->system->RuntimeServices;}
inline efi_guid& GlobalVars() {static efi_guid g={0x8BE4DF61,0x93CA,0x11d2,{0xAA,0x0D,0x00,0xE0,0x98,0x03,0x2B,0x8C}};return g;}
inline void VarName(const char16_t* prefix,uint16_t id,char16_t* name) {
    static const char16_t hex[]=u"0123456789ABCDEF";size_t n=0;
    for(;prefix[n];++n)name[n]=prefix[n];
    for(int i=0;i<4;++i)name[n+i]=hex[(id>>(12-4*i))&15];
    name[n+4]=0;
}
inline bool SameDescription(const uint8_t* option,size_t size,const char16_t* description) {
    size_t i=0;for(;description[i];++i) {if(6+2*i+1>=size)return false;if((option[6+2*i]|(option[7+2*i]<<8))!=description[i])return false;}
    return 6+2*i+1<size && option[6+2*i]==0 && option[7+2*i]==0;
}
// Writes (or rewrites, same description) <kind>#### and puts it into
// <kind>Order: first, or right after the entry `after` when that is found.
// shortForm (r405): the path starts at the partition node HD(...), as efibootmgr
// writes it, so the entry survives the disk moving to another port or controller.
inline efi_status PutOption(bool driver,const char16_t* description,efi_handle volume,const char16_t* file,bool first,const char16_t* after,const char16_t* data=nullptr,bool shortForm=false) {
    size_t devBytes=0;const uint8_t* dev=DevicePathOf(volume,devBytes);
    if(!dev || !devBytes)return EFI_NOT_FOUND;
    if(shortForm) {
        size_t at=0;bool found=false;
        while(at+4<=devBytes) {
            const size_t length=size_t(dev[at+2])|(size_t(dev[at+3])<<8);
            if(length<4 || at+length>devBytes || dev[at]==0x7f)break;
            if(dev[at]==4 && dev[at+1]==1) {found=true;break;}
            at+=length;
        }
        if(!found)return EFI_UNSUPPORTED;
        dev+=at;devBytes-=at;
    }
    size_t descUnits=0;while(description[descUnits])++descUnits;
    size_t fileUnits=0;while(file[fileUnits])++fileUnits;
    const size_t fileNode=4+2*(fileUnits+1),pathBytes=devBytes+fileNode+4;
    size_t dataUnits=0;if(data)while(data[dataUnits])++dataUnits;
    static uint8_t option[2048];const size_t total=6+2*(descUnits+1)+pathBytes+(data?2*(dataUnits+1):0);
    if(total>sizeof(option))return EFI_BAD_BUFFER_SIZE;
    uint8_t* at=option;P32(at,driver?(1|2):1);at+=4;P16(at,uint32_t(pathBytes));at+=2;   // ACTIVE (+FORCE_RECONNECT for a driver)
    for(size_t i=0;i<=descUnits;++i) {P16(at,description[i]);at+=2;}
    memcpy(at,dev,devBytes);at+=devBytes;
    at[0]=4;at[1]=4;P16(at+2,uint32_t(fileNode));for(size_t i=0;i<=fileUnits;++i)P16(at+4+2*i,file[i]);at+=fileNode;
    at[0]=0x7f;at[1]=0xff;at[2]=4;at[3]=0;at+=4;
    if(data)for(size_t i=0;i<=dataUnits;++i) {P16(at,data[i]);at+=2;}   // optional data = the program's load options
    const char16_t* kind=driver?u"Driver":u"Boot";
    char16_t orderName[16];{size_t n=0;for(;kind[n];++n)orderName[n]=kind[n];const char16_t* o=u"Order";for(size_t k=0;o[k];++k)orderName[n++]=o[k];orderName[n]=0;}
    static uint16_t order[260];size_t size=sizeof(order)-4;
    efi_status status=Rt()->GetVariable(orderName,&GlobalVars(),nullptr,&size,order);
    if(status==EFI_NOT_FOUND)size=0;else if(status!=EFI_SUCCESS || size%2)return status==EFI_SUCCESS?EFI_INVALID_PARAMETER:status;
    size_t count=size/2;
    // an entry with the same description is rewritten in place (a second install)
    uint16_t id=0xffff;size_t afterAt=SIZE_MAX;
    static uint8_t probe[2048];
    for(size_t i=0;i<count;++i) {
        char16_t name[16];VarName(kind,order[i],name);size_t bytes=sizeof(probe);
        if(Rt()->GetVariable(name,&GlobalVars(),nullptr,&bytes,probe)!=EFI_SUCCESS)continue;
        if(SameDescription(probe,bytes,description))id=order[i];
        if(after && SameDescription(probe,bytes,after))afterAt=i;
    }
    if(id==0xffff)for(uint16_t c=0;c<0x1000;++c) {
        char16_t name[16];VarName(kind,c,name);size_t bytes=8;uint8_t small[8];
        if(Rt()->GetVariable(name,&GlobalVars(),nullptr,&bytes,small)==EFI_NOT_FOUND) {id=c;break;}
    }
    if(id==0xffff)return EFI_OUT_OF_RESOURCES;
    char16_t name[16];VarName(kind,id,name);
    status=Rt()->SetVariable(name,&GlobalVars(),7,total,option);
    if(status!=EFI_SUCCESS)return status;
    // new order: without id, then id inserted
    size_t kept=0;for(size_t i=0;i<count;++i)if(order[i]!=id)order[kept++]=order[i];
    if(afterAt!=SIZE_MAX) {afterAt=0;for(size_t i=0;i<kept;++i) {char16_t n2[16];VarName(kind,order[i],n2);size_t bytes=sizeof(probe);
        if(Rt()->GetVariable(n2,&GlobalVars(),nullptr,&bytes,probe)==EFI_SUCCESS && SameDescription(probe,bytes,after)) {afterAt=i+1;break;}}}
    const size_t place=first?0:afterAt!=SIZE_MAX?afterAt:kept;
    for(size_t i=kept;i>place;--i)order[i]=order[i-1];
    order[place]=id;++kept;
    return Rt()->SetVariable(orderName,&GlobalVars(),7,kept*2,order);
}

// ---- the profile: CHARONPC.CFG ------------------------------------------------
struct Profile {const char16_t* pl;const char16_t* en;const char* id;uint64_t bytes;bool nt;};
inline const Profile profiles[]={
    {u"bez profilu Windows, sam CharonDOS",u"no Windows profile, CharonDOS only",nullptr,0,false},
    {u"Windows 95",u"Windows 95","WIN95",uint64_t(1)<<30,false},
    {u"Windows 98",u"Windows 98","WIN98",uint64_t(2)<<30,false},
    {u"Windows ME",u"Windows ME","WINME",uint64_t(2)<<30,false},
    {u"Windows XP, z NVDA 2017.3",u"Windows XP, with NVDA 2017.3","WINXP",(uint64_t(4)<<30)-mega,true},
};
// Screen readers for the 9x profiles (src/pc/WIN9X-READERS.txt, catalogue of
// datajake.braillescreen.net checked 2026-11-20). Charon carries none of them:
// the chosen installer is downloaded into the profile (READER\) and checked by
// SHA-256. Only installers that run without an authorization are listed (the
// vendor demos and JAWS, which runs 40 minutes per Windows start without one);
// full CDs with authorization tools and cracked files are never offered.
// They speak through Charon as a VIRTUAL SYNTHESIZER: the emulated sound card
// (JAWS with Eloquence) or the emulated Braille 'n Speak on COM1, both voiced
// by Charon. All are English; outSPOKEN 2.01 (Polish interface) exists only
// inside a 370 MB archive (WebsiteScrapes/batol.7z) and is not offered until
// Charon can unpack 7z.
struct Reader {const char16_t* pl;const char16_t* en;const char* id;const char* url;const char* sha256;const char* file;const char* os;};
inline const Reader readers9x[]={
    {u"JAWS 3.31 dla Windows 95 i 98, po angielsku, mówi syntezatorem Eloquence przez kartę dźwiękową, bez autoryzacji działa 40 minut od każdego uruchomienia Windows",u"JAWS 3.31 for Windows 95 and 98, in English, speaks with the Eloquence synthesizer through the sound card, without authorisation it runs for 40 minutes after each Windows start",
     "jfw331","https://datajake.braillescreen.net/ScreenReaders/JAWS/OlderDownloads/jfw33135.exe","c99726264146598a8d680d238d094b2ab6c40989a099b63323dca2fdc0848615","JFW33135.EXE","95 98"},
    {u"JAWS 4.02 dla Windows ME, po angielsku, mówi syntezatorem Eloquence przez kartę dźwiękową, bez autoryzacji działa 40 minut; jego instalator zadaje pytania w oknach",u"JAWS 4.02 for Windows ME, in English, speaks with the Eloquence synthesizer through the sound card, without authorisation it runs for 40 minutes; its installer asks questions in dialogs",
     "jfw402","https://datajake.braillescreen.net/ScreenReaders/JAWS/OlderDownloads/j40252.exe","c3e049ffb2272d453f75fa602dd780db36c8c843009ccb4a67357f966054a9c1","J40252.EXE","ME 98"},
    {u"Window-Eyes 2.0, wersja demonstracyjna, po angielsku, działa 30 minut, mówi przez syntezator na porcie COM1",u"Window-Eyes 2.0 demo, in English, runs for 30 minutes, speaks through a synthesizer on port COM1",
     "we20demo","https://datajake.braillescreen.net/vintage-access/screen%20readers/GW%20Micro/WindowEyes%202.0%20Demo.zip","f3c2c93068e640b34ab5f8adec75409bf97211c7d93a330291a54e3a7cacd736","WE20DEMO.ZIP","95 98"},
    {u"Hal95 1.03, wersja demonstracyjna, po angielsku, mówi przez syntezator na porcie COM1",u"Hal95 1.03 demo, in English, speaks through a synthesizer on port COM1",
     "hal103demo","https://datajake.braillescreen.net/vintage-access/screen%20readers/Dolphin%20Computer%20Access/Hal95%20Demo%201.03.exe","5d42a3f756265bebe31e45314dfae6293171ef5942df3462e503dc38ced1471d","HAL103.EXE","95"},
    {u"bez czytnika ekranu w Windows: tylko czytnik ekranu Charona na ekranach tekstowych",u"no screen reader in Windows: Charon's screen reader on text screens only","none",nullptr,nullptr,nullptr,""},
};
// r405: the XP profile's reader. NVDA 2017.3 is the last NVDA for Windows XP
// (GPL-2.0, NV Access); hash computed 2026-11-20 from NV Access's own server
// (21 216 304 bytes, no official hash is published for that release).
inline const Reader readerXp={u"NVDA 2017.3",u"NVDA 2017.3","nvda-2017.3",
    "https://download.nvaccess.org/releases/2017.3/nvda_2017.3.exe","a0ad10a9f33bc5bcde629524e873c3141976f1698915d38640c46f2c140c3972","NVDA2017.EXE","XP"};
// Downloads the chosen reader into <profile>\READER\<file>, checked by SHA-256.
inline bool FetchReader(efi_handle volume,const char16_t* folder,const Reader& r) {
    if(!r.url)return true;
    if(!install::EnsureNetwork())return false;
    static char16_t url[400];{size_t n=0;for(;r.url[n] && n<399;++n)url[n]=char16_t(uint8_t(r.url[n]));url[n]=0;}
    static Memory data;data.data=nullptr;data.capacity=0;data.size=0;data.extra=0;data.reserved=false;
    static FileRef none;none.volume=nullptr;none.path[0]=0;uint64_t received=0;
    Say(T(u"Pobieram czytnik ekranu.",u"Downloading the screen reader."));
    if(!net::DownloadAny(url,none,&received,&data,T(u"czytnik ekranu",u"screen reader")))return false;
    Sha256 hash;hash.Update(data.data,data.size);uint8_t got[32];hash.Final(got);static const char hex[]="0123456789abcdef";
    bool same=true;for(int i=0;i<32 && same;++i)same=r.sha256[i*2]==hex[got[i]>>4] && r.sha256[i*2+1]==hex[got[i]&15];
    if(!same) {Free(data);Fail(T(u"Pobrany plik czytnika ekranu jest inny niż oczekiwany, więc go nie zapisano.",u"The downloaded screen reader file is not the expected one, so it was not saved."));return false;}
    static FileRef target;target.volume=volume;Set(target.path,pathUnits,folder);Cat(target.path,pathUnits,u"\\READER");MakeFolders(volume,target.path);
    Cat(target.path,pathUnits,u"\\");CatAscii(target.path,pathUnits,r.file);
    const efi_status st=WriteWhole(target,data.data,data.size);Free(data);
    if(st!=EFI_SUCCESS) {Fail(T(u"Nie udało się zapisać czytnika ekranu w profilu.",u"The screen reader could not be saved in the profile."),st);return false;}
    return true;
}
inline bool HasExtension(const char16_t* name,const char16_t* ext) {
    size_t n=0,e=0;while(name[n])++n;while(ext[e])++e;if(n<=e)return false;
    for(size_t i=0;i<e;++i) {char16_t a=name[n-e+i],b=ext[i];if(a>=u'A' && a<=u'Z')a=char16_t(a+32);if(a!=b)return false;}
    return true;
}
// Media found on the volumes: ISO (installer) or a ready disk image.
struct Found {efi_handle volume;char16_t path[260];char16_t label[300];bool disk;media::BlockIo* disc;};   // disc: a real disc in a drive (r406)
inline Found* FoundList() {static Found list[24];return list;}
inline void Scan(efi_handle volume,const char16_t* folder,unsigned depth,size_t& count) {
    if(count>=24 || depth>3)return;
    FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,folder);efi_file_protocol* dir=nullptr;
    if(OpenFile(ref,EFI_FILE_MODE_READ,0,&dir)!=EFI_SUCCESS || !dir)return;
    alignas(8) static uint8_t buffers[4][1100];uint8_t* buffer=buffers[depth];
    for(;count<24;) {
        size_t size=1100;if(dir->Read(dir,&size,buffer)!=EFI_SUCCESS || !size)break;
        const auto* info=reinterpret_cast<const efi_file_info*>(buffer);const char16_t* name=info->FileName;
        if(name[0]==u'.')continue;
        static char16_t child[pathUnits];Set(child,pathUnits,folder);if(folder[1])Cat(child,pathUnits,u"\\");Cat(child,pathUnits,name);
        if(info->Attribute&EFI_FILE_DIRECTORY) {
            if(SameFold(name,u"EFI") && depth==0)continue;   // boot files only
            static char16_t keep[4][pathUnits];Set(keep[depth],pathUnits,child);Scan(volume,keep[depth],depth+1,count);continue;}
        const bool iso=HasExtension(name,u".iso");
        const bool disk=HasExtension(name,u".vhd") || HasExtension(name,u".vmdk") || HasExtension(name,u".img");
        if(!iso && !disk)continue;
        Found& f=FoundList()[count++];f.volume=volume;Set(f.path,260,child);f.disk=disk;f.disc=nullptr;
        Set(f.label,300,disk?T(u"gotowy obraz dysku ",u"ready-made disk image "):T(u"obraz płyty instalacyjnej ",u"installation disc image "));Cat(f.label,300,child);
        Cat(f.label,300,u", ");systools::PutSize(f.label,300,info->FileSize,Pl());
    }
    dir->Close(dir);
}
// r406: the user's own disc in a CD or DVD drive. Firmware reads no ISO 9660 files, so the
// whole disc is taken: its blocks become SETUP.ISO in the profile. Charon's own disc is left out.
inline void ScanDiscs(size_t& count) {
    auto* boot=U().host->boot;efi_handle* handles=nullptr;size_t n=0;
    if(boot->LocateHandleBuffer(ByProtocol,&media::blockIoGuid,nullptr,&n,&handles)!=EFI_SUCCESS || !handles)return;
    for(size_t i=0;i<n && count<24;++i) {
        media::BlockIo* io=nullptr;
        if(boot->HandleProtocol(handles[i],&media::blockIoGuid,(void**)&io)!=EFI_SUCCESS || !io || !io->Media || io->Media->LogicalPartition
            || !io->Media->MediaPresent || io->Media->BlockSize!=2048)continue;
        alignas(8) static uint8_t sector[2048];
        if(io->ReadBlocks(io,io->Media->MediaId,16,sizeof(sector),sector)!=EFI_SUCCESS)continue;
        if(sector[1]!='C' || sector[2]!='D' || sector[3]!='0' || sector[4]!='0' || sector[5]!='1')continue;
        if(sector[40]=='C' && sector[41]=='H' && sector[42]=='A' && sector[43]=='R' && sector[44]=='O' && sector[45]=='N')continue;
        const uint64_t bytes=(io->Media->LastBlock+1)*2048;
        Found& f=FoundList()[count++];f.volume=nullptr;f.path[0]=0;f.disk=false;f.disc=io;
        char16_t name[33];size_t k=0;
        for(;k<32;++k)name[k]=sector[40+k]>=32 && sector[40+k]<127?char16_t(sector[40+k]):u' ';
        while(k && name[k-1]==u' ')--k;
        name[k]=0;
        Set(f.label,300,T(u"płyta w napędzie: ",u"disc in the drive: "));Cat(f.label,300,name[0]?name:T(u"bez nazwy",u"unnamed"));
        Cat(f.label,300,u", ");systools::PutSize(f.label,300,bytes,Pl());
    }
    boot->FreePool(handles);
}
// Reads the disc into a file of the profile. The progress is shown, sounded and spoken like every long task.
inline efi_status CopyDisc(media::BlockIo* io,const FileRef& to) {
    efi_file_protocol* out=nullptr;
    if(OpenFile(to,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&out)==EFI_SUCCESS) {
        if(out->Delete(out)!=EFI_SUCCESS)return EFI_WRITE_PROTECTED;
        out=nullptr;
    }
    auto status=OpenFile(to,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0,&out);
    if(status!=EFI_SUCCESS)return status;
    alignas(8) static uint8_t block[32*2048];
    const uint64_t blocks=io->Media->LastBlock+1,total=blocks*2048;const uint32_t id=io->Media->MediaId;
    const char16_t* what=T(u"Kopiuję płytę do profilu",u"Copying the disc into the profile");
    for(uint64_t at=0;at<blocks;) {
        const size_t count=blocks-at<32?size_t(blocks-at):32;size_t part=count*2048;
        status=io->ReadBlocks(io,id,at,part,block);
        if(status!=EFI_SUCCESS)break;
        const size_t asked=part;status=out->Write(out,&part,block);
        if(status!=EFI_SUCCESS)break;
        if(part!=asked) {status=EFI_VOLUME_FULL;break;}
        at+=count;Pump();
        if(at%1024==0 || at==blocks)Progress(what,at*2048,total);
    }
    if(status==EFI_SUCCESS && out->Flush)status=out->Flush(out);
    if(status!=EFI_SUCCESS) {out->Delete(out);return status;}
    out->Close(out);return EFI_SUCCESS;
}
inline void CfgLine(char* cfg,size_t cap,size_t& n,const char* key,const char* value) {
    for(const char* s=key;*s && n+1<cap;++s)cfg[n++]=*s;
    if(n+1<cap)cfg[n++]='=';
    for(const char* s=value;*s && n+1<cap;++s)cfg[n++]=*s;
    if(n+2<cap) {cfg[n++]='\r';cfg[n++]='\n';}
    cfg[n]=0;
}
inline void Ascii(const char16_t* in,char* out,size_t cap) {size_t i=0;for(;in[i] && i+1<cap;++i)out[i]=in[i]<128?char(in[i]):'_';out[i]=0;}
// Writes C:\PC\<ID>\CHARONPC.CFG on the new CharonDOS volume and copies the
// user's medium next to it. False: said why.
// r407: what Windows 98 / ME Setup would ask in its graphical wizard, asked by Charon instead.
// CharonPC turns it into Setup's own answer file (src/pc/pc_unattend.cpp).
struct SetupAnswers {bool automatic;char key[40];char name[64];char computer[20];};
inline SetupAnswers& Setup() {static SetupAnswers answers;return answers;}
inline bool AskSetup() {
    auto& a=Setup();a.automatic=false;a.key[0]=0;a.name[0]=0;a.computer[0]=0;
    Say(T(u"Instalator tej wersji Windows jest graficzny i nie działa w nim jeszcze żaden czytnik ekranu. Dlatego Charon zada jego pytania teraz i sam przeprowadzi instalację: przygotuje dysk, wpisze klucz i nazwę użytkownika, a przy pierwszym uruchomieniu Windows włączy instalator wybranego czytnika ekranu. Okna, które mimo to się pojawią, odczyta czytnik ekranu Charona: treść, przyciski i pola.",
          u"Setup in this version of Windows is graphical and no screen reader works in it yet. So Charon asks its questions now and carries out the installation by itself: it prepares the disk, enters the key and the user name, and at the first start of Windows it launches the installer of the chosen screen reader. Any windows that still appear are read by Charon's screen reader: their text, buttons and fields."));
    static char16_t wide[80];
    for(;;) {
        wide[0]=0;
        if(!Prompt(T(u"Klucz produktu Windows: 25 liter i cyfr z naklejki licencyjnej; myślniki można pominąć. Jeśli nic nie wpiszesz, klucz podasz w oknie instalatora.",
                     u"The Windows product key: 25 letters and digits from the licence sticker; hyphens may be left out. If you type nothing, you will give the key in the Setup window."),wide,60))return false;
        size_t n=0,k=0;
        for(size_t i=0;wide[i];++i) {
            char16_t c=wide[i];if(c>=u'a' && c<=u'z')c=char16_t(c-32);
            if((c>=u'A' && c<=u'Z') || (c>=u'0' && c<=u'9')) {if(k+1<sizeof a.key)a.key[k++]=char(c);++n;}
        }
        a.key[k]=0;
        if(n==0 || n==25)break;
        Fail(T(u"Klucz ma 25 liter i cyfr, a wpisano ich inną liczbę. Wpisz go jeszcze raz.",u"The key has 25 letters and digits, but a different number was typed. Type it again."));
    }
    Set(wide,80,T(u"Uzytkownik",u"User"));
    if(!Prompt(T(u"Imię albo nazwa użytkownika Windows, bez polskich liter:",u"Your name or Windows user name, without accented letters:"),wide,40))return false;
    Ascii(wide,a.name,sizeof a.name);
    Set(wide,80,u"CHARON");
    if(!Prompt(T(u"Nazwa komputera, do 15 liter i cyfr:",u"Computer name, up to 15 letters and digits:"),wide,16))return false;
    Ascii(wide,a.computer,sizeof a.computer);
    a.automatic=true;
    return true;
}
inline bool MakeProfile(efi_handle volume,const Profile& p,const char* reader,const Found* medium,const Reader* fetch) {
    static char16_t folder[pathUnits];Set(folder,pathUnits,u"\\EFI\\CharonDOS\\C\\PC\\");CatAscii(folder,pathUnits,p.id);
    if(MakeFolders(volume,folder)!=EFI_SUCCESS) {Fail(T(u"Nie udało się utworzyć folderu profilu.",u"The profile folder could not be created."));return false;}
    static char mediumName[64];mediumName[0]=0;
    if(medium) {
        static FileRef from,to;from.volume=medium->volume;Set(from.path,pathUnits,medium->path);
        const char* name=medium->disk?(HasExtension(medium->path,u".vhd")?"DISK.VHD":HasExtension(medium->path,u".vmdk")?"DISK.VMDK":"DISK.IMG"):"SETUP.ISO";
        size_t k=0;for(;name[k];++k)mediumName[k]=name[k];mediumName[k]=0;
        to.volume=volume;Set(to.path,pathUnits,folder);Cat(to.path,pathUnits,u"\\");CatAscii(to.path,pathUnits,name);
        Say(T(u"Kopiuję twój nośnik do profilu. To może potrwać kilka minut.",u"Copying your media into the profile. This may take a few minutes."));
        const efi_status st=medium->disc?CopyDisc(medium->disc,to):CopyFile(from,to);
        if(st!=EFI_SUCCESS) {Fail(medium->disc?T(u"Nie udało się skopiować płyty: nie da się jej odczytać albo ma ponad 4 gigabajty, a tyle nie mieści się w pliku na FAT32.",u"The disc could not be copied: it cannot be read, or it holds over 4 gigabytes, which does not fit in a file on FAT32.")
            :T(u"Nie udało się skopiować nośnika; plik większy niż 4 gigabajty nie mieści się na FAT32.",u"The media could not be copied; a file over 4 gigabytes does not fit on FAT32."),st);return false;}
    }
    static char cfg[2048];size_t n=0;cfg[0]=0;char number[24];
    CfgLine(cfg,sizeof cfg,n,"# CharonDOS Windows profile, written by Charon's installer (r403); format: src/pc/PLAN.txt","");
    CfgLine(cfg,sizeof cfg,n,"profile",p.id);
    CfgLine(cfg,sizeof cfg,n,"lang",Pl()?"pl":"en");
    CfgLine(cfg,sizeof cfg,n,"memory",p.nt?"512":"128");
    // JAWS speaks with Eloquence through the sound card; the others through the Braille 'n Speak on COM1
    CfgLine(cfg,sizeof cfg,n,"com1",(reader[0]=='j'&&reader[1]=='f'&&reader[2]=='w')?"none":"bns");
    CfgLine(cfg,sizeof cfg,n,"sound","sb16");
    CfgLine(cfg,sizeof cfg,n,"net","ne2000");
    CfgLine(cfg,sizeof cfg,n,"reader",reader);
    if(fetch && fetch->file) {static char file[80];file[0]=0;size_t k=0;const char* pre="READER\\";for(;*pre;++pre)file[k++]=*pre;for(const char* s=fetch->file;*s && k+1<sizeof file;++s)file[k++]=*s;file[k]=0;CfgLine(cfg,sizeof cfg,n,"reader_setup",file);}
    const bool readyDisk=medium && medium->disk;
    if(readyDisk) {CfgLine(cfg,sizeof cfg,n,"disk",mediumName);CfgLine(cfg,sizeof cfg,n,"disk_format",HasExtension(medium->path,u".vhd")?"vhd":HasExtension(medium->path,u".vmdk")?"vmdk":"raw");CfgLine(cfg,sizeof cfg,n,"boot","disk");CfgLine(cfg,sizeof cfg,n,"state","ready");}
    else {
        CfgLine(cfg,sizeof cfg,n,"disk","DISK.IMG");CfgLine(cfg,sizeof cfg,n,"disk_format","raw");
        {uint64_t mb=p.bytes/mega;size_t k=0;char rev[24];do {rev[k++]=char('0'+mb%10);mb/=10;}while(mb);for(size_t i=0;i<k;++i)number[i]=rev[k-1-i];number[k]=0;}
        CfgLine(cfg,sizeof cfg,n,"disk_create_mb",number);
        CfgLine(cfg,sizeof cfg,n,"cdrom",medium?mediumName:"");
        CfgLine(cfg,sizeof cfg,n,"boot",medium?"cdrom":"disk");
        CfgLine(cfg,sizeof cfg,n,"state",medium?"install":"no-media");
        if(medium && Setup().automatic) {
            CfgLine(cfg,sizeof cfg,n,"setup_auto","yes");
            if(Setup().key[0])CfgLine(cfg,sizeof cfg,n,"setup_key",Setup().key);
            CfgLine(cfg,sizeof cfg,n,"setup_name",Setup().name);
            CfgLine(cfg,sizeof cfg,n,"setup_computer",Setup().computer);
            if(Pl())CfgLine(cfg,sizeof cfg,n,"setup_zone","Central European");
        } else if(medium)CfgLine(cfg,sizeof cfg,n,"setup_auto","no");
    }
    if(fetch && !FetchReader(volume,folder,*fetch))Say(T(u"Profil zostanie utworzony bez czytnika ekranu; pobierzesz go później.",u"The profile is created without the screen reader; you can download it later."));
    static char16_t cfgPath[pathUnits];Set(cfgPath,pathUnits,folder);Cat(cfgPath,pathUnits,u"\\CHARONPC.CFG");
    if(!install::WriteText(volume,cfgPath,cfg,n)) {Fail(T(u"Nie udało się zapisać pliku profilu.",u"The profile file could not be written."));return false;}
    return true;
}

// ---- the dialogue -----------------------------------------------------------------
inline bool FindPackage(efi_handle& volume,const char16_t*& place) {
    static const char16_t* const places[]={u"\\EFI\\HAK\\packages\\CharonDOS",u"\\charon-packages\\CharonDOS"};
    efi_handle volumes[33];size_t count=0;
    if(hakefi::selfram::Active())volumes[count++]=hakefi::selfram::Handle();   // r408a: the copy in memory first - the disk that holds the package may be the one erased
    count+=Volumes(volumes+count,32);
    for(size_t i=0;i<count;++i)for(const auto* p:places) {
        FileRef ref;ref.volume=volumes[i];Set(ref.path,pathUnits,p);Cat(ref.path,pathUnits,u"\\CHARONDOS.EFI");efi_file_protocol* file=nullptr;
        if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS || !file)continue;
        file->Close(file);volume=volumes[i];place=p;return true;
    }
    return false;
}
inline void Run(bool dual=false) {
    efi_handle package=nullptr;const char16_t* packagePlace=nullptr;
    if(!FindPackage(package,packagePlace)) {Fail(T(u"Na żadnym dysku nie znalazłem pakietu CharonDOS; powinien być w folderze EFI, HAK, packages, CharonDOS.",u"I did not find the CharonDOS package on any disk; it should be in the folder EFI, HAK, packages, CharonDOS."));return;}
    // 1. the Windows profile
    static Choice profileChoices[sizeof(profiles)/sizeof(profiles[0])];
    for(size_t i=0;i<sizeof(profiles)/sizeof(profiles[0]);++i)profileChoices[i]={profiles[i].pl,profiles[i].en,profiles[i].id?profiles[i].id:"none"};
    size_t profile=0;
    if(!ask::Choose(T(u"Który profil Windows dodać do CharonDOS? Windows działa wtedy w emulatorze PC wewnątrz CharonDOS i uruchamiasz go poleceniem WIN.",
                      u"Which Windows profile should be added to CharonDOS? Windows then runs in the PC emulator inside CharonDOS, and you start it with the command WIN."),
                    profileChoices,sizeof(profiles)/sizeof(profiles[0]),profile))return;
    const Profile& p=profiles[profile];
    size_t programs=0;if(!dosapps::AskGroup(programs,true))return;   // the DOS programs, fetched after the files are in place
    const char* reader="none";const Found* medium=nullptr;const Reader* fetchReader=nullptr;
    if(p.id) {
        if(p.nt) {
            reader="nvda-2017.3";fetchReader=&readerXp;   // r405: downloaded into the profile like the 9x readers
            Say(T(u"Profil Windows XP dostaje NVDA 2017.3, ostatnią wersję NVDA dla Windows XP. Mówi po polsku.",
                  u"The Windows XP profile gets NVDA 2017.3, the last NVDA version for Windows XP. It speaks Polish."));
        } else {
            // only the readers made for this Windows (WIN95 -> "95"), "none" always
            static Choice readerChoices[sizeof(readers9x)/sizeof(readers9x[0])];static size_t readerMap[sizeof(readers9x)/sizeof(readers9x[0])];size_t rc=0;
            const char* version=p.id+3;
            for(size_t i=0;i<sizeof(readers9x)/sizeof(readers9x[0]);++i) {
                bool fits=!readers9x[i].url;
                for(const char* o=readers9x[i].os;!fits && o[0];++o)fits=o[0]==version[0] && o[1]==version[1];
                if(fits) {readerChoices[rc]={readers9x[i].pl,readers9x[i].en,readers9x[i].id};readerMap[rc++]=i;}
            }
            Say(T(u"Uwaga: czytniki ekranu dla Windows 95, 98 i ME są po angielsku. Polski outSPOKEN 2.01 jest dostępny tylko w dużym archiwum 7z, którego Charon jeszcze nie potrafi rozpakować, więc na razie nie ma go na liście.",
                  u"Note: the screen readers for Windows 95, 98 and ME are in English. The Polish outSPOKEN 2.01 is available only inside a large 7z archive, which Charon cannot unpack yet, so it is not in the list for now."));
            Say(T(u"Charon nie zawiera tych czytników ekranu: pobierze wybrany do profilu. Mówią one przez Charona, który jest dla nich wirtualnym syntezatorem.",u"Charon does not include these screen readers: it downloads the chosen one into the profile. They speak through Charon, which acts as a virtual synthesizer for them."));
            size_t r=0;if(!ask::Choose(T(u"Który czytnik ekranu zainstalować w tym Windows?",u"Which screen reader should be installed in this Windows?"),readerChoices,rc,r))return;
            reader=readers9x[readerMap[r]].id;fetchReader=&readers9x[readerMap[r]];
        }
        // the user's own medium: Charon never downloads these systems
        // A USB stick plugged in at this question: "search again" connects new devices first.
        Say(T(u"Charon nie pobiera tych systemów: potrzebny jest twój własny nośnik z licencją. Szukam płyt w napędach oraz plików ISO, VHD, VMDK i IMG na wszystkich dyskach i pendrive'ach.",
              u"Charon does not download these systems: you need your own licensed media. Looking for discs in the drives and for ISO, VHD, VMDK and IMG files on every disk and USB stick."));
        for(;;) {
            size_t count=0;efi_handle volumes[32];const size_t vn=Volumes(volumes,32);
            for(size_t i=0;i<vn;++i)Scan(volumes[i],u"\\",0,count);
            ScanDiscs(count);
            static Choice mediaChoices[26];size_t m=0;
            for(size_t i=0;i<count;++i)mediaChoices[m++]={FoundList()[i].label,FoundList()[i].label,"file"};
            mediaChoices[m++]={u"podłączyłem pendrive albo włożyłem płytę: szukaj ponownie",u"I have plugged in a USB stick or put in a disc: search again","again"};
            mediaChoices[m++]={u"nie mam teraz nośnika: profil poczeka na płytę albo obraz dysku",u"I have no media now: the profile will wait for a disc or a disk image","later"};
            if(!count)Say(T(u"Nie znalazłem żadnej płyty ani obrazu.",u"I found no disc and no image."));
            size_t pick=0;if(!ask::Choose(T(u"Z czego zainstalować Windows?",u"What should Windows be installed from?"),mediaChoices,m,pick))return;
            if(pick<count) {medium=&FoundList()[pick];break;}
            if(pick==count+1)break;
            net::ConnectEverything();
        }
    }
    // r407: Setup's own questions, for the 9x profiles installed from a disc
    Setup().automatic=false;
    if(p.id && !p.nt && medium && !medium->disk && !AskSetup())return;
    // 2. the disk
    auto& s=disks::D();disks::Load();
    static const Choice placeChoices[]={
        {u"cały dysk: CharonDOS na początku, reszta wolna, na przykład na Windows 11 obok",u"a whole disk: CharonDOS at the start, the rest left free, for example for Windows 11 alongside","whole"},
        {u"wolne miejsce na dysku, na którym już jest system",u"free space on a disk that already holds a system","free"},
    };
    size_t place=0;if(!dual && !ask::Choose(T(u"Gdzie zainstalować CharonDOS?",u"Where should CharonDOS be installed?"),placeChoices,2,place))return;
    const uint64_t need=(p.id?p.bytes+(uint64_t(1)<<30):(uint64_t(512)<<20))+(medium?uint64_t(1)<<30:0);
    const uint64_t partBytes=(need<(uint64_t(1)<<30)?(uint64_t(1)<<30):need+mega-1)/mega*mega;
    size_t chosen=0;
    if(place==0) {
        if(!images::ChooseDisk(partBytes+(uint64_t(64)<<20),false,chosen))return;
    } else {
        static char16_t labels[disks::maxDisks][300];static Choice list[disks::maxDisks];static size_t map[disks::maxDisks];size_t c=0;
        for(size_t i=0;i<s.count;++i) {const auto& d=s.list[i];
            if(d.readOnly || !d.gpt || d.blockSize!=512 || d.freeBlocks*512<partBytes+2*mega)continue;
            disks::DiskLabel(d,i,labels[c],300);list[c]={labels[c],labels[c],""};map[c]=i;++c;}
        if(!c) {static char16_t text[300];Set(text,300,T(u"Żaden dysk z tablicą GPT nie ma dość wolnego miejsca; potrzeba ",u"No disk with a GPT table has enough free space; needed: "));
            systools::PutSize(text,300,partBytes,Pl());Fail(text);return;}
        size_t k=0;if(!ask::Choose(T(u"Na którym dysku?",u"On which disk?"),list,c,k))return;
        chosen=map[k];
    }
    const disks::Disk& d=s.list[chosen];
    if(d.blockSize!=512) {Fail(T(u"Ten dysk ma sektory większe niż 512 bajtów; CharonDOS formatuje na razie tylko dyski z sektorami po 512 bajtów.",u"This disk has sectors larger than 512 bytes; for now CharonDOS can format only disks with 512-byte sectors."));return;}
    static char16_t what[300];Set(what,300,T(u"CharonDOS",u"CharonDOS"));
    if(p.id) {Cat(what,300,T(u" z profilem ",u" with the profile "));Cat(what,300,Pl()?p.pl:p.en);}
    Cat(what,300,T(u", partycja ",u", a partition of "));systools::PutSize(what,300,partBytes,Pl());
    if(place==0) {if(!images::ConfirmErase(d,chosen,what))return;}
    else {
        static char16_t text[700];Set(text,700,T(u"W wolnym miejscu powstanie nowa partycja: ",u"A new partition will be created in the free space: "));Cat(text,700,what);
        Cat(text,700,T(u". Pozostałe partycje zostają bez zmian. CharonDOS będzie pierwszy w kolejności rozruchu. Aby zacząć, wpisz słowo TAK i naciśnij Enter.",u". The other partitions are kept. CharonDOS will be first in the boot order. To start, type the word YES and press Enter."));Say(text);
        static char16_t word[16];word[0]=0;
        if(!Prompt(T(u"Wpisz TAK, aby zacząć:",u"Type YES to start:"),word,15,nullptr,nullptr,false) || !SameFold(word,T(u"tak",u"yes"))) {Say(T(u"Nic nie zostało zmienione.",u"Nothing was changed."));return;}
    }
    // 3. partition + FAT32
    seeddisk::Disk sd{d.io,d.blockSize,d.blocks,images::SeedRead,images::SeedWrite};
    seeddisk::Guid unique;images::RandomGuid(unique,77);
    uint64_t firstLba=2048;
    if(place==0) {
        seeddisk::Guid diskGuid;images::RandomGuid(diskGuid,78);
        Say(T(u"Tworzę nową tablicę partycji.",u"Creating a new partition table."));
        if(!EmptyGpt(d.io,d.blocks,diskGuid)) {Fail(T(u"Nie udało się zapisać tablicy partycji.",u"The partition table could not be written."));return;}
    } else {
        firstLba=(d.freeFirst*512+mega-1)/mega*mega/512;
    }
    seeddisk::AddRequest request{};
    static const seeddisk::Guid espGuid={{0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b}};
    request.type=place==0?espGuid:seeddisk::kTypeBasicData;request.unique=unique;request.name=u"CharonDOS";
    request.sizeBytes=partBytes;request.alignmentBytes=0;request.placement=seeddisk::Placement::At;request.firstLba=firstLba;request.attributes=0;
    seeddisk::AddResult added{};
    const auto error=seeddisk::AddPartition(sd,images::SeedWork(),seeddisk::kWorkBytes,request,added);
    if(error!=seeddisk::Error::None) {Fail(T(u"Nie udało się dodać partycji CharonDOS.",u"The CharonDOS partition could not be added."));return;}
    Say(T(u"Formatuję partycję CharonDOS jako FAT32.",u"Formatting the CharonDOS partition as FAT32."));
    uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
    if(!FormatFat32(d.io,added.firstLba,added.lbaCount,"CHARONDOS",lo^hi)) {Fail(T(u"Formatowanie nie powiodło się.",u"Formatting failed."));return;}
    if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
    images::Reconnect(d.handle);
    efi_handle volume=images::VolumeOfPartition(unique.bytes);
    if(!volume) {Fail(T(u"Firmware nie widzi nowej partycji CharonDOS.",u"The firmware does not see the new CharonDOS partition."));return;}
    // 4. files: CharonDOS, Charon, the profile
    Say(T(u"Kopiuję CharonDOS.",u"Copying CharonDOS."));
    static FileRef from,to;from.volume=package;Set(from.path,pathUnits,packagePlace);to.volume=volume;Set(to.path,pathUnits,u"\\EFI\\CharonDOS");size_t copied=0;
    if(CopyTree(from,to,0,&copied)!=EFI_SUCCESS) {Fail(T(u"Nie udało się skopiować CharonDOS.",u"CharonDOS could not be copied."));return;}
    MakeFolders(volume,u"\\EFI\\CharonDOS\\C");
    if(!images::CharonOnto(volume)) {Fail(T(u"Nie udało się skopiować Charona na partycję CharonDOS.",u"Charon could not be copied onto the CharonDOS partition."));return;}
    if(p.id && !MakeProfile(volume,p,reader,medium,fetchReader))return;
    {size_t n=0;const Choice* g=dosapps::Groups(n);
     if(programs<n && !ask::Same(g[programs].value,"none")) {if(install::EnsureNetwork())dosapps::Fetch(volume,u"\\EFI\\CharonDOS",programs);else Say(T(u"Nie ma sieci: programy pobierzesz później.",u"No network: you can download the programs later."));}}
    // a fallback loader for firmware without boot entries: the Charon boot manager (it shows CharonDOS and every system)
    if(place==0) {static FileRef a,b;a.volume=volume;Set(a.path,pathUnits,u"\\EFI\\HAK\\HAKBOOT.EFI");b.volume=volume;Set(b.path,pathUnits,u"\\EFI\\BOOT\\BOOTX64.EFI");
        MakeFolders(volume,u"\\EFI\\BOOT");CopyFile(a,b);}
    // 5. firmware entries: CharonDOS first, Charon after it, the reader from power-on
    efi_status st=PutOption(false,u"CharonDOS",volume,u"\\EFI\\CharonDOS\\CHARONDOS.EFI",true,nullptr,Pl()?u"pl":nullptr);
    if(st!=EFI_SUCCESS) {Fail(T(u"Nie udało się zapisać wpisu rozruchowego CharonDOS.",u"The CharonDOS boot entry could not be written."),st);return;}
    st=PutOption(false,u"Charon",volume,u"\\EFI\\HAK\\HAKBOOT.EFI",false,u"CharonDOS");
    if(st!=EFI_SUCCESS)Fail(T(u"Nie udało się zapisać wpisu rozruchowego menedżera rozruchu Charon.",u"The boot entry for the Charon boot manager could not be written."),st);
    st=PutOption(true,u"Charon voice",volume,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr);
    if(st!=EFI_SUCCESS)Fail(T(u"Nie udało się ustawić czytnika ekranu tak, by mówił od włączenia komputera.",u"The screen reader could not be set up to speak from power-on."),st);
    {static char16_t text[700];Set(text,700,T(u"CharonDOS jest zainstalowany i będzie się uruchamiał jako pierwszy. ",u"CharonDOS is installed and will start first. "));
     if(p.id)Cat(text,700,T(u"Profil Windows uruchamiasz w nim poleceniem WIN; pierwsze uruchomienie zaczyna instalację z twojego nośnika. ",u"In it the command WIN starts the Windows profile; the first start begins the installation from your media. "));
     Cat(text,700,T(u"Polecenie WINNT uruchamia Windows zainstalowany obok.",u"The command WINNT starts the Windows installed alongside."));Say(text);HearOut(300);}
    // 6. Windows 11 beside it
    if(place==0) {
        static const Choice next[]={{u"tak, zainstaluj teraz Windows 11 obok CharonDOS",u"yes, install Windows 11 alongside CharonDOS now","yes"},{u"nie, na razie wystarczy CharonDOS",u"no, CharonDOS is enough for now","no"}};
        size_t answer=dual?0:1;
        if(dual || (ask::Choose(T(u"Zainstalować teraz Windows 11 na wolnym miejscu tego dysku? CharonDOS zostanie systemem domyślnym, a Windows uruchomisz poleceniem WINNT.",
                         u"Install Windows 11 in the free space on this disk now? CharonDOS stays the default system, and you start Windows with the command WINNT."),next,2,answer) && answer==0)) {
            ask::Put("dosdefault","yes");
            Say(T(u"Przy pytaniu, co zrobić z dyskiem, wybierz wolne miejsce, a potem ten sam dysk.",u"When asked what should be done with the disk, choose free space, and then this same disk."));HearOut(200);
            install::Classic(2);
        }
    }
}
}  // namespace dossys
}  // namespace ws
}  // namespace hakefi
