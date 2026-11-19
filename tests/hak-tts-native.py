"""Native registered speech, original NVDA settings and audible PCM checks."""
from pathlib import Path
import json,sys,tempfile,threading,time
root=Path(sys.argv[1]).resolve()
sys.path[:0]=[str(root/'engine'),str(root/'engine/deps')]
import globalVars
globalVars.appDir=str(root/'engine')
import languageHandler
languageHandler.setLanguage('pl')
import config,speechDictHandler,synthDriverHandler as synth
from hakTtsRegistry import registrations,Worker
from speech.commands import IndexCommand,LangChangeCommand
records=registrations()
assert set(records)=={'espeak','tvox'},records.keys()
worker=Worker(records['tvox'])
count=0
try:
    for voice in ([] if '--settings-only' in sys.argv else records['tvox']['voices']):
        for lang in ('pl','en'):
            pcm=worker.render('Test.',voice=voice,lang=lang)
            assert len(pcm)>1000 and any(pcm),(voice,lang)
            count+=1
    for variant,timbres in ({} if '--settings-only' in sys.argv else records['tvox']['variants']).items():
        for timbre in timbres:
            pcm=worker.render('Test.',voice='Adam',lang='pl',variant=variant,timbre=timbre)
            assert len(pcm)>1000 and any(pcm),(variant,timbre)
            count+=1
            print('PCM',variant,timbre,len(pcm),flush=True)
    for lang,text in [('de','Guten Tag.'),('fr','Bonjour.')]:
        pcm=worker.render(text,lang=lang)
        assert len(pcm)>1000 and any(pcm),lang
        assert worker.lastBackend=='espeak'
        count+=1
    try:worker.render('Test.',lang='pl',timbre='speechplayer')
    except RuntimeError as error:assert 'unavailable' in str(error)
    else:raise AssertionError('Unavailable timbre silently fell back')
finally:worker.cancel()
print('PASS PCM',count,flush=True)
with tempfile.TemporaryDirectory(prefix='hak-tts-test-') as profile:
    globalVars.appArgs.configPath=profile
    config.initConfigPath();config.initialize();speechDictHandler.initialize();synth.initialize()
    names=dict(synth.getSynthList())
    assert set(names)=={'espeak','tvox','silence'},names
    assert names['tvox']=='TVox TTS for Haiku'
    assert synth.setSynth('tvox')
    driver=synth.getSynth()
    assert len(driver.availableVoices)==8
    assert {'pl','en','de','fr'}.issubset(driver.availableLanguages)
    done=threading.Event();indexes=[]
    def completed(synth):
        if synth is driver:done.set()
    def indexed(synth,index):
        if synth is driver:indexes.append(index)
    synth.synthDoneSpeaking.register(completed);synth.synthIndexReached.register(indexed)
    try:
        for variant in driver.availableVariants:
            driver.variant=variant
            assert driver.timbre in driver.availableTimbres
            done.clear()
            driver.speak(['Test głosu Adam w Haiku.',IndexCommand(42)])
            assert done.wait(90),'Speech timed out: '+variant
            assert indexes[-1:]==[42],indexes
            print('AUDIBLE',variant,driver.timbre,flush=True)
        from synthRegistryPlatform import settingsChanged
        refreshed=[]
        class Choice:
            def SetItems(self,items):self.items=items
            def SetSelection(self,index):self.selection=index
        class Container:
            timbreList=Choice()
            def updateDriverSettings(self,**kwargs):refreshed.append(kwargs)
        setting=next(s for s in driver.supportedSettings if s.id=='variant')
        settingsChanged(driver,setting,Container())
        assert refreshed==[{'changedSetting':'variant'}]
        requests=[]
        render=driver._worker.render
        def capture(text,**settings):
            requests.append(text)
            return render(text,**settings)
        driver._worker.render=capture
        done.clear();driver.speak(['Deskbar','okno',IndexCommand(44)])
        assert done.wait(60) and indexes[-1]==44
        assert requests==['Deskbar okno'],requests
        done.clear()
        driver.speak([LangChangeCommand('de'),'Guten Tag.',IndexCommand(43)])
        assert done.wait(60) and indexes[-1]==43
        driver.variant='klatt';driver.timbre='sam';driver.voice='Julia'
        driver.saveSettings();config.conf.save()
        assert config.conf['speech']['tvox']['timbre']=='sam'
        assert synth.setSynth('espeak')
        assert synth.setSynth('tvox')
        driver=synth.getSynth()
        assert (driver.voice,driver.variant,driver.timbre)==('Julia','klatt','sam')
        done.clear();driver.speak(['Krótki test.',IndexCommand(98)])
        assert done.wait(60)
        process=driver._worker.process.pid
        done.clear();driver.speak(['Długi test. '*200,IndexCommand(99)])
        time.sleep(.15);start=time.monotonic();driver.cancel()
        assert time.monotonic()-start<3
        done.clear();driver.speak(['Anulowano. Nowy komunikat.',IndexCommand(100)])
        assert done.wait(90) and indexes[-1]==100 and 99 not in indexes,indexes
        assert driver._worker.process.pid==process,'Cancellation reloaded voice models'
        print('PASS original settings, persistence, fallback, cancel/restart, indexes',flush=True)
    finally:
        synth.synthDoneSpeaking.unregister(completed);synth.synthIndexReached.unregister(indexed)
        synth.setSynth(None)
print('PASS HAK registered speech',flush=True)
