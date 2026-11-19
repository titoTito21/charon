"""Real default audio owner cleanup with pending/cancelled/paused PCM."""
from pathlib import Path
import ctypes as C
import sys,time
root=Path(sys.argv[1]).resolve()
sys.path[:0]=[str(root/'engine'),str(root/'engine/deps')]
from haikuAudioHostBackend import HostAudioLibrary
count=int(sys.argv[2])
assert 1<=count<=512
pcm=C.create_string_buffer(b'\x01\x00'*4410)
for index in range(count):
    backend=HostAudioLibrary(root/'bin/NVDAAudioHost')
    handle=C.c_uint64()
    try:
        status=backend.nvda_audio_create(22050,1,2,200,C.byref(handle))
        assert status==0,(index,'create',status)
        host=backend._host
        status=backend.nvda_audio_feed_pumping(handle.value,pcm,8820,index+1,lambda _:None,None)
        assert status==0,(index,'feed',status)
        time.sleep(.02)
        if index%3==1:
            assert backend.nvda_audio_cancel(handle.value)==0,(index,'cancel')
        elif index%3==2:
            assert backend.nvda_audio_pause(handle.value,1)==0,(index,'pause')
        assert backend.nvda_audio_close(handle.value)==0,(index,'close')
        assert host.process.poll()==0,(index,'owner exit',host.process.poll())
    finally:backend._close()
    if (index+1)%8==0:print('PASS cycles',index+1,flush=True)
print('PASS pending/cancelled/paused PCM owners all exited normally',count,flush=True)
