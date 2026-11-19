// Reader shortcuts inside other programs (r369). GPL-2.0-or-later.
//
// The resident reader registers key notifications with the console input
// (EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL.RegisterKeyNotify on the system table's
// console input handle, so every keyboard behind the console splitter is
// covered). Firmware calls the notification when the key is typed; the key
// still reaches the running program unchanged. Nothing is hooked or consumed.
//   Ctrl+Alt+R  read the whole screen again
//   Ctrl+Alt+S  stop speech
//   Ctrl+Alt+H  say the shortcuts
// r378 review cursor (NVDA laptop layout, letters so QEMU's SDL window, which
// keeps Ctrl+Alt+digits, F and G, passes them through):
//   Ctrl+Alt+U/I/O  previous / current / next line
//   Ctrl+Alt+J/K/L  previous / current / next word
//   Ctrl+Alt+M , .  previous / current / next character
//   Ctrl+Alt+T/B    top / bottom line of the screen
//   Ctrl+Alt+E      the highlighted entry again, with its position
//   Ctrl+Alt+P      where the review cursor is
// r379:
//   Ctrl+Alt+A      read everything from the review cursor to the end
//   Ctrl+Alt+Q      automatic reading of screen changes off / on
//   Ctrl+Alt+N      colours and highlight at the review cursor
//   Ctrl+Alt+C      time of the firmware clock
//   Ctrl+Alt+= / -  speech faster / slower
// r380:
//   Ctrl+Alt+W      where am I: program, screen title, highlighted entry
//   Ctrl+Alt+Y      the key-hint bar at the bottom of the screen
//   Ctrl+Alt+V      reader verbosity short / detailed
// r390 (NVDA text reading):
//   Ctrl+Alt+D      symbol level none / some / most / all (NVDA+P)
//   Ctrl+Alt+Z      screen text language: automatic / menu / English / Polish
//   Ctrl+Alt+X      input help on / off (NVDA+1): keys are named, not run
// r390 Alt layout (the user's, for every keyboard, also PS/2):
//   Alt+Up / Down     previous / next line     Alt+Left / Right  previous / next character
//   Alt+\             screen review with the boot paused (again or Esc: go on)
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <efi/system-table.h>
#include <efi/protocol/simple-text-input.h>
namespace hakefi {
struct KeyState {uint32_t shift;uint8_t toggle;};
struct KeyData {efi_input_key key;KeyState state;};
using KeyNotifyFn=efi_status (EFIAPI*)(KeyData*);
struct TextInputEx {
    efi_status (EFIAPI *Reset)(TextInputEx*,bool);
    efi_status (EFIAPI *ReadKeyStrokeEx)(TextInputEx*,KeyData*);
    efi_event WaitForKeyEx;
    efi_status (EFIAPI *SetState)(TextInputEx*,uint8_t*);
    efi_status (EFIAPI *RegisterKeyNotify)(TextInputEx*,KeyData*,KeyNotifyFn,void**);
    efi_status (EFIAPI *UnregisterKeyNotify)(TextInputEx*,void*);
};
inline constexpr uint32_t shiftValid=0x80000000u,rightShift=1,leftShift=2,rightControl=4,leftControl=8,rightAlt=0x10,leftAlt=0x20;
// One command per letter; each is registered as the plain letter, the
// capital and the control code (keyboard drivers turn Ctrl+letter into
// 1..26 before matching), with every left/right Ctrl and Alt combination.
class ReaderKeys {
public:
    static constexpr size_t maxHandles=400;   // r390: 24 letters x 12 forms + marks + arrows (200 dropped handles before)
    // True once registered on a console input; retried until one exists
    // (driver mode starts before firmware connects the consoles).
    bool Register(efi_system_table* table,KeyNotifyFn notify) {
        if(input_)return true;
        efi_guid guid={0xdd9e7534,0x7762,0x4698,{0x8c,0x14,0xf5,0x85,0x17,0xa6,0x25,0xaa}};
        TextInputEx* input=nullptr;
        if(!table || !table->ConsoleInHandle || table->BootServices->HandleProtocol(table->ConsoleInHandle,&guid,(void**)&input)!=EFI_SUCCESS
            || !input || !input->RegisterKeyNotify || !input->UnregisterKeyNotify)return false;
        static const char16_t letters[]={u'r',u's',u'h',u'u',u'i',u'o',u'j',u'k',u'l',u'm',u't',u'b',u'e',u'p',u'a',u'q',u'n',u'c',u'w',u'y',u'v',u'd',u'z',u'x'};
        static const char16_t marks[]={u',',u'.',u'=',u'-'};
        static const uint32_t controls[]={leftControl,rightControl},alts[]={leftAlt,rightAlt};
        count_=0;
        for(char16_t letter:letters)for(int form=0;form<3;++form)for(uint32_t c:controls)for(uint32_t a:alts) {
            KeyData data{};
            data.key.ScanCode=0;
            data.key.UnicodeChar=form==0?letter:form==1?char16_t(letter-0x20):char16_t(letter-u'a'+1);
            data.state.shift=shiftValid|c|a;data.state.toggle=0;
            void* handle=nullptr;
            if(input->RegisterKeyNotify(input,&data,notify,&handle)==EFI_SUCCESS && handle && count_<maxHandles)handles_[count_++]=handle;
        }
        // Punctuation has no control code; plain form only.
        for(char16_t mark:marks)for(uint32_t c:controls)for(uint32_t a:alts) {
            KeyData data{};data.key.ScanCode=0;data.key.UnicodeChar=mark;
            data.state.shift=shiftValid|c|a;data.state.toggle=0;
            void* handle=nullptr;
            if(input->RegisterKeyNotify(input,&data,notify,&handle)==EFI_SUCCESS && handle && count_<maxHandles)handles_[count_++]=handle;
        }
        // r388: Ctrl+Alt+arrows - the voice settings ring (Left/Right choose
        // rate, volume or pitch; Up/Down change it).
        for(uint16_t scan=1;scan<=4;++scan)for(uint32_t c:controls)for(uint32_t a:alts) {
            KeyData data{};data.key.ScanCode=scan;data.key.UnicodeChar=0;
            data.state.shift=shiftValid|c|a;data.state.toggle=0;
            void* handle=nullptr;
            if(input->RegisterKeyNotify(input,&data,notify,&handle)==EFI_SUCCESS && handle && count_<maxHandles)handles_[count_++]=handle;
        }
        // r393: Ctrl alone and Shift alone with Left/Right (screen regions).
        static const uint32_t mods[]={leftControl,rightControl,leftShift,rightShift};
        for(uint32_t m:mods)for(uint16_t scan=3;scan<=4;++scan) {
            KeyData data{};data.key.ScanCode=scan;data.key.UnicodeChar=0;data.state.shift=shiftValid|m;data.state.toggle=0;
            void* handle=nullptr;
            if(input->RegisterKeyNotify(input,&data,notify,&handle)==EFI_SUCCESS && handle && count_<maxHandles)handles_[count_++]=handle;
        }
        // r390: Alt alone with an arrow, a backslash or F1 (the Alt layout, the key list).
        for(uint32_t a:alts) {
            {KeyData data{};data.key.ScanCode=0x0b;data.key.UnicodeChar=0;data.state.shift=shiftValid|a;data.state.toggle=0;
             void* handle=nullptr;
             if(input->RegisterKeyNotify(input,&data,notify,&handle)==EFI_SUCCESS && handle && count_<maxHandles)handles_[count_++]=handle;}
            for(uint16_t scan=1;scan<=4;++scan) {
                KeyData data{};data.key.ScanCode=scan;data.key.UnicodeChar=0;data.state.shift=shiftValid|a;data.state.toggle=0;
                void* handle=nullptr;
                if(input->RegisterKeyNotify(input,&data,notify,&handle)==EFI_SUCCESS && handle && count_<maxHandles)handles_[count_++]=handle;
            }
            KeyData data{};data.key.ScanCode=0;data.key.UnicodeChar=u'\\';data.state.shift=shiftValid|a;data.state.toggle=0;
            void* handle=nullptr;
            if(input->RegisterKeyNotify(input,&data,notify,&handle)==EFI_SUCCESS && handle && count_<maxHandles)handles_[count_++]=handle;
        }
        if(!count_)return false;
        input_=input;return true;
    }
    void Unregister() {
        if(!input_)return;
        for(size_t i=0;i<count_;++i)input_->UnregisterKeyNotify(input_,handles_[i]);
        count_=0;input_=nullptr;
    }
    // The command letter of a notified key, 0 for none.
    static char16_t Command(const KeyData* data) {
        if(!data)return 0;
        // r388: arrows of the voice settings ring as private command codes.
        const uint32_t held=(data->state.shift&shiftValid)?data->state.shift:0;
        const bool control=(held&(leftControl|rightControl))!=0;
        if(!data->key.UnicodeChar) {
            // r393: Ctrl alone + Left/Right = screen regions; Shift alone = the same in the paused review.
            const bool alt=(held&(leftAlt|rightAlt))!=0,shift=(held&(leftShift|rightShift))!=0;
            if(control && !alt && !shift && (data->key.ScanCode==3 || data->key.ScanCode==4))return data->key.ScanCode==3?u'}':u'{';
            if(shift && !control && !alt && (data->key.ScanCode==3 || data->key.ScanCode==4))return data->key.ScanCode==3?u')':u'(';
            if(shift && !control && !alt)return 0;
            // r390: Alt alone + arrow = the Alt review layout.
            if(!control && (held&(leftAlt|rightAlt)))
                switch(data->key.ScanCode) {case 1:return u'u';case 2:return u'o';case 3:return u'.';case 4:return u'm';case 0x0b:return u'?';default:return 0;}
            switch(data->key.ScanCode) {case 1:return u'^';case 2:return u'_';case 3:return u'>';case 4:return u'<';default:return 0;}
        }
        if(data->key.UnicodeChar==u'\\')return control?0:u'\\';
        char16_t c=data->key.UnicodeChar;
        if(c>=1 && c<=26)c=char16_t(c+u'a'-1);
        if(c>=u'A' && c<=u'Z')c=char16_t(c+0x20);
        switch(c) {
            case u'r':case u's':case u'h':case u'u':case u'i':case u'o':case u'j':case u'k':case u'l':
            case u'm':case u't':case u'b':case u'e':case u'p':case u',':case u'.':
            case u'a':case u'q':case u'n':case u'c':case u'=':case u'-':
            case u'w':case u'y':case u'v':case u'd':case u'z':case u'x':return c;
            default:return 0;
        }
    }
    size_t Count() const {return count_;}
private:
    TextInputEx* input_=nullptr;void* handles_[maxHandles]={};size_t count_=0;
};
// r381: NVDA desktop layout. Insert is the NVDA key; the key after it is a
// reader command. UEFI reports no "keypad" flag, so the numpad works through
// the characters it sends with NumLock on (the top-row digits do the same).
// The reader intercepts these keys in the console's ReadKeyStroke(Ex), so the
// program never receives them; Insert pressed twice passes one Insert on.
//   7 8 9  previous / current / next line     Up    current line
//   4 5 6  previous / current / next word     Down  read from the review cursor
//   1 2 3  previous / current / next character
//   +      read from the review cursor        *     read the whole screen
//   Tab    highlighted entry                  End   key-hint bar
//   T      where am I (title)                 B     read the whole screen
//   F      colours (formatting)               F12   time
//   other reader letters (S H E Q C W Y V U I O J K L M A N R Z X) as with Ctrl+Alt
// r390 (NVDA): P symbol level, numpad Delete (.) review position, 0 input help, \ paused review
inline char16_t InsertCommand(const efi_input_key& key) {
    switch(key.ScanCode) {
        case 1:return u'i';      // Up
        case 2:return u'a';      // Down
        case 6:return u'y';      // End
        case 0x16:return u'c';   // F12
        case 0x0b:return u'?';   // r390 F1: the key list
        // r397 Speakup windows, NVDA-style copy marks and Speakup cut/paste, line indentation
        case 5:return u'I';      // Home: the line with its indentation
        case 8:return u'S';      // Delete: silence the window (or speak it again)
        case 9:return u'W';      // Page Up: window corners (top left, then bottom right; a third time clears)
        case 0x0a:return u'R';   // Page Down: read the window
        case 0x13:return u'C';   // F9: start mark at the review cursor
        case 0x14:return u'K';   // F10: copy from the start mark to the review cursor
        case 0x15:return u'V';   // F11: paste the copied text into the program as keys
        default:break;
    }
    if(key.ScanCode)return 0;
    char16_t c=key.UnicodeChar;
    switch(c) {
        case u'7':return u'u';case u'8':return u'i';case u'9':return u'o';
        case u'4':return u'j';case u'5':return u'k';case u'6':return u'l';
        case u'1':return u'm';case u'2':return u',';case u'3':return u'.';
        case u'+':return u'a';case u'*':return u'r';case 9:return u'e';
        case u'0':return u'x';case u'\\':return u'\\';case u'.':return u'p';
        default:break;
    }
    if(c>=u'A' && c<=u'Z')c=char16_t(c+0x20);
    switch(c) {
        case u't':return u'w';case u'b':return u'r';case u'f':return u'n';
        case u'p':return u'd';   // r390: NVDA+P symbol level
        case u'g':return u'G';   // r393 formatting mode
        case u'h':return u'H';   // r393 speech history (Ctrl+Alt+H keeps the shortcut summary)
        case u'd':return u'D';   // r393 what changed (Insert+P stays the symbol level)
        case u's':case u'e':case u'q':case u'c':case u'w':case u'y':case u'v':case u'z':case u'x':
        case u'u':case u'i':case u'o':case u'j':case u'k':case u'l':case u'm':case u'a':case u'n':case u'r':return c;
        default:return 0;
    }
}
inline bool InsertKey(const efi_input_key& key) {return key.ScanCode==7 && !key.UnicodeChar;}
}  // namespace hakefi
