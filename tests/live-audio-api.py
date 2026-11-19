"""Exercise the actual Haiku C ABI from Python, without a fake nvwave module.

Run under an external timeout: python live-audio-api.py LIBRARY PCM_FILE.
PCM must be 22050 Hz mono signed16, longer than the 200 ms queue.
"""
import ctypes as c
from itertools import product
from pathlib import Path
import sys
import threading
import time


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'engine'))
from haikuAudioPlatform import Completion, loadLibrary

lib = loadLibrary(sys.argv[1])


def call(name, *args):
    return getattr(lib, "nvda_audio_" + name)(*args)


def ok(name, *args):
    status = call(name, *args)
    assert status == 0, (name, status)


def create(capacity=200):
    handle = c.c_uint64()
    ok("create", 22050, 1, 2, capacity, c.byref(handle))
    assert handle.value
    return handle.value


def generation(handle):
    value = c.c_uint64()
    ok("generation", handle, c.byref(value))
    return value.value


events = (Completion * 4096)()


def poll(handle):
    count = c.c_size_t()
    ok("poll", handle, events, len(events), c.byref(count))
    return [(events[i].generation, events[i].id) for i in range(count.value)]


pcm = Path(sys.argv[2]).read_bytes()
assert len(pcm) > 8820 and len(pcm) % 2 == 0
buffer = c.create_string_buffer(pcm)


def feed_thread(handle, marker):
    result = []
    entered = threading.Event()

    def run():
        entered.set()
        result.append(call("feed", handle, buffer, len(pcm), marker))

    thread = threading.Thread(target=run)
    thread.start()
    assert entered.wait(1)
    return thread, result


invalid = c.c_uint64(123)
assert call("create", 0, 1, 2, 200, c.byref(invalid)) != 0 and invalid.value == 0
assert call("create", 22050, 1, 99, 200, c.byref(invalid)) != 0
assert call("close", 0) != 0

# A paused, full ring deterministically blocks Feed. Close must release it and
# retain native ownership until the admitted call has returned.
handle = create()
ok("pause", handle, 1)
thread, result = feed_thread(handle, 301)
time.sleep(0.1)
assert thread.is_alive(), "Feed did not block in paused queue"
ok("close", handle)
thread.join(2)
assert not thread.is_alive() and len(result) == 1 and result[0] != 0, result
assert call("feed", handle, None, 0, 302) != 0
assert call("close", handle) != 0
print("PASS paused blocked Feed / Close / invalidated handle", flush=True)

handle = create()
try:
    for level in (-0.1, 1.1, float("inf"), float("nan")):
        assert call("set_channel_volume", handle, 0, level) != 0
    assert call("set_channel_volume", handle, 1, 1.0) != 0  # Mono has no channel1.
    ok("set_channel_volume", handle, 0, 0.5)
    assert call("feed", handle, None, 2, 0) != 0
    assert call("feed", handle, buffer, 1, 0) != 0
    ok("sync", handle)  # Rejected PCM must not create a playback target.
    epoch = generation(handle)
    ok("pause", handle, 1)
    thread, result = feed_thread(handle, 401)
    time.sleep(0.1)
    assert thread.is_alive()
    ok("cancel", handle)
    thread.join(2)
    assert not thread.is_alive() and result[0] != 0, result
    assert generation(handle) > epoch
    assert poll(handle) == []

    # A short output array must not consume the completion.
    ok("feed", handle, None, 0, 402)
    count = c.c_size_t(999)
    assert call("poll", handle, events, 1, c.byref(count)) != 0
    assert count.value == 0
    assert poll(handle) == [(generation(handle), 402)]
    assert poll(handle) == []

    thread, result = feed_thread(handle, 403)
    thread.join(12)
    assert not thread.is_alive() and result == [0], result
    ok("sync", handle)
    received = poll(handle)
    assert received == [(generation(handle), 403)], received
    assert poll(handle) == []
    print("PASS cancel generation / preserved completion / PCM playback at half gain", flush=True)
finally:
    ok("close", handle)

