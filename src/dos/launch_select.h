// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS r409 - WINNT and LINUX: which installed system to start.
//
// Pure logic, shared by the shell (shell.cpp), both backends and the host
// tests: no firmware call, no file access, no library call. The backends only
// collect candidates (plat_launch_discover) and start the chosen one.
//
// Grammar, the same for both commands:
//   LINUX              start the default Linux; ask when there are several and
//                      no default
//   LINUX name         the system whose id or name starts with "name"
//   LINUX n            number n of the list
//   LINUX /L           list the installed systems with their numbers
//   LINUX /D name|n    remember that one as the default; /D alone forgets it
//
// Discovery order (numbers are stable: registry order, then firmware order,
// then scan order); duplicates - same partition and same path, letter case
// ignored - are merged into the earlier entry:
//   1. Charon's registry \EFI\HAK\systems.txt (the good names)
//   2. firmware entries BootOrder -> Boot####
//   3. a scan of every volume for \EFI\<dir>\ loaders
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { LAUNCH_WINDOWS = 1, LAUNCH_LINUX = 3 };          // = PL_WINNT, PL_LINUX
enum { LAUNCH_FROM_REGISTRY = 0, LAUNCH_FROM_FIRMWARE = 1, LAUNCH_FROM_SCAN = 2 };
enum { LAUNCH_MAX = 24 };

struct LaunchEntry {
    uint8_t kind;          // LAUNCH_WINDOWS / LAUNCH_LINUX
    uint8_t source;        // LAUNCH_FROM_*
    uint8_t merged;        // further systems behind the same loader (two Windows, one boot store)
    uint16_t disk;         // 1-based number of its disk, 0 = unknown
    char id[96];           // registry id, or "dir:<partition GUID>:<path>"
    char name[64];         // UTF-8
    char part[40];         // partition GUID text, lower case; "" = unknown
    char path[128];        // loader path on that volume, with a leading backslash
};
struct LaunchList { LaunchEntry e[LAUNCH_MAX]; int count; };

// ---- small text helpers (no libc) -------------------------------------------
inline size_t launch_len(const char* s) { size_t n = 0; while (s && s[n]) n++; return n; }
inline char launch_low(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
inline bool launch_ieq(const char* a, const char* b)
{
    size_t i = 0;
    for (; a[i] && launch_low(a[i]) == launch_low(b[i]); i++) {}
    return launch_low(a[i]) == launch_low(b[i]);
}
inline bool launch_istarts(const char* text, const char* prefix)
{
    for (size_t i = 0; prefix[i]; i++) if (launch_low(text[i]) != launch_low(prefix[i])) return false;
    return true;
}
inline bool launch_iends(const char* text, const char* suffix)
{
    const size_t n = launch_len(text), m = launch_len(suffix);
    return n >= m && launch_ieq(text + n - m, suffix);
}
inline void launch_copy(char* d, size_t cap, const char* s)
{
    size_t i = 0;
    if (!cap) return;
    for (; s && s[i] && i + 1 < cap; i++) d[i] = s[i];
    d[i] = 0;
}
inline void launch_cat(char* d, size_t cap, const char* s)
{
    const size_t n = launch_len(d);
    if (n < cap) launch_copy(d + n, cap - n, s);
}

// ---- classification -----------------------------------------------------------
inline bool launch_is_windows_path(const char* path) { return launch_iends(path, "\\EFI\\Microsoft\\Boot\\bootmgfw.efi"); }
// A firmware entry is a candidate only when it names a file that is not
// Charon, not CharonDOS and not a firmware application.
inline bool launch_firmware_wanted(const char* path)
{
    if (!path || !path[0]) return false;
    if (launch_istarts(path, "\\EFI\\HAK\\") || launch_istarts(path, "\\EFI\\CharonDOS\\")) return false;
    return launch_iends(path, ".efi");
}
// Folders of \EFI that the scan never treats as a Linux.
inline bool launch_scan_skips(const char* dir)
{
    return launch_ieq(dir, "Microsoft") || launch_ieq(dir, "HAK") || launch_ieq(dir, "CharonDOS") || launch_ieq(dir, "BOOT")
        || launch_ieq(dir, ".") || launch_ieq(dir, "..");
}
// Loader files of a folder, best first: shim is what the distribution
// registers and the only one that starts under Secure Boot.
inline const char* launch_loader_name(int rank)
{
    static const char* const names[] = {"shimx64.efi", "grubx64.efi", "systemd-bootx64.efi"};
    return (rank >= 0 && rank < 3) ? names[rank] : nullptr;
}
// Display name for a folder of \EFI (or a firmware description) without a
// registry line; the text itself when it is not in the table.
inline const char* launch_dir_name(const char* dir)
{
    static const char* const table[][2] = {
        {"debian", "Debian"}, {"ubuntu", "Ubuntu"}, {"fedora", "Fedora"}, {"arch", "Arch Linux"}, {"GRUB", "Arch Linux"},
        {"opensuse", "openSUSE"}, {"systemd", "Linux, systemd-boot"}, {"Linux", "Linux, unified kernel"},
    };
    // r410: the firmware's name of Windows' entry, compared in two pieces. Written out whole it stood in
    // CHARONDOS.EFI, and Charon's boot-manager detector, which looks for that very text in an image, named
    // CharonDOS's entry after Windows in the device list (tools/build-charondos.py now refuses such an image).
    if (launch_istarts(dir, "Windows Boot ") && launch_ieq(dir + 13, "Manager")) return "Windows";
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) if (launch_ieq(dir, table[i][0])) return table[i][1];
    return dir;
}

