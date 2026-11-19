/* Continuous DMA queue accounting. No timers, synthesized clocks or callbacks.
 * Caller serializes access at TPL_NOTIFY. Payload is always a prefix of a DMA
 * block; trailing silence cannot retire submitted samples prematurely. */
#ifndef HAK_AUDIO_RING_H
#define HAK_AUDIO_RING_H
#define HAK_RING_QUEUE_FRAMES 4096U /* r355: 85 ms of copy-in queue so a menu redraw longer than one 21 ms block no longer plays a silent block */
#define HAK_RING_BLOCK_FRAMES 512U  /* r389: 8 x 10.7 ms (was 4 x 21.3 ms) */
#define HAK_RING_BLOCK_COUNT 8U
#define HAK_RING_DMA_FRAMES (HAK_RING_BLOCK_COUNT*HAK_RING_BLOCK_FRAMES)
#define HAK_RING_MAX_ADVANCE 2048U /* 42.7 ms between polls, as before r389 */
#define HAK_RING_MAX_POLL_NS 40000000ULL
#define HAK_RING_PADDING_FRAMES 128U
typedef struct {
  INT16 Queue[HAK_RING_QUEUE_FRAMES*2];
  UINT32 Head,Count,Payload[HAK_RING_BLOCK_COUNT];
  UINT64 Submitted,Consumed,Underrun,Clock,LastPayloadEnd;
} HAK_AUDIO_RING;
static UINT32 HakRingWrite(HAK_AUDIO_RING *r,CONST INT16 *pcm,UINT32 frames) {
  UINT32 i,n=HAK_RING_QUEUE_FRAMES-r->Count;
  if(n>frames)n=frames;
  for(i=0;i<n;i++) {
    UINT32 at=(r->Head+r->Count+i)%HAK_RING_QUEUE_FRAMES;
    r->Queue[at*2]=pcm[i*2];r->Queue[at*2+1]=pcm[i*2+1];
  }
  r->Count+=n;r->Submitted+=n;return n;
}
/* Called only on a stopped or fully consumed DMA block. Destination must not
 * overlap the hardware's current block. DMA memory mapping is driver-owned. */
static VOID HakRingFill(HAK_AUDIO_RING *r,UINT32 block,INT16 *dma) {
  UINT32 i,n=r->Count;
  if(n>HAK_RING_BLOCK_FRAMES)n=HAK_RING_BLOCK_FRAMES;
  for(i=0;i<HAK_RING_BLOCK_FRAMES;i++) {
    if(i<n) {
      UINT32 at=(r->Head+i)%HAK_RING_QUEUE_FRAMES;
      dma[i*2]=r->Queue[at*2];dma[i*2+1]=r->Queue[at*2+1];
    } else {dma[i*2]=0;dma[i*2+1]=0;}
  }
  r->Head=(r->Head+n)%HAK_RING_QUEUE_FRAMES;r->Count-=n;r->Payload[block]=n;
}
/* Retire exactly the hardware-observed half-open interval inside one block.
 * Each interval is submitted once in increasing order by the controller.
 * The caller handles wrap by splitting intervals at the descriptor boundary. */
static VOID HakRingConsume(HAK_AUDIO_RING *r,UINT32 block,UINT32 from,UINT32 to) {
  UINT32 real=0,end=r->Payload[block];
  if(from<end) {
    if(end>to)end=to;
    real=end-from;
    r->LastPayloadEnd=r->Clock+real;
  }
  r->Consumed+=real;r->Underrun+=(to-from)-real;r->Clock+=to-from;
}
static UINT32 HakRingHardwareFrames(CONST HAK_AUDIO_RING *r) {
  return (UINT32)(r->Submitted-r->Consumed-r->Count);
}
static BOOLEAN HakRingDrained(CONST HAK_AUDIO_RING *r) {
  return r->Submitted==r->Consumed && r->Count==0
    && (r->Submitted==0 || r->Clock-r->LastPayloadEnd>=HAK_RING_PADDING_FRAMES);
}
/* r389: latency. A block is normally refilled when the hardware has played
 * it, so new frames waited a whole ring (up to 64 ms) behind silence. Blocks
 * ahead of the hardware that are still silent, and that no filled block
 * follows (the tail), take queued frames at once; the last filled block of the
 * tail is extended. 'Nearest' keeps half a block of distance from the cursor
 * (DMA prefetch). FIFO order is kept: frames only go behind the newest filled
 * frames. */
static UINT32 HakRingNearest(UINT32 offset) {return offset<HAK_RING_BLOCK_FRAMES/2?1U:2U;}
static VOID HakRingEarlyFill(HAK_AUDIO_RING *r,UINT32 current,UINT32 offset,INT16 *dma) {
  UINT32 nearest=HakRingNearest(offset),start=HAK_RING_BLOCK_COUNT,d;
  if(!r->Count)return;
  while(start>nearest && r->Payload[(current+start-1)%HAK_RING_BLOCK_COUNT]==0)--start;
  /* Extend the newest filled block when it is far enough ahead. */
  if(start>nearest) {
    UINT32 b=(current+start-1)%HAK_RING_BLOCK_COUNT,have=r->Payload[b],n=r->Count,i;
    if(have<HAK_RING_BLOCK_FRAMES) {
      INT16 *block=dma+b*HAK_RING_BLOCK_FRAMES*2;
      if(n>HAK_RING_BLOCK_FRAMES-have)n=HAK_RING_BLOCK_FRAMES-have;
      for(i=0;i<n;i++) {UINT32 at=(r->Head+i)%HAK_RING_QUEUE_FRAMES;block[(have+i)*2]=r->Queue[at*2];block[(have+i)*2+1]=r->Queue[at*2+1];}
      r->Head=(r->Head+n)%HAK_RING_QUEUE_FRAMES;r->Count-=n;r->Payload[b]=have+n;
    }
  }
  for(d=start;d<HAK_RING_BLOCK_COUNT && r->Count;d++) {
    UINT32 b=(current+d)%HAK_RING_BLOCK_COUNT;HakRingFill(r,b,dma+b*HAK_RING_BLOCK_FRAMES*2);
  }
}
/* r389: drop everything not yet near the cursor (a newer utterance replaces
 * the old one at once). Returns the frames removed from Submitted. */
static UINT64 HakRingDiscard(HAK_AUDIO_RING *r,UINT32 current,UINT32 offset,INT16 *dma) {
  UINT32 d,i;UINT64 dropped=r->Count;
  r->Count=0;
  for(d=HakRingNearest(offset);d<HAK_RING_BLOCK_COUNT;d++) {
    UINT32 b=(current+d)%HAK_RING_BLOCK_COUNT;
    if(!r->Payload[b])continue;
    dropped+=r->Payload[b];r->Payload[b]=0;
    for(i=0;i<HAK_RING_BLOCK_FRAMES*2;i++)dma[b*HAK_RING_BLOCK_FRAMES*2+i]=0;
  }
  r->Submitted-=dropped;return dropped;
}
#endif
