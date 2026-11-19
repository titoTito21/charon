"""Closing between logical Feed frames must not resurrect an audio owner."""
from pathlib import Path
import ctypes as C
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'engine'))
from haikuAudioHostBackend import HostAudioLibrary, B_CANCELED

backend = HostAudioLibrary(sys.argv[1])
handle = C.c_uint64()
assert backend.nvda_audio_create(192000, 2, 2, 200, C.byref(handle)) == 0
host = backend._host
generation = C.c_uint64()
assert backend.nvda_audio_generation(handle.value, C.byref(generation)) == 0
request = backend._request
chunks = []
def closeBeforeSecondFrame(operation, **kwargs):
    if operation == 15:
        chunks.append(len(kwargs['pcm']))
        if len(chunks) == 2:
            assert backend.nvda_audio_close(handle.value) == 0
    return request(operation, **kwargs)
backend._request = closeBeforeSecondFrame
try:
    pcm = C.create_string_buffer(b'\x64\x00' * 1100000)
    status = backend.feedWithGeneration(handle.value, pcm, 2200000, 7,
        lambda context: None, None, generation.value)
    assert status == B_CANCELED and len(chunks) == 2, (status, chunks)
    assert backend._host is None and host.process.returncode == 0
    assert backend.nvda_audio_generation(handle.value, C.byref(generation)) == B_CANCELED
    assert backend._host is None
    backend.nvda_audio_close(handle.value)
    print('PASS close between PCM frames cancels remaining Feed, cleanly exits owner and cannot respawn it', flush=True)
finally:
    backend._close()
