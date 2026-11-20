// CharonPC - CHARONPC.CFG to Bochs configuration. GPL-2.0-or-later.
// The format is described in src/pc/PLAN.txt ("CHARONPC.CFG"). Unknown keys
// are an error, so a typing mistake in a profile is reported, not ignored.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_glue.h"

namespace {

struct Writer {
    char* out;
    size_t capacity, used;
    bool overflow;
    void Add(const char* format, ...) __attribute__((format(printf, 2, 3)))
    {
        va_list list;
        va_start(list, format);
        int n = vsnprintf(out + used, capacity - used, format, list);
        va_end(list);
        if (n < 0 || (size_t)n >= capacity - used) { overflow = true; out[used] = 0; return; }
        used += (size_t)n;
    }
};

struct Settings {
    char profile[40] = "PC";
    char cpu[40] = "p2_klamath";
    unsigned memory = 64;
    unsigned long ips = 60000000;
    char sync[16] = "both";
    char disk[2][260] = {};
    char diskFormat[2][16] = {{"raw"}, {"raw"}};
    char cdrom[260] = {};
    char floppy[260] = {};
    char boot[40] = "disk";
    char com1[260] = "bns";
    char sound[16] = "none";
    char net[16] = "none";
    char reader[40] = "";
    char lang[8] = "";
    char log[260] = "";
    bool debugcon = false;
};

void Copy(char* to, size_t capacity, const char* from)
{
    size_t n = strlen(from);
    if (n >= capacity) n = capacity - 1;
    memcpy(to, from, n);
    to[n] = 0;
}

bool Same(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
    }
    return *a == *b;
}

bool OneOf(const char* value, const char* const* list)
{
    for (; *list; ++list) if (Same(value, *list)) return true;
    return false;
}

// Relative file names are relative to the folder of CHARONPC.CFG.
void VolumePath(const char* directory, const char* value, char* out, size_t capacity)
{
    if (value[0] == '\\' || value[0] == '/') snprintf(out, capacity, "%s", value);
    else snprintf(out, capacity, "%s\\%s", directory, value);
    for (char* p = out; *p; ++p) if (*p == '\\') *p = '/';
}

const char* BochsDiskMode(const char* format)
{
    if (Same(format, "vhd") || Same(format, "vpc")) return "vpc";
    if (Same(format, "vmdk")) return "vmware4";
    if (Same(format, "vmdk3")) return "vmware3";
    if (Same(format, "vdi")) return "vbox";
    if (Same(format, "vhdx")) return "vhdx";
    return "flat";
}

}  // namespace

