#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hakefi {
enum class MenuPage { main, devices, settings, confirm, language, notice, startMelody, backgroundMelody,
    bootOrder, verbosity, autoDelay, progress, tools, infoList, clockEdit, welcome, reader, detail, keyList /* r390 */, formatting /* r393 */ };
enum class MenuEvent { none, up, down, home, end, enter, right, back, space, left, pageUp, pageDown };
enum class MenuAction { none, changed, edge, openDevices, openSettings, openFirmware, openShell, continueBoot,
    startDevice, toggleWrap, refresh, firmwareInfo, pendingBoot, pendingSettings,
    pendingKeys, setContinue, openLanguage, selectLanguage, toggleSounds, openMelody, selectMelody, dismissNotice, confirmed, cancelled, changeRate,
    // r355
    openBootOrder, grabEntry, moveEntry, dropEntry, grabCancelled, saveOrder, revertOrder, changeTimeout, timeoutEdge,
    changeVolume, openVerbosity, selectVerbosity, toggleRemember, openAutoDelay, selectAutoDelay, setBootNext, leaveOrder, openProgress, selectProgress, toggleFirmwareVoice,
    // r370
    openTools, systemInfo, clockInfo, restartSystem, shutdownSystem, restartFirmware,
    // r371
    openVariables, openDrivers, updatesInfo, listDetail,
    // r372
    openClock, changeClock, clockEdge, saveClock,
    // r373
    openWake, saveWake, disableWake, connectAll,
    // r382
    openWelcome, toggleWelcome, openReader, changeReaderVoice, openDetail, toggleDetail, toggleTimer,
    // r385
    toggleCharonFirst,
    // r387
    openFiles, fileUp,
    // r396d
    startFirmwareMenu,
    // r400
    openWorkspace,
    unhandled /* internal: page-specific handler defers to generic navigation */ };
// Main rows. Continue stays last.
enum MainRow : size_t { mainDevices, mainOrder, mainSettings, mainFirmware, mainShell, mainWorkspace /* r400 */, mainTools, mainContinue, mainCount };
// System tools rows (r370).
enum ToolsRow : size_t { toolInfo, toolClock, toolSetClock, toolWake, toolVariables, toolDrivers, toolUpdates, toolConnect, toolFiles /* r387 */, toolRestart, toolShutdown, toolFirmware, toolFirmwareMenu /* r396d */, toolsCount };
// Settings rows 0..11 keep their r354 positions; r355 rows follow.
enum SettingsRow : size_t { setWrap, setRefresh, setSecure, setPendingBoot, setPendingSettings, setPendingKeys, setContinueTarget,
    setLanguage, setSounds, setStartMelody, setBackgroundMelody, setRate,
    setPitch /* r382 */, setSpeechVolume, setEffectsVolume, setVerbosity, setRemember, setAutoDelay, setBootNextRow, setProgress, setFirmwareVoice, setWelcome, setReaderVoice, setDetail,
    setSynth, setSynthVoice /* r396: the same synthesizer choice as the reader page */, settingsCount };
// Screen reader voice rows (r382).
enum ReaderRow : size_t { readerRateRow, readerPitchRow, readerVolumeRow, readerTimerSpeechRow, readerTimerSoundRow,
    readerMessageRateRow, readerMessagePitchRow /* r389 boot-message voice */,
    // r390: text reading (cycles with Left/Right), capitals and echo (check boxes).
    readerVerbosityRow, readerPunctuationRow, readerLanguageRow, readerCapSayRow, readerCapBeepRow, readerCapPitchRow,
    readerMessagesRow, readerTypedRow, readerWordsRow, readerCursorRow, readerAutoReadRow, readerHintsRow,
    // r393: boot managers (hidden menus: combo; countdowns: check box) and the text formatting submenu.
    readerHiddenMenuRow, readerStopCountdownRow, readerLingerRow /* r400 */, readerFormattingRow,
    readerSynthRow, readerSynthVoiceRow /* r393 synthesizer add-ons */,
    readerProfileRow /* r410 verbosity profile: normal, beginner, expert */, readerCount };
