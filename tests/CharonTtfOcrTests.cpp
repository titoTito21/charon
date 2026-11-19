// Host test for src/efi/CharonTtfOcr.h (r361): reads screenshots (binary
// PPM) with TrueType fonts loaded from files, prints every line and checks
// expected lines when given.
//   CharonTtfOcrTests FONTDIR SCREEN.ppm [EXPECTED.txt]
// Fonts: every *.ttf name listed in CHARON_TTF_FONTS order inside FONTDIR
// (default segoe_slboot.ttf segoen_slboot.ttf segmono_boot.ttf wgl4_boot.ttf).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../src/efi/CharonTtfOcr.h"
#include "../src/efi/CharonPng.h"
#include "../src/efi/CharonBmFont.h"

static std::vector<uint8_t> ReadFile(const std::string& path) {
    std::vector<uint8_t> data;FILE* f=std::fopen(path.c_str(),"rb");
    if(!f)return data;
    std::fseek(f,0,SEEK_END);data.resize(size_t(std::ftell(f)));std::fseek(f,0,SEEK_SET);
    if(std::fread(data.data(),1,data.size(),f)!=data.size())data.clear();
    std::fclose(f);return data;
}
static bool ReadPpm(const std::string& path,std::vector<uint32_t>& pixels,size_t& w,size_t& h) {
    std::vector<uint8_t> d=ReadFile(path);
    if(d.size()<16 || d[0]!='P' || d[1]!='6')return false;
    size_t pos=2,values[3],n=0;
    while(n<3) {
        while(pos<d.size() && (d[pos]==' ' || d[pos]=='\n' || d[pos]=='\r' || d[pos]=='\t'))++pos;
        if(d[pos]=='#') {while(pos<d.size() && d[pos]!='\n')++pos;continue;}
        size_t v=0;while(pos<d.size() && d[pos]>='0' && d[pos]<='9')v=v*10+(d[pos++]-'0');
        values[n++]=v;
    }
    ++pos;w=values[0];h=values[1];
    if(d.size()<pos+w*h*3)return false;
    pixels.resize(w*h);
    for(size_t i=0;i<w*h;++i)pixels[i]=uint32_t(d[pos+i*3])<<16|uint32_t(d[pos+i*3+1])<<8|d[pos+i*3+2];
    return true;
}
static std::string Utf8(const char16_t* t) {
    std::string s;
    for(;*t;++t) {
        const unsigned c=*t;
        if(c<0x80)s+=char(c);
        else if(c<0x800) {s+=char(0xc0|(c>>6));s+=char(0x80|(c&0x3f));}
        else {s+=char(0xe0|(c>>12));s+=char(0x80|((c>>6)&0x3f));s+=char(0x80|(c&0x3f));}
    }
    return s;
}
static void* HostAlloc(void*,size_t bytes) {return std::calloc(1,bytes);}
static void HostFree(void*,void* block) {std::free(block);}

