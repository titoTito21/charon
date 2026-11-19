#include <Application.h>
#include <Messenger.h>
#include <Message.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    BApplication app("application/x-vnd.NVDAHaiku.MenuIdentityProbe");
    team_id team=atoi(argv[1]);int64 token=strtoll(argv[2],nullptr,10);
    bool valid=strcmp(argv[3],"valid")==0;
    bool wrong=strcmp(argv[3],"wrong-target")==0;
    BMessenger target(nullptr,team);BMessage query(B_GET_PROPERTY),listed;
    query.AddSpecifier("Windows");BMessenger window;
    if(target.SendMessage(&query,&listed,250000,250000)!=B_OK || listed.FindMessenger("result",&window)!=B_OK)return 3;
    BMessage request('nmrd'),history;request.AddInt64("after",0);
    if(window.SendMessage(&request,&history,250000,500000)!=B_OK || history.GetInt32("error",B_ERROR)!=B_OK)return 4;
    BMessage event;BMessenger menu;bool found=false;
    for(int32 i=0;history.FindMessage("event",i,&event)==B_OK;++i) {
        if(event.GetInt64("selection_token",0)==token && event.GetBool("selected",false)) {
            found=event.FindMessenger("menu",&menu)==B_OK;break;
        }
    }
    if(!found || menu.Team()!=team)return 5;
    BMessage validate('nmva'),reply;validate.AddInt64("selection_token",token);
    status_t transport=(wrong ? window : menu).SendMessage(&validate,&reply,250000,500000);
    status_t status=transport==B_OK ? reply.GetInt32("error",B_ERROR) : transport;
    if(valid) {
        BMessenger returned;
        if(status!=B_OK || reply.GetInt64("selection_token",0)!=token
            || strcmp(reply.GetString("name",""),event.GetString("name",""))
            || reply.FindMessenger("menu",&returned)!=B_OK || returned!=menu
            || !reply.GetBool("selected",false))return 6;
        printf("PASS live menu selection token %lld name=%s\n",static_cast<long long>(token),reply.GetString("name",""));
    } else {
        if(status==B_OK || reply.HasString("name") || reply.HasString("role_id"))return 7;
        printf("PASS stale/wrong-target menu token %lld denied without properties (%ld)\n",static_cast<long long>(token),long(status));
    }
}
