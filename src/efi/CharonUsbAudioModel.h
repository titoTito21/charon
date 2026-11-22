// Charon's USB Audio Class output: the pure logic (r407). GPL-2.0-or-later.
//
// No firmware calls here: descriptor parser for USB Audio Class 1.0, 2.0 and
// 3.0 (also the Basic Audio Device Definition profiles), packet pacing, the
// feedback value of asynchronous endpoints, sample conversion, and the two
// transfer rings (data out, feedback in). Tested on the host by
// tests/CharonUsbAudioTests.cpp. The device side is CharonUsbAudio.h; what is
// supported and where each rule comes from is written down in USB-AUDIO.txt.
//
// Sources: usb.org "Device Class Definition for Audio Devices" 1.0/2.0/3.0 with
// "Audio Data Formats", "Terminal Types" and "Basic Audio Device Definition
// 3.0" (descriptor layouts, constants); xHCI 1.2 (TRB and context layouts).
// The way a feedback value's fixed-point format is recognised (shift until it
// matches the nominal rate) is the method of Linux sound/usb/endpoint.c
// (snd_usb_handle_sync_urb, GPL-2.0-or-later), written anew here.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "CharonUsbAudioQuirks.h"
namespace charonusbaudio {
constexpr uint32_t sourceRate=48000;   // Charon's mix: 48 kHz stereo PCM16
enum : uint8_t {speedFull=1,speedLow=2,speedHigh=3,speedSuper=4};   // xHCI port speed IDs (5 and above: SuperSpeedPlus)
enum : uint8_t {encodingPcm=0,encodingPcm8=1,encodingFloat=2};      // signed PCM, unsigned 8-bit PCM, IEEE 754 single
enum : uint8_t {clockSource=1,clockSelector=2,clockMultiplier=3};

// ---- what to play on, read from the configuration descriptor ----------------
struct Unit {uint8_t id=0;uint32_t mute=0,volume=0;};   // bit n: channel n has the control (0 = master)
struct Selector {uint8_t id=0,pin=0;};                  // selector unit on the way; the input pin (from 1) that leads to the stream
struct Clock {uint8_t id=0,kind=0,sourceCount=0,sources[8]={};bool settable=false,validity=false,selectable=false;};
struct Plan {
    uint8_t version=0;                 // 1 = UAC 1.0, 2 = UAC 2.0, 3 = UAC 3.0
    bool badd=false;                   // UAC 3.0 Basic Audio Device Definition: no class descriptors, everything inferred
    uint8_t speed=speedFull;
    uint8_t controlInterface=0;        // AudioControl interface number
    uint8_t interface=0,alternate=0;   // AudioStreaming interface and its chosen alternate setting
    uint8_t endpoint=0,attributes=0,interval=0;uint16_t maxPacket=0;   // isochronous OUT endpoint
    uint8_t burst=0,mult=0;uint32_t bytesPerInterval=0;                // high-bandwidth transactions / SuperSpeed companion
    uint32_t microframes=0;            // service interval in 125 us units
    uint8_t channels=0,subframe=0,bits=0,encoding=encodingPcm;
    uint32_t rate=0;                   // UAC 1: chosen here. UAC 2/3: 0 until the clock's range is read
    uint8_t rateCount=0;               // UAC 1: discrete rates of the alternate setting (0 = a continuous range)
    bool endpointRate=false;           // UAC 1: the endpoint has a sampling frequency control
    uint8_t terminal=0;                // the USB streaming input terminal this interface feeds
    uint16_t cluster=0;                // UAC 3: cluster descriptor (number of channels), read by a request
    uint16_t outputType=0;uint8_t outputs=0;   // the best output terminal fed by the stream, and how many there are
    uint8_t terminalClock=0;Clock clocks[8];size_t clockCount=0;       // UAC 2/3 clock entities
    uint8_t clock=0;bool clockSettable=false;                          // the clock source (first selector inputs; the device asks the selectors)
    Unit units[8];size_t unitCount=0;  // feature units between that terminal and an output terminal
    Selector selectors[4];size_t selectorCount=0;
    uint8_t feedbackEndpoint=0,feedbackInterval=0;uint16_t feedbackPacket=0;uint32_t feedbackMicroframes=0;   // explicit feedback (isochronous IN)
    bool implicit=false;               // asynchronous without a feedback endpoint: the device expects implicit feedback
};
// 48 kHz needs no resampling, 44.1 kHz is the other common rate; then the nearest.
inline uint32_t RateRank(uint32_t rate) {
    if(rate==48000)return 0;
    if(rate==44100)return 1;
    const uint32_t distance=rate>48000?rate-48000:48000-rate;
    return 2+distance/1000;
}
inline bool RateUsable(uint32_t rate) {return rate>=8000 && rate<=768000;}
// The best rate of a continuous range.
inline uint32_t PreferRate(uint32_t low,uint32_t high) {
    if(low>high) {const uint32_t t=low;low=high;high=t;}
    if(low<=48000 && high>=48000)return 48000;
    if(low<=44100 && high>=44100)return 44100;
    return high<48000?high:low;
}
// Service interval in microframes from bInterval; 0 when out of the supported range (125 us .. 8 ms).
// High speed and SuperSpeed count 2^(bInterval-1) microframes, full speed 2^(bInterval-1) frames.
inline uint32_t Microframes(uint8_t interval,bool highSpeed) {
    if(highSpeed)return interval>=1 && interval<=7?1u<<(interval-1):0;
    return interval>=1 && interval<=4?8u<<(interval-1):0;
}
// Frames per service interval: rate*microframes/8000, spread exactly (44.1 kHz at
// 1 ms: nine packets of 44 frames, then one of 45).
struct Pacing {
    uint32_t rate=48000,microframes=8;
    uint32_t Frames(uint64_t td) const {const uint64_t k=uint64_t(rate)*microframes;return uint32_t((td+1)*k/8000-td*k/8000);}
    uint32_t MaxFrames() const {return uint32_t((uint64_t(rate)*microframes+7999)/8000);}
};
// Bytes one service interval of the endpoint may carry.
inline uint32_t Capacity(const Plan& p) {
    const uint32_t packet=p.maxPacket&0x7ffu;
    if(p.speed>=speedSuper) {
        const uint32_t all=packet*(uint32_t(p.burst)+1)*(uint32_t(p.mult)+1);
        return p.bytesPerInterval && p.bytesPerInterval<all?p.bytesPerInterval:all;
    }
    if(p.speed==speedHigh)return packet*(uint32_t(p.burst)+1);   // high bandwidth: up to three transactions per microframe
    return packet;
}
// The largest packet of the format fits the endpoint.
inline bool Fits(const Plan& p) {
    if(!RateUsable(p.rate) || !p.microframes || !p.channels || !p.subframe)return false;
    const Pacing pacing{p.rate,p.microframes};
    return uint64_t(pacing.MaxFrames())*p.channels*p.subframe<=Capacity(p);
}
// The most frames one TD ever carries: with a feedback endpoint the device may ask
// for more than the nominal rate (an eighth more at most, within the endpoint).
inline uint32_t TdFrames(const Plan& p) {
    const Pacing pacing{p.rate,p.microframes};
    uint32_t frames=pacing.MaxFrames();
    const uint32_t bytes=uint32_t(p.channels)*p.subframe;
    if(p.feedbackEndpoint && bytes) {
        uint32_t more=frames+frames/8+1;const uint32_t fit=Capacity(p)/bytes;
        if(more>fit)more=fit;
        if(more>frames)frames=more;
    }
    return frames;
}
inline uint32_t Score(const Plan& p) {
    const uint32_t encoding=p.encoding==encodingPcm && p.subframe>=2?0:1;      // 8-bit and float only when nothing else exists
    const uint32_t channels=p.channels==2?0:p.channels==1?1:p.channels<=9?p.channels:9;
    uint32_t rate=p.rate?RateRank(p.rate):0;if(rate>99)rate=99;
    const uint32_t width=p.subframe==2?0:p.subframe==3?1:p.subframe==4?2:3;
    // Adaptive and synchronous endpoints take the bus clock; asynchronous ones need feedback.
    const uint32_t sync=((p.attributes>>2)&3)!=1?0:p.feedbackEndpoint?1:2;
    return encoding*1000000+channels*100000+rate*100+width*10+sync;
}
// How likely an output terminal is at the user's ears (lower is better). Terminal Types 2.2-2.6.
inline uint32_t OutputRank(uint16_t type) {
    switch(type) {
    case 0x0302: case 0x0303: case 0x0401: case 0x0402:return 0;   // headphones, head-mounted, handset, headset
    case 0x0301: case 0x0304: case 0x0305: case 0x0306: case 0x0403: case 0x0404: case 0x0405:return 1;   // speakers, speakerphones
    case 0x0300: case 0x0400:return 2;                             // undefined output
    case 0x0601: case 0x0603: case 0x0604:return 3;                // analog and line connectors
    default:return (type>>8)==0x07?3:4;                            // embedded functions; S/PDIF and other digital links last
    }
}
inline const Clock* FindClock(const Plan& p,uint8_t id) {
    for(size_t i=0;i<p.clockCount;++i)if(p.clocks[i].id==id)return &p.clocks[i];
    return nullptr;
}
// The clock source behind selectors and multipliers. pin(selector) gives the
// selector's input pin in use (from 1; anything else: the first). *through:
// the last selector passed.
template<typename Pin> inline const Clock* ResolveClock(const Plan& p,Pin&& pin,const Clock** through=nullptr) {
    uint8_t id=p.terminalClock;
    for(int hop=0;hop<6 && id;++hop) {
        const Clock* c=FindClock(p,id);
        if(!c)return nullptr;
        if(c->kind==clockSource)return c;
        if(!c->sourceCount)return nullptr;
        size_t at=0;
        if(c->kind==clockSelector) {
            if(through)*through=c;
            const uint8_t chosen=pin(*c);
            if(chosen>=1 && chosen<=c->sourceCount)at=size_t(chosen-1);
        }
        id=c->sources[at];
    }
    return nullptr;
}
namespace detail {
struct Node {uint8_t id=0,kind=0,sourceCount=0,sources[8]={};uint32_t mute=0,volume=0;uint16_t type=0;};
enum : uint8_t {nodeInput=1,nodeOutput,nodeFeature,nodeSelector,nodeOther};
struct Graph {Node nodes[48];size_t count=0;
    const Node* Find(uint8_t id) const {for(size_t i=0;i<count;++i)if(nodes[i].id==id)return &nodes[i];return nullptr;}
};
inline void Sources(Node& n,const uint8_t* d,size_t length,size_t countAt) {
    if(length<=countAt)return;
    const size_t count=d[countAt];
    for(size_t i=0;i<count && i<8 && countAt+1+i<length;++i)n.sources[n.sourceCount++]=d[countAt+1+i];
}
struct Walk {const Graph* g;uint8_t terminal;Plan* plan;uint8_t seen[256];};   // seen: 1 = on the way or no, 2 = yes
// True when `id` is fed (directly or through units) by the terminal; feature and
// selector units on the way are collected. Every entity is looked at once.
inline bool Reach(Walk& w,uint8_t id,unsigned depth) {
    if(depth>16)return false;
    if(w.seen[id])return w.seen[id]==2;
    w.seen[id]=1;
    const Node* n=w.g->Find(id);
    if(!n || n->kind==nodeOutput)return false;
    if(n->kind==nodeInput) {if(n->id==w.terminal)w.seen[id]=2;return n->id==w.terminal;}
    bool any=false;uint8_t pin=0;
    for(size_t i=0;i<n->sourceCount;++i)if(Reach(w,n->sources[i],depth+1)) {if(!any)pin=uint8_t(i+1);any=true;}
    if(!any)return false;
    w.seen[id]=2;
    Plan* plan=w.plan;
    if(n->kind==nodeFeature && plan->unitCount<8) {Unit& u=plan->units[plan->unitCount++];u.id=n->id;u.mute=n->mute;u.volume=n->volume;}
    if(n->kind==nodeSelector && plan->selectorCount<4) {Selector& s=plan->selectors[plan->selectorCount++];s.id=n->id;s.pin=pin;}
    return true;
}
inline uint32_t Le16(const uint8_t* p) {return uint32_t(p[0])|uint32_t(p[1])<<8;}
inline uint32_t Le24(const uint8_t* p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16;}
inline uint32_t Le32(const uint8_t* p) {return Le24(p)|uint32_t(p[3])<<24;}
}  // namespace detail
// Chooses the AudioStreaming alternate setting to play on and the controls to
// set. `config` is the whole configuration descriptor, `speed` the device's
// speed; `only` >= 0: only that AudioStreaming interface is looked at (one USB
// I/O handle), otherwise every one. False when there is nothing this driver
// can play on; *plan is then unchanged.
inline bool Parse(const uint8_t* config,size_t length,uint8_t speed,int only,Plan* plan) {
    using namespace detail;
    if(!config || !plan || length<9 || config[1]!=2)return false;
    const bool fast=speed>=speedHigh;
    size_t controlStart=0,controlEnd=0,bestStart=0,bestEnd=0;
    uint8_t version=0,controlInterface=0,profile=0,bestProfile=0;
    Plan best;bool found=false;uint32_t bestScore=0;
    for(size_t i=0;i+2<=length;) {
        const uint8_t* d=config+i;const size_t n=d[0];
        if(n<2 || i+n>length)break;
        size_t next=i+n;
        if(d[1]==0x0b && n>=8)profile=d[4]==1 && d[6]==0x30?d[5]:0;   // interface association: a UAC 3.0 function's profile
        else if(d[1]==4 && n>=9) {
            if(controlStart && !controlEnd)controlEnd=i;
            if(d[5]==1 && d[6]==1) {
                // An AudioControl interface starts an audio function; its class descriptors follow.
                version=d[7]==0x30?3:d[7]==0x20?2:d[7]==0?1:0;
                controlInterface=d[2];controlStart=version?i+n:0;controlEnd=0;
            } else if(d[5]==1 && d[6]==2 && d[3]!=0 && controlStart && (only<0 || d[2]==only) && d[7]==(version==3?0x30:version==2?0x20:0)) {
                Plan p;p.version=version;p.speed=speed;p.controlInterface=controlInterface;p.interface=d[2];p.alternate=d[3];
                p.badd=version==3 && profile>=0x20;
                bool general=false,format=false,endpoint=false,last=false;   // last: the endpoint read last is the data endpoint
                uint8_t synch=0;uint64_t formats=0;
                // A feedback endpoint candidate (isochronous IN in the same alternate setting).
                uint8_t fbAddress=0,fbInterval=0,fbSynch=0,fbAttributes=0;uint16_t fbPacket=0;
                size_t j=i+n;
                for(;j+2<=length;) {
                    const uint8_t* e=config+j;const size_t m=e[0];
                    if(m<2 || j+m>length || e[1]==4 || e[1]==0x0b)break;
                    if(e[1]==0x24 && m>=3 && e[2]==1) {           // AS_GENERAL
                        if(version==1 && m>=7) {
                            p.terminal=e[3];const uint32_t tag=Le16(e+5);
                            general=tag>=1 && tag<=3;p.encoding=tag==2?encodingPcm8:tag==3?encodingFloat:encodingPcm;
                        }
                        if(version==2 && m>=16) {p.terminal=e[3];p.channels=e[10];formats=Le32(e+6);general=e[5]==1;}
                        if(version==3 && m>=20) {p.terminal=e[3];p.cluster=uint16_t(Le16(e+8));formats=Le32(e+10);p.subframe=e[18];p.bits=e[19];general=format=true;}
                        if(version>=2 && general) {
                            // bmFormats: bit 0 PCM, bit 1 PCM8, bit 2 IEEE float.
                            if(formats&1)p.encoding=encodingPcm;else if(formats&2)p.encoding=encodingPcm8;else if(formats&4)p.encoding=encodingFloat;else general=false;
                        }
                    } else if(e[1]==0x24 && m>=3 && e[2]==2 && version<3) {    // FORMAT_TYPE (type I only)
                        if(version==1 && m>=8 && e[3]==1) {
                            p.channels=e[4];p.subframe=e[5];p.bits=e[6];
                            const size_t rates=e[7];uint32_t rate=0;
                            if(!rates && m>=14)rate=PreferRate(Le24(e+8),Le24(e+11));
                            for(size_t k=0;k<rates && 8+3*k+3<=m;++k) {
                                const uint32_t r=Le24(e+8+3*k);
                                if(RateUsable(r) && (!rate || RateRank(r)<RateRank(rate)))rate=r;
                            }
                            p.rate=rate;p.rateCount=uint8_t(rates>255?255:rates);format=RateUsable(rate);
                        }
                        if(version==2 && m>=6 && e[3]==1) {p.subframe=e[4];p.bits=e[5];format=true;}
                    } else if(e[1]==5 && m>=7) {
                        last=false;
                        if((e[3]&3)==1 && !(e[2]&0x80) && !endpoint && (version==1 || !(e[3]&0x30))) {
                            p.endpoint=e[2];p.attributes=e[3];p.maxPacket=uint16_t(Le16(e+4));p.interval=e[6];
                            p.burst=speed==speedHigh?uint8_t((p.maxPacket>>11)&3):uint8_t(0);synch=m>=9?e[8]:0;endpoint=last=true;
                        } else if((e[3]&3)==1 && (e[2]&0x80) && !fbAddress) {
                            fbAddress=e[2];fbAttributes=e[3];fbPacket=uint16_t(Le16(e+4)&0x7ff);fbInterval=e[6];fbSynch=m>=9?e[8]:0;
                        }
                    } else if(e[1]==0x30 && m>=6 && last && speed>=speedSuper) {   // SuperSpeed endpoint companion
                        p.burst=e[2];p.mult=e[3]&3;p.bytesPerInterval=Le16(e+4);
                    } else if(e[1]==0x25 && m>=4 && e[2]==1 && version==1 && last)p.endpointRate=(e[3]&1)!=0;
                    j+=m;
                }
                next=j>i?j:i+n;
                if(p.badd && endpoint) {
                    // Basic Audio Device Definition 3.0: 48 kHz, the format told by wMaxPacketSize
                    // (synchronous / asynchronous: mono 16, mono 24, stereo 16, stereo 24).
                    const uint32_t size=p.maxPacket&0x7ff;
                    p.channels=size==0x60 || size==0x62 || size==0x90 || size==0x93?1:size==0xc0 || size==0xc4 || size==0x120 || size==0x126?2:0;
                    p.subframe=size==0x60 || size==0x62 || size==0xc0 || size==0xc4?2:3;p.bits=uint8_t(p.subframe*8);
                    p.rate=48000;p.rateCount=1;p.terminal=1;p.encoding=encodingPcm;general=format=p.channels!=0;
                }
                // Asynchronous (or undeclared) endpoints: the feedback endpoint. UAC 1.0 names it in
                // bSynchAddress; UAC 2.0/3.0 mark it by its usage type.
                const uint32_t type=(p.attributes>>2)&3;
                if(endpoint && fbAddress && (type==1 || type==0)) {
                    const bool named=version==1?(!fbSynch && (!synch || synch==fbAddress || (synch|0x80)==fbAddress)):(fbAttributes&0x30)==0x10;
                    if(named && fbPacket>=3) {
                        p.feedbackEndpoint=fbAddress;p.feedbackInterval=fbInterval;p.feedbackPacket=fbPacket>8?8:fbPacket;
                        // Full speed feedback endpoints answer once per frame at the most (bInterval 1; bRefresh only thins the answers).
                        p.feedbackMicroframes=fast?(fbInterval>=1 && fbInterval<=10?1u<<(fbInterval-1):8u):8u;
                    }
                }
                p.implicit=endpoint && type==1 && !p.feedbackEndpoint;
                p.microframes=Microframes(p.interval,fast);
                const uint8_t top=p.encoding==encodingFloat?4:p.encoding==encodingPcm8?1:4;
                const uint8_t bottom=p.encoding==encodingFloat?4:1;
                const bool shape=general && format && endpoint && p.microframes && (p.endpoint&0x0f) && (version==3 && !p.badd?true:p.channels>=1 && p.channels<=32)
                    && p.subframe>=bottom && p.subframe<=top && p.bits>=1 && p.bits<=p.subframe*8 && (p.maxPacket&0x7ff);
                // UAC 2/3: the rate comes from the clock later; the packet check is repeated then.
                if(shape && ((version>=2 && !p.badd) || Fits(p))) {
                    const uint32_t score=Score(p);
                    if(!found || score<bestScore) {best=p;bestScore=score;found=true;bestStart=controlStart;bestEnd=controlEnd?controlEnd:i;bestProfile=profile;}
                }
            }
        }
        i=next;
    }
    if(!found)return false;
    version=best.version;
    if(best.badd) {
        // Inferred entities (BADD 3.0, 5 and 6): input terminal 1 -> feature unit 2 -> output terminal 3, clock source 9.
        best.units[0].id=2;best.units[0].mute=1;best.units[0].volume=7;best.unitCount=1;
        best.clock=9;best.clockSettable=false;best.outputs=1;
        best.outputType=bestProfile==0x21?0x0302:bestProfile==0x22?0x0301:bestProfile==0x24 || bestProfile==0x25?0x0402:bestProfile==0x26?0x0403:0x0300;
        *plan=best;return true;
    }
    // Units and terminals of the AudioControl interface. Subtype numbers differ by version.
    const uint8_t subMixer=version==3?5:4,subSelector=version==3?6:5,subFeature=version==3?7:6;
    Graph g;
    for(size_t i=bestStart;i+2<=bestEnd && i+2<=length;) {
        const uint8_t* d=config+i;const size_t n=d[0];
        if(n<2 || i+n>bestEnd)break;
        if(d[1]==0x24 && n>=4 && g.count<48) {
            Node node;node.id=d[3];
            const uint8_t sub=d[2];
            if(sub==2) {node.kind=nodeInput;if(version>=2 && n>=8 && node.id==best.terminal)best.terminalClock=d[7];}
            else if(sub==3 && n>=8) {
                node.kind=nodeOutput;node.type=uint16_t(Le16(d+4));
                // An output terminal of type "USB streaming" (0x01xx) is a recording path.
                if(d[5]!=0x01) {node.sources[0]=d[7];node.sourceCount=1;}
            } else if(sub==subMixer) {node.kind=nodeOther;Sources(node,d,n,4);}
            else if(sub==subSelector) {node.kind=nodeSelector;Sources(node,d,n,4);}
            else if(sub==subFeature && n>=6) {
                node.kind=nodeFeature;node.sources[0]=d[4];node.sourceCount=1;
                if(version==1) {
                    const size_t size=d[5];
                    if(size)for(size_t c=0;6+c*size+size<=n-1 && c<32;++c) {
                        if(d[6+c*size]&1)node.mute|=1u<<c;
                        if(d[6+c*size]&2)node.volume|=1u<<c;
                    }
                } else {
                    const size_t tail=version==3?2:1;   // iFeature / wFeatureDescrStr
                    for(size_t c=0;5+c*4+4+tail<=n && c<32;++c) {
                        if(d[5+c*4]&2)node.mute|=1u<<c;        // host programmable
                        if(d[5+c*4]&8)node.volume|=1u<<c;
                    }
                }
            } else if(version==1 && (sub==7 || sub==8)) {node.kind=nodeOther;Sources(node,d,n,6);}                     // processing, extension
            else if(version==2 && sub==7 && n>=7) {node.kind=nodeOther;node.sources[0]=d[6];node.sourceCount=1;}        // effect
            else if(version==2 && (sub==8 || sub==9)) {node.kind=nodeOther;Sources(node,d,n,6);}                     // processing, extension
            else if(version==2 && sub==0x0d && n>=5) {node.kind=nodeOther;node.sources[0]=d[4];node.sourceCount=1;}     // sample rate converter
            else if(version==3 && sub==8 && n>=7) {node.kind=nodeOther;node.sources[0]=d[6];node.sourceCount=1;}        // effect
            else if(version==3 && (sub==9 || sub==0x0a)) {node.kind=nodeOther;Sources(node,d,n,6);}                  // processing, extension
            else if(version==3 && sub==0x0e && n>=5) {node.kind=nodeOther;node.sources[0]=d[4];node.sourceCount=1;}     // sample rate converter
            else if(version>=2 && best.clockCount<8) {
                // Clock entities: source, selector, multiplier (UAC 2.0: 0x0a..0x0c, UAC 3.0: 0x0b..0x0d).
                const uint8_t first=version==3?0x0b:0x0a;Clock c;c.id=d[3];
                if(sub==first && n>=6) {c.kind=clockSource;c.settable=(d[5]&3)==3;c.validity=(d[5]&0x0c)!=0;}
                else if(sub==first+1 && n>=6) {
                    c.kind=clockSelector;const size_t pins=d[4];
                    for(size_t k=0;k<pins && k<8 && 5+k<n;++k)c.sources[c.sourceCount++]=d[5+k];
                    c.selectable=5+pins<n && (d[5+pins]&3)==3;
                } else if(sub==first+2 && n>=5) {c.kind=clockMultiplier;c.sources[0]=d[4];c.sourceCount=1;}
                if(c.kind)best.clocks[best.clockCount++]=c;
            }
            if(node.kind)g.nodes[g.count++]=node;
        }
        i+=n;
    }
    // Every output terminal the stream reaches: its units are set, the best one is told.
    Walk walk{&g,best.terminal,&best,{}};
    for(size_t i=0;i<g.count;++i) {
        const Node& out=g.nodes[i];
        if(out.kind!=nodeOutput || !out.sourceCount || !Reach(walk,out.sources[0],0))continue;
        if(!best.outputs || OutputRank(out.type)<OutputRank(best.outputType))best.outputType=out.type;
        if(best.outputs<255)++best.outputs;
    }
    if(version>=2) {
        // No clock source to be found: clock 0 (the device side refuses it, but for the devices
        // the kernel lists as having a hard-wired clock).
        const Clock* clock=ResolveClock(best,[](const Clock&) {return uint8_t(1);});
        best.clock=clock?clock->id:uint8_t(0);best.clockSettable=clock && clock->settable;
    }
    *plan=best;return true;
}
// The form of r405: every AudioStreaming interface, full or high speed.
inline bool Parse(const uint8_t* config,size_t length,bool highSpeed,Plan* plan) {return Parse(config,length,highSpeed?speedHigh:speedFull,-1,plan);}
// Volume to set, in 1/256 dB: the maximum, but not above 0 dB when the range crosses it.
inline int16_t PickVolume(int16_t minimum,int16_t maximum) {return maximum>0 && minimum<=0?int16_t(0):maximum;}

// ---- the feedback value of an asynchronous endpoint ----------------------------
// The device tells how many frames it wants per (micro)frame: 10.14 fixed point
// in 3 bytes at full speed, 16.16 in 4 bytes at high speed - and devices that
// send the other one, or 16.16 per frame. The format is therefore recognised by
// size: the value is shifted until it lies near the nominal rate, and that shift
// is kept. Values in frames per TD, 16.16.
struct Feedback {
    static constexpr int unknown=-100;
    uint32_t nominal=0,maximum=0,current=0;int shift=unknown;uint64_t accepted=0,rejected=0;
    void Init(uint32_t rate,uint32_t microframes,uint32_t maxFrames) {
        nominal=uint32_t(uint64_t(rate)*microframes*65536/8000);maximum=maxFrames<<16;
        const uint32_t half=nominal+nominal/2;if(maximum>half)maximum=half;
        current=0;shift=unknown;accepted=rejected=0;
    }
    // A packet of `bytes` bytes whose first four are `raw`. True when it gave a rate.
    bool Take(uint32_t raw,uint32_t bytes) {
        if(bytes<3 || !nominal)return false;
        uint64_t f=bytes==3?raw&0x00ffffffu:raw&0x0fffffffu;
        if(!f)return false;
        if(shift==unknown) {
            int s=0;
            while(f<nominal-nominal/4 && s<24) {f<<=1;++s;}
            while(f>uint64_t(nominal)+nominal/2 && s>-24) {f>>=1;--s;}
            shift=s;
        } else if(shift>=0)f<<=shift;else f>>=-shift;
        if(f>=nominal-nominal/8 && f<=maximum) {current=uint32_t(f);++accepted;return true;}
        shift=unknown;++rejected;return false;   // not this format: recognised again from the next value
    }
};

// ---- xHCI data structures ---------------------------------------------------
struct Trb {uint64_t parameter;uint32_t status,control;};
static_assert(sizeof(Trb)==16,"xHCI TRB");
constexpr uint32_t trbIsoch=5,trbLink=6,trbConfigureEndpoint=12,trbTransferEvent=32,trbCommandCompletion=33;
inline uint32_t TrbType(uint32_t control) {return (control>>10)&0x3f;}
// Ordered stores: the control word (with the cycle bit) is what hands a TRB to the controller.
inline void Store(Trb* trb,uint64_t parameter,uint32_t status,uint32_t control) {
    volatile Trb* v=trb;v->parameter=parameter;v->status=status;v->control=control;
}
// IEEE 754 single of sample/32768, without floating-point code.
inline uint32_t FloatBits(int16_t sample) {
    if(!sample)return 0;
    const uint32_t sign=sample<0?0x80000000u:0u;uint32_t magnitude=sample<0?uint32_t(-int32_t(sample)):uint32_t(sample);
    uint32_t top=15;while(!(magnitude>>top))--top;
    return sign|((top+112)<<23)|((magnitude<<(23-top))&0x7fffffu);   // exponent top-15, biased by 127
}

// ---- the isochronous transfer ring -------------------------------------------
// TD number n (counted from the start of the stream) lives in ring slot
// n % slots. `done` TDs were played (told by the completion events), `queued`
// were handed to the controller; the ones in between are silence unless Write
// put speech into them. A TD's length is the nominal one (exact pacing by its
// number) until a feedback rate is set: then the TDs a few milliseconds ahead of
// the controller get their length from that rate, just before speech is put
// into them (lengths[] holds what each slot carries).
class IsoRing {
public:
    struct Layout {
        Trb* trbs=nullptr;uint64_t trbsDevice=0;        // slots+1 TRBs: the last one is the Link TRB
        uint8_t* data=nullptr;uint64_t dataDevice=0;    // slots*stride bytes
        uint16_t* lengths=nullptr;                      // slots entries: frames in each slot's TD
        uint32_t slots=0,stride=0;
        uint32_t maxFrames=0;                           // the most frames a TD may carry (0: the nominal maximum); see TdFrames
        Pacing pacing;uint8_t channels=2,subframe=2,encoding=encodingPcm;
        uint32_t packet=1023,burst=0;                   // endpoint: max packet size, transactions per burst - 1
        uint32_t interrupter=0,eventEvery=1;            // every eventEvery-th TD reports its completion
        uint32_t margin=5,horizon=64;                   // TDs: nearest writable TD, silence kept queued
    };
    bool Init(const Layout& layout) {
        const Layout& l=layout;
        if(!l.trbs || !l.data || !l.lengths || l.slots<64 || !l.eventEvery || !l.margin || l.horizon<l.margin+8 || l.horizon>l.slots-16
            || !l.channels || l.channels>32 || l.subframe<1 || l.subframe>4 || l.encoding>encodingFloat || (l.encoding==encodingFloat && l.subframe!=4)
            || !RateUsable(l.pacing.rate) || !l.pacing.microframes || !l.packet || l.packet>1024 || l.burst>15
            || uint64_t(l.pacing.MaxFrames())*l.channels*l.subframe>l.stride
            || (l.maxFrames && (l.maxFrames<l.pacing.MaxFrames() || l.maxFrames>0xffff || uint64_t(l.maxFrames)*l.channels*l.subframe>l.stride)))return false;
        l_=l;done_=queued_=writeTd_=paced_=0;writeFrame_=0;phase_=0;primed_=false;last_[0]=last_[1]=0;feedback_=0;accumulator_=0;
        maxFrames_=l.maxFrames?l.maxFrames:l.pacing.MaxFrames();   // never more than the endpoint's packet holds
        // Rates below 44.1 kHz: two one-pole low-pass stages at 0.42 of the device rate before the resampler.
        lowpass_=0;memset(filter_,0,sizeof(filter_));
        if(l.pacing.rate<44100) {const uint64_t x=2639ull*l.pacing.rate;lowpass_=uint32_t(x*32768/(48000000ull+x));}
        for(uint32_t i=0;i<=l.slots;++i)Store(&l.trbs[i],0,0,0);
        memset(l.lengths,0,size_t(l.slots)*sizeof(uint16_t));
        return true;
    }
    uint64_t Done() const {return done_;}
    uint64_t Queued() const {return queued_;}
    uint32_t Slots() const {return l_.slots;}
    uint32_t MaxFrames() const {return maxFrames_;}
    // The controller reported the TD in this ring slot as finished.
    void Completed(uint32_t slot) {
        if(slot>=l_.slots)return;
        const uint64_t td=done_+(slot+l_.slots-uint32_t(done_%l_.slots))%l_.slots;
        if(td<queued_)done_=td+1;
    }
    // No completion arrives although TDs are queued (events were lost): count them as played.
    void Rebase() {done_=queued_;}
    // Keeps silence queued up to the horizon. True when TDs were handed over (ring the doorbell).
    bool Feed() {bool fed=false;FeedTo(done_+l_.horizon,&fed);return fed;}
    // Frames per TD the device asked for (16.16); 0: the nominal rate.
    void SetFeedback(uint32_t perTd) {if(!perTd || !feedback_)accumulator_=0;feedback_=perTd;}
    uint32_t FeedbackRate() const {return feedback_;}
    // With a feedback rate: the TDs up to `ahead` beyond the margin get their lengths (silence
    // follows the device's clock too, so its buffer stays where it wants it).
    void Pace(uint64_t ahead) {PaceTo(done_+l_.margin+ahead);}
    // Copies 48 kHz stereo frames into the queued TDs, at most `lead` TDs ahead of
    // the controller. Returns the frames taken. *fed: TDs were handed over.
    size_t Write(const int16_t* stereo,size_t frames,uint64_t lead,bool* fed) {
        if(!stereo || !l_.slots)return 0;
        const uint64_t first=done_+l_.margin;
        if(writeTd_<first) {writeTd_=first;writeFrame_=0;}   // late: go on after a short silence
        const uint64_t limit=done_+lead;
        size_t used=0;
        while(used<frames) {
            // One input frame makes output frames in at most three TDs: room for them.
            if(writeTd_+3>limit || !FeedTo(writeTd_+3,fed))break;
            PaceTo(writeTd_+3);
            int16_t left=stereo[used*2],right=stereo[used*2+1];
            if(lowpass_) {left=Lowpass(0,left);right=Lowpass(1,right);}
            if(l_.pacing.rate==sourceRate)Place(left,right);
            else {
                if(!primed_) {last_[0]=left;last_[1]=right;primed_=true;}
                while(phase_<l_.pacing.rate) {
                    Place(Mix(last_[0],left),Mix(last_[1],right));
                    phase_+=sourceRate;
                }
                phase_-=l_.pacing.rate;last_[0]=left;last_[1]=right;
            }
            ++used;
        }
        return used;
    }
    // Everything written was played.
    bool Drained() const {return done_>writeTd_ || (done_==writeTd_ && !writeFrame_);}
    // Silences what was written but not played yet (a newer utterance replaces it).
    void Discard() {
        const uint64_t keep=done_+l_.margin;
        if(writeTd_>keep || (writeTd_==keep && writeFrame_)) {
            for(uint64_t td=keep;td<=writeTd_ && td<queued_;++td)Silence(td);
            writeTd_=keep;writeFrame_=0;
        }
        phase_=0;primed_=false;memset(filter_,0,sizeof(filter_));
    }
    // Last resort when the endpoint cannot be stopped by a command: takes back
    // everything but the next two TDs, so the controller stops within a few
    // intervals (it then reports one Ring Underrun). Memory writes only.
    void Truncate() {
        const uint64_t keep=done_+2;
        for(uint64_t td=keep;td<queued_;++td) {volatile Trb* v=&l_.trbs[td%l_.slots];v->control=v->control^1u;}
        if(queued_>keep)queued_=keep;
        if(writeTd_>queued_) {writeTd_=queued_;writeFrame_=0;}
        if(paced_>queued_)paced_=queued_;
    }
private:
    uint32_t FrameBytes() const {return uint32_t(l_.channels)*l_.subframe;}
    uint32_t Frames(uint64_t td) const {return l_.lengths[td%l_.slots];}
    uint8_t* Buffer(uint64_t td) const {return l_.data+size_t(td%l_.slots)*l_.stride;}
    // Silence of the encoding: zero, but 0x80 for unsigned 8-bit samples.
    void Silence(uint64_t td) {memset(Buffer(td),l_.encoding==encodingPcm8?0x80:0,l_.stride);}
    int16_t Mix(int16_t before,int16_t now) const {return int16_t(before+int32_t(int64_t(int32_t(now)-before)*phase_/l_.pacing.rate));}
    int16_t Lowpass(unsigned channel,int16_t in) {
        int32_t* s=filter_[channel];
        s[0]+=int32_t((int64_t(int32_t(in)*256-s[0])*lowpass_)>>15);
        s[1]+=int32_t((int64_t(s[0]-s[1])*lowpass_)>>15);
        return int16_t(s[1]/256);
    }
    // The Isoch TRB's words for a TD of `bytes`: one TD, start as soon as possible (SIA), the
    // packets of its last burst told (TBC, TLBPC), event on completion without an interrupt (IOC+BEI).
    uint32_t Status(uint32_t bytes) const {return bytes|(l_.interrupter<<22);}
    uint32_t Control(uint32_t bytes,uint32_t cycle,bool event) const {
        uint32_t packets=(bytes+l_.packet-1)/l_.packet;if(!packets)packets=1;
        const uint32_t each=l_.burst+1,bursts=(packets+each-1)/each,rest=packets%each;
        return (trbIsoch<<10)|(1u<<31)|(event?(1u<<5)|(1u<<9):0u)|(((bursts-1)&3)<<7)|(((rest?rest-1:each-1)&0xf)<<16)|cycle;
    }
    bool FeedTo(uint64_t target,bool* fed) {
        while(queued_<target) {
            if(queued_-done_>=l_.slots-2)return false;   // ring full
            Enqueue(queued_);++queued_;
            if(fed)*fed=true;
        }
        return true;
    }
    void Enqueue(uint64_t td) {
        const uint32_t slot=uint32_t(td%l_.slots);
        const uint32_t cycle=((td/l_.slots)&1)?0:1;   // first lap 1 (the endpoint context's DCS), then toggled by the Link TRB
        const uint32_t frames=l_.pacing.Frames(td),bytes=frames*FrameBytes();
        l_.lengths[slot]=uint16_t(frames);
        Silence(td);
        // The Link TRB behind the last slot is made valid for this lap before the TD in front of it.
        if(slot==l_.slots-1)Store(&l_.trbs[l_.slots],l_.trbsDevice,0,(trbLink<<10)|2u|cycle);
        const bool event=td%l_.eventEvery==l_.eventEvery-1;
        Store(&l_.trbs[slot],l_.dataDevice+uint64_t(slot)*l_.stride,Status(bytes),Control(bytes,cycle,event));
    }
    // Lengths from the feedback rate for the TDs before `target` that hold no speech yet and
    // are at least `margin` TDs away from the controller. Only the length words are rewritten.
    void PaceTo(uint64_t target) {
        if(!feedback_)return;
        uint64_t lower=done_+l_.margin;
        const uint64_t written=writeTd_+(writeFrame_?1:0);
        if(written>lower)lower=written;
        if(paced_<lower)paced_=lower;
        for(;paced_<target && paced_<queued_;++paced_) {
            accumulator_+=feedback_;
            uint32_t frames=accumulator_>>16;accumulator_&=0xffffu;
            if(frames>maxFrames_)frames=maxFrames_;
            const uint32_t slot=uint32_t(paced_%l_.slots);
            if(l_.lengths[slot]==frames)continue;
            const uint32_t bytes=frames*FrameBytes();
            l_.lengths[slot]=uint16_t(frames);
            volatile Trb* v=&l_.trbs[slot];
            const uint32_t control=v->control,now=Control(bytes,control&1,(control&(1u<<5))!=0);
            v->status=Status(bytes);
            if(now!=control)v->control=now;
        }
    }
    void Place(int16_t left,int16_t right) {
        while(writeFrame_>=Frames(writeTd_) && writeTd_+1<queued_) {++writeTd_;writeFrame_=0;}
        if(writeFrame_>=Frames(writeTd_))return;   // no room (cannot happen while Write keeps three TDs ahead)
        uint8_t* out=Buffer(writeTd_)+size_t(writeFrame_)*FrameBytes();
        const int16_t mono=int16_t((int32_t(left)+right)/2);
        for(uint8_t c=0;c<l_.channels;++c) {
            const int16_t v=l_.channels==1?mono:c==0?left:c==1?right:int16_t(0);
            if(l_.encoding==encodingFloat) {const uint32_t bits=FloatBits(v);*out++=uint8_t(bits);*out++=uint8_t(bits>>8);*out++=uint8_t(bits>>16);*out++=uint8_t(bits>>24);continue;}
            if(l_.subframe==1) {*out++=uint8_t((uint16_t(v)>>8)^(l_.encoding==encodingPcm8?0x80u:0u));continue;}
            // little endian, the 16 bits in the most significant bytes of the subframe
            for(uint8_t b=0;b+2<l_.subframe;++b)*out++=0;
            *out++=uint8_t(v&0xff);*out++=uint8_t((uint16_t(v)>>8)&0xff);
        }
        if(++writeFrame_>=Frames(writeTd_)) {++writeTd_;writeFrame_=0;}
    }
    Layout l_;
    uint64_t done_=0,queued_=0,writeTd_=0,paced_=0;uint32_t writeFrame_=0,maxFrames_=0;
    uint32_t feedback_=0,accumulator_=0;
    uint32_t phase_=0;int16_t last_[2]={0,0};bool primed_=false;   // linear resampler
    uint32_t lowpass_=0;int32_t filter_[2][2]={{0,0},{0,0}};
};
// ---- the feedback ring: isochronous IN TDs of a few bytes each -----------------
class FeedbackRing {
public:
    struct Layout {
        Trb* trbs=nullptr;uint64_t trbsDevice=0;        // slots+1 TRBs
        uint8_t* data=nullptr;uint64_t dataDevice=0;    // slots*8 bytes
        uint32_t slots=0,packet=4,interrupter=0,eventEvery=1,horizon=64;
    };
    bool Init(const Layout& l) {
        if(!l.trbs || !l.data || l.slots<64 || !l.eventEvery || l.packet<3 || l.packet>8 || l.horizon<8 || l.horizon>l.slots-16)return false;
        l_=l;done_=queued_=0;
        for(uint32_t i=0;i<=l.slots;++i)Store(&l.trbs[i],0,0,0);
        return true;
    }
    void Clear() {l_=Layout{};done_=queued_=0;}
    bool Active() const {return l_.slots!=0;}
    uint64_t Done() const {return done_;}
    uint64_t Queued() const {return queued_;}
    uint32_t Slots() const {return l_.slots;}
    bool Feed() {
        bool fed=false;
        while(l_.slots && queued_<done_+l_.horizon && queued_-done_<l_.slots-2) {Enqueue(queued_);++queued_;fed=true;}
        return fed;
    }
    // The TD in `slot` finished with `residual` bytes not transferred. True (and the
    // packet's first four bytes and its length) when `good` and it carried data.
    bool Completed(uint32_t slot,uint32_t residual,bool good,uint32_t* raw,uint32_t* bytes) {
        if(slot>=l_.slots)return false;
        const uint64_t td=done_+(slot+l_.slots-uint32_t(done_%l_.slots))%l_.slots;
        if(td>=queued_)return false;
        done_=td+1;
        if(!good || residual>l_.packet)return false;
        const volatile uint8_t* p=l_.data+size_t(slot)*8;
        *bytes=l_.packet-residual;*raw=uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;
        return true;
    }
    void Rebase() {done_=queued_;}
    void Truncate() {
        const uint64_t keep=done_+2;
        for(uint64_t td=keep;td<queued_;++td) {volatile Trb* v=&l_.trbs[td%l_.slots];v->control=v->control^1u;}
        if(queued_>keep)queued_=keep;
    }
private:
    void Enqueue(uint64_t td) {
        const uint32_t slot=uint32_t(td%l_.slots);
        const uint32_t cycle=((td/l_.slots)&1)?0:1;
        memset(l_.data+size_t(slot)*8,0,8);
        if(slot==l_.slots-1)Store(&l_.trbs[l_.slots],l_.trbsDevice,0,(trbLink<<10)|2u|cycle);
        const bool event=td%l_.eventEvery==l_.eventEvery-1;
        Store(&l_.trbs[slot],l_.dataDevice+uint64_t(slot)*8,l_.packet|(l_.interrupter<<22),(trbIsoch<<10)|(1u<<31)|(event?(1u<<5)|(1u<<9):0u)|cycle);
    }
    Layout l_;uint64_t done_=0,queued_=0;
};
// Ring size for a service interval: about four seconds of TDs, at most 2 MiB of buffers.
inline uint32_t RingSlots(uint32_t microframes,uint32_t stride) {
    uint32_t slots=microframes>=8?4096u:4096u*(8u/microframes);
    while(slots>256 && uint64_t(slots)*stride>(2u<<20))slots/=2;
    return slots;
}
inline uint32_t StrideFor(uint32_t bytes) {
    uint32_t stride=64;
    while(stride<bytes)stride*=2;   // a power of two: no buffer crosses a page (or 64 KiB) boundary
    return stride;
}
inline uint32_t Stride(const Pacing& pacing,uint8_t channels,uint8_t subframe) {return StrideFor(pacing.MaxFrames()*channels*subframe);}
// TDs in `milliseconds`.
inline uint32_t Tds(uint32_t milliseconds,uint32_t microframes) {const uint32_t n=milliseconds*8/microframes;return n?n:1;}

// ---- the firmware's command ring, seen from its event ring memory ------------
// Index of the event the controller writes next, and the cycle bit it writes
// with, from the cycle bits of all `count` events of the ring (in ring order).
template<typename At> inline void EventEnqueue(At&& at,uint32_t count,uint32_t* index,uint32_t* cycle) {
    const uint32_t first=at(0).control&1;
    for(uint32_t i=1;i<count;++i)if((at(i).control&1)!=first) {*index=i;*cycle=first;return;}
    *index=0;*cycle=first^1;   // every entry is of one lap: the next lap starts at 0
}
}  // namespace charonusbaudio
