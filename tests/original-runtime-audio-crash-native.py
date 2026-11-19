"""Abrupt full Runtime exit after eSpeak PCM; observe actual audio-owner exits."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import time

if sys.argv[1] == 'owner':
    # Retain only host-side pipe ends. This monitor never owns a client writer.
    report = Path(os.environ['NVDA_CRASH_REPORT'])
    child = subprocess.Popen([os.environ['NVDA_REAL_AUDIO_HOST'], *sys.argv[2:]],
        pass_fds=tuple(map(int, sys.argv[2:])))
    record = report / f'owner-{child.pid}.json'
    record.write_text(json.dumps({'pid': child.pid, 'status': None}))
    status = child.wait()
    temporary = record.with_suffix('.tmp')
    temporary.write_text(json.dumps({'pid': child.pid, 'status': status}))
    temporary.replace(record)
    sys.exit(status)

if sys.argv[1] == 'client':
    root, profile, wrapper = map(Path, sys.argv[2:5])
    sys.path[:0] = [str(root/'engine'), str(root/'engine/deps')]
    from haikuAudioTransport import AudioTransport
    import haikuAudioHostBackend
    def transport(*args, **kwargs):
        host = AudioTransport(wrapper)
        request = host.request
        def dieAfterPcm(operation, *positional, **options):
            result = request(operation, *positional, **options)
            if operation == 15 and options.get('pcm') and result[0] == 0:
                print('RUNTIME_ESPEAK_PCM_ACCEPTED', flush=True)
                os._exit(23)
            return result
        host.request = dieAfterPcm
        return host
    haikuAudioHostBackend.AudioTransport = transport
    profile.mkdir()
    (profile/'nvda.ini').write_text('[general]\nlanguage = pl\n[speech]\nsynth = espeak\n[braille]\ndisplay = noBraille\n')
    from haikuRuntime import Runtime
    with Runtime(profile, sender=135, stickyFlags=0, port='nvda-audio-crash-test') as runtime:
        import synthDriverHandler, speech
        assert synthDriverHandler.getSynth().name == 'espeak'
        speech.speakMessage('Test oryginalnego NVDA na Haiku. ' * 30)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            runtime.step()
        raise AssertionError('No accepted synthesized PCM')
    raise AssertionError('Abrupt exit did not occur')

root = Path(sys.argv[1]).resolve()
count = int(sys.argv[2])
for iteration in range(count):
    with tempfile.TemporaryDirectory(prefix='nvda-runtime-audio-crash-') as directory:
        directory = Path(directory)
        wrapper = directory/'audio-owner-monitor'
        wrapper.write_text(f'#!{sys.executable}\nimport runpy, sys\nsys.argv = [{str(Path(__file__).resolve())!r}, "owner", *sys.argv[1:]]\nrunpy.run_path(sys.argv[0], run_name="__main__")\n')
        wrapper.chmod(0o755)
        env = dict(os.environ, NVDA_CRASH_REPORT=str(directory),
            NVDA_REAL_AUDIO_HOST=str(root/'bin/NVDAAudioHost'))
        child = subprocess.run([sys.executable, __file__, 'client', str(root),
            str(directory/'profile'), str(wrapper)], env=env, capture_output=True,
            text=True, timeout=35)
        assert child.returncode == 23 and 'RUNTIME_ESPEAK_PCM_ACCEPTED' in child.stdout, (iteration, child.returncode, child.stdout, child.stderr)
        deadline = time.monotonic() + 10
        while True:
            records = [json.loads(p.read_text()) for p in directory.glob('owner-*.json')]
            if records and all(r['status'] is not None for r in records): break
            assert time.monotonic() < deadline, (iteration, records)
            time.sleep(.05)
        assert all(r['status'] == 0 for r in records), records
        print(f'PASS abrupt Runtime {iteration+1}/{count}: synthesized PCM accepted; {len(records)} audio owners exit normally', flush=True)
