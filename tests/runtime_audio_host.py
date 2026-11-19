"""Explicit native integration-test selection, before Runtime initializes audio."""
import os

def select():
    mode = os.environ.get('NVDA_TEST_AUDIO_HOST')
    if mode not in ('1', 'observe'):
        return
    import haikuAudioPlatform
    import haikuAudioHostBackend
    from haikuAudioTransport import AudioTransport
    hosts = []
    def transport(*args, **kwargs):
        host = AudioTransport(*args, **kwargs)
        host.acceptedPcmBytes = 0
        request = host.request
        def trackedRequest(operation, *positional, **options):
            result = request(operation, *positional, **options)
            if operation == 15 and result[0] == 0:
                host.acceptedPcmBytes += len(options.get('pcm', b''))
            return result
        host.request = trackedRequest
        hosts.append(host)
        return host
    haikuAudioHostBackend.AudioTransport = transport
    if mode == '1':
        haikuAudioPlatform.loadLibrary = haikuAudioHostBackend.HostAudioLibrary
    return hosts

def verify(hosts):
    if hosts is None:
        return
    assert hosts, 'Runtime did not create any audio owner'
    pcmBytes = sum(h.acceptedPcmBytes for h in hosts)
    assert pcmBytes > 0, 'Runtime did not deliver synthesized PCM to audio owner'
    assert all(h.process.poll() == 0 for h in hosts), [h.process.poll() for h in hosts]
    print(f'PASS Runtime delivered {pcmBytes} PCM bytes through {len(hosts)} audio owners; all exited normally', flush=True)
