// r410 (session 4): boot messages that wait for a quiet screen - CharonMessageQueue.h.
// The recorded case is FreeBSD's loader started from a CD on a loaded machine (trace of
// a reader-trace build, 2026-11-24): its early console text is drawn for seconds
// without a quiet moment, then the screen is cleared and the menu window is drawn.
// Before the fix the menu was appended behind the waiting early text, or dropped when
// the 1600-character buffer was full.
#include "CharonMessageQueue.h"
#include <cstdio>
#include <string>
#include <vector>
using namespace hakefi;
static int checks=0,failed=0;
#define CHECK(c) do {++checks;if(!(c)) {++failed;std::printf("FAIL line %d: %s\n",__LINE__,#c);}} while(0)

// The reader's side of the queue, as CharonResident.h does it: QueueMessage, ClearShadow, the quiet timer.
struct Reader {
    static constexpr size_t capacity=1600;
    std::u16string pending;bool gone=false;
    bool everything=true;   // H4: the profile reads everything in order (beginner); false: normal and expert
    std::vector<std::u16string> spoken,history;   // the history holds what was spoken and what was only filed
    bool Alarm() const {for(auto* w:{u"error",u"warning",u"fail",u"cannot"})if(pending.find(w)!=std::u16string::npos)return true;return false;}
    void Send() {if(pending.empty()) {gone=false;return;}spoken.push_back(pending);history.push_back(pending);pending.clear();gone=false;}
    void Info(const std::u16string& info) {   // Flush found new text
        if(messagequeue::Next(pending.c_str(),capacity,info.c_str(),gone)==messagequeue::Step::sendFirst) {
            if(gone && messagequeue::Replaced(everything,Alarm())==messagequeue::Vanished::historyOnly) {history.push_back(pending);pending.clear();}
            else Send();
        }
        gone=false;
        if(!pending.empty())pending+=u". ";   // the separator (at most separatorRoom characters)
        for(char16_t c:info)if(pending.size()+1<capacity)pending.push_back(c);
    }
    void Clear() {if(!pending.empty())gone=true;}     // ClearShadow
    void Timer(bool quiet) {if(!pending.empty() && ((gone && messagequeue::SendWhenCleared(everything,Alarm())) || quiet))Send();}
};
static bool Has(const std::u16string& text,const char16_t* part) {return text.find(part)!=std::u16string::npos;}

// The flushes of the recorded run, in order (the rows each Flush read).
static const char16_t* early[]={
    u"Consoles: EFI console. Reading loader env vars from /efi/freebsd/loader.env. Setting currdev to cd1: Staging area's size is reduced: 16384 -> 1536! FreeBSD/amd64 EFI loader, Revi",
    u"FreeBSD/amd64 EFI loader, Revision 3.0. Command line arguments: loader.efi. Image base: 0x719c5000. EFI version: 2.70. EFI Firmware: EDK II (rev 1.00). Console: efi (0x20000000). Load Path: \\EFI\\BOOT\\B",
    u"Load Path: \\EFI\\BOOT\\BOOTX64.EFI. Load Device: PciRoot(0x0)/Pci(0x1F,0x2)/Sata(0x4,0xFFFF,0x0)/CDROM(0x1). BootCurrent: 0001. BootOrder: 0001[*] 0000 0002. BootInfo Path: PciRoot(0x0)/Pci(0x1F,0x2)/Sata(0x0,0",
    u"BootInfo Path: PciRoot(0x0)/Pci(0x1F,0x2)/Sata(0x0,0xFFFF,0x0). Ignoring Boot0001: Only one DP found. Trying ESP: PciRoot(0x0)/Pci(0x1F,0x2)/Sata(0x4,0xFFFF,0x0)/CDROM(0x1). Setting currdev to cd1: Trying: PciRoot(0x0)/Pci(0x1F,0x2)/Sata(0x4,0xFFFF,0x0",
    u"Trying: PciRoot(0x0)/Pci(0x1F,0x2)/Sata(0x4,0xFFFF,0x0)/CDROM(0x0). Setting currdev to cd0: Loading /boot/defaults/loader.conf. Loading /boot/defaults/loader.conf. Loading /boot/device.hints. Loading /boot/loader.conf. Loading /boot/loader.conf.d/rtw88.conf. Loading /boot/loader.conf.local"};
