// Host test for src/efi/CharonFirmwareBrowser.h (r371): variable list and
// details, driver names through ComponentName2, ESRT text.
#include "../src/efi/CharonFirmwareBrowser.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
using namespace hakefi;
static bool Contains(const char16_t* text,const char16_t* part) {
    for(size_t i=0;text[i];++i) {size_t k=0;while(part[k] && text[i+k]==part[k])++k;if(!part[k])return true;}
    return false;
}
struct Var {const char16_t* name;efi_guid guid;uint32_t attributes;uint8_t data[64];size_t size;};
static Var vars[4];static size_t varCount=0;
static size_t Len(const char16_t* s) {size_t n=0;while(s[n])++n;return n;}
static efi_status EFIAPI FakeNext(size_t* size,char16_t* name,efi_guid* guid) {
    size_t next=0;
    if(name[0]) {for(size_t i=0;i<varCount;++i)if(!memcmp(vars[i].name,name,(Len(name)+1)*2) && !memcmp(&vars[i].guid,guid,16)) {next=i+1;break;}}
    if(next>=varCount)return EFI_NOT_FOUND;
    const size_t need=(Len(vars[next].name)+1)*2;
    if(*size<need) {*size=need;return EFI_BUFFER_TOO_SMALL;}
    memcpy(name,vars[next].name,need);*guid=vars[next].guid;*size=need;return EFI_SUCCESS;
}
static efi_status EFIAPI FakeGet(char16_t* name,efi_guid* guid,uint32_t* attributes,size_t* size,void* data) {
    for(size_t i=0;i<varCount;++i)if(!memcmp(vars[i].name,name,(Len(name)+1)*2) && !memcmp(&vars[i].guid,guid,16)) {
        if(attributes)*attributes=vars[i].attributes;
        if(*size<vars[i].size || !data) {*size=vars[i].size;return EFI_BUFFER_TOO_SMALL;}
        memcpy(data,vars[i].data,vars[i].size);*size=vars[i].size;return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}
static char16_t driverName[]=u"Intel HD Audio Driver";
static efi_status EFIAPI FakeDriverName(systools::ComponentName2*,char* language,char16_t** name) {
    if(strcmp(language,"en"))return EFI_UNSUPPORTED;*name=driverName;return EFI_SUCCESS;
}
static char languages[]="en;fr";
static systools::ComponentName2 component{FakeDriverName,nullptr,languages};
static systools::DriverBinding binding{nullptr,nullptr,nullptr,0x10,nullptr,nullptr};
static efi_handle handleArray[2]={(efi_handle)0x1000,(efi_handle)0x2000};
static efi_status EFIAPI FakeLocate(efi_locate_search_type,efi_guid*,void*,size_t* count,efi_handle** buffer) {*count=2;*buffer=handleArray;return EFI_SUCCESS;}
static efi_status EFIAPI FakeHandle(efi_handle handle,efi_guid* guid,void** out) {
    if(!memcmp(guid,&systools::componentName2Guid,16) && handle==handleArray[0]) {*out=&component;return EFI_SUCCESS;}
    if(!memcmp(guid,&systools::driverBindingGuid,16)) {*out=&binding;return EFI_SUCCESS;}
    return EFI_UNSUPPORTED;
}
static efi_status EFIAPI FakeFree(void*) {return EFI_SUCCESS;}
int main() {
    const efi_guid global=EFI_GLOBAL_VARIABLE;const efi_guid other={0x12345678,0,0,{1,2,3,4,5,6,7,8}};
    vars[0]={u"BootOrder",global,7,{1,0,0x0a,0},4};
    vars[1]={u"Boot0001",global,7,{1,0,0,0,4,0,'U',0,'S',0,'B',0,0,0},14};
    vars[2]={u"Timeout",global,7,{5,0},2};
    vars[3]={u"VendorBlob",other,3,{0xde,0xad,0xbe,0xef},4};varCount=4;
    efi_runtime_services runtime{};runtime.GetNextVariableName=FakeNext;runtime.GetVariable=FakeGet;
    auto& list=systools::SharedList();
    CHECK(systools::LoadVariables(&runtime,true,list)==EFI_SUCCESS && list.count==4 && !list.truncated);
    CHECK(Contains(list.rows[0],u"BootOrder, UEFI, 4 bajty"));
    CHECK(Contains(list.rows[3],u"VendorBlob, producent 12345678, 4 bajty"));
    char16_t text[1024];
    CHECK(systools::VariableDetail(&runtime,list,0,true,text,1024)==EFI_SUCCESS && Contains(text,u"Wartość: Boot0001, Boot000a."));
    CHECK(Contains(text,u"Atrybuty: trwała, dostępna przed systemem, dostępna dla systemu."));
    CHECK(systools::VariableDetail(&runtime,list,1,false,text,1024)==EFI_SUCCESS && Contains(text,u"Value: USB, active."));
    CHECK(systools::VariableDetail(&runtime,list,2,true,text,1024)==EFI_SUCCESS && Contains(text,u"Wartość: 5 s."));
    CHECK(systools::VariableDetail(&runtime,list,3,true,text,1024)==EFI_SUCCESS && Contains(text,u"dane binarne: de ad be ef."));
    varCount=3;CHECK(systools::VariableDetail(&runtime,list,3,true,text,1024)==EFI_NOT_FOUND && Contains(text,u"już nie istnieje"));
    // Drivers.
    efi_boot_services boot{};boot.LocateHandleBuffer=FakeLocate;boot.HandleProtocol=FakeHandle;boot.FreePool=FakeFree;
    CHECK(systools::LoadDrivers(&boot,true,list)==EFI_SUCCESS && list.count==2 && list.kind==systools::ListKind::drivers);
    CHECK(!memcmp(list.rows[0],u"Intel HD Audio Driver",22*2) && Contains(list.rows[1],u"Sterownik bez nazwy"));
    CHECK(systools::DriverDetail(&boot,list,0,true,text,1024)==EFI_SUCCESS && Contains(text,u"Wersja: 0x00000010."));
    char chosen[16];CHECK(systools::PickLanguage("en-US;pl-PL",true,chosen,16) && !strcmp(chosen,"pl-PL"));
    CHECK(systools::PickLanguage("eng;fra",false,chosen,16) && !strcmp(chosen,"eng"));
    // ESRT.
    efi_system_table table{};
    systools::EsrtText(&table,true,text,1024);CHECK(Contains(text,u"nie publikuje tablicy"));
    uint8_t esrt[16+40]={};esrt[0]=1;esrt[4]=1;esrt[16+16]=1;esrt[16+20]=7;esrt[16+24]=2;esrt[16+32]=8;esrt[16+36]=5;
    efi_configuration_table configuration[1]={{systools::esrtGuid,esrt}};table.NumberOfTableEntries=1;table.ConfigurationTable=configuration;
    systools::EsrtText(&table,true,text,1024);
    CHECK(Contains(text,u"Zasoby aktualizowane przez firmware: 1. 1. firmware systemu, wersja 7, najniższa dozwolona 2, ostatnia próba 8: błąd podpisu"));
    puts("CharonFirmwareBrowserTests PASS");
    return 0;
}
