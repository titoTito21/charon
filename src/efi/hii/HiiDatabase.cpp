#include "HiiDatabase.h"
#include <new>
namespace charonhii {
Database* Database::active_=nullptr;
static efi_guid databaseGuid={0xef9fc172,0xa1b2,0x4693,{0xb3,0x27,0x6d,0x32,0xfc,0x41,0x60,0x42}};
static bool Equal(const uint8_t* a,const uint8_t* b,size_t n) {for(size_t i=0;i<n;++i)if(a[i]!=b[i])return false;return true;}
void Database::Invalidate() {if(generation_==UINT64_MAX)exhausted_=true;else ++generation_;}
efi_status EFIAPI Database::Changed(uint8_t,const efi_guid*,const void*,void*,size_t) {
    if(active_) {active_->Invalidate();}
    return EFI_SUCCESS;
}
void Database::ClearSnapshot() {
    if(model_) {model_->Clear();model_->~Model();boot_->FreePool(model_);model_=nullptr;}
    if(bytes_) {boot_->FreePool(bytes_);bytes_=nullptr;}
    identity_={};language_[0]=0;parsed_=Status::Invalid;
}
void Database::ClearEnumeration() {
    ClearSnapshot();if(handles_) {boot_->FreePool(handles_);handles_=nullptr;}
    count_=0;enumerationGeneration_=0;
}
efi_status Database::Open(efi_boot_services* boot) {
    if(api_ || active_)return EFI_ALREADY_STARTED;
    if(!boot || !boot->LocateProtocol || !boot->AllocatePool || !boot->FreePool)return EFI_INVALID_PARAMETER;
    DatabaseProtocol* found=nullptr;const auto status=boot->LocateProtocol(&databaseGuid,nullptr,(void**)&found);
    if(status!=EFI_SUCCESS)return status;
    if(!found || !found->list || !found->exportList || !found->registerNotify || !found->unregisterNotify || !found->driver)return EFI_UNSUPPORTED;
    boot_=boot;api_=found;active_=this;
    // AUDK matches one exact notification type, not a bitmask or ALL pseudo-type.
    // Watch every change to forms and strings, regardless of which list owns it.
    size_t at=0;
    const uint8_t types[]={2,4};const size_t changes[]={1,2,8};
    for(uint8_t type:types)for(size_t change:changes) {
        const auto registered=api_->registerNotify(api_,type,nullptr,Changed,change,&registrations_[at++]);
        if(registered!=EFI_SUCCESS) {Close();return registered;}
    }
    return EFI_SUCCESS;
}
efi_status Database::Close() {
    if(!api_)return EFI_SUCCESS;
    Invalidate();ClearEnumeration();efi_status failure=EFI_SUCCESS;
    for(auto& handle:registrations_)if(handle) {
        const auto status=api_->unregisterNotify(api_,handle);
        if(status==EFI_SUCCESS)handle=nullptr;else failure=status;
    }
    if(failure!=EFI_SUCCESS)return failure;
    api_=nullptr;boot_=nullptr;if(active_==this)active_=nullptr;return EFI_SUCCESS;
}
efi_status Database::Refresh() {
    if(!api_ || exhausted_)return EFI_NOT_READY;
    Invalidate();ClearEnumeration();const auto captured=generation_;
    size_t bytes=0;auto status=api_->list(api_,2,nullptr,&bytes,nullptr);
    if(status==EFI_NOT_FOUND || (status==EFI_SUCCESS && !bytes)) {enumerationGeneration_=captured;return EFI_SUCCESS;}
    if(status!=EFI_BUFFER_TOO_SMALL)return status;
    if(!bytes || bytes%sizeof(void*) || bytes>MaxHandles*sizeof(void*))return EFI_BAD_BUFFER_SIZE;
    void* memory=nullptr;status=boot_->AllocatePool(EfiLoaderData,bytes,&memory);if(status!=EFI_SUCCESS)return status;
    const auto capacity=bytes;status=api_->list(api_,2,nullptr,&bytes,static_cast<void**>(memory));
    if(status==EFI_SUCCESS && (generation_!=captured || exhausted_))status=EFI_MEDIA_CHANGED;
    if(status==EFI_SUCCESS && (bytes>capacity || bytes%sizeof(void*)))status=EFI_BAD_BUFFER_SIZE;
    if(status==EFI_SUCCESS) {
        const auto candidate=static_cast<void**>(memory);
        for(size_t i=0;i<bytes/sizeof(void*);++i) {
            if(!candidate[i]) {status=EFI_INVALID_PARAMETER;break;}
            for(size_t j=0;j<i;++j)if(candidate[j]==candidate[i])status=EFI_INVALID_PARAMETER;
        }
    }
    if(status!=EFI_SUCCESS) {boot_->FreePool(memory);return status;}
    handles_=static_cast<void**>(memory);count_=bytes/sizeof(void*);enumerationGeneration_=captured;return EFI_SUCCESS;
}
efi_status Database::Read(size_t index,const char* const* languages,size_t languageCount) {
    ClearSnapshot();
    if(!api_ || exhausted_ || enumerationGeneration_!=generation_)return EFI_MEDIA_CHANGED;
    if(index>=count_ || !languages || !languageCount || languageCount>4)return EFI_INVALID_PARAMETER;
    for(size_t i=0;i<languageCount;++i) {
        if(!languages[i])return EFI_INVALID_PARAMETER;
        size_t n=0;while(n<64 && languages[i][n]) {if(uint8_t(languages[i][n])>127)return EFI_INVALID_PARAMETER;++n;}
        if(!n || n==64)return EFI_INVALID_PARAMETER;
    }
    if(serial_==UINT64_MAX) {exhausted_=true;return EFI_OUT_OF_RESOURCES;}
    const auto captured=generation_;efi_handle owner=nullptr;
    auto status=api_->driver(api_,handles_[index],&owner);if(status!=EFI_SUCCESS)return status;
    size_t size=0;status=api_->exportList(api_,handles_[index],&size,nullptr);
    if(status!=EFI_OUT_OF_RESOURCES && status!=EFI_BUFFER_TOO_SMALL)return status==EFI_SUCCESS?EFI_BAD_BUFFER_SIZE:status;
    if(size<24 || size>MaxBytes)return EFI_BAD_BUFFER_SIZE;
    void* buffer=nullptr;status=boot_->AllocatePool(EfiLoaderData,size,&buffer);if(status!=EFI_SUCCESS)return status;
    bytes_=static_cast<uint8_t*>(buffer);const auto capacity=size;
    status=api_->exportList(api_,handles_[index],&size,bytes_);
    if(status==EFI_SUCCESS && (size>capacity || size<24))status=EFI_BAD_BUFFER_SIZE;
    efi_handle after=nullptr;
    if(status==EFI_SUCCESS)status=api_->driver(api_,handles_[index],&after);
    if(status==EFI_SUCCESS && (captured!=generation_ || exhausted_ || after!=owner))status=EFI_MEDIA_CHANGED;
    if(status!=EFI_SUCCESS) {ClearSnapshot();return status;}
    buffer=nullptr;status=boot_->AllocatePool(EfiLoaderData,sizeof(Model),&buffer);
    if(status!=EFI_SUCCESS) {ClearSnapshot();return status;}
    model_=new(buffer) Model;
    for(size_t i=0;i<languageCount;++i) {
        parsed_=Parse(bytes_,size,languages[i],*model_);
        if(parsed_==Status::Invalid || parsed_==Status::Limit) {ClearSnapshot();return EFI_COMPROMISED_DATA;}
        if(model_->stringsAvailable) {size_t n=0;do {language_[n]=languages[i][n];}while(languages[i][n++]);break;}
    }
    if(captured!=generation_ || exhausted_) {ClearSnapshot();return EFI_MEDIA_CHANGED;}
    identity_.generation=captured;identity_.serial=++serial_;identity_.hii=handles_[index];identity_.driver=owner;
    for(unsigned i=0;i<16;++i)identity_.guid[i]=bytes_[i];
    return EFI_SUCCESS;
}
bool Database::SameIdentity(const Identity& value)const {
    return !exhausted_ && model_ && value.serial && value.generation==generation_ && value.generation==identity_.generation
        && value.serial==identity_.serial && value.hii==identity_.hii && value.driver==identity_.driver && Equal(value.guid,identity_.guid,16);
}
const Model* Database::View(const Identity& value)const {return SameIdentity(value)?model_:nullptr;}
const char* Database::SelectedLanguage(const Identity& value)const {return SameIdentity(value)?language_:nullptr;}
}
