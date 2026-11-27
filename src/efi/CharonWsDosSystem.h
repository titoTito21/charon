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
inline Found& DataDisk() {static Found data;return data;}   // r410: the second disk image of the detailed mode (pc_disk2)
inline Found& KeptMedium() {static Found kept;return kept;}  // r410: the chosen medium, kept while the volumes are scanned again
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
// r409: a line that does not fit is not written cut (it was, silently): CfgOverflow() says so and MakeProfile refuses.
inline bool& CfgOverflow() {static bool overflow=false;return overflow;}
inline void CfgLine(char* cfg,size_t cap,size_t& n,const char* key,const char* value) {
    size_t need=3;for(const char* s=key;*s;++s)++need;for(const char* s=value;*s;++s)++need;   // key = value CR LF
    if(n+need+1>cap) {CfgOverflow()=true;return;}
    for(const char* s=key;*s;++s)cfg[n++]=*s;
    if(key[0]!='#')cfg[n++]='=';   // r410 (s6a): a comment line has no "="
    for(const char* s=value;*s;++s)cfg[n++]=*s;
    cfg[n++]='\r';cfg[n++]='\n';
    cfg[n]=0;
}
inline void Ascii(const char16_t* in,char* out,size_t cap) {size_t i=0;for(;in[i] && i+1<cap;++i)out[i]=in[i]<128?char(in[i]):'_';out[i]=0;}
// Writes C:\PC\<ID>\CHARONPC.CFG on the new CharonDOS volume and copies the
// user's medium next to it. False: said why.
// r407: what Windows 98 / ME Setup would ask in its graphical wizard, asked by Charon instead.
// CharonPC turns it into Setup's own answer file (src/pc/pc_unattend.cpp).
// r410: the CharonPC rows of the detailed mode (specification 5.13) ride along: an empty field is "as before",
// so the express mode (and every golden file) writes the same CHARONPC.CFG as r409. Keys as src/pc/pc_config.cpp reads them.
struct SetupAnswers {bool automatic;char key[40];char name[64];char computer[20];
    char org[64];char memory[8];char cpu[20];char diskMb[8];char sound[8];char com1[8];char net[8];bool readerLead;bool log;char disk2[16];char disk2Format[8];};
