/* Host reference: the same eSpeak NG sources and compiled data, hosted on
 * Windows with the MSVC C runtime, synthesize one sentence to raw 16-bit PCM.
 * Used to check that espeak.tts (musl-based, freestanding) produces the same
 * audio. Usage: espeak-reference DATA_ROOT (VOICE TEXT OUT.raw)...
 * Utterances run in order in one process, as in the module test. */
#include <stdio.h>
#include <string.h>
#include <espeak-ng/espeak_ng.h>
#include <espeak-ng/speak_lib.h>

static FILE* out;
static int Callback(short* wav, int n, espeak_EVENT* events)
{
    (void)events;
    if (wav && n > 0) fwrite(wav, 2, (size_t)n, out);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 5 || (argc - 2) % 3) return 2;
    espeak_ng_InitializePath(argv[1]);
    espeak_ng_ERROR_CONTEXT context = NULL;
    if (espeak_ng_Initialize(&context) != ENS_OK) return 1;
    if (espeak_ng_InitializeOutput(ENOUTPUT_MODE_SYNCHRONOUS, 20, NULL) != ENS_OK) return 1;
    espeak_SetSynthCallback(Callback);
    for (int i = 2; i + 2 < argc; i += 3) {
        if (espeak_ng_SetVoiceByName(argv[i]) != ENS_OK) return 1;
        espeak_ng_SetParameter(espeakRATE, 175, 0);
        espeak_ng_SetParameter(espeakPITCH, 50, 0);
        espeak_ng_SetParameter(espeakVOLUME, 100, 0);
        out = fopen(argv[i + 2], "wb");
        if (!out) return 1;
        espeak_ng_Synthesize(argv[i + 1], strlen(argv[i + 1]) + 1, 0, POS_CHARACTER, 0, espeakCHARS_UTF8, NULL, NULL);
        fclose(out);
    }
    return 0;
}