# The nvwave idle timer must release the running producer, but the next
# utterance must retain a usable stream and ordered completion markers.
handle = create(1000)
try:
    ok("idle", handle)  # No playback yet; also exercise repeated idle.
    ok("idle", handle)
    epoch = generation(handle)
    size = 22050 * 2 // 5
    for marker in range(801, 805):
        ok("feed", handle, buffer, size, marker)
        ok("idle", handle)
        assert generation(handle) == epoch
        assert poll(handle) == [(epoch, marker)]
        assert poll(handle) == []
    ok("pause", handle, 1)
    ok("feed", handle, buffer, size, 805)
    ok("idle", handle)  # A paused, unheard tail must never become completed.
    assert generation(handle) != epoch
    assert poll(handle) == []
    ok("feed", handle, buffer, size, 806)
    ok("idle", handle)
    assert poll(handle) == [(generation(handle), 806)]
    print("PASS idle/restart / retained markers / paused tail discarded", flush=True)
finally:
    ok("close", handle)

handle = create()
try:
    ok("pause", handle, 1)
    thread, result = feed_thread(handle, 807)
    time.sleep(0.1)
    assert thread.is_alive()
    assert call("idle", handle) != 0, "Idle accepted a blocked producer"
    ok("cancel", handle)
    thread.join(2)
    assert not thread.is_alive() and result[0] != 0
    ok("idle", handle)
    print("PASS idle rejects concurrent blocked Feed", flush=True)
finally:
    ok("close", handle)

# Exercise active media callbacks and a blocked producer during destruction.
handle = create()
thread, result = feed_thread(handle, 501)
time.sleep(0.3)
assert thread.is_alive()
start = time.monotonic()
ok("close", handle)
elapsed = time.monotonic() - start
thread.join(2)
assert not thread.is_alive() and result[0] != 0, result
print(f"PASS active Close / blocked Feed, closeSeconds={elapsed:.6f}", flush=True)

for waitOperation, interrupt in product(("sync", "idle"), ("pause", "cancel", "close")):
    handle = create(1000)
    # Fit 800 ms into the queue so Feed completes before Sync begins.
    size = 22050 * 2 * 800 // 1000
    assert len(pcm) >= size
    ok("pause", handle, 1)
    ok("feed", handle, buffer, size, 601)
    start = time.monotonic()
    ok("sync", handle)  # Upstream sync does not wait while paused.
    assert time.monotonic() - start < 1
    assert poll(handle) == [], "Paused sync reported unheard PCM as played"
    ok("pause", handle, 0)
    result = []
    entered = threading.Event()

    def wait_for_playback():
        entered.set()
        result.append(call(waitOperation, handle))

    thread = threading.Thread(target=wait_for_playback)
    thread.start()
    assert entered.wait(1)
    time.sleep(0.05)
    assert thread.is_alive(), f"{waitOperation} returned before playback"
    if interrupt == "pause":
        ok("pause", handle, 1)
    else:
        ok(interrupt, handle)
    thread.join(2)
    assert not thread.is_alive() and len(result) == 1, result
    if interrupt == "pause":
        assert result == [0], result
        assert poll(handle) == []
        ok("pause", handle, 0)
        ok("sync", handle)
        if waitOperation == "sync":
            assert poll(handle) == [(generation(handle), 601)]
        else:
            assert poll(handle) == []  # Idle discards the paused tail.
    else:
        assert result[0] != 0, result
    if interrupt != "close":
        ok("close", handle)
    print(f"PASS {waitOperation} interrupted by {interrupt}", flush=True)

handle = create()
try:
    ok("pause", handle, 1)
    ok("start_trimming", handle, 1)
    quiet = c.create_string_buffer(44100)  # One second, larger than the queue.
    result = []
    thread = threading.Thread(target=lambda: result.append(
        call("feed", handle, quiet, 44100, 701)))
    thread.start()
    thread.join(2)
    assert not thread.is_alive() and result == [0], "Silent chunk was not reduced"
    assert poll(handle) == []  # The retained silent frame has not played yet.
    ok("pause", handle, 0)
    ok("sync", handle)
    assert poll(handle) == [(generation(handle), 701)]

    ok("pause", handle, 1)
    ok("start_trimming", handle, 0)
    result = []
    thread = threading.Thread(target=lambda: result.append(
        call("feed", handle, quiet, 44100, 702)))
    thread.start()
    time.sleep(0.1)
    assert thread.is_alive(), "Disabled trimming discarded silence"
    ok("cancel", handle)
    thread.join(2)
    assert not thread.is_alive() and result[0] != 0
    assert poll(handle) == []
    print("PASS original silence detector / retained frame / disable trimming", flush=True)
finally:
    ok("close", handle)
