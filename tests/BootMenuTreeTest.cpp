#include "../src/HAKBootMenuTree.h"
#include <cassert>
#include <cstdio>
#include <set>

int main() {
    using namespace hakboot;
    std::set<int> actions,settings;
    for(int p=kMenuRoot;p<=kMenuNavigation;++p) {
        const auto page=MenuPage(p);
        const auto& entries=MenuEntries(page);
        assert(!entries.empty() && entries.size()<=5);
        for(int item:entries) {
            if(item<0) {assert(-item>=kMenuHaiku && -item<=kMenuNavigation);continue;}
            auto& seen=MenuHasSettings(page) ? settings : actions;
            assert(seen.insert(item).second);
        }
        for(const char* lang:{"en","pl","de","es","fr"})assert(MenuPageName(page,lang)[0]);
    }
    for(int i=0;i<kSettingCount;++i)assert(settings.count(i));
    for(int i=0;i<=kDiskMenuItem;++i)assert(actions.count(i));
    MenuTree tree;
    assert(!tree.Back() && !tree.Open());
    tree.current=3;assert(tree.Open() && tree.page==kMenuSettings);
    tree.current=1;assert(tree.Open() && tree.page==kMenuSound);
    tree.current=4;assert(tree.Item(tree.current)==kSettingShutdownSound && !tree.Open());
    assert(tree.Back() && tree.page==kMenuSettings && tree.current==1);
    assert(tree.Back() && tree.page==kMenuRoot && tree.current==3);
    assert(!tree.Back());
    assert(SettingDirection(kSettingSpeed,B_LEFT_ARROW)==-1);
    assert(SettingDirection(kSettingStatus,B_RIGHT_ARROW)==1);
    assert(SettingDirection(kSettingStartSound,' ')==1);
    puts("PASS all original actions/settings reachable exactly once, at most five items, nested return position and control keys");
}
