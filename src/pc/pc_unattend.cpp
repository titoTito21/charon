// CharonPC - Windows 98 / ME Setup without questions (r407). GPL-2.0-or-later.
//
// A blind user cannot work through the graphical Setup wizard before any
// screen reader runs in the guest. Charon's installer therefore asks the
// wizard's questions itself (product key, name, computer name) and stores the
// answers in CHARONPC.CFG; this file turns them into what Microsoft's own
// Setup understands:
//
//   1. the start floppy of the user's disc (the El Torito boot image of
//      SETUP.ISO, Microsoft's files, taken from the user's own medium and never
//      shipped) is copied to SETUP.IMG in the profile, with a batch-setup
//      answer file MSBATCH.INF added, the start menu's wait shortened and the
//      Setup line of AUTOEXEC.BAT given "/IS A:\MSBATCH.INF";
//   2. the profile's empty hard disk gets one active FAT32 partition, already
//      formatted, so Setup has no partitioning and formatting screens (the
//      master boot code is written by the floppy's own FDISK /MBR).
//
// Nothing is guessed: when the disc has no 1.44 MB start floppy with a Setup
// line, or the disk already holds something, the old interactive way stays.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_glue.h"
#include "pc_platform.h"

namespace {

void Log(const char* text)
{
    char line[160];
    const int n = snprintf(line, sizeof line, "CharonPC setup: %s\n", text);
    if (n > 0) pc_log_write(line, (size_t)n);
}

const unsigned floppyBytes = 1440 * 1024;

uint32_t Le16(const uint8_t* p) { return (uint32_t)(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
void P16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void P32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }

// ---- FAT12 floppy image in memory ----------------------------------------------
struct Floppy {
    uint8_t* image;
    unsigned bytesPerSector, perCluster, reserved, fats, rootEntries, total, perFat, rootStart, dataStart, clusters;

    bool Open(uint8_t* bytes)
    {
        image = bytes;
        bytesPerSector = Le16(image + 11); perCluster = image[13]; reserved = Le16(image + 14); fats = image[16];
        rootEntries = Le16(image + 17); total = Le16(image + 19); perFat = Le16(image + 22);
        if (bytesPerSector != 512 || !perCluster || !fats || !rootEntries || !perFat || total * 512u > floppyBytes) return false;
        rootStart = (reserved + fats * perFat) * 512;
        dataStart = rootStart + rootEntries * 32;
        if (dataStart >= total * 512u) return false;
        clusters = (total * 512u - dataStart) / (perCluster * 512u);
        return clusters > 0 && clusters < 4085;   // FAT12
    }
    unsigned Next(unsigned cluster) const
    {
        const uint8_t* fat = image + reserved * 512;
        const unsigned o = cluster * 3 / 2, v = fat[o] | fat[o + 1] << 8;
        return (cluster & 1) ? v >> 4 : v & 0xFFF;
    }
    void Set(unsigned cluster, unsigned value)
    {
        for (unsigned copy = 0; copy < fats; ++copy) {
            uint8_t* fat = image + (reserved + copy * perFat) * 512;
            const unsigned o = cluster * 3 / 2;
            if (cluster & 1) { fat[o] = (uint8_t)((fat[o] & 0x0F) | (value << 4)); fat[o + 1] = (uint8_t)(value >> 4); }
            else { fat[o] = (uint8_t)value; fat[o + 1] = (uint8_t)((fat[o + 1] & 0xF0) | ((value >> 8) & 0x0F)); }
        }
    }
    uint8_t* Entry(const char* name83)   // "AUTOEXECBAT"
    {
        for (unsigned i = 0; i < rootEntries; ++i) {
            uint8_t* e = image + rootStart + i * 32;
            if (!e[0]) break;
            if (e[0] != 0xE5 && e[11] != 0x0F && !(e[11] & 0x18) && !memcmp(e, name83, 11)) return e;
        }
        return nullptr;
    }
    // The file's bytes (at most `capacity`), or -1.
    long Read(const char* name83, uint8_t* out, size_t capacity)
    {
        const uint8_t* e = Entry(name83);
        if (!e) return -1;
        const size_t size = Le32(e + 28);
        if (size > capacity) return -1;
        size_t done = 0;
        unsigned cluster = Le16(e + 26);
        for (unsigned hops = 0; done < size && cluster >= 2 && cluster < 0xFF0 && hops < clusters; ++hops) {
            size_t n = perCluster * 512u;
            if (n > size - done) n = size - done;
            memcpy(out + done, image + dataStart + (cluster - 2) * perCluster * 512u, n);
            done += n;
            cluster = Next(cluster);
        }
        return done == size ? (long)size : -1;
    }
    // Replaces or creates a file in the root folder.
    bool Write(const char* name83, const uint8_t* data, size_t size)
    {
        uint8_t* e = Entry(name83);
        if (e) {
            unsigned cluster = Le16(e + 26);
            for (unsigned hops = 0; cluster >= 2 && cluster < 0xFF0 && hops < clusters; ++hops) { const unsigned next = Next(cluster); Set(cluster, 0); cluster = next; }
        } else {
            for (unsigned i = 0; i < rootEntries && !e; ++i) {
                uint8_t* slot = image + rootStart + i * 32;
                if (slot[0] == 0 || slot[0] == 0xE5) e = slot;
            }
            if (!e) return false;
            memset(e, 0, 32);
            memcpy(e, name83, 11);
            e[11] = 0x20;
            P16(e + 24, (1 << 5) | 1);   // 1980-01-01, as a file without a clock
        }
        const size_t clusterBytes = perCluster * 512u;
        unsigned need = (unsigned)((size + clusterBytes - 1) / clusterBytes), first = 0, previous = 0, at = 2;
        size_t done = 0;
        for (; need; --need) {
            while (at < clusters + 2 && Next(at)) ++at;
            if (at >= clusters + 2) return false;   // full
            if (previous) Set(previous, at); else first = at;
            Set(at, 0xFFF);
            size_t n = clusterBytes;
            if (n > size - done) n = size - done;
            uint8_t* target = image + dataStart + (at - 2) * clusterBytes;
            memset(target, 0, clusterBytes);
            memcpy(target, data + done, n);
            done += n;
            previous = at;
        }
        P16(e + 26, first);
        P32(e + 28, (uint32_t)size);
        return true;
    }
};

bool FoldHas(const char* line, size_t n, const char* what)
{
    const size_t m = strlen(what);
    for (size_t i = 0; i + m <= n; ++i) {
        size_t k = 0;
        for (; k < m; ++k) { char c = line[i + k]; if (c >= 'a' && c <= 'z') c = (char)(c - 32); if (c != what[k]) break; }
        if (k == m) return true;
    }
    return false;
}

// AUTOEXEC.BAT: the first line that runs ...SETUP.EXE gets the batch switches;
// FDISK /MBR goes before it. False: no such line (not a disc we know how to steer).
bool PatchAutoexec(const char* in, size_t bytes, char* out, size_t capacity, size_t* outBytes)
{
    static const char extra[] = " /IS A:\\MSBATCH.INF";
    size_t at = 0, used = 0;
    bool done = false;
    while (at < bytes) {
        size_t end = at;
        while (end < bytes && in[end] != '\n' && in[end] != '\r') ++end;
        size_t next = end;
        while (next < bytes && (in[next] == '\n' || in[next] == '\r') && next - end < 2) ++next;
        const char* line = in + at;
        size_t n = end - at;
        size_t lead = 0;
        while (lead < n && (line[lead] == ' ' || line[lead] == '\t')) ++lead;
        // the command word of the line
        size_t word = lead;
        while (word < n && line[word] != ' ' && line[word] != '\t') ++word;
        const bool setup = !done && word - lead >= 9 && FoldHas(line + lead, word - lead, "SETUP.EXE")
                           && !FoldHas(line + lead, word - lead, "ECHO") && !FoldHas(line + lead, 3, "REM");
        if (setup) {
            static const char mbr[] = "FDISK /MBR\r\n";
            if (used + sizeof mbr + n + sizeof extra + 4 >= capacity) return false;
            memcpy(out + used, mbr, sizeof mbr - 1); used += sizeof mbr - 1;
            // OEMSETUP.EXE /K "switches": the switches go inside the quotes
            size_t quote = n;
            while (quote > word && line[quote - 1] != '"') --quote;
            const bool quoted = quote > word && FoldHas(line + word, n - word, "/K");
            if (quoted) {
                memcpy(out + used, line, quote - 1); used += quote - 1;
                memcpy(out + used, extra, sizeof extra - 1); used += sizeof extra - 1;
                memcpy(out + used, line + quote - 1, n - (quote - 1)); used += n - (quote - 1);
            } else {
                memcpy(out + used, line, n); used += n;
                memcpy(out + used, extra, sizeof extra - 1); used += sizeof extra - 1;
            }
            out[used++] = '\r'; out[used++] = '\n';
            done = true;
        } else {
            if (used + (next - at) + 2 >= capacity) return false;
            memcpy(out + used, in + at, next - at); used += next - at;
        }
        at = next;
    }
    *outBytes = used;
    return done;
}

// CONFIG.SYS: "menudefault=NAME,30" waits half a minute for a choice nobody hears: 2 seconds.
void PatchConfig(const char* in, size_t bytes, char* out, size_t capacity, size_t* outBytes)
{
    size_t at = 0, used = 0;
    while (at < bytes && used + 8 < capacity) {
        size_t end = at;
        while (end < bytes && in[end] != '\n') ++end;
        if (end < bytes) ++end;
        const char* line = in + at;
        const size_t n = end - at;
        const char* comma = (const char*)memchr(line, ',', n);
        if (n > 12 && FoldHas(line, 11, "MENUDEFAULT") && comma) {
            const size_t head = (size_t)(comma - line) + 1;
            if (used + head + 4 >= capacity) break;
            memcpy(out + used, line, head); used += head;
            out[used++] = '2'; out[used++] = '\r'; out[used++] = '\n';
        } else {
            if (used + n >= capacity) break;
            memcpy(out + used, line, n); used += n;
        }
        at = end;
    }
    *outBytes = used;
}

// Only what an .INF string can hold: no quotes, no control characters, plain ASCII.
void Clean(const char* in, char* out, size_t capacity)
{
    size_t n = 0;
    for (; *in && n + 1 < capacity; ++in) if (*in >= 32 && *in < 127 && *in != '"' && *in != '%' && *in != ';') out[n++] = *in;
    while (n && out[n - 1] == ' ') --n;
    out[n] = 0;
}

size_t MakeBatch(const pc::SetupAnswers& a, const char* readerFile, char* out, size_t capacity)
{
    char key[40], name[64], org[64], computer[20], zone[64];
    // the key as XXXXX-XXXXX-XXXXX-XXXXX-XXXXX, whatever way it was typed
    {
        size_t n = 0;
        for (const char* p = a.key; *p && n < 29; ++p) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) continue;
            if (n % 6 == 5) key[n++] = '-';
            key[n++] = c;
        }
        key[n] = 0;
        if (n != 29) key[0] = 0;   // incomplete: Setup asks, and the reader reads its page
    }
    Clean(a.name, name, sizeof name);
    Clean(a.org, org, sizeof org);
    Clean(a.zone, zone, sizeof zone);
    {
        size_t n = 0;
        for (const char* p = a.computer; *p && n < 15; ++p) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') computer[n++] = c;
        }
        computer[n] = 0;
    }
    int n = snprintf(out, capacity,
        "; Written by Charon (CharonPC) from the answers given in Charon's dialogue.\r\n"
        "[BatchSetup]\r\nVersion=3.0 (32-bit)\r\nSaveDate=01/01/00\r\n\r\n"
        "[Version]\r\nSignature = \"$CHICAGO$\"\r\nAdvancedINF=2.5\r\nLayoutFile=layout.inf\r\n\r\n"
        "[Setup]\r\nExpress=1\r\nInstallDir=\"C:\\WINDOWS\"\r\nInstallType=1\r\n%s%s%s"
        "EBD=0\r\nShowEula=0\r\nChangeDir=0\r\nOptionalComponents=1\r\nNetwork=1\r\nSystem=0\r\nCCP=0\r\nCleanBoot=0\r\n"
        "Display=0\r\nDevicePath=0\r\nNoDirWarn=1\r\n%s%s%sUninstall=0\r\nNoPrompt2Boot=1\r\nPenWinWarning=0\r\n\r\n"
        "[NameAndOrg]\r\nName=\"%s\"\r\nOrg=\"%s\"\r\nDisplay=0\r\n\r\n"
        "[Network]\r\nComputerName=\"%s\"\r\nWorkgroup=\"WORKGROUP\"\r\nDescription=\"CharonPC\"\r\nDisplay=0\r\n",
        key[0] ? "ProductKey=\"" : "", key, key[0] ? "\"\r\n" : "",
        zone[0] ? "TimeZone=\"" : "", zone, zone[0] ? "\"\r\n" : "",
        name[0] ? name : "User", org[0] ? org : (name[0] ? name : "User"), computer[0] ? computer : "CHARONPC");
    if (n <= 0 || (size_t)n >= capacity) return 0;
    const bool reader = readerFile && readerFile[0];
    if (reader || a.sound) {
        const int m = snprintf(out + n, capacity - (size_t)n, "\r\n[Install]\r\nAddReg=%s%s%s\r\n",
                               a.sound ? "Charon.Sound" : "", a.sound && reader ? "," : "", reader ? "Charon.Reader" : "");
        if (m <= 0 || (size_t)m >= capacity - (size_t)n) return 0;
        n += m;
    }
    if (a.sound) {
        // The emulated Sound Blaster 16 as a device Windows has to install. Setup's own detection does not
        // find it (an ACPI machine: the ISA sound cards are not probed), and the blind user cannot run Add
        // New Hardware. A root device with only its hardware id and the "install me" flag is enough:
        // Windows installs its in-box driver for *PNPB003 from its own cabinets and gives it resources.
        const int m = snprintf(out + n, capacity - (size_t)n,
            "\r\n[Charon.Sound]\r\n"
            "HKLM,\"Enum\\Root\\*PNPB003\\0000\",\"HardwareID\",,\"*PNPB003\"\r\n"
            "HKLM,\"Enum\\Root\\*PNPB003\\0000\",\"ConfigFlags\",1,20,00,00,00\r\n");
        if (m <= 0 || (size_t)m >= capacity - (size_t)n) return 0;
        n += m;
    }
    if (reader) {
        // the first desktop starts the screen reader's installer by itself.
        // r407: the user's RunOnce list, not the machine's: Setup runs the machine's list in its last
        // phase, where the keyboard does not reach other programs (the reader's installer stood on its
        // first page and Windows hung); the user's list is run by the first desktop.
        const int m = snprintf(out + n, capacity - (size_t)n,
            "\r\n[Charon.Reader]\r\n"
            "HKCU,\"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce\",\"CharonReader\",,\"C:\\READER\\%s\"\r\n", readerFile);
        if (m <= 0 || (size_t)m >= capacity - (size_t)n) return 0;
        n += m;
    }
    return (size_t)n;
}

