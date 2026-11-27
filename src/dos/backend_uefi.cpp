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
#include "charon_cmd.h"

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
// r407: Charon's PCM sink (src/efi/CharonPcmSink.h, version 1): the sound of sound.cpp, mixed
// under the speech by the voice that owns the sound card. Without it CharonDOS is silent.
efi_guid g_pcm_sink = {0x8c4e2f1a, 0x3b7d, 0x4e65, {0xa1, 0x9c, 0x52, 0xd8, 0x07, 0x6e, 0xb3, 0x41}};
struct PcmSinkV1 {
    uint32_t version, size, rate, channels;
    efi_status (EFIAPI *Open)(PcmSinkV1*, uint32_t, uint32_t);
    efi_status (EFIAPI *Write)(PcmSinkV1*, const int16_t*, size_t, size_t*);
    efi_status (EFIAPI *Queued)(PcmSinkV1*, size_t*);
    efi_status (EFIAPI *Close)(PcmSinkV1*);
};
PcmSinkV1* g_pcm;                        // open sink, or null
void pcm_close() { if (g_pcm) { g_pcm->Close(g_pcm); g_pcm = nullptr; } }
int g_launch_kind;                       // 0, PL_WINNT, PL_WIN, PL_LINUX (r409)
char g_launch_name[64];                  // r409: display name of what WINNT / LINUX starts (UTF-8)
efi_status g_launch_stop;                // r409: what the voice's Stop returned before a LINUX hand-over
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

// Starts what the shell asked for. WINNT and LINUX do not return when the
// system starts.
//
// r409, the audio hand-over (read in src/efi/CharonVoiceProvider.h Stop() and
// EnsureRunning()): MenuVoice::Stop cancels queued speech, lets the frames the
// device already took play out and stops the device stream. It does not end
// the resident reader: the reader's next SpeakUtf8 opens the stream again
// (EnsureRunning). Charon's own menu calls the same Stop before every
// transfer, Linux loaders included (BootMenu.h, BeforeTransfer), and the
// reader then reads GRUB's menu. So both commands hand over exactly as the
// menu does. Because Stop cancels what is still queued, the line "Starting
// ..." is given time first: a bounded wait in which only the reader's own
// timer feeds the voice - nothing is pumped from here (two pumps at once were
// the crash of r407).
efi_status run_launch()
{
    efi_device_path_protocol* path = file_path_of(g_launch_device, g_launch_file);
    if (!path) return EFI_NOT_FOUND;
    g_launch_stop = EFI_SUCCESS;
    if (g_launch_kind == PL_WINNT || g_launch_kind == PL_LINUX) {
        // The loader must still be there (validated again right before use).
        efi_simple_file_system_protocol* fs = nullptr;
        efi_file_protocol* root = nullptr;
        bool there = false;
        if (BS->HandleProtocol(g_launch_device, &g_sfs, (void**)&fs) == EFI_SUCCESS && fs && fs->OpenVolume(fs, &root) == EFI_SUCCESS && root) {
            there = file_exists(root, g_launch_file + (g_launch_file[0] == u'\\' ? 1 : 0));
            root->Close(root);
        }
        if (!there) { BS->FreePool(path); return EFI_NOT_FOUND; }
        MenuVoiceV1* voice = nullptr;
        if (BS->LocateProtocol(&g_menu_voice, nullptr, (void**)&voice) == EFI_SUCCESS && voice && voice->Stop) {
            // About 70 ms per character of the announcement, 2 to 6 seconds.
            size_t chars = 40;
            for (const char* c = g_launch_name; *c; c++) chars++;
            size_t ms = chars * 70;
            if (ms < 2000) ms = 2000;
            if (ms > 6000) ms = 6000;
            for (size_t waited = 0; waited < ms; waited += 50) BS->Stall(50000);
            const efi_status st = voice->Stop(voice);
            g_launch_stop = st;
            // Windows: a failed Stop forbids the transfer (the rule since r403).
            // Linux: as in Charon's menu since r400, a sound device that does
            // not stop is recorded and the start goes on; the reader stays.
            if (st != EFI_SUCCESS && g_launch_kind == PL_WINNT) { BS->FreePool(path); return st; }
        }
    }
    efi_handle child = nullptr;
    efi_status st = BS->LoadImage(false, g_image, path, nullptr, 0, &child);
    BS->FreePool(path);
    if (st != EFI_SUCCESS && child) { BS->UnloadImage(child); child = nullptr; }   // r409: a refused image (Secure Boot) is not left loaded
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
    charon_clock_local(out);   // r409: local time when the clock keeps universal time (\EFI\HAK\clock.txt)
}
void* plat_alloc(size_t bytes)
{
    void* p = nullptr;
    if (BS->AllocatePool(EfiLoaderData, bytes, &p) != EFI_SUCCESS || !p) return nullptr;
    BS->SetMem(p, bytes, 0);
    return p;
}

