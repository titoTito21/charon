"""Exercise real audio semantics through the private owner protocol."""
import struct
import subprocess
import sys
from pathlib import Path

owner = subprocess.Popen([str(Path(sys.argv[1]).resolve())], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
def readAll(count):
    data = b''
    while len(data) < count:
        part = owner.stdout.read(count-len(data))
        assert part, 'Owner closed before reply'
        data += part
    return data
def request(op, pcm=b'', rate=0, channels=0, fmt=0, capacity=0, marker=0, success=True):
    data = memoryview(struct.pack('<8IQ', 1, op, len(pcm), rate, channels, fmt, capacity, 0, marker)+pcm)
    while data:
        size = owner.stdin.write(data)
        assert size
        data = data[size:]
    status, reserved, value = struct.unpack('<iIQ', readAll(16))
    assert reserved == 0
    assert (status == 0) == success, (op, status)
    if op == 7 and status == 0:
        assert value <= 4096
        return list(struct.iter_unpack('<QQ', readAll(value*16)))
    return value
def volumeBits(level):
    return struct.unpack('<I', struct.pack('<f', level))[0]
try:
    request(1, rate=22050, channels=1, fmt=2, capacity=200)
    epoch = request(8)
    request(9, rate=0, channels=volumeBits(.5))
    request(9, rate=1, channels=volumeBits(.5), success=False)
    request(9, rate=0, channels=volumeBits(float('nan')), success=False)
    request(10, rate=1); request(10, rate=0)
    request(6, rate=1)
    request(2, bytes(2205*2), marker=7)
    assert request(7) == [], 'Paused data completed before resume'
    request(6, rate=0); request(3)
    assert request(7) == [(epoch, 7)]
    assert request(7) == [], 'Completion duplicated'
    request(6, rate=1)
    request(2, bytes(2205*2), marker=8)
    request(5)
    newEpoch = request(8)
    assert newEpoch > epoch and request(7) == []
    request(6, rate=0)
    request(2, bytes(2205*2), marker=9); request(11)
    assert request(7) == [(newEpoch, 9)]
    request(13); request(12, rate=30, channels=volumeBits(0)); request(14)
    request(4); request(8, success=False)
    request(1, rate=48000, channels=2, fmt=2, capacity=200)
    request(4)
    owner.stdin.close()
    assert owner.wait(timeout=10) == 0
    assert not owner.stderr.read()
    print('PASS owner protocol: real pause/resume, single completion, cancellation generation, stale marker removal, idle, volume validation, trimming, keep-awake and reopen', flush=True)
finally:
    if not owner.stdin.closed: owner.stdin.close()
    if owner.poll() is None:
        try: owner.wait(timeout=10)
        except subprocess.TimeoutExpired:
            owner.kill(); owner.wait(timeout=5)
    owner.stdout.close(); owner.stderr.close()
