// Host test for src/efi/CharonPng.h (r365): decodes palette PNGs and compares
// every pixel index with a reference dump (one byte per pixel, row-major).
//   CharonPngTests IMAGE.png REFERENCE.bin [IMAGE.png REFERENCE.bin ...]
#include <cstdio>
#include <string>
#include <vector>
#include "../src/efi/CharonPng.h"
static std::vector<uint8_t> Read(const char* path) {
    std::vector<uint8_t> d;FILE* f=std::fopen(path,"rb");if(!f)return d;
    int c;while((c=std::fgetc(f))!=EOF)d.push_back(uint8_t(c));std::fclose(f);return d;
}
int main(int argc,char** argv) {
    int bad=0;
    for(int i=1;i+1<argc;i+=2) {
        std::vector<uint8_t> png=Read(argv[i]),ref=Read(argv[i+1]);
        std::vector<uint8_t> work(4*1024*1024),pixels(4*1024*1024);
        hakefi::PngImage image;
        const bool ok=hakefi::DecodePalettePng(png.data(),png.size(),work.data(),work.size(),pixels.data(),pixels.size(),image);
        bool same=ok && ref.size()==image.width*image.height;
        for(size_t k=0;same && k<ref.size();++k)same=ref[k]==pixels[k];
        std::printf("%s %s: %zux%zu depth %zu palette %zu\n",same?"ok  ":"FAIL",argv[i],image.width,image.height,image.depth,image.paletteSize);
        if(!same)++bad;
    }
    std::printf(bad?"FAIL\n":"PASS\n");return bad?1:0;
}
