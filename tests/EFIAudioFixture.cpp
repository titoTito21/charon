// Disposable pre-OS audio test. No firmware variables or Haiku files changed.
#define efi_main unused_component_entry
#include "../src/efi/HAKBootEFI.cpp"
#undef efi_main
#include "../src/efi/BootSpeech.h"
#include "../src/efi/BootAnnouncements.h"
static uint8_t pcm[4*1024*1024];
static hakaudioefi::Completion completed;
static void Status(efi_status status) {
    char16_t message[]=u"Audio status: 0000000000000000\r\n";
    const char16_t hex[]=u"0123456789abcdef";
    for(unsigned i=0;i<16;++i)message[14+i]=hex[(uint64_t(status)>>(60-4*i))&15];
    Say(message);
}
extern "C" efi_status EFIAPI efi_main(efi_handle self,efi_system_table* table) {
    systemTable=table;Say(u"HAK EFI AUDIO: entry.\r\n");
    auto* boot=table->BootServices;
    efi_loaded_image_protocol* loaded=nullptr;efi_simple_file_system_protocol* fs=nullptr;
    if(boot->HandleProtocol(self,&imageGuid,(void**)&loaded)!=EFI_SUCCESS
       || boot->HandleProtocol(loaded->DeviceHandle,&fsGuid,(void**)&fs)!=EFI_SUCCESS)return EFI_NOT_FOUND;
    efi_file_protocol* root=nullptr;
    if(fs->OpenVolume(fs,&root)!=EFI_SUCCESS)return EFI_NOT_FOUND;
    // Load the external driver from this test ESP; never install it in firmware.
    efi_file_protocol* file=nullptr;
    auto status=root->Open(root,&file,u"\\EFI\\HAK\\AudioDxe.efi",EFI_FILE_MODE_READ,0);
    if(status!=EFI_SUCCESS)return status;
    size_t size=sizeof(pcm);status=file->Read(file,&size,pcm);file->Close(file);
    if(status!=EFI_SUCCESS || size==sizeof(pcm))return EFI_BAD_BUFFER_SIZE;
    efi_handle driver=nullptr;
    status=boot->LoadImage(false,self,nullptr,pcm,size,&driver);
    if(status!=EFI_SUCCESS) {if(driver)boot->UnloadImage(driver);Status(status);return status;}
    status=boot->StartImage(driver,nullptr,nullptr);
    if(status!=EFI_SUCCESS) {Status(status);boot->UnloadImage(driver);return status;}
    Say(u"HAK EFI AUDIO: driver started.\r\n");
    size_t count=0;efi_handle* handles=nullptr;
    if(boot->LocateHandleBuffer(AllHandles,nullptr,nullptr,&count,&handles)==EFI_SUCCESS) {
        for(size_t i=0;i<count;++i)boot->ConnectController(handles[i],nullptr,nullptr,true);
        boot->FreePool(handles);
    }
    hakaudioefi::Protocol* audio=nullptr;
    status=boot->LocateProtocol(&hakaudioefi::guid,nullptr,(void**)&audio);
    if(status!=EFI_SUCCESS || audio->revision!=4) {Status(status);return EFI_UNSUPPORTED;}
    hakaudioefi::Port* ports=nullptr;count=0;
    status=audio->outputs(audio,&ports,&count);if(status!=EFI_SUCCESS)return status;
    uint64_t mask=0;
    for(size_t i=0;i<count && i<64;++i)
        if(ports[i].type==0 && (ports[i].bits&2) && (ports[i].frequencies&96)==96)mask|=uint64_t(1)<<i;
    boot->FreePool(ports);
    if(!mask) {Say(u"FAIL: no 44.1/48kHz 16-bit output.\r\n");return EFI_UNSUPPORTED;}
    status=audio->setup(audio,mask,-12,64,2,2,0);if(status!=EFI_SUCCESS) {Status(status);return status;}
#ifdef HAK_AUTOMATIC_ANNOUNCEMENTS
    hakaudioefi::Speech speech(audio,root,pcm,sizeof(pcm),mask,&completed,4800,7200);
#else
    hakaudioefi::Speech speech(audio,root,pcm,sizeof(pcm),mask,&completed);
#endif
    Say(u"HAK EFI AUDIO: playing TVox Adam before OS boot.\r\n");
#ifdef HAK_AUTOMATIC_ANNOUNCEMENTS
#ifdef HAK_AUTOMATIC_LANGUAGE_EN
    status=hakaudioefi::Announce(boot,speech,completed,"en");
#else
    status=hakaudioefi::Announce(boot,speech,completed,"pl");
#endif
    Status(status);
    bool interrupted=status==EFI_SUCCESS,replayed=interrupted;
#else
#ifdef HAK_AUTOMATIC_LANGUAGE_EN
    status=speech.SayText("en","Firmware initialized.");
#else
    status=speech.SayText("pl","Firmware załadowany.");
#endif
    Status(status);
    if(status!=EFI_SUCCESS)return status;
    Say(u"HAK EFI AUDIO: keyboard ready; Enter replay, Space interrupt, Escape exit.\r\n");
    bool interrupted=false,replayed=false;
    efi_event timer=nullptr;
    status=boot->CreateEvent(EVT_TIMER,TPL_APPLICATION,nullptr,nullptr,&timer);
    if(status!=EFI_SUCCESS) {speech.Stop();root->Close(root);return status;}
    status=boot->SetTimer(timer,TimerPeriodic,100000);
    if(status!=EFI_SUCCESS) {boot->CloseEvent(timer);speech.Stop();root->Close(root);return status;}
    for(unsigned tick=0;tick<6000;++tick) {
        efi_event events[]={table->ConIn->WaitForKey,timer};size_t eventIndex=0;
        status=boot->WaitForEvent(2,events,&eventIndex);
        if(status!=EFI_SUCCESS)break;
        efi_input_key key={};
        if(table->ConIn->ReadKeyStroke(table->ConIn,&key)==EFI_SUCCESS) {
            if(key.ScanCode==0x17)break;
            if(key.UnicodeChar=='f') {
                status=speech.SayText("en","Firmware initialized.");
                if(status!=EFI_SUCCESS)break;
                Say(u"SPEECH firmware initialized en.\r\n");
            }
            if(key.UnicodeChar=='e' || key.UnicodeChar=='p') {
                status=key.UnicodeChar=='e' ? speech.SayText("en","Starting boot manager.")
                    : speech.SayText("pl","Uruchamianie menedżera rozruchu.");
                if(status!=EFI_SUCCESS)break;
                Say(key.UnicodeChar=='e' ? u"SPEECH language en.\r\n" : u"SPEECH language pl.\r\n");
            }
            if(key.UnicodeChar==' ' || key.UnicodeChar=='\r') {
                status=speech.Stop();
                if(status!=EFI_SUCCESS)break;
                if(key.UnicodeChar==' ') {interrupted=true;Say(u"PASS: keyboard stopped pre-OS speech.\r\n");}
                else {
                    status=speech.SayText("pl","Uruchamianie menedżera rozruchu.");
                    if(status!=EFI_SUCCESS)break;
                    replayed=true;Say(u"PASS: keyboard restarted pre-OS speech.\r\n");
                }
            }
        }
    }
    boot->CloseEvent(timer);
#endif
    const auto interactionStatus=status;
    const auto stopped=speech.Handoff();
    if(stopped==EFI_SUCCESS && speech.SayText("pl","Uruchamianie menedżera rozruchu.")==EFI_ACCESS_DENIED)
        Say(u"PASS: firmware speech handoff blocks late announcements.\r\n");
    else {root->Close(root);Say(u"FAIL: speech handoff.\r\n");return EFI_ABORTED;}
    // Test-only successor: require stopped audio before loading another image.
    status=root->Open(root,&file,u"\\EFI\\HAK\\PAYLOAD.EFI",EFI_FILE_MODE_READ,0);
    if(status!=EFI_SUCCESS) {root->Close(root);return status;}
    size=sizeof(pcm);status=file->Read(file,&size,pcm);file->Close(file);root->Close(root);
    if(status!=EFI_SUCCESS || !size || size>=sizeof(pcm))return EFI_BAD_BUFFER_SIZE;
    efi_handle successor=nullptr;
    efi_guid reachedGuid={0x48616b11,0x8412,0x4610,{0xab,0x11,0x43,0x13,0x15,0x71,0x33,0x02}};
    uint8_t reached=0;
    status=table->RuntimeServices->SetVariable((char16_t*)u"HakFixtureReached",&reachedGuid,EFI_VARIABLE_BOOTSERVICE_ACCESS,1,&reached);
    if(status!=EFI_SUCCESS)return status;
    status=boot->LoadImage(false,self,nullptr,pcm,size,&successor);
    if(status!=EFI_SUCCESS) {if(successor)boot->UnloadImage(successor);return status;}
    efi_loaded_image_protocol* child=nullptr;
    status=boot->HandleProtocol(successor,&imageGuid,(void**)&child);
    uint8_t options[]={0x48,0x41};
    if(status==EFI_SUCCESS) {
        child->LoadOptionsSize=sizeof(options);child->LoadOptions=options;
        status=boot->StartImage(successor,nullptr,nullptr);
    }
    boot->UnloadImage(successor);
    size=1;
    if(status!=EFI_SUCCESS || table->RuntimeServices->GetVariable((char16_t*)u"HakFixtureReached",&reachedGuid,nullptr,&size,&reached)!=EFI_SUCCESS || size!=1 || reached!=1)
        return EFI_ABORTED;
    Say(u"PASS: successor image returned after speech handoff.\r\n");
    if(interactionStatus==EFI_SUCCESS && status==EFI_SUCCESS && stopped==EFI_SUCCESS && interrupted && replayed)
#ifdef HAK_AUTOMATIC_ANNOUNCEMENTS
        Say(u"PASS: automatic pre-OS announcements completed before handoff.\r\n");
#else
        Say(u"PASS: interruptible pre-OS TVox keyboard audio complete.\r\n");
#endif
    else {Say(u"FAIL: pre-OS keyboard audio incomplete.\r\n");status=EFI_ABORTED;}
    return status;
}
