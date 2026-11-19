// Native HAK client check without NVDA or Python: the mixer's sound streams.
// GPL-2.0-or-later. Usage: AudioStreamsTest | AudioStreamsTest hold SECONDS
// "hold" only plays silence for that long: another program for ducking tests.
// Plays silence through its own BSoundPlayer, finds that stream by its team,
// lowers its gain, reads it back and restores it.
#include "../include/hak/client.h"
#include <Application.h>
#include <OS.h>
#include <SoundPlayer.h>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(condition) do { if(!(condition)) { printf("FAIL %s (line %d)\n",#condition,__LINE__); return 1; } } while(0)

namespace {

struct Record {
    int32_t id=0,team=0,channels=0,muted=0;
    std::vector<float> gains;
    float minimum=0,maximum=0;
    std::string name;
};

void Silence(void*,void* buffer,size_t size,const media_raw_audio_format&) {memset(buffer,0,size);}

// "-12.34": digits with a decimal point, independent of the locale. False for anything else.
bool Number(const std::string& text,float& value) {
    size_t position=0;
    bool negative=false;
    if(position<text.size() && text[position]=='-') {negative=true;++position;}
    long whole=0,fraction=0;int wholeDigits=0,fractionDigits=0;
    for(;position<text.size() && text[position]>='0' && text[position]<='9';++position,++wholeDigits)whole=whole*10+(text[position]-'0');
    if(position>=text.size() || text[position]!='.')return false;
    for(++position;position<text.size() && text[position]>='0' && text[position]<='9';++position,++fractionDigits)fraction=fraction*10+(text[position]-'0');
    if(position!=text.size() || !wholeDigits || fractionDigits!=2)return false;
    value=float(whole)+float(fraction)/100.0f;
    if(negative)value=-value;
    return true;
}

bool Read(std::vector<Record>& records) {
    static char buffer[65536];
    int64_t used=0;int32_t count=0;
    if(hak_audio_streams_read(buffer,sizeof(buffer),&used,&count)!=HAK_OK)return false;
    records.clear();
    const std::string text(buffer,size_t(used));
    size_t start=0;
    while(start<text.size()) {
        const size_t end=text.find('\n',start);
        if(end==std::string::npos)return false;
        const std::string line=text.substr(start,end-start);
        start=end+1;
        std::vector<std::string> fields;
        size_t field=0;
        for(;;) {
            const size_t tab=line.find('\t',field);
            fields.push_back(line.substr(field,tab==std::string::npos ? std::string::npos : tab-field));
            if(tab==std::string::npos)break;
            field=tab+1;
        }
        if(fields.size()!=8)return false;
        Record record;
        record.id=atoi(fields[0].c_str());record.team=atoi(fields[1].c_str());
        record.channels=atoi(fields[2].c_str());record.muted=atoi(fields[3].c_str());
        size_t position=0;
        while(position<=fields[4].size()) {
            const size_t comma=fields[4].find(',',position);
            float gain=0;
            if(!Number(fields[4].substr(position,comma==std::string::npos ? std::string::npos : comma-position),gain))return false;
            record.gains.push_back(gain);
            if(comma==std::string::npos)break;
            position=comma+1;
        }
        if(!Number(fields[5],record.minimum) || !Number(fields[6],record.maximum))return false;
        record.name=fields[7];
        if(int32_t(record.gains.size())!=record.channels)return false;
        records.push_back(record);
    }
    return int32_t(records.size())==count;
}

}

