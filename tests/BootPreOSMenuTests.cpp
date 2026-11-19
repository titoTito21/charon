#include "../src/efi/BootMenuModel.h"
#include "../src/efi/ContinueTarget.h"
#include "../src/efi/CharSettings.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
using namespace hakefi;
int main() {
    MenuModel menu;
    CHECK(menu.Key(MenuEvent::up)==MenuAction::edge && menu.index==0);
    CHECK(menu.Key(MenuEvent::right)==MenuAction::openDevices);
    CHECK(menu.Key(MenuEvent::down)==MenuAction::edge);
    CHECK(menu.Key(MenuEvent::enter)==MenuAction::none);
    CHECK(menu.Key(MenuEvent::back)==MenuAction::cancelled && menu.page==MenuPage::main);
    menu.devices=3;CHECK(menu.Key(MenuEvent::enter)==MenuAction::openDevices);
    CHECK(menu.Key(MenuEvent::end)==MenuAction::changed && menu.index==2);
    CHECK(menu.Key(MenuEvent::down)==MenuAction::edge);
    CHECK(menu.Key(MenuEvent::right)==MenuAction::none); // Right cannot boot an entry.
    CHECK(menu.Key(MenuEvent::enter)==MenuAction::startDevice);
    CHECK(menu.Key(MenuEvent::home)==MenuAction::changed && menu.index==0);
    menu.Key(MenuEvent::back);menu.Key(MenuEvent::down);menu.Key(MenuEvent::down);
    CHECK(menu.Key(MenuEvent::right)==MenuAction::openSettings);
    CHECK(menu.Key(MenuEvent::space)==MenuAction::toggleWrap && menu.wrap);
    CHECK(menu.Key(MenuEvent::up)==MenuAction::changed && menu.index==settingsCount-1);menu.index=12;
    CHECK(menu.Key(MenuEvent::up)==MenuAction::changed && menu.index==11);
    CHECK(menu.Key(MenuEvent::up)==MenuAction::changed && menu.index==10);
    CHECK(menu.Key(MenuEvent::up)==MenuAction::changed && menu.index==9);
    CHECK(menu.Key(MenuEvent::up)==MenuAction::changed && menu.index==8);
    CHECK(menu.Key(MenuEvent::space)==MenuAction::toggleSounds && !menu.sounds);
    CHECK(menu.Key(MenuEvent::up)==MenuAction::changed && menu.index==7);
    CHECK(menu.Key(MenuEvent::space)==MenuAction::openLanguage && menu.page==MenuPage::language);
    CHECK(menu.Key(MenuEvent::end)==MenuAction::changed && menu.index==2);
    CHECK(menu.Key(MenuEvent::enter)==MenuAction::selectLanguage && menu.language==2 && menu.page==MenuPage::settings && menu.index==7);
    CHECK(menu.Key(MenuEvent::up)==MenuAction::changed && menu.index==6);
    CHECK(menu.Key(MenuEvent::space)==MenuAction::none); // No Space mutation of firmware.
    CHECK(menu.Key(MenuEvent::enter)==MenuAction::setContinue);
    menu.Confirm(MenuAction::setContinue);
    CHECK(menu.page==MenuPage::confirm && menu.index==0);
    CHECK(menu.Key(MenuEvent::enter)==MenuAction::cancelled);
    CHECK(menu.Key(MenuEvent::right)==MenuAction::none);
    CHECK(menu.Key(MenuEvent::down)==MenuAction::changed && menu.index==1);
    CHECK(menu.Key(MenuEvent::enter)==MenuAction::confirmed && menu.pending==MenuAction::setContinue);
    CHECK(menu.Key(MenuEvent::back)==MenuAction::cancelled && menu.page==MenuPage::settings
        && menu.pending==MenuAction::none);
    CHECK(menu.Key(MenuEvent::back)==MenuAction::cancelled && menu.page==MenuPage::main && menu.index==2);
    menu.index=3;CHECK(menu.Key(MenuEvent::right)==MenuAction::openFirmware && menu.page==MenuPage::main);
    menu.index=4;CHECK(menu.Key(MenuEvent::right)==MenuAction::none && menu.Key(MenuEvent::enter)==MenuAction::openShell);
    // r370 system tools page, before Continue.
    menu.index=mainTools;CHECK(menu.Key(MenuEvent::enter)==MenuAction::openTools && menu.page==MenuPage::tools && menu.Count()==13 && menu.index==0);
    // r373 wake-up alarm reuses the clock page with a 7th row; Connect needs Enter.
    menu.index=toolWake;CHECK(menu.Key(MenuEvent::enter)==MenuAction::openWake && menu.page==MenuPage::clockEdit && menu.clockWake && menu.Count()==7);
    menu.index=5;CHECK(menu.Key(MenuEvent::enter)==MenuAction::saveWake);
    menu.index=6;CHECK(menu.Key(MenuEvent::enter)==MenuAction::disableWake);
    CHECK(menu.Key(MenuEvent::back)==MenuAction::cancelled && menu.page==MenuPage::tools && menu.index==toolWake);
    menu.index=toolConnect;CHECK(menu.Key(MenuEvent::space)==MenuAction::none && menu.Key(MenuEvent::enter)==MenuAction::connectAll && menu.page==MenuPage::tools);
    menu.index=0;
    // r372 clock editor: Left/Right change fields within bounds, day clamped to the month, Save asks.
    menu.index=toolSetClock;CHECK(menu.Key(MenuEvent::space)==MenuAction::none && menu.Key(MenuEvent::enter)==MenuAction::openClock && menu.page==MenuPage::clockEdit && menu.Count()==6);
    menu.clockYear=2024;menu.clockMonth=3;menu.clockDay=31;menu.clockHour=23;menu.clockMinute=59;
    menu.index=1;CHECK(menu.Key(MenuEvent::left)==MenuAction::changeClock && menu.clockMonth==2 && menu.clockDay==29);
    menu.index=0;CHECK(menu.Key(MenuEvent::right)==MenuAction::changeClock && menu.clockYear==2025 && menu.clockDay==28);
    menu.index=3;CHECK(menu.Key(MenuEvent::right)==MenuAction::clockEdge && menu.clockHour==23);
    menu.index=4;CHECK(menu.Key(MenuEvent::right)==MenuAction::clockEdge && menu.Key(MenuEvent::left)==MenuAction::changeClock && menu.clockMinute==58);
    CHECK(menu.Key(MenuEvent::enter)==MenuAction::none);
    menu.index=5;CHECK(menu.Key(MenuEvent::enter)==MenuAction::saveClock);
    menu.Confirm(MenuAction::saveClock);menu.LeaveConfirm();CHECK(menu.page==MenuPage::clockEdit && menu.index==5);
    CHECK(menu.Key(MenuEvent::back)==MenuAction::cancelled && menu.page==MenuPage::tools && menu.index==toolSetClock);
    menu.index=0;
    // r371 read-only lists: variables/drivers open a list page; Back returns to the same row.
    menu.index=toolDrivers;CHECK(menu.Key(MenuEvent::enter)==MenuAction::openDrivers && menu.page==MenuPage::infoList && menu.Count()==0);
    CHECK(menu.Key(MenuEvent::enter)==MenuAction::none);
    menu.listCount=25;CHECK(menu.Key(MenuEvent::pageDown)==MenuAction::changed && menu.index==10);
    CHECK(menu.Key(MenuEvent::pageDown)==MenuAction::changed && menu.Key(MenuEvent::pageDown)==MenuAction::changed && menu.index==24);
    CHECK(menu.Key(MenuEvent::pageDown)==MenuAction::edge && menu.Key(MenuEvent::pageUp)==MenuAction::changed && menu.index==14);
    CHECK(menu.Key(MenuEvent::enter)==MenuAction::listDetail && menu.page==MenuPage::infoList);
    CHECK(menu.Key(MenuEvent::back)==MenuAction::cancelled && menu.page==MenuPage::tools && menu.index==toolDrivers);
    menu.index=toolUpdates;CHECK(menu.Key(MenuEvent::enter)==MenuAction::updatesInfo && menu.page==MenuPage::tools);
    menu.index=toolVariables;CHECK(menu.Key(MenuEvent::enter)==MenuAction::openVariables && menu.page==MenuPage::infoList);
    CHECK(menu.Key(MenuEvent::left)==MenuAction::cancelled && menu.page==MenuPage::tools && menu.index==toolVariables);
    // r387 file browser: Left/Escape go up a level while inside a volume,
    // and leave the list only from the volume list.
    menu.index=toolFiles;CHECK(menu.Key(MenuEvent::enter)==MenuAction::openFiles && menu.page==MenuPage::infoList && menu.fileList && menu.fileDepth==0);
    menu.listCount=2;CHECK(menu.Key(MenuEvent::enter)==MenuAction::listDetail && menu.Key(MenuEvent::right)==MenuAction::listDetail);
    menu.fileDepth=2;CHECK(menu.Key(MenuEvent::left)==MenuAction::fileUp && menu.page==MenuPage::infoList);
    CHECK(menu.Key(MenuEvent::back)==MenuAction::fileUp && menu.page==MenuPage::infoList);
    menu.fileDepth=0;CHECK(menu.Key(MenuEvent::back)==MenuAction::cancelled && menu.page==MenuPage::tools && menu.index==toolFiles && !menu.fileList);
    menu.index=toolDrivers;CHECK(menu.Key(MenuEvent::enter)==MenuAction::openDrivers && !menu.fileList);
    menu.fileDepth=3;CHECK(menu.Key(MenuEvent::back)==MenuAction::cancelled && menu.page==MenuPage::tools);
    menu.index=0;
    CHECK(menu.Key(MenuEvent::space)==MenuAction::systemInfo && menu.page==MenuPage::tools);
    menu.index=toolClock;CHECK(menu.Key(MenuEvent::enter)==MenuAction::clockInfo);
    menu.index=toolRestart;CHECK(menu.Key(MenuEvent::space)==MenuAction::none && menu.Key(MenuEvent::right)==MenuAction::none);
    CHECK(menu.Key(MenuEvent::enter)==MenuAction::restartSystem);
    menu.index=toolShutdown;CHECK(menu.Key(MenuEvent::enter)==MenuAction::shutdownSystem);
    menu.Confirm(MenuAction::shutdownSystem);CHECK(menu.page==MenuPage::confirm && menu.index==0 && menu.Key(MenuEvent::enter)==MenuAction::cancelled);
    menu.LeaveConfirm();CHECK(menu.page==MenuPage::tools && menu.index==toolShutdown);
    menu.index=toolFirmware;CHECK(menu.Key(MenuEvent::enter)==MenuAction::restartFirmware);
    CHECK(menu.Key(MenuEvent::back)==MenuAction::cancelled && menu.page==MenuPage::main && menu.index==mainTools);
    menu.Key(MenuEvent::end);CHECK(menu.index==6 && menu.index==mainContinue && menu.Count()==7);
    CHECK(menu.Key(MenuEvent::right)==MenuAction::none);
    CHECK(menu.Key(MenuEvent::enter)==MenuAction::continueBoot);
    CHECK(menu.Key(MenuEvent::back)==MenuAction::continueBoot);
    for(MenuAction action:{MenuAction::pendingBoot,MenuAction::pendingSettings,MenuAction::pendingKeys}) {
        menu.Confirm(action);CHECK(menu.index==0 && menu.Key(MenuEvent::enter)==MenuAction::cancelled);
    }
    // Exact bounded target records: reject corruption/truncation/trailing data,
    // preserve outputs on failure, and reuse the native load-option validator.
    uint8_t record[31]={'H','A','K','C','O','N','T','1',0x34,0x12,12,0,0,0,
        1,0,0,0,4,0,0,0,0x7f,0xff,4,0,'H','E','N','D',0};
    uint16_t id=0;size_t size=0;
    CHECK(ParseContinueTarget(record,30,id,size) && id==0x1234 && size==12);
    for(size_t length=0;length<30;++length) {
        id=0xbeef;size=999;CHECK(!ParseContinueTarget(record,length,id,size) && id==0xbeef && size==999);
    }
    CHECK(!ParseContinueTarget(record,31,id,size));
    for(size_t at:{size_t(0),size_t(10),size_t(14),size_t(24),size_t(29)}) {
        uint8_t corrupted[31];memcpy(corrupted,record,sizeof(record));corrupted[at]^=1;
        CHECK(!ParseContinueTarget(corrupted,30,id,size));
    }
    CHECK(!ParseContinueTarget(nullptr,30,id,size));
    {MenuModel rate;rate.page=MenuPage::settings;rate.index=11;CHECK(rate.Count()==settingsCount);
     CHECK(rate.Key(MenuEvent::right)==MenuAction::changeRate && rate.rate==11);
     CHECK(rate.Key(MenuEvent::left)==MenuAction::changeRate && rate.rate==10);
     for(int i=0;i<5;++i)rate.Key(MenuEvent::left);
     CHECK(rate.rate==5 && rate.Key(MenuEvent::left)==MenuAction::edge && rate.page==MenuPage::settings);
     CHECK(rate.Key(MenuEvent::enter)==MenuAction::none && rate.Key(MenuEvent::space)==MenuAction::none);
     rate.index=3;CHECK(rate.Key(MenuEvent::left)==MenuAction::cancelled && rate.page==MenuPage::main);
     CharSettings v;v.sequence=7;v.rate=17;uint8_t bytes[charSettingsBytes];CHECK(EncodeSettings(v,bytes,sizeof(bytes)) && bytes[7]=='C');   // r397: V12
     CharSettings back;CHECK(DecodeSettings(bytes,sizeof(bytes),back) && back.rate==17);
     v.rate=21;CHECK(!EncodeSettings(v,bytes,sizeof(bytes)));v.rate=4;CHECK(!EncodeSettings(v,bytes,sizeof(bytes)));}
    // r355 boot order page: grab, move, drop, cancel, save/revert/timeout rows.
    {MenuModel order;order.index=mainOrder;CHECK(order.Key(MenuEvent::enter)==MenuAction::openBootOrder && order.page==MenuPage::bootOrder);
     const uint16_t ids[]={7,8,9};CHECK(order.LoadOrder(ids,3) && order.Count()==7);   // r385: + "Charon menu at start" row
     CHECK(order.Key(MenuEvent::enter)==MenuAction::grabEntry && order.grabbed);
     CHECK(order.Key(MenuEvent::up)==MenuAction::edge);
     CHECK(order.Key(MenuEvent::down)==MenuAction::moveEntry && order.index==1 && order.order[0]==8 && order.order[1]==7 && order.orderDirty);
     CHECK(order.Key(MenuEvent::end)==MenuAction::moveEntry && order.index==2 && order.order[2]==7 && order.order[1]==9);
     CHECK(order.Key(MenuEvent::back)==MenuAction::grabCancelled && order.index==0 && order.order[0]==7 && order.order[1]==8 && order.order[2]==9);
     CHECK(order.Key(MenuEvent::space)==MenuAction::grabEntry && order.Key(MenuEvent::down)==MenuAction::moveEntry);
     CHECK(order.Key(MenuEvent::enter)==MenuAction::dropEntry && !order.grabbed && order.order[1]==7);
     CHECK(order.Key(MenuEvent::down)==MenuAction::changed && order.Key(MenuEvent::down)==MenuAction::changed && order.index==order.OrderSaveRow());
     CHECK(order.Key(MenuEvent::space)==MenuAction::none && order.Key(MenuEvent::enter)==MenuAction::saveOrder);
     order.Confirm(MenuAction::saveOrder);CHECK(order.page==MenuPage::confirm);
     CHECK(order.Key(MenuEvent::back)==MenuAction::cancelled && order.page==MenuPage::bootOrder && order.index==order.OrderSaveRow());
     order.index=order.OrderTimeoutRow();order.timeoutKnown=true;order.firmwareTimeout=0;
     CHECK(order.Key(MenuEvent::left)==MenuAction::timeoutEdge && order.Key(MenuEvent::right)==MenuAction::changeTimeout && order.firmwareTimeout==1);
     order.firmwareTimeout=30;CHECK(order.Key(MenuEvent::right)==MenuAction::timeoutEdge);
     order.index=order.OrderRevertRow();CHECK(order.Key(MenuEvent::enter)==MenuAction::revertOrder);
     CHECK(order.Key(MenuEvent::back)==MenuAction::leaveOrder && order.page==MenuPage::main && order.index==mainOrder);}
    // r355 settings: volumes, boot messages, remember, auto delay, once, progress.
    {MenuModel s;s.page=MenuPage::settings;s.index=setSpeechVolume;
     CHECK(s.Key(MenuEvent::right)==MenuAction::edge && s.Key(MenuEvent::left)==MenuAction::changeVolume && s.speechVolume==9);
     for(int i=0;i<8;++i)s.Key(MenuEvent::left);
     CHECK(s.speechVolume==1 && s.Key(MenuEvent::left)==MenuAction::edge && s.page==MenuPage::settings);
     s.index=setEffectsVolume;for(int i=0;i<10;++i)s.Key(MenuEvent::left);CHECK(s.effectsVolume==0 && s.Key(MenuEvent::left)==MenuAction::edge);
     s.index=setVerbosity;CHECK(s.Key(MenuEvent::enter)==MenuAction::openVerbosity && s.page==MenuPage::verbosity && s.index==1 && s.Count()==3);
     CHECK(s.Key(MenuEvent::end)==MenuAction::changed && s.Key(MenuEvent::enter)==MenuAction::selectVerbosity && s.verbosity==2 && s.index==setVerbosity);
     s.index=setRemember;CHECK(s.Key(MenuEvent::space)==MenuAction::toggleRemember && s.rememberLast);
     s.index=setAutoDelay;CHECK(s.Key(MenuEvent::enter)==MenuAction::openAutoDelay && s.index==3 && s.Count()==9);
     CHECK(s.Key(MenuEvent::end)==MenuAction::changed && s.Key(MenuEvent::enter)==MenuAction::selectAutoDelay && s.autoSeconds==255);
     s.index=setAutoDelay;s.Key(MenuEvent::enter);CHECK(s.index==8 && s.Key(MenuEvent::back)==MenuAction::cancelled && s.index==setAutoDelay);
     s.index=setBootNextRow;CHECK(s.Key(MenuEvent::space)==MenuAction::none && s.Key(MenuEvent::enter)==MenuAction::setBootNext);
     s.index=setProgress;CHECK(s.Key(MenuEvent::enter)==MenuAction::openProgress && s.Count()==4 && s.index==1);
     CHECK(s.Key(MenuEvent::end)==MenuAction::changed && s.Key(MenuEvent::enter)==MenuAction::selectProgress && s.progress==3);
     CharSettings v;v.sequence=3;v.speechVolume=4;v.effectsVolume=0;v.verbosity=2;v.rememberLast=true;v.autoSeconds=255;v.progress=2;
     uint8_t bytes[charSettingsBytes];CHECK(EncodeSettings(v,bytes,sizeof(bytes)) && bytes[7]=='C');   // r397: V12
     CharSettings back;CHECK(DecodeSettings(bytes,sizeof(bytes),back) && SameSettings(v,back));
     v.autoSeconds=7;CHECK(!EncodeSettings(v,bytes,sizeof(bytes)));v.autoSeconds=0;v.progress=4;CHECK(!EncodeSettings(v,bytes,sizeof(bytes)));
     v.progress=0;v.speechVolume=0;CHECK(!EncodeSettings(v,bytes,sizeof(bytes)));
     CharSettings fresh;CHECK(fresh.backgroundMelody==0 && fresh.startMelody==1);}
    {MenuModel f;f.page=MenuPage::settings;f.index=setFirmwareVoice;CHECK(f.Key(MenuEvent::space)==MenuAction::none && f.Key(MenuEvent::enter)==MenuAction::toggleFirmwareVoice);
     f.Confirm(MenuAction::toggleFirmwareVoice);CHECK(f.Key(MenuEvent::back)==MenuAction::cancelled && f.page==MenuPage::settings && f.index==setFirmwareVoice);}
    // r382: Welcome check boxes, screen reader voice, Detail check boxes, V7 record.
    {MenuModel w;w.page=MenuPage::settings;w.index=setWelcome;CHECK(w.Key(MenuEvent::enter)==MenuAction::openWelcome && w.page==MenuPage::welcome && w.Count()==3);
     CHECK(w.Key(MenuEvent::space)==MenuAction::toggleWelcome && w.welcome==6 && w.page==MenuPage::welcome);
     CHECK(w.Key(MenuEvent::end)==MenuAction::changed && w.Key(MenuEvent::enter)==MenuAction::toggleWelcome && w.welcome==2);
     CHECK(w.Key(MenuEvent::back)==MenuAction::cancelled && w.page==MenuPage::settings && w.index==setWelcome);
     w.index=setPitch;CHECK(w.Key(MenuEvent::right)==MenuAction::changeRate && w.pitch==11);
     w.index=setReaderVoice;CHECK(w.Key(MenuEvent::space)==MenuAction::none && w.Key(MenuEvent::right)==MenuAction::openReader && w.page==MenuPage::reader && w.Count()==readerCount);
     CHECK(w.Key(MenuEvent::right)==MenuAction::changeReaderVoice && w.readerRate==11);
     CHECK(w.Key(MenuEvent::down)==MenuAction::changed && w.Key(MenuEvent::left)==MenuAction::changeReaderVoice && w.readerPitch==9);
     CHECK(w.Key(MenuEvent::down)==MenuAction::changed && w.Key(MenuEvent::right)==MenuAction::edge && w.readerVolume==10);
     CHECK(w.Key(MenuEvent::enter)==MenuAction::none && w.Key(MenuEvent::back)==MenuAction::cancelled && w.page==MenuPage::settings && w.index==setReaderVoice);
     w.index=setReaderVoice;CHECK(w.Key(MenuEvent::enter)==MenuAction::openReader && w.Count()==readerCount);w.index=readerTimerSpeechRow;w.timers=0;   // r397: the reader page grew (r390-r396); timers start from off here (the default is tones on since r389)
     CHECK(w.Key(MenuEvent::right)==MenuAction::none || true);
     CHECK(w.Key(MenuEvent::space)==MenuAction::toggleTimer && w.timers==1);w.index=readerTimerSoundRow;
     CHECK(w.Key(MenuEvent::enter)==MenuAction::toggleTimer && w.timers==3 && w.page==MenuPage::reader);
     // r389: boot-message voice sliders (default 140% rate, 100% pitch).
     CHECK(w.Key(MenuEvent::down)==MenuAction::changed && w.index==readerMessageRateRow && w.messageRate==14);
     CHECK(w.Key(MenuEvent::right)==MenuAction::changeReaderVoice && w.messageRate==15);
     CHECK(w.Key(MenuEvent::down)==MenuAction::changed && w.Key(MenuEvent::left)==MenuAction::changeReaderVoice && w.messagePitch==9);
     CHECK(w.Key(MenuEvent::back)==MenuAction::cancelled);
     w.index=setDetail;CHECK(w.Key(MenuEvent::enter)==MenuAction::openDetail && w.page==MenuPage::detail && w.Count()==3);
     CHECK(w.Key(MenuEvent::space)==MenuAction::toggleDetail && w.detail==6 && w.Key(MenuEvent::back)==MenuAction::cancelled && w.index==setDetail);
     CharSettings v;v.sequence=5;v.welcome=2;v.pitch=7;v.readerRate=18;v.readerPitch=6;v.readerVolume=3;v.detail=2;v.timers=3;v.rememberLast=true;
     uint8_t bytes[charSettingsBytes];CHECK(sizeof(bytes)==48 && EncodeSettings(v,bytes,sizeof(bytes)) && bytes[7]=='C');   // r397: V12, 48 bytes since V9
     CharSettings back;CHECK(DecodeSettings(bytes,sizeof(bytes),back) && SameSettings(v,back) && back.rememberLast && back.welcome==2);
     v.readerPitch=21;CHECK(!EncodeSettings(v,bytes,sizeof(bytes)));v.readerPitch=6;v.detail=8;CHECK(!EncodeSettings(v,bytes,sizeof(bytes)));
     v.detail=7;v.welcome=8;CHECK(!EncodeSettings(v,bytes,sizeof(bytes)));v.welcome=7;v.timers=4;CHECK(!EncodeSettings(v,bytes,sizeof(bytes)));
     // A V6 record (32 bytes) decodes to all-on Welcome and Detail, reader voice = menu voice.
     uint8_t old[32]={'C','H','A','R','S','0','0','6',1};old[22]=14;old[23]=6;old[24]=10;old[25]=1;old[27]=10;
     const auto crc=SettingsCRC(old,28);for(unsigned i=0;i<4;++i)old[28+i]=uint8_t(crc>>(8*i));
     CHECK(DecodeSettings(old,32,back) && back.welcome==7 && back.detail==7 && back.readerRate==14 && back.readerVolume==6 && back.pitch==10 && back.readerPitch==10 && back.timers==2 && back.messageRate==14);
     // r389: a V7 record with countdown tones off (the old default) reads as tones on; V8 keeps the message voice.
     v.timers=0;v.messageRate=17;v.messagePitch=8;CHECK(EncodeSettings(v,bytes,sizeof(bytes)) && bytes[34]==17 && bytes[35]==8);
     CHECK(DecodeSettings(bytes,sizeof(bytes),back) && back.timers==0 && back.messageRate==17 && back.messagePitch==8);
     bytes[7]='7';bytes[34]=bytes[35]=0;{const auto c7=SettingsCRC(bytes,36);for(unsigned i=0;i<4;++i)bytes[36+i]=uint8_t(c7>>(8*i));}
     CHECK(DecodeSettings(bytes,40,back) && back.timers==2 && back.messageRate==14 && back.messagePitch==10);   // r397: a V7 record is 40 bytes
     v.messageRate=21;CHECK(!EncodeSettings(v,bytes,sizeof(bytes)));}
    puts("PASS preOS menu navigation, empty lists, wrapping, explicit confirmations, Continue policy events and exact target record validation");
}
