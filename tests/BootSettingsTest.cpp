#include "../src/efi/BootSettings.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main() {
    uint8_t data[30]={'H','A','K','S','E','T','1',0,1,0,1,0,5,0,3,0};
    hakefi::Put16(data+24,1);hakefi::Put16(data+26,2);hakefi::Put16(data+28,3);
    assert(hakefi::Settings(data,sizeof(data)));
    assert(hakefi::ChangeSettings(data,sizeof(data),hakefi::kDefault,3));
    assert(hakefi::U16(data+24)==1 && hakefi::U16(data+26)==3 && hakefi::U16(data+28)==2);
    assert(!hakefi::ChangeSettings(data,sizeof(data),hakefi::kEarlier,3));
    assert(!hakefi::ChangeSettings(data,sizeof(data),hakefi::kDefault,1));
    assert(hakefi::ChangeSettings(data,sizeof(data),hakefi::kLater,3));
    assert(hakefi::U16(data+26)==2 && hakefi::U16(data+28)==3);
    assert(hakefi::ChangeSettings(data,sizeof(data),hakefi::kTimeout,17));
    assert(hakefi::U16(data+12)==17);
    assert(!hakefi::ChangeSettings(data,sizeof(data),hakefi::kTimeout,301));
    data[10]=0;assert(!hakefi::ChangeSettings(data,sizeof(data),hakefi::kTimeout,5));
    data[10]=1;hakefi::Put16(data+8,2);
    assert(!hakefi::ChangeSettings(data,sizeof(data),hakefi::kDefault,3));
    for(size_t i=0;i<sizeof(data);++i)assert(!hakefi::Settings(data,i));
    hakefi::Put16(data+28,2);assert(!hakefi::Settings(data,sizeof(data)));
    puts("PASS EFI settings bounds, snapshot validation, HAK-first ordering and explicit timeout capability");
}
