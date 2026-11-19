// Live transport/timestamp test, not a complete NVDA WavePlayer test.
// GPL-2.0-or-later.
#include "../src/HaikuAudioNode.h"
#include <Application.h>
#include <ByteOrder.h>
#include <atomic>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>

struct Stream {
    std::vector<char> pcm;
    size_t offset = 0, previous = 0;
    std::atomic<bigtime_t> midpoint{0}, end{0};
    std::atomic<size_t> renders{0};
};
static void Render(void* cookie, void* buffer, size_t size) {
    Stream& stream = *static_cast<Stream*>(cookie);
    stream.previous = stream.offset;
    size_t copied = std::min(size, stream.pcm.size() - stream.offset);
    memset(buffer, 0, size);
    if (copied) memcpy(buffer, stream.pcm.data() + stream.offset, copied);
    stream.offset += copied;
    ++stream.renders;
}
static void Submitted(void* cookie, bigtime_t when, size_t) {
    Stream& stream = *static_cast<Stream*>(cookie);
    size_t mark = (stream.pcm.size() / 4) * 2;
    if (stream.previous < mark && stream.offset >= mark)
        stream.midpoint = when + (mark - stream.previous) * 1000000LL / 44100;
    if (stream.previous < stream.pcm.size() && stream.offset == stream.pcm.size())
        stream.end = when + (stream.offset - stream.previous) * 1000000LL / 44100;
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    BApplication app("application/x-vnd.NVDAHaiku.AudioNodeTest");
    Stream stream;
    std::ifstream input(argv[1], std::ios::binary);
    stream.pcm.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (stream.pcm.empty() || stream.pcm.size() % 2) return 3;
    media_raw_audio_format format = media_raw_audio_format::wildcard;
    format.frame_rate = 22050; format.channel_count = 1;
    format.format = media_raw_audio_format::B_AUDIO_SHORT;
    format.byte_order = B_MEDIA_HOST_ENDIAN;
    HaikuAudioNode* node = new HaikuAudioNode(format, Render, Submitted, &stream);
    status_t status = node->Open();
    // Halt must acknowledge a queued START as well as an already-running node.
    for (int trial = 0; status == B_OK && trial < 3; ++trial) {
        status = node->Begin();
        if (status == B_OK) status = node->Halt();
        size_t count = stream.renders.load();
        snooze(150000);
        if (stream.renders.load() != count) status = B_ERROR;
    }
    if (status == B_OK) puts("queued start/halt: no rendering after stop acknowledgement");
    stream.offset = stream.previous = 0;
    stream.midpoint = stream.end = 0;
    if (status == B_OK) status = node->Begin();
    bool middle = false, done = false;
    bigtime_t deadline = system_time() + 10000000;
    while (status == B_OK && system_time() < deadline) {
        bigtime_t now = node->PlaybackTime();
        if (!middle && stream.midpoint.load() && now >= stream.midpoint.load()) {
            printf("midpoint reached: scheduled=%lld observed=%lld\n",
                (long long)stream.midpoint.load(), (long long)now);
            middle = true;
        }
        if (stream.end.load() && now >= stream.end.load()) {
            printf("end reached: scheduled=%lld observed=%lld\n",
                (long long)stream.end.load(), (long long)now);
            done = true; break;
        }
        status = node->Error(); snooze(1000);
    }
    // Capture the final device tail before tearing down the test connection.
    if (done) snooze(100000);
    node->Close(); node->Release();
    printf("status=%ld midpoint=%d end=%d\n", (long)status, middle, done);
    return status == B_OK && middle && done ? 0 : 1;
}