// The 1.44 MB El Torito start floppy of an ISO 9660 disc image.
bool ReadStartFloppy(void* iso, uint64_t isoBytes, uint8_t* image)
{
    static uint8_t sector[2048];
    if (isoBytes < 20 * 2048 || pc_vol_read(iso, 17 * 2048, sector, 2048) != 2048) return false;
    if (sector[0] != 0 || memcmp(sector + 1, "CD001", 5) || memcmp(sector + 7, "EL TORITO SPECIFICATION", 23)) return false;
    const uint64_t catalog = Le32(sector + 0x47);
    if ((catalog + 1) * 2048 > isoBytes || pc_vol_read(iso, catalog * 2048, sector, 2048) != 2048) return false;
    if (sector[0] != 1 || sector[30] != 0x55 || sector[31] != 0xAA) return false;
    const uint8_t* entry = sector + 32;
    if (entry[0] != 0x88 || entry[1] != 2) return false;   // bootable, 1.44 MB floppy emulation
    const uint64_t lba = Le32(entry + 8);
    if (lba * 2048 + floppyBytes > isoBytes) return false;
    return pc_vol_read(iso, lba * 2048, image, floppyBytes) == (long)floppyBytes;
}

// ---- the hard disk: one active FAT32 partition, formatted ----------------------
bool WriteAt(void* file, uint64_t sector, const uint8_t* data, size_t bytes)
{
    return pc_vol_write(file, sector * 512, data, bytes) == (long)bytes;
}

