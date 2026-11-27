// The entries of a Windows Boot Configuration Data store, read only (r410).
// GPL-2.0-or-later.
//
// Lists what Windows Boot Manager would show: the objects of its display
// order with their descriptions, which one is the default, each loader's menu
// policy, then the tools (Windows Memory Diagnostic), and on request every
// other application object of the store. The input is the BCD file's bytes
// (\EFI\Microsoft\Boot\BCD, a registry hive); the caller reads the file.
//
// READ ONLY: there is no code here that changes a hive, and none may be
// added. Every offset is checked against the buffer before it is used; a
// damaged or cut-off hive gives fewer entries or none, never a read outside
// [hive,hive+size). The cell reader is bcd::Hive of CharonBcd.h (r389).
//
// Sources (checked 2026-11-24):
//  https://learn.microsoft.com/en-us/previous-versions/windows/desktop/bcd/bcdlibraryelementtypes
//    BcdLibraryString_ApplicationPath 0x12000002, BcdLibraryString_Description
//    0x12000004 ("Display name of the boot environment application").
//  https://learn.microsoft.com/en-us/previous-versions/windows/desktop/bcd/bcdbootmgrelementtypes
//    BcdBootMgrObjectList_DisplayOrder 0x24000001, BcdBootMgrObject_DefaultObject
//    0x23000003, BcdBootMgrInteger_Timeout 0x25000004 (seconds),
//    BcdBootMgrObjectList_ToolsDisplayOrder 0x24000010,
//    BcdBootMgrBoolean_DisplayBootMenu 0x26000020 ("Forces the display of the
//    legacy boot menu, regardless of the number of OS entries in the BCD store
//    and their BcdOSLoaderInteger_BootMenuPolicy").
//  https://learn.microsoft.com/en-us/previous-versions/windows/desktop/bcd/bcdosloaderelementtypes
//    BcdOSLoaderInteger_BootMenuPolicy 0x250000C2: menupolicylegacy (0),
//    menupolicystandard (1); "The default value is menupolicylegacy (0)".
//  https://www.geoffchappell.com/notes/windows/boot/bcd/objects.htm
//    each object's GUID names a subkey of Objects; the type is the DWORD Type
//    in its Description subkey; 0xF0000000 object class (1 application,
//    2 inherit, 3 device); 0x10100001 {fwbootmgr}, 0x10100002 {bootmgr},
//    0x10200003 osloader, 0x10200004 resume, 0x10200005 {memdiag},
//    0x10300006 {ntldr}, 0x10400008 bootsector, 0x10400009 startup,
//    0x1020000A bootapp; {bootmgr} = {9DEA862C-5CDD-4E70-ACC1-F32B344D4795},
//    {memdiag} = {B2721D73-1DB4-4C62-BF78-C548A880142D}.
//  https://www.geoffchappell.com/notes/windows/boot/bcd/elements.htm
//    element numbers and formats (string, GUID, GUID list, integer, boolean).
//  https://raw.githubusercontent.com/msuhanov/regf/master/Windows%20registry%20file%20format%20specification.md
//    regf base block (root cell offset at 0x24), cells, nk / vk records and
//    the li / lf / lh / ri subkey lists.
// Not stated on those pages, taken from the stores read (work/bcd-menu/BCD,
// the DVD template of Windows) and from CharonBcd.h: the key layout
// Objects\{guid}\Elements\<8 hex digits> with a value named "Element"; strings
// as REG_SZ, GUID lists as REG_MULTI_SZ, integers and booleans as REG_BINARY
// (8 bytes and 1 byte).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "CharonBcd.h"
namespace hakefi::bcdlist {
constexpr uint32_t typeFirmwareBootManager=0x10100001,typeBootManager=0x10100002,typeOsLoader=0x10200003,typeResume=0x10200004,
    typeMemoryDiagnostic=0x10200005,typeNtldr=0x10300006,typeBootSector=0x10400008,typeStartup=0x10400009,typeBootApplication=0x1020000a;
constexpr uint8_t notListed=0xff,policyNotSet=0xff,policyLegacy=0,policyStandard=1;
struct Entry {
    char16_t guid[40]={};          // "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" as written in the store
    char16_t description[96]={};   // 12000004
    char16_t path[128]={};         // 12000002, e.g. \Windows\system32\winload.efi
    bool isDefault=false;          // named by the boot manager's 23000003
    bool inMenu=false;             // in the display order (24000001)
    bool tool=false;               // in the tools display order (24000010)
    uint8_t order=notListed;       // position in its list, from 0
    uint8_t menuPolicy=policyNotSet;   // 250000C2: 0 legacy (text menu), 1 standard
    uint32_t type=0;               // Description\Type, 0 when unreadable
};
struct Store {
    bool found=false;              // the boot manager object {9dea862c-...} exists
    long timeout=-1;               // 25000004, seconds; -1 not set
    int8_t displayBootMenu=-1;     // 26000020: 1 yes, 0 no, -1 not set
    char16_t defaultGuid[40]={};   // 23000003 as written
    size_t menuCount=0,toolCount=0;
};
namespace detail {
using bcd::Hive;
constexpr uint32_t none=0xffffffffu;
// A REG_SZ / one REG_MULTI_SZ item (UTF-16LE) into out; control characters become spaces.
inline size_t Text(const uint8_t* v,size_t bytes,char16_t* out,size_t capacity) {
    size_t n=0;
    for(size_t i=0;i+1<bytes && n+1<capacity;i+=2) {const uint16_t c=Hive::U16(v+i);if(!c)break;out[n++]=char16_t(c<32?u' ':c);}
    out[n]=0;return n;
}
inline uint32_t Element(const Hive& h,uint32_t elements,const char* number,const uint8_t*& value,uint32_t& type,size_t& bytes) {
    value=nullptr;type=0;bytes=0;
    if(elements==none)return none;
    const uint32_t key=h.Subkey(elements,number);
    if(key==none)return none;
    value=h.Value(key,"Element",type,bytes);
    return key;
}
inline bool String(const Hive& h,uint32_t elements,const char* number,char16_t* out,size_t capacity) {
    const uint8_t* v=nullptr;uint32_t type=0;size_t bytes=0;
    out[0]=0;Element(h,elements,number,v,type,bytes);
    return v && type==1 && Text(v,bytes,out,capacity)>0;
}
inline bool Integer(const Hive& h,uint32_t elements,const char* number,uint64_t& out) {
    const uint8_t* v=nullptr;uint32_t type=0;size_t bytes=0;
    Element(h,elements,number,v,type,bytes);
    if(!v || type!=3 || bytes<1 || bytes>8)return false;
    out=0;for(size_t i=bytes;i--;)out=out<<8|v[i];
    return true;
}
inline uint32_t ObjectType(const Hive& h,uint32_t object) {
    const uint32_t description=h.Subkey(object,"Description");
    if(description==none)return 0;
    uint32_t type=0;size_t bytes=0;
    const uint8_t* v=h.Value(description,"Type",type,bytes);
    return v && type==4 && bytes==4?Hive::U32(v):0;
}
inline bool SameGuid(const char16_t* a,const char16_t* b) {
    for(;*a && *b;++a,++b) {
        char16_t x=*a,y=*b;
        if(x>=u'A' && x<=u'Z')x=char16_t(x+32);
        if(y>=u'A' && y<=u'Z')y=char16_t(y+32);
        if(x!=y)return false;
    }
    return !*a && !*b;
}
// "{...}" as ASCII for Hive::Subkey; false when it is not plain ASCII.
inline bool Ascii(const char16_t* guid,char* out,size_t capacity) {
    size_t n=0;
    for(;guid[n];++n) {if(guid[n]>=127 || guid[n]<=32 || n+1>=capacity)return false;out[n]=char(guid[n]);}
    out[n]=0;return n>0;
}
inline void Fill(const Hive& h,uint32_t object,const char16_t* guid,const Store& store,Entry& e) {
    e=Entry{};
    size_t n=0;for(;guid[n] && n+1<40;++n)e.guid[n]=guid[n];e.guid[n]=0;
    e.type=ObjectType(h,object);
    const uint32_t elements=h.Subkey(object,"Elements");
    String(h,elements,"12000004",e.description,96);
    String(h,elements,"12000002",e.path,128);
    uint64_t policy=0;
    if(Integer(h,elements,"250000c2",policy) && policy<=1)e.menuPolicy=uint8_t(policy);
    e.isDefault=store.defaultGuid[0] && SameGuid(store.defaultGuid,e.guid);
}
inline bool Listed(const Entry* out,size_t count,const char16_t* guid) {
    for(size_t i=0;i<count;++i)if(SameGuid(out[i].guid,guid))return true;
    return false;
}
// The objects named by a GUID list element, appended to out.
inline size_t List(const Hive& h,uint32_t objects,uint32_t elements,const char* number,bool tool,const Store& store,Entry* out,size_t max,size_t count) {
    const uint8_t* list=nullptr;uint32_t type=0;size_t bytes=0;
    Element(h,elements,number,list,type,bytes);
    if(!list || type!=7)return count;
    size_t position=0;
    for(size_t at=0;at+1<bytes && position<255;++position) {
        char16_t guid[40];size_t n=0;
        while(at+1<bytes && Hive::U16(list+at)) {if(n+1<40)guid[n++]=char16_t(Hive::U16(list+at));at+=2;}
        at+=2;guid[n]=0;
        if(!n)break;
        char name[40];
        if(!Ascii(guid,name,sizeof(name)) || Listed(out,count,guid))continue;
        const uint32_t object=h.Subkey(objects,name);
        if(object==none || count>=max)continue;
        Fill(h,object,guid,store,out[count]);
        out[count].order=uint8_t(position);out[count].inMenu=!tool;out[count].tool=tool;
        ++count;
    }
    return count;
}
// Calls `each(keyCell,name)` for the subkeys of `key` (li / lf / lh lists,
// ri one level deep); at most `limit` subkeys.
template<class Each> void Subkeys(const Hive& h,uint32_t list,size_t& budget,int depth,Each each) {
    size_t n=0;const uint8_t* l=h.Cell(list,n);
    if(!l || n<4 || depth>2)return;
    const size_t count=Hive::U16(l+2);
    const bool ri=l[0]=='r' && l[1]=='i',li=l[0]=='l' && l[1]=='i',lf=l[0]=='l' && (l[1]=='f' || l[1]=='h');
    if(!ri && !li && !lf)return;
    const size_t step=lf?8:4;
    if(4+count*step>n)return;
    for(size_t i=0;i<count && budget;++i) {
        const uint32_t child=Hive::U32(l+4+i*step);
        if(ri) {Subkeys(h,child,budget,depth+1,each);continue;}
        --budget;
        size_t m=0;const uint8_t* nk=h.Cell(child,m);
        if(!nk || m<76 || nk[0]!='n' || nk[1]!='k')continue;
        const size_t length=Hive::U16(nk+72);const bool ascii=(Hive::U16(nk+2)&0x20)!=0;
        if(76+length>m)continue;
        char16_t name[40];size_t k=0;
        const size_t characters=ascii?length:length/2;
        for(;k<characters && k+1<40;++k)name[k]=ascii?char16_t(nk[76+k]):char16_t(Hive::U16(nk+76+2*k));
        name[k]=0;
        if(characters<40)each(child,name);
    }
}
}  // namespace detail
// Fills `out` with at most `max` entries and returns their number: first the
// display order, then the tools, then (all=true) every other application
// object except the boot managers themselves (order = notListed). `store`
// (may be nullptr) receives the boot manager's own settings.
inline size_t ListEntries(const uint8_t* hive,size_t size,Entry* out,size_t max,Store* store=nullptr,bool all=false) {
    using namespace detail;
    Store local;Store& s=store?*store:local;s=Store{};
    if(!hive || !out || size<4096+8)return 0;
    const Hive h{hive,size};
    const uint32_t root=h.Root();if(root==none)return 0;
    const uint32_t objects=h.Subkey(root,"Objects");if(objects==none)return 0;
    size_t count=0;
    const uint32_t manager=h.Subkey(objects,"{9dea862c-5cdd-4e70-acc1-f32b344d4795}");
    if(manager!=none) {
        s.found=true;
        const uint32_t elements=h.Subkey(manager,"Elements");
        String(h,elements,"23000003",s.defaultGuid,40);
        uint64_t number=0;
        if(Integer(h,elements,"25000004",number) && number<=0x7fffffff)s.timeout=long(number);
        if(Integer(h,elements,"26000020",number))s.displayBootMenu=number?1:0;
        count=List(h,objects,elements,"24000001",false,s,out,max,count);s.menuCount=count;
        count=List(h,objects,elements,"24000010",true,s,out,max,count);s.toolCount=count-s.menuCount;
    }
    if(all) {
        size_t n=0;const uint8_t* nk=h.Cell(objects,n);
        if(nk && n>=76 && nk[0]=='n' && nk[1]=='k') {
            size_t budget=1024;
            Subkeys(h,Hive::U32(nk+28),budget,0,[&](uint32_t object,const char16_t* guid) {
                if(count>=max || guid[0]!=u'{' || Listed(out,count,guid))return;
                const uint32_t type=ObjectType(h,object);
                if(type>>28!=1 || type==typeBootManager || type==typeFirmwareBootManager)return;
                Fill(h,object,guid,s,out[count]);++count;
            });
        }
    }
    return count;
}
// What kind of program an object type is, for speech; nullptr when unknown.
inline const char16_t* TypeName(uint32_t type,bool polish) {
    switch(type) {
    case typeOsLoader:return polish?u"program ładujący Windows":u"Windows boot loader";
    case typeResume:return polish?u"wznowienie z hibernacji":u"resume from hibernation";
    case typeMemoryDiagnostic:return polish?u"test pamięci":u"memory test";
    case typeNtldr:return polish?u"starszy program ładujący Windows":u"legacy Windows loader";
    case typeBootSector:return polish?u"sektor rozruchowy":u"boot sector";
    case typeStartup:return polish?u"moduł startowy":u"startup module";
    case typeBootApplication:return polish?u"aplikacja rozruchowa":u"boot application";
    case typeBootManager:return polish?u"menedżer rozruchu Windows":u"Windows Boot Manager";
    case typeFirmwareBootManager:return polish?u"menedżer rozruchu oprogramowania układowego":u"firmware boot manager";
    default:return nullptr;
    }
}
}  // namespace hakefi::bcdlist
