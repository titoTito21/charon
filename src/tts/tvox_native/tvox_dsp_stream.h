/* MIT-0. Original TVox DSP with explicit resumable lifetime. */
#ifndef HAK_TVOX_DSP_STREAM_H
#define HAK_TVOX_DSP_STREAM_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t tvox_dsp_stream_size(void);
/* Storage must be double-aligned and at least stream_size bytes. Frames remain
 * immutable and alive until done/cancel. Begin resets all state, no allocation.
 * Same frame layout and edge pause metadata as tvox_dsp_render_pl. */
int tvox_dsp_stream_begin(void* storage,const double* frames,size_t count,
    size_t leading,size_t trailing,double pitch,double volume);
/* English Adam uses original EN gain and source EQ, sharing actual LF/DSP. */
int tvox_dsp_stream_begin_en(void* storage,const double* frames,size_t count,
    size_t leading,size_t trailing,double pitch,double volume);
/* Maximum output samples, mono PCM16/22050. At most ceil(maximum/110)
 * acoustic frames evaluated (each original frame=110 samples), including
 * skipped silence. Returns 0 pending, 1 completed/cancelled, -1 invalid/error.
 * Pending may write zero samples while fast-forwarding a leading pause.
 * maximum=0 is a nonadvancing poll. No callbacks; calls must be serialized. */
int tvox_dsp_stream_render(void* storage,short* output,size_t maximum,size_t* written);
void tvox_dsp_stream_cancel(void* storage);
#ifdef __cplusplus
}
#endif
#endif
