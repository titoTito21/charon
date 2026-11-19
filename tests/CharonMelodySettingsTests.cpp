#include "../src/efi/CharSettings.h"
#include "../src/efi/CharonCues.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(false)
using namespace hakefi;
void CRC40(uint8_t* data) {const auto crc=SettingsCRC(data,44);for(unsigned i=0;i<4;++i)data[44+i]=uint8_t(crc>>(8*i));}
void CRC(uint8_t* data) {const auto crc=SettingsCRC(data,28);for(unsigned i=0;i<4;++i)data[28+i]=uint8_t(crc>>(8*i));}
int main() {
    CharSettings value,result;CHECK(value.startMelody==1 && value.backgroundMelody==0);
    value.sequence=1;value.explicitMessages=true;uint8_t bytes[48],other[48];
    for(unsigned start=0;start<=4;++start)for(unsigned bed=0;bed<=4;++bed) {
        value.startMelody=uint8_t(start);value.backgroundMelody=uint8_t(bed);
        CHECK(EncodeSettings(value,bytes,48) && DecodeSettings(bytes,48,result));
        CHECK(result.startMelody==start && result.backgroundMelody==bed && result.explicitMessages);
    }
    for(char version:{'1','2','3'}) {
        memcpy(other,bytes,32);other[7]=version;for(unsigned i=20;i<28;++i)other[i]=0;
        if(version=='1') {other[18]=0;}
        if(version!='3') {other[19]=0;}
        CRC(other);
        CHECK(DecodeSettings(other,32,result) && result.startMelody==1 && result.backgroundMelody==0);
        CHECK(result.explicitMessages==(version=='3'));
        other[20]=1;CRC(other);CHECK(!DecodeSettings(other,32,result));
    }
    for(unsigned field:{20u,21u,22u}) {memcpy(other,bytes,48);other[field]=field==22?21:5;CRC40(other);CHECK(!DecodeSettings(other,48,result));}
    memcpy(other,bytes,48);other[22]=4;CRC40(other);CHECK(!DecodeSettings(other,48,result));
    value.startMelody=5;CHECK(!EncodeSettings(value,bytes,48));value.startMelody=1;value.backgroundMelody=5;CHECK(!EncodeSettings(value,bytes,48));
    value.backgroundMelody=1;CHECK(EncodeSettings(value,bytes,48));value.startMelody=2;CHECK(EncodeSettings(value,other,48));
    CHECK(!ChooseSettings(bytes,48,other,48,result));
    for(unsigned row:{9u,10u}) {
        MenuModel model;model.page=MenuPage::settings;model.index=row;model.sounds=false;model.backgroundMelody=1;
        CHECK(model.Key(MenuEvent::enter)==MenuAction::openMelody && model.Count()==5 && model.index==1);
        CHECK(model.Key(MenuEvent::home)==MenuAction::changed && model.index==0);
        CHECK(model.Key(MenuEvent::up)==MenuAction::edge);
        CHECK(model.Key(MenuEvent::back)==MenuAction::cancelled && model.page==MenuPage::settings && model.index==row);
        CHECK(model.startMelody==1 && model.backgroundMelody==1);
        CHECK(model.Key(MenuEvent::space)==MenuAction::openMelody);
        CHECK(model.Key(MenuEvent::end)==MenuAction::changed && model.index==4);
        CHECK(model.Key(MenuEvent::enter)==MenuAction::selectMelody && model.page==MenuPage::settings && model.index==row);
        CHECK((row==9?model.startMelody:model.backgroundMelody)==4 && !model.sounds);
        CHECK(model.Key(MenuEvent::enter)==MenuAction::openMelody && model.index==4);
        model.wrap=true;CHECK(model.Key(MenuEvent::down)==MenuAction::changed && model.index==0);
        CHECK(model.Key(MenuEvent::space)==MenuAction::selectMelody && (row==9?model.startMelody:model.backgroundMelody)==0);
    }
    PresentedMenuCues cues;std::vector<MenuCue> events;auto emit=[&](MenuCue cue) {events.push_back(cue);};
    cues.Present(MenuPage::settings,true,emit);events.clear();
    cues.Present(MenuPage::startMelody,true,emit);cues.Present(MenuPage::settings,true,emit);
    CHECK(events==std::vector<MenuCue>({MenuCue::popupOpen,MenuCue::popupClose}));
    events.clear();cues.Present(MenuPage::backgroundMelody,false,emit);cues.Present(MenuPage::settings,false,emit);CHECK(events.empty());
    puts("PASS melody choices/off, independent UI sounds, popup navigation/cues, strict v4 and v1-v3 migration");
}
