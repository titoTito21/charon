// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - UEFI backend: efi_main, console through SimpleTextOutput (so a
// resident screen reader that hooks the console hears everything), keyboard
// through SimpleTextInput(Ex), drives through SimpleFileSystem, time from
// the runtime services. Nothing here writes firmware variables.
#include <efi/types.h>
#include <efi/boot-services.h>
#include <efi/runtime-services.h>
#include <efi/system-table.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/file.h>
#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>
#include <efi/protocol/simple-text-input.h>
#include <efi/protocol/simple-text-output.h>
#include "dos_platform.h"

namespace {

struct KeyState { uint32_t shift; uint8_t toggle; };
struct KeyData { efi_input_key key; KeyState state; };
struct InputEx {
    efi_status (EFIAPI *Reset)(InputEx*, bool);
    efi_status (EFIAPI *ReadKeyStrokeEx)(InputEx*, KeyData*);
    efi_event WaitForKeyEx;
    efi_status (EFIAPI *SetState)(InputEx*, uint8_t*);
    void* RegisterKeyNotify;
    void* UnregisterKeyNotify;
};

efi_guid g_loaded_image = EFI_LOADED_IMAGE_PROTOCOL_GUID;
efi_guid g_sfs = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
efi_guid g_file_info = EFI_FILE_INFO_GUID;
efi_guid g_fs_info = EFI_FILE_SYSTEM_INFO_GUID;
efi_guid g_input_ex = {0xdd9e7534, 0x7762, 0x4698, {0x8c, 0x14, 0xf5, 0x85, 0x17, 0xa6, 0x25, 0xaa}};

efi_system_table* ST;
efi_boot_services* BS;
efi_simple_text_output_protocol* Out;
InputEx* Ex;
efi_file_protocol* g_root[26];
char16_t g_prefix[26][300];          // path of the drive root inside its volume ("" = volume root)
volatile uint32_t g_ms;
bool g_timer_ok;
uint32_t g_fallback;
int g_attr = -1;
alignas(8) uint8_t g_info[1100];
char16_t g_path[420], g_path2[720];
uint8_t g_last_second = 0xFF;
uint32_t g_second_ms;

void EFIAPI tick(efi_event, void*) { g_ms = g_ms + 10; }

// ---- WINNT / WIN: what to start when the shell has ended ----------------------
efi_guid g_device_path = EFI_DEVICE_PATH_PROTOCOL_GUID;
// Charon's published voice (src/efi/BootMenuVoice.h, v1 prefix): stopped
// before the Windows Boot Manager takes over, as Charon itself does before
// any OS hand-off; a failed Stop forbids the transfer.
efi_guid g_menu_voice = {0x99e5faac, 0x6c86, 0x45db, {0xb0, 0x9d, 0x3c, 0xba, 0x80, 0x4d, 0x0e, 0x51}};
struct MenuVoiceV1 {
    uint32_t version, size;
    efi_status (EFIAPI *SpeakUtf8)(MenuVoiceV1*, const char*, const char*);
    efi_status (EFIAPI *Pump)(MenuVoiceV1*, uint32_t);
    efi_status (EFIAPI *Stop)(MenuVoiceV1*);
};
int g_launch_kind;                       // 0, PL_WINNT, PL_WIN
efi_handle g_launch_device;              // volume that holds the program
char16_t g_launch_file[300];             // its path on that volume
char16_t g_launch_options[400];          // load options for CHARONPC.EFI
char16_t g_self_dir[300];                // folder of CHARONDOS.EFI on its volume
efi_handle g_self_device;
bool g_polish;
efi_handle g_image;

size_t wlen(const char16_t* s) { size_t n = 0; while (s[n]) n++; return n; }
void wcopy(char16_t* d, const char16_t* s, size_t cap)
{
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++) d[i] = s[i];
    d[i] = 0;
}
void wcat(char16_t* d, const char16_t* s, size_t cap) { const size_t n = wlen(d); if (n < cap) wcopy(d + n, s, cap - n); }

