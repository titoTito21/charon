// CharonPC - where the emulated machine's sound goes (r407). GPL-2.0-or-later.
//
// The Bochs sound driver "charon" (pc_sound.cc) produces 48 kHz stereo PCM of
// the emulated Sound Blaster 16, its FM chip and the PC speaker. Two routes:
//  a) Charon is there (CharonDOS started from Charon, or the reader add-on
//     CHARONVOICE.EFI is resident): Charon's voice owns the sound card and
//     publishes the PCM sink (src/efi/CharonPcmSink.h). The guest's sound is
//     written there and Charon mixes it under its speech.
//  b) No Charon voice at all: CharonPC opens the sound card itself, with the
//     code Charon uses (src/efi/CharonAudioDevice.h: the HAK stream of
//     \EFI\HAK\AudioDxe.efi - loaded from this volume when no stream is
//     published -, AC'97, ES1371, a USB sound card), and stops it on exit.
// A Charon voice without the sink (a Charon older than r407) owns the card
// and cannot mix: the guest then stays silent, and the log says so.
#include <new>
#include <stdio.h>
#include <string.h>
#include "pc_glue.h"
#include "pc_platform.h"
#include "CharonPcmSink.h"
// Charon's own headers are written for Charon's build, not for -Wall -Wextra -Werror.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#include "CharonAudioDevice.h"
#pragma clang diagnostic pop

namespace {

enum class Route { none, charon, own };
Route route = Route::none;
bool tried;
hakefi::PcmSink* sink;
char routeText[96] = "not opened";

alignas(charonaudioefi::Device) unsigned char deviceStorage[sizeof(charonaudioefi::Device)];
alignas(charonaudioefi::PcmMix) unsigned char mixStorage[sizeof(charonaudioefi::PcmMix)];
charonaudioefi::Device* device;
charonaudioefi::PcmMix* mix;
uint64_t retryUs;
unsigned retries;

// \EFI\HAK\AudioDxe.efi of this volume, as Charon's provider loads it: started,
// the firmware's own HD Audio driver taken off the controllers, everything connected.
efi_status LoadAudioDriver()
{
    void* file = nullptr;
    uint64_t size = 0;
    if (pc_vol_open("\\EFI\\HAK\\AudioDxe.efi", 0, 0, 0, &file, &size)) return EFI_NOT_FOUND;
    void* image = nullptr;
    efi_status status = size && size <= (8u << 20) ? pc::BS->AllocatePool(EfiLoaderData, (size_t)size, &image) : EFI_BAD_BUFFER_SIZE;
    if (status == EFI_SUCCESS && pc_vol_read(file, 0, image, (size_t)size) != (long)size) status = EFI_DEVICE_ERROR;
    pc_vol_close(file);
    efi_handle driver = nullptr;
    if (status == EFI_SUCCESS) status = pc::BS->LoadImage(false, pc::Image, nullptr, image, (size_t)size, &driver);
    if (image) pc::BS->FreePool(image);
    if (status != EFI_SUCCESS) { if (driver) pc::BS->UnloadImage(driver); return status; }
    status = pc::BS->StartImage(driver, nullptr, nullptr);
    if (status != EFI_SUCCESS) { pc::BS->UnloadImage(driver); return status; }
    // A started driver owns its bindings and stays resident (as in Charon).
    charonaudioefi::audioDriverImage = driver;
    charonaudioefi::DisconnectFirmwareHda(pc::BS, driver);
    efi_handle* handles = nullptr;
    size_t count = 0;
    status = pc::BS->LocateHandleBuffer(AllHandles, nullptr, nullptr, &count, &handles);
    if (status != EFI_SUCCESS || !handles) return status != EFI_SUCCESS ? status : EFI_DEVICE_ERROR;
    for (size_t i = 0; i < count && i < 4096; ++i) pc::BS->ConnectController(handles[i], nullptr, nullptr, true);
    pc::BS->FreePool(handles);
    return EFI_SUCCESS;
}

bool OpenOwnDevice()
{
    efi_status status = device->Open(pc::BS, false);
    efi_status loaded = EFI_SUCCESS;
    if (status == EFI_NOT_FOUND && !device->NeedsStop()) {
        loaded = LoadAudioDriver();
        status = device->Open(pc::BS);
    }
    if (status != EFI_SUCCESS) {
        // A failed set-up may hold the stream: let go of it.
        if (device->NeedsStop()) device->Stop();
        snprintf(routeText, sizeof routeText, "no sound card (open %lx, AudioDxe %lx)", (unsigned long)status, (unsigned long)loaded);
        return false;
    }
    snprintf(routeText, sizeof routeText, "CharonPC's own output (%s)",
             device->UsbPrimary() ? "USB sound card" : device->Handle() ? "HAK stream" : "AC'97 or ES1371");
    return true;
}

}  // namespace

bool pc::AudioOpen()
{
    if (route != Route::none) return true;
    if (tried) return false;
    tried = true;
    sink = hakefi::FindPcmSink(BS);
    if (sink) {
        if (sink->Open(sink, 48000, 2) == EFI_SUCCESS) {
            route = Route::charon;
            snprintf(routeText, sizeof routeText, "Charon's PCM sink (mixed under the speech)");
            return true;
        }
        sink = nullptr;
        snprintf(routeText, sizeof routeText, "Charon's PCM sink refused 48000 Hz stereo");
        return false;
    }
    if (Voice()) {
        snprintf(routeText, sizeof routeText, "Charon's voice owns the sound card and has no PCM sink (Charon older than r407)");
        return false;
    }
    device = new (deviceStorage) charonaudioefi::Device;
    mix = new (mixStorage) charonaudioefi::PcmMix;
    if (!OpenOwnDevice()) return false;
    mix->Open(48000, 2);
    route = Route::own;
    return true;
}

const char* pc::AudioRoute() { return routeText; }

size_t pc::AudioWrite(const int16_t* stereo, size_t frames)
{
    if (route == Route::charon) {
        size_t accepted = 0;
        if (sink->Write(sink, stereo, frames, &accepted) != EFI_SUCCESS || accepted > frames) return 0;
        return accepted;
    }
    if (route == Route::own) return mix->Write(stereo, frames);
    return 0;
}

size_t pc::AudioQueued()
{
    if (route == Route::charon) {
        size_t frames = 0;
        return sink->Queued(sink, &frames) == EFI_SUCCESS ? frames : 0;
    }
    if (route == Route::own) return mix->Queued();
    return 0;
}

void pc::AudioService()
{
    // Route a) is served by Charon's voice pump (PumpVoice, called beside this).
    if (route != Route::own) return;
    // The card failed or was taken away (the firmware reconnected its driver): stopped
    // and opened again, at most once a second and a few times.
    if (device->LastError() != EFI_SUCCESS || device->Vanished() || !device->NeedsStop()) {
        const uint64_t now = pc_usec();
        if (now - retryUs < 1000000 || retries >= 8) return;
        retryUs = now;
        ++retries;
        if (device->NeedsStop() && device->Stop() != EFI_SUCCESS) return;
        if (!OpenOwnDevice()) return;
    }
    mix->Service(device->Sink());
}

void pc::AudioClose()
{
    if (route == Route::charon) sink->Close(sink);
    if (route == Route::own) {
        mix->Close();
        // The sound card must be silent and released before CharonPC returns.
        for (int attempt = 0; attempt < 50 && device->NeedsStop() && device->Stop() != EFI_SUCCESS; ++attempt) BS->Stall(20000);
    }
    route = Route::none;
    tried = false;
}
