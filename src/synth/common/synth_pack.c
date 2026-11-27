/* Charon synthesizer modules: compressed data packs and the virtual file
 * system. GPL-2.0-or-later. LZMA decoding: LZMA SDK LzmaDec.c (public domain).
 *
 * Pack format "CHSP" version 1 (all integers little endian):
 *   char magic[8] = "CHSPACK1"; u32 count; u32 reserved
 *   count x { u16 name_bytes; char name[name_bytes];  (relative, '/')
 *             u8 method;       0 = stored, 1 = LZMA ("alone" stream)
 *             u8 range_count;  float32 byte-plane ranges, see below
 *             u32 raw_bytes; u32 packed_bytes; u32 offset; u32 crc32;
 *             range_count x { u32 offset; u32 bytes; } }
 *   then the data blocks, each at its `offset` from the start of the pack.
 * A byte-plane range of n bytes (n % 4 == 0) was stored as four planes (all
 * first bytes of each 32-bit word, then all second bytes, ...); decoding
 * interleaves them again. This is a lossless reordering that lets LZMA see
 * the slowly changing sign/exponent bytes of HTS model floats together.
 * The decompressed bytes are checked against crc32 before use.
 */
#include <stdlib.h>
#include <string.h>
#include "synth_rt.h"
#include "LzmaDec.h"

typedef struct pack_entry {
    char* path;            /* full virtual path: mount + "/" + name */
    const unsigned char* data;
    uint32_t method, raw, packed, crc;
    uint32_t range_count;
    const unsigned char* ranges;
    unsigned char* cached;
} pack_entry;

typedef struct mount {
    char source[256];
    unsigned char* pack;
    size_t pack_bytes;
    pack_entry* entries;
    size_t count;
} mount;

#define MAX_MOUNTS 8   /* r409: rhvoice mounts up to 2 language packs + 3 voice packs */
static mount mounts[MAX_MOUNTS];
static size_t mount_count;
static int cache_enabled;

void synth_vfs_set_cache(int enable) { cache_enabled = enable; }

static uint32_t rd32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd16(const unsigned char* p) { return p[0] | p[1] << 8; }

static void* lzma_alloc(ISzAllocPtr p, size_t size) { (void)p; return malloc(size); }
static void lzma_free(ISzAllocPtr p, void* address) { (void)p; free(address); }
static const ISzAlloc lzma_allocator = { lzma_alloc, lzma_free };

/* Normalises a path into out: collapses "//", "/./" and "dir/../". */
static int normalise(const char* in, char* out, size_t capacity)
{
    size_t n = 0;
    const char* p = in;
    while (*p) {
        while (*p == '/' || *p == '\\') ++p;
        if (!*p) break;
        const char* s = p;
        while (*p && *p != '/' && *p != '\\') ++p;
        size_t len = (size_t)(p - s);
        if (len == 1 && s[0] == '.') continue;
        if (len == 2 && s[0] == '.' && s[1] == '.') {
            while (n && out[n - 1] != '/') --n;
            if (n) --n;
            continue;
        }
        if (n + 1 + len + 1 > capacity) return -1;
        out[n++] = '/';
        memcpy(out + n, s, len);
        n += len;
    }
    if (!n) out[n++] = '/';
    out[n] = 0;
    return 0;
}

