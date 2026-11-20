/* CharonPC - Bochs inside a UEFI application. GPL-2.0-or-later (glue).
 *
 * The firmware side (pc_efi.cpp) offers these functions to the C library
 * layer (pc_posix.c), the Bochs GUI (pc_gui.cc) and the serial synthesizer
 * (pc_bns.cpp). Nothing else in CharonPC talks to UEFI.
 *
 * Paths are '/'-separated or '\'-separated paths on the volume CHARONPC.EFI
 * was loaded from. Two prefixes name things that are not volume files:
 *   "rom:NAME"     an image built into CHARONPC.EFI (BIOS, VGA BIOS) or the
 *                  generated Bochs configuration ("rom:bochsrc");
 *   "charon:bns"   the Braille 'n Speak synthesizer on COM1 (write only);
 *   "charon:log"   the Bochs log (write only).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- memory (synth_malloc.c's platform) ---- */
void* pc_allocate(void* context, size_t bytes);
void pc_release(void* context, void* memory);

/* ---- volume files: 0 or a positive errno value ---- */
int pc_vol_open(const char* path, int write, int create, int truncate, void** handle, uint64_t* size);
long pc_vol_read(void* handle, uint64_t position, void* buffer, size_t bytes);
long pc_vol_write(void* handle, uint64_t position, const void* buffer, size_t bytes);
int pc_vol_set_size(void* handle, uint64_t size);
int pc_vol_flush(void* handle);
void pc_vol_close(void* handle);
/* kind: 1 file, 2 directory. */
int pc_vol_stat(const char* path, uint64_t* size, int* kind);
int pc_vol_delete(const char* path);

/* Memory images ("rom:NAME"); NULL when unknown. */
const unsigned char* pc_rom(const char* name, size_t* size);

/* ---- time ---- */
uint64_t pc_usec(void);          /* monotonic microseconds since start */
int64_t pc_epoch_start(void);    /* firmware clock at start, seconds since 1970 (local time) */
void pc_stall(uint32_t microseconds);

/* ---- output sinks ---- */
void pc_log_write(const char* text, size_t bytes);
void pc_bns_write(const unsigned char* data, size_t bytes);

/* ---- runtime (pc_posix.c) ---- */
int pc_rt_init(size_t memory_limit);
void pc_rt_close_all(void);
/* The Bochs entry (pc_main.cc): runs bxmain() with the generated config. */
int pc_bochs_main(void);

#ifdef __cplusplus
}
#endif
