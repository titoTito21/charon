// Disposable Secure Boot QEMU fixture only; never installed in HAK.
static bool TestKeyUpdates() {
    uint8_t mode=255;size_t size=1;
    if(systemTable->RuntimeServices->GetVariable((char16_t*)u"SetupMode",&globalGuid,nullptr,&size,&mode)!=EFI_SUCCESS || mode!=1) {
        Say(u"SKIP: authenticated key updates require disposable setup-mode firmware.\r\n");return true;
    }
    const uint8_t sha256[]={0x26,0x16,0xc4,0xc1,0x4c,0x50,0x92,0x40,0xac,0xa9,0x41,0xf9,0x36,0x93,0x43,0x28};
    const uint8_t pkcs7[]={0x9d,0xd2,0xaf,0x4a,0xdf,0x68,0xee,0x49,0x8a,0xa9,0x34,0x7d,0x37,0x56,0x65,0xa7};
    const char16_t* names[]={u"db",u"dbx"};
    bool ok=true;
    for(unsigned database=0;ok && database<2;++database) {
        uint8_t old[124]={},next[124]={};size_t oldSize=0;
        size=sizeof(old);uint32_t attributes=0;
        if(systemTable->RuntimeServices->GetVariable((char16_t*)names[database],&securityDatabaseGuid,&attributes,&size,old)!=EFI_NOT_FOUND)return false;
        for(unsigned step=0;ok && step<4;++step) {
            // One hash, two hashes, remove first, remove final entry.
            const size_t nextSize=step==3 ? 0 : step==1 ? 124 : 76;
            memset(next,0,sizeof(next));memcpy(next,sha256,16);
            next[16]=uint8_t(nextSize);next[24]=48;
            for(unsigned item=0;item<(step==1 ? 2U : 1U);++item) {
                next[28+48*item]=1; // arbitrary owner GUID
                memset(next+44+48*item,(step==2 || item==1) ? 0xb2 : 0xa1,32);
            }
            uint8_t request[48+124+40+124]={};
            memcpy(request,"HAKUPD1",8);request[8]=uint8_t(1+database*4+step);
            request[24]=uint8_t(2+database);request[25]=1;
            if(!oldSize) {request[28]=14;request[35]=0x80;}
            else request[36]=0x27;
            request[40]=uint8_t(oldSize);request[44]=uint8_t(40+nextSize);
            memcpy(request+48,old,oldSize);
            uint8_t* auth=request+48+oldSize;
            hakefi::Put16(auth,2026);auth[2]=11;auth[3]=15;auth[6]=uint8_t(step+1);
            auth[16]=24;auth[21]=2;auth[22]=0xf1;auth[23]=0x0e;
            memcpy(auth+24,pkcs7,16);memcpy(auth+40,next,nextSize);
            const size_t length=48+oldSize+40+nextSize;
            ok=SaveSettingsFile(volume,u"\\EFI\\HAK\\keys-next.bin",request,length)
                && ProcessKeyRequest(volume)==EFI_SUCCESS;
            // Result belongs to this exact request, not an earlier successful one.
            efi_file_protocol* result=nullptr;
            if(ok && volume->Open(volume,&result,u"\\EFI\\HAK\\keys-result.bin",EFI_FILE_MODE_READ,0)==EFI_SUCCESS) {
                uint8_t bytes[33];size=sizeof(bytes);
                ok=result->Read(result,&size,bytes)==EFI_SUCCESS && size==32
                    && hakefi::Same(bytes+8,request+8,16) && hakefi::KeyU64(bytes+24)==EFI_SUCCESS;
                result->Close(result);
            } else ok=false;
            ok=ok && ProcessKeyRequest(volume)==EFI_NOT_FOUND;
            // Same old database must not authorize a second replacement.
            ok=ok && ApplyKeyRequest(request,length)==EFI_MEDIA_CHANGED;
            memcpy(old,next,nextSize);oldSize=nextSize;
        }
    }
    SecureBootInventory(volume);
    Say(ok ? u"PASS: db/dbx authenticated setup-mode add/remove, stale rejection, consumption and correlated results.\r\n"
           : u"FAIL: authenticated key update fixture.\r\n");
    return ok;
}
