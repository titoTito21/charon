// Database protocol fixture derived from HiiDatabaseTests; actual VfrCompile IFR input.
#include "HiiConfig.h"
#include <string>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(false)
using namespace charonhii;using Bytes=std::vector<uint8_t>;
static void le(Bytes& b,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));}
static void set(Bytes& b,size_t at,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b[at+i]=uint8_t(v>>(8*i));}
static void pack(Bytes& b,unsigned type,const Bytes& data){le(b,data.size()+4,3);b.push_back(uint8_t(type));b.insert(b.end(),data.begin(),data.end());}
static Bytes Fixture() {
    Bytes list(20);list[0]=42;
    for(const auto* lang:{"pl-PL","en-US"}) {
        Bytes strings(42);strings.insert(strings.end(),lang,lang+6);set(strings,0,strings.size()+4,4);set(strings,4,strings.size()+4,4);
        strings.push_back(0x14);le(strings,lang[0]=='p'?'P':'E',2);le(strings,0,2);
        if(lang[0]=='e') {strings.push_back(0x14);le(strings,'X',2);le(strings,0,2);}
        strings.push_back(0);pack(list,4,strings);
    }
    Bytes forms={0x0e,0x97};forms.resize(18);le(forms,1,2);le(forms,0,2);forms.push_back(0);
    forms.insert(forms.end(),{1,0x86,1,0,1,0,6,14,1,0,0,0,7,0,0,0,0,0,0,0,0x29,2,0x29,2});
    pack(list,2,forms);pack(list,0xdf,{});set(list,16,list.size(),4);return list;
}
static efi_boot_services boot{};static DatabaseProtocol api{};static Bytes data;
static std::vector<PackageNotify> callbacks;static std::vector<efi_handle> registration;
static size_t live=0,exportCalls=0,listCalls=0;static bool grow=false,mutate=false,missing=false,failUnregister=false,failAllocation=false;
static bool ownerChanges=false;static unsigned ownerCalls=0,registrationFailure=0;
static size_t listSize=sizeof(void*),exportSize=0;static char handle,owner,other;
static efi_status EFIAPI DatabaseLocate(efi_guid*,void*,void** out){if(missing)return EFI_NOT_FOUND;*out=&api;return EFI_SUCCESS;}
static efi_status EFIAPI Allocate(efi_memory_type,size_t size,void** out){if(failAllocation)return EFI_OUT_OF_RESOURCES;*out=malloc(size);if(!*out)return EFI_OUT_OF_RESOURCES;++live;return EFI_SUCCESS;}
static efi_status EFIAPI Free(void* pointer){CHECK(live);--live;free(pointer);return EFI_SUCCESS;}
static efi_status EFIAPI List(const DatabaseProtocol*,uint8_t type,const efi_guid* guid,size_t* bytes,void** out){
    ++listCalls;CHECK(type==2 && !guid);if(!out){*bytes=listSize;return listSize?EFI_BUFFER_TOO_SMALL:EFI_NOT_FOUND;}
    CHECK(*bytes>=sizeof(void*));*bytes=sizeof(void*);*out=&handle;return EFI_SUCCESS;
}
static efi_status EFIAPI Export(const DatabaseProtocol*,void* hii,size_t* bytes,void* out){
    ++exportCalls;CHECK(hii==&handle);
    if(!out){*bytes=exportSize?exportSize:data.size();return EFI_OUT_OF_RESOURCES;}
    if(grow){*bytes=data.size()+8;return EFI_OUT_OF_RESOURCES;}
    CHECK(*bytes>=data.size());memcpy(out,data.data(),data.size());*bytes=data.size();
    if(mutate) {callbacks[0](2,nullptr,nullptr,&handle,1);}
    return EFI_SUCCESS;
}
static efi_status EFIAPI Register(const DatabaseProtocol*,uint8_t type,const efi_guid* guid,PackageNotify callback,size_t change,efi_handle* out){
    CHECK((type==2 || type==4) && !guid && (change==1 || change==2 || change==8));
    if(registrationFailure && callbacks.size()+1==registrationFailure)return EFI_OUT_OF_RESOURCES;
    callbacks.push_back(callback);*out=reinterpret_cast<void*>(callbacks.size());registration.push_back(*out);return EFI_SUCCESS;
}
static efi_status EFIAPI Unregister(const DatabaseProtocol*,efi_handle){return failUnregister?EFI_DEVICE_ERROR:EFI_SUCCESS;}
static efi_status EFIAPI Driver(const DatabaseProtocol*,void* hii,efi_handle* out){CHECK(hii==&handle);*out=ownerChanges && ++ownerCalls>1?&other:&owner;return EFI_SUCCESS;}
void Reset(){CHECK(!live);boot={};boot.LocateProtocol=DatabaseLocate;boot.AllocatePool=Allocate;boot.FreePool=Free;
    api={};api.list=List;api.exportList=Export;api.registerNotify=Register;api.unregisterNotify=Unregister;api.driver=Driver;
    callbacks.clear();registration.clear();data=Fixture();listSize=sizeof(void*);exportSize=exportCalls=listCalls=0;
    grow=mutate=missing=failUnregister=failAllocation=ownerChanges=false;ownerCalls=registrationFailure=0;}
