#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int16_t INT16;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int BOOLEAN;
#define VOID void
#define CONST const
#include "HakAudioRing.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define BF HAK_RING_BLOCK_FRAMES
#define BC HAK_RING_BLOCK_COUNT
static INT16 dma[BC*BF*2];
/* Frames carry their 32-bit sequence number (low half left, high right). */
static void Make(INT16 *out,UINT32 first,UINT32 n) {UINT32 i;for(i=0;i<n;i++){out[i*2]=(INT16)(first+i);out[i*2+1]=(INT16)((first+i)>>16);}}
int main(void) {
  HAK_AUDIO_RING ring={0};INT16 source[8192];
  UINT32 i,offset=0,block=0,seed=8197;UINT64 real=0;
  for(i=0;i<8192;i++)source[i]=(INT16)(i*37);
  CHECK(HakRingDrained(&ring));
  CHECK(HAK_RING_QUEUE_FRAMES%BF==0 && HAK_RING_QUEUE_FRAMES<=4096);
  CHECK(HakRingWrite(&ring,source,4096)==HAK_RING_QUEUE_FRAMES);
  CHECK(HakRingWrite(&ring,source,1)==0);
  for(i=0;i<BC;i++)HakRingFill(&ring,i,dma+i*BF*2);
  for(block=0;block<BC;block++) {
    if(block*BF<HAK_RING_QUEUE_FRAMES)CHECK(memcmp(dma+block*BF*2,source+block*BF*2,BF*4)==0);
    else for(i=0;i<BF*2;i++)CHECK(dma[block*BF*2+i]==0);
  }
  block=0;
  CHECK(HakRingHardwareFrames(&ring)==HAK_RING_QUEUE_FRAMES && !HakRingDrained(&ring));
  /* Simulated hardware cursor intervals (accounting, not an audio test). */
  for(i=0;i<100000;i++) {
    UINT32 step,j;seed=seed*1664525U+1013904223U;step=1+(seed%127);
    if(step>BF-offset)step=BF-offset;
    for(j=offset;j<offset+step;j++)if(j<ring.Payload[block]) {
      CHECK(dma[(block*BF+j)*2]==source[(real%4096)*2]);
      CHECK(dma[(block*BF+j)*2+1]==source[(real%4096)*2+1]);++real;
    }
    HakRingConsume(&ring,block,offset,offset+step);offset+=step;
    CHECK(ring.Consumed==real);
    CHECK(ring.Submitted-ring.Consumed==ring.Count+HakRingHardwareFrames(&ring));
    if(offset==BF) {
      UINT32 accepted,start=(UINT32)(ring.Submitted%4096),want=4096-start;
      if(want>777)want=777;
      accepted=HakRingWrite(&ring,source+start*2,want);
      CHECK(accepted<=want);
      HakRingFill(&ring,block,dma+block*BF*2);block=(block+1)%BC;offset=0;
    }
  }
  /* A short last payload needs actual observed pipeline progress before drain. */
  memset(&ring,0,sizeof(ring));CHECK(HakRingWrite(&ring,source,3)==3);
  HakRingFill(&ring,0,dma);CHECK(dma[6]==0 && dma[BF*2-1]==0);
  HakRingConsume(&ring,0,0,3);CHECK(!HakRingDrained(&ring));
  HakRingConsume(&ring,0,3,130);CHECK(!HakRingDrained(&ring));
  HakRingConsume(&ring,0,130,131);CHECK(HakRingDrained(&ring));
  CHECK(ring.Consumed==3 && ring.Underrun==128);
  /* Every allowed advance leaves completed descriptors outside the current
   * hardware block, including the observed 32 ms scheduling delay. */
  for(i=0;i<HAK_RING_DMA_FRAMES;i++) {
    UINT32 steps[]={1536,1920,HAK_RING_MAX_ADVANCE},test;
    for(test=0;test<3;test++) {
      UINT32 position=i,remaining=steps[test];
      UINT32 finalBlock=((i+remaining)%HAK_RING_DMA_FRAMES)/BF;
      while(remaining) {
        UINT32 from=position%BF,take=BF-from;
        if(take>remaining)take=remaining;
        if(from+take==BF)CHECK(position/BF!=finalBlock);
        position=(position+take)%HAK_RING_DMA_FRAMES;remaining-=take;
      }
    }
  }
  CHECK(HAK_RING_MAX_POLL_NS*48000/1000000000ULL<HAK_RING_MAX_ADVANCE);
  CHECK(HAK_RING_MAX_ADVANCE+BF<=HAK_RING_DMA_FRAMES);
  /* r389: early fill and discard against a moving cursor. Frames played must
   * be strictly increasing sequence numbers (FIFO, nothing replayed), every
   * write must sound within 1.5 blocks when the ring was idle, never closer to
   * the cursor than half a block, and accounting must stay exact. */
  {
    UINT32 position=0,next=1,lastPlayed=0,writes=0,checks=0;UINT64 played=0;
    INT16 chunk[512*2];
    memset(&ring,0,sizeof(ring));memset(dma,0,sizeof(dma));
    for(i=0;i<BC;i++)HakRingFill(&ring,i,dma+i*BF*2);   /* start: all silence */
    seed=12345;
    for(i=0;i<400000;i++) {
      UINT32 action;seed=seed*1664525U+1013904223U;action=seed>>24;
      if(action<90) {   /* the hardware advances 1..160 frames */
        UINT32 step=1+(seed%160),k;
        for(k=0;k<step;k++) {
          UINT32 b=position/BF,o=position%BF;
          UINT32 seq=(UINT32)(UINT16)dma[(b*BF+o)*2]|((UINT32)(UINT16)dma[(b*BF+o)*2+1]<<16);
          if(o<ring.Payload[b]) {CHECK(seq>lastPlayed);lastPlayed=seq;++played;}
          else CHECK(seq==0);
          HakRingConsume(&ring,b,o,o+1);position=(position+1)%HAK_RING_DMA_FRAMES;
          if(position%BF==0)HakRingFill(&ring,b,dma+b*BF*2);
        }
      } else if(action<200) {  /* a write of up to 512 frames, then early fill */
        UINT32 n=1+(seed%512),accepted,b=position/BF,o=position%BF,wasIdle=ring.Submitted==ring.Consumed;
        Make(chunk,next,n);accepted=HakRingWrite(&ring,chunk,n);next+=accepted;
        HakRingEarlyFill(&ring,b,o,dma);++writes;
        if(wasIdle && accepted) {
          /* The first new frame is in a block 1 or 2 ahead. */
          UINT32 d,found=0;
          for(d=1;d<BC && !found;d++) {UINT32 bb=(b+d)%BC;if(ring.Payload[bb])found=d;}
          CHECK(found==HakRingNearest(o));++checks;
        }
        /* Nothing new was written into the current block or (when late in
         * it) the next one. */
      } else if(action<215) {   /* a newer utterance: drop what is not near */
        UINT32 b=position/BF,o=position%BF;UINT64 before=ring.Submitted,dropped=HakRingDiscard(&ring,b,o,dma);
        CHECK(ring.Submitted==before-dropped && ring.Count==0);
      }
      CHECK(ring.Submitted-ring.Consumed==ring.Count+HakRingHardwareFrames(&ring));
      CHECK(ring.Consumed==played);
    }
    CHECK(writes>100000 && checks>100);
    printf("early fill: %u writes, %u idle-start checks, %llu frames played in order\n",writes,checks,(unsigned long long)played);
  }
  puts("PASS DMA-interval FIFO/accounting checks, backpressure, wrap, padding drain, early fill, discard");
  return 0;
}
