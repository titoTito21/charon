// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - host backend (Windows), used only by the tests in tests/dos.
//
//   charondos-host --root C=DIR [--root D=DIR] [--pl] [-c "COMMAND"]
//                  [--keys "text{ENTER}{UP}{F1}{CTRLC}..."] [--rowlog FILE]
//                  [--dump-screen]
// Teletype output goes to stdout as UTF-8. Cell output (redrawn rows) goes
// to the row log. --dump-screen prints the final text screen, one line per
// row, prefixed with "SCREEN|". Without --keys and -c the real keyboard is
// used.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include "dos_platform.h"
#include "charon_cmd.h"

extern uint8_t* g_mem;
extern const uint16_t g_cp437[256];

static wchar_t g_roots[26][MAX_PATH];
static PlatKey g_keys[4096];
static int g_nkeys, g_key_pos;
static bool g_scripted, g_interactive;
static FILE* g_rowlog;
static int g_cx, g_cy;

static void put_utf8(FILE* f, unsigned u)
{
    if (u < 0x80) fputc((int)u, f);
    else if (u < 0x800) { fputc((int)(0xC0 | (u >> 6)), f); fputc((int)(0x80 | (u & 0x3F)), f); }
    else { fputc((int)(0xE0 | (u >> 12)), f); fputc((int)(0x80 | ((u >> 6) & 0x3F)), f); fputc((int)(0x80 | (u & 0x3F)), f); }
}

static FILE* g_inttrace;
void plat_trace_int(uint8_t number, const uint16_t* before, uint16_t ax_after, bool carry)
{
    if (g_inttrace) fprintf(g_inttrace, "INT %02X AX=%04X BX=%04X CX=%04X DX=%04X -> AX=%04X%s\n", number, before[0], before[1], before[2], before[3], ax_after, carry ? " CF" : "");
}
void plat_con_size(int* cols, int* rows) { *cols = 80; *rows = 25; }
void plat_con_clear(uint8_t attr) { if (g_rowlog) fprintf(g_rowlog, "CLEAR %02X\n", attr); g_cx = g_cy = 0; }
void plat_con_goto(int col, int row) { g_cx = col; g_cy = row; }
void plat_con_out(const dchar16* text, int count, uint8_t attr, bool tty)
{
    if (tty) {
        for (int i = 0; i < count; i++) put_utf8(stdout, text[i]);
        fflush(stdout);
        if (g_rowlog) {
            fprintf(g_rowlog, "TTY %d %d %02X ", g_cy, g_cx, attr);
            for (int i = 0; i < count; i++) { if (text[i] == '\r') fputs("\\r", g_rowlog); else if (text[i] == '\n') fputs("\\n", g_rowlog); else put_utf8(g_rowlog, text[i]); }
            fputc('\n', g_rowlog);
        }
        return;
    }
    if (g_rowlog) {
        fprintf(g_rowlog, "CELLS %d %d %02X ", g_cy, g_cx, attr);
        for (int i = 0; i < count; i++) put_utf8(g_rowlog, text[i]);
        fputc('\n', g_rowlog);
    }
}
void plat_con_show_cursor(bool visible) { if (g_rowlog) fprintf(g_rowlog, "CURSOR %d\n", visible ? 1 : 0); }

