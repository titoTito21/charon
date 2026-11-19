// Included after SecureBootInventory.h. Firmware authenticates every update.
#pragma once
#include "SecureBootUpdate.h"
static efi_status ApplyKeyRequest(uint8_t* request,size_t length) {
    size_t payloadOffset=0;
    if(!hakefi::KeyRequest(request,length,payloadOffset))return EFI_INVALID_PARAMETER;
    auto* runtime=systemTable->RuntimeServices;auto* boot=systemTable->BootServices;
    const char16_t* modes[]={u"SetupMode",u"SecureBoot"};
    for(unsigned i=0;i<2;++i) {
        uint8_t value=255;size_t size=1;
        if(runtime->GetVariable((char16_t*)modes[i],&globalGuid,nullptr,&size,&value)!=EFI_SUCCESS
            || size!=1 || value!=request[25+i])return EFI_MEDIA_CHANGED;
    }
    const char16_t* names[]={u"PK",u"KEK",u"db",u"dbx"};
    const unsigned target=request[24];
    auto* guid=target<2 ? &globalGuid : &securityDatabaseGuid;
    void* live=nullptr;
    if(boot->AllocatePool(EfiLoaderData,hakefi::kKeyLimit+1,&live)!=EFI_SUCCESS)return EFI_OUT_OF_RESOURCES;
    size_t size=hakefi::kKeyLimit+1;uint32_t attributes=0;
    efi_status status=runtime->GetVariable((char16_t*)names[target],guid,&attributes,&size,live);
    const size_t oldSize=hakefi::U32(request+40),authSize=hakefi::U32(request+44);
    const bool same=uint64_t(status)==hakefi::KeyU64(request+28)
        && (status==EFI_NOT_FOUND || (status==EFI_SUCCESS && attributes==hakefi::U32(request+36)
            && size==oldSize && hakefi::Same((uint8_t*)live,request+48,size)));
    if(!same)status=EFI_MEDIA_CHANGED;
    else {
        uint8_t* auth=request+48+oldSize;
        status=runtime->SetVariable((char16_t*)names[target],guid,0x27,authSize,auth);
        if(status==EFI_SUCCESS) {
            size=hakefi::kKeyLimit+1;attributes=0;
            const efi_status read=runtime->GetVariable((char16_t*)names[target],guid,&attributes,&size,live);
            const size_t wanted=authSize-payloadOffset;
            const bool verified=wanted ? (read==EFI_SUCCESS && size==wanted && attributes==0x27
                && hakefi::Same((uint8_t*)live,auth+payloadOffset,wanted)) : read==EFI_NOT_FOUND;
            if(!verified)status=EFI_DEVICE_ERROR;
        }
    }
    boot->FreePool(live);return status;
}
static efi_status ProcessKeyRequest(efi_file_protocol* root) {
    efi_file_protocol* file=nullptr;
    efi_status status=root->Open(root,&file,u"\\EFI\\HAK\\keys-next.bin",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(status!=EFI_SUCCESS)return status;
    void* memory=nullptr;auto* boot=systemTable->BootServices;
    status=boot->AllocatePool(EfiLoaderData,hakefi::kKeyRequestLimit+1,&memory);
    if(status!=EFI_SUCCESS) {file->Close(file);return status;}
    auto* request=(uint8_t*)memory;size_t size=hakefi::kKeyRequestLimit+1;
    memset(request,0,24);
    status=file->Read(file,&size,request);
    const efi_status consumed=file->Delete(file);
    if(consumed!=EFI_SUCCESS)status=EFI_DEVICE_ERROR;
    else if(status==EFI_SUCCESS)status=ApplyKeyRequest(request,size);
    uint8_t result[32]={'H','A','K','K','R','E','S',0};
    if(size>=24)memcpy(result+8,request+8,16);
    for(unsigned i=0;i<8;++i)result[24+i]=uint8_t(uint64_t(status)>>(8*i));
    const bool saved=SaveSettingsFile(root,u"\\EFI\\HAK\\keys-result.bin",result,sizeof(result));
    boot->FreePool(memory);return saved ? status : EFI_DEVICE_ERROR;
}
