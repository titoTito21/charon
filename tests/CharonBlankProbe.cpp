// Test-only UEFI application for Charon's resident reader (r397): leaves the
// screen black, then all-blue, then returns. Draws only a full-screen GOP fill
// and never touches ConOut, so the reader sees a blank/blue frame buffer with
// no text - the "black screen / blue screen of death" case. No ExitBootServices,
// no key waiting: each phase is a fixed hold so the reader's dwell can fire.
#include <efi/system-table.h>
#include "../src/efi/CharonGraphicsTypes.h"
using namespace hakefi;
extern "C" void* memset(void* d,int v,size_t n){auto* p=(unsigned char*)d;while(n--)*p++=(unsigned char)v;return d;}
extern "C" void* memcpy(void* d,const void* s,size_t n){auto* a=(unsigned char*)d;auto* b=(const unsigned char*)s;while(n--)*a++=*b++;return d;}
extern "C" efi_status EFIAPI efi_main(efi_handle,efi_system_table* table) {
    auto* boot=table->BootServices;
    Gop* gop=nullptr;
    if(boot->LocateProtocol(&gopGuid,nullptr,(void**)&gop)!=EFI_SUCCESS || !gop)return EFI_UNSUPPORTED;
    const size_t W=gop->Mode->info->horizontal,H=gop->Mode->info->vertical;
    uint32_t black=0x00000000,blue=0x000000c0,bg=0x00303060,white=0x00ffffff;   // 0x00RRGGBB in Blt pixels
    // Black for ~10 s (reader: "Czarny ekran" + low menacing tone).
    gop->Blt(gop,&black,bltVideoFill,0,0,0,0,W,H,0);
    for(unsigned i=0;i<100;++i)boot->Stall(100000);
    // Blue for ~10 s (reader: "Niebieski ekran śmierci").
    gop->Blt(gop,&blue,bltVideoFill,0,0,0,0,W,H,0);
    for(unsigned i=0;i<100;++i)boot->Stall(100000);
    // Strictly graphical: a background and three separate blocks, one
    // highlighted, with no text. Arrows / Tab / Shift+Tab move the highlight
    // (reader: "Ekran graficzny. 3 obszary graficzne... Zaznaczony N z 3.").
    uint32_t grey=0x00808080;
    const size_t bw=W/8,bh=H/6;const size_t bx[3]={W/8,W/2-bw/2,W-W/8-bw};
    unsigned sel=0;
    auto draw=[&](){
        gop->Blt(gop,&bg,bltVideoFill,0,0,0,0,W,H,0);
        for(unsigned j=0;j<3;++j){uint32_t c=(j==sel)?white:grey;gop->Blt(gop,&c,bltVideoFill,0,0,bx[j],H/8,bw,bh,0);}
    };
    draw();
    for(unsigned i=0;i<220;++i){
        efi_input_key k{};
        if(table->ConIn->ReadKeyStroke(table->ConIn,&k)==EFI_SUCCESS){
            if(k.ScanCode==0x17)break;                       // Esc
            else if(k.ScanCode==0x03||k.ScanCode==0x02){sel=(sel+1)%3;draw();}   // Right/Down
            else if(k.ScanCode==0x04||k.ScanCode==0x01){sel=(sel+2)%3;draw();}   // Left/Up
            else if(k.UnicodeChar==9){sel=(sel+1)%3;draw();}                     // Tab
        }
        boot->Stall(100000);
    }
    return EFI_SUCCESS;
}
