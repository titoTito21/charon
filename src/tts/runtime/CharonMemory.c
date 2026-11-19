/* Freestanding byte operations required by the TVox compiler output.
 * Build with -ffreestanding -fno-builtin so loops cannot become self calls.
 * Standard C memory contracts apply; memmove also supports overlap. */
#include <stddef.h>
#include <stdint.h>

void* memcpy(void* destination, const void* source, size_t count)
{
    unsigned char* d = (unsigned char*)destination;
    const unsigned char* s = (const unsigned char*)source;
    for (size_t i = 0; i < count; ++i) d[i] = s[i];
    return destination;
}

void* memmove(void* destination, const void* source, size_t count)
{
    unsigned char* d = (unsigned char*)destination;
    const unsigned char* s = (const unsigned char*)source;
    /* EFI x64 uses a flat address space. Avoid relational comparisons of
     * unrelated C object pointers and avoid end-address overflow. */
    if ((uintptr_t)d > (uintptr_t)s && (uintptr_t)d - (uintptr_t)s < count) {
        while (count) { --count; d[count] = s[count]; }
    } else {
        for (size_t i = 0; i < count; ++i) d[i] = s[i];
    }
    return destination;
}

void* memset(void* destination, int value, size_t count)
{
    unsigned char* d = (unsigned char*)destination;
    for (size_t i = 0; i < count; ++i) d[i] = (unsigned char)value;
    return destination;
}

int memcmp(const void* left, const void* right, size_t count)
{
    const unsigned char* a = (const unsigned char*)left;
    const unsigned char* b = (const unsigned char*)right;
    for (size_t i = 0; i < count; ++i)
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    return 0;
}

size_t strlen(const char* text)
{
    size_t count = 0;
    while (text[count]) ++count;
    return count;
}
