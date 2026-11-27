// Host test loader for Charon synthesizer modules (.tts). GPL-2.0-or-later.
//
// Loads the real PE32+ module file into memory on Windows x64, relocates it,
// and starts it the way Charon does (CharonSynthModule.h): a fake UEFI boot
// services table offers LoadedImage (with LoadOptions = data folder),
// SimpleFileSystem over a host directory standing for Charon's volume,
// AllocatePool/AllocatePages with accounting, and InstallProtocolInterface.
// It then drives Begin/Render/Cancel of the installed CharonSynthModule and
// writes WAV files.
//
// Usage: tts-host-loader VOLUME_DIR MODULE_NAME OUT_DIR|- VOICES [DIAGNOSTIC_RVA STDERR_RVA]
//   VOICES: voice indices to Prepare, in order ("0", "1", "0,1"); OUT_DIR "-" writes no WAV.
//   r409: "all" = every voice of the module; a leading '@' ("@0,5,1,0" or "@all") is the
//   cycle test: each listed voice is prepared and speaks at once (a module may release
//   other voices to make room), as when a user steps through the voice list quickly.
//   WAVs: <module>-<language>.wav for the first voice of a language, <voice id>.wav for all.
//   VOLUME_DIR holds EFI\HAK\synths\<name>.tts and <name>\ (staging layout).
// The program prints one JSON object per test utterance and exits non-zero
// when a check fails.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <map>

typedef uint64_t efi_status;
typedef void* efi_handle;
#define EFIAPI __attribute__((ms_abi))
#define API __attribute__((ms_abi))   // CHARON_SYNTH_API (ABI 2)
static const efi_status OK = 0, UNSUPPORTED = 0x8000000000000003ull, NOT_FOUND = 0x800000000000000Eull,
                        OUT_OF_RESOURCES = 0x8000000000000009ull, BUFFER_TOO_SMALL = 0x8000000000000005ull,
                        INVALID = 0x8000000000000002ull;
