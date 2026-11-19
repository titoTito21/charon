// Isolated transport probe, not an installed provider endpoint.
#include "../src/HaikuMenuEvents.h"
#include <Window.h>
#include <Menu.h>
#include <Application.h>
#include <Roster.h>
#include <dlfcn.h>
void BWindow::DispatchMessage(BMessage* message,BHandler* target) {
    using Function=void(*)(BWindow*,BMessage*,BHandler*);
    static Function original=reinterpret_cast<Function>(dlsym(RTLD_NEXT,"_ZN7BWindow15DispatchMessageEP8BMessageP8BHandler"));
    if(message->what=='nmrd' || message->what=='nmva') {
        BMessage reply(B_REPLY);app_info active;
        status_t status=be_roster->GetActiveAppInfo(&active);
        if(status==B_OK && (!be_app || active.team!=be_app->Team()))status=B_NOT_ALLOWED;
        if(status==B_OK) {
            if(message->what=='nmrd')status=ReadHaikuMenuEvents(message->GetInt64("after",-3),reply);
            else status=ReadHaikuMenuSelection(dynamic_cast<BMenu*>(target),message->GetInt64("selection_token",0),reply);
        }
        if(status!=B_OK)reply.MakeEmpty();
        reply.AddInt32("error",status);message->SendReply(&reply);return;
    }
    if(original)original(this,message,target);
}
