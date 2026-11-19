// Owned menu test app. Items have no destructive actions.
#include <Application.h>
#include <Window.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <TextControl.h>
#include <cstdlib>
class MenuWindow : public BWindow {
    BMenu* menu;
public:
    MenuWindow():BWindow(BRect(100,100,600,300),"NVDA menu fixture",B_TITLED_WINDOW,B_QUIT_ON_WINDOW_CLOSE) {
    auto* bar=new BMenuBar(BRect(0,0,500,25),"bar");
    menu=new BMenu("Test menu");
    menu->AddItem(new BMenuItem("Pierwsza pozycja",nullptr));
    auto* checked=new BMenuItem("Zaznaczona opcja",nullptr);checked->SetMarked(true);menu->AddItem(checked);
    auto* disabled=new BMenuItem("Niedostępna opcja",nullptr);disabled->SetEnabled(false);menu->AddItem(disabled);
    auto* sub=new BMenu("Podmenu");sub->AddItem(new BMenuItem("Element podmenu",nullptr));menu->AddItem(sub);
    bar->AddItem(menu);AddChild(bar);
    auto* field=new BTextControl(BRect(20,70,460,100),"field","Pole testowe","",nullptr);
    AddChild(field);field->MakeFocus(true);
    }
    void MessageReceived(BMessage* message) override {
        if(message->what=='repl') {
            // Only mutate this owned fixture after its popup has closed.
            BMessage reply(B_REPLY);
            if(menu->Window()!=nullptr)reply.AddInt32("error",B_BUSY);
            else {
                delete menu->RemoveItem(int32(0));
                bool added=menu->AddItem(new BMenuItem("Nowa pozycja",nullptr),0);
                reply.AddInt32("error",added ? B_OK : B_ERROR);
            }
            message->SendReply(&reply);return;
        }
        BWindow::MessageReceived(message);
    }
};
int main(int argc,char** argv) {
    BApplication app(argc==2 ? "application/x-vnd.NVDAHaiku.MenuFixtureCommand" : "application/x-vnd.NVDAHaiku.MenuFixture");
    if(argc==2) {
        BMessenger target(nullptr,atoi(argv[1]));BMessage query(B_GET_PROPERTY),listed;
        query.AddSpecifier("Windows");BMessenger window;
        if(target.SendMessage(&query,&listed,250000,250000)!=B_OK || listed.FindMessenger("result",&window)!=B_OK)return 2;
        BMessage request('repl'),reply;
        if(window.SendMessage(&request,&reply,250000,500000)!=B_OK)return 3;
        return reply.GetInt32("error",B_ERROR)==B_OK ? 0 : 4;
    }
    (new MenuWindow())->Show();app.Run();
}
