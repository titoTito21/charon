"""Original synthesizer selection (modal sub-dialog) and voice panel refresh through the native presenter.

python3 tests/settings-synth-modal-native.py RUNTIME
"""
from pathlib import Path
import json
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
    profile = Path('/tmp/nvda-settings-modal-profile')
    shutil.rmtree(profile, ignore_errors=True); profile.mkdir()
    (profile / 'nvda.ini').write_text('[general]' + NL + 'language = en' + NL + '[speech]' + NL + 'synth = espeak' + NL
                                      + '[braille]' + NL + 'display = noBraille' + NL)
    protocolLog = Path('/tmp/nvda-settings-modal-protocol.log')
    for suffix in ('', '.modal'):
        Path(str(protocolLog) + suffix).unlink(missing_ok=True)
    scenario = Path('/tmp/nvda-settings-modal-scenario.json')
    scenario.write_text(json.dumps([
        {'after': r'^SHOW 0$', 'emit': 'CATEGORY 1'},
        {'after': r'^SHOW 1$', 'emit': 'BUTTON {token:Change...}'},
        # The modal runs now; the main window then receives the rebuilt Speech panel.
        {'after': r'^END 1$', 'emit': 'BUTTON ok'},
    ]))
    Path(str(scenario) + '.modal.json').write_text(json.dumps([
        {'after': r'^SHOW 0$', 'emit': 'CHANGED {token:Synthesizer:} {index:Synthesizer:=No speech}'},
        {'after': r'.', 'emit': 'BUTTON ok'},
    ]))
    helper = ['/boot/system/bin/python3.14', str(project / 'tests/settings-fake-helper.py'), str(protocolLog), str(scenario)]
    with Runtime(profile, sender=input_server_team(), stickyFlags=0, settingsHelper=helper) as runtime:
        import config
        import sessionPlatform
        import synthDriverHandler
        from logHandler import log
        import logging
        errors = []; warnings = []

        class Errors(logging.Handler):
            def emit(self, record):
                if record.levelno >= logging.ERROR:
                    errors.append(logging.Formatter().format(record))
                elif record.levelno >= 15:
                    warnings.append(logging.Formatter().format(record)[:400])
        handler = Errors(); log.addHandler(handler)
        try:
            deadline = time.monotonic() + 60
            while sessionPlatform.haikuSessionState() is not sessionPlatform.SessionState.UNLOCKED:
                runtime.step()
                assert time.monotonic() < deadline, 'Session observation did not reconcile'
            assert synthDriverHandler.getSynth().name == 'espeak'
            model = runtime.menuCommands.build()
            item = next(i for i, cb in model._bindings.items() if cb == runtime.menuCommands.onNVDASettingsCommand)
            model.dispatch(item)
            assert runtime.settingsDialog.active
            print('DIALOG_OPEN', runtime.settingsDialog.dialog.GetTitle(), flush=True)
            deadline = time.monotonic() + 300
            while runtime.settingsDialog.active:
                runtime.step()
                assert not runtime.errors, runtime.errors
                if time.monotonic() >= deadline:
                    tail = protocolLog.read_text(encoding='utf-8', errors='replace')[-2500:]
                    modal = Path(str(protocolLog) + '.modal')
                    tail += NL + '--- modal ---' + NL + (modal.read_text(encoding='utf-8', errors='replace')[-2500:] if modal.exists() else '(none)')
                    raise AssertionError('Dialog did not finish' + NL + tail)
            synth = synthDriverHandler.getSynth().name
            print('SYNTH_AFTER', synth, 'config', config.conf['speech']['synth'], flush=True)
            main = protocolLog.read_text(encoding='utf-8')
            modal = Path(str(protocolLog) + '.modal').read_text(encoding='utf-8')
            print('MAIN_PANEL1_PRESENTATIONS', main.count('>> PANEL 1'), 'MODAL_LINES', modal.count(NL), flush=True)
            for text in warnings[-8:]:
                print('  W:', text.replace(NL, ' | '), flush=True)
            assert synth == 'silence', synth
            assert config.conf['speech']['synth'] == 'silence'
            assert '>> SIMPLE' in modal and '<< CHANGED' in modal and '>> QUIT' in modal, modal[-800:]
            assert main.count('>> PANEL 1') >= 2, 'Speech panel must be re-presented after the synthesizer change'
            assert not errors, errors
            print('PASS original synthesizer selection modal: nested native window, setSynth through original onOk, voice panel rebuilt and re-presented, settings saved', flush=True)
        finally:
            log.removeHandler(handler)
            print('ERRORS', len(errors), flush=True)
            for text in errors[-5:]:
                print('  E:', text.replace(NL, ' | ')[:1500], flush=True)
            for text in warnings[-10:]:
                print('  W:', text.replace(NL, ' | ')[:400], flush=True)
