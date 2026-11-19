#include "../src/HAKBootDevices.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace hakbootdev;
    uint8 sector[kSector]={};
    Target target;target.deviceName="Fixture USB";
    target.name="Windows Linux Haiku FreeDOS";
    target.system="FAT32 File System";
    IdentifyBootSystem(target,sector);
    assert(target.bootSystem.empty()); // Names and file systems are not OS evidence.
    assert(DeviceAndSystem(target,"en")=="Fixture USB, Unknown operating system");
    sector[510]=0x55;sector[511]=0xaa;
    memcpy(sector+3,"FRDOS5.1",8);memcpy(sector+497,"KERNEL  SYS",11);
    IdentifyBootSystem(target,sector);
    assert(target.bootSystem=="FreeDOS" && target.bootEvidence=="freedos-vbr");
    assert(DeviceAndSystem(target,"pl")=="Fixture USB, FreeDOS");
    sector[497]='X';IdentifyBootSystem(target,sector);assert(target.bootSystem.empty());
    sector[497]='K';sector[510]=0;IdentifyBootSystem(target,sector);assert(target.bootSystem.empty());
    target.current=true;IdentifyBootSystem(target,sector);
    assert(target.bootSystem=="Haiku" && target.bootEvidence=="running-system");
    assert(DeviceName("/nonexistent-hak-test-device")=="/nonexistent-hak-test-device");
    puts("PASS device-first labels, explicit unknown, concrete FreeDOS VBR, current Haiku, invalid signatures and name fallback");
}
