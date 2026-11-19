#include "../src/efi/SecureBootControl.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main() {
    uint8_t state[24]={};memcpy(state,"HAKSB01",8);state[16]=3;state[20]=1;
    assert(hakefi::SecureControlAvailable(state,24));
    state[21]=1;assert(hakefi::SecureControl(state,24) && !hakefi::SecureControlAvailable(state,24));
    state[21]=0;state[22]=255;assert(!hakefi::SecureControlAvailable(state,24));
    state[22]=0;state[16]=7;assert(!hakefi::SecureControl(state,24));
    state[16]=0;state[8]=14;state[15]=128;state[20]=255;
    assert(hakefi::SecureControl(state,24) && !hakefi::SecureControlAvailable(state,24));
    for(size_t n=0;n<24;++n)assert(!hakefi::SecureControl(state,n));
    assert(!hakefi::SecureControl(nullptr,24));
    puts("PASS EDK2 preference snapshot bounds, absence, mode/attribute validation");
}
