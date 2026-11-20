/* HAK additive continuous audio protocol. Firmware ABI, unrelated to NVDA.
 * Implementation installs this beside AudioIo on the same codec child handle.
 * All entry points are called at TPL_APPLICATION; no application callbacks.
 * Initial negotiated format is exactly 48000 Hz, stereo interleaved PCM16LE.
 * Revision 2 (r389) adds Discard; revision 1 providers end at Stop.
 * Revision 3 (r400) adds Linger.
 * This header alone does not establish driver availability. */
#ifndef HAK_AUDIO_STREAM_H
#define HAK_AUDIO_STREAM_H
#include <Uefi.h>
#define HAK_AUDIO_STREAM_GUID \
 {0xa5e7cf46,0x421c,0x4ea9,{0x93,0x1e,0x16,0x4b,0xf3,0xa8,0xb2,0x70}}
#define HAK_AUDIO_STREAM_REVISION 3
#define HAK_AUDIO_STREAM_RUNNING 1
#define HAK_AUDIO_STREAM_DRAINED 2
#define HAK_AUDIO_STREAM_FAULT 4
typedef struct HAK_AUDIO_STREAM HAK_AUDIO_STREAM;
typedef struct {
  UINT32 Size;
  UINT32 Flags;
  UINT64 SubmittedFrames;
  UINT64 ConsumedFrames;
  UINT64 UnderrunFrames;
  UINT32 QueuedFrames;
  UINT32 HardwareFrames;
  EFI_STATUS LastError;
} HAK_AUDIO_STREAM_STATE;
struct HAK_AUDIO_STREAM {
  UINT32 Revision;
  UINT32 Size;
  /* Call existing AudioIo.SetupPlayback on this same handle first. Begin
   * verifies actual configured hardware format, rejects an active stream and
   * establishes fresh counters. It never guesses an output codec. */
  EFI_STATUS (EFIAPI *Begin)(HAK_AUDIO_STREAM *This);
  /* Copies an accepted prefix before returning. Remaining frames stay caller
   * owned; accepted frames never require caller memory again. No allocation.
   * Full queue succeeds with AcceptedFrames=0 (backpressure). */
  EFI_STATUS (EFIAPI *Write)(HAK_AUDIO_STREAM *This,CONST INT16 *Samples,
    UINTN Frames,UINTN *AcceptedFrames);
  /* Output atomically committed on success. Drained is based on actual DMA
   * consumption plus hardware pipeline padding, never wall-clock estimation.
   * A running stream may be drained while outputting silence. */
  EFI_STATUS (EFIAPI *GetState)(HAK_AUDIO_STREAM *This,HAK_AUDIO_STREAM_STATE *State);
  /* Success guarantees DMA/timer quiescence, queue cleared. On failure retain
   * driver/image ownership; never unload a possibly running provider. */
  EFI_STATUS (EFIAPI *Stop)(HAK_AUDIO_STREAM *This);
  /* Revision 2: drops queued frames and DMA blocks the hardware has not come
   * near yet (a newer utterance replaces the old one). Dropped frames are
   * subtracted from SubmittedFrames and reported; the stream keeps running. */
  EFI_STATUS (EFIAPI *Discard)(HAK_AUDIO_STREAM *This,UINT64 *DiscardedFrames);
  /* Revision 3: sound for the time AFTER ExitBootServices, when no firmware
   * code runs. Call it last, on a stopped stream (after Stop). The controller
   * is left playing from memory reserved at Begin (EfiReservedMemoryType, so
   * the operating system never reuses it): Loop (at most 65536 frames) is
   * heard Repeats times (at most 64), then about 20 minutes of silence follow
   * before the cycle starts again - a cue can therefore never sound without
   * end. It stops for good when an operating system driver resets the
   * controller. No timer, interrupt or callback remains. */
  EFI_STATUS (EFIAPI *Linger)(HAK_AUDIO_STREAM *This,CONST INT16 *Loop,UINT32 LoopFrames,UINT32 Repeats);
};
#endif
