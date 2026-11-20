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
// One program. False: said why.
inline bool Install(const doscatalog::Program& p,efi_handle volume,const char16_t* package) {
    auto& d=D();static char16_t url[400];url[0]=0;CatAscii(url,400,p.url);
    {size_t n=0;for(;p.url[n] && n<399;++n)url[n]=char16_t(uint8_t(p.url[n]));url[n]=0;}   // an address keeps its slashes
    static FileRef none;none.volume=nullptr;none.path[0]=0;uint64_t received=0;
    d.archive.data=nullptr;d.archive.capacity=0;d.archive.size=0;d.archive.extra=0;d.archive.reserved=false;
    if(!net::DownloadAny(url,none,&received,&d.archive,Pl()?p.namePl:p.nameEn))return false;
    bool ok=false;const char16_t* why=T(u"Archiwum jest uszkodzone.",u"The archive is damaged.");
    {   // the very file that was tested
        Sha256 hash;hash.Update(d.archive.data,d.archive.size);uint8_t got[32];hash.Final(got);static const char hex[]="0123456789abcdef";
        bool same=true;for(int i=0;i<32 && same;++i)same=p.sha256[i*2]==hex[got[i]>>4] && p.sha256[i*2+1]==hex[got[i]&15];
        if(!same) {Free(d.archive);Fail(T(u"Plik na serwerze jest inny niż ten, który sprawdzono w CharonDOS. Nie zainstalowano.",u"The file on the server differs from the one tested in CharonDOS. Not installed."));return false;}
    }
    static uint8_t scratch[65536+64];static uint8_t directory[8192];static uint8_t input[65536];static uint8_t window[32768];static inflate::Decoder decoder;
    zip::Source source{nullptr,Read};zip::Archive archive{};
    if(zip::Open(source,d.archive.size,scratch,sizeof(scratch),archive)==zip::Error::None) {
        ok=true;
        for(const doscatalog::File* f=p.files;ok && f->zipPath;++f) {
            zip::Cursor cursor{};zip::Begin(cursor,archive,directory,sizeof(directory));zip::Entry entry{};bool found=false;
            while(zip::Next(cursor,entry)==zip::Error::None)if(zip::NameEquals(entry,f->zipPath)) {found=true;break;}
            if(!found || entry.uncompressedSize>(uint64_t(8)<<20)) {ok=false;why=T(u"W archiwum brakuje pliku z katalogu.",u"A file of the catalog is missing from the archive.");break;}
            static Memory file;file.reserved=false;file.extra=0;
            if(!Reserve(file,size_t(entry.uncompressedSize)+16)) {ok=false;why=T(u"Brak pamięci.",u"Out of memory.");break;}
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
    Free(d.archive);
    if(!ok)Fail(why);
    return ok;
}
// r403: the program groups; the installer of CharonDOS asks the same question.
inline const ask::Choice* Groups(size_t& n) {
    static const ask::Choice groups[]={{u"wszystkie sprawdzone programy",u"all the tested programs",""},{u"edytory",u"editors","edit"},{u"pliki i foldery",u"files and folders","files"},
        {u"przeglądanie tekstu i pomoc",u"text viewing and help","view"},{u"narzędzia",u"tools","tools"},{u"archiwa",u"archivers","archive"},{u"programowanie",u"development","devel"},{u"gry",u"games","games"},
        {u"bez programów",u"no programs","none"}};
    n=sizeof(groups)/sizeof(groups[0]);return groups;
}
inline bool AskGroup(size_t& group,bool allowNone) {
    size_t n=0;const ask::Choice* groups=Groups(n);if(!allowNone)--n;
    const size_t count=sizeof(doscatalog::kPrograms)/sizeof(doscatalog::kPrograms[0]);
    static char16_t text[300];Set(text,300,T(u"CharonDOS pobierze darmowe programy DOS z oficjalnego repozytorium FreeDOS. W katalogu jest ",u"CharonDOS fetches free DOS programs from the official FreeDOS repository. The catalog holds "));
    CatN(text,300,count);Cat(text,300,T(u" programów sprawdzonych w CharonDOS.",u" programs tested in CharonDOS."));Say(text);install::HearOut(150);
    group=0;return ask::Choose(T(u"Które programy pobrać?",u"Which programs to fetch?"),groups,n,group);
}
// Fetches group `group` into <package>\C on `volume` (network must be up).
inline void Fetch(efi_handle volume,const char16_t* package,size_t group) {
    size_t n=0;const ask::Choice* groups=Groups(n);
    if(group>=n || ask::Same(groups[group].value,"none"))return;
    const size_t count=sizeof(doscatalog::kPrograms)/sizeof(doscatalog::kPrograms[0]);
    size_t done=0,failed=0,wanted=0;
    for(size_t i=0;i<count;++i)if(!groups[group].value[0] || ask::Same(doscatalog::kPrograms[i].group,groups[group].value))++wanted;
    for(size_t i=0;i<count;++i) {
        const auto& p=doscatalog::kPrograms[i];
        if(groups[group].value[0] && !ask::Same(p.group,groups[group].value))continue;
        if(EscapePressed()) {Say(T(u"Przerwano.",u"Stopped."));break;}
        static char16_t text[300];Set(text,300,Pl()?p.namePl:p.nameEn);Cat(text,300,u", ");CatN(text,300,done+failed+1);Cat(text,300,T(u" z ",u" of "));CatN(text,300,wanted);Say(text);
        if(Install(p,volume,package))++done;else ++failed;
    }
    static char16_t summary[400];Set(summary,400,T(u"Zainstalowano programów: ",u"Programs installed: "));CatN(summary,400,done);
    if(failed) {Cat(summary,400,T(u", nie udało się: ",u", failed: "));CatN(summary,400,failed);}
    Cat(summary,400,T(u". Są na dysku C CharonDOS w folderze APPS; każdy program uruchamiasz w jego folderze.",u". They are on CharonDOS drive C in the folder APPS; start each program in its own folder."));
    Play(Icon::done);Say(summary);install::HearOut(300);
}
inline void Run() {
    efi_handle volume=nullptr;static char16_t package[pathUnits];
    if(!Package(volume,package,pathUnits)) {
        Fail(T(u"Nie znalazłem pakietu CharonDOS na dysku, na którym da się pisać: folder EFI, HAK, packages, CharonDOS.",u"I did not find the CharonDOS package on a writable disk: the folder EFI, HAK, packages, CharonDOS."));install::HearOut(200);return;}
    if(!install::EnsureNetwork()) {install::HearOut(200);return;}
    size_t group=0;if(!AskGroup(group,false))return;
    Fetch(volume,package,group);
}
}  // namespace dosapps
}  // namespace ws
}  // namespace hakefi
#endif
