// Native lifecycle regression: paused producer, resume, cancellation and reuse.
// GPL-2.0-or-later.
#include "../src/HaikuAudioStream.h"
#include <Application.h>
#include <ByteOrder.h>
#include <fstream>
#include <iterator>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    bool cancel = std::strcmp(argv[2], "cancel") == 0;
    if (!cancel && std::strcmp(argv[2], "pause") != 0) return 2;
    BApplication app("application/x-vnd.NVDAHaiku.AudioStreamLiveTest");
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<char> pcm{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (pcm.size() < 44100 || pcm.size() % 2) return 3;
    media_raw_audio_format format = media_raw_audio_format::wildcard;
    format.frame_rate = 22050; format.channel_count = 1;
    format.format = media_raw_audio_format::B_AUDIO_SHORT;
    format.byte_order = B_MEDIA_HOST_ENDIAN;
    HaikuAudioStream audio(format, 8820);
    status_t status = audio.Open();
    if (status != B_OK) { printf("open=%ld\n", (long)status); return 4; }
    std::atomic<bool> finished{false}, fed{false};
    std::thread producer([&] { fed = audio.Feed(pcm.data(), pcm.size(), 201); finished = true; });
    // Ensure the producer is blocked on a substantially shorter ring than input.
    snooze(300000);
    bool ok = !finished.load();
    uint64_t oldGeneration = audio.Generation();
    bigtime_t requested = system_time();
    status = cancel ? audio.Cancel() : audio.Pause(true);
    bigtime_t elapsed = system_time() - requested;
    printf("%s returned: status=%ld elapsed_us=%lld\n", argv[2], (long)status,
        (long long)elapsed);
    ok = ok && status == B_OK;
    ok = ok && elapsed < 100000;
    bigtime_t deadline = system_time() + 600000;
    while (system_time() < deadline) {
        if (!audio.Poll().empty()) ok = false;
        if (!cancel && finished.load()) ok = false;
        snooze(1000);
    }
    if (cancel) {
        ok = ok && finished.load() && !fed.load() && audio.Generation() != oldGeneration;
        if (!finished.load()) audio.Close();
        producer.join();
        finished = fed = false;
        producer = std::thread([&] { fed = audio.Feed(pcm.data(), pcm.size(), 202); finished = true; });
    } else {
        status = audio.Pause(false);
        ok = ok && status == B_OK && audio.Generation() == oldGeneration;
    }
    std::vector<uint64_t> marks;
    deadline = system_time() + 10000000;
    while (marks.empty() && system_time() < deadline && audio.Error() == B_OK) {
        for (auto mark : audio.Poll()) marks.push_back(mark.id);
        snooze(1000);
    }
    snooze(100000);
    audio.Close(); producer.join();
    ok = ok && fed.load() && marks == std::vector<uint64_t>{cancel ? 202u : 201u};
    printf("mode=%s fed=%d markers=%zu passed=%d\n", argv[2], fed.load(), marks.size(), ok);
    return ok ? 0 : 1;
}
