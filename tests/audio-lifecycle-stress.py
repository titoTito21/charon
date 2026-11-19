"""Native resource lifecycle probe; explicit close versus process shutdown."""
import ctypes as C
from pathlib import Path
import subprocess
import sys
import os
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'engine'))
from haikuAudioPlatform import loadLibrary
library = str(Path(sys.argv[1]).resolve())
mode = sys.argv[2]
count = int(sys.argv[3])
if mode in ('processes', 'exit-processes', 'abrupt-processes'):
    childMode = {'processes': 'close', 'exit-processes': 'exit', 'abrupt-processes': 'abrupt'}[mode]
    for i in range(count):
        result = subprocess.run([sys.executable, __file__, library, childMode, '1', *sys.argv[4:]],
                                capture_output=True, text=True, timeout=15)
        assert result.returncode == 0, (i, result.stdout, result.stderr)
        assert 'AUDIO_READY' in result.stdout, (i, result.stdout, result.stderr)
    print(f'PASS {count} independent audio processes, shutdown mode {childMode}', flush=True)
else:
    assert mode in ('close', 'exit', 'abrupt')
    lib = loadLibrary(library)
    for i in range(count):
        handle = C.c_uint64()
        status = lib.nvda_audio_silence_init()
        assert status == 0, (i, 'silence_init', status)
        try:
            status = lib.nvda_audio_create(22050, 1, 2, 200, C.byref(handle))
            assert status == 0, (i, 'create', status)
            if 'play' in sys.argv[4:]:
                pcm = (C.c_int16 * 2205)()
                assert lib.nvda_audio_feed(handle, pcm, C.sizeof(pcm), 1) == 0
                assert lib.nvda_audio_sync(handle) == 0
            print('AUDIO_READY', flush=True)
            if mode == 'abrupt': os._exit(0)
            if mode == 'exit': sys.exit(0)
        finally:
            if mode == 'close':
                if handle.value:
                    assert lib.nvda_audio_close(handle) == 0
                assert lib.nvda_audio_silence_terminate() == 0
    print(f'PASS {count} native audio/silence open-close cycles in one process', flush=True)