// Shared code paths start with a backslash; a file protocol wants them
// relative to the directory handle, and "." for the directory itself.
const char16_t* rel(const dchar16* path, char16_t* buffer = g_path)
{
    if (!path[0]) { buffer[0] = u'.'; buffer[1] = 0; return buffer; }
    wcopy(buffer, (const char16_t*)path + (path[0] == u'\\' ? 1 : 0), 420);
    return buffer;
}
int dos_error(efi_status st)
{
    if (st == EFI_SUCCESS) return 0;
    if (st == EFI_NOT_FOUND) return 2;
    if (st == EFI_NO_MEDIA || st == EFI_MEDIA_CHANGED) return 21;
    return 5;
}
efi_file_info* info_of(efi_file_protocol* f)
{
    size_t size = sizeof g_info;
    if (f->GetInfo(f, &g_file_info, &size, g_info) != EFI_SUCCESS) return nullptr;
    return (efi_file_info*)g_info;
}
uint16_t dos_date(const efi_time& t)
{
    if (t.Year < 1980) return 0x0021;
    return (uint16_t)(((t.Year - 1980) << 9) | (t.Month << 5) | t.Day);
}
uint16_t dos_time(const efi_time& t) { return (uint16_t)((t.Hour << 11) | (t.Minute << 5) | (t.Second / 2)); }
void fill(PlatDirEnt* out, const efi_file_info* info)
{
    size_t i = 0;
    for (; info->FileName[i] && i < 255; i++) out->name[i] = info->FileName[i];
    out->name[i] = 0;
    out->size = info->FileSize > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)info->FileSize;
    out->attr = (uint8_t)(info->Attribute & 0x37);
    out->date = dos_date(info->ModificationTime);
    out->time = dos_time(info->ModificationTime);
}
efi_file_protocol* open_at(int drive, const dchar16* path, uint64_t mode, uint64_t attr, efi_status* status)
{
    efi_file_protocol* f = nullptr;
    efi_file_protocol* root = g_root[drive];
    const efi_status st = root->Open(root, &f, rel(path), mode, attr);
    if (status) *status = st;
    return st == EFI_SUCCESS ? f : nullptr;
}
bool is_dir(efi_file_protocol* f)
{
    const efi_file_info* info = info_of(f);
    return info && (info->Attribute & EFI_FILE_DIRECTORY);
}

