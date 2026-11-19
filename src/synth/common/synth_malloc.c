/* Charon synthesizer modules: malloc family over platform memory.
 * GPL-2.0-or-later.
 *
 * Small requests (<= 64 KiB) use segregated free lists carved from 1 MiB
 * arenas; larger ones go straight to the platform (AllocatePool in firmware)
 * and are released on free. Every block carries a 16-byte header. The total
 * obtained from the platform never exceeds the budget given to
 * synth_rt_init (the 64 MiB module limit). Single threaded by contract.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "synth_rt.h"

#define ARENA_BYTES ((size_t)1 << 20)
#define SMALL_LIMIT ((size_t)64 << 10)
#define KIND_SMALL 0x534d4131u
#define KIND_LARGE 0x4c524731u
#define KIND_ALIGNED 0x414c4e31u
#define CLASS_COUNT 64

typedef struct header {
    uint32_t kind;
    uint32_t cls;     /* small: class index; aligned: offset to base */
    uint64_t size;    /* usable bytes */
} header;

typedef struct free_node { struct free_node* next; } free_node;

static const synth_platform* platform;
static size_t budget, used, peak;
static free_node* free_lists[CLASS_COUNT];
static size_t class_sizes[CLASS_COUNT];
static size_t class_count;
static unsigned char* arena_cursor;
static size_t arena_left;

static void* platform_take(size_t bytes)
{
    if (!platform || bytes > budget - used) return NULL;
    void* p = platform->allocate(platform->context, bytes);
    if (!p) return NULL;
    used += bytes;
    if (used > peak) peak = used;
    return p;
}

static void platform_give(void* p, size_t bytes)
{
    platform->release(platform->context, p);
    used -= bytes;
}

int synth_rt_init_memory(const synth_platform* p, size_t limit);
int synth_rt_init_memory(const synth_platform* p, size_t limit)
{
    platform = p;
    budget = limit;
    used = peak = 0;
    memset(free_lists, 0, sizeof free_lists);
    arena_cursor = NULL;
    arena_left = 0;
    /* 16..128 in steps of 16, then four classes per power of two. */
    class_count = 0;
    for (size_t s = 16; s <= 128; s += 16) class_sizes[class_count++] = s;
    for (size_t base = 128; base < SMALL_LIMIT; base *= 2)
        for (int q = 1; q <= 4; ++q) class_sizes[class_count++] = base + base * q / 4;
    return class_count <= CLASS_COUNT ? 0 : -1;
}

size_t synth_rt_memory_used(void) { return used; }
size_t synth_rt_memory_peak(void) { return peak; }

static size_t class_for(size_t n)
{
    size_t lo = 0, hi = class_count - 1;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (class_sizes[mid] >= n) hi = mid; else lo = mid + 1;
    }
    return lo;
}

static void* small_alloc(size_t n)
{
    size_t c = class_for(n);
    free_node* node = free_lists[c];
    header* h;
    if (node) {
        free_lists[c] = node->next;
        h = (header*)node - 1;
    } else {
        size_t need = sizeof(header) + class_sizes[c];
        if (arena_left < need) {
            /* The remainder of the old arena feeds the smaller lists. */
            while (arena_left >= sizeof(header) + 16) {
                size_t k = class_for(arena_left - sizeof(header));
                if (class_sizes[k] > arena_left - sizeof(header)) {
                    if (!k) break;
                    --k;
                }
                header* r = (header*)arena_cursor;
                r->kind = KIND_SMALL; r->cls = (uint32_t)k; r->size = class_sizes[k];
                ((free_node*)(r + 1))->next = free_lists[k];
                free_lists[k] = (free_node*)(r + 1);
                arena_cursor += sizeof(header) + class_sizes[k];
                arena_left -= sizeof(header) + class_sizes[k];
            }
            arena_cursor = platform_take(ARENA_BYTES);
            if (!arena_cursor) { arena_left = 0; return NULL; }
            arena_left = ARENA_BYTES;
        }
        h = (header*)arena_cursor;
        arena_cursor += need;
        arena_left -= need;
        h->kind = KIND_SMALL;
        h->cls = (uint32_t)c;
        h->size = class_sizes[c];
    }
    return h + 1;
}

void* malloc(size_t n)
{
    if (!n) n = 1;
    if (n <= SMALL_LIMIT) {
        void* p = small_alloc(n);
        if (!p) errno = ENOMEM;
        return p;
    }
    if (n > ((size_t)1 << 40)) { errno = ENOMEM; return NULL; }
    n = (n + 15) & ~(size_t)15;
    header* h = platform_take(sizeof(header) + n);
    if (!h) { errno = ENOMEM; return NULL; }
    h->kind = KIND_LARGE;
    h->cls = 0;
    h->size = n;
    return h + 1;
}

static header* base_header(void* p)
{
    header* h = (header*)p - 1;
    if (h->kind == KIND_ALIGNED) h = (header*)((unsigned char*)p - h->cls) - 1;
    return h;
}

void free(void* p)
{
    if (!p) return;
    header* h = base_header(p);
    if (h->kind == KIND_SMALL) {
        free_node* node = (free_node*)(h + 1);
        node->next = free_lists[h->cls];
        free_lists[h->cls] = node;
    } else if (h->kind == KIND_LARGE) {
        platform_give(h, sizeof(header) + h->size);
    }
    /* Anything else is a foreign pointer: ignored rather than corrupting. */
}

void* calloc(size_t count, size_t size)
{
    if (size && count > (size_t)-1 / size) { errno = ENOMEM; return NULL; }
    void* p = malloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}

static size_t usable(void* p)
{
    header* h = (header*)p - 1;
    if (h->kind == KIND_ALIGNED) {
        header* b = base_header(p);
        return b->size - ((unsigned char*)p - (unsigned char*)(b + 1));
    }
    return h->size;
}

void* realloc(void* p, size_t n)
{
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    size_t have = usable(p);
    if (n <= have && n * 2 > have) return p;
    void* q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, have < n ? have : n);
    free(p);
    return q;
}

void* aligned_alloc(size_t alignment, size_t n)
{
    if (alignment <= 16) return malloc(n);
    if (alignment & (alignment - 1)) { errno = EINVAL; return NULL; }
    unsigned char* base = malloc(n + alignment + sizeof(header));
    if (!base) return NULL;
    uintptr_t at = ((uintptr_t)base + sizeof(header) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    header* h = (header*)at - 1;
    h->kind = KIND_ALIGNED;
    h->cls = (uint32_t)(at - (uintptr_t)base);
    h->size = n;
    return (void*)at;
}

int posix_memalign(void** out, size_t alignment, size_t n)
{
    if (alignment < sizeof(void*) || (alignment & (alignment - 1))) return EINVAL;
    void* p = aligned_alloc(alignment, n);
    if (!p) return ENOMEM;
    *out = p;
    return 0;
}

void* memalign(size_t alignment, size_t n) { return aligned_alloc(alignment, n); }
size_t malloc_usable_size(void* p) { return p ? usable(p) : 0; }

/* musl-internal names (newlocale/duplocale allocate through them). */
void* __libc_malloc(size_t n) { return malloc(n); }
void* __libc_calloc(size_t count, size_t size) { return calloc(count, size); }
void* __libc_realloc(void* p, size_t n) { return realloc(p, n); }
void __libc_free(void* p) { free(p); }
