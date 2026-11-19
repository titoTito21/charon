// Disposable fixture only: request files contain throwaway test certificates.
#include "SecureBootControlFixture.h"
static bool TestSignedKeyUpdates() {
    uint8_t setup=255;size_t size=1;
    if(systemTable->RuntimeServices->GetVariable((char16_t*)u"SetupMode",&globalGuid,nullptr,&size,&setup)!=EFI_SUCCESS || setup!=1)return false;
    bool ok=true;
    for(unsigned step=1;ok && step<=14;++step) {
        char16_t name[]=u"\\EFI\\HAK\\SIGNED\\00.bin";
        name[16]=char16_t('0'+step/10);name[17]=char16_t('0'+step%10);
        efi_file_protocol* input=nullptr;
        if(volume->Open(volume,&input,name,EFI_FILE_MODE_READ,0)!=EFI_SUCCESS)return false;
        uint8_t request[65536];size=sizeof(request);
        const efi_status read=input->Read(input,&size,request);input->Close(input);
        if(read!=EFI_SUCCESS || size<48 || size==sizeof(request))return false;
        const size_t length=size;
        // Firmware may defer its SecureBoot indicator until reset. Capture
        // actual modes, without altering any signed data or expected database.
        const char16_t* modes[]={u"SetupMode",u"SecureBoot"};
        for(unsigned i=0;i<2;++i) {
            size=1;
            if(systemTable->RuntimeServices->GetVariable((char16_t*)modes[i],&globalGuid,nullptr,&size,request+25+i)!=EFI_SUCCESS)return false;
        }
        if((step==1 && request[25]!=1) || (step>1 && request[25]!=0))return false;
        const bool denied=step>=5 && step<=8;
        if(!SaveSettingsFile(volume,u"\\EFI\\HAK\\keys-next.bin",request,length))return false;
        const efi_status status=ProcessKeyRequest(volume);
        ok=status==(denied ? EFI_SECURITY_VIOLATION : EFI_SUCCESS);
        if(denied && ok) {
            const char16_t* names[]={u"PK",u"KEK",u"db",u"dbx"};
            uint8_t actual[16384];size=sizeof(actual);uint32_t attributes=0;
            const unsigned target=request[24];
            ok=systemTable->RuntimeServices->GetVariable((char16_t*)names[target],target<2 ? &globalGuid : &securityDatabaseGuid,&attributes,&size,actual)==EFI_SUCCESS
                && attributes==hakefi::U32(request+36) && size==hakefi::U32(request+40)
                && hakefi::Same(actual,request+48,size);
        }
        efi_file_protocol* output=nullptr;
        if(volume->Open(volume,&output,u"\\EFI\\HAK\\keys-result.bin",EFI_FILE_MODE_READ,0)!=EFI_SUCCESS)return false;
        uint8_t result[33];size=sizeof(result);
        ok=ok && output->Read(output,&size,result)==EFI_SUCCESS && size==32
            && hakefi::Same(result+8,request+8,16) && hakefi::KeyU64(result+24)==status;
        output->Close(output);
        ok=ok && ProcessKeyRequest(volume)==EFI_NOT_FOUND;
        char16_t message[]=u"Signed key step 00: FAIL\r\n";
        message[16]=char16_t('0'+step/10);message[17]=char16_t('0'+step%10);
        if(ok) {message[20]='P';message[21]='A';message[22]='S';message[23]='S';}
        Say(message);
        if(step==1 && ok)ok=TestSecureBootPreference();
    }
    size=1;
    ok=ok && systemTable->RuntimeServices->GetVariable((char16_t*)u"SetupMode",&globalGuid,nullptr,&size,&setup)==EFI_SUCCESS && setup==1;
    SecureBootInventory(volume);
    Say(ok ? u"PASS: signed PK/KEK/db/dbx lifecycle, wrong signer, corruption and timestamp replay refusal.\r\n"
           : u"FAIL: signed key lifecycle.\r\n");
    return ok;
}
