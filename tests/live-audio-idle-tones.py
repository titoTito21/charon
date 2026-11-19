"""Record through QEMU to check audible output across native idle/restart.

Expected: three 200 ms tones (440, 880, 1320 Hz), separated by silence.
The C ABI marker test alone cannot detect replay in a downstream mixer.
"""
import array
import ctypes as c
import math
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'engine'))
from haikuAudioPlatform import loadLibrary

lib = loadLibrary(sys.argv[1])


def ok(name, *args):
    status = getattr(lib, 'nvda_audio_' + name)(*args)
    assert status == 0, (name, status)


handle = c.c_uint64()
ok('create', 22050, 1, 2, 1000, c.byref(handle))
try:
    time.sleep(.4)
    for frequency in (440, 880, 1320):
        pcm = array.array('h', (round(10000 * math.sin(2 * math.pi * frequency * i / 22050))
                               for i in range(4410))).tobytes()
        ok('feed', handle, c.c_char_p(pcm), len(pcm), 0)
        ok('sync' if '--sync-only' in sys.argv else 'idle', handle)
        time.sleep(.6)
    mode = 'sync-only' if '--sync-only' in sys.argv else 'idle/restart'
    print(f'PASS three tone feeds with {mode}; inspect QEMU recording', flush=True)
finally:
    ok('close', handle)
