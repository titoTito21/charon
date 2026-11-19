// Native unit test of the HAK lone-Windows-key tap recognizer. No input_server,
// no Deskbar request: only hak::DeskbarMenuKey::Observe over built messages.
#include "../src/HAKDeskbarMenuKey.h"
#include <cstdio>
#include <initializer_list>

static BMessage Key(uint32 what,int32 key,std::initializer_list<int32> held,bool states=true) {
    BMessage message(what);
    message.AddInt32("key",key);
    if(states) {
        uint8 bitmap[16]={};
        for(int32 code:held)bitmap[code>>3]|=1<<(7-(code&7));
        message.AddData("states",B_UINT8_TYPE,bitmap,sizeof(bitmap));
    }
    return message;
}

static int failures=0;
static void Expect(const char* name,int32 actual,int32 expected) {
    if(actual!=expected) {
        printf("FAIL %s: got %ld expected %ld\n",name,long(actual),long(expected));
        ++failures;
    }
}

int main() {
    const int32 L=hak::DeskbarMenuKey::kLeftWindowsKey,R=hak::DeskbarMenuKey::kRightWindowsKey;
    const int32 E=0x29,SHIFT=0x4b;
    {   // Lone tap of either Windows key.
        hak::DeskbarMenuKey tap;
        Expect("left down",tap.Observe(Key(B_UNMAPPED_KEY_DOWN,L,{L})),0);
        Expect("modifiers changed ignored",tap.Observe(BMessage(B_MODIFIERS_CHANGED)),0);
        Expect("pointer motion ignored",tap.Observe(BMessage(B_MOUSE_MOVED)),0);
        Expect("left up",tap.Observe(Key(B_UNMAPPED_KEY_UP,L,{})),L);
        Expect("no second fire",tap.Observe(Key(B_UNMAPPED_KEY_UP,L,{})),0);
        Expect("right down",tap.Observe(Key(B_UNMAPPED_KEY_DOWN,R,{R})),0);
        Expect("right repeat",tap.Observe(Key(B_UNMAPPED_KEY_DOWN,R,{R})),0);
        Expect("right up",tap.Observe(Key(B_UNMAPPED_KEY_UP,R,{})),R);
    }
    {   // Option+E is a combination, not a tap.
        hak::DeskbarMenuKey tap;
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,L,{L}));
        tap.Observe(Key(B_KEY_DOWN,E,{L,E}));
        tap.Observe(Key(B_KEY_UP,E,{L}));
        Expect("combination",tap.Observe(Key(B_UNMAPPED_KEY_UP,L,{})),0);
    }
    {   // Option+click and Option+wheel.
        hak::DeskbarMenuKey tap;
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,L,{L}));
        tap.Observe(BMessage(B_MOUSE_DOWN));
        Expect("click",tap.Observe(Key(B_UNMAPPED_KEY_UP,L,{})),0);
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,L,{L}));
        tap.Observe(BMessage(B_MOUSE_WHEEL_CHANGED));
        Expect("wheel",tap.Observe(Key(B_UNMAPPED_KEY_UP,L,{})),0);
    }
    {   // Another modifier held before, pressed during, or still held at release.
        hak::DeskbarMenuKey tap;
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,SHIFT,{SHIFT}));
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,L,{SHIFT,L}));
        Expect("shift held first",tap.Observe(Key(B_UNMAPPED_KEY_UP,L,{SHIFT})),0);
        tap.Observe(Key(B_UNMAPPED_KEY_UP,SHIFT,{}));
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,L,{L}));
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,SHIFT,{L,SHIFT}));
        tap.Observe(Key(B_UNMAPPED_KEY_UP,SHIFT,{L}));
        Expect("shift pressed during",tap.Observe(Key(B_UNMAPPED_KEY_UP,L,{})),0);
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,L,{L}));
        Expect("other key still held at release",tap.Observe(Key(B_UNMAPPED_KEY_UP,L,{E})),0);
    }
    {   // Both Windows keys, a foreign release, and messages without a bitmap.
        hak::DeskbarMenuKey tap;
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,L,{L}));
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,R,{L,R}));
        Expect("both: first up",tap.Observe(Key(B_UNMAPPED_KEY_UP,R,{L})),0);
        Expect("both: second up",tap.Observe(Key(B_UNMAPPED_KEY_UP,L,{})),0);
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,L,{L}));
        Expect("foreign release",tap.Observe(Key(B_UNMAPPED_KEY_UP,R,{})),0);
        Expect("after foreign release",tap.Observe(Key(B_UNMAPPED_KEY_UP,L,{})),0);
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,L,{L},false));
        Expect("down without states",tap.Observe(Key(B_UNMAPPED_KEY_UP,L,{})),0);
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,L,{L}));
        Expect("up without states",tap.Observe(Key(B_UNMAPPED_KEY_UP,L,{},false)),0);
        BMessage noKey(B_UNMAPPED_KEY_DOWN);
        Expect("down without key",tap.Observe(noKey),0);
    }
    {   // An unmapped key that is not a Windows key never arms.
        hak::DeskbarMenuKey tap;
        tap.Observe(Key(B_UNMAPPED_KEY_DOWN,SHIFT,{SHIFT}));
        Expect("shift tap",tap.Observe(Key(B_UNMAPPED_KEY_UP,SHIFT,{})),0);
    }
    if(failures)return 1;
    printf("PASS HAK Deskbar menu key: lone Windows-key taps recognized; combinations, clicks, other modifiers and malformed messages rejected\n");
    return 0;
}
