"""Original NVDA content recognition on Haiku: the kit's screen reader program and the Tesseract engine.

python3 tests/ocr-native.py RUNTIME
Needs the packages tesseract and tesseract_data_eng, the desktop (a Tracker pose
view with the stock icons BeBook and Quick Tour) in front and with the focus.
The host must keep the screen saver away while it runs (a key every 20 s).
The original commands are run; the recognized text and the screen place of a
recognized word are compared with what the kit knows about the same icon.
"""
from pathlib import Path
import logging
import shutil
import sys
import time
root = Path(sys.argv[1]).resolve(); project = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(root / 'engine'), str(root / 'engine/deps')]
from haikuRuntime import Runtime
from haikuKeyboardPlatform import input_server_team
from haikuInstanceLock import acquire

NL = chr(10)
with acquire(Path.home() / 'config/settings/nvda-haiku/original-runtime.lock'):
    profile = Path('/tmp/nvda-ocr-profile')
    shutil.rmtree(profile, ignore_errors=True); profile.mkdir()
    (profile / 'nvda.ini').write_text('[general]' + NL + 'language = en' + NL + '[speech]' + NL + 'synth = espeak' + NL
                                      + '[braille]' + NL + 'display = noBraille' + NL
                                      + '[screenCurtain]' + NL + 'enabled = False' + NL + 'warnOnLoad = False' + NL)
    with Runtime(profile, sender=input_server_team(), stickyFlags=0) as runtime:
        import api
        import config
        import ocrPlatform
        import screenCurtain
        import sessionPlatform
        import speech
        import speech.extensions
        import textInfos
        import globalCommands
        import winVersion
        from contentRecog import recogUi, uwpOcr
        from logHandler import log
        from NVDAObjects.haiku import HaikuPoseListObject
        errors = []

        class Errors(logging.Handler):
            def emit(self, record):
                if record.levelno >= logging.ERROR:
                    errors.append(logging.Formatter().format(record))
        handler = Errors(); log.addHandler(handler)
        spoken = []

        def onSpeech(speechSequence=None, **kwargs):
            spoken.extend(item for item in speechSequence or () if isinstance(item, str))
        speech.extensions.pre_speech.register(onSpeech)
        commands = globalCommands.commands

        def settle(seconds):
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                runtime.step()

        def run(name, seconds=3):
            del spoken[:]
            getattr(commands, 'script_' + name)(None)
            settle(seconds)
            said = ' | '.join(spoken)
            print('COMMAND %-28s %s' % (name, said[:200]), flush=True)
            return said
        try:
            deadline = time.monotonic() + 60
            while sessionPlatform.haikuSessionState() is not sessionPlatform.SessionState.UNLOCKED:
                runtime.step(); assert time.monotonic() < deadline, 'Session observation did not reconcile'
            settle(3)
            print('ENGINE', ocrPlatform.engine(), 'languages', ocrPlatform.installedLanguages(), flush=True)
            assert winVersion.isUwpOcrAvailable(), 'Tesseract with a language must be installed for this test'
            assert uwpOcr.UwpOcr().getResizeFactor(1280, 800) == 3 and uwpOcr.UwpOcr().getResizeFactor(40, 40) == 4
            languages = uwpOcr.getLanguages()
            print('LANGUAGES', languages, 'initial', uwpOcr.getConfigLanguage(), flush=True)
            assert 'en' in languages
            first = run('cycleOcrLanguage')
            assert first.strip(), 'The language was not announced'
            config.conf['uwpOcr']['language'] = 'en'
            focus = api.getFocusObject()
            print('FOCUS', type(focus).__name__, tuple(focus.location), flush=True)
            assert isinstance(focus, HaikuPoseListObject), 'The desktop (a pose view) must have the focus'
            api.setNavigatorObject(focus)
            # What the kit knows about one icon, to compare the recognized place with.
            expected = None
            for index in range(focus.childCount):
                item = focus._itemObject(index)
                if item.name == 'BeBook':
                    expected = item.location
            assert expected, 'The desktop has no BeBook icon with a frame'
            # Under the screen curtain the original refuses (there is no picture to read).
            screenCurtain.screenCurtain.enable()
            said = run('recognizeWithUwpOcr', 4)
            screenCurtain.screenCurtain.disable(persist=False)
            assert 'Screen Curtain' in said and not isinstance(api.getFocusObject(), recogUi.RecogResultNVDAObject), said
            settle(2)
            started = time.monotonic()
            said = run('recognizeWithUwpOcr', 1)
            assert 'Recognizing' in said, said
            del spoken[:]
            deadline = time.monotonic() + 420
            while not isinstance(api.getFocusObject(), recogUi.RecogResultNVDAObject):
                runtime.step()
                assert time.monotonic() < deadline, 'No recognition result: ' + ' | '.join(spoken)
                assert not any('failed' in text for text in spoken), spoken
            result = api.getFocusObject()
            settle(2)
            text = result.makeTextInfo(textInfos.POSITION_ALL).text
            print('RECOGNIZED in %.0f s, %d characters, spoken: %s' % (time.monotonic() - started, len(text),
                                                                      ' | '.join(spoken)[:160]), flush=True)
            for line in text.splitlines()[:12]:
                print('  TEXT', line[:150], flush=True)
            assert 'BeBook' in text and 'Quick' in text, 'The icon labels were not recognized'
            # The screen place of the recognized word, converted back by the original code.
            offset = text.index('BeBook')
            info = result.makeTextInfo(textInfos.POSITION_FIRST)
            info.move(textInfos.UNIT_CHARACTER, offset)
            info.expand(textInfos.UNIT_WORD)
            point = info.pointAtStart
            print('WORD', repr(info.text), 'recognized at', (point.x, point.y), 'kit frame', tuple(expected), flush=True)
            left, top, width, height = expected
            assert left - 12 <= point.x <= left + width and top - 12 <= point.y <= top + height, (point, expected)
            # The result is read with the cursor keys of the original cursor manager.
            del spoken[:]
            result.script_moveByLine_forward(None)
            settle(2)
            print('NEXT LINE', ' | '.join(spoken)[:160], flush=True)
            assert spoken, 'Moving by line in the result said nothing'
            said = run('recognizeWithUwpOcr', 3)
            assert 'Already in a content recognition result' in said, said
            result.script_exit(None)
            settle(3)
            after = api.getFocusObject()
            print('AFTER EXIT', type(after).__name__, flush=True)
            assert isinstance(after, HaikuPoseListObject), 'Escape must return to the desktop'
            said = run('toggleAutomaticRefreshOfRecogResult')
            assert 'enabled' in said, said
            run('toggleAutomaticRefreshOfRecogResult')
            assert not errors, errors
            print('PASS original content recognition: languages cycle, refused under the curtain, the desktop recognized '
                  'with the icon labels, the word place matches the kit frame, result read by line, escape returns',
                  flush=True)
        finally:
            speech.extensions.pre_speech.unregister(onSpeech)
            log.removeHandler(handler)
            print('ERRORS', len(errors), flush=True)
            for text in errors[-3:]:
                print('  E:', text.replace(NL, ' | ')[:1800], flush=True)
