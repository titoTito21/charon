// Disposable OVMF/private NVRAM ONLY. Never deploy as a HAK boot component.
#define efi_main unused_component_entry
#include "../src/efi/HAKBootEFI.cpp"
#undef efi_main
static efi_guid fixtureGuid={0x48616b11,0x8412,0x4610,{0xab,0x11,0x43,0x13,0x15,0x71,0x33,0x02}};
static efi_file_protocol* volume;
static bool ReadFixture(const char16_t* path,uint8_t* data,size_t& size) {
    efi_file_protocol* file=nullptr;
    if(volume->Open(volume,&file,path,EFI_FILE_MODE_READ,0)!=EFI_SUCCESS)return false;
    auto status=file->Read(file,&size,data);file->Close(file);return status==EFI_SUCCESS;
}
static bool KeyRequest(const char16_t* path) {
    uint8_t request[65536];size_t size=sizeof(request);
    if(!ReadFixture(path,request,size) || size<48 || size==sizeof(request))return false;
    uint8_t state[hakefi::kSecureControlSize];SnapshotSecureControl(state);
    request[25]=state[21];request[26]=state[22];
    return ApplyKeyRequest(request,size)==EFI_SUCCESS;
}
static bool UntrustedImagePolicy(efi_handle self,efi_handle device,bool enabled) {
    efi_guid pathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
    efi_device_path_protocol* devicePath=nullptr;
    auto* boot=systemTable->BootServices;
    if(boot->HandleProtocol(device,&pathGuid,(void**)&devicePath)!=EFI_SUCCESS)return false;
    uint8_t path[2048];size_t at=0;auto* bytes=(uint8_t*)devicePath;
    while(at<1024) {
        const size_t length=hakefi::U16(bytes+at+2);
        if(length<4 || length>1024-at)return false;
        if(bytes[at]==0x7f && bytes[at+1]==0xff)break;
        at+=length;
    }
    if(at>=1024)return false;
    memcpy(path,bytes,at);
    const char16_t file[]=u"\\EFI\\HAK\\PAYLOAD.EFI";
    path[at]=4;path[at+1]=4;hakefi::Put16(path+at+2,4+sizeof(file));
    memcpy(path+at+4,file,sizeof(file));at+=4+sizeof(file);
    path[at]=0x7f;path[at+1]=0xff;path[at+2]=4;path[at+3]=0;
    efi_handle child=nullptr;
    const auto status=boot->LoadImage(false,self,(efi_device_path_protocol*)path,nullptr,0,&child);
    // SECURITY_VIOLATION may still return an image handle. Never start it.
    if(child)boot->UnloadImage(child);
    // UEFI LoadImage allows ACCESS_DENIED when policy refuses the image,
    // or SECURITY_VIOLATION when a loaded image cannot be authenticated.
    bool ok=enabled ? (status==EFI_SECURITY_VIOLATION || status==EFI_ACCESS_DENIED) : status==EFI_SUCCESS;
    if(!ok) {
        char16_t message[]=u"FAIL: LoadImage status 0000000000000000\r\n";
        const char16_t hex[]=u"0123456789abcdef";
        for(unsigned i=0;i<16;++i)message[23+i]=hex[(uint64_t(status)>>(60-i*4))&15];
        Say(message);
    }
    if(ok)Say(enabled ? u"PASS: untrusted payload refused with SecureBoot active.\r\n"
                     : u"PASS: same untrusted payload loads with SecureBoot disabled.\r\n");
    return ok;
}
extern "C" efi_status EFIAPI efi_main(efi_handle self,efi_system_table* table) {
    systemTable=table;Say(u"HAK reset fixture entered.\r\n");
    efi_loaded_image_protocol* loaded=nullptr;efi_simple_file_system_protocol* fs=nullptr;
    auto* boot=table->BootServices;auto* runtime=table->RuntimeServices;
    if(boot->HandleProtocol(self,&imageGuid,(void**)&loaded)!=EFI_SUCCESS
       || boot->HandleProtocol(loaded->DeviceHandle,&fsGuid,(void**)&fs)!=EFI_SUCCESS
       || fs->OpenVolume(fs,&volume)!=EFI_SUCCESS)return EFI_NOT_FOUND;
    uint8_t stage=0;size_t size=1;
    auto status=runtime->GetVariable((char16_t*)u"HAKResetFixture",&fixtureGuid,nullptr,&size,&stage);
    if(status!=EFI_NOT_FOUND && (status!=EFI_SUCCESS || size!=1 || stage>4))return EFI_ABORTED;
    uint8_t state[hakefi::kSecureControlSize];SnapshotSecureControl(state);
    bool ok=false;
    if(stage==0 && state[21]==1 && state[22]==0) {
        uint8_t auth[116]={};size=76;
        const uint8_t pkcs7[]={0x9d,0xd2,0xaf,0x4a,0xdf,0x68,0xee,0x49,0x8a,0xa9,0x34,0x7d,0x37,0x56,0x65,0xa7};
        hakefi::Put16(auth,2026);auth[2]=11;auth[3]=16;
        auth[16]=24;auth[21]=2;auth[22]=0xf1;auth[23]=0x0e;memcpy(auth+24,pkcs7,16);
        ok=ReadFixture(u"\\EFI\\HAK\\trust.esl",auth+40,size) && size==76
            && runtime->SetVariable((char16_t*)u"db",&securityDatabaseGuid,0x27,sizeof(auth),auth)==EFI_SUCCESS
            && KeyRequest(u"\\EFI\\HAK\\SIGNED\\01.bin");
    } else if(stage==1 || stage==3) {
        ok=state[21]==0 && state[22]==1 && state[20]==1 && UntrustedImagePolicy(self,loaded->DeviceHandle,true);
        if(ok)Say(u"PASS: SecureBoot active after reset; trusted fixture admitted.\r\n");
        if(ok && stage==1)ok=SetEdk2SecureBootPreference(state,sizeof(state),0)==EFI_SUCCESS;
        if(ok && stage==3)ok=KeyRequest(u"\\EFI\\HAK\\SIGNED\\14.bin");
    } else if(stage==2) {
        ok=state[21]==0 && state[22]==0 && state[20]==0 && UntrustedImagePolicy(self,loaded->DeviceHandle,false);
        if(ok)Say(u"PASS: SecureBoot disabled after reset, PK retained.\r\n");
        if(ok)ok=SetEdk2SecureBootPreference(state,sizeof(state),1)==EFI_SUCCESS;
    } else if(stage==4) {
        ok=state[21]==1 && state[22]==0;
        if(ok) {Say(u"PASS: reset cycle enable-disable-enable and signed PK cleanup complete.\r\n");return EFI_SUCCESS;}
    }
    if(!ok) {Say(u"FAIL: reset cycle state or mutation.\r\n");return EFI_ABORTED;}
    ++stage;
    if(runtime->SetVariable((char16_t*)u"HAKResetFixture",&fixtureGuid,7,1,&stage)!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    volume->Close(volume);
    Say(u"HAK reset fixture: cold reset.\r\n");
    runtime->ResetSystem(EfiResetCold,EFI_SUCCESS,0,nullptr);
    return EFI_DEVICE_ERROR;
}
