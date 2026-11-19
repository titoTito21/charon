// Included by HAKBootEFI.cpp after EFI helpers. Only explicit queued changes
// reach SetVariable; Secure Boot and vendor setup variables are never written.
#pragma once
#include "BootSettings.h"
static efi_status SnapshotSettings(uint16_t current,uint8_t* output,size_t& length,uint32_t& orderAttributes,uint32_t& timeoutAttributes) {
    const uint8_t magic[8]={'H','A','K','S','E','T','1',0};
    memset(output,0,hakefi::kSettingsMaximum);memcpy(output,magic,8);
    hakefi::Put16(output+8,current);output[11]=255;
    auto* runtime=systemTable->RuntimeServices;
    size_t count=512;
    efi_status status=runtime->GetVariable((char16_t*)u"BootOrder",&globalGuid,&orderAttributes,&count,output+24);
    if(status!=EFI_SUCCESS || count%2)return EFI_NOT_FOUND;
    hakefi::Put16(output+14,uint16_t(count/2));length=24+count;
    size_t size=2;
    if(runtime->GetVariable((char16_t*)u"Timeout",&globalGuid,&timeoutAttributes,&size,output+12)==EFI_SUCCESS && size==2)output[10]=1;
    size=1;uint8_t secure=255;
    if(runtime->GetVariable((char16_t*)u"SecureBoot",&globalGuid,nullptr,&size,&secure)==EFI_SUCCESS && size==1 && secure<=1)output[11]=secure;
    size=8;
    if(runtime->GetVariable((char16_t*)u"OsIndicationsSupported",&globalGuid,nullptr,&size,output+16)!=EFI_SUCCESS || size!=8)memset(output+16,0,8);
    return hakefi::Settings(output,length) ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}
static bool SaveSettingsFile(efi_file_protocol* root,const char16_t* name,const uint8_t* data,size_t length) {
    efi_file_protocol* file=nullptr;
    efi_status status=root->Open(root,&file,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(status==EFI_SUCCESS && file->Delete(file)!=EFI_SUCCESS)return false;
    if(root->Open(root,&file,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0)!=EFI_SUCCESS)return false;
    const bool ok=Write(file,data,length) && file->Flush(file)==EFI_SUCCESS;
    file->Close(file);return ok;
}
static efi_status ProcessSettings(efi_file_protocol* root,uint16_t current) {
    uint8_t before[hakefi::kSettingsMaximum],storage[hakefi::kSettingsMaximum+29];
    size_t length=0;uint32_t orderAttributes=0,timeoutAttributes=0;
    efi_status snapshot=SnapshotSettings(current,before,length,orderAttributes,timeoutAttributes);
    efi_file_protocol* file=nullptr;
    efi_status status=root->Open(root,&file,u"\\EFI\\HAK\\settings-next.bin",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    const bool requested=status!=EFI_NOT_FOUND;
    efi_status operation=status;
    if(status==EFI_SUCCESS) {
        size_t size=sizeof(storage);status=file->Read(file,&size,storage);
        const uint8_t magic[8]={'H','A','K','R','E','Q','2',0};
        const bool version2=size>=28 && hakefi::Same(storage,magic,8);
        uint8_t* request=storage+(version2 ? 24 : 0);
        if(version2)size-=24;
        const efi_status consumed=file->Delete(file);
        if(consumed!=EFI_SUCCESS)status=EFI_DEVICE_ERROR;
        else if(status!=EFI_SUCCESS || snapshot!=EFI_SUCCESS || size!=length+4 || !hakefi::Same(request+4,before,length))status=EFI_INVALID_PARAMETER;
        else {
            const uint16_t command=hakefi::U16(request),value=hakefi::U16(request+2);
            if(!hakefi::ChangeSettings(request+4,length,command,value))status=EFI_UNSUPPORTED;
            else {
                auto* runtime=systemTable->RuntimeServices;
                if(command==hakefi::kTimeout)
                    status=runtime->SetVariable((char16_t*)u"Timeout",&globalGuid,timeoutAttributes,2,request+16);
                else {
                    size_t optionLength;hakefi::BootOption option;
                    if(ReadOption(value,optionLength)!=EFI_SUCCESS || !hakefi::Parse(optionBytes,optionLength,option))status=EFI_NOT_FOUND;
                    else status=runtime->SetVariable((char16_t*)u"BootOrder",&globalGuid,orderAttributes,length-24,request+28);
                }
            }
        }
        uint8_t result[32]={'H','A','K','R','E','S','1',0};
        if(version2) {result[6]='2';memcpy(result+8,storage+8,16);}
        for(unsigned i=0;i<8;++i)result[(version2 ? 24 : 8)+i]=uint8_t(uint64_t(status)>>(i*8));
        operation=status;
        if(!SaveSettingsFile(root,u"\\EFI\\HAK\\settings-result.bin",result,version2 ? 32 : 16)
            && operation==EFI_SUCCESS)operation=EFI_DEVICE_ERROR;
        snapshot=SnapshotSettings(current,before,length,orderAttributes,timeoutAttributes);
    }
    // Refresh/export even after rejection, but never turn an operation failure
    // into a successful menu result merely because its snapshot was saved.
    const auto exported=snapshot!=EFI_SUCCESS ? snapshot
        : SaveSettingsFile(root,u"\\EFI\\HAK\\settings.bin",before,length) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
    return requested && operation!=EFI_SUCCESS ? operation : exported;
}