inline void ClearPc(SetupAnswers& a) {a.org[0]=0;a.memory[0]=0;a.cpu[0]=0;a.diskMb[0]=0;a.sound[0]=0;a.com1[0]=0;a.net[0]=0;a.readerLead=false;a.log=false;a.disk2[0]=0;a.disk2Format[0]=0;}
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
// r409: the boot floppy of the Windows 95 profile. Windows 95 installation discs do not start by
// themselves (src/pc/PLAN.txt: "Win95 CDs are not bootable: boot floppy"); Setup is started from the
// boot floppy that came with the system. The dialogue never asked for it, so a Windows 95 profile
// made from a disc had nothing to start from. The image is the USER'S OWN, as the disc is: a file
// found on the volumes (1.44 MB, .img .ima .vfd .flp), or an address the user types. Charon holds
// no address of any Microsoft boot disk and downloads none on its own.
constexpr uint64_t floppyBytes=1474560;   // 1.44 MB: the only size CharonPC's floppy drive takes (pc_config.cpp "floppya: 1_44=")
inline constexpr char bootFloppyFile[]="BOOT95.IMG";   // the image's name in the profile folder
struct BootFloppy {const Found* file;char16_t url[400];};   // file: found on a volume; url: typed by the user; neither: none
inline BootFloppy& Floppy() {static BootFloppy floppy;return floppy;}
inline bool NeedsBootFloppy(const Profile& p) {return p.id && p.id[0]=='W' && p.id[1]=='I' && p.id[2]=='N' && p.id[3]=='9' && p.id[4]=='5' && !p.id[5];}
inline Found* FloppyList() {static Found list[16];return list;}
inline void ScanFloppies(efi_handle volume,const char16_t* folder,unsigned depth,size_t& count) {
    if(count>=16 || depth>3)return;
    FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,folder);efi_file_protocol* dir=nullptr;
    if(OpenFile(ref,EFI_FILE_MODE_READ,0,&dir)!=EFI_SUCCESS || !dir)return;
    alignas(8) static uint8_t buffers[4][1100];uint8_t* buffer=buffers[depth];
    for(;count<16;) {
        size_t size=1100;if(dir->Read(dir,&size,buffer)!=EFI_SUCCESS || !size)break;
        const auto* info=reinterpret_cast<const efi_file_info*>(buffer);const char16_t* name=info->FileName;
        if(name[0]==u'.')continue;
        static char16_t child[pathUnits];Set(child,pathUnits,folder);if(folder[1])Cat(child,pathUnits,u"\\");Cat(child,pathUnits,name);
        if(info->Attribute&EFI_FILE_DIRECTORY) {
            if(SameFold(name,u"EFI") && depth==0)continue;   // boot files only
            static char16_t keep[4][pathUnits];Set(keep[depth],pathUnits,child);ScanFloppies(volume,keep[depth],depth+1,count);continue;}
        if(info->FileSize!=floppyBytes)continue;
        if(!HasExtension(name,u".img") && !HasExtension(name,u".ima") && !HasExtension(name,u".vfd") && !HasExtension(name,u".flp"))continue;
        Found& f=FloppyList()[count++];f.volume=volume;Set(f.path,260,child);f.disk=false;f.disc=nullptr;
        Set(f.label,300,T(u"obraz dyskietki ",u"floppy image "));Cat(f.label,300,child);
    }
    dir->Close(dir);
}
// Asked for the Windows 95 profile installed from a disc. False: the user left (Escape).
inline bool AskBootFloppy() {
    auto& chosen=Floppy();chosen.file=nullptr;chosen.url[0]=0;
    Say(T(u"Płyty instalacyjne Windows 95 zwykle nie uruchamiają się same: instalator startuje z dyskietki startowej, która była w zestawie z systemem. Potrzebny jest obraz takiej dyskietki, 1,44 megabajta, z twojego własnego zestawu. Charon go nie zawiera i sam go nie pobiera. Szukam plików IMG, IMA, VFD i FLP tej wielkości na wszystkich dyskach i pendrive'ach.",
          u"Windows 95 installation discs usually do not start by themselves: Setup starts from the boot floppy that came with the system. An image of that floppy is needed, 1.44 megabytes, from your own set. Charon does not include one and does not download one by itself. Looking for IMG, IMA, VFD and FLP files of that size on every disk and USB stick."));
    for(;;) {
        size_t count=0;efi_handle volumes[32];const size_t vn=Volumes(volumes,32);
        for(size_t i=0;i<vn;++i)ScanFloppies(volumes[i],u"\\",0,count);
        static Choice choices[19];size_t m=0;
        for(size_t i=0;i<count;++i)choices[m++]={FloppyList()[i].label,FloppyList()[i].label,"file"};
        choices[m++]={u"wpiszę adres, z którego Charon pobierze mój obraz dyskietki",u"I will type an address from which Charon downloads my floppy image","url"};
        choices[m++]={u"podłączyłem pendrive z obrazem dyskietki: szukaj ponownie",u"I have plugged in a USB stick with the floppy image: search again","again"};
        choices[m++]={u"nie mam obrazu dyskietki: profil wystartuje z płyty, jeśli jest ona rozruchowa",u"I have no floppy image: the profile will start from the disc, if the disc is bootable","none"};
        if(!count)Say(T(u"Nie znalazłem żadnego obrazu dyskietki.",u"I found no floppy image."));
        size_t pick=0;
        if(!ask::Choose(T(u"Z której dyskietki startowej uruchomić instalator Windows 95?",u"Which boot floppy should start Windows 95 Setup?"),choices,m,pick))return false;
        if(pick<count) {chosen.file=&FloppyList()[pick];break;}
        if(pick==count) {
            chosen.url[0]=0;
            if(!Prompt(T(u"Adres obrazu dyskietki, http albo https:",u"Address of the floppy image, http or https:"),chosen.url,399))continue;
            bool good=chosen.url[0]==u'h' && chosen.url[1]==u't' && chosen.url[2]==u't' && chosen.url[3]==u'p';
            for(size_t i=0;chosen.url[i];++i)if(chosen.url[i]<33 || chosen.url[i]>126)good=false;
            if(!good) {chosen.url[0]=0;Fail(T(u"To nie jest adres http ani https albo zawiera niedozwolone znaki.",u"This is not an http or https address, or it contains characters that are not allowed."));continue;}
            break;
        }
        if(pick==count+2) {
            Say(T(u"Bez dyskietki startowej profil Windows 95 uruchomi się tylko z płyty rozruchowej. Obraz dyskietki możesz dodać później: skopiuj go do folderu profilu jako BOOT95.IMG i dopisz w pliku CHARONPC.CFG wiersze floppy=BOOT95.IMG oraz boot=floppy,disk.",
                  u"Without a boot floppy the Windows 95 profile starts only from a bootable disc. You can add the floppy image later: copy it into the profile folder as BOOT95.IMG and add the lines floppy=BOOT95.IMG and boot=floppy,disk to the file CHARONPC.CFG."));
            HearOut(200);break;
        }
        net::ConnectEverything();
    }
    if(chosen.file || chosen.url[0]) {
        Say(T(u"Instalator Windows 95 wystartuje z tej dyskietki. Taka instalacja nie jest automatyczna: przechodzisz przez nią samodzielnie, a Charon nie wpisze za ciebie klucza ani nazw.",
              u"Windows 95 Setup will start from this floppy. Such an installation is not automatic: you go through it yourself, and Charon does not enter the key or the names for you."));
        HearOut(200);
    }
    return true;
}
// Puts the chosen boot floppy into the profile folder as BOOT95.IMG. False: said why; the profile is made without it.
inline bool PlaceBootFloppy(efi_handle volume,const char16_t* folder) {
    auto& chosen=Floppy();
    static FileRef to;to.volume=volume;Set(to.path,pathUnits,folder);Cat(to.path,pathUnits,u"\\");CatAscii(to.path,pathUnits,bootFloppyFile);
    if(chosen.file) {
        static FileRef from;from.volume=chosen.file->volume;Set(from.path,pathUnits,chosen.file->path);
        const efi_status st=CopyFile(from,to);
        if(st!=EFI_SUCCESS) {Fail(T(u"Nie udało się skopiować obrazu dyskietki do profilu.",u"The floppy image could not be copied into the profile."),st);return false;}
        return true;
    }
    if(!install::EnsureNetwork())return false;
    if(!install::Fetch(chosen.url,volume,to.path,T(u"obraz dyskietki startowej",u"the boot floppy image")))return false;
    efi_file_protocol* file=nullptr;uint64_t bytes=0;
    if(OpenFile(to,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&file)==EFI_SUCCESS && file) {
        const auto* info=InfoOf(file);bytes=info?info->FileSize:0;
        if(bytes==floppyBytes) {file->Close(file);return true;}
        file->Delete(file);
    }
    Fail(T(u"Pobrany plik nie jest obrazem dyskietki 1,44 megabajta, więc go nie zapisałem.",u"The downloaded file is not a 1.44 megabyte floppy image, so I did not keep it."));
    return false;
}
// r409: the text of CHARONPC.CFG, apart from MakeProfile's file work, so that the host test
// (tests/charon-install-answers-host.py) can compare it with golden files. readerFile: the reader's
// installer in READER\ ("" none); mediumName: the medium's name in the profile (null: no medium);
// readyFormat: the format of a ready-made disk image (null: the medium is an installation disc);
// floppyName: the user's boot floppy image in the profile ("" none; asked for Windows 95, AskBootFloppy).
constexpr size_t profileTextBytes=8192;   // r409: was 2048; static in MakeProfile, not on the stack
inline void ProfileText(char* cfg,size_t cap,size_t& n,const Profile& p,const char* reader,const char* readerFile,const char* mediumName,const char* readyFormat,const char* floppyName) {
    char number[24];const bool medium=mediumName!=nullptr;
    CfgLine(cfg,cap,n,"# CharonDOS Windows profile, written by Charon's installer (r403); format: src/pc/PLAN.txt","");
    CfgLine(cfg,cap,n,"profile",p.id);
    // r410 (user's answer 2026-11-25): the profile's language follows the installer's CharonDOS language answer
    // (dos_lang of the detailed mode: English / Polish), not Charon's interface language; "charon" or no answer = Charon's.
    const bool dosLangAsked=ask::Is("dos_mode","detailed");   // an express run never reads a stale answer
    CfgLine(cfg,cap,n,"lang",dosLangAsked && ask::Is("dos_lang","en")?"en":dosLangAsked && ask::Is("dos_lang","pl")?"pl":Pl()?"pl":"en");
    const SetupAnswers& pc=Setup();   // r410: the detailed mode's CharonPC answers; empty = as before
    CfgLine(cfg,cap,n,"memory",pc.memory[0]?pc.memory:p.nt?"512":"128");
    // JAWS speaks with Eloquence through the sound card; the others through the Braille 'n Speak on COM1
    CfgLine(cfg,cap,n,"com1",pc.com1[0]?pc.com1:(reader[0]=='j'&&reader[1]=='f'&&reader[2]=='w')?"none":"bns");
    CfgLine(cfg,cap,n,"sound",pc.sound[0]?pc.sound:"sb16");
    CfgLine(cfg,cap,n,"net",pc.net[0]?pc.net:"ne2000");
    CfgLine(cfg,cap,n,"reader",reader);
    if(pc.cpu[0])CfgLine(cfg,cap,n,"cpu",pc.cpu);
    if(pc.readerLead)CfgLine(cfg,cap,n,"reader_lead","yes");
    if(pc.log)CfgLine(cfg,cap,n,"log","CHARONPC.LOG");
    if(pc.disk2[0]) {CfgLine(cfg,cap,n,"disk2",pc.disk2);CfgLine(cfg,cap,n,"disk2_format",pc.disk2Format[0]?pc.disk2Format:"raw");}
    if(readerFile[0]) {static char file[80];file[0]=0;size_t k=0;const char* pre="READER\\";for(;*pre;++pre)file[k++]=*pre;for(const char* s=readerFile;*s && k+1<sizeof file;++s)file[k++]=*s;file[k]=0;CfgLine(cfg,cap,n,"reader_setup",file);}
    if(medium && readyFormat) {CfgLine(cfg,cap,n,"disk",mediumName);CfgLine(cfg,cap,n,"disk_format",readyFormat);CfgLine(cfg,cap,n,"boot","disk");CfgLine(cfg,cap,n,"state","ready");}
    else {
        CfgLine(cfg,cap,n,"disk","DISK.IMG");CfgLine(cfg,cap,n,"disk_format","raw");
        {uint64_t mb=p.bytes/mega;if(pc.diskMb[0]) {mb=0;for(const char* s=pc.diskMb;*s>='0' && *s<='9';++s)mb=mb*10+uint64_t(*s-'0');}
         size_t k=0;char rev[24];do {rev[k++]=char('0'+mb%10);mb/=10;}while(mb);for(size_t i=0;i<k;++i)number[i]=rev[k-1-i];number[k]=0;}
        CfgLine(cfg,cap,n,"disk_create_mb",number);
        CfgLine(cfg,cap,n,"cdrom",medium?mediumName:"");
        // r409: the user's boot floppy (src/pc/PLAN.txt: "Win95 CDs are not bootable: boot floppy", floppy= and
        // boot=floppy,disk; read by src/pc/pc_config.cpp). With a floppy CharonPC makes no SETUP.IMG of its own
        // (pc_config.cpp prepares the unattended Setup only when no floppy is given), so no setup_ lines are written.
        const bool floppy=medium && floppyName[0];
        if(floppy)CfgLine(cfg,cap,n,"floppy",floppyName);
        CfgLine(cfg,cap,n,"boot",floppy?"floppy,disk":medium?"cdrom":"disk");
        CfgLine(cfg,cap,n,"state",medium?"install":"no-media");
        if(floppy)CfgLine(cfg,cap,n,"setup_auto","no");
        else if(medium && Setup().automatic) {
            CfgLine(cfg,cap,n,"setup_auto","yes");
            if(Setup().key[0])CfgLine(cfg,cap,n,"setup_key",Setup().key);
            CfgLine(cfg,cap,n,"setup_name",Setup().name);
            CfgLine(cfg,cap,n,"setup_computer",Setup().computer);
            if(pc.org[0])CfgLine(cfg,cap,n,"setup_org",pc.org);   // r410: [NameAndOrg] Org= of Setup's answer file (src/pc/pc_unattend.cpp)
            if(Pl())CfgLine(cfg,cap,n,"setup_zone","Central European");
        } else if(medium)CfgLine(cfg,cap,n,"setup_auto","no");
    }
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
    // r409: the user's boot floppy (Windows 95 from a disc), next to the medium
    bool floppy=false;
    if(medium && !medium->disk && NeedsBootFloppy(p) && (Floppy().file || Floppy().url[0])) {
        floppy=PlaceBootFloppy(volume,folder);
        if(!floppy)Say(T(u"Profil zostanie utworzony bez dyskietki startowej. Dodasz ją później: skopiuj obraz do folderu profilu jako BOOT95.IMG i dopisz w pliku CHARONPC.CFG wiersze floppy=BOOT95.IMG oraz boot=floppy,disk.",
                         u"The profile is created without the boot floppy. You can add it later: copy the image into the profile folder as BOOT95.IMG and add the lines floppy=BOOT95.IMG and boot=floppy,disk to the file CHARONPC.CFG."));
    }
    // r410: the second disk of the detailed mode (pc_disk2): the user's own image, copied next to the profile
    if(Setup().disk2[0]) {
        static FileRef from,to;from.volume=DataDisk().volume;Set(from.path,pathUnits,DataDisk().path);
        to.volume=volume;Set(to.path,pathUnits,folder);Cat(to.path,pathUnits,u"\\");CatAscii(to.path,pathUnits,Setup().disk2);
        Say(T(u"Kopiuję drugi obraz dysku do profilu.",u"Copying the second disk image into the profile."));
        const efi_status st=CopyFile(from,to);
        if(st!=EFI_SUCCESS) {Fail(T(u"Nie udało się skopiować drugiego obrazu dysku; profil powstanie bez niego. Plik większy niż 4 gigabajty nie mieści się na FAT32.",
                                    u"The second disk image could not be copied; the profile is made without it. A file over 4 gigabytes does not fit on FAT32."),st);
            Setup().disk2[0]=0;Setup().disk2Format[0]=0;}
    }
    static char cfg[profileTextBytes];size_t n=0;cfg[0]=0;CfgOverflow()=false;
    ProfileText(cfg,sizeof cfg,n,p,reader,(fetch && fetch->file)?fetch->file:"",medium?mediumName:nullptr,
        (medium && medium->disk)?(HasExtension(medium->path,u".vhd")?"vhd":HasExtension(medium->path,u".vmdk")?"vmdk":"raw"):nullptr,floppy?bootFloppyFile:"");
    if(CfgOverflow()) {Fail(T(u"Ustawienia profilu nie mieszczą się w pliku CHARONPC.CFG, więc profil nie został utworzony.",u"The profile's settings do not fit in the file CHARONPC.CFG, so the profile was not created."));return false;}
    if(fetch && !FetchReader(volume,folder,*fetch))Say(T(u"Profil zostanie utworzony bez czytnika ekranu; pobierzesz go później.",u"The profile is created without the screen reader; you can download it later."));
    static char16_t cfgPath[pathUnits];Set(cfgPath,pathUnits,folder);Cat(cfgPath,pathUnits,u"\\CHARONPC.CFG");
    if(!install::WriteText(volume,cfgPath,cfg,n)) {Fail(T(u"Nie udało się zapisać pliku profilu.",u"The profile file could not be written."));return false;}
    return true;
}

