// Exercise actual EFI validation/loading boundaries, with firmware callbacks.
#define HAK_CHARON_LIVE_VOICE
#include "../src/efi/HAKBootEFI.cpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
using namespace hakefi;
static uint8_t target[]={1,0,0,0,4,0,0,0,0x7f,0xff,4,0};
static bool missing=false,inOrder=true,space=false,spaceOnLoad=false;
static unsigned loads=0,starts=0,unloads=0;
static efi_loaded_image_protocol loaded{};
static efi_status EFIAPI MissingFile(efi_file_protocol*,efi_file_protocol**,const char16_t*,uint64_t,uint64_t) {return EFI_NOT_FOUND;}
static efi_status EFIAPI Variable(char16_t* name,efi_guid*,uint32_t*,size_t* size,void* data) {
    if(name[4]==u'O') {if(!inOrder)return EFI_NOT_FOUND;*(uint16_t*)data=2;*size=2;return EFI_SUCCESS;}
    if(missing)return EFI_NOT_FOUND;
    memcpy(data,target,sizeof(target));*size=sizeof(target);return EFI_SUCCESS;
}
static efi_status EFIAPI Load(bool,efi_handle,efi_device_path_protocol*,void*,size_t,efi_handle* out) {
    ++loads;*out=(efi_handle)1;if(spaceOnLoad)space=true;return EFI_SUCCESS;
}
static efi_status EFIAPI Protocol(efi_handle,efi_guid*,void** out) {*out=&loaded;return EFI_SUCCESS;}
static efi_status EFIAPI Start(efi_handle,size_t*,char16_t**) {++starts;return EFI_SUCCESS;}
static efi_status EFIAPI Unload(efi_handle) {++unloads;return EFI_SUCCESS;}
static efi_status EFIAPI ReadKey(efi_simple_text_input_protocol*,efi_input_key* key) {
    if(!space)return EFI_NOT_READY;space=false;key->ScanCode=0;key->UnicodeChar=32;return EFI_SUCCESS;
}
static efi_status EFIAPI Check(efi_event) {return space?EFI_SUCCESS:EFI_NOT_READY;}
static bool Contains(const char16_t* text,const char16_t* part) {
    for(;*text;++text) {size_t i=0;while(part[i] && part[i]==text[i])++i;if(!part[i])return true;}
    return false;
}
int main() {
    efi_boot_services boot{};efi_runtime_services runtime{};efi_system_table table{};
    efi_simple_text_input_protocol input{};
    boot.LoadImage=Load;boot.HandleProtocol=Protocol;boot.StartImage=Start;boot.UnloadImage=Unload;boot.CheckEvent=Check;
    runtime.GetVariable=Variable;table.BootServices=&boot;table.RuntimeServices=&runtime;systemTable=&table;input.ReadKeyStroke=ReadKey;
    uint8_t saved[sizeof(target)];memcpy(saved,target,sizeof(saved));BootAttemptStage stage;
    efi_file_protocol disk{};disk.Open=MissingFile;uint16_t savedId=0xbeef;size_t savedSize=99;
    CHECK(ReadContinue(&disk,savedId,savedSize)==EFI_NOT_FOUND && savedId==0xbeef && savedSize==99 && starts==0);
    CHECK(ValidateContinueTarget(1,2,saved,sizeof(saved))==EFI_SUCCESS);
    CHECK(ValidateContinueTarget(2,2,saved,sizeof(saved))==EFI_ACCESS_DENIED);
    missing=true;CHECK(StartVerified(nullptr,1,2,saved,sizeof(saved),stage)==EFI_NOT_FOUND && loads==0);missing=false;
    inOrder=false;CHECK(StartVerified(nullptr,1,2,saved,sizeof(saved),stage)==EFI_ACCESS_DENIED && loads==0);inOrder=true;
    target[0]=3;CHECK(StartVerified(nullptr,1,2,saved,sizeof(saved),stage)==EFI_NOT_FOUND && loads==0);target[0]=1;
    EfiAutoContinue gate{&boot,&input,nullptr,nullptr,nullptr};
    space=true;CHECK(StartVerified(nullptr,1,2,saved,sizeof(saved),stage,EfiAutoContinue::BeforeTransfer,&gate)==EFI_ABORTED);
    CHECK(gate.cancelled && loads==0 && starts==0);
    gate.cancelled=false;spaceOnLoad=true;
    CHECK(StartVerified(nullptr,1,2,saved,sizeof(saved),stage,EfiAutoContinue::BeforeTransfer,&gate)==EFI_ABORTED);
    CHECK(gate.cancelled && loads==1 && starts==0 && unloads==1);
    gate.cancelled=false;spaceOnLoad=false;
    CHECK(StartVerified(nullptr,1,2,saved,sizeof(saved),stage,EfiAutoContinue::BeforeTransfer,&gate)==EFI_SUCCESS);
    CHECK(loads==2 && starts==1 && unloads==2);
    uint8_t named[]={1,0,0,0,4,0,'O',0,'S',0,0,0,0x7f,0xff,4,0};
    MenuEntry entry{2,named,sizeof(named)};char16_t confirmation[512];
    ConfirmationMessage(MenuAction::setContinue,false,&entry,confirmation,512);
    CHECK(Contains(confirmation,u"automatic Continue target") && Contains(confirmation,u"UEFI device — OS") && Contains(confirmation,u"Cancel is selected"));
    ConfirmationMessage(MenuAction::setContinue,true,&entry,confirmation,512);
    CHECK(Contains(confirmation,u"automatyczny cel Kontynuuj") && Contains(confirmation,u"Urządzenie UEFI — OS") && Contains(confirmation,u"Anuluj"));
    ConfirmationMessage(MenuAction::pendingKeys,false,nullptr,confirmation,512);
    CHECK(Contains(confirmation,u"Secure Boot key update") && !Contains(confirmation,u"OS"));
    std::puts("PASS actual saved target validation, missing/stale/order/self rejection, cancellation during LoadImage and verified handoff");
}
