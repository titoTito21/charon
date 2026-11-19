// Charon synthesizer module "rhvoice.tts": RHVoice 1.18.5 with the voices
// Natan (Polish) and Slt (English). This glue: GPL-2.0-or-later.
//
// ABI 2: the entry point only checks the data index; Prepare(voice) mounts
// that voice's pack (its language + voice data) and creates an RHVoice
// engine for it alone, so only the chosen voice is decompressed and loaded.
// Both voices stay prepared when they fit; if preparing one runs out of
// memory, the other is released and preparation is retried once.
//
// RHVoice runs through its public C API (src/lib/lib.cpp). RHVoice_speak()
// is synchronous; its play_speech callback copies samples into a ring and
// yields the synthesis coroutine, so Render() advances the engine a few
// frames at a time and Begin() does no synthesis. Language hints ESC 'p' /
// ESC 'e' choose the other voice for the following text when it is
// prepared; ESC 'v' (+4 bytes), ESC 'f' (+6) and ESC 'i' (+2 bytes) are
// formatting switches this module skips. C++ exceptions work inside the
// module (libc++abi + libunwind over the image's own .eh_frame); none may
// leave it.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <exception>
#include "RHVoice.h"
#include "core/engine.hpp"
#include "CharonSynthModule.h"
#include "synth_rt.h"

using hakefi::CharonSynthModule;
using hakefi::CharonSynthVoice;

