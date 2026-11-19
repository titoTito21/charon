// Disposable firmware test only. Writes variables in the test VM's private
// NVRAM and files on its scratch ESP. Never install this as the HAK component.
#define efi_main unused_component_entry
#include "../src/efi/HAKBootEFI.cpp"
#undef efi_main

static efi_guid pathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
static efi_guid fixtureGuid={0x48616b11,0x8412,0x4610,{0xab,0x11,0x43,0x13,0x15,0x71,0x33,0x02}};
static efi_file_protocol* volume;
static efi_handle ownImage;
static uint8_t fixturePath[2048],fixtureOption[4096];
#include "FirmwareKeyFixture.h"
#include "FirmwareSignedKeyFixture.h"
static bool CheckSecureInventory() {
    efi_file_protocol* file=nullptr;
    if(volume->Open(volume,&file,u"\\EFI\\HAK\\secure-boot.bin",EFI_FILE_MODE_READ,0)!=EFI_SUCCESS)return false;
    uint8_t header[12];size_t size=sizeof(header);
    bool ok=file->Read(file,&size,header)==EFI_SUCCESS && size==sizeof(header)
        && hakefi::Same(header,(const uint8_t*)"HAKKEY1",8);
    const char16_t* names[]={u"PK",u"KEK",u"db",u"dbx"};
    for(unsigned i=0;ok && i<4;++i) {
        uint8_t record[16],live[65536],saved[65536];size=sizeof(record);
        ok=file->Read(file,&size,record)==EFI_SUCCESS && size==sizeof(record);
        if(!ok)break;
        uint64_t recordedStatus=0;
        for(unsigned n=0;n<8;++n)recordedStatus|=uint64_t(record[n])<<(8*n);
        size_t liveSize=sizeof(live);uint32_t attributes=0;
        auto status=systemTable->RuntimeServices->GetVariable((char16_t*)names[i],
            i<2 ? &globalGuid : &securityDatabaseGuid,&attributes,&liveSize,live);
        ok=recordedStatus==status;
        const size_t savedSize=hakefi::U32(record+12);
        if(status==EFI_SUCCESS) {
            ok=ok && savedSize==liveSize && savedSize<=sizeof(saved)
                && hakefi::U32(record+8)==attributes;
            size=savedSize;
            ok=ok && file->Read(file,&size,saved)==EFI_SUCCESS && size==savedSize
                && hakefi::Same(saved,live,savedSize);
        } else ok=ok && savedSize==0 && hakefi::U32(record+8)==0;
    }
    uint8_t end[9];size=sizeof(end);
    ok=ok && file->Read(file,&size,end)==EFI_SUCCESS && size==8
        && hakefi::Same(end,(const uint8_t*)"HAKEND1",8);
    file->Close(file);return ok;
}
static size_t Path(efi_handle device,const char16_t* filename,uint8_t* output) {
    efi_device_path_protocol* path=nullptr;
    if(systemTable->BootServices->HandleProtocol(device,&pathGuid,(void**)&path)!=EFI_SUCCESS)return 0;
    auto* bytes=(uint8_t*)path;size_t at=0;
    while(at<1024) {
        size_t size=hakefi::U16(bytes+at+2);
        if(size<4 || size>1024-at)return 0;
        if(bytes[at]==0x7f && bytes[at+1]==0xff)break;
        at+=size;
    }
    if(at>=1024)return 0;
    memcpy(output,bytes,at);
    size_t chars=0;while(filename[chars])++chars;
    size_t length=4+(chars+1)*2;if(at+length+4>2048)return 0;
    output[at]=4;output[at+1]=4;output[at+2]=(uint8_t)length;output[at+3]=(uint8_t)(length>>8);
    memcpy(output+at+4,filename,(chars+1)*2);
    at+=length;output[at]=0x7f;output[at+1]=0xff;output[at+2]=4;output[at+3]=0;
    return at+4;
}
static bool Request(uint16_t id,const uint8_t* data,size_t length) {
    efi_file_protocol* file=nullptr;
    if(volume->Open(volume,&file,u"\\EFI\\HAK\\next.bin",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0)!=EFI_SUCCESS)return false;
    uint8_t head[6]={(uint8_t)id,(uint8_t)(id>>8),(uint8_t)length,(uint8_t)(length>>8),(uint8_t)(length>>16),0};
    bool ok=Write(file,head,6) && Write(file,data,length) && file->Flush(file)==EFI_SUCCESS;
    file->Close(file);return ok;
}
static efi_status Run(efi_handle device) {
    if(!Path(device,u"\\EFI\\HAK\\HAKBOOT.EFI",fixturePath))return EFI_NOT_FOUND;
    efi_handle child=nullptr;
    auto* boot=systemTable->BootServices;
    efi_status status=boot->LoadImage(false,ownImage,(efi_device_path_protocol*)fixturePath,nullptr,0,&child);
    if(status!=EFI_SUCCESS)return status;
    status=boot->StartImage(child,nullptr,nullptr);boot->UnloadImage(child);return status;
}
static bool Consumed() {
    efi_file_protocol* file=nullptr;
    efi_status status=volume->Open(volume,&file,u"\\EFI\\HAK\\next.bin",EFI_FILE_MODE_READ,0);
    if(status==EFI_SUCCESS)file->Close(file);
    return status==EFI_NOT_FOUND;
}
extern "C" efi_status EFIAPI efi_main(efi_handle self,efi_system_table* table) {
    systemTable=table;ownImage=self;
    Say(u"HAK fixture: entry.\r\n");
    efi_loaded_image_protocol* loaded=nullptr;
    if(table->BootServices->HandleProtocol(self,&imageGuid,(void**)&loaded)!=EFI_SUCCESS)return EFI_NOT_FOUND;
    Say(u"HAK fixture: loaded image.\r\n");
    efi_simple_file_system_protocol* fs=nullptr;
    if(table->BootServices->HandleProtocol(loaded->DeviceHandle,&fsGuid,(void**)&fs)!=EFI_SUCCESS)return EFI_NOT_FOUND;
    Say(u"HAK fixture: filesystem.\r\n");
    if(fs->OpenVolume(fs,&volume)!=EFI_SUCCESS)return EFI_NOT_FOUND;
    Say(u"HAK fixture: volume.\r\n");
    size_t length=Path(loaded->DeviceHandle,u"\\EFI\\HAK\\PAYLOAD.EFI",fixturePath);
    if(!length)return EFI_NOT_FOUND;
    Say(u"HAK fixture: device path.\r\n");
    fixtureOption[0]=1;fixtureOption[4]=(uint8_t)length;fixtureOption[5]=(uint8_t)(length>>8);
    const char16_t label[]=u"HAK firmware fixture";
    memcpy(fixtureOption+6,label,sizeof(label));
    size_t offset=6+sizeof(label);memcpy(fixtureOption+offset,fixturePath,length);
    size_t total=offset+length;fixtureOption[total++]=0x48;fixtureOption[total++]=0x41;
    const uint32_t attributes=EFI_VARIABLE_BOOTSERVICE_ACCESS|EFI_VARIABLE_RUNTIME_ACCESS|EFI_VARIABLE_NON_VOLATILE;
    auto* runtime=table->RuntimeServices;
    if(runtime->SetVariable((char16_t*)u"BootF001",&globalGuid,attributes,total,fixtureOption)!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    Say(u"HAK fixture: option saved.\r\n");
    uint16_t order[256];size_t size=sizeof(order)-2;
    if(runtime->GetVariable((char16_t*)u"BootOrder",&globalGuid,nullptr,&size,order)!=EFI_SUCCESS || size%2)return EFI_NOT_FOUND;
    // Model an installed HAK firmware entry at the front of BootOrder. Some
    // OVMF images put their firmware UI application before disk boot entries.
    uint16_t fixtureCurrent=0;size_t currentSize=2;
    if(runtime->GetVariable((char16_t*)u"BootCurrent",&globalGuid,nullptr,&currentSize,&fixtureCurrent)!=EFI_SUCCESS)return EFI_NOT_FOUND;
    size_t currentIndex=size/2;
    for(size_t i=0;i<size/2;++i)if(order[i]==fixtureCurrent)currentIndex=i;
    if(currentIndex==size/2)return EFI_NOT_FOUND;
    for(size_t i=currentIndex;i>0;--i)order[i]=order[i-1];
    order[0]=fixtureCurrent;
    order[size/2]=0xf001;
    if(runtime->SetVariable((char16_t*)u"BootOrder",&globalGuid,attributes,size+2,order)!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    bool ok=Request(0xf001,fixtureOption,total);
    efi_status status=ok ? Run(loaded->DeviceHandle) : EFI_DEVICE_ERROR;
    uint8_t reached=0;size=1;
    ok=ok && status==EFI_NOT_FOUND && Consumed()
        && runtime->GetVariable((char16_t*)u"HakFixtureReached",&fixtureGuid,nullptr,&size,&reached)==EFI_SUCCESS && reached==1;
    Say(ok ? u"PASS: real EFI image chainload, load options, one-time consumption.\r\n" : u"FAIL: EFI chainload.\r\n");
    reached=0;
    bool reset=runtime->SetVariable((char16_t*)u"HakFixtureReached",&fixtureGuid,EFI_VARIABLE_BOOTSERVICE_ACCESS,1,&reached)==EFI_SUCCESS;
    fixtureOption[8]^=1; // request no longer matches the live BootF001
    bool stale=reset && Request(0xf001,fixtureOption,total) && Run(loaded->DeviceHandle)==EFI_NOT_FOUND && Consumed();
    size=1;
    stale=stale && runtime->GetVariable((char16_t*)u"HakFixtureReached",&fixtureGuid,nullptr,&size,&reached)==EFI_SUCCESS && reached==0;
    Say(stale ? u"PASS: stale firmware option refused and consumed.\r\n" : u"FAIL: stale request.\r\n");
    // A second no-request invocation must not launch the previous selection.
    reached=0;runtime->SetVariable((char16_t*)u"HakFixtureReached",&fixtureGuid,EFI_VARIABLE_BOOTSERVICE_ACCESS,1,&reached);
    bool repeat=Run(loaded->DeviceHandle)==EFI_NOT_FOUND;
    size=1;repeat=repeat && runtime->GetVariable((char16_t*)u"HakFixtureReached",&fixtureGuid,nullptr,&size,&reached)==EFI_SUCCESS && reached==0;
    Say(repeat ? u"PASS: no request never repeats the previous target.\r\n" : u"FAIL: repeated request.\r\n");
    uint16_t current=0;size=2;
    bool settings=runtime->GetVariable((char16_t*)u"BootCurrent",&globalGuid,nullptr,&size,&current)==EFI_SUCCESS;
    uint8_t state[hakefi::kSettingsMaximum]={},change[hakefi::kSettingsMaximum+4];
    size_t stateSize=0;uint32_t orderAttributes=0,timeoutAttributes=0;
    settings=settings && SnapshotSettings(current,state,stateSize,orderAttributes,timeoutAttributes)==EFI_SUCCESS;
    const uint16_t oldTimeout=hakefi::U16(state+12);
    if(settings) {
        hakefi::Put16(change,hakefi::kTimeout);hakefi::Put16(change+2,17);memcpy(change+4,state,stateSize);
        settings=SaveSettingsFile(volume,u"\\EFI\\HAK\\settings-next.bin",change,stateSize+4) && Run(loaded->DeviceHandle)==EFI_NOT_FOUND;
        uint16_t timeout=0;size=2;
        settings=settings && runtime->GetVariable((char16_t*)u"Timeout",&globalGuid,nullptr,&size,&timeout)==EFI_SUCCESS && timeout==17;
        // Reusing the pre-change snapshot must not apply a second change.
        hakefi::Put16(change+2,18);
        settings=settings && SaveSettingsFile(volume,u"\\EFI\\HAK\\settings-next.bin",change,stateSize+4) && Run(loaded->DeviceHandle)==EFI_NOT_FOUND;
        size=2;settings=settings && runtime->GetVariable((char16_t*)u"Timeout",&globalGuid,nullptr,&size,&timeout)==EFI_SUCCESS && timeout==17;
        settings=settings && SnapshotSettings(current,state,stateSize,orderAttributes,timeoutAttributes)==EFI_SUCCESS;
        hakefi::Put16(change,hakefi::kDefault);hakefi::Put16(change+2,0xf001);memcpy(change+4,state,stateSize);
        settings=settings && SaveSettingsFile(volume,u"\\EFI\\HAK\\settings-next.bin",change,stateSize+4) && Run(loaded->DeviceHandle)==EFI_NOT_FOUND;
        uint16_t changedOrder[256];size=sizeof(changedOrder);
        settings=settings && runtime->GetVariable((char16_t*)u"BootOrder",&globalGuid,nullptr,&size,changedOrder)==EFI_SUCCESS
            && size>=4 && changedOrder[0]==current && changedOrder[1]==0xf001;
        // Restore disposable VM settings after testing, and refresh the snapshot.
        uint8_t latest[hakefi::kSettingsMaximum];size_t latestSize=0;
        settings=settings && SnapshotSettings(current,latest,latestSize,orderAttributes,timeoutAttributes)==EFI_SUCCESS;
        uint8_t identified[hakefi::kSettingsMaximum+28]={};memcpy(identified,"HAKREQ2",7);
        for(unsigned i=0;i<16;++i)identified[8+i]=uint8_t(i+31);
        hakefi::Put16(identified+24,hakefi::kTimeout);hakefi::Put16(identified+26,19);
        memcpy(identified+28,latest,latestSize);
        settings=settings && SaveSettingsFile(volume,u"\\EFI\\HAK\\settings-next.bin",identified,latestSize+28) && Run(loaded->DeviceHandle)==EFI_NOT_FOUND;
        efi_file_protocol* report=nullptr;uint8_t reply[33]={};size_t replySize=sizeof(reply);
        bool matched=volume->Open(volume,&report,u"\\EFI\\HAK\\settings-result.bin",EFI_FILE_MODE_READ,0)==EFI_SUCCESS;
        if(matched) {matched=report->Read(report,&replySize,reply)==EFI_SUCCESS;report->Close(report);}
        const uint8_t zero[8]={};
        settings=settings && matched && replySize==32 && hakefi::Same(reply,(const uint8_t*)"HAKRES2",8)
            && hakefi::Same(reply+8,identified+8,16) && hakefi::Same(reply+24,zero,8);
        size=2;settings=settings && runtime->GetVariable((char16_t*)u"Timeout",&globalGuid,nullptr,&size,&timeout)==EFI_SUCCESS && timeout==19;
        runtime->SetVariable((char16_t*)u"Timeout",&globalGuid,timeoutAttributes,2,(void*)&oldTimeout);
        runtime->SetVariable((char16_t*)u"BootOrder",&globalGuid,orderAttributes,stateSize-24,state+24);
        Run(loaded->DeviceHandle);
    }
    Say(settings ? u"PASS: firmware timeout/default, stale settings refused, HAK entry preserved.\r\n" : u"FAIL: firmware settings.\r\n");
    bool updates=TestKeyUpdates();
    bool signedKeys=updates && TestSignedKeyUpdates();
    bool keys=CheckSecureInventory();
    Say(keys ? u"PASS: Secure Boot database snapshot matches live firmware reads.\r\n" : u"FAIL: Secure Boot database inventory.\r\n");
    Say(ok && stale && repeat && settings && keys && updates && signedKeys ? u"HAK EFI TEST COMPLETE: PASS\r\n" : u"HAK EFI TEST COMPLETE: FAIL\r\n");
    volume->Close(volume);
    for(;;)table->BootServices->Stall(1000000);
}
