"""Stream recovery after the audio owner process dies, through the real backend.

python3 tests/audio-reopen-native.py RUNTIME

Kills only owner processes started by this test. Checks: a feed after the
owner's death reopens the stream inside the same call, completions of the
dead owner are not delivered, a failed create is retried no sooner than
Stream.REOPEN_INTERVAL, the keep-awake silence owner respawns, and every owner
started here has exited afterwards.
"""
import ctypes as c
import errno
import os
import signal
import sys
import time
from pathlib import Path

root = Path(sys.argv[1]).resolve()
sys.path[:0] = [str(root / 'engine'), str(root / 'engine/deps')]
import haikuAudioPlatform as platform  # noqa: E402

owners = []
completions = []
stream = platform.Stream(22050, 1, 16, lambda s, marker: completions.append(marker))
owners.append(stream.library.ownerPid)
pcm = bytes(4410)  # 100 ms
first = stream.feed(c.c_char_p(pcm), len(pcm), True)
stream.sync()
assert first is not None and first[1][0] == 1 and first[0] in completions, (first, completions)
print('PASS first owner', owners[0], 'played and completed marker', first[0], flush=True)

os.kill(owners[0], signal.SIGKILL)
time.sleep(0.3)
started = time.monotonic()
second = stream.feed(c.c_char_p(pcm), len(pcm), True)
elapsed = time.monotonic() - started
owners.append(stream.library.ownerPid)
assert second is not None and second[1][0] == 2 and stream.reopens == 1, (second, stream.reopens)
assert owners[1] and owners[1] != owners[0], owners
assert stream.library.abandoned == 1
stream.sync()
assert second[0] in completions and completions.count(first[0]) == 1, completions
callbacks = {}
stream.registerCompletion(first, callbacks, lambda: None)
assert not callbacks, 'A token of the dead owner must not register a completion'
print(f'PASS feed after owner death reopened in {elapsed:.2f} s: new owner {owners[1]}, marker {second[0]} completed', flush=True)

# The owner cannot be created: the stream reports the failure, defers the next
# attempt for REOPEN_INTERVAL and recovers once creation works again.
os.kill(owners[1], signal.SIGKILL)
time.sleep(0.3)
stream.library.executable = '/nonexistent/NVDAAudioHost'
try:
    stream.feed(c.c_char_p(pcm), len(pcm), False)
    raise AssertionError('feed must fail while the owner cannot be created')
except OSError as error:
    print('  create failure reported:', type(error).__name__, str(error)[:80], flush=True)
try:
    stream.feed(c.c_char_p(pcm), len(pcm), False)
    raise AssertionError('feed must be deferred inside REOPEN_INTERVAL')
except OSError as error:
    assert error.errno == errno.EAGAIN, error
    print('  deferred:', error, flush=True)
stream.library.executable = None
stream._reopenNotBefore = 0.0
third = stream.feed(c.c_char_p(pcm), len(pcm), True)
owners.append(stream.library.ownerPid)
assert third is not None and third[1][0] == 3 and stream.reopens == 2, (third, stream.reopens)
stream.sync()
assert third[0] in completions
stream.cancel()
stream.pause(True); stream.pause(False)
print('PASS deferred reopen then recovery with owner', owners[2], flush=True)

platform.silenceInit(None)
silencePid = platform._silenceLibrary.ownerPid
owners.append(silencePid)
os.kill(silencePid, signal.SIGKILL)
time.sleep(0.3)
try:
    platform.silencePlayFor(1000, 0.0)
    raise AssertionError('lost silence owner must be reported once')
except OSError as error:
    print('  silence owner loss reported:', type(error).__name__, flush=True)
platform.silencePlayFor(1000, 0.0)
owners.append(platform._silenceLibrary.ownerPid)
assert owners[-1] != silencePid
print('PASS silence owner respawned', owners[-1], flush=True)

stream.destroy()
platform.silenceTerminate()
deadline = time.monotonic() + 10
remaining = set(owners)
while remaining and time.monotonic() < deadline:
    for pid in list(remaining):
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            remaining.discard(pid)
    time.sleep(0.1)
assert not remaining, f'owners still alive: {remaining}'
print('PASS all', len(owners), 'owners exited; audio stream reopen', flush=True)
