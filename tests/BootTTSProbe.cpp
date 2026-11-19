#include "../src/HAKBootTTS.h"
#include <cassert>
int main(int argc,char** argv) {
    assert(argc==2);
    HAKBootTTS voice;
    voice.Open(std::string(argv[1])+"/engine/synthDrivers/native","pl");
    std::vector<int16> pcm;
    bigtime_t start=system_time();
    assert(voice.Render("Menedżer rozruchu Haiku Accessibility Kit.",pcm));
    assert(!pcm.empty() && system_time()-start<1500000);
    printf("PASS cached Adam greeting %zu samples\n",pcm.size());fflush(stdout);
    const bigtime_t deadline=system_time()+90000000;
    while(!voice.Render("Nowy komunikat testowy menedżera HAK.",pcm)) {
        assert(system_time()<deadline);snooze(100000);
    }
    assert(!pcm.empty());
    printf("PASS live Adam worker %zu samples\n",pcm.size());fflush(stdout);
    voice.language="de";
    assert(!voice.Render("HAK Bootmanager.",pcm));
    puts("PASS unsupported language delegates to native fallback");
}
