#include "../src/HAKEFISettingsClient.h"
#include <cassert>
int main(int argc,char** argv) {
    assert(argc==2);std::string base=argv[1];
    assert(mkdir(base.c_str(),0700)==0);
    assert(mkdir((base+"/EFI").c_str(),0700)==0);
    assert(mkdir((base+"/EFI/HAK").c_str(),0700)==0);
    std::vector<uint8_t> data(30,0);memcpy(data.data(),"HAKSET1",7);
    data[10]=1;data[11]=255;data[12]=5;data[14]=3;data[26]=1;data[28]=2;
    FILE* f=fopen((base+"/EFI/HAK/settings.bin").c_str(),"wb");assert(f);
    assert(fwrite(data.data(),1,data.size(),f)==data.size());assert(fclose(f)==0);
    std::vector<uint8_t> read;
    assert(hakeficlient::ReadSettings(base.c_str(),read) && read==data);
    auto stale=data;stale[12]=4;
    using hakeficlient::SettingsState;
    assert(hakeficlient::SettingsOutcome(base.c_str()).state==SettingsState::None);
    assert(!hakeficlient::QueueSettings(base.c_str(),stale,hakefi::kTimeout,6));
    assert(!hakeficlient::QueueSettings(base.c_str(),data,hakefi::kTimeout,5));
    assert(hakeficlient::QueueSettings(base.c_str(),data,hakefi::kTimeout,6));
    assert(!hakeficlient::QueueSettings(base.c_str(),data,hakefi::kDefault,2));
    f=fopen((base+"/EFI/HAK/settings-next.bin").c_str(),"rb");assert(f);
    uint8_t bytes[59];assert(fread(bytes,1,sizeof(bytes),f)==58);fclose(f);
    assert(!memcmp(bytes,"HAKREQ2",8));
    assert(hakefi::U16(bytes+24)==hakefi::kTimeout && hakefi::U16(bytes+26)==6);
    assert(!memcmp(bytes+28,data.data(),data.size()));
    assert(hakeficlient::SettingsOutcome(base.c_str()).state==SettingsState::Pending);
    assert(unlink((base+"/EFI/HAK/settings-next.bin").c_str())==0);
    assert(hakeficlient::SettingsOutcome(base.c_str()).state==SettingsState::Unverified);
    uint8_t result[32]={};memcpy(result,"HAKRES2",7);memcpy(result+8,bytes+8,16);
    auto saveResult=[&]() {FILE* out=fopen((base+"/EFI/HAK/settings-result.bin").c_str(),"wb");assert(out);assert(fwrite(result,1,32,out)==32);assert(fclose(out)==0);};
    saveResult();assert(hakeficlient::SettingsOutcome(base.c_str()).state==SettingsState::Accepted);
    result[24]=26;result[31]=128;saveResult();auto rejected=hakeficlient::SettingsOutcome(base.c_str());
    assert(rejected.state==SettingsState::Rejected && rejected.status==0x800000000000001aULL);
    result[8]^=1;saveResult();assert(hakeficlient::SettingsOutcome(base.c_str()).state==SettingsState::Unverified);
    puts("PASS native settings snapshot, stale rejection, no-op rejection, exact queued request and pending preservation");
}
