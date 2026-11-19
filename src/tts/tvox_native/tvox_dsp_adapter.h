/* Source-derived Titan Voice adapter. MIT-0; see COPYING. */
#ifndef HAK_TVOX_DSP_ADAPTER_H
#define HAK_TVOX_DSP_ADAPTER_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Blocking reference renderer, NOT an EFI Pump implementation.
 * Adam/PL/default timbre/clarity .5, mono PCM16 at 22050 Hz. Frames have
 * the original 28-double FRAME_KEYS+TRACK_KEYS+rd,creak layout.
 * leading/trailing are actual edge pause segment lengths (zero if absent).
 * Caller owns disjoint frames, scratch and output for the complete call.
 * required is set after validation. Status 1 queries/insufficient capacity,
 * -1 invalid input, -2 upstream renderer failure, 0 success. No allocation.
 * Scratch requires the same element count as output (double vs short).
 * Output is unchanged on validation/capacity failure. */
int tvox_dsp_render_pl(const double* frames,size_t count,size_t leading,
    size_t trailing,double pitch,double volume,double* scratch,
    size_t scratchCapacity,short* output,size_t outputCapacity,size_t* required);
int tvox_dsp_render_en(const double* frames,size_t count,size_t leading,
    size_t trailing,double pitch,double volume,double* scratch,
    size_t scratchCapacity,short* output,size_t outputCapacity,size_t* required);
#ifdef __cplusplus
}
#endif
#endif
