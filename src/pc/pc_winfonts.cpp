// CharonPC - the guest Windows's own screen fonts, for the control reader (r407).
// GPL-2.0-or-later.
//
// The control reader (pc_winread) recognises text by the exact glyphs of the
// fonts Windows draws with. Those are Microsoft's files and are never shipped:
// they are read at run time from the user's own medium -
//   1. the installation disc image of the profile: <WIN9X|WIN98|WIN95>\MINI.CAB
//      (the small Windows that runs Setup) holds SSERIFE.FON, VGASYS.FON, ...;
//   2. the installed system on the profile's disk: \WINDOWS\FONTS\*.FON
//      (FAT16 / FAT32, short names).
// Each .FON found is handed to the caller, who copies what it needs.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_glue.h"
#include "pc_platform.h"

namespace {

uint32_t Le16(const uint8_t* p) { return (uint32_t)(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

bool FoldSame(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return false;
    }
    return true;
}

// ---- raw DEFLATE into one growing buffer (MSZIP blocks share their history) ----
struct Inflater {
    const uint8_t* in; size_t inBytes, inAt;
    uint8_t* out; size_t outCapacity, outAt;
    uint32_t bits; int count;
    bool bad;

    int Bits(int need)
    {
        uint32_t value = bits;
        while (count < need) {
            if (inAt >= inBytes) { bad = true; return 0; }
            value |= (uint32_t)in[inAt++] << count;
            count += 8;
        }
        bits = value >> need;
        count -= need;
        return (int)(value & ((1u << need) - 1));
    }
    struct Huffman { uint16_t count[16]; uint16_t symbol[288]; };
    static void Build(Huffman* h, const uint8_t* lengths, int n)
    {
        uint16_t offsets[16];
        for (int i = 0; i < 16; ++i) h->count[i] = 0;
        for (int i = 0; i < n; ++i) h->count[lengths[i]]++;
        h->count[0] = 0;
        offsets[1] = 0;
        for (int i = 1; i < 15; ++i) offsets[i + 1] = (uint16_t)(offsets[i] + h->count[i]);
        for (int i = 0; i < n; ++i) if (lengths[i]) h->symbol[offsets[lengths[i]]++] = (uint16_t)i;
    }
    int Decode(const Huffman* h)
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= Bits(1);
            if (bad) return -1;
            const int n = h->count[len];
            if (code - n < first) return h->symbol[index + (code - first)];
            index += n; first += n; first <<= 1; code <<= 1;
        }
        bad = true;
        return -1;
    }
    bool Codes(const Huffman* lit, const Huffman* dist)
    {
        static const uint16_t lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
        static const uint8_t lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
        static const uint16_t dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
        static const uint8_t dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
        for (;;) {
            int symbol = Decode(lit);
            if (bad) return false;
            if (symbol < 256) {
                if (outAt >= outCapacity) { bad = true; return false; }
                out[outAt++] = (uint8_t)symbol;
            } else if (symbol == 256) return true;
            else {
                symbol -= 257;
                if (symbol >= 29) { bad = true; return false; }
                const int length = lbase[symbol] + Bits(lext[symbol]);
                const int ds = Decode(dist);
                if (bad || ds < 0 || ds >= 30) { bad = true; return false; }
                const size_t distance = dbase[ds] + (size_t)Bits(dext[ds]);
                if (bad || distance > outAt || outAt + (size_t)length > outCapacity) { bad = true; return false; }
                for (int i = 0; i < length; ++i) { out[outAt] = out[outAt - distance]; ++outAt; }
            }
        }
    }
    // One complete DEFLATE stream (all its blocks up to the final one).
    bool Stream()
    {
        static Huffman lit, dist;
        static uint8_t lengths[320];
        bits = 0; count = 0;
        for (;;) {
            const int last = Bits(1), type = Bits(2);
            if (bad) return false;
            if (type == 0) {
                bits = 0; count = 0;
                if (inAt + 4 > inBytes) return false;
                const size_t n = Le16(in + inAt);
                if ((n ^ Le16(in + inAt + 2)) != 0xFFFF) return false;
                inAt += 4;
                if (inAt + n > inBytes || outAt + n > outCapacity) return false;
                memcpy(out + outAt, in + inAt, n);
                inAt += n; outAt += n;
            } else if (type == 1) {
                int i = 0;
                for (; i < 144; ++i) lengths[i] = 8;
                for (; i < 256; ++i) lengths[i] = 9;
                for (; i < 280; ++i) lengths[i] = 7;
                for (; i < 288; ++i) lengths[i] = 8;
                Build(&lit, lengths, 288);
                for (i = 0; i < 30; ++i) lengths[i] = 5;
                Build(&dist, lengths, 30);
                if (!Codes(&lit, &dist)) return false;
            } else if (type == 2) {
                static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                const int nlen = Bits(5) + 257, ndist = Bits(5) + 1, ncode = Bits(4) + 4;
                if (bad || nlen > 286 || ndist > 30) return false;
                int i = 0;
                for (; i < ncode; ++i) lengths[order[i]] = (uint8_t)Bits(3);
                for (; i < 19; ++i) lengths[order[i]] = 0;
                Build(&lit, lengths, 19);
                for (i = 0; i < nlen + ndist; ) {
                    int symbol = Decode(&lit);
                    if (bad) return false;
                    if (symbol < 16) lengths[i++] = (uint8_t)symbol;
                    else {
                        int repeat, value = 0;
                        if (symbol == 16) { if (!i) return false; value = lengths[i - 1]; repeat = 3 + Bits(2); }
                        else if (symbol == 17) repeat = 3 + Bits(3);
                        else repeat = 11 + Bits(7);
                        if (bad || i + repeat > nlen + ndist) return false;
                        while (repeat--) lengths[i++] = (uint8_t)value;
                    }
                }
                static uint8_t distLengths[32];
                memcpy(distLengths, lengths + nlen, (size_t)ndist);
                Build(&lit, lengths, nlen);
                Build(&dist, distLengths, ndist);
                if (!Codes(&lit, &dist)) return false;
            } else return false;
            if (last) return !bad;
        }
    }
};

