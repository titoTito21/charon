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
void plat_time(PlatTime* out)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    out->year = t.wYear; out->month = (uint8_t)t.wMonth; out->day = (uint8_t)t.wDay;
    out->hour = (uint8_t)t.wHour; out->minute = (uint8_t)t.wMinute; out->second = (uint8_t)t.wSecond;
    out->hsec = (uint8_t)(t.wMilliseconds / 10);
}
void* plat_alloc(size_t bytes) { return calloc(1, bytes); }

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
    const char* env = getenv(kind == PL_WINNT ? "CHARONDOS_TEST_WINNT" : "CHARONDOS_TEST_WIN");
    if (!env || !*env) return 2;
    if (chosen && chosen_size) {
        const char* src = (name && *name) ? name : env;
        size_t i = 0;
        for (; src[i] && i + 1 < chosen_size; i++) chosen[i] = src[i];
        chosen[i] = 0;
    }
    fprintf(stderr, "[launch %s %s]\n", kind == PL_WINNT ? "winnt" : "win", (name && *name) ? name : env);
    return 0;
}

bool plat_disk_space(int drive, uint64_t* total, uint64_t* free_bytes)
{
    ULARGE_INTEGER avail, tot, fr;
    if (!GetDiskFreeSpaceExW(g_roots[drive], &avail, &tot, &fr)) return false;
    *total = tot.QuadPart; *free_bytes = avail.QuadPart;
    return true;
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
    fflush(stdout);
    return result;
}
