// Read-only Secure Boot inventory. No key or mode variables are written here.
#pragma once
static efi_guid securityDatabaseGuid={0xd719b2cb,0x3d3a,0x4596,{0xa3,0xbc,0xda,0xd0,0x0e,0x67,0x65,0x6f}};
static efi_status SecureBootInventory(efi_file_protocol* root) {
    efi_file_protocol* file=nullptr;
    const char16_t* path=u"\\EFI\\HAK\\secure-boot.bin";
    efi_status status=root->Open(root,&file,path,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(status==EFI_SUCCESS) {
        if(file->Delete(file)!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    } else if(status!=EFI_NOT_FOUND)return status;
    status=root->Open(root,&file,path,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(status!=EFI_SUCCESS)return status;
    uint8_t header[12]={'H','A','K','K','E','Y','1',0,255,255,255,255};
    const char16_t* modes[]={u"SecureBoot",u"SetupMode",u"AuditMode",u"DeployedMode"};
    auto* runtime=systemTable->RuntimeServices;
    auto* boot=systemTable->BootServices;
    for(unsigned i=0;i<4;++i) {
        size_t size=1;uint8_t value=255;
        if(runtime->GetVariable((char16_t*)modes[i],&globalGuid,nullptr,&size,&value)==EFI_SUCCESS && size==1 && value<=1)header[8+i]=value;
    }
    bool ok=Write(file,header,sizeof(header));
    const char16_t* names[]={u"PK",u"KEK",u"db",u"dbx"};
    for(unsigned i=0;ok && i<4;++i) {
        efi_guid* guid=i<2 ? &globalGuid : &securityDatabaseGuid;
        size_t size=0;uint32_t attributes=0;void* data=nullptr;
        status=runtime->GetVariable((char16_t*)names[i],guid,&attributes,&size,nullptr);
        if(status==EFI_SUCCESS && size!=0)status=EFI_BAD_BUFFER_SIZE;
        if(status==EFI_BUFFER_TOO_SMALL) {
            if(size==0 || size>4*1024*1024)status=EFI_BAD_BUFFER_SIZE;
            else {
                const size_t capacity=size;
                status=boot->AllocatePool(EfiLoaderData,capacity,&data);
                if(status==EFI_SUCCESS) {
                    status=runtime->GetVariable((char16_t*)names[i],guid,&attributes,&size,data);
                    if(status==EFI_SUCCESS && size>capacity)status=EFI_BAD_BUFFER_SIZE;
                }
            }
        }
        // Errors retain their status, never masquerading as an empty database.
        if(status!=EFI_SUCCESS) {size=0;attributes=0;}
        uint8_t record[16]={};
        for(unsigned n=0;n<8;++n)record[n]=uint8_t(uint64_t(status)>>(8*n));
        for(unsigned n=0;n<4;++n) {
            record[8+n]=uint8_t(attributes>>(8*n));record[12+n]=uint8_t(size>>(8*n));
        }
        ok=Write(file,record,sizeof(record)) && (!size || Write(file,data,size));
        if(data)boot->FreePool(data);
    }
    const uint8_t end[8]={'H','A','K','E','N','D','1',0};
    ok=ok && Write(file,end,sizeof(end)) && file->Flush(file)==EFI_SUCCESS;
    file->Close(file);return ok ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