// ---- Microsoft cabinet, MSZIP folders ---------------------------------------------
// Calls `found` for every member whose name ends in .FON. `cab` is the whole file.
unsigned CabinetFonts(const uint8_t* cab, size_t bytes, pc::FontSink found, void* context)
{
    if (bytes < 40 || memcmp(cab, "MSCF", 4)) return 0;
    const size_t filesAt = Le32(cab + 16);
    const unsigned folders = Le16(cab + 26), files = Le16(cab + 28), flags = Le16(cab + 30);
    size_t at = 36;
    unsigned folderReserve = 0, dataReserve = 0;
    if (flags & 4) {
        if (at + 4 > bytes) return 0;
        const unsigned headerReserve = Le16(cab + at);
        folderReserve = cab[at + 2]; dataReserve = cab[at + 3];
        at += 4 + headerReserve;
    }
    if (flags & 3) return 0;   // a cabinet chained to others: not what MINI.CAB is
    const size_t foldersAt = at;
    unsigned count = 0;
    for (unsigned folder = 0; folder < folders; ++folder) {
        const uint8_t* f = cab + foldersAt + folder * (8 + folderReserve);
        if ((size_t)(f - cab) + 8 > bytes) break;
        const size_t dataAt = Le32(f);
        const unsigned blocks = Le16(f + 4), type = Le16(f + 6) & 15;
        if (type > 1) continue;   // only stored and MSZIP
        // does this folder hold a font at all, and how long is it unpacked?
        size_t unpacked = 0;
        bool wanted = false;
        size_t fileAt = filesAt;
        for (unsigned i = 0; i < files && fileAt + 17 <= bytes; ++i) {
            const uint8_t* e = cab + fileAt;
            const char* name = (const char*)e + 16;
            const size_t n = strnlen(name, bytes - fileAt - 16);
            if (Le16(e + 8) == folder) {
                if (Le32(e + 4) + (size_t)Le32(e) > unpacked) unpacked = Le32(e + 4) + (size_t)Le32(e);
                if (n > 4 && FoldSame(name + n - 4, ".FON", 4)) wanted = true;
            }
            fileAt += 16 + n + 1;
        }
        if (!wanted || !unpacked || unpacked > ((size_t)64 << 20)) continue;
        uint8_t* data = (uint8_t*)malloc(unpacked + 32768);
        if (!data) continue;
        Inflater z = {};
        z.out = data; z.outCapacity = unpacked + 32768;
        bool ok = true;
        size_t blockAt = dataAt;
        for (unsigned b = 0; b < blocks && ok && z.outAt < unpacked; ++b) {
            if (blockAt + 8 + dataReserve > bytes) { ok = false; break; }
            const unsigned packed = Le16(cab + blockAt + 4), plain = Le16(cab + blockAt + 6);
            const uint8_t* block = cab + blockAt + 8 + dataReserve;
            if ((size_t)(block - cab) + packed > bytes) { ok = false; break; }
            const size_t before = z.outAt;
            if (type == 0) {
                if (z.outAt + packed > z.outCapacity) { ok = false; break; }
                memcpy(z.out + z.outAt, block, packed); z.outAt += packed;
            } else {
                if (packed < 2 || block[0] != 'C' || block[1] != 'K') { ok = false; break; }
                z.in = block + 2; z.inBytes = packed - 2; z.inAt = 0; z.bad = false;
                ok = z.Stream() && z.outAt - before == plain;
            }
            blockAt += 8 + dataReserve + packed;
        }
        if (ok) {
            fileAt = filesAt;
            for (unsigned i = 0; i < files && fileAt + 17 <= bytes; ++i) {
                const uint8_t* e = cab + fileAt;
                const char* name = (const char*)e + 16;
                const size_t n = strnlen(name, bytes - fileAt - 16);
                const size_t size = Le32(e), offset = Le32(e + 4);
                if (Le16(e + 8) == folder && n > 4 && FoldSame(name + n - 4, ".FON", 4) && offset + size <= z.outAt) {
                    found(name, data + offset, size, context);
                    ++count;
                }
                fileAt += 16 + n + 1;
            }
        }
        free(data);
    }
    return count;
}

