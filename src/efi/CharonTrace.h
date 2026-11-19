#pragma once
#include <stdint.h>
// Opt-in x86 QEMU COM1 diagnostics. Production builds do not touch I/O ports.
// Each byte has a finite polling bound; this is not an audio-thread logger.
namespace charontrace {
#ifdef HAK_CHARON_TRACE
inline void Byte(unsigned char value) {
    for(unsigned n=0;n<1024;++n) {
        unsigned char ready;
        __asm__ volatile("inb %1,%0":"=a"(ready):"Nd"((unsigned short)0x3fd));
        if(ready&0x20) {
            __asm__ volatile("outb %0,%1"::"a"(value),"Nd"((unsigned short)0x3f8));
            return;
        }
    }
}
inline void Text(const char* text) {
    if(text)for(unsigned i=0;text[i]&&i<256;++i)Byte((unsigned char)text[i]);
    Byte('\r');Byte('\n');
}
inline void Value(const char* label,uint64_t value) {
    if(label)for(unsigned i=0;label[i]&&i<80;++i)Byte((unsigned char)label[i]);
    Byte('=');for(int n=60;n>=0;n-=4)Byte("0123456789abcdef"[(value>>n)&15]);
    Byte('\r');Byte('\n');
}
#else
inline void Text(const char*) {}
inline void Value(const char*,uint64_t) {}
#endif
}