static ConfigAccessProtocol accessProtocol{};static ConfigRoutingProtocol routingProtocol{};
static Bytes current(52);static uint8_t devicePath[]={1,1,6,0,0xab,0xcd,0x7f,0xff,4,0};
static unsigned reads=0,routes=0;static int corruptResponse=0;static bool changedDuringRead=false,partialRoute=false;
static efi_status extractStatus=0,routeStatus=0;static std::u16string lastRequest;
static size_t field(const std::u16string& s,const char16_t* label){size_t at=s.find(label);CHECK(at!=s.npos);at+=std::char_traits<char16_t>::length(label);size_t n=0;
    while(at<s.size()&&s[at]!=u'&'){auto c=s[at++];CHECK((c>=u'0'&&c<=u'9')||(c>=u'a'&&c<=u'f'));n=n*16+(c<=u'9'?c-u'0':c-u'a'+10);}return n;}
static efi_status EFIAPI ConfigLocate(efi_guid* guid,void* context,void** out){if(guid->data1==0x587e72d7){*out=&routingProtocol;return 0;}return DatabaseLocate(guid,context,out);}
static efi_status EFIAPI Handle(efi_handle driver,efi_guid* guid,void** out){CHECK(driver==&owner);
    if(guid->data1==0x330d4706)*out=&accessProtocol;else if(guid->data1==0x09576e91)*out=devicePath;else return EFI_NOT_FOUND;return 0;}
