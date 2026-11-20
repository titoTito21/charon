// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - freestanding helpers, code page 437, calendar.
#include "dos.h"

void d_memcpy(void* d, const void* s, size_t n)
{
    u8* dd = (u8*)d; const u8* ss = (const u8*)s;
    while (n--) *dd++ = *ss++;
}
void d_memmove(void* d, const void* s, size_t n)
{
    u8* dd = (u8*)d; const u8* ss = (const u8*)s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
}
void d_memset(void* d, int c, size_t n)
{
    u8* dd = (u8*)d;
    while (n--) *dd++ = (u8)c;
}
int d_memcmp(const void* a, const void* b, size_t n)
{
    const u8* x = (const u8*)a; const u8* y = (const u8*)b;
    for (; n--; x++, y++) if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}
#ifndef DOS_HOST
// The compiler may emit calls to these for structure copies.
extern "C" {
void* memcpy(void* d, const void* s, size_t n) { d_memcpy(d, s, n); return d; }
void* memmove(void* d, const void* s, size_t n) { d_memmove(d, s, n); return d; }
void* memset(void* d, int c, size_t n) { d_memset(d, c, n); return d; }
int memcmp(const void* a, const void* b, size_t n) { return d_memcmp(a, b, n); }
}
#endif

size_t d_strlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
int d_strcmp(const char* a, const char* b)
{
    for (; *a && *a == *b; a++, b++) {}
    return (u8)*a - (u8)*b;
}
char d_upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
int d_stricmp(const char* a, const char* b)
{
    for (; *a && d_upper(*a) == d_upper(*b); a++, b++) {}
    return (u8)d_upper(*a) - (u8)d_upper(*b);
}
int d_strnicmp(const char* a, const char* b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (d_upper(*a) != d_upper(*b)) return (u8)d_upper(*a) - (u8)d_upper(*b);
        if (!*a) break;
    }
    return 0;
}
void d_strcpy(char* d, const char* s, size_t cap)
{
    if (!cap) return;
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++) d[i] = s[i];
    d[i] = 0;
}
void d_strcat(char* d, const char* s, size_t cap)
{
    size_t n = d_strlen(d);
    if (n < cap) d_strcpy(d + n, s, cap - n);
}
void d_strupr(char* s) { for (; *s; s++) *s = d_upper(*s); }
size_t d_wlen(const dchar16* s) { size_t n = 0; while (s[n]) n++; return n; }
void d_wcpy(dchar16* d, const dchar16* s, size_t cap)
{
    if (!cap) return;
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++) d[i] = s[i];
    d[i] = 0;
}
void d_wcat(dchar16* d, const dchar16* s, size_t cap)
{
    size_t n = d_wlen(d);
    if (n < cap) d_wcpy(d + n, s, cap - n);
}
static dchar16 wupper(dchar16 c)
{
    if (c >= 'a' && c <= 'z') return (dchar16)(c - 32);
    if ((c >= 0xE0 && c <= 0xFE && c != 0xF7) ) return (dchar16)(c - 32);
    if (c >= 0x100 && c <= 0x17F && (c & 1) && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c;   // odd = capital in these runs
        return (dchar16)(c - 1);
    }
    if (((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) && !(c & 1)) return (dchar16)(c - 1);
    return c;
}
int d_wicmp(const dchar16* a, const dchar16* b)
{
    for (; *a && wupper(*a) == wupper(*b); a++, b++) {}
    return (int)wupper(*a) - (int)wupper(*b);
}
int d_utoa(u32 value, char* out, int base)
{
    char tmp[34]; int n = 0;
    do { u32 d = value % (u32)base; tmp[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10); value /= (u32)base; } while (value);
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
    return n;
}
int d_u64toa(u64 value, char* out)
{
    char tmp[24]; int n = 0;
    do { tmp[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
    return n;
}
bool d_atou(const char* s, u32* out)
{
    u32 v = 0; int n = 0;
    for (; *s >= '0' && *s <= '9'; s++, n++) v = v * 10 + (u32)(*s - '0');
    *out = v;
    return n > 0;
}

const u16 g_cp437[256] = {
    0x0020,0x263A,0x263B,0x2665,0x2666,0x2663,0x2660,0x2022,0x25D8,0x25CB,0x25D9,0x2642,0x2640,0x266A,0x266B,0x263C,
    0x25BA,0x25C4,0x2195,0x203C,0x00B6,0x00A7,0x25AC,0x21A8,0x2191,0x2193,0x2192,0x2190,0x221F,0x2194,0x25B2,0x25BC,
    0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002A,0x002B,0x002C,0x002D,0x002E,0x002F,
    0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003A,0x003B,0x003C,0x003D,0x003E,0x003F,
    0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004A,0x004B,0x004C,0x004D,0x004E,0x004F,
    0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005A,0x005B,0x005C,0x005D,0x005E,0x005F,
    0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006A,0x006B,0x006C,0x006D,0x006E,0x006F,
    0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007A,0x007B,0x007C,0x007D,0x007E,0x2302,
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0
};
dchar16 cp437_to_uni(u8 c) { return g_cp437[c]; }

// Best fit for the letters code page 437 does not have (Polish first).
static const u16 g_fit[][2] = {
    {0x0104,'A'},{0x0105,'a'},{0x0106,'C'},{0x0107,'c'},{0x0118,'E'},{0x0119,'e'},{0x0141,'L'},{0x0142,'l'},
    {0x0143,'N'},{0x0144,'n'},{0x00D3,'O'},{0x015A,'S'},{0x015B,'s'},{0x0179,'Z'},{0x017A,'z'},{0x017B,'Z'},
    {0x017C,'z'},{0x2018,'\''},{0x2019,'\''},{0x201C,'"'},{0x201D,'"'},{0x201E,'"'},{0x2013,'-'},{0x2014,'-'},
    {0x2026,'.'},{0x00C1,'A'},{0x00CD,'I'},{0x00DA,'U'},{0x00C0,'A'},{0x00C8,'E'},{0x00CA,'E'},{0x00CB,'E'}
};
u8 uni_to_cp437(dchar16 u, bool* exact)
{
    if (exact) *exact = true;
    if (u >= 0x20 && u < 0x7F) return (u8)u;
    for (int i = 0x80; i < 256; i++) if (g_cp437[i] == u) return (u8)i;
    for (int i = 1; i < 0x20; i++) if (g_cp437[i] == u) return (u8)i;
    if (u == 0x2302) return 0x7F;
    if (exact) *exact = false;
    for (size_t i = 0; i < sizeof g_fit / sizeof g_fit[0]; i++) if (g_fit[i][0] == u) return (u8)g_fit[i][1];
    return 0;
}

int utf8_next(const char** s)
{
    const u8* p = (const u8*)*s;
    int c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7) << 18) | ((p[0] & 0x3F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
    else if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 0xF) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
    else if (c >= 0xC0 && p[0]) { c = ((c & 0x1F) << 6) | (p[0] & 0x3F); p += 1; }
    *s = (const char*)p;
    return c;
}

int day_of_week(int y, int m, int d)
{
    static const u8 t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 1 || m > 12) return 0;
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
u32 tod_millis(const PlatTime* t)
{
    return (((u32)t->hour * 60 + t->minute) * 60 + t->second) * 1000 + (u32)t->hsec * 10;
}
