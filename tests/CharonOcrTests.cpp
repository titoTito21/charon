// Host test for src/efi/CharonOcr.h (r358): render known text with the
// template font at an unaligned grid offset and two colour schemes, then
// recognize it; also a variant glyph within the nearest-match bound and a
// picture without text.
#include "../src/efi/CharonOcr.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(false)
using namespace hakefi;
static const uint8_t* Glyph(char16_t ch) {for(unsigned i=0;i<glyphCount;++i)if(glyphCodes[i]==ch)return glyphRows[i];return nullptr;}
static void Draw(std::vector<uint32_t>& px,size_t stride,size_t x,size_t y,const char16_t* text,uint32_t fg,uint32_t bg) {
    for(;*text;++text,x+=8) {
        const uint8_t* g=Glyph(*text);CHECK(g);
        for(size_t r=0;r<16;++r)for(size_t c=0;c<8;++c)px[(y+r)*stride+x+c]=(g[r]&(0x80>>c))?fg:bg;
    }
}
struct Line {std::u16string text;size_t x,y;uint32_t fg,bg;};
int main() {
    const size_t W=320,H=200;std::vector<uint32_t> px(W*H,0x000000);
    Draw(px,W,3+8*2,5+16*1,u"GNU GRUB  version 2.12",0xaaaaaa,0x000000);
    Draw(px,W,3+8*2,5+16*3,u"*Debian GNU/Linux",0x000000,0xaaaaaa);
    Draw(px,W,3+8*2,5+16*4,u" Advanced options",0xaaaaaa,0x000000);
    Draw(px,W,3+8*2,5+16*6,u"Zażółć gęślą jaźń ↑↓",0xffffff,0x0000aa);
    PixelOcr ocr;std::vector<Line> lines;
    auto emit=[&](const char16_t* text,size_t x,size_t y,uint32_t fg,uint32_t bg) {lines.push_back({text,x,y,fg,bg});};
    const size_t count=ocr.Recognize(px.data(),W,H,W,0,0,emit);
    if(getenv("OCR_DEBUG"))for(auto& l:lines) {
        printf("[%zu,%zu fg%06x bg%06x] ",l.x,l.y,unsigned(l.fg),unsigned(l.bg));
        for(auto c:l.text)putchar(c<128?char(c):'?');putchar('\n');
    }
    CHECK(count>=60);
    auto find=[&](const char16_t* text)->const Line* {for(auto& l:lines)if(l.text==text)return &l;return nullptr;};
    const Line* title=find(u"GNU GRUB  version 2.12");CHECK(title && title->x==3+16 && title->y==5+16 && title->fg==0xaaaaaa);
    const Line* selected=find(u"*Debian GNU/Linux");CHECK(selected && selected->bg==0xaaaaaa && selected->fg==0x000000);
    CHECK(find(u"Advanced options") || find(u" Advanced options"));
    CHECK(find(u"Zażółć gęślą jaźń ↑↓"));
    // A sub-rectangle with a different origin keeps the cached grid.
    lines.clear();
    const size_t left=19,top=53;std::vector<uint32_t> part(200*40);
    for(size_t y=0;y<40;++y)for(size_t x=0;x<200;++x)part[y*200+x]=px[(top+y)*W+left+x];
    ocr.Recognize(part.data(),200,40,200,left,top,emit);
    CHECK(find(u"*Debian GNU/Linux"));
    // Variant glyph: two flipped pixels still match; a picture yields nothing.
    // (the grid is found from exact cells; a damaged cell uses the nearest).
    std::vector<uint32_t> v(96*32,0);Draw(v,96,0,0,u"BOOT OK",0xffffff,0);v[3*96+1]^=0xffffff;v[9*96+41]^=0xffffff;
    PixelOcr fresh;lines.clear();fresh.Recognize(v.data(),96,32,96,0,0,emit);
    CHECK(!lines.empty() && lines[0].text==u"BOOT OK");
    std::vector<uint32_t> picture(160*80);for(size_t i=0;i<picture.size();++i)picture[i]=uint32_t(i*2654435761u)&0xffffff;
    PixelOcr none;lines.clear();CHECK(none.Recognize(picture.data(),160,80,160,0,0,emit)==0 && lines.empty());
    // Wrong frame-buffer geometry (adapter reprogrammed behind GOP): an
    // 800-pixel screen read with 1280-pixel rows gives no text, not
    // look-alike punctuation; the 800 geometry has the best quality.
    std::vector<uint32_t> fb(1280*800,0);
    for(size_t row=0;row<12;++row)Draw(fb,800,34,75+row*16,u"Charon test entry  UEFI firmware settings  Probe loader",0xa8a8a8,0);
    PixelOcr wrong;lines.clear();
    CHECK(wrong.Recognize(fb.data(),1280,600,1280,0,0,emit)==0 && lines.empty() && wrong.LastInked()>=20);
    PixelOcr geometry;const size_t q800=geometry.Quality(fb.data(),800,600,800,80),q1024=geometry.Quality(fb.data(),1024,600,1024,80),q1280=geometry.Quality(fb.data(),1280,600,1280,80);
    CHECK(q800>=900 && q1024<600 && q1280<600);
    lines.clear();CHECK(geometry.Recognize(fb.data(),800,600,800,0,0,emit)>=500 && find(u"Charon test entry  UEFI firmware settings  Probe loader"));
    puts("PASS pixel OCR: unaligned grid, colours, highlight, Polish letters/arrows, cached grid on sub-rectangles, variant glyph, picture, wrong geometry rejected and found");
}