bool plat_key_poll(PlatKey* key)
{
    if (g_scripted) {
        if (g_key_pos >= g_nkeys) return false;
        *key = g_keys[g_key_pos++];
        return true;
    }
    if (!g_interactive || !_kbhit()) return false;
    key->ch = 0; key->code = PK_NONE; key->mods = 0;
    const wint_t c = _getwch();
    if (c == 0 || c == 0xE0) {
        switch (_getwch()) {
        case 72: key->code = PK_UP; break;
        case 80: key->code = PK_DOWN; break;
        case 75: key->code = PK_LEFT; break;
        case 77: key->code = PK_RIGHT; break;
        case 71: key->code = PK_HOME; break;
        case 79: key->code = PK_END; break;
        case 73: key->code = PK_PGUP; break;
        case 81: key->code = PK_PGDN; break;
        case 82: key->code = PK_INS; break;
        case 83: key->code = PK_DEL; break;
        default: return false;
        }
    } else if (c == 27) key->code = PK_ESC;
    else key->ch = (uint16_t)c;
    return true;
}
bool plat_quit(void) { return g_scripted ? g_key_pos >= g_nkeys : !g_interactive; }
void plat_idle(unsigned ms) { if (!g_scripted && g_interactive && ms) Sleep(ms); }
uint32_t plat_millis(void) { return GetTickCount(); }
static PlatTime g_rtc;        // r409: --rtc "YYYY-MM-DD HH:MM:SS" stands in for the hardware clock
static bool g_have_rtc;
void plat_time(PlatTime* out)
{
    if (g_have_rtc) { *out = g_rtc; charon_clock_local(out); return; }   // as the UEFI backend: clock, then \EFI\HAK\clock.txt
    SYSTEMTIME t;
    GetLocalTime(&t);
    out->year = t.wYear; out->month = (uint8_t)t.wMonth; out->day = (uint8_t)t.wDay;
    out->hour = (uint8_t)t.wHour; out->minute = (uint8_t)t.wMinute; out->second = (uint8_t)t.wSecond;
    out->hsec = (uint8_t)(t.wMilliseconds / 10);
}
void* plat_alloc(size_t bytes) { return calloc(1, bytes); }

// ---- sound (r407): --pcm FILE collects what would be played, as raw 48 kHz stereo 16 bit ----
static FILE* g_pcm;
bool plat_pcm_begin(void) { return g_pcm != nullptr; }
size_t plat_pcm_write(const int16_t* stereo, size_t frames) { return g_pcm ? fwrite(stereo, 4, frames, g_pcm) : 0; }
size_t plat_pcm_queued(void) { return 0; }
void plat_pcm_end(void) { if (g_pcm) fflush(g_pcm); }

// ---- files ----------------------------------------------------------------------------------
static const wchar_t* full(int drive, const dchar16* path)
{
    static wchar_t buffer[4][1024];
    static int turn;
    wchar_t* b = buffer[turn++ & 3];
    wcscpy(b, g_roots[drive]);
    wcsncat(b, (const wchar_t*)path, 1023 - wcslen(b));
    return b;
}
static int win_error(void)
{
    switch (GetLastError()) {
    case ERROR_FILE_NOT_FOUND: return 2;
    case ERROR_PATH_NOT_FOUND: return 3;
    default: return 5;
    }
}
static void fill(PlatDirEnt* out, const WIN32_FIND_DATAW* d)
{
    size_t i = 0;
    for (; d->cFileName[i] && i < 255; i++) out->name[i] = d->cFileName[i];
    out->name[i] = 0;
    out->size = d->nFileSizeHigh ? 0xFFFFFFFFu : d->nFileSizeLow;
    out->attr = (uint8_t)(d->dwFileAttributes & 0x37);
    FILETIME local;
    WORD date = 0x21, time = 0;
    FileTimeToLocalFileTime(&d->ftLastWriteTime, &local);
    FileTimeToDosDateTime(&local, &date, &time);
    out->date = date; out->time = time;
}

