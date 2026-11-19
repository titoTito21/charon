#include "../src/efi/BootAudio.h"
#include <cassert>
#include <cstring>
#include <cstdio>
static efi_file_protocol root={},file={};
static hakaudioefi::Protocol audio={};
static efi_status openStatus=EFI_SUCCESS,closeStatus=EFI_SUCCESS,loadStatus=EFI_SECURITY_VIOLATION;
static const char* bytes="pl";static size_t length=2;
static bool available=true;static unsigned opened=0,closed=0,loaded=0,started=0,freed=0;
static unsigned setups=0;
static efi_status setupStatus=EFI_SUCCESS;
static hakaudioefi::Port ports[2]={{0,2,96,0,0,0},{0,2,64,0,0,0}};
static efi_status EFIAPI Open(efi_file_protocol*,efi_file_protocol** out,const char16_t*,uint64_t,uint64_t) {++opened;*out=&file;return openStatus;}
static efi_status EFIAPI Read(efi_file_protocol*,size_t* size,void* buffer) {*size=length<*size ? length : *size;memcpy(buffer,bytes,*size);return EFI_SUCCESS;}
static efi_status EFIAPI Close(efi_file_protocol*) {++closed;return closeStatus;}
static efi_status EFIAPI Locate(efi_guid*,void*,void** out) {*out=available ? &audio : nullptr;return available ? EFI_SUCCESS : EFI_NOT_FOUND;}
static efi_status EFIAPI Outputs(hakaudioefi::Protocol*,hakaudioefi::Port** out,size_t* count) {*out=ports;*count=2;return EFI_SUCCESS;}
static efi_status EFIAPI Free(void* p) {assert(p==ports);++freed;return EFI_SUCCESS;}
static efi_status EFIAPI Load(bool,efi_handle,efi_device_path_protocol*,void*,size_t,efi_handle*) {++loaded;return loadStatus;}
static efi_status EFIAPI Start(efi_handle,size_t*,char16_t**) {++started;return EFI_ABORTED;}
static efi_status EFIAPI Setup(hakaudioefi::Protocol*,uint64_t mask,int8_t gain,uint32_t rate,uint32_t bits,uint8_t channels,size_t delay) {
    assert(mask==1 && gain==-12 && rate==64 && bits==2 && channels==2 && delay==0);
    ++setups;return setupStatus;
}
static efi_status EFIAPI Play(hakaudioefi::Protocol*,void*,size_t,size_t,hakaudioefi::Callback,void*) {return EFI_SUCCESS;}
static efi_status EFIAPI Stop(hakaudioefi::Protocol*) {return EFI_SUCCESS;}
int main() {
    root.Open=Open;file.Read=Read;file.Close=Close;
    char lang[3]={};assert(hakaudioefi::ReadLanguage(&root,lang)==EFI_SUCCESS && !strcmp(lang,"pl"));
    bytes="en";assert(hakaudioefi::ReadLanguage(&root,lang)==EFI_SUCCESS && !strcmp(lang,"en"));
    bytes="en\n";length=3;assert(hakaudioefi::ReadLanguage(&root,lang)==EFI_INVALID_PARAMETER);
    bytes="de";length=2;assert(hakaudioefi::ReadLanguage(&root,lang)==EFI_INVALID_PARAMETER);
    length=0;assert(hakaudioefi::ReadLanguage(&root,lang)==EFI_INVALID_PARAMETER);
    bytes="en";length=2;closeStatus=EFI_DEVICE_ERROR;assert(hakaudioefi::ReadLanguage(&root,lang)==EFI_DEVICE_ERROR);
    closeStatus=EFI_SUCCESS;assert(opened==closed);
    efi_boot_services boot={};boot.LocateProtocol=Locate;boot.FreePool=Free;boot.LoadImage=Load;boot.StartImage=Start;
    audio.revision=4;audio.outputs=Outputs;audio.setup=Setup;audio.playAsync=Play;audio.stop=Stop;
    hakaudioefi::Protocol* found=nullptr;uint64_t mask=0;
    assert(hakaudioefi::InitializeAudio(nullptr,&boot,&root,found,mask)==EFI_SUCCESS && found==&audio && mask==1 && freed==1 && !loaded);
    assert(setups==1);
    setupStatus=EFI_DEVICE_ERROR;
    assert(hakaudioefi::InitializeAudio(nullptr,&boot,&root,found,mask)==EFI_DEVICE_ERROR && setups==2);
    setupStatus=EFI_SUCCESS;
    audio.revision=3;assert(hakaudioefi::InitializeAudio(nullptr,&boot,&root,found,mask)==EFI_UNSUPPORTED);
    audio.revision=4;ports[0].frequencies=64;assert(hakaudioefi::InitializeAudio(nullptr,&boot,&root,found,mask)==EFI_UNSUPPORTED && freed==3 && setups==2);
    available=false;
    assert(hakaudioefi::InitializeAudio(nullptr,&boot,&root,found,mask)==EFI_SECURITY_VIOLATION && loaded==1 && !started);
    openStatus=EFI_NOT_FOUND;assert(hakaudioefi::ReadLanguage(&root,lang)==EFI_NOT_FOUND);
    assert(hakaudioefi::InitializeAudio(nullptr,&boot,&root,found,mask)==EFI_NOT_FOUND && loaded==1);
    puts("PASS: boot audio configuration, ABI/rate selection, missing assets and firmware authentication refusal");
}
