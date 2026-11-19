// Test-only UEFI application for Charon's reader (r397): firmware setup
// screens in the text styles of three vendors that QEMU cannot run -
// Lenovo ThinkPad Setup, InsydeH2O and Phoenix SecureCore (original test
// content; only the layout conventions are imitated: tab bar, item column,
// value column with [values], greyed-out items, "►" sub-menus, item help
// panel, two key-hint lines, a Yes/No confirmation box and a password box).
// Keys: Left/Right tabs, Up/Down items, +/- or Space change a value, Enter
// opens (sub-menu marker / password box), F10 asks "Save changes and exit?",
// V switches the vendor style, Escape closes a box or leaves.
// Drawn only through the console. GPL-2.0-or-later.
#include <efi/system-table.h>
extern "C" void* memset(void* d,int v,size_t n){auto* p=(unsigned char*)d;while(n--)*p++=(unsigned char)v;return d;}
extern "C" void* memcpy(void* d,const void* s,size_t n){auto* a=(unsigned char*)d;auto* b=(const unsigned char*)s;while(n--)*a++=*b++;return d;}
static efi_system_table* st;
enum : size_t {black=0,blue=1,green=2,cyan=3,red=4,lightgray=7,darkgray=8,lightblue=9,white=15,yellow=14};
static size_t Attr(size_t fg,size_t bg) {return fg|(bg<<4);}
static void At(size_t col,size_t row,const char16_t* text,size_t attr) {
    st->ConOut->SetAttribute(st->ConOut,attr);st->ConOut->SetCursorPosition(st->ConOut,col,row);
    st->ConOut->OutputString(st->ConOut,(char16_t*)text);
}
static void Fill(size_t row,size_t from,size_t to,size_t attr) {
    static char16_t line[200];size_t n=0;for(size_t c=from;c<to;++c)line[n++]=u' ';line[n]=0;At(from,row,line,attr);
}
static size_t Len(const char16_t* s) {size_t n=0;while(s && s[n])++n;return n;}
// An item: name, its values (nullptr: a sub-menu or an action), the chosen value, help, greyed out.
struct Item {const char16_t* name;const char16_t* const* values;size_t value;const char16_t* help;bool grey;bool password;};
static const char16_t* onOff[]={u"Enabled",u"Disabled",nullptr};
static const char16_t* bootMode[]={u"UEFI Only",u"Both",u"Legacy Only",nullptr};
static const char16_t* sata[]={u"AHCI",u"RAID",nullptr};
static Item lenovoMain[]={{u"UEFI BIOS Version",nullptr,0,u"Version of the UEFI BIOS.",true,false},{u"Embedded Controller Version",nullptr,0,u"Version of the controller.",true,false},
    {u"Machine Type Model",nullptr,0,u"Machine type and model.",true,false}};
static Item lenovoConfig[]={{u"Network",nullptr,0,u"Configure network functions.",false,false},{u"USB",nullptr,0,u"Configure USB functions.",false,false},
    {u"Wake On LAN",onOff,0,u"Wake the computer from the network.",false,false},{u"Intel AMT",onOff,1,u"Not available on this model.",true,false},
    {u"SATA Controller Mode",sata,0,u"Select the SATA mode.",false,false}};
static Item lenovoSecurity[]={{u"Supervisor Password",nullptr,0,u"Set the supervisor password.",false,true},{u"Secure Boot",nullptr,0,u"Secure Boot settings.",false,false}};
static Item lenovoStartup[]={{u"Boot",nullptr,0,u"Set the boot priority order.",false,false},{u"UEFI/Legacy Boot",bootMode,0,u"Select the boot mode.",false,false},
    {u"Boot Mode",onOff,0,u"Quick or diagnostic boot.",false,false}};
struct Tab {const char16_t* name;Item* items;size_t count;};
static Tab lenovoTabs[]={{u"Main",lenovoMain,3},{u"Config",lenovoConfig,5},{u"Security",lenovoSecurity,2},{u"Startup",lenovoStartup,3},{u"Restart",lenovoMain,0}};
static Item insydeInfo[]={{u"Language",nullptr,0,u"Select the setup language.",false,false},{u"BIOS Version",nullptr,0,u"",true,false},{u"CPU Type",nullptr,0,u"",true,false}};
static Item insydeAdvanced[]={{u"Boot Configuration",nullptr,0,u"Configure settings during boot.",false,false},{u"Virtualization Technology",onOff,0,u"Hardware assisted virtualization.",false,false},
    {u"Fast Boot",onOff,1,u"Skip some devices at start.",false,false}};
