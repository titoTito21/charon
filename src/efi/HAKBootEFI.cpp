// HAK UEFI component. Own ESP files only; no Haiku loader modifications.
// GPL-2.0-or-later. EFI declarations in haiku/ retain their upstream licenses.
#include <efi/system-table.h>
#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>
#include "BootOption.h"

#ifdef HAK_CHARON_LIVE_VOICE
extern "C" void* memcpy(void*,const void*,size_t);
extern "C" void* memset(void*,int,size_t);
#else
extern "C" void* memcpy(void* dst,const void* src,size_t count) {
    auto* a=(uint8_t*)dst;auto* b=(const uint8_t*)src;
    for(size_t i=0;i<count;++i)a[i]=b[i];
    return dst;
}
extern "C" void* memset(void* dst,int value,size_t count) {
    auto* a=(uint8_t*)dst;for(size_t i=0;i<count;++i)a[i]=(uint8_t)value;return dst;
}
#endif
static efi_guid globalGuid=EFI_GLOBAL_VARIABLE;
static efi_guid imageGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
static efi_guid fsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static uint8_t optionBytes[65536],requestBytes[65543];
static efi_system_table* systemTable;
static void Say(const char16_t* text) {
    if(systemTable->ConOut)systemTable->ConOut->OutputString(systemTable->ConOut,(char16_t*)text);
}
// r376: console lines in explicit mode (QRWA). Before the menu the saved
// setting decides; after it, the menu's current choice (residentPolish bit 1).
static bool consoleExplicit=false,consolePolish=false;
static void SayStyled(const char16_t* normal,const char16_t* plRude,const char16_t* enRude) {
    Say(consoleExplicit?(consolePolish?plRude:enRude):normal);
}
static void Name(uint16_t id,char16_t* name) {
    const char16_t hex[]=u"0123456789ABCDEF";
    name[0]='B';name[1]='o';name[2]='o';name[3]='t';
    for(int i=0;i<4;++i)name[4+i]=hex[(id>>(12-4*i))&15];
    name[8]=0;
}
static efi_status ReadOption(uint16_t id,size_t& size) {
    char16_t name[9];Name(id,name);size=sizeof(optionBytes);
    return systemTable->RuntimeServices->GetVariable(name,&globalGuid,nullptr,&size,optionBytes);
}
static bool Write(efi_file_protocol* file,const void* data,size_t length) {
    size_t actual=length;
    return file->Write(file,&actual,(void*)data)==EFI_SUCCESS && actual==length;
}
static efi_status Inventory(efi_file_protocol* root,uint16_t current) {
    uint16_t order[256];size_t size=sizeof(order);
    efi_status status=systemTable->RuntimeServices->GetVariable((char16_t*)u"BootOrder",&globalGuid,nullptr,&size,order);
    if(status!=EFI_SUCCESS || size%2)return EFI_INVALID_PARAMETER;
    efi_file_protocol* file=nullptr;
    // Delete the old inventory; incomplete new inventories are rejected by the
    // client via a mandatory end marker. A failed refresh never selects a boot.
    status=root->Open(root,&file,u"\\EFI\\HAK\\inventory.bin",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(status==EFI_SUCCESS && file->Delete(file)!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    status=root->Open(root,&file,u"\\EFI\\HAK\\inventory.bin",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(status!=EFI_SUCCESS)return status;
    const uint8_t magic[8]={'H','A','K','E','F','I','1',0};
    bool ok=Write(file,magic,8);
    for(size_t i=0;ok && i<size/2;++i) {
        if(order[i]==current)continue;
        size_t bytes;hakefi::BootOption parsed;
        if(ReadOption(order[i],bytes)!=EFI_SUCCESS || !hakefi::Parse(optionBytes,bytes,parsed))continue;
        uint8_t head[6]={(uint8_t)order[i],(uint8_t)(order[i]>>8),(uint8_t)bytes,(uint8_t)(bytes>>8),(uint8_t)(bytes>>16),0};
        ok=Write(file,head,6) && Write(file,optionBytes,bytes);
    }
    const uint8_t end[6]={0xff,0xff,0,0,0,0};
    ok=ok && Write(file,end,6) && file->Flush(file)==EFI_SUCCESS;
    file->Close(file);return ok ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
static efi_status StartRequested(efi_handle self,efi_file_protocol* root,uint16_t current) {
    efi_file_protocol* file=nullptr;
    efi_status status=root->Open(root,&file,u"\\EFI\\HAK\\next.bin",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
    if(status!=EFI_SUCCESS)return status;
    size_t size=sizeof(requestBytes);
    status=file->Read(file,&size,requestBytes);
    // Consume before attempting a load, including invalid requests. If deletion
    // fails, refuse to boot: a one-time request must never become a boot loop.
    const efi_status consumed=file->Delete(file);
    if(consumed!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    if(status!=EFI_SUCCESS || size<6 || hakefi::U32(requestBytes+2)!=size-6)return EFI_INVALID_PARAMETER;
    const uint16_t id=hakefi::U16(requestBytes);
    if(id==current)return EFI_ACCESS_DENIED;
    size_t bytes;hakefi::BootOption parsed;
    if(ReadOption(id,bytes)!=EFI_SUCCESS || bytes!=size-6
        || !hakefi::Same(optionBytes,requestBytes+6,bytes)
        || !hakefi::Parse(optionBytes,bytes,parsed))return EFI_NOT_FOUND;
    auto* boot=systemTable->BootServices;
    efi_handle image=nullptr;
    status=boot->LoadImage(true,self,(efi_device_path_protocol*)(optionBytes+parsed.pathOffset),nullptr,0,&image);
    if(status!=EFI_SUCCESS) {if(image)boot->UnloadImage(image);return status;}
    efi_loaded_image_protocol* loaded=nullptr;
    status=boot->HandleProtocol(image,&imageGuid,(void**)&loaded);
    if(status==EFI_SUCCESS) {
        loaded->LoadOptionsSize=(uint32_t)(bytes-parsed.optionsOffset);
        loaded->LoadOptions=loaded->LoadOptionsSize ? optionBytes+parsed.optionsOffset : nullptr;
        SayStyled(u"HAK EFI: starting selected boot option.\r\n",u"HAK EFI: odpalam, co wybrałeś, i niech cię Bóg ma w opiece, kurwa mać.\r\n",u"HAK EFI: firing up your pick, God help you, fuck it.\r\n");
        // r387: boot-option watchdog (UEFI 2.11 section 3.1.2), as in StartVerified.
        if(boot->SetWatchdogTimer)boot->SetWatchdogTimer(300,0,0,nullptr);
        status=boot->StartImage(image,nullptr,nullptr);
        if(boot->SetWatchdogTimer)boot->SetWatchdogTimer(0,0,0,nullptr);
    }
    boot->UnloadImage(image);return status;
}
#include "FirmwareSettings.h"
#include "SecureBootInventory.h"
#include "FirmwareKeyUpdate.h"
#include "FirmwareSecureBootControl.h"
#include "OwnedShellLauncher.h"
#include "MenuVoicePublication.h"
#ifdef HAK_EFI_DIAGNOSTIC_CACHED_ANNOUNCEMENTS
#include "BootAudio.h"
#endif
#include "BootMenu.h"
#ifdef HAK_CHARON_LIVE_VOICE
#include "CharonVoiceProvider.h"
#include <new>
#endif
#include "CharonReaderControl.h"
#ifdef HAK_CHARON_VOICE_DRIVER
// ---- CHARONVOICE.EFI: resident voice and screen reader from Driver#### ----
// Same sources as CHARON.EFI, built as a UEFI boot-service driver. Firmware
// loads it before its own boot manager: from then on every text screen
// (firmware menus and setup, Charon, loaders, tools) is read until
// ExitBootServices. Storage is static (no allocation outlives a failure path
// silently) and constructed in place: there is no C++ runtime.
#include <new>
alignas(charonaudioefi::VoiceProvider) static unsigned char providerStorage[sizeof(charonaudioefi::VoiceProvider)];
static charonaudioefi::VoiceProvider* driverProvider;
static hakefi::MenuVoicePublication driverPublication;
static hakefi::ReaderControl driverControl;
static efi_status EFIAPI DriverSuspend(hakefi::ReaderControl*) {return hakefi::residentReader.Suspend();}
static void UnpackReading(hakefi::ResidentOptions& o,uint32_t reading) {
    o.punctuation=uint8_t(reading&3);o.capitals=uint8_t((reading>>2)&7);o.screenLanguage=uint8_t((reading>>5)&3);o.readerFlags=uint8_t((reading>>8)&63);
    o.formatFlags=hakefi::ReadingFormat(reading);o.stereo=hakefi::ReadingStereo(reading);const uint8_t extra=uint8_t((reading>>20)&31);o.managerFlags=uint8_t(extra&7);o.linger=(reading>>29)&1;o.brailleAttributes=(extra&8)!=0;o.dictionary=(extra&16)!=0;   // r393
}
static efi_status EFIAPI DriverResume(hakefi::ReaderControl*,const char* language,uint8_t polish,uint8_t verbosity,uint8_t progress,uint32_t voice,uint32_t reading) {
    hakefi::ResidentOptions update;
    if(language && language[0] && language[1]) {update.language[0]=language[0];update.language[1]=language[1];}
    // polish: bit 0 Polish, bit 1 explicit messages (r373; older menus pass 0/1).
    update.polish=(polish&1)!=0;update.explicitMessages=(polish&2)!=0;update.positions=(polish&4)==0;update.timers=uint8_t((polish>>3)&3);update.verbosity=verbosity;update.progress=progress;
    update.reviewOnly=(polish&0x20)!=0;   // r401: Charon's own screens - review keys only
    update.rate=uint8_t(voice);update.pitch=uint8_t(voice>>8);update.volume=uint8_t(voice>>16);update.effectsVolume=uint8_t(voice>>24);
    UnpackReading(update,reading);
    return hakefi::residentReader.Resume(&update);
}
// r402: an active driver option marked "force reconnect" (the options Charon's
// Windows installer writes) makes firmware disconnect and reconnect every
// controller right after the driver options are processed. A sound card bound
// before that loses its driver again under the voice (r400/r401 crashed the
// firmware there), and the reconnect binds it anyway.
static bool ReconnectFollows() {
    if(!systemTable || !systemTable->RuntimeServices)return false;
    uint16_t order[64];size_t size=sizeof(order);
    if(systemTable->RuntimeServices->GetVariable((char16_t*)u"DriverOrder",&globalGuid,nullptr,&size,order)!=EFI_SUCCESS)return false;
    static uint8_t option[4096];
    for(size_t i=0;i<size/2;++i) {
        char16_t name[11]=u"Driver0000";static const char16_t hex[]=u"0123456789ABCDEF";
        for(int d=0;d<4;++d)name[6+d]=hex[(order[i]>>(12-4*d))&15];
        size_t bytes=sizeof(option);
        if(systemTable->RuntimeServices->GetVariable(name,&globalGuid,nullptr,&bytes,option)!=EFI_SUCCESS || bytes<4)continue;
        if((option[0]&1) && (option[0]&2))return true;   // LOAD_OPTION_ACTIVE | LOAD_OPTION_FORCE_RECONNECT
    }
    return false;
}
static void EFIAPI DriverConnectAudio(efi_event,void*) {
    if(systemTable && systemTable->BootServices)charonaudioefi::ConnectAudioControllers(systemTable->BootServices);
}
extern "C" efi_status EFIAPI efi_main(efi_handle self,efi_system_table* table) {
    systemTable=table;
    if(!table || table->Hdr.Signature!=EFI_SYSTEM_TABLE_SIGNATURE)return EFI_INVALID_PARAMETER;
    auto* boot=table->BootServices;
    efi_loaded_image_protocol* loaded=nullptr;
    efi_status status=boot->HandleProtocol(self,&imageGuid,(void**)&loaded);
    if(status!=EFI_SUCCESS)return status;
    efi_simple_file_system_protocol* fs=nullptr;
    status=boot->HandleProtocol(loaded->DeviceHandle,&fsGuid,(void**)&fs);
    if(status!=EFI_SUCCESS)return status;
    efi_file_protocol* root=nullptr;
    status=fs->OpenVolume(fs,&root);if(status!=EFI_SUCCESS)return status;
    hakefi::CharSettings settings;hakefi::LoadCharSettings(root,settings);
    char locale[64]={};hakefi::InstallationLocale(root,locale);
    const bool pl=hakefi::PolishSettings(settings,locale);
    driverProvider=new(providerStorage) charonaudioefi::VoiceProvider;
    status=driverProvider->Initialize(self,boot,root,true);
    root->Close(root);
    if(status!=EFI_SUCCESS) {
        // Never leave DMA or callbacks behind an unloaded image.
        if(driverProvider->Shutdown()!=EFI_SUCCESS)return EFI_SUCCESS; // stay resident, silent
        return status;
    }
    auto* effects=driverProvider->Effects();
    // r382: from power-on this voice is the screen reader's.
    hakefi::ApplyVoiceProfile(effects,settings.readerRate,settings.readerPitch,settings.readerVolume,settings.effectsVolume);
    if(effects && effects->SetMessageVoice)effects->SetMessageVoice(effects->context,settings.messageRate,settings.messagePitch);   // r389
    if(effects && effects->SetEnabled)effects->SetEnabled(effects->context,settings.sounds);
    status=driverPublication.Install(boot,driverProvider->Voice());
    if(status!=EFI_SUCCESS)return EFI_SUCCESS; // resident; without publication the menu uses its own voice
    hakefi::ResidentOptions options;options.selfImage=self;options.voice=driverProvider->Voice();options.effects=effects;
    options.language[0]=pl?'p':'e';options.language[1]=pl?'l':'n';options.polish=pl;
    options.verbosity=settings.verbosity?settings.verbosity:1;options.progress=settings.progress;options.explicitMessages=settings.explicitMessages;
    options.rate=settings.readerRate;options.pitch=settings.readerPitch;options.volume=settings.readerVolume;options.effectsVolume=settings.effectsVolume;
    options.positions=(settings.detail&4)!=0;options.timers=settings.timers;
    UnpackReading(options,hakefi::PackReading(settings.punctuation,settings.capitals,settings.screenLanguage,settings.readerFlags,settings.formatFlags,uint8_t(settings.readerExtra&~7)));   // r393: a firmware boot (add-on) is a quick boot; the device list turns the manager handling on
    // r393/r397 (user): the add-on detects the external synthesizers in
    // \EFI\HAK\synths too. The chosen one speaks from power-on (TVox when it
    // cannot - then the reason is spoken); the voice ring later switches among
    // TVox and the add-ons through the same provider that scans them.
    char16_t synthNote[220]={};
    if(settings.synthKey && effects && effects->SetSynth) {
        char16_t report[200]={};const bool ok=effects->SetSynth(effects->context,settings.synthKey,settings.synthVoice,report,200);
        if(!ok)hakefi::SynthMessage(false,report,pl,synthNote,220);   // TVox falls in; say why so detection failures are not silent
    }
    charontrace::Text("CHARON driver: before reader install");
    status=hakefi::residentReader.Install(table,options);
    charontrace::Value("CHARON driver: reader installed",uint64_t(status));
    if(status!=EFI_SUCCESS)return EFI_SUCCESS;
    driverControl={1,sizeof(hakefi::ReaderControl),hakefi::charonRelease,0,driverProvider->Voice(),effects,DriverSuspend,DriverResume};
    efi_handle handle=nullptr;
    boot->InstallProtocolInterface(&handle,&hakefi::readerControlGuid,EFI_NATIVE_INTERFACE,&driverControl);
    // r406: a mark that firmware loaded this add-on (a volatile variable the started system can
    // read). Firmware that ignores driver options (VMware's) never sets it; the installed system
    // then starts the reader through the Charon boot manager instead (install-charon.sh check).
    {static efi_guid markGuid={0x7a3c5e21,0x4b9d,0x4f1e,{0xa6,0x52,0x1c,0x8e,0x93,0x0d,0x27,0xb5}};uint8_t one=1;
     if(table->RuntimeServices && table->RuntimeServices->SetVariable)table->RuntimeServices->SetVariable((char16_t*)u"CharonVoiceLoaded",&markGuid,6,1,&one);}
    // r385: the add-on greets with one short PC-speaker style beep (as after
    // POST), not with the menu's welcome sound.
    // r400: fast-booting firmware never binds the sound card for us.
    // r402: not when firmware is about to reconnect everything (see ReconnectFollows).
    charontrace::Value("CHARON driver: reconnect follows",uint64_t(ReconnectFollows()));
    if(!ReconnectFollows())charonaudioefi::ConnectAudioControllers(boot);
    charontrace::Text("CHARON driver: sound cards connected");
    {static efi_guid readyToBoot={0x7ce88fb3,0x4bd7,0x4679,{0x87,0xa8,0xa8,0xd8,0xde,0xe5,0x0d,0x2b}};
     static efi_guid endOfDxe={0x02ce967a,0xdd7e,0x4ffc,{0x9e,0xe7,0x81,0x0c,0xf0,0x47,0x08,0x80}};
     static efi_event events[2];
     if(boot->CreateEventEx) {
         boot->CreateEventEx(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,DriverConnectAudio,nullptr,&endOfDxe,&events[0]);
         boot->CreateEventEx(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,DriverConnectAudio,nullptr,&readyToBoot,&events[1]);
     }}
    // r400 (user): two rising tones and just the name.
    if(effects && effects->Beep)effects->Beep(effects->context,8000,320);
    char16_t greet[400]={};hakefi::Append(greet,400,u"Charon.");
    if(synthNote[0]) {hakefi::Append(greet,400,u" ");hakefi::Append(greet,400,synthNote);}
    charontrace::Text("CHARON driver: before greeting");
    hakefi::residentReader.Announce(greet);
    charontrace::Text("CHARON driver: started, returning to firmware");
    return EFI_SUCCESS;
}
#else
extern "C" efi_status EFIAPI efi_main(efi_handle self,efi_system_table* table) {
    systemTable=table;
    if(!table || table->Hdr.Signature!=EFI_SYSTEM_TABLE_SIGNATURE)return EFI_INVALID_PARAMETER;
    // An interactive boot manager may remain open longer than the boot
    // watchdog's usual timeout; do not reset while the user explores its menu.
    if(table->BootServices->SetWatchdogTimer)
        table->BootServices->SetWatchdogTimer(0,0,0,nullptr);
    efi_loaded_image_protocol* loaded=nullptr;
    efi_status status=table->BootServices->HandleProtocol(self,&imageGuid,(void**)&loaded);
    if(status!=EFI_SUCCESS)return status;
    efi_simple_file_system_protocol* fs=nullptr;
    status=table->BootServices->HandleProtocol(loaded->DeviceHandle,&fsGuid,(void**)&fs);
    if(status!=EFI_SUCCESS)return status;
    efi_file_protocol* root=nullptr;
    status=fs->OpenVolume(fs,&root);if(status!=EFI_SUCCESS)return status;
    root=hakefi::selfram::Wrap(root);   // r408: so every holder of `root` follows a later switch to the RAM copy
    {hakefi::CharSettings saved;hakefi::LoadCharSettings(root,saved);char locale[64]={};hakefi::InstallationLocale(root,locale);
        consoleExplicit=saved.explicitMessages;consolePolish=hakefi::PolishSettings(saved,locale);}
    uint16_t current=0xffff;size_t size=sizeof(current);
    if(table->RuntimeServices->GetVariable((char16_t*)u"BootCurrent",&globalGuid,nullptr,&size,&current)!=EFI_SUCCESS || size!=2) {
        // Without our firmware identity we cannot exclude a self-chainload or
        // safely interpret a saved Continue selection. Never choose another OS.
        SayStyled(u"HAK EFI: boot identity unavailable; automatic handoff blocked.\r\n",u"HAK EFI: nie wiem, kurwa, z czego wystartowałem; automatyczny start zablokowany.\r\n",u"HAK EFI: no fucking idea what I booted from; automatic handoff blocked.\r\n");
        while(true)table->BootServices->Stall(1000000);
    }
#ifdef HAK_EFI_DIAGNOSTIC_CACHED_ANNOUNCEMENTS
    const auto audioStatus=hakaudioefi::BootAudio(self,table,root);
    if(audioStatus==EFI_SUCCESS)SayStyled(u"HAK EFI: announcements complete; audio stopped.\r\n",u"HAK EFI: nagadałem się, dźwięk wyłączony, i chuj.\r\n",u"HAK EFI: done yapping, audio off, fuck it.\r\n");
    else if(audioStatus!=EFI_NOT_FOUND)SayStyled(u"HAK EFI: announcements unavailable; continuing boot.\r\n",u"HAK EFI: zapowiedzi poszły w pizdu; startuję dalej bez nich.\r\n",u"HAK EFI: announcements went to shit; booting on without them.\r\n");
#endif
    // Requests changing firmware settings/keys require explicit menu
    // confirmation. Merely starting this component never applies them.
    SecureBootInventory(root);
    SaveSecureControl(root);
    status=Inventory(root,current);
    if(status==EFI_SUCCESS)SayStyled(u"HAK EFI: inventory saved.\r\n",u"HAK EFI: spis zapisany, i chuj.\r\n",u"HAK EFI: inventory saved, fuck yeah.\r\n");
    else SayStyled(u"HAK EFI: inventory unavailable.\r\n",u"HAK EFI: spisu chuj, nie zapisał się.\r\n",u"HAK EFI: inventory fucked, not saved.\r\n");
#ifdef HAK_CHARON_LIVE_VOICE
    // r356: a resident CHARONVOICE.EFI of this release already speaks and
    // reads the screen; the menu then uses that voice and pauses the reader.
    hakefi::residentDriver=hakefi::FindReaderControl(table->BootServices);
    // r400: the add-on's sound card may still have no driver bound (fast boot).
    if(hakefi::residentDriver)charonaudioefi::ConnectAudioControllers(table->BootServices);
    if(hakefi::residentDriver)hakefi::residentDriver->Suspend(hakefi::residentDriver);
    // r386: the provider holds ~200 KB of tone buffers (r384's 500 ms end
    // tone): on the firmware's stack it reached below the stack, and a deep
    // boot manager (Limine) overwrote its voice callbacks (#UD at the next
    // pump). It lives in static storage like the driver's provider.
    alignas(charonaudioefi::VoiceProvider) static unsigned char menuProviderStorage[sizeof(charonaudioefi::VoiceProvider)];
    charonaudioefi::VoiceProvider& provider=*new(menuProviderStorage) charonaudioefi::VoiceProvider;
    const auto voiceStatus=hakefi::residentDriver?EFI_SUCCESS:provider.Initialize(self,table->BootServices,root);
    if(voiceStatus!=EFI_SUCCESS) {
        SayStyled(u"Charon: live synthesis/audio unavailable.\r\n",u"Charon: głos zdechł, synteza i dźwięk nie działają, kurwa.\r\n",u"Charon: my voice is fucking dead, synthesis and audio unavailable.\r\n");
        while(provider.Shutdown()!=EFI_SUCCESS) {
            SayStyled(u"Charon: audio stop failed; handoff blocked.\r\n",u"Charon: dźwięk się, kurwa, nie zatrzymał; start zablokowany.\r\n",u"Charon: the fucking audio won’t stop; handoff blocked.\r\n");
            table->BootServices->Stall(100000);
        }
    }
    hakefi::MenuVoicePublication publication;
    if(!hakefi::residentDriver && provider.Voice()) {
        const auto published=publication.Install(table->BootServices,provider.Voice());
        if(published!=EFI_SUCCESS) {
            SayStyled(u"Charon: speech service publication failed; owned Shell speech unavailable.\r\n",u"Charon: usługa mowy się wyjebała; powłoka będzie niema jak ryba.\r\n",u"Charon: the speech service shat itself; the Shell stays mute.\r\n");
            // A failed installation may still have exposed this interface.
            // Retain the provider until those exact callbacks are removed.
            while(publication.StopAndUninstall()!=EFI_SUCCESS) {
                SayStyled(u"Charon: speech callbacks retained; handoff blocked.\r\n",u"Charon: wywołania mowy dalej wiszą, kurwa; start zablokowany.\r\n",u"Charon: speech callbacks still hanging, damn it; handoff blocked.\r\n");
                table->BootServices->Stall(100000);
            }
        }
    }
#endif
    bool returnToFirmware=false;
    while(!returnToFirmware) {
#ifdef HAK_CHARON_LIVE_VOICE
        status=hakefi::residentDriver
            ?hakefi::RunBootMenu(self,root,current,returnToFirmware,hakefi::residentDriver->effects,hakefi::residentDriver->voice)
            :hakefi::RunBootMenu(self,root,current,returnToFirmware,provider.Effects(),provider.Voice());
#else
        status=hakefi::RunBootMenu(self,root,current,returnToFirmware);
#endif
        if(!returnToFirmware) {
            SayStyled(u"HAK EFI: menu input unavailable; handoff blocked, retrying.\r\n",u"HAK EFI: klawiatura zdechła; start zablokowany, próbuję znowu, ja pierdolę.\r\n",u"HAK EFI: keyboard is dead; handoff blocked, retrying, for fuck’s sake.\r\n");
            table->BootServices->Stall(1000000);
        }
    }
    consoleExplicit=(hakefi::residentPolish&2)!=0;consolePolish=(hakefi::residentPolish&1)!=0;
    while(hakefi::CleanupCharonShell()!=EFI_SUCCESS) {
        SayStyled(u"Charon: Shell resources or audio retained; handoff blocked.\r\n",u"Charon: powłoka albo dźwięk dalej trzymają zasoby, kurwa; start zablokowany.\r\n",u"Charon: the Shell or audio is still hogging shit; handoff blocked.\r\n");
        table->BootServices->Stall(100000);
    }
#ifdef HAK_CHARON_LIVE_VOICE
    while(publication.StopAndUninstall()!=EFI_SUCCESS) {
        SayStyled(u"Charon: speech service still published; handoff blocked.\r\n",u"Charon: usługa mowy dalej wisi; start zablokowany, ja jebię.\r\n",u"Charon: speech service still hanging around; handoff blocked, bloody hell.\r\n");
        table->BootServices->Stall(100000);
    }
    while(!hakefi::residentDriver && provider.Shutdown()!=EFI_SUCCESS) {
        SayStyled(u"Charon: audio stop failed; handoff blocked.\r\n",u"Charon: dźwięk się, kurwa, nie zatrzymał; start zablokowany.\r\n",u"Charon: the fucking audio won’t stop; handoff blocked.\r\n");
        table->BootServices->Stall(100000);
    }
#endif
    // The resident reader keeps reading whatever firmware boots next.
    if(hakefi::residentDriver)hakefi::residentDriver->Resume(hakefi::residentDriver,hakefi::residentLanguage,hakefi::residentPolish,
        hakefi::residentVerbosity,hakefi::residentProgress,hakefi::residentVoice,hakefi::residentReading&~(uint32_t(7)<<20));   // r393: firmware boot order = quick boot
    root->Close(root);
    // Returning lets the firmware continue its configured BootOrder. No
    // inference of a default OS. BootOrder changes require an explicit,
    // snapshot-matching settings request; another loader is never overwritten.
    SayStyled(u"HAK EFI: returning to firmware boot order.\r\n",u"HAK EFI: wracam do kolejności firmware, dalej radź sobie sam, łamago.\r\n",u"HAK EFI: back to the firmware boot order, you’re on your own, numpty.\r\n");
    return status==EFI_SUCCESS ? EFI_NOT_FOUND : status;
}
#endif
