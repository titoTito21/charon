// Host test for src/efi/CharonSystemTools.h (r370). Build with an include
// directory holding src/efi/haiku as "efi" (see tools/build-charon-windows.py).
#include "../src/efi/CharonSystemTools.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
using namespace hakefi;
static bool Contains(const char16_t* text,const char16_t* part) {
    for(size_t i=0;text[i];++i) {size_t k=0;while(part[k] && text[i+k]==part[k])++k;if(!part[k])return true;}
    return false;
}
static efi_time clockValue;static efi_status clockStatus=EFI_SUCCESS;
static efi_status EFIAPI FakeGetTime(efi_time* t,efi_time_capabilities*) {*t=clockValue;return clockStatus;}
static uint64_t supported=0,indications=0;static bool indicationsPresent=false;
static efi_status EFIAPI FakeGetVariable(char16_t* name,efi_guid*,uint32_t* attributes,size_t* size,void* data) {
    auto same=[&](const char16_t* s) {size_t i=0;while(s[i] && name[i]==s[i])++i;return !s[i] && !name[i];};
    if(same(u"OsIndicationsSupported")) {if(*size<8) {*size=8;return EFI_BUFFER_TOO_SMALL;}*size=8;memcpy(data,&supported,8);return EFI_SUCCESS;}
    if(same(u"OsIndications")) {if(!indicationsPresent)return EFI_NOT_FOUND;if(attributes)*attributes=7;*size=8;memcpy(data,&indications,8);return EFI_SUCCESS;}
    if(same(u"SecureBoot")) {*size=1;*(uint8_t*)data=1;return EFI_SUCCESS;}
    return EFI_NOT_FOUND;
}
static efi_status EFIAPI FakeSetVariable(char16_t*,efi_guid*,uint32_t attributes,size_t size,void* data) {
    CHECK(attributes==7 && size==8);memcpy(&indications,data,8);indicationsPresent=true;return EFI_SUCCESS;
}
static std::vector<uint8_t> pool;
static efi_status EFIAPI FakeMemoryMap(size_t* size,efi_memory_descriptor* map,size_t* key,size_t* descriptor,uint32_t* version) {
    const size_t need=3*sizeof(efi_memory_descriptor);*descriptor=sizeof(efi_memory_descriptor);*version=1;*key=1;
    if(*size<need) {*size=need;return EFI_BUFFER_TOO_SMALL;}
    memset(map,0,need);
    map[0].Type=EfiConventionalMemory;map[0].NumberOfPages=262144*2-256; // ~2 GiB minus 1 MiB
    map[1].Type=EfiBootServicesData;map[1].NumberOfPages=256;
    map[2].Type=EfiMemoryMappedIO;map[2].NumberOfPages=1000000;      // not RAM
    *size=need;return EFI_SUCCESS;
}
static efi_status EFIAPI FakeAllocate(efi_memory_type,size_t size,void** out) {pool.assign(size,0);*out=pool.data();return EFI_SUCCESS;}
static efi_status EFIAPI FakeFree(void*) {return EFI_SUCCESS;}
int main() {
    CHECK(systools::Weekday(2026,11,17)==2); // Tuesday
    CHECK(systools::Weekday(2000,1,1)==6);
    char16_t text[1024]={};
    systools::PutRevision(text,64,(2<<16)|70);CHECK(!memcmp(text,u"2.7",8));
    text[0]=0;systools::PutRevision(text,64,(2<<16)|31);CHECK(!memcmp(text,u"2.3.1",12));
    text[0]=0;systools::PutSize(text,64,uint64_t(1536)<<20,true);CHECK(!memcmp(text,u"1,5 GB",14));
    text[0]=0;systools::PutSize(text,64,uint64_t(512)<<20,false);CHECK(!memcmp(text,u"512 MB",14));
    // SMBIOS 3 table: type 0, type 1 (placeholder product), type 4, end.
    std::vector<uint8_t> s;
    auto structure=[&](std::initializer_list<uint8_t> formatted,std::initializer_list<const char*> strings) {
        for(auto b:formatted)s.push_back(b);
        for(auto* str:strings) {while(*str)s.push_back(uint8_t(*str++));s.push_back(0);}
        if(!strings.size())s.push_back(0);
        s.push_back(0);
    };
    structure({0,9,0,0, 1,2,0,0, 3},{"EFI Development Kit II / OVMF","0.0.0","02/06/2015"});
    s.resize(s.size());
    structure({1,0x1b,1,0, 1,2,3,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0},{"QEMU","Standard PC (Q35 + ICH9, 2009)","Default string"});
    std::vector<uint8_t> cpu(0x2a,0);cpu[0]=4;cpu[1]=0x2a;cpu[0x10]=1;cpu[0x18]=0x41;cpu[0x23]=4;cpu[0x25]=8;
    for(auto b:cpu)s.push_back(b);
    for(const char* p="QEMU Virtual CPU version 2.5+";*p;++p)s.push_back(uint8_t(*p));s.push_back(0);s.push_back(0);
    structure({127,4,0,0},{});
    uint8_t entry[0x18]={'_','S','M','3','_',0,0x18,3,0};
    const uint32_t length=uint32_t(s.size());memcpy(entry+0x0c,&length,4);const uint64_t address=uint64_t(uintptr_t(s.data()));memcpy(entry+0x10,&address,8);
    efi_configuration_table configuration[1]={{SMBIOS3_TABLE_GUID,entry}};
    efi_runtime_services runtime{};runtime.GetTime=FakeGetTime;runtime.GetVariable=FakeGetVariable;runtime.SetVariable=FakeSetVariable;
    efi_boot_services boot{};boot.GetMemoryMap=FakeMemoryMap;boot.AllocatePool=FakeAllocate;boot.FreePool=FakeFree;
    efi_system_table table{};table.Hdr.Revision=(2<<16)|70;table.FirmwareVendor=(char16_t*)u"EDK II";table.FirmwareRevision=0x10000;
    table.RuntimeServices=&runtime;table.BootServices=&boot;table.NumberOfTableEntries=1;table.ConfigurationTable=configuration;
    systools::ComputerInfo(&table,true,true,text,1024);
    CHECK(Contains(text,u"Komputer: QEMU Standard PC (Q35 + ICH9, 2009). "));
    CHECK(!Contains(text,u"Default string"));
    CHECK(Contains(text,u"Procesor: QEMU Virtual CPU version 2.5+, 4 rdzenie, 8 wątków."));
    CHECK(Contains(text,u"Pamięć: około 2 GB."));
    CHECK(Contains(text,u"Firmware: EDK II, wersja 1.0, BIOS 0.0.0 z 02/06/2015, UEFI 2.7."));
    CHECK(Contains(text,u"Secure Boot włączony."));
    CHECK(Contains(text,u"Dostawca BIOS: EFI Development Kit II / OVMF."));
    CHECK(Contains(text,u"SMBIOS 3.0."));
    // No SMBIOS at all.
    table.NumberOfTableEntries=0;text[0]=0;systools::ComputerInfo(&table,false,false,text,1024);
    CHECK(Contains(text,u"Computer: no SMBIOS table."));
    // Clock.
    clockValue={2026,11,17,7,5,9,0,0,-60,EFI_TIME_IN_DAYLIGHT,0};
    CHECK(systools::ClockText(&runtime,true,text,400)==EFI_SUCCESS);
    CHECK(Contains(text,u"wtorek, 17 listopada 2026, godzina 07:05:09. Strefa UTC+1. Czas letni."));
    clockValue.TimeZone=EFI_UNSPECIFIED_TIMEZONE;clockValue.Daylight=0;
    CHECK(systools::ClockText(&runtime,false,text,400)==EFI_SUCCESS && Contains(text,u"Tuesday, 17 November 2026, 07:05:09. Time zone not stated"));
    clockValue.Month=13;CHECK(systools::ClockText(&runtime,false,text,400)==EFI_DEVICE_ERROR);
    clockStatus=EFI_DEVICE_ERROR;CHECK(systools::ClockText(&runtime,false,text,400)==EFI_DEVICE_ERROR);
    // Firmware setup request only when advertised; other bits kept.
    CHECK(!systools::FirmwareSetupSupported(&runtime) && systools::RequestFirmwareSetup(&runtime)==EFI_UNSUPPORTED && !indicationsPresent);
    supported=EFI_OS_INDICATIONS_BOOT_TO_FW_UI|EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED;
    CHECK(systools::FirmwareSetupSupported(&runtime) && systools::RequestFirmwareSetup(&runtime)==EFI_SUCCESS && indications==1);
    indications=EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED;
    CHECK(systools::RequestFirmwareSetup(&runtime)==EFI_SUCCESS && indications==5);
    // r372 SetClock keeps the zone, verifies by reading back.
    clockStatus=EFI_SUCCESS;clockValue={2026,11,17,7,5,9,0,0,-60,0,0};
    runtime.SetTime=[](efi_time* t) EFIAPI -> efi_status {clockValue=*t;return EFI_SUCCESS;};
    CHECK(systools::SetClock(&runtime,2027,2,28,9,30)==EFI_SUCCESS && clockValue.Year==2027 && clockValue.TimeZone==-60 && clockValue.Second==0);
    runtime.SetTime=[](efi_time*) EFIAPI -> efi_status {return EFI_SUCCESS;};
    CHECK(systools::SetClock(&runtime,2028,1,1,0,0)==EFI_DEVICE_ERROR);
    uint16_t y;uint8_t mo,d,h,mi;CHECK(systools::ReadClockFields(&runtime,y,mo,d,h,mi)==EFI_SUCCESS && y==2027 && mo==2 && h==9 && mi==30);
    text[0]=0;systools::DateWords(true,2027,2,28,9,30,text,200);CHECK(Contains(text,u"niedziela, 28 lutego 2027, godzina 09:30"));
    // r373 wake-up alarm: unsupported said plainly; set/disable verified by reading back.
    static efi_time wake{};static bool wakeOn=false;
    runtime.GetWakeupTime=nullptr;CHECK(systools::WakeText(&runtime,true,text,300)==EFI_UNSUPPORTED && Contains(text,u"nie obsługuje budzenia"));
    runtime.GetWakeupTime=[](bool* on,bool* pending,efi_time* t) EFIAPI -> efi_status {*on=wakeOn;*pending=false;*t=wake;return EFI_SUCCESS;};
    runtime.SetWakeupTime=[](bool on,efi_time* t) EFIAPI -> efi_status {wakeOn=on;if(t)wake=*t;return EFI_SUCCESS;};
    runtime.SetTime=[](efi_time* t) EFIAPI -> efi_status {clockValue=*t;return EFI_SUCCESS;};
    CHECK(systools::SetWake(&runtime,2027,3,1,6,45)==EFI_SUCCESS && wakeOn && wake.Hour==6 && wake.TimeZone==-60);
    CHECK(systools::WakeText(&runtime,true,text,300)==EFI_SUCCESS && Contains(text,u"Budzenie włączone: poniedziałek, 1 marca 2027, godzina 06:45."));
    CHECK(systools::DisableWake(&runtime)==EFI_SUCCESS && !wakeOn);
    CHECK(systools::WakeText(&runtime,false,text,300)==EFI_SUCCESS && Contains(text,u"Wake-up alarm off."));
    puts("CharonSystemToolsTests PASS");
    return 0;
}
