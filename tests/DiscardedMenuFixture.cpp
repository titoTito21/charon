// Owned protocol fixture: deliberately discarded selection replies.
#include "../src/ProviderProtocol.h"
#include <Application.h>
#include <Window.h>
#include <Messenger.h>
#include <Message.h>
#include <cstdlib>
#include <cstring>
class TestWindow : public BWindow {
    bool selected=true;
    bool malformed=false;
    bool delayed=false;
    BLooper* endpoint=nullptr;
    BMessenger menu;
public:
    TestWindow(bool separate):BWindow(BRect(80,80,360,180),"Discarded menu fixture",
        B_TITLED_WINDOW,B_QUIT_ON_WINDOW_CLOSE) {
        if(separate) {endpoint=new BLooper("owned menu endpoint");endpoint->Run();menu=BMessenger(endpoint);}
        else menu=BMessenger(this);
    }
    void MessageReceived(BMessage* message) override {
        BMessage reply(B_REPLY);
        if(message->what==HaikuAccess::kMenuSelection) {
            if(delayed)snooze(650000);
            BMessage discarded(B_NO_REPLY);message->SendReply(&discarded);return;
        }
        if(message->what=='dead') {
            if(endpoint && endpoint->Lock()) {endpoint->Quit();endpoint=nullptr;}
        } else if(message->what=='drop' || message->what=='badj' || message->what=='slow') {
            if(message->what=='drop')selected=false;
            else if(message->what=='slow')delayed=true;
            else malformed=true;
        } else if(message->what==HaikuAccess::kMenuEvents) {
            reply.AddInt64("sequence",selected ? 1 : 2);
            if(selected) {
                BMessage event('nmev');event.AddInt64("sequence",1);
                event.AddInt64("selection_token",1);event.AddString("role_id","MENUITEM");
                event.AddString("name","Owned selection");event.AddBool("selected",true);
                event.AddBool("marked",false);event.AddBool("enabled",true);
                event.AddBool("submenu",false);event.AddInt32("index",0);
                event.AddMessenger("menu",menu);reply.AddMessage("event",&event);
            }
            if(malformed)reply.RemoveName("sequence");
        } else {BWindow::MessageReceived(message);return;}
        reply.AddInt32("error",B_OK);message->SendReply(&reply);
    }
};
int main(int argc,char** argv) {
    BApplication app(argc==3 ? "application/x-vnd.NVDAHaiku.DiscardedMenuCommand"
        : "application/x-vnd.NVDAHaiku.DiscardedMenuFixture");
    if(argc==3) {
        BMessage query(B_GET_PROPERTY),listed;query.AddSpecifier("Windows");
        BMessenger window;
        if(BMessenger(nullptr,atoi(argv[1])).SendMessage(&query,&listed,250000,250000)!=B_OK
            || listed.FindMessenger("result",&window)!=B_OK)return 2;
        if(strcmp(argv[2],"drop") && strcmp(argv[2],"badj") && strcmp(argv[2],"slow") && strcmp(argv[2],"dead"))return 3;
        BMessage request(!strcmp(argv[2],"drop") ? 'drop' : (!strcmp(argv[2],"slow") ? 'slow' : (!strcmp(argv[2],"dead") ? 'dead' : 'badj'))),reply;
        if(window.SendMessage(&request,&reply,250000,500000)!=B_OK)return 4;
        return reply.GetInt32("error",B_ERROR)==B_OK ? 0 : 5;
    }
    (new TestWindow(argc==2 && !strcmp(argv[1],"port")))->Show();app.Run();
}