bool pc::TranslateConfig(const char* cfg, size_t bytes, const char* directory, char* out, size_t capacity,
                         char* error, size_t errorCapacity)
{
    Settings s;
    error[0] = 0;
    size_t at = 0;
    int lineNumber = 0;
    while (at < bytes) {
        char line[400];
        size_t n = 0;
        while (at < bytes && cfg[at] != '\n') { if (n + 1 < sizeof line && cfg[at] != '\r') line[n++] = cfg[at]; ++at; }
        ++at;
        ++lineNumber;
        line[n] = 0;
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p || *p == '#' || *p == ';') continue;
        char* equals = strchr(p, '=');
        if (!equals) { snprintf(error, errorCapacity, "line %d: expected key=value", lineNumber); return false; }
        char* keyEnd = equals;
        while (keyEnd > p && (keyEnd[-1] == ' ' || keyEnd[-1] == '\t')) --keyEnd;
        *keyEnd = 0;
        char* value = equals + 1;
        while (*value == ' ' || *value == '\t') ++value;
        char* end = value + strlen(value);
        while (end > value && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
        if (*value == '"' && end > value + 1 && end[-1] == '"') { *--end = 0; ++value; }
        const char* key = p;
        static const char* const formats[] = {"raw", "img", "flat", "vhd", "vpc", "vmdk", "vmdk3", "vdi", "vhdx", nullptr};
        static const char* const cpus[] = {"i486dx4", "pentium", "pentium_mmx", "p2_klamath", "p3_katmai", "p4_willamette",
                                           "amd_k6_2_chomper", "athlon_xp", nullptr};
        if (Same(key, "profile")) Copy(s.profile, sizeof s.profile, value);
        else if (Same(key, "cpu")) {
            if (!OneOf(value, cpus)) { snprintf(error, errorCapacity, "line %d: unknown cpu '%s'", lineNumber, value); return false; }
            Copy(s.cpu, sizeof s.cpu, value);
        } else if (Same(key, "memory")) {
            s.memory = (unsigned)strtoul(value, nullptr, 10);
            if (s.memory < 1 || s.memory > 2048) { snprintf(error, errorCapacity, "line %d: memory must be 1..2048 MB", lineNumber); return false; }
        } else if (Same(key, "ips")) s.ips = strtoul(value, nullptr, 10);
        else if (Same(key, "sync")) Copy(s.sync, sizeof s.sync, value);
        else if (Same(key, "disk")) Copy(s.disk[0], sizeof s.disk[0], value);
        else if (Same(key, "disk2")) Copy(s.disk[1], sizeof s.disk[1], value);
        else if (Same(key, "disk_format") || Same(key, "disk2_format")) {
            if (!OneOf(value, formats)) { snprintf(error, errorCapacity, "line %d: unknown disk format '%s'", lineNumber, value); return false; }
            Copy(s.diskFormat[Same(key, "disk2_format") ? 1 : 0], sizeof s.diskFormat[0], value);
        } else if (Same(key, "cdrom")) Copy(s.cdrom, sizeof s.cdrom, value);
        else if (Same(key, "floppy")) Copy(s.floppy, sizeof s.floppy, value);
        else if (Same(key, "boot")) Copy(s.boot, sizeof s.boot, value);
        else if (Same(key, "com1")) Copy(s.com1, sizeof s.com1, value);
        else if (Same(key, "sound")) Copy(s.sound, sizeof s.sound, value);
        else if (Same(key, "net")) Copy(s.net, sizeof s.net, value);
        else if (Same(key, "reader")) Copy(s.reader, sizeof s.reader, value);
        else if (Same(key, "lang")) Copy(s.lang, sizeof s.lang, value);
        else if (Same(key, "log")) Copy(s.log, sizeof s.log, value);
        else if (Same(key, "debugcon")) s.debugcon = Same(value, "1") || Same(value, "yes") || Same(value, "on");
        else { snprintf(error, errorCapacity, "line %d: unknown key '%s'", lineNumber, key); return false; }
    }
    if (Same(s.lang, "pl")) polish = true;
    else if (Same(s.lang, "en")) polish = false;
    if (!s.disk[0][0] && !s.cdrom[0] && !s.floppy[0]) { snprintf(error, errorCapacity, "no disk, cdrom or floppy given"); return false; }

    Writer w{out, capacity, 0, false};
    out[0] = 0;
    w.Add("# generated by CharonPC from the profile %s\n", s.profile);
    w.Add("config_interface: textconfig\ndisplay_library: charon\n");
    w.Add("memory: guest=%u, host=%u\n", s.memory, s.memory);
    w.Add("romimage: file=rom:BIOS-bochs-latest\nvgaromimage: file=rom:VGABIOS-lgpl-latest.bin\n");
    w.Add("cpu: model=%s, count=1, ips=%lu, reset_on_triple_fault=1\n", s.cpu, s.ips);
    w.Add("clock: sync=%s, time0=local\n", s.sync);
    w.Add("pci: enabled=1, chipset=i440fx\n");
    w.Add("vga: extension=vbe, update_freq=15\n");
    w.Add("keyboard: type=mf, serial_delay=150, paste_delay=100000\n");
    w.Add("mouse: enabled=0\n");
    w.Add("log: -\npanic: action=fatal\nerror: action=report\ninfo: action=report\ndebug: action=ignore\n");
    w.Add("ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14\n");
    w.Add("ata1: enabled=1, ioaddr1=0x170, ioaddr2=0x370, irq=15\n");
    char path[300];
    if (s.disk[0][0]) {
        VolumePath(directory, s.disk[0], path, sizeof path);
        w.Add("ata0-master: type=disk, path=\"%s\", mode=%s\n", path, BochsDiskMode(s.diskFormat[0]));
    }
    if (s.disk[1][0]) {
        VolumePath(directory, s.disk[1], path, sizeof path);
        w.Add("ata0-slave: type=disk, path=\"%s\", mode=%s\n", path, BochsDiskMode(s.diskFormat[1]));
    }
    if (s.cdrom[0]) {
        VolumePath(directory, s.cdrom, path, sizeof path);
        w.Add("ata1-master: type=cdrom, path=\"%s\", status=inserted\n", path);
    }
    if (s.floppy[0]) {
        VolumePath(directory, s.floppy, path, sizeof path);
        w.Add("floppya: 1_44=\"%s\", status=inserted\n", path);
    }
    char boot[40];
    Copy(boot, sizeof boot, s.boot);
    for (char* p = boot; *p; ++p) if (*p == ' ') *p = ',';
    w.Add("boot: %s\n", boot);
    settings.bns = Same(s.com1, "bns");
    if (settings.bns) w.Add("com1: enabled=1, mode=file, dev=charon:bns\n");
    else if (!strncmp(s.com1, "file:", 5)) {
        VolumePath(directory, s.com1 + 5, path, sizeof path);
        w.Add("com1: enabled=1, mode=file, dev=\"%s\"\n", path);
    } else if (!Same(s.com1, "none")) { snprintf(error, errorCapacity, "com1 must be bns, none or file:<name>"); return false; }
    // Stage 1: SB16 and NE2000 are designed (PLAN.txt) but not built yet.
    if (!Same(s.sound, "none")) w.Add("# sound=%s is not available yet (CharonPC stage 1)\n", s.sound);
    if (!Same(s.net, "none")) w.Add("# net=%s is not available yet (CharonPC stage 1)\n", s.net);
    if (s.reader[0]) w.Add("# reader=%s\n", s.reader);
    settings.debugcon = s.debugcon;
    if (s.log[0]) VolumePath(directory, s.log, settings.logFile, sizeof settings.logFile);
    if (w.overflow) { snprintf(error, errorCapacity, "configuration too long"); return false; }
    return true;
}
