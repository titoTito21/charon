// Host tests of the pure logic in src/efi/CharonUsbAudioModel.h (r405, r407): descriptor
// parser (UAC 1.0, 2.0, 3.0 and BADD), packet pacing, the isochronous ring (against a
// small model of an xHCI endpoint), resampling and sample layout, feedback values and
// feedback pacing, the feedback ring, quirk lookup, event ring position.
//
// Build (Windows host, LLVM in work/llvm-23.1.5/bin; INC holds src/efi/haiku as "efi"):
//   clang++ -std=c++17 -fuse-ld=lld -I INC tests/CharonUsbAudioTests.cpp -o usbaudio-tests.exe
#include "../src/efi/CharonUsbAudio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(false)
using namespace charonusbaudio;

// ---- descriptors -------------------------------------------------------------
using Bytes=std::vector<uint8_t>;
static void Add(Bytes& to,std::initializer_list<int> bytes) {for(int b:bytes)to.push_back(uint8_t(b));}
static Bytes Finish(Bytes body,int interfaces) {
    Bytes all;const size_t total=9+body.size();
    Add(all,{9,2,int(total&0xff),int(total>>8),interfaces,1,0,0x80,50});
    all.insert(all.end(),body.begin(),body.end());return all;
}
// A UAC 1.0 speaker like QEMU's usb-audio: IT 1 (USB streaming) -> FU 2 -> OT 3 (speaker).
static Bytes Speaker(bool multi) {
    Bytes b;
    Add(b,{9,4,0,0,0,1,1,0,0});                       // AudioControl interface 0
    Add(b,{9,0x24,1,0x00,0x01,0x2b,0,1,1});           // header
    Add(b,{12,0x24,2,1,0x01,0x01,0,2,0x03,0x00,0,0});  // input terminal 1: USB streaming
    Add(b,{10,0x24,6,2,1,1,0x03,0x00,0x00,0});         // feature unit 2: master mute+volume, 2 channels none
    Add(b,{9,0x24,3,3,0x01,0x03,0,2,0});               // output terminal 3: speaker, source 2
    Add(b,{9,4,1,0,0,1,2,0,0});                       // AudioStreaming interface 1, alternate 0: no endpoint
    Add(b,{9,4,1,1,1,1,2,0,0});                       // alternate 1: stereo
    Add(b,{7,0x24,1,1,0,0x01,0x00});                   // AS general: terminal 1, PCM
    Add(b,{11,0x24,2,1,2,2,16,1,0x80,0xbb,0x00});      // format type I: 2 ch, 2 bytes, 16 bit, 48000
    Add(b,{9,5,0x01,0x0d,192,0,1,0,0});                // endpoint 1 OUT, isochronous adaptive, 192 bytes, 1 ms
    Add(b,{7,0x25,1,0,0,0,0});                         // class endpoint: no controls
    if(multi) {
        Add(b,{9,4,1,2,1,1,2,0,0});                   // alternate 2: 5.1
        Add(b,{7,0x24,1,1,0,0x01,0x00});
        Add(b,{11,0x24,2,1,6,2,16,1,0x80,0xbb,0x00});
        Add(b,{9,5,0x01,0x0d,0x40,0x02,1,0,0});        // 576 bytes
        Add(b,{7,0x25,1,0,0,0,0});
    }
    return Finish(b,2);
}
// A UAC 1.0 headset: speaker path IT 1 -> FU 5 (per-channel volume) -> OT 3, microphone path
// IT 4 (microphone) -> FU 6 -> OT 7 (USB streaming); playback 44.1 kHz only with a rate control;
// the recording interface comes first.
static Bytes Headset() {
    Bytes b;
    Add(b,{9,4,0,0,0,1,1,0,0});
    Add(b,{10,0x24,1,0x00,0x01,0x40,0,2,1,2});
    Add(b,{12,0x24,2,1,0x01,0x01,0,2,0x03,0x00,0,0});
    Add(b,{12,0x24,2,4,0x01,0x02,0,1,0x00,0x00,0,0});  // microphone
    Add(b,{10,0x24,6,5,1,1,0x01,0x02,0x02,0});         // FU 5: master mute, volume on channels 1 and 2
    Add(b,{9,0x24,6,6,4,1,0x03,0x00,0});               // FU 6: microphone
    Add(b,{9,0x24,3,3,0x02,0x03,0,5,0});               // headphones, source FU 5
    Add(b,{9,0x24,3,7,0x01,0x01,0,6,0});               // USB streaming (recording), source FU 6
    Add(b,{9,4,2,0,0,1,2,0,0});                       // recording interface 2
    Add(b,{9,4,2,1,1,1,2,0,0});
    Add(b,{7,0x24,1,7,0,0x01,0x00});
    Add(b,{11,0x24,2,1,1,2,16,1,0x80,0xbb,0x00});
    Add(b,{9,5,0x82,0x05,100,0,1,0,0});                // IN endpoint
    Add(b,{7,0x25,1,1,0,0,0});
    Add(b,{9,4,1,0,0,1,2,0,0});                       // playback interface 1
    Add(b,{9,4,1,1,1,1,2,0,0});
    Add(b,{7,0x24,1,1,0,0x01,0x00});
    Add(b,{14,0x24,2,1,2,2,16,2,0x44,0xac,0x00,0x22,0x56,0x00});   // 44100 and 22050
    Add(b,{9,5,0x03,0x09,200,0,1,0,0});                // endpoint 3 OUT, adaptive
    Add(b,{7,0x25,1,1,0,0,0});                         // sampling frequency control
    return Finish(b,3);
}
// A UAC 2.0 DAC: clock source 9 (programmable) -> clock selector 8; IT 1 -> FU 2 -> OT 3;
// alternate 1: 2 channels, 16 bit in 2 bytes; alternate 2: 24 bit in 4 bytes; 125 us interval.
static Bytes Dac2() {
    Bytes b;
    Add(b,{9,4,0,0,0,1,1,0x20,0});
    Add(b,{9,0x24,1,0x00,0x02,0x0a,0x40,0,0});
    Add(b,{8,0x24,0x0a,9,0x01,0x03,0,0});              // clock source 9: frequency programmable
    Add(b,{8,0x24,0x0b,8,1,9,0x03,0});                 // clock selector 8: one input (9)
    Add(b,{17,0x24,2,1,0x01,0x01,0,8,2,0x03,0,0,0,0,0,0,0});   // input terminal 1, clock 8
    Add(b,{18,0x24,6,2,1,0x0f,0,0,0,0,0,0,0,0,0,0,0,0});       // FU 2: master mute+volume programmable
    Add(b,{12,0x24,3,3,0x02,0x03,0,2,8,0,0,0});        // headphones, source 2
    Add(b,{9,4,1,0,0,1,2,0x20,0});
    Add(b,{9,4,1,1,1,1,2,0x20,0});
    Add(b,{16,0x24,1,1,0,1,1,0,0,0,2,3,0,0,0,0});
    Add(b,{6,0x24,2,1,2,16});
    Add(b,{7,5,0x01,0x05,0x00,0x04,1});                // endpoint 1 OUT, asynchronous, 1024 bytes, bInterval 1
    Add(b,{8,0x25,1,0,0,0,0,0});
    Add(b,{9,4,1,2,1,1,2,0x20,0});
    Add(b,{16,0x24,1,1,0,1,1,0,0,0,2,3,0,0,0,0});
    Add(b,{6,0x24,2,1,4,24});
    Add(b,{7,5,0x01,0x05,0x00,0x04,1});
    Add(b,{8,0x25,1,0,0,0,0,0});
    return Finish(b,2);
}
static void TestDescriptors() {
    Plan p;
    Bytes d=Speaker(false);
    CHECK(Parse(d.data(),d.size(),false,&p));
    CHECK(p.version==1 && p.controlInterface==0 && p.interface==1 && p.alternate==1 && p.endpoint==1 && p.maxPacket==192);
    CHECK(p.channels==2 && p.subframe==2 && p.bits==16 && p.rate==48000 && p.microframes==8 && !p.endpointRate && p.terminal==1);
    CHECK(p.unitCount==1 && p.units[0].id==2 && p.units[0].mute==1 && p.units[0].volume==1);
    d=Speaker(true);                                   // stereo is preferred over 5.1
    CHECK(Parse(d.data(),d.size(),false,&p) && p.alternate==1 && p.channels==2);
    // Only the 5.1 setting left (the stereo one mangled into a recording endpoint): six channels.
    {Bytes m=d;for(size_t i=0;i+9<=m.size();++i)if(m[i]==9 && m[i+1]==5 && m[i+4]==192) {m[i+2]=0x81;break;}
     CHECK(Parse(m.data(),m.size(),false,&p) && p.alternate==2 && p.channels==6 && p.maxPacket==576);}
    d=Headset();
    CHECK(Parse(d.data(),d.size(),false,&p));
    CHECK(p.interface==1 && p.alternate==1 && p.endpoint==3 && p.rate==44100 && p.endpointRate && p.terminal==1);
    CHECK(p.unitCount==1 && p.units[0].id==5 && p.units[0].mute==1 && p.units[0].volume==6);   // not the microphone's unit 6
    d=Dac2();
    CHECK(Parse(d.data(),d.size(),true,&p));
    CHECK(p.version==2 && p.alternate==1 && p.subframe==2 && p.channels==2 && p.rate==0 && p.microframes==1);
    CHECK(p.clock==9 && p.clockSettable && p.unitCount==1 && p.units[0].id==2 && p.units[0].mute==1 && p.units[0].volume==1);
    p.rate=48000;CHECK(Fits(p));                       // 6 or 7 frames of 4 bytes
    p.rate=96000;p.maxPacket=40;CHECK(!Fits(p));       // 12 frames need 48 bytes
    // High-speed interval 4 = 1 ms; full-speed interval 5 is out of range.
    CHECK(Microframes(4,true)==8 && Microframes(1,true)==1 && Microframes(1,false)==8 && Microframes(5,false)==0 && Microframes(0,true)==0);
    // Robustness: every truncation and every single-byte corruption parses without reading outside.
    for(const Bytes& whole:{Speaker(true),Headset(),Dac2()}) {
        for(size_t n=0;n<=whole.size();++n) {Bytes cut(whole.begin(),whole.begin()+n);Plan q;Parse(cut.data(),cut.size(),false,&q);Parse(cut.data(),cut.size(),true,&q);}
        for(size_t i=0;i<whole.size();++i)for(int v:{0,1,2,0xff}) {Bytes bad=whole;bad[i]=uint8_t(v);Plan q;Parse(bad.data(),bad.size(),false,&q);}
    }
    Plan untouched;untouched.rate=123;
    Bytes none=Finish({},0);CHECK(!Parse(none.data(),none.size(),false,&untouched) && untouched.rate==123);
    CHECK(PreferRate(8000,96000)==48000 && PreferRate(8000,44100)==44100 && PreferRate(8000,32000)==32000 && PreferRate(88200,96000)==88200);
    CHECK(RateRank(48000)<RateRank(44100) && RateRank(44100)<RateRank(32000) && RateRank(32000)<RateRank(16000));
    CHECK(PickVolume(-12800,0)==0 && PickVolume(-12800,1536)==0 && PickVolume(-12800,-256)==-256 && PickVolume(256,512)==512);
}

