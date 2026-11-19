// Host integration: actual frontend + original TVox streaming DSP + real mixer.
#include "../src/tts/runtime/CharonLiveVoice.h"
#include "../src/tts/tvox_native/tvox_freestanding.h"
#include "../src/tts/tvox_native/tvox_pl.h"
#include "../src/tts/tvox_native/tvox_dsp_stream.h"
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
using charon::LiveVoice;using charon::AudioEngine;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static bool allocationFailure=false;static size_t live=0;
static LiveVoice* observeCancellation=nullptr;
static void* allocate(void*,size_t n){if(allocationFailure)return nullptr;void* p=std::malloc(n);if(p)++live;return p;}
static void release(void*,void* p){if(observeCancellation)CHECK(observeCancellation->BufferedSpeech()==0);--live;std::free(p);}
struct Deadline {
    size_t remaining=0,calls=0;
    static int Yield(void* p){auto& d=*(Deadline*)p;++d.calls;if(!d.remaining)return 1;--d.remaining;return 0;}
};
struct Device {
    std::vector<int16_t> pcm;size_t limit=128,calls=0,blocked=0;bool hardwareDrained=false,invalid=false;
    LiveVoice* reenter=nullptr;
    static size_t Write(void* c,const int16_t* p,size_t n){auto& d=*(Device*)c;++d.calls;
        if(d.reenter){CHECK(d.reenter->QueueText(LiveVoice::Language::Polish,"a",1)==LiveVoice::Busy);CHECK(d.reenter->CancelSpeech()==LiveVoice::Busy);}
        if(d.invalid)return n+1;if(d.blocked){--d.blocked;return 0;}
        size_t take=n<d.limit?n:d.limit;d.pcm.insert(d.pcm.end(),p,p+take*2);return take;
    }
    static bool Drained(void* c){return ((Device*)c)->hardwareDrained;}
    AudioEngine::Sink sink(){return {this,Write,Drained};}
};
static std::vector<int16_t> reference(CharonPool& pool,const char* text,bool english=false){
    CHECK(tvox_frontend_configure(&pool,nullptr,nullptr)==0);size_t n=0,a=0,b=0;
    auto prepare=english?tvox_en_frames_ex:tvox_pl_frames_ex;
    CHECK(prepare(text,std::strlen(text),1,1,nullptr,0,&n,&a,&b)==1);
    std::vector<double> frames(n*28);CHECK(prepare(text,std::strlen(text),1,1,frames.data(),n,&n,&a,&b)==0);
    void* stream=charon_pool_malloc(&pool,tvox_dsp_stream_size());CHECK(stream);
    CHECK((english?tvox_dsp_stream_begin_en(stream,frames.data(),n,a,b,1,1):tvox_dsp_stream_begin(stream,frames.data(),n,a,b,1,1))==0);
    std::vector<int16_t> mono;int status;do{int16_t block[511];size_t made=0;status=tvox_dsp_stream_render(stream,block,511,&made);CHECK(status>=0);mono.insert(mono.end(),block,block+made);}while(!status);
    charon_pool_free(&pool,stream);AudioEngine audio;auto generation=audio.BeginSpeech();Device device;
    size_t offset=0;bool ended=false;
    while(offset<mono.size()||!audio.SoftwareDrained()){
        offset+=audio.PushSpeech(generation,mono.data()+offset,mono.size()-offset);
        if(offset==mono.size()&&!ended){CHECK(audio.EndSpeech(generation));ended=true;}
        audio.Pump(device.sink(),512);
    }
    return device.pcm;
}
static void finish(LiveVoice& voice,Device& device,const LiveVoice::Budget& budget){
    size_t steps=0;while(!voice.SoftwareDrained()){
        LiveVoice::Progress progress;CHECK(voice.Pump(budget,device.sink(),&progress)==0);
        CHECK(progress.frontend<=budget.frontend&&progress.dspAllowance<=budget.dspSamples&&progress.generated<=progress.dspAllowance&&progress.sinkFrames<=budget.sinkFrames);
        CHECK(++steps<2000000);
    }
}
int main(){
    CharonPool pool;CHECK(charon_pool_init(&pool,nullptr,allocate,release,128*1024*1024));
    const char* text="Menedżer rozruchu Haiku.";auto expected=reference(pool,text);CHECK(live==0);
    const char* englishText="Haiku Accessibility Kit boot manager.";auto englishExpected=reference(pool,englishText,true);CHECK(live==0);
    bool nonzero=false;for(auto s:expected)if(s)nonzero=true;CHECK(nonzero);
    for(auto budget:{LiveVoice::Budget{1,17,31},LiveVoice::Budget{64,256,480},LiveVoice::Budget{4096,511,1000}}){
        LiveVoice voice;CHECK(voice.Initialize(&pool)==0);LiveVoice other;CHECK(other.Initialize(&pool)==LiveVoice::Busy);
        Device device;device.limit=17;device.blocked=32;device.reenter=&voice;
        std::vector<char> temporary(text,text+std::strlen(text));CHECK(voice.QueueText(LiveVoice::Language::Polish,temporary.data(),temporary.size())==0);
        for(auto& c:temporary)c='X'; // queued text must be owned, not borrowed
        CHECK(voice.QueueText(static_cast<LiveVoice::Language>(99),"hello",5)==LiveVoice::UnsupportedLanguage);
        CHECK(voice.QueueText(LiveVoice::Language::Polish,"\xff",1)==LiveVoice::Invalid);
        finish(voice,device,budget);CHECK(device.pcm==expected);
        CHECK(!voice.Drained(device.sink()));AudioEngine::Sink noProof{&device,Device::Write,nullptr};CHECK(!voice.Drained(noProof));
        device.hardwareDrained=true;CHECK(voice.Drained(device.sink()));CHECK(voice.Shutdown()==0&&live==0);
    }
    {
        LiveVoice englishVoice;CHECK(englishVoice.Initialize(&pool)==0);Device englishDevice;englishDevice.limit=17;
        CHECK(englishVoice.QueueText(LiveVoice::Language::English,englishText,std::strlen(englishText))==0);
        finish(englishVoice,englishDevice,{7,17,31});CHECK(englishDevice.pcm==englishExpected);
        englishDevice=Device{};CHECK(englishVoice.QueueText(LiveVoice::Language::Polish,text,std::strlen(text))==0);
        finish(englishVoice,englishDevice,{4096,256,480});CHECK(englishDevice.pcm==expected);
        CHECK(englishVoice.Shutdown()==0&&live==0);
    }
    LiveVoice voice;CHECK(voice.Initialize(&pool)==0);Device device;LiveVoice::Progress progress;
    // A signalled deadline returns before even one frontend checkpoint, but
    // still pumps the melody. Resuming with tiny independent deadlines must
    // produce exactly the original sentence, not restart/segment its prosody.
    CHECK(voice.QueueText(LiveVoice::Language::English,englishText,std::strlen(englishText))==0);
    Deadline deadline;CHECK(voice.Pump({4096,256,480,Deadline::Yield,&deadline},device.sink(),&progress)==0);
    CHECK(progress.frontend==0&&deadline.calls==1&&device.pcm.empty());
    size_t deadlineSteps=0;
    while(!voice.SoftwareDrained()){
        deadline.remaining=7;deadline.calls=0;
        CHECK(voice.Pump({4096,256,480,Deadline::Yield,&deadline},device.sink(),&progress)==0);
        CHECK(progress.frontend<=7&&deadline.calls<=8);CHECK(++deadlineSteps<2000000);
    }
    CHECK(device.pcm==englishExpected);device=Device{};
    device.blocked=100000;CHECK(voice.QueueText(LiveVoice::Language::Polish,"Stary komunikat.",16)==0);
    for(size_t i=0;!voice.BufferedSpeech();++i){CHECK(voice.Pump({4096,256,480},device.sink(),&progress)==0);CHECK(i<10000);}
    CHECK(device.pcm.empty());observeCancellation=&voice;
    CHECK(voice.QueueText(LiveVoice::Language::English,englishText,std::strlen(englishText))==0);observeCancellation=nullptr;
    CHECK(voice.BufferedSpeech()==0);device.blocked=0;finish(voice,device,{4096,256,480});CHECK(device.pcm==englishExpected);
    // Rapid replacement AFTER samples were actually accepted: none of the old
    // sentence may be generated after replacement, independent of sink limits.
    for(unsigned replacement=0;replacement<14;++replacement){
        CHECK(voice.QueueText(LiveVoice::Language::Polish,text,std::strlen(text))==0);
        device=Device{};
        for(size_t i=0;device.pcm.size()<1200;++i){CHECK(voice.Pump({4096,256,480},device.sink(),&progress)==0);CHECK(i<10000);}
        CHECK(voice.QueueText(LiveVoice::Language::English,englishText,std::strlen(englishText))==0);
        device=Device{};finish(voice,device,{4096,256,480});CHECK(device.pcm==englishExpected);
    }
    device=Device{};
    const int16_t melody[]={400,800,600,1000,800,1200,1000,1400};const int16_t cue[]={1000,-1000,1100,-1100,1200,-1200};
    CHECK(voice.SetMelody(melody,4));CHECK(voice.QueueCue(cue,3));
    CHECK(voice.QueueText(LiveVoice::Language::Polish,text,std::strlen(text))==0);
    CHECK(voice.Pump({1,1,1},device.sink(),&progress)==0);auto position=voice.MelodyPosition();
    CHECK(voice.QueueCue(cue,3));auto cues=voice.QueuedCues();
    CHECK(voice.QueueText(LiveVoice::Language::Polish,"System gotowy.",14)==0);
    CHECK(voice.MelodyPosition()==position&&voice.QueuedCues()==cues);
    CHECK(voice.CancelSpeech()==0);CHECK(!voice.SoftwareDrained());
    CHECK(voice.Pump({0,0,7},device.sink(),&progress)==0&&progress.sinkFrames==7);
    CHECK(voice.StopAll()==0&&voice.SoftwareDrained());CHECK(!voice.Drained(device.sink()));
    // Accepted hardware frames survive software StopAll until real device proof.
    device.hardwareDrained=true;CHECK(voice.Drained(device.sink()));
    allocationFailure=true;CHECK(voice.QueueText(LiveVoice::Language::Polish,text,std::strlen(text))==LiveVoice::NoMemory);allocationFailure=false;
    CHECK(voice.LastError()==LiveVoice::NoMemory);CHECK(voice.QueueText(LiveVoice::Language::Polish,text,std::strlen(text))==0);
    allocationFailure=true;CHECK(voice.Pump({4096,256,480},device.sink(),&progress)==LiveVoice::NoMemory);allocationFailure=false;
    CHECK(live==1);CHECK(voice.QueueText(LiveVoice::Language::Polish,text,std::strlen(text))==0);
    AudioEngine::Sink missing{&device,nullptr,Device::Drained};CHECK(voice.Pump({64,256,480},missing,&progress)==LiveVoice::SinkError);
    device.invalid=true;
    for(size_t i=0;voice.LastError()!=LiveVoice::SinkError;++i){voice.Pump({4096,256,480},device.sink(),&progress);CHECK(i<10000);}
    CHECK(voice.Shutdown()==0&&live==0);
    CHECK(voice.Initialize(&pool)==0);device=Device{};CHECK(voice.QueueText(LiveVoice::Language::Polish,text,std::strlen(text))==0);finish(voice,device,{4096,511,480});CHECK(device.pcm==expected);
    CHECK(voice.Shutdown()==0&&live==0&&pool.used==0);
    std::puts("PASS LiveVoice actual TVox PL/EN PCM parity, language replacement, budgets, text copy, partial/zero sink, latest speech + cue/music, hardware drain, OOM/sink faults/recovery");
}
