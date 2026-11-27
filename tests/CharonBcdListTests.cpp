// Host test of src/efi/CharonBcdList.h (r410): the entries of a Windows BCD
// store, read only.
//   clang++ -std=c++17 -O1 -fuse-ld=lld -I src/efi tests/CharonBcdListTests.cpp -o t.exe
//   t.exe [BCD-FILE[=EXPECTED-MENU-ENTRIES] ...]
// A small hive is built here (regf base block, one hbin, nk / vk cells, lf, lh,
// li and ri subkey lists) and listed; then it, and every real BCD file named
// on the command line (Microsoft data: read in place, never copied or
// shipped), is cut off at every length and damaged byte by byte, the bytes
// placed right before an inaccessible page (Windows), so a read outside the
// buffer stops the test. Prints PASS/FAIL lines, then "N checks, M failed";
// the exit code is M.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "CharonBcdList.h"
using namespace hakefi::bcdlist;
static int checks=0,failed=0;
static void Check(bool ok,const char* what) {++checks;if(!ok)++failed;std::printf("%s %s\n",ok?"PASS":"FAIL",what);}
static std::string U8(const char16_t* s) {
    std::string o;
    for(;*s;++s) {
        const unsigned c=*s;
        if(c<0x80)o+=char(c);
        else if(c<0x800) {o+=char(0xc0|c>>6);o+=char(0x80|(c&63));}
        else {o+=char(0xe0|c>>12);o+=char(0x80|((c>>6)&63));o+=char(0x80|(c&63));}
    }
    return o;
}
static void Is(const char16_t* got,const char* want,const char* what) {
    const std::string g=U8(got);++checks;
    if(g==want)std::printf("PASS %s\n",what);else {++failed;std::printf("FAIL %s\n     got:  %s\n     want: %s\n",what,g.c_str(),want);}
}
// ---- a hive built in memory ----------------------------------------------
struct Builder {
    std::vector<uint8_t> bins=std::vector<uint8_t>(32,0);   // the hbin header, filled at the end
    static void P16(std::vector<uint8_t>& v,unsigned x) {v.push_back(uint8_t(x));v.push_back(uint8_t(x>>8));}
    static void P32(std::vector<uint8_t>& v,uint32_t x) {for(int i=0;i<4;++i)v.push_back(uint8_t(x>>(8*i)));}
    uint32_t Cell(const std::vector<uint8_t>& payload) {
        const uint32_t offset=uint32_t(bins.size());
        size_t total=(payload.size()+4+7)&~size_t(7);
        const int32_t negative=-int32_t(total);
        for(int i=0;i<4;++i)bins.push_back(uint8_t(uint32_t(negative)>>(8*i)));
        bins.insert(bins.end(),payload.begin(),payload.end());
        while(bins.size()<offset+total)bins.push_back(0);
        return offset;
    }
    uint32_t Value(const char* name,uint32_t type,const std::vector<uint8_t>& data) {
        std::vector<uint8_t> vk={'v','k'};const size_t n=std::strlen(name);
        P16(vk,unsigned(n));
        if(data.size()<=4) {P32(vk,uint32_t(data.size())|0x80000000u);uint32_t inl=0;for(size_t i=0;i<data.size();++i)inl|=uint32_t(data[i])<<(8*i);P32(vk,inl);}
        else {const uint32_t cell=Cell(data);P32(vk,uint32_t(data.size()));P32(vk,cell);}
        P32(vk,type);P16(vk,1);P16(vk,0);
        vk.insert(vk.end(),name,name+n);
        return Cell(vk);
    }
    // kind: 'f' lf, 'h' lh, 'i' li, 'r' ri over two lf lists; wide: UTF-16 key name.
    uint32_t Key(const char* name,const std::vector<uint32_t>& subkeys,const std::vector<uint32_t>& values,char kind='f',bool wide=false) {
        uint32_t valueList=0xffffffffu,subkeyList=0xffffffffu;
        if(!values.empty()) {std::vector<uint8_t> l;for(uint32_t v:values)P32(l,v);valueList=Cell(l);}
        auto leaf=[&](size_t from,size_t to,char k) {
            std::vector<uint8_t> l={'l',uint8_t(k)};P16(l,unsigned(to-from));
            for(size_t i=from;i<to;++i) {P32(l,subkeys[i]);if(k!='i')P32(l,0x12345678);}
            return Cell(l);
        };
        if(!subkeys.empty()) {
            if(kind=='r') {
                const size_t half=subkeys.size()/2;const uint32_t a=leaf(0,half,'f'),b=leaf(half,subkeys.size(),'h');
                std::vector<uint8_t> l={'r','i'};P16(l,2);P32(l,a);P32(l,b);subkeyList=Cell(l);
            } else subkeyList=leaf(0,subkeys.size(),kind);
        }
        std::vector<uint8_t> nk={'n','k'};const size_t n=std::strlen(name);
        P16(nk,wide?0:0x20);
        nk.resize(20,0);P32(nk,uint32_t(subkeys.size()));P32(nk,0);P32(nk,subkeyList);P32(nk,0xffffffffu);
        P32(nk,uint32_t(values.size()));P32(nk,valueList);
        nk.resize(72,0);P16(nk,unsigned(wide?2*n:n));P16(nk,0);
        for(size_t i=0;i<n;++i) {nk.push_back(uint8_t(name[i]));if(wide)nk.push_back(0);}
        return Cell(nk);
    }
    static std::vector<uint8_t> Sz(const char16_t* text) {std::vector<uint8_t> d;for(;*text;++text)P16(d,*text);P16(d,0);return d;}
    static std::vector<uint8_t> Multi(std::initializer_list<const char16_t*> items) {std::vector<uint8_t> d;for(const char16_t* t:items) {for(;*t;++t)P16(d,*t);P16(d,0);}P16(d,0);return d;}
    static std::vector<uint8_t> U64(uint64_t v) {std::vector<uint8_t> d;for(int i=0;i<8;++i)d.push_back(uint8_t(v>>(8*i)));return d;}
    uint32_t Element(const char* number,uint32_t type,const std::vector<uint8_t>& data) {return Key(number,{},{Value("Element",type,data)});}
    uint32_t Object(const char* guid,uint32_t type,const std::vector<uint32_t>& elements,char kind='f',bool wide=false) {
        std::vector<uint8_t> t;P32(t,type);
        const uint32_t description=Key("Description",{},{Value("Type",4,t)});
        const uint32_t list=Key("Elements",elements,{},kind);
        return Key(guid,{description,list},{},'i',wide);
    }
    std::vector<uint8_t> Finish(uint32_t root) {
        while(bins.size()%4096)bins.push_back(0);
        std::memcpy(bins.data(),"hbin",4);
        const uint32_t size=uint32_t(bins.size());
        for(int i=0;i<4;++i)bins[8+i]=uint8_t(size>>(8*i));
        std::vector<uint8_t> file(4096,0);
        std::memcpy(file.data(),"regf",4);
        for(int i=0;i<4;++i) {file[0x24+i]=uint8_t(root>>(8*i));file[0x28+i]=uint8_t(size>>(8*i));}
        file.insert(file.end(),bins.begin(),bins.end());
        return file;
    }
};
#define BOOTMGR "{9dea862c-5cdd-4e70-acc1-f32b344d4795}"
#define MEMDIAG "{b2721d73-1db4-4c62-bf78-c548a880142d}"
#define WIN11 "{11111111-aaaa-4bbb-8ccc-000000000011}"
#define WIN10 "{22222222-aaaa-4bbb-8ccc-000000000010}"
#define RESUME "{33333333-aaaa-4bbb-8ccc-0000000000aa}"
#define SETTINGS "{7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}"
#define GONE "{44444444-aaaa-4bbb-8ccc-0000000000ff}"
static std::vector<uint8_t> Synthetic() {
    Builder b;
    const uint32_t manager=b.Object(BOOTMGR,0x10100002,{
        b.Element("12000004",1,Builder::Sz(u"Windows Boot Manager")),
        b.Element("23000003",1,Builder::Sz(u"" WIN10)),
        b.Element("24000001",7,Builder::Multi({u"" WIN11,u"" GONE,u"{22222222-AAAA-4BBB-8CCC-000000000010}"})),
        b.Element("24000010",7,Builder::Multi({u"" MEMDIAG})),
        b.Element("25000004",3,Builder::U64(30)),
        b.Element("26000020",3,{1})},'h');
    const uint32_t win11=b.Object(WIN11,0x10200003,{
        b.Element("12000002",1,Builder::Sz(u"\\Windows\\system32\\winload.efi")),
        b.Element("12000004",1,Builder::Sz(u"Windows 11")),
        b.Element("250000c2",3,Builder::U64(1))},'i');
    const uint32_t win10=b.Object(WIN10,0x10200003,{
        b.Element("12000004",1,Builder::Sz(u"Windows 10 – kopia zapasowa, żółć")),
        b.Element("250000C2",3,Builder::U64(0))},'f',true);
    const uint32_t memdiag=b.Object(MEMDIAG,0x10200005,{
        b.Element("12000002",1,Builder::Sz(u"\\EFI\\Microsoft\\Boot\\memtest.efi")),
        b.Element("12000004",1,Builder::Sz(u"Windows Memory Diagnostic"))});
    const uint32_t resume=b.Object(RESUME,0x10200004,{b.Element("12000004",1,Builder::Sz(u"Windows Resume Application"))});
    const uint32_t settings=b.Object(SETTINGS,0x20100000,{b.Element("12000004",1,Builder::Sz(u"Global settings"))});
    const uint32_t objects=b.Key("Objects",{manager,win11,win10,memdiag,resume,settings},{},'r');
    const uint32_t description=b.Key("Description",{},{});
    const uint32_t root=b.Key("NewStoreRoot",{description,objects},{},'h');
    return b.Finish(root);
}
// ---- damage ---------------------------------------------------------------
struct Guarded {
    uint8_t* at=nullptr;void* base=nullptr;
    Guarded(const uint8_t* bytes,size_t size) {
#ifdef _WIN32
        const size_t page=4096,pages=(size+page-1)/page+1;
        base=VirtualAlloc(nullptr,(pages+1)*page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        DWORD old=0;VirtualProtect((char*)base+pages*page,page,PAGE_NOACCESS,&old);
        at=(uint8_t*)base+pages*page-size;
#else
        base=at=new uint8_t[size+1];
#endif
        if(size)std::memcpy(at,bytes,size);
    }
    ~Guarded() {
#ifdef _WIN32
        VirtualFree(base,0,MEM_RELEASE);
#else
        delete[] (uint8_t*)base;
#endif
    }
};
static bool Sane(const Entry* e,size_t n,size_t max) {
    if(n>max)return false;
    for(size_t i=0;i<n;++i) {
        size_t k=0;while(k<40 && e[i].guid[k])++k;if(k>=40)return false;
        k=0;while(k<96 && e[i].description[k])++k;if(k>=96)return false;
        k=0;while(k<128 && e[i].path[k])++k;if(k>=128)return false;
    }
    return true;
}
static void Damage(const std::vector<uint8_t>& hive,const char* name,size_t flipStep) {
    Entry e[12];bool sane=true;size_t runs=0;
    for(size_t n=0;n<=hive.size();n+=(n<4200 || hive.size()-n<64)?1:7) {
        Guarded g(hive.data(),n);Store s;
        sane&=Sane(e,ListEntries(g.at,n,e,12,&s,true),12);++runs;
    }
    {
        Guarded g(hive.data(),hive.size());
        static const uint8_t masks[]={0xff,0x01,0x80,0x7f};
        for(size_t i=0;i<hive.size();i+=flipStep)for(uint8_t mask:masks) {
            g.at[i]^=mask;Store s;
            sane&=Sane(e,ListEntries(g.at,hive.size(),e,12,&s,true),12);++runs;
            sane&=Sane(e,ListEntries(g.at,hive.size(),e,2,&s,(i&1)!=0),2);
            g.at[i]^=mask;
        }
        // whole 32-bit fields overwritten with extreme values
        static const uint32_t words[]={0,0xffffffffu,0x7fffffffu,0x80000000u,0xfffff000u,0x00001000u};
        for(size_t i=0;i+4<=hive.size();i+=4*flipStep)for(uint32_t word:words) {
            uint8_t keep[4];std::memcpy(keep,g.at+i,4);
            for(int k=0;k<4;++k)g.at[i+k]=uint8_t(word>>(8*k));
            Store s;sane&=Sane(e,ListEntries(g.at,hive.size(),e,12,&s,true),12);++runs;
            std::memcpy(g.at+i,keep,4);
        }
    }
    std::printf("     %s: %zu damaged or cut-off hives\n",name,runs);
    char what[200];std::snprintf(what,sizeof(what),"%s: cut-off and damaged hives give in-range results and no read outside the buffer",name);
    Check(sane,what);
}
int main(int argc,char** argv) {
    const std::vector<uint8_t> hive=Synthetic();
    Entry e[16];Store s;
    size_t n=ListEntries(hive.data(),hive.size(),e,16,&s);
    Check(n==3 && s.found && s.menuCount==2 && s.toolCount==1,"synthetic hive: 2 menu entries (the object missing from the store is skipped) and 1 tool");
    Check(s.timeout==30 && s.displayBootMenu==1,"synthetic hive: timeout 30 (8-byte integer), displaybootmenu yes (1 byte, stored in the value record)");
    Is(s.defaultGuid,WIN10,"synthetic hive: default object");
    Is(e[0].guid,WIN11,"synthetic hive: first GUID");
    Is(e[0].description,"Windows 11","synthetic hive: first description");
    Is(e[0].path,"\\Windows\\system32\\winload.efi","synthetic hive: application path");
    Check(e[0].type==typeOsLoader && e[0].inMenu && !e[0].tool && e[0].order==0 && !e[0].isDefault && e[0].menuPolicy==policyStandard,"synthetic hive: Windows 11 is a loader, first in the menu, standard menu policy");
    Is(e[1].description,"Windows 10 \xe2\x80\x93 kopia zapasowa, \xc5\xbc\xc3\xb3\xc5\x82\xc4\x87","synthetic hive: description with letters outside ASCII (UTF-16 key name, lf list)");
    Check(e[1].isDefault && e[1].order==2 && e[1].menuPolicy==policyLegacy && e[1].type==typeOsLoader,"synthetic hive: the default is found though the list writes its GUID in capitals; order is the position in the list");
    Check(e[2].tool && !e[2].inMenu && e[2].order==0 && e[2].type==typeMemoryDiagnostic && e[2].menuPolicy==policyNotSet,"synthetic hive: Windows Memory Diagnostic is a tool");
    Is(e[2].description,"Windows Memory Diagnostic","synthetic hive: tool description");
    n=ListEntries(hive.data(),hive.size(),e,16,&s,true);
    Check(n==4 && e[3].type==typeResume && e[3].order==notListed && !e[3].inMenu && !e[3].tool,"synthetic hive, all objects: the resume application is added; the boot manager and inherit objects are not");
    Is(e[3].guid,RESUME,"synthetic hive, all objects: GUID from the key name (ri list of lf and lh)");
    Check(ListEntries(hive.data(),hive.size(),e,2,&s,true)==2 && ListEntries(hive.data(),hive.size(),e,0,&s)==0,"synthetic hive: a small buffer limits the list");
    Check(ListEntries(nullptr,0,e,16,&s)==0 && !s.found && ListEntries(hive.data(),4095,e,16)==0 && ListEntries(hive.data(),hive.size(),nullptr,16)==0,"no hive, a hive without bins and no output buffer give nothing");
    {std::vector<uint8_t> other=hive;other[0]='x';Check(ListEntries(other.data(),other.size(),e,16,&s)==0,"a file that is not a registry hive gives nothing");}
    Check(TypeName(typeOsLoader,false) && TypeName(typeMemoryDiagnostic,true) && !TypeName(0x12345678,false),"TypeName: known and unknown types");
    Damage(hive,"synthetic hive",1);
    // ---- real stores, by path --------------------------------------------
    for(int a=1;a<argc;++a) {
        std::string arg=argv[a];long expected=-1;
        const size_t eq=arg.rfind('=');
        if(eq!=std::string::npos && eq+1<arg.size() && arg[eq+1]>='0' && arg[eq+1]<='9') {expected=std::atol(arg.c_str()+eq+1);arg=arg.substr(0,eq);}
        FILE* f=std::fopen(arg.c_str(),"rb");
        if(!f) {std::printf("     %s: not readable, skipped\n",arg.c_str());continue;}
        std::vector<uint8_t> data;uint8_t block[65536];size_t got;
        while((got=std::fread(block,1,sizeof(block),f))>0)data.insert(data.end(),block,block+got);
        std::fclose(f);
        Entry r[32];Store rs;
        const size_t count=ListEntries(data.data(),data.size(),r,32,&rs,true);
        std::printf("     %s: %zu bytes, boot manager %s, timeout %ld, displaybootmenu %d, default %s\n",arg.c_str(),data.size(),rs.found?"found":"missing",rs.timeout,int(rs.displayBootMenu),U8(rs.defaultGuid).c_str());
        for(size_t i=0;i<count;++i)
            std::printf("       %s%s%s order %d type %08x policy %d  \"%s\"  %s\n",r[i].inMenu?"menu":r[i].tool?"tool":"other",r[i].isDefault?" default":"",
                "",int(r[i].order),unsigned(r[i].type),int(r[i].menuPolicy),U8(r[i].description).c_str(),U8(r[i].path).c_str());
        char what[400];
        std::snprintf(what,sizeof(what),"%s: boot manager object found, entries in range",arg.c_str());
        Check(rs.found && Sane(r,count,32),what);
        if(expected>=0) {std::snprintf(what,sizeof(what),"%s: %ld entries in the display order",arg.c_str(),expected);Check(long(rs.menuCount)==expected,what);}
        // the same store through the r389 summary reader: same names in the same order
        hakefi::BcdSummary summary;
        if(hakefi::ReadBcd(data.data(),data.size(),summary)) {
            bool same=true;size_t at=0;
            for(size_t i=0;i<count && at<summary.count;++i) {
                if(!r[i].inMenu || !r[i].description[0])continue;
                char16_t cut[64];size_t k=0;for(;r[i].description[k] && k+1<64;++k)cut[k]=r[i].description[k];cut[k]=0;
                same&=U8(cut)==U8(summary.name[at]);
                if(r[i].isDefault)same&=summary.defaultIndex==at;
                ++at;
            }
            same&=summary.timeout==rs.timeout;
            std::snprintf(what,sizeof(what),"%s: agrees with the r389 summary reader (names, default, timeout)",arg.c_str());
            Check(same,what);
        }
        Damage(data,arg.c_str(),data.size()>40000?5:1);
    }
    std::printf("%d checks, %d failed\n",checks,failed);
    return failed;
}
