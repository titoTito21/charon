// Full PCM path with a bounded producer and playback-clock markers.
// GPL-2.0-or-later. Not yet an NVDA Python driver integration test.
#include "../src/HaikuAudioNode.h"
#include "../src/HaikuPcmQueue.h"
#include <Application.h>
#include <ByteOrder.h>
#include <fstream>
#include <iterator>
#include <thread>
#include <cstdio>
#include <algorithm>

struct Audio {
    HaikuPcmQueue queue{8820, 2, 22050}; // 200ms, independent of utterance length
    HaikuAudioNode* node = nullptr;
};
static void Render(void* context, void* data, size_t size) {
    static_cast<Audio*>(context)->queue.Render(data, size);
}
static void Submitted(void* context, bigtime_t when, size_t) {
    static_cast<Audio*>(context)->queue.Submitted(when);
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    BApplication app("application/x-vnd.NVDAHaiku.AudioQueueLiveTest");
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<char> pcm{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (pcm.empty() || pcm.size() % 2) return 3;
    Audio audio;
    media_raw_audio_format format = media_raw_audio_format::wildcard;
    format.frame_rate = 22050; format.channel_count = 1;
    format.format = media_raw_audio_format::B_AUDIO_SHORT;
    format.byte_order = B_MEDIA_HOST_ENDIAN;
    audio.node = new HaikuAudioNode(format, Render, Submitted, &audio);
    status_t status = audio.node->Open();
    bool fed = false;
    std::thread producer([&] {
        size_t half = (pcm.size() / 4) * 2;
        fed = audio.queue.Feed(pcm.data(), half, 101)
            && audio.queue.Feed(pcm.data() + half, pcm.size() - half, 102);
    });
    if (status == B_OK) status = audio.node->Begin();
    std::vector<uint64_t> completed;
    size_t maximum = 0;
    bigtime_t deadline = system_time() + 10000000;
    while (status == B_OK && completed.size() < 2 && system_time() < deadline) {
        auto ready = audio.queue.Poll(audio.node->PlaybackTime());
        for (auto mark : ready) {
            if (mark.generation != audio.queue.Generation()) { status = B_ERROR; break; }
            completed.push_back(mark.id);
            printf("played marker=%llu\n", (unsigned long long)mark.id);
        }
        maximum = std::max(maximum, audio.queue.Buffered());
        if (status == B_OK) status = audio.node->Error();
        snooze(1000);
    }
    if (completed.size() == 2) snooze(100000);
    audio.node->Halt();
    audio.queue.Close(); // Unblock producer even if the device or test failed.
    producer.join();
    audio.node->Close(); audio.node->Release();
    bool success = status == B_OK && fed && maximum <= 8820
        && completed == std::vector<uint64_t>{101, 102};
    printf("status=%ld fed=%d peakBuffered=%zu passed=%d\n", (long)status, fed, maximum, success);
    return success ? 0 : 1;
}