// ---- ISO 9660: one file by path ------------------------------------------------------
bool IsoFind(void* iso, uint64_t isoBytes, const char* const* path, uint64_t* position, uint32_t* size)
{
    static uint8_t sector[2048];
    if (isoBytes < 17 * 2048 || pc_vol_read(iso, 16 * 2048, sector, 2048) != 2048 || memcmp(sector + 1, "CD001", 5)) return false;
    uint64_t lba = Le32(sector + 156 + 2);
    uint32_t length = Le32(sector + 156 + 10);
    for (; *path; ++path) {
        const size_t want = strlen(*path);
        bool found = false, directory = false;
        for (uint32_t done = 0; done < length && !found; done += 2048) {
            if (pc_vol_read(iso, (lba * 2048) + done, sector, 2048) != 2048) return false;
            for (unsigned i = 0; i < 2048 && sector[i]; i += sector[i]) {
                const uint8_t* r = sector + i;
                if (i + 33 > 2048 || i + r[0] > 2048) break;
                unsigned n = r[32];
                const char* name = (const char*)r + 33;
                for (unsigned k = 0; k < n; ++k) if (name[k] == ';') { n = k; break; }
                if (n && name[n - 1] == '.') --n;
                if (n == want && FoldSame(name, *path, n)) {
                    lba = Le32(r + 2); length = Le32(r + 10); directory = r[25] & 2; found = true;
                    break;
                }
            }
        }
        if (!found) return false;
        if (!path[1]) { if (directory) return false; *position = lba * 2048; *size = length; return true; }
        if (!directory) return false;
    }
    return false;
}

