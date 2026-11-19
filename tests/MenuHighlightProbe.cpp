// Probe only: observe original menu tracking without offsets or library patches.
#include <MenuItem.h>
#include <Menu.h>
#include <Window.h>
#include <dlfcn.h>
#include <cstdio>
void BMenuItem::Highlight(bool highlighted) {
    using Function=void(*)(BMenuItem*,bool);
    static Function original=reinterpret_cast<Function>(dlsym(RTLD_NEXT,"_ZN9BMenuItem9HighlightEb"));
    if(!original)return;
    original(this,highlighted);
    BMenu* menu=Menu();
    fprintf(stderr,"MENU highlight=%d selected=%d marked=%d enabled=%d submenu=%d window=%d label=%s\n",
        int(highlighted),int(IsSelected()),int(IsMarked()),int(IsEnabled()),int(Submenu()!=nullptr),
        int(menu && menu->Window()!=nullptr),Label() ? Label() : "");
    fflush(stderr);
}
