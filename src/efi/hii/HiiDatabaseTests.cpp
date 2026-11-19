#include "HiiDatabase.h"
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
static efi_status EFIAPI Locate(efi_guid*,void*,void** out){if(missing)return EFI_NOT_FOUND;*out=&api;return EFI_SUCCESS;}
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
void Reset(){CHECK(!live);boot={};boot.LocateProtocol=Locate;boot.AllocatePool=Allocate;boot.FreePool=Free;
    api={};api.list=List;api.exportList=Export;api.registerNotify=Register;api.unregisterNotify=Unregister;api.driver=Driver;
    callbacks.clear();registration.clear();data=Fixture();listSize=sizeof(void*);exportSize=exportCalls=listCalls=0;
    grow=mutate=missing=failUnregister=failAllocation=ownerChanges=false;ownerCalls=registrationFailure=0;}
int main(){
    const char* pl[]={"pl-PL","en-US"};const char* fallback[]={"de-DE","en-US"};const char* none[]={"de-DE"};
    Reset();Database database;CHECK(database.Open(&boot)==0 && callbacks.size()==6);CHECK(database.Refresh()==0 && database.Count()==1);
    CHECK(database.Read(0,pl,2)==0);const auto token=database.Current();const auto* model=database.View(token);
    CHECK(model && model->questionCount==1 && model->questions[0].kind==Kind::Checkbox && model->stringsAvailable);
    CHECK(!strcmp(database.SelectedLanguage(token),"pl-PL") && model->FindString(1) && !model->FindString(2));
    data[0]=99;CHECK(model->snapshot[0]==42); // Exported bytes are owned, not borrowed from firmware.
    auto wrong=token;wrong.driver=&other;CHECK(!database.View(wrong));wrong=token;wrong.guid[0]^=1;CHECK(!database.View(wrong));
    CHECK(database.Read(0,fallback,2)==0 && !database.View(token));
    CHECK(!strcmp(database.SelectedLanguage(database.Current()),"en-US") && database.View(database.Current())->FindString(2));
    CHECK(database.Read(0,none,1)==0 && !database.View(database.Current())->stringsAvailable && !*database.SelectedLanguage(database.Current()));
    auto stale=database.Current();callbacks[1](2,nullptr,nullptr,&handle,2);
    CHECK(!database.View(stale) && database.Count()==0 && database.Read(0,pl,2)==EFI_MEDIA_CHANGED);
    CHECK(database.Refresh()==0);grow=true;CHECK(database.Read(0,pl,2)==EFI_OUT_OF_RESOURCES && !database.View(database.Current()));grow=false;
    mutate=true;CHECK(database.Read(0,pl,2)==EFI_MEDIA_CHANGED);mutate=false;CHECK(database.Refresh()==0);
    CHECK(database.Read(0,pl,2)==0);stale=database.Current();CHECK(database.Refresh()==0 && !database.View(stale));
    failUnregister=true;CHECK(database.Close()==EFI_DEVICE_ERROR && database.NeedsClose() && !live);
    Database second;CHECK(second.Open(&boot)==EFI_ALREADY_STARTED);failUnregister=false;CHECK(database.Close()==0 && !database.NeedsClose());
    Reset();missing=true;CHECK(database.Open(&boot)==EFI_NOT_FOUND && !database.NeedsClose());missing=false;
    CHECK(database.Open(&boot)==0);listSize=513*sizeof(void*);CHECK(database.Refresh()==EFI_BAD_BUFFER_SIZE && !live);
    listSize=sizeof(void*);CHECK(database.Refresh()==0);exportSize=Database::MaxBytes+1;CHECK(database.Read(0,pl,2)==EFI_BAD_BUFFER_SIZE);
    exportSize=0;failAllocation=true;CHECK(database.Read(0,pl,2)==EFI_OUT_OF_RESOURCES);failAllocation=false;
    CHECK(database.Close()==0 && !live);
    Reset();registrationFailure=3;failUnregister=true;
    CHECK(database.Open(&boot)==EFI_OUT_OF_RESOURCES && database.NeedsClose());
    CHECK(second.Open(&boot)==EFI_ALREADY_STARTED);failUnregister=false;CHECK(database.Close()==0);
    Reset();CHECK(database.Open(&boot)==0 && database.Refresh()==0);ownerChanges=true;
    CHECK(database.Read(0,pl,2)==EFI_MEDIA_CHANGED && !database.View(database.Current()));
    CHECK(database.Close()==0 && !live);
    puts("PASS actual HII protocol snapshots, ownership, whole-language fallback, invalidation, growth/limits and callback lifetime");
}
