// Host test (r400): SHA-256 crypt against the reference vectors of the scheme
// and the cpio "newc" writer's framing.
//   clang++ -std=c++17 -fuse-ld=lld -I src/efi tests/CharonCryptTests.cpp
#include "CharonCrypt.h"
#include <stdio.h>
#include <string.h>
int main() {
    int failed=0;char out[80];
    struct Vector {const char* key;const char* salt;const char* want;};
    static const Vector vectors[]={
        {"Hello world!","saltstring","$5$saltstring$5B8vYYiY.CVt1RlTTf8KbXBH3hsxY/GNooZaBBGWEc5"},
    };
    for(const auto& v:vectors) {
        const bool ok=hakefi::crypt::Sha256Crypt(v.key,v.salt,out,sizeof(out)) && !strcmp(out,v.want);
        printf("%s crypt %s -> %s\n",ok?"PASS":"FAIL",v.key,out);if(!ok)++failed;
    }
    static unsigned char buffer[4096];
    hakefi::crypt::Cpio cpio{buffer,sizeof(buffer),0,0,false};
    cpio.File("preseed.cfg","abc\n",4);cpio.End();
    const bool framing=!cpio.overflow && cpio.size%512==0 && !memcmp(buffer,"070701",6) && !memcmp(buffer+110,"preseed.cfg",12)
        && !memcmp(buffer+124,"abc\n",4) && !memcmp(buffer+128,"070701",6);
    printf("%s cpio framing, %u bytes\n",framing?"PASS":"FAIL",unsigned(cpio.size));if(!framing)++failed;
    return failed;
}
