// Independent HAK UEFI load-option validation. GPL-2.0-or-later.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hakefi {
inline uint16_t U16(const uint8_t* p) {return uint16_t(p[0]) | uint16_t(p[1])<<8;}
inline uint32_t U32(const uint8_t* p) {return U16(p) | uint32_t(U16(p+2))<<16;}
struct BootOption {
    size_t descriptionBytes=0,pathOffset=0,pathBytes=0,optionsOffset=0;
};
inline bool Parse(const uint8_t* data,size_t size,BootOption& result) {
    if(!data || size<12 || size>65536)return false;
    // Only active boot-category entries, not firmware applications.
    if(!(U32(data)&1) || (U32(data)&0x1f00))return false;
    size_t end=6;
    while(end+2<=size && U16(data+end)) {
        const uint16_t unit=U16(data+end);end+=2;
        if(unit>=0xd800 && unit<=0xdbff) {
            if(end+2>size || U16(data+end)<0xdc00 || U16(data+end)>0xdfff)return false;
            end+=2;
        } else if(unit>=0xdc00 && unit<=0xdfff)return false;
    }
    if(end+2>size)return false;
    const size_t start=end+2,length=U16(data+4);
    if(length<4 || length>size-start)return false;
    size_t at=start;
    bool finished=false;
    while(at<start+length) {
        if(start+length-at<4)return false;
        const size_t node=U16(data+at+2);
        if(node<4 || node>start+length-at)return false;
        if(data[at]==0x7f) {
            if(node!=4)return false;
            if(data[at+1]==0xff) {
                if(at+4!=start+length)return false;
                finished=true;
            } else if(data[at+1]!=1 || at+4==start+length)return false;
        }
        at+=node;
    }
    if(!finished)return false;
    result={end-6,start,length,start+length};return true;
}
inline bool Same(const uint8_t* a,const uint8_t* b,size_t size) {
    for(size_t i=0;i<size;++i)if(a[i]!=b[i])return false;
    return true;
}
}
