"""Staged original synth manager; verifies No speech, not audio synthesis."""
import importlib.util
from pathlib import Path
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'engine'))
sys.path.insert(0, str(root / 'engine/deps'))
assert sys.platform.startswith('haiku')
import globalVars
globalVars.appDir = str(root / 'engine')


def load(name, path, subdirectories=None):
    spec = importlib.util.spec_from_file_location(name, path,
        submodule_search_locations=subdirectories)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


config = load('config', root / 'porting/config/__init__.py', [str(root / 'engine/config')])
import languageHandler
languageHandler.setLanguage('en')
synth = load('synthDriverHandler', root / 'porting/synthDriverHandler.py')
import speechDictHandler
import scriptHandler
from speech.commands import IndexCommand
assert languageHandler.getLanguage() == 'en'
assert __import__('builtins')._('No speech') == 'No speech'
assert 'haikuSpeechDictRuntime' not in sys.modules

with tempfile.TemporaryDirectory(prefix='nvda-synth-manager-') as directory:
    globalVars.appArgs.configPath = directory
    config.initConfigPath()
    config.initialize()
    speechDictHandler.initialize()
    events = []
    def changed(synth, audioOutputDevice, isFallback):
        events.append((synth.name, audioOutputDevice, isFallback))
    synth.synthChanged.register(changed)
    synth.initialize()
    try:
        assert synth.setSynth('silence')
        driver = synth.getSynth()
        from synthDrivers.silence import SynthDriver
        assert type(driver) is SynthDriver
        assert isinstance(driver, synth.SynthDriver)
        assert config.conf['speech']['synth'] == 'silence'
        assert driver.description == 'No speech'
        assert events == [('silence', config.conf['audio']['outputDevice'], False)]
        from NVDAState import WritePaths
        from speechDictHandler.types import DictionaryType
        from speechDictHandler import definitions
        expectedVoicePath = Path(directory) / 'speechDicts/voiceDicts.v1/silence/silence.dic'
        assert Path(definitions.getDictionaryDefinition(DictionaryType.VOICE).path) == expectedVoicePath
        assert Path(WritePaths.voiceDictsDir, 'espeak').is_dir()
        assert Path(WritePaths.voiceDictsBackupDir).is_dir()
        assert globalVars.settingsRing.currentSettingName is None
        assert globalVars.settingsRing.next() is None
        assert globalVars.settingsRing.increase() is None
        driver.speak(['Test', IndexCommand(37)])
        assert driver.lastIndex == 37
        driver.cancel()
        assert driver.lastIndex is None
        config.conf.save()
        assert (Path(directory) / 'nvda.ini').is_file()
        assert scriptHandler.getCurrentScript() is None
        assert not scriptHandler.isScriptWaiting()
        assert synth.setSynth(None)
        assert synth.getSynth() is None

        class SettingsFixture(synth.SynthDriver):
            """Parameter-only fixture; cannot synthesize audio."""
            name = 'ringTest'
            description = 'Ring integration fixture'
            supportedSettings = (synth.SynthDriver.RateSetting(), synth.SynthDriver.RateBoostSetting())
            rateBoost = False
            def _get_rate(self):
                return getattr(self, '_rate', 50)
            def _set_rate(self, value):
                self._rate = value
            def speak(self, speechSequence):
                raise NotImplementedError('This fixture does not synthesize speech')

        fixture = SettingsFixture()
        fixture.initSettings()
        ring = globalVars.settingsRing
        assert ring.currentSettingName == 'Rate'
        assert ring.increase() == '55' and fixture.rate == 55
        assert ring.increaseLarge() == '75'
        assert ring.last() == '100'
        assert ring.increase() == '100'
        assert ring.first() == '0'
        assert ring.decrease() == '0'
        assert ring.next() == 'Rate boost'
        assert ring.increase() == 'on' and fixture.rateBoost is True
        assert ring.decrease() == 'off' and fixture.rateBoost is False
        assert ring.next() == 'Rate'
        assert ring.previous() == 'Rate boost'
        assert config.conf['speech']['ringTest']['rate'] == 0
        assert config.conf['speech']['ringTest']['rateBoost'] is False
        fixture.terminate()
        globalVars.settingsRing = None
    finally:
        synth.setSynth(None)
        synth.synthChanged.unregister(changed)
        config.post_configProfileSwitch.unregister(synth.handlePostConfigProfileSwitch)
        speechDictHandler.terminate()
print('PASS: original synth manager, real No speech driver, empty settings ring, '
      'index handling, cancellation, configuration save, notification and termination; '
      'original numeric/boolean settings ring, limits, steps, cycling and config updates')
