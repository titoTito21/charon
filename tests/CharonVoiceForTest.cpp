// Host logic test: VoiceFor must map colour/font/style/position to distinct,
// audible voice dimensions (non-monotone). No synthesis; checks the mapping.
#include "CharonFormatting.h"
#include <cstdio>
#include <set>
using namespace hakefi::formatting;

static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);++fails;} }while(0)

int main(){
    const uint8_t F=flagVoice|flagIcons|flagColours|flagBright|flagFont|flagInverse|flagEvents|flagFamilies;
    Base base; base.fg=7; base.bg=0; base.face=0; base.height=16;   // typical console base

    // 1) kolor -> wysokosc: 16 colours give many distinct pitches, body (7,15) ~ 0
    std::set<int> pitches;
    for(int c=0;c<16;++c){Style s;s.fg=(uint8_t)c;s.bg=0;s.height=16; Voice v=VoiceFor(s,base,F); pitches.insert(v.pitch);}
    printf("distinct pitches over 16 colours: %zu\n",pitches.size());
    CHECK(pitches.size()>=10,"colour->pitch should give >=10 distinct pitches");
    {Style s;s.fg=7;s.height=16; Voice v=VoiceFor(s,base,F); CHECK(v.pitch==0,"body colour 7 ~ neutral pitch");}
    {Base wb;wb.fg=15;wb.height=16; Style s;s.fg=15;s.height=16; Voice v=VoiceFor(s,wb,F); CHECK(v.pitch==0,"body colour (base=white) neutral pitch");}
    {Style r;r.fg=4;r.height=16; Voice vr=VoiceFor(r,base,F);
     Style b;b.fg=1;b.height=16; Voice vb=VoiceFor(b,base,F);
     CHECK(vr.pitch>3,"red high pitch"); CHECK(vb.pitch<0,"blue low pitch");
     CHECK(vr.range>vb.range,"warm red more modulation than cool blue");}

    // 2) czcionka height -> predkosc (rate): large slower, small faster
    {Style big;big.fg=7;big.height=32; Voice vb=VoiceFor(big,base,F);
     Style sm;sm.fg=7;sm.height=8;   Voice vs=VoiceFor(sm,base,F);
     printf("rate large=%d small=%d\n",vb.rate,vs.rate);
     CHECK(vb.rate<0,"large font slower"); CHECK(vs.rate>0,"small font faster");}
    // czcionka face -> predkosc + barwa(timbre)
    {Style f;f.fg=7;f.height=16;f.face=3; Voice v=VoiceFor(f,base,F);
     CHECK(v.rate!=0,"alt face changes rate"); CHECK(v.timbre!=0,"alt face changes timbre (barwa)");}

    // 3) styl bold -> glosnosc (volume) up
    {Style bo;bo.fg=7;bo.height=16;bo.bold=true; Voice v=VoiceFor(bo,base,F);
     printf("bold volume=%d stress=%d\n",v.volume,v.stress);
     CHECK(v.volume>0,"bold louder"); CHECK(v.stress>0,"bold firmer");}
    // dim -> quieter
    {Style d;d.fg=8;d.height=16; Voice v=VoiceFor(d,base,F); CHECK(v.volume<0,"dim quieter");}

    // 4) highlight -> modulacja (range) + echo(barwa)
    {Style h;h.fg=7;h.height=16;h.highlight=true; Voice v=VoiceFor(h,base,F);
     printf("highlight range=%d timbre=%u\n",v.range,v.timbre);
     CHECK(v.range>0,"highlight more modulation"); CHECK(v.timbre&1,"highlight echo (barwa)");}

    // 5) pozycja (wiersz) -> modulacja: top row more animated than bottom
    {Style s;s.fg=7;s.height=16;
     Voice top=VoiceFor(s,base,F,0,24);
     Voice bot=VoiceFor(s,base,F,23,24);
     printf("row range top=%d bottom=%d\n",top.range,bot.range);
     CHECK(top.range>bot.range,"top row more modulation than bottom");}

    // 6) non-monotone: a varied screen must not collapse to one voice
    std::set<long long> voices;
    for(int c=0;c<8;++c)for(int hl=0;hl<2;++hl)for(int bold=0;bold<2;++bold){
        Style s;s.fg=(uint8_t)c;s.height=16;s.highlight=hl;s.bold=bold;
        Voice v=VoiceFor(s,base,F,c*2,24);
        long long key=((long long)v.rate*37+v.pitch)*37+v.volume; key=key*37+v.range; key=key*37+(int)v.timbre;
        voices.insert(key);
    }
    printf("distinct voice combinations over varied styles: %zu\n",voices.size());
    CHECK(voices.size()>=16,"varied styles must give many distinct voices (non-monotone)");

    printf(fails? "\n%d CHECK(S) FAILED\n":"\nALL VOICEFOR MAPPING CHECKS PASSED\n",fails);
    return fails?1:0;
}
