// Windows Boot Configuration Data, read only (r389). GPL-2.0-or-later.
//
// The BCD store (\EFI\Microsoft\Boot\BCD) is a registry hive ("regf"):
// Objects\{GUID}\Elements\{type}\Element. Charon reads the Windows Boot
// Manager object {9dea862c-5cdd-4e70-acc1-f32b344d4795}: display order
// (24000001), default entry (23000003), timeout (25000004), and the
// description (12000004) of each listed entry, so the device list can say what
// the Windows Boot Manager on a disk offers before it is started. Nothing is
// ever written; a damaged or unusual hive just gives no information. Every
// offset is bounds-checked against the file read into memory.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hakefi {
namespace bcd {
struct Hive {
    const uint8_t* data;size_t size;
    static uint32_t U32(const uint8_t* p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
    static uint16_t U16(const uint8_t* p) {return uint16_t(p[0]|p[1]<<8);}
    // A cell's data (after its size field), with its usable length.
    const uint8_t* Cell(uint32_t offset,size_t& length) const {
        const uint64_t at=uint64_t(offset)+4096;
        if(offset==0xffffffffu || at+4>size)return nullptr;
        const int32_t raw=int32_t(U32(data+at));
        const uint64_t cell=raw<0?uint64_t(-int64_t(raw)):uint64_t(raw);
        if(cell<8 || at+cell>size)return nullptr;
        length=size_t(cell-4);return data+at+4;
    }
    static bool SameName(const uint8_t* name,size_t length,bool ascii,const char* want) {
        size_t i=0;
        for(;i<length;++i) {
            const uint16_t c=ascii?name[i]:U16(name+2*i);
            if(!want[i])return false;
            uint16_t a=c,b=uint8_t(want[i]);
            if(a>='A' && a<='Z')a=uint16_t(a+32);if(b>='A' && b<='Z')b=uint16_t(b+32);
            if(a!=b)return false;
        }
        return !want[i];
    }
    // The subkey of key cell 'key' named 'name'; 0xffffffff when missing.
    uint32_t Subkey(uint32_t key,const char* name) const {
        size_t n=0;const uint8_t* nk=Cell(key,n);
        if(!nk || n<76 || nk[0]!='n' || nk[1]!='k')return 0xffffffffu;
        return FindIn(U32(nk+28),name,0);
    }
    uint32_t FindIn(uint32_t list,const char* name,int depth) const {
        size_t n=0;const uint8_t* l=Cell(list,n);
        if(!l || n<4 || depth>2)return 0xffffffffu;
        const size_t count=U16(l+2);
        const bool ri=l[0]=='r' && l[1]=='i',li=l[0]=='l' && l[1]=='i';
        const bool lf=l[0]=='l' && (l[1]=='f' || l[1]=='h');
        if(!ri && !li && !lf)return 0xffffffffu;
        const size_t step=lf?8:4;
        if(4+count*step>n)return 0xffffffffu;
        for(size_t i=0;i<count;++i) {
            const uint32_t child=U32(l+4+i*step);
            if(ri) {const uint32_t found=FindIn(child,name,depth+1);if(found!=0xffffffffu)return found;continue;}
            size_t m=0;const uint8_t* nk=Cell(child,m);
            if(!nk || m<76 || nk[0]!='n' || nk[1]!='k')continue;
            const size_t length=U16(nk+72);const bool ascii=(U16(nk+2)&0x20)!=0;
            if(76+length>m)continue;
            if(SameName(nk+76,ascii?length:length/2,ascii,name))return child;
        }
        return 0xffffffffu;
    }
    // The data of value 'name' ("" = default) of a key: type and bytes.
    const uint8_t* Value(uint32_t key,const char* name,uint32_t& type,size_t& bytes) const {
        size_t n=0;const uint8_t* nk=Cell(key,n);
        if(!nk || n<76 || nk[0]!='n' || nk[1]!='k')return nullptr;
        const size_t count=U32(nk+36);size_t m=0;const uint8_t* list=Cell(U32(nk+40),m);
        if(!list || count*4>m || count>4096)return nullptr;
        for(size_t i=0;i<count;++i) {
            size_t k=0;const uint8_t* vk=Cell(U32(list+4*i),k);
            if(!vk || k<20 || vk[0]!='v' || vk[1]!='k')continue;
            const size_t length=U16(vk+2);const bool ascii=(U16(vk+16)&1)!=0;
            if(20+length>k)continue;
            if(!SameName(vk+20,ascii?length:length/2,ascii,name))continue;
            const uint32_t size=U32(vk+4);type=U32(vk+12);
            if(size&0x80000000u) {bytes=size&0x7fffffffu;if(bytes>4)return nullptr;return vk+8;}
            size_t d=0;const uint8_t* cell=Cell(U32(vk+8),d);
            if(!cell || size>d)return nullptr;   // big-data ('db') values are not used by BCD text elements
            bytes=size;return cell;
        }
        return nullptr;
    }
    uint32_t Root() const {return size>=4096+8 && data[0]=='r' && data[1]=='e' && data[2]=='g' && data[3]=='f'?U32(data+0x24):0xffffffffu;}
};
// "{9dea862c-...}" from a 16-byte binary GUID is not needed: BCD keeps GUIDs
// as key names and REG_SZ/REG_MULTI_SZ text, so text is compared directly.
inline void Ascii(const uint8_t* utf16,size_t bytes,char* out,size_t capacity) {
    size_t at=0;for(size_t i=0;i+1<bytes && at+1<capacity;i+=2) {const uint16_t c=Hive::U16(utf16+i);if(!c)break;out[at++]=c<128?char(c):'?';}
    out[at]=0;
}
}  // namespace bcd
struct BcdSummary {
    static constexpr size_t maxEntries=8;
    size_t count=0,defaultIndex=SIZE_MAX;
    long timeout=-1;
    char16_t name[maxEntries][64]={};
    // r393 (other boot managers' configurations, CharonManagerProfiles.h):
    bool hidden=false,elsewhere=false,automatic=false;
    // r410 (CharonBcdList.h, read only): the default system asks for the graphical menu that Windows draws after
    // it has started (boot menu policy Standard, no forced text menu); the store's tools.
    bool tileMenu=false;uint8_t tools=0;char16_t tool[64]={};
};
// Fills 'summary' from a BCD hive in memory; false when nothing was found.
inline bool ReadBcd(const uint8_t* data,size_t size,BcdSummary& summary) {
    using bcd::Hive;
    summary=BcdSummary{};
    const Hive hive{data,size};
    const uint32_t root=hive.Root();if(root==0xffffffffu)return false;
    const uint32_t objects=hive.Subkey(root,"Objects");if(objects==0xffffffffu)return false;
    const uint32_t manager=hive.Subkey(objects,"{9dea862c-5cdd-4e70-acc1-f32b344d4795}");if(manager==0xffffffffu)return false;
    const uint32_t elements=hive.Subkey(manager,"Elements");if(elements==0xffffffffu)return false;
    uint32_t type=0;size_t bytes=0;
    char defaultId[48]={};
    if(const uint32_t k=hive.Subkey(elements,"23000003");k!=0xffffffffu)
        if(const uint8_t* v=hive.Value(k,"Element",type,bytes);v && type==1)bcd::Ascii(v,bytes,defaultId,sizeof(defaultId));
    if(const uint32_t k=hive.Subkey(elements,"25000004");k!=0xffffffffu)
        if(const uint8_t* v=hive.Value(k,"Element",type,bytes);v && type==3 && bytes>=1 && bytes<=8) {uint64_t t=0;for(size_t i=bytes>4?4:bytes;i--;)t=t<<8|v[i];summary.timeout=long(t);}   // stored inline when it fits 4 bytes (30 = one byte)
    const uint32_t order=hive.Subkey(elements,"24000001");if(order==0xffffffffu)return summary.timeout>=0;
    const uint8_t* list=hive.Value(order,"Element",type,bytes);
    if(!list || type!=7)return summary.timeout>=0;
    for(size_t at=0;at+1<bytes && summary.count<BcdSummary::maxEntries;) {
        char id[48]={};size_t n=0;
        while(at+1<bytes && Hive::U16(list+at)) {if(n+1<sizeof(id))id[n++]=char(Hive::U16(list+at));at+=2;}
        at+=2;id[n]=0;if(!n)break;
        const uint32_t object=hive.Subkey(objects,id);if(object==0xffffffffu)continue;
        const uint32_t its=hive.Subkey(object,"Elements");
        const uint32_t description=its==0xffffffffu?0xffffffffu:hive.Subkey(its,"12000004");
        char16_t* out=summary.name[summary.count];size_t k=0;
        uint32_t dt=0;size_t db=0;
        const uint8_t* v=description==0xffffffffu?nullptr:hive.Value(description,"Element",dt,db);
        if(v && dt==1) {
                for(size_t i=0;i+1<db && k+1<64;i+=2) {const uint16_t c=Hive::U16(v+i);if(!c)break;out[k++]=char16_t(c<32?u' ':c);}
            }
        out[k]=0;
        if(!k)continue;
        bool isDefault=defaultId[0];
        for(size_t i=0;isDefault && (id[i] || defaultId[i]);++i) {char a=id[i],b=defaultId[i];if(a>='A' && a<='Z')a=char(a+32);if(b>='A' && b<='Z')b=char(b+32);if(a!=b)isDefault=false;}
        if(isDefault)summary.defaultIndex=summary.count;
        ++summary.count;
    }
    return summary.count || summary.timeout>=0;
}
// "Windows 11 (domyślny), Windows Memory Diagnostic; 30 s" into out.
inline void DescribeBcd(const BcdSummary& s,bool pl,char16_t* out,size_t capacity) {
    size_t at=0;
    auto put=[&](const char16_t* t) {while(*t && at+1<capacity)out[at++]=*t++;};
    for(size_t i=0;i<s.count;++i) {
        if(i)put(u", ");
        put(s.name[i]);
        if(i==s.defaultIndex && s.count>1)put(pl?u" (domyślny)":u" (default)");
    }
    if(s.elsewhere) {put(pl?u"konfiguracja na innej partycji":u"configuration on another partition");}
    if(s.automatic && s.count)put(pl?u" i pozycje wykryte automatycznie":u" and entries found automatically");
    else if(s.automatic)put(pl?u"pozycje wykryte automatycznie":u"entries found automatically");
    if(s.tools) {put(pl?u"; narzędzia: ":u"; tools: ");put(s.tool);if(s.tools>1)put(pl?u" i inne":u" and others");}
    if(s.hidden) {put(at?u"; ":u"");put(pl?u"menu ukryte":u"menu hidden");}
    if(s.timeout>=0 && s.count>1) {
        put(u"; ");
        char16_t digits[12];size_t n=0;unsigned long v=(unsigned long)s.timeout;do {digits[n++]=char16_t(u'0'+v%10);v/=10;} while(v && n<11);
        while(n && at+1<capacity)out[at++]=digits[--n];
        put(u" s");
    }
    out[at]=0;
}
}  // namespace hakefi
