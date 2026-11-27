// SPDX-License-Identifier: MIT-0
// Charon Hypervisor: TVox, Charon's own synthesizer (src/tts), on Alpine Linux. The same LiveVoice runtime
// Charon's UEFI build uses, hosted (malloc pool, stdout sink):
//   charonhv-tvox pl|en RATE PITCH VOLUME < text (UTF-8, Charon's in-text voice switches allowed)
//   > raw signed 16-bit stereo PCM at 48000 Hz
// RATE, PITCH and VOLUME are relative (1.0 = TVox's own). Exit 0 = spoken, 2 = TVox refused, 3 = output failed.
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include "CharonLiveVoice.h"

using namespace charon;

static void* allocate(void*, size_t bytes) { return std::malloc(bytes); }
static void release(void*, void* p) { std::free(p); }
static bool failed = false;
static size_t write(void*, const int16_t* frames, size_t count)
{
    if (std::fwrite(frames, sizeof(int16_t) * AudioEngine::channels, count, stdout) != count)
        failed = true;
    return count;
}
static bool drained(void*) { return true; }

int main(int argc, char** argv)
{
    if (argc != 5) {
        std::fprintf(stderr, "usage: charonhv-tvox pl|en RATE PITCH VOLUME < text\n");
        return 1;
    }
    std::string text((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    static CharonPool pool;
    if (!charon_pool_init(&pool, nullptr, allocate, release, size_t(512) << 20))
        return 2;
    static LiveVoice voice;
    int status = voice.Initialize(&pool);
    const LiveVoice::Language lang = argv[1][0] == 'p' ? LiveVoice::Language::Polish : LiveVoice::Language::English;
    if (!status)
        status = voice.QueueText(lang, text.data(), text.size(), std::atof(argv[2]), std::atof(argv[3]), std::atof(argv[4]));
    if (status) {
        std::fprintf(stderr, "TVox refused the text: %d\n", status);
        return 2;
    }
    AudioEngine::Sink sink;
    sink.Write = write;
    sink.Drained = drained;
    LiveVoice::Budget budget;
    budget.frontend = 4096;
    budget.dspSamples = 8192;
    budget.sinkFrames = 4800;
    while (!voice.SpeechIdle() && !failed) {
        LiveVoice::Progress progress;
        status = voice.Pump(budget, sink, &progress);
        if (status < 0) {
            std::fprintf(stderr, "TVox failed: %d\n", status);
            return 2;
        }
    }
    std::fflush(stdout);
    voice.Shutdown();
    return failed ? 3 : 0;
}