struct Guid { uint32_t a; uint16_t b, c; uint8_t d[8]; };
static bool Same(const Guid* x, const Guid& y) { return !memcmp(x, &y, sizeof y); }
static const Guid loadedImageGuid = {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const Guid fileSystemGuid = {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const Guid fileInfoGuid = {0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const Guid moduleGuid = {0x6c7e5a21, 0x3b9d, 0x4f0e, {0x9a, 0x41, 0xc4, 0xa7, 0xe2, 0xd5, 0x13, 0x93}};

// ---- CharonSynthModule (ABI 2, ms_abi) as seen from Charon --------------
struct Voice { char id[32]; char16_t name[48]; char language[8]; uint32_t sampleRate; };
struct Module {
    uint32_t abi, voiceCount;
    char16_t engine[32];
    const Voice* voices;
    int (API *Prepare)(Module*, uint32_t);
    int (API *Begin)(Module*, uint32_t, const char*, size_t, double, double, double);
    int (API *Render)(Module*, int16_t*, size_t);
    void (API *Cancel)(Module*);
    void* context;
};
static_assert(offsetof(Module, voices) == 72 && offsetof(Module, Prepare) == 80 && offsetof(Module, context) == 112, "ABI layout");
static_assert(sizeof(Voice) == 32 + 96 + 8 + 4, "voice layout");

// ---- fake firmware --------------------------------------------------------
struct TableHeader { uint64_t Signature; uint32_t Revision, HeaderSize, CRC32, Reserved; };
typedef efi_status (EFIAPI *Fn)();
struct BootServices {
    TableHeader Hdr;
    void* RaiseTPL; void* RestoreTPL;
    efi_status (EFIAPI *AllocatePages)(int, int, size_t, uint64_t*);
    efi_status (EFIAPI *FreePages)(uint64_t, size_t);
    void* GetMemoryMap;
    efi_status (EFIAPI *AllocatePool)(int, size_t, void**);
    efi_status (EFIAPI *FreePool)(void*);
    void* CreateEvent; void* SetTimer; void* WaitForEvent; void* SignalEvent; void* CloseEvent; void* CheckEvent;
    efi_status (EFIAPI *InstallProtocolInterface)(efi_handle*, const Guid*, int, void*);
    void* ReinstallProtocolInterface; void* UninstallProtocolInterface;
    efi_status (EFIAPI *HandleProtocol)(efi_handle, const Guid*, void**);
    void* Reserved; void* RegisterProtocolNotify; void* LocateHandle; void* LocateDevicePath; void* InstallConfigurationTable;
    void* LoadImage; void* StartImage; void* Exit; void* UnloadImage; void* ExitBootServices;
    void* GetNextMonotonicCount; void* Stall; void* SetWatchdogTimer;
    void* ConnectController; void* DisconnectController;
    void* OpenProtocol; void* CloseProtocol; void* OpenProtocolInformation;
    void* ProtocolsPerHandle; void* LocateHandleBuffer; void* LocateProtocol;
    void* InstallMultipleProtocolInterfaces; void* UninstallMultipleProtocolInterfaces;
    void* CalculateCrc32; void* CopyMem; void* SetMem; void* CreateEventEx;
};
struct SystemTable {
    TableHeader Hdr;
    char16_t* FirmwareVendor; uint32_t FirmwareRevision;
    efi_handle ConsoleInHandle; void* ConIn; efi_handle ConsoleOutHandle; void* ConOut;
    efi_handle StandardErrorHandle; void* StdErr;
    void* RuntimeServices; BootServices* BootServices;
    size_t NumberOfTableEntries; void* ConfigurationTable;
};
struct LoadedImage {
    uint32_t Revision; efi_handle ParentHandle; SystemTable* System; efi_handle DeviceHandle; void* FilePath;
    void* Reserved; uint32_t LoadOptionsSize; void* LoadOptions; void* ImageBase; uint64_t ImageSize;
    int ImageCodeType, ImageDataType; void* Unload;
};
struct File;
struct FileProtocol {
    uint64_t Revision;
    efi_status (EFIAPI *Open)(FileProtocol*, FileProtocol**, const char16_t*, uint64_t, uint64_t);
    efi_status (EFIAPI *Close)(FileProtocol*);
    void* Delete;
    efi_status (EFIAPI *Read)(FileProtocol*, size_t*, void*);
    void* Write; void* GetPosition; void* SetPosition;
    efi_status (EFIAPI *GetInfo)(FileProtocol*, const Guid*, size_t*, void*);
    void* SetInfo; void* Flush; void* OpenEx; void* ReadEx; void* WriteEx; void* FlushEx;
};
struct File { FileProtocol protocol; std::string path; FILE* stream; uint64_t size; };
struct FileSystem { uint64_t Revision; efi_status (EFIAPI *OpenVolume)(FileSystem*, FileProtocol**); };

static std::string volume;
static uintptr_t imageOriginal, imageCopy;
static size_t poolBytes, poolPeak, poolLive, pageBytes;
static std::map<void*, size_t> pools;
static std::vector<std::pair<Guid, void*>> installed;
static char imageHandle, deviceHandle, newHandleTag;
static LoadedImage loaded;
static FileSystem fileSystem;
static int fileOpens;

static efi_status EFIAPI AllocatePool(int, size_t bytes, void** out)
{
    // Firmware pools are only 8-byte aligned: hand out 8 mod 16 on purpose.
    char* raw = (char*)malloc(bytes + 8);
    if (!raw) return OUT_OF_RESOURCES;
    void* p = raw + 8;
    pools[p] = bytes; poolLive += bytes; if (poolLive + pageBytes > poolPeak) poolPeak = poolLive + pageBytes;
    poolBytes += bytes; *out = p; return OK;
}
static efi_status EFIAPI FreePool(void* p)
{
    auto it = pools.find(p);
    if (it == pools.end()) { fprintf(stderr, "FreePool of unknown pointer\n"); exit(3); }
    poolLive -= it->second; pools.erase(it); free((char*)p - 8); return OK;
}
static efi_status EFIAPI AllocatePages(int, int, size_t pages, uint64_t* out)
{
    void* p = VirtualAlloc(nullptr, pages * 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!p) return OUT_OF_RESOURCES;
    if (!imageCopy) imageCopy = (uintptr_t)p;
    pageBytes += pages * 4096; if (poolLive + pageBytes > poolPeak) poolPeak = poolLive + pageBytes;
    *out = (uint64_t)(uintptr_t)p; return OK;
}
// Pages stay mapped (only accounted as freed) so a failed module's
// diagnostic text can still be read afterwards.
static efi_status EFIAPI FreePages(uint64_t, size_t pages) { pageBytes -= pages * 4096; return OK; }
static efi_status EFIAPI Install(efi_handle* handle, const Guid* guid, int, void* intf)
{
    if (*handle == &imageHandle) { fprintf(stderr, "protocol installed on the image handle\n"); exit(3); }
    if (!*handle) *handle = &newHandleTag;
    installed.push_back({*guid, intf}); return OK;
}
static efi_status EFIAPI FileClose(FileProtocol* f) { File* file = (File*)f; if (file->stream) fclose(file->stream); delete file; return OK; }
static efi_status EFIAPI FileRead(FileProtocol* f, size_t* bytes, void* buffer)
{
    File* file = (File*)f; if (!file->stream) return UNSUPPORTED;
    *bytes = fread(buffer, 1, *bytes, file->stream); return OK;
}
static efi_status EFIAPI FileInfo(FileProtocol* f, const Guid* guid, size_t* bytes, void* buffer)
{
    if (!Same(guid, fileInfoGuid)) return UNSUPPORTED;
    size_t need = 80 + 2;
    if (*bytes < need) { *bytes = need; return BUFFER_TOO_SMALL; }
    memset(buffer, 0, need);
    uint64_t* info = (uint64_t*)buffer; info[0] = need; info[1] = ((File*)f)->size; info[2] = ((File*)f)->size;
    *bytes = need; return OK;
}
static efi_status EFIAPI FileOpen(FileProtocol* self, FileProtocol** out, const char16_t* name, uint64_t mode, uint64_t)
{
    if (mode != 1) return INVALID;
    std::string relative;
    for (const char16_t* p = name; *p; ++p) relative += *p == u'\\' ? '\\' : (char)*p;
    File* parent = (File*)self;
    std::string path = relative[0] == '\\' ? volume + relative : parent->path + "\\" + relative;
    FILE* stream = fopen(path.c_str(), "rb");
    if (!stream) return NOT_FOUND;
    File* file = new File(*parent);
    file->path = path; file->stream = stream;
    _fseeki64(stream, 0, SEEK_END); file->size = _ftelli64(stream); _fseeki64(stream, 0, SEEK_SET);
    ++fileOpens; *out = &file->protocol; return OK;
}
static efi_status EFIAPI OpenVolume(FileSystem*, FileProtocol** out)
{
    File* root = new File{};
    root->protocol.Revision = 0x10000; root->protocol.Open = FileOpen; root->protocol.Close = FileClose;
    root->protocol.Read = FileRead; root->protocol.GetInfo = FileInfo; root->path = volume; root->stream = nullptr;
    *out = &root->protocol; return OK;
}
static efi_status EFIAPI HandleProtocol(efi_handle handle, const Guid* guid, void** out)
{
    if (handle == &imageHandle && Same(guid, loadedImageGuid)) { *out = &loaded; return OK; }
    if (handle == &deviceHandle && Same(guid, fileSystemGuid)) { *out = &fileSystem; return OK; }
    return UNSUPPORTED;
}
static efi_status EFIAPI Unsupported() { return UNSUPPORTED; }

// ---- PE loader -------------------------------------------------------------
static void* LoadPe(const std::vector<unsigned char>& file, uint64_t& size, uint32_t& entry)
{
    auto u16 = [&](size_t a) { return *(const uint16_t*)&file[a]; };
    auto u32 = [&](size_t a) { return *(const uint32_t*)&file[a]; };
    uint32_t pe = u32(0x3c); size_t optional = pe + 24;
    uint64_t base = *(const uint64_t*)&file[optional + 24];
    size = u32(optional + 56); entry = u32(optional + 16);
    unsigned char* image = (unsigned char*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(image, file.data(), u32(optional + 60));
    uint16_t count = u16(pe + 6), optionalSize = u16(pe + 20);
    for (uint16_t i = 0; i < count; ++i) {
        size_t at = optional + optionalSize + i * 40;
        uint32_t virt = u32(at + 12), raw = u32(at + 16), rawAt = u32(at + 20);
        if (raw) memcpy(image + virt, file.data() + rawAt, raw);
    }
    uint32_t relocRva = u32(optional + 152), relocSize = u32(optional + 156);
    uint64_t delta = (uint64_t)(uintptr_t)image - base;
    for (uint32_t at = relocRva; at < relocRva + relocSize;) {
        uint32_t page = *(uint32_t*)(image + at), bytes = *(uint32_t*)(image + at + 4);
        for (uint32_t k = 8; k < bytes; k += 2) {
            uint16_t e = *(uint16_t*)(image + at + k);
            if (e >> 12 == 10) *(uint64_t*)(image + page + (e & 0xfff)) += delta;
        }
        at += bytes;
    }
    return image;
}

static void WriteWav(const std::string& path, const std::vector<int16_t>& pcm, uint32_t rate)
{
    FILE* f = fopen(path.c_str(), "wb");
    uint32_t data = (uint32_t)(pcm.size() * 2), riff = 36 + data, fmt = 16, byteRate = rate * 2;
    uint16_t pcmTag = 1, channels = 1, align = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcmTag, 2, 1, f); fwrite(&channels, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(pcm.data(), 2, pcm.size(), f); fclose(f);
}

// Crash report: the faulting address relative to the module image (the
// original load or the module's own relocated copy), for llvm-symbolizer.
static LONG CALLBACK Crash(EXCEPTION_POINTERS* e)
{
    uintptr_t rip = (uintptr_t)e->ContextRecord->Rip;
    uintptr_t base = rip >= imageCopy && imageCopy ? imageCopy : imageOriginal;
    fprintf(stderr, "CRASH code 0x%lx rip=%p rva=0x%llx rsp=%p fault=%p\n",
            e->ExceptionRecord->ExceptionCode, (void*)rip, (unsigned long long)(rip - base), (void*)e->ContextRecord->Rsp,
            e->ExceptionRecord->NumberParameters > 1 ? (void*)e->ExceptionRecord->ExceptionInformation[1] : nullptr);
    fflush(stderr);
    ExitProcess(4);
    return EXCEPTION_CONTINUE_SEARCH;
}

static double Now() { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart / f.QuadPart; }

int main(int argc, char** argv)
{
    if (argc != 5 && argc != 7) { fprintf(stderr, "usage: VOLUME_DIR MODULE_NAME OUT_DIR|- VOICES [DIAGNOSTIC_RVA STDERR_RVA]\n"); return 2; }
    std::vector<uint32_t> prepareList;
    const bool cycle = argv[4][0] == '@';
    const bool all = !strcmp(argv[4] + cycle, "all");
    if (!all) for (const char* p = argv[4] + cycle; *p;) { prepareList.push_back((uint32_t)strtoul(p, (char**)&p, 10)); if (*p == ',') ++p; }
    uintptr_t diagnosticRva = argc == 7 ? (uintptr_t)strtoull(argv[5], nullptr, 16) : 0;
    uintptr_t stderrRva = argc == 7 ? (uintptr_t)strtoull(argv[6], nullptr, 16) : 0;
    auto report = [&](const char* when) {
        if (!diagnosticRva) return;
        uintptr_t base = imageCopy ? imageCopy : imageOriginal;
        const char* text = (const char*)(base + diagnosticRva);
        const char* tail = (const char*)(base + stderrRva);
        printf("{\"when\":\"%s\",\"diagnostic\":\"%.159s\",\"stderr\":\"%.511s\"}\n", when, text, tail);
    };
    volume = argv[1]; std::string name = argv[2], outDir = argv[3];
    std::string modulePath = volume + "\\EFI\\HAK\\synths\\" + name + ".tts";
    FILE* f = fopen(modulePath.c_str(), "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", modulePath.c_str()); return 2; }
    std::vector<unsigned char> file;
    for (int c; (c = fgetc(f)) != EOF;) file.push_back((unsigned char)c);
    fclose(f);
    uint64_t imageSize; uint32_t entry;
    unsigned char* image = (unsigned char*)LoadPe(file, imageSize, entry);
    imageOriginal = (uintptr_t)image;
    AddVectoredExceptionHandler(1, Crash);

    static BootServices boot;
    void** slots = (void**)((char*)&boot + sizeof(TableHeader));
    for (size_t i = 0; i < (sizeof boot - sizeof(TableHeader)) / sizeof(void*); ++i) slots[i] = (void*)Unsupported;
    boot.AllocatePool = AllocatePool; boot.FreePool = FreePool; boot.AllocatePages = AllocatePages;
    boot.FreePages = FreePages; boot.InstallProtocolInterface = Install; boot.HandleProtocol = HandleProtocol;
    static SystemTable system{}; system.BootServices = &boot;
    std::u16string options = u"\\EFI\\HAK\\synths\\";
    for (char c : name) options += (char16_t)c;
    loaded.Revision = 0x1000; loaded.System = &system; loaded.DeviceHandle = &deviceHandle;
    loaded.LoadOptions = (void*)options.c_str(); loaded.LoadOptionsSize = (uint32_t)((options.size() + 1) * 2);
    loaded.ImageBase = image; loaded.ImageSize = imageSize;
    fileSystem.Revision = 0x10000; fileSystem.OpenVolume = OpenVolume;

    auto efiMain = (efi_status (EFIAPI *)(efi_handle, SystemTable*))(image + entry);
    double t0 = Now();
    efi_status status = efiMain(&imageHandle, &system);
    double startSeconds = Now() - t0;
    report("start");
    // A boot service driver stays loaded: the image is left in place.
    printf("{\"module\":\"%s\",\"efi_main\":\"0x%llx\",\"start_ms\":%.1f,\"pool_after_start\":%zu,\"pool_peak\":%zu,\"file_opens\":%d}\n",
           name.c_str(), (unsigned long long)status, startSeconds * 1000, poolLive, poolPeak, fileOpens);
    if (status != OK) return 1;
    Module* module = nullptr;
    for (auto& p : installed) if (Same(&p.first, moduleGuid)) module = (Module*)p.second;
    if (!module || (module->abi != 2 && module->abi != 3) || !module->voiceCount || module->voiceCount > 16) { fprintf(stderr, "no valid module\n"); return 1; }
    bool ok = true;
    std::vector<bool> prepared(module->voiceCount);
    std::vector<int16_t> buffer(1024);
    if (all) for (uint32_t v = 0; v < module->voiceCount; ++v) prepareList.push_back(v);
    for (uint32_t v = 0; v < module->voiceCount; ++v) {
        char name8[48]; size_t k = 0;
        for (; module->voices[v].name[k] && k < 47; ++k) name8[k] = module->voices[v].name[k] < 128 ? (char)module->voices[v].name[k] : '?';
        name8[k] = 0;
        printf("{\"voice_index\":%u,\"id\":\"%s\",\"name\":\"%s\",\"language\":\"%s\"}\n", v, module->voices[v].id, name8, module->voices[v].language);
    }
    for (uint32_t v : prepareList) if (v >= module->voiceCount) { fprintf(stderr, "voice index %u out of range\n", v); return 2; }
    if (cycle) {
        // Prepare + speak each listed voice in turn; nothing else stays guaranteed prepared.
        int step = 0;
        for (uint32_t v : prepareList) {
            double p0 = Now();
            int r = module->Prepare(module, v);
            double prepareMs = (Now() - p0) * 1000;
            std::string text = !strcmp(module->voices[v].language, "pl") ? "Menu rozruchu, pozycja 2 z 5." : "Boot menu, item 2 of 5.";
            int b = r ? r : module->Begin(module, v, text.data(), text.size(), 1.0, 1.0, 1.0);
            std::vector<int16_t> pcm; int n = 0;
            if (!b) while ((n = module->Render(module, buffer.data(), 1024)) != -1 && n >= 0) pcm.insert(pcm.end(), buffer.begin(), buffer.begin() + n);
            double sum = 0; for (int16_t x : pcm) sum += (double)x * x;
            uint32_t rate = module->voices[v].sampleRate;
            double rms = pcm.empty() ? 0 : sqrt(sum / pcm.size()), seconds = rate ? (double)pcm.size() / rate : 0;
            bool good = !r && !b && n == -1 && rms > 300 && seconds > 0.8 && seconds < 8.0;
            ok = ok && good;
            printf("{\"cycle\":%d,\"voice\":\"%s\",\"prepare\":%d,\"prepare_ms\":%.1f,\"begin\":%d,\"seconds\":%.3f,\"rms\":%.1f,"
                   "\"pool_live\":%zu,\"pool_peak\":%zu,\"ok\":%s}\n", step++, module->voices[v].id, r, prepareMs, b, seconds, rms,
                   poolLive, poolPeak, good ? "true" : "false");
            if (!good) report("cycle");
            if (good && outDir != "-") WriteWav(outDir + "\\" + module->voices[v].id + "-cycle.wav", pcm, rate);
        }
        bool memoryOk = poolPeak <= ((size_t)64 << 20);
        printf("{\"memory_peak_bytes\":%zu,\"within_64MiB\":%s}\n", poolPeak, memoryOk ? "true" : "false");
        puts(ok && memoryOk ? "PASS" : "FAIL");
        return ok && memoryOk ? 0 : 1;
    }
    // Begin before Prepare must refuse with -3.
    for (uint32_t v = 0; v < module->voiceCount; ++v) {
        int r = module->Begin(module, v, "a", 1, 1.0, 1.0, 1.0);
        if (r != -3) { printf("{\"unprepared_begin\":%d,\"voice\":%u}\n", r, v); ok = false; }
    }
    for (uint32_t v : prepareList) {
        size_t opensBefore = fileOpens;
        double p0 = Now();
        int r = module->Prepare(module, v);
        double ms = (Now() - p0) * 1000;
        printf("{\"prepare\":\"%s\",\"result\":%d,\"prepare_ms\":%.1f,\"pool_live\":%zu,\"pool_peak\":%zu,\"files_read\":%zu}\n",
               module->voices[v].id, r, ms, poolLive, poolPeak, (size_t)fileOpens - opensBefore);
        if (r) { report("prepare"); ok = false; } else prepared[v] = true;
    }
    for (uint32_t v = 0; v < module->voiceCount; ++v) if (!prepared[v]) {
        int r = module->Begin(module, v, "a", 1, 1.0, 1.0, 1.0);
        if (r != -3) { printf("{\"unprepared_begin\":%d,\"voice\":%u}\n", r, v); ok = false; }
    }
    struct Case { const char* tag; uint32_t voice; std::string text; double rate; };
    std::vector<Case> cases;
    std::map<std::string, bool> written;   // <module>-<tag>.wav belongs to the first voice with that tag
    for (uint32_t v = 0; v < module->voiceCount; ++v) {
        if (!prepared[v]) continue;
        std::string language = module->voices[v].language;
        std::string id = module->voices[v].id;
        if (language == "pl") cases.push_back({"pl", v, "Menu rozruchu, pozycja 2 z 5.", 1.0});
        else cases.push_back({"en", v, "Boot menu, item 2 of 5.", 1.0});
    }
    // Language switch, formatting switches to be skipped, a fast rate.
    // ESC 'f' carries 12 bytes (r397; 6 before) that must all be skipped, even ones that look
    // like another switch (here ESC 'e').
    static const char switchText[] = "Pozycja \x1b" "e" "Windows Boot Manager\x1b" "p" " wybrana.\x1bv\x01\x02\x03\x04 Koniec."
                                     "\x1bi\x01\x02 \x1b" "f" "\x1b" "eabcdefghij" "Dalej.";
    if (prepared[0]) {
        cases.push_back({"switch", 0, std::string(switchText, sizeof switchText - 1), 1.0});
        cases.push_back({"fast", 0, cases[0].text, 2.0});
    }
    for (const Case& c : cases) {
        double b0 = Now();
        int r = module->Begin(module, c.voice, c.text.data(), c.text.size(), c.rate, 1.0, 1.0);
        double beginMs = (Now() - b0) * 1000;
        if (r) { printf("{\"case\":\"%s\",\"begin\":%d}\n", c.tag, r); ok = false; continue; }
        std::vector<int16_t> pcm; double worst = 0; int calls = 0, zeroCalls = 0;
        for (;;) {
            double r0 = Now();
            int n = module->Render(module, buffer.data(), 1024);
            double ms = (Now() - r0) * 1000; if (ms > worst) worst = ms; ++calls;
            if (n == -1) break;
            if (n < -1) { printf("{\"case\":\"%s\",\"render_error\":%d}\n", c.tag, n); ok = false; break; }
            if (n == 0 && ++zeroCalls > 100000) { ok = false; break; }
            pcm.insert(pcm.end(), buffer.begin(), buffer.begin() + n);
        }
        uint32_t rate = module->voices[c.voice].sampleRate;
        double sum = 0; int peak = 0;
        for (int16_t s : pcm) { sum += (double)s * s; peak = abs(s) > peak ? abs(s) : peak; }
        double rms = pcm.empty() ? 0 : sqrt(sum / pcm.size()), seconds = (double)pcm.size() / rate;
        if (outDir != "-") {
            if (!written[c.tag]) { WriteWav(outDir + "\\" + name + "-" + c.tag + ".wav", pcm, rate); written[c.tag] = true; }
            if (!strcmp(c.tag, "pl") || !strcmp(c.tag, "en")) WriteWav(outDir + "\\" + module->voices[c.voice].id + ".wav", pcm, rate);
        }
        bool plausible = rms > 300 && seconds > 0.8 && seconds < 8.0;
        ok = ok && plausible;
        printf("{\"case\":\"%s\",\"voice\":\"%s\",\"rate\":%u,\"seconds\":%.3f,\"rms\":%.1f,\"peak\":%d,\"begin_ms\":%.2f,"
               "\"render_calls\":%d,\"worst_render_ms\":%.2f,\"pool_peak\":%zu,\"wav\":\"%s\",\"ok\":%s}\n",
               c.tag, module->voices[c.voice].id, rate, seconds, rms, peak, beginMs, calls, worst, poolPeak,
               (name + "-" + c.tag + ".wav").c_str(), plausible ? "true" : "false");
    }
    // Cancel in the middle, then speak again: must not crash and must restart.
    if (cases.empty()) { puts("FAIL"); return 1; }
    uint32_t cv = cases[0].voice;
    module->Begin(module, cv, cases[0].text.data(), cases[0].text.size(), 1.0, 1.0, 1.0);
    for (int i = 0; i < 5; ++i) module->Render(module, buffer.data(), 1024);
    module->Cancel(module);
    int after = module->Render(module, buffer.data(), 1024);
    int again = module->Begin(module, cv, cases[0].text.data(), cases[0].text.size(), 1.0, 1.0, 1.0);
    size_t total = 0;
    for (int n; (n = module->Render(module, buffer.data(), 1024)) != -1 && n >= 0;) total += n;
    bool cancelOk = after == -1 && again == 0 && total > 10000;
    ok = ok && cancelOk;
    printf("{\"case\":\"cancel\",\"render_after_cancel\":%d,\"begin_again\":%d,\"samples_again\":%zu,\"pool_peak\":%zu,\"ok\":%s}\n",
           after, again, total, poolPeak, cancelOk ? "true" : "false");
    bool memoryOk = poolPeak <= ((size_t)64 << 20);
    printf("{\"memory_peak_bytes\":%zu,\"within_64MiB\":%s}\n", poolPeak, memoryOk ? "true" : "false");
    report("end");
    puts(ok && memoryOk ? "PASS" : "FAIL");
    return ok && memoryOk ? 0 : 1;
}