static efi_status EFIAPI Extract(const ConfigAccessProtocol* self,const char16_t* request,char16_t** progress,char16_t** response){
    CHECK(self==&accessProtocol);++reads;lastRequest=request;size_t offset=field(lastRequest,u"&OFFSET="),width=field(lastRequest,u"&WIDTH=");
    CHECK(offset+width<=current.size());CHECK(offset+width<=34); //password is never requested
    *progress=const_cast<char16_t*>(request)+lastRequest.size();*response=nullptr;if(extractStatus)return extractStatus;
    std::u16string out=lastRequest+u"&VALUE=";for(size_t i=offset+width;i>offset;--i){out+=u"0123456789abcdef"[current[i-1]>>4];out+=u"0123456789abcdef"[current[i-1]&15];}
    if(corruptResponse==1)out[5]=out[5]==u'0'?u'1':u'0';if(corruptResponse==2)out+=u"&OFFSET=0000&WIDTH=0001&VALUE=ff";
    if(corruptResponse==3)out.back()=u'z';if(corruptResponse==4)--*progress;
    CHECK(Allocate(EfiBootServicesData,(out.size()+1)*2,(void**)response)==0);memcpy(*response,out.c_str(),(out.size()+1)*2);
    if(changedDuringRead)callbacks[0](2,nullptr,nullptr,&handle,1);return 0;
}
static efi_status EFIAPI Route(const ConfigRoutingProtocol* self,const char16_t* request,char16_t** progress){
    CHECK(self==&routingProtocol);++routes;std::u16string s=request;size_t offset=field(s,u"&OFFSET="),width=field(s,u"&WIDTH=");CHECK(offset+width<=34);
    size_t value=s.find(u"&VALUE=")+7;*progress=const_cast<char16_t*>(request)+s.size();
    if(!routeStatus||partialRoute)for(size_t i=0;i<(partialRoute?1:width);++i){size_t at=value+(width-1-i)*2;
        auto digit=[](char16_t c){return c<=u'9'?c-u'0':c-u'a'+10;};current[offset+i]=uint8_t(digit(s[at])*16+digit(s[at+1]));}
    return routeStatus;
}
int main(int argc,char** argv){
    CHECK(argc==2);Reset();std::ifstream file(argv[1],std::ios::binary);CHECK(bool(file));Bytes compiled((std::istreambuf_iterator<char>(file)),{});
    data=Bytes(20);data.insert(data.end(),compiled.begin(),compiled.end());pack(data,0xdf,{});set(data,16,data.size(),4);
    boot.LocateProtocol=ConfigLocate;boot.HandleProtocol=Handle;accessProtocol.extract=Extract;routingProtocol.route=Route;
    Database database;CHECK(database.Open(&boot)==0&&database.Refresh()==0);const char* languages[]={"en-US"};CHECK(database.Read(0,languages,1)==0);
    auto identity=database.Current();const auto* model=database.View(identity);CHECK(model&&model->questionCount==5);
    current[model->questions[3].storeInfo]='A';Config config;ConfigValue value;ConfigProposal proposal;ConfigCommitResult result;
    CHECK(config.Bind(&boot,database,identity,0)==0);CHECK(config.Read(value)==0&&value.kind==Kind::Checkbox&&value.number==0);
    CHECK(lastRequest==u"GUID=7a21bc32f230f84391878c4d771a1001&NAME=0043006f006e006600690067&PATH=01010600abcd7fff0400&OFFSET=0000&WIDTH=0001");
    value.number=1;CHECK(config.Propose(value,proposal)==0&&routes==0);
    CHECK(config.Commit(proposal,false,result)==EFI_ACCESS_DENIED&&!result.attempted&&routes==0);
    CHECK(config.Commit(proposal,true,result)==0&&result.attempted&&result.applied&&result.changed&&current[0]==1);
    CHECK(config.Commit(proposal,true,result)==EFI_MEDIA_CHANGED&&!result.attempted&&routes==1);
    ConfigValue editStart=value;editStart.number=0;value.number=0;
    CHECK(config.Propose(value,proposal,&editStart)==EFI_MEDIA_CHANGED);
    editStart.number=1;CHECK(config.Propose(value,proposal,&editStart)==0);
    CHECK(config.Bind(&boot,database,identity,1)==0);CHECK(config.Read(value)==0);value.number=2;CHECK(config.Propose(value,proposal)==EFI_INVALID_PARAMETER);
    value.number=1;CHECK(config.Propose(value,proposal)==0&&config.Commit(proposal,true,result)==0);
    CHECK(config.Bind(&boot,database,identity,2)==0);CHECK(config.Read(value)==0);value.number=7;CHECK(config.Propose(value,proposal)==EFI_INVALID_PARAMETER);
    value.number=105;CHECK(config.Propose(value,proposal)==EFI_INVALID_PARAMETER);value.number=10;CHECK(config.Propose(value,proposal)==0);
    routeStatus=EFI_ACCESS_DENIED;CHECK(config.Commit(proposal,true,result)==EFI_ACCESS_DENIED&&result.attempted&&!result.changed&&!result.applied);
    partialRoute=true;CHECK(config.Commit(proposal,true,result)==EFI_ACCESS_DENIED&&result.changed&&result.applied);partialRoute=false;routeStatus=0;
    CHECK(config.Bind(&boot,database,identity,3)==0);CHECK(config.Read(value)==0&&value.text[0]==u'A');value.text[0]=0;CHECK(config.Propose(value,proposal)==EFI_INVALID_PARAMETER);
    value.text[0]=0xd800;CHECK(config.Propose(value,proposal)==EFI_INVALID_PARAMETER);value.text[0]=u'Z';CHECK(config.Propose(value,proposal)==0&&config.Commit(proposal,true,result)==0);
    value.text[0]=u'B';value.text[1]=u'C';value.text[2]=0;CHECK(config.Propose(value,proposal)==0);
    partialRoute=true;routeStatus=EFI_ACCESS_DENIED;
    CHECK(config.Commit(proposal,true,result)==EFI_ACCESS_DENIED&&result.changed&&!result.applied&&result.readbackStatus==0);
    CHECK(config.Read(value)==0&&value.text[0]==u'B'&&value.text[1]==0);
    value.text[0]=u'D';value.text[1]=u'E';CHECK(config.Propose(value,proposal)==0);routeStatus=0;
    CHECK(config.Commit(proposal,true,result)==EFI_DEVICE_ERROR&&result.changed&&!result.applied);partialRoute=false;
    const unsigned beforeProtected=reads;CHECK(config.Bind(&boot,database,identity,4)==EFI_UNSUPPORTED&&reads==beforeProtected);
    CHECK(config.Bind(&boot,database,identity,0)==0);
    for(corruptResponse=1;corruptResponse<=4;++corruptResponse){value.number=99;CHECK(config.Read(value)==EFI_COMPROMISED_DATA&&value.number==99);}corruptResponse=0;
    extractStatus=EFI_ACCESS_DENIED;CHECK(config.Read(value)==EFI_ACCESS_DENIED);extractStatus=0;
    value.kind=Kind::Checkbox;value.number=0;CHECK(config.Propose(value,proposal)==0);current[0]=0;
    auto routed=routes;CHECK(config.Commit(proposal,true,result)==EFI_MEDIA_CHANGED&&routes==routed);current[0]=1;
    devicePath[4]^=1;CHECK(config.Read(value)==EFI_MEDIA_CHANGED);devicePath[4]^=1;
    ownerChanges=true;ownerCalls=1;CHECK(config.Read(value)==EFI_MEDIA_CHANGED);ownerChanges=false;
    value.kind=Kind::Checkbox;value.number=0;CHECK(config.Propose(value,proposal)==0);
    config.Reset();CHECK(config.Commit(proposal,true,result)==EFI_MEDIA_CHANGED);
    CHECK(config.Bind(&boot,database,identity,0)==0);changedDuringRead=true;CHECK(config.Read(value)==EFI_MEDIA_CHANGED);changedDuringRead=false;
    CHECK(config.Commit(proposal,true,result)==EFI_MEDIA_CHANGED&&!result.attempted);
    // Re-export actual compiler IFR with explicit read-only/callback flags and
    // a password alias: bounds alone never authorize reads or writes.
    const auto original=data;
    auto refresh=[&](){CHECK(database.Refresh()==0&&database.Read(0,languages,1)==0);identity=database.Current();model=database.View(identity);CHECK(model);};
    refresh();const auto checkboxAt=model->questions[0].offset;const auto passwordOffset=model->questions[4].storeInfo;
    data[checkboxAt+12]=1;refresh();CHECK(config.Bind(&boot,database,identity,0)==0&&config.Read(value)==0);
    CHECK(config.Propose(value,proposal)==EFI_WRITE_PROTECTED);
    data=original;data[checkboxAt+12]=4;refresh();CHECK(config.Bind(&boot,database,identity,0)==EFI_UNSUPPORTED);
    data=original;set(data,checkboxAt+10,passwordOffset,2);refresh();auto beforeAlias=reads;
    CHECK(config.Bind(&boot,database,identity,0)==EFI_ACCESS_DENIED&&reads==beforeAlias);
    CHECK(database.Close()==0&&!live);puts("PASS actual HII config access/routing, VfrCompile types, exact headers, explicit commit, stale identities, protected data, partial failures and reread");return 0;
}


