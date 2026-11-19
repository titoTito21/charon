"""Real audio completions retained across one injected failed poll response."""
from pathlib import Path
import ctypes as C
import sys
root=Path(sys.argv[1]).resolve();sys.path[:0]=[str(root/'engine'),str(root/'engine/deps')]
from haikuAudioHostBackend import HostAudioLibrary
from haikuAudioPlatform import Completion
backend=HostAudioLibrary(root/'bin/NVDAAudioHost')
handle=C.c_uint64();events=(Completion*4096)();count=C.c_size_t()
pcm=C.create_string_buffer(b'\x01\x00'*2205)
try:
    assert backend.nvda_audio_create(22050,1,2,200,C.byref(handle))==0
    host=backend._host
    def retain(marker):
        assert backend.nvda_audio_feed_pumping(handle,pcm,4410,marker,lambda _:None,None)==0
        assert backend.nvda_audio_sync_pumping(handle,lambda _:None,None)==0
        # Exercise the same retention rule used before feed registers onDone.
        backend._pending=marker
        assert backend.nvda_audio_poll(handle,events,len(events),C.byref(count))==0
        assert count.value==0 and [item[1] for item in backend._held]==[marker]
        backend._pending=None
    retain(41)
    original=backend._request
    def failed(operation,**kwargs):
        if operation==7:return -2147483639,0,()
        return original(operation,**kwargs)
    backend._request=failed
    count.value=97;events[0].id=98
    assert backend.nvda_audio_poll(handle,events,len(events),C.byref(count))==-2147483639
    assert count.value==97 and events[0].id==98
    backend._request=original
    assert backend.nvda_audio_poll(handle,events,len(events),C.byref(count))==0
    assert count.value==1 and events[0].id==41
    assert backend.nvda_audio_poll(handle,events,len(events),C.byref(count))==0 and count.value==0
    print('PASS real completion survives failed poll and is delivered exactly once',flush=True)
    retain(42)
    assert backend.nvda_audio_cancel(handle)==0
    assert backend.nvda_audio_poll(handle,events,len(events),C.byref(count))==0 and count.value==0
    print('PASS cancellation still discards retained old-generation completion',flush=True)
finally:
    backend._close()
assert host.process.poll()==0
print('PASS native audio owner exited normally',flush=True)