namespace {
constexpr size_t ringCapacity = 16384;
constexpr size_t stackBytes = (size_t)1 << 20;
constexpr int maxResumesPerRender = 64;
constexpr uint32_t voiceCount = 2;

CharonSynthVoice voices[voiceCount] = {
    {"rhvoice-natan", u"Natan", "pl", 16000},
    {"rhvoice-slt", u"Slt", "en", 16000},
};
const char* const profiles[voiceCount] = {"Natan", "Slt"};
const char* const packs[voiceCount] = {"/rhvoice-pl.chsp", "/rhvoice-en.chsp"};
const char* const mounts[voiceCount] = {"/rhvoice/pl", "/rhvoice/en"};
const char* const dataPaths[voiceCount] = {"/rhvoice/pl/data", "/rhvoice/en/data"};

struct Utterance {
    char* text;
    size_t bytes;
    uint32_t voice;
    double rate, pitch, volume;
};

struct State {
    RHVoice_tts_engine engines[voiceCount];
    synth_coro* coroutine;
    Utterance utterance;
    int16_t ring[ringCapacity];
    size_t head, count;
    int sampleRate;
    bool cancel, failed, active, preloading;
    int prepareStatus;
    uint32_t prepareVoice;
    char dataDirectory[512];
};

State* state;
CharonSynthModule module;

int SetSampleRate(int rate, void*)
{
    state->sampleRate = rate;
    return 1;
}

int PlaySpeech(const short* samples, unsigned int n, void*)
{
    if (state->cancel) return 0;
    if (state->preloading) return 1;  // warm-up utterance in Prepare: audio discarded
    while (n > 0) {
        size_t space = ringCapacity - state->count;
        if (!space) { synth_coro_yield(); if (state->cancel) return 0; continue; }
        size_t take = n < space ? n : space;
        for (size_t i = 0; i < take; ++i)
            state->ring[(state->head + state->count + i) % ringCapacity] = samples[i];
        state->count += take;
        samples += take;
        n -= (unsigned)take;
    }
    synth_coro_yield();
    return state->cancel ? 0 : 1;
}

void Diagnose(const char* prefix, const char* what)
{
    char text[160];
    size_t n = strlen(prefix);
    memcpy(text, prefix, n);
    size_t m = what ? strlen(what) : 0;
    if (n + m >= sizeof text) m = sizeof text - n - 1;
    if (m) memcpy(text + n, what, m);
    text[n + m] = 0;
    synth_diagnose(text);
}

void PackPath(uint32_t voice, char* path)
{
    strcpy(path, state->dataDirectory);
    strcat(path, packs[voice]);
}

void SpeakSegment(const char* text, size_t n, uint32_t voice)
{
    RHVoice_synth_params p;
    memset(&p, 0, sizeof p);
    p.voice_profile = profiles[voice];
    p.absolute_rate = 0;
    p.absolute_pitch = 0;
    p.absolute_volume = 0;
    p.relative_rate = state->utterance.rate;
    p.relative_pitch = state->utterance.pitch;
    p.relative_volume = state->utterance.volume;
    p.punctuation_mode = RHVoice_punctuation_default;
    p.capitals_mode = RHVoice_capitals_default;
    RHVoice_message message = RHVoice_new_message(state->engines[voice], text, (unsigned)n, RHVoice_message_text, &p, nullptr);
    if (!message) return;
    RHVoice_speak(message);
    RHVoice_delete_message(message);
}

void SpeakEntry(void*)
{
    try {
        Utterance& u = state->utterance;
        uint32_t voice = u.voice;
        size_t at = 0;
        char* segment = static_cast<char*>(malloc(u.bytes + 1));
        if (!segment) return;
        while (at < u.bytes && !state->cancel) {
            size_t n = 0;
            uint32_t next = voice;
            while (at < u.bytes) {
                unsigned char c = (unsigned char)u.text[at];
                if (c == 0x1b && at + 1 < u.bytes) {
                    char code = u.text[at + 1];
                    if (code == 'p' || code == 'e') {
                        next = code == 'p' ? 0 : 1;
                        if (!state->engines[next]) next = voice;
                        at += 2;
                        break;
                    }
                    at += code == 'v' ? 6 : code == 'f' ? 14 : code == 'i' ? 4 : 2;   // r397: ESC 'f' + 12
                    continue;
                }
                if (c == 0x1b) { ++at; continue; }
                segment[n++] = (char)c;
                ++at;
            }
            segment[n] = 0;
            bool speakable = false;
            for (size_t i = 0; i < n; ++i) if ((unsigned char)segment[i] > ' ') { speakable = true; break; }
            if (speakable) SpeakSegment(segment, n, voice);
            voice = next;
        }
        free(segment);
    } catch (...) {
        state->failed = true;
    }
}

void Release(uint32_t voice)
{
    if (state->engines[voice]) RHVoice_delete_tts_engine(state->engines[voice]);
    state->engines[voice] = nullptr;
    char path[600];
    PackPath(voice, path);
    synth_vfs_unmount_pack(path);
}

// Prepare one voice: mount its pack, create its engine, speak one discarded
// word so the models are loaded now and the first Begin/Render stays short.
void PrepareEntry(void*)
{
    uint32_t v = state->prepareVoice;
    try {
        char path[600];
        PackPath(v, path);
        if (!synth_vfs_mounted(path)) {
            int mounted = synth_vfs_mount_pack(path, mounts[v]);
            if (mounted) {
                synth_diagnose("rhvoice: data pack missing or corrupt");
                state->prepareStatus = mounted == -2 ? -4 : mounted == -3 ? -2 : -5;
                return;
            }
        }
        RHVoice_init_params p;
        memset(&p, 0, sizeof p);
        p.data_path = dataPaths[v];
        p.config_path = "/rhvoice/config";
        p.callbacks.set_sample_rate = SetSampleRate;
        p.callbacks.play_speech = PlaySpeech;
        p.options = RHVoice_preload_voices;
        state->engines[v] = RHVoice_new_tts_engine(&p);
        if (!state->engines[v]) {
            // The C API hides the reason; the same construction through the
            // C++ API reports it for the diagnostic.
            try {
                RHVoice::engine::init_params q;
                q.data_path = p.data_path;
                q.config_path = p.config_path;
                RHVoice::engine probe(q);
                synth_diagnose("rhvoice: engine failed only through the C API");
            } catch (const std::bad_alloc&) {
                synth_diagnose("rhvoice: out of memory");
                state->prepareStatus = -2;
                return;
            } catch (const std::exception& e) {
                Diagnose("rhvoice engine: ", e.what());
            }
            state->prepareStatus = -1;
            return;
        }
        if (RHVoice_get_number_of_voices(state->engines[v]) < 1) {
            synth_diagnose("rhvoice: voice data missing");
            state->prepareStatus = -3;
            return;
        }
        state->utterance.rate = state->utterance.pitch = state->utterance.volume = 1.0;
        state->cancel = false;
        state->preloading = true;
        state->sampleRate = 0;
        SpeakSegment("a", 1, v);
        state->preloading = false;
        state->head = state->count = 0;
        if (state->sampleRate <= 0) { synth_diagnose("rhvoice: voice did not load"); state->prepareStatus = -3; return; }
        voices[v].sampleRate = (uint32_t)state->sampleRate;
        synth_diagnose("");
        state->prepareStatus = 0;
    } catch (const std::bad_alloc&) {
        synth_diagnose("rhvoice: out of memory");
        state->prepareStatus = -2;
    } catch (const std::exception& e) {
        Diagnose("rhvoice prepare: ", e.what());
        state->prepareStatus = -1;
    } catch (...) {
        synth_diagnose("rhvoice: unknown exception");
        state->prepareStatus = -1;
    }
    state->preloading = false;
}

void Finish()
{
    if (state->coroutine) synth_coro_destroy(state->coroutine);
    state->coroutine = nullptr;
    free(state->utterance.text);
    state->utterance.text = nullptr;
    state->active = false;
}

void CHARON_SYNTH_API Cancel(CharonSynthModule*)
{
    if (!state || !state->active) return;
    state->cancel = true;
    for (int i = 0; i < 100000 && state->coroutine; ++i) {
        int r = synth_coro_resume(state->coroutine);
        if (r < 0) { state->failed = true; break; }
        if (r == 0) break;
    }
    Finish();
    state->head = state->count = 0;
    state->cancel = false;
}

int RunPrepare(uint32_t voice)
{
    synth_coro* c = synth_coro_create(stackBytes, PrepareEntry, nullptr);
    if (!c) return -2;
    state->prepareVoice = voice;
    state->prepareStatus = -1;
    int r = synth_coro_resume(c);
    synth_coro_destroy(c);
    if (r < 0) { state->failed = true; return -1; }
    return r > 0 ? -1 : state->prepareStatus;
}

int CHARON_SYNTH_API Prepare(CharonSynthModule* self, uint32_t voice)
{
    if (!state || state->failed) return -1;
    if (voice >= voiceCount) return -3;
    if (state->engines[voice]) return 0;
    Cancel(self);
    int r = RunPrepare(voice);
    if (r == -2) {
        // Out of memory: release the other voice and try once more.
        Release(voice);
        for (uint32_t other = 0; other < voiceCount; ++other)
            if (other != voice && state->engines[other]) Release(other);
        r = RunPrepare(voice);
    }
    if (r) Release(voice);
    return r;
}

double Clamp(double v, double low, double high) { return !(v >= low) ? low : v > high ? high : v; }

int CHARON_SYNTH_API Begin(CharonSynthModule* self, uint32_t voice, const char* text, size_t bytes, double rate, double pitch, double volume)
{
    if (!state || state->failed) return -1;
    Cancel(self);
    if (voice >= voiceCount || !state->engines[voice]) return -3;
    if (!text && bytes) return -1;
    char* copy = static_cast<char*>(malloc(bytes + 1));
    if (!copy) return -2;
    if (bytes) memcpy(copy, text, bytes);
    copy[bytes] = 0;
    state->utterance = Utterance{copy, bytes, voice, Clamp(rate, 0.5, 2.0), Clamp(pitch, 0.5, 2.0), Clamp(volume, 0.0, 1.0)};
    state->coroutine = synth_coro_create(stackBytes, SpeakEntry, nullptr);
    if (!state->coroutine) { Finish(); return -2; }
    state->head = state->count = 0;
    state->active = true;
    return 0;
}

int CHARON_SYNTH_API Render(CharonSynthModule*, int16_t* out, size_t maxSamples)
{
    if (!state || state->failed) return -2;
    if (!state->active && !state->count) return -1;
    for (int i = 0; i < maxResumesPerRender && state->active && state->count < maxSamples; ++i) {
        int r = synth_coro_resume(state->coroutine);
        if (r < 0 || state->failed) { state->failed = true; Finish(); return -2; }
        if (r == 0) Finish();
    }
    size_t n = state->count < maxSamples ? state->count : maxSamples;
    for (size_t i = 0; i < n; ++i) out[i] = state->ring[(state->head + i) % ringCapacity];
    state->head = (state->head + n) % ringCapacity;
    state->count -= n;
    if (!n && !state->active) return -1;
    return (int)n;
}
}  // namespace