// ---- pacing -----------------------------------------------------------------
static void TestPacing() {
    Pacing a{44100,8};uint64_t sum=0;
    for(uint64_t td=0;td<1000;++td) {const uint32_t f=a.Frames(td);CHECK(f==44 || f==45);sum+=f;if(td%10==9)CHECK(sum==(td+1)/10*441);}
    CHECK(a.MaxFrames()==45);
    Pacing b{48000,8};for(uint64_t td=0;td<100;++td)CHECK(b.Frames(td)==48);CHECK(b.MaxFrames()==48);
    Pacing c{44100,1};sum=0;for(uint64_t td=0;td<8000;++td) {const uint32_t f=c.Frames(td);CHECK(f==5 || f==6);sum+=f;}CHECK(sum==44100 && c.MaxFrames()==6);
    Pacing e{48000,1};for(uint64_t td=0;td<100;++td)CHECK(e.Frames(td)==6);
    Pacing f{8000,1};for(uint64_t td=0;td<100;++td)CHECK(f.Frames(td)==1);
    // Far into a stream (a year of 125 us packets) the pattern is still exact.
    Pacing g{44100,1};const uint64_t far=8000ull*86400*365;CHECK(g.Frames(far)+g.Frames(far+1)+g.Frames(far+2)+g.Frames(far+3)>=22);
    CHECK(Stride(a,2,2)==256 && Stride(b,2,2)==256 && Stride(e,2,2)==64 && Stride(b,6,2)==1024);
    CHECK(RingSlots(8,256)==4096 && RingSlots(1,64)==32768 && RingSlots(8,1024)==2048 && Tds(5,8)==5 && Tds(5,1)==40 && Tds(1,16)==1);
}

