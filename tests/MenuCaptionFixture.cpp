#include <Application.h>
#include <Window.h>
#include <View.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ListView.h>
#include <ScrollView.h>
#include <ScrollBar.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

// Actual Interface Kit objects: localized command titles are captions; marked
// selection values and internal handler identifiers are not captions.
class FixtureScrollView : public BView {
public:
    FixtureScrollView() : BView(BRect(300,27,530,85),"custom-scroll",B_FOLLOW_NONE,0) {
        BListView* list=new BListView(BRect(0,0,210,58),"custom-list");
        AddChild(new BScrollBar(BRect(212,0,227,58),"bar",list,0,100,B_VERTICAL));
        AddChild(list);
    }
};
class CaptionWindow : public BWindow {
public:
    CaptionWindow() : BWindow(BRect(80,80,760,730),"HAK menu captions",B_TITLED_WINDOW,B_QUIT_ON_WINDOW_CLOSE) {
        for(int row=0;row<6;++row) {
            BView* pane=new BView(BRect(10,10+row*100,650,100+row*100),"row",B_FOLLOW_NONE,0);
            AddChild(pane);
            const bool dynamic=row==1 || row==4;
            BPopUpMenu* menu=new BPopUpMenu("Ścieżki",dynamic,dynamic);
            BMenuItem* item=new BMenuItem("Zmienna wartość",nullptr);
            menu->AddItem(item);if(dynamic)item->SetMarked(true);
            const char* id[]={"static","dynamic","explicit","tooltip","dynamic-list","hidden"};
            BMenuField* field=new BMenuField(BRect(0,0,220,25),id[row],row==2 ? "Jawna etykieta" : "",menu);
            pane->AddChild(field);
            if(row==3)field->SetToolTip("Pomoc menu");
            if(row==5)field->Hide();
            const char* listId[]={"static-list","value-list","explicit-list","tooltip-list","changing-list","hidden-list"};
            BListView* list=new BListView(BRect(0,27,200,85),listId[row]);
            pane->AddChild(new BScrollView("scroll",list,B_FOLLOW_NONE,0,false,true));
            if(row==0) {
                BMenu* commands=new BMenu("Właściwości");
                commands->AddItem(new BMenuItem("Polecenie",nullptr));
                pane->AddChild(new BMenuField(BRect(300,0,530,25),"custom-menu","",commands));
                pane->AddChild(new FixtureScrollView());
            }
            if(row>=2 && row<=4) {
                const bool changing=row==3;
                BPopUpMenu* category=new BPopUpMenu("Grupa",changing,changing);
                BMenuItem* selected=new BMenuItem("Wybór",nullptr);
                category->AddItem(selected);if(changing)selected->SetMarked(true);
                const char* menuId=row==2 ? "category-menu" : row==3 ? "changing-category" : "protected-category";
                pane->AddChild(new BMenuField(BRect(300,0,405,25),menuId,"",category));
                BTextControl* entry=new BTextControl(BRect(416,0,625,25),"entry","","",nullptr);
                pane->AddChild(entry);
                if(row==4)entry->TextView()->HideTyping(true);
            }
        }
    }
};
int main(){BApplication app("application/x-vnd.HAK.MenuCaptionFixture");(new CaptionWindow())->Show();app.Run();}
