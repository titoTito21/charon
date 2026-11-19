#include <Application.h>
#include <Window.h>
#include <MenuField.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Message.h>
#include <Messenger.h>
#include <cstdlib>
class TestWindow : public BWindow {
    BMenuField* field;
public:
    TestWindow():BWindow(BRect(80,80,500,240),"Menu field fixture",B_TITLED_WINDOW,B_QUIT_ON_WINDOW_CLOSE) {
        auto* menu=new BPopUpMenu("choices");
        menu->AddItem(new BMenuItem("Pierwszy żółw",nullptr));
        menu->AddItem(new BMenuItem("Drugi element",nullptr));
        menu->ItemAt(0)->SetMarked(true);
        field=new BMenuField(BRect(10,10,390,45),"choice","Wybór:",menu);
        AddChild(field);field->MakeFocus(true);
    }
    void MessageReceived(BMessage* message) override {
        if(message->what!='chng') {BWindow::MessageReceived(message);return;}
        int32 index=message->GetInt32("index",-1);
        BMessage reply(B_REPLY);status_t status=B_OK;
        if(index==0 || index==1)field->Menu()->ItemAt(index)->SetMarked(true);
        else if(index==2)field->MenuBar()->ItemAt(0)->SetLabel("Własny żółw");
        else status=B_BAD_VALUE;
        reply.AddInt32("error",status);message->SendReply(&reply);
    }
};
int main(int argc,char** argv) {
    BApplication app(argc==3 ? "application/x-vnd.NVDAHaiku.MenuFieldCommand" : "application/x-vnd.NVDAHaiku.MenuFieldFixture");
    if(argc==3) {
        BMessage query(B_GET_PROPERTY),listed;query.AddSpecifier("Windows");BMessenger window;
        if(BMessenger(nullptr,atoi(argv[1])).SendMessage(&query,&listed,250000,500000)!=B_OK
            || listed.FindMessenger("result",&window)!=B_OK)return 2;
        BMessage request('chng'),reply;request.AddInt32("index",atoi(argv[2]));
        if(window.SendMessage(&request,&reply,250000,500000)!=B_OK)return 3;
        return reply.GetInt32("error",B_ERROR)==B_OK ? 0 : 4;
    }
    (new TestWindow())->Show();app.Run();
}
