// CharonPC - UEFI side: entry point, memory, volume files, time, console,
// Charon's voice. GPL-2.0-or-later.
//
// CHARONPC.EFI is a UEFI application. CharonDOS starts it with the command
// WIN [profile] and passes LoadOptions "config=<path>\CHARONPC.CFG [pl]"
// (path on the volume CHARONPC.EFI was loaded from). Without options it reads
// CHARONPC.CFG from its own folder. It runs Bochs (on a private 8 MiB stack,
// src/synth/common/synth_coro.c) until the emulated machine is switched off
// or the user presses Ctrl+Shift+F12, then closes every file, stops its
// speech, restores the console and returns to its caller.
#include <stdio.h>
#include <string.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/file.h>
#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>
#include "pc_glue.h"
#include "pc_platform.h"
#include "synth_rt.h"

extern "C" void synth_run_constructors(void);
extern "C" const unsigned char pc_rom_bios[], pc_rom_bios_end[], pc_rom_vgabios[], pc_rom_vgabios_end[];

namespace pc {
efi_system_table* ST;
efi_boot_services* BS;
efi_simple_text_output_protocol* Out;
InputEx* InEx;
efi_handle Image;
bool polish;
size_t consoleColumns = 80, consoleRows = 25;
GlueSettings settings;
}  // namespace pc

using namespace pc;

