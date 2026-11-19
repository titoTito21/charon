#include "../src/efi/SecureBootUpdate.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
    uint8_t request[48+40+76]={};
    memcpy(request,"HAKUPD1",8);request[24]=2;request[25]=1;
    request[28]=14;request[35]=0x80;request[44]=116;
    uint8_t* auth=request+48;
    auth[16]=24;auth[21]=2;auth[22]=0xf1;auth[23]=0x0e;
    const uint8_t pkcs7[]={0x9d,0xd2,0xaf,0x4a,0xdf,0x68,0xee,0x49,0x8a,0xa9,0x34,0x7d,0x37,0x56,0x65,0xa7};
    memcpy(auth+24,pkcs7,16);auth[40+16]=76;auth[40+24]=48;
    size_t payload=0;
    assert(hakefi::KeyRequest(request,sizeof(request),payload) && payload==40);
    for(size_t size=0;size<sizeof(request);++size)assert(!hakefi::KeyRequest(request,size,payload));
    const unsigned offsets[]={0,24,25,26,27,28,35,36,40,44,64,68,70,72,104,112};
    for(unsigned offset:offsets) {
        const uint8_t before=request[offset];request[offset]=0xff;
        assert(!hakefi::KeyRequest(request,sizeof(request),payload));request[offset]=before;
    }
    request[44]=40;
    assert(hakefi::KeyRequest(request,88,payload)); // authenticated deletion, empty payload
    request[24]=0;assert(hakefi::KeyRequest(request,88,payload));
    puts("PASS key request bounds, mode/target/status/attribute checks, AUTH2 envelope, ESL dimensions and deletion");
}
