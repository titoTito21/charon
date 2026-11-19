#include "CharonPool.h"
#include <stdint.h>

/* Align payloads independently of the backing allocator's alignment. Keep the
 * original pointer so UEFI FreePool always sees exactly AllocatePool's result.
 */
typedef struct Header { CharonPool* owner; void* allocation; size_t bytes; } Header;
#define ALIGNMENT 16u
#define OVERHEAD (sizeof(Header) + ALIGNMENT - 1u)

int charon_pool_init(CharonPool* pool, void* context, CharonAllocate allocate,
    CharonRelease release, size_t limit) {
    if (!pool || !allocate || !release || !limit) return 0;
    pool->context=context; pool->allocate=allocate; pool->release=release;
    pool->limit=limit; pool->used=0; pool->allocations=0;
    return 1;
}
void* charon_pool_malloc(CharonPool* pool, size_t bytes) {
    if (!pool || !pool->allocate || !pool->release) return NULL;
    if (!bytes) bytes=1;
    if (bytes>SIZE_MAX-OVERHEAD || pool->used>pool->limit
        || bytes+OVERHEAD>pool->limit-pool->used) return NULL;
    void* allocation=pool->allocate(pool->context,bytes+OVERHEAD);
    if (!allocation) return NULL;
    uintptr_t payload=((uintptr_t)allocation+sizeof(Header)+ALIGNMENT-1)&~(uintptr_t)(ALIGNMENT-1);
    Header* header=(Header*)payload-1;
    header->owner=pool;header->allocation=allocation;header->bytes=bytes;
    pool->used+=bytes+OVERHEAD;pool->allocations++;
    return (void*)payload;
}
void charon_pool_free(CharonPool* pool, void* payload) {
    if (!payload || !pool) return;
    Header* header=(Header*)payload-1;
    /* As with C free(), payload must be a live allocation. Cross-instance
     * release is refused, so one synthesizer cannot consume another's budget.
     */
    if (header->owner!=pool) return;
    void* allocation=header->allocation;
    pool->used-=header->bytes+OVERHEAD;pool->allocations--;
    pool->release(pool->context,allocation);
}
void* charon_pool_calloc(CharonPool* pool, size_t count, size_t size) {
    if (size && count>SIZE_MAX/size) return NULL;
    size_t bytes=count*size;
    unsigned char* output=(unsigned char*)charon_pool_malloc(pool,bytes);
    if (output) for (size_t i=0;i<bytes;i++)output[i]=0;
    return output;
}
void* charon_pool_realloc(CharonPool* pool, void* payload, size_t bytes) {
    if (!payload) return charon_pool_malloc(pool,bytes);
    if (!pool || ((Header*)payload-1)->owner!=pool) return NULL;
    if (!bytes) {charon_pool_free(pool,payload);return NULL;}
    size_t before=((Header*)payload-1)->bytes;
    if (before==bytes) return payload;
    /* Account for both live blocks during copying; failure preserves the
     * original pointer and all of its contents, including at the budget limit.
     */
    unsigned char* output=(unsigned char*)charon_pool_malloc(pool,bytes);
    if (!output) return NULL;
    for (size_t i=0;i<(before<bytes?before:bytes);i++)output[i]=((unsigned char*)payload)[i];
    charon_pool_free(pool,payload);
    return output;
}
