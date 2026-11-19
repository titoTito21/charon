#include "../src/efi/BootOption.h"
#include <cassert>
#include <cstdio>
#include <vector>
int main() {
    // Active option, UTF-16 description, device path with final end node,
    // followed by arbitrary load options. Do not treat those as a path.
    std::vector<uint8_t> data={1,0,0,0,8,0,'O',0,'S',0,0,0,1,1,4,0,0x7f,0xff,4,0,9,8};
    hakefi::BootOption result;
    assert(hakefi::Parse(data.data(),data.size(),result));
    assert(result.pathOffset==12 && result.optionsOffset==20 && result.descriptionBytes==4);
    for(size_t n=0;n<20;++n)assert(!hakefi::Parse(data.data(),n,result));
    auto bad=data;bad[0]=0;assert(!hakefi::Parse(bad.data(),bad.size(),result));
    bad=data;bad[1]=1;assert(!hakefi::Parse(bad.data(),bad.size(),result));
    bad=data;bad[14]=0;assert(!hakefi::Parse(bad.data(),bad.size(),result));
    bad=data;bad[18]=5;assert(!hakefi::Parse(bad.data(),bad.size(),result));
    bad=data;bad[17]=1;assert(!hakefi::Parse(bad.data(),bad.size(),result));
    bad=data;bad[4]=0xff;assert(!hakefi::Parse(bad.data(),bad.size(),result));
    bad=data;bad[6]=0;bad[7]=0xdc;assert(!hakefi::Parse(bad.data(),bad.size(),result));
    bad=data;bad[6]=0;bad[7]=0xd8;assert(!hakefi::Parse(bad.data(),bad.size(),result));
    bad[8]=0;bad[9]=0xdc;assert(hakefi::Parse(bad.data(),bad.size(),result));
    assert(!hakefi::Parse(nullptr,22,result));
    puts("PASS UEFI active boot category, description UTF-16, bounded paths, end nodes, truncations and optional data");
}
