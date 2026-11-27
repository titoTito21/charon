// Charon synthesizer module "rhvoice.tts": RHVoice 1.18.5 with every Polish
// and English RHVoice voice whose data is present (r409; Natan and Slt are
// the defaults and the first two voices). This glue: GPL-2.0-or-later.
//
// Data folder (r409):
//   rhvoice-pl.chsp        Polish language data + Natan   (base, as before)
//   rhvoice-en.chsp        English language data + Slt    (base, as before)
//   rhvoice-<voice>.chsp   one further voice (voice.info, voice.params,
//                          16000/...): optional, any subset
// The entry point only probes which packs exist (nothing is read) and offers
// exactly those voices; a voice needs its language's base pack.
//
// ABI 2: Prepare(voice) mounts that voice's pack(s) and creates an RHVoice
// engine for it alone, so only the chosen voice is decompressed and loaded.
// At most maxPrepared voices stay prepared: normally the chosen voice and
// the voice of the other language. A new voice first replaces a prepared
// voice of its own language, else the least recently used one. If preparing
// fails although the pack is intact (out of memory: the HTS loader reports
// that only as a file it cannot open), the other voices are released one by
// one and preparation is retried.
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
constexpr uint32_t maxVoices = 16;      // CharonSynthModule.h: voiceCount <= 16
constexpr uint32_t maxPrepared = 2;     // engines kept loaded: 12..22 MiB each of the 62 MiB budget (three large ones did not fit)

// The two base packs: a language and its default voice.
struct Language { const char* code; const char* pack; const char* mount; const char* dataPath; };
const Language languages[2] = {
    {"pl", "/rhvoice-pl.chsp", "/rhvoice/pl", "/rhvoice/pl/data"},
    {"en", "/rhvoice-en.chsp", "/rhvoice/en", "/rhvoice/en/data"},
};
// Every Polish and English voice of RHVoice 1.18.5. id = "rhvoice-" + the
// RHVoice voice name in lower case (src/synth/rhvoice/VOICES.tsv maps it to
// the names other systems use); profile = the RHVoice voice name. pack: the
// voice's own pack, nullptr for the voice inside its language's base pack.
// Order: the defaults first (Charon takes the first voice of the other
// language for text in that language), then by language and name. Indices
// 0 and 1 never change.
struct Known { const char* id; const char16_t* name; uint32_t language; const char* profile; const char* pack; const char* mount; };
const Known known[] = {
    {"rhvoice-natan", u"Natan", 0, "Natan", nullptr, nullptr},
    {"rhvoice-slt", u"Slt", 1, "Slt", nullptr, nullptr},
    {"rhvoice-alicja", u"Alicja", 0, "Alicja", "/rhvoice-alicja.chsp", "/rhvoice/v/alicja"},
    {"rhvoice-cezary", u"Cezary", 0, "Cezary", "/rhvoice-cezary.chsp", "/rhvoice/v/cezary"},
    {"rhvoice-magda", u"Magda", 0, "Magda", "/rhvoice-magda.chsp", "/rhvoice/v/magda"},
    {"rhvoice-michal", u"Micha\u0142", 0, "Michal", "/rhvoice-michal.chsp", "/rhvoice/v/michal"},
    {"rhvoice-alan", u"Alan", 1, "Alan", "/rhvoice-alan.chsp", "/rhvoice/v/alan"},
    {"rhvoice-bdl", u"Bdl", 1, "Bdl", "/rhvoice-bdl.chsp", "/rhvoice/v/bdl"},
    {"rhvoice-clb", u"Clb", 1, "Clb", "/rhvoice-clb.chsp", "/rhvoice/v/clb"},
    {"rhvoice-dasha-eng", u"Dasha-Eng", 1, "Dasha-Eng", "/rhvoice-dasha-eng.chsp", "/rhvoice/v/dasha-eng"},
    {"rhvoice-evgeniy-eng", u"Evgeniy-Eng", 1, "Evgeniy-Eng", "/rhvoice-evgeniy-eng.chsp", "/rhvoice/v/evgeniy-eng"},
    {"rhvoice-ksp", u"Ksp", 1, "Ksp", "/rhvoice-ksp.chsp", "/rhvoice/v/ksp"},
    {"rhvoice-lyubov", u"Lyubov", 1, "Lyubov", "/rhvoice-lyubov.chsp", "/rhvoice/v/lyubov"},
};
constexpr uint32_t knownCount = sizeof known / sizeof known[0];
static_assert(knownCount <= maxVoices, "CharonSynthModule voice limit");