// ---- sound (r407) ---------------------------------------------------------------------------
bool plat_pcm_begin(void)
{
    pcm_close();
    PcmSinkV1* sink = nullptr;
    if (BS->LocateProtocol(&g_pcm_sink, nullptr, (void**)&sink) != EFI_SUCCESS || !sink) return false;
    if (sink->version < 1 || sink->size < sizeof(PcmSinkV1) || !sink->Open || !sink->Write || !sink->Queued || !sink->Close) return false;
    if (sink->Open(sink, 48000, 2) != EFI_SUCCESS) return false;
    g_pcm = sink;
    return true;
}
size_t plat_pcm_write(const int16_t* stereo, size_t frames)
{
    size_t accepted = 0;
    if (!g_pcm || g_pcm->Write(g_pcm, stereo, frames, &accepted) != EFI_SUCCESS || accepted > frames) return 0;
    return accepted;
}
size_t plat_pcm_queued(void)
{
    size_t frames = 0;
    return g_pcm && g_pcm->Queued(g_pcm, &frames) == EFI_SUCCESS ? frames : 0;
}
void plat_pcm_end(void) { pcm_close(); }

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

// ---- the CHARON command: CharonDOS -> Charon, through the same layer as WIN ----
// A CharonDOS program cannot call a UEFI protocol; the shell built-in CHARON
// does, from here (native, like plat_request_launch). Live speech uses Charon's
// published MenuVoice (g_menu_voice, above). Reader settings persist into the
// CharSettings record on the boot volume's \EFI\HAK; the boot-manager actions
// use the standard firmware variables (BootOrder / Boot#### / BootNext /
// OsIndications) and ResetSystem. Nothing here touches Secure Boot keys.
namespace {
efi_guid g_global_var = EFI_GLOBAL_VARIABLE;
// CharonReaderControl.h: readerControlGuid (read its `release` field only).
efi_guid g_reader_control = {0x5f0c2c8e, 0x8a2d, 0x4c8b, {0x9e, 0x31, 0x6a, 0x0d, 0x71, 0x2b, 0x44, 0x55}};
struct ReaderCtrlHdr { uint32_t version, size, release, reserved; };

// The boot volume's root (where \EFI\HAK lives). Caller closes it.
efi_file_protocol* hak_root()
{
    if (!g_self_device) return nullptr;
    efi_simple_file_system_protocol* fs = nullptr;
    efi_file_protocol* vol = nullptr;
    if (BS->HandleProtocol(g_self_device, &g_sfs, (void**)&fs) != EFI_SUCCESS || !fs) return nullptr;
    if (fs->OpenVolume(fs, &vol) != EFI_SUCCESS || !vol) return nullptr;
    return vol;
}
// "EFI\HAK\<name>" (name ASCII, may contain '\').
void hak_path(const char* name, char16_t* out, size_t cap)
{
    size_t n = 0;
    for (const char16_t* p = u"EFI\\HAK\\"; *p && n + 1 < cap; p++) out[n++] = *p;
    for (const char* s = name; *s && n + 1 < cap; s++) out[n++] = (char16_t)(uint8_t)*s;
    out[n] = 0;
}
void boot_var_name(uint16_t id, char16_t* out)   // "Boot####"
{
    static const char16_t hex[] = u"0123456789ABCDEF";
    out[0] = u'B'; out[1] = u'o'; out[2] = u'o'; out[3] = u't';
    for (int i = 0; i < 4; i++) out[4 + i] = hex[(id >> (12 - 4 * i)) & 15];
    out[8] = 0;
}
// Description of a Boot#### option (UTF-16 at offset 6) to ASCII.
void option_desc(const uint8_t* opt, size_t size, char* out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 6; i + 1 < size && o + 1 < cap; i += 2) {
        const uint16_t c = (uint16_t)(opt[i] | (opt[i + 1] << 8));
        if (!c) break;
        out[o++] = c < 128 ? (char)c : '?';
    }
    out[o] = 0;
}
uint16_t g_boot_order[256];
size_t read_boot_order()
{
    size_t sz = sizeof g_boot_order;
    if (ST->RuntimeServices->GetVariable((char16_t*)u"BootOrder", &g_global_var, nullptr, &sz, g_boot_order) != EFI_SUCCESS || sz % 2 || sz > sizeof g_boot_order) return 0;
    return sz / 2;
}
}  // namespace

