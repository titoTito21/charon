"""Send keys to a VMware VM over its built-in VNC server (RemoteDisplay.vnc.enabled, no password).
  vnc-keys.py PORT KEY [KEY ...]        KEY: a character, or ret/esc/tab/spc/down/up/f12, or wait:SECONDS
  vnc-keys.py PORT --repeat SECONDS KEY  press KEY every second for SECONDS (e.g. to answer "Press any key to boot from CD")
  vnc-keys.py PORT --shot FILE.png       save the screen
"""
import socket, struct, sys, time
KEYS = {'ret': 0xff0d, 'esc': 0xff1b, 'tab': 0xff09, 'spc': 0x20, 'down': 0xff54, 'up': 0xff52, 'left': 0xff51, 'right': 0xff53,
        **{'f%d' % n: 0xffbd + n for n in range(1, 13)}, 'bksp': 0xff08, 'win': 0xffeb, 'alt': 0xffe9, 'ctrl': 0xffe3, 'shift': 0xffe1}
def connect(port):
    s = socket.create_connection(('127.0.0.1', port), timeout=20)
    version = s.recv(12); s.sendall(b'RFB 003.008\n')
    count = s.recv(1)[0]; types = s.recv(count)
    if 1 not in types: raise SystemExit('VNC asks for a password')
    s.sendall(b'\x01'); result = struct.unpack('>I', recvall(s, 4))[0]
    if result: raise SystemExit('VNC refused')
    s.sendall(b'\x01')   # shared
    head = recvall(s, 24); width, height = struct.unpack('>HH', head[:4]); name = recvall(s, struct.unpack('>I', head[20:24])[0])
    return s, width, height
def recvall(s, n):
    data = b''
    while len(data) < n:
        part = s.recv(n - len(data))
        if not part: raise SystemExit('VNC closed')
        data += part
    return data
def key(s, code, down):
    s.sendall(struct.pack('>BBxxI', 4, 1 if down else 0, code))
SHIFTED = {'~': '`', '!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6', '&': '7', '*': '8', '(': '9', ')': '0', '_': '-', '+': '=',
           '{': '[', '}': ']', '|': chr(92), ':': ';', '"': "'", '<': ',', '>': '.', '?': '/'}
def type_text(s, text):
    for c in text:
        base = SHIFTED.get(c, c.lower() if c.isupper() else None)
        if base is not None:
            key(s, KEYS['shift'], True); key(s, ord(base), True); key(s, ord(base), False); key(s, KEYS['shift'], False)
        else:
            key(s, ord(c), True); key(s, ord(c), False)
        time.sleep(0.05)
def press(s, name):
    combo = name.split('+')
    codes = [KEYS.get(c, ord(c) if len(c) == 1 else None) for c in combo]
    for c in codes: key(s, c, True)
    for c in reversed(codes): key(s, c, False)
    time.sleep(0.08)
def shot(s, width, height, path):
    # 32-bit true colour, then one full update
    s.sendall(struct.pack('>BxxxBBBBHHHBBBxxx', 0, 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
    s.sendall(struct.pack('>BxH', 2, 1) + struct.pack('>i', 0))
    s.sendall(struct.pack('>BBHHHH', 3, 0, 0, 0, width, height))
    pixels = bytearray(width * height * 3)
    while True:
        kind = recvall(s, 1)[0]
        if kind != 0:   # skip other messages (bell = 2, cut text = 3)
            if kind == 3: recvall(s, 3); recvall(s, struct.unpack('>I', recvall(s, 4))[0])
            continue
        recvall(s, 1); rects = struct.unpack('>H', recvall(s, 2))[0]
        for _ in range(rects):
            x, y, w, h, enc = struct.unpack('>HHHHi', recvall(s, 12))
            if enc != 0: raise SystemExit('unexpected encoding %d' % enc)
            raw = recvall(s, w * h * 4)
            for row in range(h):
                for col in range(w):
                    o = (row * w + col) * 4; d = ((y + row) * width + x + col) * 3
                    pixels[d:d + 3] = bytes((raw[o + 2], raw[o + 1], raw[o]))
        break
    from PIL import Image
    Image.frombytes('RGB', (width, height), bytes(pixels)).save(path)
port = int(sys.argv[1]); s, width, height = connect(port)
args = sys.argv[2:]
if args[0] == '--shot': shot(s, width, height, args[1])
elif args[0] == '--type': type_text(s, args[1])
elif args[0] == '--combo':   # hold all the named keys together (ctrl alt f2)
    codes = [KEYS.get(a, ord(a[0])) for a in args[1:]]
    for c in codes: key(s, c, True); time.sleep(0.05)
    for c in reversed(codes): key(s, c, False); time.sleep(0.05)
elif args[0] == '--repeat':
    end = time.time() + float(args[1])
    while time.time() < end: press(s, args[2]); time.sleep(1)
else:
    for a in args:
        if a.startswith('wait:'): time.sleep(float(a[5:]))
        else: press(s, a)
s.close()
