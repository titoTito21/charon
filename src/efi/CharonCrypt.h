#pragma once
// r400: SHA-256 crypt ("$5$salt$hash", Ulrich Drepper's scheme, 5000 rounds),
// so a password typed in Charon goes into an installer's answer file only as a
// hash that Debian and Ubuntu accept. Also a "newc" cpio writer for the small
// extra initrd that carries answer files to a Linux installer.
// GPL-2.0-or-later.
#include <stddef.h>
#include <stdint.h>
#include "CharonSha256.h"
namespace hakefi {
namespace crypt {
inline size_t Length(const char* s) {size_t n=0;while(s[n])++n;return n;}
// out: at least 64 characters. salt: up to 16 characters of [./0-9A-Za-z].
inline bool Sha256Crypt(const char* key,const char* salt,char* out,size_t capacity) {
    const size_t keyLength=Length(key);size_t saltLength=Length(salt);
    if(saltLength>16)saltLength=16;
    if(capacity<3+saltLength+1+43+1 || keyLength>256)return false;
    auto* k=reinterpret_cast<const uint8_t*>(key);auto* s=reinterpret_cast<const uint8_t*>(salt);
    uint8_t a[32],b[32],dp[32],ds[32];
    {Sha256 h;h.Update(k,keyLength);h.Update(s,saltLength);h.Update(k,keyLength);h.Final(b);}
    {Sha256 h;h.Update(k,keyLength);h.Update(s,saltLength);
        size_t count=keyLength;for(;count>32;count-=32)h.Update(b,32);
        h.Update(b,count);
        for(count=keyLength;count>0;count>>=1) {if(count&1)h.Update(b,32);else h.Update(k,keyLength);}
        h.Final(a);}
    {Sha256 h;for(size_t i=0;i<keyLength;++i)h.Update(k,keyLength);h.Final(dp);}
    {Sha256 h;for(size_t i=0;i<16u+a[0];++i)h.Update(s,saltLength);h.Final(ds);}
    static uint8_t p[256];uint8_t sq[16];
    for(size_t i=0;i<keyLength;++i)p[i]=dp[i%32];
    for(size_t i=0;i<saltLength;++i)sq[i]=ds[i%32];
    for(unsigned round=0;round<5000;++round) {
        Sha256 h;
        if(round&1)h.Update(p,keyLength);else h.Update(a,32);
        if(round%3)h.Update(sq,saltLength);
        if(round%7)h.Update(p,keyLength);
        if(round&1)h.Update(a,32);else h.Update(p,keyLength);
        h.Final(a);
    }
    size_t at=0;out[at++]='$';out[at++]='5';out[at++]='$';
    for(size_t i=0;i<saltLength;++i)out[at++]=salt[i];
    out[at++]='$';
    static const char alphabet[]="./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    auto put=[&](uint8_t b2,uint8_t b1,uint8_t b0,int n) {
        uint32_t w=(uint32_t(b2)<<16)|(uint32_t(b1)<<8)|b0;
        while(n-->0) {out[at++]=alphabet[w&63];w>>=6;}
    };
    put(a[0],a[10],a[20],4);put(a[21],a[1],a[11],4);put(a[12],a[22],a[2],4);put(a[3],a[13],a[23],4);put(a[24],a[4],a[14],4);
    put(a[15],a[25],a[5],4);put(a[6],a[16],a[26],4);put(a[27],a[7],a[17],4);put(a[18],a[28],a[8],4);put(a[9],a[19],a[29],4);
    put(0,a[31],a[30],3);
    out[at]=0;
    for(auto& x:p)x=0;
    return true;
}
// ---- cpio "newc" ------------------------------------------------------------
struct Cpio {
    uint8_t* data;size_t capacity,size;uint32_t inode;bool overflow;
    void Raw(const void* bytes,size_t n) {
        if(size+n>capacity) {overflow=true;return;}
        const auto* b=static_cast<const uint8_t*>(bytes);for(size_t i=0;i<n;++i)data[size++]=b[i];
    }
    void Hex(uint32_t value) {static const char digits[]="0123456789abcdef";char t[8];for(int i=7;i>=0;--i) {t[i]=digits[value&15];value>>=4;}Raw(t,8);}
    void Pad() {static const uint8_t zero[4]={};Raw(zero,(4-size%4)%4);}
    void Entry(const char* name,uint32_t mode,const void* bytes,size_t length) {
        const size_t nameLength=Length(name)+1;
        Raw("070701",6);Hex(++inode);Hex(mode);Hex(0);Hex(0);Hex(1);Hex(0);Hex(uint32_t(length));
        Hex(0);Hex(0);Hex(0);Hex(0);Hex(uint32_t(nameLength));Hex(0);
        Raw(name,nameLength);Pad();
        if(length)Raw(bytes,length);
        Pad();
    }
    // A file whose bytes the caller reads straight into the archive: the
    // header is written and the place for `length` bytes returned (null: no room).
    uint8_t* Begin(const char* name,uint32_t mode,size_t length) {
        const size_t nameLength=Length(name)+1;
        if(size+110+nameLength+8+length+4>capacity) {overflow=true;return nullptr;}
        Raw("070701",6);Hex(++inode);Hex(mode);Hex(0);Hex(0);Hex(1);Hex(0);Hex(uint32_t(length));
        Hex(0);Hex(0);Hex(0);Hex(0);Hex(uint32_t(nameLength));Hex(0);
        Raw(name,nameLength);Pad();
        uint8_t* at=data+size;size+=length;
        while(size%4)data[size++]=0;
        return at;
    }
    void Folder(const char* name) {Entry(name,0040755,nullptr,0);}
    void File(const char* name,const void* bytes,size_t length) {Entry(name,0100644,bytes,length);}
    void Program(const char* name,const void* bytes,size_t length) {Entry(name,0100755,bytes,length);}
    void End() {Entry("TRAILER!!!",0,nullptr,0);static const uint8_t zero[512]={};Raw(zero,(512-size%512)%512);}
};
}  // namespace crypt
}  // namespace hakefi
