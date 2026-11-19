"""Real native Feed/Sync callback timing, ordering, and cancellation tests."""
import ctypes as c
from pathlib import Path
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'engine'))
from haikuAudioPlatform import Completion, PumpCallback, loadLibrary

lib = loadLibrary(sys.argv[1])
pcm = Path(sys.argv[2]).read_bytes()
assert len(pcm) > 44100
buffer = c.create_string_buffer(pcm)


def call(name, *args):
    return getattr(lib, 'nvda_audio_' + name)(*args)


def ok(name, *args):
    status = call(name, *args)
    assert status == 0, (name, status)


def create():
    handle = c.c_uint64()
    ok('create', 22050, 1, 2, 200, c.byref(handle))
    return handle.value


def poll(handle):
    events = (Completion * 4096)()
    count = c.c_size_t()
    ok('poll', handle, events, len(events), c.byref(count))
    return [(event.generation, event.id) for event in events[:count.value]]


def callback(operation):
    errors = []
    @PumpCallback
    def invoke(_):
        try:
            operation()
        except BaseException as error:
            errors.append(error)  # ctypes callbacks cannot propagate exceptions.
    return invoke, errors


handle = create()
try:
    received = []
    threads = set()
    owner = threading.get_ident()
    feedReturned = False
    premature = []
    def dispatch():
        threads.add(threading.get_ident())
        for epoch, marker in poll(handle):
            received.append((marker, time.monotonic()))
            if marker == 102 and not feedReturned:
                premature.append(marker)
    pump, errors = callback(dispatch)
    ok('feed', handle, buffer, 4410, 101)
    ok('feed_pumping', handle, buffer, len(pcm), 102, pump, None)
    returnedAt = time.monotonic()
    feedReturned = True
    assert received and received[0][0] == 101 and received[0][1] < returnedAt
    assert not premature, premature
    ok('sync_pumping', handle, pump, None)
    assert [marker for marker, _ in received] == [101, 102], received
    assert threads == {owner}, threads
    assert not errors, errors
    assert poll(handle) == []
    print('PASS prior marker during blocked Feed / new marker after return / Sync tail / caller thread')
finally:
    ok('close', handle)

handle = create()
try:
    received = []
    for marker in range(1, 4097):
        ok('feed', handle, None, 0, marker)
    pump, errors = callback(lambda: received.extend(poll(handle)))
    ok('feed_pumping', handle, None, 0, 4097, pump, None)
    assert [marker for _, marker in received] == list(range(1, 4097))
    ok('sync_pumping', handle, pump, None)
    assert [marker for _, marker in received] == list(range(1, 4098))
    assert not errors, errors
    print('PASS full marker queue drained by feeder callback without publishing current marker early')
finally:
    ok('close', handle)

for operation in ('feed_pumping', 'sync_pumping'):
    handle = create()
    try:
        invocations = []
        def cancel():
            if invocations:
                return
            invocations.append(threading.get_ident())
            # Never let a callback synchronously wait for its own operation.
            for name, args in (('close', (handle,)), ('sync', (handle,)),
                               ('idle', (handle,)), ('feed', (handle, None, 0, 999))):
                assert call(name, *args) != 0, name
            ok('cancel', handle)
        pump, errors = callback(cancel)
        if operation == 'feed_pumping':
            status = call(operation, handle, buffer, len(pcm), 201, pump, None)
        else:
            ok('feed', handle, buffer, 4410, 201)
            status = call(operation, handle, pump, None)
        assert status != 0
        assert invocations == [threading.get_ident()], invocations
        assert not errors, errors
        assert poll(handle) == []
        ok('feed', handle, None, 0, 202)
        assert [marker for _, marker in poll(handle)] == [202]
        print(f'PASS cancel from {operation} callback / stale marker removed / reentrant waits rejected')
    finally:
        ok('close', handle)
