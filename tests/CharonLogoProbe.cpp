// Test-only UEFI application for Charon's resident reader (r400): pictures
// with no text. Phase 1: a blue "logo" in the middle with a small progress
// mark under it (a loading screen). Phase 2: three coloured icons in a row,
// the highlight moving. Each phase is held so the reader's dwell can fire.
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
    uint32_t black=0,blue=0x000078d4,white=0x00ffffff,red=0x00d02020,green=0x0020b040,yellow=0x00e0d020;
    // Phase 1: logo + progress mark.
    gop->Blt(gop,&black,bltVideoFill,0,0,0,0,W,H,0);
    gop->Blt(gop,&blue,bltVideoFill,0,0,W/2-W/12,H/3-H/10,W/6,H/5,0);
    gop->Blt(gop,&white,bltVideoFill,0,0,W/2-8,H*3/4,16,16,0);
    boot->Stall(9000000);
    // Phase 2: three icons, the highlight (white frame fill) moves.
    for(unsigned sel=0;sel<2;++sel) {
        gop->Blt(gop,&black,bltVideoFill,0,0,0,0,W,H,0);
        const uint32_t colours[3]={red,green,yellow};
        for(unsigned j=0;j<3;++j) {
            uint32_t c=j==sel?white:colours[j];
            gop->Blt(gop,&c,bltVideoFill,0,0,W/8+j*W/4,H/2-H/10,W/8,H/5,0);
        }
        boot->Stall(9000000);
    }
    gop->Blt(gop,&black,bltVideoFill,0,0,0,0,W,H,0);
    return EFI_SUCCESS;
}
