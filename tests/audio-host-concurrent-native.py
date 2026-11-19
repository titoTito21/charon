"""Independent inherited control channel interrupts real blocked PCM feed."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import threading
import time

commandRead, commandWrite = os.pipe()
replyRead, replyWrite = os.pipe()
owner = subprocess.Popen([str(Path(sys.argv[1]).resolve()), str(commandRead), str(replyWrite)],
    pass_fds=(commandRead, replyWrite), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
    stderr=subprocess.PIPE, bufsize=0)
os.close(commandRead); os.close(replyWrite)
def writeAll(fd, data):
    data = memoryview(data)
    while data:
        size = os.write(fd, data); assert size > 0
        data = data[size:]
def readAll(fd, size):
    data = b''
    while len(data) < size:
        part = os.read(fd, size-len(data)); assert part, 'Unexpected owner EOF'
        data += part
    return data
def header(op, pcm=b'', flag=0, marker=0):
    return struct.pack('<8IQ', 1, op, len(pcm), flag, 0, 0, 0, 0, marker)+pcm
def response(fd):
    status, reserved, value = struct.unpack('<iIQ', readAll(fd, 16))
    assert reserved == 0
    return status, value
def control(op, flag=0):
    writeAll(commandWrite, header(op, flag=flag))
    status, value = response(replyRead)
    assert status == 0, (op, status)
    if op == 7:
        assert value <= 4096
        return list(struct.iter_unpack('<QQ', readAll(replyRead, value*16)))
    return value
worker = None
try:
    writeAll(owner.stdin.fileno(), struct.pack('<8IQ', 1, 1, 0, 22050, 1, 2, 200, 0, 0))
    assert response(owner.stdout.fileno())[0] == 0
    oldGeneration = control(8)
    control(6, 1)
    sent = threading.Event()
    result, errors = [], []
    def feed():
        try:
            writeAll(owner.stdin.fileno(), header(2, bytes(1024*1024), marker=31))
            sent.set()
            result.append(response(owner.stdout.fileno()))
        except BaseException as error: errors.append(error)
    worker = threading.Thread(target=feed)
    worker.start()
    assert sent.wait(5), errors
    time.sleep(.1)
    assert worker.is_alive(), (result, errors)
    assert control(7) == []
    start = time.monotonic()
    control(5)
    elapsed = time.monotonic()-start
    worker.join(5)
    assert not worker.is_alive() and not errors, errors
    # Native status_t B_CANCELED (Errors.h); POSIX ECANCELED is different here.
    assert result == [(-2147483636, 0)], result
    assert elapsed < 1, elapsed
    generation = control(8)
    assert generation > oldGeneration and control(7) == []
    control(6, 0)
    writeAll(owner.stdin.fileno(), header(2, bytes(4410), marker=32))
    assert response(owner.stdout.fileno())[0] == 0
    writeAll(owner.stdin.fileno(), header(3))
    assert response(owner.stdout.fileno())[0] == 0
    assert control(7) == [(generation, 32)]
    pcm = bytes(4410)
    writeAll(owner.stdin.fileno(), struct.pack('<8IQ', 1, 15, len(pcm),
             generation & 0xffffffff, generation >> 32, 0, 0, 0, 33)+pcm[:2204])
    control(5)
    writeAll(owner.stdin.fileno(), pcm[2204:])
    assert response(owner.stdout.fileno()) == (-2147483636, 0)
    assert control(7) == []
    print('PASS cancellation during partial PCM reception rejects stale generation at native admission', flush=True)
    # A partial control request must not prevent main EOF from joining its thread.
    writeAll(commandWrite, b'\x01')
    owner.stdin.close()
    assert owner.wait(timeout=5) == 0
    assert not owner.stderr.read()
    print(f'PASS independent control cancels blocked 1MiB feed in {elapsed:.4f}s; new generation completes once; partial control header cannot block shutdown', flush=True)
finally:
    if not owner.stdin.closed: owner.stdin.close()
    os.close(commandWrite); os.close(replyRead)
    if owner.poll() is None:
        try: owner.wait(timeout=5)
        except subprocess.TimeoutExpired:
            owner.kill(); owner.wait(timeout=5)
    if worker is not None: worker.join(5)
    owner.stdout.close(); owner.stderr.close()
