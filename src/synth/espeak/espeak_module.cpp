// Charon synthesizer module "espeak.tts": eSpeak NG (GPL-3.0-or-later).
// This glue: GPL-3.0-or-later (it links eSpeak NG).
//
// eSpeak NG runs unmodified in its synchronous mode. Its audio callback
// copies each 20 ms buffer into a ring and yields the synthesis coroutine,
// so Render() advances the engine one buffer at a time and Begin() does no
// synthesis at all. Prepare(voice) loads the phoneme tables (once) and that
// language's dictionary (ABI 2). Language hints ESC 'p' / ESC 'e' select
// the other voice for the following text when it is prepared; ESC 'v' (+4 bytes), ESC 'f' (+6) and ESC 'i'
// (+2 bytes) are formatting switches this module skips.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <espeak-ng/espeak_ng.h>
#include <espeak-ng/speak_lib.h>
#include "CharonSynthModule.h"
#include "synth_rt.h"

using hakefi::CharonSynthModule;
using hakefi::CharonSynthVoice;

namespace {
constexpr size_t ringCapacity = 8192;
constexpr size_t stackBytes = (size_t)512 << 10;
constexpr int maxResumesPerRender = 12;

CharonSynthVoice voices[2] = {
    {"espeak-pl", u"eSpeak NG polski", "pl", 22050},
    {"espeak-en", u"eSpeak NG English", "en", 22050},
};
const char* const voiceNames[2] = {"pl", "en"};

struct Utterance {
    char* text;
    size_t bytes;
    uint32_t voice;
    int rate, pitch, volume;
    int range;          // r397 ACSS pitch range, espeakRANGE 0..100 (50 = the voice's own)
    uint32_t family;    // r397 voice family 0..7 (a variant, familyVariants)
};
// r397: Charon's voice families as eSpeak NG variants (packed by build-charon-synths.py ESPEAK_VARIANTS).
const char* const familyVariants[8] = {nullptr, "m3", "f3", "m6", "m7", "f4", "klatt", "f1"};

struct State {
    synth_coro* coroutine;
    Utterance utterance;
    int16_t ring[ringCapacity];
    size_t head, count;
    bool cancel, finished, failed, active;
    bool initialized, prepared[2];
    int personaRange;       // r397 SetPersona, used by the next Begin
    uint32_t personaFamily;
    uint32_t prepareVoice;
    int initStatus;
    char dataDirectory[512];
};

State* state;
CharonSynthModule module;

void Push(const short* samples, int n)
{
    while (n > 0) {
        size_t space = ringCapacity - state->count;
        if (!space) { synth_coro_yield(); if (state->cancel) return; continue; }
        size_t take = (size_t)n < space ? (size_t)n : space;
        for (size_t i = 0; i < take; ++i)
            state->ring[(state->head + state->count + i) % ringCapacity] = samples[i];
        state->count += take;
        samples += take;
        n -= (int)take;
    }
}

int SynthCallback(short* samples, int n, espeak_EVENT* events)
{
    (void)events;
    if (state->cancel) return 1;
    if (samples && n > 0) {
        Push(samples, n);
        // One engine buffer per resume: Render decides whether to go on.
        synth_coro_yield();
    }
    return state->cancel ? 1 : 0;
}

void ApplyVoice(uint32_t voice)
{
    // r397: the family's variant ("pl+m3"); the plain voice if it is missing.
    const char* variant = state->utterance.family < 8 ? familyVariants[state->utterance.family] : nullptr;
    bool set = false;
    if (variant) {
        char name[16];
        strcpy(name, voiceNames[voice]); strcat(name, "+"); strcat(name, variant);
        set = espeak_ng_SetVoiceByName(name) == ENS_OK;
    }
    if (!set) espeak_ng_SetVoiceByName(voiceNames[voice]);
    espeak_ng_SetParameter(espeakRANGE, state->utterance.range, 0);
    espeak_ng_SetParameter(espeakRATE, state->utterance.rate, 0);
    espeak_ng_SetParameter(espeakPITCH, state->utterance.pitch, 0);
    espeak_ng_SetParameter(espeakVOLUME, state->utterance.volume, 0);
}

// Speaks text[0..bytes) as consecutive segments split at language switches.
void SpeakEntry(void*)
{
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
                    // Language hint: honoured only for a prepared voice.
                    next = code == 'p' ? 0 : 1;
                    if (!state->prepared[next]) next = voice;
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
        if (speakable) {
            ApplyVoice(voice);
            espeak_ng_Synthesize(segment, n + 1, 0, POS_CHARACTER, 0, espeakCHARS_UTF8, nullptr, nullptr);
        }
        voice = next;
    }
    free(segment);
}