struct Layout { uint32_t start, reserved, perFat, perCluster, clusters; };

bool FormatDisk(void* file, uint64_t diskBytes, Layout* layout)
{
    static uint8_t block[128 * 512];
    const uint64_t diskSectors = diskBytes / 512;
    if (diskSectors < 1024 * 1024) return false;   // under 512 MB: FAT32 is not for it
    // BIOS geometry by the LBA-assisted translation table, as the Bochs BIOS does it
    const unsigned heads = diskSectors <= 1032192u ? 16 : diskSectors <= 2064384u ? 32 : diskSectors <= 4128768u ? 64 : diskSectors <= 8257536u ? 128 : 255;
    const uint32_t start = 63;
    const uint32_t cylinderSectors = heads * 63;
    uint32_t sectors = (uint32_t)(diskSectors - start);
    sectors -= (uint32_t)((start + sectors) % cylinderSectors);   // ends on a cylinder boundary
    const unsigned perCluster = 8, reserved = 32;
    // FAT size: Microsoft's formula (fatgen103)
    const uint32_t tmp1 = sectors - reserved, tmp2 = (256u * perCluster + 2) / 2;
    const uint32_t perFat = (tmp1 + tmp2 - 1) / tmp2;
    const uint32_t clusters = (sectors - reserved - 2 * perFat) / perCluster;
    if (clusters < 65525) return false;
    // master boot record: the table only; FDISK /MBR of the start floppy adds the code
    memset(block, 0, 512);
    uint8_t* p = block + 446;
    p[0] = 0x80; p[1] = 1; p[2] = 1; p[3] = 0;          // active, starts at CHS 0/1/1
    p[4] = 0x0C;                                         // FAT32, LBA
    p[5] = 0xFE; p[6] = 0xFF; p[7] = 0xFF;               // past CHS: LBA counts
    P32(p + 8, start); P32(p + 12, sectors);
    block[510] = 0x55; block[511] = 0xAA;
    if (!WriteAt(file, 0, block, 512)) return false;
    memset(block, 0, 512);
    for (unsigned s = 1; s < start; ++s) if (!WriteAt(file, s, block, 512)) return false;
    // boot sector with the BPB (Setup's SYS step writes the start code)
    uint8_t* b = block;
    memset(b, 0, 512);
    b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90; memcpy(b + 3, "MSWIN4.1", 8);
    P16(b + 11, 512); b[13] = (uint8_t)perCluster; P16(b + 14, reserved); b[16] = 2; b[21] = 0xF8;
    P16(b + 24, 63); P16(b + 26, heads); P32(b + 28, start); P32(b + 32, sectors);
    P32(b + 36, perFat); P32(b + 44, 2); P16(b + 48, 1); P16(b + 50, 6);
    b[64] = 0x80; b[66] = 0x29; P32(b + 67, (uint32_t)pc_usec() ^ 0x43485232u);
    memcpy(b + 71, "NO NAME    ", 11); memcpy(b + 82, "FAT32   ", 8);
    b[0x5A] = 0xCD; b[0x5B] = 0x18;                      // until Setup writes its code: INT 18h
    b[510] = 0x55; b[511] = 0xAA;
    uint8_t* info = block + 512;
    memset(info, 0, 512);
    P32(info, 0x41615252); P32(info + 484, 0x61417272); P32(info + 488, 0xFFFFFFFF); P32(info + 492, 0xFFFFFFFF);   // free space: not known, Windows counts
    info[510] = 0x55; info[511] = 0xAA;
    uint8_t* third = block + 1024;
    memset(third, 0, 512);
    third[510] = 0x55; third[511] = 0xAA;
    if (!WriteAt(file, start, block, 3 * 512) || !WriteAt(file, start + 6, block, 3 * 512)) return false;
    memset(block, 0, 512);
    for (unsigned s = 3; s < reserved; ++s) if (s < 6 || s > 8) if (!WriteAt(file, start + s, block, 512)) return false;
    // both FATs and the root folder's cluster: zeros, then the three first entries
    memset(block, 0, sizeof block);
    const uint64_t zeroEnd = (uint64_t)start + reserved + 2ull * perFat + perCluster;
    for (uint64_t at = (uint64_t)start + reserved; at < zeroEnd; ) {
        const size_t n = zeroEnd - at > 128 ? 128 : (size_t)(zeroEnd - at);
        if (!WriteAt(file, at, block, n * 512)) return false;
        at += n;
        pc::PumpVoice();
    }
    P32(block, 0x0FFFFFF8); P32(block + 4, 0x0FFFFFFF); P32(block + 8, 0x0FFFFFFF);
    if (!WriteAt(file, start + reserved, block, 512) || !WriteAt(file, (uint64_t)start + reserved + perFat, block, 512)) return false;
    layout->start = start; layout->reserved = reserved; layout->perFat = perFat; layout->perCluster = perCluster; layout->clusters = clusters;
    return pc_vol_flush(file) == 0;
}

