// EDK2-specific preference snapshot, distinct from effective SecureBoot state.
#pragma once
#include "SecureBootUpdate.h"
namespace hakefi {
constexpr size_t kSecureControlSize=24;
inline bool SecureControl(const uint8_t* p,size_t size) {
    if(!p || size!=kSecureControlSize || !Same(p,(const uint8_t*)"HAKSB01",8) || p[23])return false;
    for(unsigned i=21;i<=22;++i)if(p[i]>1 && p[i]!=255)return false;
    if(KeyU64(p+8)==0)return p[20]<=1 && U32(p+16)==3;
    return p[20]==255 && U32(p+16)==0;
}
inline bool SecureControlAvailable(const uint8_t* p,size_t size) {
    return SecureControl(p,size) && !KeyU64(p+8) && p[21]==0 && p[22]<=1;
}
}
