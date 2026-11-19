#include "../src/efi/BootMix.h"
#include <cassert>
#include <cstdio>
int main() {
    using namespace hakaudioefi;
    int16_t bed[]={100,200,300,400,500,600},voice[]={1000,2000,3000,4000};
    int16_t chime[]={10000,11000,12000,13000,14000,15000},out[16]={};
    Mix mix;assert(mix.Background({bed,3}) && mix.Chime({chime,3}));
    assert(mix.Say({voice,2}));mix.Fill(out,1);assert(mix.BedPosition()==1);
    mix.Hush();mix.Fill(out,1);
    // Hush preserved the next background frame and the second chime frame.
    assert(out[0]==12300 && out[1]==13400 && mix.BedPosition()==2);
    assert(mix.Say({voice,2}));mix.Fill(out,1);
    assert(out[0]>15000 && out[1]>17000 && mix.BedPosition()==0);
    mix.Hush();mix.Fill(out,1);assert(out[0]==100 && out[1]==200);
    int16_t loud[]={32767,-32768};Mix clipped;
    assert(clipped.Say({loud,1}) && clipped.Chime({loud,1}));clipped.Fill(out,1);
    assert(out[0]==32767 && out[1]==-32768);
    Mix queue;for(int i=0;i<4;++i)assert(queue.Chime({loud,1}));
    assert(!queue.Chime({loud,1}));queue.Fill(out,4);assert(queue.Chime({loud,1}));
    mix.Handoff();const size_t at=mix.BedPosition();mix.Fill(out,8);
    for(auto value:out)assert(value==0);
    assert(mix.BedPosition()==at && !mix.Say({voice,2}) && !mix.Chime({chime,3}) && !mix.Background({bed,3}));
    puts("PASS mixer background/chime continuity across speech cancellation, saturation, queue and handoff");
}