extern "C" void synth_run_constructors(void);

extern "C" int synth_engine_start(const char* dataDirectory, CharonSynthModule** out)
{
    state = static_cast<State*>(calloc(1, sizeof(State)));
    if (!state) return -2;
    size_t n = strlen(dataDirectory);
    if (n >= sizeof state->dataDirectory) return -1;
    memcpy(state->dataDirectory, dataDirectory, n + 1);
    // Only the data index: every voice pack must exist; nothing is read.
    const synth_platform* platform = synth_rt_platform();
    for (uint32_t v = 0; v < voiceCount; ++v) {
        char path[600];
        PackPath(v, path);
        if (platform->probe_file(platform->context, path, nullptr)) { synth_diagnose("rhvoice: data pack missing"); return -4; }
    }
    // C++ static objects of RHVoice and libc++ (std::cout, locale ids, tables)
    // are constructed here, explicitly, on the engine's own stack.
    synth_coro* constructors = synth_coro_create(stackBytes, [](void*) { synth_run_constructors(); }, nullptr);
    if (!constructors) return -2;
    int r = synth_coro_resume(constructors);
    synth_coro_destroy(constructors);
    if (r != 0) { synth_diagnose("rhvoice: static constructors failed"); return -1; }
    module.abi = hakefi::charonSynthAbiOldest;   // r397: ABI 2, no SetPersona (Charon applies the persona after it)
    module.voiceCount = voiceCount;
    const char16_t engine[] = u"RHVoice 1.18";
    for (size_t i = 0; i < sizeof engine / sizeof engine[0]; ++i) module.engine[i] = engine[i];
    module.voices = voices;
    module.Prepare = Prepare;
    module.Begin = Begin;
    module.Render = Render;
    module.Cancel = Cancel;
    module.context = state;
    *out = &module;
    return 0;
}
