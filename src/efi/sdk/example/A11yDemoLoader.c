/*
 * Example boot menu that uses the HAK Boot Accessibility Protocol (r396).
 * SPDX-License-Identifier: MIT-0
 *
 * A small text-mode "boot loader" as a reference for loader authors and as
 * Charon's test program: a menu of three systems and two options, a 10 s
 * countdown, an entry editor (e) with a caret, a check box (Space), a
 * progress bar on Enter, its own key help and a message. Everything is drawn
 * on the console as usual; the protocol only adds a description. Without a
 * screen reader the program behaves the same.
 *
 * Build (freestanding, Clang): see tools/build-a11y-demo.py.
 */
#include <stdint.h>
#include <stddef.h>
#include "../hak_boot_accessibility.h"

/* --- the few UEFI definitions this example needs ------------------------ */
#define EFIAPI HAK_A11Y_EFIAPI
typedef uintptr_t EFI_STATUS;
typedef void *EFI_HANDLE, *EFI_EVENT;
typedef uint16_t CHAR16;
typedef struct {uint32_t a;uint16_t b,c;uint8_t d[8];} EFI_GUID;
typedef struct {uint16_t ScanCode;CHAR16 UnicodeChar;} EFI_INPUT_KEY;
typedef struct EFI_SIMPLE_TEXT_INPUT {
    EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_TEXT_INPUT*,uint8_t);
    EFI_STATUS (EFIAPI *ReadKeyStroke)(struct EFI_SIMPLE_TEXT_INPUT*,EFI_INPUT_KEY*);
    EFI_EVENT WaitForKey;
} EFI_SIMPLE_TEXT_INPUT;
typedef struct EFI_SIMPLE_TEXT_OUTPUT {
    void *Reset;
    EFI_STATUS (EFIAPI *OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT*,const CHAR16*);
    void *TestString,*QueryMode,*SetMode;
    EFI_STATUS (EFIAPI *SetAttribute)(struct EFI_SIMPLE_TEXT_OUTPUT*,uintptr_t);
    EFI_STATUS (EFIAPI *ClearScreen)(struct EFI_SIMPLE_TEXT_OUTPUT*);
    EFI_STATUS (EFIAPI *SetCursorPosition)(struct EFI_SIMPLE_TEXT_OUTPUT*,uintptr_t,uintptr_t);
    EFI_STATUS (EFIAPI *EnableCursor)(struct EFI_SIMPLE_TEXT_OUTPUT*,uint8_t);
    void *Mode;
} EFI_SIMPLE_TEXT_OUTPUT;
typedef struct {uint64_t Signature;uint32_t Revision,HeaderSize,CRC32,Reserved;} EFI_TABLE_HEADER;
typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL,*RestoreTPL,*AllocatePages,*FreePages,*GetMemoryMap,*AllocatePool,*FreePool;
    EFI_STATUS (EFIAPI *CreateEvent)(uint32_t,uintptr_t,void*,void*,EFI_EVENT*);
    EFI_STATUS (EFIAPI *SetTimer)(EFI_EVENT,int,uint64_t);
    EFI_STATUS (EFIAPI *WaitForEvent)(uintptr_t,EFI_EVENT*,uintptr_t*);
    void *SignalEvent;
    EFI_STATUS (EFIAPI *CloseEvent)(EFI_EVENT);
    void *CheckEvent,*InstallProtocolInterface,*ReinstallProtocolInterface,*UninstallProtocolInterface,*HandleProtocol,*Reserved,
         *RegisterProtocolNotify,*LocateHandle,*LocateDevicePath,*InstallConfigurationTable,*LoadImage,*StartImage,*Exit,*UnloadImage,
         *ExitBootServices,*GetNextMonotonicCount;
    EFI_STATUS (EFIAPI *Stall)(uintptr_t);
    void *SetWatchdogTimer,*ConnectController,*DisconnectController,*OpenProtocol,*CloseProtocol,*OpenProtocolInformation,
         *ProtocolsPerHandle,*LocateHandleBuffer;
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID*,void*,void**);
} EFI_BOOT_SERVICES;
typedef struct {
    EFI_TABLE_HEADER Hdr;CHAR16 *FirmwareVendor;uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;EFI_SIMPLE_TEXT_INPUT *ConIn;
    EFI_HANDLE ConsoleOutHandle;EFI_SIMPLE_TEXT_OUTPUT *ConOut;
    EFI_HANDLE StandardErrorHandle;EFI_SIMPLE_TEXT_OUTPUT *StdErr;
    void *RuntimeServices;EFI_BOOT_SERVICES *BootServices;
} EFI_SYSTEM_TABLE;
#define EVT_TIMER 0x80000000u
#define TimerPeriodic 1
#define TimerCancel 0