// Puts one host file into the freshly formatted partition as C:\READER\<name83>: the guest screen
// reader's installer, which the first Windows desktop starts by itself (MSBATCH.INF, RunOnce).
// The volume is empty, so the folder is cluster 3 and the file follows it without gaps.
bool AddReaderFile(void* disk, const Layout& l, const char* hostPath, const char* name83)
{
    void* in = nullptr;
    uint64_t size = 0;
    if (pc_vol_open(hostPath, 0, 0, 0, &in, &size)) return false;
    static uint8_t block[128 * 512];
    const uint32_t clusterBytes = l.perCluster * 512;
    const uint32_t count = (uint32_t)((size + clusterBytes - 1) / clusterBytes);
    bool ok = size > 0 && size < 0xFFFFFFFFull && count + 8 < l.clusters;
    const uint64_t fat = (uint64_t)l.start + l.reserved, data = fat + 2ull * l.perFat;
    auto clusterSector = [&](uint32_t cluster) { return data + (uint64_t)(cluster - 2) * l.perCluster; };
    // the file's data, clusters 4 .. 4+count-1
    for (uint64_t done = 0; ok && done < size; ) {
        size_t n = sizeof block;
        if (n > size - done) n = (size_t)(size - done);
        memset(block, 0, sizeof block);
        ok = pc_vol_read(in, done, block, n) == (long)n
             && WriteAt(disk, clusterSector(4) + done / 512, block, (n + 511) / 512 * 512);
        done += n;
        pc::PumpVoice();
    }
    pc_vol_close(in);
    // the FAT: 0,1 reserved; 2 root; 3 folder; 4.. the file's chain
    for (uint32_t first = 0; ok && first < count + 4; first += sizeof block / 4) {
        memset(block, 0, sizeof block);
        for (uint32_t i = 0; i < sizeof block / 4 && first + i < count + 4; ++i) {
            const uint32_t c = first + i;
            P32(block + i * 4, c == 0 ? 0x0FFFFFF8 : c < 4 || c == count + 3 ? 0x0FFFFFFF : c + 1);
        }
        ok = WriteAt(disk, fat + first / 128, block, sizeof block) && WriteAt(disk, fat + l.perFat + first / 128, block, sizeof block);
    }
    if (!ok) return false;
    const uint16_t date = (uint16_t)((20 << 9) | (1 << 5) | 1);   // 2000-01-01
    auto entry = [&](uint8_t* e, const char* name, uint8_t attribute, uint32_t cluster, uint32_t bytes) {
        memset(e, 0, 32); memcpy(e, name, 11); e[11] = attribute;
        P16(e + 16, date); P16(e + 18, date); P16(e + 24, date);
        P16(e + 20, cluster >> 16); P16(e + 26, cluster & 0xFFFF); P32(e + 28, bytes);
    };
    memset(block, 0, clusterBytes);
    entry(block, "READER     ", 0x10, 3, 0);
    if (!WriteAt(disk, clusterSector(2), block, clusterBytes)) return false;
    memset(block, 0, clusterBytes);
    entry(block, ".          ", 0x10, 3, 0);
    entry(block + 32, "..         ", 0x10, 0, 0);
    entry(block + 64, name83, 0x20, 4, (uint32_t)size);
    return WriteAt(disk, clusterSector(3), block, clusterBytes) && pc_vol_flush(disk) == 0;
}