// ---- the list -----------------------------------------------------------------
inline void launch_clear(LaunchList* list) { list->count = 0; }
// Adds a candidate; a duplicate (same partition and path) is merged into the
// entry that is already there. Returns its index, -1 when the list is full or
// the candidate has no path.
inline int launch_add(LaunchList* list, const LaunchEntry* in)
{
    if (!in->path[0]) return -1;
    for (int i = 0; i < list->count; i++) {
        LaunchEntry* e = &list->e[i];
        if (!launch_ieq(e->path, in->path) || !launch_ieq(e->part, in->part)) continue;
        if (!e->disk) e->disk = in->disk;
        // Two registry systems behind one loader: two Windows that share one
        // boot store. One entry; the choice is made in Windows' own menu.
        if (e->source == LAUNCH_FROM_REGISTRY && in->source == LAUNCH_FROM_REGISTRY && !launch_ieq(e->id, in->id)) {
            if (e->merged < 255) e->merged++;
            launch_cat(e->name, sizeof e->name, ", ");
            launch_cat(e->name, sizeof e->name, in->name);
        }
        return i;
    }
    if (list->count >= LAUNCH_MAX) return -1;
    LaunchEntry* e = &list->e[list->count];
    *e = *in;
    e->merged = 0;
    if (!e->id[0]) {
        launch_copy(e->id, sizeof e->id, "dir:");
        launch_cat(e->id, sizeof e->id, e->part);
        launch_cat(e->id, sizeof e->id, ":");
        launch_cat(e->id, sizeof e->id, e->path);
    }
    if (!e->name[0]) launch_copy(e->name, sizeof e->name, e->kind == LAUNCH_WINDOWS ? "Windows" : "Linux");
    return list->count++;
}

// Takes entry `index` out of the list (a registry line whose loader is gone is
// not shown as installed).
inline void launch_remove(LaunchList* list, int index)
{
    if (index < 0 || index >= list->count) return;
    for (int i = index; i + 1 < list->count; i++) list->e[i] = list->e[i + 1];
    list->count--;
}

// Charon's registry: "# charon-systems 1", then lines
// id|name|kind|partition GUID|loader path|plan id|date. Lines of another kind
// (charondos) and faulty lines are skipped. Returns the entries added.
inline int launch_parse_registry(const char* text, size_t size, LaunchList* list)
{
    int added = 0;
    for (size_t at = 0; at < size;) {
        size_t end = at;
        while (end < size && text[end] != '\n') end++;
        size_t len = end - at;
        if (len && text[at + len - 1] == '\r') len--;
        const char* line = text + at;
        at = end + 1;
        if (!len || line[0] == '#') continue;
        LaunchEntry e;
        e.kind = 0; e.source = LAUNCH_FROM_REGISTRY; e.merged = 0; e.disk = 0;
        e.id[0] = e.name[0] = e.part[0] = e.path[0] = 0;
        char kind[16]; kind[0] = 0;
        char* const fields[5] = {e.id, e.name, kind, e.part, e.path};
        const size_t caps[5] = {sizeof e.id, sizeof e.name, sizeof kind, sizeof e.part, sizeof e.path};
        size_t p = 0;
        int f = 0;
        bool fits = true;
        for (; f < 5; f++) {
            size_t n = 0;
            while (p < len && line[p] != '|') {
                if (n + 1 < caps[f]) fields[f][n++] = line[p]; else fits = false;
                p++;
            }
            fields[f][n] = 0;
            if (p < len) p++; else { f++; break; }
        }
        if (f < 5 || !fits || !e.id[0] || !e.path[0]) continue;
        if (launch_ieq(kind, "windows")) e.kind = LAUNCH_WINDOWS;
        else if (launch_ieq(kind, "linux")) e.kind = LAUNCH_LINUX;
        else continue;
        for (size_t i = 0; e.part[i]; i++) e.part[i] = launch_low(e.part[i]);
        const int before = list->count;
        if (launch_add(list, &e) >= 0 && list->count > before) added++;
    }
    return added;
}

