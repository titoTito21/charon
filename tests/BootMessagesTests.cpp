#include "../src/efi/BootMessages.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d\n",__LINE__);exit(1);}}while(false)
using namespace hakefi;
int main() {
    const uint64_t error=uint64_t(1)<<63;
    const unsigned codes[]={14,26,15,1,3,10,27,33,12,13,7,99};
    const BootProblem expected[]={BootProblem::loadMissing,BootProblem::security,BootProblem::denied,
        BootProblem::loadFormat,BootProblem::unsupported,BootProblem::corrupted,BootProblem::corrupted,
        BootProblem::corrupted,BootProblem::noMedia,BootProblem::mediaChanged,BootProblem::loadFailed,BootProblem::loadFailed};
    for(size_t i=0;i<sizeof(codes)/sizeof(*codes);++i) {
        CHECK(AttemptProblem(BootAttemptStage::load,error|codes[i])==expected[i]);
        CHECK(AttemptProblem(BootAttemptStage::validation,error|codes[i])==BootProblem::targetChanged);
        CHECK(AttemptProblem(BootAttemptStage::protocol,error|codes[i])==BootProblem::imageProtocol);
        CHECK(AttemptProblem(BootAttemptStage::returned,error|codes[i])==BootProblem::loaderReturned);
    }
    CHECK(AttemptProblem(BootAttemptStage::load,14)==BootProblem::loadFailed);
    CHECK(AttemptProblem(BootAttemptStage::returned,0)==BootProblem::loaderReturned);
    for(unsigned i=0;i<=unsigned(BootProblem::loaderReturned);++i) {
        const auto problem=BootProblem(i);const std::u16string en=BootMessage(problem,false),pl=BootMessage(problem,true);
        CHECK(!en.empty() && !pl.empty() && en!=pl && en.size()<450 && pl.size()<450);
    }
    CHECK(std::u16string(BootMessage(BootProblem::emptyInventory,false)).find(u"does not mean disks are absent")!=std::u16string::npos);
    char16_t text[80]=u"Failure";AppendBootStatus(text,80,error|99);
    CHECK(std::u16string(text)==u"Failure UEFI: 0x8000000000000063");
    char16_t tiny[8]=u"Failure";AppendBootStatus(tiny,8,error);CHECK(std::u16string(tiny)==u"Failure");
    puts("PASS staged UEFI failures, localized actionable messages, no false disk/signature inference, exact status code");
}