// "J40252.EXE" -> "J40252  EXE"; false when it is not a plain 8.3 name.
bool Name83(const char* name, char* out)
{
    memset(out, ' ', 11); out[11] = 0;
    const char* dot = strrchr(name, '.');
    const size_t base = dot ? (size_t)(dot - name) : strlen(name), ext = dot ? strlen(dot + 1) : 0;
    if (!base || base > 8 || ext > 3) return false;
    for (size_t i = 0; i < base + ext; ++i) {
        char c = i < base ? name[i] : dot[1 + i - base];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        out[i < base ? i : 8 + i - base] = c;
    }
    return true;
}

}  // namespace

// Host-test entry points (tests/CharonPcUnattendTests.cpp).
bool pc_unattend_patch_floppy(uint8_t* image, const pc::SetupAnswers& answers, const char* readerFile)
{
    Floppy f;
    if (!f.Open(image)) return false;
    static char text[8192], patched[8192 + 256], batch[4096];
    long n = f.Read("AUTOEXECBAT", (uint8_t*)text, sizeof text);
    size_t out = 0;
    if (n <= 0 || !PatchAutoexec(text, (size_t)n, patched, sizeof patched, &out)) return false;
    if (!f.Write("AUTOEXECBAT", (const uint8_t*)patched, out)) return false;
    n = f.Read("CONFIG  SYS", (uint8_t*)text, sizeof text);
    if (n > 0) {
        PatchConfig(text, (size_t)n, patched, sizeof patched, &out);
        if (!f.Write("CONFIG  SYS", (const uint8_t*)patched, out)) return false;
    }
    const size_t b = MakeBatch(answers, readerFile, batch, sizeof batch);
    return b && f.Write("MSBATCH INF", (const uint8_t*)batch, b);
}