void charon_plat_info(CharonInfo* out)
{
    out->release = 0; out->have_voice = 0; out->have_reader = 0; out->secure_boot = -1;
    MenuVoiceV1* v = nullptr;
    if (BS->LocateProtocol(&g_menu_voice, nullptr, (void**)&v) == EFI_SUCCESS && v && v->SpeakUtf8) out->have_voice = 1;
    ReaderCtrlHdr* rc = nullptr;
    if (BS->LocateProtocol(&g_reader_control, nullptr, (void**)&rc) == EFI_SUCCESS && rc && rc->version == 1) { out->have_reader = 1; out->release = rc->release; }
    uint8_t sb = 0; size_t sz = sizeof sb;
    if (ST->RuntimeServices->GetVariable((char16_t*)u"SecureBoot", &g_global_var, nullptr, &sz, &sb) == EFI_SUCCESS && sz >= 1) out->secure_boot = sb ? 1 : 0;
}

int charon_plat_speak(const char* lang, const char* text)
{
    MenuVoiceV1* v = nullptr;
    if (BS->LocateProtocol(&g_menu_voice, nullptr, (void**)&v) != EFI_SUCCESS || !v || !v->SpeakUtf8 || v->version < 1) return 0;
    return v->SpeakUtf8(v, lang, text) == EFI_SUCCESS ? 1 : 0;   // the resident reader pumps it
}

long charon_plat_hak_read(const char* name, void* buf, unsigned long cap)
{
    efi_file_protocol* root = hak_root();
    if (!root) return -2;
    char16_t path[200]; hak_path(name, path, 200);
    efi_file_protocol* f = nullptr;
    const efi_status st = root->Open(root, &f, path, EFI_FILE_MODE_READ, 0);
    if (st != EFI_SUCCESS || !f) { root->Close(root); return st == EFI_NOT_FOUND ? -1 : -2; }
    size_t n = cap;
    const efi_status rst = f->Read(f, &n, buf);
    f->Close(f); root->Close(root);
    return rst == EFI_SUCCESS ? (long)n : -2;
}

int charon_plat_hak_write(const char* name, const void* buf, unsigned long len)
{
    efi_file_protocol* root = hak_root();
    if (!root) return 0;
    char16_t path[200]; hak_path(name, path, 200);
    efi_file_protocol* f = nullptr;
    if (root->Open(root, &f, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0) == EFI_SUCCESS && f) { f->Delete(f); f = nullptr; }
    if (root->Open(root, &f, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0) != EFI_SUCCESS || !f) { root->Close(root); return 0; }
    size_t n = len;
    const bool ok = f->Write(f, &n, (void*)buf) == EFI_SUCCESS && n == len && f->Flush(f) == EFI_SUCCESS;
    f->Close(f); root->Close(root);
    return ok ? 1 : 0;
}

int charon_plat_boot_list(CharonBootEntry* out, int cap)
{
    const size_t n = read_boot_order();
    if (!n) return -1;
    static uint8_t opt[4096];
    int count = 0;
    for (size_t i = 0; i < n && count < cap; i++) {
        char16_t name[9]; boot_var_name(g_boot_order[i], name);
        size_t os = sizeof opt;
        if (ST->RuntimeServices->GetVariable(name, &g_global_var, nullptr, &os, opt) != EFI_SUCCESS || os < 6) continue;
        out[count].id = g_boot_order[i];
        option_desc(opt, os, out[count].desc, sizeof out[count].desc);
        count++;
    }
    return count;
}

int charon_plat_set_continue(uint16_t id)
{
    const size_t n = read_boot_order();
    if (!n) return 1;
    bool in = false; for (size_t i = 0; i < n; i++) if (g_boot_order[i] == id) in = true;
    if (!in) return 1;
    char16_t name[9]; boot_var_name(id, name);
    static uint8_t opt[4096]; size_t os = sizeof opt;
    if (ST->RuntimeServices->GetVariable(name, &g_global_var, nullptr, &os, opt) != EFI_SUCCESS || !os || os > 4000) return 1;
    static uint8_t rec[4096 + 32];
    static const uint8_t magic[8] = {'H', 'A', 'K', 'C', 'O', 'N', 'T', '1'};
    for (int i = 0; i < 8; i++) rec[i] = magic[i];
    rec[8] = (uint8_t)id; rec[9] = (uint8_t)(id >> 8);
    for (int i = 0; i < 4; i++) rec[10 + i] = (uint8_t)(os >> (8 * i));
    for (size_t i = 0; i < os; i++) rec[14 + i] = opt[i];
    rec[14 + os] = 'H'; rec[15 + os] = 'E'; rec[16 + os] = 'N'; rec[17 + os] = 'D';
    const uint8_t marker = 1;                                    // durable intent first (as Charon's SaveContinue)
    if (!charon_plat_hak_write("continue-set", &marker, 1)) return 2;
    if (!charon_plat_hak_write("continue.bin", rec, (unsigned long)(18 + os))) return 2;
    return 0;
}

