// Real Media Kit buffer exhaustion, recovery, stop and terminal timeout.
// GPL-2.0-or-later.
#include "../src/HaikuAudioNode.h"
#include "../src/HaikuAudioStream.h"
#include <Application.h>
#include <Buffer.h>
#include <BufferGroup.h>
#include <ByteOrder.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

struct AudioNodeBackpressureTest {
    static HaikuAudioNode* Node(HaikuAudioStream& stream) { return stream.node; }
    static std::vector<BBuffer*> Hold(HaikuAudioNode* node) {
        std::vector<BBuffer*> held;
        while (auto* buffer = node->buffers->RequestBuffer(
                node->requested.u.raw_audio.buffer_size, 0)) held.push_back(buffer);
        if (held.size() != 16) throw std::runtime_error("Could not hold the complete buffer pool");
        return held;
    }
};
struct HeldBuffers {
    std::vector<BBuffer*> buffers;
    explicit HeldBuffers(HaikuAudioNode* node) : buffers(AudioNodeBackpressureTest::Hold(node)) {}
    void Release() { for (auto* b : buffers) b->Recycle(); buffers.clear(); }
    ~HeldBuffers() { Release(); }
};
static void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
struct Fixture {
    std::atomic<size_t> rendered{0}, submitted{0};
    std::atomic<bigtime_t> first{0};
    HaikuAudioNode* node;
    std::vector<BBuffer*> held;
    static void Render(void* cookie, void* data, size_t size) {
        // All-zero samples: this regression emits no audible test tone.
        memset(data, 0, size);
        ++static_cast<Fixture*>(cookie)->rendered;
    }
    static void Submitted(void* cookie, bigtime_t when, size_t) {
        auto* self = static_cast<Fixture*>(cookie);
        if (self->submitted.fetch_add(1) == 0) self->first = when;
    }
    Fixture() {
        auto format = media_raw_audio_format::wildcard;
        format.frame_rate = 22050; format.channel_count = 1;
        format.format = media_raw_audio_format::B_AUDIO_SHORT;
        format.byte_order = B_MEDIA_HOST_ENDIAN;
        node = new HaikuAudioNode(format, Render, Submitted, this);
        Require(node->Open() == B_OK, "Open failed");
        held = AudioNodeBackpressureTest::Hold(node);
    }
    void ReleaseBuffers() {
        for (auto* buffer : held) buffer->Recycle();
        held.clear();
    }
    ~Fixture() { ReleaseBuffers(); node->Close(); node->Release(); }
    void StartBlocked() {
        Require(node->Begin() == B_OK, "Begin failed");
        snooze(250000);
        Require(rendered == 0 && submitted == 0, "PCM consumed without a buffer");
        Require(node->Error() == B_OK, "Transient backpressure latched a fatal error");
    }
    void WaitForAudio() {
        bigtime_t deadline = system_time() + 2000000;
        while (submitted < 5 && node->Error() == B_OK && system_time() < deadline) snooze(1000);
        Require(node->Error() == B_OK && submitted >= 5, "Audio did not resume");
    }
};
int main() {
    BApplication app("application/x-vnd.NVDAHaiku.AudioBackpressureTest");
    try {
        {
            Fixture f; f.StartBlocked();
            const bigtime_t released = f.node->PlaybackTime();
            f.ReleaseBuffers(); f.WaitForAudio();
            Require(f.first >= released, "Resumed audio retained a stale presentation timestamp");
            Require(f.node->Halt() == B_OK, "Halt after recovery failed");
            Require(f.rendered == f.submitted, "Render/submission count mismatch");
            puts("PASS transient exhaustion: no PCM consumed, recovery, rebased timestamps, all renders submitted");
        }
        {
            Fixture f; f.StartBlocked();
            const bigtime_t before = system_time();
            Require(f.node->Halt() == B_OK, "Halt during backpressure failed");
            Require(system_time() - before < 500000, "Retry blocked stop handling");
            f.ReleaseBuffers(); snooze(100000);
            Require(f.rendered == 0, "Retry survived acknowledged stop");
            Require(f.node->Begin() == B_OK, "Restart after stopped retry failed");
            f.WaitForAudio();
            puts("PASS stop during exhaustion: responsive, no stale retry, restart succeeds");
        }
        {
            Fixture f; f.StartBlocked();
            const bigtime_t deadline = system_time() + 2000000;
            while (f.node->Error() == B_OK && system_time() < deadline) snooze(1000);
            Require(f.node->Error() == B_TIMED_OUT || f.node->Error() == B_WOULD_BLOCK,
                "Permanent exhaustion did not report a bounded native error");
            f.ReleaseBuffers(); snooze(100000);
            Require(f.rendered == 0 && f.submitted == 0, "Terminal failure consumed PCM");
            Require(f.node->Halt() == B_OK, "Halt after terminal timeout failed");
            puts("PASS permanent exhaustion: bounded error remains visible, no PCM consumed");
        }
        for (bool cancel : {false, true}) {
            auto format = media_raw_audio_format::wildcard;
            format.frame_rate = 22050; format.channel_count = 1;
            format.format = media_raw_audio_format::B_AUDIO_SHORT;
            format.byte_order = B_MEDIA_HOST_ENDIAN;
            HaikuAudioStream stream(format, 8820);
            Require(stream.Open() == B_OK, "Stream open failed");
            HeldBuffers held(AudioNodeBackpressureTest::Node(stream));
            std::vector<char> pcm(4410, 0);
            Require(stream.Feed(pcm.data(), pcm.size(), 101), "Buffered feed failed");
            snooze(250000);
            Require(stream.Error() == B_OK && stream.Poll().empty(), "Starved PCM completed early");
            uint64_t generation = stream.Generation();
            if (cancel) {
                Require(stream.Cancel() == B_OK, "Cancel during starvation failed");
                Require(stream.Generation() != generation, "Cancel retained old generation");
                generation = stream.Generation();
                Require(stream.Feed(pcm.data(), pcm.size(), 102), "Replacement PCM rejected");
            }
            held.Release();
            Require(stream.Sync() == B_OK, "Stream sync after buffer recovery failed");
            auto done = stream.Poll();
            Require(done.size() == 1 && done[0].generation == generation
                && done[0].id == (cancel ? 102u : 101u), "Missing, duplicated or stale completion");
            Require(stream.Poll().empty(), "Completion delivered twice");
            Require(stream.Idle() == B_OK, "Idle after recovery failed");
            puts(cancel ? "PASS stream cancellation during starvation: only replacement marker completes"
                : "PASS stream starvation: queued PCM completes exactly once after recovery");
        }
    } catch (const std::exception& error) {
        fprintf(stderr, "FAIL %s\n", error.what()); return 1;
    }
    return 0;
}
