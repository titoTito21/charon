// Test-only UEFI application for Charon's reader (r392): a firmware setup
// screen in the style of AMI Aptio text setup - a title, a tab bar (Main,
// Advanced, Chipset, Security, Boot, Save & Exit; the open tab in reverse
// colours), items with values, a help panel on the right and the key-hint
// panel. Left/Right switch tabs, Up/Down move the highlight, Escape exits.
// Drawn only through the console (text mode, no pictures). GPL-2.0-or-later.
#include <efi/system-table.h>
extern "C" void* memset(void* d,int v,size_t n){auto* p=(unsigned char*)d;while(n--)*p++=(unsigned char)v;return d;}
extern "C" void* memcpy(void* d,const void* s,size_t n){auto* a=(unsigned char*)d;auto* b=(const unsigned char*)s;while(n--)*a++=*b++;return d;}
static efi_system_table* st;
enum : size_t {black=0,blue=1,cyan=3,lightgray=7,white=15,yellow=14};
static size_t Attr(size_t fg,size_t bg) {return fg|(bg<<4);}
static void At(size_t col,size_t row,const char16_t* text,size_t attr) {
    st->ConOut->SetAttribute(st->ConOut,attr);st->ConOut->SetCursorPosition(st->ConOut,col,row);
    st->ConOut->OutputString(st->ConOut,(char16_t*)text);
}
static void Fill(size_t row,size_t from,size_t to,size_t attr) {
    static char16_t line[200];size_t n=0;for(size_t c=from;c<to;++c)line[n++]=u' ';line[n]=0;At(from,row,line,attr);
}
struct Item {const char16_t* name;const char16_t* value;const char16_t* help;};
static const Item mainItems[]={{u"BIOS Vendor",u"American Megatrends",u"BIOS information."},{u"System Date",u"[Wed 11/18/2026]",u"Set the date. Use Tab to switch between date elements."},
    {u"System Time",u"[14:05:32]",u"Set the time. Use Tab to switch between time elements."},{u"Access Level",u"Administrator",u"Current access level."}};
static const Item advancedItems[]={{u"CPU Configuration",nullptr,u"CPU Configuration Parameters"},{u"SATA Configuration",nullptr,u"SATA Device Options Settings"},
    {u"USB Configuration",nullptr,u"USB Configuration Parameters"},{u"Network Stack",u"[Disabled]",u"Enable/Disable UEFI Network Stack"}};
static const Item chipsetItems[]={{u"System Agent (SA) Configuration",nullptr,u"System Agent (SA) Parameters"},{u"PCH-IO Configuration",nullptr,u"PCH Parameters"}};
static const Item securityItems[]={{u"Administrator Password",nullptr,u"Set Administrator Password"},{u"User Password",nullptr,u"Set User Password"},{u"Secure Boot",nullptr,u"Secure Boot configuration"}};
static const Item bootItems[]={{u"Setup Prompt Timeout",u"1",u"Number of seconds to wait for setup activation key."},{u"Bootup NumLock State",u"[On]",u"Select the keyboard NumLock state"},
    {u"Quiet Boot",u"[Enabled]",u"Enables or disables Quiet Boot option"},{u"Boot Option #1",u"[Windows Boot Manager]",u"Sets the system boot order"},{u"Boot Option #2",u"[UEFI: USB Stick]",u"Sets the system boot order"}};
static const Item exitItems[]={{u"Save Changes and Exit",nullptr,u"Exit system setup after saving the changes."},{u"Discard Changes and Exit",nullptr,u"Exit system setup without saving any changes."},
    {u"Restore Defaults",nullptr,u"Restore/Load Default values for all the setup options."}};
struct Tab {const char16_t* name;const Item* items;size_t count;};
static const Tab tabs[]={{u"Main",mainItems,4},{u"Advanced",advancedItems,4},{u"Chipset",chipsetItems,2},{u"Security",securityItems,3},{u"Boot",bootItems,5},{u"Save & Exit",exitItems,3}};
constexpr size_t tabCount=6;
static void Draw(size_t tab,size_t item,bool all) {
    const size_t screen=Attr(blue,lightgray),bar=Attr(lightgray,blue);
    if(all) {
        st->ConOut->SetAttribute(st->ConOut,screen);st->ConOut->ClearScreen(st->ConOut);
        Fill(0,0,80,bar);At(14,0,u"Aptio Setup - Charon tab test  (C) 2026 Test",Attr(white,blue));
    }
    // The tab bar: the open tab in reverse colours.
    Fill(1,0,80,bar);
    size_t col=1;
    for(size_t t=0;t<tabCount;++t) {
        At(col,1,tabs[t].name,t==tab?Attr(blue,lightgray):bar);
        size_t n=0;while(tabs[t].name[n])++n;col+=n+2;
    }
    for(size_t r=2;r<22;++r)Fill(r,0,80,screen);
    for(size_t r=2;r<22;++r)At(54,r,u"│",screen);
    const Tab& t=tabs[tab];
    for(size_t i=0;i<t.count;++i) {
        const size_t a=i==item?Attr(white,lightgray):screen;
        At(2,3+i,t.items[i].name,a);
        if(t.items[i].value)At(32,3+i,t.items[i].value,i==item?Attr(white,lightgray):Attr(black,lightgray));
        else At(1,3+i,u"►",screen);
    }
    {static char16_t help[24];size_t n=0;for(const char16_t* h=t.items[item].help;*h && n<23;++h)help[n++]=*h;help[n]=0;At(56,3,help,screen);}   // the panel is 23 columns
    At(56,12,u"→←: Select Screen",screen);At(56,13,u"↑↓: Select Item",screen);
    At(56,14,u"Enter: Select",screen);At(56,15,u"+/-: Change Opt.",screen);At(56,16,u"F1: General Help",screen);
    At(56,17,u"F10: Save & Exit",screen);At(56,18,u"ESC: Exit",screen);
    Fill(23,0,80,bar);At(18,23,u"Version 2.22.1286 Copyright (C) 2026 Test",bar);
}
extern "C" efi_status EFIAPI efi_main(efi_handle,efi_system_table* table) {
    st=table;st->ConOut->EnableCursor(st->ConOut,false);
    size_t tab=0,item=0;Draw(tab,item,true);
    for(;;) {
        efi_input_key key{};size_t index;
        st->BootServices->WaitForEvent(1,&st->ConIn->WaitForKey,&index);
        if(st->ConIn->ReadKeyStroke(st->ConIn,&key)!=EFI_SUCCESS)continue;
        if(key.ScanCode==23)break;   // Escape
        if(key.ScanCode==3) {tab=(tab+1)%tabCount;item=0;}
        else if(key.ScanCode==4) {tab=(tab+tabCount-1)%tabCount;item=0;}
        else if(key.ScanCode==2 && item+1<tabs[tab].count)++item;
        else if(key.ScanCode==1 && item)--item;
        else continue;
        Draw(tab,item,false);
    }
    st->ConOut->SetAttribute(st->ConOut,Attr(lightgray,black));st->ConOut->ClearScreen(st->ConOut);
    return EFI_SUCCESS;
}