int charon_plat_boot_next_charon(void)
{
    const size_t n = read_boot_order();
    if (!n) return 1;
    static uint8_t opt[4096];
    bool found = false; uint16_t id = 0;
    for (size_t i = 0; i < n && !found; i++) {
        char16_t name[9]; boot_var_name(g_boot_order[i], name);
        size_t os = sizeof opt;
        if (ST->RuntimeServices->GetVariable(name, &g_global_var, nullptr, &os, opt) != EFI_SUCCESS || os < 6) continue;
        char desc[64]; option_desc(opt, os, desc, sizeof desc);
        const char* c = "Charon";
        bool same = true; int k = 0; for (; c[k]; k++) if (desc[k] != c[k]) same = false;
        if (same && desc[k] == 0) { id = g_boot_order[i]; found = true; }
    }
    if (!found) return 1;
    uint16_t next = id;
    const efi_status st = ST->RuntimeServices->SetVariable((char16_t*)u"BootNext", &g_global_var,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS, sizeof next, &next);
    return st == EFI_SUCCESS ? 0 : 2;
}

int charon_plat_firmware_setup(void)
{
    uint64_t supported = 0; size_t sz = sizeof supported;
    if (ST->RuntimeServices->GetVariable((char16_t*)u"OsIndicationsSupported", &g_global_var, nullptr, &sz, &supported) != EFI_SUCCESS
        || !(supported & EFI_OS_INDICATIONS_BOOT_TO_FW_UI)) return 1;
    uint64_t indications = 0; sz = sizeof indications;
    ST->RuntimeServices->GetVariable((char16_t*)u"OsIndications", &g_global_var, nullptr, &sz, &indications);
    indications |= EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
    const efi_status st = ST->RuntimeServices->SetVariable((char16_t*)u"OsIndications", &g_global_var,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS, sizeof indications, &indications);
    return st == EFI_SUCCESS ? 0 : 2;
}

void charon_plat_reset(int mode)
{
    MenuVoiceV1* v = nullptr;                                    // stop the voice before leaving, as Charon does
    if (BS->LocateProtocol(&g_menu_voice, nullptr, (void**)&v) == EFI_SUCCESS && v && v->Stop) v->Stop(v);
    ST->RuntimeServices->ResetSystem(mode ? EfiResetShutdown : EfiResetWarm, EFI_SUCCESS, 0, nullptr);
}

