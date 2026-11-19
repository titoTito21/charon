// HAK authenticated-variable replacement transport (AUTHENTICATION_2).
#pragma once
#include "BootOption.h"
namespace hakefi {
constexpr size_t kKeyLimit=4*1024*1024,kKeyRequestLimit=48+2*kKeyLimit;
inline uint64_t KeyU64(const uint8_t* p) {return uint64_t(U32(p))|(uint64_t(U32(p+4))<<32);}
inline bool SignatureLists(const uint8_t* p,size_t length,unsigned* count=nullptr) {
    unsigned entries=0;
    for(size_t at=0;at<length;) {
        if(length-at<28)return false;
        const size_t total=U32(p+at+16),header=U32(p+at+20),item=U32(p+at+24);
        if(total<28 || total>length-at || header>total-28 || item<=16
            || total==28+header || (total-28-header)%item)return false;
        entries+=unsigned((total-28-header)/item);at+=total;
    }
    if(count)*count=entries;
    return true;
}
inline bool AuthenticatedPayload(const uint8_t* auth,size_t length,size_t& payloadOffset) {
    const uint8_t pkcs7[]={0x9d,0xd2,0xaf,0x4a,0xdf,0x68,0xee,0x49,0x8a,0xa9,0x34,0x7d,0x37,0x56,0x65,0xa7};
    if(length<40)return false;
    const size_t certificate=U32(auth+16);
    if(certificate<24 || certificate>length-16 || U16(auth+20)!=0x200
        || U16(auth+22)!=0xef1 || !Same(auth+24,pkcs7,16))return false;
    payloadOffset=16+certificate;
    return SignatureLists(auth+payloadOffset,length-payloadOffset);
}
inline bool KeyRequest(const uint8_t* p,size_t length,size_t& payloadOffset) {
    if(length<48 || length>kKeyRequestLimit || !Same(p,(const uint8_t*)"HAKUPD1",8)
        || p[24]>3 || p[25]>1 || p[26]>1 || p[27]!=0)return false;
    const uint64_t status=KeyU64(p+28);
    const size_t oldSize=U32(p+40),authSize=U32(p+44);
    if(oldSize>kKeyLimit || authSize>kKeyLimit || length!=48+oldSize+authSize)return false;
    if(status==0) {if(U32(p+36)!=0x27)return false;}
    else if(status!=0x800000000000000eULL || oldSize || U32(p+36))return false;
    if(!SignatureLists(p+48,oldSize)
        || !AuthenticatedPayload(p+48+oldSize,authSize,payloadOffset))return false;
    unsigned count=0;
    SignatureLists(p+48+oldSize+payloadOffset,authSize-payloadOffset,&count);
    return p[24]!=0 || count<=1;
}
}