unsigned FromDisc(const char* isoPath, pc::FontSink found, void* context)
{
    void* iso = nullptr;
    uint64_t isoBytes = 0;
    if (!isoPath[0] || pc_vol_open(isoPath, 0, 0, 0, &iso, &isoBytes)) return 0;
    unsigned count = 0;
    static const char* const folders[] = {"WIN9X", "WIN98", "WIN95", nullptr};
    for (unsigned f = 0; folders[f] && !count; ++f) {
        const char* path[] = {folders[f], "MINI.CAB", nullptr};
        uint64_t position = 0;
        uint32_t size = 0;
        if (!IsoFind(iso, isoBytes, path, &position, &size) || size < 64 || size > (32u << 20)) continue;
        uint8_t* cab = (uint8_t*)malloc(size);
        if (!cab) break;
        if (pc_vol_read(iso, position, cab, size) == (long)size) count = CabinetFonts(cab, size, found, context);
        free(cab);
    }
    pc_vol_close(iso);
    return count;
}

// ---- FAT16 / FAT32 on the profile's disk image: \WINDOWS\FONTS\*.FON -------------------
struct Fat {
    void* file;
    uint64_t start, fat, data, root;   // sectors
    uint32_t perCluster, rootEntries, rootCluster, clusters;
    bool fat32;

    bool Read(uint64_t sector, void* buffer, unsigned count) { return pc_vol_read(file, sector * 512, buffer, (size_t)count * 512) == (long)count * 512; }
    bool Open()
    {
        static uint8_t b[512];
        if (!Read(0, b, 1) || b[510] != 0x55 || b[511] != 0xAA) return false;
        start = 0;
        for (unsigned e = 0; e < 4 && !start; ++e) {
            const uint8_t* p = b + 446 + e * 16;
            if (p[4] == 0x01 || p[4] == 0x04 || p[4] == 0x06 || p[4] == 0x0B || p[4] == 0x0C || p[4] == 0x0E) start = Le32(p + 8);
        }
        if (!start || !Read(start, b, 1) || b[510] != 0x55 || b[511] != 0xAA || Le16(b + 11) != 512) return false;
        perCluster = b[13];
        const uint32_t reserved = Le16(b + 14), fats = b[16], fat16 = Le16(b + 22);
        rootEntries = Le16(b + 17);
        const uint32_t perFat = fat16 ? fat16 : Le32(b + 36);
        uint32_t total = Le16(b + 19);
        if (!total) total = Le32(b + 32);
        if (!perCluster || !fats || !perFat) return false;
        fat32 = !fat16;
        fat = start + reserved;
        root = fat + (uint64_t)fats * perFat;
        data = root + (fat32 ? 0 : (rootEntries * 32 + 511) / 512);
        rootCluster = fat32 ? Le32(b + 44) : 0;
        clusters = (uint32_t)((start + total - data) / perCluster);
        return clusters >= 4085;   // FAT12 hard disks are not Windows 9x system disks
    }
    uint32_t Next(uint32_t cluster)
    {
        static uint8_t b[512];
        static uint64_t held = ~0ull;
        const uint64_t sector = fat + (fat32 ? cluster / 128 : cluster / 256);
        if (held != sector) { if (!Read(sector, b, 1)) { held = ~0ull; return 0x0FFFFFFF; } held = sector; }
        return fat32 ? Le32(b + (cluster % 128) * 4) & 0x0FFFFFFF : Le16(b + (cluster % 256) * 2);   // a FAT16 end mark is above every cluster
    }
    bool End(uint32_t cluster) const { return cluster < 2 || cluster >= clusters + 2; }
    // Walks a folder; `each` returns true to stop. cluster 0 = the FAT16 root.
    template <typename F> bool Folder(uint32_t cluster, F each)
    {
        static uint8_t b[64 * 512];
        if (!cluster && !fat32) {
            for (uint32_t done = 0; done < rootEntries * 32; done += 512) {
                if (!Read(root + done / 512, b, 1)) return false;
                for (unsigned i = 0; i < 512; i += 32) { if (!b[i]) return false; if (each(b + i)) return true; }
            }
            return false;
        }
        if (!cluster) cluster = rootCluster;
        if (perCluster > 64) return false;
        for (unsigned hops = 0; hops < 4096 && !End(cluster); ++hops) {
            if (!Read(data + (uint64_t)(cluster - 2) * perCluster, b, perCluster)) return false;
            for (unsigned i = 0; i < perCluster * 512; i += 32) { if (!b[i]) return false; if (each(b + i)) return true; }
            cluster = Next(cluster);
        }
        return false;
    }
    static uint32_t First(const uint8_t* e) { return Le16(e + 26) | Le16(e + 20) << 16; }
    bool Find(uint32_t folder, const char* name83, bool directory, uint32_t* cluster)
    {
        return Folder(folder, [&](const uint8_t* e) {
            if (e[0] == 0xE5 || e[11] == 0x0F || (bool)(e[11] & 0x10) != directory || memcmp(e, name83, 11)) return false;
            *cluster = First(e);
            return true;
        });
    }
};

