#pragma once
#include <efi/boot-services.h>
#include "HiiModel.h"
namespace charonhii {
// ABI from pinned AUDK MdePkg/Include/Protocol/HiiDatabase.h (BSD-2-Clause-Patent).
// Unused slots retain pointer width, never called through an invented signature.
struct DatabaseProtocol;
using PackageNotify=efi_status (EFIAPI *)(uint8_t,const efi_guid*,const void*,void*,size_t);
struct DatabaseProtocol {
    void* newList;void* removeList;void* updateList;
    efi_status (EFIAPI *list)(const DatabaseProtocol*,uint8_t,const efi_guid*,size_t*,void**);
    efi_status (EFIAPI *exportList)(const DatabaseProtocol*,void*,size_t*,void*);
    efi_status (EFIAPI *registerNotify)(const DatabaseProtocol*,uint8_t,const efi_guid*,PackageNotify,size_t,efi_handle*);
    efi_status (EFIAPI *unregisterNotify)(const DatabaseProtocol*,efi_handle);
    void* findKeyboard;void* getKeyboard;void* setKeyboard;
    efi_status (EFIAPI *driver)(const DatabaseProtocol*,void*,efi_handle*);
};
static_assert(sizeof(DatabaseProtocol)==11*sizeof(void*) && offsetof(DatabaseProtocol,list)==3*sizeof(void*)
    && offsetof(DatabaseProtocol,exportList)==4*sizeof(void*) && offsetof(DatabaseProtocol,driver)==10*sizeof(void*),"HII database ABI");
class Database {
public:
    static constexpr size_t MaxHandles=512,MaxBytes=4*1024*1024;
    struct Identity {uint64_t generation=0,serial=0;void* hii=nullptr;efi_handle driver=nullptr;uint8_t guid[16]={};};
    Database()=default;Database(const Database&)=delete;Database& operator=(const Database&)=delete;
    // Explicit Close required before destruction/unload. Failed unregister keeps
    // callback ownership alive; caller must retain this object and application.
    efi_status Open(efi_boot_services*);
    efi_status Close();
    bool NeedsClose()const {return api_!=nullptr;}
    efi_status Refresh();
    size_t Count()const {return enumerationGeneration_==generation_?count_:0;}
    efi_status Read(size_t index,const char* const* languages,size_t languageCount);
    Identity Current()const {return identity_;}
    const Model* View(const Identity&)const;
    const char* SelectedLanguage(const Identity&)const;
    Status ParseStatus()const {return parsed_;}
private:
    static Database* active_;
    static efi_status EFIAPI Changed(uint8_t,const efi_guid*,const void*,void*,size_t);
    void Invalidate();void ClearSnapshot();void ClearEnumeration();
    bool SameIdentity(const Identity&)const;
    efi_boot_services* boot_=nullptr;DatabaseProtocol* api_=nullptr;
    efi_handle registrations_[6]={};void** handles_=nullptr;size_t count_=0;
    Model* model_=nullptr;uint8_t* bytes_=nullptr;
    uint64_t generation_=1,enumerationGeneration_=0,serial_=0;
    Identity identity_{};char language_[64]={};Status parsed_=Status::Invalid;
    bool exhausted_=false;
};
}
