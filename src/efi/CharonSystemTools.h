#pragma once
// r370: system tools page — computer information (firmware, UEFI revision,
// SMBIOS identity, processor, memory, variable storage, Secure Boot), the
// firmware clock, restart, shutdown and restart into firmware setup
// (OsIndications BOOT_TO_FW_UI, only when OsIndicationsSupported says so).
// Everything is read from firmware interfaces at the moment it is asked for;
// nothing is cached and missing data is said to be missing, never guessed.
#include <stddef.h>
#include <stdint.h>
#include <efi/system-table.h>
#include <efi/runtime-services.h>
#include <efi/boot-services.h>
#include "CharonClockZone.h"
namespace hakefi {
namespace systools {
// r409: \EFI\HAK\clock.txt, written by the finish step of a multi-system plan, says that the
// hardware clock keeps universal time and names the zone. Charon's clock tools then show and take
// local time. Loaded when the boot menu starts (LoadClockSetting); without the file nothing changes.
inline clockzone::Setting& ClockSetting() {static clockzone::Setting setting{false,nullptr};return setting;}
inline void LoadClockSetting(efi_file_protocol* root) {
    ClockSetting()=clockzone::Setting{false,nullptr};efi_file_protocol* file=nullptr;
    if(!root || root->Open(root,&file,(char16_t*)u"\\EFI\\HAK\\clock.txt",EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !file)return;
    char text[256];size_t size=sizeof(text);
    if(file->Read(file,&size,text)==EFI_SUCCESS)ClockSetting()=clockzone::Parse(text,size);
    file->Close(file);
}
// The clock reading as it is shown, and a shown time as it is stored.
inline void ShownTime(efi_time& time,bool* summer=nullptr) {
    if(summer)*summer=false;
    if(!ClockSetting().universal)return;
    const clockzone::Stamp shown=clockzone::Shown(ClockSetting(),clockzone::Stamp{time.Year,time.Month,time.Day,time.Hour,time.Minute,time.Second},summer);
    time.Year=uint16_t(shown.year);time.Month=shown.month;time.Day=shown.day;time.Hour=shown.hour;time.Minute=shown.minute;
}
inline void StoredTime(uint16_t& year,uint8_t& month,uint8_t& day,uint8_t& hour,uint8_t& minute) {
    if(!ClockSetting().universal)return;
    const clockzone::Stamp stored=clockzone::Stored(ClockSetting(),clockzone::Stamp{year,month,day,hour,minute,0});
    year=uint16_t(stored.year);month=stored.month;day=stored.day;hour=stored.hour;minute=stored.minute;
}
inline void Put(char16_t* out,size_t capacity,const char16_t* text) {
    size_t at=0;while(at+1<capacity && out[at])++at;
    while(at+1<capacity && text && *text)out[at++]=*text++;
    out[at]=0;
}
inline void PutNumber(char16_t* out,size_t capacity,uint64_t value,unsigned minimumDigits=1) {
    char16_t digits[24];unsigned count=0;
    do {digits[count++]=char16_t(u'0'+value%10);value/=10;} while(value && count<20);
    while(count<minimumDigits && count<20)digits[count++]=u'0';
    char16_t text[24];for(unsigned i=0;i<count;++i)text[i]=digits[count-1-i];text[count]=0;
    Put(out,capacity,text);
}
// SMBIOS strings are bytes (ASCII in practice). Non-ASCII bytes become '?'
// rather than being guessed into another code page; padding is trimmed.
inline void PutAscii(char16_t* out,size_t capacity,const char* text,size_t length) {
    while(length && (text[length-1]==' ' || text[length-1]=='\t'))--length;
    size_t start=0;while(start<length && (text[start]==' ' || text[start]=='\t'))++start;
    char16_t buffer[130];size_t at=0;
    for(size_t i=start;i<length && at+1<130;++i) {
        const unsigned char c=(unsigned char)text[i];
        buffer[at++]=c>=0x20 && c<0x7f?char16_t(c):u'?';
    }
    buffer[at]=0;Put(out,capacity,buffer);
}
inline bool GuidEqual(const efi_guid& a,const efi_guid& b) {
    const auto* x=(const uint8_t*)&a;const auto* y=(const uint8_t*)&b;
    for(size_t i=0;i<sizeof(efi_guid);++i)if(x[i]!=y[i])return false;
    return true;
}
inline uint16_t Le16(const uint8_t* p) {return uint16_t(p[0]|(p[1]<<8));}
inline uint32_t Le32(const uint8_t* p) {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline uint64_t Le64(const uint8_t* p) {return uint64_t(Le32(p))|(uint64_t(Le32(p+4))<<32);}
// Placeholder strings vendors leave in SMBIOS; they are not an identity.
inline bool Placeholder(const char* s,size_t n) {
    const char* words[]={"To Be Filled By O.E.M.","To be filled by O.E.M.","Default string","Not Specified","Not Applicable","System Product Name",
        "System manufacturer","System Version","None","N/A","Unknown","0123456789","OEM","O.E.M."};
    size_t start=0;while(start<n && s[start]==' ')++start;
    while(n>start && s[n-1]==' ')--n;
    if(n==start)return true;
    for(const char* word:words) {size_t k=0;while(word[k] && start+k<n && word[k]==s[start+k])++k;if(!word[k] && start+k==n)return true;}
    return false;
}
struct SmbiosTable {
    const uint8_t* data=nullptr;size_t size=0;uint8_t major=0,minor=0;
    bool Valid() const {return data && size;}
};
inline SmbiosTable FindSmbios(efi_system_table* table) {
    SmbiosTable result;
    if(!table || !table->ConfigurationTable)return result;
    const efi_guid v3=SMBIOS3_TABLE_GUID,v2=SMBIOS_TABLE_GUID;
    const uint8_t* entry3=nullptr;const uint8_t* entry2=nullptr;
    for(size_t i=0;i<table->NumberOfTableEntries;++i) {
        const auto& item=table->ConfigurationTable[i];
        if(GuidEqual(item.VendorGuid,v3))entry3=(const uint8_t*)item.VendorTable;
        else if(GuidEqual(item.VendorGuid,v2))entry2=(const uint8_t*)item.VendorTable;
    }
    // Anchors are checked; the length of the structure table is bounded.
    if(entry3 && entry3[0]=='_' && entry3[1]=='S' && entry3[2]=='M' && entry3[3]=='3' && entry3[4]=='_') {
        result.major=entry3[7];result.minor=entry3[8];
        result.size=Le32(entry3+0x0c);result.data=(const uint8_t*)(uintptr_t)Le64(entry3+0x10);
    } else if(entry2 && entry2[0]=='_' && entry2[1]=='S' && entry2[2]=='M' && entry2[3]=='_') {
        result.major=entry2[6];result.minor=entry2[7];
        result.size=Le16(entry2+0x16);result.data=(const uint8_t*)(uintptr_t)Le32(entry2+0x18);
    }
    if(result.size>0x100000)result.size=0x100000;
    if(!result.data)result.size=0;
    return result;
}
// Walks structures; Visit(type,formatted,length,strings,stringsEnd).
template<class Visit> void WalkSmbios(const SmbiosTable& table,Visit visit) {
    size_t at=0;
    while(table.Valid() && at+4<=table.size) {
        const uint8_t* s=table.data+at;const uint8_t type=s[0],length=s[1];
        if(length<4 || at+length>table.size)return;
        size_t end=at+length;
        while(end+1<table.size && !(table.data[end]==0 && table.data[end+1]==0))++end;
        if(end+1>=table.size)return;
        visit(type,s,length,(const char*)(table.data+at+length),(const char*)(table.data+end));
        if(type==127)return;
        at=end+2;
    }
}
// String number index (1-based) of a structure; false when absent.
inline bool SmbiosString(const char* strings,const char* end,uint8_t index,const char*& text,size_t& length) {
    if(!index)return false;
    const char* p=strings;
    for(uint8_t n=1;p<end;++n) {
        const char* q=p;while(q<end && *q)++q;
        if(n==index) {text=p;length=size_t(q-p);return length && !Placeholder(p,length);}
        if(q>=end)break;
        p=q+1;
    }
    return false;
}
inline void PutRevision(char16_t* out,size_t capacity,uint32_t revision) {
    // UEFI header revision: major<<16 | minor*10 + errata (2.70 = 2.7, 2.31 = 2.3.1).
    const uint32_t major=revision>>16,minor=revision&0xffff;
    PutNumber(out,capacity,major);Put(out,capacity,u".");PutNumber(out,capacity,minor/10);
    if(minor%10) {Put(out,capacity,u".");PutNumber(out,capacity,minor%10);}
}
// Approximate installed memory from the memory map: every RAM type except
// reserved, unusable, MMIO, PAL and persistent memory.
inline efi_status MemoryBytes(efi_boot_services* boot,uint64_t& bytes) {
    bytes=0;
    if(!boot || !boot->GetMemoryMap || !boot->AllocatePool || !boot->FreePool)return EFI_UNSUPPORTED;
    size_t size=0,key=0,descriptor=0;uint32_t version=0;
    auto status=boot->GetMemoryMap(&size,nullptr,&key,&descriptor,&version);
    if(status!=EFI_BUFFER_TOO_SMALL)return status==EFI_SUCCESS?EFI_NOT_FOUND:status;
    if(!descriptor || descriptor<sizeof(efi_memory_descriptor))return EFI_INVALID_PARAMETER;
    for(unsigned attempt=0;attempt<3;++attempt) {
        size+=descriptor*8;void* buffer=nullptr;
        status=boot->AllocatePool(EfiLoaderData,size,&buffer);
        if(status!=EFI_SUCCESS)return status;
        size_t used=size;
        status=boot->GetMemoryMap(&used,(efi_memory_descriptor*)buffer,&key,&descriptor,&version);
        if(status==EFI_SUCCESS) {
            uint64_t pages=0;
            for(size_t at=0;at+descriptor<=used;at+=descriptor) {
                const auto* d=(const efi_memory_descriptor*)((const uint8_t*)buffer+at);
                const uint32_t type=d->Type;
                if((type>=EfiLoaderCode && type<=EfiConventionalMemory) || type==EfiACPIReclaimMemory || type==EfiACPIMemoryNVS)pages+=d->NumberOfPages;
            }
            boot->FreePool(buffer);bytes=pages*4096;return EFI_SUCCESS;
        }
        boot->FreePool(buffer);
        if(status!=EFI_BUFFER_TOO_SMALL)return status;
        size=used;
    }
    return EFI_BUFFER_TOO_SMALL;
}
// Size with one decimal; Polish uses a decimal comma.
inline void PutSize(char16_t* out,size_t capacity,uint64_t bytes,bool pl) {
    const uint64_t mib=(bytes+(1u<<19))>>20;
    if(bytes<(uint64_t(1)<<20)) {PutNumber(out,capacity,(bytes+1023)>>10);Put(out,capacity,u" KB");return;}
    if(mib<1024) {PutNumber(out,capacity,mib);Put(out,capacity,u" MB");return;}
    const uint64_t tenths=(mib*10+512)/1024;
    PutNumber(out,capacity,tenths/10);
    if(tenths%10) {Put(out,capacity,pl?u",":u".");PutNumber(out,capacity,tenths%10);}
    Put(out,capacity,u" GB");
}
inline void ComputerInfo(efi_system_table* table,bool pl,bool detailed,char16_t* out,size_t capacity) {
    out[0]=0;
    const SmbiosTable smbios=FindSmbios(table);
    // Manufacturer and model first: that is what a person asks for.
    const char* maker=nullptr;size_t makerLength=0;const char* model=nullptr;size_t modelLength=0;
    const char* boardMaker=nullptr;size_t boardMakerLength=0;const char* board=nullptr;size_t boardLength=0;
    const char* biosVendor=nullptr;size_t biosVendorLength=0;const char* biosVersion=nullptr;size_t biosVersionLength=0;
    const char* biosDate=nullptr;size_t biosDateLength=0;const char* cpu=nullptr;size_t cpuLength=0;
    unsigned cores=0,threads=0,processors=0;
    WalkSmbios(smbios,[&](uint8_t type,const uint8_t* s,uint8_t length,const char* strings,const char* end) {
        if(type==0 && length>=9) {SmbiosString(strings,end,s[4],biosVendor,biosVendorLength);SmbiosString(strings,end,s[5],biosVersion,biosVersionLength);SmbiosString(strings,end,s[8],biosDate,biosDateLength);}
        else if(type==1 && length>=8) {SmbiosString(strings,end,s[4],maker,makerLength);SmbiosString(strings,end,s[5],model,modelLength);}
        else if(type==2 && length>=8) {SmbiosString(strings,end,s[4],boardMaker,boardMakerLength);SmbiosString(strings,end,s[5],board,boardLength);}
        else if(type==4 && length>=0x1a) {
            // Only populated sockets (status bit 6).
            if(length>0x18 && !(s[0x18]&0x40))return;
            ++processors;
            if(!cpu)SmbiosString(strings,end,s[0x10],cpu,cpuLength);
            if(length>0x23) {cores+=s[0x23];threads+=s[0x25];}
        }
    });
    Put(out,capacity,pl?u"Komputer: ":u"Computer: ");
    if(maker || model) {
        if(maker)PutAscii(out,capacity,maker,makerLength);
        if(maker && model)Put(out,capacity,u" ");
        if(model)PutAscii(out,capacity,model,modelLength);
    } else if(boardMaker || board) {
        Put(out,capacity,pl?u"płyta ":u"board ");
        if(boardMaker)PutAscii(out,capacity,boardMaker,boardMakerLength);
        if(boardMaker && board)Put(out,capacity,u" ");
        if(board)PutAscii(out,capacity,board,boardLength);
    } else Put(out,capacity,smbios.Valid()?(pl?u"producent i model nieopisane w SMBIOS":u"maker and model not described in SMBIOS"):(pl?u"brak tablicy SMBIOS":u"no SMBIOS table"));
    Put(out,capacity,u". ");
    if(cpu) {
        Put(out,capacity,pl?u"Procesor: ":u"Processor: ");PutAscii(out,capacity,cpu,cpuLength);
        // Polish count forms: 1 rdzeń, 2-4 rdzenie (not 12-14), else rdzeni.
        auto counted=[&](unsigned n,const char16_t* one,const char16_t* few,const char16_t* many,const char16_t* enOne,const char16_t* enMany) {
            Put(out,capacity,u", ");PutNumber(out,capacity,n);Put(out,capacity,u" ");
            if(!pl)Put(out,capacity,n==1?enOne:enMany);
            else Put(out,capacity,n==1?one:(n%10>=2 && n%10<=4 && (n%100<12 || n%100>14))?few:many);
        };
        if(cores)counted(cores,u"rdzeń",u"rdzenie",u"rdzeni",u"core",u"cores");
        if(threads && threads!=cores)counted(threads,u"wątek",u"wątki",u"wątków",u"thread",u"threads");
        if(processors>1)counted(processors,u"gniazdo",u"gniazda",u"gniazd",u"socket",u"sockets");
        Put(out,capacity,u". ");
    }
    uint64_t bytes=0;
    if(table && MemoryBytes(table->BootServices,bytes)==EFI_SUCCESS && bytes) {
        Put(out,capacity,pl?u"Pamięć: około ":u"Memory: about ");PutSize(out,capacity,bytes,pl);Put(out,capacity,u". ");
    } else Put(out,capacity,pl?u"Pamięć: nie odczytano mapy pamięci. ":u"Memory: memory map not read. ");
    Put(out,capacity,pl?u"Firmware: ":u"Firmware: ");
    if(table && table->FirmwareVendor && table->FirmwareVendor[0]) {
        char16_t vendor[96]={};size_t i=0;while(i+1<96 && table->FirmwareVendor[i]) {vendor[i]=table->FirmwareVendor[i];++i;}
        Put(out,capacity,vendor);
    } else Put(out,capacity,pl?u"dostawca nieznany":u"vendor unknown");
    if(table) {Put(out,capacity,pl?u", wersja ":u", revision ");PutNumber(out,capacity,table->FirmwareRevision>>16);Put(out,capacity,u".");PutNumber(out,capacity,table->FirmwareRevision&0xffff);}
    if(biosVersion) {Put(out,capacity,u", BIOS ");PutAscii(out,capacity,biosVersion,biosVersionLength);}
    if(biosDate) {Put(out,capacity,pl?u" z ":u" of ");PutAscii(out,capacity,biosDate,biosDateLength);}
    if(table) {Put(out,capacity,u", UEFI ");PutRevision(out,capacity,table->Hdr.Revision);}
    Put(out,capacity,u". ");
    if(table && table->RuntimeServices && table->RuntimeServices->GetVariable) {
        static efi_guid global=EFI_GLOBAL_VARIABLE;
        uint8_t secure=0;size_t size=1;
        const auto status=table->RuntimeServices->GetVariable((char16_t*)u"SecureBoot",&global,nullptr,&size,&secure);
        Put(out,capacity,status!=EFI_SUCCESS || size!=1 || secure>1?(pl?u"Secure Boot: stan nieznany. ":u"Secure Boot: status unknown. ")
            :secure?(pl?u"Secure Boot włączony. ":u"Secure Boot enabled. "):(pl?u"Secure Boot wyłączony. ":u"Secure Boot disabled. "));
    }
    if(detailed) {
        if(biosVendor) {Put(out,capacity,pl?u"Dostawca BIOS: ":u"BIOS vendor: ");PutAscii(out,capacity,biosVendor,biosVendorLength);Put(out,capacity,u". ");}
        if((maker || model) && (boardMaker || board)) {
            Put(out,capacity,pl?u"Płyta główna: ":u"Mainboard: ");
            if(boardMaker)PutAscii(out,capacity,boardMaker,boardMakerLength);
            if(boardMaker && board)Put(out,capacity,u" ");
            if(board)PutAscii(out,capacity,board,boardLength);
            Put(out,capacity,u". ");
        }
        if(smbios.Valid()) {Put(out,capacity,u"SMBIOS ");PutNumber(out,capacity,smbios.major);Put(out,capacity,u".");PutNumber(out,capacity,smbios.minor);Put(out,capacity,u". ");}
        auto* runtime=table?table->RuntimeServices:nullptr;
        if(runtime && runtime->QueryVariableInfo && (table->Hdr.Revision>>16)>=2) {
            uint64_t maximum=0,remaining=0,largest=0;
            if(runtime->QueryVariableInfo(EFI_VARIABLE_NON_VOLATILE|EFI_VARIABLE_BOOTSERVICE_ACCESS|EFI_VARIABLE_RUNTIME_ACCESS,&maximum,&remaining,&largest)==EFI_SUCCESS && maximum) {
                Put(out,capacity,pl?u"Pamięć zmiennych firmware: wolne ":u"Firmware variable storage: ");
                PutNumber(out,capacity,remaining/1024);Put(out,capacity,pl?u" z ":u" of ");PutNumber(out,capacity,maximum/1024);
                Put(out,capacity,pl?u" KB. ":u" KB free. ");
            }
        }
    }
}
inline unsigned Weekday(unsigned year,unsigned month,unsigned day) {
    static const unsigned offsets[]={0,3,2,5,0,3,5,1,4,6,2,4};
    if(month<3)--year;
    return (year+year/4-year/100+year/400+offsets[(month-1)%12]+day)%7; // 0 = Sunday
}
// The firmware real-time clock exactly as GetTime returns it. Many machines
// keep it in UTC (Linux) or local time (Windows); the zone is said only when
// firmware states one.
inline efi_status ClockText(efi_runtime_services* runtime,bool pl,char16_t* out,size_t capacity) {
    out[0]=0;
    if(!runtime || !runtime->GetTime)return EFI_UNSUPPORTED;
    efi_time time{};
    const auto status=runtime->GetTime(&time,nullptr);
    if(status!=EFI_SUCCESS)return status;
    if(time.Month<1 || time.Month>12 || time.Day<1 || time.Day>31 || time.Hour>23 || time.Minute>59 || time.Second>59)return EFI_DEVICE_ERROR;
    const bool universal=ClockSetting().universal;bool summer=false;ShownTime(time,&summer);   // r409
    static const char16_t* plDays[]={u"niedziela",u"poniedziałek",u"wtorek",u"środa",u"czwartek",u"piątek",u"sobota"};
    static const char16_t* enDays[]={u"Sunday",u"Monday",u"Tuesday",u"Wednesday",u"Thursday",u"Friday",u"Saturday"};
    static const char16_t* plMonths[]={u"stycznia",u"lutego",u"marca",u"kwietnia",u"maja",u"czerwca",u"lipca",u"sierpnia",u"września",u"października",u"listopada",u"grudnia"};
    static const char16_t* enMonths[]={u"January",u"February",u"March",u"April",u"May",u"June",u"July",u"August",u"September",u"October",u"November",u"December"};
    const unsigned weekday=Weekday(time.Year,time.Month,time.Day);
    Put(out,capacity,universal?(pl?u"Czas lokalny: ":u"Local time: "):(pl?u"Zegar firmware: ":u"Firmware clock: "));
    Put(out,capacity,pl?plDays[weekday]:enDays[weekday]);Put(out,capacity,u", ");
    PutNumber(out,capacity,time.Day);Put(out,capacity,u" ");Put(out,capacity,pl?plMonths[time.Month-1]:enMonths[time.Month-1]);
    Put(out,capacity,u" ");PutNumber(out,capacity,time.Year);Put(out,capacity,pl?u", godzina ":u", ");
    PutNumber(out,capacity,time.Hour,2);Put(out,capacity,u":");PutNumber(out,capacity,time.Minute,2);
    Put(out,capacity,u":");PutNumber(out,capacity,time.Second,2);Put(out,capacity,u". ");
    if(universal) {
        char16_t zone[40];size_t n=0;for(const char* c=ClockSetting().zone->name;*c && n+1<40;++c)zone[n++]=char16_t(*c);
        zone[n]=0;
        Put(out,capacity,pl?u"Strefa ":u"Zone ");Put(out,capacity,zone);
        Put(out,capacity,summer?(pl?u", czas letni. ":u", summer time. "):(pl?u", czas zimowy. ":u", standard time. "));
        Put(out,capacity,pl?u"Zegar komputera trzyma czas uniwersalny.":u"The computer's clock keeps universal time.");
        return EFI_SUCCESS;
    }
    if(time.TimeZone==EFI_UNSPECIFIED_TIMEZONE)Put(out,capacity,pl?u"Strefa czasowa nieokreślona przez firmware.":u"Time zone not stated by firmware.");
    else if(time.TimeZone>=-1440 && time.TimeZone<=1440) {
        // UEFI: local time = UTC - TimeZone (minutes).
        const int offset=-time.TimeZone;const unsigned magnitude=unsigned(offset<0?-offset:offset);
        Put(out,capacity,pl?u"Strefa UTC":u"Zone UTC");Put(out,capacity,offset<0?u"−":u"+");
        PutNumber(out,capacity,magnitude/60);if(magnitude%60) {Put(out,capacity,u":");PutNumber(out,capacity,magnitude%60,2);}
        Put(out,capacity,u".");
    }
    if(time.Daylight&EFI_TIME_IN_DAYLIGHT)Put(out,capacity,pl?u" Czas letni.":u" Daylight saving time.");
    return EFI_SUCCESS;
}
// Date and time words for the clock editor and its confirmation.
inline void DateWords(bool pl,uint16_t year,uint8_t month,uint8_t day,uint8_t hour,uint8_t minute,char16_t* out,size_t capacity) {
    static const char16_t* plDays[]={u"niedziela",u"poniedziałek",u"wtorek",u"środa",u"czwartek",u"piątek",u"sobota"};
    static const char16_t* enDays[]={u"Sunday",u"Monday",u"Tuesday",u"Wednesday",u"Thursday",u"Friday",u"Saturday"};
    static const char16_t* plMonths[]={u"stycznia",u"lutego",u"marca",u"kwietnia",u"maja",u"czerwca",u"lipca",u"sierpnia",u"września",u"października",u"listopada",u"grudnia"};
    static const char16_t* enMonths[]={u"January",u"February",u"March",u"April",u"May",u"June",u"July",u"August",u"September",u"October",u"November",u"December"};
    if(month<1 || month>12)return;
    const unsigned weekday=Weekday(year,month,day);
    Put(out,capacity,pl?plDays[weekday]:enDays[weekday]);Put(out,capacity,u", ");PutNumber(out,capacity,day);Put(out,capacity,u" ");
    Put(out,capacity,pl?plMonths[month-1]:enMonths[month-1]);Put(out,capacity,u" ");PutNumber(out,capacity,year);
    Put(out,capacity,pl?u", godzina ":u", ");PutNumber(out,capacity,hour,2);Put(out,capacity,u":");PutNumber(out,capacity,minute,2);
}
inline efi_status ReadClockFields(efi_runtime_services* runtime,uint16_t& year,uint8_t& month,uint8_t& day,uint8_t& hour,uint8_t& minute) {
    if(!runtime || !runtime->GetTime)return EFI_UNSUPPORTED;
    efi_time time{};const auto status=runtime->GetTime(&time,nullptr);
    if(status!=EFI_SUCCESS)return status;
    if(time.Month<1 || time.Month>12 || time.Day<1 || time.Day>31 || time.Hour>23 || time.Minute>59)return EFI_DEVICE_ERROR;
    ShownTime(time);   // r409: the editor works in local time
    year=time.Year<2000?2000:time.Year>2099?2099:time.Year;month=time.Month;day=time.Day;hour=time.Hour;minute=time.Minute;
    return EFI_SUCCESS;
}
// Sets the firmware clock, keeping its time zone and daylight flags; read
// back afterwards (the minute may have advanced by one meanwhile).
inline efi_status SetClock(efi_runtime_services* runtime,uint16_t year,uint8_t month,uint8_t day,uint8_t hour,uint8_t minute) {
    if(!runtime || !runtime->GetTime || !runtime->SetTime)return EFI_UNSUPPORTED;
    StoredTime(year,month,day,hour,minute);   // r409: the user gave local time; the clock keeps universal time
    efi_time time{};auto status=runtime->GetTime(&time,nullptr);
    if(status!=EFI_SUCCESS)return status;
    time.Year=year;time.Month=month;time.Day=day;time.Hour=hour;time.Minute=minute;time.Second=0;time.Nanosecond=0;
    status=runtime->SetTime(&time);
    if(status!=EFI_SUCCESS)return status;
    efi_time check{};
    if(runtime->GetTime(&check,nullptr)!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    const unsigned set=unsigned(hour)*60+minute,got=unsigned(check.Hour)*60+check.Minute;
    const bool sameMinute=check.Year==year && check.Month==month && check.Day==day && (got==set || got==(set+1)%1440);
    return sameMinute?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
// Wake-up alarm (r373). EFI_UNSUPPORTED is said as such; many PCs and VMs
// have no wake alarm through UEFI.
inline efi_status WakeState(efi_runtime_services* runtime,bool& enabled,bool& pending,efi_time& time) {
    enabled=pending=false;time=efi_time{};
    if(!runtime || !runtime->GetWakeupTime)return EFI_UNSUPPORTED;
    return runtime->GetWakeupTime(&enabled,&pending,&time);
}
inline efi_status WakeText(efi_runtime_services* runtime,bool pl,char16_t* out,size_t capacity) {
    out[0]=0;bool enabled,pending;efi_time time;
    const auto status=WakeState(runtime,enabled,pending,time);
    if(status==EFI_UNSUPPORTED) {Put(out,capacity,pl?u"To firmware nie obsługuje budzenia komputera o zadanej godzinie.":u"This firmware does not support a wake-up alarm.");return status;}
    if(status!=EFI_SUCCESS)return status;
    if(enabled && time.Month>=1 && time.Month<=12) {
        ShownTime(time);   // r409
        Put(out,capacity,pl?u"Budzenie włączone: ":u"Wake-up alarm on: ");DateWords(pl,time.Year,time.Month,time.Day,time.Hour,time.Minute,out,capacity);Put(out,capacity,u".");
    } else Put(out,capacity,pl?u"Budzenie wyłączone.":u"Wake-up alarm off.");
    if(pending)Put(out,capacity,pl?u" Alarm już wystąpił.":u" The alarm has fired.");
    return EFI_SUCCESS;
}
inline efi_status SetWake(efi_runtime_services* runtime,uint16_t year,uint8_t month,uint8_t day,uint8_t hour,uint8_t minute) {
    if(!runtime || !runtime->GetTime || !runtime->SetWakeupTime || !runtime->GetWakeupTime)return EFI_UNSUPPORTED;
    StoredTime(year,month,day,hour,minute);   // r409
    efi_time time{};auto status=runtime->GetTime(&time,nullptr);
    if(status!=EFI_SUCCESS)return status;
    time.Year=year;time.Month=month;time.Day=day;time.Hour=hour;time.Minute=minute;time.Second=0;time.Nanosecond=0;
    status=runtime->SetWakeupTime(true,&time);
    if(status!=EFI_SUCCESS)return status;
    bool enabled=false,pending=false;efi_time check{};
    if(runtime->GetWakeupTime(&enabled,&pending,&check)!=EFI_SUCCESS || !enabled)return EFI_DEVICE_ERROR;
    return check.Year==year && check.Month==month && check.Day==day && check.Hour==hour && check.Minute==minute?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
inline efi_status DisableWake(efi_runtime_services* runtime) {
    if(!runtime || !runtime->SetWakeupTime || !runtime->GetWakeupTime)return EFI_UNSUPPORTED;
    auto status=runtime->SetWakeupTime(false,nullptr);
    if(status!=EFI_SUCCESS)return status;
    bool enabled=true,pending=false;efi_time check{};
    if(runtime->GetWakeupTime(&enabled,&pending,&check)!=EFI_SUCCESS || enabled)return EFI_DEVICE_ERROR;
    return EFI_SUCCESS;
}
// Connect every driver to every controller (recursive), as firmware does for
// a full boot, so devices that appeared or were skipped by a fast boot get
// their file systems. Returns how many handles accepted a connection.
inline efi_status ConnectAll(efi_boot_services* boot,size_t& connected) {
    connected=0;
    if(!boot || !boot->LocateHandleBuffer || !boot->ConnectController || !boot->FreePool)return EFI_UNSUPPORTED;
    efi_handle* handles=nullptr;size_t count=0;
    const auto status=boot->LocateHandleBuffer(AllHandles,nullptr,nullptr,&count,&handles);
    if(status!=EFI_SUCCESS)return status;
    for(size_t i=0;i<count;++i)if(boot->ConnectController(handles[i],nullptr,nullptr,true)==EFI_SUCCESS)++connected;
    boot->FreePool(handles);
    return EFI_SUCCESS;
}
inline bool FirmwareSetupSupported(efi_runtime_services* runtime) {
    if(!runtime || !runtime->GetVariable)return false;
    static efi_guid global=EFI_GLOBAL_VARIABLE;
    uint64_t supported=0;size_t size=8;
    return runtime->GetVariable((char16_t*)u"OsIndicationsSupported",&global,nullptr,&size,&supported)==EFI_SUCCESS
        && size==8 && (supported&EFI_OS_INDICATIONS_BOOT_TO_FW_UI);
}
// Sets BOOT_TO_FW_UI in OsIndications, keeping other bits; read back before
// reporting success. Firmware clears it on the next start.
inline efi_status RequestFirmwareSetup(efi_runtime_services* runtime) {
    if(!FirmwareSetupSupported(runtime) || !runtime->SetVariable)return EFI_UNSUPPORTED;
    static efi_guid global=EFI_GLOBAL_VARIABLE;
    uint64_t value=0;size_t size=8;uint32_t attributes=0;
    auto status=runtime->GetVariable((char16_t*)u"OsIndications",&global,&attributes,&size,&value);
    if(status==EFI_NOT_FOUND) {value=0;attributes=0;}
    else if(status!=EFI_SUCCESS)return status;
    else if(size!=8)return EFI_INVALID_PARAMETER;
    if(!attributes)attributes=EFI_VARIABLE_NON_VOLATILE|EFI_VARIABLE_BOOTSERVICE_ACCESS|EFI_VARIABLE_RUNTIME_ACCESS;
    value|=EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
    status=runtime->SetVariable((char16_t*)u"OsIndications",&global,attributes,8,&value);
    if(status!=EFI_SUCCESS)return status;
    uint64_t check=0;size=8;
    if(runtime->GetVariable((char16_t*)u"OsIndications",&global,nullptr,&size,&check)!=EFI_SUCCESS || size!=8 || !(check&EFI_OS_INDICATIONS_BOOT_TO_FW_UI))return EFI_DEVICE_ERROR;
    return EFI_SUCCESS;
}
}
}
