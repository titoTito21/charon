#include "HiiMenu.h"
#include "ConsoleMenu.h"
#include "CharonMessageMode.h"
#include "BootMessages.h"
#include "hii/HiiSettingsView.h"
namespace hakefi {
namespace {
void Text(char16_t* out,size_t cap,const char16_t* text) {
    if(!cap)return;
    size_t i=0;while(i+1<cap && text[i]) {out[i]=text[i];++i;}out[i]=0;
}
void Utf(const char16_t* in,char* out,size_t cap) {
    size_t at=0;
    while(*in && at+4<cap) {
        uint32_t c=*in++;
        if(c>=0xd800 && c<=0xdbff && *in>=0xdc00 && *in<=0xdfff)c=0x10000+((c-0xd800)<<10)+(*in++-0xdc00);
        if(c<128)out[at++]=char(c);
        else if(c<2048) {out[at++]=char(0xc0|(c>>6));out[at++]=char(0x80|(c&63));}
        else if(c<65536) {out[at++]=char(0xe0|(c>>12));out[at++]=char(0x80|((c>>6)&63));out[at++]=char(0x80|(c&63));}
        else {out[at++]=char(0xf0|(c>>18));out[at++]=char(0x80|((c>>12)&63));out[at++]=char(0x80|((c>>6)&63));out[at++]=char(0x80|(c&63));}
    }
    out[at]=0;
}
}
efi_status RunHiiMenu(efi_system_table* system,MenuVoice* voice,AudioEffects* effects,bool pl,bool sounds,bool explicitMessages) {
    using namespace charonhii;
    if(!system || !system->BootServices || !system->ConIn)return EFI_INVALID_PARAMETER;
    auto* boot=system->BootServices;auto* input=system->ConIn;
    Database database;Config config;SettingsView view;
    efi_event timer=nullptr;bool audioFailed=!ValidMenuVoice(voice);
    auto cue=[&](MenuCue c) {if(sounds && effects && effects->QueueCue)effects->QueueCue(effects->context,c);};
    auto pump=[&]() {if(!audioFailed && voice->Pump(voice,480)!=EFI_SUCCESS)audioFailed=true;};
    auto speak=[&](const char16_t* text) {
        if(audioFailed)return;
        char16_t styled[1024];char bytes[4096];
        FormatCharonMessage(text,styled,1024,explicitMessages,pl);Utf(styled,bytes,sizeof(bytes));
        if(voice->SpeakUtf8(voice,pl?"pl":"en",bytes)!=EFI_SUCCESS)audioFailed=true;
    };
    if(!audioFailed) {
        if(boot->CreateEvent(EVT_TIMER,TPL_APPLICATION,nullptr,nullptr,&timer)!=EFI_SUCCESS
            || boot->SetTimer(timer,TimerPeriodic,100000)!=EFI_SUCCESS)audioFailed=true;
    }
    char16_t statusText[512]={},notice[1024]={};bool popup=false,render=true,exit=false,packages=true,staleNotified=false;
    size_t package=0;efi_status status=database.Open(boot);pump();
    auto error=[&](const char16_t* text,efi_status code) {
        Text(notice,1024,text);AppendBootStatus(notice,1024,code);popup=true;render=true;speak(notice);
    };
    const char* languages[]={pl?"pl-PL":"en-US",pl?"pl":"en","en-US","en"};
    // Formset titles for the package list. Read failures keep the numbered name.
    constexpr size_t TitleSlots=64,TitleUnits=96;static char16_t titles[TitleSlots][TitleUnits];
    auto loadTitles=[&]() {
        for(auto& title:titles)title[0]=0;
        for(size_t i=0;i<database.Count() && i<TitleSlots;++i) {
            pump();if(database.Read(i,languages,4)!=EFI_SUCCESS)continue;
            const auto* model=database.View(database.Current());if(!model || !model->setCount)continue;
            uint16_t raw[512];const auto units=model->CopyString(model->sets[0].title,raw,512);
            if(!units || units>512)continue;
            size_t at=0;for(size_t k=0;k+1<units && at+1<TitleUnits;++k)if(raw[k]>=32)titles[i][at++]=char16_t(raw[k]);
            while(at && titles[i][at-1]==u' ') {--at;}
            titles[i][at]=0;
        }
    };
    auto refresh=[&]() {
        config.Reset();packages=true;package=0;staleNotified=false;pump();
        status=database.Refresh();pump();render=true;
        if(status==EFI_SUCCESS)loadTitles();
        if(status!=EFI_SUCCESS)error(pl?u"Nie można odświeżyć ustawień UEFI.":u"Cannot refresh UEFI settings.",status);
    };
    if(status==EFI_SUCCESS)refresh();
    else error(pl?u"Baza ustawień HII jest niedostępna.":u"HII settings database unavailable.",status);
    bool presented=false,wasPopup=false;ViewPage presentedPage=ViewPage::Sets;bool presentedPackages=true;
    bool numericTyped=false;
    while(!exit) {
        if(!packages && !popup && !staleNotified && !database.View(database.Current())) {
            // View owns the captured identity; asking Count also retires proposals.
            view.Count();staleNotified=true;error(pl?u"Ustawienia zmieniły się. Naciśnij F5, aby odświeżyć.":u"Settings changed. Press F5 to refresh.",EFI_MEDIA_CHANGED);
        }
        if(render) {
            auto label=[&](size_t row,char16_t* out,size_t cap) {
                if(popup)Text(out,cap,u"OK");
                else if(packages) {
                    if(database.Count() && row<TitleSlots && titles[row][0])Text(out,cap,titles[row]);
                    else {
                        Text(out,cap,database.Count()?(pl?u"Pakiet ustawień firmware ":u"Firmware settings package "):(pl?u"Brak pakietów ustawień HII":u"No HII settings packages"));
                        if(database.Count())ConsoleNumber(out,cap,row+1);
                    }
                } else if(!view.RowLabel(row,out,cap))Text(out,cap,pl?u"Etykieta niedostępna":u"Label unavailable");
            };
            char16_t focus[1024]={},help[512]={},title[256]={},keys[512]={};
            if(!popup) {
                if(packages)label(package,focus,1024);else {view.FocusAnnouncement(focus,1024);view.Help(help,512);}
                speak(focus);
            }
            FormatCharonMessage(pl?u"Ustawienia UEFI":u"UEFI settings",title,256,explicitMessages,pl);
            FormatCharonMessage(pl?u"Strzałki: wybór / edycja; Enter / Spacja: otwórz; Esc: wstecz; F1: pomoc; F5: odśwież":u"Arrows: select / edit; Enter / Space: open; Esc: back; F1: help; F5: refresh",keys,512,explicitMessages,pl);
            const auto page=view.Page();
            if(DrawConsoleMenu(system->ConOut,title,popup?1:packages?database.Count():view.Count(),popup?0:packages?package:view.Selected(),label,
                (popup?statusText:focus),audioFailed?(pl?u"Mowa niedostępna; tylko konsola.":u"Speech unavailable; console only."):help,keys,popup?notice:nullptr)) {
                if(presented) {
                    if(wasPopup && !popup)cue(MenuCue::popupClose);
                    if(!wasPopup && popup)cue(MenuCue::popupOpen);
                    if(!popup && !wasPopup && (page!=presentedPage || packages!=presentedPackages)) {
                        if(presentedPage==ViewPage::Confirm)cue(MenuCue::dialogClose);
                        if(page==ViewPage::Confirm)cue(MenuCue::dialogOpen);else cue(MenuCue::submenuOpen);
                    }
                } else cue(MenuCue::submenuOpen);
                presented=true;wasPopup=popup;presentedPage=page;presentedPackages=packages;
            } else {status=EFI_DEVICE_ERROR;exit=true;continue;}
            render=false;
        }
        efi_event events[]={input->WaitForKey,timer};size_t selected=0;
        status=WaitMenuVoiceEvent(boot,audioFailed?nullptr:voice,events,timer?2:1,1,selected);
        if(status!=EFI_SUCCESS) {exit=true;continue;}
        if(selected==1) {pump();continue;}
        efi_input_key key{};if(input->ReadKeyStroke(input,&key)!=EFI_SUCCESS)continue;
        ++ExplicitVariety();
        const bool back=key.ScanCode==23 || (key.UnicodeChar==8 && (packages || view.Page()!=ViewPage::String)) || key.ScanCode==4;
        if(popup) {
            if(back || key.UnicodeChar==13 || key.UnicodeChar==32) {popup=false;render=true;}
            continue;
        }
        if(key.ScanCode==15) {refresh();continue;} // F5, never an implicit retry of a write.
        if(key.ScanCode==11) {
            char16_t help[512]={};if(!packages)view.Help(help,512);
            if(!help[0])Text(help,512,pl?u"Brak pomocy dla tej pozycji.":u"No help for this item.");
            Text(notice,1024,help);popup=true;render=true;speak(notice);continue;
        }
        if(packages) {
            if(back) {exit=true;continue;}
            if(key.ScanCode==1 || key.ScanCode==2) {
                const size_t old=package;
                if(key.ScanCode==1 && package)--package;
                if(key.ScanCode==2 && package+1<database.Count())++package;
                cue(old==package?MenuCue::edge:MenuCue::focus);render=true;
            } else if((key.UnicodeChar==13 || key.ScanCode==3) && database.Count()) {
                pump();status=database.Read(package,languages,4);pump();
                if(status==EFI_SUCCESS && view.Bind(boot,database,config,database.Current(),pl))packages=false;
                else error(pl?u"Nie można otworzyć tego pakietu ustawień.":u"Cannot open this settings package.",status);
                render=true;
            }
            continue;
        }
        const auto before=view.Page();ViewResult result=ViewResult::None;
        if(before==ViewPage::Number && !key.ScanCode && key.UnicodeChar>=u'0' && key.UnicodeChar<=u'9') {
            char16_t text[32]={};uint64_t number=0;
            if(numericTyped) {view.RowLabel(0,text,32);for(size_t i=0;text[i];++i)number=number*10+uint64_t(text[i]-u'0');}
            const auto digit=uint64_t(key.UnicodeChar-u'0');
            if(number>(UINT64_MAX-digit)/10)result=ViewResult::Edge;
            else {result=view.SetEditNumber(number*10+digit)?ViewResult::Focus:ViewResult::Stale;numericTyped=true;}
        } else if(before==ViewPage::String && key.UnicodeChar>=32 && !key.ScanCode) {
            char16_t text[256]={};view.RowLabel(0,text,256);size_t n=0;while(n<255 && text[n])++n;
            if(n<255) {text[n++]=key.UnicodeChar;text[n]=0;result=view.SetEditText(text,n)?ViewResult::Focus:ViewResult::Unavailable;}
            else result=ViewResult::Edge;
        } else {
            ViewKey translated=ViewKey::Enter;bool known=true;
            if(key.ScanCode==1)translated=ViewKey::Up;else if(key.ScanCode==2)translated=ViewKey::Down;
            else if(key.ScanCode==3)translated=ViewKey::Right;else if(key.ScanCode==4)translated=ViewKey::Left;
            else if(key.ScanCode==23)translated=ViewKey::Back;
            else if(key.UnicodeChar==8)translated=before==ViewPage::String?ViewKey::Erase:ViewKey::Back;
            else if(key.UnicodeChar==32)translated=ViewKey::Space;else if(key.UnicodeChar!=13)known=false;
            if(known) {pump();result=view.Key(translated);pump();}
            if(result==ViewResult::Closed && before==ViewPage::Sets)packages=true;
        }
        if(before!=view.Page())numericTyped=false;
        if(result==ViewResult::ProposalReady) {
            const auto* proposal=view.ConfirmedProposal();ConfigCommitResult committed;
            pump();status=proposal?config.Commit(*proposal,true,committed):EFI_MEDIA_CHANGED;pump();
            // Consume this confirmation before rendering a result. Enter on OK
            // cannot route the same proposal again, including partial failures.
            view.Key(ViewKey::Back);
            if(status==EFI_SUCCESS && committed.applied) {
                Text(notice,1024,committed.resetRequired?(pl?u"Zmiana zapisana. Wymagany restart.":u"Change saved. Restart required."):(pl?u"Zmiana zapisana i odczytana ponownie.":u"Change saved and read back."));
                popup=true;speak(notice);
            } else error(committed.changed?(pl?u"Firmware częściowo zmieniło ustawienie. Sprawdź bieżącą wartość.":u"Firmware partially changed the setting. Check its current value."):
                (pl?u"Zmiana nie została potwierdzona. Sprawdź bieżącą wartość.":u"Change was not confirmed. Check its current value."),status);
            render=true;
        } else if(result==ViewResult::Unavailable)error(pl?u"Pole jest niedostępne, tylko do odczytu lub wartość zmieniła się.":u"Field unavailable, read only, or its value changed.",view.ValueStatus());
        else if(result==ViewResult::Stale) {staleNotified=true;error(pl?u"Widok nieaktualny. Naciśnij F5.":u"View stale. Press F5.",EFI_MEDIA_CHANGED);}
        else if(result!=ViewResult::None) {cue(result==ViewResult::Edge?MenuCue::edge:MenuCue::focus);render=true;}
    }
    config.Reset();
    // A firmware callback still referring to this stack object forbids return.
    bool closeNotified=false;
    while(database.NeedsClose()) {
        const auto closed=database.Close();pump();if(closed==EFI_SUCCESS)break;
        Text(notice,1024,pl?u"Nie można zamknąć obserwacji HII. Powrót zablokowany; trwa ponawianie zamknięcia.":u"Cannot unregister HII observation. Return blocked; retrying cleanup.");
        AppendBootStatus(notice,1024,closed);if(!closeNotified)speak(notice);closeNotified=true;
        if(system->ConOut)system->ConOut->OutputString(system->ConOut,notice);
        boot->Stall(10000);
    }
    if(timer)boot->CloseEvent(timer);
    cue(MenuCue::submenuClose);return status;
}
}
