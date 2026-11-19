// Charon synthesizer modules: UEFI entry point shared by every .tts module.
// GPL-2.0-or-later.
//
// A module is a boot service driver (subsystem 11, CharonSynthModule.h ABI 2):
// the firmware keeps it resident after efi_main returns. efi_main reads its
// data folder from LoadOptions, lets the engine check its data index on the
// volume it was loaded from (no voice is loaded yet: Prepare does that),
// installs CharonSynthModule on a new handle and returns EFI_SUCCESS.
#include <stddef.h>
#include <stdint.h>
#include <efi/types.h>
#include <efi/system-table.h>
#include <efi/boot-services.h>
#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>
#include <efi/protocol/file.h>
#include <efi/protocol/device-path.h>
#include "CharonSynthModule.h"
#include "synth_rt.h"

extern "C" int synth_engine_start(const char* data_directory, hakefi::CharonSynthModule** module);

namespace {
constexpr size_t moduleBudget = (size_t)62 << 20;  // below the 64 MiB limit

efi_boot_services* boot;
efi_file_protocol* volumeRoot;
efi_handle volumeDevice;   // r395: the volume is opened again when its handle went stale
// r395: the volume's device path, to find it again after the firmware
// reconnected the disk (BDS before a boot option gives it a new handle).
alignas(8) unsigned char volumePath[1024];
size_t volumePathBytes;
bool ReopenVolume()
{
    efi_guid fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    efi_handle device = volumeDevice;
    efi_simple_file_system_protocol* fs = nullptr;
    if (boot->HandleProtocol(device, &fsGuid, reinterpret_cast<void**>(&fs)) != EFI_SUCCESS || !fs) {
        if (!volumePathBytes) return false;
        alignas(8) unsigned char copy[sizeof volumePath];
        for (size_t i = 0; i < volumePathBytes; ++i) copy[i] = volumePath[i];
        auto* remaining = reinterpret_cast<efi_device_path_protocol*>(copy);
        device = nullptr;
        if (boot->LocateDevicePath(&fsGuid, &remaining, &device) != EFI_SUCCESS || !device) return false;
        // Only the same volume: the whole path must match, not a parent disk.
        if (reinterpret_cast<const unsigned char*>(remaining)[0] != 0x7f) return false;
        fs = nullptr;
        if (boot->HandleProtocol(device, &fsGuid, reinterpret_cast<void**>(&fs)) != EFI_SUCCESS || !fs) return false;
    }
    efi_file_protocol* root = nullptr;
    if (fs->OpenVolume(fs, &root) != EFI_SUCCESS || !root) return false;
    volumeDevice = device;
    volumeRoot = root;
    return true;
}
synth_platform platform;
efi_handle protocolHandle;

// AllocatePool guarantees 8-byte alignment only; the runtime needs 16.
void* Allocate(void*, size_t bytes)
{
    void* p = nullptr;
    if (bytes > SIZE_MAX - 16 || boot->AllocatePool(EfiBootServicesData, bytes + 16, &p) != EFI_SUCCESS || !p) return nullptr;
    uintptr_t aligned = ((uintptr_t)p + 16) & ~(uintptr_t)15;
    reinterpret_cast<void**>(aligned)[-1] = p;
    return reinterpret_cast<void*>(aligned);
}
void Release(void*, void* p) { if (p) boot->FreePool(reinterpret_cast<void**>(p)[-1]); }

efi_file_protocol* OpenFile(const char* path, uint64_t* size)
{
    char16_t name[512];
    size_t n = 0;
    for (const char* p = path; *p && n + 1 < sizeof name / sizeof name[0]; ++p)
        name[n++] = *p == '/' ? u'\\' : (char16_t)(unsigned char)*p;
    name[n] = 0;
    if (name[0] != u'\\') return nullptr;
    efi_file_protocol* file = nullptr;
    if (volumeRoot->Open(volumeRoot, &file, name, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS || !file) {
        // r395: firmware may reconnect the volume (BDS before a boot option),
        // which leaves the root opened at load time unusable. Open it again once.
        file = nullptr;
        if (!ReopenVolume()) return nullptr;
        if (volumeRoot->Open(volumeRoot, &file, name, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS || !file) return nullptr;
    }
    efi_guid infoGuid = EFI_FILE_INFO_GUID;
    alignas(8) unsigned char infoBuffer[sizeof(efi_file_info) + 512];
    size_t infoBytes = sizeof infoBuffer;
    if (file->GetInfo(file, &infoGuid, &infoBytes, infoBuffer) != EFI_SUCCESS) { file->Close(file); return nullptr; }
    *size = reinterpret_cast<efi_file_info*>(infoBuffer)->FileSize;
    return file;
}

int ProbeFile(void*, const char* path, size_t* size)
{
    uint64_t bytes = 0;
    efi_file_protocol* file = OpenFile(path, &bytes);
    if (!file) return -1;
    file->Close(file);
    if (size) *size = (size_t)bytes;
    return 0;
}

int ReadFile(void*, const char* path, void** data, size_t* size)
{
    uint64_t total = 0;
    efi_file_protocol* file = OpenFile(path, &total);
    if (!file) return -1;
    int result = -1;
    size_t bytes = (size_t)total;
    void* buffer = Allocate(nullptr, bytes ? bytes : 1);
    if (buffer) {
        size_t done = 0;
        while (done < bytes) {
            size_t chunk = bytes - done;
            if (chunk > ((size_t)4 << 20)) chunk = (size_t)4 << 20;
            if (file->Read(file, &chunk, (unsigned char*)buffer + done) != EFI_SUCCESS || !chunk) break;
            done += chunk;
        }
        if (done == bytes) { *data = buffer; *size = bytes; result = 0; }
        else Release(nullptr, buffer);
    }
    file->Close(file);
    return result;
}
}  // namespace

extern "C" EFIAPI efi_status efi_main(efi_handle image, efi_system_table* system)
{
    boot = system->BootServices;
    efi_guid loadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    efi_loaded_image_protocol* loaded = nullptr;
    if (boot->HandleProtocol(image, &loadedGuid, reinterpret_cast<void**>(&loaded)) != EFI_SUCCESS || !loaded)
        return EFI_LOAD_ERROR;
    efi_guid fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    efi_simple_file_system_protocol* fs = nullptr;
    if (boot->HandleProtocol(loaded->DeviceHandle, &fsGuid, reinterpret_cast<void**>(&fs)) != EFI_SUCCESS || !fs)
        return EFI_NOT_FOUND;
    if (fs->OpenVolume(fs, &volumeRoot) != EFI_SUCCESS || !volumeRoot) return EFI_NOT_FOUND;
    volumeDevice = loaded->DeviceHandle;
    {
        efi_guid pathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
        efi_device_path_protocol* path = nullptr;
        volumePathBytes = 0;
        if (boot->HandleProtocol(volumeDevice, &pathGuid, reinterpret_cast<void**>(&path)) == EFI_SUCCESS && path) {
            const unsigned char* at = reinterpret_cast<const unsigned char*>(path);
            size_t n = 0;
            for (;;) {
                const size_t length = (size_t)at[n + 2] | (size_t)at[n + 3] << 8;
                if (length < 4 || n + length > sizeof volumePath) { n = 0; break; }
                const bool end = at[n] == 0x7f && at[n + 1] == 0xff;
                n += length;
                if (end) break;
            }
            for (size_t i = 0; i < n; ++i) volumePath[i] = at[i];
            volumePathBytes = n;
        }
    }
    char dataDirectory[512];
    synth_path_from_utf16(static_cast<const uint16_t*>(loaded->LoadOptions), loaded->LoadOptionsSize,
                          dataDirectory, sizeof dataDirectory);
    if (dataDirectory[0] != '/') return EFI_INVALID_PARAMETER;
    platform.context = nullptr;
    platform.allocate = Allocate;
    platform.release = Release;
    platform.read_file = ReadFile;
    platform.probe_file = ProbeFile;
    if (synth_rt_init(&platform, moduleBudget)) return EFI_OUT_OF_RESOURCES;
    hakefi::CharonSynthModule* module = nullptr;
    int started = synth_engine_start(dataDirectory, &module);
    // -2 no memory, -3 voices unusable, -4 data missing, -5 data corrupt.
    if (started || !module)
        return started == -2 ? EFI_OUT_OF_RESOURCES : started == -3 ? EFI_UNSUPPORTED : started == -4 ? EFI_NOT_FOUND
             : started == -5 ? EFI_VOLUME_CORRUPTED : EFI_LOAD_ERROR;
    efi_guid moduleGuid = CHARON_SYNTH_MODULE_GUID;
    protocolHandle = nullptr;
    if (boot->InstallProtocolInterface(&protocolHandle, &moduleGuid, EFI_NATIVE_INTERFACE, module) != EFI_SUCCESS)
        return EFI_OUT_OF_RESOURCES;
    return EFI_SUCCESS;
}