int synth_vfs_mount_pack(const char* pack_path, const char* mount_point)
{
    const synth_platform* platform = synth_rt_platform();
    if (!platform || mount_count == MAX_MOUNTS) return -1;
    void* raw = NULL;
    size_t bytes = 0;
    if (platform->read_file(platform->context, pack_path, &raw, &bytes) || !raw) return -2;
    /* Keep the pack in module heap memory (the platform buffer is released). */
    unsigned char* pack = malloc(bytes ? bytes : 1);
    if (!pack) { platform->release(platform->context, raw); return -3; }
    memcpy(pack, raw, bytes);
    platform->release(platform->context, raw);
    if (bytes < 16 || memcmp(pack, "CHSPACK1", 8)) { free(pack); return -4; }
    size_t count = rd32(pack + 8), at = 16;
    pack_entry* entries = calloc(count ? count : 1, sizeof *entries);
    char base[256];
    if (!entries || normalise(mount_point, base, sizeof base)) { free(entries); free(pack); return -3; }
    size_t base_len = strcmp(base, "/") ? strlen(base) : 0;
    for (size_t i = 0; i < count; ++i) {
        if (at + 2 > bytes) goto corrupt;
        size_t name_len = rd16(pack + at);
        at += 2;
        if (at + name_len + 18 > bytes) goto corrupt;
        pack_entry* e = &entries[i];
        char name[512], full[768];
        if (name_len >= sizeof name) goto corrupt;
        memcpy(name, pack + at, name_len);
        name[name_len] = 0;
        at += name_len;
        e->method = pack[at];
        e->range_count = pack[at + 1];
        e->raw = rd32(pack + at + 2);
        e->packed = rd32(pack + at + 6);
        uint32_t offset = rd32(pack + at + 10);
        e->crc = rd32(pack + at + 14);
        at += 18;
        e->ranges = pack + at;
        at += 8u * e->range_count;
        if (at > bytes || offset > bytes || e->packed > bytes - offset) goto corrupt;
        e->data = pack + offset;
        memcpy(full, base, base_len);
        full[base_len] = '/';
        strcpy(full + base_len + 1, name);
        char clean[768];
        if (normalise(full, clean, sizeof clean)) goto corrupt;
        e->path = malloc(strlen(clean) + 1);
        if (!e->path) goto corrupt;
        strcpy(e->path, clean);
    }
    memset(mounts[mount_count].source, 0, sizeof mounts[mount_count].source);
    strncpy(mounts[mount_count].source, pack_path, sizeof mounts[mount_count].source - 1);
    mounts[mount_count].pack = pack;
    mounts[mount_count].pack_bytes = bytes;
    mounts[mount_count].entries = entries;
    mounts[mount_count].count = count;
    ++mount_count;
    return 0;
corrupt:
    for (size_t i = 0; i < count; ++i) free(entries[i].path);
    free(entries);
    free(pack);
    return -5;
}

static int mount_index(const char* pack_path)
{
    for (size_t m = 0; m < mount_count; ++m)
        if (!strncmp(mounts[m].source, pack_path, sizeof mounts[m].source - 1)) return (int)m;
    return -1;
}

int synth_vfs_mounted(const char* pack_path) { return mount_index(pack_path) >= 0; }

int synth_vfs_unmount_pack(const char* pack_path)
{
    int m = mount_index(pack_path);
    if (m < 0) return -1;
    for (size_t i = 0; i < mounts[m].count; ++i) {
        free(mounts[m].entries[i].path);
        free(mounts[m].entries[i].cached);
    }
    free(mounts[m].entries);
    free(mounts[m].pack);
    for (size_t k = (size_t)m; k + 1 < mount_count; ++k) mounts[k] = mounts[k + 1];
    --mount_count;
    return 0;
}

static pack_entry* find(const char* path)
{
    char clean[768];
    if (normalise(path, clean, sizeof clean)) return NULL;
    for (size_t m = 0; m < mount_count; ++m)
        for (size_t i = 0; i < mounts[m].count; ++i)
            if (!strcmp(mounts[m].entries[i].path, clean)) return &mounts[m].entries[i];
    return NULL;
}

static int unplane(unsigned char* data, uint32_t raw, const unsigned char* ranges, uint32_t count)
{
    for (uint32_t r = 0; r < count; ++r) {
        uint32_t offset = rd32(ranges + 8 * r), bytes = rd32(ranges + 8 * r + 4);
        if (offset > raw || bytes > raw - offset || bytes % 4) return -1;
        unsigned char* copy = malloc(bytes ? bytes : 1);
        if (!copy) return -2;
        memcpy(copy, data + offset, bytes);
        uint32_t words = bytes / 4;
        for (uint32_t k = 0; k < 4; ++k)
            for (uint32_t w = 0; w < words; ++w) data[offset + 4 * w + k] = copy[k * words + w];
        free(copy);
    }
    return 0;
}

