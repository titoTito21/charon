#pragma once
#include "HiiDatabase.h"
namespace charonhii {
struct ConfigAccessProtocol {
    efi_status (EFIAPI *extract)(const ConfigAccessProtocol*,const char16_t*,char16_t**,char16_t**);
    void* route;void* callback;
};
struct ConfigRoutingProtocol {
    void* extract;void* exportConfig;
    efi_status (EFIAPI *route)(const ConfigRoutingProtocol*,const char16_t*,char16_t**);
    void* blockToConfig;void* configToBlock;void* alternate;
};
static_assert(sizeof(ConfigAccessProtocol)==3*sizeof(void*)&&offsetof(ConfigRoutingProtocol,route)==2*sizeof(void*)
    &&sizeof(ConfigRoutingProtocol)==6*sizeof(void*),"HII config protocol ABI");
struct ConfigValue {Kind kind=Kind::Checkbox;uint64_t number=0;char16_t text[256]={};};
class Config;
struct ConfigProposal {
    const Config* owner=nullptr;uint64_t binding=0;
    ConfigValue value{};uint8_t baseline[512]={};size_t bytes=0;
};
struct ConfigCommitResult {
    bool attempted=false,applied=false,changed=false,resetRequired=false;
    efi_status routeStatus=EFI_NOT_READY,readbackStatus=EFI_NOT_READY;
};
// Single-owner, synchronous operations. No owned firmware allocations survive
// calls. No destructor/Close needed; Reset invalidates outstanding proposals.
// Caller must arrange keyboard/audio scheduling around firmware callbacks.
class Config {
public:
    Config()=default;Config(const Config&)=delete;Config& operator=(const Config&)=delete;
    void Reset();
    efi_status Bind(efi_boot_services*,Database&,const Database::Identity&,uint16_t questionIndex);
    efi_status Read(ConfigValue&);
    // Optional edit-start value prevents lost updates while the user types.
    efi_status Propose(const ConfigValue&,ConfigProposal&,const ConfigValue* expectedCurrent=nullptr);
    efi_status Commit(const ConfigProposal&,bool explicitlyConfirmed,ConfigCommitResult&);
private:
    efi_status Fresh(const Model*&,const Question*&,const Store*&);
    efi_status Header(const Model&,const Question&,const Store&,char16_t*,size_t&);
    efi_status Extract(uint8_t*);
    efi_status Encode(const ConfigValue&,uint8_t*);
    efi_status Decode(const uint8_t*,ConfigValue&);
    efi_boot_services* boot_=nullptr;Database* database_=nullptr;Database::Identity identity_{};
    ConfigAccessProtocol* access_=nullptr;ConfigRoutingProtocol* routing_=nullptr;
    uint16_t question_=None;size_t width_=0,pathBytes_=0;uint8_t path_[1024]={};
    uint64_t binding_=0;
};
}
