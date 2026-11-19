// Test-only UEFI application for Charon's reader (r389): framed message
// windows drawn through the console, one per key: an error window with an
// [ OK ] button, a confirmation dialog with [ Yes ] [ No ], a plain notice.
// Escape exits. GPL-2.0-or-later.
#include <efi/system-table.h>
extern "C" void* memset(void* d,int v,size_t n){auto* p=(unsigned char*)d;while(n--)*p++=(unsigned char)v;return d;}
extern "C" void* memcpy(void* d,const void* s,size_t n){auto* a=(unsigned char*)d;auto* b=(const unsigned char*)s;while(n--)*a++=*b++;return d;}
static efi_system_table* st;
enum : size_t {black=0,blue=1,red=4,lightgray=7,white=15,yellow=14};
static size_t Attr(size_t fg,size_t bg) {return fg|(bg<<4);}
static void At(size_t col,size_t row,const char16_t* text,size_t attr) {
    st->ConOut->SetAttribute(st->ConOut,attr);st->ConOut->SetCursorPosition(st->ConOut,col,row);
    st->ConOut->OutputString(st->ConOut,(char16_t*)text);
}
static void Box(size_t left,size_t top,size_t width,size_t height,const char16_t* title,size_t attr) {
    static char16_t line[120];
    for(size_t r=0;r<height;++r) {
        size_t n=0;
        const char16_t l=r==0?u'┌':r+1==height?u'└':u'│',m=(r==0 || r+1==height)?u'─':u' ',rr=r==0?u'┐':r+1==height?u'┘':u'│';
        line[n++]=l;for(size_t c=2;c<width;++c)line[n++]=m;line[n++]=rr;line[n]=0;
        At(left,top+r,line,attr);
    }
    if(title) {static char16_t t[60];size_t n=0;t[n++]=u' ';for(const char16_t* p=title;*p;++p)t[n++]=*p;t[n++]=u' ';t[n]=0;At(left+2,top,t,attr);}
}
static void Page() {
    st->ConOut->SetAttribute(st->ConOut,Attr(lightgray,blue));st->ConOut->ClearScreen(st->ConOut);
    At(2,1,u"Setup Utility - Charon dialog test",Attr(white,blue));
    At(2,3,u"Boot Order",Attr(lightgray,blue));
    At(2,4,u"Secure Boot              [Enabled]",Attr(lightgray,blue));
    At(2,22,u"F10: Save  Esc: Exit",Attr(white,blue));
}
static void Wait() {efi_input_key key{};size_t index;for(;;){st->BootServices->WaitForEvent(1,&st->ConIn->WaitForKey,&index);if(st->ConIn->ReadKeyStroke(st->ConIn,&key)==EFI_SUCCESS)break;}}
extern "C" efi_status EFIAPI efi_main(efi_handle,efi_system_table* table) {
    st=table;st->ConOut->EnableCursor(st->ConOut,false);
    Page();Wait();
    Box(15,8,50,7,u"Error",Attr(white,red));
    At(17,10,u"Error: the boot device could not be found.",Attr(white,red));
    At(37,12,u"[ OK ]",Attr(yellow,red));
    Wait();Page();Wait();
    Box(15,8,50,7,u"Save & Exit",Attr(black,lightgray));
    At(17,10,u"Save configuration changes and exit now?",Attr(black,lightgray));
    At(28,12,u"[ Yes ]",Attr(black,lightgray));At(42,12,u"[ No ]",Attr(black,lightgray));
    Wait();Page();Wait();
    Box(15,8,50,6,u"Information",Attr(black,lightgray));
    At(17,10,u"The settings were saved.",Attr(black,lightgray));
    Wait();
    st->ConOut->SetAttribute(st->ConOut,Attr(lightgray,black));st->ConOut->ClearScreen(st->ConOut);
    return EFI_SUCCESS;
}