static Item insydeSecurity[]={{u"Set Supervisor Password",nullptr,0,u"Install or change the password.",false,true},{u"TPM Device",onOff,0,u"Trusted Platform Module.",false,false}};
static Item insydeBoot[]={{u"Boot Mode",bootMode,0,u"Select the boot mode.",false,false},{u"Secure Boot",onOff,0,u"Secure Boot.",false,false},{u"PXE Boot to LAN",onOff,1,u"Network boot.",false,false}};
static Tab insydeTabs[]={{u"Information",insydeInfo,3},{u"Main",insydeInfo,1},{u"Advanced",insydeAdvanced,3},{u"Security",insydeSecurity,2},{u"Boot",insydeBoot,3},{u"Exit",insydeInfo,0}};
static Item phoenixMain[]={{u"System Time",nullptr,0,u"Set the time.",false,false},{u"System Date",nullptr,0,u"Set the date.",false,false},
    {u"Legacy Diskette A",onOff,1,u"Diskette drive type.",true,false},{u"Quick Boot",onOff,0,u"Skip memory tests.",false,false}};
static Item phoenixAdvanced[]={{u"Processor Options",nullptr,0,u"CPU options.",false,false},{u"Integrated Peripherals",nullptr,0,u"Onboard devices.",false,false},{u"Plug & Play O/S",onOff,1,u"Let the OS configure devices.",false,false}};
static Item phoenixSecurity[]={{u"Set Supervisor Password",nullptr,0,u"Supervisor password controls access to the setup.",false,true},{u"Password on boot",onOff,1,u"Ask for the password at start.",false,false}};
static Tab phoenixTabs[]={{u"Main",phoenixMain,4},{u"Advanced",phoenixAdvanced,3},{u"Security",phoenixSecurity,2},{u"Boot",insydeBoot,3},{u"Exit",phoenixMain,0}};
struct Vendor {const char16_t* title;Tab* tabs;size_t tabCount;size_t screen,bar,tabOn,item,itemOn,value,grey,greyOn,box;const char16_t* keys1;const char16_t* keys2;size_t helpColumn;};
static const Vendor vendors[]={
    {u"ThinkPad Setup",lenovoTabs,5,Attr(blue,lightgray),Attr(white,blue),Attr(blue,lightgray),Attr(blue,lightgray),Attr(white,blue),Attr(blue,lightgray),Attr(darkgray,lightgray),Attr(darkgray,cyan),Attr(white,blue),
     u"F1 Help  \x2191\x2193 Select Item   +/- Change Values     F9  Setup Defaults",u"Esc Exit \x2194  Select Menu   Enter Select \x25ba Sub-Menu F10 Save and Exit",50},
    {u"InsydeH2O Setup Utility                                Rev. 5.0",insydeTabs,6,Attr(lightgray,blue),Attr(white,cyan),Attr(white,blue),Attr(white,blue),Attr(blue,lightgray),Attr(yellow,blue),Attr(darkgray,blue),Attr(darkgray,lightgray),Attr(black,lightgray),
     u"F1  Help     \x2191/\x2193 Select Item  F5/F6 Change Values    F9  Setup Defaults",u"Esc Exit     \x2190/\x2192 Select Menu  Enter Select \x25ba SubMenu F10 Save and Exit",52},
    {u"PhoenixBIOS Setup Utility",phoenixTabs,5,Attr(blue,lightgray),Attr(lightgray,blue),Attr(blue,lightgray),Attr(blue,lightgray),Attr(white,blue),Attr(blue,lightgray),Attr(darkgray,lightgray),Attr(darkgray,cyan),Attr(white,red),
     u"F1  Help  \x2191\x2193 Select Item  -/+   Change Values      F9   Setup Defaults",u"Esc Exit  \x2194  Select Menu  Enter Select \x25ba Sub-Menu  F10  Save and Exit",52}};