// A firmware boot entry (BootOrder -> Boot####): kept when it names a loader
// file on a disk partition that is not Charon or CharonDOS. Windows when the
// path is Windows Boot Manager's, else Linux; the entry's description is the
// name when the registry had none for that loader.
inline int launch_add_firmware(LaunchList* list, const char* description, const char* part, const char* path, uint16_t disk)
{
    if (!part || !part[0] || !launch_firmware_wanted(path)) return -1;
    LaunchEntry e;
    e.kind = launch_is_windows_path(path) ? (uint8_t)LAUNCH_WINDOWS : (uint8_t)LAUNCH_LINUX;
    e.source = LAUNCH_FROM_FIRMWARE; e.merged = 0; e.disk = disk; e.id[0] = 0;
    launch_copy(e.name, sizeof e.name, launch_dir_name(description && description[0] ? description : ""));
    launch_copy(e.part, sizeof e.part, part);
    for (size_t i = 0; e.part[i]; i++) e.part[i] = launch_low(e.part[i]);
    launch_copy(e.path, sizeof e.path, path);
    return launch_add(list, &e);
}

// One volume as the scan sees it; the backend supplies the two callbacks.
// Paths have a leading backslash.
struct LaunchVolume {
    void* context;
    const char* part;      // partition GUID text
    uint16_t disk;
    bool (*file)(void* context, const char* path, uint32_t* size);     // true: a file is there
    // Names inside a folder - sub-folders (folders = true) or files; returns
    // how many were stored (at most cap).
    int (*names)(void* context, const char* folder, bool folders, char (*out)[64], int cap);
};
// Scan of one volume: Windows Boot Manager; then every folder \EFI\<dir>\
// except Microsoft, HAK, CharonDOS and BOOT, taking the first that exists of
// shimx64.efi, grubx64.efi, systemd-bootx64.efi; unified kernels
// \EFI\Linux\*.efi; and \EFI\BOOT\BOOTX64.EFI only when nothing else is known
// on that volume and the file is not Charon, CharonDOS or Windows (same size
// as \EFI\HAK\HAKBOOT.EFI, \EFI\CharonDOS\CHARONDOS.EFI or bootmgfw.efi).
inline void launch_scan_volume(const LaunchVolume* v, LaunchList* list)
{
    LaunchEntry e;
    e.source = LAUNCH_FROM_SCAN; e.merged = 0; e.disk = v->disk;
    launch_copy(e.part, sizeof e.part, v->part);
    for (size_t i = 0; e.part[i]; i++) e.part[i] = launch_low(e.part[i]);
    uint32_t windows_size = 0;
    const bool windows = v->file(v->context, "\\EFI\\Microsoft\\Boot\\bootmgfw.efi", &windows_size);
    if (windows) {
        e.kind = LAUNCH_WINDOWS; e.id[0] = 0;
        launch_copy(e.name, sizeof e.name, "Windows");
        launch_copy(e.path, sizeof e.path, "\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
        launch_add(list, &e);
    }
    static char dirs[32][64];
    const int n = v->names(v->context, "\\EFI", true, dirs, 32);
    for (int d = 0; d < n; d++) {
        if (launch_scan_skips(dirs[d])) continue;
        char folder[100];
        launch_copy(folder, sizeof folder, "\\EFI\\");
        launch_cat(folder, sizeof folder, dirs[d]);
        if (launch_ieq(dirs[d], "Linux")) {
            static char files[16][64];
            const int f = v->names(v->context, folder, false, files, 16);
            for (int k = 0; k < f; k++) {
                if (!launch_iends(files[k], ".efi")) continue;
                e.kind = LAUNCH_LINUX; e.id[0] = 0;
                launch_copy(e.name, sizeof e.name, "Linux, ");
                launch_cat(e.name, sizeof e.name, files[k]);
                launch_copy(e.path, sizeof e.path, folder);
                launch_cat(e.path, sizeof e.path, "\\");
                launch_cat(e.path, sizeof e.path, files[k]);
                launch_add(list, &e);
            }
            continue;
        }
        for (int rank = 0; launch_loader_name(rank); rank++) {
            char path[128];
            launch_copy(path, sizeof path, folder);
            launch_cat(path, sizeof path, "\\");
            launch_cat(path, sizeof path, launch_loader_name(rank));
            uint32_t size = 0;
            if (!v->file(v->context, path, &size)) continue;
            e.kind = LAUNCH_LINUX; e.id[0] = 0;
            launch_copy(e.name, sizeof e.name, launch_dir_name(dirs[d]));
            launch_copy(e.path, sizeof e.path, path);
            launch_add(list, &e);
            break;
        }
    }
    for (int i = 0; i < list->count; i++) if (launch_ieq(list->e[i].part, e.part)) return;   // something is known here
    uint32_t size = 0, other = 0;
    if (!v->file(v->context, "\\EFI\\BOOT\\BOOTX64.EFI", &size)) return;
    if (v->file(v->context, "\\EFI\\HAK\\HAKBOOT.EFI", &other) && other == size) return;
    // Charon's own start medium (its disc or stick): Charon is \EFI\BOOT\BOOTX64.EFI there and has no
    // HAKBOOT.EFI to compare with; the reader add-on next to it says whose medium this is. (Seen in the
    // VMware run of r409: with Charon's disc left in the drive, LINUX offered it as a second "Linux".)
    if (v->file(v->context, "\\EFI\\HAK\\CHARONVOICE.EFI", &other) || v->file(v->context, "\\EFI\\HAK\\AudioDxe.efi", &other)) return;
    if (v->file(v->context, "\\EFI\\CharonDOS\\CHARONDOS.EFI", &other) && other == size) return;
    if (windows && windows_size == size) return;
    e.kind = LAUNCH_LINUX; e.id[0] = 0;
    launch_copy(e.name, sizeof e.name, "Linux");
    launch_copy(e.path, sizeof e.path, "\\EFI\\BOOT\\BOOTX64.EFI");
    launch_add(list, &e);
}

// Index in the list of number `n` (from 1) among the entries of `kind`; -1.
inline int launch_nth(const LaunchList* list, int kind, int n)
{
    for (int i = 0; i < list->count; i++) if (list->e[i].kind == kind && --n == 0) return i;
    return -1;
}
inline int launch_count(const LaunchList* list, int kind)
{
    int n = 0;
    for (int i = 0; i < list->count; i++) if (list->e[i].kind == kind) n++;
    return n;
}
// The entry an argument names: a number of the list, an exact id, else the
// first whose id or name starts with the text. -1 = none.
inline int launch_find(const LaunchList* list, int kind, const char* arg)
{
    if (!arg || !arg[0]) return -1;
    bool digits = true;
    int number = 0;
    for (size_t i = 0; arg[i]; i++) {
        if (arg[i] < '0' || arg[i] > '9' || i > 3) { digits = false; break; }
        number = number * 10 + (arg[i] - '0');
    }
    if (digits) return launch_nth(list, kind, number);
    for (int i = 0; i < list->count; i++) if (list->e[i].kind == kind && launch_ieq(list->e[i].id, arg)) return i;
    for (int i = 0; i < list->count; i++)
        if (list->e[i].kind == kind && (launch_istarts(list->e[i].id, arg) || launch_istarts(list->e[i].name, arg))) return i;
    return -1;
}

// ---- the command line -----------------------------------------------------------
enum { LAUNCH_START = 0, LAUNCH_LIST = 1, LAUNCH_SET_DEFAULT = 2, LAUNCH_BAD_ARGS = 3 };
struct LaunchArgs { int mode; char text[96]; };
inline void launch_parse_args(const char* args, LaunchArgs* out)
{
    out->mode = LAUNCH_START; out->text[0] = 0;
    while (*args == ' ' || *args == '\t') args++;
    if (args[0] == '/') {
        const char c = launch_low(args[1]);
        if (c == 'l' && (!args[2] || args[2] == ' ' || args[2] == '\t')) out->mode = LAUNCH_LIST;
        else if (c == 'd' && (!args[2] || args[2] == ' ' || args[2] == '\t')) out->mode = LAUNCH_SET_DEFAULT;
        else { out->mode = LAUNCH_BAD_ARGS; return; }
        args += 2;
        while (*args == ' ' || *args == '\t') args++;
        if (out->mode == LAUNCH_LIST && *args) { out->mode = LAUNCH_BAD_ARGS; return; }
    }
    launch_copy(out->text, sizeof out->text, args);
    size_t n = launch_len(out->text);
    while (n && (out->text[n - 1] == ' ' || out->text[n - 1] == '\t' || out->text[n - 1] == '\r' || out->text[n - 1] == '\n')) out->text[--n] = 0;
}

// What a start request leads to. Errorlevels: 2 = none installed or no such
// name, 3 = several and no choice was made (Escape, or a batch file).
enum { LAUNCH_GO = 0, LAUNCH_NONE = 1, LAUNCH_NO_MATCH = 2, LAUNCH_ASK = 3, LAUNCH_NO_CHOICE = 4 };
// `interactive` false: a batch file is running or the keyboard is redirected;
// the choice is then never shown, so AUTOEXEC.BAT cannot hang.
inline int launch_choose(const LaunchList* list, int kind, const char* arg, const char* default_id, bool interactive, int* index)
{
    const int n = launch_count(list, kind);
    *index = -1;
    if (!n) return LAUNCH_NONE;
    if (arg && arg[0]) {
        *index = launch_find(list, kind, arg);
        return *index >= 0 ? LAUNCH_GO : LAUNCH_NO_MATCH;
    }
    if (n == 1) { *index = launch_nth(list, kind, 1); return LAUNCH_GO; }
    if (default_id && default_id[0]) {
        for (int i = 0; i < list->count; i++)
            if (list->e[i].kind == kind && launch_ieq(list->e[i].id, default_id)) { *index = i; return LAUNCH_GO; }
        // A default that is no longer installed is as good as none.
    }
    return interactive ? LAUNCH_ASK : LAUNCH_NO_CHOICE;
}

// ---- BOOT.CFG next to CHARONDOS.EFI: "linux=<id>", "winnt=<id>" ------------------
inline const char* launch_cfg_key(int kind) { return kind == LAUNCH_WINDOWS ? "winnt" : "linux"; }
inline bool launch_cfg_get(const char* text, size_t size, const char* key, char* out, size_t cap)
{
    const size_t k = launch_len(key);
    if (cap) out[0] = 0;
    for (size_t at = 0; at < size;) {
        size_t end = at;
        while (end < size && text[end] != '\n') end++;
        size_t len = end - at;
        if (len && text[at + len - 1] == '\r') len--;
        const char* line = text + at;
        at = end + 1;
        if (len > k && line[k] == '=' && launch_istarts(line, key)) {
            size_t n = 0;
            for (size_t i = k + 1; i < len && n + 1 < cap; i++) out[n++] = line[i];
            out[n] = 0;
            return true;
        }
    }
    return false;
}
// The file with `key` set to `value` (null or "" removes the line); every
// other line is kept as it is. Returns the new length, or -1 when it does not fit.
inline long launch_cfg_set(const char* text, size_t size, const char* key, const char* value, char* out, size_t cap)
{
    const size_t k = launch_len(key);
    size_t n = 0;
    for (size_t at = 0; at < size;) {
        size_t end = at;
        while (end < size && text[end] != '\n') end++;
        size_t len = end - at;
        if (len && text[at + len - 1] == '\r') len--;
        const char* line = text + at;
        at = end + 1;
        if (!len || (len > k && line[k] == '=' && launch_istarts(line, key))) continue;
        if (n + len + 2 > cap) return -1;
        for (size_t i = 0; i < len; i++) out[n++] = line[i];
        out[n++] = '\r'; out[n++] = '\n';
    }
    if (value && value[0]) {
        const size_t v = launch_len(value);
        if (n + k + 1 + v + 2 > cap) return -1;
        for (size_t i = 0; i < k; i++) out[n++] = key[i];
        out[n++] = '=';
        for (size_t i = 0; i < v; i++) out[n++] = value[i];
        out[n++] = '\r'; out[n++] = '\n';
    }
    return (long)n;
}
