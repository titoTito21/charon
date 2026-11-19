// Only this documented EDK2 variable is supported. Never changes keys/CustomMode.
// Source contract: SecurityPkg/Include/Guid/AuthenticatedVariableFormat.h.
#pragma once
#include "SecureBootControl.h"
static efi_guid secureEnableGuid={0xf0a30bc7,0xaf08,0x4556,{0x99,0xc4,0,0x10,9,0xc9,0x3a,0x44}};
static void SnapshotSecureControl(uint8_t* output) {
    memset(output,0,hakefi::kSecureControlSize);memcpy(output,"HAKSB01",8);
    output[20]=output[21]=output[22]=255;
    auto* runtime=systemTable->RuntimeServices;
    uint8_t value=255;uint32_t attributes=0;size_t size=1;
    efi_status status=runtime->GetVariable((char16_t*)u"SecureBootEnable",&secureEnableGuid,&attributes,&size,&value);
    if(status==EFI_SUCCESS && (size!=1 || value>1 || attributes!=3))status=EFI_UNSUPPORTED;
    for(unsigned i=0;i<8;++i)output[8+i]=uint8_t(uint64_t(status)>>(8*i));
    if(status==EFI_SUCCESS) {output[16]=3;output[20]=value;}
    const char16_t* names[]={u"SetupMode",u"SecureBoot"};
    for(unsigned i=0;i<2;++i) {
        size=1;value=255;
        if(runtime->GetVariable((char16_t*)names[i],&globalGuid,nullptr,&size,&value)==EFI_SUCCESS && size==1 && value<=1)output[21+i]=value;
    }
}
// No file/OS request invokes this. Reserved for a physically confirmed firmware
// interface; firmware remains responsible for its physical-presence policy.
[[maybe_unused]] static efi_status SetEdk2SecureBootPreference(const uint8_t* expected,size_t length,uint8_t value) {
    if(value>1 || !hakefi::SecureControlAvailable(expected,length))return EFI_UNSUPPORTED;
    uint8_t live[hakefi::kSecureControlSize];SnapshotSecureControl(live);
    if(!hakefi::Same(expected,live,sizeof(live)))return EFI_MEDIA_CHANGED;
    if(value==live[20])return EFI_ALREADY_STARTED;
    efi_status status=systemTable->RuntimeServices->SetVariable((char16_t*)u"SecureBootEnable",&secureEnableGuid,3,1,&value);
    if(status!=EFI_SUCCESS)return status;
    SnapshotSecureControl(live);
    return hakefi::SecureControlAvailable(live,sizeof(live)) && live[20]==value ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
static bool SaveSecureControl(efi_file_protocol* root) {
    uint8_t snapshot[hakefi::kSecureControlSize];SnapshotSecureControl(snapshot);
    return SaveSettingsFile(root,u"\\EFI\\HAK\\secure-control.bin",snapshot,sizeof(snapshot));
}
