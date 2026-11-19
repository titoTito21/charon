// Numpad screen review on USB keyboards (r390). GPL-2.0-or-later.
//
// NVDA reviews the screen with the numeric keypad alone (NumLock off):
// 7 8 9 lines, 4 5 6 words, 1 2 3 characters, + say all, Shift+7/9 top and
// bottom. UEFI's text input reports a keypad 8 exactly like the Up arrow
// and a keypad 7 like Home (or like the digit 7), so the keypad cannot be
// told from the keys around it there. USB keyboards say it in their HID
// report: the reader wraps UsbAsyncInterruptTransfer of every boot keyboard
// interface (EFI_USB_IO_PROTOCOL, per-interface instance), reconnects the
// keyboard so the firmware's own driver registers its report callback
// through the wrapper, sees each report before that driver and passes it on
// unchanged. A keypad press becomes a reader command; the key the firmware
// driver then makes of it is taken out of the program's input. Keyboards on
// PS/2 (and firmware with a keyboard stack of its own) are not covered:
// there the Alt layout (Alt+arrows, Alt+\) and Insert/Ctrl+Alt work.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <efi/system-table.h>
#include <efi/protocol/simple-text-input.h>
namespace hakefi {
struct UsbInterfaceDescriptor {uint8_t length,type,number,alternate,endpoints,interfaceClass,subClass,protocol,name;};
using UsbAsyncCallback=efi_status (EFIAPI*)(void* data,size_t length,void* context,uint32_t status);
struct UsbIo {
    void* ControlTransfer;void* BulkTransfer;
    efi_status (EFIAPI *AsyncInterruptTransfer)(UsbIo*,uint8_t endpoint,uint8_t isNew,size_t interval,size_t length,UsbAsyncCallback callback,void* context);
    void* SyncInterruptTransfer;void* IsochronousTransfer;void* AsyncIsochronousTransfer;
    void* GetDeviceDescriptor;void* GetConfigDescriptor;
    efi_status (EFIAPI *GetInterfaceDescriptor)(UsbIo*,UsbInterfaceDescriptor*);
    void* GetEndpointDescriptor;void* GetStringDescriptor;void* GetSupportedLanguages;void* PortReset;
};
class UsbKeypad {
public:
    static constexpr size_t maxKeyboards=8;
    // HID usages 0x54..0x63: / * - + Enter 1..9 0 .
    static constexpr uint8_t firstUsage=0x54,lastUsage=0x63;
    // The reader command of a keypad usage (NVDA desktop layout), 0 to pass it on.
    static char16_t CommandOf(uint8_t usage,bool shift) {
        switch(usage) {
            case 0x5f:return shift?u't':u'u';   // 7: previous line (Shift: top)
            case 0x60:return u'i';             // 8: current line
            case 0x61:return shift?u'b':u'o';   // 9: next line (Shift: bottom)
            case 0x5c:return u'j';case 0x5d:return u'k';case 0x5e:return u'l';   // 4 5 6 words
            case 0x59:return u'm';case 0x5a:return u',';case 0x5b:return u'.';   // 1 2 3 characters
            case 0x57:return u'a';             // +: say all
            case 0x55:return u'r';             // *: the whole screen
            case 0x56:return u'e';             // -: the highlighted entry (NVDA: the navigator object)
            case 0x63:return u'p';             // . (Delete): where the review cursor is
            default:return 0;                  // / Enter 0 (Insert, NVDA's key) pass
        }
    }
    // What the firmware's USB keyboard driver makes of the usage with NumLock off.
    static bool Produces(uint8_t usage,const efi_input_key& key) {
        static const uint16_t scan[16]={0,0,0,0,0, 6,2,0xa,4,0,3,5,1,9, 7,8};
        static const char16_t ch[16]={u'/',u'*',u'-',u'+',13, 0,0,0,0,0,0,0,0,0, 0,0};
        if(usage<firstUsage || usage>lastUsage)return false;
        const size_t i=usage-firstUsage;
        return (scan[i] && key.ScanCode==scan[i] && !key.UnicodeChar) || (ch[i] && !key.ScanCode && key.UnicodeChar==ch[i]);
    }
    // Hooks keyboards not hooked yet; true when one was added.
    bool Scan(efi_boot_services* boot) {
        if(!boot || !boot->LocateHandleBuffer)return false;
        efi_guid guid={0x2b2f68d6,0x0cd2,0x44cf,{0x8e,0x8b,0xbb,0xa2,0x0b,0x1b,0x5b,0x75}};
        size_t count=0;efi_handle* handles=nullptr;
        if(boot->LocateHandleBuffer(ByProtocol,&guid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return false;
        bool added=false;
        for(size_t i=0;i<count && used_<maxKeyboards;++i) {
            UsbIo* io=nullptr;
            if(boot->HandleProtocol(handles[i],&guid,(void**)&io)!=EFI_SUCCESS || !io || !io->GetInterfaceDescriptor || !io->AsyncInterruptTransfer)continue;
            if(io->AsyncInterruptTransfer==HookAsync)continue;
            UsbInterfaceDescriptor d{};
            if(io->GetInterfaceDescriptor(io,&d)!=EFI_SUCCESS || d.interfaceClass!=3 || d.subClass!=1 || d.protocol!=1)continue;
            Slot& s=slots_[used_++];s={};s.io=io;s.handle=handles[i];s.original=io->AsyncInterruptTransfer;
            io->AsyncInterruptTransfer=HookAsync;
            // The firmware driver registered its callback before the hook:
            // restart it on this interface so it registers through the hook.
            boot->DisconnectController(handles[i],nullptr,nullptr);
            boot->ConnectController(handles[i],nullptr,nullptr,true);
            added=true;
        }
        boot->FreePool(handles);
        return added;
    }
    // Restores the original transfer functions and lets the driver register again without the wrapper.
    void Unhook(efi_boot_services* boot) {
        for(size_t i=0;i<used_;++i) {
            Slot& s=slots_[i];
            if(s.io && s.io->AsyncInterruptTransfer==HookAsync) {
                s.io->AsyncInterruptTransfer=s.original;
                if(boot) {boot->DisconnectController(s.handle,nullptr,nullptr);boot->ConnectController(s.handle,nullptr,nullptr,true);}
            }
            s={};
        }
        used_=0;
    }
    void Attach(void (*press)(void*,char16_t),void* context) {self_=this;press_=press;pressContext_=context;}
    void SetNumLock(bool on) {numLock_=on;}
    size_t Keyboards() const {return used_;}
    // True when the key is the one a reviewed keypad press made (taken out of the input).
    bool Consume(const efi_input_key& key) {
        if(numLock_ || !used_)return false;
        for(uint8_t u=firstUsage;u<=lastUsage;++u) {
            const size_t i=u-firstUsage;
            if(!(pendingEat_[i] || (held_&(1u<<i))))continue;
            if(!Produces(u,key))continue;
            if(pendingEat_[i])--pendingEat_[i];
            return true;
        }
        return false;
    }
private:
    struct Slot {UsbIo* io;efi_handle handle;decltype(UsbIo::AsyncInterruptTransfer) original;UsbAsyncCallback callback;void* context;};
    static inline UsbKeypad* self_=nullptr;
    static efi_status EFIAPI HookAsync(UsbIo* io,uint8_t endpoint,uint8_t isNew,size_t interval,size_t length,UsbAsyncCallback callback,void* context) {
        UsbKeypad* k=self_;
        Slot* s=nullptr;
        if(k)for(size_t i=0;i<k->used_;++i)if(k->slots_[i].io==io) {s=&k->slots_[i];break;}
        if(!s)return EFI_DEVICE_ERROR;   // never: only hooked instances point here
        if(!isNew || !callback)return s->original(io,endpoint,isNew,interval,length,callback,context);
        s->callback=callback;s->context=context;
        return s->original(io,endpoint,isNew,interval,length,Report,s);
    }
    static efi_status EFIAPI Report(void* data,size_t length,void* context,uint32_t status) {
        auto* s=static_cast<Slot*>(context);
        if(UsbKeypad* k=self_)if(!status && data && length>=3)k->Parse(static_cast<const uint8_t*>(data),length);
        return s->callback(data,length,s->context,status);
    }
    void Parse(const uint8_t* report,size_t length) {
        // Boot protocol: modifiers, reserved, up to six key usages.
        uint32_t now=0;
        for(size_t i=2;i<length && i<8;++i)if(report[i]>=firstUsage && report[i]<=lastUsage)now|=1u<<(report[i]-firstUsage);
        const uint32_t pressed=now&~held_;
        held_=now;
        if(!pressed || numLock_)return;   // NVDA: with NumLock on the keypad types
        const bool shift=(report[0]&0x22)!=0;
        for(uint8_t u=firstUsage;u<=lastUsage;++u) {
            const size_t i=u-firstUsage;
            if(!(pressed&(1u<<i)))continue;
            const char16_t c=CommandOf(u,shift);
            if(!c) {held_&=~(1u<<i);continue;}   // passes: never eaten
            if(pendingEat_[i]<4)++pendingEat_[i];
            if(press_)press_(pressContext_,c);
        }
    }
    Slot slots_[maxKeyboards]={};size_t used_=0;
    volatile uint32_t held_=0;volatile uint8_t pendingEat_[16]={};volatile bool numLock_=false;
    void (*press_)(void*,char16_t)=nullptr;void* pressContext_=nullptr;
};
}  // namespace hakefi
