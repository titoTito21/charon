// TrueType glyph rasterization for Charon's resident reader (r361).
// GPL-2.0-or-later; stb_truetype.h (third_party/stb) is public domain / MIT.
//
// Charon reads text that boot programs draw with their own TrueType fonts
// (Windows Boot Manager: Segoe UI and others from \EFI\Microsoft\Boot\Fonts on
// the user's disk). The fonts are never shipped: the reader loads them from
// the disk at run time and renders glyph templates with stb_truetype. All
// memory comes from a fixed arena reset before each glyph, so no allocator
// is needed in a timer callback and nothing leaks.
#include <stddef.h>
#include "CharonTtf.h"

double floor(double);
double sqrt(double);
double pow(double,double);
double fmod(double,double);
double cos(double);
double fabs(double);
void* memcpy(void*,const void*,size_t);
void* memset(void*,int,size_t);
size_t strlen(const char*);

static unsigned char charonTtfArena[256*1024] __attribute__((aligned(16)));
static size_t charonTtfUsed;
static int charonTtfFailed;

static void* CharonTtfAlloc(size_t bytes) {
    bytes=(bytes+15)&~(size_t)15;
    if(bytes>sizeof(charonTtfArena)-charonTtfUsed) {charonTtfFailed=1;return NULL;}
    void* p=charonTtfArena+charonTtfUsed;charonTtfUsed+=bytes;return p;
}
static double CharonTtfAcosUnused(double x) {(void)x;return 0;}  // SDF only, never called

#define STBTT_ifloor(x)   ((int)floor(x))
#define STBTT_iceil(x)    (-(int)floor(-(x)))
#define STBTT_sqrt(x)     sqrt(x)
#define STBTT_pow(x,y)    pow(x,y)
#define STBTT_fmod(x,y)   fmod(x,y)
#define STBTT_cos(x)      cos(x)
#define STBTT_acos(x)     CharonTtfAcosUnused(x)
#define STBTT_fabs(x)     fabs(x)
#define STBTT_malloc(x,u) ((void)(u),CharonTtfAlloc(x))
#define STBTT_free(x,u)   ((void)(u),(void)(x))
#define STBTT_assert(x)   ((void)0)
#define STBTT_strlen(x)   strlen(x)
#define STBTT_memcpy      memcpy
#define STBTT_memset      memset
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
#include "third_party/stb/stb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

int charon_ttf_init(stbtt_fontinfo* info,const unsigned char* data,size_t size) {
    if(!info || !data || size<12)return 0;
    // stb trusts table offsets: refuse files whose tables leave the buffer.
    const unsigned tables=(unsigned)data[4]<<8|data[5];
    if(12+16*(size_t)tables>size)return 0;
    for(unsigned i=0;i<tables;++i) {
        const unsigned char* t=data+12+16*i;
        const size_t offset=(size_t)t[8]<<24|(size_t)t[9]<<16|(size_t)t[10]<<8|t[11];
        const size_t length=(size_t)t[12]<<24|(size_t)t[13]<<16|(size_t)t[14]<<8|t[15];
        if(offset>size || length>size-offset)return 0;
    }
    charonTtfUsed=0;charonTtfFailed=0;
    return stbtt_InitFont(info,data,0)!=0;
}

int charon_ttf_render(const stbtt_fontinfo* info,int glyph,float scale,float scaleY,float shiftX,float shiftY,unsigned char* out,int capacity,CharonTtfBox* box) {
    int x0,y0,x1,y1;
    charonTtfUsed=0;charonTtfFailed=0;
    stbtt_GetGlyphBitmapBoxSubpixel(info,glyph,scale,scaleY,shiftX,shiftY,&x0,&y0,&x1,&y1);
    box->x0=x0;box->y0=y0;box->width=x1>x0?x1-x0:0;box->height=y1>y0?y1-y0:0;
    if(!box->width || !box->height)return 1;
    if(box->width>255 || box->height>255 || box->width*box->height>capacity)return 0;
    stbtt_MakeGlyphBitmapSubpixel(info,out,box->width,box->height,box->width,scale,scaleY,shiftX,shiftY,glyph);
    return !charonTtfFailed;
}
