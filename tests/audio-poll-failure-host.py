"""Actual host backend must not consume held completions on failed IPC."""
from pathlib import Path
import ctypes as C
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'engine'))
from haikuAudioHostBackend import HostAudioLibrary
from haikuAudioPlatform import Completion

backend=HostAudioLibrary()
backend._held=[(7,10),(7,11)]
backend._pending=11
responses=iter([(-2147483639,0,()),(0,0,((7,12),)),(0,0,())])
backend._request=lambda operation:next(responses)
events=(Completion*4096)();events[0].generation=99;events[0].id=98
count=C.c_size_t(97)
assert backend.nvda_audio_poll(1,events,len(events),C.byref(count))==-2147483639
assert backend._held==[(7,10),(7,11)],'Failed poll consumed a retained completion'
assert (events[0].generation,events[0].id,count.value)==(99,98,97),'Failed poll wrote outputs'
assert backend.nvda_audio_poll(1,events,len(events),C.byref(count))==0
assert [(e.generation,e.id) for e in events[:count.value]]==[(7,10),(7,12)]
assert backend._held==[(7,11)]
backend._pending=None
assert backend.nvda_audio_poll(1,events,len(events),C.byref(count))==0
assert [(e.generation,e.id) for e in events[:count.value]]==[(7,11)]
assert backend._held==[]
print('PASS failed poll retains completions and leaves outputs untouched; later successful polls deliver each once')
