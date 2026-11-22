#pragma once
// r407: Charon workspace - "Install Charon on this computer".
//
// Puts Charon onto an EFI system partition of the computer and registers it
// with the firmware, from Charon itself (no running system is needed):
//   reader    the screen reader alone: CHARONVOICE.EFI as a firmware driver
//             (Driver#### "Charon voice", loaded at every start); the
//             computer starts its own system as before.
//   fallback  the reader, and the Charon boot manager with a boot entry
//             "Charon" at the END of the boot order (a way in when the
//             system's own boot manager fails).
//   first     the reader, and the boot manager as the FIRST boot entry: the
//             computer starts into Charon's menu, whose Continue starts what
//             was first before.
// The files come from the copy Charon made of itself in memory after its start
// (CharonSelfCopy.h), so the stick may be gone. The user picks the partition
// from a spoken list (the one the firmware starts first is offered first) and
// confirms by typing a word. Existing files of the partition are not touched,
// except an older \EFI\HAK, which is replaced file by file.
// The entries are Charon's own two (found again by their descriptions and
// rewritten in place on a second installation); no other entry is changed.
// Included from CharonWorkspace.h after CharonWsInstall2.h. GPL-2.0-or-later.
#include "CharonSelfCopy.h"
namespace hakefi {
namespace ws {
namespace selfinstall {
#ifdef HAK_CHARON_HTTPS
using install::HearOut;
inline const ask::Choice variants[]={
    {u"sam czytnik ekranu: komputer startuje jak dotąd, a Charon czyta ekrany przed systemem",u"the screen reader alone: the computer starts as before, and Charon reads the screens before the system","reader"},
    {u"czytnik ekranu i menedżer rozruchu Charon jako zapasowy wpis, za systemem",u"the screen reader and the Charon boot manager as a spare entry, after the system","fallback"},
    {u"czytnik ekranu i menedżer rozruchu Charon jako pierwszy: komputer startuje do menu Charona",u"the screen reader and the Charon boot manager first: the computer starts into Charon's menu","first"}};
constexpr size_t maxTargets=12;
struct Target {efi_handle volume;uint64_t freeBytes;bool firstBoot;};
// Free bytes of a volume; false when it cannot be written.
inline bool Space(efi_handle volume,uint64_t& freeBytes) {
    freeBytes=0;efi_file_protocol* root=OpenRoot(volume);if(!root)return false;
    alignas(8) uint8_t info[512]={};size_t size=sizeof(info);bool ok=false;
    if(root->GetInfo(root,&media::fsInfoGuid,&size,info)==EFI_SUCCESS && size>36) {
        ok=info[8]==0;for(int i=7;i>=0;--i)freeBytes=(freeBytes<<8)|info[24+i];
    }
    root->Close(root);return ok;
}
inline bool Has(efi_handle volume,const char16_t* path) {
    FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,path);efi_file_protocol* file=nullptr;
    if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS || !file)return false;
    file->Close(file);return true;
}
// The partition signature of a volume's device path (the HD node), or null.
inline const uint8_t* Signature(const uint8_t* path,size_t bytes) {
    for(size_t at=0;at+4<=bytes;) {
        const size_t length=size_t(path[at+2])|(size_t(path[at+3])<<8);
        if(length<4 || at+length>bytes || path[at]==0x7f)break;
        if(path[at]==4 && path[at+1]==1 && length>=42)return path+at+24;
        at+=length;
    }
    return nullptr;
}
// True when the firmware's first boot entry starts from this volume.
inline bool FirstBootVolume(efi_handle volume) {
    size_t own=0;const uint8_t* path=DevicePathOf(volume,own);const uint8_t* mine=path?Signature(path,own):nullptr;
    if(!mine)return false;
    static uint16_t order[260];size_t size=sizeof(order);
    if(dossys::Rt()->GetVariable((char16_t*)u"BootOrder",&dossys::GlobalVars(),nullptr,&size,order)!=EFI_SUCCESS || size<2)return false;
    static uint8_t option[2048];char16_t name[16];dossys::VarName(u"Boot",order[0],name);size_t bytes=sizeof(option);
    if(dossys::Rt()->GetVariable(name,&dossys::GlobalVars(),nullptr,&bytes,option)!=EFI_SUCCESS || bytes<8)return false;
    size_t at=6;while(at+1<bytes && (option[at] || option[at+1]))at+=2;
    at+=2;if(at>=bytes)return false;
    const uint8_t* theirs=Signature(option+at,bytes-at);
    if(!theirs)return false;
    for(int i=0;i<16;++i)if(mine[i]!=theirs[i])return false;
    return true;
}
// What starts from the partition today, for the spoken list.
inline const char16_t* Holds(efi_handle volume) {
    if(Has(volume,u"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"))return T(u"Windows",u"Windows");
    static const char16_t* const linux[][2]={{u"\\EFI\\debian",u"Debian"},{u"\\EFI\\ubuntu",u"Ubuntu"},{u"\\EFI\\fedora",u"Fedora"},{u"\\EFI\\arch",u"Arch"},
        {u"\\EFI\\opensuse",u"openSUSE"},{u"\\EFI\\systemd",u"systemd-boot"},{u"\\EFI\\GRUB",u"GRUB"},{u"\\EFI\\CharonDOS",u"CharonDOS"},{u"\\EFI\\refind",u"rEFInd"}};
    for(const auto& l:linux)if(Has(volume,l[0]))return l[1];
    return T(u"bez rozpoznanego systemu",u"no system recognised");
}
// Files that a variant does not need (the reader alone leaves the boot manager and its tools out).
inline bool Wanted(const selfcopy::Item& item,bool manager,bool synths) {
    auto starts=[&](const char16_t* prefix) {
        size_t i=0;for(;prefix[i];++i)if(selfcopy::Fold(item.path[i])!=selfcopy::Fold(prefix[i]))return false;
        return true;};
    if(!synths && starts(u"\\EFI\\HAK\\SYNTHS\\"))return false;
    if(manager)return true;
    return !(starts(u"\\EFI\\HAK\\HAKBOOT.EFI") || starts(u"\\EFI\\HAK\\PACKAGES\\") || starts(u"\\EFI\\HAK\\TOOLS\\") || starts(u"\\EFI\\HAK\\ADDON\\")
        || starts(u"\\EFI\\HAK\\CharonShell.efi") || starts(u"\\EFI\\HAK\\install-charon.sh"));
}
inline uint64_t Needed(bool manager,bool synths,efi_handle volume) {
    auto& c=selfcopy::S();uint64_t bytes=0;
    for(size_t i=0;i<c.count;++i) {
        if(!Wanted(c.items[i],manager,synths))continue;
        bytes+=(c.items[i].size+4095)&~uint64_t(4095);
        // a file that is replaced gives its room back
        FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,c.items[i].path);efi_file_protocol* file=nullptr;
        if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)==EFI_SUCCESS && file) {
            const auto* info=InfoOf(file);const uint64_t old=info?(info->FileSize+4095)&~uint64_t(4095):0;
            bytes=bytes>old?bytes-old:0;file->Close(file);
        }
    }
    return bytes+(uint64_t(1)<<20);   // folders and a little slack
}
inline bool WriteOne(efi_handle volume,const selfcopy::Item& item) {
    static char16_t folder[pathUnits];Set(folder,pathUnits,item.path);
    size_t cut=0;for(size_t i=0;folder[i];++i)if(folder[i]==u'\\')cut=i;
    folder[cut]=0;
    if(cut && MakeFolders(volume,folder)!=EFI_SUCCESS)return false;
    FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,item.path);efi_file_protocol* out=nullptr;
    if(OpenFile(ref,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&out)==EFI_SUCCESS && out) {if(out->Delete(out)!=EFI_SUCCESS)return false;out=nullptr;}
    if(OpenFile(ref,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0,&out)!=EFI_SUCCESS || !out)return false;
    bool ok=true;
    for(uint64_t done=0;ok && done<item.size;) {
        size_t part=item.size-done>65536?65536:size_t(item.size-done);const size_t asked=part;
        ok=out->Write(out,&part,item.data+done)==EFI_SUCCESS && part==asked;
        done+=part;Pump();
    }
    if(ok && out->Flush)ok=out->Flush(out)==EFI_SUCCESS;
    if(!ok) {out->Delete(out);return false;}
    out->Close(out);return true;
}
// r408: the ESP is too small, or there is none. Rather than resize a partition (fragile - needs
// free space right after the ESP and a FAT grow), make Charon its OWN small EFI partition in a
// disk's free space and install there; firmware loads Driver####/Boot#### by device path, so they
// need not be on the system's ESP. Nothing else on the disk is touched. GPT, 512-byte sectors only.
inline bool InstallToNewPartition(bool manager,bool first) {
    auto& cc=selfcopy::S();constexpr uint64_t mega=uint64_t(1)<<20;
    uint64_t want=0;for(size_t i=0;i<cc.count;++i)if(Wanted(cc.items[i],manager,true))want+=cc.items[i].size+4096;
    uint64_t partBytes=(want+(uint64_t(16)<<20)+mega-1)/mega*mega;if(partBytes<(uint64_t(96)<<20))partBytes=uint64_t(96)<<20;   // r408a: room for FAT32's 65525 clusters with a margin
    disks::Load();auto& s=disks::D();
    static char16_t labels[disks::maxDisks][300];static ask::Choice list[disks::maxDisks];static size_t map[disks::maxDisks];size_t count=0;
    for(size_t i=0;i<s.count;++i) {const auto& d=s.list[i];
        if(d.readOnly || !d.gpt || d.blockSize!=512)continue;
        if(d.hasCharon && !hakefi::selfram::Active())continue;   // never rewrite the GPT of the disk Charon is still running from
        if(d.freeBlocks*512<partBytes+2*mega)continue;
        disks::DiskLabel(d,i,labels[count],300);list[count]={labels[count],labels[count],""};map[count]=i;++count;}
    if(!count) {static char16_t t[400];Set(t,400,T(u"Żaden dysk nie ma dość wolnego miejsca na nową partycję Charona: potrzeba około ",u"No disk has enough free space for a new Charon partition: about "));
        CatN(t,400,partBytes>>20);Cat(t,400,T(u" MB. W Windows zwolnisz miejsce skryptem Charon MakeRoom.",u" MB are needed. In Windows free space with the Charon MakeRoom script."));Fail(t);return false;}
    size_t k=0;if(!ask::Choose(T(u"Na którym dysku utworzyć partycję Charona w wolnym miejscu?",u"On which disk should a Charon partition be made in the free space?"),list,count,k))return false;
    const auto& d=s.list[map[k]];
    {static char16_t t[700];Set(t,700,T(u"W wolnym miejscu tego dysku powstanie nowa partycja EFI dla Charona, ",u"A new EFI partition for Charon will be created in this disk's free space, "));
     CatN(t,700,partBytes>>20);Cat(t,700,T(u" MB. Pozostałe partycje zostają bez zmian. Aby zacząć, wpisz słowo TAK i naciśnij Enter. Escape rezygnuje.",u" MB. The other partitions are kept. To start, type the word YES and press Enter. Escape gives up."));Say(t);
     static char16_t w[16];w[0]=0;if(!Prompt(T(u"Wpisz TAK, aby zacząć:",u"Type YES to start:"),w,15,nullptr,nullptr,false))return false;
     if(!SameFold(w,T(u"tak",u"yes"))) {Say(T(u"Nie wpisano potwierdzenia. Nic nie zrobiono.",u"The confirmation was not typed. Nothing was done."));return false;}}
    Say(T(u"Tworzę partycję Charona.",u"Creating the Charon partition."));
    seeddisk::Disk sd{d.io,d.blockSize,d.blocks,images::SeedRead,images::SeedWrite};
    seeddisk::Guid unique;images::RandomGuid(unique,91);
    const uint64_t firstLba=(d.freeFirst*512+mega-1)/mega*mega/512;
    static const seeddisk::Guid espGuid={{0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b}};
    seeddisk::AddRequest request{};request.type=espGuid;request.unique=unique;request.name=u"Charon";request.sizeBytes=partBytes;request.alignmentBytes=0;request.placement=seeddisk::Placement::At;request.firstLba=firstLba;request.attributes=0;
    seeddisk::AddResult added{};
    if(seeddisk::AddPartition(sd,images::SeedWork(),seeddisk::kWorkBytes,request,added)!=seeddisk::Error::None) {Fail(T(u"Nie udało się dodać partycji Charona.",u"The Charon partition could not be added."));return false;}
    uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
    if(!dossys::FormatFat32(d.io,added.firstLba,added.lbaCount,"CHARON",lo^hi)) {Fail(T(u"Formatowanie partycji Charona nie powiodło się.",u"Formatting the Charon partition failed."));return false;}
    if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
    images::Reconnect(d.handle);
    const efi_handle esp=images::VolumeOfPartition(unique.bytes);
    if(!esp) {Fail(T(u"Firmware nie widzi nowej partycji Charona.",u"The firmware does not see the new Charon partition."));return false;}
    Say(T(u"Kopiuję Charona na nową partycję.",u"Copying Charon to the new partition."));
    uint64_t total=0,done=0;size_t written=0;
    for(size_t i=0;i<cc.count;++i)if(Wanted(cc.items[i],manager,true))total+=cc.items[i].size;
    for(size_t i=0;i<cc.count;++i) {if(!Wanted(cc.items[i],manager,true))continue;
        if(!WriteOne(esp,cc.items[i])) {Fail(T(u"Zapis na nową partycję nie powiódł się.",u"Writing to the new partition failed."));return false;}
        done+=cc.items[i].size;++written;Progress(T(u"Charon na partycję",u"Charon to the partition"),done,total);}
    efi_status status=dossys::PutOption(true,u"Charon voice",esp,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr,nullptr,true);
    if(status==EFI_UNSUPPORTED)status=dossys::PutOption(true,u"Charon voice",esp,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr);
    if(status!=EFI_SUCCESS) {Fail(T(u"Pliki są na nowej partycji, ale firmware nie przyjął wpisu czytnika ekranu.",u"The files are on the new partition, but the firmware did not accept the screen reader's entry."),status);return false;}
    if(manager) {status=dossys::PutOption(false,u"Charon",esp,u"\\EFI\\HAK\\HAKBOOT.EFI",first,nullptr,nullptr,true);
        if(status==EFI_UNSUPPORTED)status=dossys::PutOption(false,u"Charon",esp,u"\\EFI\\HAK\\HAKBOOT.EFI",first,nullptr);
        if(status!=EFI_SUCCESS) {Fail(T(u"Czytnik jest zainstalowany, ale firmware nie przyjął wpisu rozruchowego Charona.",u"The screen reader is installed, but the firmware did not accept Charon's boot entry."),status);return false;}}
    {static char16_t t[500];Set(t,500,T(u"Gotowe. Utworzyłem partycję Charona i skopiowałem plików: ",u"Done. I made the Charon partition and copied files: "));CatN(t,500,written);
     Cat(t,500,T(u". Czytnik ekranu będzie ładowany przy każdym starcie komputera.",u". The screen reader will be loaded at every start of the computer."));Play(Icon::done);Say(t);HearOut(200);}
    return true;
}
inline void Run() {
    ScriptBegin(T(u" Charon: instalacja Charona na tym komputerze",u" Charon: installing Charon on this computer"));
    struct End {~End() {ScriptEnd();}} end;
    auto& host=*U().host;
    // 1. Charon's own files in memory
    auto& c=selfcopy::S();
    if(!selfcopy::Ready() && !selfcopy::Failed()) {
        Say(T(u"Kopiuję Charona do pamięci.",u"Copying Charon into memory."));
        unsigned rounds=0;
        while(selfcopy::Step(host.boot,host.root,true)) {
            Pump();
            if(++rounds%32==0)Progress(T(u"Charon do pamięci",u"Charon into memory"),selfcopy::Done(),selfcopy::Total());
        }
    }
    if(!selfcopy::Ready() || !c.count) {
        Fail(T(u"Nie udało się wczytać plików Charona z nośnika, z którego wystartował. Włóż ten nośnik i spróbuj ponownie.",
               u"Charon's files could not be read from the medium it started from. Insert that medium and try again."));return;}
    bool haveReader=false,haveManager=false;
    for(size_t i=0;i<c.count;++i) {
        if(selfcopy::Same(c.items[i].path,u"\\EFI\\HAK\\CHARONVOICE.EFI"))haveReader=true;
        if(selfcopy::Same(c.items[i].path,u"\\EFI\\HAK\\HAKBOOT.EFI"))haveManager=true;
    }
    if(!haveReader) {Fail(T(u"Na nośniku Charona nie ma czytnika ekranu CHARONVOICE.EFI, więc nie ma czego instalować.",
                            u"Charon's medium has no screen reader CHARONVOICE.EFI, so there is nothing to install."));return;}
    // 2. what to install
    size_t variant=0;
    if(!ask::Choose(T(u"Co zainstalować na tym komputerze?",u"What to install on this computer?"),variants,haveManager?3:1,variant))return;
    const bool manager=variant!=0,first=variant==2;
    // 3. where: the EFI system partitions (volumes with an \EFI folder, writable, not Charon's own medium)
    static Target targets[maxTargets];size_t count=0;
    {static efi_handle volumes[64];const size_t n=Volumes(volumes,64);
     for(size_t i=0;i<n && count<maxTargets;++i) {
        if(volumes[i]==host.selfDevice || hakefi::selfram::IsVolume(volumes[i]) || !Has(volumes[i],u"\\EFI"))continue;   // r408a: not Charon's medium, not its copy in memory
        uint64_t freeBytes=0;if(!Space(volumes[i],freeBytes))continue;
        targets[count++]={volumes[i],freeBytes,FirstBootVolume(volumes[i])};
     }}
    if(!count) {
        Say(T(u"Nie znalazłem partycji systemowej EFI, na której można zapisać. Mogę utworzyć nową, małą partycję Charona w wolnym miejscu dysku.",
              u"I found no EFI system partition that can be written. I can make a new, small Charon partition in the disk's free space."));HearOut(80);
        InstallToNewPartition(manager,first);return;}
    // the one the firmware starts first goes to the top of the list
    for(size_t i=1;i<count;++i)if(targets[i].firstBoot && !targets[0].firstBoot) {const Target t=targets[0];targets[0]=targets[i];targets[i]=t;}
    static char16_t labels[maxTargets][260];static ask::Choice list[maxTargets];
    for(size_t i=0;i<count;++i) {
        static char16_t name[80];VolumeLabel(targets[i].volume,name,80);
        Set(labels[i],260,T(u"partycja EFI ",u"EFI partition "));Cat(labels[i],260,name);
        Cat(labels[i],260,u", ");Cat(labels[i],260,Holds(targets[i].volume));
        Cat(labels[i],260,T(u", wolne ",u", free "));CatN(labels[i],260,targets[i].freeBytes>>20);Cat(labels[i],260,u" MB");
        if(targets[i].firstBoot)Cat(labels[i],260,T(u", z niej komputer startuje",u", the computer starts from it"));
        if(Has(targets[i].volume,u"\\EFI\\HAK\\CHARONVOICE.EFI"))Cat(labels[i],260,T(u", jest na niej starszy Charon",u", an older Charon is on it"));
        list[i]={labels[i],labels[i],"x"};
    }
    size_t chosen=0;
    if(!ask::Choose(T(u"Na której partycji systemowej EFI?",u"On which EFI system partition?"),list,count,chosen))return;
    const efi_handle esp=targets[chosen].volume;
    // 4. room
    bool synths=true;
    if(Needed(manager,true,esp)>targets[chosen].freeBytes) {
        synths=false;
        if(Needed(manager,false,esp)>targets[chosen].freeBytes) {
            static char16_t text[500];Set(text,500,T(u"Na tej partycji EFI jest za mało miejsca: potrzeba ",u"This EFI partition has too little room: "));
            CatN(text,500,Needed(manager,false,esp)>>20);Cat(text,500,T(u" MB, wolne jest ",u" MB are needed, free are "));CatN(text,500,targets[chosen].freeBytes>>20);
            Cat(text,500,T(u" MB. Zamiast tego mogę utworzyć nową, małą partycję Charona w wolnym miejscu dysku.",u" MB. Instead I can make a new, small Charon partition in the disk's free space."));
            Say(text);HearOut(120);
            InstallToNewPartition(manager,first);return;}
        Say(T(u"Dodatkowe syntezatory mowy nie zmieszczą się na tej partycji, więc ich nie kopiuję. Charon będzie mówił wbudowanym głosem.",
              u"The additional speech synthesizers do not fit on this partition, so I leave them out. Charon will speak with its built-in voice."));
        HearOut(60);
    }
    // 5. the plan, confirmed by a typed word
    {static char16_t text[900];
     Set(text,900,T(u"Zainstaluję: ",u"I will install: "));Cat(text,900,T(variants[variant].pl,variants[variant].en));
     Cat(text,900,T(u". Miejsce: ",u". Place: "));Cat(text,900,labels[chosen]);
     Cat(text,900,T(u". Do firmware dopiszę sterownik „Charon voice”",u". I will add the driver \"Charon voice\" to the firmware"));
     if(manager)Cat(text,900,first?T(u" i wpis rozruchowy „Charon” na początku kolejki",u" and the boot entry \"Charon\" at the start of the boot order")
                                  :T(u" i wpis rozruchowy „Charon” na końcu kolejki",u" and the boot entry \"Charon\" at the end of the boot order"));
     Cat(text,900,T(u". Pozostałych wpisów i plików systemu nie zmieniam. Aby zacząć, wpisz słowo TAK i naciśnij Enter. Escape rezygnuje.",
                    u". I change no other entry and no file of the system. To start, type the word YES and press Enter. Escape gives up."));
     Say(text);
     static char16_t word[16];word[0]=0;
     if(!Prompt(T(u"Wpisz TAK, aby zacząć:",u"Type YES to start:"),word,15,nullptr,nullptr,false))return;
     if(!SameFold(word,T(u"tak",u"yes"))) {Say(T(u"Nie wpisano potwierdzenia. Nic nie zrobiono.",u"The confirmation was not typed. Nothing was done."));return;}
    }
    // 6. the files
    Say(T(u"Kopiuję Charona na partycję EFI.",u"Copying Charon to the EFI partition."));
    uint64_t total=0,done=0;size_t written=0;
    for(size_t i=0;i<c.count;++i)if(Wanted(c.items[i],manager,synths))total+=c.items[i].size;
    for(size_t i=0;i<c.count;++i) {
        if(!Wanted(c.items[i],manager,synths))continue;
        if(!WriteOne(esp,c.items[i])) {
            Fail(T(u"Zapis na partycję EFI nie powiódł się. Wpisów w firmware nie zmieniłem; pliki zapisane do tej chwili zostały w folderze EFI, HAK.",
                   u"Writing to the EFI partition failed. I did not change the firmware's entries; the files written so far remain in the folder EFI, HAK."));return;}
        done+=c.items[i].size;++written;
        Progress(T(u"Charon na partycję EFI",u"Charon to the EFI partition"),done,total);
    }
    // 7. the firmware's entries: the reader always, the boot manager when asked for
    efi_status status=dossys::PutOption(true,u"Charon voice",esp,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr,nullptr,true);
    if(status==EFI_UNSUPPORTED)status=dossys::PutOption(true,u"Charon voice",esp,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr);
    if(status!=EFI_SUCCESS) {
        Fail(T(u"Pliki są na partycji, ale firmware nie przyjął wpisu czytnika ekranu. Niektóre komputery blokują zapis takich wpisów; wtedy Charona instaluje się z poziomu systemu.",
               u"The files are on the partition, but the firmware did not accept the screen reader's entry. Some computers block such entries; Charon is then installed from the running system."),status);return;}
    if(manager) {
        status=dossys::PutOption(false,u"Charon",esp,u"\\EFI\\HAK\\HAKBOOT.EFI",first,nullptr,nullptr,true);
        if(status==EFI_UNSUPPORTED)status=dossys::PutOption(false,u"Charon",esp,u"\\EFI\\HAK\\HAKBOOT.EFI",first,nullptr);
        if(status!=EFI_SUCCESS) {
            Fail(T(u"Czytnik ekranu jest zainstalowany, ale firmware nie przyjął wpisu rozruchowego Charona.",
                   u"The screen reader is installed, but the firmware did not accept Charon's boot entry."),status);return;}
    }
    // 8. the result, with the Secure Boot warning where it applies
    {static char16_t text[700];
     Set(text,700,T(u"Gotowe. Skopiowano plików: ",u"Done. Files copied: "));CatN(text,700,written);
     Cat(text,700,T(u". Czytnik ekranu Charona będzie ładowany przy każdym starcie komputera",u". Charon's screen reader will be loaded at every start of the computer"));
     if(manager)Cat(text,700,first?T(u", a komputer wystartuje do menu Charona; Kontynuuj uruchamia dotychczasowy system",u", and the computer will start into Charon's menu; Continue starts the system you had")
                                  :T(u", a wpis Charon jest zapasowym wejściem na końcu kolejki rozruchu",u", and the entry Charon is a spare way in at the end of the boot order"));
     Cat(text,700,u".");
     uint8_t secure=0;size_t size=1;
     if(dossys::Rt()->GetVariable((char16_t*)u"SecureBoot",&dossys::GlobalVars(),nullptr,&size,&secure)==EFI_SUCCESS && size==1 && secure==1)
        Cat(text,700,T(u" Uwaga: na tym komputerze włączony jest Secure Boot. Firmware uruchomi Charona dopiero wtedy, gdy jego klucz zostanie dodany do zaufanych albo Secure Boot zostanie wyłączony.",
                       u" Note: Secure Boot is on in this computer. The firmware will start Charon only after its key has been added to the trusted keys or Secure Boot has been turned off."));
     Play(Icon::done);Say(text);HearOut(250);
    }
}
#else
inline void Run() {Fail(T(u"Ta wersja Charona nie ma instalatora.",u"This build of Charon has no installer."));}
#endif
}  // namespace selfinstall
}  // namespace ws
}  // namespace hakefi