bool pc::PrepareUnattended(const char* isoPath, const char* floppyPath, const char* diskPath, const SetupAnswers& answers,
                           const char* readerPath)
{
    void* file = nullptr;
    uint64_t size = 0;
    if (pc_vol_open(floppyPath, 0, 0, 0, &file, &size) == 0) {   // made at an earlier start
        pc_vol_close(file);
        return size == floppyBytes;
    }
    // The disk must be untouched (no partition table yet): never format something that holds data.
    static uint8_t first[512];
    void* disk = nullptr;
    uint64_t diskBytes = 0;
    if (pc_vol_open(diskPath, 1, 0, 0, &disk, &diskBytes)) { Log("the disk file cannot be opened for writing"); return false; }
    bool empty = pc_vol_read(disk, 0, first, 512) == 512;
    // A partition table means the disk is in use. A file CharonPC made a moment ago may hold stale
    // bytes of the host volume instead: only a valid table (55 AA with a used entry) counts.
    if (empty && first[510] == 0x55 && first[511] == 0xAA)
        for (unsigned e = 0; e < 4; ++e) if (first[446 + e * 16 + 4] && Le32(first + 446 + e * 16 + 12)) empty = false;
    if (!empty) { Log("the disk already has a partition table: left alone"); pc_vol_close(disk); return false; }
    static uint8_t image[floppyBytes];   // not malloc: the emulator's heap does not exist yet at this point
    bool ok = true;
    void* iso = nullptr;
    uint64_t isoBytes = 0;
    if (ok && pc_vol_open(isoPath, 0, 0, 0, &iso, &isoBytes)) { Log("the disc image cannot be opened"); ok = false; }
    if (ok) { ok = ReadStartFloppy(iso, isoBytes, image); pc_vol_close(iso); if (!ok) Log("the disc has no 1.44 MB start floppy"); }
    // the reader's installer, when it is a program with a plain 8.3 name that exists in the profile
    char reader83[12] = "", readerName[16] = "";
    if (readerPath && readerPath[0]) {
        const char* name = strrchr(readerPath, '/');
        name = name ? name + 1 : readerPath;
        const size_t length = strlen(name);
        uint64_t readerBytes = 0; int kind = 0;
        if (length > 4 && length < sizeof readerName && FoldHas(name + length - 4, 4, ".EXE") && Name83(name, reader83)
            && pc_vol_stat(readerPath, &readerBytes, &kind) == 0 && readerBytes) {
            for (size_t i = 0; i <= length; ++i) readerName[i] = name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - 32) : name[i];
        }
    }
    if (ok) { ok = pc_unattend_patch_floppy(image, answers, readerName); if (!ok) Log("the start floppy has no Setup line or no room"); }
    Layout layout{};
    if (ok) {
        Say(polish ? "Przygotowuję instalację bez pytań: dysk i plik odpowiedzi dla instalatora Windows."
                   : "Preparing Setup without questions: the disk and the answer file for Windows Setup.");
        ok = FormatDisk(disk, diskBytes, &layout);
        if (!ok) Log("the disk could not be formatted");
    }
    if (ok && readerName[0]) ok = AddReaderFile(disk, layout, readerPath, reader83);
    pc_vol_close(disk);
    if (ok) {
        ok = pc_vol_open(floppyPath, 1, 1, 1, &file, &size) == 0;
        if (ok) {
            ok = pc_vol_write(file, 0, image, floppyBytes) == (long)floppyBytes && pc_vol_flush(file) == 0;
            pc_vol_close(file);
            if (!ok) pc_vol_delete(floppyPath);
        }
    }
    Log(ok ? "prepared: SETUP.IMG with MSBATCH.INF, disk formatted" : "not prepared");
    return ok;
}

