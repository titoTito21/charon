"""Check the real pinned shared library's PCM/SSML ABI, independently of audio.

Usage: python3 tests/live-pinned-synth.py LIBESPEAK_PATH DATA_PARENT
This is a prerequisite test, not a substitute for NVDA driver/playback tests.
"""
import ctypes as c
import json
import sys


class EventId(c.Union):
    _fields_ = [("number", c.c_int), ("name", c.c_char_p), ("string", c.c_char * 8)]


class Event(c.Structure):
    _fields_ = [
        ("type", c.c_int), ("unique_identifier", c.c_uint),
        ("text_position", c.c_int), ("length", c.c_int),
        ("audio_position", c.c_int), ("sample", c.c_int),
        ("user_data", c.c_void_p), ("id", EventId),
    ]


Callback = c.CFUNCTYPE(c.c_int, c.POINTER(c.c_short), c.c_int, c.POINTER(Event))
lib = c.CDLL(sys.argv[1])
# This must also work when dlopened into Python, not just in the CLI executable.
# Native Haiku errno values are negative; upstream expects positive POSIX errno.
lib.GetFileLength.argtypes = (c.c_char_p,)
assert lib.GetFileLength(b"/nvda-pinned-synth-path-does-not-exist") < 0
assert lib.GetFileLength(sys.argv[2].encode()) < 0
lib.espeak_Initialize.argtypes = (c.c_int, c.c_int, c.c_char_p, c.c_int)
lib.espeak_SetSynthCallback.argtypes = (Callback,)
lib.espeak_SetVoiceByName.argtypes = (c.c_char_p,)
lib.espeak_Synth.argtypes = (
    c.c_char_p, c.c_size_t, c.c_uint, c.c_int, c.c_uint,
    c.c_uint, c.POINTER(c.c_uint), c.c_void_p,
)
lib.espeak_Info.restype = c.c_char_p
state = {}


@Callback
def collect(samples, count, events):
    # Do not raise across the C callback boundary; assertions occur after synthesis.
    for event in events:
        if event.type == 0:
            break
        if event.type == 3:
            state["marks"].append(event.id.name.decode("utf-8"))
    if samples:
        state["samples"] += count
        state["nonzero"] += sum(samples[i] != 0 for i in range(count))
    else:
        state["completed"] += 1
    return 0


sample_rate = lib.espeak_Initialize(2, 300, sys.argv[2].encode(), 0x8000)
assert sample_rate > 0, sample_rate
try:
    lib.espeak_SetSynthCallback(collect)
    assert lib.espeak_SetVoiceByName(b"pl") == 0
    results = []
    for rate in (175, 600):
        assert lib.espeak_SetParameter(1, rate, 0) == 0
        state = dict(samples=0, nonzero=0, marks=[], completed=0)
        text = 'Test <mark name="7"/> czytnika ekranu NVDA na Haiku.'.encode()
        identifier = c.c_uint()
        assert lib.espeak_Synth(text, 0, 0, 0, 0, 1 | 0x10 | 0x100,
                               c.byref(identifier), None) == 0
        assert state["samples"] > 0 and state["nonzero"] > 0, state
        assert state["marks"] == ["7"] and state["completed"] == 1, state
        results.append(dict(rate=rate, **state))
    assert results[1]["samples"] < results[0]["samples"], results
    print(json.dumps(dict(version=lib.espeak_Info(None).decode(),
                         sampleRate=sample_rate, results=results)))
finally:
    assert lib.espeak_Terminate() == 0
