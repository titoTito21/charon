// Host tests of the pure logic in src/efi/CharonEs1371.h (r406): sample rate
// converter words, codec and SRC RAM command words, card list, DMA position.
//
// Build (Windows host, LLVM in work/llvm-23.1.5/bin; INC holds src/efi/haiku as "efi"):
//   clang++ -std=c++17 -fuse-ld=lld -I INC tests/CharonEs1371Tests.cpp -o es1371-tests.exe
#include "../src/efi/CharonEs1371.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(false)
using namespace charones1371;
int main() {
    // The ratio is rate/3000 in 5.15 fixed point: 48000 Hz is exactly 16.0.
    CHECK(SrcFrequency(48000)==16u<<15);
    CHECK(SrcFrequency(3000)==1u<<15);
    CHECK(SrcFrequency(44100)==481690);           // 14.7 * 32768 = 481689.6, rounded to nearest
    CHECK(SrcFrequency(22050)==240845);           // 240844.8
    CHECK(SrcFrequency(8000)==87381);             // 87381.33
    CHECK(SrcFrequency(4000)==43691);             // 43690.67 rounds up
    // Integer part in bits 10-15, the chip's own low byte kept, bits 8-9 cleared.
    CHECK(SrcIntegerWord(0x0000,SrcFrequency(48000))==0x4000);
    CHECK(SrcIntegerWord(0xffff,SrcFrequency(48000))==0x40ff);
    CHECK(SrcIntegerWord(0x12ab,SrcFrequency(44100))==0x38ab);   // 14 << 10
    CHECK(SrcFractionWord(SrcFrequency(48000))==0);
    CHECK(SrcFractionWord(SrcFrequency(44100))==(481690&0x7fff));
    // The words give the ratio back: integer*32768 + fraction.
    for(uint32_t rate=4000;rate<=48000;rate+=37) {
        const uint32_t f=SrcFrequency(rate);
        CHECK(((uint32_t(SrcIntegerWord(0,f))>>10)<<15)+SrcFractionWord(f)==f);
        const int64_t error=int64_t(f)*3000-(int64_t(rate)<<15);
        CHECK(error>=-1500 && error<=1500);
    }
    // SRC RAM access: address in bits 25-31, write enable bit 24, only the four
    // channel/disable bits of the old value are kept (never busy or old data).
    CHECK(SrcRamWrite(0,0x75,0x4000)==((0x75u<<25)|(1u<<24)|0x4000));
    CHECK(SrcRamWrite(0xffffffff,0x00,0)==(srcKeep|srcWrite));
    CHECK(SrcRamRead(0xffffffff,0x7f)==(srcKeep|(0x7fu<<25)));
    CHECK(SrcRamRead(srcBusy|0x1234,0x75)==(0x75u<<25));
    CHECK(srcKeep==0x00780000 && srcBusy==0x00800000);
    CHECK(srcDac2+srcInt==0x75 && srcDac2+srcFraction==0x77 && srcVolDac2==0x7e);
    // Codec words: register in bits 16-22, bit 23 = read.
    CHECK(CodecWriteWord(0x18,0x0808)==0x00180808);
    CHECK(CodecWriteWord(0x02,0)==0x00020000);
    CHECK(CodecReadWord(0x7c)==0x00fc0000);
    CHECK(CodecReadWord(0x02)==0x00820000);
    // DAC2: stereo 16 bit, end increment 2, no interrupt enable (bit 9), no pause (bit 12), looping (bit 14 clear).
    CHECK(serialDac2Stereo16==0x0010000c);
    CHECK(!(serialDac2Stereo16&((1u<<9)|(1u<<12)|(1u<<14))));
    // Cards.
    CHECK(Supported(0x1274,0x1371) && Supported(0x1274,0x5880) && Supported(0x1102,0x8938));
    CHECK(!Supported(0x1274,0x5000) && !Supported(0x8086,0x2415) && !Supported(0x1102,0x1371));   // ES1370 has no AC'97 codec and no SRC
    CHECK(NeedsAc97Reset(0x1274,0x5880,0x02) && NeedsAc97Reset(0x1274,0x5880,0x04) && NeedsAc97Reset(0x1274,0x1371,0x07) && NeedsAc97Reset(0x1274,0x1371,0x08));
    CHECK(!NeedsAc97Reset(0x1274,0x1371,0x02) && !NeedsAc97Reset(0x1274,0x1371,0x09) && !NeedsAc97Reset(0x1102,0x8938,0x00) && !NeedsAc97Reset(0x1274,0x5880,0x05));
    // Position: the current frame is the high half of the size register; the
    // value VMware showed after its first period of 512 frames was 0x02003fff.
    CHECK(Position(0x02003fff,16384)==512);
    CHECK(Position(0x00003fff,16384)==0);
    CHECK(Position(0x3fff3fff,16384)==16383);
    CHECK(Position(0xffff3fff,16384)==16383);     // garbage stays inside the ring
    CHECK(Step(512,0,16384)==512 && Step(0,16383,16384)==1 && Step(100,100,16384)==0 && Step(2048,14336+2047,16384)==2049);
    // A whole run of period-sized jumps adds up without loss across the wrap.
    uint64_t played=0,last=0;
    for(int i=1;i<=100;++i) {const uint64_t now=(uint64_t(i)*2048)%16384;played+=Step(now,last,16384);last=now;}
    CHECK(played==100*2048);
    puts("PASS CharonEs1371Tests");
    return 0;
}
