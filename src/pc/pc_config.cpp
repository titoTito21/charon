// CharonPC - CHARONPC.CFG to Bochs configuration. GPL-2.0-or-later.
// The format is described in src/pc/PLAN.txt ("CHARONPC.CFG"). Unknown keys
// are an error, so a typing mistake in a profile is reported, not ignored.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_glue.h"
#include "pc_platform.h"

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
    char cpu[40] = "";         // r407: empty = by profile (Pentium III; Windows 95: Pentium II, see below)
    unsigned memory = 64;
    unsigned long ips = 0;     // r407: 0 = measured on this computer (HostIps)
    char sync[16] = "slowdown";   // r407: not "both": Bochs catching up with real time made the sound card run fast (cut sound)
    char disk[2][260] = {};
    char diskFormat[2][16] = {{"raw"}, {"raw"}};
    char cdrom[260] = {};
    char floppy[260] = {};
    char boot[40] = "disk";
    char com1[260] = "bns";
    char sound[16] = "none";
    unsigned soundDebug = 0;   // r407: sounddebug=1..5, the SB16's own log lines in the Bochs log
    char net[16] = "none";
    char reader[40] = "";
    char lang[8] = "";
    char log[260] = "";
    bool debugcon = false;
    bool debugserial = false;
    unsigned long diskCreateMb = 0;   // r406: the installer's profile asks for an empty disk of this size
    char state[24] = "";              // r406: install | no-media | installed (written by the installer)
    pc::SetupAnswers answers = {};    // r407: Setup without questions
    bool setupAuto = true;
    char readerSetup[260] = "";
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