namespace {

efi_file_protocol* volumeRoot;
uint64_t tscStart, tscPerUs = 1;
int64_t epochStart;
char bochsrc[24 * 1024];
size_t bochsrcBytes;
char cfgDirectory[300];
char calibration[128];

// ---- small helpers ---------------------------------------------------------
size_t Utf8ToPath(const char* path, char16_t* out, size_t capacity)
{
    size_t n = 0;
    if (path[0] != '/' && path[0] != '\\' && n + 1 < capacity) out[n++] = u'\\';
    for (const unsigned char* p = (const unsigned char*)path; *p && n + 1 < capacity;) {
        uint32_t c = *p++;
        if (c >= 0xC0 && (*p & 0xC0) == 0x80) {
            if (c < 0xE0) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
            else if ((p[1] & 0xC0) == 0x80) { c = ((c & 0x0F) << 12) | ((uint32_t)(p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
        }
        out[n++] = c == '/' ? u'\\' : (char16_t)c;
    }
    out[n] = 0;
    return n;
}

void Print(const char16_t* text) { Out->OutputString(Out, const_cast<char16_t*>(text)); }
void PrintAscii(const char* text)
{
    char16_t buffer[160];
    while (*text) {
        size_t n = 0;
        while (*text && n + 1 < 160) {
            if (*text == '\n' && n + 2 < 160) buffer[n++] = u'\r';
            buffer[n++] = (char16_t)(unsigned char)*text++;
        }
        buffer[n] = 0;
        Print(buffer);
    }
}

uint64_t Rdtsc()
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

void WaitKeyOrSeconds(unsigned seconds)
{
    for (unsigned i = 0; i < seconds * 20; ++i) {
        efi_input_key key;
        if (ST->ConIn->ReadKeyStroke(ST->ConIn, &key) == EFI_SUCCESS) return;
        BS->Stall(50000);
    }
}

// ---- the log ---------------------------------------------------------------
char logTail[2048];
size_t logTailBytes;
void* logHandle;
uint64_t logPosition;
bool logOpenTried;

void KeepTail(const char* text, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i) {
        if (logTailBytes == sizeof logTail) {
            memmove(logTail, logTail + 512, sizeof logTail - 512);
            logTailBytes -= 512;
        }
        logTail[logTailBytes++] = text[i];
    }
}

void OpenLogFile()
{
    if (logOpenTried) return;
    logOpenTried = true;
    if (!settings.logFile[0]) return;
    uint64_t size = 0;
    if (pc_vol_open(settings.logFile, 1, 1, 1, &logHandle, &size)) logHandle = nullptr;
}
}  // namespace

// ---- memory ----------------------------------------------------------------
// Every block carries a 32-byte header and sits on one list, so everything
// Bochs allocated (guest RAM included) is returned to the firmware when
// CHARONPC exits: UEFI does not free an application's memory by itself.
namespace {
constexpr uint64_t poolMagic = 0x4c4f4f50435043ull, pageMagic = 0x45474150435043ull;
struct BlockHeader { uint64_t magic, value; BlockHeader* previous; BlockHeader* next; };
static_assert(sizeof(BlockHeader) == 32, "block header");
BlockHeader* blocks;
void Link(BlockHeader* h) { h->previous = nullptr; h->next = blocks; if (blocks) blocks->previous = h; blocks = h; }
void Unlink(BlockHeader* h)
{
    if (h->previous) h->previous->next = h->next; else blocks = h->next;
    if (h->next) h->next->previous = h->previous;
}
void FreeBlock(BlockHeader* h)
{
    const uint64_t magic = h->magic;
    h->magic = 0;
    if (magic == pageMagic) BS->FreePages((efi_physical_addr)(uintptr_t)h, (size_t)h->value);
    else if (magic == poolMagic) BS->FreePool(reinterpret_cast<void*>(h->value));
}
void ReleaseEverything()
{
    while (blocks) { BlockHeader* h = blocks; Unlink(h); FreeBlock(h); }
}
}  // namespace
extern "C" void* pc_allocate(void*, size_t bytes)
{
    if (bytes > ((size_t)1 << 40)) return nullptr;
    BlockHeader* h = nullptr;
    if (bytes >= ((size_t)1 << 20)) {
        const size_t pages = (bytes + sizeof(BlockHeader) + 4095) / 4096;
        efi_physical_addr address = 0;
        if (BS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &address) != EFI_SUCCESS || !address) return nullptr;
        h = reinterpret_cast<BlockHeader*>(address);
        h->magic = pageMagic;
        h->value = pages;
    } else {
        void* p = nullptr;
        if (BS->AllocatePool(EfiLoaderData, bytes + sizeof(BlockHeader) + 16, &p) != EFI_SUCCESS || !p) return nullptr;
        const uintptr_t aligned = ((uintptr_t)p + sizeof(BlockHeader) + 15) & ~(uintptr_t)15;
        h = reinterpret_cast<BlockHeader*>(aligned) - 1;
        h->magic = poolMagic;
        h->value = (uint64_t)(uintptr_t)p;
    }
    Link(h);
    return h + 1;
}

extern "C" void pc_release(void*, void* memory)
{
    if (!memory) return;
    BlockHeader* h = static_cast<BlockHeader*>(memory) - 1;
    if (h->magic != pageMagic && h->magic != poolMagic) return;
    Unlink(h);
    FreeBlock(h);
}

// ---- volume files ----------------------------------------------------------
namespace {
struct VolumeFile { efi_file_protocol* file; };
efi_guid fileInfoGuid = EFI_FILE_INFO_GUID;

int ErrnoOf(efi_status status)
{
    switch (status) {
    case EFI_NOT_FOUND: return 2;            // ENOENT
    case EFI_WRITE_PROTECTED: return 30;     // EROFS
    case EFI_ACCESS_DENIED: return 13;       // EACCES
    case EFI_VOLUME_FULL: return 28;         // ENOSPC
    case EFI_OUT_OF_RESOURCES: return 12;    // ENOMEM
    default: return 5;                       // EIO
    }
}

efi_status FileSize(efi_file_protocol* file, uint64_t* size, bool* directory)
{
    alignas(8) unsigned char info[sizeof(efi_file_info) + 600];
    size_t bytes = sizeof info;
    efi_status status = file->GetInfo(file, &fileInfoGuid, &bytes, info);
    if (status != EFI_SUCCESS) return status;
    const efi_file_info* fi = reinterpret_cast<const efi_file_info*>(info);
    if (size) *size = fi->FileSize;
    if (directory) *directory = (fi->Attribute & EFI_FILE_DIRECTORY) != 0;
    return EFI_SUCCESS;
}
}  // namespace

extern "C" int pc_vol_open(const char* path, int write, int create, int truncate, void** handle, uint64_t* size)
{
    char16_t name[600];
    Utf8ToPath(path, name, 600);
    uint64_t mode = EFI_FILE_MODE_READ | (write ? EFI_FILE_MODE_WRITE : 0) | (create ? EFI_FILE_MODE_CREATE : 0);
    efi_file_protocol* file = nullptr;
    efi_status status = volumeRoot->Open(volumeRoot, &file, name, mode, 0);
    if (status != EFI_SUCCESS || !file) return ErrnoOf(status);
    bool directory = false;
    status = FileSize(file, size, &directory);
    if (status != EFI_SUCCESS || directory) { file->Close(file); return directory ? 21 : ErrnoOf(status); }
    if (truncate && write && *size) {
        if (pc_vol_set_size(file, 0)) { file->Close(file); return 5; }
        *size = 0;
    }
    *handle = file;
    return 0;
}

extern "C" long pc_vol_read(void* handle, uint64_t position, void* buffer, size_t bytes)
{
    efi_file_protocol* file = static_cast<efi_file_protocol*>(handle);
    if (file->SetPosition(file, position) != EFI_SUCCESS) return -1;
    size_t done = 0;
    while (done < bytes) {
        size_t chunk = bytes - done;
        if (chunk > ((size_t)1 << 22)) chunk = (size_t)1 << 22;
        if (file->Read(file, &chunk, static_cast<unsigned char*>(buffer) + done) != EFI_SUCCESS) return done ? (long)done : -1;
        if (!chunk) break;
        done += chunk;
    }
    return (long)done;
}

extern "C" long pc_vol_write(void* handle, uint64_t position, const void* buffer, size_t bytes)
{
    efi_file_protocol* file = static_cast<efi_file_protocol*>(handle);
    if (file->SetPosition(file, position) != EFI_SUCCESS) return -1;
    size_t chunk = bytes;
    if (file->Write(file, &chunk, const_cast<void*>(buffer)) != EFI_SUCCESS) return -1;
    return (long)chunk;
}

extern "C" int pc_vol_set_size(void* handle, uint64_t size)
{
    efi_file_protocol* file = static_cast<efi_file_protocol*>(handle);
    alignas(8) unsigned char info[sizeof(efi_file_info) + 600];
    size_t bytes = sizeof info;
    if (file->GetInfo(file, &fileInfoGuid, &bytes, info) != EFI_SUCCESS) return 5;
    reinterpret_cast<efi_file_info*>(info)->FileSize = size;
    return file->SetInfo(file, &fileInfoGuid, bytes, info) == EFI_SUCCESS ? 0 : 5;
}

extern "C" int pc_vol_flush(void* handle)
{
    efi_file_protocol* file = static_cast<efi_file_protocol*>(handle);
    return file->Flush(file) == EFI_SUCCESS ? 0 : 5;
}

extern "C" void pc_vol_close(void* handle)
{
    efi_file_protocol* file = static_cast<efi_file_protocol*>(handle);
    if (file) file->Close(file);
}

extern "C" int pc_vol_stat(const char* path, uint64_t* size, int* kind)
{
    char16_t name[600];
    Utf8ToPath(path, name, 600);
    efi_file_protocol* file = nullptr;
    efi_status status = volumeRoot->Open(volumeRoot, &file, name, EFI_FILE_MODE_READ, 0);
    if (status != EFI_SUCCESS || !file) return ErrnoOf(status);
    bool directory = false;
    status = FileSize(file, size, &directory);
    file->Close(file);
    if (status != EFI_SUCCESS) return ErrnoOf(status);
    *kind = directory ? 2 : 1;
    return 0;
}

extern "C" int pc_vol_delete(const char* path)
{
    char16_t name[600];
    Utf8ToPath(path, name, 600);
    efi_file_protocol* file = nullptr;
    efi_status status = volumeRoot->Open(volumeRoot, &file, name, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (status != EFI_SUCCESS || !file) return ErrnoOf(status);
    return file->Delete(file) == EFI_SUCCESS ? 0 : 5;
}

extern "C" const unsigned char* pc_rom(const char* name, size_t* size)
{
    if (!strcmp(name, "BIOS-bochs-latest")) { *size = (size_t)(pc_rom_bios_end - pc_rom_bios); return pc_rom_bios; }
    if (!strcmp(name, "VGABIOS-lgpl-latest.bin")) { *size = (size_t)(pc_rom_vgabios_end - pc_rom_vgabios); return pc_rom_vgabios; }
    if (!strcmp(name, "bochsrc")) { *size = bochsrcBytes; return reinterpret_cast<const unsigned char*>(bochsrc); }
    return nullptr;
}

// ---- time ------------------------------------------------------------------
extern "C" uint64_t pc_usec(void) { return (Rdtsc() - tscStart) / tscPerUs; }
extern "C" int64_t pc_epoch_start(void) { return epochStart; }
extern "C" void pc_stall(uint32_t microseconds)
{
    // Long waits are cut so the voice keeps being fed.
    while (microseconds) {
        uint32_t slice = microseconds > 2000 ? 2000 : microseconds;
        BS->Stall(slice);
        microseconds -= slice;
        AudioService();   // r407: the guest's sound keeps flowing to CharonPC's own card
        PumpVoice();
    }
}

// ---- log -------------------------------------------------------------------
extern "C" void pc_log_write(const char* text, size_t bytes)
{
    KeepTail(text, bytes);
    if (settings.debugcon)
        for (size_t i = 0; i < bytes; ++i) __asm__ volatile("outb %0, %1" : : "a"((uint8_t)text[i]), "Nd"((uint16_t)0x402));
    if (settings.debugserial)
        for (size_t i = 0; i < bytes; ++i) {
            uint8_t status = 0;
            for (unsigned spin = 0; spin < 20000; ++spin) {
                __asm__ volatile("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)0x3FD));
                if (status & 0x20) break;
            }
            __asm__ volatile("outb %0, %1" : : "a"((uint8_t)text[i]), "Nd"((uint16_t)0x3F8));
        }
    OpenLogFile();
    if (logHandle) {
        long n = pc_vol_write(logHandle, logPosition, text, bytes);
        if (n > 0) logPosition += (uint64_t)n;
        // r406: the log reaches the disk every two seconds, so a run that is switched off still leaves it.
        static uint64_t flushed;
        const uint64_t now = tscPerUs ? pc_usec() : 0;
        if (now - flushed > 2000000) { flushed = now; pc_vol_flush(logHandle); }
    }
}

// ---- voice -----------------------------------------------------------------
namespace {
hakefi::MenuVoice* voice;
bool voiceLooked;
}
hakefi::MenuVoice* pc::Voice()
{
    if (!voiceLooked) { voiceLooked = true; voice = hakefi::FindMenuVoice(BS); }
    return voice;
}
void pc::PumpVoice()
{
    hakefi::MenuVoice* v = voice;
    if (!v || hakefi::VoiceSwitching()) return;
    // r407: a resident reader pumps the same voice from its timer (TPL_CALLBACK). That notification must not
    // interrupt this call: two pumps inside each other wrote the sound card's ring out of order (the guest's
    // tone lost and repeated pieces). The emulator runs at TPL_APPLICATION.
    const efi_tpl old = BS->RaiseTPL(TPL_CALLBACK);
    v->Pump(v, 2048);
    BS->RestoreTPL(old);
}
bool pc::Say(const char* utf8)
{
    hakefi::MenuVoice* v = Voice();
    if (!v || hakefi::VoiceSwitching()) return false;
    // r407: like the pump, not to be interrupted by the resident reader's timer (see PumpVoice)
    const efi_tpl old = BS->RaiseTPL(TPL_CALLBACK);
    const bool ok = v->SpeakUtf8(v, polish ? "pl" : "en", utf8) == EFI_SUCCESS;
    BS->RestoreTPL(old);
    return ok;
}
void pc::StopVoice()
{
    if (!voice) return;
    const efi_tpl old = BS->RaiseTPL(TPL_CALLBACK);
    voice->Stop(voice);
    BS->RestoreTPL(old);
}

// ---- the Bochs run ---------------------------------------------------------
namespace {
int bochsResult = -1;
void RunBochs(void*)
{
    synth_run_constructors();
    bochsResult = pc_bochs_main();
}

bool ReadWholeFile(const char* path, char* buffer, size_t capacity, size_t* bytes)
{
    void* handle = nullptr;
    uint64_t size = 0;
    if (pc_vol_open(path, 0, 0, 0, &handle, &size)) return false;
    bool ok = size < capacity;
    long n = ok ? pc_vol_read(handle, 0, buffer, (size_t)size) : -1;
    pc_vol_close(handle);
    if (!ok || n != (long)size) return false;
    buffer[size] = 0;
    *bytes = (size_t)size;
    return true;
}

// LoadOptions: "config=<path> [pl]" (UTF-16). Copies the path as UTF-8.
void ParseOptions(const efi_loaded_image_protocol* loaded, char* config, size_t capacity)
{
    config[0] = 0;
    const char16_t* text = static_cast<const char16_t*>(loaded->LoadOptions);
    size_t units = loaded->LoadOptionsSize / 2;
    if (!text) return;
    size_t i = 0;
    while (i < units && text[i]) {
        while (i < units && text[i] == u' ') ++i;
        size_t start = i;
        while (i < units && text[i] && text[i] != u' ') ++i;
        size_t length = i - start;
        const char16_t* word = text + start;
        if (length == 2 && (word[0] | 0x20) == u'p' && (word[1] | 0x20) == u'l') polish = true;
        else if (length > 7 && word[0] == u'c' && word[1] == u'o' && word[2] == u'n' && word[3] == u'f' && word[4] == u'i'
                 && word[5] == u'g' && word[6] == u'=') {
            size_t n = 0;
            for (size_t k = 7; k < length && n + 4 < capacity; ++k) {
                char16_t c = word[k];
                if (c < 0x80) config[n++] = (char)c;
                else if (c < 0x800) { config[n++] = (char)(0xC0 | (c >> 6)); config[n++] = (char)(0x80 | (c & 0x3F)); }
                else { config[n++] = (char)(0xE0 | (c >> 12)); config[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); config[n++] = (char)(0x80 | (c & 0x3F)); }
            }
            config[n] = 0;
        }
    }
}

// The folder of this image's file path (MEDIA file path nodes), "\..." UTF-8.
void OwnFolder(const efi_loaded_image_protocol* loaded, char* out, size_t capacity)
{
    out[0] = 0;
    const unsigned char* node = reinterpret_cast<const unsigned char*>(loaded->FilePath);
    size_t n = 0;
    for (int guard = 0; node && guard < 64; ++guard) {
        const size_t length = (size_t)node[2] | (size_t)node[3] << 8;
        if (node[0] == 0x7f || length < 4) break;
        if (node[0] == 4 && node[1] == 4) {
            const char16_t* name = reinterpret_cast<const char16_t*>(node + 4);
            for (size_t k = 0; k < (length - 4) / 2 && name[k] && n + 2 < capacity; ++k) {
                if (n == 0 && name[k] != u'\\') out[n++] = '\\';
                if (n && out[n - 1] == '\\' && name[k] == u'\\') continue;
                out[n++] = name[k] < 0x80 ? (char)name[k] : '_';
            }
        }
        node += length;
    }
    out[n] = 0;
    char* slash = strrchr(out, '\\');
    if (slash) *slash = 0;
}

size_t savedMode;
size_t savedAttribute;
bool modeSwitched;

void PrepareConsole()
{
    savedMode = (size_t)Out->Mode->Mode;
    savedAttribute = (size_t)Out->Mode->Attribute;
    size_t columns = 0, rows = 0;
    if (Out->QueryMode(Out, savedMode, &columns, &rows) != EFI_SUCCESS || columns != 80 || rows != 25) {
        for (int32_t m = 0; m < Out->Mode->MaxMode; ++m)
            if (Out->QueryMode(Out, (size_t)m, &columns, &rows) == EFI_SUCCESS && columns == 80 && rows == 25) {
                modeSwitched = Out->SetMode(Out, (size_t)m) == EFI_SUCCESS;
                break;
            }
    }
    if (Out->QueryMode(Out, (size_t)Out->Mode->Mode, &columns, &rows) == EFI_SUCCESS) { consoleColumns = columns; consoleRows = rows; }
    Out->SetAttribute(Out, 0x07);
    Out->ClearScreen(Out);
    if (settings.debugserial || settings.debugcon) {
        char line[160];
        snprintf(line, sizeof line, "CharonPC: console mode %d of %d, switched %d, now %d (%ux%u)\n", (int)savedMode, (int)Out->Mode->MaxMode, (int)modeSwitched,
                 (int)Out->Mode->Mode, (unsigned)consoleColumns, (unsigned)consoleRows);
        pc_log_write(line, strlen(line));
        Print(u"CharonPC console test line 0123456789\r\n");
        BS->Stall(4000000);
    }
}

void RestoreConsole()
{
    Out->SetAttribute(Out, savedAttribute & 0x7F);
    if (modeSwitched) Out->SetMode(Out, savedMode);
    Out->ClearScreen(Out);
    Out->EnableCursor(Out, true);
}
}  // namespace

extern "C" EFIAPI efi_status efi_main(efi_handle image, efi_system_table* system)
{
    ST = system;
    BS = system->BootServices;
    Out = system->ConOut;
    Image = image;
    // An emulated session runs for hours; the boot watchdog must not reset the machine.
    BS->SetWatchdogTimer(0, 0, 0, nullptr);
    efi_guid loadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    efi_loaded_image_protocol* loaded = nullptr;
    if (BS->HandleProtocol(image, &loadedGuid, reinterpret_cast<void**>(&loaded)) != EFI_SUCCESS || !loaded) return EFI_LOAD_ERROR;
    efi_guid fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    efi_simple_file_system_protocol* fs = nullptr;
    if (BS->HandleProtocol(loaded->DeviceHandle, &fsGuid, reinterpret_cast<void**>(&fs)) != EFI_SUCCESS || !fs
        || fs->OpenVolume(fs, &volumeRoot) != EFI_SUCCESS || !volumeRoot) {
        Print(u"CharonPC: no file system on the boot volume.\r\n");
        return EFI_NOT_FOUND;
    }
    efi_guid inputExGuid = {0xdd9e7534, 0x7762, 0x4698, {0x8c, 0x14, 0xf5, 0x85, 0x17, 0xa6, 0x25, 0xaa}};
    if (BS->HandleProtocol(ST->ConsoleInHandle, &inputExGuid, reinterpret_cast<void**>(&InEx)) != EFI_SUCCESS) InEx = nullptr;

    // Time base: the TSC, measured against Stall; the firmware clock for the date.
    // Stall alone is not a reliable reference (under the Windows hypervisor
    // OVMF's Stall ran about 12 times short): the TSC is measured over 20
    // ticks of a periodic 10 ms firmware timer, Stall only as a fallback.
    {
        uint64_t stallStart = Rdtsc();
        BS->Stall(20000);
        const uint64_t perUsByStall = (Rdtsc() - stallStart) / 20000;
        uint64_t perUsByTimer = 0;
        efi_event tick = nullptr;
        if (BS->CreateEvent(EVT_TIMER, 0, nullptr, nullptr, &tick) == EFI_SUCCESS && tick) {
            if (BS->SetTimer(tick, TimerPeriodic, 100000) == EFI_SUCCESS) {
                size_t index = 0;
                BS->WaitForEvent(1, &tick, &index);
                const uint64_t start = Rdtsc();
                for (int i = 0; i < 20; ++i) BS->WaitForEvent(1, &tick, &index);
                perUsByTimer = (Rdtsc() - start) / 200000;
                BS->SetTimer(tick, TimerCancel, 0);
            }
            BS->CloseEvent(tick);
        }
        tscPerUs = perUsByTimer ? perUsByTimer : perUsByStall;
        if (!tscPerUs) tscPerUs = 1;
        snprintf(calibration, sizeof calibration, "CharonPC: TSC %llu MHz by firmware timer, %llu MHz by Stall; using %llu\n",
                 (unsigned long long)perUsByTimer, (unsigned long long)perUsByStall, (unsigned long long)tscPerUs);
    }
    tscStart = Rdtsc();
    efi_time now;
    if (ST->RuntimeServices->GetTime(&now, nullptr) == EFI_SUCCESS)
        epochStart = DaysFromCivil(now.Year, now.Month, now.Day) * 86400 + now.Hour * 3600 + now.Minute * 60 + now.Second;

    char config[300];
    ParseOptions(loaded, config, sizeof config);
    if (!config[0]) {
        OwnFolder(loaded, config, sizeof config - 16);
        strcat(config, "\\CHARONPC.CFG");
    }
    strcpy(cfgDirectory, config);
    for (char* p = cfgDirectory; *p; ++p) if (*p == '/') *p = '\\';
    if (char* slash = strrchr(cfgDirectory, '\\')) *slash = 0; else cfgDirectory[0] = 0;

    static char cfgText[16 * 1024];
    size_t cfgBytes = 0;
    char error[200];
    if (!ReadWholeFile(config, cfgText, sizeof cfgText, &cfgBytes)) {
        Print(polish ? u"CharonPC: nie mogę przeczytać pliku konfiguracji " : u"CharonPC: cannot read the configuration file ");
        PrintAscii(config);
        Print(u"\r\n");
        WaitKeyOrSeconds(10);
        return EFI_NOT_FOUND;
    }
    if (!TranslateConfig(cfgText, cfgBytes, cfgDirectory, bochsrc, sizeof bochsrc, error, sizeof error)) {
        Print(polish ? u"CharonPC: błąd w konfiguracji: " : u"CharonPC: configuration error: ");
        PrintAscii(error);
        Print(u"\r\n");
        WaitKeyOrSeconds(10);
        return EFI_INVALID_PARAMETER;
    }
    bochsrcBytes = strlen(bochsrc);
    pc_log_write(calibration, strlen(calibration));
    pc_log_write("CharonPC: generated Bochs configuration:\n", 42);
    pc_log_write(bochsrc, bochsrcBytes);

    PrepareConsole();
    efi_status result = EFI_SUCCESS;
    if (pc_rt_init((size_t)3 << 30)) result = EFI_OUT_OF_RESOURCES;
    synth_coro* coroutine = result == EFI_SUCCESS ? synth_coro_create((size_t)8 << 20, RunBochs, nullptr) : nullptr;
    if (!coroutine) result = EFI_OUT_OF_RESOURCES;
    int state = coroutine ? synth_coro_resume(coroutine) : -1;
    {
        char line[96];
        snprintf(line, sizeof line, "CharonPC: Bochs returned %d, coroutine state %d, user quit %d\n", bochsResult, state, (int)userQuit);
        pc_log_write(line, strlen(line));
    }
    AudioClose();   // r407: the guest's sound ends; CharonPC's own sound card is stopped
    pc_rt_close_all();
    ReleaseEverything();
    if (logHandle) { pc_vol_flush(logHandle); pc_vol_close(logHandle); logHandle = nullptr; }
    StopVoice();
    RestoreConsole();
    logTail[logTailBytes < sizeof logTail ? logTailBytes : sizeof logTail - 1] = 0;
    const bool panicked = !userQuit && strstr(logTail, "PANIC");
    if (state != 0 || panicked) {
        // Show the end of the Bochs log: that is where the reason is.
        Print(polish ? u"CharonPC: emulator zakończył pracę z błędem. Koniec dziennika:\r\n"
                     : u"CharonPC: the emulator stopped with an error. End of its log:\r\n");
        const char* start = logTail;
        if (logTailBytes > 900) start = logTail + logTailBytes - 900;
        PrintAscii(start);
        if (state != 0) { PrintAscii("\n"); PrintAscii(synth_diagnostic); }
        Print(u"\r\n");
        WaitKeyOrSeconds(15);
        if (result == EFI_SUCCESS) result = EFI_ABORTED;
    }
    return result;
}