// r393 "Formatowanie tekstu" rows: check boxes (formatFlags bits 0-5, then readerExtra braille, dictionary).
enum FormattingRow : size_t { formatVoiceRow, formatIconsRow, formatColoursRow, formatBrightRow, formatFontRow, formatInverseRow,
                              formatEventsRow, formatFamiliesRow, formatStereoRow,   // r397 (user): Emacspeak event icons, voice families, stereo
    formatBrailleRow, formatDictionaryRow,
    formatKindRow, formatStateRow, formatPositionRow,   // r410 (user): what the screen model drives: kind of element, state and warnings, position and structure
    formatAbsoluteRow, formatBarRow,                    // r410 V14: the r399 absolute colour ladder; the reader bar on the bottom line
    formatItemRow, formatItemLastRow=formatItemRow+11,   // r410 V14: the custom verbosity profile, one check box per item
    formattingCount };
// r390: the check-box rows and their bits (capitals bits, then CharSettings readerFlag*).
inline bool ReaderCheckRow(size_t row) {return row>=readerCapSayRow && row<=readerHintsRow;}
constexpr size_t maximumOrder=256;
constexpr uint16_t maximumFirmwareTimeout=30;
struct MenuModel {
    MenuPage page=MenuPage::main;
    size_t index=0,devices=0;
    bool wrap=false,sounds=true;
    uint8_t language=0,startMelody=1,backgroundMelody=0,rate=10;
    uint8_t speechVolume=10,effectsVolume=10,verbosity=1,autoSeconds=10,progress=1;bool rememberLast=false;
    uint8_t welcome=7;   // r382: bit0 hint, bit1 sound, bit2 spoken greeting (welcome page rows 0..2)
    uint8_t pitch=10,readerRate=10,readerPitch=10,readerVolume=10;  // r382 voices (tenths)
    uint8_t messageRate=14,messagePitch=10;  // r389 boot-message voice (tenths)
    uint8_t punctuation=1,capitals=1,screenLanguage=0,readerFlags=47;  // r390 reader text settings
    uint8_t formatFlags=252,readerExtra=153;   // r410: formatting off until the user turns it on
    uint16_t profileItems=1083;bool readerBar=false,absoluteColours=false;   // r410 V14
    uint8_t formatGroups=7,profile=0;          // r410 formatting groups of the screen model, verbosity profile
    // r393: synthesizer add-ons found on the disk (keys, voice counts); 0 = TVox.
    uint8_t synthKey=0,synthVoice=0;uint8_t synthKeys[8]={};uint8_t synthVoiceCounts[8]={};size_t synthCount=0;
    uint16_t synthVoiceMask[8]={0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff};   // r410: voices whose data is installed
    // The next (or previous) usable voice of the chosen synthesizer; false at a synthesizer with one voice.
    bool StepSynthVoice(bool forward) {
        const size_t slot=SynthSlot();if(slot==SIZE_MAX)return false;
        const uint8_t n=synthVoiceCounts[slot];if(n<2)return false;
        uint8_t v=synthVoice<n?synthVoice:0;
        for(uint8_t tries=0;tries<n;++tries) {v=uint8_t(forward?(v+1)%n:(v+n-1)%n);if((synthVoiceMask[slot]>>v)&1)break;}
        if(v==synthVoice || !((synthVoiceMask[slot]>>v)&1))return false;
        synthVoice=v;return true;
    }
    size_t SynthSlot() const {for(size_t i=0;i<synthCount;++i)if(synthKeys[i]==synthKey)return i;return SIZE_MAX;}
    uint8_t detail=7;    // r382: bit0 disk name, bit1 boot manager name, bit2 list positions
    uint8_t timers=0;    // r384: bit0 countdown speech every 10 s, bit1 countdown ticks
    // Clock editor fields (r372): year, month, day, hour, minute; row 5 saves.
    bool clockWake=false;   // the clock page edits the wake-up alarm (r373)
    uint16_t clockYear=2026;uint8_t clockMonth=1,clockDay=1,clockHour=0,clockMinute=0;
    static uint8_t DaysIn(uint16_t year,uint8_t month) {
        static const uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
        if(month==2 && ((year%4==0 && year%100!=0) || year%400==0))return 29;
        return month>=1 && month<=12?days[month-1]:31;
    }
    MenuAction ClockKey(MenuEvent event) {
        if(index>=5 || (event!=MenuEvent::left && event!=MenuEvent::right))return MenuAction::unhandled;
        const bool up=event==MenuEvent::right;
        auto step=[&](auto& value,int low,int high) {
            const int next=int(value)+(up?1:-1);
            if(next<low || next>high)return false;
            value=decltype(+value)(next);return true;
        };
        bool moved=false;
        if(index==0) {int y=clockYear;moved=step(y,2000,2099);clockYear=uint16_t(y);}
        else if(index==1) {int m=clockMonth;moved=step(m,1,12);clockMonth=uint8_t(m);}
        else if(index==2) {int d=clockDay;moved=step(d,1,DaysIn(clockYear,clockMonth));clockDay=uint8_t(d);}
        else if(index==3) {int h=clockHour;moved=step(h,0,23);clockHour=uint8_t(h);}
        else {int n=clockMinute;moved=step(n,0,59);clockMinute=uint8_t(n);}
        if(clockDay>DaysIn(clockYear,clockMonth))clockDay=DaysIn(clockYear,clockMonth);
        return moved?MenuAction::changeClock:MenuAction::clockEdge;
    }
    size_t listCount=0,listReturn=0;   // infoList rows (variables or drivers) and its tools row
    bool fileList=false;size_t fileDepth=0;   // r387: the infoList is the file browser (depth 0 = volumes)
    MenuPage noticeReturn=MenuPage::main;
    // r390: the reader's key list (F1, Insert+F1, Alt+F1) returns where it was opened.
    size_t keyListCount=0;MenuPage keyListReturn=MenuPage::main;size_t keyListIndex=0;
    void OpenKeyList(size_t count) {
        if(page==MenuPage::keyList || page==MenuPage::notice || page==MenuPage::confirm)return;
        keyListReturn=page;keyListIndex=index;keyListCount=count;page=MenuPage::keyList;index=0;
    }
    size_t noticeIndex=0;
    MenuPage confirmReturn=MenuPage::settings;
    size_t confirmIndex=0;
    MenuAction pending=MenuAction::none;
    // Boot order page: movable firmware entries (Charon itself excluded and
    // placed by the writer). Rows: entries, Save, Revert, firmware Timeout,
    // and (r385) the "Charon menu at start" check box: unchecked, Charon's
    // entry goes last and firmware starts the first other entry directly;
    // only the resident CHARONVOICE.EFI (Driver####) then reads the screen.
    uint16_t order[maximumOrder]={};
    size_t orderCount=0,grabOrigin=0;
    bool grabbed=false,orderDirty=false,timeoutKnown=false;
    bool charonFirst=true;
    uint16_t firmwareTimeout=0;
    size_t OrderSaveRow() const {return orderCount;}
    size_t OrderRevertRow() const {return orderCount+1;}
    size_t OrderTimeoutRow() const {return orderCount+2;}
    size_t OrderCharonRow() const {return orderCount+3;}
    size_t Count() const {
        switch(page) {
            case MenuPage::main:return mainCount;
            case MenuPage::devices:return devices;
            case MenuPage::settings:return settingsCount;
            case MenuPage::language:case MenuPage::verbosity:return 3;
            case MenuPage::notice:return 1;
            case MenuPage::startMelody:case MenuPage::backgroundMelody:return 5;
            case MenuPage::bootOrder:return orderCount+4;
            case MenuPage::autoDelay:return 9;
            case MenuPage::progress:return 4;
            case MenuPage::welcome:case MenuPage::detail:return 3;
            case MenuPage::reader:return readerCount;
            case MenuPage::formatting:return formattingCount;
            case MenuPage::keyList:return keyListCount;
            case MenuPage::tools:return toolsCount;
            case MenuPage::infoList:return listCount;
            case MenuPage::clockEdit:return clockWake?7:6;
            default:return 2;
        }
    }
    void Confirm(MenuAction action) {
        if(page!=MenuPage::confirm) {confirmReturn=page;confirmIndex=index;}
        pending=action;page=MenuPage::confirm;index=0;
    }
    void LeaveConfirm() {page=confirmReturn;index=confirmIndex;pending=MenuAction::none;}
    void Notice() {
        if(page!=MenuPage::notice) {noticeReturn=page;noticeIndex=index;}
        page=MenuPage::notice;index=0;
    }
    // Replace the movable list (on page open, revert or after a save).
    bool LoadOrder(const uint16_t* ids,size_t count) {
        if(count>maximumOrder || (count && !ids))return false;
        for(size_t i=0;i<count;++i)order[i]=ids[i];
        orderCount=count;grabbed=false;orderDirty=false;
        if(page==MenuPage::bootOrder && index>=Count())index=Count()-1;
        return true;
    }
    static bool Slider(uint8_t& value,uint8_t low,uint8_t high,MenuEvent event) {
        const uint8_t next=event==MenuEvent::left?(value>low?value-1:value):(value<high?value+1:value);
        if(next==value)return false;
        value=next;return true;
    }
    MenuAction OrderKey(MenuEvent event) {
        if(grabbed) {
            if(event==MenuEvent::up || event==MenuEvent::down || event==MenuEvent::home || event==MenuEvent::end) {
                size_t target=index;
                if(event==MenuEvent::up && index)target=index-1;
                else if(event==MenuEvent::down && index+1<orderCount)target=index+1;
                else if(event==MenuEvent::home)target=0;
                else if(event==MenuEvent::end)target=orderCount-1;
                if(target==index)return MenuAction::edge;
                const uint16_t moving=order[index];
                if(target<index)for(size_t i=index;i>target;--i)order[i]=order[i-1];
                else for(size_t i=index;i<target;++i)order[i]=order[i+1];
                order[target]=moving;index=target;orderDirty=true;return MenuAction::moveEntry;
            }
            if(event==MenuEvent::enter || event==MenuEvent::space) {grabbed=false;return MenuAction::dropEntry;}
            if(event==MenuEvent::back || event==MenuEvent::left) {
                // Return the entry to where it was grabbed; other moves stay.
                const uint16_t moving=order[index];
                if(grabOrigin<index)for(size_t i=index;i>grabOrigin;--i)order[i]=order[i-1];
                else for(size_t i=index;i<grabOrigin;++i)order[i]=order[i+1];
                order[grabOrigin]=moving;index=grabOrigin;grabbed=false;return MenuAction::grabCancelled;
            }
            return MenuAction::none;
        }
        if(index==OrderTimeoutRow() && (event==MenuEvent::left || event==MenuEvent::right)) {
            if(!timeoutKnown)return MenuAction::timeoutEdge;
            const uint16_t next=event==MenuEvent::left?(firmwareTimeout?firmwareTimeout-1:0)
                :(firmwareTimeout<maximumFirmwareTimeout?firmwareTimeout+1:firmwareTimeout);
            if(next==firmwareTimeout)return MenuAction::timeoutEdge;
            firmwareTimeout=next;return MenuAction::changeTimeout;
        }
        if(event==MenuEvent::enter || event==MenuEvent::space) {
            if(index<orderCount) {grabbed=true;grabOrigin=index;return MenuAction::grabEntry;}
            if(index==OrderSaveRow())return event==MenuEvent::enter?MenuAction::saveOrder:MenuAction::none;
            if(index==OrderRevertRow())return MenuAction::revertOrder;
            if(index==OrderCharonRow()) {charonFirst=!charonFirst;orderDirty=true;return MenuAction::toggleCharonFirst;}
            return MenuAction::none;
        }
        if(event==MenuEvent::right)return MenuAction::none;
        return MenuAction::unhandled;
    }
    MenuAction Key(MenuEvent event) {
        if(page==MenuPage::bootOrder) {
            const auto handled=OrderKey(event);
            if(handled!=MenuAction::unhandled)return handled;
        }
        if(page==MenuPage::clockEdit) {
            const auto handled=ClockKey(event);
            if(handled!=MenuAction::unhandled)return handled;
            if(index==5 && event==MenuEvent::enter)return clockWake?MenuAction::saveWake:MenuAction::saveClock;
            if(index==6 && clockWake && event==MenuEvent::enter)return MenuAction::disableWake;
            if(event==MenuEvent::enter || event==MenuEvent::space || event==MenuEvent::right)return MenuAction::none;
        }
        const size_t count=Count();
        // Sliders: Left/Right change them in single steps.
        if(page==MenuPage::settings && (event==MenuEvent::left || event==MenuEvent::right)) {
            if(index==setRate) {
                if(!Slider(rate,5,20,event))return MenuAction::edge;
                return MenuAction::changeRate;
            }
            if(index==setPitch) {
                if(!Slider(pitch,5,20,event))return MenuAction::edge;
                return MenuAction::changeRate;
            }
            if(index==setSpeechVolume || index==setEffectsVolume) {
                if(!Slider(index==setSpeechVolume?speechVolume:effectsVolume,index==setSpeechVolume?1:0,10,event))return MenuAction::edge;
                return MenuAction::changeVolume;
            }
        }
        // r382: screen reader voice sliders.
        if(page==MenuPage::reader && (event==MenuEvent::left || event==MenuEvent::right) && (index==readerMessageRateRow || index==readerMessagePitchRow)) {
            const bool moved=Slider(index==readerMessageRateRow?messageRate:messagePitch,5,20,event);
            return moved?MenuAction::changeReaderVoice:MenuAction::edge;
        }
        // r390: choices that cycle (no edge): verbosity, symbol level, screen language.
        if(page==MenuPage::reader && (event==MenuEvent::left || event==MenuEvent::right)
            && (index==readerVerbosityRow || index==readerPunctuationRow || index==readerLanguageRow || index==readerProfileRow)) {
            uint8_t& value=index==readerVerbosityRow?verbosity:index==readerPunctuationRow?punctuation:index==readerProfileRow?profile:screenLanguage;
            const uint8_t states=index==readerVerbosityRow?3:4;   // r410 V14: the profile has four states (custom)
            value=uint8_t(event==MenuEvent::right?(value+1)%states:(value+states-1)%states);
            return MenuAction::changeReaderVoice;
        }
        if(page==MenuPage::reader && (event==MenuEvent::left || event==MenuEvent::right) && (ReaderCheckRow(index) || index==readerTimerSpeechRow || index==readerTimerSoundRow))return MenuAction::none;
        // r393: hidden boot-manager menus cycle with Left/Right; Right opens the formatting submenu.
        if(page==MenuPage::reader && (event==MenuEvent::left || event==MenuEvent::right) && index==readerHiddenMenuRow) {
            uint8_t mode=uint8_t(readerExtra&3);mode=event==MenuEvent::right?uint8_t((mode+1)%3):uint8_t((mode+2)%3);
            readerExtra=uint8_t((readerExtra&~3)|mode);return MenuAction::toggleTimer;}
        if(page==MenuPage::reader && event==MenuEvent::left && (index==readerStopCountdownRow || index==readerLingerRow || index==readerFormattingRow))return MenuAction::none;
        if(page==MenuPage::reader && event==MenuEvent::right && (index==readerStopCountdownRow || index==readerLingerRow))return MenuAction::none;
        if(page==MenuPage::reader && event==MenuEvent::right && index==readerFormattingRow)event=MenuEvent::enter;
        if(page==MenuPage::formatting && (event==MenuEvent::left || event==MenuEvent::right))return MenuAction::none;
        // r393: Left/Right choose the synthesizer (TVox, then the add-ons) and its voice.
        // r396: also on Charon's settings page (one choice for menu and reader).
        if(page==MenuPage::settings && (event==MenuEvent::left || event==MenuEvent::right || event==MenuEvent::enter || event==MenuEvent::space) && (index==setSynth || index==setSynthVoice)) {
            if(event==MenuEvent::enter || event==MenuEvent::space)return MenuAction::none;
            if(index==setSynth) {
                const size_t states=synthCount+1;size_t at=synthKey?SynthSlot()+1:0;if(at>states-1)at=0;
                at=event==MenuEvent::right?(at+1)%states:(at+states-1)%states;
                const uint8_t key=at?synthKeys[at-1]:0;if(key==synthKey)return MenuAction::edge;
                synthKey=key;synthVoice=0;return MenuAction::changeReaderVoice;}
            if(!StepSynthVoice(event==MenuEvent::right))return MenuAction::edge;   // r410: only voices whose data is installed
            return MenuAction::changeReaderVoice;}
        if(page==MenuPage::reader && (event==MenuEvent::left || event==MenuEvent::right) && index==readerSynthRow) {
            const size_t states=synthCount+1;size_t at=synthKey?SynthSlot()+1:0;if(at>states-1)at=0;
            at=event==MenuEvent::right?(at+1)%states:(at+states-1)%states;
            const uint8_t key=at?synthKeys[at-1]:0;if(key==synthKey)return MenuAction::edge;
            synthKey=key;synthVoice=0;return MenuAction::changeReaderVoice;}
        if(page==MenuPage::reader && (event==MenuEvent::left || event==MenuEvent::right) && index==readerSynthVoiceRow) {
            if(!StepSynthVoice(event==MenuEvent::right))return MenuAction::edge;   // r410: only voices whose data is installed
            return MenuAction::changeReaderVoice;}
        if(page==MenuPage::reader && (event==MenuEvent::left || event==MenuEvent::right) && index<readerTimerSpeechRow) {
            const bool moved=index==readerVolumeRow?Slider(readerVolume,1,10,event):Slider(index==readerRateRow?readerRate:readerPitch,5,20,event);
            return moved?MenuAction::changeReaderVoice:MenuAction::edge;
        }
        if(event==MenuEvent::left)event=MenuEvent::back;
        // Dismiss a message only. Never forward its Enter/Space/Escape to the
        // underlying boot action, popup choice or confirmation.
        if(page==MenuPage::notice && (event==MenuEvent::back || event==MenuEvent::enter || event==MenuEvent::space)) {
            page=noticeReturn;index=noticeIndex;return MenuAction::dismissNotice;
        }
        if(event==MenuEvent::back && page==MenuPage::keyList) {page=keyListReturn;index=keyListIndex;return MenuAction::cancelled;}
        if(page==MenuPage::keyList && (event==MenuEvent::enter || event==MenuEvent::space))return MenuAction::none;
        if(event==MenuEvent::back) {
            if(page==MenuPage::infoList && fileList && fileDepth)return MenuAction::fileUp;
            if(page==MenuPage::main)return MenuAction::continueBoot;
            if(page==MenuPage::confirm) {LeaveConfirm();return MenuAction::cancelled;}
            if(page==MenuPage::bootOrder) {
                const bool dirty=orderDirty;page=MenuPage::main;index=mainOrder;grabbed=false;orderDirty=false;
                return dirty?MenuAction::leaveOrder:MenuAction::cancelled;
            }
            if(page==MenuPage::language) {page=MenuPage::settings;index=setLanguage;}
            else if(page==MenuPage::verbosity) {page=MenuPage::settings;index=setVerbosity;}
            else if(page==MenuPage::autoDelay) {page=MenuPage::settings;index=setAutoDelay;}
            else if(page==MenuPage::progress) {page=MenuPage::settings;index=setProgress;}
            else if(page==MenuPage::welcome) {page=MenuPage::settings;index=setWelcome;}
            else if(page==MenuPage::reader) {page=MenuPage::settings;index=setReaderVoice;}
            else if(page==MenuPage::formatting) {page=MenuPage::reader;index=readerFormattingRow;}
            else if(page==MenuPage::detail) {page=MenuPage::settings;index=setDetail;}
            else if(page==MenuPage::tools) {page=MenuPage::main;index=mainTools;}
            else if(page==MenuPage::infoList) {page=MenuPage::tools;index=listReturn;fileList=false;}
            else if(page==MenuPage::clockEdit) {page=MenuPage::tools;index=clockWake?toolWake:toolSetClock;}
            else if(page==MenuPage::startMelody || page==MenuPage::backgroundMelody) {index=page==MenuPage::startMelody?setStartMelody:setBackgroundMelody;page=MenuPage::settings;}
            else {index=page==MenuPage::settings ? mainSettings : mainDevices;page=MenuPage::main;}
            pending=MenuAction::none;return MenuAction::cancelled;
        }
        if(event==MenuEvent::up || event==MenuEvent::down || event==MenuEvent::home || event==MenuEvent::end
            || event==MenuEvent::pageUp || event==MenuEvent::pageDown) {
            if(!count)return MenuAction::edge;
            const size_t before=index;
            if(event==MenuEvent::pageUp)index=index>10?index-10:0;
            else if(event==MenuEvent::pageDown)index=index+10<count?index+10:count-1;
            else if(event==MenuEvent::home)index=0;
            else if(event==MenuEvent::end)index=count-1;
            else if(event==MenuEvent::up) {if(index)--index;else if(wrap)index=count-1;}
            else {if(index+1<count)++index;else if(wrap)index=0;}
            return before==index ? MenuAction::edge : MenuAction::changed;
        }
        if(page==MenuPage::main && (event==MenuEvent::enter || event==MenuEvent::right)) {
            if(index==mainDevices) {page=MenuPage::devices;index=0;return MenuAction::openDevices;}
            if(index==mainOrder) {page=MenuPage::bootOrder;index=0;grabbed=false;orderDirty=false;return MenuAction::openBootOrder;}
            if(index==mainSettings) {page=MenuPage::settings;index=0;return MenuAction::openSettings;}
            if(index==mainFirmware)return MenuAction::openFirmware;
            if(index==mainShell)return event==MenuEvent::enter?MenuAction::openShell:MenuAction::none;
            if(index==mainWorkspace)return event==MenuEvent::enter?MenuAction::openWorkspace:MenuAction::none;
            if(index==mainTools) {page=MenuPage::tools;index=0;return MenuAction::openTools;}
            return event==MenuEvent::enter ? MenuAction::continueBoot : MenuAction::none;
        }
        if(page==MenuPage::settings && event==MenuEvent::right && (index==setReaderVoice || index==setDetail))event=MenuEvent::enter;
        if(page==MenuPage::settings && (event==MenuEvent::enter || event==MenuEvent::space)) {
            switch(index) {
                case setWrap:wrap=!wrap;return MenuAction::toggleWrap;
                case setLanguage:page=MenuPage::language;index=language;return MenuAction::openLanguage;
                case setSounds:sounds=!sounds;return MenuAction::toggleSounds;
                case setStartMelody:case setBackgroundMelody: {const bool start=index==setStartMelody;page=start?MenuPage::startMelody:MenuPage::backgroundMelody;
                    index=start?startMelody:backgroundMelody;return MenuAction::openMelody;}
                case setVerbosity:page=MenuPage::verbosity;index=verbosity;return MenuAction::openVerbosity;
                case setRemember:rememberLast=!rememberLast;return MenuAction::toggleRemember;
                case setAutoDelay:page=MenuPage::autoDelay;index=AutoIndex();return MenuAction::openAutoDelay;
                case setProgress:page=MenuPage::progress;index=progress;return MenuAction::openProgress;
                case setWelcome:page=MenuPage::welcome;index=0;return MenuAction::openWelcome;
                case setReaderVoice:if(event!=MenuEvent::enter)return MenuAction::none;page=MenuPage::reader;index=0;return MenuAction::openReader;
                case setDetail:if(event!=MenuEvent::enter)return MenuAction::none;page=MenuPage::detail;index=0;return MenuAction::openDetail;
                case setPitch:return MenuAction::none;
                case setRate:case setSpeechVolume:case setEffectsVolume:return MenuAction::none;
                default:break;
            }
            if(event==MenuEvent::space)return MenuAction::none;
            const MenuAction actions[]={MenuAction::toggleWrap,MenuAction::refresh,
                MenuAction::firmwareInfo,MenuAction::pendingBoot,MenuAction::pendingSettings,MenuAction::pendingKeys,MenuAction::setContinue};
            if(index==setBootNextRow)return MenuAction::setBootNext;
            if(index==setFirmwareVoice)return MenuAction::toggleFirmwareVoice;
            return index<7?actions[index]:MenuAction::none;
        }
        if(page==MenuPage::devices && event==MenuEvent::enter && count)return MenuAction::startDevice;
        if(page==MenuPage::infoList && (event==MenuEvent::enter || event==MenuEvent::space || event==MenuEvent::right))
            return count?MenuAction::listDetail:MenuAction::none;
        // Tools: information rows answer Enter/Space; power rows need Enter
        // and then a confirmation (Cancel first) in BootMenu.
        if(page==MenuPage::tools && (event==MenuEvent::enter || event==MenuEvent::space || event==MenuEvent::right)) {
            if(index==toolInfo)return MenuAction::systemInfo;
            if(index==toolClock)return MenuAction::clockInfo;
            if(index==toolSetClock) {if(event!=MenuEvent::enter && event!=MenuEvent::right)return MenuAction::none;clockWake=false;page=MenuPage::clockEdit;index=0;return MenuAction::openClock;}
            if(index==toolWake) {if(event!=MenuEvent::enter && event!=MenuEvent::right)return MenuAction::none;clockWake=true;page=MenuPage::clockEdit;index=0;return MenuAction::openWake;}
            if(index==toolConnect)return event==MenuEvent::enter?MenuAction::connectAll:MenuAction::none;
            if(index==toolFiles) {listReturn=index;page=MenuPage::infoList;index=0;listCount=0;fileList=true;fileDepth=0;return MenuAction::openFiles;}
            if(index==toolVariables) {listReturn=index;page=MenuPage::infoList;index=0;listCount=0;fileList=false;return MenuAction::openVariables;}
            if(index==toolDrivers) {listReturn=index;page=MenuPage::infoList;index=0;listCount=0;fileList=false;return MenuAction::openDrivers;}
            if(index==toolUpdates)return MenuAction::updatesInfo;
            if(index==toolFirmwareMenu)return event==MenuEvent::enter?MenuAction::startFirmwareMenu:MenuAction::none;
            if(event!=MenuEvent::enter)return MenuAction::none;
            return index==toolRestart?MenuAction::restartSystem:index==toolShutdown?MenuAction::shutdownSystem:MenuAction::restartFirmware;
        }
        if(page==MenuPage::language && (event==MenuEvent::enter || event==MenuEvent::space)) {
            language=uint8_t(index);page=MenuPage::settings;index=setLanguage;return MenuAction::selectLanguage;
        }
        if(page==MenuPage::verbosity && (event==MenuEvent::enter || event==MenuEvent::space)) {
            verbosity=uint8_t(index);page=MenuPage::settings;index=setVerbosity;return MenuAction::selectVerbosity;
        }
        // r382: welcome check boxes; Space or Enter toggles, the list stays open.
        if(page==MenuPage::welcome && (event==MenuEvent::enter || event==MenuEvent::space) && index<3) {
            welcome=uint8_t(welcome^(1u<<index));return MenuAction::toggleWelcome;
        }
        if(page==MenuPage::detail && (event==MenuEvent::enter || event==MenuEvent::space) && index<3) {
            detail=uint8_t(detail^(1u<<index));return MenuAction::toggleDetail;
        }
        if(page==MenuPage::reader && (event==MenuEvent::enter || event==MenuEvent::space) && ReaderCheckRow(index)) {
            if(index<=readerCapPitchRow)capitals=uint8_t(capitals^(1u<<(index-readerCapSayRow)));
            else {static const uint8_t bits[]={1,2,16,8,4,32};readerFlags=uint8_t(readerFlags^bits[index-readerMessagesRow]);}
            return MenuAction::toggleTimer;
        }
        if(page==MenuPage::reader && event==MenuEvent::enter && index==readerFormattingRow) {page=MenuPage::formatting;index=0;return MenuAction::openDetail;}
        if(page==MenuPage::reader && (event==MenuEvent::enter || event==MenuEvent::space) && index==readerStopCountdownRow) {readerExtra=uint8_t(readerExtra^4);return MenuAction::toggleTimer;}
        if(page==MenuPage::reader && (event==MenuEvent::enter || event==MenuEvent::space) && index==readerLingerRow) {readerExtra=uint8_t(readerExtra^128);return MenuAction::toggleTimer;}
        if(page==MenuPage::reader && (event==MenuEvent::enter || event==MenuEvent::space) && (index==readerHiddenMenuRow || index==readerFormattingRow || index==readerSynthRow || index==readerSynthVoiceRow))return MenuAction::none;
        if(page==MenuPage::formatting && (event==MenuEvent::enter || event==MenuEvent::space) && index<formattingCount) {
            if(index<=formatFamiliesRow)formatFlags=uint8_t(formatFlags^(1u<<index));
            else if(index>=formatItemRow)profileItems=uint16_t(profileItems^(1u<<(index-formatItemRow)));   // r410 V14
            else if(index==formatAbsoluteRow)absoluteColours=!absoluteColours;
            else if(index==formatBarRow)readerBar=!readerBar;
            else if(index>=formatKindRow)formatGroups=uint8_t(formatGroups^(1u<<(index-formatKindRow)));   // r410
            else if(index==formatStereoRow) {const unsigned s=(((readerExtra>>5)&3)+1)%3;readerExtra=uint8_t((readerExtra&~0x60)|(s<<5));}
            else readerExtra=uint8_t(readerExtra^(index==formatBrailleRow?8:16));
            return MenuAction::toggleDetail;}
        if(page==MenuPage::reader && (event==MenuEvent::enter || event==MenuEvent::space)) {
            if(index!=readerTimerSpeechRow && index!=readerTimerSoundRow)return MenuAction::none;
            timers=uint8_t(timers^(index==readerTimerSpeechRow?1:2));return MenuAction::toggleTimer;
        }
        if(page==MenuPage::progress && (event==MenuEvent::enter || event==MenuEvent::space)) {
            progress=uint8_t(index);page=MenuPage::settings;index=setProgress;return MenuAction::selectProgress;
        }
        if(page==MenuPage::autoDelay && (event==MenuEvent::enter || event==MenuEvent::space)) {
            autoSeconds=AutoValue(index);page=MenuPage::settings;index=setAutoDelay;return MenuAction::selectAutoDelay;
        }
        if((page==MenuPage::startMelody || page==MenuPage::backgroundMelody) && (event==MenuEvent::enter || event==MenuEvent::space)) {
            const bool start=page==MenuPage::startMelody;
            if(start)startMelody=uint8_t(index);else backgroundMelody=uint8_t(index);
            page=MenuPage::settings;index=start?setStartMelody:setBackgroundMelody;return MenuAction::selectMelody;
        }
        if(page==MenuPage::confirm && event==MenuEvent::enter)
            return index==1 ? MenuAction::confirmed : MenuAction::cancelled;
        return MenuAction::none;
    }
    // Same list as CharSettings autoDelays; kept local so the model has no
    // dependency on the persistence header.
    static uint8_t AutoValue(size_t row) {const uint8_t values[]={0,3,5,10,15,20,30,60,255};return row<9?values[row]:10;}
    size_t AutoIndex() const {for(size_t i=0;i<9;++i)if(AutoValue(i)==autoSeconds)return i;return 3;}
};
}