bool has_word_pl(const void* options, uint32_t size)
{
    const uint8_t* b = (const uint8_t*)options;
    // UTF-16 first (the usual form), then plain bytes.
    for (int wide = 1; wide >= 0; wide--) {
        const uint32_t step = wide ? 2 : 1, count = size / step;
        auto at = [&](uint32_t i) -> uint32_t { return wide ? (uint32_t)(b[i * 2] | (b[i * 2 + 1] << 8)) : b[i]; };
        auto alnum = [](uint32_t c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
        for (uint32_t i = 0; i + 1 < count; i++) {
            if ((at(i) | 0x20) != 'p' || (at(i + 1) | 0x20) != 'l') continue;
            if (i > 0 && alnum(at(i - 1))) continue;
            if (i + 2 < count && alnum(at(i + 2))) continue;
            return true;
        }
    }
    return false;
}

void find_drives(efi_handle image)
{
    efi_loaded_image_protocol* loaded = nullptr;
    efi_handle boot_device = nullptr;
    if (BS->HandleProtocol(image, &g_loaded_image, (void**)&loaded) == EFI_SUCCESS && loaded) {
        boot_device = loaded->DeviceHandle;
        efi_simple_file_system_protocol* fs = nullptr;
        efi_file_protocol* root = nullptr;
        if (boot_device && BS->HandleProtocol(boot_device, &g_sfs, (void**)&fs) == EFI_SUCCESS && fs
            && fs->OpenVolume(fs, &root) == EFI_SUCCESS && root) {
            g_root[2] = root;
            // The folder "C" next to the program, when there is one.
            char16_t dir[300];
            dir[0] = u'\\'; dir[1] = 0;
            const uint8_t* node = (const uint8_t*)loaded->FilePath;
            for (int guard = 0; node && guard < 32; guard++) {
                const uint8_t type = node[0], sub = node[1];
                const uint32_t len = (uint32_t)(node[2] | (node[3] << 8));
                if (type == DEVICE_PATH_END || len < 4) break;
                if (type == DEVICE_PATH_MEDIA && sub == MEDIA_FILEPATH_DP) {
                    size_t n = wlen(dir);
                    if (n && dir[n - 1] != u'\\' && n < 298) { dir[n++] = u'\\'; dir[n] = 0; }
                    for (uint32_t i = 4; i + 1 < len && n < 298; i += 2) {
                        const char16_t c = (char16_t)(node[i] | (node[i + 1] << 8));
                        if (!c) break;
                        if (c == u'\\' && n && dir[n - 1] == u'\\') continue;
                        dir[n++] = c;
                    }
                    dir[n] = 0;
                }
                node += len;
            }
            size_t cut = wlen(dir);
            while (cut && dir[cut - 1] != u'\\') cut--;
            if (cut) cut--;
            dir[cut] = 0;
            wcopy(g_self_dir, dir, 300);
            g_self_device = boot_device;
            wcat(dir, u"\\C", 300);
            efi_file_protocol* sub = nullptr;
            if (root->Open(root, &sub, dir + 1, EFI_FILE_MODE_READ, 0) == EFI_SUCCESS && sub) {
                if (is_dir(sub)) { g_root[2] = sub; wcopy(g_prefix[2], dir, 300); }
                else sub->Close(sub);
            }
        }
    }
    size_t count = 0;
    efi_handle* handles = nullptr;
    if (BS->LocateHandleBuffer(ByProtocol, &g_sfs, nullptr, &count, &handles) != EFI_SUCCESS || !handles) return;
    int next = 3;
    for (size_t i = 0; i < count && next < 26; i++) {
        if (handles[i] == boot_device && g_root[2]) continue;
        efi_simple_file_system_protocol* fs = nullptr;
        efi_file_protocol* root = nullptr;
        if (BS->HandleProtocol(handles[i], &g_sfs, (void**)&fs) != EFI_SUCCESS || !fs) continue;
        if (fs->OpenVolume(fs, &root) != EFI_SUCCESS || !root) continue;
        if (!g_root[2]) g_root[2] = root; else g_root[next++] = root;
    }
    BS->FreePool(handles);
}

bool file_exists(efi_file_protocol* root, const char16_t* path)
{
    efi_file_protocol* f = nullptr;
    if (!root || root->Open(root, &f, (char16_t*)path, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS || !f) return false;
    const bool dir = is_dir(f);
    f->Close(f);
    return !dir;
}

// Full device path: the volume's own path + one file path node + end.
efi_device_path_protocol* file_path_of(efi_handle device, const char16_t* file)
{
    efi_device_path_protocol* dev = nullptr;
    if (BS->HandleProtocol(device, &g_device_path, (void**)&dev) != EFI_SUCCESS || !dev) return nullptr;
    size_t dev_len = 0;
    for (const uint8_t* n = (const uint8_t*)dev;; ) {
        const uint32_t len = (uint32_t)(n[2] | (n[3] << 8));
        if (n[0] == DEVICE_PATH_END || len < 4) break;
        dev_len += len; n += len;
        if (dev_len > 4096) return nullptr;
    }
    const size_t chars = wlen(file) + 1;
    const size_t node = 4 + chars * 2;
    uint8_t* out = nullptr;
    if (BS->AllocatePool(EfiLoaderData, dev_len + node + 4, (void**)&out) != EFI_SUCCESS || !out) return nullptr;
    const uint8_t* src = (const uint8_t*)dev;
    for (size_t i = 0; i < dev_len; i++) out[i] = src[i];
    uint8_t* f = out + dev_len;
    f[0] = DEVICE_PATH_MEDIA; f[1] = MEDIA_FILEPATH_DP; f[2] = (uint8_t)node; f[3] = (uint8_t)(node >> 8);
    for (size_t i = 0; i < chars; i++) { f[4 + i * 2] = (uint8_t)file[i]; f[5 + i * 2] = (uint8_t)(file[i] >> 8); }
    uint8_t* e = f + node;
    e[0] = DEVICE_PATH_END; e[1] = DEVICE_PATH_ENTIRE_END; e[2] = 4; e[3] = 0;
    return (efi_device_path_protocol*)out;
}

bool find_windows()
{
    size_t count = 0;
    efi_handle* handles = nullptr;
    if (BS->LocateHandleBuffer(ByProtocol, &g_sfs, nullptr, &count, &handles) != EFI_SUCCESS || !handles) return false;
    bool found = false;
    for (size_t i = 0; i < count && !found; i++) {
        efi_simple_file_system_protocol* fs = nullptr;
        efi_file_protocol* root = nullptr;
        if (BS->HandleProtocol(handles[i], &g_sfs, (void**)&fs) != EFI_SUCCESS || !fs) continue;
        if (fs->OpenVolume(fs, &root) != EFI_SUCCESS || !root) continue;
        if (file_exists(root, u"EFI\\Microsoft\\Boot\\bootmgfw.efi")) {
            g_launch_device = handles[i];
            wcopy(g_launch_file, u"\\EFI\\Microsoft\\Boot\\bootmgfw.efi", 300);
            found = true;
        }
        root->Close(root);
    }
    BS->FreePool(handles);
    return found;
}

// Starts what the shell asked for. WINNT does not return when Windows starts.
efi_status run_launch()
{
    efi_device_path_protocol* path = file_path_of(g_launch_device, g_launch_file);
    if (!path) return EFI_NOT_FOUND;
    if (g_launch_kind == PL_WINNT) {
        MenuVoiceV1* voice = nullptr;
        if (BS->LocateProtocol(&g_menu_voice, nullptr, (void**)&voice) == EFI_SUCCESS && voice && voice->Stop) {
            const efi_status st = voice->Stop(voice);
            if (st != EFI_SUCCESS) { BS->FreePool(path); return st; }
        }
    }
    efi_handle child = nullptr;
    efi_status st = BS->LoadImage(false, g_image, path, nullptr, 0, &child);
    BS->FreePool(path);
    if (st != EFI_SUCCESS || !child) return st == EFI_SUCCESS ? EFI_LOAD_ERROR : st;
    if (g_launch_kind == PL_WIN) {
        efi_loaded_image_protocol* li = nullptr;
        if (BS->HandleProtocol(child, &g_loaded_image, (void**)&li) == EFI_SUCCESS && li) {
            li->LoadOptions = g_launch_options;
            li->LoadOptionsSize = (uint32_t)((wlen(g_launch_options) + 1) * 2);
        }
    }
    st = BS->StartImage(child, nullptr, nullptr);
    return st;
}

}  // namespace

int plat_request_launch(int kind, const char* name, char* chosen, size_t chosen_size)
{
    g_launch_kind = 0;
    if (chosen && chosen_size) chosen[0] = 0;
    if (kind == PL_WINNT) {
        if (!find_windows()) return 2;
        g_launch_kind = PL_WINNT;
        return 0;
    }
    if (kind != PL_WIN || !g_self_device || !g_root[2]) return 2;
    // The emulator lives next to CHARONDOS.EFI; profiles on drive C: \PC\<name>.
    char16_t emu[300];
    wcopy(emu, g_self_dir, 300); wcat(emu, u"\\CHARONPC.EFI", 300);
    efi_simple_file_system_protocol* fs = nullptr;
    efi_file_protocol* vol = nullptr;
    if (BS->HandleProtocol(g_self_device, &g_sfs, (void**)&fs) != EFI_SUCCESS || !fs || fs->OpenVolume(fs, &vol) != EFI_SUCCESS || !vol) return 2;
    const bool have_emu = file_exists(vol, emu + 1);
    vol->Close(vol);
    if (!have_emu) return 2;
    char16_t profile[64]; profile[0] = 0;
    if (name && *name) {
        size_t i = 0;
        for (; name[i] && i + 1 < 64; i++) { char c = name[i]; if (c >= 'a' && c <= 'z') c = (char)(c - 32); profile[i] = (char16_t)(uint8_t)c; }
        profile[i] = 0;
    } else {
        // No name: the first folder in C:\PC that holds a CHARONPC.CFG.
        efi_file_protocol* pc = nullptr;
        if (g_root[2]->Open(g_root[2], &pc, (char16_t*)u"PC", EFI_FILE_MODE_READ, 0) != EFI_SUCCESS || !pc) return 2;
        for (;;) {
            alignas(8) static uint8_t listing[1100];   // own buffer: file_exists() reuses g_info
            size_t size = sizeof listing;
            if (pc->Read(pc, &size, listing) != EFI_SUCCESS || !size) break;
            const efi_file_info* info = (const efi_file_info*)listing;
            if (!(info->Attribute & EFI_FILE_DIRECTORY) || info->FileName[0] == u'.') continue;
            char16_t cfg[300];
            wcopy(cfg, info->FileName, 300); wcat(cfg, u"\\CHARONPC.CFG", 300);
            if (file_exists(pc, cfg)) { wcopy(profile, info->FileName, 64); break; }
        }
        pc->Close(pc);
        if (!profile[0]) return 2;
    }
    char16_t cfg[300];
    wcopy(cfg, u"PC\\", 300); wcat(cfg, profile, 300); wcat(cfg, u"\\CHARONPC.CFG", 300);
    if (!file_exists(g_root[2], cfg)) return 2;
    // Options: config=<path of the .CFG on drive C:'s volume> [pl]
    wcopy(g_launch_options, u"config=", 400);
    wcat(g_launch_options, g_prefix[2], 400);
    wcat(g_launch_options, u"\\", 400);
    wcat(g_launch_options, cfg, 400);
    if (g_polish) wcat(g_launch_options, u" pl", 400);
    wcopy(g_launch_file, emu, 300);
    g_launch_device = g_self_device;
    g_launch_kind = PL_WIN;
    if (chosen && chosen_size) {
        size_t i = 0;
        for (; profile[i] && i + 1 < chosen_size; i++) chosen[i] = profile[i] < 128 ? (char)profile[i] : '?';
        chosen[i] = 0;
    }
    return 0;
}

// ---- console ------------------------------------------------------------------------------
void plat_trace_int(uint8_t, const uint16_t*, uint16_t, bool) {}
void plat_con_size(int* cols, int* rows)
{
    size_t c = 80, r = 25;
    if (Out->QueryMode(Out, (size_t)Out->Mode->Mode, &c, &r) != EFI_SUCCESS) { c = 80; r = 25; }
    *cols = (int)c; *rows = (int)r;
}
void plat_con_clear(uint8_t attr)
{
    Out->SetAttribute(Out, attr & 0x7F);
    g_attr = attr & 0x7F;
    Out->ClearScreen(Out);
}
void plat_con_goto(int col, int row) { Out->SetCursorPosition(Out, (size_t)col, (size_t)row); }
void plat_con_out(const dchar16* text, int count, uint8_t attr, bool)
{
    char16_t buffer[200];
    if ((attr & 0x7F) != g_attr) { g_attr = attr & 0x7F; Out->SetAttribute(Out, (size_t)g_attr); }
    while (count > 0) {
        int n = count > 199 ? 199 : count;
        for (int i = 0; i < n; i++) buffer[i] = text[i] ? (char16_t)text[i] : u' ';
        buffer[n] = 0;
        Out->OutputString(Out, buffer);
        text += n; count -= n;
    }
}
void plat_con_show_cursor(bool visible) { Out->EnableCursor(Out, visible); }

bool plat_key_poll(PlatKey* out)
{
    for (int guard = 0; guard < 16; guard++) {
        efi_input_key key;
        uint32_t shift = 0;
        if (Ex) {
            KeyData data;
            data.state.shift = 0;
            if (Ex->ReadKeyStrokeEx(Ex, &data) != EFI_SUCCESS) return false;
            key = data.key;
            if (data.state.shift & 0x80000000u) shift = data.state.shift;
        } else if (ST->ConIn->ReadKeyStroke(ST->ConIn, &key) != EFI_SUCCESS) return false;
        const bool ctrl = shift & 0x0C, alt = shift & 0x30, shifted = shift & 0x03;
        // Charon's resident reader owns Ctrl+Alt+key, Alt+arrows and Alt+backslash.
        if (ctrl && alt) continue;
        if (alt && ((key.ScanCode >= 1 && key.ScanCode <= 4) || key.UnicodeChar == u'\\')) continue;
        out->mods = (uint8_t)((shifted ? PM_SHIFT : 0) | (ctrl ? PM_CTRL : 0) | (alt ? PM_ALT : 0));
        out->ch = 0; out->code = PK_NONE;
        static const uint8_t scan[] = {PK_NONE, PK_UP, PK_DOWN, PK_RIGHT, PK_LEFT, PK_HOME, PK_END, PK_INS, PK_DEL, PK_PGUP, PK_PGDN,
                                       PK_F1, PK_F2, PK_F3, PK_F4, PK_F5, PK_F6, PK_F7, PK_F8, PK_F9, PK_F10, PK_F11, PK_F12, PK_ESC};
        if (key.ScanCode == 0x48) out->code = PK_BREAK;                  // Pause / Break
        else if (key.ScanCode && key.ScanCode < sizeof scan) out->code = scan[key.ScanCode];
        else if (key.UnicodeChar) out->ch = key.UnicodeChar;
        else continue;
        return true;
    }
    return false;
}
bool plat_quit(void) { return false; }
void plat_idle(unsigned ms)
{
    if (!ms) return;
    BS->Stall((size_t)ms * 1000);
    if (!g_timer_ok) g_ms = g_ms + ms;
}
uint32_t plat_millis(void)
{
    if (!g_timer_ok && (++g_fallback & 0xFF) == 0) g_ms = g_ms + 1;
    return g_ms;
}
void plat_time(PlatTime* out)
{
    efi_time t;
    if (ST->RuntimeServices->GetTime(&t, nullptr) != EFI_SUCCESS) {
        const uint32_t s = g_ms / 1000;
        out->year = 1980; out->month = 1; out->day = 1;
        out->hour = (uint8_t)(s / 3600 % 24); out->minute = (uint8_t)(s / 60 % 60); out->second = (uint8_t)(s % 60);
        out->hsec = (uint8_t)(g_ms / 10 % 100);
        return;
    }
    const uint32_t now = plat_millis();
    if (t.Second != g_last_second) { g_last_second = t.Second; g_second_ms = now; }
    uint32_t hs = (now - g_second_ms) / 10;
    if (hs > 99) hs = 99;
    out->year = t.Year; out->month = t.Month; out->day = t.Day;
    out->hour = t.Hour; out->minute = t.Minute; out->second = t.Second; out->hsec = (uint8_t)hs;
}
void* plat_alloc(size_t bytes)
{
    void* p = nullptr;
    if (BS->AllocatePool(EfiLoaderData, bytes, &p) != EFI_SUCCESS || !p) return nullptr;
    BS->SetMem(p, bytes, 0);
    return p;
}

// ---- files ----------------------------------------------------------------------------------
bool plat_drive_ok(int drive) { return drive >= 0 && drive < 26 && g_root[drive]; }

int plat_open(int drive, const dchar16* path, int flags, void** file)
{
    efi_status st;
    efi_file_protocol* f;
    const int mode = flags & PO_MODE;
    if (flags & PO_CREATE) {
        f = open_at(drive, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &st);
        if (f) {
            if (is_dir(f)) { f->Close(f); return 5; }
            if (flags & PO_TRUNC) { f->Delete(f); f = nullptr; }
        } else if (st != EFI_NOT_FOUND) return dos_error(st);
        if (!f) f = open_at(drive, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0, &st);
        if (!f) return dos_error(st);
    } else {
        f = open_at(drive, path, mode == PO_READ ? EFI_FILE_MODE_READ : (EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE), 0, &st);
        if (!f) return dos_error(st);
        if (is_dir(f)) { f->Close(f); return 5; }
    }
    *file = f;
    return 0;
}
long plat_read(void* file, void* buffer, uint32_t count)
{
    efi_file_protocol* f = (efi_file_protocol*)file;
    size_t n = count;
    if (f->Read(f, &n, buffer) != EFI_SUCCESS) return -1;
    return (long)n;
}
long plat_write(void* file, const void* buffer, uint32_t count)
{
    efi_file_protocol* f = (efi_file_protocol*)file;
    size_t n = count;
    const efi_status st = f->Write(f, &n, (void*)buffer);
    if (st != EFI_SUCCESS && !n) return -1;
    return (long)n;
}
bool plat_seek(void* file, uint32_t position)
{
    efi_file_protocol* f = (efi_file_protocol*)file;
    return f->SetPosition(f, position) == EFI_SUCCESS;
}
uint32_t plat_size(void* file)
{
    const efi_file_info* info = info_of((efi_file_protocol*)file);
    if (!info) return 0;
    return info->FileSize > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)info->FileSize;
}
bool plat_truncate(void* file, uint32_t size)
{
    efi_file_protocol* f = (efi_file_protocol*)file;
    efi_file_info* info = info_of(f);
    if (!info) return false;
    info->FileSize = size;
    return f->SetInfo(f, &g_file_info, (size_t)info->Size, info) == EFI_SUCCESS;
}
bool plat_set_ftime(void* file, uint16_t date, uint16_t time)
{
    efi_file_protocol* f = (efi_file_protocol*)file;
    efi_file_info* info = info_of(f);
    if (!info) return false;
    efi_time& t = info->ModificationTime;
    t.Year = (uint16_t)(1980 + (date >> 9)); t.Month = (uint8_t)((date >> 5) & 15); t.Day = (uint8_t)(date & 31);
    t.Hour = (uint8_t)(time >> 11); t.Minute = (uint8_t)((time >> 5) & 63); t.Second = (uint8_t)((time & 31) * 2);
    t.Nanosecond = 0;
    if (!t.Month) t.Month = 1;
    if (!t.Day) t.Day = 1;
    return f->SetInfo(f, &g_file_info, (size_t)info->Size, info) == EFI_SUCCESS;
}
void plat_close(void* file) { efi_file_protocol* f = (efi_file_protocol*)file; f->Close(f); }