// ---- r410: the detailed mode (specification 5.13) -----------------------------------------------
// Asked only after "detailed mode"; the express mode keeps the dialogue of r409 key for key. Every question has a
// default (the express answer), help, F2 (the default said) and Delete (back to it) through ask::AskOne, and
// Escape goes back one question. What reads each answer in the installed CharonDOS:
//   dos_first, dos_lang       the boot entries and the load option "pl" (src/dos/backend_uefi.cpp)
//   dos_sets, dos_set_<g>     dosapps::FetchChosen: only the checked programs are downloaded
//   dos_autoexec, dos_prompt  C:\AUTOEXEC.BAT, run at every start (src/dos/shell.cpp dos_main); PATH, ECHO, DATE,
//                             PROMPT, CD and IF EXIST are CharonDOS built-ins (shell.cpp builtin, cmd_if)
//   dos_speech, dos_rate ...  Charon's settings record on the CharonDOS partition (\EFI\HAK\charon-settings-a/-b.bin:
//                             readerRate, readerPitch, readerVolume, punctuation = what CHARON RATE, PITCH, VOLUME and
//                             PUNCT change, src/dos/CHARON-COMMANDS.txt); global to the Charon on that partition
//   dos_size                  the size of the partition
//   pc_*                      CHARONPC.CFG keys as src/pc/pc_config.cpp reads them (SetupAnswers above)
//   dos_win11_boot            "dosdefault" (CharonWsInstall2.h: Install-Charon.ps1 -FirstEntry CharonDOS)
// Not offered: dos_cp and dos_kbd (new emulator work), pc_zone (Windows 98 zone names not taken from the media),
// pc_speed (a wrong value cuts the sound), "the system that is first now" (dos_first) and "Charon's menu"
// (dos_win11_boot): neither has a writer here.
namespace dosq {
using ask::Kind;using ask::Question;
struct PcContext {bool profile,nt,jaws,comReader,ready;};
inline PcContext& Pc() {static PcContext context;return context;}
inline bool HasProfile() {return Pc().profile;}
inline bool Is9x() {return Pc().profile && !Pc().nt;}
inline bool MakesDisk() {return Pc().profile && !Pc().ready;}
inline bool JawsReader() {return Pc().jaws;}
inline bool AutoSetup() {return Is9x() && Setup().automatic;}
template<size_t G> inline bool SetOpen() {return ask::Has("dos_sets",dosapps::SetGroup(G));}
inline bool Commands() {return ask::Has("dos_autoexec","commands");}
inline bool OwnPrompt() {return ask::Is("dos_prompt","own");}
inline bool SetSpeech() {return ask::Is("dos_speech","set");}
inline bool Detailed() {return ask::Is("dos_mode","detailed");}
inline const Choice firsts[]={{u"CharonDOS",u"CharonDOS","dos"},{u"menu Charona, a CharonDOS jest na jego liście",u"Charon's menu, with CharonDOS in its list","charon"},{u"to, co uruchamia się pierwsze teraz; CharonDOS i Charon na końcu kolejności",u"what starts first now; CharonDOS and Charon at the end of the boot order","keep"}};
inline const Choice languages[]={{u"taki jak język Charona",u"the same as Charon's language","charon"},{u"angielski",u"English","en"},{u"polski",u"Polish","pl"}};
inline Question start[]={
    {"dos_first",false,Kind::choice,u"Co uruchamia się pierwsze?",u"What starts first?","dos",firsts,3,
     u"Pierwszy wpis rozruchowy po włączeniu komputera. Charon zawsze dostaje też swój wpis, a jego menu pokazuje CharonDOS i każdy inny system.",
     u"The first boot entry after power-on. Charon always gets its own entry as well, and its menu shows CharonDOS and every other system."},
    {"dos_lang",false,Kind::choice,u"Język CharonDOS:",u"Language of CharonDOS:","charon",languages,3,
     u"Język komunikatów CharonDOS i poleceń CHARON, WINNT i LINUX. Czytnik ekranu Charona mówi swoim językiem.",
     u"The language of CharonDOS's messages and of the commands CHARON, WINNT and LINUX. Charon's screen reader speaks in its own language."},
};
inline Question programs[]={
    {"dos_sets",false,Kind::multi,u"Zestawy programów:",u"Program sets:","edit,files,view,tools,archive,devel,games",nullptr,0,
     u"Spacja zaznacza zestaw. Dla każdego zaznaczonego zestawu padnie pytanie o jego programy. Bez zaznaczenia nic nie zostanie pobrane.",
     u"Space checks a set. For each checked set you are asked which of its programs to take. With nothing checked, nothing is downloaded."},
    {"dos_set_edit",false,Kind::multi,u"Edytory: które programy?",u"Editors: which programs?",nullptr,nullptr,0,nullptr,nullptr,nullptr,0,SetOpen<0>},
    {"dos_set_files",false,Kind::multi,u"Pliki i foldery: które programy?",u"Files and folders: which programs?",nullptr,nullptr,0,nullptr,nullptr,nullptr,0,SetOpen<1>},
    {"dos_set_view",false,Kind::multi,u"Przeglądanie tekstu i pomoc: które programy?",u"Text viewing and help: which programs?",nullptr,nullptr,0,
     u"Pomoc FreeDOS potrzebuje czytnika książek AMB; Charon pobierze go razem z nią.",u"The FreeDOS help needs the AMB book reader; Charon downloads it with the help.",nullptr,0,SetOpen<2>},
    {"dos_set_tools",false,Kind::multi,u"Narzędzia: które programy?",u"Tools: which programs?",nullptr,nullptr,0,nullptr,nullptr,nullptr,0,SetOpen<3>},
    {"dos_set_archive",false,Kind::multi,u"Archiwa i pakowanie: które programy?",u"Archives and compression: which programs?",nullptr,nullptr,0,nullptr,nullptr,nullptr,0,SetOpen<4>},
    {"dos_set_devel",false,Kind::multi,u"Programowanie: które programy?",u"Programming: which programs?",nullptr,nullptr,0,nullptr,nullptr,nullptr,0,SetOpen<5>},
    {"dos_set_games",false,Kind::multi,u"Gry: które programy?",u"Games: which programs?",nullptr,nullptr,0,nullptr,nullptr,nullptr,0,SetOpen<6>},
};
inline const Choice cpus[]={{u"według profilu: Pentium II dla Windows 95, Pentium III dla pozostałych",u"by profile: Pentium II for Windows 95, Pentium III for the others","auto"},
    {u"Intel 486 DX4",u"Intel 486 DX4","i486dx4"},{u"Pentium",u"Pentium","pentium"},{u"Pentium MMX",u"Pentium MMX","pentium_mmx"},{u"Pentium II, Klamath",u"Pentium II, Klamath","p2_klamath"},
    {u"Pentium III, Katmai",u"Pentium III, Katmai","p3_katmai"},{u"Pentium 4, Willamette",u"Pentium 4, Willamette","p4_willamette"},{u"AMD K6-2, Chomper",u"AMD K6-2, Chomper","amd_k6_2_chomper"},
    {u"AMD Athlon XP",u"AMD Athlon XP","athlon_xp"}};
inline const Choice sounds[]={{u"Sound Blaster 16",u"Sound Blaster 16","sb16"},{u"bez karty dźwiękowej",u"no sound card","none"}};
inline const Choice synths[]={{u"automatycznie według czytnika: JAWS przez kartę dźwiękową, pozostałe przez syntezator Braille 'n Speak na COM1",u"automatic by reader: JAWS through the sound card, the others through the Braille 'n Speak synthesizer on COM1","auto"},
    {u"syntezator Braille 'n Speak na porcie COM1",u"the Braille 'n Speak synthesizer on port COM1","bns"},{u"bez syntezatora na COM1",u"no synthesizer on COM1","none"}};
inline const Choice nets[]={{u"NE2000",u"NE2000","ne2000"},{u"bez karty sieciowej",u"no network card","none"}};
inline char* Defaults(size_t i) {static char texts[8][16];return texts[i];}   // defaults that depend on the profile and on Charon's voice
inline Question pc[]={
    {"pc_memory",false,Kind::number,u"Pamięć profilu Windows w megabajtach:",u"Memory of the Windows profile in megabytes:",nullptr,nullptr,0,
     u"Od 16 do 2048. Windows 95, 98 i ME mogą nie startować z więcej niż 512 megabajtami.",u"From 16 to 2048. Windows 95, 98 and ME may fail to start with more than 512 megabytes.",nullptr,0,HasProfile,0,16,2048},
    {"pc_cpu",false,Kind::choice,u"Emulowany procesor:",u"Emulated processor:","auto",cpus,9,
     u"Windows 95 zawodzi na szybkich procesorach, dlatego profil daje mu Pentium II.",u"Windows 95 fails on fast processors, which is why the profile gives it a Pentium II.",nullptr,0,HasProfile},
    {"pc_disk",false,Kind::number,u"Rozmiar dysku Windows w megabajtach:",u"Size of the Windows disk in megabytes:",nullptr,nullptr,0,
     u"Od 512 do 4095: tyle mieści jeden plik na FAT32. Partycja CharonDOS rośnie razem z nim.",u"From 512 to 4095: as much as one file on FAT32 holds. The CharonDOS partition grows with it.",nullptr,0,MakesDisk,0,512,4095},
    {"pc_sound",false,Kind::choice,u"Karta dźwiękowa:",u"Sound card:","sb16",sounds,2,
     u"JAWS mówi przez kartę dźwiękową: bez niej JAWS milczy.",u"JAWS speaks through the sound card: without it JAWS is silent.",nullptr,0,HasProfile},
    {"pc_synth",false,Kind::choice,u"Jak mówi czytnik ekranu w Windows (wirtualny syntezator):",u"How the Windows screen reader speaks (virtual synthesizer):","auto",synths,3,
     u"Window-Eyes i Hal mówią przez syntezator na COM1. Przy JAWS z kartą dźwiękową CharonPC i tak wyłącza COM1, bo JAWS 4.02 zatrzymuje wtedy Windows.",
     u"Window-Eyes and Hal speak through the synthesizer on COM1. With JAWS and the sound card CharonPC turns COM1 off anyway, because JAWS 4.02 then stops Windows.",nullptr,0,Is9x},
    {"pc_net",false,Kind::choice,u"Karta sieciowa:",u"Network card:","ne2000",nets,2,
     u"Na razie bez skutku: CharonPC nie ma jeszcze karty sieciowej, odpowiedź jest tylko zapisywana w profilu.",u"No effect yet: CharonPC has no network card so far; the answer is only stored in the profile.",nullptr,0,HasProfile},
    {"pc_org",false,Kind::text,u"Nazwa organizacji dla instalatora Windows, bez polskich liter:",u"Organisation name for Windows Setup, without accented letters:","",nullptr,0,
     u"Pusta: instalator wpisze tam nazwę użytkownika.",u"Empty: Setup puts the user name there.",nullptr,0,AutoSetup},
    {"pc_reader_lead",false,Kind::yesNo,u"Czy Charon ma odpowiadać na okna startowe JAWS, na przykład Run as Demo, przy każdym starcie Windows?",u"Should Charon answer JAWS's start windows, such as Run as Demo, at every start of Windows?","no",nullptr,0,
     u"Bez tego Charon prowadzi tylko instalację JAWS.",u"Without it Charon leads only the installation of JAWS.",nullptr,0,JawsReader},
    {"pc_log",false,Kind::yesNo,u"Zapisywać dziennik emulatora, do zgłoszeń błędów?",u"Write an emulator log, for fault reports?","no",nullptr,0,
     u"Plik CHARONPC.LOG w folderze profilu.",u"The file CHARONPC.LOG in the profile folder.",nullptr,0,HasProfile},
};
inline const Choice autoexecs[]={{u"dodaj folder każdego programu do PATH: programy uruchomisz z każdego folderu",u"add every program folder to PATH: programs start from any folder","path"},
    {u"przywitaj się",u"say a greeting","greet"},{u"pokaż datę",u"show the date","date"},
    {u"uruchom menedżer plików Doszip, jeśli jest wśród programów",u"start the file manager Doszip, if it is among the programs","doszip"},{u"wykonaj moje polecenia",u"run my commands","commands"}};
inline const Choice prompts[]={{u"folder i znak większości, na przykład C:\\APPS>",u"folder and >, for example C:\\APPS>","$P$G"},{u"litera dysku i znak większości: C>",u"drive letter and >: C>","$N$G"},{u"wpiszę własny",u"I will type my own","own"}};
inline const Choice speeches[]={{u"jak w Charonie",u"as in Charon","charon"},{u"ustaw teraz tempo, wysokość, głośność i interpunkcję",u"set rate, pitch, volume and punctuation now","set"}};
inline const Choice puncts[]={{u"brak",u"none","none"},{u"trochę",u"some","some"},{u"większość",u"most","most"},{u"wszystko",u"all","all"}};
inline Question startup[]={
    {"dos_autoexec",false,Kind::multi,u"Plik startowy:",u"Start-up file:","",autoexecs,5,
     u"Charon zapisze plik C:\\AUTOEXEC.BAT, który CharonDOS wykonuje przy każdym starcie. Nic nie zaznaczone: bez pliku, jak dotąd.",
     u"Charon writes the file C:\\AUTOEXEC.BAT, which CharonDOS runs at every start. Nothing checked: no file, as before."},
    {"dos_autoexec_cmd",false,Kind::text,u"Twoje polecenia, kilka oddzielaj pionową kreską:",u"Your commands, separate several with a vertical bar:","",nullptr,0,
     u"Bez polskich liter. Na przykład: VER | CHARON",u"Without accented letters. For example: VER | CHARON",nullptr,0,Commands},
    {"dos_prompt",false,Kind::choice,u"Znak zachęty:",u"Prompt:","$P$G",prompts,3,
     u"Inny niż domyślny trafia do pliku startowego jako polecenie PROMPT.",u"Anything but the default goes into the start-up file as the command PROMPT."},
    {"dos_prompt_text",false,Kind::text,u"Twój znak zachęty:",u"Your prompt:","$P$G",nullptr,0,
     u"Kody: dolar P folder, dolar N dysk, dolar G znak większości, dolar D data, dolar T godzina, dolar podkreślenie nowy wiersz. Bez polskich liter i bez znaku procentu.",
     u"Codes: dollar P folder, dollar N drive, dollar G greater-than sign, dollar D date, dollar T time, dollar underscore new line. Without accented letters and without the percent sign.",nullptr,0,OwnPrompt},
    {"dos_speech",false,Kind::choice,u"Mowa w CharonDOS:",u"Speech in CharonDOS:","charon",speeches,2,
     u"W CharonDOS mówi czytnik ekranu Charona. Te ustawienia są wspólne dla całego Charona na partycji CharonDOS: jego menu, czytnika od włączenia komputera i CharonDOS. Charon na tym nośniku się nie zmienia. Później zmienisz je poleceniami CHARON RATE, PITCH, VOLUME i PUNCT.",
     u"In CharonDOS, Charon's screen reader speaks. These settings are shared by the whole Charon on the CharonDOS partition: its menu, the reader from power-on and CharonDOS. The Charon on this medium does not change. Later you change them with the commands CHARON RATE, PITCH, VOLUME and PUNCT."},
    {"dos_rate",false,Kind::number,u"Tempo mowy w procentach:",u"Speech rate in percent:",nullptr,nullptr,0,u"Od 0 do 100; 50 to tempo zwykłe.",u"From 0 to 100; 50 is the normal rate.",nullptr,0,SetSpeech,0,0,100},
    {"dos_pitch",false,Kind::number,u"Wysokość głosu w procentach:",u"Voice pitch in percent:",nullptr,nullptr,0,u"Od 0 do 100; 50 to wysokość zwykła.",u"From 0 to 100; 50 is the normal pitch.",nullptr,0,SetSpeech,0,0,100},
    {"dos_volume",false,Kind::number,u"Głośność mowy w procentach:",u"Speech volume in percent:",nullptr,nullptr,0,u"Od 10 do 100, co 10.",u"From 10 to 100, in steps of 10.",nullptr,0,SetSpeech,0,10,100},
    {"dos_punct",false,Kind::choice,u"Interpunkcja:",u"Punctuation:",nullptr,puncts,4,nullptr,nullptr,nullptr,0,SetSpeech},
};
inline Question sizeRow[]={{"dos_size",false,Kind::number,u"Rozmiar partycji CharonDOS w megabajtach:",u"Size of the CharonDOS partition in megabytes:",nullptr,nullptr,0,
    u"Domyślnie najmniejszy, który pomieści CharonDOS, jego programy i profil. Większy zostawia mniej miejsca na inne systemy.",
    u"By default the smallest that holds CharonDOS, its programs and the profile. A larger one leaves less room for other systems."}};
inline const Choice win11s[]={{u"CharonDOS; Windows uruchamiasz w nim poleceniem WINNT",u"CharonDOS; you start Windows in it with the command WINNT","dos"},{u"Menedżer rozruchu Windows; CharonDOS w menu rozruchowym komputera",u"Windows Boot Manager; CharonDOS in the computer's boot menu","wbm"},{u"menu Charona; z niego CharonDOS albo Windows",u"Charon's menu; from it CharonDOS or Windows","charon"}};
inline Question win11Row[]={{"dos_win11_boot",false,Kind::choice,u"Gdy obok jest Windows 11: co uruchamia się pierwsze?",u"With Windows 11 alongside: which starts first?","dos",win11s,3,
    u"Ustawia to skrypt Charona przy pierwszym logowaniu do Windows.",u"Charon's script sets this at the first sign-in to Windows."}};
template<size_t N> constexpr size_t Count(const Question (&)[N]) {return N;}
inline void Number(char* out,uint64_t v) {char rev[24];size_t k=0;do {rev[k++]=char('0'+v%10);v/=10;}while(v);for(size_t i=0;i<k;++i)out[i]=rev[k-1-i];out[k]=0;}
inline uint64_t Value(const char* key) {uint64_t v=0;for(const char* s=ask::Get(key);*s>='0' && *s<='9';++s)v=v*10+uint64_t(*s-'0');return v;}
// Every row back to its default (a second install in the same session starts from the defaults again).
inline void Reset(Question* rows,size_t n) {for(size_t i=0;i<n;++i)ask::Put(rows[i].key,rows[i].fallback?rows[i].fallback:"");}
// The rate and pitch scale of Charon's menu and of CHARON RATE (CharSettings.h VoicePercent; charon_cmd.cpp).
inline uint8_t TenthsOf(uint64_t p) {if(p>=100)return 20;if(p<=50)return uint8_t(5+(p+5)/10);const uint64_t t=10+(p-50+2)/5;return uint8_t(t>20?20:t);}
inline uint8_t LevelOf(uint64_t p) {uint64_t v=(p+5)/10;if(v<1)v=1;if(v>10)v=10;return uint8_t(v);}
inline void PrepareRows(const Profile& p) {
    dosapps::MakeSetLists();
    programs[0].choices=dosapps::SetChoices();programs[0].count=dosapps::setCount;
    for(size_t g=0;g<dosapps::setCount;++g) {const auto& l=dosapps::SetLists()[g];programs[1+g].choices=l.choices;programs[1+g].count=l.count;programs[1+g].fallback=l.all;}
    Number(Defaults(0),p.nt?512:128);pc[0].fallback=Defaults(0);
    Number(Defaults(1),p.bytes/mega);pc[2].fallback=Defaults(1);
    pc[3].risk=Pc().jaws?"none":nullptr;      // JAWS without the sound card is silent
    pc[4].risk=Pc().comReader?"none":nullptr; // Window-Eyes or Hal without COM1 is silent
    CharSettings now;if(LoadCharSettings(U().host->root,now)!=EFI_SUCCESS)now=CharSettings{};
    Number(Defaults(2),VoicePercent(now.readerRate));startup[5].fallback=Defaults(2);
    Number(Defaults(3),VoicePercent(now.readerPitch));startup[6].fallback=Defaults(3);
    Number(Defaults(4),uint64_t(now.readerVolume)*10);startup[7].fallback=Defaults(4);
    startup[8].fallback=puncts[now.punctuation&3].value;
}
// Typed fields go into CHARONPC.CFG and AUTOEXEC.BAT, which CharonDOS reads as plain bytes: ASCII only.
inline bool AsciiOnly(const char* key,bool noPercent) {for(const char* s=ask::Get(key);*s;++s)if(uint8_t(*s)>126 || (noPercent && *s=='%'))return false;return true;}
// Asks a table. False: Escape at its first question.
// The list of questions is made again after every answer (as detailed::OpenGroup does): an answer opens or closes a
// later question (a program set, "run my commands", "type your own prompt", "set the speech now"). ask::Run made the
// list once at the start, so in the first machine run the questions of an unchecked set were asked and the typed
// commands were never asked.
inline bool Ask(Question* rows,size_t n) {
    for(size_t i=0;i<n;++i)if(!ask::Get(rows[i].key)[0] && rows[i].fallback)ask::Put(rows[i].key,rows[i].fallback);
    if(!ask::Complete())return false;
    for(size_t at=0;;) {
        size_t count=0,index=n;
        for(size_t i=0;i<n;++i)if(ask::Applies(rows[i])) {if(count==at)index=i;++count;}
        if(index>=n)return true;
        const Question& q=rows[index];
        const int r=ask::AskOne(q,true);
        if(r==2)continue;
        if(r==0) {if(!at)return false;--at;Say(T(u"Wracam do poprzedniego pytania.",u"Back to the previous question."));continue;}
        const bool prompt=ask::Same(q.key,"dos_prompt_text");
        if(q.kind==Kind::text && !AsciiOnly(q.key,prompt)) {
            Fail(prompt?T(u"Tylko litery bez polskich znaków, cyfry i znaki ASCII, bez znaku procentu.",u"Only letters without accents, digits and ASCII signs, without the percent sign.")
                       :T(u"Tylko litery bez polskich znaków, cyfry i znaki ASCII.",u"Only letters without accents, digits and ASCII signs."));
            continue;   // the same question again
        }
        ++at;
    }
}
// "Answers that differ from the defaults: N. <question> <value>. ..." (said before the typed confirmation).
inline void Summary(char16_t* out,size_t cap) {
    struct Table {Question* rows;size_t n;};
    const Table tables[]={{start,Count(start)},{programs,Count(programs)},{pc,Count(pc)},{startup,Count(startup)},{sizeRow,1}};
    size_t changed=0;static char16_t body[3600];body[0]=0;
    for(const auto& t:tables)for(size_t i=0;i<t.n;++i) {
        const Question& q=t.rows[i];if(!ask::Applies(q) || !detailed::Changed(q))continue;
        ++changed;Cat(body,3600,Pl()?q.pl:q.en);Cat(body,3600,u" ");
        static char16_t value[1200];detailed::ValueText(q,value,1200);Cat(body,3600,value);Cat(body,3600,u". ");
    }
    if(Setup().disk2[0]) {++changed;Cat(body,3600,T(u"Drugi dysk profilu: ",u"Second disk of the profile: "));Cat(body,3600,DataDisk().label);Cat(body,3600,u". ");}
    if(!changed) {Set(out,cap,T(u"Wszystkie odpowiedzi szczegółowe mają wartości domyślne.",u"Every detailed answer has its default."));return;}
    Set(out,cap,T(u"Odpowiedzi szczegółowe różne od domyślnych: ",u"Detailed answers that differ from the defaults: "));CatN(out,cap,changed);Cat(out,cap,u". ");Cat(out,cap,body);
}
// The answers into the fields ProfileText writes (only those that differ from the default).
inline void ApplyPc() {
    auto& a=Setup();auto copy=[](char* to,size_t cap,const char* from) {size_t i=0;for(;from[i] && i+1<cap;++i)to[i]=from[i];to[i]=0;};
    if(!Pc().profile)return;
    if(detailed::Changed(pc[0]))copy(a.memory,sizeof a.memory,ask::Get("pc_memory"));
    if(!ask::Is("pc_cpu","auto"))copy(a.cpu,sizeof a.cpu,ask::Get("pc_cpu"));
    if(MakesDisk() && detailed::Changed(pc[2]))copy(a.diskMb,sizeof a.diskMb,ask::Get("pc_disk"));
    if(detailed::Changed(pc[3]))copy(a.sound,sizeof a.sound,ask::Get("pc_sound"));
    if(Is9x() && !ask::Is("pc_synth","auto"))copy(a.com1,sizeof a.com1,ask::Get("pc_synth"));
    if(detailed::Changed(pc[5]))copy(a.net,sizeof a.net,ask::Get("pc_net"));
    if(AutoSetup())copy(a.org,sizeof a.org,ask::Get("pc_org"));
    a.readerLead=JawsReader() && ask::Is("pc_reader_lead","yes");
    a.log=ask::Is("pc_log","yes");
}
// pc_disk2: a ready disk image the user has (found on the volumes as the medium is) becomes the profile's second disk.
inline bool AskSecondDisk(const Found* medium,uint64_t& bytes) {
    bytes=0;auto& a=Setup();a.disk2[0]=0;a.disk2Format[0]=0;
    size_t count=0;efi_handle volumes[32];const size_t vn=Volumes(volumes,32);
    for(size_t i=0;i<vn;++i)Scan(volumes[i],u"\\",0,count);
    static Choice list[25];static size_t map[25];size_t m=0;
    for(size_t i=0;i<count;++i) {const Found& f=FoundList()[i];
        if(!f.disk || !f.volume || (medium && medium->volume==f.volume && SameFold(medium->path,f.path)))continue;
        list[m]={f.label,f.label,"file"};map[m++]=i;}
    if(!m) {Say(T(u"Drugi obraz dysku: nie znalazłem żadnego pliku VHD, VMDK ani IMG poza nośnikiem instalacji. Dodasz go później: skopiuj go do folderu profilu i dopisz w CHARONPC.CFG wiersz disk2= z jego nazwą.",
                  u"Second disk image: I found no VHD, VMDK or IMG file apart from the installation media. You can add one later: copy it into the profile folder and add the line disk2= with its name to CHARONPC.CFG."));
        install::HearOut(200);return true;}
    list[m]={u"bez drugiego dysku",u"no second disk","none"};
    Say(T(u"Drugi dysk w Windows to twój własny obraz z danymi. Charon skopiuje go do profilu.",u"The second disk in Windows is your own image with data. Charon copies it into the profile."));
    size_t pick=m;if(!ask::Choose(T(u"Drugi obraz dysku, twoje dane:",u"Second disk image, your data:"),list,m+1,pick,m))return false;
    if(pick==m)return true;
    DataDisk()=FoundList()[map[pick]];const Found& f=DataDisk();
    const bool vhd=HasExtension(f.path,u".vhd"),vmdk=HasExtension(f.path,u".vmdk");
    const char* name=vhd?"DATA.VHD":vmdk?"DATA.VMDK":"DATA.IMG";const char* format=vhd?"vhd":vmdk?"vmdk":"raw";
    for(size_t i=0;;++i) {a.disk2[i]=name[i];if(!name[i])break;}
    for(size_t i=0;;++i) {a.disk2Format[i]=format[i];if(!format[i])break;}
    FileRef ref;ref.volume=f.volume;Set(ref.path,pathUnits,f.path);efi_file_protocol* file=nullptr;
    if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)==EFI_SUCCESS && file) {const auto* info=InfoOf(file);bytes=info?info->FileSize:0;file->Close(file);}
    return true;
}
// C:\AUTOEXEC.BAT. 0: nothing to write (every answer at its default: no file, as in the express mode).
// PATH is set in pieces ("PATH %PATH%;...") because a line of a batch file holds 259 characters; the whole PATH
// stays under 510 (CharonDOS expands a line into 520). Folders that do not fit are counted in `left`.
inline size_t AutoexecText(char* out,size_t cap,bool polishDos,size_t& folders,size_t& left) {
    folders=0;left=0;
    const bool path=ask::Has("dos_autoexec","path"),greet=ask::Has("dos_autoexec","greet"),date=ask::Has("dos_autoexec","date");
    const bool doszip=ask::Has("dos_autoexec","doszip"),commands=Commands() && ask::Any("dos_autoexec_cmd");
    const char* prompt=OwnPrompt()?ask::Get("dos_prompt_text"):ask::Get("dos_prompt");
    const bool promptLine=prompt[0] && !ask::Same(prompt,"$P$G");
    if(!path && !greet && !date && !doszip && !commands && !promptLine)return 0;
    detailed::Out o{out,cap,0,false};out[0]=0;
    o.Add("@ECHO OFF\r\nREM Written by Charon's installer (r410, detailed mode). CharonDOS runs this file at every start.\r\n");
    if(path) {
        static char line[300];size_t total=3,lineLength=0;line[0]=0;   // "C:\" is CharonDOS's own PATH
        auto flush=[&]() {if(lineLength) {o.Add(line);o.Add("\r\n");}line[0]=0;lineLength=0;};
        for(size_t i=0;i<dosapps::programCount;++i) {
            if(!dosapps::Installed()[i])continue;
            const char* dir=doscatalog::kPrograms[i].installDir;bool seen=false;
            for(size_t k=0;k<i && !seen;++k)seen=dosapps::Installed()[k] && ask::Same(doscatalog::kPrograms[k].installDir,dir);
            if(seen)continue;
            size_t d=0;while(dir[d])++d;
            if(total+3+d>510) {++left;continue;}
            if(lineLength+3+d>200)flush();
            if(!lineLength) {const char* head=folders?"PATH %PATH%":"PATH C:\\";for(;*head;++head)line[lineLength++]=*head;}
            line[lineLength++]=';';line[lineLength++]='C';line[lineLength++]=':';for(size_t c=0;c<d;++c)line[lineLength++]=dir[c];line[lineLength]=0;
            total+=3+d;++folders;
        }
        flush();
    }
    if(promptLine) {o.Add("PROMPT ");o.Add(prompt);o.Add("\r\n");}
    if(greet)o.Add(polishDos?"ECHO Witaj w CharonDOS.\r\n":"ECHO Welcome to CharonDOS.\r\n");
    if(date)o.Add("DATE\r\n");
    if(commands) {
        const char* c=ask::Get("dos_autoexec_cmd");
        while(*c) {
            while(*c==' ')++c;
            const char* e=c;while(*e && *e!='|')++e;
            const char* stop=e;while(stop>c && stop[-1]==' ')--stop;
            if(stop>c) {for(const char* s=c;s<stop;++s)o.Char(*s);o.Add("\r\n");}
            c=*e?e+1:e;
        }
    }
    if(doszip)o.Add("IF EXIST C:\\APPS\\DOSZIP\\DZ.EXE CD \\APPS\\DOSZIP\r\nIF EXIST C:\\APPS\\DOSZIP\\DZ.EXE DZ\r\nCD \\\r\n");
    return o.overflow?0:o.size;
}
// dos_speech: Charon's settings record on the CharonDOS partition (the copy CharonOnto made from this Charon, or this
// Charon's own when the copy has none), both slots, as charon_cmd.cpp writes them; read back and compared.
inline bool WriteSpeech(efi_handle volume) {
    efi_file_protocol* root=OpenRoot(volume);if(!root)return false;
    CharSettings s;
    if(LoadCharSettings(root,s)!=EFI_SUCCESS) {CharSettings here;s=LoadCharSettings(U().host->root,here)==EFI_SUCCESS?here:CharSettings{};}
    s.readerRate=TenthsOf(Value("dos_rate"));s.readerPitch=TenthsOf(Value("dos_pitch"));s.readerVolume=LevelOf(Value("dos_volume"));
    for(uint8_t k=0;k<4;++k)if(ask::Is("dos_punct",puncts[k].value))s.punctuation=k;
    bool ok=true;
    for(int pass=0;pass<2 && ok;++pass) {
        ++s.sequence;uint8_t bytes[charSettingsBytes];
        if(!EncodeSettings(s,bytes,sizeof bytes)) {ok=false;break;}
        FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,(s.sequence&1)?u"\\EFI\\HAK\\charon-settings-a.bin":u"\\EFI\\HAK\\charon-settings-b.bin");
        ok=WriteWhole(ref,bytes,sizeof bytes)==EFI_SUCCESS;
    }
    CharSettings back;
    ok=ok && LoadCharSettings(root,back)==EFI_SUCCESS && back.readerRate==s.readerRate && back.readerPitch==s.readerPitch && back.readerVolume==s.readerVolume && back.punctuation==s.punctuation;
    root->Close(root);return ok;
}
}  // namespace dosq

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
    // r410: the mode question CharonDOS did not have. Express = exactly the dialogue of r409. Detailed adds
    // CharonDOS's own questions (specification 5.13, the rows that need no new emulator work): what starts first
    // and the language CharonDOS speaks. Not asked when Windows 11 follows at once (dual) - that path decides both.
    bool dosFirst=true,keepFirst=false;const char16_t* dosLanguage=Pl()?u"pl":nullptr;
    // r410 (block C): the detailed mode asks the rest of specification 5.13 too (namespace dosq above).
    ask::Put("dos_mode","quick");ClearPc(Setup());
    if(!dual) {
        static const Choice modes[]={{u"tryb ekspresowy: jak najmniej pytań",u"express mode: as few questions as possible","quick"},
            {u"tryb szczegółowy: także start, język, programy, plik startowy, mowa, rozmiar partycji i ustawienia profilu Windows",u"detailed mode: also start-up, language, programs, the start-up file, speech, the partition size and the Windows profile's settings","detailed"}};
        size_t mode=0;if(!ask::Choose(T(u"Jak chcesz zainstalować CharonDOS?",u"How do you want to install CharonDOS?"),modes,2,mode,0))return;
        if(mode==1) {
            ask::Put("dos_mode","detailed");
            dosq::Reset(dosq::start,dosq::Count(dosq::start));
            if(!dosq::Ask(dosq::start,dosq::Count(dosq::start)))return;
            dosFirst=ask::Is("dos_first","dos");keepFirst=ask::Is("dos_first","keep");
            if(ask::Is("dos_lang","en"))dosLanguage=nullptr;else if(ask::Is("dos_lang","pl"))dosLanguage=u"pl";
        }
    }
    const bool detailedMode=dosq::Detailed();
    // 1. the Windows profile
    static Choice profileChoices[sizeof(profiles)/sizeof(profiles[0])];
    for(size_t i=0;i<sizeof(profiles)/sizeof(profiles[0]);++i)profileChoices[i]={profiles[i].pl,profiles[i].en,profiles[i].id?profiles[i].id:"none"};
    size_t profile=0;
    if(!ask::Choose(T(u"Który profil Windows dodać do CharonDOS? Windows działa wtedy w emulatorze PC wewnątrz CharonDOS i uruchamiasz go poleceniem WIN.",
                      u"Which Windows profile should be added to CharonDOS? Windows then runs in the PC emulator inside CharonDOS, and you start it with the command WIN."),
                    profileChoices,sizeof(profiles)/sizeof(profiles[0]),profile))return;
    const Profile& p=profiles[profile];
    size_t programs=0;   // the DOS programs, fetched after the files are in place
    if(detailedMode) {
        dosq::Pc()=dosq::PcContext{p.id!=nullptr,p.nt,false,false,false};
        dosq::PrepareRows(p);
        dosq::Reset(dosq::programs,dosq::Count(dosq::programs));dosq::Reset(dosq::pc,dosq::Count(dosq::pc));
        dosq::Reset(dosq::startup,dosq::Count(dosq::startup));
        dosapps::SayCatalogue();
        if(!dosq::Ask(dosq::programs,dosq::Count(dosq::programs)))return;
    } else if(!dosapps::AskGroup(programs,true))return;
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
    // r409: Windows 95 from a disc: the boot floppy the disc needs (the user's own image). With a floppy Setup is
    // not automatic (CharonPC prepares its own start floppy only when none is given), so its questions are not asked.
    Floppy().file=nullptr;Floppy().url[0]=0;
    if(medium && !medium->disk && NeedsBootFloppy(p) && !AskBootFloppy())return;
    const bool withFloppy=Floppy().file || Floppy().url[0];
    if(p.id && !p.nt && medium && !medium->disk && !withFloppy && !AskSetup())return;
    uint64_t secondDisk=0;   // bytes of the detailed mode's second disk image (pc_disk2)
    if(detailedMode) {
        auto& c=dosq::Pc();c.jaws=reader[0]=='j' && reader[1]=='f' && reader[2]=='w';
        c.comReader=p.id && !p.nt && !c.jaws && !ask::Same(reader,"none");c.ready=medium && medium->disk;
        dosq::pc[3].risk=c.jaws?"none":nullptr;dosq::pc[4].risk=c.comReader?"none":nullptr;
        if(p.id) {
            if(!dosq::Ask(dosq::pc,dosq::Count(dosq::pc)))return;
            if(medium) {KeptMedium()=*medium;medium=&KeptMedium();}   // the volumes are scanned again for the second disk
            if(!dosq::AskSecondDisk(medium,secondDisk))return;
            dosq::ApplyPc();
        }
        if(!dosq::Ask(dosq::startup,dosq::Count(dosq::startup)))return;
    }
    // 2. the disk
    auto& s=disks::D();disks::Load();
    static const Choice placeChoices[]={
        {u"cały dysk: CharonDOS na początku, reszta wolna, na przykład na Windows 11 obok",u"a whole disk: CharonDOS at the start, the rest left free, for example for Windows 11 alongside","whole"},
        {u"wolne miejsce na dysku, na którym już jest system",u"free space on a disk that already holds a system","free"},
    };
    size_t place=0;if(!dual && !ask::Choose(T(u"Gdzie zainstalować CharonDOS?",u"Where should CharonDOS be installed?"),placeChoices,2,place))return;
    const uint64_t profileBytes=Setup().diskMb[0]?dosq::Value("pc_disk")*mega:p.bytes;   // r410: pc_disk (the profile's DISK.IMG)
    const uint64_t need=(p.id?profileBytes+(uint64_t(1)<<30):(uint64_t(512)<<20))+(medium?uint64_t(1)<<30:0)+secondDisk;
    uint64_t partBytes=(need<(uint64_t(1)<<30)?(uint64_t(1)<<30):need+mega-1)/mega*mega;
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
    const bool ownHome=place==0 && d.hasCharon;   // r409: Charon's own disk is erased whole: Charon is carried back with its answer sets and plan state
    if(d.blockSize!=512) {Fail(T(u"Ten dysk ma sektory większe niż 512 bajtów; CharonDOS formatuje na razie tylko dyski z sektorami po 512 bajtów.",u"This disk has sectors larger than 512 bytes; for now CharonDOS can format only disks with 512-byte sectors."));return;}
    if(detailedMode) {
        // r410 dos_size: from the computed minimum to what the disk (whole: after the GPT's own sectors) or its gap holds;
        // FAT32 here takes at most 2^32-1 sectors.
        auto& q=dosq::sizeRow[0];
        uint64_t most=place==0?(d.blocks>2048+34?(d.blocks-2048-34)*512/mega:0):(d.freeBlocks*512/mega>2?d.freeBlocks*512/mega-2:0);
        if(most>2097151)most=2097151;
        const uint64_t least=partBytes/mega;if(most<least)most=least;
        q.low=int32_t(least);q.high=int32_t(most);dosq::Number(dosq::Defaults(5),least);q.fallback=dosq::Defaults(5);
        dosq::Reset(dosq::sizeRow,1);
        {static char16_t range[200];Set(range,200,T(u"Najwięcej: ",u"At most: "));CatN(range,200,size_t(most));Cat(range,200,T(u" megabajtów.",u" megabytes."));Say(range);}
        if(!dosq::Ask(dosq::sizeRow,1))return;
        partBytes=dosq::Value("dos_size")*mega;
        static char16_t summary[4000];dosq::Summary(summary,4000);Say(summary);detailed::Hear(1200);
    }
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
    if(!images::CharonOnto(volume,ownHome)) {Fail(T(u"Nie udało się skopiować Charona na partycję CharonDOS.",u"Charon could not be copied onto the CharonDOS partition."));return;}
    if(p.id && !MakeProfile(volume,p,reader,medium,fetchReader))return;
    if(detailedMode) {
        for(size_t i=0;i<dosapps::programCount;++i)dosapps::Installed()[i]=false;
        if(dosapps::CountWanted()) {if(install::EnsureNetwork())dosapps::FetchChosen(volume,u"\\EFI\\CharonDOS");else Say(T(u"Nie ma sieci: programy pobierzesz później.",u"No network: you can download the programs later."));}
        // the start-up file (dos_autoexec, dos_prompt): written only when an answer differs from the default
        static char text[4096];size_t folders=0,left=0;const size_t n=dosq::AutoexecText(text,sizeof text,dosLanguage!=nullptr,folders,left);
        if(n && !install::WriteText(volume,u"\\EFI\\CharonDOS\\C\\AUTOEXEC.BAT",text,n))Fail(T(u"Nie udało się zapisać pliku startowego AUTOEXEC.BAT.",u"The start-up file AUTOEXEC.BAT could not be written."));
        else if(n) {static char16_t said[300];Set(said,300,T(u"Zapisano plik startowy C:\\AUTOEXEC.BAT.",u"The start-up file C:\\AUTOEXEC.BAT was written."));
            if(ask::Has("dos_autoexec","path")) {Cat(said,300,T(u" Folderów programów w PATH: ",u" Program folders in PATH: "));CatN(said,300,folders);Cat(said,300,u".");}
            if(left) {Cat(said,300,T(u" Nie zmieściło się w PATH folderów: ",u" Folders that did not fit in PATH: "));CatN(said,300,left);Cat(said,300,u".");}
            Say(said);}
        // the speech settings (dos_speech): the settings record of the Charon on this partition
        if(dosq::SetSpeech()) {
            if(dosq::WriteSpeech(volume))Say(T(u"Ustawienia mowy zapisano w Charonie na partycji CharonDOS.",u"The speech settings were saved in the Charon on the CharonDOS partition."));
            else Fail(T(u"Nie udało się zapisać ustawień mowy na partycji CharonDOS; mowa będzie taka jak w Charonie.",u"The speech settings could not be saved on the CharonDOS partition; speech stays as in Charon."));
        }
    } else {size_t n=0;const Choice* g=dosapps::Groups(n);
     if(programs<n && !ask::Same(g[programs].value,"none")) {if(install::EnsureNetwork())dosapps::Fetch(volume,u"\\EFI\\CharonDOS",programs);else Say(T(u"Nie ma sieci: programy pobierzesz później.",u"No network: you can download the programs later."));}}
    // a fallback loader for firmware without boot entries: the Charon boot manager (it shows CharonDOS and every system)
    if(place==0) {static FileRef a,b;a.volume=volume;Set(a.path,pathUnits,u"\\EFI\\HAK\\HAKBOOT.EFI");b.volume=volume;Set(b.path,pathUnits,u"\\EFI\\BOOT\\BOOTX64.EFI");
        MakeFolders(volume,u"\\EFI\\BOOT");CopyFile(a,b);}
    // 5. firmware entries: CharonDOS first, Charon after it, the reader from power-on
    efi_status st=PutOption(false,u"CharonDOS",volume,u"\\EFI\\CharonDOS\\CHARONDOS.EFI",!keepFirst,nullptr,dosLanguage);   // r410 "keep": at the end
    if(st!=EFI_SUCCESS) {Fail(T(u"Nie udało się zapisać wpisu rozruchowego CharonDOS.",u"The CharonDOS boot entry could not be written."),st);return;}
    // r410: "Charon's menu first" puts Charon's entry in front of CharonDOS's instead of behind it.
    st=dosFirst || keepFirst?PutOption(false,u"Charon",volume,u"\\EFI\\HAK\\HAKBOOT.EFI",false,u"CharonDOS"):PutOption(false,u"Charon",volume,u"\\EFI\\HAK\\HAKBOOT.EFI",true,nullptr);
    if(st!=EFI_SUCCESS)Fail(T(u"Nie udało się zapisać wpisu rozruchowego menedżera rozruchu Charon.",u"The boot entry for the Charon boot manager could not be written."),st);
    st=PutOption(true,u"Charon voice",volume,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr);
    if(st!=EFI_SUCCESS)Fail(T(u"Nie udało się ustawić czytnika ekranu tak, by mówił od włączenia komputera.",u"The screen reader could not be set up to speak from power-on."),st);
    {static char16_t text[700];Set(text,700,keepFirst?T(u"CharonDOS jest zainstalowany. Jako pierwszy uruchamia się nadal ten sam system co dotąd; CharonDOS i Charon są na końcu kolejności rozruchu. ",u"CharonDOS is installed. The system that started first before still starts first; CharonDOS and Charon are at the end of the boot order. "):dosFirst?T(u"CharonDOS jest zainstalowany i będzie się uruchamiał jako pierwszy. ",u"CharonDOS is installed and will start first. ")
        :T(u"CharonDOS jest zainstalowany. Jako pierwsze uruchamia się menu Charona; CharonDOS jest na jego liście. ",u"CharonDOS is installed. Charon's menu starts first; CharonDOS is in its list. "));
     if(p.id)Cat(text,700,T(u"Profil Windows uruchamiasz w nim poleceniem WIN; pierwsze uruchomienie zaczyna instalację z twojego nośnika. ",u"In it the command WIN starts the Windows profile; the first start begins the installation from your media. "));
     Cat(text,700,T(u"Polecenie WINNT uruchamia Windows zainstalowany obok.",u"The command WINNT starts the Windows installed alongside."));Say(text);HearOut(300);}
    // 6. Windows 11 beside it
    if(place==0) {
        static const Choice next[]={{u"tak, zainstaluj teraz Windows 11 obok CharonDOS",u"yes, install Windows 11 alongside CharonDOS now","yes"},{u"nie, na razie wystarczy CharonDOS",u"no, CharonDOS is enough for now","no"}};
        size_t answer=dual?0:1;
        if(dual || (ask::Choose(T(u"Zainstalować teraz Windows 11 na wolnym miejscu tego dysku? CharonDOS zostanie systemem domyślnym, a Windows uruchomisz poleceniem WINNT.",
                         u"Install Windows 11 in the free space on this disk now? CharonDOS stays the default system, and you start Windows with the command WINNT."),next,2,answer) && answer==0)) {
            ask::Put("dosdefault","yes");
            // r410 dos_win11_boot (detailed): Windows Boot Manager first instead (no -FirstEntry CharonDOS at the first sign-in)
            if(detailedMode && !dual) {
                dosq::Reset(dosq::win11Row,1);
                if(dosq::Ask(dosq::win11Row,1) && ask::Is("dos_win11_boot","wbm")) {ask::Put("dosdefault","no");
                    Say(T(u"Po instalacji Windows jako pierwszy uruchomi się Menedżer rozruchu Windows. CharonDOS uruchomisz z menu rozruchowego komputera albo z menu Charona.",u"After the installation of Windows, Windows Boot Manager starts first. You start CharonDOS from the computer's boot menu or from Charon's menu."));HearOut(200);}
                else if(ask::Is("dos_win11_boot","charon")) {ask::Put("dosdefault","charon");   // r410 (agent 1): Install-Charon.ps1 -FirstEntry Charon
                    Say(T(u"Po instalacji Windows jako pierwsze uruchomi się menu Charona; z niego uruchomisz CharonDOS albo Windows.",u"After the installation of Windows, Charon's menu starts first; from it you start CharonDOS or Windows."));HearOut(200);}
            }
            Say(T(u"Przy pytaniu, co zrobić z dyskiem, wybierz wolne miejsce, a potem ten sam dysk.",u"When asked what should be done with the disk, choose free space, and then this same disk."));HearOut(200);
            install::Classic(2);
        }
    }
}
}  // namespace dossys
}  // namespace ws
}  // namespace hakefi
