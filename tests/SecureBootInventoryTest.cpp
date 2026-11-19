#include "../src/HAKSecureBootInventory.h"
#include <cassert>
int main() {
    std::vector<uint8_t> data(12,255);memcpy(data.data(),"HAKKEY1",8);data[8]=1;
    const uint8_t kind[]={0x26,0x16,0xc4,0xc1,0x4c,0x50,0x92,0x40,0xac,0xa9,0x41,0xf9,0x36,0x93,0x43,0x28};
    for(unsigned i=0;i<4;++i) {
        size_t at=data.size();data.resize(at+16,0);
        if(i==0) {data[at]=14;data[at+7]=128;}
        if(i==1) {data[at]=7;data[at+7]=128;}
        if(i==2) {
            data[at+8]=0x27;data[at+12]=76;
            size_t base=data.size();data.resize(base+76,0);memcpy(data.data()+base,kind,16);
            data[base+16]=76;data[base+24]=48;
            for(unsigned j=0;j<32;++j)data[base+44+j]=uint8_t(j);
        }
    }
    const char end[]="HAKEND1";data.insert(data.end(),end,end+8);
    haksecure::Inventory parsed;assert(haksecure::Decode(data,parsed));
    assert(parsed.databases[0].status==haksecure::missing && parsed.databases[1].status!=0);
    assert(parsed.databases[2].entries.size()==1 && parsed.databases[3].entries.empty());
    assert(parsed.databases[2].entries[0].kind=="SHA-256");
    assert(parsed.databases[2].entries[0].digest=="000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    for(size_t n=0;n<data.size();++n) {auto cut=data;cut.resize(n);haksecure::Inventory rejected;assert(!haksecure::Decode(cut,rejected));}
    auto corrupt=data;corrupt[8]=2;assert(!haksecure::Decode(corrupt,parsed));
    corrupt=data;corrupt[12+12]=1;assert(!haksecure::Decode(corrupt,parsed));
    corrupt=data;corrupt[60+24]=16;assert(!haksecure::Decode(corrupt,parsed));
    puts("PASS Secure Boot inventory: exact digest, absent/error/empty distinction, every truncation and invalid ESL");
}
