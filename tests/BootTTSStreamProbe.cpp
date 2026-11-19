#include "../src/HAKBootTTS.h"
#include <cassert>
int main(int argc,char** argv) {
    assert(argc==2);
    HAKBootTTS voice;
    voice.Open(std::string(argv[1])+"/engine/synthDrivers/native","pl");
    std::vector<int16> pcm,heard;
    assert(voice.Render("Menedżer rozruchu Haiku Accessibility Kit.",pcm));
    bigtime_t first=0,start=system_time();int blocks=0;
    const char* text="Zawijanie list: zaznaczone. Pole wyboru. Druga pozycja.";
    assert(voice.Render(text,pcm,[](){return false;},[&](const auto& block) {
        if(!first)first=system_time();
        ++blocks;
        heard.insert(heard.end(),block.begin(),block.end());
    }));
    assert(blocks>1 && !pcm.empty() && heard==pcm);
    printf("PASS streamed PCM, no duplicate audio: first %lld ms total %lld ms blocks %d\n",
        (long long)((first-start)/1000),(long long)((system_time()-start)/1000),blocks);fflush(stdout);
    heard.clear();blocks=0;
    assert(voice.Render(text,pcm,[](){return false;},[&](const auto& block) {++blocks;heard.insert(heard.end(),block.begin(),block.end());}));
    assert(blocks==1 && heard==pcm);
    bool stop=false;
    assert(!voice.Render("Pierwsza część. Nieaktualna druga część. Nieaktualna trzecia część.",pcm,[&](){return stop;},[&](const auto&) {stop=true;}));
    heard.clear();start=system_time();
    assert(voice.Render("Aktualna pozycja menu.",pcm,[](){return false;},[&](const auto& block){heard.insert(heard.end(),block.begin(),block.end());}));
    assert(!pcm.empty() && heard==pcm);
    printf("PASS cancelled synthesis and replacement framing %lld ms\n",(long long)((system_time()-start)/1000));
}