static size_t vendor=0;
static void Draw(size_t tab,size_t item) {
    const Vendor& v=vendors[vendor];
    st->ConOut->SetAttribute(st->ConOut,v.screen);st->ConOut->ClearScreen(st->ConOut);
    Fill(0,0,80,v.bar);At((80-Len(v.title))/2,0,v.title,v.bar);
    Fill(1,0,80,v.bar);
    size_t col=2;
    for(size_t t=0;t<v.tabCount;++t) {At(col,1,v.tabs[t].name,t==tab?v.tabOn:v.bar);col+=Len(v.tabs[t].name)+3;}
    for(size_t r=3;r<21;++r)At(v.helpColumn-1,r,u"\x2502",v.screen);
    At(v.helpColumn+1,3,u"Item Specific Help",v.screen);
    const Tab& t=v.tabs[tab];
    for(size_t i=0;i<t.count;++i) {
        const Item& it=t.items[i];const bool on=i==item;
        const size_t a=it.grey?(on?v.greyOn:v.grey):(on?v.itemOn:v.item);
        if(!it.values && !it.grey && !it.password)At(2,4+i,u"\x25ba",v.item);
        At(4,4+i,it.name,a);
        if(it.values) {static char16_t val[40];size_t n=0;val[n++]=u'[';for(const char16_t* s=it.values[it.value];*s;++s)val[n++]=*s;val[n++]=u']';val[n]=0;At(32,4+i,val,it.grey?v.grey:on?v.itemOn:v.value);}
    }
    if(t.count && item<t.count) {static char16_t help[28];size_t n=0;for(const char16_t* h=t.items[item].help;*h && n<26;++h)help[n++]=*h;help[n]=0;At(v.helpColumn+1,5,help,v.screen);}
    Fill(22,0,80,v.bar);At(1,22,v.keys1,v.bar);Fill(23,0,80,v.bar);At(1,23,v.keys2,v.bar);
}
static void Box(const char16_t* title,const char16_t* line,size_t top) {
    const Vendor& v=vendors[vendor];
    At(20,top,u"\x250c\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2510",v.box);
    for(size_t r=top+1;r<top+5;++r) {Fill(r,20,60,v.box);At(20,r,u"\x2502",v.box);At(59,r,u"\x2502",v.box);}
    At(20,top+5,u"\x2514\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2518",v.box);
    At(40-Len(title)/2,top,title,v.box);At(22,top+2,line,v.box);
}
static void Buttons(size_t focus,size_t row) {
    const Vendor& v=vendors[vendor];const size_t inverse=(((v.box&7))<<4)|((v.box>>4)&7);   // UEFI backgrounds are 0..7: the foreground's low bits
    At(30,row,u"[Yes]",focus==0?inverse:v.box);At(44,row,u"[No]",focus==1?inverse:v.box);
}
extern "C" efi_status EFIAPI efi_main(efi_handle,efi_system_table* table) {
    st=table;st->ConOut->EnableCursor(st->ConOut,false);
    size_t tab=0,item=0;Draw(tab,item);
    enum class Mode {page,confirm,password} mode=Mode::page;size_t focus=0,typed=0;
    for(;;) {
        efi_input_key key{};size_t index;
        st->BootServices->WaitForEvent(1,&st->ConIn->WaitForKey,&index);
        if(st->ConIn->ReadKeyStroke(st->ConIn,&key)!=EFI_SUCCESS)continue;
        const Vendor& v=vendors[vendor];Tab& t=v.tabs[tab];
        if(mode==Mode::confirm) {
            if(key.ScanCode==3 || key.ScanCode==4 || key.UnicodeChar==9) {focus^=1;Buttons(focus,9);}
            else if(key.ScanCode==23 || key.UnicodeChar==13) {mode=Mode::page;Draw(tab,item);}
            continue;
        }
        if(mode==Mode::password) {
            if(key.ScanCode==23 || key.UnicodeChar==13) {mode=Mode::page;st->ConOut->EnableCursor(st->ConOut,false);Draw(tab,item);continue;}
            if(key.UnicodeChar>=32 && typed<12) {At(32+typed,10,u"*",vendors[vendor].box);++typed;}
            else if(key.UnicodeChar==8 && typed) {--typed;At(32+typed,10,u" ",vendors[vendor].box);st->ConOut->SetCursorPosition(st->ConOut,32+typed,10);}
            continue;
        }
        if(key.ScanCode==23)break;   // Escape
        if(key.UnicodeChar==u'v' || key.UnicodeChar==u'V') {vendor=(vendor+1)%3;tab=0;item=0;Draw(tab,item);continue;}
        if(key.ScanCode==0x14) {Box(u" Setup Confirmation ",u"Save configuration changes and exit now?",6);focus=0;Buttons(focus,9);mode=Mode::confirm;continue;}   // F10
        if(key.ScanCode==3) {tab=(tab+1)%v.tabCount;item=0;}
        else if(key.ScanCode==4) {tab=(tab+v.tabCount-1)%v.tabCount;item=0;}
        else if(key.ScanCode==2 && item+1<t.count)++item;
        else if(key.ScanCode==1 && item)--item;
        else if(t.count && (key.UnicodeChar==u'+' || key.UnicodeChar==u'-' || key.UnicodeChar==u' ' || key.ScanCode==0x0f || key.ScanCode==0x10) && t.items[item].values && !t.items[item].grey) {
            Item& it=t.items[item];size_t n=0;while(it.values[n])++n;it.value=(it.value+1)%n;}
        else if(t.count && key.UnicodeChar==13 && t.items[item].password) {
            Box(u" Set Supervisor Password ",u"Enter New Password",6);At(30,10,u"[            ]",vendors[vendor].box);typed=0;
            st->ConOut->SetCursorPosition(st->ConOut,32,10);st->ConOut->EnableCursor(st->ConOut,true);mode=Mode::password;continue;}
        else continue;
        Draw(tab,item);
    }
    st->ConOut->SetAttribute(st->ConOut,Attr(lightgray,black));st->ConOut->ClearScreen(st->ConOut);
    return EFI_SUCCESS;
}
