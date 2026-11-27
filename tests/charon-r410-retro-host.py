# Host checks of the r410 6q/6r retro assistant parts (simple web, remote-browser page building, download scanning,
# mail bridge refusal, print spool, clock TZ, machine command line). Not shipped. python3 -I tests/charon-r410-retro-host.py
import os, socket, sys, tempfile, threading, types

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, '..', 'src', 'hypervisor', 'overlay', 'usr', 'lib', 'charonhv')
sys.path.insert(0, LIB)
failed = 0


def check(name, ok):
    global failed
    print(('PASS ' if ok else 'FAIL ') + name)
    failed += 0 if ok else 1


import hvclock, hvsimple, hvprint
check('clock: TZ for +60 min is CHV-1:00, 0 is UTC0, -330 is CHV+5:30',
      hvclock.tz_string(60) == 'CHV-1:00' and hvclock.tz_string(0) == 'UTC0' and hvclock.tz_string(-330) == 'CHV+5:30')

doc = ('<html><head><title>T</title><style>x{}</style><script>alert(1)</script></head><body><nav><a href="/a?b=1">A link</a></nav>'
       '<img src="x.png" alt="Logo"><img src="y.png"><form action="/search" method="get"><input type="search" name="q"><input type="submit" value="Go"></form>'
       '<form action="https://ex.org/login" method="POST"><input type="email" name="e"><input type="password" name="p"><button>Log in</button></form>'
       '<p onclick="x()" style="color:red">Text<script>bad()</script> more</p></body></html>')
s = hvsimple.Simplifier('https://example.com/dir/page', '/r?u=')
s.feed(doc)
out = ''.join(s.out)
check('simple web: scripts, styles and event attributes gone', 'alert' not in out and 'bad()' not in out and 'onclick' not in out and 'color:red' not in out)
check('simple web: links rewritten through simple.charon', '/?u=https%3A%2F%2Fexample.com%2Fa%3Fb%3D1' in out)
check('simple web: GET form keeps its target in hidden fields', '<form method="get" action="/f"><input type="hidden" name="u" value="https://example.com/search">' in out)
check('simple web: POST form posts to /f with the target', 'action="/f?u=https%3A%2F%2Fex.org%2Flogin&amp;m=post"' in out and 'method="post"' in out)
check('simple web: search/email inputs become text, password stays password', 'type="text" name="q"' in out and 'type="password"' in out)
check('simple web: image alt kept, image without alt dropped', '[Logo]' in out and 'y.png' not in out)
check('simple web: title read', s.title == 'T')

sw = hvsimple.SimpleWeb(tempfile.mkdtemp(), lambda k, t: None)
seen = []
sw.render = lambda url, data=None, blk=None: seen.append((url, data)) or (200, 'text/html', b'', False)
sw.handle('GET', '/f?u=https%3A%2F%2Fexample.com%2Fsearch&m=get&q=abc+d', b'')
sw.handle('POST', '/f?u=https%3A%2F%2Fex.org%2Flogin&m=post', b'e=a%40b&p=secret')
sw.handle('GET', '/?u=wikipedia.org', b'')
sw.handle('GET', '/s?q=old+windows', b'')
check('simple web: GET form goes to the target with its fields only', seen[0] == ('https://example.com/search?q=abc+d', None))
check('simple web: POST form re-posted with its fields', seen[1] == ('https://ex.org/login', b'e=a%40b&p=secret'))
check('simple web: a bare name opens https, words search DuckDuckGo HTML', seen[2][0] == 'https://wikipedia.org' and seen[3][0] == hvsimple.SEARCH + 'old+windows')

# ---- remote browser page from an accessibility tree (no Chromium needed)
if os.name == 'posix':
    import hvremote
    rb = hvremote.RemoteBrowser(tempfile.mkdtemp(), lambda k, t: None)
    tree = {'nodes': [
        {'nodeId': '1', 'role': {'value': 'RootWebArea'}, 'name': {'value': 'Feed'}, 'childIds': ['2', '3', '4', '5', '6']},
        {'nodeId': '2', 'role': {'value': 'heading'}, 'name': {'value': 'News'}, 'properties': [{'name': 'level', 'value': {'value': 1}}], 'childIds': []},
        {'nodeId': '3', 'role': {'value': 'link'}, 'name': {'value': 'Profile'}, 'backendDOMNodeId': 11, 'childIds': []},
        {'nodeId': '4', 'role': {'value': 'textbox'}, 'name': {'value': 'Write a comment'}, 'value': {'value': ''}, 'backendDOMNodeId': 12, 'childIds': []},
        {'nodeId': '5', 'role': {'value': 'button'}, 'name': {'value': 'Post'}, 'backendDOMNodeId': 13, 'childIds': []},
        {'nodeId': '6', 'role': {'value': 'StaticText'}, 'name': {'value': 'Hello <world>'}, 'childIds': []}]}
    rb.call = lambda method, timeout=30, **p: tree if method == 'Accessibility.getFullAXTree' else {'result': {'value': 'https://www.facebook.com/'}}
    page = rb.render().decode()
    check('remote browser: heading, link as a click, text box, button as submit, text escaped',
          '<h1>News</h1>' in page and '/r/click?n=11' in page and 'name="t12"' in page and 'name="b13" value="Post"' in page and 'Hello &lt;world&gt;' in page)
    check("remote browser: Facebook's terms said on the page", '3.2.3' in page)
    acts = []
    rb.set_text = lambda b, v: acts.append(('text', b, v))
    rb.click = lambda b: acts.append(('click', b))
    rb.start = lambda: None
    rb.render = lambda note='': b'ok'
    rb._handle('POST', '/r/act', b't12=Nice+photo&b13=Post', None)
    check('remote browser: typed text put in, then the button clicked', acts == [('text', 12, 'Nice photo'), ('click', 13)])