unsigned FromDisk(const char* diskPath, pc::FontSink found, void* context)
{
    Fat f = {};
    uint64_t bytes = 0;
    if (!diskPath[0] || pc_vol_open(diskPath, 0, 0, 0, &f.file, &bytes)) return 0;
    unsigned count = 0;
    uint32_t windows = 0, fonts = 0;
    if (f.Open() && f.Find(0, "WINDOWS    ", true, &windows) && f.Find(windows, "FONTS      ", true, &fonts)) {
        // collect first (the folder walk uses one buffer), then read the files
        struct Entry { char name[13]; uint32_t cluster, size; };
        static Entry list[24];
        unsigned n = 0;
        f.Folder(fonts, [&](const uint8_t* e) {
            if (e[0] == 0xE5 || e[11] == 0x0F || (e[11] & 0x18) || memcmp(e + 8, "FON", 3) || n >= 24) return false;
            // the dialog and system fonts only: the big decorative sets are not needed
            if (memcmp(e, "SSERIF", 6) && memcmp(e, "VGASYS", 6) && memcmp(e, "VGAFIX", 6) && memcmp(e, "SERIF", 5) && memcmp(e, "8514", 4)) return false;
            Entry& x = list[n++];
            unsigned k = 0;
            for (unsigned i = 0; i < 8 && e[i] != ' '; ++i) x.name[k++] = (char)e[i];
            x.name[k++] = '.'; x.name[k++] = 'F'; x.name[k++] = 'O'; x.name[k++] = 'N'; x.name[k] = 0;
            x.cluster = Fat::First(e); x.size = Le32(e + 28);
            return false;
        });
        for (unsigned i = 0; i < n; ++i) {
            if (!list[i].size || list[i].size > (2u << 20)) continue;
            const size_t clusterBytes = f.perCluster * 512u;
            uint8_t* data = (uint8_t*)malloc(list[i].size + clusterBytes);
            if (!data) break;
            size_t done = 0;
            uint32_t cluster = list[i].cluster;
            for (unsigned hops = 0; done < list[i].size && !f.End(cluster) && hops < 65536; ++hops) {
                if (!f.Read(f.data + (uint64_t)(cluster - 2) * f.perCluster, data + done, f.perCluster)) break;
                done += clusterBytes;
                cluster = f.Next(cluster);
            }
            if (done >= list[i].size) { found(list[i].name, data, list[i].size, context); ++count; }
            free(data);
        }
    }
    pc_vol_close(f.file);
    return count;
}

}  // namespace

unsigned pc::LoadGuestFonts(const char* isoPath, const char* diskPath, FontSink found, void* context)
{
    unsigned count = FromDisc(isoPath, found, context);
    if (!count) count = FromDisk(diskPath, found, context);
    return count;
}
