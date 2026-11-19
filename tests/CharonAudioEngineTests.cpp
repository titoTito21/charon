#include "../src/tts/runtime/CharonAudioEngine.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(false)
using charon::AudioEngine;
struct Sink {
    std::vector<int16_t> data;size_t limit=SIZE_MAX;bool idle=true;
    static size_t Write(void* context,const int16_t* data,size_t frames) {
        auto& self=*static_cast<Sink*>(context);const auto take=(std::min)(self.limit,frames);
        self.data.insert(self.data.end(),data,data+take*2);return take;
    }
    static bool Drained(void* context) {return static_cast<Sink*>(context)->idle;}
    AudioEngine::Sink Interface() {return {this,Write,Drained};}
};
std::vector<int16_t> Convert(const std::vector<int16_t>& input,size_t chunk,size_t sinkLimit) {
    AudioEngine engine;CHECK(engine.SetGains({32768,0,0}));const auto generation=engine.BeginSpeech();
    Sink sink;sink.limit=sinkLimit;size_t at=0;
    while(at<input.size()) {const auto count=(std::min)(chunk,input.size()-at);
        const auto written=engine.PushSpeech(generation,input.data()+at,count);CHECK(written);at+=written;
        while(engine.Pump(sink.Interface(),97)) {}}
    CHECK(engine.EndSpeech(generation));while(engine.Pump(sink.Interface(),79)) {}
    CHECK(engine.Drained(sink.Interface()));return sink.data;
}
int main() {
    std::vector<int16_t> input;for(int i=0;i<5000;++i)input.push_back(int16_t((i*571)%65536-32768));
    const auto baseline=Convert(input,2048,SIZE_MAX);
    CHECK(baseline.size()/2==(input.size()*48000+22049)/22050);
    for(size_t i=0;i<baseline.size()/2;++i) {
        const uint64_t position=uint64_t(i)*22050;const size_t at=size_t(position/48000),next=(std::min)(at+1,input.size()-1);
        const int64_t phase=int64_t(position%48000);
        const auto expected=int16_t((int64_t(input[at])*(48000-phase)+int64_t(input[next])*phase)/48000);
        CHECK(baseline[i*2]==expected && baseline[i*2+1]==expected);
    }
    CHECK(Convert(input,1,1)==baseline);CHECK(Convert(input,17,7)==baseline);
    CHECK(Convert(std::vector<int16_t>{-32768},1,1)==std::vector<int16_t>(6,-32768));
    // EOF arrives only after the open producer has stalled, including one
    // sample and partial/zero sink acceptance. No melody/cue can mask drain.
    for(size_t length:{size_t(1),size_t(2),size_t(3),size_t(147),size_t(2048)}) {
        AudioEngine delayed;Sink receiver;receiver.limit=1;
        CHECK(delayed.SetGains({32768,0,0}));const auto token=delayed.BeginSpeech();
        CHECK(delayed.PushSpeech(token,input.data(),length)==length);
        while(delayed.Pump(receiver.Interface(),128)) {}
        CHECK(!delayed.SoftwareDrained());
        const auto acceptedBefore=receiver.data.size();receiver.limit=0;
        CHECK(delayed.Pump(receiver.Interface(),128)==0 && receiver.data.size()==acceptedBefore);
        CHECK(delayed.EndSpeech(token));CHECK(!delayed.EndSpeech(token));
        CHECK(delayed.Pump(receiver.Interface(),128)==0 && receiver.data.size()==acceptedBefore);
        receiver.limit=1;while(delayed.Pump(receiver.Interface(),128)) {}
        CHECK(delayed.Drained(receiver.Interface()));
        CHECK(receiver.data==Convert(std::vector<int16_t>(input.begin(),input.begin()+length),length,SIZE_MAX));
        delayed.CancelSpeech();CHECK(!delayed.EndSpeech(token));
        CHECK(!delayed.EndSpeech(token+1));
    }
    AudioEngine engine;Sink sink;const int16_t melody[]={100,200,300,400,500,600};
    CHECK(engine.SetMelody(melody,3));CHECK(engine.SetGains({16384,8192,8192}));
    auto generation=engine.BeginSpeech();CHECK(engine.PushSpeech(generation,input.data(),3000)==2048);
    CHECK(engine.PushSpeech(generation,input.data(),1)==0);sink.limit=0;
    CHECK(engine.Pump(sink.Interface(),100)==0 && engine.MelodyPosition()==0 && engine.BufferedSpeech()==2048);
    sink.limit=1;CHECK(engine.Pump(sink.Interface(),100)==1 && engine.MelodyPosition()==1);
    generation=engine.BeginSpeech();CHECK(engine.MelodyPosition()==1 && engine.BufferedSpeech()==0);
    CHECK(engine.PushSpeech(generation-1,input.data(),1)==0 && !engine.EndSpeech(generation-1));
    CHECK(engine.Pump(sink.Interface(),100)==1 && engine.MelodyPosition()==2);
    engine.StopMelody();CHECK(!engine.SoftwareDrained());CHECK(engine.EndSpeech(generation));CHECK(engine.SoftwareDrained());sink.idle=false;CHECK(!engine.Drained(sink.Interface()));
    sink.idle=true;CHECK(engine.Drained(sink.Interface()));
    CHECK(!engine.Drained({}));
    const int16_t close[]={1000,2000,3000,4000},open[]={5000,6000};
    CHECK(engine.SetGains({0,32768,0}));CHECK(engine.QueueCue(close,2));CHECK(engine.QueueCue(open,1));
    sink.data.clear();sink.limit=SIZE_MAX;CHECK(engine.Pump(sink.Interface(),100)==3);
    CHECK(sink.data==std::vector<int16_t>({1000,2000,3000,4000,5000,6000}));CHECK(engine.SoftwareDrained());
    for(unsigned i=0;i<12;++i) {CHECK(engine.QueueCue(open,1));}CHECK(!engine.QueueCue(open,1));
    CHECK(engine.SetMelody(melody,3));generation=engine.BeginSpeech();CHECK(engine.PushSpeech(generation,input.data(),2)==2);
    engine.SetUiSoundsEnabled(false);CHECK(engine.QueuedCues()==0 && engine.BufferedSpeech()==2);
    CHECK(!engine.QueueCue(open,1));CHECK(engine.Pump(sink.Interface(),1)==1 && engine.MelodyPosition()==1);
    engine.StopAll();CHECK(engine.SoftwareDrained());CHECK(!engine.SetGains({32768,1,0}));
    CHECK(!engine.QueueCue(nullptr,1));
    auto invalid=[](void*,const int16_t*,size_t count)->size_t{return count+1;};
    engine.SetUiSoundsEnabled(true);CHECK(engine.QueueCue(open,1));
    CHECK(engine.Pump({nullptr,invalid,nullptr},5)==0 && engine.SinkFault() && engine.QueuedCues()==1);
    CHECK(!engine.Drained(sink.Interface()));
    puts("PASS streaming resampler reference/chunks/EOF, cue FIFO, cancellation generations, melody phase, gains and sink backpressure/drain");
}