// r407: how many emulated instructions per second this computer manages. Bochs slows the machine down
// to "ips" and its devices (the sound card's clock) count in it, so a value above what the computer
// can do gives sound with gaps, and a value far below wastes the computer. A short fixed piece of work
// of the interpreter's kind (table look-ups, data-dependent branches) is timed; the factor comes from
// the trial of 2026-11-21 (a computer where Bochs ran 150 million instructions per second scored
// hostScoreAt150), and two thirds of the result are used.
namespace {
constexpr unsigned long hostScoreAt150 = 2330;   // the trial computer scored about 1630: 70 million there
unsigned long HostIps()
{
    static uint32_t table[4096];
    uint32_t x = 2463534242u, sum = 0;
    for (unsigned i = 0; i < 4096; ++i) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; table[i] = x; }
    const uint64_t start = pc_usec();
    unsigned long rounds = 0;
    while (pc_usec() - start < 60000) {
        for (unsigned i = 0; i < 20000; ++i) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            const uint32_t v = table[x & 4095];
            if (v & 1) sum += v >> 3; else sum ^= v;
            table[(x >> 12) & 4095] = sum + i;
        }
        ++rounds;
    }
    char line[100];
    const int n = snprintf(line, sizeof line, "CharonPC speed: host score %lu (check %u)\n", rounds, (unsigned)(sum & 0xFF));
    if (n > 0) pc_log_write(line, (size_t)n);
    if (!hostScoreAt150 || !rounds) return 70000000;
    unsigned long long ips = 100000000ull * rounds / hostScoreAt150;   // two thirds of 150 million at the reference score
    if (ips < 20000000) ips = 20000000;
    if (ips > 400000000) ips = 400000000;
    return (unsigned long)ips;
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
        else if (Same(key, "sounddebug")) s.soundDebug = (unsigned)strtoul(value, nullptr, 10) % 6;
        else if (Same(key, "net")) Copy(s.net, sizeof s.net, value);
        else if (Same(key, "reader")) Copy(s.reader, sizeof s.reader, value);
        else if (Same(key, "lang")) Copy(s.lang, sizeof s.lang, value);
        else if (Same(key, "log")) Copy(s.log, sizeof s.log, value);
        else if (Same(key, "debugcon")) s.debugcon = Same(value, "1") || Same(value, "yes") || Same(value, "on");
        else if (Same(key, "debugserial")) s.debugserial = Same(value, "1") || Same(value, "yes") || Same(value, "on");
        else if (Same(key, "disk_create_mb")) {
            s.diskCreateMb = strtoul(value, nullptr, 10);
            if (s.diskCreateMb > 4095) { snprintf(error, errorCapacity, "line %d: disk_create_mb must be at most 4095 (FAT32 file limit)", lineNumber); return false; }
        } else if (Same(key, "state")) Copy(s.state, sizeof s.state, value);
        else if (Same(key, "reader_setup")) Copy(s.readerSetup, sizeof s.readerSetup, value);
        else if (Same(key, "reader_lead")) settings.readerLead = Same(value, "1") || Same(value, "yes") || Same(value, "on");
        else if (Same(key, "setup_auto")) s.setupAuto = Same(value, "1") || Same(value, "yes") || Same(value, "on");
        else if (Same(key, "setup_key")) Copy(s.answers.key, sizeof s.answers.key, value);
        else if (Same(key, "setup_name")) Copy(s.answers.name, sizeof s.answers.name, value);
        else if (Same(key, "setup_org")) Copy(s.answers.org, sizeof s.answers.org, value);
        else if (Same(key, "setup_computer")) Copy(s.answers.computer, sizeof s.answers.computer, value);
        else if (Same(key, "setup_zone")) Copy(s.answers.zone, sizeof s.answers.zone, value);
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
    // r407: the fastest processor the period knew, as fast as this computer can emulate it. Windows 95
    // fails on fast processors (its network driver's timing loop: "Windows protection error"), so that
    // profile keeps the Pentium II at the old speed unless the profile file says otherwise.
    settings.debugcon = s.debugcon; settings.debugserial = s.debugserial;   // the lines below are to be seen in the log
    const bool win95 = Same(s.profile, "WIN95");
    if (!s.cpu[0]) Copy(s.cpu, sizeof s.cpu, win95 ? "p2_klamath" : "p3_katmai");
    if (!s.ips) s.ips = win95 ? 60000000 : HostIps();
    if (s.ips < 1000000) s.ips = 1000000;
    {
        char line[100];
        const int n = snprintf(line, sizeof line, "CharonPC speed: cpu %s, %lu instructions per second\n", s.cpu, s.ips);
        if (n > 0) pc_log_write(line, (size_t)n);
    }
    w.Add("cpu: model=%s, count=1, ips=%lu, reset_on_triple_fault=1\n", s.cpu, s.ips);
    w.Add("clock: sync=%s, time0=local\n", s.sync);
    w.Add("pci: enabled=1, chipset=i440fx\n");
    w.Add("vga: extension=vbe, update_freq=15\n");
    w.Add("keyboard: type=mf, serial_delay=150, paste_delay=100000\n");
    w.Add("mouse: enabled=1, type=ps2\n");   // r407: only so that CharonPC can park the pointer away from the text
    w.Add("log: -\npanic: action=fatal\nerror: action=report\ninfo: action=report\ndebug: action=ignore\n");
    w.Add("ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14\n");
    w.Add("ata1: enabled=1, ioaddr1=0x170, ioaddr2=0x370, irq=15\n");
    char path[300];
    if (s.disk[0][0]) {
        VolumePath(directory, s.disk[0], path, sizeof path);
        // r406: the first start of an installer-made profile makes its empty hard disk.
        if (s.diskCreateMb && Same(BochsDiskMode(s.diskFormat[0]), "flat")) {
            void* file = nullptr; uint64_t size = 0;
            if (pc_vol_open(path, 0, 0, 0, &file, &size) == 0) pc_vol_close(file);
            else {
                Say(polish ? "Tworzę pusty dysk profilu. To potrwa chwilę." : "Making the profile's empty disk. This takes a moment.");
                if (pc_vol_open(path, 1, 1, 0, &file, &size) != 0) { snprintf(error, errorCapacity, "cannot create the disk file %s", s.disk[0]); return false; }
                const int failed = pc_vol_set_size(file, (uint64_t)s.diskCreateMb << 20);
                pc_vol_close(file);
                if (failed) { pc_vol_delete(path); snprintf(error, errorCapacity, "no room for a disk of %lu MB on this volume", s.diskCreateMb); return false; }
            }
        }
        // r407: Bochs leaves "<image>.lock" behind when the machine is switched off without a shutdown, and
        // then refuses the disk ("image locked") at every later start. Nothing else can hold the image
        // here (one program runs at a time), so a lock found at the start is always a stale one.
        {
            char lock[320];
            snprintf(lock, sizeof lock, "%s.lock", path);
            if (pc_vol_delete(lock) == 0) pc_log_write("CharonPC: a stale disk lock was removed\n", 40);
        }
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
    settings.debugcon = s.debugcon;
    settings.debugserial = s.debugserial;
    settings.installing = Same(s.state, "install");
    settings.diskFile[0] = 0;
    if (s.disk[0][0] && Same(BochsDiskMode(s.diskFormat[0]), "flat")) VolumePath(directory, s.disk[0], settings.diskFile, sizeof settings.diskFile);
    // r407: an installing profile with the user's disc and an untouched disk is prepared for Setup
    // without questions; SETUP.IMG then starts the machine until the disk itself starts Windows.
    settings.floppySetup = false;
    settings.autoSetup = false;
    settings.cdromFile[0] = 0;
    if (s.cdrom[0]) VolumePath(directory, s.cdrom, settings.cdromFile, sizeof settings.cdromFile);
    Copy(settings.reader, sizeof settings.reader, s.reader);
    if (settings.installing && s.setupAuto && s.cdrom[0] && !s.floppy[0] && settings.diskFile[0]) {
        char iso[300], floppy[300], reader[300] = "";
        VolumePath(directory, s.cdrom, iso, sizeof iso);
        VolumePath(directory, "SETUP.IMG", floppy, sizeof floppy);
        if (s.readerSetup[0]) VolumePath(directory, s.readerSetup, reader, sizeof reader);
        s.answers.sound = Same(s.sound, "sb16");   // r407: Setup gets the Sound Blaster as a device to install
        if (PrepareUnattended(iso, floppy, settings.diskFile, s.answers, reader)) {
            Copy(s.floppy, sizeof s.floppy, "SETUP.IMG");
            settings.floppySetup = true;
            settings.autoSetup = true;
            Copy(s.boot, sizeof s.boot, DiskStartable() ? "disk" : "floppy,disk");
        }
    }
    // r407: a system that is already installed gets the profile's screen reader too: its installer is
    // put on a disc of its own once (READER.ISO, the second CD drive) and the first desktop starts it
    // from there (pc_reader.cpp). The disc stays in the drive afterwards, so drive letters do not move.
    settings.readerRun[0] = 0;
    if (s.readerSetup[0] && settings.diskFile[0]) {
        char reader[300], disc[300], name[16];
        VolumePath(directory, s.readerSetup, reader, sizeof reader);
        VolumePath(directory, "READER.ISO", disc, sizeof disc);
        void* file = nullptr; uint64_t size = 0;
        bool have = pc_vol_open(disc, 0, 0, 0, &file, &size) == 0;
        if (have) pc_vol_close(file);
        else if (GuestHasReader(settings.diskFile, reader) == 0 && MakeReaderDisc(reader, disc, name, sizeof name)) {
            have = true;
            // one hard disk volume is C:, the profile's own disc (when there is one) D:, this disc after it
            snprintf(settings.readerRun, sizeof settings.readerRun, "%c:\\%s", s.cdrom[0] ? 'E' : 'D', name);
        }
        if (have) w.Add("ata1-slave: type=cdrom, path=\"%s\", status=inserted\n", disc);
    }
    if (s.floppy[0]) {
        VolumePath(directory, s.floppy, path, sizeof path);
        w.Add("floppya: 1_44=\"%s\", status=inserted\n", path);
    }
    char boot[40];
    Copy(boot, sizeof boot, s.boot);
    // r406: an installing profile restarts into its new hard disk when the disc declines to boot.
    if (Same(boot, "cdrom") && s.disk[0][0]) Copy(boot, sizeof boot, "cdrom,disk");
    for (char* p = boot; *p; ++p) if (*p == ' ') *p = ',';
    w.Add("boot: %s\n", boot);
    // r407: JAWS speaks with its own Eloquence through the emulated sound card. With a synthesizer on
    // COM1 as well, JAWS 4.02 stops the whole guest at its start (seen in the trials), so a JAWS
    // profile has no Braille 'n Speak, whatever an older profile file says.
    if (!strncmp(s.reader, "jfw", 3) && Same(s.com1, "bns") && Same(s.sound, "sb16")) Copy(s.com1, sizeof s.com1, "none");
    settings.bns = Same(s.com1, "bns");
    if (settings.bns) w.Add("com1: enabled=1, mode=file, dev=charon:bns\n");
    else if (!strncmp(s.com1, "file:", 5)) {
        VolumePath(directory, s.com1 + 5, path, sizeof path);
        w.Add("com1: enabled=1, mode=file, dev=\"%s\"\n", path);
    } else if (!Same(s.com1, "none")) { snprintf(error, errorCapacity, "com1 must be bns, none or file:<name>"); return false; }
    // r407: sound=sb16 is a Sound Blaster 16 (port 220h, IRQ 5, DMA 1 and 5, MPU-401 at 330h, FM at 388h)
    // and the PC speaker, both through the sound driver "charon" (pc_sound.cc) to Charon's audio.
    if (Same(s.sound, "sb16")) {
        w.Add("sound: driver=charon\n");
        // loglevel only counts with a log name; the lines go to the Bochs log (tools/build-charon-bochs.py), no file is made
        if (s.soundDebug) w.Add("sb16: enabled=1, midimode=0, wavemode=1, dmatimer=1000000, log=bochs, loglevel=%u\n", s.soundDebug);
        else w.Add("sb16: enabled=1, midimode=0, wavemode=1, dmatimer=1000000\n");
        w.Add("speaker: enabled=1, mode=sound, volume=10\n");
    } else if (!Same(s.sound, "none")) { snprintf(error, errorCapacity, "sound must be sb16 or none"); return false; }
    // NE2000 is designed (PLAN.txt) but not built yet.
    if (!Same(s.net, "none")) w.Add("# net=%s is not available yet (CharonPC stage 1)\n", s.net);
    if (s.reader[0]) w.Add("# reader=%s\n", s.reader);
    if (s.log[0]) VolumePath(directory, s.log, settings.logFile, sizeof settings.logFile);
    if (w.overflow) { snprintf(error, errorCapacity, "configuration too long"); return false; }
    return true;
}
