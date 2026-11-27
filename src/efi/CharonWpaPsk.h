#pragma once
// r409: the WPA2 pre-shared key of a network, computed from its passphrase and name as every WPA2 station
// does (IEEE 802.11i-2004, H.4: PSK = PBKDF2(passphrase, ssid, 4096, 256 bits) with HMAC-SHA1). Charon hands
// this key to an installed system instead of the passphrase: the passphrase the user typed never reaches a
// file. Pure code with no dependency (SHA-1 after FIPS 180-4), so the host test runs it against the
// standard's own test vectors (tests/charon-wpa-psk-host.py). GPL-2.0-or-later.
#include <stddef.h>
#include <stdint.h>
namespace hakefi {
namespace wpapsk {
struct Sha1 {uint32_t h[5];uint64_t bytes;uint8_t block[64];size_t fill;};
inline uint32_t Rol(uint32_t v,int n) {return (v<<n)|(v>>(32-n));}
inline void Compress(Sha1& s,const uint8_t* p) {
    uint32_t w[80];
    for(int i=0;i<16;++i)w[i]=uint32_t(p[i*4])<<24|uint32_t(p[i*4+1])<<16|uint32_t(p[i*4+2])<<8|uint32_t(p[i*4+3]);
    for(int i=16;i<80;++i)w[i]=Rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    uint32_t a=s.h[0],b=s.h[1],c=s.h[2],d=s.h[3],e=s.h[4];
    for(int i=0;i<80;++i) {
        uint32_t f,k;
        if(i<20) {f=(b&c)|(~b&d);k=0x5a827999;}
        else if(i<40) {f=b^c^d;k=0x6ed9eba1;}
        else if(i<60) {f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdc;}
        else {f=b^c^d;k=0xca62c1d6;}
        const uint32_t t=Rol(a,5)+f+e+k+w[i];e=d;d=c;c=Rol(b,30);b=a;a=t;
    }
    s.h[0]+=a;s.h[1]+=b;s.h[2]+=c;s.h[3]+=d;s.h[4]+=e;
}
inline void Begin(Sha1& s) {s.h[0]=0x67452301;s.h[1]=0xefcdab89;s.h[2]=0x98badcfe;s.h[3]=0x10325476;s.h[4]=0xc3d2e1f0;s.bytes=0;s.fill=0;}
inline void Add(Sha1& s,const uint8_t* data,size_t size) {
    s.bytes+=size;
    for(size_t i=0;i<size;++i) {s.block[s.fill++]=data[i];if(s.fill==64) {Compress(s,s.block);s.fill=0;}}
}
inline void End(Sha1& s,uint8_t out[20]) {
    const uint64_t bits=s.bytes*8;uint8_t pad=0x80;Add(s,&pad,1);pad=0;
    while(s.fill!=56)Add(s,&pad,1);
    uint8_t length[8];for(int i=0;i<8;++i)length[i]=uint8_t(bits>>(56-8*i));
    Add(s,length,8);
    for(int i=0;i<5;++i) {out[i*4]=uint8_t(s.h[i]>>24);out[i*4+1]=uint8_t(s.h[i]>>16);out[i*4+2]=uint8_t(s.h[i]>>8);out[i*4+3]=uint8_t(s.h[i]);}
}
struct Hmac {Sha1 inner,outer;};
inline void HmacKey(Hmac& h,const uint8_t* key,size_t size) {
    uint8_t k[64]={};
    if(size>64) {Sha1 s;Begin(s);Add(s,key,size);End(s,k);} else for(size_t i=0;i<size;++i)k[i]=key[i];
    uint8_t pad[64];
    for(int i=0;i<64;++i)pad[i]=uint8_t(k[i]^0x36);
    Begin(h.inner);Add(h.inner,pad,64);
    for(int i=0;i<64;++i)pad[i]=uint8_t(k[i]^0x5c);
    Begin(h.outer);Add(h.outer,pad,64);
    for(auto& b:k)b=0;for(auto& b:pad)b=0;
}
inline void HmacOnce(const Hmac& h,const uint8_t* data,size_t size,uint8_t out[20]) {
    Sha1 s=h.inner;Add(s,data,size);uint8_t digest[20];End(s,digest);
    s=h.outer;Add(s,digest,20);End(s,out);
}
// The 32-byte key. passphrase: 8 to 63 bytes; ssid: 1 to 32 bytes. False: nothing written.
inline bool Key(const char* passphrase,size_t passBytes,const uint8_t* ssid,size_t ssidBytes,uint8_t out[32]) {
    if(passBytes<8 || passBytes>63 || !ssidBytes || ssidBytes>32)return false;
    Hmac h;HmacKey(h,reinterpret_cast<const uint8_t*>(passphrase),passBytes);
    for(uint32_t block=1;block<=2;++block) {
        uint8_t salt[36];for(size_t i=0;i<ssidBytes;++i)salt[i]=ssid[i];
        salt[ssidBytes]=uint8_t(block>>24);salt[ssidBytes+1]=uint8_t(block>>16);salt[ssidBytes+2]=uint8_t(block>>8);salt[ssidBytes+3]=uint8_t(block);
        uint8_t u[20],t[20];HmacOnce(h,salt,ssidBytes+4,u);
        for(int i=0;i<20;++i)t[i]=u[i];
        for(int round=1;round<4096;++round) {HmacOnce(h,u,20,u);for(int i=0;i<20;++i)t[i]^=u[i];}
        const size_t take=block==1?20:12;
        for(size_t i=0;i<take;++i)out[(block-1)*20+i]=t[i];
        for(auto& b:u)b=0;for(auto& b:t)b=0;
    }
    h=Hmac{};
    return true;
}
// The key as 64 lower-case hexadecimal digits and a terminating zero (65 bytes).
inline bool KeyHex(const char* passphrase,size_t passBytes,const uint8_t* ssid,size_t ssidBytes,char out[65]) {
    uint8_t key[32];
    if(!Key(passphrase,passBytes,ssid,ssidBytes,key))return false;
    static const char hex[]="0123456789abcdef";
    for(int i=0;i<32;++i) {out[i*2]=hex[key[i]>>4];out[i*2+1]=hex[key[i]&15];}
    out[64]=0;for(auto& b:key)b=0;
    return true;
}
}  // namespace wpapsk
}  // namespace hakefi
