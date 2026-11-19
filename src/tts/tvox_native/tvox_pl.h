/* MIT-0. Exact native Polish frontend and Adam acoustic frames. */
#ifndef HAK_TVOX_PL_H
#define HAK_TVOX_PL_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* mode0: a word; mode1: newline-separated already-normalized phrase words.
 * mode1 applies original proclitic/enclitic merges and phrase stress.
 * Output UTF8 rows: spelling<TAB>space-separated phones<TAB>stress digits LF.
 * required includes NUL; 0=success,1=insufficient capacity,-1=invalid input,
 * -2=allocation/internal failure. Output is unchanged on failure.
 * UTF8 must be valid, without NUL; at most65536 input bytes,1024 words and
 * 1024 codepoints per resulting word. TAB/CR are rejected. No text tokenizer
 * or utterance cache is implied. Input/output/required storage must not overlap.
 */
int tvox_pl_analyze(const char* input,size_t bytes,int mode,char* output,size_t capacity,size_t* required);
/* Built-in original textproc.normalise: language0=PL,1=EN. No auto-detection
 * or additional language packs. Output punctuation<TAB>space-joined words LF.
 * Same buffer/error contract and 65536-byte UTF8 limit as above. */
int tvox_normalise(const char* input,size_t bytes,int language,char* output,size_t capacity,size_t* required);
/* Original engine.text_to_phones for PL: full built-in normalization + G2P.
 * Output each nonempty phrase: @punctuation<TAB>wordCount LF, followed by
 * phone<TAB>stress<TAB>wordIndex<TAB>lastInWord LF. Same error/buffer contract.
 * This is phoneme/prosody INPUT only: no waveform or voice imitation. */
int tvox_pl_text(const char* input,size_t bytes,char* output,size_t capacity,size_t* required);
/* Original English word G2P, dictionary/composition/fallback/stress; output
 * space-joined phones TAB stress digits LF. Same UTF8/buffer contract. */
int tvox_en_analyze(const char* input,size_t bytes,char* output,size_t capacity,size_t* required);
/* Actual Adam/PL default Klatt acoustic frames. One row is 28 doubles:
 * f0,av,ah,af,a2,a3,a4,a5,a6,ab,f1,b1,f2,b2,f3,b3,f4,b4,f5,b5,f6,b6,
 * fnp,bnp,fnz,bnz,rd,creak. Capacity/required are ROW counts, not doubles.
 * Positive finite rate .1..10 and pitch <=10, max500000 rows. No PCM here.
 * _ex also returns actual edge pause segment lengths for original DSP trimming.
 * Generation is synchronous; it is not yet bounded firmware Pump work. */
int tvox_pl_frames(const char* input,size_t bytes,double rate,double pitch,double* output,size_t capacityFrames,size_t* requiredFrames);
int tvox_pl_frames_ex(const char* input,size_t bytes,double rate,double pitch,double* output,size_t capacityFrames,size_t* requiredFrames,size_t* leadingPauseFrames,size_t* trailingPauseFrames);
/* Actual Adam EN, original English phonemes/prosody and calibrated pitch. */
int tvox_en_frames_ex(const char* input,size_t bytes,double rate,double pitch,double* output,size_t capacityFrames,size_t* requiredFrames,size_t* leadingPauseFrames,size_t* trailingPauseFrames);
#ifdef __cplusplus
}
#endif
#endif
