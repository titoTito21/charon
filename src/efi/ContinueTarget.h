#pragma once
#include "BootOption.h"
namespace hakefi {
constexpr size_t continueCapacity=65555; // maximum record plus one truncation byte
inline bool ParseContinueTarget(const uint8_t* bytes,size_t length,uint16_t& id,size_t& payloadSize) {
    const uint8_t magic[8]={'H','A','K','C','O','N','T','1'};
    if(!bytes || length<18 || length>=continueCapacity || !Same(bytes,magic,8)
        || U32(bytes+10)!=length-18 || U32(bytes+length-4)!=0x444e4548)return false;
    BootOption parsed;
    if(!Parse(bytes+14,length-18,parsed))return false;
    id=U16(bytes+8);payloadSize=length-18;return true;
}
}
