"""Python transport with real helper/native queues and concurrent cancellation."""
from pathlib import Path
import sys
import threading
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'engine'))
from haikuAudioTransport import AudioTransport

def check(transport, op, **kwargs):
    status, value, events = transport.request(op, **kwargs)
    assert status == 0, (op, status)
    return events if op == 7 else value

for ending in ('cancel', 'close'):
    transport = AudioTransport(sys.argv[1])
    worker = None
    try:
        check(transport, 1, rate=22050, channels=1, fmt=2, capacity=200)
        epoch = check(transport, 8)
        check(transport, 6, rate=1)
        entered = threading.Event()
        result, errors, pumps = [], [], []
        def pump():
            pumps.append(check(transport, 8))
            # Same-channel recursion must be rejected before transmitting data.
            try: transport.request(3)
            except RuntimeError: pass
            else: raise AssertionError('Reentrant blocking request accepted')
            entered.set()
        def feed():
            try: result.append(transport.request(2, bytes(1024*1024), marker=40, pump=pump))
            except BaseException as error: errors.append(error)
        worker = threading.Thread(target=feed)
        worker.start()
        assert entered.wait(5)
        time.sleep(.15)
        assert worker.is_alive(), (result, errors)
        if ending == 'cancel':
            check(transport, 5)
            worker.join(5)
            assert not worker.is_alive() and not errors, errors
            assert result == [(-2147483636, 0, ())], result
            assert pumps and check(transport, 8) > epoch
            assert check(transport, 7) == ()
            check(transport, 6, rate=0)
            generation = check(transport, 8)
            check(transport, 2, pcm=bytes(4410), marker=41)
            check(transport, 3)
            assert check(transport, 7) == ((generation, 41),)
        else:
            transport.close()
            worker.join(5)
            assert not worker.is_alive() and errors, (result, errors)
            assert isinstance(errors[0], (OSError, EOFError)), errors
        transport.close(); transport.close()
        assert transport.process.returncode == 0
        try: transport.request(8)
        except ConnectionAbortedError: pass
        else: raise AssertionError('Closed transport accepted a request')
        print(f'PASS Python audio transport {ending}: concurrent channel, callback pumping, recursion guard, clean owner exit', flush=True)
    finally:
        transport.close()
        if worker is not None: worker.join(5)

with AudioTransport(sys.argv[1]) as transport:
    check(transport, 1, rate=22050, channels=1, fmt=2, capacity=200)
    status, _, _ = transport.request(9, rate=999)
    assert status != 0
    assert check(transport, 8) > 0, 'Native request rejection poisoned the connection'
    try: transport.request(2, bytes(1024*1024+1))
    except ValueError: pass
    else: raise AssertionError('Oversized frame accepted')
    assert check(transport, 8) > 0
    check(transport, 6, rate=1)
    try: transport.request(2, bytes(1024*1024), timeout=.15)
    except TimeoutError: pass
    else: raise AssertionError('Blocked feed ignored its deadline')
    assert transport.process.returncode == 0, 'Timeout did not allow graceful owner cleanup'
print('PASS native rejection and invalid local frame preserve connection; deadline closes blocked feed and owner cleanly', flush=True)
