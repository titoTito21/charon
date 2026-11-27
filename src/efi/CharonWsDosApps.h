#pragma once
// r402: Charon workspace - CharonDOS fetches its programs itself.
//
// The catalog (CharonDosCatalog.h) lists free DOS programs that were run in
// the CharonDOS emulator and passed (see src/dos/APPS-CATALOG.txt). Each is a
// ZIP on the official FreeDOS repository: it is downloaded into memory, its
// SHA-256 is compared with the catalog's, and only the files the catalog
// names are unpacked onto the CharonDOS drive (the folder C beside
// CHARONDOS.EFI). Nothing else is written. GPL-2.0-or-later.
#ifdef HAK_CHARON_HTTPS
#include "CharonDosCatalog.h"
#include "CharonInflate.h"
#include "CharonZip.h"
#include "CharonSha256.h"
namespace hakefi {
namespace ws {
namespace dosapps {
struct State {Memory archive;uint8_t* out;size_t outSize,outCap;bool overflow;};
inline State& D() {static State state;return state;}
inline bool Read(void*,uint64_t offset,uint8_t* out,size_t size) {
    auto& d=D();if(offset+size>d.archive.size)return false;
    memcpy(out,d.archive.data+offset,size);return true;
}
inline bool Take(void*,const uint8_t* data,size_t size) {
    auto& d=D();if(d.outSize+size>d.outCap) {d.overflow=true;return false;}
    memcpy(d.out+d.outSize,data,size);d.outSize+=size;return true;
}
inline void CatAscii(char16_t* out,size_t cap,const char* text) {size_t n=Len(out);for(;*text && n+1<cap;++text)out[n++]=*text=='/'?u'\\':char16_t(uint8_t(*text));out[n]=0;}
// The CharonDOS package folder (where CHARONDOS.EFI lies).
inline bool Package(efi_handle& volume,char16_t* folder,size_t cap) {
    efi_handle volumes[32];const size_t count=Volumes(volumes,32);
    static const char16_t* const places[]={u"\\EFI\\HAK\\packages\\CharonDOS",u"\\charon-packages\\CharonDOS"};
    for(size_t i=0;i<count;++i)for(const auto* place:places) {
        FileRef ref;ref.volume=volumes[i];Set(ref.path,pathUnits,place);Cat(ref.path,pathUnits,u"\\CHARONDOS.EFI");efi_file_protocol* file=nullptr;
        if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS || !file)continue;
        file->Close(file);
        if(VolumeReadOnly(volumes[i]))continue;
        volume=volumes[i];Set(folder,cap,place);return true;
    }
    return false;
}
// One downloaded program (its archive in D().archive): checked and unpacked. False: `why` says why (not said yet).
// r410 (agent 3): split from the download, so that many programs download at once (FetchMany).
inline bool Unpack(const doscatalog::Program& p,efi_handle volume,const char16_t* package,const char16_t*& why) {
    auto& d=D();bool ok=false;why=T(u"Archiwum jest uszkodzone.",u"The archive is damaged.");
    {   // the very file that was tested
        Sha256 hash;hash.Update(d.archive.data,d.archive.size);uint8_t got[32];hash.Final(got);static const char hex[]="0123456789abcdef";
        bool same=true;for(int i=0;i<32 && same;++i)same=p.sha256[i*2]==hex[got[i]>>4] && p.sha256[i*2+1]==hex[got[i]&15];
        if(!same) {why=T(u"Plik na serwerze jest inny niż ten, który sprawdzono w CharonDOS. Nie zainstalowano go.",u"The file on the server differs from the one tested in CharonDOS. It was not installed.");return false;}
    }
    static uint8_t scratch[65536+64];static uint8_t directory[8192];static uint8_t input[65536];static uint8_t window[32768];static inflate::Decoder decoder;
    zip::Source source{nullptr,Read};zip::Archive archive{};
    if(zip::Open(source,d.archive.size,scratch,sizeof(scratch),archive)==zip::Error::None) {
        ok=true;
        for(const doscatalog::File* f=p.files;ok && f->zipPath;++f) {
            zip::Cursor cursor{};zip::Begin(cursor,archive,directory,sizeof(directory));zip::Entry entry{};bool found=false;
            while(zip::Next(cursor,entry)==zip::Error::None)if(zip::NameEquals(entry,f->zipPath)) {found=true;break;}
            if(!found || entry.uncompressedSize>(uint64_t(8)<<20)) {ok=false;why=T(u"W archiwum brakuje pliku wymienionego w katalogu programów.",u"A file listed in the program catalogue is missing from the archive.");break;}
            static Memory file;file.reserved=false;file.extra=0;
            if(!Reserve(file,size_t(entry.uncompressedSize)+16)) {ok=false;why=T(u"Za mało pamięci RAM.",u"Not enough RAM.");break;}
            d.out=file.data;d.outSize=0;d.outCap=size_t(entry.uncompressedSize);d.overflow=false;
            zip::ExtractWork work{};work.input=input;work.inputBytes=sizeof(input);work.decoder=&decoder;work.window=window;work.windowBytes=sizeof(window);
            zip::Sink sink{nullptr,Take};
            if(zip::Extract(archive,entry,work,sink)!=zip::Error::None || d.overflow)ok=false;
            else {
                static FileRef target;target.volume=volume;Set(target.path,pathUnits,package);Cat(target.path,pathUnits,u"\\C");CatAscii(target.path,pathUnits,p.installDir);
                Cat(target.path,pathUnits,u"\\");CatAscii(target.path,pathUnits,f->dest);
                static char16_t folder[pathUnits];ParentOf(target.path,folder,pathUnits);MakeFolders(volume,folder);
                if(WriteWhole(target,file.data,d.outSize)!=EFI_SUCCESS) {ok=false;why=T(u"Zapis na dysk CharonDOS nie powiódł się; może brakuje miejsca.",u"Writing to the CharonDOS drive failed; it may be full.");}
            }
            Free(file);Pump();
        }
    }
    return ok;
}
// r410 (agent 3, user 2026-11-26: CharonDOS's programs download several at once): the programs `list` (catalogue
// indexes) in ONE batch of net::many::Download - 4 kept-alive connections to the FreeDOS mirror, at most 64 MB of
// archives in memory at once. Each archive is checked (SHA-256) and unpacked as soon as it is complete, while the
// others still download, then its memory is given back; its name is said as it is installed (the newest, "and 3
// more" when faster than speech). The first failure is said in full afterwards.
struct Many {efi_handle volume;const char16_t* package;const size_t* list;size_t done;const char16_t* why;bool* installed;};
inline Many& M() {static Many many;return many;}
inline bool InstallArrived(net::many::Item& item) {
    auto& m=M();auto& d=D();const size_t k=size_t(reinterpret_cast<uintptr_t>(item.user));
    const auto& p=doscatalog::kPrograms[m.list[k]];
    d.archive=*item.memory;
    const char16_t* why=nullptr;const bool ok=Unpack(p,m.volume,m.package,why);
    Free(*item.memory);d.archive=Memory{};
    if(!ok) {if(!m.why)m.why=why;return false;}
    ++m.done;if(m.installed)m.installed[m.list[k]]=true;
    return true;
}
inline void FetchMany(efi_handle volume,const char16_t* package,const size_t* list,size_t count,size_t& done,size_t& failed,bool* installed) {
    done=failed=0;if(!count)return;
    auto& m=M();m.volume=volume;m.package=package;m.list=list;m.done=0;m.why=nullptr;m.installed=installed;
    auto* items=static_cast<net::many::Item*>(Allocate(sizeof(net::many::Item)*count));
    auto* memory=static_cast<Memory*>(Allocate(sizeof(Memory)*count));
    if(!items || !memory) {Release(items);Release(memory);failed=count;Fail(T(u"Za mało pamięci RAM.",u"Not enough RAM."));return;}
    for(size_t i=0;i<count;++i) {
        const auto& p=doscatalog::kPrograms[list[i]];
        memset(&items[i],0,sizeof(items[i]));memory[i]=Memory{};
        items[i].url=p.url;items[i].size=p.zipBytes?p.zipBytes:https::unknownLength;items[i].memory=&memory[i];
        items[i].check=InstallArrived;items[i].name=Pl()?p.namePl:p.nameEn;items[i].user=reinterpret_cast<void*>(uintptr_t(i));
    }
    {static char16_t text[200];Set(text,200,T(u"Pobieram programy, kilka naraz. Liczba programów: ",u"Downloading the programs, several at once. Programs: "));
     CatN(text,200,count);Cat(text,200,u".");Say(text);}
    net::many::Options o{};o.connections=4;o.perHost=4;o.inFlightBytes=uint64_t(64)<<20;
    o.label=T(u"Programy CharonDOS",u"CharonDOS programs");
    const net::many::Outcome r=net::many::Download(items,count,o);
    done=m.done;failed=count-done;
    if(r.cancelled)Say(T(u"Przerwano.",u"Stopped."));
    else if(r.failed) {
        if(r.firstFailure && r.firstFailure->checkFailed && m.why)Fail(m.why);
        else if(r.firstFailure)net::many::Explain(*r.firstFailure);
    }
    for(size_t i=0;i<count;++i)Free(memory[i]);
    Release(items);Release(memory);
}
// r403: the program groups; the installer of CharonDOS asks the same question.
inline const ask::Choice* Groups(size_t& n) {
    static const ask::Choice groups[]={{u"wszystkie sprawdzone programy",u"all the tested programs",""},{u"edytory",u"editors","edit"},{u"pliki i foldery",u"files and folders","files"},
        {u"przeglądanie tekstu i pomoc",u"text viewing and help","view"},{u"narzędzia",u"tools","tools"},{u"archiwa i pakowanie",u"archives and compression","archive"},{u"programowanie",u"programming","devel"},{u"gry",u"games","games"},
        {u"bez programów",u"no programs","none"}};
    n=sizeof(groups)/sizeof(groups[0]);return groups;
}
constexpr size_t programCount=sizeof(doscatalog::kPrograms)/sizeof(doscatalog::kPrograms[0]);
// r410: the sentence before the program question (said by AskGroup and by the detailed mode's program sets).
inline void SayCatalogue() {
    static char16_t text[300];Set(text,300,T(u"CharonDOS pobierze darmowe programy DOS z oficjalnego repozytorium FreeDOS. Liczba programów w katalogu, sprawdzonych w CharonDOS: ",u"CharonDOS downloads free DOS programs from the official FreeDOS repository. Number of programs in the catalogue, tested in CharonDOS: "));
    CatN(text,300,programCount);Cat(text,300,T(u".",u"."));Say(text);install::HearOut(150);
}
inline bool AskGroup(size_t& group,bool allowNone) {
    size_t n=0;const ask::Choice* groups=Groups(n);if(!allowNone)--n;
    SayCatalogue();
    group=0;return ask::Choose(T(u"Które programy pobrać?",u"Which programs should be downloaded?"),groups,n,group);
}
// ---- r410: the detailed mode - program sets, and single programs inside each set -----------------
// dos_sets (check boxes: the seven groups) and dos_set_<group> (check boxes: the group's programs by catalogue
// id). FetchChosen installs exactly the checked programs, plus a program another checked one needs (requires_).
constexpr size_t setCount=7;
inline const char* const setKeys[setCount]={"dos_set_edit","dos_set_files","dos_set_view","dos_set_tools","dos_set_archive","dos_set_devel","dos_set_games"};
inline const char* SetGroup(size_t g) {return setKeys[g]+8;}   // "edit", "files", ...
inline const char* SetKeyOf(const char* group) {for(size_t g=0;g<setCount;++g)if(ask::Same(SetGroup(g),group))return setKeys[g];return nullptr;}
inline const ask::Choice* SetChoices() {size_t n=0;return Groups(n)+1;}   // the groups without "all" and "none"
// The programs of group g as check boxes (filled once; the catalogue is constant).
struct SetList {ask::Choice choices[programCount];size_t count;char all[programCount*12];};
inline SetList* SetLists() {static SetList lists[setCount];return lists;}
inline void MakeSetLists() {
    SetList* lists=SetLists();
    for(size_t g=0;g<setCount;++g) {
        SetList& l=lists[g];l.count=0;size_t m=0;l.all[0]=0;
        for(size_t i=0;i<programCount;++i) {
            const auto& p=doscatalog::kPrograms[i];if(!ask::Same(p.group,SetGroup(g)))continue;
            l.choices[l.count++]=ask::Choice{p.namePl,p.nameEn,p.id};
            if(m)l.all[m++]=',';
            for(const char* s=p.id;*s && m+2<sizeof l.all;++s)l.all[m++]=*s;
            l.all[m]=0;
        }
    }
}
inline bool Chosen(const doscatalog::Program& p) {const char* key=SetKeyOf(p.group);return key && ask::Has("dos_sets",p.group) && ask::Has(key,p.id);}
// What FetchChosen installed (by catalogue index), for the start-up file's PATH.
inline bool* Installed() {static bool installed[programCount];return installed;}
inline bool Wanted(size_t i) {
    const auto& p=doscatalog::kPrograms[i];if(Chosen(p))return true;
    for(size_t k=0;k<programCount;++k) {const auto& q=doscatalog::kPrograms[k];if(q.requires_ && ask::Same(q.requires_,p.id) && Chosen(q))return true;}
    return false;
}
inline size_t CountWanted() {size_t n=0;for(size_t i=0;i<programCount;++i)if(Wanted(i))++n;return n;}
inline void FetchChosen(efi_handle volume,const char16_t* package) {
    for(size_t i=0;i<programCount;++i)Installed()[i]=false;
    const size_t wanted=CountWanted();if(!wanted)return;
    size_t done=0,failed=0;
    static size_t list[programCount];size_t n=0;
    for(size_t i=0;i<programCount;++i)if(Wanted(i))list[n++]=i;
    FetchMany(volume,package,list,n,done,failed,Installed());
    static char16_t summary[400];Set(summary,400,T(u"Zainstalowano programów: ",u"Programs installed: "));CatN(summary,400,done);
    if(failed) {Cat(summary,400,T(u", nie udało się: ",u", failed: "));CatN(summary,400,failed);}
    Cat(summary,400,T(u". Są na dysku C CharonDOS w folderze APPS; każdy program uruchamiasz w jego folderze.",u". They are on drive C of CharonDOS in the folder APPS; start each program from its own folder."));
    Play(Icon::done);Say(summary);install::HearOut(300);
}
// Fetches group `group` into <package>\C on `volume` (network must be up).
inline void Fetch(efi_handle volume,const char16_t* package,size_t group) {
    size_t n=0;const ask::Choice* groups=Groups(n);
    if(group>=n || ask::Same(groups[group].value,"none"))return;
    const size_t count=sizeof(doscatalog::kPrograms)/sizeof(doscatalog::kPrograms[0]);
    size_t done=0,failed=0;
    static size_t list[programCount];size_t m=0;
    for(size_t i=0;i<count && m<programCount;++i)if(!groups[group].value[0] || ask::Same(doscatalog::kPrograms[i].group,groups[group].value))list[m++]=i;
    FetchMany(volume,package,list,m,done,failed,nullptr);
    static char16_t summary[400];Set(summary,400,T(u"Zainstalowano programów: ",u"Programs installed: "));CatN(summary,400,done);
    if(failed) {Cat(summary,400,T(u", nie udało się: ",u", failed: "));CatN(summary,400,failed);}
    Cat(summary,400,T(u". Są na dysku C CharonDOS w folderze APPS; każdy program uruchamiasz w jego folderze.",u". They are on drive C of CharonDOS in the folder APPS; start each program from its own folder."));
    Play(Icon::done);Say(summary);install::HearOut(300);
}
inline void Run() {
    efi_handle volume=nullptr;static char16_t package[pathUnits];
    if(!Package(volume,package,pathUnits)) {
        Fail(T(u"Nie znalazłem pakietu CharonDOS na dysku, na którym można zapisywać; powinien być w folderze EFI, HAK, packages, CharonDOS.",u"I did not find the CharonDOS package on a disk that can be written to; it should be in the folder EFI, HAK, packages, CharonDOS."));install::HearOut(200);return;}
    if(!install::EnsureNetwork()) {install::HearOut(200);return;}
    size_t group=0;if(!AskGroup(group,false))return;
    Fetch(volume,package,group);
}
}  // namespace dosapps
}  // namespace ws
}  // namespace hakefi
#endif