int main(int argc,char** argv) {
    if(argc<3) {std::fprintf(stderr,"usage: CharonTtfOcrTests FONTDIR SCREEN.ppm [EXPECTED.txt]\n");return 2;}
    static hakefi::TtfOcr ocr;
    ocr.SetAllocator(HostAlloc,HostFree,nullptr);
    std::vector<std::vector<uint8_t>> fonts;
    std::string list=std::getenv("CHARON_TTF_FONTS")?std::getenv("CHARON_TTF_FONTS"):"segoe_slboot.ttf segoen_slboot.ttf segmono_boot.ttf wgl4_boot.ttf";
    fonts.reserve(16);
    for(size_t p=0;p<list.size();) {
        size_t e=list.find(' ',p);if(e==std::string::npos)e=list.size();
        const std::string name=list.substr(p,e-p);p=e+1;
        if(name.empty())continue;
        fonts.push_back(ReadFile(std::string(argv[1])+"/"+name));
        std::printf("font %s: %s\n",name.c_str(),ocr.AddFont(fonts.back().data(),fonts.back().size())?"loaded":"REJECTED");
    }
#ifdef CHARON_TTF_DEBUG
    if(std::getenv("TTF_FONT"))hakefi::debugFont=std::atoi(std::getenv("TTF_FONT"));
    if(std::getenv("TTF_Q"))hakefi::debugQuarter=std::atoi(std::getenv("TTF_Q"));
    if(std::getenv("TTF_X"))hakefi::debugColumn=std::atoi(std::getenv("TTF_X"));
#endif
#ifdef CHARON_TTF_DEBUG
    if(std::getenv("TTF_GLYPH")) {ocr.DumpGlyph(size_t(hakefi::debugFont),uint16_t(hakefi::debugQuarter),char16_t(std::getenv("TTF_GLYPH")[0]));return 0;}
#endif
    // TTF_CELL=FILE:OFFSET adds an 8x16 cell font found in a program image.
    static std::vector<uint8_t> cellImage;
    if(std::getenv("TTF_CELL")) {
        std::string spec=std::getenv("TTF_CELL");const size_t colon=spec.rfind(':');
        cellImage=ReadFile(spec.substr(0,colon));const size_t offset=std::strtoul(spec.c_str()+colon+1,nullptr,10);
        static uint16_t cell16[4096];
        if(offset+4096<=cellImage.size())for(size_t k=0;k<4096;++k)cell16[k]=uint16_t(cellImage[offset+k]<<8);
        std::printf("cell font: %s\n",offset+4096<=cellImage.size() && ocr.AddCellFont(cell16,8,16,8)?"loaded":"REJECTED");
    }
    // TTF_CELL14=FILE:OFFSET_OF_A adds a 14-row, low-bit-left cell font
    // (OpenCore), normalized as the resident reader does.
    static uint16_t cell14[4096];
    if(std::getenv("TTF_CELL14")) {
        std::string spec=std::getenv("TTF_CELL14");const size_t colon=spec.rfind(':');
        std::vector<uint8_t> img=ReadFile(spec.substr(0,colon));const size_t a=std::strtoul(spec.c_str()+colon+1,nullptr,10);
        for(size_t c=32;c<127;++c)for(size_t r=0;r<14;++r) {
            uint8_t v=img[a+(c-65)*14+r],m=0;for(int bit=0;bit<8;++bit)if(v&(1<<bit))m|=uint8_t(0x80>>bit);cell14[c*16+r]=uint16_t(m<<8);}
        std::printf("cell14 font: %s\n",ocr.AddCellFont(cell14,8,16,8)?"loaded":"REJECTED");
    }
    // TTF_GRID=FONT.png adds a 16x16 glyph grid font image (Clover).
    static uint16_t grid[256*24];
    if(std::getenv("TTF_GRID")) {
        std::vector<uint8_t> png=ReadFile(std::getenv("TTF_GRID"));
        std::vector<uint8_t> work(1<<20),index(1<<20);hakefi::PngImage image;
        size_t cw=0,ch=0;
        if(hakefi::DecodePalettePng(png.data(),png.size(),work.data(),work.size(),index.data(),index.size(),image) && hakefi::GridFont(index.data(),image,grid,cw,ch))
            std::printf("grid font %zux%zu: %s\n",cw,ch,ocr.AddCellFont(grid,cw,ch,cw-2)?"loaded":"REJECTED");
        else std::printf("grid font: REJECTED\n");
    }
    // TTF_BMFONT=DIR/Font_1x adds OpenCanopy's BMFont (DIR/Font_1x.png + .bin).
    if(std::getenv("TTF_BMFONT")) {
        static std::vector<uint8_t> png=ReadFile(std::string(std::getenv("TTF_BMFONT"))+".png"),bin=ReadFile(std::string(std::getenv("TTF_BMFONT"))+".bin");
        hakefi::Pf2Font* font=nullptr;
        const bool built=hakefi::BuildBmFont(png.data(),png.size(),bin.data(),bin.size(),{HostAlloc,HostFree,nullptr},font);
        std::printf("bmfont: %s\n",built && ocr.AddPf2Font(*font)?"loaded":"REJECTED");
    }
    if(std::getenv("TTF_PF2"))for(unsigned k=0;k<hakefi::pf2FontCount;++k)std::printf("pf2 font %s: %s\n",hakefi::pf2Fonts[k].name,ocr.AddPf2Font(hakefi::pf2Fonts[k])?"loaded":"REJECTED");
    std::vector<uint32_t> pixels;size_t w=0,h=0;
    if(!ReadPpm(argv[2],pixels,w,h)) {std::fprintf(stderr,"cannot read %s\n",argv[2]);return 2;}
    std::vector<std::string> lines;
    const auto start=std::chrono::steady_clock::now();
    const size_t n=ocr.Recognize(pixels.data(),w,h,w,0,0,[&](const char16_t* text,size_t x,size_t y,uint32_t,uint32_t,size_t height) {
        lines.push_back(Utf8(text));std::printf("%4zu,%4zu h%-3zu %s\n",x,y,height,lines.back().c_str());
    },[]() {});
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::printf("%zu lines, %.1f ms\n",n,ms);
    if(argc>3) {
        std::vector<uint8_t> e=ReadFile(argv[3]);std::string all(e.begin(),e.end());size_t bad=0,p=0;
        while(p<all.size()) {
            size_t q=all.find('\n',p);if(q==std::string::npos)q=all.size();
            std::string want=all.substr(p,q-p);p=q+1;
            if(!want.empty() && want.back()=='\r')want.pop_back();
            if(want.empty())continue;
            bool found=false;for(auto& l:lines)if(l==want)found=true;
            if(!found) {++bad;std::printf("MISSING: %s\n",want.c_str());}
        }
        std::printf(bad?"FAIL\n":"PASS\n");return bad?1:0;
    }
    return 0;
}
