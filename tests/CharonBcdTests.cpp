// Host test of src/efi/CharonBcd.h (r389): reads a BCD hive and checks the
// Windows Boot Manager summary. The hive is not shipped (Microsoft data):
//   clang++ -std=c++17 -D_CRT_SECURE_NO_WARNINGS -I src/efi tests/CharonBcdTests.cpp -o bcdtest.exe
//   bcdtest.exe work/bcd-menu/BCD   (DVD template + make-bcd-menu.py)
#include <cstdio>
#include <cstring>
#include <vector>
#include "CharonBcd.h"
static bool Same(const char16_t* a,const char* b) {while(*a && *b && *a==char16_t(*b)) {++a;++b;}return !*a && !*b;}
int main(int argc,char** argv) {
    if(argc<2) {std::puts("usage: bcdtest BCD");return 2;}
    FILE* f=std::fopen(argv[1],"rb");if(!f) {std::puts("FAIL open");return 1;}
    std::vector<uint8_t> data;int c;while((c=std::fgetc(f))!=EOF)data.push_back(uint8_t(c));std::fclose(f);
    hakefi::BcdSummary s;
    if(!hakefi::ReadBcd(data.data(),data.size(),s) || s.count!=2 || s.defaultIndex!=0 || s.timeout!=30
        || !Same(s.name[0],"Windows Setup") || !Same(s.name[1],"Windows Memory Diagnostic")) {std::puts("FAIL summary");return 1;}
    char16_t out[300];hakefi::DescribeBcd(s,false,out,300);
    if(!Same(out,"Windows Setup (default), Windows Memory Diagnostic; 30 s")) {std::puts("FAIL describe");return 1;}
    // Damaged hives never read out of bounds: every truncation and a byte
    // flipped at every 7th position gives an answer or none, no crash.
    for(size_t n=0;n<data.size();n+=97) {hakefi::BcdSummary t;hakefi::ReadBcd(data.data(),n,t);}
    for(size_t i=0;i<data.size();i+=7) {auto copy=data;copy[i]^=0xff;hakefi::BcdSummary t;hakefi::ReadBcd(copy.data(),copy.size(),t);}
    std::puts("PASS BCD summary, truncated and damaged hives");
    return 0;
}
