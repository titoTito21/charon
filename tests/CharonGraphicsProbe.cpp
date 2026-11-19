// Test-only graphical UEFI application for Charon's resident reader (r357).
// Draws only through GOP and the HII font service (no console text): a menu
// with a highlighted entry moved by arrows, a growing progress bar, and a
// picture without text. Enter on an entry continues; returns to the caller.
#include <efi/system-table.h>
#include "../src/efi/CharonGraphicsTypes.h"
using namespace hakefi;
extern "C" void* memset(void* d,int v,size_t n){auto* p=(unsigned char*)d;while(n--)*p++=(unsigned char)v;return d;}
extern "C" void* memcpy(void* d,const void* s,size_t n){auto* a=(unsigned char*)d;auto* b=(const unsigned char*)s;while(n--)*a++=*b++;return d;}
struct ImageOutput {uint16_t width,height;Gop* screen;};
struct FullFontDisplayInfo {uint32_t foreground,background,mask;uint32_t style;uint16_t size;char16_t name[2];};
static Gop* gop;static HiiFont* font;static efi_boot_services* boot;static efi_system_table* st;
static uint32_t picture[320*200];
static void Fill(size_t x,size_t y,size_t w,size_t h,uint32_t color) {gop->Blt(gop,&color,bltVideoFill,0,0,x,y,w,h,0);}
static void Text(size_t x,size_t y,const char16_t* text,uint32_t fg,uint32_t bg) {
    ImageOutput screen={uint16_t(gop->Mode->info->horizontal),uint16_t(gop->Mode->info->vertical),gop};
    ImageOutput* output=&screen;
    FullFontDisplayInfo info={fg,bg,0x07,0,0,{0,0}}; // system font, size and style
    font->StringToImage(font,hiiDirectToScreen,text,(const FontDisplayInfo*)&info,(void**)&output,x,y,nullptr,nullptr,nullptr);
}
static uint16_t Key() {
    efi_input_key key{};size_t index;
    for(;;) {
        boot->WaitForEvent(1,&st->ConIn->WaitForKey,&index);
        if(st->ConIn->ReadKeyStroke(st->ConIn,&key)==EFI_SUCCESS)return key.ScanCode?uint16_t(0x100|key.ScanCode):key.UnicodeChar;
    }
}
extern "C" efi_status EFIAPI efi_main(efi_handle,efi_system_table* table) {
    st=table;boot=table->BootServices;
    if(boot->LocateProtocol(&gopGuid,nullptr,(void**)&gop)!=EFI_SUCCESS || !gop)return EFI_UNSUPPORTED;
    if(boot->LocateProtocol(&hiiFontGuid,nullptr,(void**)&font)!=EFI_SUCCESS || !font)return EFI_UNSUPPORTED;
    const uint32_t navy=0x00203060,white=0x00ffffff,grey=0x00c0c0c0,black=0;
    const size_t W=gop->Mode->info->horizontal,H=gop->Mode->info->vertical;
    // Screen 1: a graphical menu.
    Fill(0,0,W,H,navy);
    Text(96,95,u"Graphical Loader 2.0",white,navy);
    const char16_t* items[]={u"Start normally",u"Start in safe mode",u"Memory test"};
    size_t selected=0;
    auto draw=[&](size_t i) {
        const bool on=i==selected;
        Fill(88,190+i*38,320,26,on?grey:navy);
        Text(96,190+i*38,items[i],on?black:white,on?grey:navy);
    };
    for(size_t i=0;i<3;++i)draw(i);
    Text(96,342,u"Use arrows and Enter",grey,navy);
    for(;;) {
        const uint16_t key=Key();
        if(key==13)break;
        const size_t before=selected;
        if(key==0x102 && selected<2)++selected;
        if(key==0x101 && selected)--selected;
        if(before!=selected) {draw(before);draw(selected);}
    }
    // Screen 2: progress bar without any percentage text.
    Fill(0,0,W,H,navy);
    Text(96,152,u"Installing updates",white,navy);
    Fill(96,228,400,16,grey);
    for(size_t width=20;width<=400;width+=20) {Fill(96,228,width,16,0x0000a0ff);boot->Stall(250000);}
    boot->Stall(800000);
    // Screen 3: a picture only.
    Fill(0,0,W,H,black);
    for(size_t y=0;y<200;++y)for(size_t x=0;x<320;++x)picture[y*320+x]=uint32_t((x*255/320)<<16|(y*255/200)<<8|0x40);
    for(int frame=0;frame<3;++frame) {
        gop->Blt(gop,picture,bltBufferToVideo,0,0,(W-320)/2,(H-200)/2,320,200,0);boot->Stall(1500000);
    }
    boot->Stall(2500000);
    return EFI_SUCCESS;
}
