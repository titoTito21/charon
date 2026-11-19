#include "../src/HAKBootMenuSounds.h"
#include "../src/HAKBootSettingsTexts.h"
#include <cassert>
int main(int argc,char** argv) {
    using namespace hakboot;
    assert(argc==2);
    int current=0;
    assert(MoveMenu(current,3,-1,false)==Edge && current==0);
    assert(MoveMenu(current,3,-1,true)==Wrap && current==2);
    assert(MoveMenu(current,3,1,false)==Edge && current==2);
    assert(MoveMenu(current,3,1,true)==Wrap && current==0);
    assert(MoveMenu(current,3,1,false)==Focus && current==1);
    current=0;assert(MoveMenu(current,1,1,true)==Edge && current==0);
    Settings settings;
    assert(settings.menuSounds && settings.wrapLists);
    assert(ChangeSetting(settings,kSettingMenuSounds,1) && !settings.menuSounds);
    assert(ChangeSetting(settings,kSettingWrapLists,1) && !settings.wrapLists);
    settings.status=2;ChangeSetting(settings,kSettingStatus,1);assert(settings.status==2);
    settings.startMelody=0;ChangeSetting(settings,kSettingSignal,-1);assert(settings.startMelody==0);
    settings.wrapLists=true;ChangeSetting(settings,kSettingSignal,-1);assert(settings.startMelody==4);
    const std::string path=std::string(argv[1])+"/menu-settings";
    assert(StoreSetting("menu_sounds","off",path.c_str()));
    assert(StoreSetting("wrap_lists","off",path.c_str()));
    Settings restored;LoadSettings(restored,path.c_str());
    assert(!restored.menuSounds && !restored.wrapLists);
    for(const auto& text:kSettingsTexts)
        for(int i=0;i<kSettingCount;++i)assert(text.labels[i] && text.labels[i][0]);
    puts("PASS boundary/wrap feedback, list limits, persistent switches and all settings languages");
}
