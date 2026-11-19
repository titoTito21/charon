// Host reference: the same (patched) RHVoice sources and data, hosted on
// Windows with the MSVC C++ library, synthesize sentences to raw 16-bit PCM
// through the public C API, as rhvoice.tts does.
// Usage: rhvoice-reference DATA_DIR CONFIG_DIR (PROFILE TEXT OUT.raw)...
#include <stdio.h>
#include <string.h>
#include "RHVoice.h"

static FILE* out;
static int rate;
static int SetRate(int r, void*) { rate = r; return 1; }
static int Play(const short* samples, unsigned int count, void*) { fwrite(samples, 2, count, out); return 1; }

int main(int argc, char** argv)
{
    if (argc < 6 || (argc - 3) % 3) return 2;
    RHVoice_init_params p{};
    p.data_path = argv[1];
    p.config_path = argv[2];
    p.callbacks.set_sample_rate = SetRate;
    p.callbacks.play_speech = Play;
    RHVoice_tts_engine engine = RHVoice_new_tts_engine(&p);
    if (!engine) { fprintf(stderr, "engine failed\n"); return 1; }
    for (int i = 3; i + 2 < argc; i += 3) {
        RHVoice_synth_params s{};
        s.voice_profile = argv[i];
        s.absolute_rate = 0; s.absolute_pitch = 0; s.absolute_volume = 0;
        s.relative_rate = 1; s.relative_pitch = 1; s.relative_volume = 1;
        s.punctuation_mode = RHVoice_punctuation_default;
        s.capitals_mode = RHVoice_capitals_default;
        RHVoice_message m = RHVoice_new_message(engine, argv[i + 1], (unsigned)strlen(argv[i + 1]), RHVoice_message_text, &s, nullptr);
        if (!m) { fprintf(stderr, "message failed\n"); return 1; }
        out = fopen(argv[i + 2], "wb");
        int ok = RHVoice_speak(m);
        fclose(out);
        RHVoice_delete_message(m);
        fprintf(stderr, "%s rate %d ok %d\n", argv[i], rate, ok);
    }
    RHVoice_delete_tts_engine(engine);
    return 0;
}
