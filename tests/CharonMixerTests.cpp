#include "../src/tts/runtime/CharonMixer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
using charon::Mixer;
int main() {
    const int16_t melody[]={80,-80,160,-160,240,-240,320,-320};
    const int16_t speech[]={800,-800,1600,-1600};
    const int16_t cue[]={400,-400,800,-800,1200,-1200};
    int16_t out[32]={};Mixer mix;
    CHECK(mix.SetMelody(melody,4));CHECK(mix.Render(out,1));
    CHECK(out[0]==10 && out[1]==-10 && mix.MelodyPosition()==1);
    CHECK(mix.StartSpeech(speech,2) && mix.StartCue(cue,3));CHECK(mix.Render(out,1));
    CHECK(out[0]==520 && out[1]==-520 && mix.MelodyPosition()==2);
    mix.CancelSpeech();CHECK(mix.MelodyPosition()==2 && mix.CuePosition()==1);
    CHECK(mix.Render(out,1) && out[0]==230 && out[1]==-230 && mix.MelodyPosition()==3);
    CHECK(mix.StartSpeech(speech,2));mix.SetUiSoundsEnabled(false);
    CHECK(!mix.CueActive() && mix.SpeechActive() && mix.MelodyPosition()==3);
    CHECK(!mix.StartCue(cue,3));CHECK(mix.Render(out,1));
    CHECK(out[0]==440 && out[1]==-440 && mix.MelodyPosition()==0);
    CHECK(mix.Render(out,1) && out[0]==810 && !mix.SpeechActive() && mix.MelodyPosition()==1);
    mix.SetUiSoundsEnabled(true);CHECK(mix.StartCue(cue,3));
    mix.CancelCue();CHECK(mix.MelodyPosition()==1 && mix.MelodyActive());
    // No advance on invalid buffers/lengths, no implicit reset of prior audio.
    CHECK(!mix.StartSpeech(nullptr,1) && !mix.SetMelody(nullptr,1));
    CHECK(!mix.StartCue(cue,SIZE_MAX) && !mix.Render(nullptr,1) && !mix.Render(out,SIZE_MAX));
    CHECK(mix.MelodyPosition()==1 && mix.Render(nullptr,0));
    CHECK(!mix.SetGains({32768,1,0}) && !mix.SetGains({UINT32_MAX,0,0}));
    mix.StopAll();CHECK(!mix.MelodyActive() && !mix.CueActive() && !mix.SpeechActive());
    memset(out,0x55,sizeof(out));CHECK(mix.Render(out,16));for(auto sample:out)CHECK(sample==0);
    // Exact extrema and headroom: independent channel signs are retained.
    const int16_t extremes[]={32767,-32768};
    CHECK(mix.SetGains({16384,8192,8192}));
    CHECK(mix.StartSpeech(extremes,1) && mix.StartCue(extremes,1) && mix.SetMelody(extremes,1));
    CHECK(mix.Render(out,1) && out[0]==32767 && out[1]==-32768);
    CHECK(mix.Render(out,1) && out[0]==8191 && out[1]==-8192);
    // Chunk boundaries cannot change samples or the looping melody phase.
    Mixer full,chunked;CHECK(full.SetMelody(melody,4) && chunked.SetMelody(melody,4));
    CHECK(full.StartSpeech(speech,2) && chunked.StartSpeech(speech,2));
    CHECK(full.StartCue(cue,3) && chunked.StartCue(cue,3));
    int16_t first[26],second[26];CHECK(full.Render(first,13));
    CHECK(chunked.Render(second,1) && chunked.Render(second+2,3) && chunked.Render(second+8,9));
    CHECK(memcmp(first,second,sizeof(first))==0 && full.MelodyPosition()==chunked.MelodyPosition());
    // Replacing speech repeatedly advances the music on the same shared clock.
    Mixer music;CHECK(music.SetMelody(melody,4));
    for(size_t i=0;i<101;++i) {CHECK(music.StartSpeech(speech,2));CHECK(music.Render(out,1));music.CancelSpeech();CHECK(music.MelodyPosition()==(i+1)%4);}
    puts("PASS actual shared PCM mixer: melody phase across speech/cancel, cue-only mute, headroom/extrema, bounded inputs and chunk invariance; no hardware proof");
}
