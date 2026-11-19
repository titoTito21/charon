// Host test for src/efi/CharonBmOcr.h (r384): OpenCanopy labels read from a
// screenshot with the BMFont of OcBinaryData (not shipped; path given).
//   CharonBmOcrTests FONT_BASE SCREEN.ppm X Y W H [EXPECTED...]
// FONT_BASE: .../Resources/Font/Font_1x (".png" and ".bin" are appended).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "../src/efi/CharonBmFont.h"
#include "../src/efi/CharonBmOcr.h"
static std::vector<uint8_t> ReadFile(const std::string& path) {
    std::vector<uint8_t> data;FILE* f=std::fopen(path.c_str(),"rb");if(!f)return data;
    std::fseek(f,0,SEEK_END);data.resize(size_t(std::ftell(f)));std::fseek(f,0,SEEK_SET);
    if(std::fread(data.data(),1,data.size(),f)!=data.size())data.clear();
    std::fclose(f);return data;
}
static bool ReadPpm(const std::string& path,std::vector<uint32_t>& pixels,size_t& w,size_t& h) {
    std::vector<uint8_t> d=ReadFile(path);if(d.size()<16 || d[0]!='P' || d[1]!='6')return false;
    size_t pos=2,values[3],n=0;
    while(n<3) {while(pos<d.size() && (d[pos]==' ' || d[pos]=='\n' || d[pos]=='\r'))++pos;size_t v=0;while(pos<d.size() && d[pos]>='0' && d[pos]<='9')v=v*10+(d[pos++]-'0');values[n++]=v;}
    ++pos;w=values[0];h=values[1];if(d.size()<pos+w*h*3)return false;
    pixels.resize(w*h);for(size_t i=0;i<w*h;++i)pixels[i]=uint32_t(d[pos+i*3])<<16|uint32_t(d[pos+i*3+1])<<8|d[pos+i*3+2];
    return true;
}
static void* HostAlloc(void*,size_t bytes) {return std::calloc(1,bytes);}
static void HostFree(void*,void* block) {std::free(block);}
int main(int argc,char** argv) {
    if(argc<7) {std::fprintf(stderr,"usage: CharonBmOcrTests FONT_BASE SCREEN.ppm X Y W H [EXPECTED...]\n");return 2;}
    std::vector<uint8_t> png=ReadFile(std::string(argv[1])+".png"),bin=ReadFile(std::string(argv[1])+".bin");
    hakefi::Pf2Font* font=nullptr;
    if(!hakefi::BuildBmFont(png.data(),png.size(),bin.data(),bin.size(),{HostAlloc,HostFree,nullptr},font)) {std::puts("FAIL font");return 1;}
    std::vector<uint32_t> px;size_t w=0,h=0;if(!ReadPpm(argv[2],px,w,h)) {std::puts("FAIL ppm");return 1;}
    const size_t x=std::strtoul(argv[3],nullptr,10),y=std::strtoul(argv[4],nullptr,10),cw=std::strtoul(argv[5],nullptr,10),ch=std::strtoul(argv[6],nullptr,10);
    hakefi::BmOcr ocr;ocr.SetFont(font);
    std::vector<std::string> lines;
    ocr.Recognize(px.data()+y*w+x,cw,ch,w,x,y,[&](const char16_t* t,size_t lx,size_t ly,uint32_t,uint32_t) {
        std::string s;for(;*t;++t)s+=char(*t);lines.push_back(s);std::printf("%4zu,%4zu %s\n",lx,ly,s.c_str());});
    int bad=0;
    for(int i=7;i<argc;++i) {bool found=false;for(auto& l:lines)if(l.find(argv[i])!=std::string::npos)found=true;if(!found) {++bad;std::printf("MISSING %s\n",argv[i]);}}
    std::puts(bad?"FAIL":"PASS r384 OpenCanopy BMFont labels");return bad?1:0;
}