int synth_vfs_load(const char* path, void** out, size_t* size)
{
    pack_entry* e = find(path);
    if (!e) return -1;
    if (e->cached) {
        unsigned char* copy = malloc(e->raw ? e->raw : 1);
        if (!copy) return -2;
        memcpy(copy, e->cached, e->raw);
        *out = copy;
        *size = e->raw;
        return 0;
    }
    unsigned char* data = malloc(e->raw ? e->raw : 1);
    if (!data) return -2;
    if (e->method == 0) {
        if (e->packed != e->raw) { free(data); return -3; }
        memcpy(data, e->data, e->raw);
    } else if (e->method == 1) {
        if (e->packed < LZMA_PROPS_SIZE + 8) { free(data); return -3; }
        SizeT out_len = e->raw, in_len = e->packed - LZMA_PROPS_SIZE - 8;
        ELzmaStatus status;
        SRes res = LzmaDecode(data, &out_len, e->data + LZMA_PROPS_SIZE + 8, &in_len,
                              e->data, LZMA_PROPS_SIZE, LZMA_FINISH_ANY, &status, &lzma_allocator);
        if (res != SZ_OK || out_len != e->raw) { free(data); return -3; }
    } else {
        free(data);
        return -3;
    }
    if (unplane(data, e->raw, e->ranges, e->range_count)) { free(data); return -3; }
    if (synth_crc32(data, e->raw) != e->crc) { free(data); return -4; }
    if (cache_enabled) {
        unsigned char* copy = malloc(e->raw ? e->raw : 1);
        if (copy) { memcpy(copy, data, e->raw); e->cached = data; data = copy; }
    }
    *out = data;
    *size = e->raw;
    return 0;
}

int synth_vfs_kind(const char* path, size_t* size)
{
    char clean[768];
    if (normalise(path, clean, sizeof clean)) return 0;
    size_t n = strlen(clean);
    for (size_t m = 0; m < mount_count; ++m)
        for (size_t i = 0; i < mounts[m].count; ++i) {
            const pack_entry* e = &mounts[m].entries[i];
            if (!strcmp(e->path, clean)) { if (size) *size = e->raw; return 1; }
            if (n == 1 || (!strncmp(e->path, clean, n) && e->path[n] == '/')) return 2;
        }
    return 0;
}

const char* synth_vfs_child(const char* directory, size_t index, int* kind)
{
    static char name[256];
    char clean[768];
    if (normalise(directory, clean, sizeof clean)) return NULL;
    size_t n = strcmp(clean, "/") ? strlen(clean) : 0;
    size_t seen = 0;
    /* Children in order of first appearance; duplicates (several files in
     * one subdirectory) are counted once. Quadratic, packs are small. */
    for (size_t m = 0; m < mount_count; ++m)
        for (size_t i = 0; i < mounts[m].count; ++i) {
            const char* p = mounts[m].entries[i].path;
            if (strncmp(p, clean, n) || p[n] != '/') continue;
            const char* child = p + n + 1;
            const char* slash = strchr(child, '/');
            size_t len = slash ? (size_t)(slash - child) : strlen(child);
            int duplicate = 0;
            for (size_t m2 = 0; m2 <= m && !duplicate; ++m2)
                for (size_t j = 0; j < (m2 == m ? i : mounts[m2].count); ++j) {
                    const char* q = mounts[m2].entries[j].path;
                    if (!strncmp(q, clean, n) && q[n] == '/' && !strncmp(q + n + 1, child, len)
                        && (q[n + 1 + len] == '/' || q[n + 1 + len] == 0)) { duplicate = 1; break; }
                }
            if (duplicate) continue;
            if (seen++ == index) {
                if (len >= sizeof name) len = sizeof name - 1;
                memcpy(name, child, len);
                name[len] = 0;
                if (kind) *kind = slash ? 2 : 1;
                return name;
            }
        }
    return NULL;
}
