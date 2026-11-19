/* Source-derived Titan Voice components. MIT-0; see COPYING. */
#ifndef HAK_TVOX_NUMERIC_H
#define HAK_TVOX_NUMERIC_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Exact g2p_pl.number_to_words for the unsigned 64-bit domain. UTF-8 output.
 * required includes NUL. Returns 0 on success, 1 for insufficient capacity,
 * -1 for invalid pointers. Failure does not modify output. No allocation/OS. */
int tvox_pl_number(uint64_t number,char* output,size_t capacity,size_t* required);
typedef struct {
    const char* name;
    char sex;
    double f0,range,fscale,f1scale,bwscale,rd,tilt,breath,flutter,rate;
    /* Exact bundled default Klatt calibration at pitch=1, clarity=.5.
     * order PL, EN. These are not a replacement synthesis algorithm. */
    double calibrated_f0[2],calibrated_gain[2];
} tvox_voice;
extern const tvox_voice tvox_voices[8];
#ifdef __cplusplus
}
#endif
#endif
