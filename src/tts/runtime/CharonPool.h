#pragma once
#include <stddef.h>
#include <stdint.h>

/* Single-owner allocation for a pre-OS synthesis instance. The backing store
 * supplies UEFI pools in firmware and an instrumented allocator in tests.
 * No system allocator, exception, recorded speech or host process is required.
 */
typedef void* (*CharonAllocate)(void* context, size_t bytes);
typedef void (*CharonRelease)(void* context, void* memory);
typedef struct CharonPool {
    void* context;
    CharonAllocate allocate;
    CharonRelease release;
    size_t limit, used, allocations;
} CharonPool;

#ifdef __cplusplus
extern "C" {
#endif
int charon_pool_init(CharonPool*, void*, CharonAllocate, CharonRelease, size_t);
void* charon_pool_malloc(CharonPool*, size_t);
void* charon_pool_calloc(CharonPool*, size_t, size_t);
void* charon_pool_realloc(CharonPool*, void*, size_t);
void charon_pool_free(CharonPool*, void*);
#ifdef __cplusplus
}
#endif
