// Host test for src/efi/CharonAaOcr.h (r359): text alpha-blended with the
// rEFInd font strips (Liberation Mono 14/12/28/24) at unaligned positions and
// colours, two touching lines in one ink block, a line over a picture, and
// a screen with only pictures. Optional: CharonAaOcrTests FILE.ppm prints
// what is read from a screenshot.
#include "../src/efi/CharonAaOcr.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(false)
using namespace hakefi;
struct Line {std::u16string text;size_t x,y,height;uint32_t fg,bg;};
static uint32_t Blend(uint32_t bg,uint32_t fg,unsigned a) {
    uint32_t out=0;
    for(int s=0;s<24;s+=8) {const unsigned b=(bg>>s)&255,f=(fg>>s)&255;out|=((b*(255-a)+f*a+127)/255)<<s;}
    return out;
}
static void Draw(std::vector<uint32_t>& px,size_t stride,unsigned font,size_t x,size_t y,const char* text,uint32_t fg) {
    const AaFont& f=aaFonts[font];
    for(;*text;++text,x+=f.width) {
        const unsigned g=unsigned(*text)-0x20;
        for(unsigned r=0;r<f.height;++r)for(unsigned c=0;c<f.width;++c) {
            uint32_t& p=px[(y+r)*stride+x+c];p=Blend(p,fg,f.alpha[(g*f.height+r)*f.width+c]);
        }
    }
}
static std::u16string U(const char* s) {std::u16string r;while(*s)r+=char16_t(*s++);return r;}
int main(int argc,char** argv) {
    std::vector<Line> lines;
    auto emit=[&](const char16_t* t,size_t x,size_t y,uint32_t fg,uint32_t bg,size_t h) {lines.push_back({t,x,y,h,fg,bg});};
    auto find=[&](const char* s)->const Line* {for(auto& l:lines)if(l.text==U(s))return &l;return nullptr;};
    static AlphaOcr ocr;
    if(argc>1) {
        FILE* f=fopen(argv[1],"rb");int w,h,m;if(!f || fscanf(f,"P6 %d %d %d",&w,&h,&m)!=3)return 2;fgetc(f);
        std::vector<uint32_t> px(size_t(w)*h);for(auto& p:px) {int r=fgetc(f),g=fgetc(f),b=fgetc(f);p=uint32_t(r<<16|g<<8|b);}
        printf("recognized %zu\n",ocr.Recognize(px.data(),w,h,w,0,0,emit,[]() {}));
        for(auto& l:lines) {printf("[%zu,%zu h%zu] ",l.x,l.y,l.height);for(auto c:l.text)putchar(c<128?char(c):'?');putchar('\n');}
        return 0;
    }
    const size_t W=1280,H=520;std::vector<uint32_t> px(W*H,0xd0d0d0);
    Draw(px,W,0,483,40,"Boot EFI\\probe\\GPROBE.EFI from GRUBTEST",0x000000);
    Draw(px,W,0,531,58,"Automatic boot in 15 seconds",0x000000);
    // Two lines touching (hint lines at the bottom of rEFInd's screen).
    Draw(px,W,0,460,100,"Use arrow keys to move cursor; Enter to boot;",0x000000);
    Draw(px,W,0,385,113,"Insert, Tab, or F2 for more options; Esc or Backspace to refresh",0x000000);
    // Large font, light text on a dark band.
    for(size_t y=160;y<230;++y)for(size_t x=0;x<W;++x)px[y*W+x]=0x203060;
    Draw(px,W,2,101,180,"Reboot Computer",0xffffff);
    Draw(px,W,1,37,260,"Boot Microsoft EFI boot from EFI",0x101010);
    // A picture: noise rows give nothing.
    for(size_t y=300;y<380;++y)for(size_t x=0;x<W;++x)px[y*W+x]=uint32_t((x*2654435761u)^(y*40503u))&0xffffff;
    // Theme wallpaper: a noisy gradient under white Ubuntu Mono 16 text.
    for(size_t y=400;y<460;++y)for(size_t x=0;x<W;++x) {
        const unsigned n=unsigned((x*1103515245u+y*12345u)>>16)%11;
        const unsigned r=40+unsigned(x*60/W)+n,g=30+unsigned(y-400)/2+n,b=90+unsigned(x*40/W)+n;
        px[y*W+x]=(r<<16)|(g<<8)|b;
    }
    Draw(px,W,6,211,421,"Boot Ubuntu from wallpaper",0xffffff);
    Draw(px,W,10,77,480,"Nimbus Mono line 14",0x000000);
    const size_t count=ocr.Recognize(px.data(),W,H,W,0,0,emit,[]() {});
    for(auto& l:lines) {printf("[%zu,%zu h%zu %06x/%06x] ",l.x,l.y,l.height,unsigned(l.fg),unsigned(l.bg));for(auto c:l.text)putchar(c<128?char(c):'?');putchar('\n');}
    const Line* label=find("Boot EFI\\probe\\GPROBE.EFI from GRUBTEST");
    CHECK(label && label->x==483 && label->y==40 && label->height==14);
    CHECK(find("Automatic boot in 15 seconds"));
    CHECK(find("Use arrow keys to move cursor; Enter to boot;"));
    CHECK(find("Insert, Tab, or F2 for more options; Esc or Backspace to refresh"));
    const Line* big=find("Reboot Computer");CHECK(big && big->height==28 && big->fg==0xffffff);
    CHECK(find("Boot Microsoft EFI boot from EFI"));
    const Line* wall=find("Boot Ubuntu from wallpaper");CHECK(wall && wall->x==211 && wall->height==16);
    const Line* nimbus=find("Nimbus Mono line 14");CHECK(nimbus && nimbus->x==77);
    CHECK(lines.size()==8 && count>=180);
    puts("PASS anti-aliased OCR: rEFInd label, countdown, touching hint lines, 28 px light-on-dark, 12 px, Ubuntu Mono 16 over a noisy wallpaper, Nimbus Mono 14, picture rows ignored");
}
