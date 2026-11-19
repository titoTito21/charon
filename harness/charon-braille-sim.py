"""r390: a Baum braille display for QEMU's usb-braille (FTDI serial) device.

  charon-braille-sim.py LOG [CELLS] [PORT]

Connects to the QEMU chardev socket (charon-vm.py with CHARON_BRAILLE=1:
127.0.0.1:4470), answers the identity request ("Baum Vario 40"), writes
every shown line to LOG as Unicode braille and as the text it reads back
(8-dot computer braille), and sends keys written to LOG + '.keys' (one per
line: 'top N' presses display key dN, 'route N' presses routing key N).
"""
import socket,sys,time,os
log=sys.argv[1];cells=int(sys.argv[2]) if len(sys.argv)>2 else 40;port=int(sys.argv[3]) if len(sys.argv)>3 else 4470
# Reverse of CharonBraille.h Cell(): dots -> character (lower case, dot 7 = capital).
letters='abcdefghijklmnopqrstuvwxyz'
L=[1,3,9,25,17,11,27,19,10,26,5,7,13,29,21,15,31,23,14,30,37,39,58,45,61,53]
D1,D2,D3,D4,D5,D6,D7,D8=1,2,4,8,16,32,64,128
ascii={0:' ',D2|D3|D4|D6:'!',D5:'"',D3|D4|D5|D6:'#',D1|D2|D4|D6:'$',D1|D4|D6:'%',D1|D2|D3|D4|D6:'&',D3:"'",D1|D2|D3|D5|D6:'(',D2|D3|D4|D5|D6:')',
 D1|D6:'*',D3|D4|D6:'+',D6:',',D3|D6:'-',D4|D6:'.',D3|D4:'/',D3|D5|D6:'0',D2:'1',D2|D3:'2',D2|D5:'3',D2|D5|D6:'4',D2|D6:'5',D2|D3|D5:'6',
 D2|D3|D5|D6:'7',D2|D3|D6:'8',D3|D5:'9',D1|D5|D6:':',D5|D6:';',D1|D2|D6:'<',D1|D2|D3|D4|D5|D6:'=',D3|D4|D5:'>',D1|D4|D5|D6:'?',D4|D7:'@',
 D2|D4|D6|D7:'[',D1|D2|D5|D6|D7:'\\',D1|D2|D4|D5|D6|D7:']',D4|D5|D7:'^',D4|D5|D6|D7:'_',D4:'`',D2|D4|D6:'{',D1|D2|D5|D6:'|',D1|D2|D4|D5|D6:'}',D4|D5:'~',
 D1|D6:'*',D2|D5:'─'}
pl={D1|D6:'ą',D1|D4|D6:'ć',D1|D5|D6:'ę',D1|D2|D6:'ł',D1|D4|D5|D6:'ń',D3|D4|D6:'ó',D2|D4|D6:'ś',D2|D3|D4|D6:'ź',D1|D2|D3|D4|D6:'ż'}
def text_of(dots):
    out=''
    for d in dots:
        base=d&0x3f;cap=d&D7
        if d&D8 and d&D7 and base==0:out+='_';continue   # cursor on a blank
        if d&D8 and (d&0x3f) in pl and not (d&D7 and (d&0x3f) not in pl):
            ch=pl[d&0x3f];out+=ch.upper() if d&D7 else ch;continue   # Polish letters carry dot 8
        d2=d&~(D7|D8) if d&D8 else d   # the cursor adds 7+8
        if d2 in ascii and not ((d2&0x3f) in L and not (d2&0xc0)==0x40 and d2 not in (D4|D7,)):out+=ascii[d2];continue
        b=d2&0x3f
        if b in L:ch=letters[L.index(b)];out+=ch.upper() if d2&D7 else ch;continue
        if (d2&~D7) in pl:ch=pl[d2&~D7];out+=ch.upper() if d2&D7 else ch;continue
        out+=ascii.get(d2,'?')
    return out
def connect():
    for _ in range(120):
        try:return socket.create_connection(('127.0.0.1',port),timeout=1)
        except OSError:time.sleep(1)
    raise SystemExit('no QEMU braille socket')
s=connect();s.settimeout(0.05)
f=open(log,'a',encoding='utf-8');keys=log+'.keys';seen=0
def say(line):f.write(time.strftime('%H:%M:%S ')+line+'\n');f.flush()
say('connected')
buf=b''
def send(code,payload):
    data=bytes([0x1b,code])+b''.join(bytes([b,b]) if b==0x1b else bytes([b]) for b in payload);s.sendall(data)
while True:
    try:
        chunk=s.recv(4096)
        if not chunk:break
        buf+=chunk
    except socket.timeout:pass
    except OSError:break
    # packets
    while True:
        i=buf.find(b'\x1b')
        if i<0:buf=b'';break
        buf=buf[i:]
        if len(buf)<2:break
        code=buf[1]
        if code==0x84:say('identity request');send(0x84,b'Baum Vario %-6d'%cells);send(0x01,bytes([cells]));buf=buf[2:];continue
        if code==0x01:
            # cells: ESC doubled inside
            out=[];j=2
            while j<len(buf) and len(out)<cells:
                if buf[j]==0x1b:
                    if j+1<len(buf) and buf[j+1]==0x1b:out.append(0x1b);j+=2;continue
                    break
                out.append(buf[j]);j+=1
            if len(out)<cells:break   # wait for the rest
            buf=buf[j:]
            say('SHOW '+''.join(chr(0x2800+d) for d in out)+' | '+text_of(out).rstrip())
            continue
        buf=buf[2:]
    if os.path.exists(keys):
        lines=open(keys,encoding='utf-8').read().splitlines()
        for line in lines[seen:]:
            k,_,n=line.partition(' ');n=int(n)
            if k=='top':send(0x24,bytes([1<<(n-1)]));time.sleep(.15);send(0x24,b'\0')
            elif k=='route':
                b=bytearray((cells+7)//8);b[n//8]|=1<<(n%8);send(0x22,bytes(b));time.sleep(.15);send(0x22,bytes(len(b)))
            say('KEY '+line)
        seen=len(lines)
say('closed')