int main(int argc,char** argv) {
    CHECK(hak_client_capabilities()&HAK_CAP_AUDIO_STREAMS);
    // A locale with a decimal comma (when the system has one): the records must not change.
    const char* locale=setlocale(LC_ALL,"pl_PL.UTF-8");
    if(!locale)locale=setlocale(LC_ALL,"de_DE.UTF-8");
    char probe[16];
    snprintf(probe,sizeof(probe),"%.1f",1.5);
    printf("LOCALE %s, printf writes 1.5 as %s\n",locale ? locale : "(C)",probe);
    BApplication app("application/x-vnd.HAK.AudioStreamsTest");
    if(argc==3 && !strcmp(argv[1],"hold")) {
        media_raw_audio_format held=media_raw_audio_format::wildcard;
        held.frame_rate=44100;held.channel_count=2;held.format=media_raw_audio_format::B_AUDIO_SHORT;
        held.byte_order=B_MEDIA_HOST_ENDIAN;held.buffer_size=4096;
        BSoundPlayer holder(&held,"HAK audio streams test",Silence);
        CHECK(holder.InitCheck()==B_OK && holder.Start()==B_OK);
        holder.SetHasData(true);
        puts("HOLDING");fflush(stdout);
        snooze(bigtime_t(atol(argv[2]))*1000000);
        holder.Stop();
        return 0;
    }
    int64_t used=77;int32_t count=77;
    CHECK(hak_audio_streams_read(nullptr,0,&used,&count)<0 && used==77 && count==77);
    media_raw_audio_format format=media_raw_audio_format::wildcard;
    format.frame_rate=44100;format.channel_count=2;format.format=media_raw_audio_format::B_AUDIO_SHORT;
    format.byte_order=B_MEDIA_HOST_ENDIAN;format.buffer_size=4096;
    BSoundPlayer player(&format,"HAK audio streams test",Silence);
    CHECK(player.InitCheck()==B_OK);
    CHECK(player.Start()==B_OK);
    player.SetHasData(true);
    const team_id self=getpid();
    std::vector<Record> records;
    Record own;
    for(int attempt=0;attempt<40 && own.id==0;++attempt) {
        snooze(250000);
        CHECK(Read(records));
        for(const Record& record : records)if(record.team==self)own=record;
    }
    CHECK(own.id>0);
    printf("STREAMS %d; own: id %d team %d channels %d gain %.2f name '%s' range %.1f..%.1f\n",int(records.size()),
        own.id,own.team,own.channels,double(own.gains[0]),own.name.c_str(),double(own.minimum),double(own.maximum));
    CHECK(own.name=="HAK audio streams test" && own.channels==2 && own.minimum<-20 && own.maximum>=0);
    // Refusals: wrong team, wrong channel count, unknown stream. The gain stays.
    float lowered[2]={own.gains[0]-12.0f,own.gains[1]-12.0f};
    CHECK(hak_audio_stream_set_gain(own.id,self+100000,lowered,2)<0);
    CHECK(hak_audio_stream_set_gain(own.id,self,lowered,1)<0);
    CHECK(hak_audio_stream_set_gain(999999,self,lowered,2)<0);
    CHECK(hak_audio_stream_set_gain(own.id,self,nullptr,2)<0);
    CHECK(Read(records));
    for(const Record& record : records)if(record.id==own.id)CHECK(fabs(record.gains[0]-own.gains[0])<0.01);
    // Lower, read back, restore.
    CHECK(hak_audio_stream_set_gain(own.id,self,lowered,2)==HAK_OK);
    bool seen=false;
    for(int attempt=0;attempt<20 && !seen;++attempt) {
        snooze(100000);
        CHECK(Read(records));
        for(const Record& record : records)
            if(record.id==own.id && fabs(record.gains[0]-lowered[0])<0.01 && fabs(record.gains[1]-lowered[1])<0.01)seen=true;
    }
    CHECK(seen);
    const float original[2]={own.gains[0],own.gains[1]};
    CHECK(hak_audio_stream_set_gain(own.id,self,original,2)==HAK_OK);
    seen=false;
    for(int attempt=0;attempt<20 && !seen;++attempt) {
        snooze(100000);
        CHECK(Read(records));
        for(const Record& record : records)if(record.id==own.id && fabs(record.gains[0]-original[0])<0.01)seen=true;
    }
    CHECK(seen);
    player.Stop();
    printf("PASS audio streams: listed, own stream found by team, refusals leave the gain, gain lowered by 12 dB, read back, restored\n");
    return 0;
}
