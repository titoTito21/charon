// Charon resident speech layer (r355). GPL-2.0-or-later.
//
// While a UEFI application started by Charon runs (a loader such as GRUB,
// Windows Boot Manager, rEFInd, Haiku's loader, a firmware tool), Charon's
// image stays loaded below it in StartImage. This layer:
//  - replaces EFI_SYSTEM_TABLE.ConOut with a forwarding proxy and keeps a
//    shadow copy of the text screen (characters, attributes, cursor);
//  - after output pauses, speaks the highlighted line (menus) or the rows that
//    changed (messages, prompts), newer text interrupting older speech;
//  - keeps audio running from a TPL_CALLBACK timer (the child owns the main
//    loop, so this is the only place synthesis can advance);
//  - intercepts ExitBootServices: says that the system takes over, stops all
//    audio DMA, removes every hook and only then calls the firmware service.
// Purely graphical applications (GOP only, no ConOut text) get no speech; the
// background melody continues as an audible "still loading" signal.
// The layer never writes variables, never changes what is booted and never
// allocates inside the ExitBootServices path (the caller's memory map key
// must remain valid; if firmware still reports a change, standard loaders
// retry with the restored service).
#pragma once
#include "BootMenuVoice.h"
#include "CharonCues.h"
#include "CharonReaderControl.h"   // r407: graphicsOwnerGuid
#include "CharonTrace.h"
#include "CharonGraphicsTypes.h"
#include "CharonOcr.h"
#include "CharonAaOcr.h"
#include "CharonBootManagers.h"
#include "CharonKeyNotify.h"
#include "CharonPoolEFI.h"
#include "CharonTtfOcr.h"
#include "CharonPng.h"
#include "CharonBmFont.h"
#include "CharonBmOcr.h"
#include "CharonMessageMode.h"
#include "CharonSystemTools.h"
#include "CharonSpeechText.h"
#include "CharSettings.h"
#include "CharonUsbKeypad.h"
#include "CharonKeyHelp.h"
#include "CharonBraille.h"
#include "CharonFormatting.h"
#include "CharonProgress.h"
#include "CharonManagerProbe.h"
#include "CharonSecretInput.h"   // r407: VeraCrypt password entry
#include <efi/protocol/simple-file-system.h>
#include <efi/protocol/file.h>
#include <efi/protocol/loaded-image.h>
#include "sdk/hak_boot_accessibility.h"   // r396 public protocol for boot loaders
namespace hakefi {
inline void Utf8(const char16_t* text,char* out,size_t capacity); // BootMenu.h
struct ResidentOptions {
    efi_handle selfImage=nullptr;   // r396: Charon's image (its disk holds \EFI\HAK\profiles)
    MenuVoice* voice=nullptr;
    AudioEffects* effects=nullptr;
    char language[3]={'e','n',0};
    bool polish=false;
    uint8_t verbosity=1; // 1 short, 2 detailed. 0 never installs the layer.
    uint8_t progress=1;  // 0 off, 1 beeps, 2 speech every 10%, 3 both.
    bool explicitMessages=false; // r373: the reader's own phrases swear too (screen text never changes)
    // r382: the reader's own voice (tenths; 0 keeps the voice as it is).
    uint8_t rate=0,pitch=0,volume=0,effectsVolume=10;
    bool positions=true; // r382 Detail: "2 of 5" after a highlighted menu entry
    uint8_t timers=0;    // r384 countdowns: bit0 speech every 10 s, bit1 tick every second + 500 ms end tone
    bool messageVoice=true;  // r389: screen text outside the selection on the boot-message voice
    // r390 reading (settings V9 bytes 36-38): NVDA symbol level 0-3, capitals
    // (bit0 say "cap", bit1 beep, bit2 higher pitch), screen text language
    // (speechtext::ScreenLanguage).
    uint8_t punctuation=speechtext::levelSome,capitals=1,screenLanguage=speechtext::screenAuto;
    uint8_t readerFlags=47;   // r390 CharSettings readerFlag*: messages, typed characters, automatic reading, cursor echo, typed words
    // r393 (settings V10 bytes 40-41): text formatting (formatting::flag*),
    // braille attribute dots, boot-manager handling (bits 0-1 hidden menu: 0 off,
    // 1 announce and wait 2 s, 2 always reveal; bit 2 stop countdowns).
    uint8_t formatFlags=formatting::flagsDefault;bool brailleAttributes=true,dictionary=true;uint8_t managerFlags=1;bool linger=true;bool reviewOnly=false;   // r401 reviewOnly: Charon itself speaks (its installer dialogue); the reader only serves the review keys. r400: loading cue left playing after ExitBootServices
    uint8_t stereo=0;   // r397: 0 off, 1 sound icons placed by screen column, 2 speech and icons
};
class ResidentReader {
public:
    static constexpr size_t maxColumns=160,maxRows=64;
    efi_status Install(efi_system_table* table,const ResidentOptions& options) {
        if(installed_)return EFI_ALREADY_STARTED;
        if(!table || !table->ConOut || !table->BootServices || !ValidMenuVoice(options.voice) || !options.verbosity)return EFI_INVALID_PARAMETER;
        auto* boot=table->BootServices;
        if(!boot->CalculateCrc32 || !boot->CreateEvent || !boot->SetTimer || !boot->RaiseTPL)return EFI_UNSUPPORTED;
        table_=table;boot_=boot;options_=options;real_=table->ConOut;bootManager_=nullptr;currentImage_=nullptr;currentBase_=nullptr;
        charon::speechReserve.Start(boot);   // r397
        marker_=charonImageMarker;  // keeps the marker in this image (see CharonBootManagers.h)
        stopped_=false;handedOff_=false;failed_=false;suspended_=false;tick_=lastOutput_=0;lastBlt_=0;pollWidth_=pollHeight_=0;geoWidth_=geoModeWidth_=geoModeHeight_=0;geoBase_=geoRetry_=0;geoPages_=1;geoPage_=0;sampleDue_=false;dispi_=nullptr;dispiSearched_=false;sampleOffset_=0;dispiActive_=inFont_=false;aaRead_=false;
        ResetShadow();QueryDimensions();ApplyVoice();ApplyFlags();
        // r401: the reader is installed more than once in a session (beside Charon, then for a program): no key or countdown state is carried over.
        keyPending_=false;keyTick_=0;keyScan_=0;keyChar_=0;command_=0;countdownSeen_=0;quietTick_=0;heldMenu_[0]=0;heldInfo_[0]=0;serviceDue_=0;servicing_=false;
        auto status=boot->CreateEvent(EVT_TIMER|EVT_NOTIFY_SIGNAL,TPL_CALLBACK,TimerEntry,this,&timer_);
        if(status!=EFI_SUCCESS) {timer_=nullptr;return status;}
        status=boot->CreateEvent(EVT_SIGNAL_EXIT_BOOT_SERVICES,TPL_CALLBACK,ExitNotify,this,&exitEvent_);
        if(status!=EFI_SUCCESS) {exitEvent_=nullptr;boot->CloseEvent(timer_);timer_=nullptr;return status;}
        // r356: hook the console protocol instance in place instead of
        // replacing EFI_SYSTEM_TABLE.ConOut. Firmware boot managers re-check
        // that pointer against the console handle (EDK2 BDS replaces any
        // foreign pointer), so a swapped pointer would be silently dropped.
        // Every caller keeps the same interface and reaches the hooks.
        // A wrapper another program left on top of our proxies at the last
        // Uninstall still calls them: it stays on top and the originals below
        // are kept (hooking over it again would make the two call each other).
        const efi_simple_text_output_protocol below=saved_;const bool again=leftAt_==real_ && leftMask_;
        saved_=*real_;
        attribute_=real_->Mode?size_t(real_->Mode->Attribute):7;baseAttribute_=attribute_;
        if(real_->Mode && real_->Mode->CursorColumn>=0 && real_->Mode->CursorRow>=0) {
            column_=size_t(real_->Mode->CursorColumn);row_=size_t(real_->Mode->CursorRow);
            // A console larger than the shadow (more than 64 rows): never outside it.
            if(column_>=columns_)column_=0;
            if(row_>=rows_)row_=rows_-1;
        }
        originalExit_=boot->ExitBootServices;
        active_=this;
        const efi_tpl old=boot->RaiseTPL(TPL_NOTIFY);
        if(again && (leftMask_&1) && real_->Reset==left_.Reset)saved_.Reset=below.Reset;else real_->Reset=ProxyReset;
        if(again && (leftMask_&2) && real_->OutputString==left_.OutputString)saved_.OutputString=below.OutputString;else real_->OutputString=ProxyOutput;
        if(again && (leftMask_&4) && real_->SetMode==left_.SetMode)saved_.SetMode=below.SetMode;else real_->SetMode=ProxySetMode;
        if(again && (leftMask_&8) && real_->SetAttribute==left_.SetAttribute)saved_.SetAttribute=below.SetAttribute;else real_->SetAttribute=ProxyAttribute;
        if(again && (leftMask_&16) && real_->ClearScreen==left_.ClearScreen)saved_.ClearScreen=below.ClearScreen;else real_->ClearScreen=ProxyClear;
        if(again && (leftMask_&32) && real_->SetCursorPosition==left_.SetCursorPosition)saved_.SetCursorPosition=below.SetCursorPosition;else real_->SetCursorPosition=ProxyCursor;
        boot->ExitBootServices=HookExit;
        // r400 (user): a shutdown or restart asked for while firmware services
        // still run (a boot manager's "halt", firmware setup's exit) is told.
        if(table->RuntimeServices && table->RuntimeServices->ResetSystem && table->RuntimeServices->ResetSystem!=HookReset) {
            originalReset_=table->RuntimeServices->ResetSystem;table->RuntimeServices->ResetSystem=HookReset;RecrcRuntime();
        }
        // r361: images started later (other boot managers) are recognized.
        originalStart_=boot->StartImage;boot->StartImage=HookStart;
        // r390: Alt+\ pauses the boot: a program waiting (Stall) or printing waits too.
        originalStall_=boot->Stall;boot->Stall=HookStall;
        // r396e: a synthesizer chosen in the voice ring loads where programs wait (TPL_APPLICATION).
        originalWait_=boot->WaitForEvent;boot->WaitForEvent=HookWait;
        Recrc();
        boot->RestoreTPL(old);
        installed_=true;
        // r357: graphical programs. Text drawn through the HII font service
        // is read like console text; GOP fills reveal progress bars and
        // screens without readable text. Instances installed later (consoles
        // connected after Driver####) are hooked when they appear.
        HookGraphics();
        if(boot->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,GraphicsNotify,this,&graphicsNotify_)==EFI_SUCCESS && boot->RegisterProtocolNotify) {
            boot->RegisterProtocolNotify(&gopGuid,graphicsNotify_,&gopRegistration_);
            boot->RegisterProtocolNotify(&hiiFontGuid,graphicsNotify_,&fontRegistration_);
        }
        // r363: every new loaded image, also those a chain loader starts with
        // its own PE loader instead of StartImage (Debian's GRUB).
        observedCount_=0;
        if(boot->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,ImageNotify,this,&imageNotify_)==EFI_SUCCESS && boot->RegisterProtocolNotify) {
            efi_guid loadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
            boot->RegisterProtocolNotify(&loadedGuid,imageNotify_,&imageRegistration_);
        }
        // r369: reader shortcuts (Ctrl+Alt+R/S/H) in every program.
        command_=0;
#ifndef BISECT_NO_NOTIFY
        keys_.Register(table_,KeyNotify);
#endif
        keypad_.Attach(KeypadPress,this);keypad_.Scan(boot_);   // r390 numpad review (USB)
        braille_.Find(boot_);                                    // r390 braille display (Baum, USB)
#ifndef BISECT_NO_HOOK
        HookInput();
#endif
        status=boot->SetTimer(timer_,TimerPeriodic,100000);
        if(status!=EFI_SUCCESS) {Uninstall();return status;}
        // r402: a program that stays at TPL_CALLBACK (iPXE and so netboot.xyz) starves the timer above:
        // the reader was deaf and mute in its menus. A second timer one level higher takes over while
        // the first has not ticked for half a second, and only then.
        rescueSeen_=tick_;rescueIdle_=0;rescuing_=false;
        if(boot->CreateEvent(EVT_TIMER|EVT_NOTIFY_SIGNAL,TPL_NOTIFY,RescueNotify,this,&rescue_)==EFI_SUCCESS && rescue_) {
            if(boot->SetTimer(rescue_,TimerPeriodic,200000)!=EFI_SUCCESS) {boot->CloseEvent(rescue_);rescue_=nullptr;}
        } else rescue_=nullptr;
        InstallAccessibility();   // r396
        return EFI_SUCCESS;
    }
    // After StartImage returned. Restores only the hooks that are still ours.
    efi_status Uninstall() {
        if(!installed_)return EFI_SUCCESS;
        if(timer_)boot_->SetTimer(timer_,TimerCancel,0);
        if(rescue_) {boot_->SetTimer(rescue_,TimerCancel,0);boot_->CloseEvent(rescue_);rescue_=nullptr;}
        RemoveHooks();
        if(timer_) {boot_->CloseEvent(timer_);timer_=nullptr;}
        if(exitEvent_) {boot_->CloseEvent(exitEvent_);exitEvent_=nullptr;}
        if(graphicsNotify_) {boot_->CloseEvent(graphicsNotify_);graphicsNotify_=nullptr;}
        if(imageNotify_) {boot_->CloseEvent(imageNotify_);imageNotify_=nullptr;}
        keypad_.Unhook(boot_);   // r390: the image may be unloaded after this
        UninstallAccessibility();   // r396
        // active_ stays: a hook another program wrapped (and so could not be
        // restored) still reaches this static object and forwards to the
        // saved original (every hook checks installed_).
        installed_=false;
        return foreignConOut_?EFI_ACCESS_DENIED:EFI_SUCCESS;
    }
    // Resident driver mode (r356): the Charon menu speaks for itself, so the
    // reader pauses (no capture, no pumping: the menu owns the voice) and
    // resumes, with the menu's current language and levels, before Charon
    // starts another program or returns to firmware.
    // The ordinary timer's entry: while it works, the rescue timer (one priority
    // level higher) must not come in on top of it. On a slow machine one round of
    // screen recognition takes longer than the rescue timer's patience, and the
    // first r402 regression showed menus read twice and as loose messages.
    static void EFIAPI TimerEntry(efi_event e,void* context) {
        auto& self=*static_cast<ResidentReader*>(context);
        self.working_=true;TimerNotify(e,context);self.working_=false;
    }
    static void EFIAPI RescueNotify(efi_event,void* context) {
        auto& self=*static_cast<ResidentReader*>(context);
        if(self.stopped_ || self.suspended_ || !self.installed_ || self.options_.reviewOnly || self.rescuing_)return;
        if(self.working_ || self.servicing_ || GuardDepth()>0) {self.rescueIdle_=0;return;}
        if(self.tick_!=self.rescueSeen_) {self.rescueSeen_=self.tick_;self.rescueIdle_=0;return;}
        if(++self.rescueIdle_<25)return;   // half a second without a tick
        if(self.rescueIdle_==25)charontrace::Value("reader rescue timer",self.tick_);
        self.rescuing_=true;++self.tick_;TimerNotify(nullptr,&self);self.rescueSeen_=self.tick_;self.rescuing_=false;
    }
    // r401: the timer's work for Charon's own screens, run from the key hooks.
    void Service() {
        if(!options_.reviewOnly || !serviceDue_ || servicing_ || stopped_ || suspended_)return;
        Guard guard(boot_);
        const unsigned due=serviceDue_;serviceDue_=0;
        if(due>1)tick_+=due-1;
        servicing_=true;TimerNotify(nullptr,this);servicing_=false;
    }
    efi_status Suspend() {
        if(!installed_ || stopped_)return EFI_NOT_STARTED;
        suspended_=true;return boot_->SetTimer(timer_,TimerCancel,0);
    }
    efi_status Resume(const ResidentOptions* update) {
        if(!installed_ || stopped_)return EFI_NOT_STARTED;
        if(update) {
            options_.language[0]=update->language[0];options_.language[1]=update->language[1];
            options_.polish=update->polish;options_.progress=update->progress;options_.explicitMessages=update->explicitMessages;options_.positions=update->positions;options_.timers=update->timers;
            options_.punctuation=update->punctuation;options_.capitals=update->capitals;options_.screenLanguage=update->screenLanguage;
            options_.readerFlags=update->readerFlags;options_.reviewOnly=update->reviewOnly;serviceDue_=0;ApplyFlags();
            options_.formatFlags=update->formatFlags;options_.stereo=update->stereo;options_.brailleAttributes=update->brailleAttributes;options_.dictionary=update->dictionary;options_.managerFlags=update->managerFlags;options_.linger=update->linger;   // r393
            if(update->verbosity)options_.verbosity=update->verbosity;
            if(update->rate) {options_.rate=update->rate;options_.pitch=update->pitch;options_.volume=update->volume;options_.effectsVolume=update->effectsVolume;}
        }
        ApplyVoice();
        {Guard guard(boot_);QueryDimensions();ClearShadow();}
        command_=0;  // keys typed in Charon's own menu are not reader commands
        suspended_=false;return boot_->SetTimer(timer_,TimerPeriodic,100000);
    }
    // r390: settings that are reader state.
    void ApplyFlags() {
        options_.messageVoice=(options_.readerFlags&readerFlagMessages)!=0;
        quiet_=(options_.readerFlags&readerFlagAutoRead)==0 || options_.reviewOnly;
    }
    // r382: the reader speaks with its own rate, pitch and volume.
    void ApplyVoice() {
        if(!options_.rate)return;
        rate_=options_.rate;pitch_=options_.pitch?options_.pitch:10;volume_=options_.volume?options_.volume:10;appliedPitch_=pitch_;
        ApplyVoiceProfile(options_.effects,options_.rate,options_.pitch?options_.pitch:10,options_.volume?options_.volume:10,options_.effectsVolume);
    }
    bool Installed() const {return installed_;}
    bool HandedOff() const {return handedOff_;}
    bool SpeechFailed() const {return failed_;}
    // Speak text of the reader itself (startup notice of the driver).
    void Announce(const char16_t* text) {Guard guard(boot_);SayOwn(text);}
private:
    static inline ResidentReader* active_=nullptr;
    static ResidentReader& Self(efi_simple_text_output_protocol*) {return *active_;}
    void Recrc() {
        boot_->Hdr.CRC32=0;uint32_t crc=0;
        if(boot_->CalculateCrc32(boot_,boot_->Hdr.HeaderSize,&crc)==EFI_SUCCESS)boot_->Hdr.CRC32=crc;
    }
    void RemoveHooks() {
        if(hooksRemoved_)return;
        keys_.Unregister();
        UnhookInput();
        const efi_tpl old=boot_->RaiseTPL(TPL_NOTIFY);
        foreignConOut_=real_->OutputString!=ProxyOutput;
        if(real_->Reset==ProxyReset)real_->Reset=saved_.Reset;
        if(real_->OutputString==ProxyOutput)real_->OutputString=saved_.OutputString;
        if(real_->SetMode==ProxySetMode)real_->SetMode=saved_.SetMode;
        if(real_->SetAttribute==ProxyAttribute)real_->SetAttribute=saved_.SetAttribute;
        if(real_->ClearScreen==ProxyClear)real_->ClearScreen=saved_.ClearScreen;
        if(real_->SetCursorPosition==ProxyCursor)real_->SetCursorPosition=saved_.SetCursorPosition;
        // What could not be restored (a wrapper on top of a proxy), for the next Install.
        leftAt_=real_;left_=*real_;
        leftMask_=uint8_t((real_->Reset!=saved_.Reset?1:0)|(real_->OutputString!=saved_.OutputString?2:0)|(real_->SetMode!=saved_.SetMode?4:0)
            |(real_->SetAttribute!=saved_.SetAttribute?8:0)|(real_->ClearScreen!=saved_.ClearScreen?16:0)|(real_->SetCursorPosition!=saved_.SetCursorPosition?32:0));
        if(boot_->ExitBootServices==HookExit)boot_->ExitBootServices=originalExit_;
        if(table_->RuntimeServices && table_->RuntimeServices->ResetSystem==HookReset) {table_->RuntimeServices->ResetSystem=originalReset_;RecrcRuntime();}
        if(boot_->StartImage==HookStart)boot_->StartImage=originalStart_;
        if(boot_->Stall==HookStall)boot_->Stall=originalStall_;
        if(boot_->WaitForEvent==HookWait)boot_->WaitForEvent=originalWait_;
        reviewPause_=false;helpOpen_=false;historyOpen_=false;inputHelp_=false;   // nothing stays paused or in input help for the next program
        UnhookReads();
        PruneGops();
        // The entries stay (HookGraphics uses them again at the next Install; a
        // copy of a hooked instance still finds its original by Mode).
        for(size_t i=0;i<gopCount_;++i)if(gops_[i].gop && gops_[i].gop->Blt==HookBlt)gops_[i].gop->Blt=gops_[i].original;
        if(font_) {
            // The originals stay for a wrapper that still calls our hooks; font_
            // is forgotten only when both were restored, so the next Install hooks again.
            bool restored=true;
            if(font_->StringToImage==HookStringToImage)font_->StringToImage=fontOriginal_;else restored=false;
            if(font_->StringIdToImage==HookStringIdToImage)font_->StringIdToImage=fontIdOriginal_;else if(fontIdOriginal_)restored=false;
            if(restored)font_=nullptr;
        }
        Recrc();
        boot_->RestoreTPL(old);
        hooksRemoved_=true;
    }
    // ---- shadow screen -------------------------------------------------
    void ResetShadow() {
        for(size_t r=0;r<maxRows;++r) {for(size_t c=0;c<maxColumns;++c) {text_[r][c]=u' ';attr_[r][c]=0;cellFont_[r][c]=0;}dirty_[r]=false;}
        secret_.Reset();
        column_=row_=0;highlight_=SIZE_MAX;anyDirty_=false;hooksRemoved_=false;foreignConOut_=false;lastPercent_=-1;menuAnnounced_=false;everKey_=false;quietMenu_=0;rereadAfterKey_=false;menuIntro_=false;
        spoken_[0]=0;
        for(size_t r=0;r<maxRows;++r)for(size_t c=0;c<maxColumns;++c)prevRaw_[r][c]=0;
    }
    void QueryDimensions() {
        size_t columns=80,rows=25;
        if(real_ && real_->Mode && real_->QueryMode
            && real_->QueryMode(real_,size_t(real_->Mode->Mode),&columns,&rows)==EFI_SUCCESS) {}
        columns_=columns>maxColumns?maxColumns:columns<1?1:columns;
        rows_=rows>maxRows?maxRows:rows<1?1:rows;
    }
    void Scroll() {
        // r389: a new line that scrolls off the top before it was read (long
        // command output) is kept and read first at the next reading.
        if(dirty_[0]) {
            char16_t line[maxColumns+1];size_t n=0;bool changed=false;
            for(size_t c=0;c<columns_;++c) {line[n++]=text_[0][c];if(text_[0][c]!=(prevRaw_[0][c]?prevRaw_[0][c]:u' '))changed=true;}
            while(n && line[n-1]==u' ')--n;line[n]=0;
            if(changed && n) {
                size_t at=0;while(scrolledOut_[at])++at;
                if(at+n+2<sizeof(scrolledOut_)/2) {if(at)scrolledOut_[at++]=u' ';for(size_t i=0;i<n;++i)scrolledOut_[at++]=line[i];scrolledOut_[at]=0;}
            }
        }
        for(size_t r=1;r<rows_;++r) {
            for(size_t c=0;c<columns_;++c) {text_[r-1][c]=text_[r][c];attr_[r-1][c]=attr_[r][c];cellFont_[r-1][c]=cellFont_[r][c];}
            dirty_[r-1]=dirty_[r];
            for(size_t c=0;c<columns_;++c)prevRaw_[r-1][c]=prevRaw_[r][c];
        }
        for(size_t c=0;c<columns_;++c) {text_[rows_-1][c]=u' ';attr_[rows_-1][c]=0;cellFont_[rows_-1][c]=0;}
        dirty_[rows_-1]=false;for(size_t c=0;c<columns_;++c)prevRaw_[rows_-1][c]=0;
        secret_.ScrollUp(rows_,columns_);   // r407
        if(highlight_!=SIZE_MAX)highlight_=highlight_?highlight_-1:SIZE_MAX;
        lastScroll_=tick_;   // r389: scrolling output = a console
    }
    void NewLine() {if(row_+1<rows_)++row_;else Scroll();}
    // Highlight = a background different from the screen's dominant one.
    // Firmware form browsers paint their background without ClearScreen, so
    // the colour at the last clear says nothing; count what is on screen.
    bool Highlighted(size_t attribute) const {return ((attribute>>4)&7)!=majorityBg_;}
    uint8_t MajorityBackground() const {
        size_t counts[8]={};
        // r397: cells nothing wrote since ClearScreen show the clear's background (vendor
        // setups clear the page in its colour and draw only their text: the bars, full of
        // text, must not outweigh the empty page).
        const uint8_t cleared=uint8_t((baseAttribute_>>4)&7);
        for(size_t r=0;r<rows_;++r)for(size_t c=0;c<columns_;++c) {if(attr_[r][c]&0x80)++counts[(attr_[r][c]>>4)&7];else if(clearedScreen_)++counts[cleared];}
        uint8_t best=0;for(uint8_t b=1;b<8;++b)if(counts[b]>counts[best])best=b;
        return best;
    }
    // r392: AMI Aptio and Insyde mark the selected item by its text colour
    // only (white on the same grey): the one row of a column of items whose
    // bright text colour no other row of that column has.
    // r397: a bar in another background from the first to the last column (title, key hints).
    bool FullWidthBar(size_t r) const {
        const size_t edge=usedColumns_<=columns_?usedColumns_:columns_;
        if(edge<2)return false;const uint8_t a=attr_[r][0];const uint8_t b=(attr_[r][edge-1]&0x80)?attr_[r][edge-1]:attr_[r][edge-2];
        return (a&0x80) && (b&0x80) && ((a>>4)&7)==((b>>4)&7) && ((a>>4)&7)!=majorityBg_;
    }
    bool ColourSelectedRow(size_t r) const {
        const size_t c=TextStart(r);if(c==SIZE_MAX || !(attr_[r][c]&0x80))return false;
        if(FullWidthBar(r))return false;   // r397: the key-hint bars of vendor setups (ThinkPad, Insyde, Phoenix)
        // Only where items are chosen: after a key, or on a setup page with a tab bar
        // (a bright title among grey lines - iPXE's banner - is no selection).
        if(!KeyRecent()) {TabBar tab;if(!FindTabBar(tab))return false;}
        if(CursorShown())return false;   // r397: a command line (UEFI Shell's yellow "Shell>" under a white "edit:") is no menu
        const int mine=attr_[r][c]&0x0f,bg=(attr_[r][c]>>4)&7;
        if(mine<8 || Highlighted(attr_[r][c]))return false;
        size_t same=0,other=0;int otherFg=-1;
        for(size_t x=0;x<rows_;++x) {
            if(x==r)continue;
            const size_t s=TextStart(x);
            if(s!=c || !(attr_[x][s]&0x80) || ((attr_[x][s]>>4)&7)!=bg)continue;
            const int f=attr_[x][s]&0x0f;
            if(f==mine)++same;else if(otherFg<0 || f==otherFg) {otherFg=f;++other;}
        }
        return !same && other>=1;
    }
    // r392: the cells of a colour-selected row in the selection's colour
    // (the item and its value, not the help panel beside them).
    size_t ColourText(size_t r,char16_t* out,size_t capacity) const {
        if(!ColourSelectedRow(r) || !capacity)return 0;
        const uint8_t fg=attr_[r][TextStart(r)]&0x0f;
        size_t at=0;bool space=true;
        for(size_t c=0;c<columns_ && at+1<capacity;++c) {
            char16_t ch=text_[r][c]?text_[r][c]:u' ';
            if(!(attr_[r][c]&0x80) || (attr_[r][c]&0x0f)!=fg || (ch>=0x2500 && ch<=0x257f))ch=u' ';
            if(ch==u' ') {if(!space)out[at++]=u' ';space=true;} else {out[at++]=ch;space=false;}
        }
        while(at && out[at-1]==u' ')--at;
        // A submenu mark in front of the item (any colour).
        bool mark=false;for(size_t c=0;c<TextStart(r);++c)if(text_[r][c]==0x25ba || text_[r][c]==0x25b6)mark=true;
        if(mark && at && at+12<capacity) {const char16_t* w=options_.polish?u", podmenu":u", submenu";out[at++]=speechtext::ownOpen;while(*w)out[at++]=*w++;out[at++]=speechtext::ownClose;}
        out[at]=0;return at;
    }
    // A selected item: text in a non-dominant background that does not span
    // (nearly) the whole row, unlike title and key-help bars.
    bool HighlightRow(size_t r) const {
        // Bars run from the first to the last column; a selected item (GRUB's
        // bar is 76 of 80 columns) leaves at least the frame cells out.
        bool text=false,left=false,right=false;size_t cells=0;
        for(size_t c=0;c<columns_;++c) {
            const uint8_t a=attr_[r][c];if(!(a&0x80) || !Highlighted(a))continue;
            ++cells;
            if(text_[r][c]!=u' ')text=true;
            if(c==0)left=true;
            if(c+2>=usedColumns_)right=true;   // r397: the drawn area's right edge (an 80-column page on a 100-column mode), or the last but one
        }
        // r390: one reverse cell is a drawn caret (the UEFI Shell editor), not a selection.
        if(cells==1)return false;
        return text && !(left && right);
    }
    // r390: the caret of full-screen editors that draw it as one reverse
    // cell and hide or park the console cursor (edk2 edit, hexedit); the
    // console cursor otherwise.
    bool DrawnCaret(size_t& row,size_t& column) const {
        // A visible console cursor is the caret; the reverse cell is then a
        // mouse pointer (edk2's editor draws one when a pointer exists).
        if(CursorShown())return false;
        size_t found=0;
        for(size_t r=0;r<rows_ && found<2;++r) {
            size_t cells=0,at=0;
            for(size_t c=0;c<columns_;++c) {const uint8_t a=attr_[r][c];if((a&0x80) && Highlighted(a)) {++cells;at=c;}}
            if(cells==1) {++found;row=r;column=at;}
        }
        return found==1;
    }
    void CaretPos(size_t& row,size_t& column) const {
        if(DrawnCaret(row,column))return;
        row=row_;column=column_;
    }
    bool CaretKnown() const {size_t r,c;return CursorShown() || DrawnCaret(r,c);}
    void EchoFromCaret() {CaretPos(echoRow_,echoCol_);}
    void Capture(const char16_t* text) {
        // r407: characters written into a secret entry (CharonSecretInput.h) are kept as '*'.
        const bool notice=secret_.active && secretinput::PublicNotice(text);
        const size_t inked=secret_.active?secretinput::Inked(text):0;
        for(;*text;++text) {
            const char16_t ch=*text;
            if(ch==u'\r') {column_=0;continue;}
            if(ch==u'\n') {NewLine();continue;}
            if(ch==u'\b') {if(column_)--column_;continue;}
            if(ch<32 || ch==127)continue;
            if(row_>=rows_)row_=rows_-1;   // never outside the shadow (rows_ is at least 1)
            if(column_>=columns_) {column_=0;NewLine();}
            bool mask=false;
            if(secret_.active) {
                if(row_!=secret_.row || column_<secret_.column)SecretLeave();
                else if(secret_.secret) {
                    mask=secret_.Masks(row_,column_,inked,notice);
                    if(!mask && !notice && ch!=u' ')SecretLeave();   // the program's own output: the entry is over
                }
            }
            text_[row_][column_]=secret_.Put(row_,column_,ch,mask);attr_[row_][column_]=uint8_t(0x80|(attribute_&0x7f));cellFont_[row_][column_]=0;
            if(!anyDirty_)firstDirty_=tick_;
            dirty_[row_]=true;anyDirty_=true;
            if(Highlighted(attribute_) && ch!=u' ')highlight_=row_;
            ++column_;
        }
        if(secret_.active && (row_!=secret_.row || column_<secret_.column))SecretLeave();   // Enter, a carriage return
        lastOutput_=tick_;lastText_=tick_;lastConsoleText_=tick_;
    }
    // ---- r407 secret typed input (VeraCrypt's password and PIM) ----------
    // See CharonSecretInput.h. The screen model holds '*' in a secret entry;
    // nothing automatic (speech, braille, history, "what changed", the
    // clipboard, recognised graphics) ever sees the real characters.
    secretinput::Mask<maxRows,maxColumns> secret_{};bool revealing_=false;
    bool SecretScreen() const {return secret_.marks!=0 || secret_.Entry();}
    // A program with secret input (VeraCrypt) showing console text: one prompt line on a black screen, waiting for the user.
    bool SecretConsole() const {
        const auto* profile=RunningProfile();
        if(!profile || !profile->secretInput)return false;
        for(size_t r=0;r<rows_;++r)for(size_t c=0;c<columns_;++c)if((attr_[r][c]&0x80) && !cellFont_[r][c] && text_[r][c]!=u' ' && text_[r][c])return true;
        return false;
    }
    // The entry row is done with: what was typed is no news. A prompt that
    // has not been read yet stays to be read.
    void SecretSync(size_t r,size_t from) {
        if(r>=rows_ || r>=maxRows)return;
        bool promptNew=false;
        for(size_t c=0;c<from && c<columns_;++c)if(text_[r][c]!=(prevRaw_[r][c]?prevRaw_[r][c]:u' '))promptNew=true;
        for(size_t c=from;c<columns_;++c)prevRaw_[r][c]=text_[r][c];
        if(!promptNew)dirty_[r]=false;
    }
    void SecretLeave() {
        const bool was=secret_.secret;const size_t r=secret_.row,from=secret_.column;
        secret_.Leave();
        if(was)SecretSync(r,from);
    }
    // A key the program reads: an entry begins at the caret when the running
    // program's typed input is secret (its profile says so).
    void SecretKey() {
        const auto* profile=RunningProfile();
        if(!profile || !profile->secretInput) {if(secret_.active)SecretLeave();return;}
        if(row_>=rows_ || row_>=maxRows || column_>maxColumns)return;
        if(secret_.active && (row_!=secret_.row || column_<secret_.column))SecretLeave();
        secret_.Key(row_,column_,text_[row_]);
    }
    // The cue of a secret entry: one neutral word for every character the
    // program accepted (it echoed one more cell) or removed, as for password
    // fields of the accessibility protocol. Notices stay readable.
    void SecretEcho() {
        const size_t r=secret_.row;
        if(r>=rows_ || !dirty_[r])return;
        const bool pl=options_.polish;
        const size_t now=secret_.Count(r);const char16_t* cue=nullptr;
        if(now>secret_.counted)cue=pl?u"gwiazdka":u"star";else if(now<secret_.counted)cue=pl?u"usunięto":u"deleted";
        secret_.counted=now;
        char16_t notice[maxColumns+1];size_t n=0;bool changed=false;
        for(size_t c=secret_.column;c<columns_;++c) {
            const char16_t ch=secret_.clear[r][c]?u' ':(text_[r][c]?text_[r][c]:u' ');
            if(ch!=u' ' && ch!=(prevRaw_[r][c]?prevRaw_[r][c]:u' '))changed=true;
            if(ch!=u' ' || n)notice[n++]=ch;
        }
        while(n && notice[n-1]==u' ')--n;notice[n]=0;
        SecretSync(r,secret_.column);
        EchoFromCaret();keyPending_=false;
        reviewRow_=row_;reviewCol_=column_<columns_?column_:columns_-1;   // review and braille follow the caret (braille shows the stars)
        if(quiet_)return;
        if(changed && n)Speak(notice);else if(cue)SayOwn(cue);
    }
    // An explicit review command reads the real characters (F5 shows them on
    // the screen as well); they are put back before anything else runs.
    void SecretSwap() {
        for(size_t r=0;r<maxRows;++r)for(size_t c=0;c<maxColumns;++c)if(secret_.clear[r][c]) {const char16_t t=text_[r][c];text_[r][c]=secret_.clear[r][c];secret_.clear[r][c]=t?t:u'*';}
    }
    static bool GrubFamily(const char16_t* name) {return name && (name[0]==u'G' || (name[0]==u'V' && name[1]==u'e' && name[2]==u'n'));}   // GRUB, Ventoy (not VeraCrypt)
    // ---- graphics (r357) -----------------------------------------------
    void HookGraphics() {
        efi_handle* handles=nullptr;size_t count=0;
        const efi_tpl old=boot_->RaiseTPL(TPL_NOTIFY);
        if(boot_->LocateHandleBuffer(ByProtocol,&gopGuid,nullptr,&count,&handles)==EFI_SUCCESS && handles) {
            for(size_t i=0;i<count;++i) {
                Gop* gop=nullptr;
                if(boot_->HandleProtocol(handles[i],&gopGuid,(void**)&gop)!=EFI_SUCCESS || !gop || !gop->Blt || gop->Blt==HookBlt)continue;
                // An instance hooked by an earlier Install keeps its entry (no duplicates
                // that fill the table after a few programs).
                size_t at=0;while(at<gopCount_ && gops_[at].gop!=gop)++at;
                if(at==gopCount_) {if(gopCount_>=sizeof(gops_)/sizeof(gops_[0]))continue;++gopCount_;}
                gops_[at]={gop,gop->Blt,table_ && handles[i]==table_->ConsoleOutHandle,gop->Mode};gop->Blt=HookBlt;
            }
            boot_->FreePool(handles);
        }
        HiiFont* font=nullptr;
        if(!font_ && boot_->LocateProtocol(&hiiFontGuid,nullptr,(void**)&font)==EFI_SUCCESS && font && font->StringToImage) {
            font_=font;fontOriginal_=font->StringToImage;fontIdOriginal_=font->StringIdToImage;
            font->StringToImage=HookStringToImage;
            if(font->StringIdToImage)font->StringIdToImage=HookStringIdToImage;
        }
        if(!strings_)boot_->LocateProtocol(&hiiStringGuid,nullptr,(void**)&strings_);
        boot_->RestoreTPL(old);
    }
    static void EFIAPI GraphicsNotify(efi_event,void* context) {
        auto& self=*static_cast<ResidentReader*>(context);
        if(!self.stopped_ && !self.hooksRemoved_)self.HookGraphics();
    }
    GopBltFn OriginalBlt(Gop* gop) const {
        for(size_t i=0;i<gopCount_;++i)if(gops_[i].gop==gop)return gops_[i].original;
        // r386: a copy of a hooked instance (same Mode), also after the
        // original instance was uninstalled and pruned. Without this its
        // every Blt failed: OpenCore's picker stayed black.
        if(gop && gop->Mode) {
            for(size_t i=0;i<gopCount_;++i)if(gops_[i].mode==gop->Mode)return gops_[i].original;
            for(const auto& old:retired_)if(old.mode && old.mode==gop->Mode)return old.original;
        }
        return nullptr;
    }
    // Nearest of the eight console colours by channel thresholds; the bit
    // layout (blue 1, green 2, red 4) is the console attribute layout.
    static uint8_t ConsoleColor(uint32_t pixel) {
        const uint8_t blue=uint8_t(pixel),green=uint8_t(pixel>>8),red=uint8_t(pixel>>16);
        const uint8_t index=uint8_t((blue>0x50?1:0)|(green>0x50?2:0)|(red>0x50?4:0));
        const bool bright=blue>0xc0 || green>0xc0 || red>0xc0;
        return uint8_t(index|(bright?8:0));
    }
    // Text drawn by the HII font service at pixel (x,y): placed on the shadow
    // grid of 8x19 cells (the UEFI system font's narrow glyph).
    void CaptureGraphic(size_t x,size_t y,const char16_t* text,const FontDisplayInfo* info) {
        captureFont_=uint16_t((2<<8)|19);   // r393: the HII system font
        if(info)CaptureCells(x,y,text,info->foreground,info->background,19);
        else CaptureCells(x,y,text,0xffffff,0,19);
        lastOutput_=tick_;
    }
    void CaptureCells(size_t x,size_t y,const char16_t* text,uint32_t foreground,uint32_t background,size_t cellHeight) {
        size_t column=x/8,row=y/cellHeight;
        if(row>=maxRows || column>=maxColumns)return;
        if(SecretScreen()) {captureFont_=0;return;}   // r407: only the console's own text counts beside a secret entry
        const uint8_t attribute=uint8_t((ConsoleColor(foreground)&0x0f)|((ConsoleColor(background)&7)<<4));
        const uint16_t font=captureFont_;captureFont_=0;
#if defined(HAK_CHARON_READER_TRACE) || defined(HAK_CHARON_POLL_DIAG)
        {char u8[200];Utf8(text,u8,sizeof(u8));charontrace::Value("reader cells",(uint64_t(row)<<32)|(uint64_t(column)<<16)|attribute);charontrace::Text(u8);}
#endif
        for(;*text && column<maxColumns;++text) {
            const char16_t ch=*text;
            if(ch==u'\n' || ch==u'\r') {++row;column=x/8;if(row>=maxRows)break;continue;}
            if(ch<32 || ch==127)continue;
            if(column>=columns_)columns_=column+1;
            if(row>=rows_)rows_=row+1;
            if(!anyDirty_)firstDirty_=tick_;
            text_[row][column]=ch;attr_[row][column]=uint8_t(0x80|attribute);cellFont_[row][column]=font;
            dirty_[row]=true;anyDirty_=true;
            if(Highlighted(attribute) && ch!=u' ')highlight_=row;
            ++column;
        }
        lastText_=tick_;
    }
    static efi_status EFIAPI HookStringToImage(HiiFont* self,uint32_t flags,const char16_t* string,const FontDisplayInfo* info,
        void** blt,size_t x,size_t y,void** rowInfo,size_t* rowCount,size_t* columnInfo) {
        auto* r=active_;
        if(!r || !r->fontOriginal_)return EFI_DEVICE_ERROR;
        r->inFont_=true;const auto status=r->fontOriginal_(self,flags,string,info,blt,x,y,rowInfo,rowCount,columnInfo);r->inFont_=false;
        if(status==EFI_SUCCESS && string && r->installed_ && !r->inConOut_ && !r->stopped_ && !r->suspended_) {Guard guard(r->boot_);r->CaptureGraphic(x,y,string,info);}
        return status;
    }
    static efi_status EFIAPI HookStringIdToImage(HiiFont* self,uint32_t flags,void* packageList,uint16_t stringId,const char* language,
        const FontDisplayInfo* info,void** blt,size_t x,size_t y,void** rowInfo,size_t* rowCount,size_t* columnInfo) {
        auto* r=active_;
        if(!r || !r->fontIdOriginal_)return EFI_DEVICE_ERROR;
        r->inFont_=true;const auto status=r->fontIdOriginal_(self,flags,packageList,stringId,language,info,blt,x,y,rowInfo,rowCount,columnInfo);r->inFont_=false;
        if(status==EFI_SUCCESS && r->installed_ && r->strings_ && r->strings_->GetString && !r->inConOut_ && !r->stopped_ && !r->suspended_) {
            Guard guard(r->boot_);
            size_t size=sizeof(r->stringBuffer_)-2;
            if(r->strings_->GetString(r->strings_,language?language:"en-US",packageList,stringId,r->stringBuffer_,&size,nullptr)==EFI_SUCCESS) {
                r->stringBuffer_[sizeof(r->stringBuffer_)/2-1]=0;r->CaptureGraphic(x,y,r->stringBuffer_,info);
            }
        }
        return status;
    }
    static efi_status EFIAPI HookBlt(Gop* self,uint32_t* buffer,uint32_t operation,size_t sourceX,size_t sourceY,
        size_t x,size_t y,size_t width,size_t height,size_t delta) {
        auto* r=active_;
        const GopBltFn original=r?r->OriginalBlt(self):nullptr;
        if(!original)return EFI_DEVICE_ERROR;
        r->ApplyPendingSynth();   // r396e: graphical pickers draw without reading ConIn
        const auto status=original(self,buffer,operation,sourceX,sourceY,x,y,width,height,delta);
        if(status!=EFI_SUCCESS || !r->installed_ || r->stopped_ || !self->Mode || !self->Mode->info || operation==bltVideoToBuffer)return status;
        Guard guard(r->boot_);
        // Console and HII font text is read from the text hooks: its pixels
        // become the new baseline at once (also while Charon's own menu has
        // the reader suspended), so recognition never reads them again and a
        // program that draws over them right after is still seen.
        if(r->inConOut_ || r->inFont_)r->RehashBands(y,height);
        else if(!r->suspended_)r->ObserveBlt(self,buffer,operation,x,y,width,height);
        return status;
    }
    // r407: true while another program reads the graphical screen itself (graphicsOwnerGuid).
    // Looked up at most once per tick: the Blt hook asks for every tile.
    bool GraphicsOwned() {
        if(ownedTick_!=tick_ || !ownedKnown_) {
            void* any=nullptr;ownedTick_=tick_;ownedKnown_=true;
            owned_=boot_ && boot_->LocateProtocol(&graphicsOwnerGuid,nullptr,&any)==EFI_SUCCESS;
            if(owned_) {graphicsActivity_=0;graphicsAnnounced_=false;pixelDirty_=false;}
        }
        return owned_;
    }
    void ObserveBlt(Gop* gop,const uint32_t* buffer,uint32_t operation,size_t x,size_t y,size_t width,size_t height) {
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader blit",(uint64_t(operation)<<60)|(uint64_t(x)<<45)|(uint64_t(y)<<30)|(uint64_t(width)<<15)|height);
#endif
        if(GraphicsOwned())return;
        const size_t screen=size_t(gop->Mode->info->horizontal)*gop->Mode->info->vertical;
        const size_t area=width*height;
        if(!screen || !area)return;
        // r364: a blinking cursor (OpenCore's, whose text goes straight to
        // the frame buffer) must not keep the full read waiting forever.
        if(area>=2048)lastBlt_=tick_;
        if((operation==bltVideoFill || operation==bltBufferToVideo) && area*10>=screen*9)ClearIcons();
        if(operation==bltVideoFill && area*10>=screen*9) {
            // A new graphical screen: the old text is gone.
            ClearShadow();graphicsActivity_=tick_?tick_:1;graphicsAnnounced_=false;
            for(auto& bar:bars_)bar={};
            return;
        }
        if((operation==bltBufferToVideo || operation==bltVideoFill) && area*4>=screen) {
            if(!graphicsActivity_ || graphicsAnnounced_) {graphicsActivity_=tick_?tick_:1;graphicsAnnounced_=false;}
        }
        if(operation==bltVideoFill && buffer && height>=3 && height<=64 && width>=8)ObserveBar(x,y,width,height,buffer[0]&0xffffff);
        if(operation==bltBufferToVideo)ObserveIcon(x,y,width,height);
        // r358: text a program renders itself. Recognize the changed area
        // once drawing pauses (single-cell blits such as a cursor are skipped).
        // r384: a mouse pointer (OpenCanopy) is redrawn in place many times a
        // second; it must not keep the screen from ever counting as settled.
        if(width<=48 && height<=48) {
            for(const auto& small:small_)if(small.x==x && small.y==y && small.w==width && small.h==height)return;
            small_[smallNext_]={x,y,width,height};smallNext_=(smallNext_+1)%4;
        }
        if(operation!=bltVideoToBuffer && width*height>=8*16*2) {
            if(!pixelDirty_ || pixelGop_!=gop) {dirtyLeft_=x;dirtyTop_=y;dirtyRight_=x+width;dirtyBottom_=y+height;}
            else {
                if(x<dirtyLeft_)dirtyLeft_=x;
                if(y<dirtyTop_)dirtyTop_=y;
                if(x+width>dirtyRight_)dirtyRight_=x+width;
                if(y+height>dirtyBottom_)dirtyBottom_=y+height;
            }
            pixelGop_=gop;pixelDirty_=true;pixelTick_=tick_;
        }
    }
    // r358: programs with a linear frame buffer (GRUB gfxterm, most
    // graphical loaders) draw without Blt. After 1 s with neither console
    // output nor Blt, watch the frame buffer (sparse samples every 100 ms,
    // a full read after a change or every 5 s): a hash per 16-pixel
    // band, and the changed bands go to recognition.
    // r361: drop hooks of GOP instances that are no longer installed (a
    // display driver was stopped and freed them, e.g. when firmware setup
    // reconnects the consoles) or that no longer carry our hook. Using such
    // an entry read freed memory (#GP in firmware setup after Windows Boot
    // Manager's "UEFI Firmware Settings").
    void PruneGops() {
        if(!gopCount_ || !boot_->LocateHandle)return;
        efi_handle handles[16];size_t bytes=sizeof(handles);
        const efi_status status=boot_->LocateHandle(ByProtocol,&gopGuid,nullptr,&bytes,handles);
        if(status==EFI_BUFFER_TOO_SMALL || (status!=EFI_SUCCESS && status!=EFI_NOT_FOUND))return;
        const size_t count=status==EFI_SUCCESS?bytes/sizeof(efi_handle):0;
        Gop* live[16];size_t liveCount=0;
        for(size_t i=0;i<count && i<16;++i) {Gop* gop=nullptr;if(boot_->HandleProtocol(handles[i],&gopGuid,(void**)&gop)==EFI_SUCCESS && gop)live[liveCount++]=gop;}
        size_t kept=0;
        for(size_t i=0;i<gopCount_;++i) {
            bool installed=false;for(size_t j=0;j<liveCount;++j)if(live[j]==gops_[i].gop)installed=true;
            if(installed && gops_[i].gop->Blt==HookBlt)gops_[kept++]=gops_[i];
            else if(gops_[i].mode) {retired_[retiredNext_]={gops_[i].mode,gops_[i].original};retiredNext_=(retiredNext_+1)%8;}
        }
        for(size_t i=kept;i<gopCount_;++i)gops_[i]={};
        if(kept!=gopCount_ && pixelGop_ && !OriginalBltKept(pixelGop_,kept))pixelGop_=nullptr;
        gopCount_=kept;
    }
    bool OriginalBltKept(Gop* gop,size_t count) const {for(size_t i=0;i<count;++i)if(gops_[i].gop==gop)return true;return false;}
    Gop* DeviceGop() {
        PruneGops();
        // The device GOP, not the console splitter's virtual one: a loader
        // may change the device mode and the splitter keeps the old one.
        for(size_t i=0;i<gopCount_;++i)if(!gops_[i].console && gops_[i].gop->Mode && gops_[i].gop->Mode->info && gops_[i].gop->Mode->frameBufferBase)return gops_[i].gop;
        for(size_t i=0;i<gopCount_;++i)if(gops_[i].gop->Mode && gops_[i].gop->Mode->info && gops_[i].gop->Mode->frameBufferBase)return gops_[i].gop;
        return nullptr;
    }
    // What the adapter shows when it differs from the GOP mode: a loader may
    // reprogram the adapter behind GOP (GRUB's native bochs and cirrus
    // drivers, which also flip between two pages).
    struct FrameGeometry {size_t width,height,bytes,pitch,offset;};
    // QEMU standard VGA / bochs-display (PCI 1234:1111) whose BAR0 is the
    // GOP frame buffer: its DISPI registers, read only, through PCI I/O
    // memory reads of BAR2 at 0x500. They give the exact mode and the
    // displayed page. No other device is touched.
    void FindDispi(Gop* gop) {
        dispiSearched_=true;
        efi_handle* handles=nullptr;size_t count=0;
        if(boot_->LocateHandleBuffer(ByProtocol,&pciIoGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return;
        for(size_t i=0;i<count && !dispi_;++i) {
            PciIo* io=nullptr;
            if(boot_->HandleProtocol(handles[i],&pciIoGuid,(void**)&io)!=EFI_SUCCESS || !io)continue;
            uint16_t ids[2]={};uint32_t bar0=0;uint16_t id=0;
            if(io->Pci.Read(io,pciIoWidth16,0,2,ids)!=EFI_SUCCESS || ids[0]!=0x1234 || ids[1]!=0x1111)continue;
            if(io->Pci.Read(io,pciIoWidth32,0x10,1,&bar0)!=EFI_SUCCESS || (gop->Mode->frameBufferBase>>32) || (bar0&~0xfu)!=uint32_t(gop->Mode->frameBufferBase))continue;
            if(io->Mem.Read(io,pciIoWidth16,2,0x500,1,&id)==EFI_SUCCESS && (id&0xfff0)==0xb0c0)dispi_=io;
        }
        boot_->FreePool(handles);
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader dispi",dispi_?1:0);
#endif
    }
    uint64_t FrameLimit(Gop* gop,bool dispi) const {
        return dispi && dispiVram_>gop->Mode->frameBufferSize?dispiVram_:gop->Mode->frameBufferSize;
    }
    uint64_t dispiVram_=0;
    bool DispiGeometry(Gop* gop,FrameGeometry& g) {
        if(!dispiSearched_)FindDispi(gop);
        if(!dispi_)return false;
        uint16_t r[11]={};
        if(dispi_->Mem.Read(dispi_,pciIoWidth16,2,0x500,11,r)!=EFI_SUCCESS || (r[0]&0xfff0)!=0xb0c0 || !(r[4]&1))return false;
        // r386: video memory size (index 0xA, 64 KiB units). GOP's frame
        // buffer size covers only the firmware's mode: GRUB's second page
        // (Ventoy at 1024x768x24) lies beyond it, and the reader fell back to
        // the GOP geometry and read the screen at the wrong width.
        dispiVram_=uint64_t(r[10])*65536;
        const size_t bytes=r[3]==32?4:r[3]==24?3:(r[3]==16 || r[3]==15)?2:0;
        const size_t virtualWidth=r[6]?r[6]:r[1];
        if(!bytes || r[1]<64 || r[2]<64 || virtualWidth<r[1])return false;
        g={r[1],r[2],bytes,virtualWidth*bytes,(size_t(r[9])*virtualWidth+r[8])*bytes};
        const GopModeInfo* info=gop->Mode->info;
        // The firmware's own mode is what GOP describes: nothing to override.
        if(bytes==4 && g.width==info->horizontal && g.height==info->vertical && !g.offset && g.pitch==size_t(info->pixelsPerScanLine)*4)return false;
        const uint64_t limit=dispiVram_>gop->Mode->frameBufferSize?dispiVram_:gop->Mode->frameBufferSize;
        return g.height<=16*maxBands && g.offset+g.pitch*g.height<=limit;
    }
    // Bochs registers first; otherwise a geometry found by recognition,
    // whose displayed page is the one drawn last.
    bool DirectGeometry(Gop* gop,FrameGeometry& g,bool& exact) {
        exact=DispiGeometry(gop,g);
        if(exact)return true;
        if(!geoWidth_)return false;
        g={geoWidth_,geoHeight_,geoBytes_,geoWidth_*geoBytes_,geoPage_*geoWidth_*geoHeight_*geoBytes_};
        return true;
    }
    // Every 100 ms: a sparse hash of each frame-buffer page (every 8th row,
    // every 4th pixel) and the displayed page. A change schedules a full
    // poll once drawing pauses, so arrows in a graphical menu are read
    // within about 0.2 s instead of at the next one-second poll.
    void SampleFrameBuffer() {
        if(options_.reviewOnly || GraphicsOwned()) {pixelDirty_=false;return;}   // r401: Charon's own screens are console text; no frame-buffer work beside Charon
        Gop* gop=DeviceGop();
        if(!gop)return;
        FrameGeometry g{};size_t pages=1;bool exact=false;
        const bool direct=DirectGeometry(gop,g,exact);
        dispiActive_=exact;
        if(direct) {
            if(exact) {if(g.offset!=sampleOffset_) {sampleOffset_=g.offset;sampleChanged_=tick_;sampleDue_=true;}}
            else {g.offset=0;pages=geoPages_;}
        } else g={gop->Mode->info->horizontal,gop->Mode->info->vertical,4,size_t(gop->Mode->info->pixelsPerScanLine)*4,0};
        if(!g.width || !g.height || g.pitch<g.width*g.bytes || g.offset+pages*g.pitch*g.height>FrameLimit(gop,exact))return;
        const volatile uint8_t* frame=reinterpret_cast<const volatile uint8_t*>(uintptr_t(gop->Mode->frameBufferBase))+g.offset;
        unsigned changed=0;size_t newest=geoPage_;
        for(size_t page=0;page<pages;++page) {
            uint32_t h=2166136261u;
            for(size_t y=3;y<g.height;y+=8) {
                const volatile uint8_t* row=frame+page*g.pitch*g.height+y*g.pitch;
                for(size_t x=1;x<g.width;x+=4) {h^=row[x*g.bytes]|uint32_t(row[x*g.bytes+1])<<8;h*=16777619u;}
            }
            if(h!=pageSample_[page]) {++changed;newest=page;}
            pageSample_[page]=h;
        }
        if(!exact && changed==1)geoPage_=newest;
#ifdef HAK_CHARON_READER_TRACE
        if(changed)charontrace::Value("reader sample",(uint64_t(tick_)<<16)|(changed<<8)|newest);
#endif
        if(changed) {sampleChanged_=tick_;sampleDue_=true;}
        else if(sampleDue_ && tick_-sampleChanged_>=6) {sampleDue_=false;PollFrameBuffer();}   // r388: 60 ms
    }
    // Band hashes straight from the device frame buffer in the GOP mode, the
    // same values PollFrameBuffer computes from its Blt copy. The first call
    // in a mode hashes the whole screen.
    // r384: one band's hash from 4 of its 16 rows and every second pixel
    // (pixel(x,y) gives 0x00RRGGBB). Cheap enough to check a 2560x1600
    // frame buffer in place; only changed bands are then copied and read.
    template<class Pixel> static uint32_t BandHash(size_t band,size_t width,size_t height,Pixel pixel) {
        uint32_t h=2166136261u;
        for(size_t row=band*16+1;row<band*16+16 && row<height;row+=4)
            for(size_t x=0;x<width;x+=2) {h^=pixel(x,row);h*=16777619u;}
        return h;
    }
    void RehashBands(size_t y,size_t height) {
        if(dispiActive_ || geoWidth_)return;  // the console cannot draw into another mode
        Gop* gop=DeviceGop();
        if(!gop)return;
        const GopModeInfo* info=gop->Mode->info;
        const size_t width=info->horizontal,screenHeight=info->vertical,pitch=info->pixelsPerScanLine;
        if(info->pixelFormat>1 || width<64 || screenHeight<64 || screenHeight>16*maxBands || pitch<width || pitch*screenHeight*4>gop->Mode->frameBufferSize)return;
        size_t first=y/16,end=(y+height+15)/16;
        if(width!=pollWidth_ || screenHeight!=pollHeight_) {pollWidth_=width;pollHeight_=screenHeight;first=0;end=(screenHeight+15)/16;}
        if(end>(screenHeight+15)/16)end=(screenHeight+15)/16;
        const volatile uint32_t* frame=reinterpret_cast<const volatile uint32_t*>(uintptr_t(gop->Mode->frameBufferBase));
        const bool rgb=info->pixelFormat==0;
        for(size_t band=first;band<end;++band)
            bandHash_[band]=BandHash(band,width,screenHeight,[&](size_t x,size_t row) {
                uint32_t pixel=frame[row*pitch+x]&0xffffff;
                return rgb?((pixel&0xff)<<16)|(pixel&0xff00)|(pixel>>16):pixel;});
    }
    // r397 (user): a screen left black, or an all-blue error fill, with no
    // console text and no drawing is an error state (a loader that drew
    // nothing, a hang, a bug-check fill before the OS paints its own screen).
    // Classify the device frame buffer cheaply: 0 = has content, 1 = black,
    // 2 = blue. Only the standard RGB/BGR GOP modes (Memtest86+, the Windows
    // loader, OpenCore and the like); reprogrammed/DISPI modes return 0.
    int ClassifyBlankScreen() {
        Gop* gop=DeviceGop();
        if(!gop || !gop->Mode || !gop->Mode->info)return 0;
        const GopModeInfo* info=gop->Mode->info;
        if(info->pixelFormat>1 || !gop->Mode->frameBufferBase)return 0;
        const size_t width=info->horizontal,height=info->vertical,pitch=info->pixelsPerScanLine;
        if(width<64 || height<64 || width>8192 || pitch<width || pitch*height*4>gop->Mode->frameBufferSize)return 0;
        const volatile uint32_t* frame=reinterpret_cast<const volatile uint32_t*>(uintptr_t(gop->Mode->frameBufferBase));
        const bool rgb=info->pixelFormat==0;
        size_t total=0,dark=0,blue=0;
        for(size_t y=8;y<height;y+=24)
            for(size_t x=8;x<width;x+=24) {
                const uint32_t raw=frame[y*pitch+x]&0xffffff;
                const uint32_t n=rgb?((raw&0xff)<<16)|(raw&0xff00)|(raw>>16):raw;  // -> 0xRRGGBB
                const unsigned r=(n>>16)&0xff,g=(n>>8)&0xff,b=n&0xff;
                ++total;
                const unsigned mx=r>g?(r>b?r:b):(g>b?g:b);
                if(mx<48)++dark;                             // near-black pixel
                else if(b>=70 && b>r+40 && b>g+40)++blue;      // blue-dominant pixel
            }
        if(total<64)return 0;
        if(dark*1000>=total*997)return 1;                      // essentially nothing drawn (black)
        if(blue*100>=total*92)return 2;                        // near-uniform blue fill (bug-check), a little white text allowed
        return 0;
    }
    void AnnounceBlank(int kind) {
        Speak(options_.polish?(kind==2?u"Niebieski ekran śmierci.":u"Czarny ekran.")
                             :(kind==2?u"Blue screen of death.":u"Black screen."));
        blankToneUntil_=tick_+300;   // ~3 s of menacing tone follows
        blankToneNext_=0;blankTonePhase_=0;
    }
    // Low, long, "menacing" oscillator growl: two close low pitches re-triggered
    // just under their length make a slow throbbing warble (no sound files, no
    // buffer growth; the 500 ms tone cap is respected on every trigger).
    void DriveBlankTone() {
        if(!blankToneUntil_ || tick_>=blankToneUntil_ || !options_.effects || !options_.effects->Tone)return;
        if(tick_<blankToneNext_)return;
        options_.effects->Tone(options_.effects->context,(blankTonePhase_++&1)?90:80,500);
        blankToneNext_=tick_+46;
    }
    // r397 (user): a strictly graphical UEFI screen - pictures, logos, icons,
    // graphical buttons, a splash - with no text the reader can read is still
    // described, not dismissed. A coarse occupancy grid of the device frame
    // buffer gives the background, the content's bounding box and the number of
    // separate graphical regions, so the blind user learns the screen's shape.
    bool DescribeGraphics(char16_t* out,size_t cap,uint32_t* sigOut=nullptr) {
        out[0]=0;if(sigOut)*sigOut=0;
        Gop* gop=DeviceGop();
        if(!gop || !gop->Mode || !gop->Mode->info || gop->Mode->info->pixelFormat>1 || !gop->Mode->frameBufferBase)return false;
        const GopModeInfo* info=gop->Mode->info;
        const size_t W=info->horizontal,H=info->vertical,pitch=info->pixelsPerScanLine;
        if(W<64 || H<64 || W>8192 || pitch<W || pitch*H*4>gop->Mode->frameBufferSize)return false;
        const volatile uint32_t* frame=reinterpret_cast<const volatile uint32_t*>(uintptr_t(gop->Mode->frameBufferBase));
        const bool rgb=info->pixelFormat==0;
        auto at=[&](size_t x,size_t y)->uint32_t {uint32_t raw=frame[y*pitch+x]&0xffffff;return rgb?((raw&0xff)<<16)|(raw&0xff00)|(raw>>16):raw;};
        constexpr size_t GX=24,GY=16;
        static uint8_t cell[GY][GX];static uint8_t bright[GY][GX];static uint32_t colour[GY][GX];
        const uint32_t bg=at(2,2);   // the corner colour (a splash's field)
        auto lum=[&](uint32_t c)->unsigned {return (((c>>16)&0xff)*3+((c>>8)&0xff)*6+(c&0xff))/10;};
        auto diff=[&](uint32_t c)->unsigned {
            const int dr=int((c>>16)&0xff)-int((bg>>16)&0xff),dg=int((c>>8)&0xff)-int((bg>>8)&0xff),db=int(c&0xff)-int(bg&0xff);
            return unsigned((dr<0?-dr:dr)+(dg<0?-dg:dg)+(db<0?-db:db));
        };
        size_t inked=0,minx=GX,maxx=0,miny=GY,maxy=0;
        for(size_t gy=0;gy<GY;++gy)for(size_t gx=0;gx<GX;++gx) {
            const uint32_t c=at(gx*W/GX+W/(2*GX),gy*H/GY+H/(2*GY));
            const bool ink=diff(c)>96;cell[gy][gx]=ink;bright[gy][gx]=uint8_t(lum(c));colour[gy][gx]=c;
            if(ink) {++inked;if(gx<minx)minx=gx;if(gx>maxx)maxx=gx;if(gy<miny)miny=gy;if(gy>maxy)maxy=gy;}
        }
        if(!inked)return false;   // nothing drawn: the blank/BSOD path owns this
        // Separate graphical regions: flood fill the occupancy grid (4-neighbour),
        // recording each region's reading-order key, size and average brightness.
        static uint8_t seen[GY][GX];for(size_t y=0;y<GY;++y)for(size_t x=0;x<GX;++x)seen[y][x]=0;
        struct Region {uint16_t key;uint16_t cells;uint32_t lumSum;uint32_t r,g,b;uint8_t x0,y0,x1,y1;};
        static Region reg[48];size_t regions=0;static uint8_t sx[GX*GY],sy[GX*GY];
        for(size_t y=0;y<GY;++y)for(size_t x=0;x<GX;++x)if(cell[y][x] && !seen[y][x]) {
            size_t top=0;sx[top]=uint8_t(x);sy[top]=uint8_t(y);++top;seen[y][x]=1;
            uint16_t rminx=uint16_t(x),rminy=uint16_t(y),rmaxx=uint16_t(x),rmaxy=uint16_t(y),cells=0;uint32_t lumSum=0,rs=0,gs=0,bs=0;
            while(top) {const size_t cx=sx[--top],cy=sy[top];++cells;lumSum+=bright[cy][cx];
                rs+=(colour[cy][cx]>>16)&0xff;gs+=(colour[cy][cx]>>8)&0xff;bs+=colour[cy][cx]&0xff;
                if(cx>rmaxx)rmaxx=uint16_t(cx);if(cy>rmaxy)rmaxy=uint16_t(cy);
                if(cx<rminx)rminx=uint16_t(cx);if(cy<rminy)rminy=uint16_t(cy);
                const int nb[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
                for(auto& d:nb) {const int ux=int(cx)+d[0],uy=int(cy)+d[1];
                    if(ux>=0 && ux<int(GX) && uy>=0 && uy<int(GY) && cell[uy][ux] && !seen[uy][ux]) {seen[uy][ux]=1;sx[top]=uint8_t(ux);sy[top]=uint8_t(uy);++top;}}}
            if(regions<48)reg[regions++]={uint16_t((rminy/2)*GX+rminx),cells,lumSum,rs/cells,gs/cells,bs/cells,uint8_t(rminx),uint8_t(rminy),uint8_t(rmaxx),uint8_t(rmaxy)};   // reading order: rows (of 2 cells) then columns
        }
        // Reading order so "N of M" is stable as a highlight moves.
        for(size_t i=1;i<regions;++i) {Region r=reg[i];size_t j=i;while(j && reg[j-1].key>r.key) {reg[j]=reg[j-1];--j;}reg[j]=r;}
        // A highlighted region: its average brightness clearly stands out.
        size_t selected=SIZE_MAX;int bestDev=0;long mean=0;
        for(size_t i=0;i<regions;++i)mean+=long(reg[i].lumSum/(reg[i].cells?reg[i].cells:1));
        if(regions)mean/=long(regions);
        for(size_t i=0;i<regions;++i) {const int avg=int(reg[i].lumSum/(reg[i].cells?reg[i].cells:1));int dev=avg-int(mean);if(dev<0)dev=-dev;if(dev>bestDev) {bestDev=dev;selected=i;}}
        if(regions<2 || bestDev<48)selected=SIZE_MAX;   // no clear single highlight
        // r400: one white or grey region among coloured ones is the highlight
        // (a frame or a lit tile), whatever the brightness average says.
        {size_t plain=SIZE_MAX,plainCount=0;
         for(size_t i=0;i<regions;++i) {
            const unsigned mx=reg[i].r>reg[i].g?(reg[i].r>reg[i].b?reg[i].r:reg[i].b):(reg[i].g>reg[i].b?reg[i].g:reg[i].b);
            const unsigned mn=reg[i].r<reg[i].g?(reg[i].r<reg[i].b?reg[i].r:reg[i].b):(reg[i].g<reg[i].b?reg[i].g:reg[i].b);
            if(mx>=150 && mx-mn<36) {plain=i;++plainCount;}
         }
         if(regions>=2 && plainCount==1)selected=plain;}
        const bool pl=options_.polish;
        const size_t coverage=inked*100/(GX*GY);
        size_t a=0;Put(out,a,cap,pl?u"Ekran graficzny. ":u"Graphical screen. ");
        const size_t boxH=maxy-miny+1;
        // r400 (user: audio-described icons and logos). A picture in the middle
        // is named when it can be: the computer maker's logo (the firmware's
        // BGRT table says where it is drawn), or the logo of the system being
        // loaded; a small mark under it is the progress indicator. Otherwise
        // each region gets its colour and shape.
        size_t logo=SIZE_MAX,spinner=SIZE_MAX;
        if(regions>=1 && regions<=3 && coverage<45) {
            for(size_t i=0;i<regions;++i)if(logo==SIZE_MAX || reg[i].cells>reg[logo].cells)logo=i;
            const size_t cx=(reg[logo].x0+reg[logo].x1)/2;
            if(cx<GX/2-4 || cx>GX/2+3)logo=SIZE_MAX;
            for(size_t i=0;i<regions && logo!=SIZE_MAX;++i)if(i!=logo) {
                if(reg[i].cells<=2 && reg[i].y0>reg[logo].y1)spinner=i;else {logo=SIZE_MAX;spinner=SIZE_MAX;}
            }
        }
        if(logo!=SIZE_MAX) {
            size_t bx=0,by=0,bw=0,bh=0;bool maker=false;
            if(BgrtRect(bx,by,bw,bh)) {
                const size_t px=(reg[logo].x0+reg[logo].x1+1)*W/(2*GX),py=(reg[logo].y0+reg[logo].y1+1)*H/(2*GY);
                maker=px+W/GX>=bx && px<=bx+bw+W/GX && py+H/GY>=by && py<=by+bh+H/GY;
            }
            const bool windows=bootManager_ && bootManager_[0]==u'W';
            if(maker)Put(out,a,cap,pl?u"Logo producenta komputera":u"The computer maker's logo");
            else if(windows)Put(out,a,cap,pl?u"Logo Windows":u"The Windows logo");
            else {Put(out,a,cap,pl?u"Logo, kolor ":u"A logo, ");Put(out,a,cap,ColourName(reg[logo].r,reg[logo].g,reg[logo].b,pl));}
            Put(out,a,cap,pl?u" na środku ekranu":u" in the middle of the screen");
            if(spinner!=SIZE_MAX)Put(out,a,cap,pl?u", pod nim wskaźnik postępu":u", a progress indicator under it");
            Put(out,a,cap,u".");
            if(sigOut) {uint32_t h=2166136261u;auto mix=[&](uint32_t v){h=(h^v)*16777619u;};mix(77);mix(uint32_t(regions));mix(uint32_t(maker));mix(uint32_t(spinner!=SIZE_MAX));*sigOut=h?h:1;}
            return true;
        }
        if(regions==1 && coverage>=55)Put(out,a,cap,pl?u"Pełnoekranowy obraz.":u"A full-screen picture.");
        else if(regions==1) {Put(out,a,cap,pl?u"Obraz, ":u"A picture, ");Put(out,a,cap,ColourName(reg[0].r,reg[0].g,reg[0].b,pl));VerticalWhere(out,a,cap,miny,maxy,pl);Put(out,a,cap,u".");}
        else {
            char16_t n[16];size_t k=0;Number(n,k,regions);n[k]=0;Put(out,a,cap,n);Put(out,a,cap,u" ");
            Put(out,a,cap,pl?PolishRegions(regions):u"graphical regions");
            if(regions<=12 && boxH<=4)Put(out,a,cap,pl?u" w jednym rzędzie":u" in one row");
            Put(out,a,cap,u".");
            if(selected!=SIZE_MAX) {   // which one is highlighted (navigation feedback)
                Put(out,a,cap,pl?u" Zaznaczony ":u" Highlighted ");char16_t s[16];k=0;Number(s,k,selected+1);s[k]=0;Put(out,a,cap,s);
                Put(out,a,cap,pl?u" z ":u" of ");k=0;Number(s,k,regions);s[k]=0;Put(out,a,cap,s);
                Put(out,a,cap,u", ");Put(out,a,cap,ColourName(reg[selected].r,reg[selected].g,reg[selected].b,pl));
                Put(out,a,cap,u" ");Put(out,a,cap,ShapeName(reg[selected].x1-reg[selected].x0+1,reg[selected].y1-reg[selected].y0+1,W*GY,H*GX,pl));Put(out,a,cap,u".");
            }
            if(regions<=6) {   // every icon in reading order: colour and shape
                Put(out,a,cap,pl?u" Po kolei: ":u" In order: ");
                for(size_t i=0;i<regions;++i) {
                    if(i)Put(out,a,cap,u", ");
                    Put(out,a,cap,ColourName(reg[i].r,reg[i].g,reg[i].b,pl));Put(out,a,cap,u" ");
                    Put(out,a,cap,ShapeName(reg[i].x1-reg[i].x0+1,reg[i].y1-reg[i].y0+1,W*GY,H*GX,pl));
                }
                Put(out,a,cap,u".");
            }
        }
        char16_t pc[16];size_t k=0;Number(pc,k,coverage);pc[k]=0;
        Put(out,a,cap,pl?u" Zajmuje około ":u" It covers about ");Put(out,a,cap,pc);Put(out,a,cap,pl?u" procent ekranu.":u" percent of the screen.");
        if(sigOut) {uint32_t h=2166136261u;auto mix=[&](uint32_t v){h=(h^v)*16777619u;};mix(uint32_t(regions));mix(uint32_t(selected));mix(uint32_t(coverage));mix(uint32_t(minx|(miny<<8)|(maxx<<16)|(maxy<<24)));*sigOut=h?h:1;}
        return true;
    }
    // Plain colour words for a picture's average colour.
    static const char16_t* ColourName(unsigned r,unsigned g,unsigned b,bool pl) {
        const unsigned mx=r>g?(r>b?r:b):(g>b?g:b),mn=r<g?(r<b?r:b):(g<b?g:b);
        if(mx<50)return pl?u"czarny":u"black";
        if(mx-mn<36)return mx>200?(pl?u"biały":u"white"):mx>110?(pl?u"jasnoszary":u"light grey"):(pl?u"szary":u"grey");
        if(r==mx) {
            if(g>b+40)return g*10>r*8?(pl?u"żółty":u"yellow"):(pl?u"pomarańczowy":u"orange");
            if(b>g+40)return b*10>r*7?(pl?u"fioletowy":u"purple"):(pl?u"różowy":u"pink");
            return pl?u"czerwony":u"red";
        }
        if(g==mx)return b*10>g*8?(pl?u"turkusowy":u"cyan"):r*10>g*8?(pl?u"żółtozielony":u"yellow-green"):(pl?u"zielony":u"green");
        return g*10>b*8?(pl?u"turkusowy":u"cyan"):r*10>b*7?(pl?u"fioletowy":u"purple"):(pl?u"niebieski":u"blue");
    }
    // A region's shape from its box in grid cells (xScale/yScale make cells square).
    static const char16_t* ShapeName(size_t w,size_t h,size_t xScale,size_t yScale,bool pl) {
        const size_t pw=w*xScale,ph=h*yScale;
        if(pw>=ph*3)return pl?u"pasek":u"bar";
        if(ph>=pw*3)return pl?u"słupek":u"column";
        if(w<=1 && h<=1)return pl?u"znak":u"mark";
        return (w>=6 || h>=6)?(pl?u"duży obraz":u"large picture"):(pl?u"kwadrat":u"square");
    }
    // The firmware's boot graphics (ACPI BGRT): where the maker's logo is drawn.
    bool BgrtRect(size_t& x,size_t& y,size_t& w,size_t& h) {
        static const efi_guid acpi2={0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
        if(!table_ || !table_->ConfigurationTable)return false;
        const uint8_t* rsdp=nullptr;
        for(size_t i=0;i<table_->NumberOfTableEntries;++i) {
            const auto& entry=table_->ConfigurationTable[i];const uint8_t* a=reinterpret_cast<const uint8_t*>(&entry);const uint8_t* b=reinterpret_cast<const uint8_t*>(&acpi2);
            bool same=true;for(size_t k=0;k<16;++k)if(a[k]!=b[k])same=false;
            if(same)rsdp=*reinterpret_cast<const uint8_t* const*>(a+16);
        }
        auto le32=[](const uint8_t* p) {return uint32_t(p[0]|p[1]<<8|p[2]<<16|uint32_t(p[3])<<24);};
        auto le64=[&](const uint8_t* p) {return uint64_t(le32(p))|(uint64_t(le32(p+4))<<32);};
        if(!rsdp || rsdp[0]!='R' || rsdp[1]!='S' || rsdp[15]<2)return false;
        const uint8_t* xsdt=reinterpret_cast<const uint8_t*>(uintptr_t(le64(rsdp+24)));
        if(!xsdt || xsdt[0]!='X')return false;
        const uint32_t length=le32(xsdt+4);
        for(uint32_t at=36;at+8<=length && at<36+8*256;at+=8) {
            const uint8_t* t=reinterpret_cast<const uint8_t*>(uintptr_t(le64(xsdt+at)));
            if(!t || t[0]!='B' || t[1]!='G' || t[2]!='R' || t[3]!='T' || le32(t+4)<56)continue;
            const uint8_t* bmp=reinterpret_cast<const uint8_t*>(uintptr_t(le64(t+40)));
            if(!bmp || bmp[0]!='B' || bmp[1]!='M')return false;
            const int32_t bw=int32_t(le32(bmp+18)),bh=int32_t(le32(bmp+22));
            if(bw<=0 || !bh)return false;
            x=le32(t+48);y=le32(t+52);w=size_t(bw);h=size_t(bh<0?-bh:bh);return true;
        }
        return false;
    }
    static const char16_t* PolishRegions(size_t n) {
        if(n==1)return u"obszar graficzny";
        const size_t t=n%10,h=n%100;
        return (t>=2 && t<=4 && (h<12 || h>14))?u"obszary graficzne":u"obszarów graficznych";
    }
    static void VerticalWhere(char16_t* out,size_t& a,size_t cap,size_t miny,size_t maxy,bool pl) {
        const size_t mid=(miny+maxy)/2;
        if(mid<5)Put(out,a,cap,pl?u" u góry":u" at the top");
        else if(mid>10)Put(out,a,cap,pl?u" u dołu":u" at the bottom");
        else Put(out,a,cap,pl?u" na środku":u" in the middle");
    }
    // Keep audio fed between the stages of a long screen read: the timer
    // that runs recognition is the one that pumps speech into DMA.
    void FeedAudio() {
        if(tick_>=pumpRetry_ && options_.voice->Pump(options_.voice,2048)!=EFI_SUCCESS)pumpRetry_=tick_+50;
    }
    void PollFrameBuffer() {
        if(options_.reviewOnly || GraphicsOwned()) {pixelDirty_=false;return;}   // r401: Charon's own screens are console text; no frame-buffer work beside Charon
        lastPoll_=tick_;
        Gop* gop=DeviceGop();
        const GopBltFn original=OriginalBlt(gop);
#ifdef HAK_CHARON_TICK_TRACE
        if(!gop || !original)charontrace::Value("reader poll no gop",(uint64_t(gop!=nullptr)<<1)|(original!=nullptr));
#endif
        if(!gop || !original)return;
        const GopModeInfo* info=gop->Mode->info;
        // A new GOP mode drops a geometry found for a reprogrammed adapter.
        if(info->horizontal!=geoModeWidth_ || info->vertical!=geoModeHeight_ || gop->Mode->frameBufferBase!=geoBase_) {
            geoModeWidth_=info->horizontal;geoModeHeight_=info->vertical;geoBase_=gop->Mode->frameBufferBase;
            geoWidth_=0;geoRetry_=0;geoPages_=1;geoPage_=0;dispi_=nullptr;dispiSearched_=false;aaRead_=false;
        }
        const uint32_t* source=nullptr;size_t width=0,height=0,stride=0;
        FrameGeometry g{};bool exact=false;
        const bool direct=DirectGeometry(gop,g,exact);
        dispiActive_=exact;
#if defined(HAK_CHARON_READER_TRACE) || defined(HAK_CHARON_POLL_DIAG)
        if(direct!=lastDirect_ || g.width!=lastGeoWidth_) {lastDirect_=direct;lastGeoWidth_=g.width;
            charontrace::Value("reader poll geometry",(uint64_t(direct)<<63)|(uint64_t(exact)<<62)|(uint64_t(g.bytes)<<56)|(uint64_t(g.width)<<32)|(uint64_t(g.height)<<16)|(gop->Mode->frameBufferSize>>20));}
#endif
        if(direct) {
            width=g.width;height=g.height;stride=width;
            if(!ConvertFrame(gop,g,height)) {
#ifdef HAK_CHARON_TICK_TRACE
                charontrace::Value("reader convert fail",(uint64_t(g.offset)<<32)|(uint64_t(g.pitch)<<8)|exact);charontrace::Value("reader convert limit",FrameLimit(gop,exact));
#endif
#if defined(HAK_CHARON_READER_TRACE) || defined(HAK_CHARON_POLL_DIAG)
                if(!convertFailed_)charontrace::Value("reader convert failed",(uint64_t(g.offset)<<32)|g.pitch);
                convertFailed_=true;
#endif
                return;
            }
            source=pixels_;
        } else {
            width=info->horizontal;height=info->vertical;stride=width;
            if(width<64 || height<64 || width>8192 || height>16*maxBands)return;
            // r384: a full copy of a large screen in the timer starves the
            // program (OpenCore missed every key at 2560x1600): the bands are
            // first hashed in the frame buffer itself; only a changed range
            // is copied and read.
            const size_t pitchPx=info->pixelsPerScanLine;
            size_t fbRows=0;
            if(info->pixelFormat<=1 && gop->Mode->frameBufferBase && pitchPx>=width) {
                fbRows=gop->Mode->frameBufferSize/(pitchPx*4);if(fbRows>height)fbRows=height;
            }
            if(fbRows>=64 && width==pollWidth_ && fbRows==pollHeight_) {
                const volatile uint32_t* frame=reinterpret_cast<const volatile uint32_t*>(uintptr_t(gop->Mode->frameBufferBase));
                const bool rgb=info->pixelFormat==0;
                size_t first=SIZE_MAX,last=0;
                for(size_t band=0;band*16<fbRows;++band) {
                    const uint32_t h=BandHash(band,width,fbRows,[&](size_t x,size_t row) {
                        uint32_t pixel=frame[row*pitchPx+x]&0xffffff;
                        return rgb?((pixel&0xff)<<16)|(pixel&0xff00)|(pixel>>16):pixel;});
                    if(bandHash_[band]!=h) {if(first==SIZE_MAX)first=band;last=band;}
                    bandHash_[band]=h;
                }
                if(first==SIZE_MAX)return;
                FeedAudio();
                const size_t top=first>=2?first*16-32:0,bottom=(last+3)*16<fbRows?(last+3)*16:fbRows,rows=bottom-top;   // r397: two bands of margin
                if(!pixels_ || pixelCapacity_<width*rows) {
                    if(pixels_)boot_->FreePool(pixels_);
                    pixels_=nullptr;pixelCapacity_=0;
                    if(boot_->AllocatePool(EfiBootServicesData,width*height*4,(void**)&pixels_)!=EFI_SUCCESS || !pixels_) {pixels_=nullptr;return;}
                    pixelCapacity_=width*height;
                }
                // A mode larger than its reported frame buffer (OpenCore at 2560x1600):
                // the GOP's Blt must not be asked for those rows at all.
                if(fbRows<height || original(gop,pixels_,bltVideoToBuffer,0,top,0,0,width,rows,width*4)!=EFI_SUCCESS) {
                    // The Blt refuses rows beyond its reported frame buffer: copy them in place.
                    for(size_t y=0;y<rows;++y)for(size_t x=0;x<width;++x) {
                        uint32_t pixel=frame[(top+y)*pitchPx+x]&0xffffff;
                        pixels_[y*width+x]=rgb?((pixel&0xff)<<16)|(pixel&0xff00)|(pixel>>16):pixel;
                    }
                }
                pixelGop_=gop;
                suspectRead_=false;
                RecognizeArea(pixels_,width,rows,width,0,top);
                if(suspectRead_) {++suspectRetries_;for(size_t band=first;band<=last && band<maxBands;++band)bandHash_[band]^=0x5a5a5a5a;}   // r397: read again
                else suspectRetries_=0;
                FeedAudio();
                return;
            }
            if(!pixels_ || pixelCapacity_<width*height) {
                if(pixels_)boot_->FreePool(pixels_);
                pixels_=nullptr;pixelCapacity_=0;
                if(boot_->AllocatePool(EfiBootServicesData,width*height*4,(void**)&pixels_)!=EFI_SUCCESS || !pixels_) {
#if defined(HAK_CHARON_READER_TRACE) || defined(HAK_CHARON_POLL_DIAG)
                    if(!convertFailed_)charontrace::Value("reader poll alloc failed",width*height*4);
                    convertFailed_=true;
#endif
                    pixels_=nullptr;return;}
                pixelCapacity_=width*height;
            }
            {const efi_status read=original(gop,pixels_,bltVideoToBuffer,0,0,0,0,width,height,width*4);
             if(read!=EFI_SUCCESS) {
#if defined(HAK_CHARON_READER_TRACE) || defined(HAK_CHARON_POLL_DIAG)
                if(!convertFailed_)charontrace::Value("reader poll blt failed",uint64_t(read));
                convertFailed_=true;
#endif
                // r364: a mode larger than the frame buffer the GOP reports
                // (2560x1600 under OpenCore): its Blt refuses whole-screen
                // reads; the rows that fit are read from memory directly.
                const size_t pitch=size_t(info->pixelsPerScanLine)*4;
                if(info->pixelFormat>1 || !gop->Mode->frameBufferBase || pitch<width*4)return;
                size_t rows=gop->Mode->frameBufferSize/pitch;if(rows>height)rows=height;
                if(rows<64 || !ConvertFrame(gop,{width,rows,4,pitch,0},rows))return;
                height=rows;
             }}
            source=pixels_;
        }
        const bool resized=width!=pollWidth_ || height!=pollHeight_;
#if defined(HAK_CHARON_READER_TRACE) || defined(HAK_CHARON_POLL_DIAG)
        if(resized)charontrace::Value("reader poll mode",(uint64_t(exact)<<63)|(uint64_t(g.bytes)<<48)|(uint64_t(width)<<32)|height);
#endif
        pollWidth_=width;pollHeight_=height;
        size_t first=SIZE_MAX,last=0;
        for(size_t band=0;band*16<height;++band) {
            const uint32_t h=BandHash(band,width,height,[&](size_t x,size_t y) {return source[y*stride+x]&0xffffff;});
            if(resized || bandHash_[band]!=h) {if(first==SIZE_MAX)first=band;last=band;}
            bandHash_[band]=h;
        }
#ifdef HAK_CHARON_TICK_TRACE
        charontrace::Value("reader poll bands",(uint64_t(first)<<32)|(last<<8)|resized);
#endif
        if(first==SIZE_MAX)return;
        FeedAudio();
        pixelGop_=gop;
        const size_t top=first>=2?first*16-32:0,bottom=(last+3)*16<height?(last+3)*16:height;   // r397: two bands of margin (a 22-pixel line drawn in two steps lost its ascenders: "urn tne cacne")
        suspectRead_=false;
        const size_t recognized=RecognizeArea(source+top*stride,width,bottom-top,stride,0,top);
        if(suspectRead_) {++suspectRetries_;for(size_t band=first;band<=last && band<maxBands;++band)bandHash_[band]^=0x5a5a5a5a;}   // r397: read again
        else suspectRetries_=0;
        FeedAudio();
        // No exact glyphs where many cells carry ink and no bochs registers:
        // the rows may have another length and depth than GOP says. Look for
        // the geometry (again at most every 5 s while changed screens stay
        // unreadable).
        if(!recognized && !aaRead_ && !exact && !geoWidth_ && tick_>=geoRetry_ && ocr_.LastInked()>=20) {
            geoRetry_=tick_+500;
            if(FindGeometry(gop))pollWidth_=0;  // read the whole screen at the next poll
        }
    }
    // ---- r387: loading seen through disk reads ----------------------------
    // Windows Boot Manager reads a whole WinPE/recovery image (about 1 GB)
    // with a black screen; the spinning dots come later from winload, which
    // holds firmware events back, so no reader code runs then. The reads
    // are what can be observed: many reads while the screen holds no text
    // count as loading - said at once, then (progress tones on) a circling
    // scale while the reads go on, like the spinner itself.
    struct BlockIoHead {
        uint64_t Revision;void* Media;void* Reset;
        efi_status (EFIAPI *ReadBlocks)(BlockIoHead*,uint32_t,uint64_t,size_t,void*);
    };
    using ReadFn=efi_status (EFIAPI*)(BlockIoHead*,uint32_t,uint64_t,size_t,void*);
    struct ReadHook {BlockIoHead* io;ReadFn original;};
    static constexpr size_t maxReadHooks=32;
    ReadHook readHooks_[maxReadHooks]={};size_t readHookCount_=0;
    volatile uint64_t blockReads_=0;uint64_t readsSeen_=0;unsigned spinStep_=0;
    void HookBlockReads() {
        if(readHookCount_ || !boot_->LocateHandleBuffer)return;
        efi_guid guid={0x964e5b21,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
        efi_handle* handles=nullptr;size_t n=0;
        if(boot_->LocateHandleBuffer(ByProtocol,&guid,nullptr,&n,&handles)!=EFI_SUCCESS || !handles)return;
        const efi_tpl old=boot_->RaiseTPL(TPL_NOTIFY);
        for(size_t i=0;i<n && readHookCount_<maxReadHooks;++i) {
            BlockIoHead* io=nullptr;
            if(boot_->HandleProtocol(handles[i],&guid,reinterpret_cast<void**>(&io))!=EFI_SUCCESS || !io || !io->ReadBlocks || io->ReadBlocks==HookBlockRead)continue;
            readHooks_[readHookCount_++]={io,io->ReadBlocks};io->ReadBlocks=HookBlockRead;
        }
        boot_->RestoreTPL(old);
        boot_->FreePool(handles);
        readsSeen_=blockReads_;
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader read hooks",readHookCount_);
#endif
    }
    void UnhookReads() {
        for(size_t i=0;i<readHookCount_;++i)if(readHooks_[i].io && readHooks_[i].io->ReadBlocks==HookBlockRead)readHooks_[i].io->ReadBlocks=readHooks_[i].original;
        readHookCount_=0;
    }
    static efi_status EFIAPI HookBlockRead(BlockIoHead* io,uint32_t media,uint64_t lba,size_t size,void* buffer) {
        auto* self=active_;ReadFn original=nullptr;
        if(self)for(size_t i=0;i<self->readHookCount_;++i)if(self->readHooks_[i].io==io) {original=self->readHooks_[i].original;break;}
        if(!original)return EFI_DEVICE_ERROR;
        if(self)self->blockReads_=self->blockReads_+1;
        return original(io,media,lba,size,buffer);
    }
    // Timer, every 200 ms while read hooks are in place.
    void WatchLoading() {
        const uint64_t reads=blockReads_-readsSeen_;readsSeen_=blockReads_;
        if(reads<8 || tick_-lastText_<100)return;
        if(busyAnnounced_ && lastText_>busyAt_)busyAnnounced_=false;
        if(!busyAnnounced_) {
            busyAnnounced_=true;busyAt_=tick_;
#ifdef HAK_CHARON_READER_TRACE
            charontrace::Value("reader loading",reads);
#endif
            static const char16_t* plBusy[]={u"Kurwa, wczytuje się, czekaj.",u"Kręci się to gówno, czekaj, ja pierdolę.",u"Wczytywanie, siedź na dupie i czekaj."};
            static const char16_t* enBusy[]={u"Fucking loading, wait.",u"This crap is spinning, hold on, damn it.",u"Loading, sit on your arse and wait."};
            Speak(OwnPhrase(options_.polish?u"Wczytywanie, czekaj.":u"Loading, please wait.",plBusy,enBusy,3));
            return;
        }
        static const uint16_t notes[8]={523,587,659,698,784,880,988,1047};
        if((options_.progress&1) && tick_>=endToneUntil_ && options_.effects && options_.effects->Tone)
            options_.effects->Tone(options_.effects->context,notes[spinStep_++%8],35);
    }
    // Copy `rows` rows of a frame buffer geometry into pixels_ as 32-bit
    // values; colours only need to stay distinct.
    bool ConvertFrame(Gop* gop,const FrameGeometry& g,size_t rows) {
        if(g.offset+rows*g.pitch>FrameLimit(gop,dispiActive_) || g.pitch<g.width*g.bytes)return false;
        if(!pixels_ || pixelCapacity_<g.width*rows) {
            if(pixels_)boot_->FreePool(pixels_);
            pixels_=nullptr;pixelCapacity_=0;
            if(boot_->AllocatePool(EfiBootServicesData,g.width*rows*4,(void**)&pixels_)!=EFI_SUCCESS || !pixels_) {pixels_=nullptr;return false;}
            pixelCapacity_=g.width*rows;
        }
        const volatile uint8_t* frame=reinterpret_cast<const volatile uint8_t*>(uintptr_t(gop->Mode->frameBufferBase))+g.offset;
        for(size_t y=0;y<rows;++y) {
            if(y && !(y&127))FeedAudio();  // large modes: 16 MB of frame buffer
            const volatile uint8_t* row=frame+y*g.pitch;uint32_t* out=pixels_+y*g.width;
            if(g.bytes==4) {const volatile uint32_t* words=reinterpret_cast<const volatile uint32_t*>(row);for(size_t x=0;x<g.width;++x)out[x]=words[x]&0xffffff;}
            else if(g.bytes==3)for(size_t x=0;x<g.width;++x)out[x]=uint32_t(row[x*3])|uint32_t(row[x*3+1])<<8|uint32_t(row[x*3+2])<<16;
            else for(size_t x=0;x<g.width;++x)out[x]=uint32_t(row[x*2])|uint32_t(row[x*2+1])<<8;
        }
        return true;
    }
    // Common widths at 32, 24 and 16 bits per pixel with rows packed
    // without padding; the geometry in which most inked cells are exact
    // glyphs wins (at least 60%).
    bool FindGeometry(Gop* gop) {
        static const uint16_t widths[]={640,720,800,1024,1152,1280,1360,1366,1400,1440,1600,1680,1920,2048,2560};
        static const uint8_t depths[]={4,3,2};
        size_t best=0,bestWidth=0,bestHeight=0,bestBytes=0;
        for(const uint8_t bytes:depths)for(const uint16_t width:widths) {
            if(bytes==4 && width==gop->Mode->info->horizontal)continue;
            size_t height=width*3/4;
            if(gop->Mode->frameBufferSize/(width*bytes)<height)height=gop->Mode->frameBufferSize/(width*bytes);
            if(height<64 || height>16*maxBands)continue;
            // The upper part is enough to judge; the whole screen is read once chosen.
            const size_t rows=height<480?height:480;
            if(!ConvertFrame(gop,{width,height,bytes,size_t(width)*bytes,0},rows))continue;
            const size_t quality=ocr_.Quality(pixels_,width,rows,width,80);
            FeedAudio();
            if(quality>best) {best=quality;bestWidth=width;bestHeight=height;bestBytes=bytes;}
        }
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader geometry",(uint64_t(best)<<40)|(uint64_t(bestBytes)<<32)|(uint64_t(bestWidth)<<16)|bestHeight);
#endif
        if(best<600)return false;
        geoWidth_=bestWidth;geoHeight_=bestHeight;geoBytes_=bestBytes;ocr_.Forget();
        // Double buffering by page flipping: a second page right after the
        // first; without device registers the page drawn last counts as shown
        // (a flip back to an unchanged page cannot be seen this way).
        geoPages_=gop->Mode->frameBufferSize>=2*bestWidth*bestHeight*bestBytes?2:1;geoPage_=0;
        for(auto& sample:pageSample_)sample=0;
        return true;
    }
    // Recognize one area of a screen image (left,top = its screen position)
    // into the shadow screen; returns the recognized non-blank cells.
    size_t RecognizeArea(const uint32_t* area,size_t width,size_t height,size_t stride,size_t left,size_t top) {
        const size_t right=left+width,bottom=top+height,clearLeft=left,clearTop=top;
        // r386: the readers see only the rectangle that differs from the
        // area's corner colour (16-pixel margin): an area of one colour (a
        // screen just cleared: OpenCore at start, a mode switch) holds no
        // text, and OpenCore's 2560x1600 menu fills its top rows only. All
        // three readers used to scan everything, billions of cycles in the
        // timer while the program stood still (its menu was not drawn).
        // Text that the area showed before is still replaced in all of it.
        {
            const uint32_t c=area[0]&0xffffff;size_t minX=width,maxX=0,minY=height,maxY=0;
            for(size_t y=0;y<height;++y) {
                const uint32_t* row=area+y*stride;size_t x=0;
                while(x<width && (row[x]&0xffffff)==c)++x;
                if(x==width)continue;
                if(x<minX)minX=x;
                size_t e=width;while(e>x && (row[e-1]&0xffffff)==c)--e;
                if(e-1>maxX)maxX=e-1;
                if(minY==height)minY=y;
                maxY=y;
            }
            if(minY==height)return 0;
            const size_t y0=minY>=16?minY-16:0,y1=maxY+17<height?maxY+17:height;
            const size_t x0=minX>=16?minX-16:0,x1=maxX+17<width?maxX+17:width;
            area+=y0*stride+x0;left+=x0;top+=y0;width=x1-x0;height=y1-y0;
        }
        size_t lines=0;
        bool rowCleared[maxRows]={};   // r396
        bool baselineRows=false;  // r367: TrueType/bitmap lines sit in rows by their baseline
        auto emit=[&](const char16_t* text,size_t x,size_t y,uint32_t fg,uint32_t bg) {
            if(SecretScreen())return;   // r407
            if(!lines++) {
                // Replace what this area showed before (only when it holds
                // text), but only cells that lie wholly inside the area: a
                // text row cut by its edge is not read, so it must not be
                // cleared either.
                // Baseline rows: a row's lines reach up to 20 pixels above
                // and 5 below the baselines it holds.
                const size_t firstRow=baselineRows?(clearTop+35)/16:(clearTop+15)/16;
                const size_t endRow=baselineRows?(bottom>=20?(bottom-20)/16+1:0):(bottom>=31?(bottom-31)/16+1:0);
                const size_t firstColumn=(clearLeft+7)/8,endColumn=right>=15?(right-15)/8+1:0;
                for(size_t r=firstRow;r<endRow && r<maxRows;++r)for(size_t c=firstColumn;c<endColumn && c<maxColumns;++c) {
                    if(text_[r][c]!=u' ')dirty_[r]=true;text_[r][c]=u' ';attr_[r][c]=0;cellFont_[r][c]=0;}
            }
            // r396: a row that gets new text here is cleared across the area's
            // columns first, even when the area's edge cuts it (it starts at
            // the first changed pixel, below the row's blank top): Limine's
            // key bar kept "NTER Bo" of the old bar between the new words.
            {const size_t row=y/16;
             if(row<maxRows && !rowCleared[row]) {
                 rowCleared[row]=true;
                 const size_t c0=(clearLeft+7)/8,c1=right>=15?(right-15)/8+1:0;
                 for(size_t c=c0;c<c1 && c<maxColumns;++c) {if(text_[row][c]!=u' ')dirty_[row]=true;text_[row][c]=u' ';attr_[row][c]=0;cellFont_[row][c]=0;}
             }}
            if(y/16<maxRows)ocrRow_[y/16]=true;   // r397
            CaptureCells(x,y,text,fg,bg,16);
        };
        // r362: lines of box-drawing and block glyphs only are no text: the
        // anti-aliased edge of a highlight bar matches Unifont's block
        // elements (Windows Boot Manager's menu), and taking them as read
        // kept the other readers away from the real text.
        size_t wordLines=0;
        auto unifont=[&](const char16_t* text,size_t x,size_t y,uint32_t fg,uint32_t bg) {
            bool word=false;
            // r367: a letter or digit; a lone bar ('|' of a theme's
            // scrollbar) is no text either.
            for(const char16_t* p=text;*p && !word;++p)word=(*p>=u'0' && *p<=u'9') || (*p>=u'A' && *p<=u'Z') || (*p>=u'a' && *p<=u'z') || (*p>=0xc0 && *p<0x2500);
            if(!word)return;
            // r386: a background a few shades off the area's own (another
            // part of a wallpaper) is not a selection bar.
            {const uint32_t corner=area[0]&0xffffff;unsigned d=0;for(unsigned s=0;s<24;s+=8) {const int v=int((bg>>s)&255)-int((corner>>s)&255);d+=unsigned(v<0?-v:v);}
             if(d<120)bg=corner;}
            ++wordLines;captureFont_=uint16_t((1<<8)|16);emit(text,x,y,fg,bg);
        };
        // r384: OpenCanopy's labels, read exactly with its own BMFont.
        if(bootManager_ && bootManager_[0]==u'O' && bmOcr_.Ready()) {
            const size_t n=bmOcr_.Recognize(area,width,height,stride,left,top,[&](const char16_t* text,size_t x,size_t y,uint32_t fg,uint32_t bg) {captureFont_=uint16_t(5<<8);emit(text,x,y,fg,bg);});
            if(n) {lastText_=tick_;return n;}
        }
#ifdef HAK_CHARON_POLL_DIAG
        const uint64_t diag0=Tsc();
#endif
        ocr_.bandsEnabled=!bootManager_ || GrubFamily(bootManager_);  // r386: GRUB or a program without a known manager
        ocr_.bandsAlways=GrubFamily(bootManager_);
        size_t recognized=ocr_.Recognize(area,width,height,stride,left,top,unifont,[&]() {FeedAudio();});
        if(!wordLines)recognized=0;
#ifdef HAK_CHARON_POLL_DIAG
        const uint64_t diag1=Tsc();uint64_t diagFonts=0,diagAa=0;
#endif
        // r359: no bitmap Unifont text — anti-aliased monospace text blended
        // in rEFInd's font (Liberation Mono), on a plain background.
        if(!recognized) {
            FeedAudio();
            auto blended=[&](const char16_t* text,size_t x,size_t y,uint32_t fg,uint32_t bg,size_t h) {captureFont_=uint16_t((3<<8)|(h<255?h:255));emit(text,x,y,fg,bg);};
            // r361: proportional TrueType text of a boot program whose fonts
            // were loaded from its disk (Windows Boot Manager); r363+ bitmap
            // and cell fonts of the running boot manager.
            // r367: lines are placed by their baseline; one on a row
            // boundary (a theme's entry that turns bold moves by a pixel)
            // keeps the row it already has in that column.
            auto placed=[&](const char16_t* text,size_t x,size_t y,uint32_t fg,uint32_t bg,size_t h) {
                // r397: a selection bar a few shades off a plain background
                // (Clover's options menu: 0xADADAD on 0xBFBFBF) falls into the
                // same console colour; it is given another one so the row is
                // the selection.
                {const uint32_t corner=area[0]&0xffffff;unsigned d=0;for(unsigned s=0;s<24;s+=8) {const int v=int((bg>>s)&255)-int((corner>>s)&255);d+=unsigned(v<0?-v:v);}
                 if(d>=24 && d<120 && (ConsoleColor(bg)&7)==(ConsoleColor(corner)&7))bg=(ConsoleColor(corner)&7)==1?0x909090u:0x000090u;}
                captureFont_=uint16_t((4<<8)|(h<255?h:255)|(ttfOcr_.LastBold()?0x4000:0));
                size_t row=y/16;const size_t column=x/8;
                if(row<maxRows && column<maxColumns && text_[row][column]==u' ') {
                    if(row && text_[row-1][column]!=u' ' && (y%16)<=1)y-=16;
                    else if(row+1<maxRows && text_[row+1][column]!=u' ' && (y%16)>=14)y+=16;
                }
                // r397: a line caught half drawn (Windows Memory Diagnostic:
                // "!urn tne cacne on ror a!! tests." for "Turn the cache on for
                // all tests.") has '!' inside words: it is not taken; the area
                // is read again at the next polls (twice at most).
                {bool suspect=false;
                 for(const char16_t* p=text;*p && !suspect;++p)if(*p==u'!') {
                    const char16_t next=p[1];
                    suspect=(next>=u'a' && next<=u'z') || next==u'!';}
                 if(suspect && suspectRetries_<2) {suspectRead_=true;return;}}
                {const size_t r2=y/16;if(r2<maxRows && caretLineX_[r2]==SIZE_MAX)caretLineX_[r2]=x;}   // r396: the line's first pixel, for the caret's column
                emit(text,x,y,fg,bg);
            };
            auto fonts=[&]() {
                for(size_t r2=0;r2<maxRows;++r2)caretLineX_[r2]=SIZE_MAX;
                baselineRows=true;
                const size_t n=ttfOcr_.Recognize(area,width,height,stride,left,top,placed,[&]() {FeedAudio();});
                baselineRows=false;
                // r396: a drawn block caret (Limine's entry editor) becomes one
                // cell in another background, which DrawnCaret takes as the
                // caret: arrows read the line or letter under it.
                {size_t cx=0,cy=0;uint32_t ink=0;
                 if(ttfOcr_.TakeCaret(cx,cy,ink)) {
                     const size_t row=(cy+8)/16;
                     size_t column=(cx+4)/8;
                     if(row<maxRows && caretLineX_[row]!=SIZE_MAX && cx+4>=caretLineX_[row])column=caretLineX_[row]/8+(cx+4-caretLineX_[row])/8;
#ifdef HAK_CHARON_READER_TRACE
                     charontrace::Value("reader caret",(uint64_t(cx)<<32)|(uint64_t(cy)<<16)|column);
#endif
                     if(n && row<maxRows && column<maxColumns) {
                         uint8_t& a=attr_[row][column];
                         if(!(a&0x80))a=uint8_t(0x80|0x07|(majorityBg_<<4));
                         uint8_t other=uint8_t(ConsoleColor(ink)&7);if(other==((a>>4)&7))other=uint8_t((other+1)&7);
                         a=uint8_t((a&0x8f)|(other<<4));
                         if(text_[row][column]==0)text_[row][column]=u' ';
                     }
                 }}
                return n;
            };
            // r367: a recognized boot manager other than rEFInd with fonts of
            // its own is read with them first; rEFInd's anti-aliased reader
            // (slow on pictures) only when they find nothing.
            const bool ownFirst=ttfOcr_.FontCount() && bootManager_ && bootManager_[0]!=u'r';
#ifdef HAK_CHARON_POLL_DIAG
            uint64_t t=Tsc();
#define DIAG_SPAN(v) {const uint64_t n=Tsc();v+=n-t;t=n;}
#else
#define DIAG_SPAN(v)
#endif
            if(ownFirst) {recognized=fonts();FeedAudio();DIAG_SPAN(diagFonts)}
            if(!recognized) {
                recognized=aaOcr_.Recognize(area,width,height,stride,left,top,blended,[&]() {FeedAudio();});
                DIAG_SPAN(diagAa)
                if(!recognized && ttfOcr_.FontCount() && !ownFirst) {FeedAudio();recognized=fonts();DIAG_SPAN(diagFonts)}
            }
#undef DIAG_SPAN
            if(recognized)aaRead_=true;
        }
        if(recognized)lastText_=tick_;
#if defined(HAK_CHARON_READER_TRACE) || defined(HAK_CHARON_POLL_DIAG)
        charontrace::Value("reader ocr",(uint64_t(top)<<48)|(uint64_t(bottom)<<32)|(uint64_t(lines)<<16)|recognized);
#endif
#ifdef HAK_CHARON_POLL_DIAG
        charontrace::Value("reader ocr area",(uint64_t(width)<<32)|height);
        charontrace::Value("reader ocr unifont Mcyc",(diag1-diag0)>>20);
        charontrace::Value("reader ocr fonts Mcyc",diagFonts>>20);
        charontrace::Value("reader ocr aa Mcyc",diagAa>>20);
#endif
        return recognized;
    }
    // r384: OpenCore's GOP refuses Blt reads: copy a rectangle straight from
    // the linear frame buffer instead (32-bit RGB/BGR modes, inside its size).
    bool CopyFromFrame(Gop* gop,size_t left,size_t top,size_t width,size_t height) {
        const GopModeInfo* info=gop->Mode->info;
        const size_t pitch=info->pixelsPerScanLine;
        if(info->pixelFormat>1 || !gop->Mode->frameBufferBase || pitch<info->horizontal || left+width>info->horizontal
            || (top+height)*pitch*4>gop->Mode->frameBufferSize || width*height>pixelCapacity_)return false;
        const volatile uint32_t* frame=reinterpret_cast<const volatile uint32_t*>(uintptr_t(gop->Mode->frameBufferBase));
        const bool rgb=info->pixelFormat==0;
        for(size_t y=0;y<height;++y)for(size_t x=0;x<width;++x) {
            const uint32_t p=frame[(top+y)*pitch+left+x]&0xffffff;
            pixels_[y*width+x]=rgb?((p&0xff)<<16)|(p&0xff00)|(p>>16):p;
        }
        return true;
    }
    void RecognizePixels() {
        if(options_.reviewOnly) {pixelDirty_=false;return;}   // r401: Charon's own screens are console text; no frame-buffer work beside Charon
        pixelDirty_=false;
        PruneGops();   // an instance uninstalled since its Blt is forgotten (pixelGop_ too), never read
        Gop* gop=pixelGop_;const GopBltFn original=OriginalBlt(gop);
        if(!gop || !original || !gop->Mode || !gop->Mode->info)return;
        const size_t screenWidth=gop->Mode->info->horizontal,screenHeight=gop->Mode->info->vertical;
        size_t left=dirtyLeft_,top=dirtyTop_,right=dirtyRight_<screenWidth?dirtyRight_:screenWidth,bottom=dirtyBottom_<screenHeight?dirtyBottom_:screenHeight;
        // Whole cells around the area, so a glyph cut by the rectangle edge
        // is still read in full.
        left=left>=8?left-8:0;top=top>=16?top-16:0;
        right=right+8<screenWidth?right+8:screenWidth;bottom=bottom+16<screenHeight?bottom+16:screenHeight;
        if(right<=left+8 || bottom<=top+16)return;
        const size_t width=right-left,height=bottom-top;
        if(!pixels_ || pixelCapacity_<width*height) {
            if(pixels_)boot_->FreePool(pixels_);
            pixels_=nullptr;pixelCapacity_=0;
            const size_t want=screenWidth*screenHeight;
            if(boot_->AllocatePool(EfiBootServicesData,want*4,(void**)&pixels_)!=EFI_SUCCESS || !pixels_) {pixels_=nullptr;return;}
            pixelCapacity_=want;
        }
        if(original(gop,pixels_,bltVideoToBuffer,left,top,0,0,width,height,width*4)!=EFI_SUCCESS && !CopyFromFrame(gop,left,top,width,height))return;
        RecognizeArea(pixels_,width,height,width,left,top);
        UpdateIcons(pixels_,width,height,left,top);
        // r384: an icon strip is examined whole, also when only two of its
        // tiles were redrawn.
        if(strip_.used && size_t(strip_.x)+strip_.w<=screenWidth && size_t(strip_.y)+strip_.h<=screenHeight && size_t(strip_.w)*strip_.h<=pixelCapacity_
            && (original(gop,pixels_,bltVideoToBuffer,strip_.x,strip_.y,0,0,strip_.w,strip_.h,size_t(strip_.w)*4)==EFI_SUCCESS || CopyFromFrame(gop,strip_.x,strip_.y,strip_.w,strip_.h)))
            UpdateStrip(pixels_,strip_.w,strip_.h,strip_.x,strip_.y);
    }
    // Graphical progress bar: a track drawn once at full width, then a fill
    // of another colour that grows at least twice from the same left edge.
    void ObserveBar(size_t x,size_t y,size_t width,size_t height,uint32_t color) {
        Bar* bar=nullptr;
        for(auto& candidate:bars_)if(candidate.height && (candidate.y>y?candidate.y-y:y-candidate.y)<=2
            && (candidate.height>height?candidate.height-height:height-candidate.height)<=4
            && (candidate.x>x?candidate.x-x:x-candidate.x)<=6) {bar=&candidate;break;}
        if(!bar) {bar=&bars_[nextBar_++%(sizeof(bars_)/sizeof(bars_[0]))];*bar={};bar->x=x;bar->y=y;bar->height=height;}
        // The track is the widest rectangle; a fill reaching its full width
        // (100%) keeps its own colour and stays the fill.
        if(width>bar->track || (width==bar->track && color==bar->trackColor) || !bar->track) {if(color!=bar->fillColor || !bar->rises) {bar->track=width;bar->trackColor=color;}}
        if(color!=bar->trackColor) {
            if(color==bar->fillColor && width>bar->fill)++bar->rises;
            else if(color!=bar->fillColor) {bar->fillColor=color;bar->rises=0;}
            bar->fill=width;
        }
        if(bar->rises>=2 && bar->track)Progress(int(bar->fill*100/bar->track));
    }
    // Readable text of one row: box drawing, blocks and geometric shapes are
    // decoration in text-mode menus and become spaces; runs of spaces collapse.
    // onlyHighlighted: the cells drawn in the highlight colour, i.e. the
    // selected item without a help column or frame sharing its row.
    size_t RowText(size_t r,char16_t* out,size_t capacity,bool onlyHighlighted=false) const {
        return CellsText(text_[r],attr_[r],0,columns_,onlyHighlighted,out,capacity);
    }
    // r393: map (may be null) receives the cell column of every character written.
    size_t CellsText(const char16_t* cells,const uint8_t* attrs,size_t from,size_t to,bool onlyHighlighted,char16_t* out,size_t capacity,uint8_t* map=nullptr) const {
        size_t at=0;bool space=true,submenu=false;
        if(to>columns_)to=columns_;
        for(size_t c=from;c<to && at+1<capacity;++c) {
            if(map)map[at]=uint8_t(c);
            char16_t ch=cells[c]?cells[c]:u' ';
            if(onlyHighlighted && attrs && !Highlighted(attrs[c]))ch=u' ';
            // A triangle in front of an item marks a submenu (firmware forms).
            if(!at && (ch==0x25ba || ch==0x25b6)) {submenu=true;continue;}
            // Arrow glyphs in key hints: r390 they stay arrows (triangles
            // become arrows); NVDA's symbol processing names them in the
            // language of the text ("up arrow" in English hints).
            if(ch==0x25b2 || ch==0x25b4)ch=0x2191;
            else if(ch==0x25bc || ch==0x25be)ch=0x2193;
            else if(ch==0x25c4 || ch==0x25c0)ch=0x2190;
            else if(ch==0x25ba || ch==0x25b6)ch=0x2192;
            if(ch>=0x2190 && ch<=0x2193) {
                if(!space && at+2<capacity) {out[at++]=u' ';if(map)map[at]=uint8_t(c);}   // room for the arrow and the terminator too
                out[at++]=ch;space=false;continue;
            }
            // Frames and a menu marker '*' in front of an entry are decoration.
            if((ch>=0x2500 && ch<=0x25ff && ch!=0x25c9 && ch!=0x25cb) || (ch==u'*' && space && (c+1>=to || cells[c+1]!=u' '))
                || (!at && (ch==u'*' || ch==u'>') && c+1<to && cells[c+1]==u' '))ch=u' ';  // r386: OpenCore's "* " default and "> " selection marks
            if(map && at<capacity)map[at]=uint8_t(c);
            if(ch==u' ') {if(!space)out[at++]=u' ';space=true;}
            else {out[at++]=ch;space=false;}
        }
        while(at && out[at-1]==u' ')--at;
        if(submenu && at && at+4<capacity) {const char16_t* word=options_.polish?u", podmenu":u", submenu";out[at++]=speechtext::ownOpen;while(*word && at+2<capacity)out[at++]=*word++;out[at++]=speechtext::ownClose;}
        out[at]=0;return at;
    }
    // r382: controls drawn as text by boot managers and firmware forms.
    // "[X] Name" / "Name [ ]" check boxes, "(*)" radio buttons, "[ OK ]" and
    // "< OK >" buttons, "[5]" spin buttons (HII numbers), "[12:30:00]" edit
    // fields, "<UEFI>" / "Mode [Enabled]" combo boxes (HII one-of, AMI
    // style), Limine's "[+]" / "[-]" folders and the ☐☑☒ ○◉ symbols.
    // Out: label, then role and state, NVDA order. Returns false when the
    // text holds no control (out untouched).
    enum class Control {none,checked,unchecked,radioOn,radioOff,button,spin,edit,combo,collapsed,expanded,
        submenu,date,time,password,progress /* r388 */};
    static bool KeyName(const char16_t* t,size_t n) {
        if(!n)return false;
        if((t[0]==u'F' || t[0]==u'f') && n<=3 && n>=2) {bool digits=true;for(size_t i=1;i<n;++i)if(t[i]<u'0' || t[i]>u'9')digits=false;if(digits)return true;}
        static const char16_t* keys[]={u"Esc",u"ESC",u"Enter",u"ENTER",u"Tab",u"TAB",u"Space",u"SPACE",u"Spacebar",u"SpaceBar",u"Space Bar",u"Spacja",u"Shift+Tab",u"Home",u"End",u"PgUp",u"PgDn",u"Page Up",u"Page Down",
            u"Del",u"Ins",u"INS",u"OVR",u"Ovr",u"Ctrl",u"Alt",u"Shift",u"+/-",u"-/+",u"Up",u"Down",u"Left",u"Right",u"Insert",u"Delete",u"Backspace"};
        for(auto* k:keys) {size_t i=0;while(i<n && k[i] && k[i]==t[i])++i;if(i==n && !k[i])return true;}
        return false;
    }
    static Control Classify(const char16_t* text,size_t open,size_t close,bool alone) {
        const char16_t o=text[open];const char16_t* in=text+open+1;size_t n=close-open-1;
        while(n && in[0]==u' ') {++in;--n;}while(n && in[n-1]==u' ')--n;
        const char16_t after=text[close+1];
        if(after==u'=' || after==u':' || (after==u' ' && text[close+2]==u'=') || KeyName(in,n))return Control::none;
        if(o==u'(') {
            if(!n)return Control::radioOff;
            if(n==1 && (in[0]==u'*' || in[0]==u'o' || in[0]==u'x' || in[0]==u'X' || in[0]==0x2022))return Control::radioOn;
            return Control::none;
        }
        if(o==u'[') {
            if(!n)return close>open+4?Control::edit:Control::unchecked;   // r397: "[            ]" is an empty text field (a password box), "[ ]" a check box
            if(n==1 && (in[0]==u'X' || in[0]==u'x' || in[0]==u'*' || in[0]==0x2713 || in[0]==0x2714 || in[0]==0x221a))return Control::checked;
            if(n==1 && in[0]==u'+')return Control::collapsed;
            if(n==1 && in[0]==u'-')return Control::expanded;
        }
        bool digits=n>0,numeric=n>0,letter=false;
        for(size_t i=0;i<n;++i) {const char16_t c=in[i];
            const bool d=c>=u'0' && c<=u'9';
            if(!d && !(i==0 && c==u'-'))digits=false;
            if(!d && c!=u'/' && c!=u':' && c!=u'.' && c!=u'-' && c!=u' ')numeric=false;
            if(c>=0x41)letter=true;}
        if(o==u'[' && digits)return Control::spin;
        // r388: setup fields - hidden passwords, dates, times, text progress bars.
        if(o==u'[' && n) {
            bool stars=true,bar=true;size_t filled=0,slashes=0,colons=0,digitsSeen=0;
            for(size_t i=0;i<n;++i) {const char16_t c=in[i];
                if(c!=u'*' && c!=0x2022)stars=false;
                if(c==u'#' || c==u'=' || c==u'|' || c==0x2588 || c==0x2593)++filled;else if(c!=u'.' && c!=u'-' && c!=u' ' && c!=0x2591 && c!=0x2592)bar=false;
                if(c==u'/')++slashes;if(c==u':')++colons;if(c>=u'0' && c<=u'9')++digitsSeen;}
            if(stars)return Control::password;
            if(bar && n>=5 && filled)return Control::progress;
            if(slashes==2 && digitsSeen>=4)return Control::date;
            if(colons>=1 && colons<=2 && digitsSeen>=3 && numeric)return Control::time;
        }
        if(o==u'[' && numeric && n>=3)return Control::edit;
        if(!letter || n>32)return Control::none;
        // r397: a code address or symbol (Limine's stack trace "<panic+0x143>", "<0x1bfaf4c3>") is text, not a control.
        if(o==u'<') {
            auto hex=[](char16_t c) {return (c>=u'0' && c<=u'9') || ((c|0x20)>=u'a' && (c|0x20)<=u'f');};
            for(size_t i=0;i+3<n;++i)if(in[i]==u'+' && in[i+1]==u'0' && (in[i+2]|0x20)==u'x' && hex(in[i+3]))return Control::none;   // symbol+offset
            if(n>=8 && in[0]==u'0' && (in[1]|0x20)==u'x') {bool all=true;for(size_t i=2;i<n;++i)all=all && hex(in[i]);if(all)return Control::none;}   // a bare address
        }
        if(o==u'|')return alone?Control::button:Control::none;  // OpenCore's |Shutdown| |Restart|
        if(alone && o==u'<' && in==text+open+1 && text[close-1]!=u' ')return Control::combo;  // edk2 one-of value "<NONE>"
        // r392: an option value of a setup form ("[Enabled]", "[Auto]" in AMI and Insyde setup) is a choice, not a button.
        if(o==u'[' && in==text+open+1 && text[close-1]!=u' ') {
            static const char16_t* values[]={u"Enabled",u"Disabled",u"On",u"Off",u"Auto",u"Manual",u"Legacy",u"UEFI",u"None",u"Normal",u"Fast",u"Full",u"Minimal",
                u"Thorough",u"Default",u"Custom",u"Standard",u"AHCI",u"RAID",u"IDE",u"Deployed",u"Setup",u"User",u"Locked",u"Unlocked",u"Both",u"Never",u"Always"};
            for(auto* v:values) {size_t k=0;while(k<n && v[k] && v[k]==in[k])++k;if(k==n && !v[k])return Control::combo;}
        }
        if(alone)return Control::button;
        // r397: a list of values in brackets (OpenBSD's "mem[640K 7M 20K 2021M]") is text.
        {size_t words=1;for(size_t k=0;k<n;++k)if(in[k]==u' ' && k+1<n && in[k+1]!=u' ')++words;if(words>3)return Control::none;}
        return Control::combo;
    }
    static void Put(char16_t* out,size_t& at,size_t capacity,const char16_t* text,size_t n=SIZE_MAX) {
        for(size_t i=0;i<n && text[i] && at+1<capacity;++i)out[at++]=text[i];
        out[at]=0;
    }
    static void PutRole(char16_t* out,size_t& at,size_t capacity,Control kind,const char16_t* value,size_t n,bool pl) {
        const char16_t* role=u"";const char16_t* state=u"";
        switch(kind) {
            case Control::checked:role=pl?u"pole wyboru":u"check box";state=pl?u"zaznaczone":u"checked";break;
            case Control::unchecked:role=pl?u"pole wyboru":u"check box";state=pl?u"niezaznaczone":u"not checked";break;
            case Control::radioOn:role=pl?u"przycisk opcji":u"radio button";state=pl?u"zaznaczony":u"checked";break;
            case Control::radioOff:role=pl?u"przycisk opcji":u"radio button";state=pl?u"niezaznaczony":u"not checked";break;
            case Control::button:role=pl?u"przycisk":u"button";break;
            case Control::spin:role=pl?u"pokrętło":u"spin button";break;
            case Control::edit: {role=pl?u"pole edycji":u"edit";bool blank=true;for(size_t i=0;i<n && blank;++i)if(value[i]!=u' ')blank=false;if(blank)state=pl?u"puste":u"blank";break;}   // r397
            case Control::combo:role=pl?u"lista rozwijana":u"combo box";break;
            case Control::collapsed:role=pl?u"folder":u"folder";state=pl?u"zwinięty":u"collapsed";break;
            case Control::expanded:role=pl?u"folder":u"folder";state=pl?u"rozwinięty":u"expanded";break;
            case Control::submenu:role=pl?u"podmenu":u"submenu";break;
            case Control::date:role=pl?u"pole daty":u"date field";break;
            case Control::time:role=pl?u"pole czasu":u"time field";break;
            case Control::password:role=pl?u"pole hasła":u"password field";state=pl?u"hasło ustawione":u"password set";break;
            case Control::progress: {
                role=pl?u"pasek postępu":u"progress bar";
                size_t filled=0,total=0;for(size_t i=0;i<n;++i) {const char16_t c=value[i];if(c==u' ' && (!i || i+1==n))continue;++total;
                    if(c==u'#' || c==u'=' || c==u'|' || c==0x2588 || c==0x2593)++filled;}
                static char16_t percent[8];size_t p=0;const size_t v=total?filled*100/total:0;
                if(v>=100)percent[p++]=u'1';if(v>=10)percent[p++]=char16_t(u'0'+(v/10)%10);percent[p++]=char16_t(u'0'+v%10);percent[p++]=u'%';percent[p]=0;
                state=percent;break;}
            default:return;
        }
        // r390: the reader's own words between markers (no symbol processing, menu language).
        const bool lead=at!=0;Put(out,at,capacity,u"\xE001");
        if(lead)Put(out,at,capacity,u", ");
        Put(out,at,capacity,role);
        if(state[0]) {Put(out,at,capacity,u", ");Put(out,at,capacity,state);}
        if(kind==Control::spin || kind==Control::edit || kind==Control::combo || kind==Control::date || kind==Control::time) {
            while(n && value[0]==u' ') {++value;--n;}while(n && value[n-1]==u' ')--n;
            if(n) {Put(out,at,capacity,u", \xE002");Put(out,at,capacity,value,n);return;}
        }
        Put(out,at,capacity,u"\xE002");
    }
    // Finds the first control token; label = the text around it.
    static bool DescribeControls(const char16_t* text,char16_t* out,size_t capacity,bool pl,size_t* roleAt=nullptr) {
        size_t length=0;while(text[length])++length;
        if(!length || length>=maxColumns+1)return false;
        // An empty text field of a firmware form: underscores only.
        {bool underscore=false,only=true;for(size_t i=0;i<length;++i) {if(text[i]==u'_')underscore=true;else if(text[i]!=u' ')only=false;}
         if(underscore && only) {size_t at=0;out[0]=0;if(roleAt)*roleAt=0;Put(out,at,capacity,u"\xE001");Put(out,at,capacity,pl?u"pole edycji, puste":u"edit, blank");Put(out,at,capacity,u"\xE002");return true;}}
        // r388: a submenu mark before or after the name (AMI Aptio '►', '▶', '›', '>').
        {size_t a=0;while(a<length && text[a]==u' ')++a;size_t e=length;while(e>a && text[e-1]==u' ')--e;
         auto mark=[](char16_t c) {return c==0x25ba || c==0x25b6 || c==0x203a || c==0x25b8;};
         bool lead=a<e && (mark(text[a]) || (text[a]==u'>' && a+1<e && text[a+1]==u' '));
         bool trail=e>a && (mark(text[e-1]) || (text[e-1]==u'>' && e-1>a && text[e-2]==u' '));
         const bool arrow=e>a+2 && text[e-1]==u'>' && text[e-2]==u'-' && text[e-3]!=u'-' && text[e-3]!=u' ';   // r397: Clover "GUI tuning->"
         if(arrow)trail=true;
         if((lead || trail) && e-a>2) {
            if(lead) {++a;while(a<e && text[a]==u' ')++a;}
            if(trail) {e-=arrow?2:1;while(e>a && text[e-1]==u' ')--e;}
            bool bracket=false,letter=false;for(size_t i=a;i<e;++i) {if(text[i]==u'[' || text[i]==u'<' || text[i]==u'(')bracket=true;if((text[i]|0x20)>=u'a' && (text[i]|0x20)<=u'z')letter=true;}
            if(!bracket && letter && e>a) {size_t at=0;out[0]=0;Put(out,at,capacity,text+a,e-a);if(roleAt)*roleAt=at;PutRole(out,at,capacity,Control::submenu,nullptr,0,pl);return true;}
         }}
        // Symbols first.
        for(size_t i=0;i<length;++i) {
            const char16_t c=text[i];Control kind=Control::none;
            if(c==0x2611 || c==0x2612 || c==0x2705)kind=Control::checked;
            else if(c==0x2610)kind=Control::unchecked;
            else if(c==0x25c9 || c==0x29bf)kind=Control::radioOn;
            else if(c==0x25cb)kind=Control::radioOff;
            if(kind==Control::none)continue;
            size_t at=0;char16_t label[maxColumns+1];size_t k=0;
            for(size_t j=0;j<length;++j)if(j!=i)label[k++]=text[j];label[k]=0;
            size_t a=0;while(label[a]==u' ')++a;size_t e=k;while(e>a && label[e-1]==u' ')--e;
            out[0]=0;Put(out,at,capacity,label+a,e-a);if(roleAt)*roleAt=at;
            PutRole(out,at,capacity,kind,nullptr,0,pl);return true;
        }
        // Only buttons on the row ("[ Yes ]  [ No ]"): each one in turn.
        {
            size_t at=0,count=0;bool onlyButtons=true;out[0]=0;
            for(size_t i=0;i<length && onlyButtons;) {
                if(text[i]==u' ') {++i;continue;}
                const char16_t o=text[i],c=o==u'['?u']':o==u'<'?u'>':o==u'|'?u'|':0;
                size_t close=i+1;while(c && close<length && text[close]!=c && close-i<36)++close;
                if(!c || close>=length || text[close]!=c || Classify(text,i,close,true)!=Control::button) {onlyButtons=false;break;}
                const char16_t* in=text+i+1;size_t n=close-i-1;while(n && in[0]==u' ') {++in;--n;}while(n && in[n-1]==u' ')--n;
                if(at)Put(out,at,capacity,u"\xE001" u", " u"\xE002");
                Put(out,at,capacity,in,n);PutRole(out,at,capacity,Control::button,nullptr,0,pl);++count;i=close+1;
            }
            if(onlyButtons && count) {if(roleAt)*roleAt=0;return true;}
        }
        for(size_t i=0;i<length;++i) {
            const char16_t o=text[i];
            const char16_t c=o==u'['?u']':o==u'<'?u'>':o==u'('?u')':0;
            if(!c)continue;
            size_t close=i+1;while(close<length && text[close]!=c && text[close]!=o && close-i<36)++close;
            if(close>=length || text[close]!=c)continue;
            size_t a=0;while(a<i && text[a]==u' ')++a;size_t e=length;while(e>close+1 && text[e-1]==u' ')--e;
            const bool alone=a==i && e==close+1;
            const Control kind=Classify(text,i,close,alone);
            if(kind==Control::none) {i=close;continue;}
            // Label: text before and after the token.
            size_t at=0;out[0]=0;
            size_t be=i;while(be>a && text[be-1]==u' ')--be;
            size_t as=close+1;while(as<e && text[as]==u' ')++as;
            if(kind==Control::button) {Put(out,at,capacity,text+i+1,close-i-1);
                size_t t=0;while(out[t]==u' ')++t;if(t) {size_t k=0;while(out[t+k]) {out[k]=out[t+k];++k;}out[k]=0;at=k;}
                while(at && out[at-1]==u' ')out[--at]=0;}
            else {
                Put(out,at,capacity,text+a,be-a);
                if(as<e) {if(at)Put(out,at,capacity,u" ");Put(out,at,capacity,text+as,e-as);}
            }
            if(roleAt)*roleAt=at;
            PutRole(out,at,capacity,kind,text+i+1,close-i-1,pl);
            return true;
        }
        return false;
    }
    // Add with controls described (screen text otherwise unchanged).
    void AddText(char16_t* speech,size_t capacity,const char16_t* text) {
        char16_t described[maxColumns*2+64];
        if(DescribeControls(text,described,sizeof(described)/2,options_.polish))Add(speech,capacity,described);
        else Add(speech,capacity,text);
    }
    // r382: icons of graphical pickers (rEFInd, Clover...): square tiles
    // blitted in a row. The selected tile has its own background (the
    // selection image) where an unselected one shows the screen behind it.
    struct IconTile {uint16_t x,y,w,h;bool lit,used;};
    IconTile icons_[24]={};
    size_t iconLabelLength_=0;   // r384: the label in front of pendingIcon_ (OpenCanopy)
    size_t iconSelected_=SIZE_MAX;
    char16_t pendingIcon_[64]={};   // label (at most 38) + ", " + "ikona" + position
    void ClearIcons() {for(auto& tile:icons_)tile={};iconSelected_=SIZE_MAX;pendingIcon_[0]=0;strip_={};stripSelected_=SIZE_MAX;}
    // r384: icons inside one wide blit. Columns with ink in the upper three
    // quarters (labels sit below) form the icons; the selected one sits on a
    // box whose four inner corners share one colour unlike the background.
    void UpdateStrip(const uint32_t* area,size_t width,size_t height,size_t left,size_t top) {
#if defined(HAK_CHARON_READER_TRACE) || defined(HAK_CHARON_POLL_DIAG)
        charontrace::Value("reader strip check",(uint64_t(strip_.used)<<63)|(uint64_t(strip_.x)<<32)|(uint64_t(left)<<16)|width);
#endif
        if(!strip_.used || strip_.x<left || strip_.y<top || size_t(strip_.x)+strip_.w>left+width || size_t(strip_.y)+strip_.h>top+height)return;
        auto px=[&](size_t x,size_t y) {return area[(y-top)*width+(x-left)]&0xffffff;};
        const size_t x0=strip_.x,y0=strip_.y,x1=x0+strip_.w,band=y0+strip_.h*3/4;
        const uint32_t bg=px(x0+1,y0+1);
        struct Group {size_t start,end,topY,bottomY;};Group groups[16];size_t count=0;
        size_t run=SIZE_MAX,gap=0;
        for(size_t x=x0;x<x1;++x) {
            bool ink=false;for(size_t y=y0;y<band && !ink;y+=2)ink=!SameColour(px(x,y),bg);
            if(ink) {if(run==SIZE_MAX)run=x;gap=0;}
            else if(run!=SIZE_MAX && ++gap>=4) {if(x-gap+1-run>=16 && count<16)groups[count++]={run,x-gap+1,0,0};run=SIZE_MAX;}
        }
        if(run!=SIZE_MAX && x1-run>=16 && count<16)groups[count++]={run,x1,0,0};
        if(count<2)return;
        size_t selected=SIZE_MAX;
        for(size_t g=0;g<count;++g) {
            Group& gr=groups[g];gr.topY=band;gr.bottomY=y0;
            for(size_t y=y0;y<band;++y)for(size_t x=gr.start;x<gr.end;x+=2)if(!SameColour(px(x,y),bg)) {if(y<gr.topY)gr.topY=y;if(y>gr.bottomY)gr.bottomY=y;break;}
            if(gr.bottomY<gr.topY+20 || gr.end-gr.start<24)continue;
            const uint32_t c=px(gr.start+6,gr.topY+6);
            const size_t boxBottom=gr.topY+(gr.end-gr.start)-7;  // the selection box is square; labels may follow below
            if(boxBottom<y0+strip_.h && !SameColour(c,bg) && SameColour(c,px(gr.end-7,gr.topY+6)) && SameColour(c,px(gr.start+6,boxBottom)) && SameColour(c,px(gr.end-7,boxBottom)))
                selected=selected==SIZE_MAX?g:SIZE_MAX-1;
        }
#if defined(HAK_CHARON_READER_TRACE) || defined(HAK_CHARON_POLL_DIAG)
        charontrace::Value("reader strip",(uint64_t(strip_.x)<<48)|(uint64_t(count)<<16)|(selected&0xffff));
#endif
        if(selected>=count || selected==stripSelected_)return;
        stripSelected_=selected;
        // The label under the selected icon, from the recognized text.
        size_t at=0;
        const size_t firstRow=(groups[selected].bottomY+1)/16,lastRow=(size_t(strip_.y)+strip_.h)/16,c0=groups[selected].start/8,c1=(groups[selected].end+7)/8;
        for(size_t row=firstRow;row<=lastRow && row<rows_ && at+2<40;++row)for(size_t c=c0;c<c1 && c<columns_ && at+2<40;++c) {
            const char16_t ch=text_[row][c];
            if(ch!=u' ' || (at && pendingIcon_[at-1]!=u' '))pendingIcon_[at++]=ch;
        }
        while(at && pendingIcon_[at-1]==u' ')--at;
        iconLabelLength_=at;
        if(at)pendingIcon_[at++]=u',';
        if(at)pendingIcon_[at++]=u' ';
        pendingIcon_[at]=0;
        const char16_t* word=options_.polish?u"ikona":u"icon";Put(pendingIcon_,at,sizeof(pendingIcon_)/2,word);
        // The position is built apart and appended bounded (a 38-character label ran past the buffer).
        if(options_.positions) {char16_t where[48];size_t k=0;where[k++]=u' ';Number(where,k,selected+1);where[k]=0;Put(where,k,48,options_.polish?u" z ":u" of ");Number(where,k,count);where[k]=0;Put(pendingIcon_,at,sizeof(pendingIcon_)/2,where);}
        if(!anyDirty_)firstDirty_=tick_;
        anyDirty_=true;
    }
    void ObserveIcon(size_t x,size_t y,size_t w,size_t h) {
        if(h>=64 && h<=320 && w>=h*2 && w<=4096 && x<=0xffff && y<=0xffff) {strip_={uint16_t(x),uint16_t(y),uint16_t(w),uint16_t(h),true};return;}
        // Inside a strip, its own analysis decides (tiles redrawn alone would
        // count only themselves).
        if(strip_.used && x+8>=strip_.x && x+w<=size_t(strip_.x)+strip_.w+8 && y+8>=strip_.y && y+h<=size_t(strip_.y)+strip_.h+8)return;
        if(w<32 || h<32 || w>320 || h>320 || w*3>h*4 || h*2>w*3 || x>0xffff || y>0xffff)return;  // r384: up to 1.5x as tall (icon with its label)
        IconTile* slot=nullptr;
        for(auto& tile:icons_)if(tile.used && tile.x==x && tile.y==y && tile.w==w && tile.h==h) {slot=&tile;break;}
        if(!slot) {
            // A tile drawn over another one of a different size replaces it.
            for(auto& tile:icons_)if(tile.used && x<size_t(tile.x)+tile.w && size_t(tile.x)<x+w && y<size_t(tile.y)+tile.h && size_t(tile.y)<y+h) {tile.used=false;}
            for(auto& tile:icons_)if(!tile.used) {slot=&tile;break;}
            if(!slot)return;
            *slot={uint16_t(x),uint16_t(y),uint16_t(w),uint16_t(h),false,true};
        }
    }
    static bool SameColour(uint32_t a,uint32_t b) {
        for(unsigned shift=0;shift<24;shift+=8) {const int d=int((a>>shift)&255)-int((b>>shift)&255);if(d>10 || d<-10)return false;}
        return true;
    }
    void UpdateIcons(const uint32_t* area,size_t width,size_t height,size_t left,size_t top) {
        bool changed=false;
        for(auto& tile:icons_) {
            if(!tile.used || tile.x<left+4 || tile.y<top || size_t(tile.x)+tile.w>left+width || size_t(tile.y)+tile.h>top+height)continue;
            // Inside the selection image (past its light rim), beside and above
            // the icon itself (icons sit centred with a margin).
            const size_t margin=tile.w/24>3?tile.w/24:3;
            const size_t row=tile.y-top+tile.h/2,inside=tile.x-left+margin,outside=tile.x-left-4;
            const uint32_t screen=area[row*width+outside]&0xffffff;
            const uint32_t side=area[row*width+inside]&0xffffff,head=area[(tile.y-top+margin)*width+tile.x-left+tile.w/2]&0xffffff;
            const bool lit=!SameColour(side,screen) && !SameColour(head,screen);
#ifdef HAK_CHARON_READER_TRACE
            charontrace::Value("reader icon",(uint64_t(tile.x)<<48)|(uint64_t(tile.y)<<32)|(uint64_t(tile.w)<<16)|(lit?1:0));
            charontrace::Value("reader icon colours",(uint64_t(screen)<<40)|(uint64_t(side)<<16)|(head>>8));
#endif
            if(lit!=tile.lit) {tile.lit=lit;changed=true;}
        }
        if(!changed)return;
        size_t selected=SIZE_MAX,litCount=0;
        for(size_t i=0;i<24;++i)if(icons_[i].used && icons_[i].lit) {selected=i;++litCount;}
        if(litCount!=1 || selected==iconSelected_)return;
        // Position in its row: tiles of the same height on the same line.
        size_t index=0,count=0;
        for(const auto& tile:icons_)if(tile.used && tile.h==icons_[selected].h && tile.y==icons_[selected].y) {
            ++count;if(tile.x<icons_[selected].x)++index;}
        if(count<2)return;
        iconSelected_=selected;iconLabelLength_=0;
        size_t at=0;const char16_t* word=options_.polish?u"ikona":u"icon";Put(pendingIcon_,at,sizeof(pendingIcon_)/2,word);
        if(options_.positions) {char16_t where[48];size_t k=0;where[k++]=u' ';Number(where,k,index+1);where[k]=0;Put(where,k,48,options_.polish?u" z ":u" of ");Number(where,k,count);where[k]=0;Put(pendingIcon_,at,sizeof(pendingIcon_)/2,where);}
        if(!anyDirty_)firstDirty_=tick_;
        anyDirty_=true;
    }
    // r384: the first number that differs between two renderings of a line.
    static long ChangedNumber(const char16_t* a,const char16_t* b) {
        for(;;) {
            while(*a && !(*a>=u'0' && *a<=u'9'))++a;
            while(*b && !(*b>=u'0' && *b<=u'9'))++b;
            if(!*a || !*b)return -1;
            long x=0,y=0;
            while(*a>=u'0' && *a<=u'9') {x=x*10+(*a-u'0');++a;if(x>100000)return -1;}
            while(*b>=u'0' && *b<=u'9') {y=y*10+(*b-u'0');++b;if(y>100000)return -1;}
            if(x!=y)return y<x?y:-1;  // counting down only (progress counts up)
        }
    }
    static bool Same16(const char16_t* a,const char16_t* b) {for(;*a==*b;++a,++b)if(!*a)return true;return false;}
    static bool SameIgnoringDigits(const char16_t* a,const char16_t* b) {
        for(;;) {
            while(*a>=u'0' && *a<=u'9')++a;
            while(*b>=u'0' && *b<=u'9')++b;
            if(*a!=*b)return false;
            if(!*a)return true;
            ++a;++b;
        }
    }
    // Last "N%" (1-3 digits, optional space) on a row, 0..100, or -1.
    static int Percent(const char16_t* line) {
        int found=-1;
        for(size_t i=0;line[i];++i) {
            if(line[i]!=u'%')continue;
            size_t end=i;if(end && line[end-1]==u' ')--end;
            size_t start=end;while(start && end-start<3 && line[start-1]>=u'0' && line[start-1]<=u'9')--start;
            if(start==end || (start && line[start-1]>=u'0' && line[start-1]<=u'9'))continue;
            int value=0;for(size_t k=start;k<end;++k)value=value*10+(line[k]-u'0');
            if(value<=100)found=value;
        }
        return found;
    }
    // NVDA progress beep: 110 Hz * 2^(percent/25), 40 ms, one beep per change.
    static unsigned ProgressHertz(int percent) {return progressfb::Hertz(percent);}   // r401: shared with Charon's own tasks (CharonProgress.h)
    void Progress(int percent) {
        charontrace::Value("reader progress",uint64_t(percent));
        if(!options_.progress || options_.reviewOnly || percent==lastPercent_)return;
        const int previous=lastPercent_;lastPercent_=percent;
        if((options_.progress&1) && tick_>=endToneUntil_ && options_.effects && options_.effects->Tone)  // r384: never cut the countdown's end tone
           
            options_.effects->Tone(options_.effects->context,ProgressHertz(percent),progressfb::toneMs);
        if((options_.progress&2) && progressfb::SpeakAt(previous,percent)) {
            if(percent>=100)Raise(formatting::Event::taskDone);   // r397
            char16_t text[64]={};progressfb::Phrase(percent,options_.polish,options_.explicitMessages,unsigned(ExplicitVariety()),text);Speak(text);
        }
    }
    static void Number(char16_t* out,size_t& at,size_t value) {
        char16_t digits[20];size_t n=0;do {digits[n++]=char16_t(u'0'+value%10);value/=10;}while(value && n<20);
        while(n)out[at++]=digits[--n];
    }
    static void Append16(char16_t* out,size_t capacity,const char16_t* text) {
        size_t at=0;while(at+1<capacity && out[at])++at;
        while(at+1<capacity && *text)out[at++]=*text++;
        out[at]=0;
    }
    // r397 (user): more selection marks before an entry, each followed by a
    // space: "->", "=>", ">>", "»", "→", "⇒", "➜", "➤" (not "(*)" / "[*]": radio buttons and check boxes) (and the box/arrow
    // block 0x2500-0x25ff: "►", "▶", "●", "◆" as before). Returns its length.
    size_t LeadMark(size_t r,size_t c) const {
        if(c>=columns_)return 0;
        const char16_t a=text_[r][c],b=c+1<columns_?text_[r][c+1]:u' ',d=c+2<columns_?text_[r][c+2]:u' ';
        if((a==u'-' || a==u'=' || a==u'>') && b==u'>' && d==u' ')return 2;
        if((a==0xbb || a==0x2192 || a==0x21d2 || a==0x279c || a==0x27a4) && b==u' ')return 1;
        return 0;
    }
    // r397: a mark after an entry ("Windows  <", "◄", "«") that only one row has.
    static bool TrailMark(char16_t ch) {return ch==u'<' || ch==0x25c4 || ch==0x25c0 || ch==0xab || ch==0x2190;}
    size_t TextStart(size_t r) const {
        for(size_t c=0;c<columns_;++c) {
            const char16_t ch=text_[r][c];
            if(ch==u'>' && c+1<columns_ && text_[r][c+1]==u' ')continue;  // r386: OpenCore's "> " selection mark
            if(const size_t m=LeadMark(r,c)) {c+=m-1;continue;}            // r397
            if(ch!=u' ' && ch!=u'*' && !(ch>=0x2500 && ch<=0x25ff))return c;
        }
        return SIZE_MAX;
    }
    // r386: GRUB's text menu marks the selected entry with '*' right before
    // its title ("*Advanced options ..."), the others with a space. On a
    // picture background (gfxterm wallpaper) that is the only selection mark.
    bool StarRow(size_t r,char16_t mark=u'*') const {
        size_t c=0;while(c<columns_ && (text_[r][c]==u' ' || (text_[r][c]>=0x2500 && text_[r][c]<=0x25ff)))++c;
        if(c+1>=columns_ || text_[r][c]!=mark || text_[r][c+1]==mark)return false;
        // r386: OpenCore's built-in picker: "> 2. GRUBTEST" is the selection,
        // "* 1. CHARON" the default (a space after the mark). Only one row of
        // the menu may carry a mark (a list of bullets is no selection), and
        // another row must start where its text starts.
        const size_t start=TextStart(r);
        if(start==SIZE_MAX || start<=c)return false;
        // Scaled cell fonts (OpenCore at UIScale 2) put text one shadow cell
        // per glyph, so the rows' starts may differ by a few cells.
        bool other=false;
        for(size_t o=0;o<rows_;++o) {
            if(o==r)continue;
            size_t oc=0;while(oc<columns_ && (text_[o][oc]==u' ' || (text_[o][oc]>=0x2500 && text_[o][oc]<=0x25ff)))++oc;
            if(oc<columns_ && text_[o][oc]==mark && (oc>c?oc-c:c-oc)<=4)return false;
            const size_t os=TextStart(o);
            if(os!=SIZE_MAX && (os>start?os-start:start-os)<=4 && o+4>=r && o<=r+4)other=true;
        }
        return other;
    }
    // r397: a selection by one of the marks LeadMark knows, or by a mark at
    // the row's end; only one row of the menu may carry it, and an aligned
    // row without it must be near (as StarRow).
    bool MarkRow(size_t r) const {
        size_t c=0;while(c<columns_ && text_[r][c]==u' ')++c;
        size_t len=LeadMark(r,c);
        auto sameKind=[&](size_t o,size_t oc) {for(size_t k=0;k<len && k<3;++k)if(oc+k>=columns_ || text_[o][oc+k]!=text_[r][c+k])return false;return true;};
        const size_t start=TextStart(r);if(start==SIZE_MAX)return false;
        bool trailing=false;size_t end=0;
        if(!len) {
            end=columns_;while(end && text_[r][end-1]==u' ')--end;
            if(!end || !TrailMark(text_[r][end-1]) || end<start+3 || text_[r][end-2]!=u' ')return false;
            trailing=true;
        }
        bool other=false;
        for(size_t o=0;o<rows_;++o) {
            if(o==r)continue;
            if(trailing) {size_t oe=columns_;while(oe && text_[o][oe-1]==u' ')--oe;if(oe && text_[o][oe-1]==text_[r][end-1])return false;}
            else {size_t oc=0;while(oc<columns_ && text_[o][oc]==u' ')++oc;if(LeadMark(o,oc)==len && sameKind(o,oc) && (oc>c?oc-c:c-oc)<=4)return false;}
            const size_t os=TextStart(o);
            if(os!=SIZE_MAX && (os>start?os-start:start-os)<=4 && o+4>=r && o<=r+4)other=true;
        }
        return other;
    }
    // r389: "► CPU Configuration" and "Flash Update" are one list (Aptio):
    // the submenu mark takes the two cells before the text.
    bool Aligned(size_t x,size_t column,size_t row) const {
        const size_t s=TextStart(x);if(s==SIZE_MAX)return false;if(s==column)return true;
        auto mark=[&](size_t r,size_t c) {const char16_t ch=text_[r][c];return ch==0x25ba || ch==0x25b6 || ch==0x25b8;};
        if(s==column+2)return mark(x,column);
        if(s+2==column)return mark(row,s);
        return false;
    }
    void MenuBlock(size_t row,size_t& index,size_t& count) const {
        const size_t column=TextStart(row);index=0;count=0;
        if(column==SIZE_MAX)return;
        // r390: console menus: the items are the aligned rows in the colour
        // of a not highlighted neighbour item (a header above and help text
        // below in the same column have another colour: edk2's Boot Manager
        // page), up to two blank rows apart (OVMF's front page: Continue).
        {
            auto fg=[&](size_t x)->int {const size_t c=TextStart(x);if(c==SIZE_MAX || !(attr_[x][c]&0x80) || Highlighted(attr_[x][c]))return -1;return attr_[x][c]&0x0f;};
            auto aligned=[&](size_t x) {return x<rows_ && Aligned(x,column,row);};
            // r397: a greyed-out item (dark grey, ThinkPad's "Intel AMT") stays in its menu
            auto same=[](int a,int b) {return a>=0 && b>=0 && (a==b || a==8 || b==8);};
            int itemFg=-1;
            for(size_t d=1;d<=3 && itemFg<0;++d) {
                if(row>=d && aligned(row-d))itemFg=fg(row-d);
                if(itemFg<0 && aligned(row+d))itemFg=fg(row+d);
            }
            if(itemFg>=0) {
                // r392: a row with text only far right of the menu column (a
                // help panel's wrapped line) is a gap for the menu.
                auto gapRow=[&](size_t x) {const size_t t=TextStart(x);return t==SIZE_MAX || t>column+8;};
                size_t top=row;
                for(;;) {size_t k=1;while(k<=3 && top>=k && gapRow(top-k))++k;
                    if(k>3 || top<k || !aligned(top-k) || !same(fg(top-k),itemFg))break;top-=k;}
                size_t found=0,position=0;
                for(size_t r=top,gap=0;r<rows_;++r) {
                    if(r==row || (aligned(r) && same(fg(r),itemFg))) {if(r==row)position=found;++found;gap=0;}
                    else if(gapRow(r)) {if(++gap>2)break;}
                    else break;
                }
                if(found>=2) {index=position;count=found;return;}
            }
        }
        // Items aligned in one column, possibly one or two empty rows apart
        // (graphical menus spaced wider than the 19-pixel text grid).
        size_t items[64],found=0,position=0;
        size_t r=row;
        for(;;) {
            size_t step=1;while(step<=3 && r>=step && TextStart(r-step)==SIZE_MAX)++step;
            if(step>3 || r<step || !Aligned(r-step,column,row))break;
            r-=step;
        }
        for(size_t gap=0;r<rows_ && found<64;++r) {
            const size_t start=TextStart(r);
            if(Aligned(r,column,row)) {if(r==row)position=found;items[found++]=r;gap=0;}
            else if(start==SIZE_MAX) {if(++gap>2)break;}
            else break;
        }
        // r389: a header or a hint in the same column ("Devices List",
        // "Press ESC to exit." in edk2 forms) has another text colour than the
        // items: only rows in the items' colour are counted.
        if(found>=3) {
            auto fg=[&](size_t x)->int {const size_t c=TextStart(x);if(c==SIZE_MAX || !(attr_[x][c]&0x80))return -1;return attr_[x][c]&0x0f;};
            int counts[16]={};for(size_t i=0;i<found;++i)if(items[i]!=row && fg(items[i])>=0)++counts[fg(items[i])];
            int best=0;for(int k=1;k<16;++k)if(counts[k]>counts[best])best=k;
            size_t kept=0,keptPosition=0;
            for(size_t i=0;i<found;++i)if(items[i]==row || fg(items[i])==best || fg(items[i])<0) {if(items[i]==row)keptPosition=kept;++kept;}
            if(counts[best]>=1 && kept>=2 && kept<found) {position=keptPosition;found=kept;}
        }
        index=position;count=found;
        if(count>=2)return;
        // r363: centred items on consecutive rows (systemd-boot); a rule or a
        // blank row ends the menu (the countdown below it is no item).
        const size_t centre=TextCentre(row);
        r=row;while(r && TextStart(r-1)!=SIZE_MAX && Near(TextCentre(r-1),centre))--r;
        found=0;position=0;
        for(;r<rows_ && TextStart(r)!=SIZE_MAX && Near(TextCentre(r),centre);++r) {if(r==row)position=found;++found;}
        if(found>=2) {index=position;count=found;return;}
        // r386: a row with a selection mark ("> 2. GRUBTEST") in a scaled cell
        // font: its neighbours start up to 4 cells away.
        if(!(StarRow(row,u'>') || StarRow(row))) return;
        auto near=[&](size_t x) {const size_t s=TextStart(x);return s!=SIZE_MAX && (s>column?s-column:column-s)<=4;};
        r=row;
        for(;;) {
            size_t step=1;while(step<=3 && r>=step && TextStart(r-step)==SIZE_MAX)++step;
            if(step>3 || r<step || !near(r-step))break;
            r-=step;
        }
        found=0;position=0;
        for(size_t gap=0;r<rows_;++r) {
            if(near(r)) {if(r==row)position=found;++found;gap=0;}
            else if(TextStart(r)==SIZE_MAX) {if(++gap>2)break;}
            else break;
        }
        if(found>=2) {index=position;count=found;}
    }
    // Twice the centre column of a row's text (SIZE_MAX when blank).
    size_t TextCentre(size_t r) const {
        const size_t start=TextStart(r);
        if(start==SIZE_MAX)return SIZE_MAX;
        size_t end=start;
        for(size_t c=start;c<columns_;++c) {const char16_t ch=text_[r][c];if(ch!=u' ' && ch && !(ch>=0x2500 && ch<=0x25ff))end=c;}
        return start+end;
    }
    static bool Near(size_t a,size_t b) {return a!=SIZE_MAX && b!=SIZE_MAX && (a>b?a-b:b-a)<=2;}
    bool ScreenHas(const char16_t* needle) const {
        for(size_t r=0;r<rows_;++r)for(size_t c=0;c<columns_;++c) {
            size_t k=0;while(needle[k] && c+k<columns_ && text_[r][c+k]==needle[k])++k;
            if(!needle[k])return true;
        }
        return false;
    }
    // Identification only from what the program itself shows on screen.
    const char16_t* ProgramName() const {
        const bool pl=options_.polish;
        // r363: the boot manager recognized when it started comes first (its
        // menu may list another one, e.g. Limine's "Windows Boot Manager").
        if(bootManager_ && bootManager_[0]==u'W')return pl?u"Menedżer rozruchu Windows":u"Windows Boot Manager";
        if(bootManager_)return bootManager_;
        if(ScreenHas(u"GNU GRUB"))return u"GRUB";
        if(ScreenHas(u"Windows Boot Manager") || ScreenHas(u"Menedżer rozruchu systemu Windows"))return pl?u"Menedżer rozruchu Windows":u"Windows Boot Manager";
        if(ScreenHas(u"rEFInd"))return u"rEFInd";
        if(ScreenHas(u"systemd-boot"))return u"systemd-boot";
        if(ScreenHas(u"Haiku"))return u"Haiku";
        if(ScreenHas(u"Device Manager") || ScreenHas(u"Boot Maintenance Manager"))return pl?u"Ustawienia firmware":u"Firmware setup";
        if(ScreenHas(u"Boot Manager") || ScreenHas(u"Boot Menu") || ScreenHas(u"Boot menu"))return pl?u"Menedżer rozruchu":u"Boot manager";
        return pl?u"Program":u"Program";
    }
    static void Add(char16_t* out,size_t capacity,const char16_t* text) {
        size_t at=0;while(at+1<capacity && out[at])++at;
        if(at && at+4<capacity) {
            // r390: the separator is the reader's (no "dot" at punctuation level all).
            size_t k=at;while(k && (out[k-1]==speechtext::ownClose || out[k-1]==u' '))--k;
            const char16_t last=k?out[k-1]:0;
            out[at++]=speechtext::ownOpen;
            if(last!=u'.' && last!=u'!' && last!=u'?' && last!=u':' && last!=u';' && last!=u',')out[at++]=u'.';
            out[at++]=u' ';out[at++]=speechtext::ownClose;
        }
        while(at+1<capacity && *text)out[at++]=*text++;
        out[at]=0;
    }
    struct Segment {size_t start,end;};
    static bool Blank(char16_t ch) {return !ch || ch==u' ' || (ch>=0x2500 && ch<=0x257f);}
    // r386: ASCII art (FreeBSD's logo and mascot beside its menu, banners):
    // a part of at least 6 visible characters of which under 40% are letters
    // or digits is a picture, not text ("___" in it is no edit field).
    static bool ArtText(const char16_t* text) {
        size_t visible=0,words=0;
        for(const char16_t* p=text;*p;++p) {
            if(*p==u' ')continue;
            ++visible;
            if((*p>=u'0' && *p<=u'9') || (*p>=u'A' && *p<=u'Z') || (*p>=u'a' && *p<=u'z') || (*p>=0xc0 && *p<0x2500))++words;
        }
        if(visible>=6 && words*10<visible*4)return true;
        // Scraps with a letter or two among symbols ("+o", "y/", "yo`:").
        {size_t letters=0,digits=0;for(const char16_t* p=text;*p;++p) {if((*p>=u'a' && *p<=u'z') || (*p>=u'A' && *p<=u'Z'))++letters;else if(*p>=u'0' && *p<=u'9')++digits;}
         if(visible<=6 && letters && letters<=2 && !digits && visible-letters>=letters) {bool bracket=false;for(const char16_t* p=text;*p;++p)if(*p==u'[' || *p==u']' || *p==u'(' || *p==u')' || *p==u'<' || *p==u'>' || *p==u'_')bracket=true;if(!bracket)return true;}}
        // Short scraps of a picture ("```", "-/", "+."): symbols only, no
        // underscores (an empty form field) and no brackets (check boxes).
        if(!words && visible==1) {for(const char16_t* p=text;*p;++p)if(*p==u'`' || *p==u'\'' || *p==u'/' || *p==u'\\' || *p==u'.' || *p==u':' || *p==u'+' || *p==u'~' || *p==u'^' || *p==u',')return true;}
        if(!words && visible>=2) {for(const char16_t* p=text;*p;++p)if(*p==u'_' || *p==u'[' || *p==u']' || *p==u'(' || *p==u')' || *p==u'<' || *p==u'>')return false;return true;}
        return false;
    }
    bool ArtRow(size_t r) const {
        char16_t row[maxColumns+1];CellsText(text_[r],attr_[r],0,columns_,false,row,maxColumns+1);
        return ArtText(row);
    }
    size_t Segments(size_t r,Segment* out,size_t maximum) const {
        size_t count=0,c=0;
        while(c<columns_ && count<maximum) {
            while(c<columns_ && Blank(text_[r][c]))++c;
            if(c>=columns_)break;
            const size_t start=c;size_t end=c,gap=0;
            while(c<columns_ && gap<3) {if(Blank(text_[r][c]))++gap;else {gap=0;end=c+1;}++c;}
            out[count++]={start,end};c=end;
        }
        return count;
    }
    bool SegmentAt(size_t r,size_t start,Segment& found) const {
        Segment list[40];const size_t n=Segments(r,list,40);
        for(size_t i=0;i<n;++i)if(list[i].start==start) {found=list[i];return true;}
        return false;
    }
    bool SegmentChanged(size_t r,const Segment& s) const {
        for(size_t c=s.start;c<s.end;++c)if(text_[r][c]!=prevRaw_[r][c])return true;
        return false;
    }
    // r388: tab bars of firmware setups (AMI Aptio, Insyde H2O, Phoenix:
    // "Main  Advanced  Boot  Security  Save & Exit" switched with Left and
    // Right). One of the first four text rows holds 3 to 12 short names
    // separated by two spaces or by a change of background, and exactly one
    // of them has a background other than the bar's own: the open tab.
    struct TabBar {size_t row,index,count;char16_t name[40];};
    bool FindTabBar(TabBar& tab) const {
        size_t seen=0;
        for(size_t r=0;r<rows_ && seen<4;++r) {
            if(TextStart(r)==SIZE_MAX)continue;
            ++seen;
            size_t counts[8]={};bool frame=false;
            for(size_t c=0;c<columns_;++c) {
                if(attr_[r][c]&0x80)++counts[(attr_[r][c]>>4)&7];
                if(text_[r][c]>=0x2500 && text_[r][c]<=0x257f)frame=true;
            }
            if(frame)continue;
            uint8_t bg=0;for(uint8_t b=1;b<8;++b)if(counts[b]>counts[bg])bg=b;
            auto lit=[&](size_t c) {return (attr_[r][c]&0x80) && ((attr_[r][c]>>4)&7)!=bg;};
            size_t groups=0,litGroups=0,litIndex=0,litStart=0,litEnd=0;bool tooLong=false;
            for(size_t c=0;c<columns_;) {
                while(c<columns_ && text_[r][c]==u' ')++c;
                if(c>=columns_)break;
                const size_t start=c;const bool on=lit(c);size_t end=c+1;c=end;
                while(c<columns_) {
                    if(text_[r][c]!=u' ') {if(lit(c)!=on)break;end=++c;continue;}
                    if(c+1<columns_ && text_[r][c+1]!=u' ' && lit(c+1)==on) {++c;continue;}   // one space inside a name
                    break;
                }
                if(end-start>16)tooLong=true;   // r389: tab names are short ("Save & Exit"); help text runs longer
                {bool letter=false;for(size_t k=start;k<end;++k)if((text_[r][k]|0x20)>=u'a' && (text_[r][k]|0x20)<=u'z')letter=true;if(!letter)tooLong=true;}   // "►" alone
                // r389: a value in a form ("<Standard English>", "[Enabled]") is no tab.
                for(size_t k=start;k<end;++k)if(text_[r][k]==u'<' || text_[r][k]==u'>' || text_[r][k]==u'[' || text_[r][k]==u']')tooLong=true;
                if(on) {++litGroups;litIndex=groups;litStart=start;litEnd=end;}
                ++groups;
            }
            if(tooLong || groups<3 || groups>12 || litGroups!=1)continue;
            if(litEnd-litStart<2)continue;   // r396: one lit cell inside a word is a drawn caret (Limine's editor), not a tab
            tab.row=r;tab.index=litIndex;tab.count=groups;
            size_t n=0;for(size_t c=litStart;c<litEnd && n+1<40;++c)tab.name[n++]=text_[r][c];
            tab.name[n]=0;return true;
        }
        return false;
    }
    bool FrameHasDirty(size_t top) const {
        const size_t column=FrameColumn(top);if(column==SIZE_MAX)return false;
        for(size_t b=top+1;b<rows_;++b) {
            if(dirty_[b])return true;
            const char16_t ch=text_[b][column];
            if(ch==0x2514 || ch==0x255a || ch==0x2570 || ch==0x2558 || ch==0x2559)return false;
        }
        return false;
    }
    // True when 'part' (case and spacing aside) is already in the reader's speech.
    // r390: the same text on the same row said in the last 5 s (editors
    // redraw their status bar after every key).
    struct RecentInfo {uint32_t hash;size_t row;uint64_t tick;};
    RecentInfo recentInfo_[8]={};size_t recentNext_=0;
    bool RepeatedInfo(size_t row,const char16_t* text) {
        uint32_t h=2166136261u;for(size_t i=0;text[i];++i) {if(text[i]>=u'0' && text[i]<=u'9')continue;h=(h^text[i])*16777619u;}   // digits (positions) ignored
        for(const auto& e:recentInfo_)if(e.hash==h && e.row==row && e.tick && tick_-e.tick<500)return true;
        recentInfo_[recentNext_++%8]={h,row,tick_};
        return false;
    }
    static bool SaidAlready(const char16_t* speech,const char16_t* part) {
        auto low=[](char16_t c) {return (c>=u'A' && c<=u'Z')?char16_t(c+32):c;};
        size_t n=0;while(part[n])++n;while(n && part[n-1]==u' ')--n;
        size_t s0=0;while(s0<n && part[s0]==u' ')++s0;
        if(n-s0<2)return false;
        for(size_t i=0;speech[i];++i) {
            size_t k=s0,j=i;
            while(k<n && speech[j] && low(speech[j])==low(part[k])) {++k;++j;}
            if(k==n)return true;
        }
        return false;
    }
    bool SegmentsChanged(size_t r) const {for(size_t c=0;c<columns_;++c)if(text_[r][c]!=(prevRaw_[r][c]?prevRaw_[r][c]:u' '))return true;return false;}
    bool EditEcho(size_t r,char16_t* speech,size_t capacity) {
        char16_t a[maxColumns+1],b[maxColumns+1];
        size_t la=0,lb=0;
        for(size_t c=0;c<columns_;++c) {a[c]=text_[r][c]?text_[r][c]:u' ';b[c]=prevRaw_[r][c]?prevRaw_[r][c]:u' ';}
        la=columns_;while(la && a[la-1]==u' ')--la;
        lb=columns_;while(lb && b[lb-1]==u' ')--lb;
        if(!lb && !la)return false;
        if(!lb && la>2)return false;   // a new line of output, not an edit
        size_t p=0;while(p<la && p<lb && a[p]==b[p])++p;
        size_t q=0;while(q<la-p && q<lb-p && a[la-1-q]==b[lb-1-q])++q;
        const size_t added=la-p-q,removed=lb-p-q;
        if(!added && !removed)return false;
        if(added>40 || removed>40)return false;  // a redraw (scrolled line), read normally
        char16_t said[maxColumns+16];size_t at=0;
        const char16_t* from=added?a+p:b+p;size_t n=added?added:removed;
        // "grub>" + "l" reads as " l" (the prompt's space was trimmed): only
        // the typed text, unless spaces are all there is.
        {size_t lead=0;while(lead<n && from[lead]==u' ')++lead;
         if(lead<n) {from+=lead;n-=lead;while(n && from[n-1]==u' ')--n;}}
        // r390: typed characters and typed words are settings (NVDA's
        // keyboard echo); a typed space or punctuation ends a word.
        const bool chars=(options_.readerFlags&readerFlagTyped)!=0,words=(options_.readerFlags&readerFlagWords)!=0;
        said[0]=0;
        if(n==1 && added && words && !speechtext::IsWord(from[0]) && p) {
            size_t e=p;size_t s0=e;while(s0 && speechtext::IsWord(a[s0-1]))--s0;
            for(size_t i=s0;i<e && at+1<maxColumns+16;++i)said[at++]=a[i];said[at]=0;
        }
        if(n==1 && (chars || !added)) {
            char16_t name[64];CharName(from[0],name,64,options_.polish);
            if(at && at+2<maxColumns+16)said[at++]=u' ';
            said[at++]=speechtext::ownOpen;for(size_t i=0;name[i] && at+2<maxColumns+16;++i)said[at++]=name[i];said[at++]=speechtext::ownClose;said[at]=0;
        }
        else if(n>1 && (chars || !added)) {for(size_t i=0;i<n && at+1<maxColumns+16;++i)said[at++]=from[i];said[at]=0;}
        // A deleted space before the cursor at a line end: "spacja".
        if(said[0])Add(speech,capacity,said);
        EchoFromCaret();keyPending_=false;
        return true;
    }
    // r397 Emacspeak event icons: the event of this read (one per utterance;
    // a warning wins). Spoken with the next utterance, then cleared.
    formatting::Event event_=formatting::Event::none,infoEvent_=formatting::Event::none;size_t lastMenuIndex_=SIZE_MAX,lastMenuCount_=0;bool lastCursorShown_=false,cursorSeen_=false,cursorAppeared_=false,screenReplaced_=false,menuMoved_=false,enterPending_=false;uint64_t enterTick_=0;
    void Raise(formatting::Event e) {if(event_==formatting::Event::none || e==formatting::Event::warnUser)event_=e;}
    bool EventsOn() const {return (options_.formatFlags&formatting::flagEvents)!=0;}
    // r397: rows the pixel OCR wrote since the last read.
    bool ocrRow_[maxRows]={};
    size_t usedColumns_=80;   // r397: one past the rightmost column anything was drawn in
    void Flush() {
        // r401: beside Charon (reviewOnly) nothing is read automatically, and the
        // review keys read the screen model, which the console hook keeps: the
        // changed rows are just taken note of. No analysis at raised priority
        // while Charon's own speech is playing.
        if(options_.reviewOnly) {
            for(size_t r=0;r<rows_ && r<maxRows;++r)dirty_[r]=false;
            anyDirty_=false;menuIntro_=false;menuMoved_=false;keyPending_=false;return;
        }
        event_=formatting::Event::none;
        if(secret_.Entry())SecretEcho();   // r407: a password being typed is never read; one cue per character
        {size_t used=0;for(size_t r=0;r<rows_;++r)for(size_t c=used;c<columns_;++c)if(attr_[r][c]&0x80)used=c+1;usedColumns_=used?used:columns_;}
        // r397: a row the OCR reads as the old text with many cells changed is the old
        // text half painted over (Syslinux hides its countdown after Tab: "An omn ic hoo
        // in 54 oocondo"), not new text: it is not read.
        for(size_t r=0;r<rows_ && r<maxRows;++r) {
            if(!ocrRow_[r] || !dirty_[r])continue;
            // against its own old text, and against console text on the rows around it (the
            // console's own glyphs read again on the OCR's 16-pixel grid)
            auto similar=[&](const char16_t* other,bool consoleOnly,size_t q) {
                size_t same=0,diff=0;
                for(size_t c=0;c<columns_;++c) {const char16_t a=text_[r][c],b=other[c];if(a==u' ' && b==u' ')continue;
                    if(consoleOnly && b!=u' ' && cellFont_[q][c])continue;if(a==b)++same;else ++diff;}
                return same>=5 && diff>=3 && same*10>=(same+diff)*4;
            };
            // (only console text: a picture-only program's own rows, e.g. Clover's selection drawn on another
            // background, are read again with small differences and are real changes)
            bool ghost=false;(void)similar;
            // the same words drawn elsewhere (any column): best alignment of the two trimmed rows
            auto shifted=[&](size_t q) {
                size_t a0=0,a1=columns_,b0=0,b1=columns_;
                while(a0<a1 && text_[r][a0]==u' ')++a0;while(a1>a0 && text_[r][a1-1]==u' ')--a1;
                while(b0<b1 && text_[q][b0]==u' ')++b0;while(b1>b0 && text_[q][b1-1]==u' ')--b1;
                const size_t na=a1-a0,nb=b1-b0;if(na<8 || nb<8)return false;
                size_t letters=0;for(size_t i=a0;i<a1;++i)if(text_[r][i]!=u' ')++letters;
                for(size_t s=0;s+4<na+nb;++s) {   // B shifted against A by s-nb
                    size_t same=0,diff=0;
                    for(size_t i=0;i<na;++i) {const long j=long(i)+long(nb)-long(s);if(j<0 || size_t(j)>=nb)continue;
                        const char16_t x=text_[r][a0+i],y=text_[q][b0+size_t(j)];if(x==u' ' || y==u' ')continue;if(x==y)++same;else ++diff;}
                    if(same>=6 && diff>=3 && same*10>=letters*4)return true;
                }
                return false;
            };
            auto console=[&](size_t q) {size_t n=0;for(size_t c=0;c<columns_;++c)if(text_[q][c]!=u' ') {if(cellFont_[q][c] || !(attr_[q][c]&0x80))return false;++n;}return n>=8;};
            for(size_t q=0;q<rows_ && !ghost;++q)if(q!=r && !ocrRow_[q] && console(q))ghost=shifted(q);
            if(ghost) {for(size_t c=0;c<columns_;++c)prevRaw_[r][c]=text_[r][c];dirty_[r]=false;
#ifdef HAK_CHARON_READER_TRACE
                charontrace::Value("reader ocr ghost",r);
#endif
            }
        }
        for(auto& o:ocrRow_)o=false;
        majorityBg_=MajorityBackground();
        // r397 Speakup enable_win: changes inside a silenced window are not read.
        if(winSilent_ && winTop_!=SIZE_MAX && !winCorner_)for(size_t r=winTop_;r<=winBottom_ && r<rows_;++r) {
            if(!dirty_[r])continue;bool outside=false;
            for(size_t c=0;c<columns_ && !outside;++c)if((c<winLeft_ || c>winRight_) && text_[r][c]!=prevRaw_[r][c])outside=true;
            if(!outside)dirty_[r]=false;
        }
        // r397 events from the whole screen: a password prompt, a help page, an editor's caret appearing.
        if(EventsOn()) {
            for(size_t r=0;r<rows_;++r) {
                if(!dirty_[r])continue;
                char16_t row[maxColumns+1];if(!RowText(r,row,maxColumns+1))continue;
                if(HasWord(row,u"password") || HasWord(row,u"hasło") || HasWord(row,u"passphrase") || HasWord(row,u"pin:"))Raise(formatting::Event::password);
                else if(KeyRecent() && (keyScan_==0x0b || keyChar_==u'h' || keyChar_==u'?') && (HasWord(row,u"help") || HasWord(row,u"pomoc")))Raise(formatting::Event::help);
            }
            // an editor's caret that appeared after a key (decided at the end: a menu's selection wins)
            const bool shown=CursorShown();cursorAppeared_=shown && !lastCursorShown_ && KeyRecent() && cursorSeen_;
            lastCursorShown_=shown;cursorSeen_=true;
            size_t dirtyRows=0,textRows=0;for(size_t r=0;r<rows_;++r) {if(dirty_[r])++dirtyRows;if(TextStart(r)!=SIZE_MAX)++textRows;}
            screenReplaced_=KeyRecent() && textRows>=3 && dirtyRows*2>=textRows;
            if(scrolledOut_[0] || (lastScroll_ && tick_-lastScroll_<20))Raise(formatting::Event::scroll);
            // a terminal's prompt back at the caret after Enter (UEFI Shell "FS0:\>", "grub>", "OK", "iPXE>", "boot:"): the command finished
            if(enterPending_ && row_<rows_ && dirty_[row_] && CursorShown()) {
                size_t e=columns_;while(e && text_[row_][e-1]==u' ')--e;
                const char16_t last=e?text_[row_][e-1]:0;
                if(e && e<=40 && (last==u'>' || last==u'$' || last==u'#' || last==u':') && column_+2>=e) {
                    if(tick_-enterTick_>=30)Raise(formatting::Event::taskDone);   // a command that took 300 ms or more
                    enterPending_=false;
                }
            }
            if(KeyRecent() && keyChar_==13) {enterPending_=true;enterTick_=tick_;}
        }
        if(SemanticNow()) {   // r396: the program describes its screen (HAK Boot Accessibility); review keys still read it
            for(size_t r=0;r<rows_;++r) {for(size_t c=0;c<columns_;++c)prevRaw_[r][c]=text_[r][c];dirty_[r]=false;}
            anyDirty_=false;highlight_=SIZE_MAX;pendingIcon_[0]=0;EchoFromCaret();keyPending_=false;return;
        }
        // r388: a tab bar is said as "Advanced, tab 2 of 5" when the open tab
        // changes; the bar itself is neither read again nor a menu selection.
        char16_t tabSaid[96]={};
        {TabBar tab;
         if(FindTabBar(tab)) {
            bool same=true;for(size_t i=0;same;++i) {if(tab.name[i]!=lastTab_[i])same=false;if(!tab.name[i] || !lastTab_[i])break;}
            if(!same || tab.row!=lastTabRow_) {
                size_t i=0;for(;tab.name[i];++i)lastTab_[i]=tab.name[i];lastTab_[i]=0;lastTabRow_=tab.row;
                size_t at=0;for(size_t k=0;tab.name[k] && at<40;++k)tabSaid[at++]=tab.name[k];
                tabSaid[at++]=speechtext::ownOpen;
                const char16_t* role=options_.polish?u", karta ":u", tab ";while(*role)tabSaid[at++]=*role++;
                Number(tabSaid,at,tab.index+1);
                const char16_t* of=options_.polish?u" z ":u" of ";while(*of)tabSaid[at++]=*of++;
                Number(tabSaid,at,tab.count);tabSaid[at++]=speechtext::ownClose;tabSaid[at]=0;
            }
            dirty_[tab.row]=false;
            if(highlight_==tab.row)highlight_=SIZE_MAX;
         } else {lastTab_[0]=0;lastTabRow_=SIZE_MAX;}}
        // r396d (user): a row of tabbable buttons (OpenCore: "[Shutdown]  |Restart|",
        // the focused one in square brackets). After a key the focused
        // button is said as "Shutdown, button, 1 of 2"; a button row that
        // only lost its focus is not read again (the list's selection is).
        char16_t buttonSaid[96]={};
        for(size_t r=0;r<rows_;++r) {   // not tied to a key read: OpenCore reads keys through its own driver
            if(!dirty_[r])continue;
            size_t buttons=0,focused=SIZE_MAX,fStart=0,fEnd=0;bool other=false;
            size_t bStart[8]={},bEnd[8]={},brackets=0;uint8_t frameBg=0xff;   // r397: [Yes] [No] in a dialog frame, focus by colour
            for(size_t c=0;c<columns_;) {
                const char16_t ch=text_[r][c];
                if(ch==u' ') {++c;continue;}
                if(ch>=0x2500 && ch<=0x257f) {if(frameBg==0xff && (attr_[r][c]&0x80))frameBg=uint8_t((attr_[r][c]>>4)&7);++c;continue;}   // the dialog's frame
                const char16_t close=ch==u'['?u']':ch==u'|'?u'|':0;
                size_t e=c+1;
                if(close) {while(e<columns_ && e<c+24 && text_[r][e]!=close && text_[r][e]!=u'[' && text_[r][e]!=u'|')++e;}
                if(close && e<columns_ && text_[r][e]==close && e>c+1) {
                    if(ch==u'[') {focused=buttons;fStart=c+1;fEnd=e;if(buttons<8) {bStart[buttons]=c+1;bEnd[buttons]=e;}++brackets;}
                    ++buttons;c=e+1;continue;
                }
                other=true;break;
            }
            if(other || buttons<2)continue;
            // all in square brackets: the focused one is drawn in another background (inverse)
            if(brackets==buttons && buttons<=8) {
                uint8_t base=frameBg;
                if(base==0xff) {const size_t s=TextStart(r);base=s<columns_?uint8_t((attr_[r][s>0?s-1:s]>>4)&7):0;}
                size_t lit=SIZE_MAX,litCount=0;
                for(size_t k=0;k<buttons;++k)if((attr_[r][bStart[k]]&0x80) && ((attr_[r][bStart[k]]>>4)&7)!=base) {lit=k;++litCount;}
                if(litCount==1) {focused=lit;fStart=bStart[lit];fEnd=bEnd[lit];}   // otherwise as before (the last bracketed one)
            }
            if(focused==SIZE_MAX) {   // focus went back to the list: the row is no news, the list's selection is said again
                bool had=false;for(size_t c=0;c<columns_ && !had;++c)had=prevRaw_[r][c]==u'[';
                if(had) {dirty_[r]=false;lastHighlightRow_=SIZE_MAX;}
                continue;
            }
            dirty_[r]=false;
            size_t at=0;for(size_t c=fStart;c<fEnd && at<60;++c)buttonSaid[at++]=text_[r][c];
            buttonSaid[at++]=speechtext::ownOpen;
            const char16_t* role=options_.polish?u", przycisk":u", button";while(*role)buttonSaid[at++]=*role++;
            if(options_.positions) {buttonSaid[at++]=u',';buttonSaid[at++]=u' ';Number(buttonSaid,at,focused+1);const char16_t* of=options_.polish?u" z ":u" of ";while(*of)buttonSaid[at++]=*of++;Number(buttonSaid,at,buttons);}
            buttonSaid[at++]=speechtext::ownClose;buttonSaid[at]=0;
            for(size_t r2=0;r2<rows_;++r2)dirty_[r2]=false;   // the list row that lost its marker is not news
            break;
        }
        if(buttonSaid[0]) {
            for(size_t r=0;r<rows_;++r)for(size_t c=0;c<columns_;++c)prevRaw_[r][c]=text_[r][c];
            anyDirty_=false;highlight_=SIZE_MAX;EchoFromCaret();keyPending_=false;lastHighlightRow_=SIZE_MAX;
            const bool repeat=Same16(buttonSaid,lastButton_) && tick_-lastButtonTick_<100;   // one focus change drawn twice
            {size_t i=0;for(;buttonSaid[i] && i<95;++i)lastButton_[i]=buttonSaid[i];lastButton_[i]=0;lastButtonTick_=tick_;}
            if(!quiet_ && !repeat) {Raise(formatting::Event::button);Speak(buttonSaid);}
            return;
        }
        // r386: GRUB's '*' mark wins over colours (on a wallpaper the key-help
        // text has a picture behind it and looks highlighted).
        // r400: the mark really wins: a framed menu on another background made
        // every entry look highlighted, the last one was taken before this
        // point, and Debian's GRUB was announced as "UEFI Firmware Settings,
        // 3 of 3" while "*Debian GNU/Linux" was selected.
        bool arrowMark=false;
        for(size_t r=0;r<rows_;++r)if(dirty_[r] && StarRow(r,u'>')) {highlight_=r;arrowMark=true;break;}
        if(!arrowMark)for(size_t r=0;r<rows_;++r)if(dirty_[r] && StarRow(r)) {highlight_=r;break;}
        if(highlight_==SIZE_MAX)for(size_t r=0;r<rows_;++r)if(dirty_[r] && MarkRow(r)) {highlight_=r;break;}   // r397
        if(highlight_==SIZE_MAX || highlight_>=rows_ || !dirty_[highlight_] || !(HighlightRow(highlight_) || StarRow(highlight_) || StarRow(highlight_,u'>') || MarkRow(highlight_) || ColourSelectedRow(highlight_))) {
            highlight_=SIZE_MAX;
            for(size_t r=0;r<rows_;++r)if(dirty_[r] && HighlightRow(r)) {highlight_=r;break;}
            if(highlight_==SIZE_MAX)for(size_t r=0;r<rows_;++r)if(dirty_[r] && StarRow(r)) {highlight_=r;break;}
            if(highlight_==SIZE_MAX)for(size_t r=0;r<rows_;++r)if(dirty_[r] && ColourSelectedRow(r)) {highlight_=r;break;}   // r392
        }
#ifdef HAK_CHARON_READER_TRACE
        for(size_t r=0;r<rows_;++r)if(dirty_[r]) {
            const uint64_t flags=(StarRow(r)?1:0)|(HighlightRow(r)?2:0)|(MarkRow(r)?4:0)|(ColourSelectedRow(r)?8:0)|(highlight_==r?128:0);
            charontrace::Value("reader pick",(uint64_t(r)<<56)|(flags<<48)|(uint64_t(text_[r][0]&0xff)<<40)|(uint64_t(text_[r][1]&0xff)<<32)|(uint64_t(text_[r][2]&0xff)<<24)|(uint64_t(text_[r][3]&0xff)<<16)|(uint64_t(text_[r][4]&0xff)<<8)|uint64_t(text_[r][5]&0xff));
        }
#endif
        // r397: a row without a letter or digit (systemd-boot's separator
        // line "─────" in the highlight colour) is no selection.
        if(highlight_<rows_) {bool word=false;for(size_t c=0;c<columns_ && !word;++c) {const char16_t ch=text_[highlight_][c];word=(ch>=u'0' && ch<=u'9') || (ch>=u'A' && ch<=u'Z') || (ch>=u'a' && ch<=u'z') || (ch>=0xc0 && ch<0x2000);}
                              if(!word)highlight_=SIZE_MAX;}
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader hl1",highlight_&0xffff);
#endif
        // r362: a bar alone (a title or key-hint bar in another colour) is no
        // selection when a highlighted row inside a menu block changed too
        // (Windows Boot Manager's legacy menu between its two grey bars).
        if(highlight_!=SIZE_MAX && highlight_<rows_) {
            size_t index=0,count=0;MenuBlock(highlight_,index,count);
            if(count<2)for(size_t r=0;r<rows_;++r)if(dirty_[r] && r!=highlight_ && HighlightRow(r)) {
                size_t i2=0,c2=0;MenuBlock(r,i2,c2);
                if(c2>=2) {highlight_=r;count=c2;break;}
            }
            // The screen's first or last text row alone in a bar colour is a
            // title or key-hint bar, read in screen order, not a selection.
            if(count<2) {
                size_t firstRow=SIZE_MAX,lastRow=0;
                for(size_t r=0;r<rows_;++r)if(TextStart(r)!=SIZE_MAX) {if(firstRow==SIZE_MAX)firstRow=r;lastRow=r;}
                if(highlight_==firstRow || highlight_==lastRow) {
                    highlight_=SIZE_MAX;
                    // r392: then a selection by text colour (AMI) inside the page.
                    for(size_t r=0;r<rows_;++r)if(dirty_[r] && r!=firstRow && r!=lastRow && ColourSelectedRow(r)) {highlight_=r;break;}
                }
            }
        }
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader hl2",highlight_&0xffff);
#endif
#if defined(HAK_CHARON_READER_TRACE) || defined(HAK_CHARON_POLL_DIAG)
        {size_t dirtyRows=0;for(size_t r=0;r<rows_;++r)if(dirty_[r])++dirtyRows;
         charontrace::Value("reader flush",(uint64_t(highlight_&0xffff)<<48)|(uint64_t(majorityBg_)<<40)|(uint64_t(rows_)<<16)|dirtyRows);
         for(size_t r=0;r<rows_;++r)if(dirty_[r]) {char16_t t[maxColumns+1];char u8[400];
             if(RowText(r,t,maxColumns+1)) {Utf8(t,u8,sizeof(u8));{const size_t ts=TextStart(r);charontrace::Value("reader row",(uint64_t(r)<<32)|(uint64_t(ColourSelectedRow(r))<<24)|(uint64_t(ts!=SIZE_MAX?attr_[r][ts]:0)<<16)|(uint64_t(HighlightRow(r))<<8)|attr_[r][0]);}charontrace::Text(u8);}}}
#endif
        // speech and info are static (6.4 KB off the started program's stack): Flush
        // runs only from the timer or under Guard and never calls itself.
        static char16_t speech[1600];for(auto& ch:speech)ch=0;
        // r389: screen text that is not the selection (banners such as "GNU
        // GRUB version ...", loader logs, help, message windows) goes to the
        // boot-message voice: it speaks alongside the reader and is not cut.
        static char16_t info[1600];for(auto& ch:info)ch=0;
        if(scrolledOut_[0]) {Add(info,1600,scrolledOut_);scrolledOut_[0]=0;}
        // A program swapped in by a chain loader is named before its screen.
        {char16_t named[96]={};WatchImage(named);if(named[0]) {Add(speech,1600,named);Raise(formatting::Event::newProgram);}}
        if(tabSaid[0]) {Add(speech,1600,tabSaid);Raise(formatting::Event::tab);}
        // r380: a framed window (top border with corners) is named once.
        char16_t windowSaid[maxColumns+16]={};
        {
            if(frameRow_<rows_ && FrameColumn(frameRow_)==SIZE_MAX) {frameRow_=SIZE_MAX;Raise(formatting::Event::closeObject);}  // the old frame is gone
            for(size_t r=0;r<rows_;++r) {
                // r389: a window drawn slowly may have its top border in an
                // earlier read: its inside changing now still names it.
                if(!dirty_[r] && !FrameHasDirty(r))continue;
                const size_t column=FrameColumn(r);
                if(column==SIZE_MAX || (r==frameRow_ && column==frameColumn_))continue;
                char16_t title[maxColumns+1]={};FrameTitle(r,column,title,maxColumns+1);
                char16_t said[maxColumns+16]={};size_t at=0;
                said[at++]=speechtext::ownOpen;
                const char16_t* head=options_.polish?u"Okno":u"Window";while(*head)said[at++]=*head++;
                if(title[0]) {said[at++]=u':';said[at++]=u' ';said[at++]=speechtext::ownClose;for(size_t i=0;title[i] && at+2<maxColumns+16;++i)said[at++]=title[i];}
                else said[at++]=speechtext::ownClose;
                said[at]=0;
                for(size_t i=0;said[i];++i)windowSaid[i]=said[i];   // said below, once it is known whether it is a list
                frameRow_=r;frameColumn_=column;break;
            }
        }
        // r383: inside a window (an opened drop-down list of a firmware form,
        // a dialog) the selection is the cell run whose background differs
        // from the window's own; arrows read its items with "N of M".
        size_t winTop=SIZE_MAX,winBottom=0,winLeft=0,winRight=0,windowRow=SIZE_MAX;
        char16_t windowItem[maxColumns+1]={};size_t windowIndex=0,windowCount=0;
        if(frameRow_<rows_) {
            winLeft=frameColumn_;winRight=winLeft+1;
            while(winRight<columns_ && !(text_[frameRow_][winRight]==0x2510 || text_[frameRow_][winRight]==0x2557 || text_[frameRow_][winRight]==0x256e || text_[frameRow_][winRight]==0x2555 || text_[frameRow_][winRight]==0x2556))++winRight;
            for(size_t b=frameRow_+1;b<rows_;++b) {const char16_t ch=text_[b][winLeft];
                if(ch==0x2514 || ch==0x255a || ch==0x2570 || ch==0x2558 || ch==0x2559) {winBottom=b;break;}}
            if(winRight<columns_ && winBottom>frameRow_+1)winTop=frameRow_;
        }
        if(winTop!=SIZE_MAX) {
            size_t counts[8]={};
            for(size_t r=winTop+1;r<winBottom;++r)for(size_t c=winLeft+1;c<winRight;++c)if(attr_[r][c]&0x80)++counts[(attr_[r][c]>>4)&7];
            uint8_t bg=0;for(uint8_t b=1;b<8;++b)if(counts[b]>counts[bg])bg=b;
            size_t candidate=SIZE_MAX;
            for(size_t r=winTop+1;r<winBottom;++r) {
                bool text=false,lit=false;
                for(size_t c=winLeft+1;c<winRight;++c) {
                    if(text_[r][c]!=u' ' && !(text_[r][c]>=0x2500 && text_[r][c]<=0x257f))text=true;
                    if((attr_[r][c]&0x80) && ((attr_[r][c]>>4)&7)!=bg && text_[r][c]!=u' ')lit=true;
                }
                if(!text)continue;
                if(lit && (candidate==SIZE_MAX || dirty_[r]))candidate=candidate==SIZE_MAX || (dirty_[r] && !dirty_[candidate])?r:candidate;
                if(lit && r==candidate)windowIndex=windowCount;
                ++windowCount;
            }
            if(candidate!=SIZE_MAX && windowCount>=1) {
                windowRow=candidate;size_t at=0;bool space=true;
                for(size_t c=winLeft+1;c<winRight && at+1<maxColumns;++c) {
                    const uint8_t a=attr_[windowRow][c];char16_t ch=text_[windowRow][c];
                    if(!(a&0x80) || ((a>>4)&7)==bg)ch=u' ';
                    if(ch>=0x2500 && ch<=0x257f)ch=u' ';
                    if(ch==u' ') {if(!space)windowItem[at++]=u' ';space=true;} else {windowItem[at++]=ch;space=false;}
                }
                while(at && windowItem[at-1]==u' ')--at;windowItem[at]=0;
                if(!windowItem[0])windowRow=SIZE_MAX;
            }
        }
        // r389: a window without a selection is a message: an error window
        // when its text (or title) reports an error, a dialog when it has
        // buttons ("[ OK ]", "< Yes >", "[Cancel]"), otherwise "Okno".
        if(windowSaid[0] && windowRow==SIZE_MAX && winTop!=SIZE_MAX) {
            bool error=Alarming(windowSaid),buttons=false;
            for(size_t r=winTop+1;r<winBottom;++r) {
                char16_t inner[maxColumns+1];CellsText(text_[r],attr_[r],winLeft+1,winRight,false,inner,maxColumns+1);
                if(!error && Alarming(inner))error=true;
                // edk2 form browser questions: "Press 'Y' to save and exit, 'N' to discard …", "[Y/N]".
                if(HasWord(inner,u"'y'") || HasWord(inner,u"y/n") || HasWord(inner,u"(y/n)"))buttons=true;
                for(size_t i=0;inner[i];++i)if(inner[i]==u'[' || inner[i]==u'<') {
                    const char16_t close=inner[i]==u'['?u']':u'>';size_t k=i+1;while(inner[k]==u' ')++k;size_t letters=0;
                    while(inner[k] && inner[k]!=close && letters<16) {if(inner[k]!=u' ')++letters;++k;}
                    if(inner[k]==close && letters>=2 && letters<=10) {
                        // a word, not a value: no digits
                        bool digit=false;for(size_t j=i+1;j<k;++j)if(inner[j]>=u'0' && inner[j]<=u'9')digit=true;
                        // a key hint ("[Enter]", "<Esc>") is no button
                        size_t a0=i+1;while(inner[a0]==u' ')++a0;size_t a1=k;while(a1>a0 && inner[a1-1]==u' ')--a1;
                        if(!digit && !KeyName(inner+a0,a1-a0))buttons=true;
                    }
                }
            }
            if(error)Raise(formatting::Event::warnUser);
            else if(buttons) {bool question=false;for(size_t r=winTop+1;r<winBottom && !question;++r) {char16_t inner[maxColumns+1];CellsText(text_[r],attr_[r],winLeft+1,winRight,false,inner,maxColumns+1);question=HasWord(inner,u"?") || HasWord(inner,u"y/n") || HasWord(inner,u"'y'");}
                Raise(question?formatting::Event::askQuestion:formatting::Event::openObject);}
            if(error || buttons) {
                const char16_t* kind=error?(options_.polish?u"Okno błędu":u"Error window"):(options_.polish?u"Okno dialogowe":u"Dialog");
                char16_t said[maxColumns+16]={};size_t at=0;said[at++]=speechtext::ownOpen;while(*kind)said[at++]=*kind++;
                size_t from=0;while(windowSaid[from] && windowSaid[from]!=u':')++from;
                if(!windowSaid[from])said[at++]=speechtext::ownClose;
                for(size_t i=from;windowSaid[i] && at+1<maxColumns+16;++i)said[at++]=windowSaid[i];
                said[at]=0;for(size_t i=0;i<=at;++i)windowSaid[i]=said[i];
            }
        }
        if(windowSaid[0]) {
            Raise(formatting::Event::openObject);
            if(windowRow!=SIZE_MAX) {
                // An opened list: "list" (with the window title, if any).
                char16_t said[maxColumns+16]={};size_t at=0;
                said[at++]=speechtext::ownOpen;
                const char16_t* head=options_.polish?u"lista":u"list";while(*head)said[at++]=*head++;
                const char16_t* colon=nullptr;for(size_t i=0;windowSaid[i];++i)if(windowSaid[i]==u':') {colon=windowSaid+i;break;}
                if(colon)for(size_t i=0;colon[i] && at+2<maxColumns+16;++i)said[at++]=colon[i];
                else said[at++]=speechtext::ownClose;
                said[at]=0;Add(speech,1600,said);
            } else if(options_.explicitMessages) {++ExplicitVariety();char16_t styled[maxColumns*2]={};if(FormatCharonMessage(windowSaid,styled,maxColumns*2,true,options_.polish))Add(info,1600,styled);else Add(info,1600,windowSaid);}
            else Add(info,1600,windowSaid);
        }
        if(windowRow!=SIZE_MAX) {
            if(!(windowRow==lastHighlightRow_ && [&]() {for(size_t i=0;;++i) {if(windowItem[i]!=lastHighlight_[i])return false;if(!windowItem[i])return true;}}())) {
                {size_t i=0;for(;windowItem[i];++i)lastHighlight_[i]=windowItem[i];lastHighlight_[i]=0;lastHighlightRow_=windowRow;}
                reviewRow_=windowRow;reviewCol_=TextStart(windowRow);
                AddText(speech,1600,windowItem);
                if(windowCount>=2 && options_.positions) {
                    char16_t where[32]={};size_t at=0;where[at++]=speechtext::ownOpen;where[at++]=u',';where[at++]=u' ';
                    Number(where,at,windowIndex+1);
                    const char16_t* of=options_.polish?u" z ":u" of ";while(*of)where[at++]=*of++;
                    Number(where,at,windowCount);where[at++]=speechtext::ownClose;where[at]=0;Append16(speech,1600,where);
                }
            }
            highlight_=SIZE_MAX;  // the window's selection replaces the page's
        }
        // A window without a selection (a message box) covers the page: the
        // page's highlighted row under it is not read.
        else if(winTop!=SIZE_MAX && highlight_!=SIZE_MAX && (highlight_<=winTop || highlight_>=winBottom))highlight_=SIZE_MAX;
        char16_t line[maxColumns+1];
        auto equal=[](const char16_t* a,const char16_t* b) {for(;*a==*b;++a,++b)if(!*a)return true;return false;};
        char16_t selected[maxColumns+1];
        const size_t previousHighlightRow=lastHighlightRow_;   // r397 events: before this read updates it
        // r397: typing in a line drawn in the selection colour (Syslinux's
        // Tab command-line editor): what was typed or deleted, not the line.
        size_t editedRow=SIZE_MAX;
        if(KeyRecent() && (keyChar_>=32 || keyChar_==8) && highlight_<rows_ && highlight_==lastHighlightRow_ && dirty_[highlight_]
            && (RowText(highlight_,selected,sizeof(selected)/2,true) || RowText(highlight_,selected,sizeof(selected)/2)) && !equal(selected,lastHighlight_)
            && EditEcho(highlight_,speech,1600)) {
            size_t i=0;for(;selected[i];++i)lastHighlight_[i]=selected[i];lastHighlight_[i]=0;
            editedRow=highlight_;reviewRow_=highlight_;highlight_=SIZE_MAX;
            if(keyChar_==8 || keyScan_==8)Raise(formatting::Event::deleteObject);
        }
        if(highlight_!=SIZE_MAX && highlight_<rows_ && RowText(highlight_,line,sizeof(line)/2)
            && (RowText(highlight_,selected,sizeof(selected)/2,true) || ColourText(highlight_,selected,sizeof(selected)/2) || RowText(highlight_,selected,sizeof(selected)/2))
            && !(highlight_==lastHighlightRow_ && equal(selected,lastHighlight_))
            && !(!KeyRecent() && highlight_==clearedRow_ && tick_-clearedTick_<300 && equal(selected,clearedHighlight_) && [&]() {   // r397: repainted, unchanged
                    size_t i=0;for(;selected[i];++i)lastHighlight_[i]=selected[i];lastHighlight_[i]=0;lastHighlightRow_=highlight_;menuAnnounced_=true;return true;}())) {
            {size_t i=0;for(;selected[i];++i)lastHighlight_[i]=selected[i];lastHighlight_[i]=0;lastHighlightRow_=highlight_;}
            reviewRow_=highlight_;reviewCol_=TextStart(highlight_);  // review follows the highlight
            // r397: a mark at the row's end ("Windows  <") is not read.
            {size_t n=0;while(selected[n])++n;if(n>=3 && TrailMark(selected[n-1]) && selected[n-2]==u' ') {n-=2;while(n && selected[n-1]==u' ')--n;selected[n]=0;}}
            // A highlighted row inside a block of rows that start in the same
            // column is a menu: name the program once per screen, then give
            // the position of every newly highlighted entry.
            size_t index=0,count=0;MenuBlock(highlight_,index,count);
            // r397 events of the selection: a new menu (a submenu entered or
            // left), a jump, going round the list's end, a changed value or
            // check box, a greyed-out item, or an ordinary move.
            if(EventsOn()) {
                const bool sameRow=highlight_==previousHighlightRow;
                if(count>=2 && !menuAnnounced_ && KeyRecent())Raise(keyChar_==13?formatting::Event::openSubmenu:keyScan_==0x17?formatting::Event::closeSubmenu:formatting::Event::openObject);
                else if(sameRow && KeyRecent() && keyScan_!=1 && keyScan_!=2) {
                    bool box=false;for(size_t i=0;selected[i] && !box;++i)box=(selected[i]==u'[' || selected[i]==u'(' || selected[i]==u'<') && selected[i+1] && selected[i+2]==(selected[i]==u'['?u']':selected[i]==u'('?u')':u'>');
                    Raise(box?formatting::Event::toggle:formatting::Event::valueChanged);
                }
                else if(KeyRecent() && (keyScan_==5 || keyScan_==6 || keyScan_==9 || keyScan_==10))Raise(formatting::Event::largeMovement);
                else if(KeyRecent() && count>=2 && lastMenuCount_==count && lastMenuIndex_<count && ((keyScan_==2 && index<lastMenuIndex_) || (keyScan_==1 && index>lastMenuIndex_)))Raise(formatting::Event::wrap);
                else {const size_t ts=TextStart(highlight_);const uint8_t a=ts<columns_?attr_[highlight_][ts]:0;
                      Raise((a&0x80) && (a&15)==8?formatting::Event::disabled:formatting::Event::selectObject);}
                lastMenuIndex_=index;lastMenuCount_=count;menuMoved_=true;
            }
            if(count>=2 && !menuAnnounced_) {
                Add(speech,1600,ProgramName());
                char16_t intro[64]={};size_t at=0;intro[at++]=speechtext::ownOpen;
                static const char16_t* plMenu[]={u": kurewskie menu, ",u": menu, ja pierdolę, ",u": znowu jakieś menu, kurwa, "};
                static const char16_t* enMenu[]={u": fucking menu, ",u": another bloody menu, ",u": menu, goddammit, "};
                const char16_t* head=OwnPhrase(u": menu, ",plMenu,enMenu,3);while(*head)intro[at++]=*head++;
                Number(intro,at,count);
                const char16_t* unit=options_.polish?(count%10>=2 && count%10<=4 && (count%100<10 || count%100>=20)?u" pozycje":u" pozycji"):u" items";
                while(*unit)intro[at++]=*unit++;intro[at++]=speechtext::ownClose;intro[at]=0;
                Append16(speech,1600,intro);menuAnnounced_=true;menuIntro_=true;
            }
            // r397: a group heading above the items ("Test Mix:", "Cache:" in
            // Windows Memory Diagnostic's options) is said when the selection
            // enters its group.
            if(count>=2) {
                const size_t column=TextStart(highlight_);size_t label=SIZE_MAX;
                for(size_t up=1;up<=8 && highlight_>=up && column!=SIZE_MAX;++up) {
                    const size_t r=highlight_-up,t=TextStart(r);
                    if(t==SIZE_MAX || t>=column)continue;
                    const size_t e=RowEnd(r);
                    if(e && text_[r][e-1]==u':' && e-t<=40)label=r;
                    break;
                }
                if(label!=SIZE_MAX && label!=lastGroupRow_) {char16_t head[maxColumns+1];RowText(label,head,maxColumns+1);if(head[0])Add(speech,1600,head);}
                lastGroupRow_=label;
            }
            // r397: an edit field whose only highlighted cell is its block caret
            // ("Pass Count (0 - 15): [ 2█ ]"): the row, without the caret.
            {size_t sel=0;while(selected[sel])++sel;size_t all=0;while(line[all])++all;
             if(sel<=2 && all>sel+4) {
                bool inside=false;size_t at=0;
                for(size_t k=0;k<all;++k) {if(line[k]==u'[')inside=true;else if(line[k]==u']')inside=false;
                    if(inside && line[k]>=0x80)continue;selected[at++]=line[k];}
                selected[at]=0;
             }}
            {   // r382: a control on the highlighted row (its value may sit in another column).
                char16_t described[maxColumns*2+64];size_t roleAt=0;
                if(DescribeControls(selected,described,sizeof(described)/2,options_.polish,&roleAt)) {
                    // Only the value is highlighted (edk2 forms): its prompt is
                    // the text left of it on the row.
                    char16_t prompt[maxColumns+1]={};
                    if(!roleAt) {
                        size_t at=SIZE_MAX;
                        for(size_t i=0;line[i] && at==SIZE_MAX;++i) {size_t k=0;while(selected[k] && line[i+k]==selected[k])++k;if(!selected[k])at=i;}
                        if(at!=SIZE_MAX && at) {size_t e=at;while(e && line[e-1]==u' ')--e;for(size_t k=0;k<e;++k)prompt[k]=line[k];prompt[e]=0;}
                    }
                    if(prompt[0]) {Add(speech,1600,prompt);Append16(speech,1600,u"\xE001" u", " u"\xE002");Append16(speech,1600,described);}
                    else Add(speech,1600,described);
                }
                else if(DescribeControls(line,described,sizeof(described)/2,options_.polish,&roleAt) && described[roleAt]) {
                    Add(speech,1600,selected);Append16(speech,1600,described+roleAt);}
                else Add(speech,1600,selected);
            }
            // r397: a greyed-out (dark grey) selected item is unavailable, as NVDA says.
            {const size_t ts=TextStart(highlight_);const uint8_t a=ts<columns_?attr_[highlight_][ts]:0;
             if((a&0x80) && (a&15)==8 && ((a>>4)&7)!=0 && count>=2)Append16(speech,1600,options_.polish?u"\xE001" u", niedostępne" u"\xE002":u"\xE001" u", unavailable" u"\xE002");}
            if(count>=2 && options_.positions) {
                char16_t where[32]={};size_t at=0;where[at++]=speechtext::ownOpen;where[at++]=u',';where[at++]=u' ';
                Number(where,at,index+1);
                const char16_t* of=options_.polish?u" z ":u" of ";while(*of)where[at++]=*of++;
                Number(where,at,count);where[at++]=speechtext::ownClose;where[at]=0;Append16(speech,1600,where);
            }
        }
        // Everything else, by screen segment: a changed segment is read with
        // the whole column block it belongs to (help panels, wrapped
        // messages); unchanged segments (menu items losing their highlight,
        // repaints) stay silent.
        for(size_t r=0;r<rows_;++r)for(size_t c=0;c<columns_;++c)done_[r][c]=false;
        // r399 (user): a terminal/boot loader that cleared and repainted the
        // same content plus a new line reads only the new line (NVDA-style),
        // not the whole screen. Only with NO menu selection (a terminal, not a
        // menu - menus are read in full); rows that came back identical to the
        // screen just before the clear are skipped. The snapshot is used once.
        if(preClearValid_) {
            if(highlight_==SIZE_MAX)for(size_t r=0;r<rows_;++r) {
                bool same=true;for(size_t c=0;c<columns_ && same;++c)same=text_[r][c]==preClear_[r][c];
                if(same)for(size_t c=0;c<columns_;++c)done_[r][c]=true;
            }
            preClearValid_=false;
        }
        if(editedRow<rows_)for(size_t c=0;c<columns_;++c)done_[editedRow][c]=true;
        // r383: the items of an opened list are read one by one, not as a block.
        // Page text beside it on the same rows is cut by the window: not read either.
        if(windowRow!=SIZE_MAX)for(size_t r=winTop;r<=winBottom;++r)for(size_t c=0;c<columns_;++c)done_[r][c]=true;
        // r389: a window's title was said with "Okno …": its top border row is not read again.
        if(windowSaid[0] && frameRow_<rows_)for(size_t c=0;c<columns_;++c)done_[frameRow_][c]=true;
        size_t typedRow=SIZE_MAX,changedRows=0;
        // r389: editing at the caret (GRUB's command line and entry editor,
        // UEFI Shell, option editors, edit fields): what was typed or what was
        // deleted (Backspace/Delete), from the whole line, even in mid-line or
        // outside the line's current text.
        if(KeyRecent() && highlight_==SIZE_MAX) {
            // Editors drawn as pictures (Limine, rEFInd, Windows Boot
            // Manager's F10 options) have no console caret: one changed line.
            size_t line=SIZE_MAX;
            size_t caretRow=0,caretCol=0;const bool drawnCaret=DrawnCaret(caretRow,caretCol);   // r390: or the caret an editor draws
            if(drawnCaret && dirty_[caretRow])line=caretRow;
            else if(row_<rows_ && dirty_[row_] && CursorShown())line=row_;
            // r396: only after a key that edits (a character, Backspace, Delete):
            // an arrow in rEFInd changed the label "PROBE" -> "GPROBE", which is
            // no typed "G".
            else if(tick_-lastConsoleText_>20 && (keyChar_ || keyScan_==8)) {size_t n=0;for(size_t r=0;r<rows_;++r)if(dirty_[r] && SegmentsChanged(r)) {++n;line=r;}if(n!=1)line=SIZE_MAX;}   // pictures only
            if(line!=SIZE_MAX && EditEcho(line,speech,1600)) {
                if(keyChar_==8 || keyScan_==8)Raise(formatting::Event::deleteObject);
                for(size_t c=0;c<columns_;++c)done_[line][c]=true;
                ++changedRows;typedRow=line;}
        }
        // r389: moving through a menu that scrolls redraws its other items:
        // the reader says the selected one, the message voice does not read
        // the rest of the list again.
        // r390 (user): also when a menu first appears: its other items are the
        // menu, read through the selection, never as boot messages.
        const size_t menuColumn=highlight_<rows_?TextStart(highlight_):SIZE_MAX;
        for(size_t r=0;r<rows_;++r) {
            if(!dirty_[r])continue;
            Segment list[40];const size_t n=Segments(r,list,40);
            for(size_t i=0;i<n;++i) {
                const Segment seg=list[i];
                if(done_[r][seg.start] || !SegmentChanged(r,seg))continue;
                // the whole row of another item (its value column too)
                // r390: the help panel on the right (firmware forms) stays a message.
                if(menuColumn!=SIZE_MAX && Near(TextStart(r),menuColumn) && seg.start*10<columns_*6) {size_t i2=0,c2=0;MenuBlock(r,i2,c2);if(c2>=2) {for(size_t c=0;c*10<columns_*6;++c)done_[r][c]=true;continue;}}
                if(r==highlight_) {
                    bool selectedCells=false;
                    for(size_t c=seg.start;c<seg.end;++c)if((attr_[r][c]&0x80) && Highlighted(attr_[r][c]))selectedCells=true;
                    if(selectedCells)continue;
                }
                char16_t now[maxColumns+1],before[maxColumns+1];
                CellsText(text_[r],attr_[r],seg.start,seg.end,false,now,maxColumns+1);
                CellsText(prevRaw_[r],nullptr,seg.start,seg.end,false,before,maxColumns+1);
                // r389: editing at the cursor (GRUB's command line and entry
                // editor, systemd-boot/Limine/rEFInd option editors, Shell
                // prompts, edit fields): what was typed, or what was deleted
                // (Backspace/Delete), from the whole row, even in mid-line.
                if(!now[0])continue;
                if(ArtText(now) || ArtRow(r)) {done_[r][seg.start]=true;continue;}
                // r386: only a selection mark moved ("> " / "*" before an
                // item, OpenCore, GRUB): the item's text is unchanged.
                if(highlight_!=SIZE_MAX && [&]() {
                        // Whole rows: a scaled cell font moves the item when
                        // the mark appears, so segments differ.
                        char16_t a1[maxColumns+1],b1[maxColumns+1];
                        CellsText(text_[r],attr_[r],0,columns_,false,a1,maxColumns+1);
                        CellsText(prevRaw_[r],nullptr,0,columns_,false,b1,maxColumns+1);
                        size_t k=0;while(a1[k] && a1[k]==b1[k])++k;return a1[k]==b1[k];}()) {done_[r][seg.start]=true;continue;}
                // r382: a text spinner (| / - \ turning in one cell) is "busy", said once.
                auto spinner=[](const char16_t* t) {return t[0] && !t[1] && (t[0]==u'|' || t[0]==u'/' || t[0]==u'-' || t[0]==u'\\');};
                if(spinner(now) && (spinner(before) || busySaid_)) {
                    if(!busySaid_) {Add(info,1600,options_.polish?u"\xE001" u"zajęty" u"\xE002":u"\xE001" u"busy" u"\xE002");busySaid_=true;}
                    done_[r][seg.start]=true;continue;}
                // A counter (GRUB "...automatically in 4s.") repeats with new
                // digits every second: say it once, not every tick.
                bool counter=before[0] && SameIgnoringDigits(now,before);
                // Centred counters move by a column when "10" becomes "9":
                // compare the whole row then.
                char16_t rowNow[maxColumns+1],rowBefore[maxColumns+1];
                CellsText(text_[r],attr_[r],0,columns_,false,rowNow,maxColumns+1);
                CellsText(prevRaw_[r],nullptr,0,columns_,false,rowBefore,maxColumns+1);
                if(!counter && rowBefore[0] && SameIgnoringDigits(rowNow,rowBefore) && !(rowNow[0]==rowBefore[0] && Same16(rowNow,rowBefore))) {
                    counter=true;for(size_t k=0;k<=maxColumns && (k==0 || rowBefore[k-1]);++k)before[k]=rowBefore[k];
                    for(size_t k=0;k<=maxColumns && (k==0 || rowNow[k-1]);++k)now[k]=rowNow[k];
                    for(size_t c=0;c<columns_;++c)done_[r][c]=true;
                }
                // r384: countdowns ("Boot in 9 s.", "automatically in 4s."):
                // the number that changed is the time left.
                // r390 (user): a number changed by a key (an editor's line and column) is no countdown.
                if(counter && !KeyRecent() && (options_.managerFlags&4) && !countdownStopped_ && RunningProfile() && ChangedNumber(before,now)>0)StopCountdown();
                if(counter && !KeyRecent() && ChangedNumber(before,now)>=0) {countdownSeen_=tick_;if(quietMenu_==1)quietMenu_=2;}   // r400
                if(counter && options_.timers && !KeyRecent()) {
                    const long left=ChangedNumber(before,now);
                    if(left>=0) {
                        if(options_.timers&2) {
                            if(options_.effects && options_.effects->Tone) {
                                if(left==0) {charontrace::Value("reader countdown end",0);{options_.effects->Tone(options_.effects->context,880,500);endToneUntil_=tick_+50;}timerEndAt_=0;}
                                else {charontrace::Value("reader countdown tick",uint64_t(left));options_.effects->Tone(options_.effects->context,1000,40);}
                            }
                            // The last second ends with the long tone even when the
                            // program starts before it shows 0.
                            timerEndAt_=left==1?tick_+100:0;timerRow_=r;
                        }
                        if((options_.timers&1) && left>0 && left%10==0) {Add(info,1600,now);++changedRows;}
                        continue;
                    }
                }
                const int percent=Percent(now);
                if(percent>=0) {Progress(percent);if(options_.progress)counter=true;}
                // r397: a number a command key changed with no caret on the
                // screen (systemd-boot "t"/"T": "Menu timeout set to 30 s.") is
                // the answer to that key; in editors (a caret) Row/Col stay quiet.
                if(counter && percent<0 && KeyRecent() && keyChar_ && !CaretKnown()) {Add(info,1600,now);++changedRows;continue;}
                if(counter && (options_.verbosity<2 || percent>=0))continue;
                // Typing at a prompt: speak only what was added.
                size_t common=0;while(before[common] && before[common]==now[common])++common;
                if(before[0] && !before[common] && now[common] && highlight_==SIZE_MAX) {typedRow=r;++changedRows;
                    Add(info,1600,now+common);continue;}
                // Read the whole column block containing this segment.
                size_t top=r,bottom=r;Segment other;
                // r389: with a visible caret (a console: GRUB's command line,
                // UEFI Shell) only changed lines are new; menus and forms
                // (caret hidden) read the whole paragraph.
                const bool console=CursorShown();
                const bool scrolling=console && lastScroll_ && tick_-lastScroll_<500;   // output of commands: plain text
                // r389: only lines that are new are read, also outside consoles
                // (a firmware log line was read again with the next one).
                // r397: a console's output is read row by row, the whole row
                // (UEFI Shell's "help": name column and description together).
                if(console && highlight_==SIZE_MAX) {
                    // Segments that are ASCII art (FreeBSD's logo beside its menu) are left out.
                    char16_t whole[maxColumns+1]={};size_t wholeAt=0;
                    for(size_t k=i;k<n;++k) {
                        char16_t part[maxColumns+1];CellsText(text_[r],attr_[r],list[k].start,list[k].end,false,part,maxColumns+1);
                        if(!part[0] || ArtText(part))continue;
                        if(wholeAt && wholeAt+1<maxColumns)whole[wholeAt++]=u' ';
                        for(size_t j=0;part[j] && wholeAt+1<maxColumns;++j)whole[wholeAt++]=part[j];
                    }
                    whole[wholeAt]=0;
                    for(size_t c=seg.start;c<columns_;++c)done_[r][c]=true;
                    if(whole[0] && !ArtText(whole) && !ArtRow(r) && !(KeyRecent() && RepeatedInfo(r,whole))) {
                        Add(info,1600,whole);
                    }
                    ++changedRows;continue;
                }
                while(top && SegmentAt(top-1,seg.start,other) && SegmentChanged(top-1,other))--top;
                while(bottom+1<rows_ && SegmentAt(bottom+1,seg.start,other) && SegmentChanged(bottom+1,other))++bottom;
                // r396d (user): after a key in a form (caret hidden: firmware
                // setup's help panel) the whole paragraph is read, not only
                // its changed lines ("take you to the Boot" was said alone).
                if(!console && KeyRecent()) {
                    while(top && SegmentAt(top-1,seg.start,other) && !done_[top-1][other.start])--top;
                    while(bottom+1<rows_ && SegmentAt(bottom+1,seg.start,other) && !done_[bottom+1][other.start])++bottom;
                }
                const bool paragraph=bottom>top;
                bool first=true;
                for(size_t rr=top;rr<=bottom;++rr) {
                    if(!SegmentAt(rr,seg.start,other))continue;
                    done_[rr][seg.start]=true;
                    char16_t part[maxColumns+1];CellsText(text_[rr],attr_[rr],other.start,other.end,false,part,maxColumns+1);
                    if(!part[0] || ArtText(part) || ArtRow(rr))continue;
                    // r389: never the same words in both voices: the selected
                    // row and text the reader already says (a form's help that
                    // repeats the label) are left out of the message.
                    if(!paragraph && SaidAlready(speech,part))continue;   // r396d: a line of a longer help text stays
                    if(!paragraph && KeyRecent() && RepeatedInfo(rr,part))continue;   // r390: a status bar redrawn after a key
                    // A console's output is plain text: "[USERLIST]" in GRUB's help is no control.
                    if(scrolling) {if(first) {Add(info,1600,part);first=false;} else {Append16(info,1600,u" ");Append16(info,1600,part);}continue;}
                    if(first) {AddText(info,1600,part);first=false;}
                    else {
                        char16_t described[maxColumns*2+64];
                        if(DescribeControls(part,described,sizeof(described)/2,options_.polish))Add(info,1600,described);
                        else {Append16(info,1600,u" ");Append16(info,1600,part);}
                    }
                }
                ++changedRows;
            }
        }
        if(timerEndAt_ && timerRow_<rows_ && dirty_[timerRow_]) {
            char16_t rowNow[maxColumns+1],rowBefore[maxColumns+1];
            CellsText(text_[timerRow_],nullptr,0,columns_,false,rowNow,maxColumns+1);
            CellsText(prevRaw_[timerRow_],nullptr,0,columns_,false,rowBefore,maxColumns+1);
            if(!SameIgnoringDigits(rowNow,rowBefore) || !rowNow[0]) {
                timerEndAt_=0;
                charontrace::Value("reader countdown end",1);
                if((options_.timers&2) && options_.effects && options_.effects->Tone){options_.effects->Tone(options_.effects->context,880,500);endToneUntil_=tick_+50;}
            }
        }
        for(size_t r=0;r<rows_;++r)for(size_t c=0;c<columns_;++c)prevRaw_[r][c]=text_[r][c];
        (void)typedRow;(void)changedRows;
        if(changedRows)busySaid_=false;
        if(pendingIcon_[0] && !iconLabelLength_ && info[0] && !speech[0] && !KeyRecent() && bootManager_ && bootManager_[0]==u'r') {
            // r396d: rEFInd's first screen: the selected item's name (its first
            // line) with the position; the rest of the screen on the message voice.
            size_t cut=0;while(info[cut] && !(info[cut]==u'.' && info[cut+1]==u' '))++cut;
            for(size_t k=0;k<cut && k<1599;++k)speech[k]=info[k];speech[cut<1599?cut:1599]=0;
            size_t rest=info[cut]?cut+2:cut,k=0;while(info[rest+k]) {info[k]=info[rest+k];++k;}info[k]=0;
            const char16_t* position=pendingIcon_;while(*position && *position!=u' ')++position;
            if(*position) {Append16(speech,1600,u"\xE001" u"," u"\xE002");Append16(speech,1600,position);}
            pendingIcon_[0]=0;
        }
        if(pendingIcon_[0] && !iconLabelLength_ && info[0] && !speech[0] && KeyRecent()) {
            // r396 (user): after a key in an icon picker (rEFInd, Clover) the
            // selected item's name, drawn as text under the icons, is said
            // with its position instead of "icon 2 of 3".
            // r397: the name only; a redrawn hint line ("Use arrow keys to move
            // cursor; Enter to boot;" after rEFInd's About) stays a message.
            {size_t cut=0;while(info[cut] && !(info[cut]==u'.' && info[cut+1]==u' '))++cut;
             for(size_t k=0;k<cut && k<1599;++k)speech[k]=info[k];speech[cut<1599?cut:1599]=0;
             size_t rest=info[cut]?cut+2:cut,k=0;while(info[rest+k]) {info[k]=info[rest+k];++k;}info[k]=0;
             if(!SaidAlready(lastIconInfo_,info)) {size_t i=0;for(;info[i] && i<255;++i)lastIconInfo_[i]=info[i];lastIconInfo_[i]=0;} else info[0]=0;}
            const char16_t* position=pendingIcon_;while(*position && *position!=u' ')++position;
            if(*position) {Append16(speech,1600,u"\xE001" u"," u"\xE002");Append16(speech,1600,position);}
            pendingIcon_[0]=0;
        }
        if(pendingIcon_[0]) {
            // The label may already end the speech (read as screen text).
            size_t length=0;while(speech[length])++length;
            bool labelSaid=iconLabelLength_ && length>=iconLabelLength_;
            for(size_t k=0;labelSaid && k<iconLabelLength_;++k)labelSaid=speech[length-iconLabelLength_+k]==pendingIcon_[k];
            if(labelSaid)Append16(speech,1600,pendingIcon_+iconLabelLength_);
            else if(speech[0]) {Append16(speech,1600,u"\xE001" u", " u"\xE002");Append16(speech,1600,pendingIcon_);} else Add(speech,1600,pendingIcon_);
            iconLabelLength_=0;
            pendingIcon_[0]=0;}
        for(size_t r=0;r<rows_;++r)dirty_[r]=false;
        anyDirty_=false;highlight_=SIZE_MAX;
        // Cell memory (prevRaw_) already drops repaints; a repeated
        // keystroke echo must still be spoken.
        // Without the message voice (older provider) messages are read as before, after the selection.
        auto* fx=options_.effects;
        const bool messages=fx && fx->SpeakMessage && options_.messageVoice;
        // r400 (user): a boot menu that counts down to starting a system by
        // itself is not read out, so the boot messages that follow stay clear;
        // any key reads it at once. A new menu is first held for a moment: with
        // no countdown in that time it is read as always.
        if(menuIntro_ && !everKey_ && !quietMenu_) {quietMenu_=1;quietTick_=tick_;heldMenu_[0]=0;heldInfo_[0]=0;}
        menuIntro_=false;
        if(quietMenu_ && !everKey_) {
            if(quietMenu_==1) {
                if(speech[0]) {heldMenu_[0]=0;Add(heldMenu_,1600,speech);}
                if(info[0])Add(heldInfo_,1600,info);
                return;
            }
            speech[0]=0;   // counting down: the menu stays unread, messages go on
        }
        if(info[0] && !messages)Add(speech,1600,info);
        // r389: messages wait until the screen is quiet for 60 ms (a form's
        // help is drawn after the selection and would come in pieces); the
        // reader's own speech is not delayed.
        // r397: an editor (its caret appeared, or a key replaced the screen and a caret is on it) or another screen.
        if(EventsOn() && event_==formatting::Event::none && !menuMoved_) {
            size_t cr=0,cc=0;const bool caret=CursorShown() || DrawnCaret(cr,cc);
            if(cursorAppeared_ || (screenReplaced_ && caret))Raise(formatting::Event::editor);
            else if(screenReplaced_)Raise(formatting::Event::openObject);
        }
        menuMoved_=false;
        // r397: without reader speech the event goes with the boot message (the message voice's utterance).
        if(info[0] && messages && !quiet_ && !speech[0] && event_!=formatting::Event::none) {infoEvent_=event_;event_=formatting::Event::none;}
        if(info[0] && messages && !quiet_) {Add(pendingInfo_,1600,info);infoTick_=tick_;}
        if(!speech[0]) {   // r389: a repaint that only moved the caret
            if(!info[0])CursorEcho();else {EchoFromCaret();keyPending_=false;}
            return;}
        EchoFromCaret();keyPending_=false;
        if(quiet_)return;  // r379: automatic reading off (Ctrl+Alt+Q); review keys still read
        // r397: a line of frame characters alone ("────────", systemd-boot's separator) is no speech.
        {size_t n=0,frame=0;bool own=false;for(size_t i=0;speech[i];++i) {const char16_t ch=speech[i];if(ch==speechtext::ownOpen)own=true;if(ch==u' ')continue;++n;if(ch>=0x2500 && ch<=0x259f)++frame;}
         if(!own && n>=1 && frame==n)return;}   // a frame alone ("│", the side of a password box) too
        Speak(speech);
    }
    // Before firmware has connected the audio controller (Driver#### runs
    // early) speech cannot start yet: keep the newest text and retry from the
    // timer; a newer screen replaces it, as speech would have.
    // The reader's own phrases (not screen text) in explicit mode.
    void SendMessages() {
        if(!pendingInfo_[0])return;
        auto* fx=options_.effects;
        static char16_t composed[1600];char language[3]={};   // static: timer only, off the program's stack
#ifdef HAK_CHARON_READER_TRACE
        {char raw[4800];Utf8(pendingInfo_,raw,sizeof(raw));charontrace::Text("reader message raw");charontrace::Text(raw);}
#endif
        if(!historyOpen_)AddHistory(pendingInfo_);
        const char16_t* source=pendingInfo_;
        {const auto keep=event_;event_=infoEvent_;infoEvent_=formatting::Event::none;   // r397: the message's own event
         if(FormatSpeech(pendingInfo_,formatted_,sizeof(formatted_)/2))source=formatted_;
         if(event_==formatting::Event::none)event_=keep;}
        ComposeSpeech(source,false,composed,1600,language);
        static char utf8[4800];Utf8(composed,utf8,sizeof(utf8));pendingInfo_[0]=0;
        if(quiet_ || !fx || !fx->SpeakMessage || !utf8[0])return;
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Text("reader message");charontrace::Text(utf8);
#endif
        fx->SpeakMessage(fx->context,language,utf8);
    }
    // r390: NVDA's symbol levels and the screen text's language (CharonSpeechText.h).
    speechtext::Reading ReadingOptions() const {
        speechtext::Reading r;r.level=options_.punctuation<=3?options_.punctuation:1;r.screenLanguage=options_.screenLanguage;r.interfacePolish=options_.polish;r.dictionary=options_.dictionary;
        return r;
    }
    void ComposeSpeech(const char16_t* text,bool own,char16_t* out,size_t capacity,char* language) {
        const auto lang=speechtext::Compose(text,own,ReadingOptions(),stickyLanguage_,out,capacity);
        language[0]=lang==speechtext::Lang::pl?'p':'e';language[1]=lang==speechtext::Lang::pl?'l':'n';language[2]=0;
    }
    void SayOwn(const char16_t* text,bool own=true) {
        ownSpeech_=own;
        if(!options_.explicitMessages) {Speak(text);ownSpeech_=false;return;}
        ++ExplicitVariety();
        static char16_t styled[1600];   // static: SayOwn runs from the timer or under Guard and does not nest
        if(FormatCharonMessage(text,styled,1600,true,options_.polish))Speak(styled);else Speak(text);
        ownSpeech_=false;
    }
    // r388: the voice's pitch says how important a message is: errors and
    // warnings 30% higher, the reader's own messages 10% lower, screen text
    // at the chosen pitch. Applied per utterance (a newer one replaces it).
    static bool HasWord(const char16_t* text,const char16_t* word) {
        for(size_t i=0;text[i];++i) {
            size_t k=0;
            while(word[k]) {char16_t a=text[i+k],b=word[k];if(a>=u'A' && a<=u'Z')a=char16_t(a+32);if(a==0x0141)a=0x0142;if(a!=b)break;++k;}
            if(!word[k])return true;
        }
        return false;
    }
    static bool Alarming(const char16_t* text) {
        static const char16_t* words[]={u"error",u"fail",u"warning",u"invalid",u"not found",u"cannot",u"can't",u"unable",u"fatal",u"denied",
            u"violation",u"corrupt",u"błąd",u"błęd",u"nie można",u"nie udało",u"ostrzeżenie",u"uwaga",u"nieprawidłow",u"odmow"};
        for(auto* w:words)if(HasWord(text,w))return true;
        return false;
    }
    void PriorityPitch(const char16_t* text) {
        auto* fx=options_.effects;if(!fx || !fx->SetPitch)return;
        int pitch=int(pitch_);
        if(capPitch_)pitch+=3;   // r390 NVDA capital pitch change (30%)
        else if(ownSpeech_)pitch-=1;else if(Alarming(text))pitch+=3;
        if(pitch<5)pitch=5;if(pitch>20)pitch=20;
        if(unsigned(pitch)!=appliedPitch_) {appliedPitch_=unsigned(pitch);fx->SetPitch(fx->context,appliedPitch_);}
    }
    // Explicit wordings for the reader's own short phrases; variant by counter.
    const char16_t* OwnPhrase(const char16_t* plain,const char16_t* const* explicitPl,const char16_t* const* explicitEn,size_t count) {
        if(!options_.explicitMessages)return plain;
        ++ExplicitVariety();
        return (options_.polish?explicitPl:explicitEn)[ExplicitVariety()%count];
    }
    void Speak(const char16_t* text) {
        if(stopped_ || farewell_)return;
        // r397: screen text of frame characters alone ("│", a box side) is no speech, on every path.
        if(!ownSpeech_ && !rawSpeech_) {bool own=false;size_t n=0,frame=0;
            for(size_t i=0;text[i];++i) {const char16_t ch=text[i];if(ch==speechtext::ownOpen)own=true;if(ch==u' ')continue;++n;if(ch>=0x2500 && ch<=0x259f)++frame;}
            if(!own && n && frame==n)return;
            // frame characters between words are no text (a dialog's sides around its field)
            if(frame && !own) {static char16_t clean[1600];size_t k=0;for(;text[k] && k+1<1600;++k)clean[k]=(text[k]>=0x2500 && text[k]<=0x257f)?u' ':text[k];clean[k]=0;text=clean;}}
#ifdef HAK_CHARON_READER_TRACE
        {char t[600];Utf8(text,t,sizeof(t));charontrace::Text("reader say");charontrace::Text(revealing_?"(review of a secret entry: not logged)":t);}
#endif
        if(!historyOpen_ && !revealing_)AddHistory(text);   // r407: revealed secret text is spoken once and kept nowhere
        const char16_t* source=text;
        if(!rawSpeech_ && FormatSpeech(text,formatted_,sizeof(formatted_)/2,ownSpeech_))source=formatted_;
        ComposeSpeech(source,ownSpeech_ || rawSpeech_,pending_,sizeof(pending_)/2,pendingLanguage_);
        if(!pending_[0])return;
        PriorityPitch(pending_);
        TrySpeakPending();
    }
    void TrySpeakPending() {
        if(!pending_[0] || stopped_)return;
        static char utf8[4800];Utf8(pending_,utf8,sizeof(utf8));   // static: off the program's stack
        if(options_.voice->SpeakUtf8(options_.voice,pendingLanguage_[0]?pendingLanguage_:options_.language,utf8)==EFI_SUCCESS) {pending_[0]=0;failed_=false;}
        else failed_=true;
    }
    // ---- reader shortcuts (r369) ----------------------------------------
    static efi_status EFIAPI KeyNotify(KeyData* data) {
        // Firmware's notification context: only note the command.
#ifdef HAK_CHARON_READER_TRACE
        if(data)charontrace::Value("reader notify",(uint64_t(data->key.ScanCode)<<48)|(uint64_t(data->key.UnicodeChar)<<32)|data->state.shift);
#endif
        if(ResidentReader* self=active_)if(const char16_t c=ReaderKeys::Command(data)) {
            self->command_=c;
            // r388: Ctrl+Alt+arrow belongs to the reader; the plain arrow the
            // console also queues must not reach the program.
            if(!data->key.UnicodeChar && data->key.ScanCode>=1 && data->key.ScanCode<=4)self->swallowScan_=data->key.ScanCode;
            if(c==u'\\')self->pauseSwallowChar_=u'\\';
            if(c==u'?')self->swallowScan_=0x0b;   // Alt+F1
            // r393: Shift+Left/Right are regions only in the paused review.
            if(c==u'(' || c==u')') {if(!self->reviewPause_) {self->command_=0;self->swallowScan_=0;return EFI_SUCCESS;}self->command_=c==u')'?u'}':u'{';}
        }
        return EFI_SUCCESS;
    }
    static void KeypadPress(void* context,char16_t c) {static_cast<ResidentReader*>(context)->command_=c;}
    void Command(char16_t c) {
        // Held keys repeat; one command per press. r390: a press 60 ms after
        // the last one is a repeat; within 200 ms only while repeating. The
        // same key again within 500 ms counts (NVDA: spell, phonetic, code).
        const uint64_t gap=tick_-lastCommand_;
        if(c==lastCommandKey_ && (gap<6 || (gap<20 && repeating_))) {lastCommand_=tick_;repeating_=true;return;}
        pressCount_=(c==lastCommandKey_ && gap<60)?pressCount_+1:1;   // r390: 600 ms (QEMU: 0.4 s measured 50 ticks)
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader press",(uint64_t(gap)<<16)|pressCount_);
#endif
        repeating_=false;
        lastCommand_=tick_;lastCommandKey_=c;
        if(helpOpen_ && KeyListCommand(c))return;
        if(historyOpen_ && HistoryCommand(c))return;
        if(ReaderExtraCommand(c))return;   // r393
        if(c==u'?') {OpenKeyList(!helpOpen_);return;}
        if(inputHelp_ && c!=u'x') {SayOwn(CommandHelp(c));return;}
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader command",uint64_t(c));
#endif
        const bool pl=options_.polish;
        if(c==u's') {
            // A new (blank) utterance cancels the current one.
            pending_[0]=0;options_.voice->SpeakUtf8(options_.voice,options_.language," ");
            return;
        }
        if(c==u'x') {
            inputHelp_=!inputHelp_;insertArmed_=false;
            SayOwn(inputHelp_?(pl?u"Pomoc klawiszy włączona. Każdy klawisz zostaje nazwany, nic się nie wykonuje. Insert 0 albo Control Alt X wyłącza.":u"Input help on. Every key is named, nothing runs. Insert 0 or Control Alt X turns it off.")
                :(pl?u"Pomoc klawiszy wyłączona.":u"Input help off."));
            return;
        }
        if(c==u'\\') {SetReviewPause(!reviewPause_);return;}
        if(c==u'd') {
            options_.punctuation=uint8_t((options_.punctuation+1)%4);
            static const char16_t* namesPl[]={u"Interpunkcja: brak",u"Interpunkcja: część",u"Interpunkcja: większość",u"Interpunkcja: wszystko"};
            static const char16_t* namesEn[]={u"Symbol level: none",u"Symbol level: some",u"Symbol level: most",u"Symbol level: all"};
            SayOwn((pl?namesPl:namesEn)[options_.punctuation]);return;
        }
        if(c==u'z') {
            options_.screenLanguage=uint8_t((options_.screenLanguage+1)%4);stickyLanguage_=speechtext::Lang::unknown;
            static const char16_t* namesPl[]={u"Język tekstu ekranu: automatycznie",u"Język tekstu ekranu: jak menu",u"Język tekstu ekranu: angielski",u"Język tekstu ekranu: polski"};
            static const char16_t* namesEn[]={u"Screen text language: automatic",u"Screen text language: as the menu",u"Screen text language: English",u"Screen text language: Polish"};
            SayOwn((pl?namesPl:namesEn)[options_.screenLanguage]);return;
        }
        if(c==u'h') {
            SayOwn(pl?u"Skróty Charona. Układ jak NVDA na komputerze stacjonarnym: Insert, a potem cyfra z klawiatury numerycznej: 7, 8, 9 wiersze, 4, 5, 6 słowa, 1, 2, 3 znaki, plus czytaj dalej, gwiazdka cały ekran, Tab zaznaczona pozycja, End pasek klawiszy, T gdzie jestem, F kolory, F12 godzina, dwa razy Insert to zwykły Insert. Układ laptopowy: Control Alt i litera: R cały ekran, A czytaj dalej, S ucisza, U, I, O wiersze, J, K, L słowa, M, przecinek, kropka znaki, T i B góra i dół, E zaznaczona pozycja, P położenie, N kolory, C godzina, Q automatyczne czytanie, równa się i minus tempo, strzałki w lewo i w prawo wybierają tempo, głośność lub wysokość, strzałki w górę i w dół je zmieniają, W gdzie jestem, Y pasek klawiszy, V szczegółowość, D interpunkcja, Z język tekstu ekranu, X pomoc klawiszy, H ta pomoc. Dwa razy wiersz lub słowo: literowanie, dwa razy znak: alfabet fonetyczny, trzy razy: kod znaku. Sam numpad przy wyłączonym NumLocku przegląda ekran na klawiaturze USB, tak jak w NVDA; Insert P interpunkcja, Insert 0 pomoc klawiszy. Układ Alt: Alt strzałki w górę i w dół wiersze, w lewo i w prawo znaki, Alt ukośnik wsteczny wstrzymuje rozruch i włącza przegląd ekranu; zwykłe strzałki czytają wtedy wiersze i znaki, Escape wznawia."
                    :u"Charon shortcuts. NVDA desktop layout: Insert, then a numpad digit: 7, 8, 9 lines, 4, 5, 6 words, 1, 2, 3 characters, plus read on, star the whole screen, Tab the highlighted entry, End the key-hint bar, T where am I, F colours, F12 time, Insert twice sends a real Insert. Laptop layout: Control Alt and a letter: R whole screen, A read on, S stop, U, I, O lines, J, K, L words, M, comma, period characters, T and B top and bottom, E highlighted entry, P position, N colours, C time, Q automatic reading, equals and minus rate, Left and Right choose rate, volume or pitch, Up and Down change it, W where am I, Y key-hint bar, V verbosity, D symbol level, Z screen text language, X input help, H this help. A line or word twice: spelled, a character twice: phonetic, three times: its code. The numpad alone with NumLock off reviews the screen on USB keyboards, as in NVDA; Insert P symbol level, Insert 0 input help. Alt layout: Alt Up and Down lines, Left and Right characters, Alt backslash pauses the boot for screen review; plain arrows then read lines and characters, Escape goes on.");
            return;
        }
        {const bool reveal=secret_.marks!=0;   // r407: only these explicit commands read a secret entry's real text
         if(reveal) {SecretSwap();revealing_=true;}
         const bool done=Review(c);
         if(reveal) {SecretSwap();revealing_=false;}
         if(done)return;}
        // Read again: every row counts as new, the menu is introduced again.
        bool any=false;
        for(size_t r=0;r<rows_ && !any;++r)any=TextStart(r)!=SIZE_MAX;
        if(!any) {
            static const char16_t* plNone[]={u"Kurwa, pusto. Na ekranie nie ma tekstu do przeczytania.",u"Gówno widzę: zero tekstu na ekranie.",u"Ekran pusty jak twój portfel po wypłacie, jebany świat."};
            static const char16_t* enNone[]={u"Fuck all on the screen, nothing to read.",u"Bugger all text on this screen.",u"Screen’s as empty as your head right now, shit."};
            Speak(OwnPhrase(pl?u"Brak tekstu na ekranie, który Charon może odczytać.":u"No text on the screen Charon can read.",plNone,enNone,3));return;}
        for(size_t r=0;r<rows_;++r) {dirty_[r]=true;for(size_t col=0;col<maxColumns;++col)prevRaw_[r][col]=0;}
        lastHighlight_[0]=0;lastHighlightRow_=SIZE_MAX;menuAnnounced_=false;highlight_=SIZE_MAX;anyDirty_=true;
        Flush();
    }
    // ---- review cursor (r378) --------------------------------------------
    // Reads the screen model (console text and recognized graphics text) by
    // line, word and character without moving anything in the program.
    bool RowHasText(size_t r) const {return r<rows_ && TextStart(r)!=SIZE_MAX;}
    size_t RowEnd(size_t r) const {size_t e=columns_;while(e && (text_[r][e-1]==u' ' || !text_[r][e-1]))--e;return e;}
    void ReviewStart() {
        if(reviewRow_<rows_ && RowHasText(reviewRow_))return;
        reviewRow_=SIZE_MAX;
        const size_t lit=highlight_<rows_?highlight_:lastHighlightRow_;
        if(lit<rows_ && RowHasText(lit))reviewRow_=lit;
        else for(size_t r=0;r<rows_;++r)if(RowHasText(r)) {reviewRow_=r;break;}
        if(reviewRow_<rows_)reviewCol_=TextStart(reviewRow_);
    }
    void SayRow(size_t r) {
        char16_t line[maxColumns*4]={};RowText(r,line,sizeof(line)/2);
        size_t n=0;while(line[n])++n;while(n && line[n-1]==u' ')line[--n]=0;
        if(line[0])Speak(line);else SayOwn(options_.polish?u"Pusty wiersz":u"Blank line");
    }
    void SayEdge(bool top) {SayOwn(top?(options_.polish?u"Góra ekranu":u"Top of the screen"):(options_.polish?u"Dół ekranu":u"Bottom of the screen"));}
    void SayWordAt(size_t r,size_t c) {
        if(Blank(text_[r][c])) {SayChar(text_[r][c]);return;}
        size_t a=c;while(a && !Blank(text_[r][a-1]))--a;
        size_t b=c;while(b<columns_ && !Blank(text_[r][b]))++b;
        char16_t word[maxColumns+1]={};size_t n=0;for(size_t i=a;i<b;++i)word[n++]=text_[r][i];word[n]=0;
        Speak(word);
    }
    // r390: NVDA's character reading: symbol names at the "char" level,
    // capitals by word, beep and/or higher pitch (settings), the second press
    // the phonetic description, the third the character's code.
    void SayChar(char16_t ch,unsigned press=1) {
        const bool pl=options_.polish,cap=speechtext::UpperCase(ch);
        char16_t text[96];speechtext::Out o{text,96};
        const bool sayCap=(options_.capitals&1)!=0;
        if(press==2 && speechtext::Phonetic(ch,pl,sayCap,o)) {}
        else if(press>=3) speechtext::CharCode(ch,pl,o);
        else speechtext::CharName(ch,pl,sayCap,o);
        if(cap && (options_.capitals&2) && options_.effects && options_.effects->Tone)options_.effects->Tone(options_.effects->context,2000,50);   // NVDA: 2000 Hz, 50 ms
        rawSpeech_=true;
        if(cap && (options_.capitals&4)) {capPitch_=true;}
        Speak(text);
        rawSpeech_=false;capPitch_=false;
    }
    void SpellText(const char16_t* s,size_t n) {
        char16_t text[1200];speechtext::Out o{text,1200};
        while(n && s[0]==u' ') {++s;--n;}while(n && s[n-1]==u' ')--n;
        speechtext::Spell(s,n,options_.polish,(options_.capitals&7)!=0,o);
        rawSpeech_=true;Speak(text);rawSpeech_=false;
    }
    bool NextTextRow(size_t& r,bool forward) const {
        if(forward) {for(size_t k=r+1;k<rows_;++k)if(RowHasText(k)) {r=k;return true;}}
        else {for(size_t k=r;k-->0;)if(RowHasText(k)) {r=k;return true;}}
        return false;
    }
    // r379: colours of the text at the review cursor (EFI text attributes).
    void SayColours(size_t r,size_t c) {
        const bool pl=options_.polish;
        static const char16_t* coloursPl[]={u"czarny",u"niebieski",u"zielony",u"turkusowy",u"czerwony",u"fioletowy",u"brązowy",u"jasnoszary",
            u"ciemnoszary",u"jasnoniebieski",u"jasnozielony",u"jasnoturkusowy",u"jasnoczerwony",u"jasnofioletowy",u"żółty",u"biały"};
        static const char16_t* backgroundsPl[]={u"czarnym",u"niebieskim",u"zielonym",u"turkusowym",u"czerwonym",u"fioletowym",u"brązowym",u"jasnoszarym"};
        static const char16_t* coloursEn[]={u"black",u"blue",u"green",u"cyan",u"red",u"magenta",u"brown",u"light grey",
            u"dark grey",u"light blue",u"light green",u"light cyan",u"light red",u"light magenta",u"yellow",u"white"};
        const uint8_t a=attr_[r][c];
        char16_t text[160]={};size_t at=0;
        auto put=[&](const char16_t* s) {while(*s && at+1<160)text[at++]=*s++;};
        // r397: the whole format, as NVDA+F and Speakup's attribute key: font and size from the OCR readers too.
        const uint16_t font=cellFont_[r][c];const unsigned height=font&255;const bool bold=(font&0x4000)!=0;
        auto fontPart=[&]() {
            if(height) {put(pl?u", czcionka ":u", font ");char16_t n[8];size_t k=0;unsigned h=height;char16_t d[4];size_t m=0;do {d[m++]=char16_t(u'0'+h%10);h/=10;} while(h && m<4);while(m)n[k++]=d[--m];n[k]=0;put(n);put(pl?u" pikseli":u" pixels");}
            if(bold)put(pl?u", pogrubiona":u", bold");
        };
        if(!(a&0x80)) {
            if(!height && !bold) {SayOwn(pl?u"Kolory tego miejsca nie są znane; tekst pochodzi z obrazu.":u"Colours here are unknown; the text comes from a picture.");return;}
            put(pl?u"Tekst z obrazu":u"Text from a picture");fontPart();put(u".");text[at]=0;SayOwn(text);return;
        }
        put(pl?u"Tekst ":u"Text ");put(pl?coloursPl[a&15]:coloursEn[a&15]);
        put(pl?u" na ":u" on ");put(pl?backgroundsPl[(a>>4)&7]:coloursEn[(a>>4)&7]);
        if((a&8) && (a&15)!=8)put(pl?u", jasny":u", bright");
        if((a&15)==8)put(pl?u", wyszarzony":u", greyed out");
        if(Highlighted(a))put(pl?u", podświetlony":u", highlighted");
        fontPart();
        put(u".");text[at]=0;SayOwn(text);
    }
    // r380: window frames. A top-left corner followed by a horizontal line.
    static bool Corner(char16_t ch) {return ch==0x250c || ch==0x2554 || ch==0x256d || ch==0x2552 || ch==0x2553;}
    static bool HLine(char16_t ch) {return ch==0x2500 || ch==0x2550 || ch==0x2501 || ch==u'-' || ch==u'=';}
    size_t FrameColumn(size_t r) const {
        if(r>=rows_)return SIZE_MAX;
        for(size_t c=0;c+2<columns_;++c)if(Corner(text_[r][c]) && HLine(text_[r][c+1])) {
            // Width up to the matching top-right corner.
            size_t e=c+1;while(e<columns_ && !(text_[r][e]==0x2510 || text_[r][e]==0x2557 || text_[r][e]==0x256e || text_[r][e]==0x2555 || text_[r][e]==0x2556))++e;
            const size_t width=e-c+1;
            char16_t title[maxColumns+1]={};FrameTitle(r,c,title,maxColumns+1);
            // A frame around (almost) the whole screen is the program's page, not a window.
            if(title[0] || width*10<columns_*7)return c;
            // r389: a wide frame in the middle of the screen (edk2's "Changes
            // have not saved" box) is a window; wide frames at the top or the
            // bottom (title and key-help boxes) belong to the page.
            size_t bottom=SIZE_MAX;
            for(size_t b=r+1;b<rows_;++b) {const char16_t ch=text_[b][c];if(ch==0x2514 || ch==0x255a || ch==0x2570 || ch==0x2558 || ch==0x2559) {bottom=b;break;}}
            if(bottom!=SIZE_MAX && r>=3 && bottom+4<rows_ && (bottom-r)*2<rows_)return c;
            return SIZE_MAX;
        }
        return SIZE_MAX;
    }
    void FrameTitle(size_t r,size_t column,char16_t* out,size_t capacity) const {
        size_t at=0;bool any=false;
        for(size_t c=column+1;c<columns_ && at+1<capacity;++c) {
            const char16_t ch=text_[r][c];
            if(ch==0x2510 || ch==0x2557 || ch==0x256e || ch==0x2555 || ch==0x2556)break;
            if(HLine(ch) || (ch>=0x2500 && ch<=0x257f)) {if(any && at && out[at-1]!=u' ')out[at++]=u' ';continue;}
            if(ch==u' ' && !any)continue;
            out[at++]=ch;any=true;
        }
        while(at && out[at-1]==u' ')--at;
        out[at]=0;
    }
    // r388: the voice settings ring (like NVDA's synth settings ring):
    // Ctrl+Alt+Left/Right choose rate, volume or pitch and say its value;
    // Ctrl+Alt+Up/Down change it in 10% steps. For this session only: the
    // reader never writes files while another program runs.
    void VoiceRing(char16_t c) {
        const bool pl=options_.polish;
        // r393: a fourth setting, the voice (TVox and the loaded synthesizer add-on's voices).
        auto* sfx=options_.effects;size_t synthCurrent=SIZE_MAX;
        const size_t synthVoices=sfx && sfx->SynthVoices && sfx->UseSynthVoice?sfx->SynthVoices(sfx->context,&synthCurrent):0;
        // r396 (user): a fifth setting, the synthesizer itself (TVox and the installed add-ons).
        const bool synthChoice=sfx && sfx->SynthAt && sfx->SetSynth;   // always listed, also with TVox alone
        const unsigned settingsCount=5;
        // r396 (user): order rate, volume, pitch, synthesizer, voice; the voice is one of the chosen synthesizer's only.
        auto usable=[&](unsigned s) {return s<3 || (s==3 && synthChoice) || s==4;};
        if(!usable(voiceSetting_))voiceSetting_=0;
        if(c==u'<' || c==u'>') {do voiceSetting_=(voiceSetting_+(c==u'>'?1:settingsCount-1))%settingsCount;while(!usable(voiceSetting_));}
        if(voiceSetting_==3) {
            uint8_t keys[9]={0};char16_t names[9][48]={};size_t states=1;
            {const char16_t* t=u"TVox";size_t i=0;for(;t[i];++i)names[0][i]=t[i];names[0][i]=0;}
            while(states<9 && sfx->SynthAt(sfx->context,states-1,&keys[states],names[states],48))++states;
            const uint8_t active=pendingSynth_!=0xffff?uint8_t(pendingSynth_):sfx->ActiveSynthKey?sfx->ActiveSynthKey(sfx->context):0;
            size_t at=0;for(size_t i=0;i<states;++i)if(keys[i]==active)at=i;
            char16_t text[160]={};size_t n=0;auto put=[&](const char16_t* s) {while(*s && n+1<160)text[n++]=*s++;text[n]=0;};
            if((c==u'^' || c==u'_') && states==1) {
                put(pl?u"Syntezator TVox. Brak dodatków syntezy w EFI, HAK, synths.":u"Synthesizer TVox. No synthesizer add-ons in EFI, HAK, synths.");SayOwn(text,false);return;}
            if(c==u'^' || c==u'_') {
                at=c==u'^'?(at+1)%states:(at+states-1)%states;
                Silence();
                const efi_tpl current=boot_->RaiseTPL(TPL_HIGH_LEVEL);boot_->RestoreTPL(current);
                const bool loaded=sfx->SynthLoaded && sfx->SynthLoaded(sfx->context,keys[at]);   // r396e: preloaded modules switch at TPL_CALLBACK
                if(current<=TPL_APPLICATION || (loaded && current<=TPL_CALLBACK)) {char16_t report[120]={};++VoiceSwitching();const bool ok=sfx->SetSynth(sfx->context,keys[at],0,report,120);--VoiceSwitching();pendingSynth_=0xffff;
                    put(pl?u"Syntezator ":u"Synthesizer ");put(ok?report:names[0]);
                    if(current<=TPL_APPLICATION) {Guard guard(boot_);SayOwn(text,false);} else SayOwn(text,false);
                    return;}
                pendingSynth_=keys[at];   // loaded at the next key read by the program (TPL_APPLICATION)
            }
            put(pl?u"Syntezator ":u"Synthesizer ");put(names[at]);SayOwn(text,false);return;
        }
        if(voiceSetting_==4) {
            char16_t text[160]={};size_t at=0;auto put=[&](const char16_t* s) {while(*s && at+1<160)text[at++]=*s++;text[at]=0;};
            put(pl?u"Głos ":u"Voice ");
            if(synthCurrent==SIZE_MAX || !synthVoices) {   // TVox: one voice
                put(u"TVox");if(c==u'^' || c==u'_')put(pl?u", ten syntezator ma jeden głos":u", this synthesizer has one voice");
                SayOwn(text,false);return;
            }
            if(c==u'^' || c==u'_') {
                size_t next=c==u'^'?(synthCurrent+1)%synthVoices:(synthCurrent+synthVoices-1)%synthVoices;
                char16_t name[64]={};Silence();
                if(sfx->UseSynthVoice(sfx->context,next,name,64))put(name);else put(u"TVox");
            } else {char16_t n[24];size_t k=0;Number(n,k,synthCurrent+1);n[k]=0;put(n);put(pl?u" z ":u" of ");k=0;Number(n,k,synthVoices);n[k]=0;put(n);}
            SayOwn(text,false);return;
        }
        unsigned& value=voiceSetting_==0?rate_:voiceSetting_==1?volume_:pitch_;
        const unsigned low=voiceSetting_==1?1:5,high=voiceSetting_==1?10:20;
        const char16_t* name=voiceSetting_==0?(pl?u"Tempo":u"Rate"):voiceSetting_==1?(pl?u"Głośność":u"Volume"):(pl?u"Wysokość":u"Pitch");
        bool edge=false;
        if(c==u'^' || c==u'_') {
            const unsigned next=c==u'^'?(value<high?value+1:high):(value>low?value-1:low);
            edge=next==value;value=next;
            auto* fx=options_.effects;
            if(fx && voiceSetting_==0 && fx->SetRate)fx->SetRate(fx->context,rate_);
            if(fx && voiceSetting_==2 && fx->SetPitch) {fx->SetPitch(fx->context,pitch_);appliedPitch_=pitch_;}
            if(fx && voiceSetting_==1 && fx->SetVolumes)fx->SetVolumes(fx->context,volume_,options_.effectsVolume);
        }
        char16_t text[80]={};size_t at=0;
        for(const char16_t* p=name;*p;++p)text[at++]=*p;
        text[at++]=u' ';
        if(edge) {const char16_t* e=c==u'^'?(pl?u"maksimum, ":u"maximum, "):(pl?u"minimum, ":u"minimum, ");while(*e)text[at++]=*e++;}
        Number(text,at,voiceSetting_==1?value*10:VoicePercent(value));text[at++]=u'%';text[at]=0;
        SayOwn(text,false);   // at the chosen pitch, so the new pitch is heard as it is
    }
    // ---- r396: HAK Boot Accessibility Protocol (sdk/hak_boot_accessibility.h)
    // A program describes its screen; the reader speaks the description in
    // the user's settings and does not read that program's screen by itself
    // until EndScreen, another image starts or the program returns.
    HAK_BOOT_A11Y_PROTOCOL a11y_{};efi_handle a11yHandle_=nullptr;
    bool semantic_=false;efi_handle semanticImage_=nullptr;
    static constexpr size_t focusText=256;
    char16_t focusName_[focusText]={},focusValue_[focusText]={};uint32_t focusRole_=UINT32_MAX,focusIndex_=0,focusCaret_=HAK_A11Y_NO_CARET,focusState_=0;
    static constexpr size_t programKeyMax=12,programKeyText=200;
    char16_t programKeyText_[programKeyMax][programKeyText]={};keyhelp::Entry programKeys_[programKeyMax]={};size_t programKeyCount_=0;efi_handle programKeysImage_=nullptr;
    int64_t a11yCountdownLast_=-1;
    bool SemanticNow() const {return semantic_ && semanticImage_==currentImage_;}
    size_t ProgramKeyCount() const {return programKeysImage_==currentImage_?programKeyCount_:0;}
    static size_t Copy16(char16_t* out,size_t capacity,const HAK_A11Y_CHAR16* in) {
        size_t n=0;if(in)for(;in[n] && n+1<capacity;++n)out[n]=char16_t(in[n]);
        out[n]=0;return n;
    }
    static bool Equal16(const char16_t* a,const char16_t* b) {size_t i=0;for(;a[i] && a[i]==b[i];++i) {}return a[i]==b[i];}
    void InstallAccessibility() {
        a11y_.Revision=HAK_BOOT_A11Y_REVISION;a11y_.Size=sizeof(a11y_);
        a11y_.Capabilities=HAK_A11Y_HAS_SPEECH|(braille_.Present()?HAK_A11Y_HAS_BRAILLE:0u);
        a11y_.Speak=A11ySpeak;a11y_.BeginScreen=A11yBegin;a11y_.Focus=A11yFocus;a11y_.Countdown=A11yCountdown;
        a11y_.Progress=A11yProgress;a11y_.SetKeyHelp=A11yKeys;a11y_.EndScreen=A11yEnd;a11y_.GetSettings=A11ySettings;
        static efi_guid guid=HAK_BOOT_A11Y_PROTOCOL_GUID;
        a11yHandle_=nullptr;
        if(boot_->InstallProtocolInterface && boot_->InstallProtocolInterface(&a11yHandle_,&guid,EFI_NATIVE_INTERFACE,&a11y_)!=EFI_SUCCESS)a11yHandle_=nullptr;
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader a11y protocol",a11yHandle_!=nullptr);
#endif
    }
    void UninstallAccessibility() {
        static efi_guid guid=HAK_BOOT_A11Y_PROTOCOL_GUID;
        if(a11yHandle_ && boot_->UninstallProtocolInterface)boot_->UninstallProtocolInterface(a11yHandle_,&guid,&a11y_);
        a11yHandle_=nullptr;semantic_=false;programKeyCount_=0;
    }
    static ResidentReader* A11yReader(HAK_BOOT_A11Y_PROTOCOL* p) {
        ResidentReader* r=active_;
        if(!r || !r->installed_ || p!=&r->a11y_ || r->stopped_ || r->suspended_ || !r->boot_)return nullptr;
        return r;
    }
    static HAK_A11Y_STATUS HAK_A11Y_EFIAPI A11ySpeak(HAK_BOOT_A11Y_PROTOCOL* p,const HAK_A11Y_CHAR16* text,uint32_t flags) {
        ResidentReader* r=A11yReader(p);if(!r)return EFI_NOT_READY;
        if(!text)return EFI_INVALID_PARAMETER;
        char16_t buffer[1600];Copy16(buffer,1600,text);
        Guard guard(r->boot_);
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader a11y speak",flags);
#endif
        if(flags&(HAK_A11Y_SPEAK_INTERRUPT|HAK_A11Y_SPEAK_ALERT))r->Silence();
        auto* fx=r->options_.effects;
        if((flags&HAK_A11Y_SPEAK_MESSAGE) && fx && fx->SpeakMessage && r->options_.messageVoice) {r->Add(r->pendingInfo_,1600,buffer);r->infoTick_=r->tick_;return EFI_SUCCESS;}
        if(r->quiet_ && !(flags&HAK_A11Y_SPEAK_ALERT))return EFI_SUCCESS;
        r->Speak(buffer);return EFI_SUCCESS;
    }
    static HAK_A11Y_STATUS HAK_A11Y_EFIAPI A11yBegin(HAK_BOOT_A11Y_PROTOCOL* p,const HAK_A11Y_CHAR16* title,uint32_t) {
        ResidentReader* r=A11yReader(p);if(!r)return EFI_NOT_READY;
        Guard guard(r->boot_);
        r->semantic_=true;r->semanticImage_=r->currentImage_;r->focusRole_=UINT32_MAX;r->focusName_[0]=0;r->a11yCountdownLast_=-1;
        r->pendingInfo_[0]=0;
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader a11y begin",uint64_t(uintptr_t(r->currentImage_))&0xffffffff);
#endif
        if(title && title[0] && !r->quiet_) {char16_t buffer[400];Copy16(buffer,400,title);r->Silence();r->Speak(buffer);}
        return EFI_SUCCESS;
    }
    static HAK_A11Y_STATUS HAK_A11Y_EFIAPI A11yEnd(HAK_BOOT_A11Y_PROTOCOL* p) {
        ResidentReader* r=A11yReader(p);if(!r)return EFI_NOT_READY;
        Guard guard(r->boot_);r->semantic_=false;r->ClearShadow();
        return EFI_SUCCESS;
    }
    const char16_t* RoleWord(uint32_t role) const {
        const bool pl=options_.polish;
        switch(role) {
            case HAK_A11Y_ROLE_SUBMENU:return pl?u"podmenu":u"submenu";
            case HAK_A11Y_ROLE_BUTTON:return pl?u"przycisk":u"button";
            case HAK_A11Y_ROLE_CHECK_BOX:return pl?u"pole wyboru":u"check box";
            case HAK_A11Y_ROLE_RADIO:return pl?u"przycisk opcji":u"radio button";
            case HAK_A11Y_ROLE_EDIT:return pl?u"edycja":u"edit";
            case HAK_A11Y_ROLE_PASSWORD:return pl?u"hasło":u"password";
            case HAK_A11Y_ROLE_COMBO:return pl?u"okno wyboru":u"combo box";
            case HAK_A11Y_ROLE_SLIDER:return pl?u"suwak":u"slider";
            case HAK_A11Y_ROLE_TAB:return pl?u"karta":u"tab";
            case HAK_A11Y_ROLE_DIALOG:return pl?u"okno dialogowe":u"dialog";
            case HAK_A11Y_ROLE_HEADING:return pl?u"nagłówek":u"heading";
            default:return nullptr;   // text, menu and list items: the name and position say enough
        }
    }
    static HAK_A11Y_STATUS HAK_A11Y_EFIAPI A11yFocus(HAK_BOOT_A11Y_PROTOCOL* p,const HAK_BOOT_A11Y_FOCUS* f) {
        ResidentReader* r=A11yReader(p);if(!r)return EFI_NOT_READY;
        if(!f || f->Size<sizeof(HAK_BOOT_A11Y_FOCUS) || f->Role>=HAK_A11Y_ROLE_COUNT)return EFI_INVALID_PARAMETER;
        char16_t name[focusText],value[focusText];
        Copy16(name,focusText,f->Name);const size_t length=Copy16(value,focusText,f->Value);
        Guard guard(r->boot_);
        const bool pl=r->options_.polish;
        const bool edit=f->Role==HAK_A11Y_ROLE_EDIT || f->Role==HAK_A11Y_ROLE_PASSWORD;
        const bool same=f->Role==r->focusRole_ && f->Index==r->focusIndex_ && Equal16(name,r->focusName_);
        char16_t text[1200];speechtext::Out o{text,1200};text[0]=0;
        auto own=[&](const char16_t* s) {o.Put(speechtext::ownOpen);o.Puts(s);o.Put(speechtext::ownClose);};
        if(same && edit) {
            // An edit field: only what changed (NVDA's typed-character and caret echo).
            size_t oldLength=0;while(r->focusValue_[oldLength])++oldLength;
            const uint32_t caret=f->Caret,oldCaret=r->focusCaret_;
            const bool secret=f->Role==HAK_A11Y_ROLE_PASSWORD;
            if(length==oldLength+1 && caret!=HAK_A11Y_NO_CARET && caret>0 && caret<=length) {
                if(secret)own(pl?u"gwiazdka":u"star");else {Copy16(r->focusValue_,focusText,f->Value);r->focusCaret_=caret;r->SayChar(value[caret-1]);return EFI_SUCCESS;}
            } else if(length+1==oldLength) {
                const uint32_t at=caret!=HAK_A11Y_NO_CARET && caret<oldLength?caret:uint32_t(oldLength-1);
                const char16_t gone=r->focusValue_[at];
                Copy16(r->focusValue_,focusText,f->Value);r->focusCaret_=caret;
                if(secret) {own(pl?u"usunięto":u"deleted");} else {r->SayChar(gone);return EFI_SUCCESS;}
            } else if(Equal16(value,r->focusValue_)) {
                r->focusCaret_=caret;
                if(caret==oldCaret || caret==HAK_A11Y_NO_CARET)return EFI_SUCCESS;
                if(secret)return EFI_SUCCESS;
                if(caret>=length) {r->SayOwn(pl?u"pusty":u"blank");return EFI_SUCCESS;}
                r->SayChar(value[caret]);return EFI_SUCCESS;
            } else {
                if(secret)own(pl?u"hasło zmienione":u"password changed");else o.Puts(length?value:(pl?u"pusty":u"blank"));
            }
            Copy16(r->focusValue_,focusText,f->Value);r->focusCaret_=caret;
            if(!r->quiet_)r->Speak(text);
            return EFI_SUCCESS;
        }
        if(same && !edit && Equal16(value,r->focusValue_) && f->State==r->focusState_)return EFI_SUCCESS;   // repeated
        if(same && !edit) {
            // The same item: its new value or state only (a check box toggled, a slider moved).
            if(!Equal16(value,r->focusValue_))o.Puts(value);
            if((f->State^r->focusState_)&HAK_A11Y_STATE_CHECKED) {if(text[0])o.Puts(u", ");own((f->State&HAK_A11Y_STATE_CHECKED)?(pl?u"zaznaczone":u"checked"):(pl?u"niezaznaczone":u"not checked"));}
            if((f->State^r->focusState_)&(HAK_A11Y_STATE_EXPANDED|HAK_A11Y_STATE_COLLAPSED)) {if(text[0])o.Puts(u", ");own((f->State&HAK_A11Y_STATE_EXPANDED)?(pl?u"rozwinięte":u"expanded"):(pl?u"zwinięte":u"collapsed"));}
        } else {
            if(name[0])o.Puts(name);
            if(const char16_t* role=r->RoleWord(f->Role)) {if(text[0])o.Puts(u", ");own(role);}
            if(f->Role==HAK_A11Y_ROLE_CHECK_BOX || f->Role==HAK_A11Y_ROLE_RADIO) {o.Puts(u", ");own((f->State&HAK_A11Y_STATE_CHECKED)?(pl?u"zaznaczone":u"checked"):(pl?u"niezaznaczone":u"not checked"));}
            if(f->Role==HAK_A11Y_ROLE_PASSWORD) {if(length) {o.Puts(u", ");o.Number(length);own(pl?u" znaków":u" characters");}}
            else if(value[0]) {if(text[0])o.Puts(u", ");o.Puts(value);}
            else if(edit) {o.Puts(u", ");own(pl?u"pusty":u"blank");}
            static const uint32_t bits[]={HAK_A11Y_STATE_EXPANDED,HAK_A11Y_STATE_COLLAPSED,HAK_A11Y_STATE_DISABLED,HAK_A11Y_STATE_DEFAULT,HAK_A11Y_STATE_READ_ONLY,HAK_A11Y_STATE_HIDDEN,HAK_A11Y_STATE_ERROR};
            static const char16_t* wordsPl[]={u"rozwinięte",u"zwinięte",u"niedostępne",u"domyślne",u"tylko do odczytu",u"ukryte",u"błąd"};
            static const char16_t* wordsEn[]={u"expanded",u"collapsed",u"unavailable",u"default",u"read only",u"hidden",u"error"};
            for(size_t i=0;i<7;++i)if(f->State&bits[i]) {o.Puts(u", ");own((pl?wordsPl:wordsEn)[i]);}
            if(f->Level) {o.Puts(u", ");own(pl?u"poziom ":u"level ");o.Number(f->Level+1);}
            if(f->Index && f->Count && r->options_.positions) {o.Puts(u", ");o.Number(f->Index);own(pl?u" z ":u" of ");o.Number(f->Count);}
            if(f->Description && f->Description[0]) {char16_t d[400];Copy16(d,400,f->Description);o.Puts(u". ");o.Puts(d);}
            if(edit && f->Caret!=HAK_A11Y_NO_CARET && f->Caret<length && r->options_.verbosity>=2) {o.Puts(u", ");own(pl?u"kursor na ":u"caret on ");o.Number(f->Caret+1);}
        }
        r->focusRole_=f->Role;r->focusIndex_=f->Index;r->focusState_=f->State;r->focusCaret_=f->Caret;
        Copy16(r->focusName_,focusText,f->Name);Copy16(r->focusValue_,focusText,f->Value);
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader a11y focus",(uint64_t(f->Role)<<32)|f->Index);
#endif
        if(!text[0] || r->quiet_)return EFI_SUCCESS;
        r->Silence();r->Speak(text);
        // Braille and review follow the item: the reader's review row is the
        // screen's; the spoken line is also kept in the history (Insert+H).
        return EFI_SUCCESS;
    }
    static void PluralSeconds(speechtext::Out& o,uint32_t n,bool pl) {
        o.Number(n);
        if(!pl) {o.Puts(n==1?u" second":u" seconds");return;}
        const uint32_t last=n%10,tens=n%100;
        o.Puts(n==1?u" sekundę":(last>=2 && last<=4 && !(tens>=12 && tens<=14))?u" sekundy":u" sekund");
    }
    static HAK_A11Y_STATUS HAK_A11Y_EFIAPI A11yCountdown(HAK_BOOT_A11Y_PROTOCOL* p,uint32_t left,uint8_t* stop) {
        ResidentReader* r=A11yReader(p);if(!r)return EFI_NOT_READY;
        Guard guard(r->boot_);
        if(stop)*stop=0;
        if(r->options_.managerFlags&4) {
            if(stop)*stop=1;
            if(r->a11yCountdownLast_!=-2) {r->a11yCountdownLast_=-2;r->SayOwn(r->options_.polish?u"Odliczanie zatrzymane.":u"Countdown stopped.");}
            return EFI_SUCCESS;
        }
        auto* fx=r->options_.effects;
        const bool first=r->a11yCountdownLast_<0;r->a11yCountdownLast_=left;
        if((r->options_.timers&2) && fx && fx->Tone) {
            if(left==0) {fx->Tone(fx->context,880,500);r->endToneUntil_=r->tick_+50;}
            else fx->Tone(fx->context,1000,40);
        }
        if(((r->options_.timers&1) && left && left%10==0) || first) {
            char16_t text[120];speechtext::Out o{text,120};text[0]=0;
            if(first) {o.Put(speechtext::ownOpen);o.Puts(r->options_.polish?u"Automatyczny start za ":u"Automatic start in ");PluralSeconds(o,left,r->options_.polish);o.Put(speechtext::ownClose);}
            else {o.Put(speechtext::ownOpen);PluralSeconds(o,left,r->options_.polish);o.Put(speechtext::ownClose);}
            if(!r->quiet_)r->Speak(text);
        }
        return EFI_SUCCESS;
    }
    static HAK_A11Y_STATUS HAK_A11Y_EFIAPI A11yProgress(HAK_BOOT_A11Y_PROTOCOL* p,uint32_t percent,const HAK_A11Y_CHAR16* label) {
        ResidentReader* r=A11yReader(p);if(!r)return EFI_NOT_READY;
        if(percent>100)return EFI_INVALID_PARAMETER;
        Guard guard(r->boot_);
        if(label && label[0] && r->lastPercent_<0 && !r->quiet_) {char16_t buffer[300];Copy16(buffer,300,label);r->Speak(buffer);}
        r->Progress(int(percent));
        if(percent>=100)r->lastPercent_=-1;
        return EFI_SUCCESS;
    }
    static HAK_A11Y_STATUS HAK_A11Y_EFIAPI A11yKeys(HAK_BOOT_A11Y_PROTOCOL* p,const HAK_A11Y_CHAR16* const* lines,uint32_t count) {
        ResidentReader* r=A11yReader(p);if(!r)return EFI_NOT_READY;
        if(count && !lines)return EFI_INVALID_PARAMETER;
        Guard guard(r->boot_);
        size_t n=0;
        for(uint32_t i=0;i<count && n<programKeyMax;++i) {
            if(!lines[i] || !lines[i][0])continue;
            Copy16(r->programKeyText_[n],programKeyText,lines[i]);
            r->programKeys_[n].pl=r->programKeyText_[n];r->programKeys_[n].en=r->programKeyText_[n];++n;
        }
        r->programKeyCount_=n;r->programKeysImage_=r->currentImage_;
        return count>programKeyMax?EFI_BUFFER_TOO_SMALL:EFI_SUCCESS;
    }
    static HAK_A11Y_STATUS HAK_A11Y_EFIAPI A11ySettings(HAK_BOOT_A11Y_PROTOCOL* p,HAK_BOOT_A11Y_SETTINGS* s) {
        ResidentReader* r=A11yReader(p);if(!r)return EFI_NOT_READY;
        if(!s || s->Size<sizeof(HAK_BOOT_A11Y_SETTINGS))return EFI_INVALID_PARAMETER;
        s->Flags=HAK_A11Y_HAS_SPEECH|(r->braille_.Present()?HAK_A11Y_HAS_BRAILLE:0u)|((r->options_.managerFlags&4)?HAK_A11Y_STOP_COUNTDOWNS:0u)|(r->options_.positions?HAK_A11Y_SAY_POSITIONS:0u);
        for(size_t i=0;i<8;++i)s->Language[i]=0;
        s->Language[0]=r->options_.polish?'p':'e';s->Language[1]=r->options_.polish?'l':'n';
        s->Verbosity=r->options_.verbosity;s->Reserved=0;
        return EFI_SUCCESS;
    }
    // r396: a synthesizer chosen in the ring while keys arrived at a raised
    // TPL is loaded here, when the program reads a key at TPL_APPLICATION.
    bool applyingSynth_=false;
    void ApplyPendingSynth() {
        if(pendingSynth_==0xffff || !installed_ || stopped_ || suspended_ || !boot_ || applyingSynth_)return;
        auto* sfx=options_.effects;if(!sfx || !sfx->SetSynth) {pendingSynth_=0xffff;return;}
        const efi_tpl current=boot_->RaiseTPL(TPL_HIGH_LEVEL);boot_->RestoreTPL(current);
        if(current>TPL_APPLICATION)return;
        const uint8_t key=uint8_t(pendingSynth_);pendingSynth_=0xffff;
        applyingSynth_=true;   // loading a module may print or stall: no re-entry
        ++VoiceSwitching();
        char16_t report[120]={};const bool ok=sfx->SetSynth(sfx->context,key,0,report,120);
        --VoiceSwitching();
        applyingSynth_=false;
        char16_t text[160]={};size_t n=0;auto put=[&](const char16_t* s) {while(*s && n+1<160)text[n++]=*s++;text[n]=0;};
        put(options_.polish?u"Syntezator ":u"Synthesizer ");put(ok?report:u"TVox");
        {Guard guard(boot_);SayOwn(text,false);}
    }
    bool Review(char16_t c) {
        if(c==u'w') {
            const bool pl=options_.polish;
            char16_t text[1200]={};size_t at=0;
            auto put=[&](const char16_t* s) {while(*s && at+1<1200)text[at++]=*s++;};
            put(pl?u"Program: ":u"Program: ");put(ProgramName());put(u". ");
            FindScreenKeys();   // r397: a known screen of it (editor, command line, options)
            if(screenName_) {put(pl?screenName_->pl:screenName_->en);put(u". ");}
            size_t first=SIZE_MAX;for(size_t r=0;r<rows_;++r)if(RowHasText(r)) {first=r;break;}
            if(first!=SIZE_MAX) {char16_t line[maxColumns*4]={};RowText(first,line,sizeof(line)/2);
                size_t n=0;while(line[n])++n;while(n && line[n-1]==u' ')line[--n]=0;
                put(pl?u"Ekran: ":u"Screen: ");put(line);put(u". ");}
            {TabBar tab;   // r390 (user): the open tab of a firmware setup's tab bar
             if(FindTabBar(tab)) {put(tab.name);put(pl?u", karta ":u", tab ");
                char16_t n[24];size_t k=0;Number(n,k,tab.index+1);n[k]=0;put(n);put(pl?u" z ":u" of ");k=0;Number(n,k,tab.count);n[k]=0;put(n);put(u". ");}}
            if(frameRow_<rows_) {char16_t title[maxColumns+1]={};FrameTitle(frameRow_,frameColumn_,title,maxColumns+1);
                put(pl?u"Okno":u"Window");if(title[0]) {put(u": ");put(title);}put(u". ");}
            const size_t lit=highlight_<rows_?highlight_:lastHighlightRow_;
            char16_t selected[maxColumns*4]={};
            if(lit<rows_ && (RowText(lit,selected,sizeof(selected)/2,true) || RowText(lit,selected,sizeof(selected)/2)) && selected[0]) {
                put(pl?u"Zaznaczone: ":u"Highlighted: ");put(selected);put(u".");}
            text[at]=0;SayOwn(text);return true;
        }
        if(c==u'y') {
            // The key-hint bar: the last one or two text rows at the bottom.
            size_t last=SIZE_MAX;for(size_t r=rows_;r-->0;)if(RowHasText(r)) {last=r;break;}
            if(last==SIZE_MAX) {SayOwn(options_.polish?u"Brak tekstu na ekranie, który Charon może odczytać.":u"No text on the screen Charon can read.");return true;}
            char16_t text[maxColumns*12]={};size_t at=0;
            size_t first=last;while(first && last-first<2 && RowHasText(first-1))--first;
            for(size_t r=first;r<=last;++r) {
                char16_t line[maxColumns*4]={};RowText(r,line,sizeof(line)/2);
                if(at && at+2<maxColumns*12)text[at++]=u' ';
                for(size_t i=0;line[i] && at+1<maxColumns*12;++i)text[at++]=line[i];
                while(at && text[at-1]==u' ')--at;
            }
            text[at]=0;
            Speak(text);return true;
        }
        if(c==u'v') {
            options_.verbosity=options_.verbosity>=2?1:2;
            SayOwn(options_.verbosity>=2?(options_.polish?u"Czytnik mówi szczegółowo.":u"Reader verbosity detailed."):(options_.polish?u"Czytnik mówi krótko.":u"Reader verbosity short."));
            return true;
        }
        if(c==u'q') {
            quiet_=!quiet_;
            SayOwn(quiet_?(options_.polish?u"Automatyczne czytanie wyłączone. Klawisze przeglądu nadal działają.":u"Automatic reading off. Review keys still work.")
                :(options_.polish?u"Automatyczne czytanie włączone.":u"Automatic reading on."));
            return true;
        }
        if(c==u'c') {
            char16_t text[400]={};
            if(!table_ || systools::ClockText(table_->RuntimeServices,options_.polish,text,400)!=EFI_SUCCESS)
                SayOwn(options_.polish?u"Firmware nie podało czasu zegara.":u"Firmware did not return the clock time.");
            else SayOwn(text);
            return true;
        }
        if(c==u'<' || c==u'>' || c==u'^' || c==u'_') {VoiceRing(c);return true;}
        if(c==u'=' || c==u'-') {
            const unsigned next=c==u'='?(rate_<20?rate_+1:20):(rate_>5?rate_-1:5);
            if(next==rate_) {SayOwn(c==u'='?(options_.polish?u"Tempo maksymalne, 100%":u"Maximum rate, 100%"):(options_.polish?u"Tempo minimalne, 0%":u"Minimum rate, 0%"));return true;}
            rate_=next;
            if(options_.effects && options_.effects->SetRate)options_.effects->SetRate(options_.effects->context,rate_);
            char16_t text[48]={};size_t at=0;const char16_t* head=options_.polish?u"Tempo ":u"Rate ";while(*head)text[at++]=*head++;
            Number(text,at,VoicePercent(rate_));text[at++]=u'%';text[at]=0;SayOwn(text);
            return true;
        }
        if(c==u'a') {
            ReviewStart();
            if(reviewRow_>=rows_) {SayOwn(options_.polish?u"Brak tekstu na ekranie, który Charon może odczytać.":u"No text on the screen Charon can read.");return true;}
            char16_t all[1600]={};size_t at=0;size_t last=reviewRow_;
            for(size_t r=reviewRow_;r<rows_ && at+4<1600;++r) {
                if(!RowHasText(r))continue;
                char16_t line[maxColumns*4]={};RowText(r,line,sizeof(line)/2);
                size_t n=0;while(line[n])++n;while(n && line[n-1]==u' ')--n;
                if(!n)continue;
                if(at) {all[at++]=u'.';all[at++]=u' ';}
                for(size_t i=0;i<n && at+1<1600;++i)all[at++]=line[i];
                last=r;
            }
            all[at]=0;reviewRow_=last;reviewCol_=TextStart(last);
            Speak(all);return true;
        }
        if(c==u'n') {ReviewStart();if(reviewRow_<rows_)SayColours(reviewRow_,reviewCol_<columns_?reviewCol_:0);return true;}
        if(!(c==u'u' || c==u'i' || c==u'o' || c==u'j' || c==u'k' || c==u'l' || c==u'm' || c==u',' || c==u'.' || c==u't' || c==u'b' || c==u'e' || c==u'p'))return false;
        const bool pl=options_.polish;
        if(c==u'e') {
            char16_t selected[maxColumns*4]={};
            // Flush() forgets highlight_ after speaking; the last highlighted row stays.
            const size_t lit=highlight_<rows_?highlight_:lastHighlightRow_;
            if(lit<rows_ && (RowText(lit,selected,sizeof(selected)/2,true) || RowText(lit,selected,sizeof(selected)/2)) && selected[0]) {
                size_t index=0,count=0;MenuBlock(lit,index,count);
                char16_t text[maxColumns*4+40]={};size_t at=0;for(size_t i=0;selected[i];++i)text[at++]=selected[i];
                if(count>=2) {const char16_t* comma=u", ";while(*comma)text[at++]=*comma++;Number(text,at,index+1);
                    const char16_t* of=pl?u" z ":u" of ";while(*of)text[at++]=*of++;Number(text,at,count);}
                text[at]=0;Speak(text);
            } else SayOwn(pl?u"Na ekranie nie ma zaznaczonej pozycji.":u"Nothing is highlighted on the screen.");
            return true;
        }
        ReviewStart();
        if(reviewRow_>=rows_) {SayOwn(pl?u"Brak tekstu na ekranie, który Charon może odczytać.":u"No text on the screen Charon can read.");return true;}
        size_t r=reviewRow_,col=reviewCol_;
        if(col>=columns_)col=columns_?columns_-1:0;
        switch(c) {
            case u'p': {
                char16_t text[96]={};size_t at=0;
                const char16_t* a=pl?u"Wiersz ":u"Line ";while(*a)text[at++]=*a++;Number(text,at,r+1);
                a=pl?u" z ":u" of ";while(*a)text[at++]=*a++;Number(text,at,rows_);
                a=pl?u", kolumna ":u", column ";while(*a)text[at++]=*a++;Number(text,at,col+1);text[at++]=u'.';text[at]=0;
                SayOwn(text);return true;}
            case u't':case u'b': {
                size_t k=c==u't'?0:rows_-1;
                if(c==u't') {while(k<rows_ && !RowHasText(k))++k;} else {while(k && !RowHasText(k))--k;}
                if(k<rows_ && RowHasText(k)) {reviewRow_=k;reviewCol_=TextStart(k);SayRow(k);}
                return true;}
            case u'i':
                if(pressCount_>=2) {char16_t line[maxColumns*4]={};RowText(r,line,sizeof(line)/2);size_t n=0;while(line[n])++n;SpellText(line,n);return true;}
                SayRow(r);return true;
            case u'u':case u'o':
                if(!NextTextRow(r,c==u'o')) {SayEdge(c==u'u');return true;}
                reviewRow_=r;reviewCol_=TextStart(r);SayRow(r);return true;
            case u'k':
                if(pressCount_>=2 && !Blank(text_[r][col])) {size_t a=col;while(a && !Blank(text_[r][a-1]))--a;size_t b=col;while(b<columns_ && !Blank(text_[r][b]))++b;SpellText(text_[r]+a,b-a);return true;}
                SayWordAt(r,col);return true;
            case u'l': {
                size_t k=col;while(k<columns_ && !Blank(text_[r][k]))++k;
                while(k<columns_ && Blank(text_[r][k]))++k;
                if(k>=RowEnd(r)) {if(!NextTextRow(r,true)) {SayEdge(false);return true;}k=TextStart(r);}
                reviewRow_=r;reviewCol_=k;SayWordAt(r,k);return true;}
            case u'j': {
                size_t k=col;while(k && !Blank(text_[r][k-1]))--k;
                // k is the start of the current word; step to the previous one.
                size_t m=k;while(m && Blank(text_[r][m-1]))--m;
                if(!m || m<=TextStart(r)) {
                    if(m && m>TextStart(r)) {}
                    else if(!NextTextRow(r,false)) {SayEdge(true);return true;}
                    else m=RowEnd(r);
                }
                if(!m) {SayEdge(true);return true;}
                size_t a=m-1;while(a && !Blank(text_[r][a-1]))--a;
                reviewRow_=r;reviewCol_=a;SayWordAt(r,a);return true;}
            case u'.': {
                size_t k=col+1;
                if(k>=RowEnd(r)) {if(!NextTextRow(r,true)) {SayEdge(false);return true;}k=TextStart(r);}
                reviewRow_=r;reviewCol_=k;SayChar(text_[r][k]);return true;}
            case u'm': {
                size_t k=col;
                if(!k || k<=TextStart(r)) {if(!NextTextRow(r,false)) {SayEdge(true);return true;}k=RowEnd(r);}
                --k;reviewRow_=r;reviewCol_=k;SayChar(text_[r][k]);return true;}
            case u',':SayChar(text_[r][col],pressCount_);return true;
        }
        return false;
    }
    // ---- NVDA desktop layout: Insert + key (r381) ------------------------
    // The console's key reads are wrapped; an Insert arms the layout for 3 s
    // and the next key, when it is a reader command, is consumed. Everything
    // else passes through unchanged. Only our own wrappers are ever removed.
    void HookInput() {
        if(!table_ || stopped_)return;
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader hook input",(uint64_t(inputHooked_!=nullptr)<<1)|(exHooked_!=nullptr));
#endif
        auto* in=table_->ConIn;
        if(in && in!=inputHooked_ && in->ReadKeyStroke && in->ReadKeyStroke!=HookRead) {
            if(inputHooked_ && inputHooked_->ReadKeyStroke==HookRead)inputHooked_->ReadKeyStroke=originalRead_;
            originalRead_=in->ReadKeyStroke;in->ReadKeyStroke=HookRead;inputHooked_=in;
        }
        if(!exHooked_ && table_->ConsoleInHandle) {
            efi_guid guid={0xdd9e7534,0x7762,0x4698,{0x8c,0x14,0xf5,0x85,0x17,0xa6,0x25,0xaa}};
            TextInputEx* ex=nullptr;
            if(table_->BootServices->HandleProtocol(table_->ConsoleInHandle,&guid,(void**)&ex)==EFI_SUCCESS && ex && ex->ReadKeyStrokeEx && ex->ReadKeyStrokeEx!=HookReadEx) {
                originalReadEx_=ex->ReadKeyStrokeEx;ex->ReadKeyStrokeEx=HookReadEx;exHooked_=ex;
            }
        }
    }
    void UnhookInput() {
        if(inputHooked_ && inputHooked_->ReadKeyStroke==HookRead)inputHooked_->ReadKeyStroke=originalRead_;
        if(exHooked_ && exHooked_->ReadKeyStrokeEx==HookReadEx)exHooked_->ReadKeyStrokeEx=originalReadEx_;
        inputHooked_=nullptr;exHooked_=nullptr;insertArmed_=false;
    }
    // r389: a key the program reads. Its screen reaction is read sooner
    // (20 ms of quiet instead of 60) and, at a prompt or in an edit field,
    // a moved cursor is read (NVDA: character or line under the caret).
    void NoteKey(uint16_t scan,char16_t ch) {
        if(stopped_ || suspended_)return;
        SecretKey();   // r407
        if(!keyPending_)EchoFromCaret();
        if(!keyPending_ || tick_-keyTick_>20)SnapshotBeforeKey();   // r393 "what changed"
        keyPending_=true;keyTick_=tick_;keyScan_=scan;keyChar_=secret_.Entry() && ch>=32?u'*':ch;   // r407: a secret key is not remembered
        everKey_=true;if(quietMenu_) {quietMenu_=0;rereadAfterKey_=true;}   // r400: the held menu is read now
    }
    bool KeyRecent() const {return keyPending_ && tick_-keyTick_<50;}
    // r389: a key's answer of one or two lines (menu move, typed character)
    // is read after 20 ms; bigger redraws (a window opening) wait for 60 ms
    // of quiet, so they are not split.
    size_t DirtyRows() const {size_t n=0;for(size_t r=0;r<rows_;++r)if(dirty_[r])++n;return n;}
    bool CursorShown() const {return real_ && real_->Mode && real_->Mode->CursorVisible;}
    void CursorEcho() {
        if(options_.reviewOnly)return;   // r401: Charon echoes its own keys
        if(secret_.Entry() && row_==secret_.row) {echoRow_=row_;echoCol_=column_;keyPending_=false;return;}   // r407: nothing under the caret of a secret entry is read
        size_t cr=0,cc=0;const bool drawn=DrawnCaret(cr,cc);
        if(!drawn) {cr=row_;cc=column_;}
        if(!KeyRecent() || !(drawn || CursorShown()) || cr>=rows_)return;
        if(!(options_.readerFlags&readerFlagCursor)) {echoRow_=cr;echoCol_=cc;keyPending_=false;return;}   // r390 setting
        if(!drawn && tick_-cursorTick_<2)return;   // r389: still moving (GRUB's editor repaints, then places it); the timer retries
        if(cr==echoRow_ && cc==echoCol_)return;
        const bool vertical=cr!=echoRow_;const size_t oldCol=echoCol_;
        echoRow_=cr;echoCol_=cc;keyPending_=false;
        reviewRow_=cr;reviewCol_=cc;   // r390: review and braille follow the caret
        if(quiet_)return;
        if(vertical) {
            // r397: a blank caret row is news only after Up or Down (a new
            // screen or program puts its caret on a row it has not drawn yet).
            if(keyScan_!=1 && keyScan_!=2) {
                // ... nor a row without a letter or digit (systemd-boot's "─────" above its help line)
                bool word=false;for(size_t c=0;c<columns_ && !word;++c) {const char16_t ch=text_[cr][c];word=(ch>=u'0' && ch<=u'9') || (ch>=u'A' && ch<=u'Z') || (ch>=u'a' && ch<=u'z') || (ch>=0xc0 && ch<0x2000);}
                if(!word)return;
            }
            SayRow(cr);return;}
        // The line's end, without an edit box's right border (GRUB's editor).
        size_t end=columns_;while(end && (text_[cr][end-1]==u' ' || !text_[cr][end-1] || (text_[cr][end-1]>=0x2500 && text_[cr][end-1]<=0x257f)))--end;
        // A typed space moves the caret but changes no visible text.
        if(keyChar_==u' ' && cc==oldCol+1 && !vertical) {SayChar(u' ');return;}
        if(cc>=end) {SayOwn(options_.polish?u"pusty":u"blank");return;}
        if(text_[cr][cc]>=0x2500 && text_[cr][cc]<=0x257f)return;   // r397: a box's frame under a caret still being placed (a password box opening)
        SayChar(text_[cr][cc]);   // arrows, Home, End: the character at the caret (NVDA)
    }
    static void CharName(char16_t ch,char16_t* out,size_t capacity,bool pl) {
        speechtext::Out o{out,capacity};speechtext::CharName(ch,pl,true,o);
    }
    // r389: Ctrl alone silences speech (NVDA). A modifier makes no key, so
    // the keyboard's state is read while no key is waiting (edk2's PS/2 and
    // USB drivers report it with EFI_NOT_READY); a key that arrives in that
    // instant is handed back to the program by the read hooks.
    void PollModifiers() {
        if(!exHooked_ || !originalReadEx_ || havePush_)return;
        efi_event wait=exHooked_->WaitForKeyEx;
        if(wait && boot_->CheckEvent(wait)==EFI_SUCCESS) {boot_->SignalEvent(wait);return;}   // a key waits: leave it
        KeyData data{};
        const auto status=originalReadEx_(exHooked_,&data);
        if(status==EFI_SUCCESS) {
            if(!data.key.ScanCode && !data.key.UnicodeChar)return;   // a partial keystroke: nothing to give back
            pushed_=data;havePush_=true;
            if(wait)boot_->SignalEvent(wait);
            if(table_->ConIn && table_->ConIn->WaitForKey)boot_->SignalEvent(table_->ConIn->WaitForKey);
            return;
        }
        if(data.state.toggle&0x80)keypad_.SetNumLock((data.state.toggle&0x02)!=0);   // r390: EFI_NUM_LOCK_ACTIVE
        if(!(data.state.shift&shiftValid))return;
        const bool control=(data.state.shift&(leftControl|rightControl))!=0;
        if(control && !controlDown_)Silence();
        controlDown_=control;
    }
    void Silence() {
        pending_[0]=0;pendingInfo_[0]=0;
        auto* fx=options_.effects;
        if(fx && fx->Silence)fx->Silence(fx->context);
    }
    // ---- r390 input help (NVDA+1) -------------------------------------
    const char16_t* CommandHelp(char16_t c) const {
        const bool pl=options_.polish;
        struct Help {char16_t c;const char16_t* pl;const char16_t* en;};
        static const Help help[]={
            {u'r',u"Czyta cały ekran od nowa",u"Reads the whole screen again"},{u's',u"Ucisza mowę",u"Stops speech"},{u'h',u"Mówi skróty Charona",u"Says Charon's shortcuts"},
            {u'u',u"Poprzedni wiersz",u"Previous line"},{u'i',u"Bieżący wiersz; dwa razy: literuje",u"Current line; twice: spells it"},{u'o',u"Następny wiersz",u"Next line"},
            {u'j',u"Poprzednie słowo",u"Previous word"},{u'k',u"Bieżące słowo; dwa razy: literuje",u"Current word; twice: spells it"},{u'l',u"Następne słowo",u"Next word"},
            {u'm',u"Poprzedni znak",u"Previous character"},{u',',u"Bieżący znak; dwa razy: alfabet fonetyczny, trzy razy: kod",u"Current character; twice: phonetic, three times: its code"},{u'.',u"Następny znak",u"Next character"},
            {u't',u"Pierwszy wiersz ekranu",u"Top line of the screen"},{u'b',u"Ostatni wiersz ekranu",u"Bottom line of the screen"},{u'e',u"Zaznaczona pozycja z położeniem",u"The highlighted entry with its position"},
            {u'p',u"Położenie kursora przeglądu",u"Where the review cursor is"},{u'a',u"Czyta od kursora przeglądu do końca",u"Reads from the review cursor to the end"},
            {u'q',u"Automatyczne czytanie zmian włącza lub wyłącza",u"Turns automatic reading of changes on or off"},{u'n',u"Formatowanie w miejscu kursora przeglądu: kolory, jasność, podświetlenie, czcionka",u"Formatting at the review cursor: colours, brightness, highlight, font"},
            {u'c',u"Godzina zegara firmware",u"Time of the firmware clock"},{u'=',u"Mowa szybciej",u"Speech faster"},{u'-',u"Mowa wolniej",u"Speech slower"},
            {u'w',u"Gdzie jestem: program, ekran, zaznaczenie",u"Where am I: program, screen, highlight"},{u'y',u"Pasek klawiszy na dole ekranu",u"The key-hint bar at the bottom"},
            {u'v',u"Szczegółowość czytnika",u"Reader verbosity"},{u'd',u"Poziom interpunkcji",u"Symbol level"},{u'z',u"Język tekstu ekranu",u"Screen text language"},
            {u'x',u"Pomoc klawiszy",u"Input help"},{u'\\',u"Przegląd ekranu ze wstrzymanym rozruchem",u"Screen review with the boot paused"},
            {u'^',u"Zwiększa wybrane ustawienie głosu",u"Raises the chosen voice setting"},{u'_',u"Zmniejsza wybrane ustawienie głosu",u"Lowers the chosen voice setting"},
            {u'<',u"Poprzednie ustawienie głosu",u"Previous voice setting"},{u'>',u"Następne ustawienie głosu",u"Next voice setting"},
            {u'G',u"Formatowanie tekstu w mowie: wyłączone, głosem, dźwiękiem, głosem i dźwiękiem",u"Text formatting in speech: off, voice, sounds, voice and sounds"},
            {u'H',u"Historia mowy",u"Speech history"},{u'D',u"Co się zmieniło od ostatniego klawisza",u"What changed since the last key"},
            {u'{',u"Poprzedni region ekranu",u"Previous screen region"},{u'}',u"Następny region ekranu",u"Next screen region"},
            {u'M',u"Klawisze działającego menedżera rozruchu",u"Keys of the running boot manager"},
            {u'W',u"Okno ekranu jak w Speakup: lewy górny róg, potem prawy dolny; na ustawionym oknie usuwa je",u"Screen window, as in Speakup: top left, then bottom right; on a set window clears it"},
            {u'S',u"Wycisza okno albo znów je czyta",u"Silences the window or reads it again"},{u'R',u"Czyta okno",u"Reads the window"},
            {u'C',u"Początek zaznaczenia w miejscu kursora przeglądu",u"Start mark at the review cursor"},
            {u'K',u"Kopiuje od początku zaznaczenia do kursora przeglądu",u"Copies from the start mark to the review cursor"},
            {u'V',u"Wkleja skopiowany tekst do programu jako klawisze",u"Pastes the copied text into the program as keys"},
            {u'I',u"Wiersz z wcięciem",u"The line with its indentation"}};
        for(const auto& h:help)if(h.c==c)return pl?h.pl:h.en;
        return pl?u"Nieznane polecenie":u"Unknown command";
    }
    void SayKeyName(const efi_input_key& key) {
        const bool pl=options_.polish;
        static const char16_t* scansPl[]={u"",u"strzałka w górę",u"strzałka w dół",u"strzałka w prawo",u"strzałka w lewo",u"Home",u"End",u"Insert",u"Delete",u"Page Up",u"Page Down",
            u"F1",u"F2",u"F3",u"F4",u"F5",u"F6",u"F7",u"F8",u"F9",u"F10",u"F11",u"F12",u"Escape"};
        static const char16_t* scansEn[]={u"",u"up arrow",u"down arrow",u"right arrow",u"left arrow",u"home",u"end",u"insert",u"delete",u"page up",u"page down",
            u"F1",u"F2",u"F3",u"F4",u"F5",u"F6",u"F7",u"F8",u"F9",u"F10",u"F11",u"F12",u"escape"};
        if(key.ScanCode && key.ScanCode<=0x17) {SayOwn(pl?scansPl[key.ScanCode]:scansEn[key.ScanCode]);return;}
        if(key.ScanCode) {SayOwn(pl?u"klawisz specjalny":u"special key");return;}
        const char16_t ch=key.UnicodeChar;
        if(ch==13) {SayOwn(pl?u"Enter":u"enter");return;}
        if(ch==8) {SayOwn(pl?u"Backspace":u"backspace");return;}
        if(ch==9) {SayOwn(pl?u"Tab":u"tab");return;}
        if(ch && ch<32) {char16_t t[40];speechtext::Out o{t,40};o.Puts(pl?u"Control ":u"control ");o.Put(char16_t(u'A'+ch-1));SayOwn(t);return;}
        SayChar(ch);
    }
    // ---- r393 text formatting in speech (CharonFormatting.h) ------------
    // The utterance is aligned with the screen; style changes become voice
    // and sound-icon markers. Off unless "voice" or "sounds" is chosen.
    // r397: own=true (the reader's own words): only an event icon.
    bool FormatSpeech(const char16_t* in,char16_t* out,size_t capacity,bool own=false) {
        using namespace formatting;
        Extras extras;
        if(EventsOn() && event_!=Event::none) {extras.event=event_;event_=Event::none;}
        if(!own)extras.stereo=options_.stereo<3?options_.stereo:0;
        const uint8_t flags=own?uint8_t(options_.formatFlags&flagEvents):options_.formatFlags;
        if((!(flags&(flagVoice|flagIcons)) && extras.event==Event::none && extras.stereo==stereoOff) || stopped_ || !in || !in[0])return false;
        Grid& g=grid_;g.rows=rows_<Grid::maxRows?rows_:Grid::maxRows;g.columns=columns_;
        size_t fgCount[16]={};
        uint16_t fonts[8]={};size_t fontCount[8]={};size_t fontKinds=0;
        for(size_t r=0;r<g.rows;++r)for(size_t c=0;c<columns_;++c) {
            const uint8_t a=attr_[r][c];if(!(a&0x80) || text_[r][c]==u' ' || !text_[r][c])continue;
            ++fgCount[a&15];
            const uint16_t f=uint16_t(cellFont_[r][c]&0x3fff);size_t k=0;
            while(k<fontKinds && fonts[k]!=f)++k;
            if(k==fontKinds && fontKinds<8) {fonts[fontKinds]=f;fontCount[fontKinds]=0;++fontKinds;}
            if(k<fontKinds)++fontCount[k];
        }
        Base base;base.bg=majorityBg_;
        {size_t best=7;for(size_t i=0;i<16;++i)if(fgCount[i]>fgCount[best])best=i;base.fg=uint8_t(best);}
        {size_t best=0;for(size_t k=1;k<fontKinds;++k)if(fontCount[k]>fontCount[best])best=k;
         if(fontKinds) {base.height=uint8_t(fonts[best]&255);base.face=uint8_t((fonts[best]>>8)&63);}}
        for(size_t r=0;r<g.rows;++r) {
            g.length[r]=CellsText(text_[r],attr_[r],0,columns_,false,g.text[r],Grid::maxText+1,g.cell[r]);
            for(size_t c=0;c<columns_ && c<Grid::maxColumns;++c) {
                const uint8_t a=attr_[r][c];const uint16_t f=cellFont_[r][c];Style s;
                if(a&0x80) {s.fg=uint8_t(a&15);s.bg=uint8_t((a>>4)&7);s.highlight=Highlighted(a);}
                else {s.fg=base.fg;s.bg=base.bg;}
                s.height=uint8_t(f&255);s.face=uint8_t((f>>8)&63);s.bold=(f&0x4000)!=0;
                g.style[r][c]=s;
            }
        }
        const size_t hint=lastHighlightRow_<rows_?lastHighlightRow_:reviewRow_;
        return Annotate<speechtext::ownOpen,speechtext::ownClose>(in,g,base,flags,hint,out,capacity,extras);
    }
    // ---- r393 speech history (Insert+H), like NVDA's Speech History ------
    void AddHistory(const char16_t* text) {
        if(!text)return;
        // Built apart and copied in only when kept: with the ring full the next
        // slot is the oldest entry, which a repeated or empty utterance erased.
        static char16_t slot[historyLength];size_t at=0;bool space=true;
        for(size_t i=0;text[i] && at+1<historyLength;++i) {
            const char16_t c=text[i];
            if(c==speechtext::ownOpen || c==speechtext::ownClose)continue;
            if(c==formatting::formatMark) {size_t k=1;while(k<=formatting::payload && text[i+k])++k;i+=k-1;continue;}
            if(c==speechtext::languageSwitch) {if(text[i+1])++i;continue;}
            if(c==u' ') {if(space)continue;space=true;} else space=false;
            slot[at++]=c;
        }
        while(at && slot[at-1]==u' ')--at;
        slot[at]=0;
        if(!at)return;
        if(historyCount_) {
            const char16_t* last=history_[(historyNext_+historySize-1)%historySize];
            size_t k=0;while(last[k] && last[k]==slot[k])++k;
            if(last[k]==slot[k])return;   // the same words again (a repeated key)
        }
        for(size_t k=0;k<=at;++k)history_[historyNext_][k]=slot[k];
        historyNext_=(historyNext_+1)%historySize;if(historyCount_<historySize)++historyCount_;
    }
    const char16_t* HistoryEntry(size_t i) const {return history_[(historyNext_+historySize-historyCount_+i)%historySize];}
    void SayHistoryEntry(const char16_t* head) {
        const bool pl=options_.polish;
        char16_t text[historyLength+200];size_t at=0;
        auto put=[&](const char16_t* s) {while(*s && at+2<sizeof(text)/2)text[at++]=*s++;};
        if(head) {text[at++]=speechtext::ownOpen;put(head);text[at++]=speechtext::ownClose;}
        put(HistoryEntry(historyIndex_));
        if(options_.positions) {
            text[at++]=speechtext::ownOpen;put(pl?u"; wypowiedź ":u"; utterance ");
            Number(text,at,historyCount_-historyIndex_);put(pl?u" z ":u" of ");Number(text,at,historyCount_);
            text[at++]=speechtext::ownClose;
        }
        text[at]=0;Speak(text);
    }
    void OpenHistory(bool open) {
        const bool pl=options_.polish;insertArmed_=false;
        if(open && !historyCount_) {SayOwn(pl?u"Historia mowy jest pusta.":u"The speech history is empty.");return;}
        if(!open) {historyOpen_=false;SayOwn(pl?u"Historia mowy zamknięta.":u"Speech history closed.");return;}
        historyOpen_=true;historyIndex_=historyCount_-1;
        SayHistoryEntry(pl?u"Historia mowy, rozruch wstrzymany. Strzałka w górę: starsze, w dół: nowsze, Escape zamyka. ":u"Speech history, boot paused. Up: older, Down: newer, Escape closes. ");
    }
    bool HistoryCommand(char16_t c) {
        const bool pl=options_.polish;
        switch(c) {
            case listPrevious:if(historyIndex_)--historyIndex_;else {SayOwn(pl?u"Najstarsza wypowiedź":u"Oldest utterance");return true;}break;
            case listNext:if(historyIndex_+1<historyCount_)++historyIndex_;else {SayOwn(pl?u"Najnowsza wypowiedź":u"Newest utterance");return true;}break;
            case listFirst:historyIndex_=0;break;
            case listLast:historyIndex_=historyCount_-1;break;
            case listClose:case u'H':OpenHistory(false);return true;
            case 0x13:break;
            default:return true;
        }
        SayHistoryEntry(nullptr);return true;
    }
    // ---- r393 what changed since the last key (Insert+D) -----------------
    void SnapshotBeforeKey() {
        for(size_t r=0;r<rows_ && r<maxRows;++r)for(size_t c=0;c<columns_ && c<maxColumns;++c)beforeKey_[r][c]=text_[r][c]?text_[r][c]:u' ';
        beforeRows_=rows_;beforeColumns_=columns_;haveBefore_=true;
    }
    static const char16_t* RowsWord(size_t n,bool pl) {
        if(!pl)return n==1?u" line":u" lines";
        if(n==1)return u" wiersz";
        return (n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20))?u" wiersze":u" wierszy";
    }
    void SayChanges() {
        const bool pl=options_.polish;
        if(!haveBefore_) {SayOwn(pl?u"Nie ma jeszcze ekranu sprzed klawisza.":u"There is no screen from before a key yet.");return;}
        char16_t lines[1400]={};size_t changed=0,emptied=0;
        for(size_t r=0;r<rows_ && r<maxRows;++r) {
            bool diff=false;
            for(size_t c=0;c<columns_ && !diff;++c) {
                const char16_t a=text_[r][c]?text_[r][c]:u' ';
                const char16_t b=(r<beforeRows_ && c<beforeColumns_)?beforeKey_[r][c]:u' ';
                diff=a!=b;
            }
            if(!diff)continue;
            char16_t line[maxColumns+1];
            if(!RowText(r,line,maxColumns+1) || ArtRow(r)) {++emptied;continue;}
            ++changed;if(changed<=12)Add(lines,1400,line);
        }
        if(!changed && !emptied) {SayOwn(pl?u"Bez zmian od ostatniego klawisza.":u"Nothing changed since the last key.");return;}
        char16_t text[1600]={};size_t at=0;
        auto put=[&](const char16_t* s) {while(*s && at+2<1600)text[at++]=*s++;};
        text[at++]=speechtext::ownOpen;
        put(pl?u"Zmieniło się ":u"Changed: ");Number(text,at,changed+emptied);put(RowsWord(changed+emptied,pl));
        if(emptied) {put(pl?u", w tym pustych ":u", empty now ");Number(text,at,emptied);}
        put(changed?u": ":u".");text[at++]=speechtext::ownClose;
        for(size_t i=0;lines[i] && at+1<1600;++i)text[at++]=lines[i];
        text[at]=0;Speak(text);
    }
    // ---- r393 screen regions (Ctrl+Left / Ctrl+Right) ---------------------
    // Blocks of rows whose text starts in the same column on the same
    // background: title, menu, help panel, key-hint bar, window, text.
    struct Region {size_t top,bottom,left,right;uint8_t bg,kind;};
    enum : uint8_t {regionText=0,regionWindow,regionMenu,regionTitle,regionKeys,regionHelp};
    size_t BuildRegions(Region* out,size_t maximum) const {
        size_t n=0,firstRow=SIZE_MAX,lastRow=0;
        for(size_t r=0;r<rows_;++r) {
            if(TextStart(r)==SIZE_MAX || ArtRow(r))continue;
            if(firstRow==SIZE_MAX)firstRow=r;lastRow=r;
            Segment list[40];const size_t k=Segments(r,list,40);
            for(size_t i=0;i<k;++i) {
                const Segment s=list[i];
                const uint8_t bg=uint8_t((attr_[r][s.start]>>4)&7);
                size_t joined=SIZE_MAX;
                for(size_t j=0;j<n && joined==SIZE_MAX;++j)if(out[j].bottom+1==r && Near(out[j].left,s.start) && (out[j].bg==bg || r==lastHighlightRow_ || out[j].bottom==lastHighlightRow_))joined=j;   // the highlighted entry belongs to its menu
                if(joined!=SIZE_MAX) {out[joined].bottom=r;if(s.end>out[joined].right)out[joined].right=s.end;if(s.start<out[joined].left)out[joined].left=s.start;continue;}
                if(n<maximum)out[n++]={r,r,s.start,s.end,bg,regionText};
            }
        }
        for(size_t j=0;j<n;++j) {
            Region& g=out[j];
            if(frameRow_<rows_ && g.top>=frameRow_ && g.left>frameColumn_ && g.left<=frameColumn_+3)g.kind=regionWindow;
            else if(lastHighlightRow_<rows_ && lastHighlightRow_>=g.top && lastHighlightRow_<=g.bottom)g.kind=regionMenu;
            else if(g.top==firstRow && g.bottom==firstRow && n>1)g.kind=regionTitle;
            else if(g.top==lastRow && n>1)g.kind=regionKeys;
            else if(g.left*20>=columns_*11)g.kind=regionHelp;
        }
        return n;
    }
    void SayRegion(bool forward) {
        const bool pl=options_.polish;
        Region list[24];const size_t n=BuildRegions(list,24);
        if(!n) {SayOwn(pl?u"Na ekranie nie ma regionów z tekstem.":u"No regions with text on the screen.");return;}
        if(regionIndex_!=SIZE_MAX && (regionIndex_>=n || regionScreen_!=n))regionIndex_=SIZE_MAX;
        regionScreen_=n;
        if(regionIndex_==SIZE_MAX) {
            // start next to the review cursor's region
            size_t at=SIZE_MAX;
            for(size_t j=0;j<n && at==SIZE_MAX;++j)if(reviewRow_>=list[j].top && reviewRow_<=list[j].bottom && reviewCol_+1>=list[j].left && reviewCol_<=list[j].right)at=j;
            if(at==SIZE_MAX)regionIndex_=forward?0:n-1;
            else if(forward)regionIndex_=at+1<n?at+1:at;
            else regionIndex_=at?at-1:0;
        }
        else if(forward) {if(regionIndex_+1>=n) {SayOwn(pl?u"Ostatni region":u"Last region");return;}++regionIndex_;}
        else {if(!regionIndex_) {SayOwn(pl?u"Pierwszy region":u"First region");return;}--regionIndex_;}
        const Region& g=list[regionIndex_];
        static const char16_t* namesPl[]={u"tekst",u"okno",u"menu",u"tytuł",u"pasek klawiszy",u"panel pomocy"};
        static const char16_t* namesEn[]={u"text",u"window",u"menu",u"title",u"key-hint bar",u"help panel"};
        char16_t text[1600]={};size_t at=0;
        auto put=[&](const char16_t* s) {while(*s && at+2<1600)text[at++]=*s++;};
        text[at++]=speechtext::ownOpen;put((pl?namesPl:namesEn)[g.kind]);
        if(options_.positions) {put(u", ");Number(text,at,regionIndex_+1);put(pl?u" z ":u" of ");Number(text,at,n);}
        put(u": ");text[at++]=speechtext::ownClose;text[at]=0;
        for(size_t r=g.top;r<=g.bottom;++r) {
            char16_t part[maxColumns+1];CellsText(text_[r],attr_[r],g.left,g.right,false,part,maxColumns+1);
            if(!part[0])continue;
            if(at && text[at-1]!=speechtext::ownClose && at+2<1600)text[at++]=u' ';
            for(size_t i=0;part[i] && at+1<1600;++i)text[at++]=part[i];
        }
        text[at]=0;
        reviewRow_=g.top;reviewCol_=g.left;
        Speak(text);
    }
    // ---- r393 boot-manager handling (CharonManagerProfiles.h) -------------
    // The folder of a loaded image's file (u"\\EFI\\debian"), from its FilePath.
    static void ImageDir(const void* path,char16_t* dir,size_t capacity) {
        dir[0]=0;if(!path)return;
        const uint8_t* p=static_cast<const uint8_t*>(path);size_t total=0;
        while(total<4096) {const size_t n=size_t(p[total+2]|(p[total+3]<<8));if(p[total]==0x7f || n<4)break;total+=n;}
        size_t deviceBytes=0;char16_t file[260];managerprobe::Split(p,total+4,deviceBytes,file,260);
        size_t cut=0;for(size_t i=0;file[i] && i<259;++i)if(file[i]==u'\\')cut=i;
        size_t i=0;for(;i<cut && i+1<capacity;++i)dir[i]=file[i];dir[i]=0;
    }
    // At start: does this manager's configuration hide its menu?
    void NoteHiddenMenu(const char16_t* name,efi_loaded_image_protocol* loaded) {
        hiddenPending_=false;hiddenProfile_=nullptr;
        if(!(options_.managerFlags&3) || !loaded || !loaded->DeviceHandle || !boot_->HandleProtocol)return;
        const managers::Profile* profile=managers::Find(name);
        if(!profile || !profile->hiddenHint)return;
        efi_guid fsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;efi_simple_file_system_protocol* fs=nullptr;
        if(boot_->HandleProtocol(loaded->DeviceHandle,&fsGuid,reinterpret_cast<void**>(&fs))!=EFI_SUCCESS || !fs)return;
        efi_file_protocol* root=nullptr;
        if(fs->OpenVolume(fs,&root)!=EFI_SUCCESS || !root)return;
        char16_t dir[260];ImageDir(loaded->FilePath,dir,260);
        BcdSummary summary;bool hidden=false;
        if(name[0]==u'W') {if(ReadBcdFile(boot_,root,summary))hidden=summary.count>1 && summary.timeout==0;}
        else if(managers::ReadConfig(boot_,root,name,dir,summary))hidden=summary.hidden;
        // r393 (user: an installed Linux): GRUB's real grub.cfg is on the
        // system partition (ext4), the ESP holds a stub: the menu may be hidden.
        hiddenMaybe_=false;
        if(!hidden && summary.elsewhere && GrubFamily(name)) {hidden=true;hiddenMaybe_=true;}
        root->Close(root);
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader hidden menu",(uint64_t(hidden)<<32)|uint64_t(summary.timeout&0xffff));
#endif
        if(hidden) {hiddenPending_=true;hiddenProfile_=profile;}
    }
    // Sends one key to the program (the next read of the console); its wait
    // events are signalled so a program waiting for a key reads it at once.
    void Arm(uint16_t scan,char16_t ch) {
        revealScan_=scan;revealChar_=ch;revealArmed_=true;revealUntil_=tick_+1000;   // 10 s
        if(table_ && table_->ConIn && table_->ConIn->WaitForKey)boot_->SignalEvent(table_->ConIn->WaitForKey);
        if(exHooked_ && exHooked_->WaitForKeyEx)boot_->SignalEvent(exHooked_->WaitForKeyEx);
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader arm key",(uint64_t(scan)<<16)|ch);
#endif
    }
    // ---- r397 Speakup windows, cut and paste, indentation ----------------
    size_t winTop_=SIZE_MAX,winLeft_=0,winBottom_=0,winRight_=0;bool winCorner_=false,winSilent_=false;
    size_t markRow_=SIZE_MAX,markCol_=0;
    char16_t clip_[512]={};size_t clipLength_=0;
    char16_t paste_[512]={};size_t pasteAt_=0,pasteLength_=0;
    bool InWindow(size_t r,size_t c) const {return winTop_!=SIZE_MAX && !winCorner_ && r>=winTop_ && r<=winBottom_ && c>=winLeft_ && c<=winRight_;}
    void SayNumbered(const char16_t* head,size_t a,const char16_t* mid,size_t b,const char16_t* tail=u"") {
        char16_t text[160];size_t at=0;auto put=[&](const char16_t* s) {while(*s && at+12<160)text[at++]=*s++;};
        put(head);Number(text,at,a);put(mid);Number(text,at,b);put(tail);text[at]=0;SayOwn(text);
    }
    void SpeakupCommand(char16_t c) {
        const bool pl=options_.polish;ReviewStart();
        const size_t row=reviewRow_<rows_?reviewRow_:0,col=reviewCol_<columns_?reviewCol_:0;
        switch(c) {
            case u'W':   // Speakup set_win: top left, then bottom right; on a defined window: clear it
                if(winTop_!=SIZE_MAX && !winCorner_) {winTop_=SIZE_MAX;winSilent_=false;SayOwn(pl?u"Okno usunięte.":u"Window cleared.");return;}
                if(!winCorner_) {winTop_=row;winLeft_=col;winCorner_=true;SayOwn(pl?u"Lewy górny róg okna ustawiony. Przesuń kursor przeglądu do prawego dolnego rogu i naciśnij Insert Page Up.":u"Window top left set. Move the review cursor to the bottom right and press Insert Page Up.");return;}
                winBottom_=row<winTop_?winTop_:row;winRight_=col<winLeft_?winLeft_:col;if(row<winTop_)winTop_=row;if(col<winLeft_)winLeft_=col;winCorner_=false;
                SayNumbered(pl?u"Okno ustawione: wiersze ":u"Window set: rows ",winTop_+1,pl?u" do ":u" to ",winBottom_+1,pl?u".":u".");return;
            case u'S':   // Speakup enable_win: the window is not read automatically
                if(winTop_==SIZE_MAX || winCorner_) {SayOwn(pl?u"Nie ma okna. Ustaw je przez Insert Page Up.":u"No window. Set one with Insert Page Up.");return;}
                winSilent_=!winSilent_;SayOwn(winSilent_?(pl?u"Okno wyciszone: zmiany w nim nie są czytane.":u"Window silenced: its changes are not read."):(pl?u"Okno znów czytane.":u"Window read again."));return;
            case u'R': {  // Speakup say_win
                if(winTop_==SIZE_MAX || winCorner_) {SayOwn(pl?u"Nie ma okna. Ustaw je przez Insert Page Up.":u"No window. Set one with Insert Page Up.");return;}
                char16_t all[1600]={};size_t at=0;
                for(size_t r=winTop_;r<=winBottom_ && r<rows_;++r) {
                    char16_t line[maxColumns+1];const size_t n=CellsText(text_[r],attr_[r],winLeft_,winRight_+1<columns_?winRight_+1:columns_,false,line,maxColumns+1);
                    if(!n || !line[0])continue;if(at && at+2<1600) {all[at++]=u'.';all[at++]=u' ';}
                    for(size_t i=0;line[i] && at+1<1600;++i)all[at++]=line[i];
                }
                all[at]=0;if(!all[0]) {SayOwn(pl?u"Okno jest puste.":u"The window is empty.");return;}
                Speak(all);return;
            }
            case u'C':markRow_=row;markCol_=col;SayOwn(pl?u"Początek zaznaczenia.":u"Start marked.");return;   // NVDA+F9
            case u'K': {  // NVDA+F10 / Speakup cut: from the mark to the review cursor
                if(markRow_==SIZE_MAX) {SayOwn(pl?u"Najpierw zaznacz początek: Insert F9.":u"Mark the start first: Insert F9.");return;}
                size_t r0=markRow_,c0=markCol_,r1=row,c1=col;if(r1<r0 || (r1==r0 && c1<c0)) {size_t a=r0;r0=r1;r1=a;a=c0;c0=c1;c1=a;}
                clipLength_=0;
                for(size_t r=r0;r<=r1 && r<rows_;++r) {
                    const size_t from=r==r0?c0:0;size_t to=r==r1?c1+1:columns_;if(to>columns_)to=columns_;
                    size_t end=to;while(end>from && text_[r][end-1]==u' ')--end;
                    if(clipLength_ && r>r0 && clipLength_+1<512)clip_[clipLength_++]=u' ';
                    for(size_t k=from;k<end && clipLength_+1<512;++k)clip_[clipLength_++]=text_[r][k]>=0x20 && text_[r][k]<0x7f?text_[r][k]:text_[r][k]>=0xa0?text_[r][k]:u' ';
                }
                clip_[clipLength_]=0;markRow_=SIZE_MAX;
                if(!clipLength_) {SayOwn(pl?u"Nic nie skopiowano.":u"Nothing copied.");return;}
                {char16_t text[600];size_t at=0;const char16_t* h=pl?u"Skopiowano: ":u"Copied: ";while(*h)text[at++]=*h++;for(size_t i=0;i<clipLength_ && at+1<600;++i)text[at++]=clip_[i];text[at]=0;Speak(text);}
                return;
            }
            case u'V':   // Speakup paste: the copied text typed into the program
                if(!clipLength_) {SayOwn(pl?u"Schowek Charona jest pusty: skopiuj tekst przez Insert F9 i Insert F10.":u"Charon's clipboard is empty: copy text with Insert F9 and Insert F10.");return;}
                for(size_t i=0;i<clipLength_;++i)paste_[i]=clip_[i];pasteLength_=clipLength_;pasteAt_=0;
                {char16_t text[64];size_t at=0;const char16_t* h=pl?u"Wklejam ":u"Pasting ";while(*h)text[at++]=*h++;Number(text,at,clipLength_);
                 const char16_t* u=pl?u" znaków.":u" characters.";while(*u)text[at++]=*u++;text[at]=0;SayOwn(text);}
                Arm(0,paste_[pasteAt_]);return;
            case u'I': {  // Speakup say_line_indent
                char16_t line[maxColumns+1];RowText(row,line,maxColumns+1);
                size_t indent=0;while(indent<columns_ && text_[row][indent]==u' ')++indent;
                if(indent>=columns_ || !line[0]) {SayOwn(pl?u"Pusty wiersz.":u"Blank line.");return;}
                char16_t text[maxColumns+48];size_t at=0;const char16_t* h=pl?u"\xE001" u"Wcięcie ":u"\xE001" u"Indent ";while(*h)text[at++]=*h++;
                Number(text,at,indent);text[at++]=u',';text[at++]=u' ';text[at++]=speechtext::ownClose;
                for(size_t i=0;line[i] && at+1<maxColumns+48;++i)text[at++]=line[i];text[at]=0;Speak(text);return;
            }
            default:return;
        }
    }
    bool Injected(uint16_t& scan,char16_t& ch) {
        // r397: a paste in progress: one character per read; the next one is armed at once.
        if(pasteLength_ && pasteAt_<pasteLength_ && !stopped_ && !suspended_) {
            scan=0;ch=paste_[pasteAt_++];revealArmed_=false;
            if(pasteAt_<pasteLength_)Arm(0,paste_[pasteAt_]);else pasteLength_=pasteAt_=0;
            return true;
        }
        if(!revealArmed_)return false;
        revealArmed_=false;
        if(tick_>revealUntil_ || stopped_ || suspended_)return false;
        scan=revealScan_;ch=revealChar_;
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader injected key",(uint64_t(scan)<<16)|ch);
#endif
        return true;
    }
    // Before the manager runs: say that its menu is hidden; "announce" gives
    // 2 s after the message for any key, "always" shows the menu at once.
    // Runs at the caller's TPL (TPL_APPLICATION): speech and the key are
    // set up under Guard, so the timer never pumps a voice being replaced.
    void HiddenMenuPrompt() {
        hiddenPending_=false;
        const managers::Profile* p=hiddenProfile_;hiddenProfile_=nullptr;
        if(!p || !p->hiddenHint)return;
        const bool pl=options_.polish,canReveal=p->revealScan || p->revealChar;
        const uint8_t mode=uint8_t(options_.managerFlags&3);
        char16_t text[400];speechtext::Out o{text,400};
        if(hiddenMaybe_)o.Puts(pl?u"GRUB wczytuje konfigurację z partycji systemu; jego menu może być ukryte.":u"GRUB loads its configuration from the system partition; its menu may be hidden.");
        else o.Puts(pl?p->hiddenHint->pl:p->hiddenHint->en);
        if(!canReveal) {{Guard guard(boot_);SayOwn(text);}return;}
        if(mode==2) {o.Puts(pl?u" Charon je pokazuje.":u" Charon shows it.");{Guard guard(boot_);SayOwn(text);}{Guard guard(boot_);Arm(p->revealScan,p->revealChar);}return;}
        o.Puts(pl?u" Naciśnij dowolny klawisz w ciągu 2 sekund, aby je pokazać.":u" Press any key within 2 seconds to show it.");
        {Guard guard(boot_);SayOwn(text);}
        const efi_tpl current=boot_->RaiseTPL(TPL_HIGH_LEVEL);boot_->RestoreTPL(current);
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader hidden prompt tpl",uint64_t(current)|(uint64_t(originalRead_!=nullptr)<<32));
#endif
        if(current>TPL_APPLICATION || !table_ || !table_->ConIn)return;
        auto* fx=options_.effects;bool pressed=false;unsigned quiet=0;
        for(unsigned i=0;i<1000 && !pressed;++i) {   // at most 10 s: the message, then 2 s
            efi_input_key k{};
            const efi_status got=(originalRead_ && inputHooked_)?originalRead_(inputHooked_,&k):table_->ConIn->ReadKeyStroke(table_->ConIn,&k);
            if(got==EFI_SUCCESS && (k.ScanCode || k.UnicodeChar)) {pressed=true;break;}
            const bool idle=!fx || !fx->SpeechIdle || fx->SpeechIdle(fx->context);
            if(idle) {quiet+=10;if(quiet>=2000)break;}
            if(originalStall_)originalStall_(10000);else boot_->Stall(10000);
        }
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader hidden prompt key",uint64_t(pressed)|(uint64_t(quiet)<<8));
#endif
        if(pressed) {{Guard guard(boot_);SayOwn(pl?u"Pokazuję menu.":u"Showing the menu.");}{Guard guard(boot_);Arm(p->revealScan,p->revealChar);}}
    }
    // A countdown of a known boot manager: one key no manager gives a
    // meaning to (F11) stops it, so the menu can be listened to.
    void StopCountdown() {
        countdownStopped_=true;Arm(0x15,0);
        SayOwn(options_.polish?u"Charon zatrzymuje odliczanie.":u"Charon stops the countdown.");
    }
    // r393 commands: formatting mode, history, changes, regions.
    bool ReaderExtraCommand(char16_t c) {
        if(inputHelp_)return false;
        const bool pl=options_.polish;
        switch(c) {
            case u'G': {
                const uint8_t mode=uint8_t(((options_.formatFlags&3)+1)&3);
                options_.formatFlags=uint8_t((options_.formatFlags&~3)|mode);
                static const char16_t* namesPl[]={u"Formatowanie tekstu: wyłączone",u"Formatowanie tekstu: głosem",u"Formatowanie tekstu: ikony dźwiękowe",u"Formatowanie tekstu: głosem i ikony dźwiękowe"};
                static const char16_t* namesEn[]={u"Text formatting: off",u"Text formatting: voice",u"Text formatting: sound icons",u"Text formatting: voice and sound icons"};
                SayOwn((pl?namesPl:namesEn)[mode]);return true;
            }
            case u'H':OpenHistory(!historyOpen_);return true;
            case u'D':SayChanges();return true;
            case u'{':SayRegion(false);return true;
            case u'}':SayRegion(true);return true;
            case u'W':case u'S':case u'R':case u'C':case u'K':case u'V':case u'I':SpeakupCommand(c);return true;   // r397
            default:return false;
        }
    }
    // ---- r390 key list (Insert+F1 / Alt+F1, like Fenrir and Speakup) ----
    static constexpr char16_t listPrevious=0x0e,listNext=0x0f,listFirst=0x10,listLast=0x11,listClose=0x12;
    // r393: the running boot manager's own keys come first in the list.
    const managers::Profile* RunningProfile() const {return bootManager_?managers::Find(bootManager_):nullptr;}
    size_t KeyCount() const {const auto* p=RunningProfile();return ProgramKeyCount()+screenKeyCount_+keyhelp::count+(p?p->keyCount:0);}
    const keyhelp::Entry& KeyEntry(size_t i) const {
        const size_t program=ProgramKeyCount();   // r396: keys a program gave through the accessibility protocol
        if(i<program)return programKeys_[i];
        i-=program;
        if(i<screenKeyCount_)return *screenKeys_[i];   // r397: keys of the screen shown now
        i-=screenKeyCount_;
        const auto* p=RunningProfile();const size_t own=p?p->keyCount:0;
        if(i<own)return p->keys[i];
        i-=own;return keyhelp::entries[i<keyhelp::count?i:0];
    }
    // r397: the screen shown now (CharonManagerProfiles.h screens and the
    // screenkey= lines of profile files), by text it shows.
    const keyhelp::Entry* screenKeys_[24]={};size_t screenKeyCount_=0;const keyhelp::Entry* screenName_=nullptr;
    bool ScreenShows(const char16_t* marker,size_t only=SIZE_MAX) const {
        size_t n=0;while(marker[n])++n;
        if(!n || n>maxColumns)return false;
        for(size_t r=0;r<maxRows;++r)for(size_t c=0;c+n<=maxColumns;++c) {
            if(only!=SIZE_MAX && r!=only)break;
            if(text_[r][c]!=marker[0])continue;
            size_t k=1;while(k<n && text_[r][c+k]==marker[k])++k;
            if(k==n)return true;
        }
        return false;
    }
    void FindScreenKeys() {
        screenKeyCount_=0;screenName_=nullptr;
        for(size_t i=0;i<managers::userProfileCount;++i) {
            const auto& u=managers::userProfiles[i];
            for(size_t k=0;k<u.screenKeyCount && screenKeyCount_<24;++k)if(ScreenShows(u.screenMarker[k]))screenKeys_[screenKeyCount_++]=&u.screenKeys[k];
        }
        uint32_t groups=0;
        for(const auto& s:managers::screens) {
            // r407: a marker of the caret's row (a prompt) or of the text row above it (the answer to the last entry)
            size_t only=SIZE_MAX;
            if(s.where==managers::ScreenKeys::caretRow) {if(row_>=rows_)continue;only=row_;}
            else if(s.where==managers::ScreenKeys::aboveCaret) {
                size_t k=row_<rows_?row_:0;bool found=false;
                while(k-->0)if(RowHasText(k)) {found=true;break;}
                if(!found)continue;only=k;
            }
            if((groups>>s.group)&1 || !ScreenShows(s.marker,only))continue;
            groups|=1u<<s.group;
            if(!screenName_)screenName_=&s.name;
            for(size_t k=0;k<s.keyCount && screenKeyCount_<24;++k)screenKeys_[screenKeyCount_++]=&s.keys[k];
        }
    }
    void SayKeyEntry() {
        const auto& e=KeyEntry(helpIndex_);
        char16_t text[400];speechtext::Out o{text,400};
        o.Puts(options_.polish?e.pl:e.en);
        if(options_.positions) {o.Puts(u", ");o.Number(helpIndex_+1);o.Puts(options_.polish?u" z ":u" of ");o.Number(KeyCount());}
        SayOwn(text);
    }
    void OpenKeyList(bool open) {
        helpOpen_=open;insertArmed_=false;
        if(!open) {SayOwn(options_.polish?u"Lista klawiszy zamknięta.":u"Key list closed.");return;}
        helpIndex_=0;Raise(formatting::Event::help);   // r397
        FindScreenKeys();
        char16_t text[480];speechtext::Out o{text,480};
        o.Puts(options_.polish?u"Lista klawiszy, rozruch wstrzymany. ":u"Key list, boot paused. ");
        if(screenName_) {o.Puts(options_.polish?screenName_->pl:screenName_->en);o.Puts(u". ");}
        const auto& e=KeyEntry(0);o.Puts(options_.polish?e.pl:e.en);
        SayOwn(text);
    }
    bool KeyListCommand(char16_t c) {
        switch(c) {
            case listPrevious:if(helpIndex_)--helpIndex_;else {SayOwn(options_.polish?u"Początek listy":u"Top of the list");return true;}break;
            case listNext:if(helpIndex_+1<KeyCount())++helpIndex_;else {SayOwn(options_.polish?u"Koniec listy":u"End of the list");return true;}break;
            case listFirst:helpIndex_=0;break;
            case listLast:helpIndex_=KeyCount()-1;break;
            case listClose:OpenKeyList(false);return true;
            case 0x13:break;
            case u'?':OpenKeyList(false);return true;
            default:return true;   // other reader commands wait until the list is closed
        }
        SayKeyEntry();return true;
    }
    char16_t KeyListKey(const efi_input_key& key) const {
        switch(key.ScanCode) {
            case 1:return listPrevious;case 2:return listNext;case 5:case 9:return listFirst;case 6:case 0xa:return listLast;
            case 0x17:case 0x0b:return listClose;
            default:break;
        }
        if(!key.ScanCode && key.UnicodeChar==13)return 0x13;   // Enter: the entry again
        return 0;
    }
    // ---- r390 screen review with the boot paused (Alt+\) ---------------
    // The user's idea: stop the boot to read the console and its messages.
    // While paused, every key belongs to the review, and a program that
    // prints, waits (Stall) or ends boot services waits until it is over.
    void SetReviewPause(bool on) {
        if(on==reviewPause_)return;
        reviewPause_=on;insertArmed_=false;
        const bool pl=options_.polish;
        if(on) {ReviewStart();SayOwn(pl?u"Przegląd ekranu, rozruch wstrzymany. Strzałki w górę i w dół czytają wiersze, w lewo i w prawo znaki, Home i End początek i koniec ekranu, Enter bieżący wiersz. Escape albo Alt ukośnik wsteczny wznawia."
            :u"Screen review, boot paused. Up and Down read lines, Left and Right characters, Home and End the top and bottom, Enter the current line. Escape or Alt backslash goes on.");}
        else SayOwn(pl?u"Rozruch wznowiony.":u"Boot resumed.");
    }
    char16_t PauseCommand(const efi_input_key& key) const {
        switch(key.ScanCode) {
            case 1:return u'u';case 2:return u'o';case 3:return u'.';case 4:return u'm';
            case 5:case 9:return u't';case 6:case 0xa:return u'b';case 0x17:return u'\\';
            default:break;
        }
        if(key.ScanCode)return 0;
        switch(key.UnicodeChar) {case 13:return u'i';case u' ':return u'k';case 9:return u'e';case u'\\':return u'\\';default:break;}
        return InsertCommand(key);
    }
    bool Paused() const {return reviewPause_ || helpOpen_ || historyOpen_;}
    // ---- r390 braille: the display follows the review cursor ------------
    void BrailleFound() {
        char16_t text[96];speechtext::Out o{text,96};
        o.Puts(options_.polish?u"Monitor brajlowski: ":u"Braille display: ");
        const char* n=braille_.Name();if(n[0])for(;*n;++n)o.Put(char16_t(*n));else o.Puts(u"Baum");
        o.Puts(u", ");o.Number(braille_.Cells());o.Puts(options_.polish?u" komórek":u" cells");
        SayOwn(text);
    }
    void BrailleWork() {
        const auto e=braille_.Poll();
        if(e.key!=braille::Key::none) {
            ReviewStart();
            const size_t cells=braille_.Cells()?braille_.Cells():40;
            switch(e.key) {
                case braille::Key::panLeft:
                    if(brailleOffset_>=cells)brailleOffset_-=cells;
                    else if(reviewRow_<rows_ && NextTextRow(reviewRow_,false)) {const size_t end=RowEnd(reviewRow_);brailleOffset_=end?((end-1)/cells)*cells:0;reviewCol_=brailleOffset_;}
                    brailleManual_=true;break;
                case braille::Key::panRight:
                    if(reviewRow_<rows_ && brailleOffset_+cells<RowEnd(reviewRow_))brailleOffset_+=cells;
                    else if(reviewRow_<rows_ && NextTextRow(reviewRow_,true)) {brailleOffset_=0;reviewCol_=TextStart(reviewRow_);}
                    brailleManual_=true;break;
                case braille::Key::lineUp:command_=u'u';break;
                case braille::Key::lineDown:command_=u'o';break;
                case braille::Key::highlight:command_=u'e';break;
                case braille::Key::sayLine:command_=u'i';break;
                case braille::Key::route:
                    if(reviewRow_<rows_) {reviewCol_=brailleOffset_+e.cell;if(reviewCol_>=columns_)reviewCol_=columns_-1;command_=u',';}
                    brailleManual_=true;break;
                default:break;
            }
        }
        UpdateBraille();
    }
    void UpdateBraille() {
        if(!braille_.Present())return;
        size_t row=reviewRow_;
        if(row>=rows_ || !RowHasText(row)) {row=highlight_<rows_?highlight_:lastHighlightRow_;if(row>=rows_)for(size_t r=0;r<rows_;++r)if(RowHasText(r)) {row=r;break;}}
        if(row>=rows_) {braille_.Show(u"",0,0,SIZE_MAX);return;}
        const size_t cells=braille_.Cells()?braille_.Cells():40,length=RowEnd(row);
        const size_t cursor=row==reviewRow_?reviewCol_:SIZE_MAX;
        // The cursor moved by review or speech: its part of the line is shown.
        if(row!=brailleRow_ || (cursor!=brailleCursor_ && !brailleManual_)) {
            brailleOffset_=cursor!=SIZE_MAX?(cursor/cells)*cells:0;
            if(cursor==SIZE_MAX || cursor<cells) {const size_t start=TextStart(row);brailleOffset_=start<cells && cursor<cells?0:brailleOffset_;}
        }
        brailleManual_=false;brailleRow_=row;brailleCursor_=cursor;
        bool marks[maxColumns]={};
        if(options_.brailleAttributes)for(size_t c=0;c<length && c<maxColumns;++c)marks[c]=(attr_[row][c]&0x80) && Highlighted(attr_[row][c]);
        braille_.Show(text_[row],length,brailleOffset_,cursor,options_.brailleAttributes?marks:nullptr);
    }
    void BlockWhilePaused() {
        if(!Paused() || stopped_ || suspended_ || !boot_)return;
        const efi_tpl current=boot_->RaiseTPL(TPL_HIGH_LEVEL);boot_->RestoreTPL(current);
        if(current>TPL_APPLICATION)return;   // firmware work at raised TPL never waits
        // Keys are read here (the program does not read now); the timer
        // (TPL_CALLBACK) speaks and runs the commands meanwhile.
        while(Paused() && !stopped_ && !suspended_) {
            if(exHooked_ && originalReadEx_) {KeyData d{};HookReadEx(exHooked_,&d);}
            else if(inputHooked_ && originalRead_) {efi_input_key k{};HookRead(inputHooked_,&k);}
            if(originalStall_)originalStall_(10000);
        }
    }
    static efi_status EFIAPI HookWait(size_t count,efi_event* events,size_t* index) {
        ResidentReader* r=active_;
        if(!r || !r->originalWait_)return EFI_DEVICE_ERROR;
        r->ApplyPendingSynth();
        return r->originalWait_(count,events,index);
    }
    static efi_status EFIAPI HookStall(size_t microseconds) {
        ResidentReader* r=active_;
        if(!r || !r->originalStall_)return EFI_DEVICE_ERROR;
        r->ApplyPendingSynth();   // r396e
        if(r->installed_ && r->Paused())r->BlockWhilePaused();
        return r->originalStall_(microseconds);
    }
    // True when the key is consumed by the reader.
    bool FilterKey(const efi_input_key& key) {
        if(stopped_ || suspended_) {insertArmed_=false;return false;}
        // r390: the numpad key a USB keypad press already reviewed.
        if(keypad_.Consume(key))return true;
        if(helpOpen_) {const char16_t c=KeyListKey(key);if(c)command_=c;return true;}
        if(historyOpen_) {const char16_t c=KeyListKey(key);if(c)command_=c;return true;}   // r393 speech history
        if(reviewPause_) {const char16_t c=PauseCommand(key);if(c)command_=c;return true;}
        if(inputHelp_) {
            Guard guard(boot_);   // speech from the program's key read: the timer must not pump or speak meanwhile
            if(insertArmed_) {insertArmed_=false;const char16_t c=InsertCommand(key);if(c==u'x') {command_=c;return true;}
                if(c)SayOwn(CommandHelp(c));else SayKeyName(key);return true;}
            if(InsertKey(key)) {insertArmed_=true;insertTick_=tick_;SayOwn(options_.polish?u"Insert, klawisz czytnika":u"Insert, the reader key");return true;}
            if(key.ScanCode==0x0b) {SayOwn(options_.polish?u"F1. Insert F1 albo Alt F1 otwiera listę klawiszy.":u"F1. Insert F1 or Alt F1 opens the key list.");return true;}
            SayKeyName(key);return true;
        }
        if(insertArmed_ && tick_-insertTick_>300)insertArmed_=false;
        if(!insertArmed_) {
            if(InsertKey(key)) {insertArmed_=true;insertTick_=tick_;return true;}
            return false;
        }
        insertArmed_=false;
        if(InsertKey(key))return false;           // Insert twice: one real Insert
        const char16_t c=InsertCommand(key);
        if(!c)return false;
        command_=c;return true;
    }
    static efi_status EFIAPI HookRead(efi_simple_text_input_protocol* self,efi_input_key* key) {
        ResidentReader* r=active_;
        auto original=r?r->originalRead_:nullptr;
        if(!original)return EFI_NOT_READY;
        if(!r->installed_)return original(self,key);   // uninstalled, reached through a wrapper
        r->Service();
        r->ApplyPendingSynth();   // r396
        if(key && r->Injected(key->ScanCode,key->UnicodeChar))return EFI_SUCCESS;   // r393 reveal / stop countdown
        // r389: a key the modifier poll read by accident goes to the program first,
        // through the same filters as a key read here.
        efi_status status=EFI_SUCCESS;
        if(r->havePush_ && key) {r->havePush_=false;*key=r->pushed_.key;}
        else status=original(self,key);
        if(status!=EFI_SUCCESS || !key)return status;
        if(r->swallowScan_ && key->ScanCode==r->swallowScan_ && !key->UnicodeChar) {
            r->swallowScan_=0;key->ScanCode=0;key->UnicodeChar=0;return EFI_NOT_READY;}   // r388 voice ring arrow
        if(r->pauseSwallowChar_ && key->UnicodeChar==r->pauseSwallowChar_ && !key->ScanCode) {
            r->pauseSwallowChar_=0;key->UnicodeChar=0;return EFI_NOT_READY;}   // r390 Alt+backslash
        const bool eaten=r->FilterKey(*key);
        if(!eaten)r->NoteKey(key->ScanCode,key->UnicodeChar);
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader key read",(uint64_t(eaten)<<40)|(uint64_t(key->ScanCode)<<16)|(r->secret_.Entry() && key->UnicodeChar>=32?u'*':key->UnicodeChar));   // r407: no secret key in the trace
#endif
        // Some programs (GRUB) look at the buffer, not only the status: an
        // eaten key leaves an empty key behind.
        if(eaten) {key->ScanCode=0;key->UnicodeChar=0;}
        return eaten?EFI_NOT_READY:EFI_SUCCESS;
    }
    static efi_status EFIAPI HookReadEx(TextInputEx* self,KeyData* data) {
        ResidentReader* r=active_;
        auto original=r?r->originalReadEx_:nullptr;
        if(!original)return EFI_NOT_READY;
        if(!r->installed_)return original(self,data);   // uninstalled, reached through a wrapper
        r->Service();
        r->ApplyPendingSynth();   // r396
        if(data && r->Injected(data->key.ScanCode,data->key.UnicodeChar)) {data->state.shift=0;data->state.toggle=0;return EFI_SUCCESS;}   // r393
        // A key the modifier poll read by accident: through the same filters as a key read here.
        efi_status status=EFI_SUCCESS;
        if(r->havePush_ && data) {r->havePush_=false;*data=r->pushed_;}
        else status=original(self,data);
        if(status!=EFI_SUCCESS || !data)return status;
        // With Ctrl or Alt held the key is a program shortcut, not ours.
        const uint32_t held=(data->state.shift&shiftValid)?data->state.shift:0;
        if((held&(leftControl|rightControl)) && (held&(leftAlt|rightAlt)) && !data->key.UnicodeChar && data->key.ScanCode>=1 && data->key.ScanCode<=4) {
            r->swallowScan_=0;data->key.ScanCode=0;data->state.shift=0;data->state.toggle=0;return EFI_NOT_READY;}   // r388 voice ring arrow
        // r390: Alt alone with an arrow or a backslash is the reader's (Alt layout).
        const bool control=(held&(leftControl|rightControl))!=0,alt=(held&(leftAlt|rightAlt))!=0;
        if(alt && !control && ((!data->key.UnicodeChar && data->key.ScanCode>=1 && data->key.ScanCode<=4) || (!data->key.ScanCode && data->key.UnicodeChar==u'\\'))) {
            r->swallowScan_=0;r->pauseSwallowChar_=0;data->key.ScanCode=0;data->key.UnicodeChar=0;data->state.shift=0;data->state.toggle=0;return EFI_NOT_READY;}
        // r393 (user): Ctrl+Left / Ctrl+Right walk the screen regions; in the
        // paused review also Shift+Left / Shift+Right. The notification ran it.
        {const bool shift=(held&3)!=0;
         if(!data->key.UnicodeChar && (data->key.ScanCode==3 || data->key.ScanCode==4) && ((control && !alt && !shift) || (shift && !control && !alt && r->reviewPause_))) {
            if(!r->keys_.Count())r->command_=data->key.ScanCode==3?u'}':u'{';
            r->swallowScan_=0;data->key.ScanCode=0;data->state.shift=0;data->state.toggle=0;return EFI_NOT_READY;}}
        if(alt && !control && !data->key.UnicodeChar && data->key.ScanCode==0x0b) {
            data->key.ScanCode=0;data->state.shift=0;data->state.toggle=0;return EFI_NOT_READY;}   // Alt+F1: the key list (notify)
        if((held&(leftControl|rightControl|leftAlt|rightAlt)) && !r->inputHelp_ && !r->Paused())return status;
        if((held&(leftControl|rightControl|leftAlt|rightAlt)) && (r->inputHelp_ || r->Paused())) {
            data->key.ScanCode=0;data->key.UnicodeChar=0;data->state.shift=0;data->state.toggle=0;return EFI_NOT_READY;}   // the notify named or ran it
        const bool eaten=r->FilterKey(data->key);
        if(!eaten)r->NoteKey(data->key.ScanCode,data->key.UnicodeChar);
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader key readex",(uint64_t(eaten)<<40)|(uint64_t(data->key.ScanCode)<<16)|(r->secret_.Entry() && data->key.UnicodeChar>=32?u'*':data->key.UnicodeChar));   // r407
#endif
        if(eaten) {data->key.ScanCode=0;data->key.UnicodeChar=0;data->state.shift=0;data->state.toggle=0;}
        return eaten?EFI_NOT_READY:EFI_SUCCESS;
    }
    // ---- events --------------------------------------------------------
#ifdef HAK_CHARON_POLL_DIAG
    static uint64_t Tsc() {uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));return (uint64_t(hi)<<32)|lo;}
    uint64_t diagCycles_=0,diagMax_=0;
    static void EFIAPI TimerNotify(efi_event e,void* context) {
        auto& self=*static_cast<ResidentReader*>(context);
        const uint64_t t0=Tsc();TimerWork(e,context);const uint64_t d=Tsc()-t0;
        self.diagCycles_+=d;if(d>self.diagMax_)self.diagMax_=d;
        if(!(self.tick_%300)) {charontrace::Value("reader cpu total",self.diagCycles_);charontrace::Value("reader cpu max",self.diagMax_);self.diagCycles_=self.diagMax_=0;}
    }
    static void TimerWork(efi_event,void* context) {
#else
    static void EFIAPI TimerNotify(efi_event,void* context) {
#endif
        auto& self=*static_cast<ResidentReader*>(context);
        if(self.stopped_ || self.suspended_)return;
        // r401: on Charon's own screens (reviewOnly) Charon speaks at application
        // level with the same voice. The timer then only counts; the work is done
        // from Charon's key polling (Service), so the two never interleave.
        if(self.options_.reviewOnly && !self.servicing_) {++self.serviceDue_;return;}
        ++self.tick_;
#ifdef HAK_CHARON_TICK_TRACE
        if(!(self.tick_%100)) {efi_tpl t=self.boot_->RaiseTPL(TPL_HIGH_LEVEL);self.boot_->RestoreTPL(t);charontrace::Value("reader heartbeat",(uint64_t(self.tick_)<<8)|t);}
#endif
        if(!(self.tick_%50)) {
#ifndef BISECT_NO_NOTIFY
            self.keys_.Register(self.table_,KeyNotify);
#endif
#ifndef BISECT_NO_HOOK
            self.HookInput();
#endif
            if(!(self.tick_%200))self.keypad_.Scan(self.boot_);   // r390: keyboards plugged or connected later
            if(!(self.tick_%100) && !self.braille_.Present() && self.braille_.Find(self.boot_))self.BrailleFound();
        }
        if(!(self.tick_%2))self.PollModifiers();   // r389: Ctrl silences
        if(self.pendingInfo_[0] && !self.anyDirty_ && self.tick_-self.lastOutput_>=6)self.SendMessages();
        if(self.command_) {const char16_t c=self.command_;self.command_=0;self.Command(c);}
        if(self.braille_.Present() && !(self.tick_%3))self.BrailleWork();   // r390
        // Speak after output pauses for 120 ms, or every 500 ms while an
        // application redraws continuously (progress bars, scrolling logs).
        // r389: 20 ms after a key the program read (its answer is one burst), 60 ms otherwise.
        // r400: quiet countdown menus (see Flush).
        if(self.quietMenu_==1 && self.tick_-self.quietTick_>=160) {
            self.quietMenu_=0;
            if(!self.quiet_) {
                if(self.heldMenu_[0])self.Speak(self.heldMenu_);
                auto* fx=self.options_.effects;
                if(self.heldInfo_[0] && fx && fx->SpeakMessage && self.options_.messageVoice) {self.Add(self.pendingInfo_,1600,self.heldInfo_);self.infoTick_=self.tick_;}
            }
            self.heldMenu_[0]=0;self.heldInfo_[0]=0;
        } else if(self.quietMenu_==2 && self.tick_-self.countdownSeen_>=160)self.quietMenu_=0;
        if(self.rereadAfterKey_ && self.tick_-self.keyTick_>=8) {
            self.rereadAfterKey_=false;
            for(size_t r=0;r<self.rows_;++r) {self.dirty_[r]=true;for(size_t col=0;col<maxColumns;++col)self.prevRaw_[r][col]=0;}
            self.lastHighlight_[0]=0;self.lastHighlightRow_=SIZE_MAX;self.menuAnnounced_=false;self.highlight_=SIZE_MAX;self.anyDirty_=true;self.firstDirty_=self.tick_;
        }
        if(self.anyDirty_ && (self.tick_-self.lastOutput_>=(self.KeyRecent() && self.DirtyRows()<=2?2u:6u) || self.tick_-self.firstDirty_>=50))self.Flush();   // r388: 60 ms (was 120)
        else if(!self.anyDirty_ && self.KeyRecent() && self.tick_-self.keyTick_>=2 && self.tick_-self.lastOutput_>=2)self.CursorEcho();
        if(self.pending_[0] && !(self.tick_%50))self.TrySpeakPending();
        if(!(self.tick_%50))self.WatchImage();
        if(self.timerEndAt_ && self.tick_>=self.timerEndAt_) {
            self.timerEndAt_=0;
            charontrace::Value("reader countdown end",2);
            if((self.options_.timers&2) && self.options_.effects && self.options_.effects->Tone){self.options_.effects->Tone(self.options_.effects->context,880,500);self.endToneUntil_=self.tick_+50;}
        }
#ifndef BISECT_NO_POLL
        if(self.readHookCount_ && !(self.tick_%20))self.WatchLoading();   // r387
        if(self.pixelDirty_ && self.tick_-self.pixelTick_>=(self.KeyRecent()?3u:6u))self.RecognizePixels();  // r389: 30 ms after a key; r388: 60 ms (r382: 100, was 150)
#else
        if(true) {}  // no screen reads at all
#endif
        else if(!self.pixelDirty_ && self.tick_-self.lastOutput_>=20 && self.tick_-self.lastBlt_>=20) {  // r382: 200 ms (was 1 s)
            // Cheap sparse sampling every 100 ms; a full read only after a
            // change, and every 5 s for changes too small for the samples.
            if(!(self.tick_%500))self.PollFrameBuffer();
            else if(!(self.tick_%10))self.SampleFrameBuffer();
        } else if(!self.pixelDirty_ && self.tick_-self.lastPoll_>=100) {  // r382: 1 s (was 3 s)
            // r364: steady drawing (a blinking cursor redrawn as a whole
            // line, OpenCore) never pauses for a second: read the screen at
            // least every 3 s; unchanged bands cost no recognition.
            self.PollFrameBuffer();
        }
        // r397 (user): a screen that is STUCK black or all-blue - a loader that
        // drew nothing, a hang, a bug-check fill - announced once with a low,
        // long, menacing tone. Deliberately conservative so a sparse but working
        // screen (a shell prompt, a firmware page) is not mistaken for an error:
        // nothing at all for 2 s (no console text, no drawing, no recognised
        // text, no key) and then the blank frame holds for a further 3 s.
        if(!self.options_.reviewOnly && !self.pixelDirty_ && self.tick_-self.lastOutput_>=250 && self.tick_-self.lastBlt_>=250
            && self.tick_-self.lastText_>=250 && self.tick_-self.keyTick_>=250 && !(self.tick_%20)) {
            const int kind=self.SecretConsole()?0:self.ClassifyBlankScreen();   // r407: a password prompt on a black console is no stuck screen
            if(kind) {
                if(kind!=self.blankKind_) {self.blankKind_=kind;self.blankSince_=self.tick_;self.blankAnnounced_=false;}
                // ~7 s of a stuck blank frame. The cooldown only suppresses the
                // SAME kind announced again soon (a slow loader scanning on a
                // black screen, like Clover, must not keep being called an
                // error); a genuine change of kind (black -> blue bug-check) is
                // always announced.
                else if(!self.blankAnnounced_ && self.tick_-self.blankSince_>=550
                        && !(kind==self.lastBlankAnnouncedKind_ && self.tick_<self.blankCooldownUntil_)) {
                    self.blankAnnounced_=true;self.lastBlankAnnouncedKind_=kind;self.blankCooldownUntil_=self.tick_+3000;self.AnnounceBlank(kind);}
            } else if(self.blankKind_) {self.blankKind_=0;self.blankAnnounced_=false;}
        } else if(self.blankKind_ && (self.tick_-self.lastOutput_<250 || self.tick_-self.lastBlt_<250 || self.tick_-self.keyTick_<250)) {
            self.blankKind_=0;self.blankAnnounced_=false;   // activity resumed: leave the blank state
        }
        self.DriveBlankTone();
        if(self.options_.reviewOnly || self.GraphicsOwned()) {}
        else if(self.graphicsActivity_ && !self.graphicsAnnounced_ && !self.blankKind_ && self.tick_-self.graphicsActivity_>=150
            && self.lastText_<self.graphicsActivity_) {
            // r397 (user): describe the strictly graphical screen (regions,
            // coverage, where, which is highlighted). Only when there is
            // graphical content; a truly empty frame is left to the black/blue
            // screen detector so the two never talk over each other.
            char16_t shape[300]={};uint32_t sig=0;
            if(self.DescribeGraphics(shape,300,&sig)) {
                self.graphicsAnnounced_=true;self.graphicsSig_=sig;
                if(self.options_.explicitMessages) {char16_t both[400]={};size_t a=0;
                    static const char16_t* plP[]={u"No popatrz, kurwa:",u"Graficzny cyrk:",u"Ładne obrazki:"};
                    static const char16_t* enP[]={u"Well, fuck me:",u"A graphical circus:",u"Pretty pictures:"};
                    Put(both,a,400,self.OwnPhrase(self.options_.polish?u"":u"",plP,enP,3));Put(both,a,400,u" ");Put(both,a,400,shape);self.Speak(both);}
                else self.Speak(shape);
            }
        }
        // r397 (user): on a graphical screen speak after a navigation key too
        // (arrows, Tab, Shift+Tab move a graphical highlight). Re-describe only
        // when the layout/highlight actually changed, so a key gives feedback
        // without repeating the same thing.
        else if(self.graphicsActivity_ && self.graphicsAnnounced_ && !self.blankKind_ && self.KeyRecent()
            && self.lastText_<self.graphicsActivity_ && self.tick_-self.keyTick_>=3
            && self.tick_-self.lastBlt_>=2 && self.tick_-self.lastOutput_>=2) {
            char16_t shape[300]={};uint32_t sig=0;
            if(self.DescribeGraphics(shape,300,&sig) && sig!=self.graphicsSig_) {self.graphicsSig_=sig;self.Speak(shape);}
        }
        // A missing device (not yet connected) is retried every half second;
        // no pumping in between, so firmware is not slowed down by retries.
        if(self.tick_<self.pumpRetry_)return;
        if(self.options_.reviewOnly) {}   // r401: beside Charon the voice is pumped by Charon itself (never twice, never at raised priority)
        else if(VoiceSwitching()) {}   // r401: a synthesizer is being changed right now
        else if(self.options_.voice->Pump(self.options_.voice,2048)!=EFI_SUCCESS)self.pumpRetry_=self.tick_+50;
    }
    // Speak the handoff, then silence DMA. Runs in the loader's context, at
    // its TPL, with no allocation (see header comment).
    uint64_t GraphicsOrBusy() const {return busyAnnounced_ && busyAt_>=graphicsActivity_?busyAt_+1:graphicsActivity_;}
    void Quiesce(bool announce,bool leaving=false) {
        if(stopped_)return;
        // r366: a program swapped in by a chain loader that ends firmware
        // services at once (Memtest86+) is named with the handoff.
        char16_t named[96]={};
        if(announce)WatchImage(named);
        stopped_=true;
        if(timer_)boot_->SetTimer(timer_,TimerCancel,0);
        if(announce) {
            // r361: the interface sound of the system taking over, with the
            // spoken notice; both are waited for (at most 5 s).
            const bool cue=options_.effects && options_.effects->QueueCue;
            if(cue)options_.effects->QueueCue(options_.effects->context,MenuCue::systemLaunch);
            const char16_t* text=options_.verbosity>=2
                ?(options_.polish?u"Program rozruchowy kończy usługi firmware. System operacyjny przejmuje sterowanie; Charon milknie.":u"The boot program is ending firmware services. The operating system takes over; Charon goes silent.")
                :(options_.polish?u"System przejmuje sterowanie.":u"The system takes over.");
            static const char16_t* plTake[]={u"No i chuj, system przejmuje stery, ja spadam.",u"System przejmuje sterowanie, dalej radź sobie sam, kurwa.",u"Pa, frajerze, system przejmuje stery."};
            static const char16_t* enTake[]={u"Fuck it, the system takes over, I’m out.",u"The system takes over, you’re on your own now, damn it.",u"Bye, sucker, the system takes the wheel."};
            text=OwnPhrase(text,plTake,enTake,3);
            static char16_t both[600];both[0]=0;
            // r400: with only pictures on the screen (a loading logo), say what is shown.
            static char16_t shown[300];shown[0]=0;
            if(leaving && lastText_<GraphicsOrBusy() && DescribeGraphics(shown,300)) {
                size_t at=0;for(size_t i=0;text[i] && at<250;++i)both[at++]=text[i];
                both[at++]=u' ';const char16_t* cut=shown;
                for(size_t i=0;shown[i];++i)if(shown[i]==u'.' && shown[i+1]==u' ') {cut=shown+i+2;break;}   // without "Graphical screen."
                for(size_t i=0;cut[i] && at<598;++i)both[at++]=cut[i];
                both[at]=0;text=both;
            }
            if(named[0] && text!=both) {size_t at=0;for(size_t i=0;named[i] && at<150;++i)both[at++]=named[i];both[at++]=u' ';for(size_t i=0;text[i] && at<254;++i)both[at++]=text[i];both[at]=0;text=both;}
            static char utf8[2000];Utf8(text,utf8,sizeof(utf8));
            const bool spoke=options_.voice->SpeakUtf8(options_.voice,options_.language,utf8)==EFI_SUCCESS;
            if(spoke || cue) {
                auto* fx=options_.effects;
                for(unsigned i=0;i<2500;++i) {
                    if(options_.voice->Pump(options_.voice,2048)!=EFI_SUCCESS)break;
                    const bool speechDone=!spoke || (fx && fx->SpeechIdle && fx->SpeechIdle(fx->context));
                    const bool cueDone=!cue || !fx->CuesIdle || fx->CuesIdle(fx->context);
                    if(i>20 && speechDone && cueDone)break;
                    boot_->Stall(2000);
                }
            }
        }
        bool stoppedAudio=false;
        for(unsigned attempt=0;attempt<3;++attempt)if(options_.voice->Stop(options_.voice)==EFI_SUCCESS) {stoppedAudio=true;break;}
        // r400 (user: sound until the system shows its own screen): firmware
        // code ends here, so the sound card itself is left repeating a soft
        // "loading" cue from reserved memory (about a minute, then silence)
        // until the operating system's audio driver takes the card. Not for
        // test tools that never load one.
        if(leaving && stoppedAudio && options_.linger && options_.effects && options_.effects->Linger
            && !(bootManager_ && BootTool(bootManager_)))options_.effects->Linger(options_.effects->context);
    }
    void RecrcRuntime() {
        auto* rt=table_->RuntimeServices;rt->Hdr.CRC32=0;uint32_t crc=0;
        if(boot_->CalculateCrc32(rt,rt->Hdr.HeaderSize,&crc)==EFI_SUCCESS)rt->Hdr.CRC32=crc;
    }
    // r400: falling tones and "Charon is turning off", then the real reset.
    static efi_status EFIAPI HookReset(efi_reset_type type,efi_status status,size_t size,void* data) {
        auto* self=active_;
        if(!self || !self->originalReset_)return EFI_DEVICE_ERROR;
        auto original=self->originalReset_;
        if(self->installed_ && !self->stopped_ && !self->suspended_ && !self->handedOff_ && !self->quiet_) {
            efi_tpl current=self->boot_->RaiseTPL(TPL_HIGH_LEVEL);self->boot_->RestoreTPL(current);
            if(current<=TPL_APPLICATION) {
                auto* fx=self->options_.effects;
                // Whatever is being read stops; the tones play alone (speech
                // start-up sounds would replace them), then the words.
                if(fx && fx->Silence)fx->Silence(fx->context);
                if(fx && fx->Beep) {fx->Beep(fx->context,7999,320);self->boot_->Stall(380000);}
                self->Announce(self->options_.polish?u"Wyłączanie Charona.":u"Charon is turning off.");
                self->farewell_=true;   // nothing drawn after this may cut the farewell
                // Speech needs a moment to start: wait for it to begin (2 s at
                // most), then for its end (4 s in all), then let the last
                // samples leave the sound card.
                bool started=false;unsigned i=0;
                for(;i<400;++i) {
                    self->boot_->Stall(10000);
                    const bool idle=!fx || !fx->SpeechIdle || fx->SpeechIdle(fx->context);
                    if(!idle)started=true;
                    if((started && idle && i>40) || (!started && i>200))break;
                }
                self->boot_->Stall(250000);
                charontrace::Value("reader farewell",(uint64_t(started)<<16)|i);
                const efi_status result=original(type,status,size,data);
                self->farewell_=false;   // the reset came back: keep reading
                return result;
            }
        }
        return original(type,status,size,data);
    }
    static efi_status EFIAPI HookExit(efi_handle image,size_t key) {
        auto* self=active_;
        if(!self)return EFI_DEVICE_ERROR;
        auto original=self->originalExit_;
        if(!self->installed_)return original?original(image,key):EFI_DEVICE_ERROR;   // uninstalled, reached through a wrapper
        if(self->Paused())self->BlockWhilePaused();   // r390: read the last messages before the system takes over
        efi_tpl current=self->boot_->RaiseTPL(TPL_HIGH_LEVEL);self->boot_->RestoreTPL(current);
        self->Quiesce(current<=TPL_APPLICATION,true);
        self->RemoveHooks();
        self->handedOff_=true;
        return original(image,key);
    }
    // r361: another boot manager starting (by Charon, a firmware boot option
    // or a chain loader) gets its interface sound and its name.
    static efi_status EFIAPI HookStart(efi_handle image,size_t* exitSize,char16_t** exitData) {
        auto* self=active_;
        if(!self)return EFI_DEVICE_ERROR;
        auto original=self->originalStart_;
        if(!self->installed_)return original?original(image,exitSize,exitData):EFI_DEVICE_ERROR;   // uninstalled, reached through a wrapper
        const char16_t* previous=self->bootManager_;
        const efi_handle outerImage=self->currentImage_;void* const outerBase=self->currentBase_;
        if(!self->stopped_ && !self->suspended_) {
            Guard guard(self->boot_);previous=self->ObserveStart(image,previous);
            self->currentImage_=image;self->currentBase_=self->ImageBaseOf(image);
            // r388: what was on the screen before (firmware's "BdsDxe: loading
            // ..." lines) has been read; a program that draws its menu without
            // clearing the screen (Syslinux) must not be read mixed with it.
            if(self->anyDirty_)self->Flush();
            self->ClearShadow();
            if(self->real_ && self->real_->Mode && self->real_->Mode->CursorColumn>=0 && self->real_->Mode->CursorRow>=0) {
                self->column_=size_t(self->real_->Mode->CursorColumn);self->row_=size_t(self->real_->Mode->CursorRow);
                if(self->column_>=self->columns_)self->column_=0;if(self->row_>=self->rows_)self->row_=self->rows_?self->rows_-1:0;
            }
        }
        if(!self->stopped_ && !self->suspended_ && self->options_.effects && self->options_.effects->PreloadSynths) {   // r396e
            const efi_tpl now=self->boot_->RaiseTPL(TPL_HIGH_LEVEL);self->boot_->RestoreTPL(now);
            if(now<=TPL_APPLICATION)self->options_.effects->PreloadSynths(self->options_.effects->context);
        }
        if(self->hiddenPending_ && !self->stopped_ && !self->suspended_)self->HiddenMenuPrompt();
        const efi_status status=original(image,exitSize,exitData);
        // Back in the program that started it: its name applies again.
        if(active_==self) {self->bootManager_=previous;self->currentImage_=outerImage;self->currentBase_=outerBase;}
        return status;
    }
    static void EFIAPI ImageNotify(efi_event,void* context) {
        auto& self=*static_cast<ResidentReader*>(context);
        if(self.stopped_ || self.hooksRemoved_ || !self.boot_->LocateHandle)return;
        for(size_t guard=0;guard<16;++guard) {
            efi_handle handle=nullptr;size_t bytes=sizeof(handle);
            if(self.boot_->LocateHandle(ByRegisterNotify,nullptr,self.imageRegistration_,&bytes,&handle)!=EFI_SUCCESS || !handle)break;
            if(!self.suspended_)self.ObserveStart(handle,self.bootManager_);
        }
    }
    // Returns the boot manager name in effect before this image (restored
    // when StartImage returns). An image seen at load time is announced once.
    const char16_t* ObserveStart(efi_handle image,const char16_t* before) {
        efi_guid guid=EFI_LOADED_IMAGE_PROTOCOL_GUID;efi_loaded_image_protocol* loaded=nullptr;
        const efi_status found=boot_->HandleProtocol?boot_->HandleProtocol(image,&guid,reinterpret_cast<void**>(&loaded)):EFI_UNSUPPORTED;
        void* const base=(found==EFI_SUCCESS && loaded)?loaded->ImageBase:nullptr;
        for(size_t i=0;i<observedCount_ && i<maxObserved;++i)if(observed_[i].handle==image && observed_[i].base==base) {
            bootManager_=observed_[i].name;return observed_[i].before;}
        Observed& entry=observed_[observedCount_++%maxObserved];entry={image,nullptr,before,base};
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader image seen",(uint64_t(found==EFI_SUCCESS)<<63)|(loaded?uint64_t(uintptr_t(loaded->ImageBase))&0x7fffffffffffull:0));
#endif
        if(found!=EFI_SUCCESS || !loaded || !loaded->ImageBase)return before;
        entry.name=Examine(loaded);
        return before;
    }
    // Names and announces a boot manager in a loaded image; its fonts are
    // taken for the screen reader.
    const char16_t* Examine(efi_loaded_image_protocol* loaded,char16_t* announcement=nullptr) {
        size_t size=size_t(loaded->ImageSize);if(size>64u*1024*1024)size=64u*1024*1024;
        const char16_t* name=DetectBootManager(static_cast<const uint8_t*>(loaded->ImageBase),size,[&]() {FeedAudio();});
        if(!name && !lastImageWasCharon) {   // r396: managers described by profile files
            if(!managers::userProfilesLoaded && options_.selfImage) {
                efi_guid lg=EFI_LOADED_IMAGE_PROTOCOL_GUID,fg=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;efi_loaded_image_protocol* own=nullptr;efi_simple_file_system_protocol* fs=nullptr;efi_file_protocol* root=nullptr;
                if(boot_->HandleProtocol(options_.selfImage,&lg,(void**)&own)==EFI_SUCCESS && own && own->DeviceHandle
                    && boot_->HandleProtocol(own->DeviceHandle,&fg,(void**)&fs)==EFI_SUCCESS && fs && fs->OpenVolume(fs,&root)==EFI_SUCCESS && root) {
                    managers::LoadUserProfiles(root);root->Close(root);}
            }
            name=managers::DetectUserManager(static_cast<const uint8_t*>(loaded->ImageBase),size,[&]() {FeedAudio();});
        }
        if(FirstStage(name))name=nullptr;  // r386: shim is announced by the loader it starts
        // The same boot manager in a second image (OpenCore's BOOTx64.efi
        // starting OpenCore.efi) is not announced twice.
        bool again=name && bootManager_;
        for(size_t i=0;again && (name[i] || bootManager_[i]);++i)again=name[i]==bootManager_[i];
        // This image's name while it runs; an image without a name (a
        // driver, a probe) keeps the name of the program around it.
        if(name)bootManager_=name;
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader start image",(uint64_t(name!=nullptr)<<63)|size);
#endif
        if(!name)return nullptr;
        if(again) {LoadCellFont(static_cast<const uint8_t*>(loaded->ImageBase),size);return name;}
        const bool tool=BootTool(name);
        if(!tool && options_.effects && options_.effects->QueueCue)options_.effects->QueueCue(options_.effects->context,MenuCue::bootManagerLoaded);
        // r387: a boot manager is not named any more (the user wants its
        // messages at once; its cue still sounds). Tools that end firmware
        // services right away (Memtest86+) keep "Started: ...".
        if(!tool && announcement)announcement[0]=0;
        char16_t text[96];size_t at=0;
        const char16_t* prefix=tool?(options_.polish?u"Uruchomiono: ":u"Started: "):(options_.polish?u"Menedżer rozruchu: ":u"Boot manager: ");
        static const char16_t* plTool[]={u"Odpalono, kurwa: ",u"Wjeżdża narzędzie, do chuja: ",u"No i odpalił się, ja pierdolę: "};
        static const char16_t* enTool[]={u"Fired up, damn it: ",u"In comes the tool, you bastard: ",u"And off it goes, fuck: "};
        static const char16_t* plManager[]={u"Wjeżdża menedżer rozruchu, kurwa: ",u"No i mamy kolejnego menedżera rozruchu, do chuja: ",u"O w dupę, menedżer rozruchu: "};
        static const char16_t* enManager[]={u"Here comes a fucking boot manager: ",u"And now another bloody boot manager: ",u"Boot manager, damn it: "};
        prefix=tool?OwnPhrase(prefix,plTool,enTool,3):OwnPhrase(prefix,plManager,enManager,3);
        for(size_t i=0;prefix[i] && at<80;++i)text[at++]=prefix[i];
        for(size_t i=0;name[i] && at<94;++i)text[at++]=name[i];
        text[at++]=u'.';text[at]=0;
        if(!tool) {}
        else if(announcement) {size_t i=0;for(;text[i];++i)announcement[i]=text[i];announcement[i]=0;}
        else Speak(text);
        // Windows Boot Manager draws with its own TrueType fonts; they are
        // read from the disk it starts from (never shipped with Charon).
        countdownStopped_=false;revealArmed_=false;
        if(!tool)NoteHiddenMenu(name,loaded);   // r393
        if(name[0]==u'W') {LoadBootFonts(loaded->DeviceHandle);HookBlockReads();}
        else LoadCellFont(static_cast<const uint8_t*>(loaded->ImageBase),size);
        if(GrubFamily(name))LoadGrubFonts(loaded->DeviceHandle);
        // r384: OpenCanopy (OpenCore's graphical picker) draws its labels with
        // the BMFont atlas in EFI\OC\Resources\Font on the same disk.
        if(name[0]==u'O')LoadCanopyFonts(loaded->DeviceHandle);
        // r367: GRUB themes draw with PF2 fonts (DejaVu Sans of Debian's
        // starfield theme and others made by grub-mkfont).
        if(GrubFamily(name)) {ttfOcr_.SetAllocator(TtfAlloc,TtfFree,this);for(unsigned k=0;k<pf2FontCount;++k)ttfOcr_.AddPf2Font(pf2Fonts[k]);}
        return name;
    }
    // r363: a chain loader with its own PE loader (Debian's GRUB) moves its
    // own loaded image to the next program instead of loading a new one:
    // the running image's base is watched twice a second.
    void WatchImage(char16_t* announcement=nullptr) {
        if(!currentImage_ || !boot_->HandleProtocol)return;
        efi_guid guid=EFI_LOADED_IMAGE_PROTOCOL_GUID;efi_loaded_image_protocol* loaded=nullptr;
        if(boot_->HandleProtocol(currentImage_,&guid,reinterpret_cast<void**>(&loaded))!=EFI_SUCCESS || !loaded || !loaded->ImageBase)return;
        if(loaded->ImageBase==currentBase_)return;
        currentBase_=loaded->ImageBase;
        Examine(loaded,announcement);
    }
    void* ImageBaseOf(efi_handle image) const {
        efi_guid guid=EFI_LOADED_IMAGE_PROTOCOL_GUID;efi_loaded_image_protocol* loaded=nullptr;
        if(!image || !boot_->HandleProtocol || boot_->HandleProtocol(image,&guid,reinterpret_cast<void**>(&loaded))!=EFI_SUCCESS || !loaded)return nullptr;
        return loaded->ImageBase;
    }
    efi_handle currentImage_=nullptr;void* currentBase_=nullptr;
    static constexpr size_t maxObserved=32;
    struct Observed {efi_handle handle;const char16_t* name;const char16_t* before;void* base;};   // base: firmware gives a later image the handle address of an unloaded one
    Observed observed_[maxObserved]={};size_t observedCount_=0;
    efi_event imageNotify_=nullptr;void* imageRegistration_=nullptr;
    // r363/r364: boot managers with their own graphical terminal carry an
    // 8-pixel-wide console font. 'A' marks it: VGA style (Limine: 16 rows,
    // leftmost pixel in the top bit) or Terminus style (OpenCore: 14 rows,
    // leftmost pixel in the low bit). Glyphs ' '..'~' are copied into 8x16
    // form (the image may be unloaded); ' ' must be empty and 'B' not.
    void LoadCellFont(const uint8_t* image,size_t size) {
        struct Style {uint8_t a[8];size_t stride;bool lowBitLeft;};
        // VGA: 'A' rows begin 38 6c (Limine) or 10 38 6c (IBM VGA, Memtest86+).
        static const Style styles[]={{{0x38,0x6c,0xc6,0xc6,0xc6,0xc6,0xfe,0xc6},16,false},{{0x10,0x38,0x6c,0xc6,0xc6,0xfe,0xc6,0xc6},16,false},{{0x3e,0x63,0x63,0x63,0x7f,0x63,0x63,0x63},14,true}};
        for(const Style& st:styles)for(size_t i=0;i+8<=size;++i) {
            if(image[i]!=st.a[0] || image[i+1]!=st.a[1])continue;
            bool match=true;for(size_t k=2;k<8 && match;++k)match=image[i+k]==st.a[k];
            if(!match)continue;
            // Rows above 'A' in its cell: VGA puts two blank rows first.
            const size_t lead=st.stride==16?2:0;  // both VGA variants start 'A' in row 2
            if(i<lead+33*st.stride || i-lead+62*st.stride+st.stride>size)continue;
            const size_t glyphA=i-lead,space=glyphA-33*st.stride,b=glyphA+st.stride;
            bool empty=true;for(size_t k=0;k<st.stride;++k)empty=empty && !image[space+k];
            bool inked=false;for(size_t k=0;k<st.stride;++k)inked=inked || image[b+k];
            if(!empty || !inked)continue;
            uint16_t* copy=nullptr;
            if(boot_->AllocatePool(EfiBootServicesData,4096*2,reinterpret_cast<void**>(&copy))!=EFI_SUCCESS || !copy)return;
            for(size_t k=0;k<4096;++k)copy[k]=0;
            for(size_t c=32;c<127;++c)for(size_t r=0;r<st.stride && r<16;++r) {
                uint8_t v=image[glyphA+(c-65)*st.stride+r];
                if(st.lowBitLeft) {uint8_t m=0;for(int bit=0;bit<8;++bit)if(v&(1<<bit))m|=uint8_t(0x80>>bit);v=m;}
                copy[c*16+r]=uint16_t(v<<8);
            }
            ttfOcr_.SetAllocator(TtfAlloc,TtfFree,this);
            if(!ttfOcr_.AddCellFont(copy,8,16,8))boot_->FreePool(copy);
#ifdef HAK_CHARON_READER_TRACE
            charontrace::Value("reader cell font",(uint64_t(st.stride)<<48)|glyphA);
#endif
            break;  // one console font per style; a grid font image may follow
        }
        // Clover carries OpenCore's console font and its own font image.
        LoadGridFont(image,size);
    }
    // r365: a font kept as a palette PNG of 16x16 glyph cells (Clover's
    // embedded theme: 176x288, cells 11x18, drawn 9 pixels apart).
    void LoadGridFont(const uint8_t* image,size_t size) {
        static const uint8_t signature[8]={0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
        for(size_t i=0;i+33<=size;++i) {
            if(image[i]!=0x89 || image[i+1]!='P')continue;
            bool match=true;for(size_t k=2;k<8 && match;++k)match=image[i+k]==signature[k];
            if(!match)continue;
            const uint8_t* h=image+i+16;
            const size_t width=size_t(h[0])<<24|size_t(h[1])<<16|size_t(h[2])<<8|h[3],height=size_t(h[4])<<24|size_t(h[5])<<16|size_t(h[6])<<8|h[7];
            if(h[9]!=3 || width%16 || height%16 || width<64 || width>256 || height<96 || height>384)continue;
            const size_t rowBytes=(width*h[8]+7)/8,workSize=height*(1+rowBytes)+size_t(64)*1024;
            uint8_t* work=nullptr;uint8_t* index=nullptr;uint16_t* table=nullptr;
            if(boot_->AllocatePool(EfiBootServicesData,workSize,reinterpret_cast<void**>(&work))!=EFI_SUCCESS)work=nullptr;
            if(boot_->AllocatePool(EfiBootServicesData,width*height,reinterpret_cast<void**>(&index))!=EFI_SUCCESS)index=nullptr;
            if(boot_->AllocatePool(EfiBootServicesData,256*24*2,reinterpret_cast<void**>(&table))!=EFI_SUCCESS)table=nullptr;
            PngImage png;size_t cellWidth=0,cellHeight=0;
            const bool ok=work && index && table && DecodePalettePng(image+i,size-i,work,workSize,index,width*height,png)
                && GridFont(index,png,table,cellWidth,cellHeight);
            if(work)boot_->FreePool(work);
            if(index)boot_->FreePool(index);
            if(ok) {
                ttfOcr_.SetAllocator(TtfAlloc,TtfFree,this);
                if(!ttfOcr_.AddCellFont(table,cellWidth,cellHeight,cellWidth>8?cellWidth-2:cellWidth))boot_->FreePool(table);
#ifdef HAK_CHARON_READER_TRACE
                charontrace::Value("reader grid font",(uint64_t(cellWidth)<<40)|(uint64_t(cellHeight)<<32)|i);
#endif
                return;
            }
            if(table)boot_->FreePool(table);
            FeedAudio();
        }
    }
    static void* TtfAlloc(void* context,size_t bytes) {
        void* block=nullptr;
        if(static_cast<ResidentReader*>(context)->boot_->AllocatePool(EfiBootServicesData,bytes,&block)==EFI_SUCCESS && block)return block;
        // r397: no firmware memory left (Windows Memory Diagnostic takes it
        // all; glyph templates of a new size could not be made and the line
        // "Turn the cache on for all tests." was read "urn tne cacne on ror a
        // tests."): the reserve of CharonPoolEFI.h.
        return charon::speechReserve.Take(bytes);
    }
    static void TtfFree(void* context,void* block) {
        if(!block)return;
        if(charon::speechReserve.Inside(block)) {charon::speechReserve.Give(block);return;}
        static_cast<ResidentReader*>(context)->boot_->FreePool(block);
    }
    // r384: OpenCanopy's Font_1x / Font_2x: an 8-bit greyscale PNG atlas and
    // a binary BMFont description (block 2 common: line height, base; block 4:
    // 20-byte chars). Glyphs become 1-bit PF2-style templates (coverage of
    // half or more), placed on the base line, with their own advances.
    uint8_t* ReadWhole(efi_file_protocol* root,const char16_t* name,size_t& size,size_t limit) {
        efi_file_protocol* file=nullptr;size=0;
        if(root->Open(root,&file,const_cast<char16_t*>(name),EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !file)return nullptr;
        uint8_t* data=nullptr;uint64_t end=0;
        if(file->SetPosition(file,UINT64_MAX)==EFI_SUCCESS && file->GetPosition(file,&end)==EFI_SUCCESS && end>=8 && end<=limit
            && file->SetPosition(file,0)==EFI_SUCCESS && boot_->AllocatePool(EfiBootServicesData,size_t(end),reinterpret_cast<void**>(&data))==EFI_SUCCESS && data) {
            size=size_t(end);size_t got=size;
            if(file->Read(file,&got,data)!=EFI_SUCCESS || got!=size) {boot_->FreePool(data);data=nullptr;size=0;}
        }
        file->Close(file);return data;
    }
    void LoadCanopyFonts(efi_handle device) {
        efi_guid guid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;efi_simple_file_system_protocol* fs=nullptr;
        if(!device || boot_->HandleProtocol(device,&guid,reinterpret_cast<void**>(&fs))!=EFI_SUCCESS || !fs)return;
        efi_file_protocol* root=nullptr;
        if(fs->OpenVolume(fs,&root)!=EFI_SUCCESS || !root)return;
        ttfOcr_.SetAllocator(TtfAlloc,TtfFree,this);
        static const char16_t* const pngs[]={u"\\EFI\\OC\\Resources\\Font\\Font_1x.png",u"\\EFI\\OC\\Resources\\Font\\Font_2x.png"};
        static const char16_t* const bins[]={u"\\EFI\\OC\\Resources\\Font\\Font_1x.bin",u"\\EFI\\OC\\Resources\\Font\\Font_2x.bin"};
        size_t loaded=0;
        for(size_t k=0;k<2;++k) {
            size_t pngSize=0,binSize=0;
            uint8_t* png=ReadWhole(root,pngs[k],pngSize,1024*1024);
            uint8_t* bin=png?ReadWhole(root,bins[k],binSize,64*1024):nullptr;
            if(png && bin && AddBmFont(png,pngSize,bin,binSize))++loaded;
            if(png)boot_->FreePool(png);
            if(bin)boot_->FreePool(bin);
        }
        root->Close(root);
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader canopy fonts",loaded);
#endif
        (void)loaded;
    }
    bool AddBmFont(const uint8_t* png,size_t pngSize,const uint8_t* bin,size_t binSize) {
        Pf2Font* font=nullptr;
        if(!BuildBmFont(png,pngSize,bin,binSize,{TtfAlloc,TtfFree,this},font))return false;
        if(!bmOcr_.Ready())bmOcr_.SetFont(font);  // the 1x atlas first (OpenCanopy's usual scale)
        if(ttfOcr_.AddPf2Font(*font))return true;
        if(bmOcr_.Ready())return true;
        TtfFree(this,const_cast<Pf2Glyph*>(font->glyphs));TtfFree(this,const_cast<uint8_t*>(font->bits));TtfFree(this,const_cast<uint8_t*>(font->coverage));TtfFree(this,font);
        return false;
    }
    // r386: GRUB's own 16-pixel font (unicode.pf2 on the disk GRUB started
    // from: Ventoy's VTOYEFI, a distribution's ESP) joins the cell reader, so
    // glyphs that differ from the built-in Unifont are read exactly. PF2:
    // sections NAME/len(BE32); ASCE, DESC; CHIX entries code(BE32) flags(1)
    // offset(BE32); a glyph is width, height, x and y offset, advance (BE16)
    // and its bits packed row by row.
    static uint32_t Be32(const uint8_t* p) {return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
    static int Be16(const uint8_t* p) {return int16_t(uint16_t(p[0]<<8|p[1]));}
    size_t AddPf2Cells(const uint8_t* d,size_t n) {
        if(n<12 || d[0]!='F' || d[1]!='I' || d[2]!='L' || d[3]!='E')return 0;
        int ascent=-1,descent=-1;const uint8_t* chix=nullptr;size_t chixLength=0;
        for(size_t at=0;at+8<=n;) {
            const uint8_t* s=d+at;const uint32_t length=Be32(s+4);
            if(s[0]=='D' && s[1]=='A' && s[2]=='T' && s[3]=='A')break;
            if(at+8+size_t(length)>n)return 0;
            if(s[0]=='A' && s[1]=='S' && s[2]=='C' && s[3]=='E' && length>=2)ascent=Be16(s+8);
            if(s[0]=='D' && s[1]=='E' && s[2]=='S' && s[3]=='C' && length>=2)descent=Be16(s+8);
            if(s[0]=='C' && s[1]=='H' && s[2]=='I' && s[3]=='X') {chix=s+8;chixLength=length;}
            at+=8+length;
        }
        if(!chix || ascent<0 || descent<0 || ascent+descent!=16)return 0;
        size_t added=0;
        for(size_t e=0;e+9<=chixLength;e+=9) {
            const uint32_t code=Be32(chix+e),offset=Be32(chix+e+5);
            if(!((code>=0x20 && code<0x180) || (code>=0x2500 && code<0x25a0)))continue;
            if(size_t(offset)+10>n)continue;
            const uint8_t* g=d+offset;
            const int w=Be16(g),h=Be16(g+2),xo=Be16(g+4),yo=Be16(g+6),adv=Be16(g+8);
            if(adv!=8 || w<0 || h<0 || w>8 || h>16 || size_t(offset)+10+size_t((w*h+7)/8)>n)continue;
            uint8_t rows[16]={};
            const int top=ascent-yo-h;
            for(int r=0;r<h;++r)for(int x=0;x<w;++x) {
                const int bit=r*w+x;
                if(!(g[10+bit/8]&(0x80>>(bit%8))))continue;
                const int row=top+r,col=x+xo;
                if(row>=0 && row<16 && col>=0 && col<8)rows[row]|=uint8_t(0x80>>col);
            }
            if(ocr_.AddGlyph(uint16_t(code),rows))++added;
        }
        return added;
    }
    void LoadGrubFonts(efi_handle device) {
        efi_guid guid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;efi_simple_file_system_protocol* fs=nullptr;
        if(!device || boot_->HandleProtocol(device,&guid,reinterpret_cast<void**>(&fs))!=EFI_SUCCESS || !fs)return;
        efi_file_protocol* root=nullptr;
        if(fs->OpenVolume(fs,&root)!=EFI_SUCCESS || !root)return;
        static const char16_t* const names[]={u"\\grub\\fonts\\unicode.pf2",u"\\boot\\grub\\fonts\\unicode.pf2",u"\\EFI\\BOOT\\fonts\\unicode.pf2",u"\\boot\\grub\\unicode.pf2"};
        size_t added=0;
        for(const char16_t* name:names) {
            size_t size=0;uint8_t* data=ReadWhole(root,name,size,8u*1024*1024);
            if(!data)continue;
            added+=AddPf2Cells(data,size);boot_->FreePool(data);
            FeedAudio();
            break;
        }
        root->Close(root);
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader grub cells",added);
#endif
        (void)added;
    }
    void LoadBootFonts(efi_handle device) {
        efi_guid guid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;efi_simple_file_system_protocol* fs=nullptr;
        if(!device || boot_->HandleProtocol(device,&guid,reinterpret_cast<void**>(&fs))!=EFI_SUCCESS || !fs)return;
        efi_file_protocol* root=nullptr;
        if(fs->OpenVolume(fs,&root)!=EFI_SUCCESS || !root)return;
        ttfOcr_.SetAllocator(TtfAlloc,TtfFree,this);
        static const char16_t* const names[]={u"\\EFI\\Microsoft\\Boot\\Fonts\\segoe_slboot.ttf",u"\\EFI\\Microsoft\\Boot\\Fonts\\segoen_slboot.ttf",
            u"\\EFI\\Microsoft\\Boot\\Fonts\\segmono_boot.ttf",u"\\EFI\\Microsoft\\Boot\\Fonts\\wgl4_boot.ttf"};
        size_t loaded=0;
        for(const char16_t* name:names) {
            if(loadedFonts_>=sizeof(fontData_)/sizeof(fontData_[0]))break;
            efi_file_protocol* file=nullptr;
            if(root->Open(root,&file,const_cast<char16_t*>(name),EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !file)continue;
            uint8_t* data=nullptr;size_t size=0;
            if(file->SetPosition(file,UINT64_MAX)==EFI_SUCCESS) {
                uint64_t end=0;
                if(file->GetPosition(file,&end)==EFI_SUCCESS && end>=12 && end<=4u*1024*1024 && file->SetPosition(file,0)==EFI_SUCCESS
                    && boot_->AllocatePool(EfiBootServicesData,size_t(end),reinterpret_cast<void**>(&data))==EFI_SUCCESS && data) {
                    size=size_t(end);size_t got=size;
                    if(file->Read(file,&got,data)!=EFI_SUCCESS || got!=size) {boot_->FreePool(data);data=nullptr;}
                }
            }
            file->Close(file);
            if(!data)continue;
            if(ttfOcr_.AddFont(data,size)) {fontData_[loadedFonts_++]=data;++loaded;}
            else boot_->FreePool(data);
        }
        root->Close(root);
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader boot fonts",loaded);
#endif
        (void)loaded;
    }
    // Safety net when a loader reached ExitBootServices through a pointer it
    // cached before Install: silence only, no speech, no hook changes.
    static void EFIAPI ExitNotify(efi_event,void* context) {
        auto& self=*static_cast<ResidentReader*>(context);
        if(self.stopped_)return;
        self.Quiesce(false);self.handedOff_=true;
    }
    // ---- proxy ---------------------------------------------------------
    static volatile int& GuardDepth() {static volatile int depth=0;return depth;}
    struct Guard {
        efi_boot_services* boot;efi_tpl old=0;bool raised=false;
        explicit Guard(efi_boot_services* b):boot(b) {
            const efi_tpl current=boot->RaiseTPL(TPL_HIGH_LEVEL);boot->RestoreTPL(current);
            if(current<TPL_CALLBACK) {old=boot->RaiseTPL(TPL_CALLBACK);raised=true;}
            ++GuardDepth();   // r402: the rescue timer stays out of guarded reader code
        }
        ~Guard() {--GuardDepth();if(raised)boot->RestoreTPL(old);}
    };
    static efi_status EFIAPI ProxyOutput(efi_simple_text_output_protocol* self,char16_t* text) {
        auto& r=Self(self);
        if(!r.installed_)return r.saved_.OutputString(self,text);   // uninstalled, reached through another program's wrapper
        r.ApplyPendingSynth();   // r396e: programs that never read ConIn (OpenCore, forms) still print
        // r386: the text starts where the real cursor is. A shadow cursor left
        // at a line end (the previous program's output) put the first letter
        // on the line above ("i. PXE initialising", "L. oading bootloader").
        if(r.Paused() && self==r.real_)r.BlockWhilePaused();   // r390 paused boot
        int32_t startColumn=-1,startRow=-1;
        if(self==r.real_ && self->Mode) {startColumn=self->Mode->CursorColumn;startRow=self->Mode->CursorRow;}
        r.inConOut_=true;const auto status=r.saved_.OutputString(self,text);r.inConOut_=false;
        // r397: only where the shadow cannot know (a new program's first text,
        // a shadow cursor at a line end): the Mode of a console splitter
        // follows its first child (a serial terminal of another width), which
        // put the UEFI Shell's help names and descriptions in swapped columns.
        const bool trustMode=r.outputImage_!=r.currentImage_ || r.column_+1>=r.columns_;
        r.outputImage_=r.currentImage_;
        if(trustMode && startColumn>=0 && startRow>=0 && size_t(startColumn)<r.columns_ && size_t(startRow)<r.rows_) {r.column_=size_t(startColumn);r.row_=size_t(startRow);}
#ifdef HAK_CHARON_READER_TRACE
        if(text && self==r.real_) {char t[400];Utf8(text,t,sizeof(t));
            charontrace::Value("reader at",(uint64_t(r.row_)<<32)|(uint64_t(r.column_)<<16)|r.attribute_);
            if(self->Mode)charontrace::Value("reader end",(uint64_t(uint32_t(self->Mode->CursorRow))<<32)|(uint64_t(uint32_t(self->Mode->CursorColumn))<<16)|uint64_t(uint32_t(startColumn)&0xffff));
            charontrace::Text(r.secret_.Masks(r.row_,r.column_,secretinput::Inked(text),secretinput::PublicNotice(text))?"(secret input)":t);}   // r407
#endif
        if(text && !r.stopped_ && !r.suspended_ && self==r.real_) {Guard guard(r.boot_);r.Capture(text);}
        return status;
    }
    static efi_status EFIAPI ProxyReset(efi_simple_text_output_protocol* self,bool extended) {
        auto& r=Self(self);r.inConOut_=true;const auto status=r.saved_.Reset(self,extended);r.inConOut_=false;
        if(!r.installed_)return status;
        Guard guard(r.boot_);r.QueryDimensions();r.ClearShadow();return status;
    }
    static efi_status EFIAPI ProxySetMode(efi_simple_text_output_protocol* self,size_t mode) {
        auto& r=Self(self);r.inConOut_=true;const auto status=r.saved_.SetMode(self,mode);r.inConOut_=false;
        if(!r.installed_)return status;
        Guard guard(r.boot_);r.QueryDimensions();r.ClearShadow();return status;
    }
    static efi_status EFIAPI ProxyAttribute(efi_simple_text_output_protocol* self,size_t attribute) {
        auto& r=Self(self);const auto status=r.saved_.SetAttribute(self,attribute);
        if(status==EFI_SUCCESS && r.installed_)r.attribute_=attribute;
        return status;
    }
    static efi_status EFIAPI ProxyClear(efi_simple_text_output_protocol* self) {
        auto& r=Self(self);r.inConOut_=true;const auto status=r.saved_.ClearScreen(self);r.inConOut_=false;
        if(!r.installed_)return status;
        Guard guard(r.boot_);r.QueryDimensions();r.SnapshotPreClear();r.ClearShadow();r.baseAttribute_=r.attribute_;r.clearedScreen_=true;return status;
    }
    static efi_status EFIAPI ProxyCursor(efi_simple_text_output_protocol* self,size_t column,size_t row) {
        auto& r=Self(self);const auto status=r.saved_.SetCursorPosition(self,column,row);
        if(!r.installed_)return status;
#ifdef HAK_CHARON_READER_TRACE
        charontrace::Value("reader cursor",(uint64_t(row)<<16)|column);
#endif
        if(status==EFI_SUCCESS && (column>=r.columns_ || row>=r.rows_)) {Guard guard(r.boot_);r.QueryDimensions();}
        if(status==EFI_SUCCESS) {r.column_=column<r.columns_?column:r.columns_-1;r.row_=row<r.rows_?row:r.rows_-1;r.cursorTick_=r.tick_;}
        return status;
    }
    // r399 (user): remember the screen just before a ClearScreen so the first
    // reading after the redraw can skip lines that came back identical (a
    // terminal/boot loader repainting), reading only the new text - but only
    // when there is no menu selection on the new screen (menus read in full).
    void SnapshotPreClear() {
        for(size_t r=0;r<rows_ && r<maxRows;++r)for(size_t c=0;c<columns_ && c<maxColumns;++c)preClear_[r][c]=text_[r][c];
        preClearValid_=true;
    }
    void ClearShadow() {
        for(size_t r=0;r<maxRows;++r) {for(size_t c=0;c<maxColumns;++c) {text_[r][c]=u' ';attr_[r][c]=0;prevRaw_[r][c]=0;}dirty_[r]=false;}
        secret_.Reset();   // r407
        column_=row_=0;highlight_=SIZE_MAX;anyDirty_=false;menuAnnounced_=false;
        // r397: a program that redraws its whole screen on every update
        // (Windows Memory Diagnostic's options) gets the same selection back
        // without a key: that is no news.
        lastGroupRow_=SIZE_MAX;
        if(lastHighlight_[0]) {size_t i=0;for(;lastHighlight_[i];++i)clearedHighlight_[i]=lastHighlight_[i];clearedHighlight_[i]=0;clearedRow_=lastHighlightRow_;clearedTick_=tick_;}
        lastHighlight_[0]=0;lastHighlightRow_=SIZE_MAX;
    }
    char16_t clearedHighlight_[maxColumns+1]={};size_t clearedRow_=SIZE_MAX;uint64_t clearedTick_=0;
    efi_system_table* table_=nullptr;efi_boot_services* boot_=nullptr;
    efi_simple_text_output_protocol* real_=nullptr;
    efi_simple_text_output_protocol saved_{};
    // What RemoveHooks could not restore (another program's wrappers on top of the proxies).
    efi_simple_text_output_protocol* leftAt_=nullptr;efi_simple_text_output_protocol left_{};uint8_t leftMask_=0;
    efi_status (EFIAPI *originalExit_)(efi_handle,size_t)=nullptr;
    efi_status (EFIAPI *originalReset_)(efi_reset_type,efi_status,size_t,void*)=nullptr;
    efi_status (EFIAPI *originalStart_)(efi_handle,size_t*,char16_t**)=nullptr;
    const char* volatile marker_=nullptr;
    const char16_t* bootManager_=nullptr;
#if defined(HAK_CHARON_READER_TRACE) || defined(HAK_CHARON_POLL_DIAG)
    bool lastDirect_=false,convertFailed_=false;size_t lastGeoWidth_=0;
#endif
    uint64_t lastPoll_=0;
    ReaderKeys keys_{};volatile char16_t command_=0;char16_t lastCommandKey_=0;uint64_t lastCommand_=0;
    TtfOcr ttfOcr_{};uint8_t* fontData_[6]={};size_t loadedFonts_=0;

    efi_event timer_=nullptr,exitEvent_=nullptr;
    efi_event rescue_=nullptr;uint64_t rescueSeen_=0;unsigned rescueIdle_=0;bool rescuing_=false;volatile bool working_=false;   // r402
    ResidentOptions options_{};
    // Every member is constant-initialized: Charon has no C runtime to run
    // global constructors (an .init_array would never execute).
    char16_t text_[maxRows][maxColumns]={};uint8_t attr_[maxRows][maxColumns]={};
    char16_t prevRaw_[maxRows][maxColumns]={};bool done_[maxRows][maxColumns]={};
    char16_t preClear_[maxRows][maxColumns]={};bool preClearValid_=false;   // r399: the screen just before a ClearScreen, to read only NEW lines in a terminal that repaints
    bool dirty_[maxRows]={};char16_t spoken_[1600]={},pending_[1600]={};
    size_t columns_=80,rows_=25,column_=0,row_=0,highlight_=SIZE_MAX,attribute_=7,baseAttribute_=7;bool clearedScreen_=false;   // r397: ClearScreen seen
    uint64_t tick_=0,lastOutput_=0,firstDirty_=0,pumpRetry_=0;
    int lastPercent_=-1;
    bool menuAnnounced_=false;
    // r400: quiet countdown menus. 0 off, 1 a new menu held, 2 counting down.
    bool menuIntro_=false,everKey_=false,rereadAfterKey_=false;uint8_t quietMenu_=0;
    uint64_t quietTick_=0,countdownSeen_=0;char16_t heldMenu_[1600]={},heldInfo_[1600]={};
    bool busySaid_=false;  // r382 text spinner announced
    BmOcr bmOcr_;          // r384 OpenCanopy labels
    struct SmallBlit {size_t x,y,w,h;};SmallBlit small_[4]={{SIZE_MAX,0,0,0},{SIZE_MAX,0,0,0},{SIZE_MAX,0,0,0},{SIZE_MAX,0,0,0}};size_t smallNext_=0;  // r384 pointer/animation blits
    struct IconStrip {uint16_t x,y,w,h;bool used;};IconStrip strip_={};size_t stripSelected_=SIZE_MAX;
    uint64_t timerEndAt_=0; // r384: tick of the countdown's end tone
    size_t timerRow_=0;uint64_t endToneUntil_=0;
    // r386: `mode` is the hooked instance's Mode pointer, kept only for
    // comparison (never dereferenced): a copy of a hooked GOP structure
    // (OpenCore's ProvideConsoleGop installs the protocol again on the
    // console handle, our HookBlt included) shares it.
    struct GopHook {Gop* gop;GopBltFn original;bool console;const void* mode;};
    struct RetiredGop {const void* mode;GopBltFn original;};
    RetiredGop retired_[8]={};size_t retiredNext_=0;
    GopHook gops_[8]={};size_t gopCount_=0;
    HiiFont* font_=nullptr;StringToImageFn fontOriginal_=nullptr;StringIdToImageFn fontIdOriginal_=nullptr;
    HiiString* strings_=nullptr;char16_t stringBuffer_[512]={};
    efi_event graphicsNotify_=nullptr;void* gopRegistration_=nullptr;void* fontRegistration_=nullptr;
    bool inConOut_=false,graphicsAnnounced_=false;
    uint64_t graphicsActivity_=0,lastText_=0;
    uint64_t ownedTick_=0;bool ownedKnown_=false,owned_=false;   // r407 GraphicsOwned
    bool busyAnnounced_=false;uint64_t busyAt_=0;   // r387 loading (disk reads)
    int blankKind_=0,lastBlankAnnouncedKind_=0;bool blankAnnounced_=false;uint64_t blankSince_=0,blankToneUntil_=0,blankToneNext_=0,blankCooldownUntil_=0;unsigned blankTonePhase_=0;   // r397 black/blue screen
    uint32_t graphicsSig_=0;   // r397: last described graphical layout, for navigation feedback
    char16_t lastTab_[40]={};size_t lastTabRow_=SIZE_MAX;   // r388 tab bars
    struct Bar {size_t x,y,height,track,fill;uint32_t trackColor,fillColor;unsigned rises;};
    Bar bars_[16]={};unsigned nextBar_=0;
    PixelOcr ocr_{};AlphaOcr aaOcr_{};bool aaRead_=false;Gop* pixelGop_=nullptr;uint32_t* pixels_=nullptr;size_t pixelCapacity_=0;
    size_t dirtyLeft_=0,dirtyTop_=0,dirtyRight_=0,dirtyBottom_=0;bool pixelDirty_=false;uint64_t pixelTick_=0;
    static constexpr size_t maxBands=320;
    uint32_t bandHash_[maxBands]={};size_t geoWidth_=0,geoHeight_=0,geoBytes_=4,geoModeWidth_=0,geoModeHeight_=0;uint64_t geoBase_=0,geoRetry_=0,sampleChanged_=0;size_t geoPages_=1,geoPage_=0;uint32_t pageSample_[2]={};bool sampleDue_=false,dispiSearched_=false;PciIo* dispi_=nullptr;size_t sampleOffset_=0;bool dispiActive_=false,inFont_=false;size_t pollWidth_=0,pollHeight_=0;uint64_t lastBlt_=0;
    uint8_t majorityBg_=0;
    char16_t lastHighlight_[maxColumns+1]={};size_t lastHighlightRow_=SIZE_MAX;
    size_t reviewRow_=SIZE_MAX,reviewCol_=0;  // r378 review cursor
    volatile unsigned serviceDue_=0;bool servicing_=false;   // r401 (reviewOnly)
    bool quiet_=false;unsigned rate_=10;      // r379 automatic reading, speech rate (tenths)
    unsigned pitch_=10,volume_=10,voiceSetting_=0;   // r388 voice settings ring
    size_t caretLineX_[maxRows]={};
    char16_t lastButton_[96]={};uint64_t lastButtonTick_=0;   // r396d
    bool suspectRead_=false;unsigned suspectRetries_=0;   // r397: a half-drawn line, read again
    size_t lastGroupRow_=SIZE_MAX;   // r397: heading row of the selection's group
    const void* outputImage_=nullptr;   // r397: the program whose text the shadow cursor follows
    char16_t lastIconInfo_[256]={};   // r397: hint text an icon picker redraws with its item name
    uint16_t pendingSynth_=0xffff;   // r396: ring synthesizer choice waiting for TPL_APPLICATION
    volatile uint16_t swallowScan_=0;
    volatile bool keyPending_=false;volatile uint64_t keyTick_=0;volatile uint16_t keyScan_=0;volatile char16_t keyChar_=0;  // r389
    size_t echoRow_=0,echoCol_=0;uint64_t cursorTick_=0,lastConsoleText_=0;
    KeyData pushed_{};volatile bool havePush_=false;bool controlDown_=false;   // r389 modifier poll
    uint64_t lastScroll_=0;char16_t scrolledOut_[1200]={};
    char16_t pendingInfo_[1600]={};uint64_t infoTick_=0;   // r389 messages waiting for a quiet screen
    bool ownSpeech_=false;unsigned appliedPitch_=0;   // r388 priority pitch
    bool rawSpeech_=false,capPitch_=false;char pendingLanguage_[3]={};speechtext::Lang stickyLanguage_=speechtext::Lang::unknown;   // r390
    unsigned pressCount_=1;           // r390 same review key pressed again (spell, phonetic, code)
    bool repeating_=false;            // r390 a held key's repeats
    volatile bool helpOpen_=false;size_t helpIndex_=0;   // r390 key list
    efi_status (EFIAPI *originalStall_)(size_t)=nullptr;
    efi_status (EFIAPI *originalWait_)(size_t,efi_event*,size_t*)=nullptr;   // r396e
    uint16_t pauseSwallowChar_=0;
    bool inputHelp_=false;            // r390 NVDA input help (Insert+0 / Ctrl+Alt+X)
    volatile bool reviewPause_=false; // r390 Alt+\ : screen review with the boot paused
    UsbKeypad keypad_{};              // r390 numpad review on USB keyboards
    braille::Display braille_{};size_t brailleOffset_=0,brailleRow_=SIZE_MAX,brailleCursor_=SIZE_MAX;bool brailleManual_=false;   // r390 braille
    size_t frameRow_=SIZE_MAX,frameColumn_=0;  // r380 last announced window frame
    efi_simple_text_input_protocol* inputHooked_=nullptr;TextInputEx* exHooked_=nullptr;  // r381
    efi_status (EFIAPI *originalRead_)(efi_simple_text_input_protocol*,efi_input_key*)=nullptr;
    efi_status (EFIAPI *originalReadEx_)(TextInputEx*,KeyData*)=nullptr;
    bool insertArmed_=false;uint64_t insertTick_=0;
    // r393: fonts per cell (bits 0-7 pixel height, 8-13 face: 1 Unifont, 2 HII,
    // 3 anti-aliased, 4 TrueType/bitmap, 5 BMFont; bit 14 bold), formatting grid,
    // speech history, the screen before the last key, regions.
    uint16_t cellFont_[maxRows][maxColumns]={};uint16_t captureFont_=0;
    formatting::Grid grid_{};char16_t formatted_[2400]={};
    static constexpr size_t historySize=64,historyLength=320;
    char16_t history_[historySize][historyLength]={};size_t historyNext_=0,historyCount_=0,historyIndex_=0;volatile bool historyOpen_=false;
    char16_t beforeKey_[maxRows][maxColumns]={};size_t beforeRows_=0,beforeColumns_=0;bool haveBefore_=false;
    size_t regionIndex_=SIZE_MAX;uint64_t regionScreen_=0;
    // r393 boot-manager handling: hidden menu prompt, a key sent to the manager.
    bool hiddenPending_=false,revealArmed_=false,countdownStopped_=false;uint16_t revealScan_=0;char16_t revealChar_=0;uint64_t revealUntil_=0;
    const managers::Profile* hiddenProfile_=nullptr;bool hiddenMaybe_=false;
    bool installed_=false,stopped_=false,handedOff_=false,failed_=false,anyDirty_=false;
    bool hooksRemoved_=false,foreignConOut_=false,suspended_=false,farewell_=false;
};
#ifdef __clang__
#define CHARON_CONSTANT_INIT [[clang::require_constant_initialization]]
#else
#define CHARON_CONSTANT_INIT
#endif
CHARON_CONSTANT_INIT static ResidentReader residentReader;
// r401: a second reader, only for review beside Charon's own screens: the reader that reads started programs keeps no state from it.
CHARON_CONSTANT_INIT [[maybe_unused]] static ResidentReader reviewReader;
}