bool plat_stat(int drive, const dchar16* path, PlatDirEnt* out)
{
    efi_file_protocol* f = open_at(drive, path, EFI_FILE_MODE_READ, 0, nullptr);
    if (!f) return false;
    const efi_file_info* info = info_of(f);
    if (info) fill(out, info);
    f->Close(f);
    return info != nullptr;
}
int plat_delete(int drive, const dchar16* path)
{
    efi_status st;
    efi_file_protocol* f = open_at(drive, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &st);
    if (!f) return dos_error(st);
    if (is_dir(f)) { f->Close(f); return 5; }
    return f->Delete(f) == EFI_SUCCESS ? 0 : 5;
}
int plat_mkdir(int drive, const dchar16* path)
{
    efi_status st;
    efi_file_protocol* f = open_at(drive, path, EFI_FILE_MODE_READ, 0, &st);
    if (f) { f->Close(f); return 5; }
    f = open_at(drive, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY, &st);
    if (!f) return dos_error(st) == 2 ? 3 : dos_error(st);
    f->Close(f);
    return 0;
}
int plat_rmdir(int drive, const dchar16* path)
{
    efi_status st;
    efi_file_protocol* f = open_at(drive, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &st);
    if (!f) return dos_error(st) == 2 ? 3 : dos_error(st);
    if (!is_dir(f)) { f->Close(f); return 3; }
    return f->Delete(f) == EFI_SUCCESS ? 0 : 5;        // a directory that is not empty is not deleted
}
int plat_rename(int drive, const dchar16* from, const dchar16* to)
{
    efi_status st;
    efi_file_protocol* f = open_at(drive, from, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &st);
    if (!f) return dos_error(st);
    efi_file_info* info = info_of(f);
    if (!info) { f->Close(f); return 5; }
    // Same directory: the new name alone. Another directory: the full path
    // from the root of the volume.
    size_t cut_from = 0, cut_to = 0;
    for (size_t i = 0; from[i]; i++) if (from[i] == u'\\') cut_from = i;
    for (size_t i = 0; to[i]; i++) if (to[i] == u'\\') cut_to = i;
    bool same = cut_from == cut_to;
    for (size_t i = 0; same && i < cut_from; i++) if (from[i] != to[i]) same = false;
    if (same) wcopy(g_path2, (const char16_t*)to + cut_to + 1, 720);
    else { wcopy(g_path2, g_prefix[drive], 720); wcat(g_path2, (const char16_t*)to, 720); }
    const size_t chars = wlen(g_path2) + 1;
    const size_t header = (size_t)((uint8_t*)info->FileName - (uint8_t*)info);
    if (header + chars * 2 > sizeof g_info) { f->Close(f); return 5; }
    for (size_t i = 0; i < chars; i++) info->FileName[i] = g_path2[i];
    info->Size = header + chars * 2;
    st = f->SetInfo(f, &g_file_info, (size_t)info->Size, info);
    f->Close(f);
    return st == EFI_SUCCESS ? 0 : 5;
}
bool plat_set_attr(int drive, const dchar16* path, uint8_t attr)
{
    efi_file_protocol* f = open_at(drive, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, nullptr);
    if (!f) return false;
    efi_file_info* info = info_of(f);
    bool ok = false;
    if (info) {
        info->Attribute = (info->Attribute & EFI_FILE_DIRECTORY) | (attr & 0x27);
        ok = f->SetInfo(f, &g_file_info, (size_t)info->Size, info) == EFI_SUCCESS;
    }
    f->Close(f);
    return ok;
}
void* plat_opendir(int drive, const dchar16* path)
{
    efi_file_protocol* f = open_at(drive, path, EFI_FILE_MODE_READ, 0, nullptr);
    if (!f) return nullptr;
    if (!is_dir(f)) { f->Close(f); return nullptr; }
    f->SetPosition(f, 0);
    return f;
}
bool plat_readdir(void* dir, PlatDirEnt* out)
{
    efi_file_protocol* f = (efi_file_protocol*)dir;
    size_t size = sizeof g_info;
    if (f->Read(f, &size, g_info) != EFI_SUCCESS || !size) return false;
    fill(out, (const efi_file_info*)g_info);
    return true;
}
void plat_closedir(void* dir) { efi_file_protocol* f = (efi_file_protocol*)dir; f->Close(f); }
bool plat_disk_space(int drive, uint64_t* total, uint64_t* free_bytes)
{
    efi_file_protocol* root = g_root[drive];
    size_t size = sizeof g_info;
    if (root->GetInfo(root, &g_fs_info, &size, g_info) != EFI_SUCCESS) return false;
    const efi_file_system_info* info = (const efi_file_system_info*)g_info;
    *total = info->VolumeSize; *free_bytes = info->FreeSpace;
    return true;
}

