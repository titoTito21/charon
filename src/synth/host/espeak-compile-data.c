/* Host (Windows) driver that compiles eSpeak NG data with the unmodified
 * library compilers, exactly as the upstream data.cmake does with the
 * espeak-ng CLI: intonations, phonemes (22050 Hz) and one dictionary per
 * argument. Usage: espeak-compile-data DATA_ROOT DICTSOURCE_DIR/ dict...
 * DATA_ROOT holds espeak-ng-data/ (lang, voices) and phsource/. */
#include <stdio.h>
#include <stdlib.h>
#include <espeak-ng/espeak_ng.h>

static int check(espeak_ng_STATUS status, espeak_ng_ERROR_CONTEXT context, const char* what)
{
    if (status == ENS_OK) return 0;
    fprintf(stderr, "FAILED %s\n", what);
    espeak_ng_PrintStatusCodeMessage(status, stderr, context);
    return 1;
}

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: DATA_ROOT DICTSOURCE/ dict...\n"); return 2; }
    espeak_ng_ERROR_CONTEXT context = NULL;
    espeak_ng_InitializePath(argv[1]);
    if (check(espeak_ng_CompileIntonation(stdout, &context), context, "intonations")) return 1;
    if (check(espeak_ng_CompilePhonemeData(22050, stdout, &context), context, "phonemes")) return 1;
    if (check(espeak_ng_Initialize(&context), context, "initialize")) return 1;
    for (int i = 3; i < argc; ++i) {
        if (check(espeak_ng_SetVoiceByName(argv[i]), NULL, argv[i])) return 1;
        if (check(espeak_ng_CompileDictionary(argv[2], argv[i], stdout, 0, &context), context, argv[i])) return 1;
    }
    return 0;
}
