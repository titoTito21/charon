"""Crash only client processes; verify private audio owners cleanly terminate."""
import os
from pathlib import Path
import struct
import subprocess
import sys

def writeAll(fd, data):
    data = memoryview(data)
    while data:
        size = os.write(fd, data)
        assert size > 0
        data = data[size:]

def readAll(fd, count):
    result = b''
    while len(result) < count:
        part = os.read(fd, count-len(result))
        assert part, 'Host closed before reply'
        result += part
    return result

if sys.argv[1] == 'client':
    writer, reader = map(int, sys.argv[2:4])
    mode = sys.argv[4]
    def request(op, pcm=b'', reply=True):
        header = struct.pack('<8IQ', 1, op, len(pcm), 22050, 1, 2, 200, 0, 1)
        writeAll(writer, header+pcm)
        if reply:
            status, reserved, value = struct.unpack('<iIQ', readAll(reader, 16))
            assert status == 0 and reserved == 0, (op, status)
            return value
    assert request(1) > 0
    if mode == 'synced':
        request(2, bytes(4410)); request(3)
    else:
        assert mode == 'feeding'
        # Entire payload enters the pipe before client dies. Feeding it takes
        # much longer than the 200ms queue; the host must cancel on pipe HUP.
        request(2, bytes(1024*1024), reply=False)
    print('CLIENT_READY', flush=True)
    os._exit(0)

host = str(Path(sys.argv[1]).resolve())
count = int(sys.argv[2])
mode = sys.argv[3]
for i in range(count):
    owner = subprocess.Popen([host], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
    client = None
    try:
        writer, reader = owner.stdin.fileno(), owner.stdout.fileno()
        client = subprocess.Popen([sys.executable, __file__, 'client', str(writer), str(reader), mode],
            pass_fds=(writer, reader), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        owner.stdin.close(); owner.stdout.close()
        output, errors = client.communicate(timeout=15)
        assert client.returncode == 0 and b'CLIENT_READY' in output, (i, output, errors)
        owner.wait(timeout=10)
        errors = owner.stderr.read()
        assert owner.returncode == 0 and not errors, (i, owner.returncode, errors)
    finally:
        if client is not None and client.poll() is None:
            client.kill(); client.wait(timeout=5)
        if owner.poll() is None:
            # Cleanup after a failed probe only; forced host death can leak Media Kit resources.
            owner.kill(); owner.wait(timeout=5)
        owner.stderr.close()
print(f'PASS {count} abrupt clients ({mode}); every audio owner exits normally after EOF', flush=True)