// ---- the ring against a model of an xHCI isochronous endpoint ------------------
struct Endpoint {
    const IsoRing::Layout* l=nullptr;
    uint32_t at=0,cycle=1;bool underrun=false;uint64_t played=0;
    std::vector<uint8_t> out;std::vector<uint32_t> events;
    // One service interval: plays the TD at the dequeue pointer, or underruns.
    bool Step() {
        for(int hop=0;hop<2;++hop) {
            const Trb& t=l->trbs[at];
            if((t.control&1)!=cycle) {underrun=true;return false;}
            if(TrbType(t.control)==trbLink) {
                CHECK(at==l->slots && t.parameter==l->trbsDevice);
                if(t.control&2)cycle^=1;
                at=0;continue;
            }
            CHECK(TrbType(t.control)==trbIsoch && (t.control>>31)==1);          // isochronous, start as soon as possible
            CHECK(t.parameter==l->dataDevice+uint64_t(at)*l->stride);
            CHECK((t.status>>22)==l->interrupter);
            const uint32_t bytes=t.status&0x1ffff;CHECK(bytes<=l->stride);
            out.insert(out.end(),l->data+size_t(at)*l->stride,l->data+size_t(at)*l->stride+bytes);
            if(t.control&(1u<<5))events.push_back(at);
            ++at;++played;underrun=false;return true;
        }
        CHECK(false);return false;
    }
};
struct Fixture {
    std::vector<Trb> trbs;std::vector<uint8_t> data;std::vector<uint16_t> lengths;IsoRing::Layout l;IsoRing ring;Endpoint ep;
    Fixture(uint32_t slots,uint32_t rate,uint32_t microframes,uint8_t channels,uint8_t subframe,uint32_t every=1,uint8_t encoding=encodingPcm,uint32_t extra=0) {
        l.pacing=Pacing{rate,microframes};l.channels=channels;l.subframe=subframe;l.encoding=encoding;l.slots=slots;
        l.maxFrames=l.pacing.MaxFrames()+extra;                               // extra: frames of room for a feedback rate
        l.stride=StrideFor(l.maxFrames*channels*subframe);
        trbs.resize(slots+1);data.assign(size_t(slots)*l.stride,0xee);lengths.assign(slots,0xeeee);
        l.trbs=trbs.data();l.trbsDevice=0x10000000;l.data=data.data();l.dataDevice=0x20000000;l.lengths=lengths.data();l.packet=1024;
        l.interrupter=7;l.eventEvery=every;l.margin=5;l.horizon=slots-32;
        CHECK(ring.Init(l));ep.l=&l;
    }
    // The timer: completion events, then silence up to the horizon.
    void Service() {for(uint32_t slot:ep.events)ring.Completed(slot);ep.events.clear();ring.Feed();}
};
static int16_t Sample(const std::vector<uint8_t>& out,size_t frame,unsigned channel,unsigned channels,unsigned subframe) {
    const uint8_t* p=out.data()+(frame*channels+channel)*subframe+subframe-2;
    return int16_t(uint16_t(p[0])|uint16_t(p[1])<<8);
}
static void TestRing() {
    // 48 kHz stereo: what is written comes out once, in order, with silence around it,
    // over many laps of a small ring (cycle bits and the Link TRB).
    Fixture f(128,48000,8,2,2);
    CHECK(f.ring.Drained());
    f.Service();CHECK(f.ring.Queued()==96 && f.ring.Done()==0);
    std::vector<int16_t> in;int16_t next=1;size_t fed=0;
    for(int ms=0;ms<3000;++ms) {
        CHECK(f.ep.Step());
        f.Service();
        // The speech mix arrives in blocks of up to 128 frames, a little faster than real time.
        int16_t block[128*2];size_t frames=ms%3==0?128:32;
        for(size_t i=0;i<frames;++i) {block[i*2]=int16_t(next+int16_t(i));block[i*2+1]=int16_t(-(next+int16_t(i)));}
        bool handed=false;
        const size_t taken=f.ring.Write(block,frames,85,&handed);
        CHECK(taken<=frames);
        for(size_t i=0;i<taken;++i)in.push_back(int16_t(next+int16_t(i)));
        next=int16_t((next+int16_t(taken)-1)%30000+1);fed+=taken;
        CHECK(f.ring.Queued()-f.ring.Done()<=126);
    }
    CHECK(fed>100000);                                   // backpressure keeps it at real time: about 48 frames per ms
    CHECK(fed<3000*48+85*48+48);
    for(int ms=0;ms<200;++ms) {CHECK(f.ep.Step());f.Service();}
    CHECK(f.ring.Drained());
    // Out: silence (zeros), then the input without a gap in the middle? Gaps are allowed only as zeros:
    // every non-zero frame is the next input frame, left = -right.
    size_t at=0;const size_t frames=f.ep.out.size()/4;
    for(size_t i=0;i<frames;++i) {
        const int16_t l=Sample(f.ep.out,i,0,2,2),r=Sample(f.ep.out,i,1,2,2);
        if(!l && !r)continue;
        CHECK(at<in.size() && l==in[at] && r==int16_t(-in[at]));++at;
    }
    CHECK(at==in.size());
    CHECK(f.ep.played==3200 && !f.ep.underrun);

    // The lead limit: nothing is accepted beyond `lead` TDs ahead, and the first frame lands `margin` TDs ahead.
    Fixture g(256,48000,8,2,2);
    g.Service();
    std::vector<int16_t> ones(48*400*2,1000);bool handed=false;
    size_t taken=g.ring.Write(ones.data(),48*400,85,&handed);
    CHECK(taken==(85-3-5+1)*48 || taken==(85-2-5)*48);    // from TD 5 up to the lead (two TDs are kept for the resampler's worst case)
    CHECK(!g.ring.Drained());
    for(int i=0;i<5;++i)CHECK(g.ep.Step());
    CHECK(g.ep.out.size()==5*192);for(uint8_t b:g.ep.out)CHECK(b==0);           // the margin is silence
    CHECK(g.ep.Step());CHECK(Sample(g.ep.out,5*48,0,2,2)==1000);
    // Discard: what is not yet near the hardware becomes silence at once.
    g.Service();g.ring.Discard();
    const size_t before=g.ep.out.size()/4;
    for(int i=0;i<120;++i) {CHECK(g.ep.Step());g.Service();}
    size_t loud=0;for(size_t i=before;i<g.ep.out.size()/4;++i)if(Sample(g.ep.out,i,0,2,2))++loud;
    CHECK(loud<=5*48);                                    // at most the margin was still played
    CHECK(g.ring.Drained());
    // A late writer (the timer was held back, the ring ran dry): the stream goes on after a restart.
    for(int i=0;i<400 && g.ep.Step();++i) {}
    CHECK(g.ep.underrun);
    g.Service();CHECK(g.ep.Step());CHECK(!g.ep.underrun);
    taken=g.ring.Write(ones.data(),480,85,&handed);CHECK(taken==480);
    for(int i=0;i<30;++i) {CHECK(g.ep.Step());g.Service();}
    CHECK(g.ring.Drained());
    // Lost completion events (every 8th TD reports): the position is still exact at each report.
    Fixture h(128,48000,1,2,2,8);
    h.Service();
    for(int i=0;i<2000;++i) {CHECK(h.ep.Step());if(i%40==39)h.Service();}
    CHECK(h.ring.Done()==2000 && h.ring.Queued()==2000+96);
    // Rebase and Truncate.
    h.ring.Truncate();CHECK(h.ring.Queued()==2002);
    CHECK(h.ep.Step());CHECK(h.ep.Step());CHECK(!h.ep.Step() && h.ep.underrun);
    Fixture k(128,48000,8,2,2);k.Service();CHECK(k.ring.Queued()==96);k.ring.Rebase();CHECK(k.ring.Done()==96);
    // Refused layouts.
    IsoRing bad;IsoRing::Layout wrong=f.l;wrong.stride=100;CHECK(!bad.Init(wrong));
    wrong=f.l;wrong.slots=32;CHECK(!bad.Init(wrong));wrong=f.l;wrong.horizon=wrong.slots;CHECK(!bad.Init(wrong));
}
static void TestConversion() {
    // 44.1 kHz: 48000 input frames become 44100 output frames (within the resampler's one frame), a ramp stays monotonic.
    Fixture f(256,44100,8,2,2);
    f.Service();
    std::vector<int16_t> block(96);size_t fed=0;
    for(int ms=0;ms<1200;++ms) {
        CHECK(f.ep.Step());f.Service();
        if(fed<48000) {
            size_t frames=48;if(frames>48000-fed)frames=48000-fed;
            for(size_t i=0;i<frames;++i) {block[i*2]=block[i*2+1]=int16_t(1+(fed+i)/2);}   // a slow ramp: 1 .. 24000
            bool handed=false;const size_t taken=f.ring.Write(block.data(),frames,85,&handed);
            fed+=taken;
        }
    }
    CHECK(fed==48000);
    size_t loud=0;int16_t last=0;
    for(size_t i=0;i<f.ep.out.size()/4;++i) {
        const int16_t l=Sample(f.ep.out,i,0,2,2);
        if(!l)continue;
        CHECK(l>=last && l-last<=1);last=l;++loud;       // 160/147 input frames per output frame, half a step each
    }
    CHECK(loud>=44098 && loud<=44101);
    CHECK(last>=23999);
    // The TDs carry 44 or 45 frames: 44100 frames in 1000 TDs.
    CHECK(f.ep.out.size()==size_t(1200)*441/10*4);
    // 96 kHz: two output frames per input frame.
    Fixture u(256,96000,8,2,2);u.Service();
    int16_t ramp[200];for(int i=0;i<100;++i)ramp[i*2]=ramp[i*2+1]=int16_t(100+i*10);
    bool handed=false;CHECK(u.ring.Write(ramp,100,85,&handed)==100);
    for(int i=0;i<10;++i)CHECK(u.ep.Step());
    loud=0;for(size_t i=0;i<u.ep.out.size()/4;++i)if(Sample(u.ep.out,i,0,2,2))++loud;
    CHECK(loud==200);
    // Mono: the mean of both channels. Six channels in three-byte samples: front left and right, the rest silent.
    Fixture m(128,48000,8,1,2);m.Service();
    const int16_t pair[2]={1000,-200};CHECK(m.ring.Write(pair,1,85,&handed)==1);
    for(int i=0;i<6;++i)CHECK(m.ep.Step());
    CHECK(m.ep.out.size()==6*48*2 && Sample(m.ep.out,5*48,0,1,2)==400);
    Fixture s(128,48000,8,6,3);s.Service();
    const int16_t stereo[2]={0x1234,-2};CHECK(s.ring.Write(stereo,1,85,&handed)==1);
    for(int i=0;i<6;++i)CHECK(s.ep.Step());
    const uint8_t* p=s.ep.out.data()+size_t(5)*48*18;
    CHECK(p[0]==0 && p[1]==0x34 && p[2]==0x12 && p[3]==0 && p[4]==0xfe && p[5]==0xff);
    for(int i=6;i<18;++i)CHECK(p[i]==0);
}
static void TestEventRing() {
    std::vector<Trb> ring(16,Trb{0,0,0});
    uint32_t index=99,cycle=99;
    auto at=[&](uint32_t i)->const Trb& {return ring[i];};
    EventEnqueue(at,16,&index,&cycle);CHECK(index==0 && cycle==1);          // fresh ring: the first lap writes cycle 1
    for(int i=0;i<5;++i)ring[i].control=1;
    EventEnqueue(at,16,&index,&cycle);CHECK(index==5 && cycle==1);
    for(int i=0;i<16;++i)ring[i].control=1;
    EventEnqueue(at,16,&index,&cycle);CHECK(index==0 && cycle==0);          // exactly full lap: the next lap toggles
    for(int i=0;i<3;++i)ring[i].control=0;
    EventEnqueue(at,16,&index,&cycle);CHECK(index==3 && cycle==0);          // second lap in progress
}
// ---- r407: the other classes, feedback, encodings ---------------------------------
// A UAC 2.0 high-speed headset: asynchronous OUT endpoint with a feedback endpoint (bInterval 4),
// selector unit 4 (pin 2 comes from the stream), clock selector 8 with two sources (10 fixed, 9 programmable),
// headphones and an S/PDIF output; the recording interface 2 carries an implicit feedback data endpoint.
static Bytes Async2() {
    Bytes b;
    Add(b,{8,0x0b,0,3,1,0,0x20,0});                    // interface association: audio function, UAC 2.0
    Add(b,{9,4,0,0,0,1,1,0x20,0});
    Add(b,{9,0x24,1,0x00,0x02,0x08,0x60,0,0});
    Add(b,{8,0x24,0x0a,10,0x01,0x05,0,0});             // clock source 10: frequency read only, validity readable
    Add(b,{8,0x24,0x0a,9,0x01,0x03,0,0});              // clock source 9: frequency programmable
    Add(b,{9,0x24,0x0b,8,2,10,9,0x03,0});              // clock selector 8: inputs 10 and 9, programmable
    Add(b,{17,0x24,2,1,0x01,0x01,0,8,2,0x03,0,0,0,0,0,0,0});   // input terminal 1 (USB streaming), clock 8
    Add(b,{17,0x24,2,6,0x01,0x02,0,8,1,0x00,0,0,0,0,0,0,0});   // input terminal 6 (microphone)
    Add(b,{9,0x24,5,4,2,6,1,0x03,0});                  // selector unit 4: pins (6, 1)
    Add(b,{18,0x24,6,2,4,0x03,0,0,0,0x0c,0,0,0,0x0c,0,0,0,0});   // FU 2 (source 4): master mute, volume on channels 1 and 2
    Add(b,{12,0x24,3,3,0x02,0x03,0,2,8,0,0,0});        // headphones, source 2
    Add(b,{12,0x24,3,5,0x05,0x06,0,4,8,0,0,0});        // S/PDIF, source 4
    Add(b,{12,0x24,3,7,0x01,0x01,0,6,8,0,0,0});        // USB streaming (recording), source 6
    Add(b,{9,4,1,0,0,1,2,0x20,0});
    Add(b,{9,4,1,1,2,1,2,0x20,0});
    Add(b,{16,0x24,1,1,0,1,1,0,0,0,2,3,0,0,0,0});
    Add(b,{6,0x24,2,1,3,24});                          // 24 bit in 3 bytes
    Add(b,{7,5,0x81,0x11,4,0,4});                      // feedback endpoint: isochronous IN, usage "feedback", 4 bytes, bInterval 4 (comes first here)
    Add(b,{7,5,0x01,0x05,0x00,0x0c,1});                // data: asynchronous, 1024 bytes, high bandwidth (two transactions), 125 us
    Add(b,{8,0x25,1,0,0,0,0,0});
    Add(b,{9,4,2,0,0,1,2,0x20,0});
    Add(b,{9,4,2,1,1,1,2,0x20,0});
    Add(b,{16,0x24,1,7,0,1,1,0,0,0,1,0,0,0,0,0});
    Add(b,{6,0x24,2,1,2,16});
    Add(b,{7,5,0x82,0x25,0x64,0,1});                   // recording data, usage "implicit feedback data"
    return Finish(b,3);
}
// A UAC 1.0 full-speed DAC with an asynchronous endpoint and its synch endpoint (bSynchAddress), 8-bit and float settings.
static Bytes Async1(int alternates) {
    Bytes b;
    Add(b,{9,4,0,0,0,1,1,0,0});
    Add(b,{9,0x24,1,0x00,0x01,0x1e,0,1,1});
    Add(b,{12,0x24,2,1,0x01,0x01,0,2,0x03,0x00,0,0});
    Add(b,{9,0x24,3,3,0x01,0x03,0,1,0});               // speaker straight from the terminal: no feature unit
    Add(b,{9,4,1,0,0,1,2,0,0});
    if(alternates&1) {
        Add(b,{9,4,1,1,2,1,2,0,0});
        Add(b,{7,0x24,1,1,0,0x01,0x00});
        Add(b,{11,0x24,2,1,2,2,16,1,0x44,0xac,0x00});  // 44100 only
        Add(b,{9,5,0x01,0x05,0xc8,0,1,0,0x82});        // asynchronous, 200 bytes, synch endpoint 0x82
        Add(b,{7,0x25,1,0,0,0,0});
        Add(b,{9,5,0x82,0x01,3,0,1,5,0});              // synch endpoint: 3 bytes, bRefresh 5
    }
    if(alternates&2) {
        Add(b,{9,4,1,2,1,1,2,0,0});
        Add(b,{7,0x24,1,1,0,0x02,0x00});               // PCM8
        Add(b,{11,0x24,2,1,1,1,8,1,0x40,0x1f,0x00});   // mono, 8 bit, 8000
        Add(b,{9,5,0x01,0x09,16,0,1,0,0});
        Add(b,{7,0x25,1,0,0,0,0});
    }
    if(alternates&4) {
        Add(b,{9,4,1,3,1,1,2,0,0});
        Add(b,{7,0x24,1,1,0,0x03,0x00});               // IEEE float
        Add(b,{11,0x24,2,1,2,4,32,1,0x80,0xbb,0x00});
        Add(b,{9,5,0x01,0x09,0x90,0x01,1,0,0});        // 400 bytes
        Add(b,{7,0x25,1,0,0,0,0});
    }
    return Finish(b,2);
}
// UAC 3.0. BADD headset adapter (profile 0x25): nothing but interface and endpoint descriptors.
static Bytes Badd(int packet) {
    Bytes b;
    Add(b,{8,0x0b,0,3,1,0x25,0x30,0});
    Add(b,{9,4,0,0,0,1,1,0x30,0});
    Add(b,{9,4,1,0,0,1,2,0x30,0});
    Add(b,{9,4,1,1,1,1,2,0x30,0});
    Add(b,{7,5,0x01,0x0d,packet&0xff,packet>>8,1});
    Add(b,{9,4,2,0,0,1,2,0x30,0});
    Add(b,{9,4,2,1,1,1,2,0x30,0});
    Add(b,{7,5,0x82,0x0d,0x60,0,1});
    return Finish(b,3);
}
// UAC 3.0 with full descriptors on SuperSpeed: IT 1 -> FU 2 -> OT 3 (headset), clock source 9.
static Bytes Full3() {
    Bytes b;
    Add(b,{8,0x0b,0,2,1,0x01,0x30,0});                 // full ADC 3.0 function
    Add(b,{9,4,0,0,0,1,1,0x30,0});
    Add(b,{10,0x24,1,0x04,0x40,0x00,0,0,0,0});          // header: category headset
    Add(b,{12,0x24,0x0b,9,0x01,0x03,0,0,0,0,0,0});      // clock source 9: frequency programmable
    Add(b,{20,0x24,2,1,0x01,0x01,0,9,0,0,0,0,0x21,0,0,0,0,0,0,0});   // input terminal 1, clock 9, cluster 0x21
    Add(b,{15,0x24,7,2,1,0x0f,0,0,0,0,0,0,0,0,0});      // FU 2: master mute+volume
    Add(b,{19,0x24,3,3,0x02,0x04,0,2,9,0,0,0,0,0,0,0,0,0,0});   // output terminal 3: headset, source 2
    Add(b,{9,4,1,0,0,1,2,0x30,0});
    Add(b,{9,4,1,1,1,1,2,0x30,0});
    Add(b,{23,0x24,1,1,0,0,0,0,0x21,0,1,0,0,0,0,0,0,0,2,16,0,0,0});   // AS general: terminal 1, cluster 0x21, PCM, 2 bytes, 16 bit
    Add(b,{7,5,0x01,0x0d,0x00,0x04,4});                // 1024 bytes, 1 ms
    Add(b,{6,0x30,0,0,0xc0,0});                        // SuperSpeed companion: burst 0, 192 bytes per interval
    Add(b,{10,0x25,1,0,0,0,0,0,0,0});
    return Finish(b,2);
}
static void TestClasses() {
    Plan p;
    Bytes d=Async2();
    CHECK(Parse(d.data(),d.size(),speedHigh,1,&p));
    CHECK(p.version==2 && p.interface==1 && p.alternate==1 && p.endpoint==1 && ((p.attributes>>2)&3)==1 && p.subframe==3 && p.bits==24 && p.channels==2);
    CHECK(p.feedbackEndpoint==0x81 && p.feedbackPacket==4 && p.feedbackMicroframes==8 && !p.implicit && p.microframes==1);
    CHECK((p.maxPacket&0x7ff)==1024 && p.burst==1 && Capacity(p)==2048);
    CHECK(p.selectorCount==1 && p.selectors[0].id==4 && p.selectors[0].pin==2);
    CHECK(p.unitCount==1 && p.units[0].id==2 && p.units[0].mute==1 && p.units[0].volume==6);
    CHECK(p.outputs==2 && p.outputType==0x0302);                               // headphones before S/PDIF
    CHECK(p.terminalClock==8 && p.clockCount==3 && p.clock==10 && !p.clockSettable);   // the selector's first input until the device is asked
    const Clock* through=nullptr;
    const Clock* second=ResolveClock(p,[](const Clock&) {return uint8_t(2);},&through);
    CHECK(second && second->id==9 && second->settable && !second->validity && through && through->id==8 && through->selectable);
    const Clock* first=ResolveClock(p,[](const Clock&) {return uint8_t(9);});   // a pin that does not exist: the first
    CHECK(first && first->id==10 && first->validity && !first->settable);
    CHECK(!Parse(d.data(),d.size(),speedHigh,2,&p));                           // the recording interface plays nothing
    CHECK(Parse(d.data(),d.size(),speedHigh,-1,&p) && p.interface==1);
    p.rate=48000;CHECK(Fits(p) && TdFrames(p)==7);                             // 6 frames nominal, one more for the feedback
    p.rate=768000;p.channels=4;CHECK(Fits(p));p.burst=0;CHECK(!Fits(p));       // 96 frames of 12 bytes need the second transaction
    d=Async1(1);
    CHECK(Parse(d.data(),d.size(),speedFull,1,&p));
    CHECK(p.version==1 && p.rate==44100 && p.rateCount==1 && p.feedbackEndpoint==0x82 && p.feedbackPacket==3 && p.feedbackMicroframes==8 && !p.implicit && p.unitCount==0 && p.outputs==1 && p.outputType==0x0301);
    CHECK(TdFrames(p)==50);                                                    // 45 nominal + an eighth + 1, as many as 200 bytes hold
    d=Async1(7);                                                               // 16-bit PCM wins over 8-bit and float
    CHECK(Parse(d.data(),d.size(),speedFull,-1,&p) && p.alternate==1 && p.encoding==encodingPcm);
    d=Async1(6);                                                               // float (48 kHz stereo) before 8-bit mono 8 kHz
    CHECK(Parse(d.data(),d.size(),speedFull,-1,&p) && p.alternate==3 && p.encoding==encodingFloat && p.subframe==4);
    d=Async1(2);
    CHECK(Parse(d.data(),d.size(),speedFull,-1,&p) && p.alternate==2 && p.encoding==encodingPcm8 && p.subframe==1 && p.rate==8000 && p.channels==1);
    // Basic Audio Device Definition: the format from wMaxPacketSize.
    d=Badd(0xc4);
    CHECK(Parse(d.data(),d.size(),speedFull,1,&p));
    CHECK(p.version==3 && p.badd && p.channels==2 && p.subframe==2 && p.rate==48000 && p.terminal==1 && p.clock==9 && !p.clockSettable);
    CHECK(p.unitCount==1 && p.units[0].id==2 && p.units[0].mute==1 && p.outputType==0x0402 && Fits(p));
    d=Badd(0x93);CHECK(Parse(d.data(),d.size(),speedFull,1,&p) && p.channels==1 && p.subframe==3);
    d=Badd(0x77);CHECK(!Parse(d.data(),d.size(),speedFull,1,&p));              // not a BADD packet size
    d=Badd(0xc0);CHECK(!Parse(d.data(),d.size(),speedFull,2,&p));              // the microphone interface
    // Full UAC 3.0 descriptors, SuperSpeed.
    d=Full3();
    CHECK(Parse(d.data(),d.size(),speedSuper,1,&p));
    CHECK(p.version==3 && !p.badd && p.cluster==0x21 && p.channels==0 && p.subframe==2 && p.bits==16 && p.microframes==8);
    CHECK(p.clock==9 && p.clockSettable && p.unitCount==1 && p.units[0].id==2 && p.units[0].mute==1 && p.units[0].volume==1 && p.outputType==0x0402);
    CHECK(p.burst==0 && p.mult==0 && p.bytesPerInterval==192 && Capacity(p)==192);
    p.channels=2;p.rate=48000;CHECK(Fits(p));p.rate=96000;CHECK(!Fits(p));
    for(const Bytes& whole:{Async2(),Async1(7),Badd(0xc4),Full3()}) {
        for(size_t n=0;n<=whole.size();++n) {Bytes cut(whole.begin(),whole.begin()+n);Plan q;Parse(cut.data(),cut.size(),speedFull,-1,&q);Parse(cut.data(),cut.size(),speedSuper,1,&q);}
        for(size_t i=0;i<whole.size();++i)for(int v:{0,1,2,3,4,8,0x7f,0xff}) {Bytes bad=whole;bad[i]=uint8_t(v);Plan q;Parse(bad.data(),bad.size(),speedHigh,-1,&q);}
    }
    CHECK(OutputRank(0x0302)<OutputRank(0x0301) && OutputRank(0x0402)<OutputRank(0x0301) && OutputRank(0x0301)<OutputRank(0x0603) && OutputRank(0x0603)<OutputRank(0x0605));
    // Quirk data from the kernel table: a device entry, a vendor entry, nothing.
    CHECK(QuirksOf(0x045e,0x083c)==(quirk::noRateReadBack|quirk::delay20ms));
    CHECK(QuirksOf(0x047f,0x1234)==(quirk::noRateReadBack|quirk::delay20ms));  // Plantronics (vendor entry)
    CHECK(QuirksOf(0x0763,0x2030)&quirk::implicitFeedback);
    CHECK(QuirksOf(0xffff,0xffff)==0);
}
static void TestFeedback() {
    // Full speed, 48 kHz: 10.14 in three bytes per frame.
    Feedback f;f.Init(48000,8,54);
    CHECK(f.nominal==48u<<16 && f.maximum==54u<<16);
    CHECK(f.Take(48u<<14,3) && f.current==48u<<16 && f.shift==2);
    CHECK(f.Take((48u<<14)+0x1000,3) && f.current==(48u<<16)+0x4000);          // 48.25 frames per frame
    CHECK(!f.Take(0,3) && !f.Take(48u<<14,2));                                  // nothing, too short
    CHECK(!f.Take(80u<<14,3) && f.shift==Feedback::unknown);                    // out of range: the format is looked for again
    CHECK(f.Take(48u<<14,3) && f.shift==2 && f.accepted==3 && f.rejected==1);
    // High speed, 44.1 kHz, one TD per microframe: 16.16 per microframe in four bytes.
    Feedback h;h.Init(44100,1,7);
    const uint32_t perMicroframe=uint32_t(44100ull*65536/8000);
    CHECK(h.Take(perMicroframe,4) && h.shift==0 && h.current==perMicroframe);
    // The same device read with a 1 ms service interval: eight times as many frames per TD.
    Feedback k;k.Init(44100,8,50);
    CHECK(k.Take(perMicroframe,4) && k.shift==3 && k.current==perMicroframe*8);
    // A full-speed device that sends 16.16 per frame (four bytes): no shift.
    Feedback w;w.Init(48000,8,54);CHECK(w.Take(48u<<16,4) && w.shift==0);

    // The ring follows a feedback rate: 48.25 frames per TD instead of 48, and the speech is whole.
    Fixture r(256,48000,8,2,2,1,encodingPcm,6);
    CHECK(r.ring.MaxFrames()==54);
    // A device that asks for more than a packet holds gets what the packet holds (the cap of the endpoint).
    {Fixture c(128,48000,8,2,2,1,encodingPcm,2);c.Service();c.ring.SetFeedback(60u<<16);
     for(int ms=0;ms<60;++ms) {CHECK(c.ep.Step());c.Service();c.ring.Pace(30);}
     CHECK(c.ep.out.size()/4<=60*50 && c.ep.out.size()/4>40*50);for(uint16_t n:c.lengths)CHECK(n<=50);}
    r.Service();
    r.ring.SetFeedback((48u<<16)+0x4000);
    std::vector<int16_t> in;int16_t next=1;uint64_t before=0;
    for(int ms=0;ms<2000;++ms) {
        CHECK(r.ep.Step());r.Service();r.ring.Pace(30);
        if(ms==999)before=r.ep.out.size()/4;
        int16_t block[64*2];const size_t frames=64;
        for(size_t i=0;i<frames;++i) {block[i*2]=int16_t(next+int16_t(i));block[i*2+1]=int16_t(-(next+int16_t(i)));}
        bool handed=false;const size_t taken=r.ring.Write(block,frames,85,&handed);
        for(size_t i=0;i<taken;++i)in.push_back(int16_t(next+int16_t(i)));
        next=int16_t((next+int16_t(taken)-1)%30000+1);
    }
    const uint64_t second=r.ep.out.size()/4-before;                             // frames played in the second second
    CHECK(second>=48249 && second<=48251);
    for(int ms=0;ms<200;++ms) {CHECK(r.ep.Step());r.Service();r.ring.Pace(30);}
    CHECK(r.ring.Drained());
    size_t at=0;
    for(size_t i=0;i<r.ep.out.size()/4;++i) {
        const int16_t l=Sample(r.ep.out,i,0,2,2),rr=Sample(r.ep.out,i,1,2,2);
        if(!l && !rr)continue;
        CHECK(at<in.size() && l==in[at] && rr==int16_t(-in[at]));++at;
    }
    CHECK(at==in.size() && in.size()>90000);
    // Back to the nominal rate: 48 frames again.
    r.ring.SetFeedback(0);
    for(int ms=0;ms<200;++ms) {CHECK(r.ep.Step());r.Service();r.ring.Pace(30);}
    const uint64_t mark=r.ep.out.size()/4;
    for(int ms=0;ms<100;++ms) {CHECK(r.ep.Step());r.Service();r.ring.Pace(30);}
    CHECK(r.ep.out.size()/4-mark==4800);
    // A slower device at 8 kHz, 125 us: TDs of one frame, now and then none.
    Fixture z(512,8000,1,1,2);z.Service();
    z.ring.SetFeedback(0xf000);                                                 // 0.9375 frames per TD
    std::vector<int16_t> tone(2*2000,700);size_t fed=0;
    for(int td=0;td<16000;++td) {
        CHECK(z.ep.Step());
        if(td%8==7) {z.Service();z.ring.Pace(240);bool handed=false;if(fed<2000)fed+=z.ring.Write(tone.data()+fed*2,2000-fed,680,&handed);}
    }
    CHECK(fed==2000 && z.ep.out.size()/2>=14900 && z.ep.out.size()/2<=15100);
}
static void TestEncodings() {
    bool handed=false;
    const int16_t loud[2]={0x4000,-0x4000},quiet[2]={0x0100,0x0100};
    // Unsigned 8-bit mono: silence is 0x80.
    Fixture u(128,48000,8,1,1,1,encodingPcm8);u.Service();
    CHECK(u.ring.Write(loud,1,85,&handed)==1 && u.ring.Write(quiet,1,85,&handed)==1);
    for(int i=0;i<6;++i)CHECK(u.ep.Step());
    CHECK(u.ep.out[0]==0x80 && u.ep.out[5*48]==0x80 && u.ep.out[5*48+1]==0x81 && u.ep.out[5*48+2]==0x80);   // (0x4000-0x4000)/2 = 0, then 0x0100
    // Signed 8-bit stereo.
    Fixture s(128,48000,8,2,1);s.Service();
    CHECK(s.ring.Write(loud,1,85,&handed)==1);
    for(int i=0;i<6;++i)CHECK(s.ep.Step());
    CHECK(s.ep.out[0]==0 && s.ep.out[5*96]==0x40 && s.ep.out[5*96+1]==0xc0);
    // IEEE float stereo: 0.5 and -0.5.
    Fixture f(128,48000,8,2,4,1,encodingFloat);f.Service();
    CHECK(f.ring.Write(loud,1,85,&handed)==1);
    for(int i=0;i<6;++i)CHECK(f.ep.Step());
    const uint8_t* p=f.ep.out.data()+size_t(5)*48*8;
    CHECK(p[0]==0 && p[1]==0 && p[2]==0 && p[3]==0x3f && p[4]==0 && p[5]==0 && p[6]==0 && p[7]==0xbf);
    CHECK(FloatBits(0)==0 && FloatBits(-32768)==0xbf800000u && FloatBits(1)==0x38000000u && FloatBits(32767)==0x3f7ffe00u);
    // 16 kHz: the low-pass in front of the resampler leaves a steady level alone and one frame in three comes out.
    Fixture n(256,16000,8,2,2);n.Service();
    std::vector<int16_t> level(4800*2,8000);size_t fed=0;
    for(int ms=0;ms<400;++ms) {CHECK(n.ep.Step());n.Service();if(fed<4800)fed+=n.ring.Write(level.data()+fed*2,4800-fed,85,&handed);}
    CHECK(fed==4800);
    size_t steady=0,loudFrames=0;
    for(size_t i=0;i<n.ep.out.size()/4;++i) {const int16_t v=Sample(n.ep.out,i,0,2,2);if(v)++loudFrames;if(v>=7990 && v<=8000)++steady;}
    CHECK(loudFrames>=1598 && loudFrames<=1601 && steady>1500);
    // High-bandwidth endpoint: a TD of 2304 bytes is three packets of one burst.
    Fixture hb(128,96000,8,6,4);hb.l.packet=1024;hb.l.burst=2;CHECK(hb.ring.Init(hb.l));hb.ring.Feed();
    const Trb& t=hb.trbs[0];
    CHECK((t.status&0x1ffff)==2304 && ((t.control>>7)&3)==0 && ((t.control>>16)&0xf)==2);
    // SuperSpeed: bursts of two packets, 2304 bytes are three packets: two bursts, one packet in the last.
    Fixture ss(128,96000,8,6,4);ss.l.packet=1024;ss.l.burst=1;CHECK(ss.ring.Init(ss.l));ss.ring.Feed();
    CHECK(((ss.trbs[0].control>>7)&3)==1 && ((ss.trbs[0].control>>16)&0xf)==0);
}
static void TestFeedbackRing() {
    std::vector<Trb> trbs(129);std::vector<uint8_t> data(128*8,0xee);
    FeedbackRing::Layout l;l.trbs=trbs.data();l.trbsDevice=0x30000000;l.data=data.data();l.dataDevice=0x40000000;l.slots=128;l.packet=3;l.interrupter=5;l.eventEvery=4;l.horizon=64;
    FeedbackRing ring;CHECK(!ring.Active() && !ring.Feed());
    CHECK(ring.Init(l) && ring.Active() && ring.Feed() && ring.Queued()==64);
    CHECK(TrbType(trbs[0].control)==trbIsoch && (trbs[0].status&0x1ffff)==3 && (trbs[0].status>>22)==5 && trbs[0].parameter==0x40000000 && !(trbs[0].control&(1u<<5)) && (trbs[3].control&(1u<<5)));
    // The device answers in slot 3 with three bytes; slot 7 was an error; slot 11 a short answer.
    data[3*8]=0x00;data[3*8+1]=0x00;data[3*8+2]=0x0c;data[3*8+3]=0xee;
    uint32_t raw=0,bytes=0;
    CHECK(ring.Completed(3,0,true,&raw,&bytes) && bytes==3 && (raw&0xffffff)==0x0c0000 && ring.Done()==4);
    CHECK(!ring.Completed(7,3,false,&raw,&bytes) && ring.Done()==8);
    CHECK(ring.Completed(11,2,true,&raw,&bytes) && bytes==1 && ring.Done()==12);
    CHECK(!ring.Completed(200,0,true,&raw,&bytes) && !ring.Completed(100,0,true,&raw,&bytes));   // outside, not queued
    // Many laps.
    for(int lap=0;lap<1000;++lap) {ring.Feed();const uint32_t slot=uint32_t((ring.Done()+3)%128);CHECK(ring.Completed(slot,0,true,&raw,&bytes));}
    CHECK(ring.Done()==12+4000 && ring.Queued()-ring.Done()<=64);
    ring.Truncate();CHECK(ring.Queued()==ring.Done()+2);
    FeedbackRing::Layout bad=l;bad.packet=2;CHECK(!ring.Init(bad));bad=l;bad.slots=16;CHECK(!ring.Init(bad));
}
int main() {
    TestDescriptors();TestPacing();TestRing();TestConversion();TestEventRing();
    TestClasses();TestFeedback();TestEncodings();TestFeedbackRing();
    puts("PASS CharonUsbAudioTests");return 0;
}
