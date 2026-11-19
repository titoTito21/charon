#include "../src/HAKBootEFIBackend.h"
#include <cassert>
#include <cstdio>
int main(int argc,char** argv) {
    assert(argc==2);
    auto backend=hakbootefi::NativeBackend();std::vector<hakbootefi::Volume> volumes;
    assert(backend.volumes(volumes));bool found=false;
    for(auto volume:volumes) {
        if(volume.device!=argv[1])continue;
        found=true;std::vector<hakbootefi::Target> targets;
        auto stale=volume;stale.offset+=512;assert(!backend.targets(stale,targets));
        assert(backend.targets(volume,targets) && targets.size()==1 && targets[0].label=="Haiku test EFI");
        auto bad=targets[0];bad.identifier+="stale";assert(!backend.choose(volume,bad));
        assert(!backend.choose(stale,targets[0]));
    }
    assert(found);
    puts("PASS boot-manager native ESP enumeration, firmware inventory, stale partition and stale option refusal");
}
