"""serial-pipe.py NAME: the two-way serial console of a VMware test machine whose vmx has
  serial0.fileType = "pipe", serial0.fileName = "\\\\.\\pipe\\charon-NAME", serial0.pipe.endPoint = "server"
Everything the guest prints is appended to work/NAME/serial.txt; every line appended to work/NAME/serial.in
is sent to the guest (so a Linux getty on ttyS0 can be used when the keyboard is dead). Runs until the pipe
closes. Send:  echo 'ls /' >> work/NAME/serial.in     (serial-send.py NAME "line" ... does it with a pause)."""
import sys,time,threading
from pathlib import Path
base=Path(__file__).resolve().parent
name=sys.argv[1];d=base/name
# serial-pipe.py NAME PORT: the same over VMware's TCP serial port
#   serial0.fileType = "network", serial0.fileName = "tcp://127.0.0.1:PORT", serial0.network.endPoint = "server"
# (a Windows named pipe opened once cannot be read and written at the same time).
import socket
class Tcp:
    def __init__(self,port):self.s=socket.create_connection(('127.0.0.1',port))
    def read(self,n):return self.s.recv(n)
    def write(self,b):self.s.sendall(b)
pipe=None
for _ in range(120):
    try:pipe=Tcp(int(sys.argv[2]));break
    except OSError:time.sleep(1)
if not pipe:raise SystemExit('no serial port')
log=open(d/'serial.txt','ab',buffering=0)
inp=d/'serial.in';inp.write_bytes(b'')
def reader():
    while True:
        try:data=pipe.read(4096)
        except OSError:break
        if not data:break
        log.write(data)
threading.Thread(target=reader,daemon=True).start()
done=0
while True:
    time.sleep(0.3)
    try:data=inp.read_bytes()
    except OSError:continue
    if len(data)>done:
        chunk=data[done:];done=len(data)
        try:
            for i in range(0,len(chunk),16):pipe.write(chunk[i:i+16].replace(b'\n',b'\r'));time.sleep(0.02)
        except OSError:break
