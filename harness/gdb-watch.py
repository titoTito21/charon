"""Minimal GDB remote client: write watchpoint on ADDR (8 bytes) in the running
QEMU (-gdb tcp:127.0.0.1:1234); prints RIP and the stack top when it fires.
  gdb-watch.py ADDR [TIMEOUT]"""
import socket,sys,time
addr=int(sys.argv[1],16);timeout=float(sys.argv[2]) if len(sys.argv)>2 else 120
s=socket.create_connection(('127.0.0.1',1234));s.settimeout(timeout)
def send(cmd):
    pkt=f"${cmd}#{sum(cmd.encode())%256:02x}".encode();s.sendall(pkt)
def recv():
    data=b''
    while True:
        c=s.recv(1)
        if c==b'$':break
    while True:
        c=s.recv(1)
        if c==b'#':s.recv(2);break
        data+=c
    s.sendall(b'+');return data.decode()
s.sendall(b'+')
send('?');print('stop:',recv())
send(f'Z2,{addr:x},8');print('Z2:',recv())
for n in range(int(sys.argv[3]) if len(sys.argv)>3 else 6):
    send('c');t0=time.time()
    try:print('hit:',recv(),round(time.time()-t0,1),'s')
    except socket.timeout:print('no hit');break
    send('g');regs=recv()
    def reg(i):b=bytes.fromhex(regs[i*16:i*16+16]);return int.from_bytes(b,'little')
    send(f'm{addr:x},8');val=recv()
    send(f'm{reg(7):x},40');st=recv()
    print(' RIP',hex(reg(16)),'RSP',hex(reg(7)),'value',val,'stack',st)
send(f'z2,{addr:x},8');recv();send('D')
