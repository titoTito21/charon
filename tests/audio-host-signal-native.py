"""SIGTERM, SIGINT and SIGHUP end NVDAAudioHost like a client EOF.

python3 tests/audio-host-signal-native.py bin/NVDAAudioHost

The owner must leave its service loops (idle or inside a long feed), release
its Media Kit node through the normal Owner destructor and exit 0 with an
empty stderr. Only processes started here are touched.
"""
import os
import signal
import struct
import subprocess
import sys
import time

host = os.path.abspath(sys.argv[1])


def writeAll(fd, data):
    view = memoryview(data)
    while view:
        count = os.write(fd, view)
        assert count > 0
        view = view[count:]


def readAll(fd, count):
    result = b''
    while len(result) < count:
        part = os.read(fd, count - len(result))
        assert part, 'Owner closed before reply'
        result += part
    return result


def request(owner, operation, pcm=b'', reply=True):
    header = struct.pack('<8IQ', 1, operation, len(pcm), 22050, 1, 2, 200, 0, 0)
    writeAll(owner.stdin.fileno(), header + pcm)
    if reply:
        status, reserved, value = struct.unpack('<iIQ', readAll(owner.stdout.fileno(), 16))
        assert status == 0 and reserved == 0, (operation, status)
        return value


cases = (('idle', signal.SIGTERM), ('feeding', signal.SIGTERM), ('idle', signal.SIGINT), ('feeding', signal.SIGHUP))
for state, number in cases:
    owner = subprocess.Popen([host], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
    try:
        assert request(owner, 1) > 0, 'create failed'
        if state == 'feeding':
            # 1 MiB of 22050 Hz mono PCM16 is about 24 s of audio behind a 200 ms
            # queue: the owner is still pumping the feed when the signal arrives.
            request(owner, 2, bytes(1024 * 1024), reply=False)
            time.sleep(0.3)
        else:
            request(owner, 2, bytes(4410))
            request(owner, 3)
        started = time.monotonic()
        owner.send_signal(number)
        code = owner.wait(timeout=20)
        elapsed = time.monotonic() - started
        errors = owner.stderr.read()
        assert code == 0 and not errors, (state, number, code, errors)
        print(f'PASS {state} owner ended by signal {int(number)} with exit 0 in {elapsed:.2f} s', flush=True)
    finally:
        if owner.poll() is None:
            owner.kill()
        for stream in (owner.stdin, owner.stdout, owner.stderr):
            stream.close()
print('PASS audio owner signal handling', flush=True)
