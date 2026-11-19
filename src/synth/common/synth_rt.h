/* Charon synthesizer modules: freestanding runtime shared by all modules.
 * GPL-2.0-or-later (this runtime); musl parts keep their MIT licence.
 *
 * The runtime sits between an engine (eSpeak NG, RHVoice) and the firmware:
 *   - the platform (EFI glue or a test loader) supplies raw memory and a
 *     whole-file reader for the module's own volume;
 *   - malloc/free run over that memory with a hard budget;
 *   - a read-only virtual file system serves the engine's fopen()/stat()/
 *     opendir() from compressed packs (synth_pack.c);
 *   - every engine call runs on a private coroutine stack, so the engine can
 *     be paused inside its own synchronous audio callback (Render slices)
 *     and so firmware stack size never limits the engine;
 *   - abort()/exit()/std::terminate() inside the engine never return to it:
 *     the coroutine is marked failed and control returns to the caller.
 * No global constructors are used anywhere in the runtime.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct synth_platform {
    void* context;
    /* Returns memory aligned to at least 16 bytes, or NULL. */
    void* (*allocate)(void* context, size_t bytes);
    void (*release)(void* context, void* memory);
    /* Reads a whole file of the module's volume. path: '/'-separated, relative
     * to the volume root. On success *data is allocated with allocate(). */
    int (*read_file)(void* context, const char* path, void** data, size_t* size);
    /* 0 when the file exists (its size in *size); reads nothing. */
    int (*probe_file)(void* context, const char* path, size_t* size);
} synth_platform;

/* Called once by the entry point before anything else. limit: maximum bytes
 * requested from the platform in total (the module's memory budget). */
int synth_rt_init(const synth_platform* platform, size_t limit);
const synth_platform* synth_rt_platform(void);

/* Allocator statistics (bytes obtained from the platform, peak). */
size_t synth_rt_memory_used(void);
size_t synth_rt_memory_peak(void);

/* ---- virtual file system ---------------------------------------------- */
/* Mounts pack file `pack_path` (volume path) at virtual directory `mount`
 * (for example "/espeak"). Entries keep their relative paths. Returns 0. */
int synth_vfs_mount_pack(const char* pack_path, const char* mount);
/* Releases a mounted pack (and every cached file of it). 0 if it was mounted. */
int synth_vfs_unmount_pack(const char* pack_path);
/* 1 when pack_path is mounted. */
int synth_vfs_mounted(const char* pack_path);
/* Loads the decompressed bytes of a virtual file (malloc'd, caller frees). */
int synth_vfs_load(const char* path, void** data, size_t* size);
/* 1: keep decompressed files in memory and hand out copies (small packs
 * whose files are re-read, such as eSpeak dictionaries on voice changes). */
void synth_vfs_set_cache(int enable);
/* 1 = regular file, 2 = directory, 0 = absent; *size for files. */
int synth_vfs_kind(const char* path, size_t* size);
/* Iterates the immediate children of a directory: returns the child name of
 * index `index` (and its kind), or NULL past the end. */
const char* synth_vfs_child(const char* directory, size_t index, int* kind);

/* ---- coroutines ------------------------------------------------------- */
typedef struct synth_coro synth_coro;
typedef void (*synth_coro_entry)(void* argument);
/* Creates a suspended coroutine with its own stack (malloc'd). */
synth_coro* synth_coro_create(size_t stack_bytes, synth_coro_entry entry, void* argument);
/* Runs the coroutine until it yields or finishes. Returns 1 while it can be
 * resumed again, 0 once finished, -1 if it failed (abort inside). */
int synth_coro_resume(synth_coro* coroutine);
/* Inside a coroutine: returns control to the resumer. */
void synth_coro_yield(void);
/* Inside a coroutine: 1 when running in one. */
int synth_coro_active(void);
void synth_coro_destroy(synth_coro* coroutine);
/* Marks the current coroutine failed and leaves it for good (abort path). */
void synth_coro_fail(void) __attribute__((noreturn));

/* ---- diagnostics ---------------------------------------------------- */
/* Last failure reason of the module (UTF-8, NUL-terminated). Never printed:
 * a module must not touch ConOut. Test loaders read it via the symbol
 * synth_diagnostic; Charon may in future. */
extern char synth_diagnostic[160];
void synth_diagnose(const char* text);

/* ---- small helpers ---------------------------------------------------- */
uint32_t synth_crc32(const void* data, size_t bytes);
/* Splits UTF-16 load options into a UTF-8 '/'-separated volume path. */
void synth_path_from_utf16(const uint16_t* text, size_t bytes, char* out, size_t capacity);

#ifdef __cplusplus
}
#endif