else:
    print('SKIP remote browser tests: not a POSIX host')

# ---- the proxy: a download is held and scanned; a page is not; mail without a server is refused and said
try:
    import hvproxy
except ImportError as e:
    hvproxy = None
    print('SKIP proxy tests: %s' % e)
if hvproxy:
    logs = []
    P = hvproxy.Proxy.__new__(hvproxy.Proxy)
    P.scan, P.retro, P.said_scanoff = True, {}, False
    P.log = lambda k, t: logs.append((k, t))
    P.is_blocked = lambda h: False

    def serve(resp):
        a, b = socket.socketpair()
        threading.Thread(target=lambda: (a.sendall(resp), a.close()), daemon=True).start()
        return b
    got = []
    P.scan_file = lambda tmp, host, path: ('Eicar-Test-Signature', None)
    P.forward(got.append, serve(b'HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n\r\nMZ....'), 'ex.org', 'files/x.exe')
    check('proxy: an infected download is refused, the file never reaches the guest', b'403' in got[0] and b'MZ' not in b''.join(got))
    got = []
    P.scan_file = lambda tmp, host, path: (None, None)
    P.forward(got.append, serve(b'HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\n\r\nPK-data'), 'ex.org', 'a.zip')
    check('proxy: a clean download is passed on whole', b''.join(got).endswith(b'PK-data') and b'200 OK' in got[0])
    got = []
    P.scan_file = lambda tmp, host, path: ('X', None)
    P.forward(got.append, serve(b'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html>'), 'ex.org', 'index.html')
    check('proxy: a page is not held for scanning', b''.join(got).endswith(b'<html>'))
    a, b = socket.socketpair()
    P.mail_one('imap', a)
    check('proxy: IMAP without a server set answers BYE and is said', b.recv(200).startswith(b'* BYE') and ('MAILNOCONF', 'imap') in logs)

# ---- print spool: a PostScript job becomes a PDF (or is kept as .ps without Ghostscript, said)
root = tempfile.mkdtemp()
hvprint.ROOT = root
done = []
sp = hvprint.Spool('m1', lambda path, pages, err: done.append((path, pages, err)), lambda t: None)
open(hvprint.port_file('m1'), 'ab').write(b'\x1b%-12345X@PJL\r\n%!PS-Adobe-3.0\n/Times-Roman findfont 12 scalefont setfont 72 720 moveto (Charon) show showpage\n%%EOF\n')
sp.poll()
check('print: one PostScript job found and finished', len(done) == 1 and (done[0][0].endswith('.pdf') or (done[0][0].endswith('.ps') and done[0][2])))

# ---- machine command line: sound slot, mail forwards, printer port, exchange drive
import hvmachine
d = tempfile.mkdtemp()
open(os.path.join(d, 'machine.json'), 'w').write('{"name": "XP", "template": "winxp", "family": "windows", "secure": true, "nic": "rtl8139", '
                                                '"disks": ["d.qcow2"], "sound": "AC97", "shared": true}')
hvmachine.MACHINES = os.path.dirname(d)
m = hvmachine.Machine(os.path.basename(d))
m.proxy_port, m.mail_ports, m.sound_slot = 4000, {'pop': 4001, 'imap': 4002, 'smtp': 4003}, 3
hvmachine.HV = tempfile.mkdtemp()
hvprint.ROOT = root
cmd = ' '.join(m.command(False))
check('machine: its own sound slot (real mute switch)', 'out.dev=charonguest3,' in cmd)
check('machine: mail forwards 110/143/25/587 to the bridge', all('10.0.2.100:%d-cmd:' % p in cmd for p in (110, 143, 25, 587)))
check('machine: LPT1 goes to the print spool file', 'isa-parallel,chardev=lpt0' in cmd and 'lpt1.raw' in cmd)
check('machine: no writable vvfat drive (it blocks savevm, measured)', 'fat:rw:' not in cmd)
import hvexchange
open(hvexchange.image(m.dir), 'wb').close()
cmd = ' '.join(m.command(False))
check('machine: exchange drive is a qcow2 FAT image (snapshots include it)', 'exchange.qcow2,if=none,id=uex,format=qcow2' in cmd)
os.remove(hvexchange.image(m.dir))
# 6u: the one browser layer routes per site (YouTube to the modern browser: its terms forbid downloading/converting)
import hvroute
check('route: YouTube -> modern browser (terms), not transcoded', hvroute.route('https://www.youtube.com/watch?v=x')[::2] == ('modern', 'video'))
check('route: Netflix -> modern browser (DRM)', hvroute.route('https://www.netflix.com/browse')[::2] == ('modern', 'drm'))
check('route: Facebook -> remote browser', hvroute.route('https://m.facebook.com/')[0] == 'remote')
check('route: Wikipedia -> simple web', hvroute.route('https://en.wikipedia.org/wiki/X')[0] == 'simple')
check('route: the page explains in both languages', b'Regulamin' in hvroute.modern_page('https://youtu.be/x', 'youtu.be', 'video'))
print('%d failed' % failed)