int charon_plat_synth_list(char out[][64], int cap)
{
    efi_file_protocol* root = hak_root();
    if (!root) return -1;
    efi_file_protocol* dir = nullptr;
    if (root->Open(root, &dir, (char16_t*)u"EFI\\HAK\\synths", EFI_FILE_MODE_READ, 0) != EFI_SUCCESS || !dir) { root->Close(root); return 0; }
    int count = 0;
    alignas(8) static uint8_t entry[1100];
    for (int guard = 0; guard < 128 && count < cap; guard++) {
        size_t bytes = sizeof entry;
        if (dir->Read(dir, &bytes, entry) != EFI_SUCCESS || !bytes) break;
        const efi_file_info* info = (const efi_file_info*)entry;
        if (info->Attribute & EFI_FILE_DIRECTORY) continue;
        size_t len = 0; while (info->FileName[len] && len < 62) len++;
        static const char16_t ext[] = u".manifest"; const size_t el = 9;
        if (len <= el) continue;
        bool match = true;
        for (size_t i = 0; i < el && match; i++) { char16_t c = info->FileName[len - el + i]; if (c >= u'A' && c <= u'Z') c = (char16_t)(c + 32); if (c != ext[i]) match = false; }
        if (!match) continue;
        size_t o = 0; for (size_t i = 0; i < len && o + 1 < 64; i++) out[count][o++] = info->FileName[i] < 128 ? (char)info->FileName[i] : '?';
        out[count][o] = 0;
        count++;
    }
    dir->Close(dir); root->Close(root);
    return count;
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
        pcm_close();   // r407: the emulator opens the sink for itself
        if (timer) BS->SetTimer(timer, TimerCancel, 0);
        for (int i = 0; i < 26; i++) if (g_root[i]) { g_root[i]->Close(g_root[i]); g_root[i] = nullptr; g_prefix[i][0] = 0; }
        Out->SetAttribute(Out, (size_t)(old_attr & 0x7F));
        Out->ClearScreen(Out);
        const int kind = g_launch_kind;
        const efi_status st = run_launch();
        g_launch_kind = 0;
        if (kind == PL_WINNT || kind == PL_LINUX) {
            // r409: the loader came back, or it could not start. Said with the
            // system's name and the reason; Secure Boot is named as such.
            static char16_t line[400];
            char16_t name[80];
            size_t n = 0;
            for (const uint8_t* c = (const uint8_t*)g_launch_name; *c && n + 1 < 80;) {   // UTF-8 -> UTF-16 (BMP)
                uint32_t u = *c++;
                int more = u >= 0xE0 ? 2 : u >= 0xC0 ? 1 : 0;
                if (u >= 0xF0) { u = '?'; more = 0; while ((*c & 0xC0) == 0x80) c++; }
                else if (more) u &= more == 2 ? 0x0F : 0x1F;
                for (; more && (*c & 0xC0) == 0x80; more--) u = (u << 6) | (*c++ & 0x3F);
                name[n++] = (char16_t)u;
            }
            name[n] = 0;
            const bool refused = st == EFI_SECURITY_VIOLATION || st == EFI_ACCESS_DENIED;
            wcopy(line, u"CharonDOS: ", 400);
            wcat(line, name, 400);
            if (options.polish) {
                wcat(line, u" nie wystartował (", 400);
                wcat(line, refused ? u"Firmware odrzucił ten program rozruchowy: Secure Boot."
                    : st == EFI_NOT_FOUND ? u"nie znaleziono programu rozruchowego"
                    : st == EFI_SUCCESS ? u"program rozruchowy zakończył pracę i wrócił"
                    : u"program rozruchowy zgłosił błąd albo nie mógł wystartować", 400);
            } else {
                wcat(line, u" did not start (", 400);
                wcat(line, refused ? u"The firmware refused this loader: Secure Boot."
                    : st == EFI_NOT_FOUND ? u"the boot loader was not found"
                    : st == EFI_SUCCESS ? u"the boot loader ended and came back"
                    : u"the boot loader reported an error or could not start", 400);
            }
            wcat(line, u").\r\n", 400);
            Out->OutputString(Out, line);
            if (kind == PL_WINNT && g_launch_stop != EFI_SUCCESS)
                Out->OutputString(Out, (char16_t*)(options.polish ? u"CharonDOS: nie udało się zatrzymać dźwięku, więc Windows nie został uruchomiony.\r\n"
                                                                  : u"CharonDOS: the sound could not be stopped, so Windows was not started.\r\n"));
        } else if (st != EFI_SUCCESS) {
            // The emulator could not start.
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

    pcm_close();   // r407
    if (timer) { BS->SetTimer(timer, TimerCancel, 0); BS->CloseEvent(timer); }
    for (int i = 0; i < 26; i++) if (g_root[i]) g_root[i]->Close(g_root[i]);
    Out->SetAttribute(Out, (size_t)(old_attr & 0x7F));
    if (switched) Out->SetMode(Out, (size_t)old_mode);
    else if (result == EFI_SUCCESS) Out->OutputString(Out, (char16_t*)u"\r\n");
    Out->EnableCursor(Out, old_cursor);
    return result;
}

// ---- WINNT / LINUX (r409): the installed systems ---------------------------------
// Collects candidates in the fixed order of launch_select.h: Charon's registry
// \EFI\HAK\systems.txt on any volume, the firmware entries BootOrder ->
// Boot####, a scan of \EFI on every volume. Only reads: no variable and no
// file is written here except BOOT.CFG by LINUX /D and WINNT /D.
namespace {
efi_guid g_block_io = {0x964e5b21, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
struct BlockIoMedia {            // EFI_BLOCK_IO_MEDIA, the fields up to LastBlock
    uint32_t MediaId;
    bool RemovableMedia, MediaPresent, LogicalPartition, ReadOnly, WriteCaching;
    uint32_t BlockSize, IoAlign;
    uint64_t LastBlock;
};
struct BlockIoHead { uint64_t Revision; BlockIoMedia* Media; };

struct Vol { efi_handle handle; char part[40]; uint16_t disk; };
Vol g_vols[32];
int g_nvols;

// Length of a device path without its end node; 0 when it is malformed.
size_t path_bytes(const uint8_t* path)
{
    size_t total = 0;
    for (int guard = 0; path && guard < 64; guard++) {
        const uint32_t len = (uint32_t)(path[total + 2] | (path[total + 3] << 8));
        if (path[total] == DEVICE_PATH_END) return total;
        if (len < 4 || total + len > 4096) return 0;
        total += len;
    }
    return 0;
}
// The hard-drive node with a GPT partition GUID inside a device path.
const uint8_t* hd_node(const uint8_t* path, size_t bytes)
{
    for (size_t at = 0; at + 4 <= bytes;) {
        const uint32_t len = (uint32_t)(path[at + 2] | (path[at + 3] << 8));
        if (len < 4 || at + len > bytes) break;
        if (path[at] == DEVICE_PATH_MEDIA && path[at + 1] == 1 && len >= 42 && path[at + 41] == 2) return path + at;
        at += len;
    }
    return nullptr;
}
void guid_text(const uint8_t* g, char* out)
{
    static const int order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    static const char hex[] = "0123456789abcdef";
    size_t n = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[n++] = '-';
        out[n++] = hex[g[order[i]] >> 4]; out[n++] = hex[g[order[i]] & 15];
    }
    out[n] = 0;
}
// Number of the disk a volume is on, counted as Charon's own disk list counts
// (src/efi/CharonWsDisks.h Load: whole disks with media in handle order,
// without optical drives and memory disks). 0 = unknown.
uint16_t disk_number(const uint8_t* volume_path, size_t volume_bytes)
{
    size_t count = 0;
    efi_handle* handles = nullptr;
    if (BS->LocateHandleBuffer(ByProtocol, &g_block_io, nullptr, &count, &handles) != EFI_SUCCESS || !handles) return 0;
    uint16_t number = 0, found = 0;
    for (size_t i = 0; i < count && !found; i++) {
        BlockIoHead* io = nullptr;
        uint8_t* path = nullptr;
        if (BS->HandleProtocol(handles[i], &g_block_io, (void**)&io) != EFI_SUCCESS || !io || !io->Media
            || io->Media->LogicalPartition || !io->Media->MediaPresent || io->Media->BlockSize == 2048) continue;
        if (BS->HandleProtocol(handles[i], &g_device_path, (void**)&path) != EFI_SUCCESS || !path) continue;
        const size_t bytes = path_bytes(path);
        if (!bytes) continue;
        bool memory = false;
        for (size_t at = 0; at + 4 <= bytes;) {
            const uint32_t len = (uint32_t)(path[at + 2] | (path[at + 3] << 8));
            if (len < 4) break;
            if ((path[at] == 1 && path[at + 1] == 3) || (path[at] == 4 && path[at + 1] == 2)) memory = true;
            at += len;
        }
        if (memory) continue;
        number++;
        if (bytes > volume_bytes) continue;
        bool same = true;
        for (size_t k = 0; same && k < bytes; k++) if (path[k] != volume_path[k]) same = false;
        if (same) found = number;
    }
    BS->FreePool(handles);
    return found;
}
void find_volumes()
{
    g_nvols = 0;
    size_t count = 0;
    efi_handle* handles = nullptr;
    if (BS->LocateHandleBuffer(ByProtocol, &g_sfs, nullptr, &count, &handles) != EFI_SUCCESS || !handles) return;
    for (size_t i = 0; i < count && g_nvols < 32; i++) {
        Vol* v = &g_vols[g_nvols];
        v->handle = handles[i]; v->disk = 0; v->part[0] = 0;
        uint8_t* path = nullptr;
        if (BS->HandleProtocol(handles[i], &g_device_path, (void**)&path) == EFI_SUCCESS && path) {
            const size_t bytes = path_bytes(path);
            const uint8_t* hd = bytes ? hd_node(path, bytes) : nullptr;
            if (hd) { guid_text(hd + 24, v->part); v->disk = disk_number(path, bytes); }
        }
        if (!v->part[0]) {
            // No GPT partition behind it (a disc, a memory disk): scanned, under a name of its own.
            launch_copy(v->part, sizeof v->part, "volume-");
            char digits[4] = {(char)('0' + g_nvols / 10), (char)('0' + g_nvols % 10), 0, 0};
            launch_cat(v->part, sizeof v->part, digits);
        }
        g_nvols++;
    }
    BS->FreePool(handles);
}
efi_file_protocol* open_volume(efi_handle handle)
{
    efi_simple_file_system_protocol* fs = nullptr;
    efi_file_protocol* root = nullptr;
    if (BS->HandleProtocol(handle, &g_sfs, (void**)&fs) != EFI_SUCCESS || !fs) return nullptr;
    if (fs->OpenVolume(fs, &root) != EFI_SUCCESS) return nullptr;
    return root;
}
// "\EFI\x" (UTF-8, ASCII in practice) -> "EFI\x" in UTF-16.
void wide_rel(const char* path, char16_t* out, size_t cap)
{
    size_t n = 0;
    if (path[0] == '\\') path++;
    for (; *path && n + 1 < cap; path++) out[n++] = (char16_t)(uint8_t)*path;
    out[n] = 0;
}
bool vol_file(void* context, const char* path, uint32_t* size)
{
    efi_file_protocol* root = (efi_file_protocol*)context;
    char16_t wide[200];
    wide_rel(path, wide, 200);
    efi_file_protocol* f = nullptr;
    if (root->Open(root, &f, wide, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS || !f) return false;
    const efi_file_info* info = info_of(f);
    const bool ok = info && !(info->Attribute & EFI_FILE_DIRECTORY);
    if (ok && size) *size = info->FileSize > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)info->FileSize;
    f->Close(f);
    return ok;
}
int vol_names(void* context, const char* folder, bool folders, char (*out)[64], int cap)
{
    efi_file_protocol* root = (efi_file_protocol*)context;
    char16_t wide[200];
    wide_rel(folder, wide, 200);
    efi_file_protocol* dir = nullptr;
    if (root->Open(root, &dir, wide, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS || !dir) return 0;
    int n = 0;
    for (int guard = 0; guard < 512 && n < cap; guard++) {
        alignas(8) static uint8_t listing[1100];
        size_t size = sizeof listing;
        if (dir->Read(dir, &size, listing) != EFI_SUCCESS || !size) break;
        const efi_file_info* info = (const efi_file_info*)listing;
        const bool is_folder = (info->Attribute & EFI_FILE_DIRECTORY) != 0;
        if (is_folder != folders || info->FileName[0] == u'.') continue;
        size_t k = 0;
        bool plain = true;
        for (; info->FileName[k] && k < 63; k++) {
            if (info->FileName[k] >= 128) plain = false;
            out[n][k] = (char)info->FileName[k];
        }
        out[n][k] = 0;
        if (plain && !info->FileName[k]) n++;      // names that are not plain ASCII or too long are not loaders
    }
    dir->Close(dir);
    return n;
}
long read_file(efi_file_protocol* root, const char16_t* path, char* buffer, unsigned long capacity)
{
    efi_file_protocol* f = nullptr;
    if (!root || root->Open(root, &f, (char16_t*)path, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS || !f) return -1;
    size_t n = capacity;
    const efi_status st = f->Read(f, &n, buffer);
    f->Close(f);
    return st == EFI_SUCCESS ? (long)n : -1;
}
const Vol* vol_of(const char* part)
{
    for (int i = 0; i < g_nvols; i++) if (launch_ieq(g_vols[i].part, part)) return &g_vols[i];
    return nullptr;
}
// One Boot#### option: description, and partition GUID + file path of its
// first device path (short form HD()/File() or a full path).
bool option_target(const uint8_t* opt, size_t size, char* desc, size_t desc_cap, char* part, char* path, size_t path_cap)
{
    if (size < 8) return false;
    const size_t list = (size_t)(opt[4] | (opt[5] << 8));
    option_desc(opt, size, desc, desc_cap);
    size_t at = 6;
    while (at + 1 < size && (opt[at] || opt[at + 1])) at += 2;
    at += 2;
    if (at >= size || list < 4 || at + list > size) return false;
    const uint8_t* dp = opt + at;
    const uint8_t* hd = nullptr;
    size_t n = 0;
    path[0] = 0;
    for (size_t p = 0; p + 4 <= list;) {
        const uint32_t len = (uint32_t)(dp[p + 2] | (dp[p + 3] << 8));
        if (len < 4 || p + len > list || dp[p] == DEVICE_PATH_END) break;
        if (dp[p] == DEVICE_PATH_MEDIA && dp[p + 1] == 1 && len >= 42 && dp[p + 41] == 2) hd = dp + p;
        if (dp[p] == DEVICE_PATH_MEDIA && dp[p + 1] == MEDIA_FILEPATH_DP && hd) {
            if (n && path[n - 1] != '\\' && n + 1 < path_cap) path[n++] = '\\';
            for (uint32_t i = 4; i + 1 < len; i += 2) {
                const uint16_t c = (uint16_t)(dp[p + i] | (dp[p + i + 1] << 8));
                if (!c) break;
                if (c >= 128 || n + 1 >= path_cap) return false;
                if (c == '\\' && n && path[n - 1] == '\\') continue;
                path[n++] = (char)c;
            }
            path[n] = 0;
        }
        p += len;
    }
    if (!hd || !path[0]) return false;
    if (path[0] != '\\') {
        if (n + 2 > path_cap) return false;
        for (size_t i = n + 1; i > 0; i--) path[i] = path[i - 1];
        path[0] = '\\';
    }
    guid_text(hd + 24, part);
    return true;
}
}  // namespace

int plat_launch_discover(LaunchList* out)
{
    static char text[8192];
    find_volumes();
    // 1. Charon's registry, on any volume.
    for (int i = 0; i < g_nvols; i++) {
        efi_file_protocol* root = open_volume(g_vols[i].handle);
        if (!root) continue;
        const long n = read_file(root, u"EFI\\HAK\\systems.txt", text, sizeof text);
        root->Close(root);
        if (n > 0) launch_parse_registry(text, (size_t)n, out);
    }
    for (int i = 0; i < out->count;) {
        // The registry is a source of names: a line whose loader is gone is not an installed system.
        const Vol* v = vol_of(out->e[i].part);
        efi_file_protocol* root = v ? open_volume(v->handle) : nullptr;
        const bool there = root && vol_file(root, out->e[i].path, nullptr);
        if (root) root->Close(root);
        if (there) { if (!out->e[i].disk) out->e[i].disk = v->disk; i++; }
        else launch_remove(out, i);
    }
    // 2. Firmware entries, in BootOrder.
    const size_t orders = read_boot_order();
    for (size_t i = 0; i < orders; i++) {
        static uint8_t opt[4096];
        char16_t name[9]; boot_var_name(g_boot_order[i], name);
        size_t size = sizeof opt;
        if (ST->RuntimeServices->GetVariable(name, &g_global_var, nullptr, &size, opt) != EFI_SUCCESS) continue;
        char desc[64], part[40], path[128];
        if (!option_target(opt, size, desc, sizeof desc, part, path, sizeof path)) continue;
        const Vol* v = vol_of(part);
        if (!v) continue;                       // its partition is not there: not startable from here
        launch_add_firmware(out, desc, part, path, v->disk);
    }
    // 3. Scan of every volume.
    for (int i = 0; i < g_nvols; i++) {
        efi_file_protocol* root = open_volume(g_vols[i].handle);
        if (!root) continue;
        LaunchVolume v;
        v.context = root; v.part = g_vols[i].part; v.disk = g_vols[i].disk;
        v.file = vol_file; v.names = vol_names;
        launch_scan_volume(&v, out);
        root->Close(root);
    }
    return out->count;
}

static bool cfg_path(char16_t* out, size_t cap)
{
    if (!g_self_device) return false;
    wcopy(out, g_self_dir, cap); wcat(out, u"\\BOOT.CFG", cap);
    return true;
}
long plat_launch_cfg_read(char* buffer, unsigned long capacity)
{
    char16_t path[320];
    if (!cfg_path(path, 320)) return -1;
    efi_file_protocol* root = open_volume(g_self_device);
    if (!root) return -1;
    const long n = read_file(root, path + (path[0] == u'\\' ? 1 : 0), buffer, capacity);
    root->Close(root);
    return n;
}
int plat_launch_cfg_write(const char* data, unsigned long length)
{
    char16_t full[320];
    if (!cfg_path(full, 320)) return 0;
    char16_t* path = full + (full[0] == u'\\' ? 1 : 0);
    efi_file_protocol* root = open_volume(g_self_device);
    if (!root) return 0;
    efi_file_protocol* f = nullptr;
    if (root->Open(root, &f, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0) == EFI_SUCCESS && f) { f->Delete(f); f = nullptr; }
    if (root->Open(root, &f, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0) != EFI_SUCCESS || !f) { root->Close(root); return 0; }
    size_t n = length;
    const bool ok = (!length || (f->Write(f, &n, (void*)data) == EFI_SUCCESS && n == length)) && f->Flush(f) == EFI_SUCCESS;
    f->Close(f); root->Close(root);
    return ok ? 1 : 0;
}

int plat_request_launch_entry(const LaunchEntry* entry)
{
    g_launch_kind = 0;
    if (entry->kind != PL_WINNT && entry->kind != PL_LINUX) return 2;
    find_volumes();                             // the handles are looked up again: nothing is cached
    const Vol* v = vol_of(entry->part);
    if (!v) return 2;
    efi_file_protocol* root = open_volume(v->handle);
    if (!root) return 2;
    const bool there = vol_file(root, entry->path, nullptr);
    root->Close(root);
    if (!there) return 2;
    g_launch_device = v->handle;
    size_t n = 0;
    for (; entry->path[n] && n + 1 < 300; n++) g_launch_file[n] = (char16_t)(uint8_t)entry->path[n];
    g_launch_file[n] = 0;
    launch_copy(g_launch_name, sizeof g_launch_name, entry->name);
    g_launch_kind = entry->kind;
    return 0;
}