static EFI_SYSTEM_TABLE *st;
static HAK_BOOT_A11Y_PROTOCOL *a11y;   /* NULL without a screen reader */

static void At(uintptr_t col,uintptr_t row,const CHAR16 *text,uintptr_t attr)
{
    st->ConOut->SetAttribute(st->ConOut,attr);
    st->ConOut->SetCursorPosition(st->ConOut,col,row);
    st->ConOut->OutputString(st->ConOut,text);
}
static size_t Len(const CHAR16 *s) {size_t n=0;while(s[n])++n;return n;}
static void Pad(CHAR16 *out,const CHAR16 *text,size_t width)
{
    size_t n=0;for(;text[n] && n<width;++n)out[n]=text[n];
    for(;n<width;++n)out[n]=u' ';
    out[n]=0;
}

enum {itemDebian,itemWindows,itemRescue,itemQuiet,itemFirmware,itemCount};
static const CHAR16 *names[itemCount]={u"Debian GNU/Linux",u"Windows 11",u"Rescue shell",u"Quiet boot",u"Firmware setup"};
static CHAR16 options[3][64]={u"root=/dev/sda2 ro quiet",u"",u"root=/dev/sda2 single"};
static int quiet=1;

static void FocusItem(int i)
{
    if(!a11y)return;
    HAK_BOOT_A11Y_FOCUS f=HAK_BOOT_A11Y_FOCUS_INIT;
    f.Name=names[i];f.Index=(uint32_t)i+1;f.Count=itemCount;
    if(i==itemQuiet) {f.Role=HAK_A11Y_ROLE_CHECK_BOX;f.State=quiet?HAK_A11Y_STATE_CHECKED:0;}
    else if(i==itemFirmware)f.Role=HAK_A11Y_ROLE_BUTTON;
    else {f.Role=HAK_A11Y_ROLE_MENU_ITEM;if(i==itemDebian)f.State=HAK_A11Y_STATE_DEFAULT;}
    a11y->Focus(a11y,&f);
}
static void DrawMenu(int selected,int seconds)
{
    CHAR16 line[80];
    st->ConOut->SetAttribute(st->ConOut,0x07);st->ConOut->ClearScreen(st->ConOut);
    At(2,1,u"Demo Loader 1.0 - HAK accessibility example",0x0f);
    for(int i=0;i<itemCount;++i) {
        CHAR16 text[60];
        if(i==itemQuiet) {const CHAR16 *box=quiet?u"[x] ":u"[ ] ";size_t n=0;for(;box[n];++n)text[n]=box[n];for(size_t k=0;names[i][k];++k)text[n++]=names[i][k];text[n]=0;}
        else {size_t n=0;for(;names[i][n];++n)text[n]=names[i][n];text[n]=0;}
        Pad(line,text,40);
        At(4,(uintptr_t)(3+i),line,i==selected?0x70:0x07);
    }
    At(2,10,u"Arrows: choose  Enter: start  e: edit  Space: toggle  Esc: leave",0x07);
    if(seconds>=0) {
        CHAR16 c[64]=u"Starting Debian in ";size_t n=Len(c);
        if(seconds>=10)c[n++]=(CHAR16)(u'0'+seconds/10);
        c[n++]=(CHAR16)(u'0'+seconds%10);
        const CHAR16 *tail=u" s. Press any key to stop.";for(size_t k=0;tail[k];++k)c[n++]=tail[k];c[n]=0;
        At(2,12,c,0x07);
    }
}
static int ReadKey(EFI_INPUT_KEY *key,EFI_EVENT timer)
{
    EFI_EVENT events[2]={st->ConIn->WaitForKey,timer};uintptr_t index=0;
    st->BootServices->WaitForEvent(timer?2:1,events,&index);
    if(index==1)return 0;
    return st->ConIn->ReadKeyStroke(st->ConIn,key)==0;
}
/* The entry editor: one line, Left/Right/Home/End, typing, Backspace, Delete; F10 or Enter accepts, Esc discards. */
static void Edit(int entry)
{
    CHAR16 text[64];size_t length=0,caret=0;
    while(options[entry][length]) {text[length]=options[entry][length];++length;}
    text[length]=0;
    if(a11y) {
        a11y->BeginScreen(a11y,u"Edit entry",HAK_A11Y_SCREEN_EDITOR);
        static const CHAR16 *keys[]={u"Editor: Left and Right move, Backspace and Delete erase, F10 or Enter keep the change, Escape discards it"};
        a11y->SetKeyHelp(a11y,keys,1);
    }
    for(;;) {
        CHAR16 line[70];Pad(line,text,60);
        st->ConOut->SetAttribute(st->ConOut,0x07);st->ConOut->ClearScreen(st->ConOut);
        At(2,1,u"Edit entry: ",0x0f);At(14,1,names[entry],0x0f);
        At(2,3,line,0x1f);
        At(2,5,u"F10/Enter: keep  Esc: discard",0x07);
        st->ConOut->SetCursorPosition(st->ConOut,2+caret,3);st->ConOut->EnableCursor(st->ConOut,1);
        if(a11y) {
            HAK_BOOT_A11Y_FOCUS f=HAK_BOOT_A11Y_FOCUS_INIT;
            f.Role=HAK_A11Y_ROLE_EDIT;f.Name=u"Kernel options";f.Value=text;f.Caret=(uint32_t)caret;
            a11y->Focus(a11y,&f);
        }
        EFI_INPUT_KEY k;if(!ReadKey(&k,0))continue;
        if(k.ScanCode==0x17) break;
        if(k.ScanCode==0x14 || k.UnicodeChar==u'\r') {for(size_t i=0;i<=length;++i)options[entry][i]=text[i];if(a11y)a11y->Speak(a11y,u"Saved.",HAK_A11Y_SPEAK_MESSAGE);break;}
        if(k.ScanCode==4 && caret)--caret;
        else if(k.ScanCode==3 && caret<length)++caret;
        else if(k.ScanCode==5)caret=0;
        else if(k.ScanCode==6)caret=length;
        else if(k.UnicodeChar==8 && caret) {for(size_t i=caret-1;i<length;++i)text[i]=text[i+1];--length;--caret;}
        else if(k.ScanCode==8 && caret<length) {for(size_t i=caret;i<length;++i)text[i]=text[i+1];--length;}
        else if(k.UnicodeChar>=0x20 && length<60) {for(size_t i=length+1;i>caret;--i)text[i]=text[i-1];text[caret++]=k.UnicodeChar;++length;}
    }
    st->ConOut->EnableCursor(st->ConOut,0);
}
static void Start(int entry)
{
    st->ConOut->SetAttribute(st->ConOut,0x07);st->ConOut->ClearScreen(st->ConOut);
    At(2,1,u"Loading ",0x07);At(10,1,names[entry],0x07);
    for(uint32_t p=0;p<=100;p+=10) {
        CHAR16 bar[24];size_t n=0;bar[n++]=u'[';for(uint32_t k=0;k<10;++k)bar[n++]=k<p/10?u'#':u'.';bar[n++]=u']';bar[n]=0;
        At(2,3,bar,0x07);
        if(a11y)a11y->Progress(a11y,p,u"Loading");
        st->BootServices->Stall(300000);
    }
    if(a11y)a11y->Speak(a11y,u"This is only a demonstration; nothing was started.",HAK_A11Y_SPEAK_MESSAGE);
    At(2,5,u"This is only a demonstration; nothing was started. Press a key.",0x07);
    EFI_INPUT_KEY k;while(!ReadKey(&k,0)) {}
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *table)
{
    (void)image;st=table;
    EFI_GUID guid=HAK_BOOT_A11Y_PROTOCOL_GUID;
    if(st->BootServices->LocateProtocol(&guid,0,(void**)&a11y)!=0 || !HakBootA11yUsable(a11y))a11y=0;
    int stopCountdowns=0;
    if(a11y) {HAK_BOOT_A11Y_SETTINGS s={sizeof(s),0,{0},0,0};if(a11y->GetSettings(a11y,&s)==0)stopCountdowns=(s.Flags&HAK_A11Y_STOP_COUNTDOWNS)!=0;}
    (void)stopCountdowns;   /* Countdown() reports it too */
    static const CHAR16 *keys[]={u"Demo Loader: Up and Down choose, Enter starts, e edits the entry, Space toggles a check box, Escape leaves",
                                 u"Demo Loader: any key stops the countdown"};
    EFI_EVENT timer=0;st->BootServices->CreateEvent(EVT_TIMER,0,0,0,&timer);
    st->BootServices->SetTimer(timer,TimerPeriodic,10000000);   /* 1 s */
    int selected=0,seconds=10,redraw=1;
    if(a11y) {a11y->BeginScreen(a11y,u"Demo Loader 1.0",HAK_A11Y_SCREEN_MENU);a11y->SetKeyHelp(a11y,keys,2);}
    DrawMenu(selected,seconds);
    if(a11y) {uint8_t stop=0;a11y->Countdown(a11y,(uint32_t)seconds,&stop);if(stop)seconds=-1;FocusItem(selected);}
    for(;;) {
        if(redraw) {DrawMenu(selected,seconds);redraw=0;}
        EFI_INPUT_KEY k;
        if(!ReadKey(&k,seconds>=0?timer:0)) {
            if(seconds<0)continue;
            --seconds;redraw=1;
            if(a11y) {uint8_t stop=0;a11y->Countdown(a11y,(uint32_t)seconds,&stop);if(stop) {seconds=-1;continue;}}
            if(seconds==0) {if(a11y)a11y->EndScreen(a11y);Start(itemDebian);break;}
            continue;
        }
        const int counting=seconds>=0;
        if(counting) {seconds=-1;redraw=1;if(a11y)a11y->Speak(a11y,u"Countdown stopped.",HAK_A11Y_SPEAK_MESSAGE);}
        if(k.ScanCode==1) {selected=(selected+itemCount-1)%itemCount;redraw=1;FocusItem(selected);}
        else if(k.ScanCode==2) {selected=(selected+1)%itemCount;redraw=1;FocusItem(selected);}
        else if(k.ScanCode==0x17)break;
        else if(k.UnicodeChar==u' ' && selected==itemQuiet) {quiet=!quiet;redraw=1;FocusItem(selected);}
        else if(k.UnicodeChar==u'e' && selected<=itemRescue) {
            Edit(selected);
            if(a11y) {a11y->BeginScreen(a11y,u"Demo Loader 1.0",HAK_A11Y_SCREEN_MENU);a11y->SetKeyHelp(a11y,keys,2);}
            redraw=1;DrawMenu(selected,seconds);redraw=0;FocusItem(selected);
        }
        else if(k.UnicodeChar==u'\r' && selected<=itemRescue) {
            if(a11y)a11y->EndScreen(a11y);
            Start(selected);
            if(a11y) {a11y->BeginScreen(a11y,u"Demo Loader 1.0",HAK_A11Y_SCREEN_MENU);a11y->SetKeyHelp(a11y,keys,2);}
            redraw=1;DrawMenu(selected,seconds);redraw=0;FocusItem(selected);
        }
        else if(k.UnicodeChar==u'\r' && selected==itemFirmware) {
            if(a11y)a11y->Speak(a11y,u"Firmware setup is not available in this demonstration.",HAK_A11Y_SPEAK_ALERT);
        }
    }
    st->BootServices->CloseEvent(timer);
    if(a11y) {a11y->SetKeyHelp(a11y,0,0);a11y->EndScreen(a11y);}
    st->ConOut->SetAttribute(st->ConOut,0x07);st->ConOut->ClearScreen(st->ConOut);
    return 0;
}
