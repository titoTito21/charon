#pragma once
// r410: saved answer sets, \EFI\HAK\answers\<name>.ini on Charon's own volume.
//
// The text of a set is made and read by CharonWsDetailed.h (SetText, LoadSet); this file only
// knows the folder. Passwords: by default a set says "ask" for each and the dialogue asks them
// again; a file may hold them in clear text when its author typed the confirmation word, and
// loading such a file says so. The folder is on Charon's private list (CharonPrivate.h): it is
// carried over a wipe of Charon's own disk and never copied into an installed system.
// GPL-2.0-or-later.
#include "CharonWsDetailed.h"
#include "CharonWsCatalogue.h"   // the software catalogue (single programs of a set)
namespace hakefi {
namespace ws {
namespace sets {
inline const char16_t* Folder() {return u"\\EFI\\HAK\\answers";}
inline void Save() {
    // Said BEFORE the name and the password question (seen in VMware on a disc: it came after them).
    {const efi_handle medium=U().host->selfDevice;
     if(!medium || VolumeReadOnly(medium)) {Fail(T(u"Na nośniku Charona nie można zapisywać, więc zestawu nie da się zapisać.",u"Charon's medium cannot be written to, so a set cannot be saved."));return;}}
    static char16_t name[32];name[0]=0;
    if(!Prompt(T(u"Nazwa zestawu, małe litery i cyfry:",u"Name of the set, lower-case letters and digits:"),name,31) || !name[0])return;
    static char narrow[32];size_t n=0;bool good=true;
    for(;name[n];++n) {const char16_t c=speechtext::LowerCase(name[n]);if(!((c>=u'a' && c<=u'z') || (c>=u'0' && c<=u'9') || c==u'-'))good=false;name[n]=c;narrow[n]=char(c);}
    narrow[n]=0;
    if(!good) {Fail(T(u"W nazwie zestawu dozwolone są małe litery bez polskich znaków, cyfry i myślnik.",u"Only lower-case letters without accents, digits and the hyphen are allowed in the name of a set."));return;}
    static const ask::Choice passwords[]={{u"nie, pytaj o nie przy wczytaniu",u"no, ask for them when the set is loaded","no"},{u"tak, zapisz je jawnym tekstem",u"yes, store them in clear text","yes"}};
    size_t with=0;
    if(!ask::Choose(T(u"Zapisać w pliku także hasła?",u"Store the passwords in the file too?"),passwords,2,with))return;
    if(with==1 && !ask::ConfirmRisk(T(u"Hasła będą stały w pliku jawnym tekstem. Każdy, kto dostanie ten nośnik, może je przeczytać.",
                                      u"The passwords will stand in the file in clear text. Anyone who gets this medium can read them.")))return;
    const efi_handle volume=U().host->selfDevice;
    if(!volume || VolumeReadOnly(volume)) {Fail(T(u"Na nośniku Charona nie można zapisywać, więc zestaw nie został zapisany.",u"Charon's medium cannot be written to, so the set was not saved."));return;}
    static char text[32768];const size_t size=detailed::SetText(detailed::Main(),narrow,with==1,text,sizeof(text));
    if(!size) {Fail(T(u"Odpowiedzi nie mieszczą się w pliku zestawu. Nic nie zapisano.",u"The answers do not fit in a set file. Nothing was saved."));return;}
    static FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,Folder());
    MakeFolders(volume,ref.path);
    Cat(ref.path,pathUnits,u"\\");Cat(ref.path,pathUnits,name);Cat(ref.path,pathUnits,u".ini");
    const bool ok=WriteWhole(ref,reinterpret_cast<const uint8_t*>(text),size)==EFI_SUCCESS;
    for(auto& c:text)c=0;
    if(!ok) {Fail(T(u"Nie udało się zapisać pliku zestawu.",u"The set file could not be written."));return;}
    static char16_t said[200];Set(said,200,T(u"Zapisano zestaw odpowiedzi ",u"Saved the answer set "));Cat(said,200,name);Cat(said,200,u".");
    Play(Icon::done);Say(said);detailed::Hear(100);
}
// r410 (es): the SITE file \EFI\HAK\answers\site\site.ini (private: never copied into an installed system). Its answers
// become this site's starting values of every detailed dialogue: [shared], [system.<kind>] of this system, and the
// first [machine serial=...] section whose serial is this computer's SMBIOS system serial number (specification 6.4).
// Keys as "shared.key" / "system.key" are taken too. For Windows a Linux key that has a Windows row of the name w_<key>
// goes there. A key of the administration layer opens its groups. A saved set loaded after it overrides it. The same
// value rules as a saved set: unknown keys and values are counted, never applied; secrets only from [secrets].
struct SiteResult {size_t applied,unknown,bad,machine;bool admin;};
inline bool MachineSerial(char* out,size_t cap) {
    out[0]=0;
    const systools::SmbiosTable table=systools::FindSmbios(U().host->system);
    systools::WalkSmbios(table,[&](uint8_t type,const uint8_t* s,uint8_t length,const char* strings,const char* end) {
        if(type!=1 || length<8 || out[0])return;
        const char* text=nullptr;size_t n=0;
        if(systools::SmbiosString(strings,end,s[7],text,n)) {size_t k=0;for(;k<n && k+1<cap;++k)out[k]=text[k];out[k]=0;}
    });
    return out[0]!=0;
}
inline void PutSiteKey(const char* section,const char* key,const char* value,SiteResult& r) {
    (void)section;
    {const char* a="shared.";const char* b="system.";size_t i=0;while(i<7 && key[i]==a[i])++i;
     if(i==7)key+=7;else {i=0;while(i<7 && key[i]==b[i])++i;if(i==7)key+=7;}}   // shared.key, system.key
    const detailed::MainTables& main=detailed::Main();
    const ask::Question* q=detailed::FindKey(main,key);
    static char wide[64];
    if(!q && detailed::C().family=='w') {size_t n=0;wide[n++]='w';wide[n++]='_';for(size_t i=0;key[i] && n+1<sizeof(wide);++i)wide[n++]=key[i];wide[n]=0;q=detailed::FindKey(main,wide);}
    if(!q) {++r.unknown;return;}
    const bool secret=q->kind==ask::Kind::secret || (q->flags&ask::flagSecret);
    if(secret!=ask::Same(section,"secrets")) {++r.unknown;return;}
    if(!secret && !detailed::ValueFits(*q,value)) {++r.bad;return;}
    if(!ask::Put(q->key,value)) {++r.bad;return;}
    ++r.applied;if(q->level)r.admin=true;
}
inline void LoadSite() {
    const efi_handle volume=U().host->selfDevice;if(!volume)return;
    static FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,Folder());Cat(ref.path,pathUnits,u"\\site\\site.ini");
    uint8_t* data=nullptr;size_t size=0;
    if(ReadWhole(ref,65536,&data,&size)!=EFI_SUCCESS || !data)return;   // no site file: nothing to say
    data[size]=0;char* text=reinterpret_cast<char*>(data);
    static char serial[80];MachineSerial(serial,sizeof(serial));
    SiteResult r{0,0,0,0,false};bool header=false,formatOk=false,machineTaken=false,inMachine=false;
    static char section[96];section[0]=0;
    static char kind[40];{const char* head="system.";size_t n=0;for(;head[n];++n)kind[n]=head[n];for(const char* k=detailed::KindName();*k && n+1<sizeof(kind);++k)kind[n++]=*k;kind[n]=0;}
    for(size_t at=0;at<size;) {
        size_t end=at;while(end<size && text[end]!='\n')++end;
        size_t stop=end;while(stop>at && (text[stop-1]=='\r' || text[stop-1]==' ' || text[stop-1]=='\t'))--stop;
        text[stop<size?stop:size]=0;char* line=text+at;while(*line==' ' || *line=='\t')++line;
        if(*line=='[') {
            size_t n=0;for(const char* p=line+1;*p && *p!=']' && n+1<sizeof(section);++p)section[n++]=*p;section[n]=0;
            if(ask::Same(section,"charon-answers"))header=true;
            inMachine=false;
            if(section[0]=='m' && section[1]=='a' && section[2]=='c' && section[3]=='h') {   // [machine serial=X]
                const char* v=section;while(*v && *v!='=')++v;
                if(!machineTaken && serial[0] && *v=='=' && ask::Same(v+1,serial)) {inMachine=true;machineTaken=true;}
            }
        } else if(*line && *line!='#' && *line!=';') {
            char* eq=line;while(*eq && *eq!='=')++eq;
            if(*eq=='=') {
                *eq=0;const char* key=line;const char* value=eq+1;
                if(ask::Same(section,"charon-answers") && ask::Same(key,"format"))formatOk=ask::Same(value,"1");
                else if(header && formatOk && (ask::Same(section,"shared") || ask::Same(section,kind) || ask::Same(section,"secrets") || inMachine)) {
                    const size_t before=r.applied;PutSiteKey(inMachine?"shared":section,key,value,r);if(inMachine && r.applied>before)++r.machine;}
                *eq='=';
            }
        }
        at=end+1;
    }
    for(size_t i=0;i<size;++i)data[i]=0;
    Release(data);
    static char16_t said[600];
    if(!header || !formatOk) {Set(said,600,T(u"Plik ustawień miejsca site.ini nie jest zestawem odpowiedzi w formacie 1. Nic z niego nie wczytano.",u"The site file site.ini is not an answer set of format 1. Nothing was taken from it."));Say(said);detailed::Hear(200);return;}
    if(r.admin)ask::AdminOpen()=true;
    Set(said,600,T(u"Ustawienia miejsca: wczytano odpowiedzi jako wartości początkowe: ",u"Site settings: answers taken as the starting values: "));CatN(said,600,r.applied);Cat(said,600,u". ");
    if(r.machine) {Cat(said,600,T(u"Z tego dla tego komputera według numeru seryjnego: ",u"Of these for this computer by its serial number: "));CatN(said,600,r.machine);Cat(said,600,u". ");}
    if(r.unknown) {Cat(said,600,T(u"Pominięte, bo nie dotyczą tego systemu: ",u"Ignored, because they do not apply to this system: "));CatN(said,600,r.unknown);Cat(said,600,u". ");}
    if(r.bad) {Cat(said,600,T(u"Z nieznaną wartością, zostają domyślne: ",u"With a value this version does not know, kept at their defaults: "));CatN(said,600,r.bad);Cat(said,600,u". ");}
    if(r.admin)Cat(said,600,T(u"Grupy administracyjne są otwarte. ",u"The administration groups are open. "));
    Say(said);detailed::Hear(300);
}
// r410 (es): is the user's own list file \EFI\HAK\catalogue\software.txt on the medium?
inline void FindSoftFile() {
    const efi_handle volume=U().host->selfDevice;if(!volume)return;
    static FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,u"\\EFI\\HAK\\catalogue\\software.txt");
    uint8_t* data=nullptr;size_t size=0;
    if(ReadWhole(ref,262144,&data,&size)==EFI_SUCCESS && data) {detailed::SoftFileOn()=size!=0;Release(data);}
}
// Offers the saved sets (when there are any) before a detailed dialogue. False: Escape.
inline bool Offer() {
    detailed::SaveHook()=Save;detailed::PickHook()=catalogue::Pick;detailed::NamesHook()=catalogue::Names;
    const efi_handle volume=U().host->selfDevice;if(!volume)return true;
    FindSoftFile();LoadSite();
    static FileRef folder;folder.volume=volume;Set(folder.path,pathUnits,Folder());
    static Listing list;if(ReadFolder(folder,list)!=EFI_SUCCESS)return true;
    static ask::Choice choices[33];static char16_t names[32][nameUnits];size_t count=0;
    choices[count++]=ask::Choice{u"nie wczytuj zestawu",u"do not load a set",""};
    for(size_t i=0;i<list.count && count<33;++i) {
        const Entry& e=list.entries[i];const size_t len=Len(e.name);
        if(e.directory || len<5 || !SameFold(e.name+len-4,u".ini"))continue;
        Set(names[count-1],nameUnits,e.name);names[count-1][len-4]=0;
        choices[count]=ask::Choice{names[count-1],names[count-1],""};++count;
    }
    if(count==1)return true;
    size_t index=0;
    if(!ask::Choose(T(u"Wczytać zapisany zestaw odpowiedzi?",u"Load a saved answer set?"),choices,count,index))return false;
    if(!index)return true;
    static FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,Folder());Cat(ref.path,pathUnits,u"\\");Cat(ref.path,pathUnits,choices[index].pl);Cat(ref.path,pathUnits,u".ini");
    uint8_t* data=nullptr;size_t size=0;
    if(ReadWhole(ref,65536,&data,&size)!=EFI_SUCCESS || !data) {Fail(T(u"Nie udało się odczytać pliku zestawu.",u"The set file could not be read."));return true;}
    data[size]=0;
    detailed::Loaded result;const bool ok=detailed::LoadSet(detailed::Main(),reinterpret_cast<char*>(data),size,result);
    for(size_t i=0;i<size;++i)data[i]=0;
    Release(data);
    if(!ok) {Fail(T(u"To nie jest zestaw odpowiedzi, który ta wersja Charona potrafi wczytać. Nic nie zostało wczytane.",u"This is not an answer set this version of Charon can read. Nothing was loaded."));return true;}
    static char16_t text[700];Set(text,700,T(u"Wczytano odpowiedzi: ",u"Answers loaded: "));CatN(text,700,result.applied);Cat(text,700,u". ");
    if(result.otherSystem)Cat(text,700,T(u"Zestaw był zrobiony dla innego systemu. ",u"The set was made for another system. "));
    if(result.unknown) {Cat(text,700,T(u"Odpowiedzi, które nie dotyczą tego systemu i zostały pominięte: ",u"Answers that do not apply to this system and were ignored: "));CatN(text,700,result.unknown);Cat(text,700,u". ");}
    if(result.bad) {Cat(text,700,T(u"Odpowiedzi o wartości, której ta wersja nie zna, zostają domyślne: ",u"Answers with a value this version does not know keep their defaults: "));CatN(text,700,result.bad);Cat(text,700,u". ");}
    if(result.secretsInside)Cat(text,700,T(u"Uwaga: ten plik zawiera hasła jawnym tekstem. ",u"Note: this file holds passwords in clear text. "));
    Cat(text,700,T(u"Pytania pokażą wczytane wartości; Enter je zatwierdza. O hasła zapytam ponownie.",u"The questions will show the loaded values; Enter keeps them. I will ask for the passwords again."));
    Say(text);detailed::Hear(300);
    return true;
}
}  // namespace sets
}  // namespace ws
}  // namespace hakefi
