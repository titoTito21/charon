"""Host check of Stream reopen policy with a scripted native library.

Run from outputs/nvda-haiku: python3 tests/audio-reopen-host.py
"""
import ctypes as c
import errno
import sys
from pathlib import Path

sys.path[:0] = ['engine', 'engine/deps']
import haikuAudioPlatform as platform  # noqa: E402
from haikuAudioHostBackend import put  # noqa: E402

B_BAD_VALUE = -2147483643
B_NO_INIT = -2147483646


class Library:
    def __init__(self):
        self.handles = 0
        self.closed = []
        self.fed = []
        self.failFeeds = 0
        self.failCreate = False

    def nvda_audio_create(self, rate, channels, fmt, capacity, output):
        if self.failCreate:
            return B_NO_INIT
        self.handles += 1
        put(output, c.c_uint64, self.handles)
        return 0

    @staticmethod
    def value(handle):
        return handle.value if hasattr(handle, 'value') else handle

    def nvda_audio_generation(self, handle, output):
        handle = self.value(handle)
        if not handle:
            return B_BAD_VALUE
        put(output, c.c_uint64, 7)
        return 0

    def feedWithGeneration(self, handle, data, size, marker, pump, context, generation):
        handle = self.value(handle)
        if not handle:
            return B_BAD_VALUE
        if self.failFeeds:
            self.failFeeds -= 1
            raise BrokenPipeError('Audio owner disconnected')
        self.fed.append((handle, marker))
        return 0

    def nvda_audio_sync_pumping(self, handle, pump, context):
        return 0 if self.value(handle) else B_BAD_VALUE

    def nvda_audio_cancel(self, handle):
        return 0 if self.value(handle) else B_BAD_VALUE

    def nvda_audio_pause(self, handle, value):
        return 0 if self.value(handle) else B_BAD_VALUE

    def nvda_audio_close(self, handle):
        self.closed.append(handle)
        return 0


library = Library()
platform.loadLibrary = lambda path=None: library
stream = platform.Stream(22050, 1, 16, lambda s, marker: None)
pcm = c.c_char_p(bytes(4410))
token = stream.feed(pcm, 4410, True)
assert token == (1, (1, 7)) and library.fed == [(1, 1)], (token, library.fed)

library.failFeeds = 1
token = stream.feed(pcm, 4410, True)
assert token == (3, (2, 7)) and stream.reopens == 1 and library.closed == [1], (token, library.closed)
assert library.fed[-1] == (2, 3), library.fed
callbacks = {}
stream.registerCompletion((1, (1, 7)), callbacks, lambda: None)
assert not callbacks, 'dead owner token must not register'
stream.registerCompletion(token, callbacks, lambda: None)
assert list(callbacks) == [3]
print('PASS feed reopened once within the call after an owner loss')

library.failFeeds = 2
try:
    stream.feed(pcm, 4410, False)
    raise AssertionError('two consecutive losses must surface')
except BrokenPipeError:
    pass
assert stream.reopens == 2 and stream._failed
token = stream.feed(pcm, 4410, False)
assert token == (0, (4, 7)) and stream.reopens == 3, token
print('PASS repeated losses surface, the next feed recovers')

library.failCreate = True
library.failFeeds = 1
try:
    stream.feed(pcm, 4410, False)
    raise AssertionError('create failure must surface')
except OSError as error:
    assert error.errno == B_NO_INIT, error
created = library.handles
try:
    stream.feed(pcm, 4410, False)
    raise AssertionError('reopen must be deferred')
except OSError as error:
    assert error.errno == errno.EAGAIN and library.handles == created, error
library.failCreate = False
stream._reopenNotBefore = 0.0
token = stream.feed(pcm, 4410, False)
assert token == (0, (5, 7)) and library.handles == created + 1, token
print('PASS failed create is deferred for REOPEN_INTERVAL, then retried')

stream.destroy()
assert library.closed[-1] == 5 and stream.epoch is None
try:
    stream.feed(pcm, 4410, False)
    raise AssertionError('closed stream must not reopen')
except OSError as error:
    assert error.errno == B_BAD_VALUE and library.handles == created + 1, error
print('PASS closed stream does not reopen')
print('PASS audio reopen policy (host)')
