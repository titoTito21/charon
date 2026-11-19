/* MIT-0. Single-owner frontend runtime, no operating-system calls. */
#pragma once
#include "../runtime/CharonPool.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Configure while idle. Pool must remain valid through calls. A nonzero cancel
 * callback aborts at allocation/acoustic-frame checkpoints, returning -3
 * without output.
 * -4 rejects unconfigured/reentrant use. This is not yet resumable Pump work.
 * Caller must serialize entry; no cross-thread mutation is supported. */
int tvox_frontend_configure(CharonPool*,int (*cancel)(void*),void*);
/* x86_64 resumable whole-utterance preparation. Input is borrowed and immutable
 * until ready/cancel. Begin allocates one fixed 1MiB private stack; no parsing.
 * Pump executes at most workBudget checkpoints (1..1000000), returning
 * 0=pending,1=ready,<0=error. Counts bound algorithm work, NOT wall-clock time
 * of caller allocation/cancellation callbacks. They must be nonblocking.
 * consumed receives checkpoints charged in this call. Pending jobs preserve
 * original whole-utterance prosody. Other frontend entry points reject busy.
 * View publishes borrowed immutable 28-column rows only when ready. They
 * remain valid through DSP rendering until cancel. Cancel also releases ready
 * results/error jobs and permits a fresh begin; no stack unwinding required.
 */
int tvox_frontend_begin_pl(const char*,size_t,double rate,double pitch);
/* Exact English path; same ownership/scheduling contract and shared instance. */
int tvox_frontend_begin_en(const char*,size_t,double rate,double pitch);
int tvox_frontend_pump(size_t workBudget,size_t* consumed);
/* Optional nonblocking cooperative deadline. Queried at existing checkpoints;
 * nonzero suspends (does NOT cancel) this activation with its exact state.
 * Callback/context are borrowed only during this Pump; no callback after return.
 * Allocation and work between checkpoints still must be bounded by the caller. */
int tvox_frontend_pump_until(size_t workBudget,size_t* consumed,int (*yield)(void*),void*);
int tvox_frontend_frames(const double**,size_t*,size_t* leading,size_t* trailing);
int tvox_frontend_cancel(void);
#ifdef __cplusplus
}
#endif
