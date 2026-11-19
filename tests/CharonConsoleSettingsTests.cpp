#include "../src/efi/ConsoleMenu.h"
#include "../src/efi/CharSettings.h"
#include "../src/efi/CharonCues.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
using namespace hakefi;
struct FakeConsole {
    struct ModeInfo {int Mode=0;} mode;
    ModeInfo* Mode=&mode;
    size_t columns=80,rows=25,row=0,attribute=7;int clears=0;bool failed=false;
    std::vector<std::u16string> lines;std::vector<size_t> attributes;
    static int query(FakeConsole* c,size_t,size_t* columns,size_t* rows) {*columns=c->columns;*rows=c->rows;return c->failed?1:0;}
    static int color(FakeConsole* c,size_t value) {c->attribute=value;return 0;}
    static int clear(FakeConsole* c) {++c->clears;c->lines.assign(c->rows,u"");c->attributes.assign(c->rows,0);return 0;}
    static int cursor(FakeConsole* c,size_t column,size_t row) {CHECK(column==0 && row<c->rows);c->row=row;return 0;}
    static int output(FakeConsole* c,char16_t* text) {
        std::u16string value(text);CHECK(value.size()<c->columns);
        CHECK(value.find(u'\n')==value.npos && value.find(u'\r')==value.npos);
        c->lines[c->row]=value;c->attributes[c->row]=c->attribute;return 0;
    }
    static int enable(FakeConsole*,bool visible) {CHECK(!visible);return 0;}
    decltype(&query) QueryMode=query;decltype(&color) SetAttribute=color;
    decltype(&clear) ClearScreen=clear;decltype(&cursor) SetCursorPosition=cursor;
    decltype(&output) OutputString=output;decltype(&enable) EnableCursor=enable;
};
int main() {
    auto label=[](size_t index,char16_t* out,size_t capacity) {out[0]=u'R';out[1]=0;ConsoleNumber(out,capacity,index);};
    FakeConsole console;
    CHECK(DrawConsoleMenu(&console,u"Settings",8,2,label,u"Saved",u"Console only",u"Enter / Esc"));
    CHECK(console.clears==1 && console.lines[0].find(u"Charon")==0);
    for(size_t i=0;i<8;++i) {std::u16string expected=(i==2?u"> R":u"  R");expected+=char16_t(u'0'+i);
        CHECK(console.lines[i+2].find(expected)==0 && console.attributes[i+2]==(i==2?0x70:7));}
    CHECK(console.lines[22].find(u"Saved")==0 && console.lines[23].find(u"Console only")==0);
    console.rows=9;
    CHECK(DrawConsoleMenu(&console,u"Devices",256,4,label,u"Kept message",u"",u"Keys"));
    CHECK(console.lines[2].find(u"  R3")==0 && console.lines[3].find(u"> R4")==0 && console.lines[4].find(u"  R5")==0);
    CHECK(console.lines[5].find(u"5 / 256")==0 && console.lines[6].find(u"Kept message")==0);
    CHECK(DrawConsoleMenu(&console,u"Devices",256,255,label,u"Kept message",u"",u"Keys"));
    CHECK(console.lines[2].find(u"> R255")==0 && console.lines[6].find(u"Kept message")==0);
    CHECK(DrawConsoleMenu(&console,u"Empty",0,0,label,u"",u"",u"Keys"));
    CHECK(console.lines[2].find(u"R0")==0 && console.attributes[2]==7);
    console.columns=12;console.rows=7;
    auto unicode=[](size_t,char16_t* out,size_t) {const char16_t value[]={u'A',u'A',u'A',u'A',u'A',u'A',u'A',u'A',0xd83d,0xde00,0};
        for(unsigned i=0;i<11;++i)out[i]=value[i];};
    CHECK(DrawConsoleMenu(&console,u"Long page name",1,0,unicode,u"status\nnew line",u"",u"Keys"));
    for(const auto& line:console.lines)for(auto ch:line)CHECK(!(ch>=0xd800 && ch<=0xdfff));
    console.failed=true;const auto clears=console.clears;
    CHECK(!DrawConsoleMenu(&console,u"Page",3,0,label,u"",u"",u"Keys") && console.clears==clears);
    console.failed=false;console.rows=6;CHECK(!DrawConsoleMenu(&console,u"Page",3,0,label,u"",u"",u"Keys"));
    CHECK(MenuViewport(256,255,7).first==255 && MenuViewport(256,255,7).count==1);
    CHECK(MenuViewport(3,3,25).capacity==0);
    console.rows=12;console.columns=30;
    CHECK(DrawConsoleMenu(&console,u"Message",1,0,label,u"Persistent status",u"",u"Enter / Esc",
        u"An operation failed. No firmware state was changed. This is a visible message."));
    CHECK(console.lines[2].find(u"An operation failed.")==0);
    CHECK(console.lines[7].find(u"> R0")==0 && console.attributes[7]==0x70);
    CHECK(console.lines[9].find(u"Persistent status")==0);
    CharSettings settings;settings.sequence=1;settings.language=2;settings.wrap=true;
    // r397: records are 48 bytes since V9 (CRC at 44); 40-byte V7/V8 records are still read.
    uint8_t bytes[48],second[48];CHECK(EncodeSettings(settings,bytes,48) && !EncodeSettings(settings,bytes,40));CharSettings parsed;
    CHECK(!EncodeSettings(settings,nullptr,32) && !DecodeSettings(nullptr,32,parsed));
    CHECK(!ChooseSettings(nullptr,32,nullptr,32,parsed));
    CHECK(DecodeSettings(bytes,48,parsed) && parsed.sequence==1 && parsed.language==2 && parsed.wrap);
    for(unsigned at=0;at<384;++at) {uint8_t corrupt[48];memcpy(corrupt,bytes,48);corrupt[at/8]^=1<<(at%8);
        parsed.sequence=123;CHECK(!DecodeSettings(corrupt,48,parsed) && parsed.sequence==123);}
    auto newer=settings;newer.sequence=2;newer.language=1;CHECK(EncodeSettings(newer,second,48));
    for(size_t cut=0;cut<48;++cut) {CHECK(ChooseSettings(bytes,48,second,cut,parsed));CHECK(parsed.sequence==1);}
    CHECK(ChooseSettings(bytes,48,second,48,parsed) && parsed.sequence==2 && parsed.language==1);
    newer.sequence=1;CHECK(EncodeSettings(newer,second,48));CHECK(!ChooseSettings(bytes,48,second,48,parsed));
    CHECK(!DecodeSettings(bytes,47,parsed) && !DecodeSettings(bytes,49,parsed) && !DecodeSettings(bytes,40,parsed) && !DecodeSettings(bytes,32,parsed));
    for(unsigned field:{0u,16u,17u,18u}) {memcpy(second,bytes,48);second[field]=3;
        const auto crc=SettingsCRC(second,44);for(unsigned i=0;i<4;++i)second[44+i]=uint8_t(crc>>(8*i));
        CHECK(!DecodeSettings(second,48,parsed));}
    CharSettings automatic;CHECK(PolishSettings(automatic,"pl") && PolishSettings(automatic,"pl-PL")
        && PolishSettings(automatic,"PL_pl.UTF-8") && PolishSettings(automatic,"pl.UTF-8"));
    CHECK(!PolishSettings(automatic,"en-US") && !PolishSettings(automatic,"plausible")
        && !PolishSettings(automatic,"") && !PolishSettings(automatic,nullptr));
    automatic.language=1;CHECK(!PolishSettings(automatic,"pl-PL"));automatic.language=2;CHECK(PolishSettings(automatic,"en-US"));
    // Version 1 migration and version 2 global UI-sound preference.
    memcpy(second,bytes,32);second[7]='1';second[18]=0;for(unsigned i=20;i<28;++i)second[i]=0;
    auto crc=SettingsCRC(second,28);for(unsigned i=0;i<4;++i)second[28+i]=uint8_t(crc>>(8*i));
    CHECK(DecodeSettings(second,32,parsed) && parsed.sounds);
    settings.sounds=false;CHECK(EncodeSettings(settings,bytes,48) && DecodeSettings(bytes,48,parsed) && !parsed.sounds);
    // a 48-byte record that claims V7 (a 40-byte version) is rejected
    bytes[7]='7';crc=SettingsCRC(bytes,44);for(unsigned i=0;i<4;++i)bytes[44+i]=uint8_t(crc>>(8*i));
    CHECK(!DecodeSettings(bytes,48,parsed));
    MenuModel model;PresentedMenuCues presentation;std::vector<MenuCue> cues;
    auto emit=[&](MenuCue cue) {cues.push_back(cue);};
    presentation.Present(model.page,model.sounds,emit);CHECK(cues.size()==1 && cues[0]==MenuCue::welcome);
    auto key=[&](MenuEvent event) {
        cues.clear();const auto before=model;const auto action=model.Key(event);
        const auto cue=TransitionCue(before,event,action,model);if(cue!=MenuCue::none)emit(cue);
        presentation.Present(model.page,model.sounds,emit);
        return cues.empty()?MenuCue::none:cues.back();
    };
    CHECK(key(MenuEvent::up)==MenuCue::edge);
    CHECK(key(MenuEvent::down)==MenuCue::focus && model.index==1);
    CHECK(key(MenuEvent::down)==MenuCue::focus && model.index==2);
    CHECK(key(MenuEvent::right)==MenuCue::submenuOpen && model.page==MenuPage::settings);
    CHECK(key(MenuEvent::space)==MenuCue::select && model.wrap);
    CHECK(key(MenuEvent::up)==MenuCue::wrap && model.index==settingsCount-1);
    model.index=12;CHECK(key(MenuEvent::up)==MenuCue::focus && model.index==11);
    CHECK(key(MenuEvent::up)==MenuCue::focus && model.index==10);
    CHECK(key(MenuEvent::up)==MenuCue::focus && model.index==9);
    CHECK(key(MenuEvent::up)==MenuCue::focus && model.index==8);
    CHECK(key(MenuEvent::space)==MenuCue::none && !model.sounds);
    CHECK(key(MenuEvent::up)==MenuCue::none && model.index==7);
    CHECK(key(MenuEvent::space)==MenuCue::none && model.page==MenuPage::language);
    CHECK(key(MenuEvent::end)==MenuCue::none && model.index==2);
    CHECK(key(MenuEvent::back)==MenuCue::none && model.language==0 && model.index==7);
    model.sounds=true;
    CHECK(key(MenuEvent::enter)==MenuCue::popupOpen && model.page==MenuPage::language);
    CHECK(key(MenuEvent::down)==MenuCue::focus);
    CHECK(key(MenuEvent::enter)==MenuCue::popupClose && model.language==1);
    model.index=6;CHECK(key(MenuEvent::enter)==MenuCue::none); // No dialog has been painted yet.
    model.Confirm(MenuAction::setContinue);presentation.Present(model.page,model.sounds,emit);
    CHECK(cues.back()==MenuCue::dialogOpen);
    CHECK(key(MenuEvent::back)==MenuCue::dialogClose);
    CHECK(key(MenuEvent::back)==MenuCue::submenuClose);
    CHECK(key(MenuEvent::back)==MenuCue::continueBoot);
    CHECK(std::strcmp(MenuCueAsset(MenuCue::popupOpen),"popUp.wav")==0);
    CHECK(std::strcmp(MenuCueAsset(MenuCue::dialogOpen),"dialog_open.wav")==0);
    CHECK(std::strcmp(MenuCueAsset(MenuCue::dialogClose),"dialog_close.wav")==0);
    CHECK(std::strcmp(MenuCueAsset(MenuCue::welcome),"welcome.wav")==0);
    CHECK(MenuCueAsset(MenuCue::none)==nullptr);
    // Popup acknowledgement cannot invoke the underlying Continue/boot action.
    for(auto dismiss:{MenuEvent::enter,MenuEvent::space,MenuEvent::back}) {
        model.page=MenuPage::main;model.index=4;model.Notice();
        CHECK(model.page==MenuPage::notice && model.Count()==1);
        model.Notice(); // Update visible text, retain the initial return target.
        CHECK(model.Key(dismiss)==MenuAction::dismissNotice && model.page==MenuPage::main && model.index==4);
    }
    model.Confirm(MenuAction::pendingKeys);model.index=1;model.Notice();
    CHECK(model.Key(MenuEvent::enter)==MenuAction::dismissNotice && model.page==MenuPage::confirm
        && model.index==1 && model.pending==MenuAction::pendingKeys);
    // Only successful frames drive lifecycle. Repaints/status updates are quiet.
    PresentedMenuCues lifecycle;std::vector<MenuCue> events;
    auto collect=[&](MenuCue event) {events.push_back(event);};
    lifecycle.Present(MenuPage::main,true,collect);events.clear();
    console.failed=true;
    if(DrawConsoleMenu(&console,u"Message",1,0,label,u"",u"",u"",u"Error"))
        lifecycle.Present(MenuPage::notice,true,collect);
    CHECK(events.empty() && lifecycle.page==MenuPage::main);
    console.failed=false;
    lifecycle.Present(MenuPage::notice,true,collect);
    CHECK(events.size()==1 && events[0]==MenuCue::popupOpen);
    lifecycle.Present(MenuPage::notice,true,collect);CHECK(events.size()==1);
    lifecycle.Present(MenuPage::main,true,collect);CHECK(events.back()==MenuCue::popupClose);
    events.clear();lifecycle.Present(MenuPage::confirm,true,collect);CHECK(events.back()==MenuCue::dialogOpen);
    events.clear();lifecycle.Present(MenuPage::notice,true,collect);
    CHECK(events.size()==2 && events[0]==MenuCue::dialogClose && events[1]==MenuCue::popupOpen);
    events.clear();lifecycle.Present(MenuPage::main,false,collect);CHECK(events.empty());
    lifecycle.Present(MenuPage::main,true,collect);CHECK(events.empty()); // No stale close after unmuting.
    // Countdown prompt commits welcome once; Space selects only after the
    // actual menu frame commits. A failed frame leaves the request pending.
    PresentedMenuCues startup;events.clear();
    startup.Present(MenuPage::main,true,collect);
    CHECK(events.size()==1 && events[0]==MenuCue::welcome);
    startup.RequestMainMenu();console.failed=true;
    if(DrawConsoleMenu(&console,u"Main",1,0,label,u"",u"",u""))
        startup.Present(MenuPage::main,true,collect);
    CHECK(events.size()==1 && startup.menuOpenPending);
    console.failed=false;
    CHECK(DrawConsoleMenu(&console,u"Main",1,0,label,u"",u"",u""));
    startup.Present(MenuPage::main,true,collect);
    CHECK(events.size()==2 && events[1]==MenuCue::select && !startup.menuOpenPending);
    startup.Present(MenuPage::main,true,collect);CHECK(events.size()==2);
    // No target (or failed prompt): the first visible popup/menu is welcome,
    // never a speculative selection followed by a second welcome.
    PresentedMenuCues noTarget;events.clear();
    noTarget.Present(MenuPage::notice,true,collect);
    noTarget.Present(MenuPage::notice,true,collect);
    CHECK(events.size()==1 && events[0]==MenuCue::welcome);
    PresentedMenuCues failedPrompt;events.clear();failedPrompt.RequestMainMenu();
    failedPrompt.Present(MenuPage::main,true,collect);
    CHECK(events.size()==1 && events[0]==MenuCue::welcome);
    PresentedMenuCues muted;events.clear();muted.Present(MenuPage::main,false,collect);
    muted.RequestMainMenu();muted.Present(MenuPage::main,false,collect);
    muted.Present(MenuPage::main,true,collect);CHECK(events.empty());
    puts("PASS console/settings plus committed autoboot welcome, menu-open cue, failed paint retry and muted startup");
}