const char* const voicePacks[2] = {"/espeak-pl.chsp", "/espeak-en.chsp"};
const char commonPack[] = "/espeak-ng-data.chsp";

int Mount(const char* name)
{
    char pack[600];
    strcpy(pack, state->dataDirectory);
    strcat(pack, name);
    if (synth_vfs_mounted(pack)) return 0;
    int mounted = synth_vfs_mount_pack(pack, "/espeak/espeak-ng-data");
    if (!mounted) return 0;
    synth_diagnose("espeak: data pack missing or corrupt");
    return mounted == -2 ? -4 : mounted == -3 ? -2 : -5;
}

// Prepare: phoneme tables on first use, then one language's dictionary and
// voice file. Both languages stay prepared (together about 4 MiB).
void PrepareEntry(void*)
{
    uint32_t v = state->prepareVoice;
    if (!state->initialized) {
        if ((state->initStatus = Mount(commonPack))) return;
        espeak_ng_InitializePath("/espeak");
        espeak_ng_ERROR_CONTEXT context = nullptr;
        if (espeak_ng_Initialize(&context) != ENS_OK) { synth_diagnose("espeak: espeak_ng_Initialize failed"); state->initStatus = -1; return; }
        if (espeak_ng_InitializeOutput(ENOUTPUT_MODE_SYNCHRONOUS, 20, nullptr) != ENS_OK) { state->initStatus = -1; return; }
        espeak_SetSynthCallback(SynthCallback);
        int rate = espeak_ng_GetSampleRate();
        if (rate <= 0) { state->initStatus = -1; return; }
        for (auto& voice : voices) voice.sampleRate = (uint32_t)rate;
        state->initialized = true;
    }
    if ((state->initStatus = Mount(voicePacks[v]))) return;
    espeak_ListVoices(nullptr);  // rescan the voice list: the new pack adds this language
    if (espeak_ng_SetVoiceByName(voiceNames[v]) != ENS_OK) { synth_diagnose("espeak: voice data unusable"); state->initStatus = -3; return; }
    state->prepared[v] = true;
    state->initStatus = 0;
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
    // Let the engine leave its callback with "abort" and unwind normally.
    for (int i = 0; i < 1000 && state->coroutine; ++i) {
        int r = synth_coro_resume(state->coroutine);
        if (r < 0) { state->failed = true; break; }
        if (r == 0) break;
    }
    Finish();
    state->head = state->count = 0;
    state->cancel = false;
    state->finished = true;
}

int Clamp(double value, int low, int high)
{
    int v = (int)(value + 0.5);
    return v < low ? low : v > high ? high : v;
}

int CHARON_SYNTH_API Begin(CharonSynthModule* self, uint32_t voice, const char* text, size_t bytes, double rate, double pitch, double volume)
{
    if (!state || state->failed) return -1;
    Cancel(self);
    if (voice >= 2 || !state->prepared[voice]) return -3;
    if (!text && bytes) return -1;
    if (!(rate >= 0.5)) rate = 0.5; if (rate > 2.0) rate = 2.0;
    if (!(pitch >= 0.5)) pitch = 0.5; if (pitch > 2.0) pitch = 2.0;
    if (!(volume >= 0.0)) volume = 0.0; if (volume > 1.0) volume = 1.0;
    char* copy = static_cast<char*>(malloc(bytes + 1));
    if (!copy) return -2;
    if (bytes) memcpy(copy, text, bytes);
    copy[bytes] = 0;
    state->utterance = Utterance{copy, bytes, voice, Clamp(175.0 * rate, 80, 450), Clamp(50.0 * pitch, 0, 100),
                                 Clamp(100.0 * volume, 0, 200), state->personaRange, state->personaFamily};
    state->coroutine = synth_coro_create(stackBytes, SpeakEntry, nullptr);
    if (!state->coroutine) { Finish(); return -2; }
    state->head = state->count = 0;
    state->finished = false;
    state->active = true;
    return 0;
}

