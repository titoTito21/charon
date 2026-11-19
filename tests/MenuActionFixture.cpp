// Owned menu test app. Items have no destructive actions.
#include <Application.h>
#include <Window.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <TextControl.h>
#include <cstdlib>
#include <cstdio>
class MenuWindow : public BWindow {
    BMenu* menu;
    int32 invoked=0;
public:
    MenuWindow():BWindow(BRect(100,100,600,300),"NVDA menu fixture",B_TITLED_WINDOW,B_QUIT_ON_WINDOW_CLOSE) {
    auto* bar=new BMenuBar(BRect(0,0,500,25),"bar",B_FOLLOW_LEFT_RIGHT,
        getenv("NVDA_TEST_VERTICAL") ? B_ITEMS_IN_COLUMN : B_ITEMS_IN_ROW);
    menu=new BMenu("Test menu");
    menu->AddItem(new BMenuItem("Pierwsza pozycja",new BMessage('actn')));
    auto* checked=new BMenuItem("Zaznaczona opcja",nullptr);checked->SetMarked(true);menu->AddItem(checked);
    auto* disabled=new BMenuItem("Niedostępna opcja",nullptr);disabled->SetEnabled(false);menu->AddItem(disabled);
    auto* sub=new BMenu("Podmenu");sub->AddItem(new BMenuItem("Element podmenu",new BMessage('actn')));menu->AddItem(sub);
    bar->AddItem(menu);AddChild(bar);
    menu->ItemAt(0)->SetTarget(this);sub->ItemAt(0)->SetTarget(this);
    auto* field=new BTextControl(BRect(20,70,460,100),"field","Pole testowe","",nullptr);
    AddChild(field);field->MakeFocus(true);
    }
    void MessageReceived(BMessage* message) override {
        if(message->what=='actn') {++invoked;return;}
        if(message->what=='stat') {BMessage reply(B_REPLY);reply.AddInt32("count",invoked);message->SendReply(&reply);return;}
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
    BApplication app(argc==2 ? "application/x-vnd.NVDAHaiku.MenuActionFixtureCommand" : "application/x-vnd.NVDAHaiku.MenuActionFixture");
    if(argc==2) {
        BMessenger target(nullptr,atoi(argv[1]));BMessage query(B_GET_PROPERTY),listed;
        query.AddSpecifier("Windows");BMessenger window;
        if(target.SendMessage(&query,&listed,250000,250000)!=B_OK || listed.FindMessenger("result",&window)!=B_OK)return 2;
        BMessage request('stat'),reply;
        if(window.SendMessage(&request,&reply,250000,500000)!=B_OK)return 3;
        printf("%d\n",int(reply.GetInt32("count",-1)));return 0;
    }
    (new MenuWindow())->Show();app.Run();
}
