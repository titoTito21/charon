// HAK EFI settings wire format, shared by firmware and clients.
// GPL-2.0-or-later.
#pragma once
#include "BootOption.h"
namespace hakefi {
constexpr size_t kSettingsHeader=24,kSettingsMaximum=kSettingsHeader+512;
enum SettingCommand {kTimeout=1,kDefault=2,kEarlier=3,kLater=4};
inline void Put16(uint8_t* p,uint16_t n) {p[0]=uint8_t(n);p[1]=uint8_t(n>>8);}
inline bool Settings(const uint8_t* p,size_t size) {
    const uint8_t magic[8]={'H','A','K','S','E','T','1',0};
    if(!p || size<kSettingsHeader || size>kSettingsMaximum || !Same(p,magic,8))return false;
    if(p[10]>1 || (p[11]!=0 && p[11]!=1 && p[11]!=255))return false;
    const size_t count=U16(p+14);
    if(!count || count>256 || size!=kSettingsHeader+2*count)return false;
    for(size_t i=0;i<count;++i)
        for(size_t j=0;j<i;++j)if(U16(p+kSettingsHeader+2*i)==U16(p+kSettingsHeader+2*j))return false;
    return true;
}
// Compute the requested value without modifying live firmware. The HAK entry
// must remain first; absence or another current entry means setup is incomplete.
inline bool ChangeSettings(uint8_t* p,size_t size,uint16_t command,uint16_t value) {
    if(!Settings(p,size))return false;
    if(command==kTimeout) {
        if(!p[10] || value>300)return false;
        Put16(p+12,value);return true;
    }
    if(command<kDefault || command>kLater || U16(p+24)!=U16(p+8))return false;
    size_t count=U16(p+14),index=count;
    for(size_t i=1;i<count;++i)if(U16(p+24+2*i)==value)index=i;
    if(index==count)return false;
    if(command==kDefault) {
        for(size_t i=index;i>1;--i)Put16(p+24+2*i,U16(p+24+2*(i-1)));
        Put16(p+26,value);return true;
    }
    if((command==kEarlier && index==1) || (command==kLater && index+1==count))return false;
    const size_t other=command==kEarlier ? index-1 : index+1;
    Put16(p+24+2*index,U16(p+24+2*other));Put16(p+24+2*other,value);return true;
}
}