int CHARON_SYNTH_API Render(CharonSynthModule*, int16_t* out, size_t maxSamples)
{
    if (!state || state->failed) return -2;
    if (!state->active && !state->count) return -1;
    for (int i = 0; i < maxResumesPerRender && state->active && state->count < maxSamples; ++i) {
        int r = synth_coro_resume(state->coroutine);
        if (r < 0) { state->failed = true; Finish(); return -2; }
        if (r == 0) { Finish(); state->finished = true; }
    }
    size_t n = state->count < maxSamples ? state->count : maxSamples;
    for (size_t i = 0; i < n; ++i) out[i] = state->ring[(state->head + i) % ringCapacity];
    state->head = (state->head + n) % ringCapacity;
    state->count -= n;
    if (!n && !state->active) return -1;
    return (int)n;
}
// r397 ABI 3: Emacspeak ACSS persona. eSpeak NG takes the pitch range
// (espeakRANGE) and the family (a voice variant); stress, richness and
// smoothness are left to Charon.
int CHARON_SYNTH_API SetPersona(CharonSynthModule*, const hakefi::CharonSynthPersona* p)
{
    if (!state || !p || p->size < sizeof(hakefi::CharonSynthPersona)) return -1;
    double range = p->range;
    if (!(range >= 0.0)) range = 0.0; if (range > 2.0) range = 2.0;
    state->personaRange = Clamp(50.0 * range, 0, 100);
    state->personaFamily = p->family < 8 ? p->family : 0;
    return int(hakefi::charonPersonaRange | hakefi::charonPersonaFamily);
}
int CHARON_SYNTH_API Prepare(CharonSynthModule* self, uint32_t voice)
{
    if (!state || state->failed) return -1;
    if (voice >= 2) return -3;
    if (state->prepared[voice]) return 0;
    Cancel(self);
    synth_coro* c = synth_coro_create(stackBytes, PrepareEntry, nullptr);
    if (!c) return -2;
    state->prepareVoice = voice;
    state->initStatus = -1;
    int r = synth_coro_resume(c);
    synth_coro_destroy(c);
    if (r < 0) { state->failed = true; return -1; }
    if (r > 0) return -1;
    return state->initStatus;
}
}  // namespace

extern "C" int synth_engine_start(const char* dataDirectory, CharonSynthModule** out)
{
    state = static_cast<State*>(calloc(1, sizeof(State)));
    if (!state) return -2;
    size_t n = strlen(dataDirectory);
    if (n >= sizeof state->dataDirectory) return -1;
    memcpy(state->dataDirectory, dataDirectory, n + 1);
    synth_vfs_set_cache(1);
    // Only the data index: every pack must exist; nothing is decompressed.
    const synth_platform* platform = synth_rt_platform();
    const char* const packs[3] = {commonPack, voicePacks[0], voicePacks[1]};
    for (const char* name : packs) {
        char path[600];
        strcpy(path, state->dataDirectory);
        strcat(path, name);
        if (platform->probe_file(platform->context, path, nullptr)) { synth_diagnose("espeak: data pack missing"); return -4; }
    }
    module.abi = hakefi::charonSynthAbi;
    module.voiceCount = 2;
    const char16_t engine[] = u"eSpeak NG 1.53";
    for (size_t i = 0; i < sizeof engine / sizeof engine[0]; ++i) module.engine[i] = engine[i];
    module.voices = voices;
    module.Prepare = Prepare;
    module.Begin = Begin;
    module.Render = Render;
    module.Cancel = Cancel;
    module.SetPersona = SetPersona;
    state->personaRange = 50;
    module.context = state;
    *out = &module;
    return 0;
}