static const char16_t* menu=u"Okno: Welcome to FreeBSD. 1. Boot Installer [Enter]. 2. Boot Single user. 3. Escape to loader prompt. 4. Reboot. 5. Cons: Dual (Video primary). Options: 6. Kernel: default/kernel (1 of 1). 7. Boot Options. Autoboot in 10 seconds. [Space] to pause";

int main() {
    using messagequeue::Next;using messagequeue::Step;
    // the rule itself
    CHECK(Next(u"",1600,u"new",false)==Step::append);
    CHECK(Next(u"",1600,u"new",true)==Step::append);          // nothing waits: nothing to send
    CHECK(Next(nullptr,1600,u"new",true)==Step::append);
    CHECK(Next(u"old",1600,u"new",false)==Step::append);
    CHECK(Next(u"old",1600,u"new",true)==Step::sendFirst);    // its screen was cleared
    CHECK(Next(u"old",12,u"new",false)==Step::append);        // 3 + 4 + 3 + 1 = 11 fits in 12
    CHECK(Next(u"old",11,u"new",false)==Step::append);
    CHECK(Next(u"old",10,u"new",false)==Step::sendFirst);     // no room: the old text goes out first
    {std::u16string big(1500,u'a'),more(200,u'b');CHECK(Next(big.c_str(),1600,more.c_str(),false)==Step::sendFirst);}

    // 1. A fast machine: the reader's first look at the screen already shows the menu. One message.
    {Reader r;r.Info(menu);r.Timer(true);
     CHECK(r.spoken.size()==1);CHECK(r.spoken.size()==1 && Has(r.spoken[0],u"Welcome to FreeBSD") && Has(r.spoken[0],u"[Space] to pause"));}

    // 2. The recorded slow run: five flushes of early text with the screen never quiet, the clear, the menu.
    {Reader r;
     for(auto* part:early) {r.Info(part);r.Timer(false);}
     CHECK(r.spoken.empty());                 // still collecting: the screen was never quiet
     r.Clear();r.Timer(false);                // the loader clears the screen for its menu
     CHECK(r.spoken.size()==1);               // the early text is sent at once ...
     CHECK(r.spoken.size()==1 && Has(r.spoken[0],u"Consoles: EFI console"));
     r.Info(menu);r.Timer(false);r.Timer(true);
     CHECK(r.spoken.size()==2);               // ... and the menu window is its own message
     CHECK(r.spoken.size()==2 && Has(r.spoken[1],u"Okno: Welcome to FreeBSD") && Has(r.spoken[1],u"[Space] to pause"));
     CHECK(r.spoken.size()==2 && !Has(r.spoken[1],u"Consoles"));}

    // 3. As 2, but the menu arrives in the same flush as the clear (no timer tick between them).
    {Reader r;
     for(auto* part:early)r.Info(part);
     r.Clear();r.Info(menu);r.Timer(true);
     CHECK(r.spoken.size()==2);
     CHECK(r.spoken.size()==2 && Has(r.spoken[1],u"Okno: Welcome to FreeBSD") && Has(r.spoken[1],u"[Space] to pause"));}

    // 4. No clear at all (a program that scrolls): text keeps coming without a quiet moment until the buffer
    //    is full. Nothing may be lost: every character put in comes out, in order.
    {Reader r;size_t in=0;std::u16string last;
     for(int round=0;round<4;++round)for(auto* part:early) {r.Info(part);in+=messagequeue::Length(part);last=part;}
     r.Info(menu);in+=messagequeue::Length(menu);r.Timer(true);
     size_t out=0;for(auto& m:r.spoken) {CHECK(m.size()<Reader::capacity);size_t separators=0;for(size_t i=0;i+1<m.size();++i)if(m[i]==u'.' && m[i+1]==u' ')++separators;out+=m.size();(void)separators;}
     CHECK(r.spoken.size()>=3);
     CHECK(Has(r.spoken.back(),u"[Space] to pause"));   // the newest text is there in full
     // all parts are found, whole, in some message
     bool all=true;for(auto* part:early) {bool found=false;for(auto& m:r.spoken)if(Has(m,part))found=true;all=all && found;}
     CHECK(all);CHECK(out>=in);}

    // 5. Before the fix (plain appending with the screen never quiet): the menu's end is cut off. This is the
    //    defect; the check documents it.
    {std::u16string pending;
     auto add=[&](const char16_t* t) {if(!pending.empty())pending+=u". ";for(const char16_t* p=t;*p;++p)if(pending.size()+1<Reader::capacity)pending.push_back(*p);};
     for(int round=0;round<2;++round)for(auto* part:early)add(part);
     add(menu);
     CHECK(!Has(pending,u"[Space] to pause"));}

    // ---- H4 (user, fourth set): vanished early text by profile ----
    using messagequeue::Replaced;using messagequeue::SendWhenCleared;using messagequeue::Vanished;
    CHECK(Replaced(true,false)==Vanished::speak);CHECK(Replaced(false,false)==Vanished::historyOnly);
    CHECK(Replaced(false,true)==Vanished::speak);CHECK(Replaced(true,true)==Vanished::speak);
    CHECK(SendWhenCleared(true,false));CHECK(!SendWhenCleared(false,false));CHECK(SendWhenCleared(false,true));

    // 6. The recorded slow run in the NORMAL (or expert) profile: the early text is not spoken; the menu is the
    //    first and only message; the early text is in the history, before the menu, whole.
    {Reader r;r.everything=false;
     for(auto* part:early) {r.Info(part);r.Timer(false);}
     r.Clear();r.Timer(false);
     CHECK(r.spoken.empty());                 // nothing is said at the clear
     r.Info(menu);r.Timer(false);r.Timer(true);
     CHECK(r.spoken.size()==1);
     CHECK(r.spoken.size()==1 && Has(r.spoken[0],u"Okno: Welcome to FreeBSD") && Has(r.spoken[0],u"[Space] to pause") && !Has(r.spoken[0],u"Consoles"));
     CHECK(r.history.size()==2);
     CHECK(r.history.size()==2 && Has(r.history[0],u"Consoles: EFI console") && Has(r.history[0],u"Loading /boot/loader.conf.local"));
     CHECK(r.history.size()==2 && Has(r.history[1],u"Welcome to FreeBSD"));}

    // 7. As 6 with the menu in the same flush as the clear.
    {Reader r;r.everything=false;
     for(auto* part:early)r.Info(part);
     r.Clear();r.Info(menu);r.Timer(true);
     CHECK(r.spoken.size()==1 && Has(r.spoken[0],u"[Space] to pause"));CHECK(r.history.size()==2);}

    // 8. Normal profile, the screen is cleared and NOTHING replaces the text: once the screen is quiet it is
    //    spoken after all (nothing is lost without a word).
    {Reader r;r.everything=false;
     r.Info(early[0]);r.Clear();r.Timer(false);
     CHECK(r.spoken.empty());
     r.Timer(true);
     CHECK(r.spoken.size()==1 && Has(r.spoken[0],u"Consoles: EFI console"));}

    // 9. Normal profile, the vanished text is an ERROR (answer 11): sent at the clear, read before the menu.
    {Reader r;r.everything=false;
     r.Info(u"error: file /boot/kernel not found. Press any key");r.Timer(false);
     r.Clear();r.Timer(false);
     CHECK(r.spoken.size()==1 && Has(r.spoken[0],u"error: file"));
     r.Info(menu);r.Timer(true);
     CHECK(r.spoken.size()==2 && Has(r.spoken[1],u"Welcome to FreeBSD"));}
    // ... and when the menu comes in the same flush as the clear, the error is still spoken first
    {Reader r;r.everything=false;
     r.Info(u"warning: staging area reduced");r.Clear();r.Info(menu);r.Timer(true);
     CHECK(r.spoken.size()==2 && Has(r.spoken[0],u"warning: staging") && Has(r.spoken[1],u"Welcome to FreeBSD"));}

    // 10. The beginner's profile is cases 2 and 3 (everything = true): early text spoken, then the menu.
    // 11. Normal profile, no clear, no room (case 4): still nothing lost and all of it SPOKEN - only cleared
    //     text goes to the history alone.
    {Reader r;r.everything=false;
     for(int round=0;round<4;++round)for(auto* part:early)r.Info(part);
     r.Info(menu);r.Timer(true);
     bool all=true;for(auto* part:early) {bool found=false;for(auto& m:r.spoken)if(Has(m,part))found=true;all=all && found;}
     CHECK(all);CHECK(r.spoken.size()==r.history.size());}

    std::printf("CharonMessageQueueTests: %d checks, %d failed\n",checks,failed);
    return failed?1:0;
}
