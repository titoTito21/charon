// Test-only UEFI application for Charon's resident layer (r355).
// Behaves like a text-mode loader: a title, a progress line that redraws in
// place, then GetMemoryMap + ExitBootServices (retrying like real loaders when
// the map key changes). Reports to COM1 (0x3f8) and halts. Never installed.
#include <efi/system-table.h>
static void Port(const char* text) {
    for(;*text;++text) {
        for(unsigned i=0;i<100000;++i) {unsigned char s;__asm__ volatile("inb %1,%0":"=a"(s):"Nd"((unsigned short)0x3fd));if(s&0x20)break;}
        __asm__ volatile("outb %0,%1"::"a"((unsigned char)*text),"Nd"((unsigned short)0x3f8));
    }
}
static void Hex(unsigned long long value) {char t[19]={'0','x'};for(int i=0;i<16;++i)t[2+i]="0123456789ABCDEF"[(value>>((15-i)*4))&15];t[18]=0;Port(t);}
static unsigned char map[64*1024];
extern "C" void* memset(void* d,int v,size_t n){auto* p=(unsigned char*)d;while(n--)*p++=(unsigned char)v;return d;}
extern "C" void* memcpy(void* d,const void* s,size_t n){auto* a=(unsigned char*)d;auto* b=(const unsigned char*)s;while(n--)*a++=*b++;return d;}
extern "C" efi_status EFIAPI efi_main(efi_handle image,efi_system_table* table) {
    auto* out=table->ConOut;auto* boot=table->BootServices;
    out->ClearScreen(out);
    out->OutputString(out,(char16_t*)u"Probe loader 1.0\r\n\r\n");
    for(int percent=0;percent<=100;percent+=5) {
        char16_t line[64]=u"Loading system files: ";size_t at=0;while(line[at])++at;
        if(percent>=100)line[at++]=u'1';
        if(percent>=10)line[at++]=char16_t(u'0'+(percent/10)%10);
        line[at++]=char16_t(u'0'+percent%10);line[at++]=u'%';line[at++]=u' ';line[at]=0;
        out->SetCursorPosition(out,0,2);out->OutputString(out,line);
        boot->Stall(400000);
    }
    out->OutputString(out,(char16_t*)u"\r\nStarting the operating system.\r\n");
    boot->Stall(1500000);
    Port("PROBE before ExitBootServices\r\n");
    efi_status status=EFI_INVALID_PARAMETER;
    for(int attempt=0;attempt<8 && status!=EFI_SUCCESS;++attempt) {
        size_t size=sizeof(map),key=0,descriptor=0;uint32_t version=0;
        if(boot->GetMemoryMap(&size,(efi_memory_descriptor*)map,&key,&descriptor,&version)!=EFI_SUCCESS)continue;
        status=boot->ExitBootServices(image,key);
        Port("PROBE ExitBootServices attempt status=");Hex(status);Port("\r\n");
    }
    if(status==EFI_SUCCESS)Port("PROBE AFTER EBS OK\r\n");
    for(;;)__asm__ volatile("cli; hlt");
}
