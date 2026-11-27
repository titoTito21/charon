// Test-only UEFI application for Charon's control reader (r410): a MOUSE-ONLY
// graphical screen. Drawn only through GOP, with its own tiny bitmap font (no
// console text, no HII): three buttons ("One", "Two", "Exit"), a slider and a
// pointer arrow. It reads ONLY the pointer: EFI_SIMPLE_POINTER_PROTOCOL
// (LocateProtocol; when there is none, EFI_ABSOLUTE_POINTER_PROTOCOL). The
// keyboard is ignored except Escape, which leaves.
//
// The pointer moves the way rEFInd moves it (refind/pointer.c):
//   x += RelativeMovementX * speed / Mode->ResolutionX,  speed = 4
// clamped to the screen, starting in the middle. A click counts when the left
// button is released (as in rEFInd: pressed in one report, up in a later one).
//
// Two ways of reading, chosen by the load option or at build time:
//   default or load option "poll": GetState in a loop with Stall(10 ms);
//   load option "wait" or -DPPROBE_WAIT: WaitForEvent on the protocol's
//   WaitForInput event (read once at start and kept, as real programs do),
//   the keyboard's WaitForKey and a one-second timer.
//
// Log on COM1 (0x3f8, 8N1 as the firmware left it), one line per event:
//   PPROBE start mode=poll|wait protocol=simple|absolute screen W H resolution RX RY
//   PPROBE control One|Two|Exit|slider X Y W H        (once each, the layout)
//   PPROBE pointer X Y                                  (after every movement)
//   PPROBE click One|Two|Exit X Y                       (the button also changes colour)
//   PPROBE slider PERCENT X Y                           (a click on the track)
//   PPROBE click none X Y                               (a click on nothing)
//   PPROBE end escape|exit
// "Exit" ends the program after its line. GPL-2.0-or-later.
#include <efi/system-table.h>
#include <efi/protocol/loaded-image.h>
#include "../src/efi/CharonGraphicsTypes.h"
using namespace hakefi;
extern "C" void* memset(void* d,int v,size_t n){auto* p=(unsigned char*)d;while(n--)*p++=(unsigned char)v;return d;}
extern "C" void* memcpy(void* d,const void* s,size_t n){auto* a=(unsigned char*)d;auto* b=(const unsigned char*)s;while(n--)*a++=*b++;return d;}
// UEFI 2.10, 12.5 Simple Pointer Protocol and 12.7 Absolute Pointer Protocol.
static efi_guid simplePointerGuid={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
static efi_guid absolutePointerGuid={0x8d59d32b,0xc655,0x4ae9,{0x9b,0x15,0xf2,0x59,0x04,0x99,0x2a,0x43}};
static efi_guid loadedImageGuid={0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
struct SimplePointerMode {uint64_t resolutionX,resolutionY,resolutionZ;uint8_t leftButton,rightButton;};
struct SimplePointerState {int32_t relativeX,relativeY,relativeZ;uint8_t leftButton,rightButton;};
struct SimplePointer {
    efi_status (EFIAPI *Reset)(SimplePointer*,uint8_t extendedVerification);
    efi_status (EFIAPI *GetState)(SimplePointer*,SimplePointerState*);
    efi_event WaitForInput;
    SimplePointerMode* Mode;
};
struct AbsolutePointerMode {uint64_t minX,minY,minZ,maxX,maxY,maxZ;uint32_t attributes;};
struct AbsolutePointerState {uint64_t x,y,z;uint32_t activeButtons;};
struct AbsolutePointer {
    efi_status (EFIAPI *Reset)(AbsolutePointer*,uint8_t extendedVerification);
    efi_status (EFIAPI *GetState)(AbsolutePointer*,AbsolutePointerState*);
    efi_event WaitForInput;
    AbsolutePointerMode* Mode;
};
static Gop* gop;static efi_boot_services* boot;static efi_system_table* st;
static void Port(const char* text) {
    for(;*text;++text) {
        for(unsigned i=0;i<100000;++i) {unsigned char s;__asm__ volatile("inb %1,%0":"=a"(s):"Nd"((unsigned short)0x3fd));if(s&0x20)break;}
        __asm__ volatile("outb %0,%1"::"a"((unsigned char)*text),"Nd"((unsigned short)0x3f8));
    }
}
static void Number(long long value) {
    char text[24];int at=23;text[at]=0;const bool negative=value<0;unsigned long long v=negative?0ull-(unsigned long long)value:(unsigned long long)value;
    do {text[--at]=char('0'+v%10);v/=10;} while(v);
    if(negative)text[--at]='-';
    Port(" ");Port(text+at);
}
static void Line(const char* what,const char* name,long long a,long long b,long long c=-1,long long d=-1,int numbers=2) {
    Port("PPROBE ");Port(what);if(name) {Port(" ");Port(name);}
    if(numbers>=1)Number(a);
    if(numbers>=2)Number(b);
    if(numbers>=3)Number(c);
    if(numbers>=4)Number(d);
    Port("\r\n");
}
static void Fill(int x,int y,int w,int h,uint32_t colour) {
    const int W=int(gop->Mode->info->horizontal),H=int(gop->Mode->info->vertical);
    if(x<0) {w+=x;x=0;}
    if(y<0) {h+=y;y=0;}
    if(x+w>W)w=W-x;
    if(y+h>H)h=H-y;
    if(w>0 && h>0)gop->Blt(gop,&colour,bltVideoFill,0,0,size_t(x),size_t(y),size_t(w),size_t(h),0);
}
// A 5x7 font with only the letters of the three captions; bit 4 = left column.
struct Glyph {char ch;uint8_t rows[7];};
static const Glyph font[]={
    {'O',{0x0e,0x11,0x11,0x11,0x11,0x11,0x0e}},{'n',{0x00,0x00,0x16,0x19,0x11,0x11,0x11}},{'e',{0x00,0x00,0x0e,0x11,0x1f,0x10,0x0e}},
    {'T',{0x1f,0x04,0x04,0x04,0x04,0x04,0x04}},{'w',{0x00,0x00,0x11,0x11,0x15,0x15,0x0a}},{'o',{0x00,0x00,0x0e,0x11,0x11,0x11,0x0e}},
    {'E',{0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f}},{'x',{0x00,0x00,0x11,0x0a,0x04,0x0a,0x11}},{'i',{0x04,0x00,0x0c,0x04,0x04,0x04,0x0e}},
    {'t',{0x08,0x08,0x1c,0x08,0x08,0x09,0x06}}};
constexpr int glyphScale=2,glyphAdvance=6*glyphScale;
static int TextWidth(const char* text) {int n=0;while(text[n])++n;return n*glyphAdvance-glyphScale;}
static void Text(int x,int y,const char* text,uint32_t colour) {
    for(;*text;++text,x+=glyphAdvance)for(const Glyph& g:font)if(g.ch==*text)
        for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(g.rows[row]&(0x10>>col))Fill(x+col*glyphScale,y+row*glyphScale,glyphScale,glyphScale,colour);
}
struct Button {const char* name;int x,y,w,h;bool lit;};
static Button buttons[3]={{"One",0,0,140,40,false},{"Two",0,0,140,40,false},{"Exit",0,0,140,40,false}};
static int sliderX,sliderY,sliderW,sliderPercent=40;
constexpr int sliderH=20,trackH=4,knobW=12;
constexpr uint32_t background=0x00203850,face=0x00d0d0d0,faceLit=0x0040c040,border=0x00101010,ink=0x00000000,track=0x00909090,knob=0x00f0f0f0,arrowFill=0x00ffffff,arrowEdge=0x00000000;
static void DrawButton(const Button& b) {
    Fill(b.x,b.y,b.w,b.h,border);Fill(b.x+2,b.y+2,b.w-4,b.h-4,b.lit?faceLit:face);
    Text(b.x+(b.w-TextWidth(b.name))/2,b.y+(b.h-7*glyphScale)/2,b.name,ink);
}
static void DrawSlider() {
    Fill(sliderX,sliderY,sliderW,sliderH,background);
    Fill(sliderX,sliderY+(sliderH-trackH)/2,sliderW,trackH,track);
    Fill(sliderX+(sliderW-knobW)*sliderPercent/100,sliderY,knobW,sliderH,knob);
}
static void DrawControls() {for(const Button& b:buttons)DrawButton(b);DrawSlider();}
constexpr int arrowW=12,arrowH=18;
// The arrow: row r has r+1 pixels down to row 11, then the tail. Tip at (x,y).
static void DrawArrow(int x,int y) {
    for(int r=0;r<arrowH;++r) {
        const int w=r<12?r+1:(r<15?5:3),from=r<12?0:(r<15?3:5);
        Fill(x+from,y+r,w,1,arrowEdge);
        if(w>2)Fill(x+from+1,y+r,w-2,1,arrowFill);
    }
}
static void MovePointer(int fromX,int fromY,int toX,int toY) {
    Fill(fromX,fromY,arrowW+1,arrowH,background);
    DrawControls();
    DrawArrow(toX,toY);
}
static bool Has(const char16_t* options,size_t bytes,const char* word) {
    if(!options)return false;
    const size_t n=bytes/2;size_t length=0;while(word[length])++length;
    for(size_t i=0;i+length<=n;++i) {size_t k=0;while(k<length && (options[i+k]|0x20)==char16_t(word[k]))++k;if(k==length)return true;}
    return false;
}
static bool Escape() {
    efi_input_key key{};
    while(st->ConIn && st->ConIn->ReadKeyStroke(st->ConIn,&key)==EFI_SUCCESS)if(key.ScanCode==23)return true;
    return false;
}
extern "C" efi_status EFIAPI efi_main(efi_handle image,efi_system_table* table) {
    st=table;boot=table->BootServices;
    if(boot->LocateProtocol(&gopGuid,nullptr,(void**)&gop)!=EFI_SUCCESS || !gop) {Port("PPROBE error no graphics\r\n");return EFI_UNSUPPORTED;}
#ifdef PPROBE_WAIT
    bool wait=true;
#else
    bool wait=false;
#endif
    efi_loaded_image_protocol* loaded=nullptr;
    if(boot->HandleProtocol(image,&loadedImageGuid,(void**)&loaded)==EFI_SUCCESS && loaded) {
        if(Has((const char16_t*)loaded->LoadOptions,loaded->LoadOptionsSize,"wait"))wait=true;
        if(Has((const char16_t*)loaded->LoadOptions,loaded->LoadOptionsSize,"poll"))wait=false;
    }
    SimplePointer* simple=nullptr;AbsolutePointer* absolute=nullptr;
    if(boot->LocateProtocol(&simplePointerGuid,nullptr,(void**)&simple)!=EFI_SUCCESS)simple=nullptr;
    if(!simple && boot->LocateProtocol(&absolutePointerGuid,nullptr,(void**)&absolute)!=EFI_SUCCESS)absolute=nullptr;
    if(!simple && !absolute) {Port("PPROBE error no pointer protocol\r\n");return EFI_UNSUPPORTED;}
    const int W=int(gop->Mode->info->horizontal),H=int(gop->Mode->info->vertical);
    // rEFInd divides by the resolution as it is; a driver that reports 0 would stop it. The probe uses 1 then.
    const long long resolutionX=simple && simple->Mode && simple->Mode->resolutionX?(long long)simple->Mode->resolutionX:1;
    const long long resolutionY=simple && simple->Mode && simple->Mode->resolutionY?(long long)simple->Mode->resolutionY:1;
    constexpr long long speed=4;
    Port("PPROBE start mode=");Port(wait?"wait":"poll");Port(" protocol=");Port(simple?"simple":"absolute");Port(" screen");Number(W);Number(H);
    Port(" resolution");Number(resolutionX);Number(resolutionY);Port("\r\n");
    for(int i=0;i<3;++i) {buttons[i].x=W/2-250+i*180;buttons[i].y=H/2-20;}
    sliderW=300;sliderX=W/2-sliderW/2;sliderY=H/2+80;
    Fill(0,0,W,H,background);
    for(const Button& b:buttons)Line("control",b.name,b.x,b.y,b.w,b.h,4);
    Line("control","slider",sliderX,sliderY,sliderW,sliderH,4);
    int x=W/2,y=H/4;
    DrawControls();DrawArrow(x,y);
    Line("pointer",nullptr,x,y);
    // The event is read once and kept, like a program that caches it.
    efi_event events[3];size_t eventCount=0;efi_event timer=nullptr;
    if(wait) {
        events[eventCount++]=simple?simple->WaitForInput:absolute->WaitForInput;
        if(st->ConIn)events[eventCount++]=st->ConIn->WaitForKey;
        if(boot->CreateEvent(EVT_TIMER,0,nullptr,nullptr,&timer)==EFI_SUCCESS && timer) {boot->SetTimer(timer,TimerPeriodic,10000000);events[eventCount++]=timer;}
    }
    bool held=false;const char* end="escape";
    for(;;) {
        if(wait) {size_t index=0;boot->WaitForEvent(eventCount,events,&index);}
        else boot->Stall(10000);
        if(Escape())break;
        int nextX=x,nextY=y;bool down=held,report=false;
        if(simple) {
            SimplePointerState state{};
            if(simple->GetState(simple,&state)==EFI_SUCCESS) {
                report=true;
                nextX=int(x+(long long)state.relativeX*speed/resolutionX);nextY=int(y+(long long)state.relativeY*speed/resolutionY);
                down=state.leftButton!=0;
            }
        } else {
            AbsolutePointerState state{};
            if(absolute->GetState(absolute,&state)==EFI_SUCCESS) {
                report=true;
                const AbsolutePointerMode* mode=absolute->Mode;
                const uint64_t spanX=mode && mode->maxX>mode->minX?mode->maxX-mode->minX:0,spanY=mode && mode->maxY>mode->minY?mode->maxY-mode->minY:0;
                if(spanX)nextX=int((state.x-mode->minX)*uint64_t(W)/spanX);
                if(spanY)nextY=int((state.y-mode->minY)*uint64_t(H)/spanY);
                down=(state.activeButtons&1)!=0;   // EFI_ABSP_TouchActive
            }
        }
        if(!report)continue;
        if(nextX<0)nextX=0;
        if(nextY<0)nextY=0;
        if(nextX>W-1)nextX=W-1;
        if(nextY>H-1)nextY=H-1;
        bool exit=false;
        const bool moved=nextX!=x || nextY!=y,clicked=held && !down;
        if(clicked) {
            const char* hit=nullptr;
            for(Button& b:buttons)if(nextX>=b.x && nextX<b.x+b.w && nextY>=b.y && nextY<b.y+b.h) {b.lit=!b.lit;hit=b.name;if(&b==&buttons[2])exit=true;}
            if(hit)Line("click",hit,nextX,nextY);
            else if(nextX>=sliderX && nextX<sliderX+sliderW && nextY>=sliderY && nextY<sliderY+sliderH) {
                int percent=(nextX-sliderX-knobW/2)*100/(sliderW-knobW);
                sliderPercent=percent<0?0:percent>100?100:percent;
                Line("slider",nullptr,sliderPercent,nextX,nextY,0,3);
            } else Line("click","none",nextX,nextY);
        }
        held=down;
        if(moved || clicked)MovePointer(x,y,nextX,nextY);
        if(moved) {x=nextX;y=nextY;Line("pointer",nullptr,x,y);}
        if(exit) {end="exit";boot->Stall(500000);break;}
    }
    if(timer)boot->CloseEvent(timer);
    Port("PPROBE end ");Port(end);Port("\r\n");
    Fill(0,0,W,H,0);
    return EFI_SUCCESS;
}