bool plat_drive_ok(int drive) { return drive >= 0 && drive < 26 && g_roots[drive][0]; }
int plat_open(int drive, const dchar16* path, int flags, void** file)
{
    const int mode = flags & PO_MODE;
    const DWORD access = mode == PO_READ ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    DWORD disposition = OPEN_EXISTING;
    if (flags & PO_CREATE) disposition = (flags & PO_TRUNC) ? CREATE_ALWAYS : OPEN_ALWAYS;
    const HANDLE h = CreateFileW(full(drive, path), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return win_error();
    *file = h;
    return 0;
}
long plat_read(void* file, void* buffer, uint32_t count)
{
    DWORD n = 0;
    return ReadFile((HANDLE)file, buffer, count, &n, nullptr) ? (long)n : -1;
}
long plat_write(void* file, const void* buffer, uint32_t count)
{
    DWORD n = 0;
    return WriteFile((HANDLE)file, buffer, count, &n, nullptr) ? (long)n : -1;
}
bool plat_seek(void* file, uint32_t position)
{
    LONG high = 0;
    return SetFilePointer((HANDLE)file, (LONG)position, &high, FILE_BEGIN) != INVALID_SET_FILE_POINTER || GetLastError() == NO_ERROR;
}
uint32_t plat_size(void* file) { return GetFileSize((HANDLE)file, nullptr); }
bool plat_truncate(void* file, uint32_t size) { return plat_seek(file, size) && SetEndOfFile((HANDLE)file); }
bool plat_set_ftime(void* file, uint16_t date, uint16_t time)
{
    FILETIME local, utc;
    if (!DosDateTimeToFileTime(date, time, &local) || !LocalFileTimeToFileTime(&local, &utc)) return false;
    return SetFileTime((HANDLE)file, nullptr, nullptr, &utc) != 0;
}
void plat_close(void* file) { CloseHandle((HANDLE)file); }
bool plat_stat(int drive, const dchar16* path, PlatDirEnt* out)
{
    if (!path[0]) return false;
    WIN32_FIND_DATAW d;
    const HANDLE h = FindFirstFileW(full(drive, path), &d);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    fill(out, &d);
    return true;
}
int plat_delete(int drive, const dchar16* path) { return DeleteFileW(full(drive, path)) ? 0 : win_error(); }
int plat_mkdir(int drive, const dchar16* path) { return CreateDirectoryW(full(drive, path), nullptr) ? 0 : win_error(); }
int plat_rmdir(int drive, const dchar16* path) { return RemoveDirectoryW(full(drive, path)) ? 0 : win_error(); }
int plat_rename(int drive, const dchar16* from, const dchar16* to) { return MoveFileW(full(drive, from), full(drive, to)) ? 0 : win_error(); }
bool plat_set_attr(int drive, const dchar16* path, uint8_t attr)
{
    DWORD a = attr & 0x27;
    if (!a) a = FILE_ATTRIBUTE_NORMAL;
    return SetFileAttributesW(full(drive, path), a) != 0;
}
struct HostDir { HANDLE h; WIN32_FIND_DATAW d; bool pending; };
void* plat_opendir(int drive, const dchar16* path)
{
    wchar_t pattern[1024];
    wcscpy(pattern, full(drive, path));
    wcscat(pattern, L"\\*");
    HostDir* dir = (HostDir*)calloc(1, sizeof(HostDir));
    dir->h = FindFirstFileW(pattern, &dir->d);
    if (dir->h == INVALID_HANDLE_VALUE) { free(dir); return nullptr; }
    dir->pending = true;
    return dir;
}
bool plat_readdir(void* handle, PlatDirEnt* out)
{
    HostDir* dir = (HostDir*)handle;
    if (!dir->pending && !FindNextFileW(dir->h, &dir->d)) return false;
    dir->pending = false;
    fill(out, &dir->d);
    return true;
}
void plat_closedir(void* handle) { HostDir* dir = (HostDir*)handle; FindClose(dir->h); free(dir); }
int plat_request_launch(int kind, const char* name, char* chosen, size_t chosen_size)
{
    // Host tests: no firmware to start anything; report what would be started.
    // r409: WINNT no longer comes here (plat_request_launch_entry below).
    const char* env = kind == PL_WIN ? getenv("CHARONDOS_TEST_WIN") : nullptr;
    if (!env || !*env) return 2;
    if (chosen && chosen_size) {
        const char* src = (name && *name) ? name : env;
        size_t i = 0;
        for (; src[i] && i + 1 < chosen_size; i++) chosen[i] = src[i];
        chosen[i] = 0;
    }
    fprintf(stderr, "[launch win %s]\n", (name && *name) ? name : env);
    return 0;
}

// ---- WINNT / LINUX (r409): the installed systems, faked for the tests --------
//   --volume GUID=DIR[@disk]   a host folder stands in for one volume (its
//                              \EFI tree is scanned; \EFI\HAK\systems.txt in
//                              it is Charon's registry)
//   --fwboot FILE              firmware boot entries in BootOrder order, one
//                              per line: description|partition GUID|path
//   --self DIR                 the folder of CHARONDOS.EFI (BOOT.CFG)
// Nothing is started: the entry that would be started is printed to stderr.
struct HostVolume { char part[40]; wchar_t dir[MAX_PATH]; uint16_t disk; };
static HostVolume g_volumes[8];
static int g_nvolumes;
static wchar_t g_fwboot[MAX_PATH], g_self_dir_host[MAX_PATH];

static void volume_path(const HostVolume* v, const char* path, wchar_t* out)
{
    wchar_t wide[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, MAX_PATH);
    wcscpy(out, v->dir);
    wcsncat(out, wide, MAX_PATH - wcslen(out) - 1);
}
static bool volume_file(void* context, const char* path, uint32_t* size)
{
    wchar_t full[MAX_PATH];
    volume_path((const HostVolume*)context, path, full);
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(full, GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    if (size) *size = d.nFileSizeLow;
    return true;
}
static int volume_names(void* context, const char* folder, bool folders, char (*out)[64], int cap)
{
    wchar_t full[MAX_PATH];
    volume_path((const HostVolume*)context, folder, full);
    wcsncat(full, L"\\*", MAX_PATH - wcslen(full) - 1);
    WIN32_FIND_DATAW d;
    const HANDLE h = FindFirstFileW(full, &d);
    if (h == INVALID_HANDLE_VALUE) return 0;
    // Sorted by name, so that the tests do not depend on the host file system's order.
    int n = 0;
    do {
        const bool is_dir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (is_dir != folders || !wcscmp(d.cFileName, L".") || !wcscmp(d.cFileName, L"..") || n >= cap) continue;
        char name[64];
        if (!WideCharToMultiByte(CP_UTF8, 0, d.cFileName, -1, name, sizeof name, nullptr, nullptr)) continue;
        int at = n;
        while (at > 0 && _stricmp(out[at - 1], name) > 0) { strcpy(out[at], out[at - 1]); at--; }
        strcpy(out[at], name);
        n++;
    } while (FindNextFileW(h, &d));
    FindClose(h);
    return n;
}
static long read_whole(const wchar_t* path, char* buffer, unsigned long capacity)
{
    const HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD n = 0;
    const BOOL ok = ReadFile(h, buffer, capacity, &n, nullptr);
    CloseHandle(h);
    return ok ? (long)n : -1;
}
int plat_launch_discover(LaunchList* out)
{
    static char text[8192];
    // 1. Charon's registry, on any volume.
    for (int i = 0; i < g_nvolumes; i++) {
        wchar_t path[MAX_PATH];
        volume_path(&g_volumes[i], "\\EFI\\HAK\\systems.txt", path);
        const long n = read_whole(path, text, sizeof text);
        if (n > 0) launch_parse_registry(text, (size_t)n, out);
    }
    for (int i = 0; i < out->count;) {
        // The registry is a source of names: a line whose loader is gone is not an installed system.
        bool there = false;
        for (int v = 0; v < g_nvolumes; v++)
            if (!_stricmp(out->e[i].part, g_volumes[v].part) && volume_file(&g_volumes[v], out->e[i].path, nullptr)) {
                there = true;
                if (!out->e[i].disk) out->e[i].disk = g_volumes[v].disk;
            }
        if (there) i++; else launch_remove(out, i);
    }
    // 2. Firmware entries.
    if (g_fwboot[0]) {
        const long n = read_whole(g_fwboot, text, sizeof text - 1);
        if (n > 0) {
            text[n] = 0;
            for (char* line = strtok(text, "\r\n"); line; line = strtok(nullptr, "\r\n")) {
                char* part = strchr(line, '|');
                if (!part) continue;
                *part++ = 0;
                char* path = strchr(part, '|');
                if (!path) continue;
                *path++ = 0;
                uint16_t disk = 0;
                for (int v = 0; v < g_nvolumes; v++) if (!_stricmp(part, g_volumes[v].part)) disk = g_volumes[v].disk;
                launch_add_firmware(out, line, part, path, disk);
            }
        }
    }
    // 3. Scan of every volume.
    for (int i = 0; i < g_nvolumes; i++) {
        LaunchVolume v;
        v.context = &g_volumes[i]; v.part = g_volumes[i].part; v.disk = g_volumes[i].disk;
        v.file = volume_file; v.names = volume_names;
        launch_scan_volume(&v, out);
    }
    return out->count;
}
static bool self_file(wchar_t* out)
{
    if (!g_self_dir_host[0]) return false;
    wcscpy(out, g_self_dir_host);
    wcsncat(out, L"\\BOOT.CFG", MAX_PATH - wcslen(out) - 1);
    return true;
}
long plat_launch_cfg_read(char* buffer, unsigned long capacity)
{
    wchar_t path[MAX_PATH];
    return self_file(path) ? read_whole(path, buffer, capacity) : -1;
}
int plat_launch_cfg_write(const char* data, unsigned long length)
{
    wchar_t path[MAX_PATH];
    if (!self_file(path)) return 0;
    const HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD n = 0;
    const BOOL ok = WriteFile(h, data, length, &n, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok && n == length ? 1 : 0;
}
int plat_request_launch_entry(const LaunchEntry* entry)
{
    // The loader must still be on its volume (as the UEFI backend checks).
    bool found = false;
    for (int v = 0; v < g_nvolumes; v++)
        if (!_stricmp(entry->part, g_volumes[v].part) && volume_file(&g_volumes[v], entry->path, nullptr)) found = true;
    if (!found) return 2;
    fprintf(stderr, "[launch %s %s %s:%s]\n", entry->kind == PL_WINNT ? "winnt" : "linux", entry->id, entry->part, entry->path);
    return 0;
}

bool plat_disk_space(int drive, uint64_t* total, uint64_t* free_bytes)
{
    ULARGE_INTEGER avail, tot, fr;
    if (!GetDiskFreeSpaceExW(g_roots[drive], &avail, &tot, &fr)) return false;
    *total = tot.QuadPart; *free_bytes = avail.QuadPart;
    return true;
}

// ---- CHARON command shim (host tests) ----------------------------------------
// A directory given by --hak DIR stands in for \EFI\HAK (the CharSettings slots,
// continue.bin, synths\). The firmware-only facts (live voice, boot entries,
// variables, reset) are faked deterministically so the command can be driven and
// its output checked; nothing is actually rebooted.
static wchar_t g_hak_dir[MAX_PATH];
static void hak_full(const char* name, wchar_t* out)
{
    wchar_t wname[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, name, -1, wname, MAX_PATH);
    wcscpy(out, g_hak_dir);
    wcsncat(out, L"\\", MAX_PATH - wcslen(out) - 1);
    wcsncat(out, wname, MAX_PATH - wcslen(out) - 1);
}
void charon_plat_info(CharonInfo* out)
{
    out->release = 401; out->have_voice = 1; out->have_reader = 1; out->secure_boot = -1;
}
int charon_plat_speak(const char* lang, const char* text)
{
    fprintf(stderr, "[speak %s] %s\n", lang, text);
    return 1;
}
long charon_plat_hak_read(const char* name, void* buf, unsigned long cap)
{
    if (!g_hak_dir[0]) return -1;
    wchar_t path[MAX_PATH]; hak_full(name, path);
    const HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD n = 0;
    const BOOL ok = ReadFile(h, buf, cap, &n, nullptr);
    CloseHandle(h);
    return ok ? (long)n : -2;
}
int charon_plat_hak_write(const char* name, const void* buf, unsigned long len)
{
    if (!g_hak_dir[0]) return 0;
    wchar_t path[MAX_PATH]; hak_full(name, path);
    const HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD n = 0;
    const BOOL ok = WriteFile(h, buf, len, &n, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok && n == len ? 1 : 0;
}
int charon_plat_boot_list(CharonBootEntry* out, int cap)
{
    static const struct { uint16_t id; const char* desc; } fake[] = {{1, "CharonDOS"}, {2, "Charon"}, {3, "Windows Boot Manager"}};
    int count = 0;
    for (int i = 0; i < 3 && count < cap; i++) { out[count].id = fake[i].id; strcpy(out[count].desc, fake[i].desc); count++; }
    return count;
}
int charon_plat_set_continue(uint16_t id)
{
    if (id < 1 || id > 3) return 1;
    uint8_t rec[18] = {'H', 'A', 'K', 'C', 'O', 'N', 'T', '1'};
    rec[8] = (uint8_t)id; rec[9] = (uint8_t)(id >> 8);
    rec[14] = 'H'; rec[15] = 'E'; rec[16] = 'N'; rec[17] = 'D';
    const uint8_t marker = 1;
    if (!charon_plat_hak_write("continue-set", &marker, 1)) return 2;
    return charon_plat_hak_write("continue.bin", rec, sizeof rec) ? 0 : 2;
}
int charon_plat_boot_next_charon(void)
{
    fprintf(stderr, "[bootnext Charon]\n");
    return 0;
}
int charon_plat_firmware_setup(void)
{
    fprintf(stderr, "[firmware-setup armed]\n");
    return 0;
}
void charon_plat_reset(int mode)
{
    fprintf(stderr, "[reset %s]\n", mode ? "off" : "restart");
}
int charon_plat_synth_list(char out[][64], int cap)
{
    if (!g_hak_dir[0]) return 0;
    wchar_t pattern[MAX_PATH];
    wcscpy(pattern, g_hak_dir);
    wcsncat(pattern, L"\\synths\\*.manifest", MAX_PATH - wcslen(pattern) - 1);
    WIN32_FIND_DATAW d;
    const HANDLE h = FindFirstFileW(pattern, &d);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int count = 0;
    do {
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        WideCharToMultiByte(CP_ACP, 0, d.cFileName, -1, out[count], 64, nullptr, nullptr);
        count++;
    } while (count < cap && FindNextFileW(h, &d));
    FindClose(h);
    return count;
}

// ---- main -------------------------------------------------------------------------------------
static void add_key(uint16_t ch, uint8_t code, uint8_t mods)
{
    if (g_nkeys < 4096) { g_keys[g_nkeys].ch = ch; g_keys[g_nkeys].code = code; g_keys[g_nkeys].mods = mods; g_nkeys++; }
}
static void parse_keys(const char* s)
{
    static const struct { const char* name; uint8_t code; uint16_t ch; uint8_t mods; } names[] = {
        {"ENTER", 0, 13, 0}, {"UP", PK_UP, 0, 0}, {"DOWN", PK_DOWN, 0, 0}, {"LEFT", PK_LEFT, 0, 0}, {"RIGHT", PK_RIGHT, 0, 0},
        {"HOME", PK_HOME, 0, 0}, {"END", PK_END, 0, 0}, {"PGUP", PK_PGUP, 0, 0}, {"PGDN", PK_PGDN, 0, 0}, {"INS", PK_INS, 0, 0},
        {"DEL", PK_DEL, 0, 0}, {"ESC", PK_ESC, 0, 0}, {"BS", 0, 8, 0}, {"TAB", 0, 9, 0}, {"CTRLC", 0, 'c', PM_CTRL}, {"CTRLZ", 0, 26, 0},
        {"BREAK", PK_BREAK, 0, 0}, {"F1", PK_F1, 0, 0}, {"F2", PK_F2, 0, 0}, {"F3", PK_F3, 0, 0}, {"F4", PK_F4, 0, 0}, {"F5", PK_F5, 0, 0},
        {"F6", PK_F6, 0, 0}, {"F7", PK_F7, 0, 0}, {"F8", PK_F8, 0, 0}, {"F9", PK_F9, 0, 0}, {"F10", PK_F10, 0, 0}, {"F11", PK_F11, 0, 0},
        {"F12", PK_F12, 0, 0}, {"ALTX", 0, 'x', PM_ALT}, {"LBRACE", 0, '{', 0},
    };
    while (*s) {
        if (*s == '{') {
            const char* end = strchr(s, '}');
            bool found = false;
            if (end) for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
                if (strlen(names[i].name) == (size_t)(end - s - 1) && !strncmp(names[i].name, s + 1, (size_t)(end - s - 1))) {
                    add_key(names[i].ch, names[i].code, names[i].mods); found = true; break;
                }
            if (found) { s = end + 1; continue; }
        }
        if (*s == '\n') add_key(13, 0, 0);
        else if (*s != '\r') add_key((uint8_t)*s, 0, 0);
        s++;
    }
}

int main(int argc, char** argv)
{
    DosOptions options;
    options.polish = false; options.command = nullptr;
    bool dump = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--root") && i + 1 < argc) {
            const char* v = argv[++i];
            const int drive = (v[0] | 0x20) - 'a';
            if (drive < 0 || drive > 25 || v[1] != '=') { fprintf(stderr, "bad --root\n"); return 2; }
            wchar_t wide[MAX_PATH], abs[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, v + 2, -1, wide, MAX_PATH);
            GetFullPathNameW(wide, MAX_PATH, abs, nullptr);
            size_t n = wcslen(abs);
            while (n > 3 && (abs[n - 1] == L'\\' || abs[n - 1] == L'/')) abs[--n] = 0;
            wcscpy(g_roots[drive], abs);
        } else if (!strcmp(argv[i], "--pl")) options.polish = true;
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) options.command = argv[++i];
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) { g_scripted = true; parse_keys(argv[++i]); }
        else if (!strcmp(argv[i], "--rowlog") && i + 1 < argc) g_rowlog = fopen(argv[++i], "wb");
        else if (!strcmp(argv[i], "--dump-screen")) dump = true;
        else if (!strcmp(argv[i], "--hak") && i + 1 < argc) { wchar_t w[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, argv[++i], -1, w, MAX_PATH); GetFullPathNameW(w, MAX_PATH, g_hak_dir, nullptr); }
        else if (!strcmp(argv[i], "--pcm") && i + 1 < argc) g_pcm = fopen(argv[++i], "wb");
        else if (!strcmp(argv[i], "--rtc") && i + 1 < argc) {
            unsigned y, mo, d, h, mi, s;
            if (sscanf(argv[++i], "%u-%u-%u %u:%u:%u", &y, &mo, &d, &h, &mi, &s) != 6) { fprintf(stderr, "bad --rtc\n"); return 2; }
            g_rtc.year = (uint16_t)y; g_rtc.month = (uint8_t)mo; g_rtc.day = (uint8_t)d; g_rtc.hour = (uint8_t)h; g_rtc.minute = (uint8_t)mi; g_rtc.second = (uint8_t)s; g_rtc.hsec = 0;
            g_have_rtc = true;
        }
        else if (!strcmp(argv[i], "--volume") && i + 1 < argc) {      // r409: GUID=DIR[@disk]
            const char* v = argv[++i];
            const char* eq = strchr(v, '=');
            if (!eq || eq - v >= 40 || g_nvolumes >= 8) { fprintf(stderr, "bad --volume\n"); return 2; }
            HostVolume* vol = &g_volumes[g_nvolumes++];
            memcpy(vol->part, v, (size_t)(eq - v)); vol->part[eq - v] = 0;
            char dir[MAX_PATH];
            strncpy(dir, eq + 1, MAX_PATH - 1); dir[MAX_PATH - 1] = 0;
            char* at = strrchr(dir, '@');
            vol->disk = 0;
            if (at && at[1] >= '0' && at[1] <= '9' && !strpbrk(at, "\\/")) { vol->disk = (uint16_t)atoi(at + 1); *at = 0; }
            wchar_t w[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, dir, -1, w, MAX_PATH); GetFullPathNameW(w, MAX_PATH, vol->dir, nullptr);
        }
        else if (!strcmp(argv[i], "--fwboot") && i + 1 < argc) { wchar_t w[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, argv[++i], -1, w, MAX_PATH); GetFullPathNameW(w, MAX_PATH, g_fwboot, nullptr); }
        else if (!strcmp(argv[i], "--self") && i + 1 < argc) { wchar_t w[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, argv[++i], -1, w, MAX_PATH); GetFullPathNameW(w, MAX_PATH, g_self_dir_host, nullptr); }
        else if (!strcmp(argv[i], "--inttrace") && i + 1 < argc) g_inttrace = fopen(argv[++i], "wb");
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (!g_roots[2][0]) { fprintf(stderr, "usage: charondos-host --root C=DIR [-c COMMAND] [--keys KEYS]\n"); return 2; }
    g_interactive = !g_scripted && !options.command;
    _setmode(_fileno(stdout), _O_BINARY);
    const int result = dos_main(&options);
    if (dump && g_mem) {
        for (int row = 0; row < 25; row++) {
            int last = 79;
            while (last >= 0 && g_mem[0xB8000 + (row * 80 + last) * 2] == ' ') last--;
            fputs("SCREEN|", stdout);
            for (int x = 0; x <= last; x++) put_utf8(stdout, g_cp437[g_mem[0xB8000 + (row * 80 + x) * 2]]);
            fputc('\n', stdout);
        }
    }
    if (g_rowlog) fclose(g_rowlog);
    if (g_pcm) fclose(g_pcm);
    fflush(stdout);
    return result;
}