// r407: the screen reader's installer for a system that is ALREADY installed (a ready disk image, or a
// reader chosen after Setup). The guest's own disk is never written here: writing into the file
// system of an installed Windows from outside was tried and Windows took the new clusters back. The
// installer is handed over on a disc of its own instead (an ISO 9660 image with that one file, made
// in the profile), and the first desktop starts it from there.

// 1: the guest's first FAT32 volume holds WINDOWS and READER\<name83> (Setup's own way put it there),
// 0: it holds WINDOWS without it, -1: no installed Windows that can be judged. Reads only.
int pc::GuestHasReader(const char* diskPath, const char* readerPath, bool* jawsInstalled)
{
    if (jawsInstalled) *jawsInstalled = false;
    const char* leaf = strrchr(readerPath, '/');
    leaf = leaf ? leaf + 1 : readerPath;
    char name83[12];
    if (!Name83(leaf, name83)) return -1;
    void* disk = nullptr;
    uint64_t diskBytes = 0;
    if (pc_vol_open(diskPath, 0, 0, 0, &disk, &diskBytes)) return -1;
    static uint8_t sector[512], block[64 * 512];
    int result = -1;
    auto readAt = [&](uint64_t lba, uint8_t* to, size_t bytes) { return pc_vol_read(disk, lba * 512, to, bytes) == (long)bytes; };
    do {
        if (!readAt(0, sector, 512) || sector[510] != 0x55 || sector[511] != 0xAA) break;
        const uint8_t type = sector[446 + 4];
        const uint32_t start = Le32(sector + 446 + 8);
        if ((type != 0x0B && type != 0x0C) || !start || !readAt(start, sector, 512)) break;
        if (Le16(sector + 11) != 512 || sector[16] != 2 || sector[510] != 0x55 || sector[511] != 0xAA) break;
        const uint32_t perCluster = sector[13], reserved = Le16(sector + 14), perFat = Le32(sector + 36), root = Le32(sector + 44);
        if (!perCluster || perCluster > 64 || !reserved || !perFat || root < 2) break;
        const uint32_t clusterBytes = perCluster * 512;
        auto clusterSector = [&](uint32_t cluster) { return (uint64_t)start + reserved + 2ull * perFat + (uint64_t)(cluster - 2) * perCluster; };
        auto next = [&](uint32_t cluster, uint32_t* value) {
            if (!readAt(start + reserved + cluster / 128, sector, 512)) return false;
            *value = Le32(sector + (cluster % 128) * 4) & 0x0FFFFFFF;
            return true;
        };
        bool windows = false, bad = false, ended = false;
        uint32_t reader = 0, guard = 0;
        for (uint32_t c = root; c >= 2 && c < 0x0FFFFFF8 && !ended; ) {
            if (++guard > 4096 || !readAt(clusterSector(c), block, clusterBytes)) { bad = true; break; }
            for (uint32_t i = 0; i < clusterBytes; i += 32) {
                const uint8_t* e = block + i;
                if (e[0] == 0) { ended = true; break; }
                if (e[0] == 0xE5 || e[11] == 0x0F || !(e[11] & 0x10)) continue;
                if (!memcmp(e, "WINDOWS    ", 11)) windows = true;
                if (!memcmp(e, "READER     ", 11)) reader = Le16(e + 20) << 16 | Le16(e + 26);
                if (jawsInstalled && (!memcmp(e, "JAWS", 4) || !memcmp(e, "JFW", 3))) *jawsInstalled = true;
            }
            if (!ended && !next(c, &c)) { bad = true; break; }
        }
        if (bad || !windows) break;
        result = 0;
        if (reader >= 2 && readAt(clusterSector(reader), block, clusterBytes))
            for (uint32_t i = 0; i < clusterBytes; i += 32) {
                if (block[i] == 0) break;
                if (block[i] != 0xE5 && block[i + 11] != 0x0F && !memcmp(block + i, name83, 11) && Le32(block + i + 28)) { result = 1; break; }
            }
    } while (false);
    pc_vol_close(disk);
    return result;
}

