#pragma once
// r410: the software catalogue in the install dialogue.
//
// The catalogue is DATA: \EFI\HAK\catalogue\charon.ini on Charon's own volume (the tree's
// tools/charon-install/catalogue/charon.ini; format in the specification, section 9.2). Charon reads it
// only to let the user tick single programs of a checked set; the installed system does the installing
// (personal.sh, Charon-Personal.ps1 read the same file from the copy of \EFI\HAK that every installation
// takes along). Without the file the sets can still be checked: the installed system then takes each
// set's default programs, and the dialogue says that single programs cannot be chosen.
// GPL-2.0-or-later.
#include "CharonWsDetailed.h"
namespace hakefi {
namespace ws {
namespace catalogue {
constexpr size_t maxItems=ask::maxBoxes;
struct Item {char id[40];char16_t name[160];bool standard;};
inline Item* Items() {static Item items[maxItems];return items;}
inline bool StartsWith(const char* line,const char* word) {for(;*word;++word,++line)if(*line!=*word)return false;return true;}
// One of the comma-separated words of `list`.
inline bool Listed(const char* list,const char* word) {
    size_t n=0;while(word[n])++n;
    for(const char* p=list;*p;) {const char* e=p;while(*e && *e!=',')++e;
        if(size_t(e-p)==n) {bool same=true;for(size_t i=0;i<n;++i)if(p[i]!=word[i])same=false;if(same)return true;}
        p=*e?e+1:e;}
    return false;
}
inline bool Fits(const char* systems) {
    if(!systems[0])return true;
    switch(detailed::C().path) {
        case 's':return true;   // the shared block of a plan: the programs of every system
        case 'w':return Listed(systems,"windows");
        case 'd':case 'D':return Listed(systems,"linux") || Listed(systems,"debian");
        case 'u':case 'U':return Listed(systems,"linux") || Listed(systems,"ubuntu");
        case 'A':return Listed(systems,"linux") || Listed(systems,"arch");
        case 'F':return Listed(systems,"linux") || Listed(systems,"fedora");
        default:return false;
    }
}
// The programs of one set that exist for the system being installed. 0: none, or no catalogue file.
inline size_t Load(const char* set,bool& fileFound) {
    fileFound=false;
    static FileRef ref;ref.volume=U().host->selfDevice;Set(ref.path,pathUnits,u"\\EFI\\HAK\\catalogue\\charon.ini");
    uint8_t* data=nullptr;size_t size=0;
    if(!ref.volume || ReadWhole(ref,size_t(512)<<10,&data,&size)!=EFI_SUCCESS || !data)return 0;
    fileFound=true;data[size]=0;
    Item* items=Items();size_t count=0;
    static char id[40],itemSet[40],systems[80],nameEn[160],namePl[160],access[16];bool standard=false,open=false;
    auto close=[&]() {
        if(open && ask::Same(itemSet,set) && Fits(systems) && count<maxItems) {
            Item& item=items[count++];size_t i=0;for(;id[i] && i<39;++i)item.id[i]=id[i];item.id[i]=0;item.standard=standard;
            const char* name=Pl() && namePl[0]?namePl:nameEn[0]?nameEn:id;
            item.name[0]=0;
            if(ask::Same(access,"poor"))Set(item.name,160,T(u"słabo dostępny: ",u"poorly accessible: "));
            else if(ask::Same(access,"unknown"))Set(item.name,160,T(u"dostępność nieznana: ",u"accessibility unknown: "));
            const size_t at=Len(item.name);
            Utf8ToUtf16(reinterpret_cast<const uint8_t*>(name),crypt::Length(name),item.name+at,160-at);
        }
        open=false;
    };
    auto copy=[](char* out,size_t cap,const char* from) {size_t i=0;for(;from[i] && i+1<cap;++i)out[i]=from[i];out[i]=0;};
    for(size_t at=0;at<size;) {
        size_t end=at;while(end<size && data[end]!='\n')++end;
        size_t stop=end;while(stop>at && (data[stop-1]=='\r' || data[stop-1]==' '))--stop;
        data[stop]=0;const char* line=reinterpret_cast<const char*>(data+at);
        if(line[0]=='[') {
            close();
            if(StartsWith(line,"[item ")) {size_t i=0;for(const char* p=line+6;*p && *p!=']' && i<39;++p)id[i++]=*p;id[i]=0;
                open=true;itemSet[0]=systems[0]=nameEn[0]=namePl[0]=access[0]=0;standard=false;}
        } else if(open) {
            if(StartsWith(line,"set="))copy(itemSet,sizeof(itemSet),line+4);
            else if(StartsWith(line,"systems="))copy(systems,sizeof(systems),line+8);
            else if(StartsWith(line,"name.en="))copy(nameEn,sizeof(nameEn),line+8);
            else if(StartsWith(line,"name.pl="))copy(namePl,sizeof(namePl),line+8);
            else if(StartsWith(line,"a11y="))copy(access,sizeof(access),line+5);
            else if(StartsWith(line,"default="))standard=ask::Same(line+8,"yes");
        }
        at=end+1;
    }
    close();
    Release(data);
    return count;
}
// The question "<set>: which programs?" (detailed::PickHook). 1: answered, 0: Escape, 2: again.
inline int Pick(const ask::Question& q) {
    const char* set=q.key;while(*set && !(set[0]=='s' && set[1]=='e' && set[2]=='t' && set[3]=='_'))++set;
    if(!*set)return 1;
    set+=4;
    bool fileFound=false;const size_t count=Load(set,fileFound);
    if(!count) {
        Say(fileFound?T(u"Katalog nie ma programów tego zestawu dla tego systemu.",u"The catalogue has no programs of this set for this system.")
                     :T(u"Na nośniku Charona nie ma pliku katalogu programów, więc pojedynczych programów nie można tu wybrać; zainstalowany system weźmie programy domyślne zestawu.",
                        u"Charon's medium has no software catalogue file, so single programs cannot be chosen here; the installed system will take the set's default programs."));
        detailed::Hear(150);return 1;
    }
    Item* items=Items();static ask::Choice choices[maxItems];static bool on[maxItems],standard[maxItems];
    const bool chosen=ask::Get(q.key)[0]!=0;
    for(size_t i=0;i<count;++i) {choices[i]=ask::Choice{items[i].name,items[i].name,items[i].id};standard[i]=items[i].standard;on[i]=chosen?ask::Has(q.key,items[i].id):items[i].standard;}
    static char16_t label[300];Set(label,300,Pl()?q.pl:q.en);
    if(!ask::Pick(label,choices,count,on,standard))return 0;
    static char joined[ask::longUnits];size_t m=0;joined[0]=0;bool same=true;
    for(size_t i=0;i<count;++i) {
        if(on[i]!=standard[i])same=false;
        if(!on[i])continue;
        size_t len=0;while(items[i].id[len])++len;
        if(m+len+2>=ask::longUnits) {Fail(T(u"Zaznaczono więcej programów, niż mieści jedna odpowiedź.",u"More programs are checked than one answer can hold."));return 2;}
        if(m)joined[m++]=',';
        for(size_t c=0;c<len;++c)joined[m++]=items[i].id[c];joined[m]=0;
    }
    // the set's own defaults are the empty answer (nothing travels); nothing ticked at all is the answer "none"
    return ask::Keep(q.key,same?"":m?joined:"none")?1:2;
}
// r410 (detailed::NamesHook): the value of a "<set>: which programs?" row in words - the names of the checked
// programs from the catalogue; "the set's default programs" for the empty answer, "none" for none. False: no
// catalogue (the caller says the stored text).
inline bool Names(const ask::Question& q,char16_t* out,size_t cap) {
    const char* set=q.key;while(*set && !(set[0]=='s' && set[1]=='e' && set[2]=='t' && set[3]=='_'))++set;
    if(!*set)return false;
    set+=4;const char* v=ask::Get(q.key);
    if(!v[0]) {Set(out,cap,T(u"programy domyślne zestawu",u"the set's default programs"));return true;}
    if(ask::Same(v,"none")) {Set(out,cap,T(u"żaden program",u"no program"));return true;}
    bool fileFound=false;const size_t count=Load(set,fileFound);
    if(!count)return false;
    Item* items=Items();out[0]=0;bool any=false;
    for(size_t i=0;i<count;++i)if(Listed(v,items[i].id)) {if(any)Cat(out,cap,u", ");Cat(out,cap,items[i].name);any=true;}
    if(!any)return false;
    return true;
}
}  // namespace catalogue
}  // namespace ws
}  // namespace hakefi