// ---- entry ----------------------------------------------------------------------------------
extern "C" efi_status EFIAPI efi_main(efi_handle image, efi_system_table* table)
{
    ST = table; BS = table->BootServices; Out = table->ConOut;
    BS->SetWatchdogTimer(0, 0, 0, nullptr);
    void* ex = nullptr;
    if (table->ConsoleInHandle && BS->HandleProtocol(table->ConsoleInHandle, &g_input_ex, &ex) == EFI_SUCCESS) Ex = (InputEx*)ex;

    // An 80x25 console lets the firmware scroll for us; pick that mode when
    // the current one is different, and put the old one back on exit.
    const int32_t old_mode = Out->Mode->Mode;
    const bool old_cursor = Out->Mode->CursorVisible;
    const int32_t old_attr = Out->Mode->Attribute;
    size_t cols = 0, rows = 0;
    bool switched = false;
    if (Out->QueryMode(Out, (size_t)old_mode, &cols, &rows) != EFI_SUCCESS || cols != 80 || rows != 25) {
        for (int32_t m = 0; m < Out->Mode->MaxMode; m++) {
            if (Out->QueryMode(Out, (size_t)m, &cols, &rows) == EFI_SUCCESS && cols == 80 && rows == 25) {
                switched = Out->SetMode(Out, (size_t)m) == EFI_SUCCESS;
                break;
            }
        }
    }
    Out->QueryMode(Out, (size_t)Out->Mode->Mode, &cols, &rows);
    if (cols < 80 || rows < 25) {
        Out->OutputString(Out, (char16_t*)u"CharonDOS needs a text console of at least 80x25.\r\n");
        return EFI_UNSUPPORTED;
    }

    efi_event timer = nullptr;
    if (BS->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, tick, nullptr, &timer) == EFI_SUCCESS) {
        if (BS->SetTimer(timer, TimerPeriodic, 100000) == EFI_SUCCESS) g_timer_ok = true;
        else { BS->CloseEvent(timer); timer = nullptr; }
    }

    DosOptions options;
    options.polish = false; options.command = nullptr;
    efi_loaded_image_protocol* loaded = nullptr;
    if (BS->HandleProtocol(image, &g_loaded_image, (void**)&loaded) == EFI_SUCCESS && loaded && loaded->LoadOptions && loaded->LoadOptionsSize)
        options.polish = has_word_pl(loaded->LoadOptions, loaded->LoadOptionsSize);
    find_drives(image);

    g_image = image; g_polish = options.polish;
    efi_status result = EFI_SUCCESS;
    for (;;) {
        if (!g_root[2]) {
            Out->OutputString(Out, (char16_t*)(options.polish ? u"CharonDOS: nie znaleziono woluminu z plikami.\r\n" : u"CharonDOS: no file system volume found.\r\n"));
            result = EFI_NOT_FOUND;
            break;
        }
        g_launch_kind = 0;
        dos_main(&options);
        if (!g_launch_kind) break;
        // Leave nothing open while another program owns the machine.
        if (timer) BS->SetTimer(timer, TimerCancel, 0);
        for (int i = 0; i < 26; i++) if (g_root[i]) { g_root[i]->Close(g_root[i]); g_root[i] = nullptr; g_prefix[i][0] = 0; }
        Out->SetAttribute(Out, (size_t)(old_attr & 0x7F));
        Out->ClearScreen(Out);
        const int kind = g_launch_kind;
        const efi_status st = run_launch();
        g_launch_kind = 0;
        if (kind == PL_WINNT || st != EFI_SUCCESS) {
            // Windows Boot Manager came back, or the program could not start.
            Out->OutputString(Out, (char16_t*)(options.polish ? u"CharonDOS: program zakończył się błędem albo nie mógł wystartować.\r\n"
                                                              : u"CharonDOS: the program ended with an error or could not start.\r\n"));
        }
        // Back to CharonDOS: same console mode, fresh drives and timer.
        if (switched || cols != 80) {
            for (int32_t m = 0; m < Out->Mode->MaxMode; m++) {
                size_t c2 = 0, r2 = 0;
                if (Out->QueryMode(Out, (size_t)m, &c2, &r2) == EFI_SUCCESS && c2 == 80 && r2 == 25) { Out->SetMode(Out, (size_t)m); break; }
            }
        }
        if (timer) BS->SetTimer(timer, TimerPeriodic, 100000);
        find_drives(image);
    }

    if (timer) { BS->SetTimer(timer, TimerCancel, 0); BS->CloseEvent(timer); }
    for (int i = 0; i < 26; i++) if (g_root[i]) g_root[i]->Close(g_root[i]);
    Out->SetAttribute(Out, (size_t)(old_attr & 0x7F));
    if (switched) Out->SetMode(Out, (size_t)old_mode);
    else if (result == EFI_SUCCESS) Out->OutputString(Out, (char16_t*)u"\r\n");
    Out->EnableCursor(Out, old_cursor);
    return result;
}