// Makes <isoPath>: an ISO 9660 disc with the installer as its only file, in the root folder. True: made
// now (name = the file's name on the disc). False: it was there already, or it could not be made.
bool pc::MakeReaderDisc(const char* readerPath, const char* isoPath, char* name, size_t nameCapacity)
{
    if (name && nameCapacity) name[0] = 0;
    void* out = nullptr; void* in = nullptr;
    uint64_t size = 0, had = 0;
    if (pc_vol_open(isoPath, 0, 0, 0, &out, &had) == 0) { pc_vol_close(out); return false; }
    const char* leaf = strrchr(readerPath, '/');
    leaf = leaf ? leaf + 1 : readerPath;
    const size_t length = strlen(leaf);
    char name83[12];
    if (length < 5 || length + 1 > nameCapacity || !Name83(leaf, name83)) { Log("the reader's installer has no plain 8.3 name"); return false; }
    if (pc_vol_open(readerPath, 0, 0, 0, &in, &size) || !size || size >= 0x7FFFFFFFull) { if (in) pc_vol_close(in); Log("the reader's installer is not in the profile"); return false; }
    if (pc_vol_open(isoPath, 1, 1, 1, &out, &had)) { pc_vol_close(in); Log("the reader's disc cannot be created"); return false; }
    char upper[16];
    for (size_t i = 0; i <= length; ++i) upper[i] = leaf[i] >= 'a' && leaf[i] <= 'z' ? (char)(leaf[i] - 32) : leaf[i];
    static uint8_t block[32 * 2048];
    const uint32_t fileSectors = (uint32_t)((size + 2047) / 2048), fileAt = 21, total = fileAt + fileSectors;
    auto both32 = [](uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) { p[i] = (uint8_t)(v >> (8 * i)); p[7 - i] = (uint8_t)(v >> (8 * i)); } };
    auto both16 = [](uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; };
    // a directory record; returns its length
    auto record = [&](uint8_t* p, const char* id, size_t idLength, uint32_t at, uint32_t bytes, bool folder) {
        const size_t n = 33 + idLength + ((idLength & 1) ? 0 : 1);
        memset(p, 0, n);
        p[0] = (uint8_t)n;
        both32(p + 2, at); both32(p + 10, bytes);
        p[18] = 100; p[19] = 1; p[20] = 1;            // 2000-01-01
        p[25] = folder ? 2 : 0;
        both16(p + 28, 1);
        p[32] = (uint8_t)idLength;
        memcpy(p + 33, id, idLength);
        return n;
    };
    bool ok = true;
    memset(block, 0, sizeof block);                    // sectors 0-15: the system area
    ok = pc_vol_write(out, 0, block, 16 * 2048) == 16 * 2048;
    // sector 16: the primary volume descriptor
    uint8_t* v = block;
    memset(v, 0, 2048);
    v[0] = 1; memcpy(v + 1, "CD001", 5); v[6] = 1;
    memset(v + 8, ' ', 32); memset(v + 40, ' ', 32); memcpy(v + 40, "CHARON_READER", 13);
    both32(v + 80, total);
    both16(v + 120, 1); both16(v + 124, 1); both16(v + 128, 2048);
    both32(v + 132, 10);
    P32(v + 140, 18);
    v[148] = 0; v[149] = 0; v[150] = 0; v[151] = 19;   // the big-endian path table
    record(v + 156, "\0", 1, 20, 2048, true);
    memset(v + 190, ' ', 813 - 190);
    for (unsigned d = 0; d < 4; ++d) { memset(v + 813 + d * 17, '0', 16); v[813 + d * 17 + 16] = 0; }
    v[881] = 1;
    // sector 17: the end of the descriptors
    uint8_t* t = block + 2048;
    memset(t, 0, 2048);
    t[0] = 255; memcpy(t + 1, "CD001", 5); t[6] = 1;
    // sectors 18 and 19: the path tables (the root alone)
    uint8_t* l = block + 2 * 2048; uint8_t* m = block + 3 * 2048;
    memset(l, 0, 4096);
    l[0] = 1; P32(l + 2, 20); l[6] = 1;
    m[0] = 1; m[5] = 20; m[7] = 1;
    // sector 20: the root folder
    uint8_t* r = block + 4 * 2048;
    memset(r, 0, 2048);
    size_t at = record(r, "\0", 1, 20, 2048, true);
    at += record(r + at, "\1", 1, 20, 2048, true);
    char id[20];
    const int idLength = snprintf(id, sizeof id, "%s;1", upper);
    record(r + at, id, (size_t)idLength, fileAt, (uint32_t)size, false);
    ok = ok && pc_vol_write(out, 16 * 2048, block, 5 * 2048) == 5 * 2048;
    // the file
    for (uint64_t done = 0; ok && done < size; ) {
        size_t n = sizeof block;
        if (n > size - done) n = (size_t)(size - done);
        memset(block, 0, sizeof block);
        const size_t padded = (n + 2047) / 2048 * 2048;
        ok = pc_vol_read(in, done, block, n) == (long)n && pc_vol_write(out, (uint64_t)fileAt * 2048 + done, block, padded) == (long)padded;
        done += n;
        pc::PumpVoice();
    }
    ok = ok && pc_vol_flush(out) == 0;
    pc_vol_close(in);
    pc_vol_close(out);
    if (!ok) { pc_vol_delete(isoPath); Log("the reader's disc could not be written"); return false; }
    if (name) memcpy(name, upper, length + 1);
    Log("the reader's installer is on a disc of its own (READER.ISO)");
    return true;
}
