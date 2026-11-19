#include <Application.h>
#include <Messenger.h>
#include <Message.h>
#include <cstdlib>
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    BApplication app("application/x-vnd.NVDAHaiku.MenuEventProbe");
    BMessenger target(nullptr,atoi(argv[1]));BMessage query(B_GET_PROPERTY),listed;
    query.AddSpecifier("Windows");BMessenger window;
    if(target.SendMessage(&query,&listed,250000,250000)!=B_OK || listed.FindMessenger("result",&window)!=B_OK)return 3;
    BMessage request('nmrd'),reply;request.AddInt64("after",strtoll(argv[2],nullptr,10));
    if(window.SendMessage(&request,&reply,250000,500000)!=B_OK)return 4;
    printf("status=%ld sequence=%lld\n",long(reply.GetInt32("error",B_ERROR)),static_cast<long long>(reply.GetInt64("sequence",-1)));
    BMessage event;
    for(int32 i=0;reply.FindMessage("event",i,&event)==B_OK;++i) {
        BMessenger menu;
        if(event.FindMessenger("menu",&menu)!=B_OK || menu.Team()!=atoi(argv[1]))return 5;
        printf("%lld %d %d %d %s %s\n",static_cast<long long>(event.GetInt64("sequence",0)),
            int(event.GetBool("selected",false)),int(event.GetBool("marked",false)),
            int(event.GetBool("submenu",false)),event.GetString("role_id",""),event.GetString("name",""));
    }
    return reply.GetInt32("error",B_ERROR)==B_OK ? 0 : 6;
}
