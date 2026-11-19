"""Original NVDA audio ducking on Haiku, through the kit's mixer streams.

python3 tests/audio-ducking-native.py RUNTIME
Another program plays (bin/AudioStreamsTest hold, built by the kit's native
test; silence through its own sound player). The original
ducking state machine lowers its mixer gain and restores it; NVDA's own
streams are not touched; the original command cycles the modes; gains left by
an NVDA that died while ducked are restored from the record.
"""
from pathlib import Path
import json
import logging
import shutil
import subprocess
import sys
import time
root = Path(sys.argv[1]).resolve(); project = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(root / 'engine'), str(root / 'engine/deps')]
from haikuRuntime import Runtime
from haikuKeyboardPlatform import input_server_team
from haikuInstanceLock import acquire

NL = chr(10)
with acquire(Path.home() / 'config/settings/nvda-haiku/original-runtime.lock'):
    profile = Path('/tmp/nvda-ducking-profile')
    shutil.rmtree(profile, ignore_errors=True); profile.mkdir()
    (profile / 'nvda.ini').write_text('[general]' + NL + 'language = en' + NL + '[speech]' + NL + 'synth = espeak' + NL
                                      + '[braille]' + NL + 'display = noBraille' + NL
                                      + '[audio]' + NL + 'audioDuckingMode = 0' + NL)
    scenario = Path('/tmp/nvda-ducking-scenario.json'); scenario.write_text(json.dumps([]))
    helper = ['/boot/system/bin/python3.14', str(project / 'tests/settings-fake-helper.py'), '/tmp/nvda-ducking-protocol.log', str(scenario)]
    # The other program: started in its own session, so it is not one of NVDA's processes.
    import os
    environment = dict(os.environ)
    environment.pop('LD_PRELOAD', None)
    environment['LIBRARY_PATH'] = str(root / 'lib') + ':' + environment.get('LIBRARY_PATH', '')
    other = subprocess.Popen([str(project / 'bin/AudioStreamsTest'), 'hold', '900'], start_new_session=True, env=environment,
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        with Runtime(profile, sender=input_server_team(), stickyFlags=0, settingsHelper=helper) as runtime:
            import audioDucking
            import audioDuckingPlatform as platform
            import config
            import sessionPlatform
            from logHandler import log
            errors = []

            class Errors(logging.Handler):
                def emit(self, record):
                    if record.levelno >= logging.ERROR:
                        errors.append(logging.Formatter().format(record))
            handler = Errors(); log.addHandler(handler)
            spoken = []
            import speech
            import speech.extensions

            def onSpeech(speechSequence=None, **kwargs):
                spoken.extend(item for item in speechSequence or () if isinstance(item, str))
            speech.extensions.pre_speech.register(onSpeech)
            try:
                deadline = time.monotonic() + 60
                while sessionPlatform.haikuSessionState() is not sessionPlatform.SessionState.UNLOCKED:
                    runtime.step(); assert time.monotonic() < deadline, 'Session observation did not reconcile'
                # The reader usually runs in a locale with a decimal comma (Polish): the kit's records
                # must read the same (found in r235: they did not, and nothing was ducked or split).
                import locale
                try:
                    locale.setlocale(locale.LC_ALL, 'pl_PL.UTF-8')
                    print('LOCALE pl_PL, decimal point', repr(locale.localeconv()['decimal_point']), flush=True)
                except locale.Error:
                    print('LOCALE pl_PL not available', flush=True)
                assert audioDucking.isAudioDuckingSupported(), 'Ducking must be supported with the kit'

                def stream():
                    return next((s for s in platform.streams() if s['team'] == other.pid), None)
                deadline = time.monotonic() + 60
                while stream() is None:
                    runtime.step(); assert time.monotonic() < deadline and other.poll() is None, 'The other program does not play'
                before = stream()
                own = [s for s in platform.streams() if platform.isOwn(s['team'])]
                ownBefore = {(s['id'], s['team']): s['gains'] for s in own}
                print('OTHER', before['name'], before['gains'], '| NVDA streams', len(own), flush=True)
                assert not platform.isOwn(other.pid) and platform.hasBackgroundAudio()

                # Mode "duck when outputting": an AudioDucker lowers the other program while enabled.
                audioDucking.setAudioDuckingMode(audioDucking.AudioDuckingMode.OUTPUTTING)
                assert stream()['gains'] == before['gains'], 'Nothing is ducked before output'
                ducker = audioDucking.AudioDucker()
                assert ducker.enable() is True
                during = stream()
                print('DUCKED', during['gains'], flush=True)
                assert all(abs(d - (b - platform.DUCK_DB)) < 0.1 for d, b in zip(during['gains'], before['gains'])), during
                for s in platform.streams():
                    if (s['id'], s['team']) in ownBefore:
                        assert s['gains'] == ownBefore[(s['id'], s['team'])], 'NVDA must not duck itself'
                assert (profile / 'audioDucking-haiku.json').is_file(), 'The crash record is written while ducked'
                ducker.disable()   # the original unducks one second later, on NVDA's timer
                deadline = time.monotonic() + 20
                while stream() and stream()['gains'] != before['gains']:
                    runtime.step(); assert time.monotonic() < deadline, ('Not restored', stream())
                print('RESTORED', stream()['gains'], flush=True)
                assert not (profile / 'audioDucking-haiku.json').exists()

                # Mode "always duck", then back to none.
                audioDucking.setAudioDuckingMode(audioDucking.AudioDuckingMode.ALWAYS)
                assert all(abs(d - (b - platform.DUCK_DB)) < 0.1 for d, b in zip(stream()['gains'], before['gains']))
                audioDucking.setAudioDuckingMode(audioDucking.AudioDuckingMode.NONE)
                deadline = time.monotonic() + 20
                while stream() and stream()['gains'] != before['gains']:
                    runtime.step(); assert time.monotonic() < deadline
                print('ALWAYS mode ducked and released', flush=True)

                # A gain the user changed while ducked is left alone.
                audioDucking.setAudioDuckingMode(audioDucking.AudioDuckingMode.ALWAYS)
                changed = tuple(gain - 3.0 for gain in stream()['gains'])
                assert platform._setGain(stream(), changed)
                audioDucking.setAudioDuckingMode(audioDucking.AudioDuckingMode.NONE)
                for _ in range(30):
                    runtime.step()
                assert all(abs(a - b) < 0.1 for a, b in zip(stream()['gains'], changed)), stream()
                assert platform._setGain(stream(), before['gains'])
                print('USER CHANGE kept', flush=True)

                # Recovery: a record left by an NVDA that died while ducked.
                lowered = tuple(gain - platform.DUCK_DB for gain in before['gains'])
                assert platform._setGain(stream(), lowered)
                (profile / 'audioDucking-haiku.json').write_text(json.dumps(
                    {'version': 1, 'streams': [{'name': before['name'], 'original': list(before['gains']), 'ducked': list(lowered)}]}))
                assert platform.recover() == 1
                assert stream()['gains'] == before['gains'] and not (profile / 'audioDucking-haiku.json').exists()
                print('RECOVERED from the record', flush=True)

                # The original command cycles the modes and speaks their names.
                import globalCommands
                from haikuQueuePlatform import pump
                spoken.clear()
                config.conf['audio']['audioDuckingMode'] = 0
                globalCommands.commands.script_cycleAudioDuckingMode(None)
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline and not spoken:
                    runtime.step()
                print('COMMAND', spoken, 'mode', config.conf['audio']['audioDuckingMode'], flush=True)
                assert config.conf['audio']['audioDuckingMode'] == 1 and any('Duck when outputting' in text for text in spoken), spoken
                audioDucking.setAudioDuckingMode(0)
                config.conf['audio']['audioDuckingMode'] = 0
                for _ in range(20):
                    runtime.step()
                assert stream() is None or stream()['gains'] == before['gains']
                assert not errors, errors
                print('PASS original audio ducking on the kit\'s mixer streams: ducked by %.0f dB while outputting and in '
                      'always mode, restored, NVDA\'s own streams untouched, a user change kept, recovery from the '
                      'record, original command speaks the mode' % platform.DUCK_DB, flush=True)
            finally:
                speech.extensions.pre_speech.unregister(onSpeech)
                log.removeHandler(handler)
                print('ERRORS', len(errors), flush=True)
                for text in errors[-3:]:
                    print('  E:', text.replace(NL, ' | ')[:1800], flush=True)
    finally:
        if other.poll() is None:
            other.terminate()
