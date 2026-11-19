#include "../src/efi/OwnedShellLauncher.h"
#include "../src/efi/MenuVoicePublication.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
using namespace hakefi;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
static const auto parent=(efi_handle)1,device=(efi_handle)2,child=(efi_handle)3,publisher=(efi_handle)4;
static MenuVoice* published=nullptr;static uint8_t data[256],devicePath[]={1,1,4,0,0x7f,0xff,4,0};
static efi_loaded_image_protocol parentInfo{},childInfo{},replacementInfo{};static efi_file_protocol file{};
static efi_loaded_image_protocol* currentChild=&childInfo;
static unsigned replaceAtStop=0;static bool reuseInterface=false;
static efi_status childProbeError=EFI_SUCCESS;
static size_t position=0,fileSize=sizeof(data);static unsigned allocations=0,loads=0,starts=0,unloads=0,stops=0;
static unsigned stopFailureAt=0;
static bool autoRetire=false,retired=false,extra=false,loadFailureHandle=true,partialInstall=false;
static efi_status loadError=EFI_SUCCESS,stopError=EFI_SUCCESS,unloadError=EFI_SUCCESS,installError=EFI_SUCCESS,uninstallError=EFI_SUCCESS;
static efi_status EFIAPI Speak(MenuVoice*,const char*,const char*) {return EFI_SUCCESS;}
static efi_status EFIAPI Pump(MenuVoice*,uint32_t) {return EFI_SUCCESS;}
static efi_status EFIAPI Stop(MenuVoice*) {
    ++stops;
    if(stops==replaceAtStop) {
        if(reuseInterface)childInfo.ImageBase=(void*)0x333000;
        else currentChild=&replacementInfo;
    }
    return stops==stopFailureAt?EFI_TIMEOUT:stopError;
}
static bool EFIAPI Preparing(MenuVoice*) {return false;}
static MenuVoiceV2 voice{{2,sizeof(MenuVoiceV2),Speak,Pump,Stop},Preparing};
static efi_status EFIAPI Locate(efi_guid*,void*,void** out) {if(!published)return EFI_NOT_FOUND;*out=published;return EFI_SUCCESS;}
static efi_status EFIAPI Install(efi_handle* handle,efi_guid*,efi_interface_type,void* interface) {
    if(installError==EFI_SUCCESS || partialInstall){*handle=publisher;published=(MenuVoice*)interface;}
    return installError;
}
static efi_status EFIAPI Uninstall(efi_handle handle,efi_guid*,void* interface) {
    CHECK(handle==publisher && interface==published);if(uninstallError!=EFI_SUCCESS)return uninstallError;
    published=nullptr;return EFI_SUCCESS;
}
static efi_status EFIAPI Protocol(efi_handle handle,efi_guid* guid,void** out) {
    if(handle==parent){*out=&parentInfo;return EFI_SUCCESS;}
    if(handle==device){CHECK(guid->data1==0x09576e91);*out=devicePath;return EFI_SUCCESS;}
    if(handle==publisher){if(!published)return EFI_UNSUPPORTED;*out=published;return EFI_SUCCESS;}
    CHECK(handle==child);if(retired)return EFI_INVALID_PARAMETER;
    if(childProbeError!=EFI_SUCCESS)return childProbeError;*out=currentChild;return EFI_SUCCESS;
}
static efi_status EFIAPI Allocate(efi_memory_type,size_t size,void** out) {CHECK(size<=OwnedShellLauncher::maximumImageBytes);*out=std::malloc(size);++allocations;return *out?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;}
static efi_status EFIAPI Free(void* ptr) {CHECK(ptr && allocations);std::free(ptr);--allocations;return EFI_SUCCESS;}
static efi_status EFIAPI Open(efi_file_protocol*,efi_file_protocol** out,const char16_t* name,uint64_t mode,uint64_t) {
    const char16_t expected[]=u"\\EFI\\HAK\\CharonShell.efi";CHECK(mode==EFI_FILE_MODE_READ && std::memcmp(name,expected,sizeof(expected))==0);
    *out=&file;position=0;return EFI_SUCCESS;
}
static efi_status EFIAPI Close(efi_file_protocol*) {return EFI_SUCCESS;}
static efi_status EFIAPI Info(efi_file_protocol*,efi_guid*,size_t* size,void* buffer) {
    auto* info=(efi_file_info*)buffer;std::memset(buffer,0,*size);info->Size=sizeof(*info)+2;info->FileSize=fileSize;*size=sizeof(*info)+2;return EFI_SUCCESS;
}
static efi_status EFIAPI Read(efi_file_protocol*,size_t* size,void* buffer) {
    if(position>=fileSize){*size=extra?1:0;return EFI_SUCCESS;}
    size_t count=fileSize-position;if(count>*size)count=*size;CHECK(position+count<=sizeof(data));
    std::memcpy(buffer,data+position,count);position+=count;*size=count;return EFI_SUCCESS;
}
static efi_status EFIAPI Load(bool policy,efi_handle self,efi_device_path_protocol* path,void* buffer,size_t size,efi_handle* out) {
    CHECK(!policy && self==parent && buffer && size==sizeof(data));auto* bytes=(uint8_t*)path;
    CHECK(std::memcmp(bytes,devicePath,4)==0 && bytes[4]==4 && bytes[5]==4);
    const char16_t name[]=u"\\EFI\\HAK\\CharonShell.efi";CHECK(std::memcmp(bytes+8,name,sizeof(name))==0);
    ++loads;retired=false;currentChild=&childInfo;childInfo.ImageBase=(void*)0x111000;childInfo.ImageSize=sizeof(data);
    if(loadError==EFI_SUCCESS || loadFailureHandle)*out=child;return loadError;
}
static efi_status EFIAPI Start(efi_handle handle,size_t*,char16_t**) {CHECK(handle==child && stops>0);++starts;retired=autoRetire;return EFI_SUCCESS;}
static efi_status EFIAPI Unload(efi_handle handle) {CHECK(handle==child && !retired);++unloads;return unloadError;}
int main() {
    efi_boot_services boot{};efi_file_protocol root{};parentInfo.DeviceHandle=device;
    boot.LocateProtocol=Locate;boot.InstallProtocolInterface=Install;boot.UninstallProtocolInterface=Uninstall;boot.HandleProtocol=Protocol;
    boot.AllocatePool=Allocate;boot.FreePool=Free;boot.LoadImage=Load;boot.StartImage=Start;boot.UnloadImage=Unload;
    root.Open=Open;file.Close=Close;file.GetInfo=Info;file.Read=Read;
    data[0]='M';data[1]='Z';data[60]=64;data[64]='P';data[65]='E';data[68]=0x64;data[69]=0x86;data[84]=160;data[88]=0x0b;data[89]=2;data[156]=10;
    MenuVoicePublication publication;CHECK(publication.Install(&boot,&voice.base)==EFI_SUCCESS && publication.Retained());
    OwnedShellLauncher launcher;
    MenuVoicePublication competitor;CHECK(competitor.Install(&boot,&voice.base)==EFI_ALREADY_STARTED && !competitor.Retained());
    MenuVoice other=voice.base;published=&other;
    CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_ACCESS_DENIED && !allocations && !loads);published=&voice.base;
    CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_SUCCESS && !launcher.Retained());
    CHECK(starts==1 && unloads==1 && stops==2 && allocations==0);
    autoRetire=true;CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_SUCCESS && !launcher.Retained());
    CHECK(starts==2 && unloads==1 && allocations==0);autoRetire=false;
    loadError=EFI_SECURITY_VIOLATION;
    CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_SECURITY_VIOLATION && starts==2 && unloads==2 && allocations==0);
    loadError=EFI_SUCCESS;stopError=EFI_TIMEOUT;
    CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_TIMEOUT && launcher.Retained() && starts==2 && allocations==2);
    CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_ALREADY_STARTED);
    stopError=EFI_SUCCESS;unloadError=EFI_ACCESS_DENIED;
    CHECK(launcher.Cleanup()==EFI_ACCESS_DENIED && launcher.Retained() && allocations==2);
    unloadError=EFI_SUCCESS;CHECK(launcher.Cleanup()==EFI_SUCCESS && !launcher.Retained() && allocations==0);
    unsigned priorLoads=loads;extra=true;CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_MEDIA_CHANGED && loads==priorLoads && !allocations);extra=false;
    fileSize=OwnedShellLauncher::maximumImageBytes+1;CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_BAD_BUFFER_SIZE && !allocations);fileSize=sizeof(data);
    data[156]=11;CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_UNSUPPORTED && loads==priorLoads && !allocations);data[156]=10;
    devicePath[2]=0;CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_INVALID_PARAMETER && loads==priorLoads && !allocations);devicePath[2]=4;
    stopFailureAt=stops+2;autoRetire=true;
    CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_TIMEOUT && retired && launcher.Retained() && allocations==2);
    CHECK(launcher.Cleanup()==EFI_SUCCESS && !launcher.Retained() && !allocations);autoRetire=false;stopFailureAt=0;
    // A callback during the post-child Stop reuses the numeric handle. Both
    // a new interface and same interface storage with a new base are refused.
    for(bool reuse:{false,true}) {
        const unsigned beforeUnload=unloads;replaceAtStop=stops+2;reuseInterface=reuse;
        CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_SUCCESS && !launcher.Retained());
        CHECK(unloads==beforeUnload && !allocations);replaceAtStop=0;
    }
    // The same race before StartImage must not execute the replacement either.
    unsigned beforeStart=starts,beforeUnload=unloads;replaceAtStop=stops+1;reuseInterface=false;
    CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_MEDIA_CHANGED && !launcher.Retained());
    CHECK(starts==beforeStart && unloads==beforeUnload && !allocations);replaceAtStop=0;
    // A failed LoadImage may return a real handle: capture it before cleanup,
    // and retain it on an observation failure instead of adopting another one.
    loadError=EFI_SECURITY_VIOLATION;childProbeError=EFI_DEVICE_ERROR;
    CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_DEVICE_ERROR && launcher.Retained() && allocations==2);
    childProbeError=EFI_SUCCESS;
    CHECK(launcher.Cleanup()==EFI_DEVICE_ERROR && launcher.Retained() && unloads==beforeUnload);
    retired=true;CHECK(launcher.Cleanup()==EFI_SUCCESS && !launcher.Retained() && !allocations);
    loadError=EFI_SUCCESS;retired=false;
    stopError=EFI_TIMEOUT;CHECK(publication.StopAndUninstall()==EFI_TIMEOUT && published && publication.Retained());
    stopError=EFI_SUCCESS;uninstallError=EFI_ACCESS_DENIED;CHECK(publication.StopAndUninstall()==EFI_ACCESS_DENIED && published && publication.Retained());
    uninstallError=EFI_SUCCESS;CHECK(publication.StopAndUninstall()==EFI_SUCCESS && !published && !publication.Retained());
    CHECK(launcher.Start(&boot,parent,&root,&voice.base)==EFI_NOT_FOUND);
    installError=EFI_DEVICE_ERROR;partialInstall=true;CHECK(publication.Install(&boot,&voice.base)==EFI_DEVICE_ERROR && publication.Retained());
    CHECK(publication.StopAndUninstall()==EFI_SUCCESS && !published);
    std::puts("PASS genuine service publication lifetime, own-ESP authenticated loader, Stop barriers, automatic retirement, failure-handle cleanup and retained resources");
}