// The voices whose packs exist on this disk (filled by synth_engine_start).
uint32_t voiceCount;
CharonSynthVoice voices[maxVoices];
const Known* table[maxVoices];

struct Utterance {
    char* text;
    size_t bytes;
    uint32_t voice;
    double rate, pitch, volume;
};

struct State {
    RHVoice_tts_engine engines[maxVoices];
    uint64_t used[maxVoices];        // r409: last Prepare/Begin of each voice (LRU order)
    uint64_t clock;
    uint32_t current[2];             // r409: the voice ESC 'p' / ESC 'e' select (last one used of each language)
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

void PackPath(const char* pack, char* path)
{
    strcpy(path, state->dataDirectory);
    strcat(path, pack);
}

void Touch(uint32_t voice)
{
    state->used[voice] = ++state->clock;
    state->current[table[voice]->language] = voice;
}

// The prepared voice ESC 'p' (language 0) / ESC 'e' (language 1) selects:
// the last one used of that language, else any prepared one, else none.
uint32_t VoiceOfLanguage(uint32_t language, uint32_t fallback)
{
    uint32_t v = state->current[language];
    if (v < voiceCount && table[v]->language == language && state->engines[v]) return v;
    for (v = 0; v < voiceCount; ++v)
        if (table[v]->language == language && state->engines[v]) return v;
    return fallback;
}

void SpeakSegment(const char* text, size_t n, uint32_t voice)
{
    RHVoice_synth_params p;
    memset(&p, 0, sizeof p);
    p.voice_profile = table[voice]->profile;
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
                        next = VoiceOfLanguage(code == 'p' ? 0 : 1, voice);
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
    if (table[voice]->pack) { PackPath(table[voice]->pack, path); synth_vfs_unmount_pack(path); }
    // The language's base pack stays while another voice of it is prepared.
    for (uint32_t v = 0; v < voiceCount; ++v)
        if (state->engines[v] && table[v]->language == table[voice]->language) return;
    PackPath(languages[table[voice]->language].pack, path);
    synth_vfs_unmount_pack(path);
}

uint32_t PreparedCount()
{
    uint32_t n = 0;
    for (uint32_t v = 0; v < voiceCount; ++v) if (state->engines[v]) ++n;
    return n;
}

// Makes room for `keep`: releases a prepared voice of the same language
// (`keep` takes its place), else the least recently used prepared voice.
bool ReleaseOldest(uint32_t keep)
{
    uint32_t oldest = UINT32_MAX;
    for (int pass = 0; pass < 2 && oldest == UINT32_MAX; ++pass)
        for (uint32_t v = 0; v < voiceCount; ++v) {
            if (v == keep || !state->engines[v]) continue;
            if (pass == 0 && table[v]->language != table[keep]->language) continue;
            if (oldest == UINT32_MAX || state->used[v] < state->used[oldest]) oldest = v;
        }
    if (oldest == UINT32_MAX) return false;
    Release(oldest);
    return true;
}

int Mount(const char* pack, const char* at)
{
    char path[600];
    PackPath(pack, path);
    if (synth_vfs_mounted(path)) return 0;
    int mounted = synth_vfs_mount_pack(path, at);
    if (!mounted) return 0;
    synth_diagnose("rhvoice: data pack missing or corrupt");
    return mounted == -2 ? -4 : mounted == -3 ? -2 : -5;
}

// Prepare one voice: mount its pack, create its engine, speak one discarded
// word so the models are loaded now and the first Begin/Render stays short.
void PrepareEntry(void*)
{
    uint32_t v = state->prepareVoice;
    try {
        const Known& k = *table[v];
        const Language& language = languages[k.language];
        int mounted = Mount(language.pack, language.mount);
        if (!mounted && k.pack) mounted = Mount(k.pack, k.mount);
        if (mounted) { state->prepareStatus = mounted; return; }
        RHVoice_init_params p;
        memset(&p, 0, sizeof p);
        p.data_path = language.dataPath;
        p.config_path = "/rhvoice/config";
        p.callbacks.set_sample_rate = SetSampleRate;
        p.callbacks.play_speech = PlaySpeech;
        // A voice with its own pack is a resource directory beside the
        // language's data; the base voice in that data is then left unloaded
        // (no preloading: the discarded word below loads the chosen voice).
        const char* resources[2] = {k.mount, nullptr};
        if (k.pack) p.resource_paths = resources;
        else p.options = RHVoice_preload_voices;
        state->engines[v] = RHVoice_new_tts_engine(&p);
        if (!state->engines[v]) {
            // The C API hides the reason; the same construction through the
            // C++ API reports it for the diagnostic.
            try {
                RHVoice::engine::init_params q;
                q.data_path = p.data_path;
                q.config_path = p.config_path;
                if (k.pack) q.resource_paths.push_back(k.mount);
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
        {
            // The engine must know this very voice (its voice.info was read).
            const unsigned n = RHVoice_get_number_of_voices(state->engines[v]);
            const RHVoice_voice_info* info = RHVoice_get_voices(state->engines[v]);
            bool present = false;
            for (unsigned i = 0; info && i < n; ++i) if (info[i].name && !strcmp(info[i].name, k.profile)) present = true;
            if (!present) {
                synth_diagnose("rhvoice: voice data missing");
                state->prepareStatus = -3;
                return;
            }
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
    if (state->engines[voice]) { Touch(voice); return 0; }
    Cancel(self);
    while (PreparedCount() >= maxPrepared && ReleaseOldest(voice)) {}
    int r = RunPrepare(voice);
    while (r && r != -4 && r != -5) {
        // Not a missing or corrupt pack: most likely out of memory (-2, or -1/-3
        // from the HTS loader). Release another voice and retry.
        Release(voice);
        if (!ReleaseOldest(voice)) break;
        r = RunPrepare(voice);
    }
    if (r) Release(voice);
    else Touch(voice);
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
    Touch(voice);
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
    // Only the data index: which packs exist; nothing is read. A voice is
    // offered when its language's base pack and its own pack (if any) exist.
    const synth_platform* platform = synth_rt_platform();
    bool base[2];
    for (uint32_t l = 0; l < 2; ++l) {
        char path[600];
        PackPath(languages[l].pack, path);
        base[l] = platform->probe_file(platform->context, path, nullptr) == 0;
    }
    voiceCount = 0;
    for (uint32_t i = 0; i < knownCount; ++i) {
        const Known& k = known[i];
        if (!base[k.language]) continue;
        if (k.pack) {
            char path[600];
            PackPath(k.pack, path);
            if (platform->probe_file(platform->context, path, nullptr)) continue;
        }
        CharonSynthVoice& out = voices[voiceCount];
        memset(&out, 0, sizeof out);
        strncpy(out.id, k.id, sizeof out.id - 1);
        for (size_t c = 0; k.name[c] && c + 1 < sizeof out.name / sizeof out.name[0]; ++c) out.name[c] = k.name[c];
        strcpy(out.language, languages[k.language].code);
        out.sampleRate = 16000;
        table[voiceCount++] = &k;
    }
    if (!voiceCount) { synth_diagnose("rhvoice: data pack missing"); return -4; }
    state->current[0] = state->current[1] = UINT32_MAX;
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
