#pragma once
// r371: read-only firmware browsers on the system tools page.
// - Firmware variables: every variable GetNextVariableName returns, with its
//   namespace and size; Enter reads one variable and explains known ones
//   (BootOrder, Boot####, Timeout, languages, Secure Boot modes...).
// - UEFI drivers: every DriverBinding handle with its ComponentName2 name.
// - Firmware updates: the ESRT (resources, versions, last update result).
// Nothing is written. Lists are built when the page opens; each detail is
// read again from firmware when asked for, never from a cached copy.
#include "CharonSystemTools.h"
#include <efi/protocol/loaded-image.h>
namespace hakefi {
namespace systools {
enum class ListKind : uint8_t {none,variables,drivers,files /* r387 */};
constexpr size_t listRows=512,listRowText=112,variableNameUnits=64;
struct InfoList {
    ListKind kind;
    size_t count;
    bool truncated;
    char16_t rows[listRows][listRowText];
    char16_t names[listRows][variableNameUnits];
    efi_guid guids[listRows];
    efi_handle handles[listRows];
};
inline InfoList& SharedList() {static InfoList list;return list;}
static const efi_guid globalNamespace=EFI_GLOBAL_VARIABLE;
static const efi_guid imageSecurityNamespace={0xd719b2cb,0x3d3a,0x4596,{0xa3,0xbc,0xda,0xd0,0x0e,0x67,0x65,0x6f}};
static const efi_guid shimNamespace={0x605dab50,0xe046,0x4300,{0xab,0xb6,0x3d,0xd8,0x10,0xdd,0x8b,0x23}};
static const efi_guid driverBindingGuid={0x18a031ab,0xb443,0x4d1a,{0xa5,0xc0,0x0c,0x09,0x26,0x1e,0x9f,0x71}};
static const efi_guid componentName2Guid={0x6a7a5cff,0xe8d9,0x4f70,{0xba,0xda,0x75,0xab,0x30,0x25,0xce,0x14}};
static const efi_guid loadedImageGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
static const efi_guid driverHealthGuid={0x2a534210,0x9280,0x41d8,{0xae,0x79,0xca,0xda,0x01,0xa2,0xb1,0x27}};
static const efi_guid esrtGuid={0xb122a263,0x3661,0x4f68,{0x99,0x29,0x78,0xf8,0xb0,0xd6,0x21,0x80}};
struct ComponentName2 {
    efi_status (EFIAPI *GetDriverName)(ComponentName2*,char* language,char16_t** name);
    void* GetControllerName;
    char* SupportedLanguages;
};
// r387: EFI_DRIVER_HEALTH_PROTOCOL (UEFI 2.11 section 11.10), status only.
struct DriverHealth {
    efi_status (EFIAPI *GetHealthStatus)(DriverHealth*,efi_handle controller,efi_handle child,uint32_t* status,void** messages,efi_handle* formHii);
    void* Repair;
};
struct DriverBinding {
    void* Supported;void* Start;void* Stop;
    uint32_t Version;
    efi_handle ImageHandle,DriverBindingHandle;
};
inline void PutHex(char16_t* out,size_t capacity,uint64_t value,unsigned digits) {
    char16_t text[20];if(digits>16)digits=16;
    for(unsigned i=0;i<digits;++i) {const unsigned n=unsigned(value>>((digits-1-i)*4))&15;text[i]=char16_t(n<10?u'0'+n:u'a'+n-10);}
    text[digits]=0;Put(out,capacity,text);
}
inline void PutGuid(char16_t* out,size_t capacity,const efi_guid& guid) {
    const auto* b=(const uint8_t*)&guid;
    PutHex(out,capacity,Le32(b),8);Put(out,capacity,u"-");PutHex(out,capacity,Le16(b+4),4);Put(out,capacity,u"-");PutHex(out,capacity,Le16(b+6),4);Put(out,capacity,u"-");
    for(int i=8;i<16;++i) {if(i==10)Put(out,capacity,u"-");PutHex(out,capacity,b[i],2);}
}
inline void PutNamespace(char16_t* out,size_t capacity,const efi_guid& guid,bool pl) {
    if(GuidEqual(guid,globalNamespace))Put(out,capacity,pl?u"UEFI":u"UEFI");
    else if(GuidEqual(guid,imageSecurityNamespace))Put(out,capacity,pl?u"baza Secure Boot":u"Secure Boot database");
    else if(GuidEqual(guid,shimNamespace))Put(out,capacity,u"shim");
    else {Put(out,capacity,pl?u"producent ":u"vendor ");PutHex(out,capacity,Le32((const uint8_t*)&guid),8);}
}
inline void PutBytes(char16_t* out,size_t capacity,uint64_t bytes,bool pl) {
    PutNumber(out,capacity,bytes);
    if(!pl)Put(out,capacity,bytes==1?u" byte":u" bytes");
    else Put(out,capacity,bytes==1?u" bajt":(bytes%10>=2 && bytes%10<=4 && (bytes%100<12 || bytes%100>14))?u" bajty":u" bajtów");
}
inline bool SameName(const char16_t* a,const char16_t* b) {size_t i=0;while(a[i] && a[i]==b[i])++i;return a[i]==b[i];}
// Boot0001-style names: prefix plus exactly four hex digits.
inline bool OptionName(const char16_t* name,const char16_t* prefix) {
    size_t i=0;while(prefix[i]) {if(name[i]!=prefix[i])return false;++i;}
    for(size_t k=0;k<4;++k) {const char16_t c=name[i+k];if(!((c>=u'0' && c<=u'9') || (c>=u'A' && c<=u'F')))return false;}
    return !name[i+4];
}
// Variables, sorted by namespace then name as firmware returns them.
inline efi_status LoadVariables(efi_runtime_services* runtime,bool pl,InfoList& list) {
    list.kind=ListKind::variables;list.count=0;list.truncated=false;
    if(!runtime || !runtime->GetNextVariableName || !runtime->GetVariable)return EFI_UNSUPPORTED;
    char16_t name[variableNameUnits]={};efi_guid guid{};
    for(unsigned guard=0;guard<4096;++guard) {
        size_t size=sizeof(name);
        const auto status=runtime->GetNextVariableName(&size,name,&guid);
        if(status==EFI_NOT_FOUND)return EFI_SUCCESS;
        if(status==EFI_BUFFER_TOO_SMALL) {
            // A name longer than the table holds cannot be continued from;
            // say the list is incomplete rather than skip silently.
            list.truncated=true;return EFI_SUCCESS;
        }
        if(status!=EFI_SUCCESS)return list.count?EFI_SUCCESS:status;
        if(list.count>=listRows) {list.truncated=true;return EFI_SUCCESS;}
        auto& row=list.rows[list.count];row[0]=0;
        for(size_t i=0;i<variableNameUnits;++i)list.names[list.count][i]=name[i];
        list.guids[list.count]=guid;
        Put(row,listRowText,name);Put(row,listRowText,u", ");PutNamespace(row,listRowText,guid,pl);
        size_t bytes=0;efi_guid copy=guid;
        const auto sized=runtime->GetVariable(name,&copy,nullptr,&bytes,nullptr);
        if(sized==EFI_BUFFER_TOO_SMALL || sized==EFI_SUCCESS) {Put(row,listRowText,u", ");PutBytes(row,listRowText,bytes,pl);}
        ++list.count;
    }
    list.truncated=true;return EFI_SUCCESS;
}
inline void PutAttributes(char16_t* out,size_t capacity,uint32_t attributes,bool pl) {
    bool any=false;
    auto add=[&](uint32_t bit,const char16_t* plText,const char16_t* enText) {
        if(!(attributes&bit))return;
        if(any)Put(out,capacity,u", ");Put(out,capacity,pl?plText:enText);any=true;
    };
    add(EFI_VARIABLE_NON_VOLATILE,u"trwała",u"non-volatile");
    add(EFI_VARIABLE_BOOTSERVICE_ACCESS,u"dostępna przed systemem",u"boot-service access");
    add(EFI_VARIABLE_RUNTIME_ACCESS,u"dostępna dla systemu",u"runtime access");
    add(EFI_VARIABLE_HARDWARE_ERROR_RECORD,u"zapis błędu sprzętu",u"hardware error record");
    add(EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS|EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS,u"zapis tylko z podpisem",u"signed writes only");
    add(EFI_VARIABLE_APPEND_WRITE,u"dopisywanie",u"append write");
    if(!any)Put(out,capacity,pl?u"brak":u"none");
}
// Printable text when the data is plain ASCII or UTF-16; false otherwise.
inline bool PutTextData(char16_t* out,size_t capacity,const uint8_t* data,size_t size) {
    if(!size)return false;
    bool ascii=true;size_t end=size;while(end && !data[end-1])--end;
    if(!end)return false;
    for(size_t i=0;i<end;++i)if(data[i]<0x20 || data[i]>=0x7f)ascii=false;
    if(ascii) {PutAscii(out,capacity,(const char*)data,end>200?200:end);return true;}
    if(size%2)return false;
    size_t units=size/2;while(units && !Le16(data+2*(units-1)))--units;
    if(!units)return false;
    char16_t text[201];size_t n=0;
    for(size_t i=0;i<units && n<200;++i) {const uint16_t c=Le16(data+2*i);if(c<0x20 || (c>=0x7f && c<0xa0) || c>=0x2100)return false;text[n++]=c;}
    text[n]=0;Put(out,capacity,text);return true;
}
inline void PutHexBytes(char16_t* out,size_t capacity,const uint8_t* data,size_t size,size_t limit) {
    for(size_t i=0;i<size && i<limit;++i) {if(i)Put(out,capacity,u" ");PutHex(out,capacity,data[i],2);}
    if(size>limit)Put(out,capacity,u" …");
}
inline efi_status VariableDetail(efi_runtime_services* runtime,const InfoList& list,size_t index,bool pl,char16_t* out,size_t capacity) {
    out[0]=0;
    if(list.kind!=ListKind::variables || index>=list.count)return EFI_INVALID_PARAMETER;
    static uint8_t data[8192];
    const char16_t* name=list.names[index];efi_guid guid=list.guids[index];
    uint32_t attributes=0;size_t size=sizeof(data);
    auto status=runtime->GetVariable((char16_t*)name,&guid,&attributes,&size,data);
    Put(out,capacity,name);Put(out,capacity,u". ");
    if(status==EFI_NOT_FOUND) {Put(out,capacity,pl?u"Zmienna już nie istnieje.":u"The variable no longer exists.");return status;}
    const bool whole=status==EFI_SUCCESS;
    if(status==EFI_BUFFER_TOO_SMALL) {
        // Too big for the detail buffer: say its size and attributes only.
        status=runtime->GetVariable((char16_t*)name,&guid,&attributes,&size,nullptr);
        if(status!=EFI_BUFFER_TOO_SMALL && status!=EFI_SUCCESS)return status;
    } else if(!whole)return status;
    Put(out,capacity,pl?u"Przestrzeń: ":u"Namespace: ");PutNamespace(out,capacity,guid,pl);
    Put(out,capacity,u" (");PutGuid(out,capacity,guid);Put(out,capacity,u"). ");
    Put(out,capacity,pl?u"Rozmiar: ":u"Size: ");PutBytes(out,capacity,size,pl);Put(out,capacity,u". ");
    Put(out,capacity,pl?u"Atrybuty: ":u"Attributes: ");PutAttributes(out,capacity,attributes,pl);Put(out,capacity,u". ");
    if(!whole) {Put(out,capacity,pl?u"Zawartość zbyt duża, by ją tu odczytać.":u"Content too large to read here.");return EFI_SUCCESS;}
    const bool global=GuidEqual(guid,globalNamespace);
    Put(out,capacity,pl?u"Wartość: ":u"Value: ");
    if(global && (SameName(name,u"BootOrder") || SameName(name,u"DriverOrder") || SameName(name,u"SysPrepOrder")) && !(size%2)) {
        const char16_t* prefix=SameName(name,u"DriverOrder")?u"Driver":SameName(name,u"SysPrepOrder")?u"SysPrep":u"Boot";
        if(!size)Put(out,capacity,pl?u"pusta lista":u"empty list");
        for(size_t i=0;i<size/2 && i<48;++i) {if(i)Put(out,capacity,u", ");Put(out,capacity,prefix);PutHex(out,capacity,Le16(data+2*i),4);}
        if(size/2>48)Put(out,capacity,u" …");
    } else if(global && (SameName(name,u"BootCurrent") || SameName(name,u"BootNext")) && size==2) {
        Put(out,capacity,u"Boot");PutHex(out,capacity,Le16(data),4);
    } else if(global && SameName(name,u"Timeout") && size==2) {
        const uint16_t seconds=Le16(data);
        if(seconds==0xffff)Put(out,capacity,pl?u"czekaj bez końca":u"wait forever");
        else {PutNumber(out,capacity,seconds);Put(out,capacity,u" s");}
    } else if(global && (OptionName(name,u"Boot") || OptionName(name,u"Driver") || OptionName(name,u"SysPrep") || OptionName(name,u"PlatformRecovery")) && size>=6) {
        const uint32_t optionAttributes=Le32(data);
        char16_t description[160]={};size_t n=0;
        for(size_t at=6;at+1<size && n<159;at+=2) {const uint16_t c=Le16(data+at);if(!c)break;description[n++]=c<0x20?u' ':c;}
        Put(out,capacity,n?description:(pl?u"bez opisu":u"no description"));
        Put(out,capacity,optionAttributes&1?(pl?u", aktywny":u", active"):(pl?u", nieaktywny":u", inactive"));
        if(optionAttributes&8)Put(out,capacity,pl?u", ukryty":u", hidden");
    } else if(global && (SameName(name,u"SecureBoot") || SameName(name,u"SetupMode") || SameName(name,u"AuditMode") || SameName(name,u"DeployedMode")) && size==1) {
        Put(out,capacity,data[0]==1?(pl?u"włączony":u"on"):data[0]==0?(pl?u"wyłączony":u"off"):u"?");
    } else if(global && (SameName(name,u"OsIndicationsSupported") || SameName(name,u"OsIndications")) && size==8) {
        const uint64_t bits=Le64(data);Put(out,capacity,u"0x");PutHex(out,capacity,bits,16);
        if(bits&EFI_OS_INDICATIONS_BOOT_TO_FW_UI)Put(out,capacity,pl?u", start ustawień firmware":u", boot to firmware setup");
        if(bits&EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED)Put(out,capacity,pl?u", kapsuły z pliku":u", file capsules");
        if(bits&EFI_OS_INDICATIONS_FMP_CAPSULE_SUPPORTED)Put(out,capacity,pl?u", kapsuły FMP":u", FMP capsules");
        if(bits&EFI_OS_INDICATIONS_START_OS_RECOVERY)Put(out,capacity,pl?u", odzyskiwanie systemu":u", OS recovery");
    } else if(!PutTextData(out,capacity,data,size)) {
        Put(out,capacity,pl?u"dane binarne: ":u"binary data: ");PutHexBytes(out,capacity,data,size,24);
    }
    {size_t end=0;while(out[end])++end;if(!end || out[end-1]!=u'…')Put(out,capacity,u".");}
    return EFI_SUCCESS;
}
inline bool PickLanguage(const char* supported,bool pl,char* chosen,size_t capacity) {
    if(!supported || !*supported || capacity<3)return false;
    const char* preferences[]={pl?"pl":"en-US",pl?"en-US":"en",pl?"en":"eng","eng"};
    for(const char* want:preferences) {
        const char* p=supported;
        while(*p) {
            const char* q=p;while(*q && *q!=';')++q;
            size_t n=size_t(q-p),k=0;while(want[k] && k<n && p[k]==want[k])++k;
            if(!want[k] && (k==n || p[k]=='-') && n+1<capacity) {for(size_t i=0;i<n;++i)chosen[i]=p[i];chosen[n]=0;return true;}
            p=*q?q+1:q;
        }
    }
    size_t n=0;while(supported[n] && supported[n]!=';' && n+1<capacity) {chosen[n]=supported[n];++n;}
    chosen[n]=0;return n>0;
}
inline efi_status LoadDrivers(efi_boot_services* boot,bool pl,InfoList& list) {
    list.kind=ListKind::drivers;list.count=0;list.truncated=false;
    if(!boot || !boot->LocateHandleBuffer || !boot->HandleProtocol || !boot->FreePool)return EFI_UNSUPPORTED;
    efi_handle* handles=nullptr;size_t count=0;efi_guid binding=driverBindingGuid;
    auto status=boot->LocateHandleBuffer(ByProtocol,&binding,nullptr,&count,&handles);
    if(status!=EFI_SUCCESS)return status;
    for(size_t i=0;i<count;++i) {
        if(list.count>=listRows) {list.truncated=true;break;}
        auto& row=list.rows[list.count];row[0]=0;
        ComponentName2* component=nullptr;efi_guid cn2=componentName2Guid;char16_t* name=nullptr;
        if(boot->HandleProtocol(handles[i],&cn2,(void**)&component)==EFI_SUCCESS && component && component->GetDriverName) {
            char language[24];
            if(PickLanguage(component->SupportedLanguages,pl,language,sizeof(language))
                && component->GetDriverName(component,language,&name)!=EFI_SUCCESS)name=nullptr;
        }
        if(name && name[0]) {char16_t copy[100]={};size_t n=0;while(n<99 && name[n]) {copy[n]=name[n]<0x20?u' ':name[n];++n;}Put(row,listRowText,copy);}
        else Put(row,listRowText,pl?u"Sterownik bez nazwy":u"Unnamed driver");
        list.handles[list.count]=handles[i];++list.count;
    }
    boot->FreePool(handles);
    return EFI_SUCCESS;
}
inline efi_status DriverDetail(efi_boot_services* boot,const InfoList& list,size_t index,bool pl,char16_t* out,size_t capacity) {
    out[0]=0;
    if(list.kind!=ListKind::drivers || index>=list.count)return EFI_INVALID_PARAMETER;
    Put(out,capacity,list.rows[index]);Put(out,capacity,u". ");
    DriverBinding* binding=nullptr;efi_guid guid=driverBindingGuid;
    // The handle may have gone (driver unloaded since the list was built).
    if(boot->HandleProtocol(list.handles[index],&guid,(void**)&binding)!=EFI_SUCCESS || !binding) {
        Put(out,capacity,pl?u"Sterownik nie jest już zainstalowany.":u"The driver is no longer installed.");return EFI_NOT_FOUND;
    }
    Put(out,capacity,pl?u"Wersja: 0x":u"Version: 0x");PutHex(out,capacity,binding->Version,8);Put(out,capacity,u". ");
    efi_loaded_image_protocol* image=nullptr;efi_guid loaded=loadedImageGuid;
    if(binding->ImageHandle && boot->HandleProtocol(binding->ImageHandle,&loaded,(void**)&image)==EFI_SUCCESS && image) {
        Put(out,capacity,pl?u"Źródło: ":u"Source: ");
        const auto* node=(const uint8_t*)image->FilePath;bool said=false;
        for(unsigned guard=0;node && guard<16 && !said;++guard) {
            const uint8_t type=node[0],sub=node[1];const uint16_t length=Le16(node+2);
            if(type==0x7f || length<4)break;
            if(type==4 && sub==6) {Put(out,capacity,pl?u"wbudowany w firmware":u"built into firmware");said=true;}
            else if(type==4 && sub==4) {
                char16_t path[120]={};size_t n=0;
                for(size_t at=4;at+1<length && n<119;at+=2) {const uint16_t c=Le16(node+at);if(!c)break;path[n++]=c;}
                Put(out,capacity,pl?u"plik ":u"file ");Put(out,capacity,path);said=true;
            }
            node+=length;
        }
        if(!said)Put(out,capacity,pl?u"nieznane":u"unknown");
        Put(out,capacity,u", ");PutSize(out,capacity,image->ImageSize,pl);Put(out,capacity,u".");
    }
    // r387: the driver's own health report, when it publishes one. With no
    // controller named the protocol reports the driver as a whole.
    DriverHealth* health=nullptr;efi_guid healthGuid=driverHealthGuid;
    if(boot->HandleProtocol(list.handles[index],&healthGuid,(void**)&health)==EFI_SUCCESS && health && health->GetHealthStatus) {
        uint32_t state=0;
        const auto asked=health->GetHealthStatus(health,nullptr,nullptr,&state,nullptr,nullptr);
        static const char16_t* plStates[]={u"sprawny",u"wymaga naprawy",u"wymaga konfiguracji",u"uszkodzony",u"wymaga ponownego podłączenia",u"wymaga restartu komputera"};
        static const char16_t* enStates[]={u"healthy",u"repair required",u"configuration required",u"failed",u"reconnect required",u"reboot required"};
        Put(out,capacity,pl?u" Stan według sterownika: ":u" Health reported by the driver: ");
        if(asked!=EFI_SUCCESS)Put(out,capacity,pl?u"nie odpowiedział":u"no answer");
        else Put(out,capacity,state<6?(pl?plStates[state]:enStates[state]):(pl?u"nieznany":u"unknown"));
        Put(out,capacity,u".");
        if(asked==EFI_SUCCESS && state==2)Put(out,capacity,pl?u" Ustawienia sterownika są w ustawieniach UEFI.":u" The driver's settings are in UEFI settings.");
    }
    return EFI_SUCCESS;
}
// ESRT: what firmware says it can update and how the last update ended.
inline void EsrtText(efi_system_table* table,bool pl,char16_t* out,size_t capacity) {
    out[0]=0;const uint8_t* esrt=nullptr;
    for(size_t i=0;table && table->ConfigurationTable && i<table->NumberOfTableEntries;++i)
        if(GuidEqual(table->ConfigurationTable[i].VendorGuid,esrtGuid))esrt=(const uint8_t*)table->ConfigurationTable[i].VendorTable;
    if(!esrt) {Put(out,capacity,pl?u"Firmware nie publikuje tablicy zasobów aktualizacji (ESRT). Aktualizacji przez kapsuły nie da się tu sprawdzić.":u"Firmware publishes no update resource table (ESRT). Capsule updates cannot be checked here.");return;}
    const uint32_t count=Le32(esrt),maximum=Le32(esrt+4);
    if(count>maximum || count>64) {Put(out,capacity,pl?u"Tablica ESRT jest nieprawidłowa.":u"The ESRT is malformed.");return;}
    Put(out,capacity,pl?u"Zasoby aktualizowane przez firmware: ":u"Firmware update resources: ");PutNumber(out,capacity,count);Put(out,capacity,u". ");
    static const char16_t* plTypes[]={u"nieznany",u"firmware systemu",u"firmware urządzenia",u"sterownik UEFI"};
    static const char16_t* enTypes[]={u"unknown",u"system firmware",u"device firmware",u"UEFI driver"};
    static const char16_t* plResults[]={u"powodzenie",u"niepowodzenie",u"za mało zasobów",u"zła wersja",u"zły format",u"błąd podpisu",u"brak zasilania sieciowego",u"za słaba bateria",u"niespełnione zależności"};
    static const char16_t* enResults[]={u"success",u"unsuccessful",u"insufficient resources",u"incorrect version",u"invalid format",u"authentication error",u"AC power needed",u"battery too low",u"unsatisfied dependencies"};
    for(uint32_t i=0;i<count;++i) {
        const uint8_t* e=esrt+16+40*i;
        const uint32_t type=Le32(e+16),version=Le32(e+20),lowest=Le32(e+24),attempt=Le32(e+32),result=Le32(e+36);
        PutNumber(out,capacity,i+1);Put(out,capacity,u". ");Put(out,capacity,type<4?(pl?plTypes[type]:enTypes[type]):(pl?u"inny":u"other"));
        Put(out,capacity,pl?u", wersja ":u", version ");PutNumber(out,capacity,version);
        if(lowest) {Put(out,capacity,pl?u", najniższa dozwolona ":u", lowest allowed ");PutNumber(out,capacity,lowest);}
        if(attempt) {Put(out,capacity,pl?u", ostatnia próba ":u", last attempt ");PutNumber(out,capacity,attempt);Put(out,capacity,u": ");
            Put(out,capacity,result<9?(pl?plResults[result]:enResults[result]):(pl?u"błąd producenta":u"vendor error"));}
        Put(out,capacity,pl?u", klasa ":u", class ");PutGuid(out,capacity,*(const efi_guid*)e);Put(out,capacity,u". ");
    }
}
}
}
