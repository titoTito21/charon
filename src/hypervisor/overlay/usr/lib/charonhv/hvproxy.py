# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: the guest's HTTP/HTTPS proxy (secure-use profile, answers 6i/6j).

One process per running machine, listening on 127.0.0.1 only; QEMU's user network runs the guest with restrict=on
(nothing leaves the guest by itself, nothing reaches it from outside) and forwards 10.0.2.100:3128 inside the guest to
this proxy. So the guest's only way out is HTTP (port 80) and HTTPS (port 443) through here, filtered by domain.

HTTPS: a client that offers TLS 1.2 or newer (Supermium, Mypal, a current browser) gets a plain tunnel - the proxy
never sees its traffic. A client that offers only TLS 1.0/1.1 (Windows XP's own SChannel: Internet Explorer,
Automatic Updates, .NET) cannot talk to today's sites; for it the proxy ends the old TLS itself with a certificate of
this hypervisor's own certificate authority (trusted in that guest only, its key never leaves /hv/secure) and opens a
modern, VERIFIED TLS connection to the site. A site whose certificate fails verification is refused and said so.
Every refusal is one line in the log that the menu reads aloud: BLOCKED, DENIED, CERTFAIL; MODERNTLS/TUNNEL/HTTP are
written for the record only."""
import json, os, select, socket, ssl, subprocess, sys, tempfile, threading, time, urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import hvscan, hvsimple    # noqa: E402  (hvremote is imported when first used: it needs pwd, a POSIX system)

UPDATES_HOST = 'updates.charon'     # 6zd: the guest's update status report (never forwarded anywhere)
TEST_DOMAINS = ('malware.testing.google.test', 'blocked.charon-hypervisor.test')
LOCK = threading.Lock()


def stamp(days):
    """A certificate time, days from now (notBefore is given as 2 days back): the hypervisor's clock may run on a
    real-time clock kept in LOCAL time (measured: Alpine's UTC was the local CET time, so a fresh certificate was
    'not yet valid' for Windows XP for an hour)."""
    return time.strftime('%Y%m%d%H%M%SZ', time.gmtime(time.time() + days * 86400))


def now():
    return time.strftime('%H:%M:%S')


RISKY = ('.exe', '.com', '.scr', '.pif', '.bat', '.cmd', '.msi', '.msp', '.zip', '.rar', '.7z', '.cab', '.arj', '.lzh',
         '.doc', '.xls', '.ppt', '.docm', '.xlsm', '.pdf', '.rtf', '.js', '.vbs', '.wsf', '.hta', '.jar', '.dll', '.sys',
         '.iso', '.img', '.gz', '.tar', '.ocx', '.cpl', '.reg', '.lnk')
PAGE_TYPES = ('text/html', 'text/css', 'image/', 'text/plain', 'application/json', 'text/xml', 'application/xml',
              'font/', 'application/font', 'text/javascript', 'application/javascript', 'application/x-javascript')
MAIL_UPSTREAM = {'pop': 995, 'imap': 993, 'smtp': 465}


class Proxy:
    def __init__(self, port, sdir, logpath, blockfile, mdir=None):
        self.port, self.dir, self.logpath = port, sdir, logpath
        self.mdir = mdir
        self.retro = {}
        try:
            self.retro = json.load(open(os.path.join(mdir, 'retro.json'), encoding='utf-8')) if mdir else {}
        except (OSError, ValueError):
            pass
        self.simple = hvsimple.SimpleWeb(mdir, self.log) if mdir and self.retro.get('simple', True) else None
        self.remote = None
        self.scan = bool(self.retro.get('scan', True))
        self.said_scanoff = False
        self.said_modern = set()    # 6za: DRM/video hosts already offered the modern-browser mode (once per site)
        self.blocked = set(TEST_DOMAINS)
        try:
            for line in open(blockfile, encoding='utf-8', errors='replace'):
                p = line.split('#', 1)[0].split()
                if not p:
                    continue
                d = p[-1].strip().lower().rstrip('.')
                if '.' in d and d not in ('localhost', 'localhost.localdomain'):
                    self.blocked.add(d)
        except OSError:
            pass
        self.certs = {}

    def log(self, kind, text):
        with LOCK:
            with open(self.logpath, 'a', encoding='utf-8') as f:
                f.write('%s %s %s\n' % (now(), kind, text))

    def is_blocked(self, host):
        h = host.lower().rstrip('.')
        parts = h.split('.')
        return any('.'.join(parts[i:]) in self.blocked for i in range(len(parts) - 1))

    def offer_modern(self, host):
        """6za: a maintained modern browser in the guest (Supermium) reaches a DRM/video site over a direct TLS 1.2+
        tunnel, so it never passes through the simple-web router that normally emits NEEDMODERN. The public pages
        (Netflix login/landing/catalogue, a YouTube page) load through the tunnel and the guest's own reader (NVDA)
        reads them; but protected video (Widevine) does not play on old Windows, so the hypervisor offers its
        modern-browser mode on the real screen (6s). We log NEEDMODERN - the same line the menu already speaks and
        acts on - ONCE per site per proxy run (deduped), and we do NOT block the tunnel: browsing/reading keeps
        working. The DRM/video host list is hvroute's (netflix.com ..., youtube.com ...)."""
        import hvroute
        kind, site, why = hvroute.route('https://' + host + '/')
        if kind != 'modern':
            return
        with LOCK:
            if site in self.said_modern:
                return
            self.said_modern.add(site)
        self.log('NEEDMODERN', '%s|%s|%s' % (site, why, 'https://' + host + '/'))

    # ---- certificates -------------------------------------------------------------------------------------
    def host_context(self, host):
        with LOCK:
            chain = self.certs.get(host)
            if chain:
                return chain, self.rsa
            hdir = os.path.join(self.dir, 'hosts')
            os.makedirs(hdir, exist_ok=True)
            safe = ''.join(ch for ch in host if ch.isalnum() or ch in '.-')[:200]
            crt = os.path.join(hdir, safe + '.crt')
            key = os.path.join(self.dir, 'host.key')
            if not os.path.exists(key):
                subprocess.run(['openssl', 'genrsa', '-out', key, '2048'], check=True, capture_output=True, timeout=120)
            if not os.path.exists(crt) or time.time() - os.path.getmtime(crt) > 300 * 86400:
                ip = all(ch.isdigit() or ch == '.' for ch in host)
                ext = os.path.join(hdir, safe + '.ext')
                open(ext, 'w').write('subjectAltName=%s:%s\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\n'
                                     'extendedKeyUsage=serverAuth\n' % ('IP' if ip else 'DNS', host))
                csr = os.path.join(hdir, safe + '.csr')
                subprocess.run(['openssl', 'req', '-new', '-key', key, '-subj', '/CN=' + host[:64], '-out', csr],
                               check=True, capture_output=True, timeout=60)
                subprocess.run(['openssl', 'x509', '-req', '-in', csr, '-CA', os.path.join(self.dir, 'ca.crt'),
                                '-CAkey', os.path.join(self.dir, 'ca.key'), '-set_serial', str(int(time.time() * 1000)),
                                '-not_before', stamp(-2), '-not_after', stamp(395), '-sha256', '-extfile', ext, '-out', crt + '.new'],
                               check=True, capture_output=True, timeout=60)
                os.replace(crt + '.new', crt)
            if not hasattr(self, 'rsa'):
                der = subprocess.run(['openssl', 'rsa', '-in', key, '-traditional', '-outform', 'DER'], check=True,
                                     capture_output=True, timeout=60).stdout
                self.rsa = rsa_numbers(der)
            chain = [ssl.PEM_cert_to_DER_cert(open(p).read()) for p in (crt, os.path.join(self.dir, 'ca.crt'))]
            self.certs[host] = chain
            return chain, self.rsa

    # ---- connections --------------------------------------------------------------------------------------
    def serve(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(('127.0.0.1', self.port))
        s.listen(64)
        self.log('READY', 'port %d, %d blocked domains' % (self.port, len(self.blocked)))
        while True:
            c, _ = s.accept()
            threading.Thread(target=self.handle, args=(c,), daemon=True).start()

    def handle(self, c):
        try:
            c.settimeout(60)
            head = b''
            while b'\r\n\r\n' not in head:
                b = c.recv(65536)
                if not b:
                    return
                head += b
                if len(head) > 65536:
                    return
            line, _, rest = head.partition(b'\r\n')
            parts = line.decode('latin-1').split()
            if len(parts) < 3:
                return
            method, target = parts[0].upper(), parts[1]
            if method == 'CONNECT':
                host, _, port = target.rpartition(':')
                host = host.strip('[]')
                port = int(port or 443)
                if port != 443:
                    self.log('DENIED', '%s:%d' % (host, port))
                    c.sendall(self.refusal(403, 'Port %d is closed by Charon Hypervisor.' % port, 'Port %d jest zamknięty przez Charon Hypervisor.' % port))
                    return
                if self.is_blocked(host):
                    self.log('BLOCKED', host)
                    c.sendall(self.refusal(403, 'Blocked by Charon Hypervisor: %s.' % host, 'Zablokowane przez Charon Hypervisor: %s.' % host))
                    return
                c.sendall(b'HTTP/1.0 200 Connection established\r\n\r\n')
                first = head.split(b'\r\n\r\n', 1)[1]
                c.settimeout(20)
                while len(first) < 11:
                    b = c.recv(4096)
                    if not b:
                        return
                    first += b
                modern = first[0] == 0x16 and (first[9] << 8 | first[10]) >= 0x0303
                if modern or first[0] != 0x16:
                    self.log('TUNNEL', host)
                    self.offer_modern(host)
                    u = socket.create_connection((host, port), 20)
                    u.sendall(first)
                    self.relay(c, u)
                else:
                    self.log('MODERNTLS', '%s (guest offered TLS %d.%d)' % (host, first[9] - 2, first[10] - 1))
                    self.bridge(c, host, port, first)
                return
            if not target.lower().startswith('http://'):
                c.sendall(self.refusal(400, 'Only web addresses go through Charon Hypervisor.', 'Przez Charon Hypervisor przechodzą tylko adresy WWW.'))
                return
            hostport, _, path = target[7:].partition('/')
            host, _, port = hostport.rpartition(':') if ':' in hostport else (hostport, '', '80')
            port = int(port or 80)
            if host.lower() == UPDATES_HOST:
                c.sendall(self.update_report('/' + path))
                return
            if host.lower() in hvsimple.HOSTS or (self.simple and self.simple.wants(host)):
                body = self.read_body(c, rest)
                c.sendall(self.local_page(method, '/' + path, body, None if host.lower() in hvsimple.HOSTS else 'http://' + hostport + '/' + path))
                return
            if port != 80:
                self.log('DENIED', '%s:%d' % (host, port))
                c.sendall(self.refusal(403, 'Port %d is closed by Charon Hypervisor.' % port, 'Port %d jest zamknięty przez Charon Hypervisor.' % port))
                return
            if self.is_blocked(host):
                self.log('BLOCKED', host)
                c.sendall(self.refusal(403, 'Blocked by Charon Hypervisor: %s.' % host, 'Zablokowane przez Charon Hypervisor: %s.' % host))
                return
            # session 11, measured (ASMX diagnostic): XP's Windows Update Agent 7.6 sends "Accept-Encoding: xpress" and
            # EXPECTS an xpress-compressed GetConfig reply (Legacy Update's native path, which real XP clients use).
            # Session 10 stripped that header so Legacy Update (via Cloudflare, Vary: accept-encoding) returned PLAIN XML;
            # WUA, having asked for xpress, rejected the plain 200 reply with 0x8024400A (soap client error 10) and
            # retried GetConfig every ~60 s forever. So the header is kept now: WUA gets the xpress reply it expects and
            # decodes it itself; the proxy relays the compressed body verbatim (text/xml, not a download).
            hdrs = [h for h in rest.split(b'\r\n\r\n', 1)[0].split(b'\r\n')
                    if h and not h.lower().startswith((b'proxy-connection:', b'connection:', b'keep-alive:'))]
            # the WHOLE request body (Content-Length): measured session 9, Windows Update Agent sends the SOAP body in
            # its own TCP write after the headers - only the first read was forwarded, the server got a cut request
            # and the guest's update search failed with 0x8024400A
            body = self.read_body(c, rest)
            req = ('%s /%s %s\r\n' % (method, path, parts[2])).encode('latin-1') + b'\r\n'.join(hdrs) + b'\r\nConnection: close\r\n\r\n' + body
            self.log('HTTP', host)
            if 'client.asmx' in path.lower():     # 6zd diagnostic: the guest's update exchange with Legacy Update
                self.log_asmx_req(method, path, rest.split(b'\r\n\r\n', 1)[0], len(body))
            u = socket.create_connection((host, port), 20)
            u.sendall(req)
            self.forward(c.sendall, u, host, path)
        except Exception as e:
            self.log('ERROR', repr(e)[:200])
        finally:
            try:
                c.close()
            except OSError:
                pass

    def update_report(self, path):
        """6zd: the guest's update status. CHARONWU.VBS (put in by the secure-use set, run by the guest's own task
        scheduler at start and every hour) asks Windows Update - i.e. Legacy Update's server - what is waiting and sends
        http://updates.charon/report?pending=N&installed=M&last=DATE&titles=...&error=HEX through this proxy. Nothing
        leaves the hypervisor; the menu says the change aloud (UPDATES line in the log, updates.json for the menu)."""
        q = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query)
        one = lambda k: (q.get(k) or [''])[0][:300]
        num = lambda k: int(one(k)) if one(k).isdigit() else None
        rep = {'time': int(time.time()), 'pending': num('pending'), 'installed': num('installed'), 'last': one('last')[:20],
               'titles': [t for t in one('titles').split('|') if t][:5], 'error': one('error')[:12]}
        if self.mdir:
            try:
                with open(os.path.join(self.mdir, 'updates.json.new'), 'w', encoding='utf-8') as f:
                    json.dump(rep, f, ensure_ascii=False)
                os.replace(os.path.join(self.mdir, 'updates.json.new'), os.path.join(self.mdir, 'updates.json'))
            except OSError as e:
                self.log('ERROR', 'updates.json: %r' % e)
        self.log('UPDATES', json.dumps(rep, ensure_ascii=False))
        body = self.protection().encode()
        return b'HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n' % len(body) + body

    def protection(self):
        """Session 10: the REAL protection state for XP's Security Center (CHARONWU.VBS): scan=1 when downloads are
        scanned now (switched on, ClamAV ready, clamd running), fresh=1 when the signatures are at most 3 days old."""
        import hvscan
        scan = getattr(self, 'scan', False) and hvscan.ready() and hvscan.clamd_running()
        age = hvscan.signature_age_days()
        return 'scan=%d\nfresh=%d\n' % (1 if scan else 0, 1 if scan and age is not None and age <= 3 else 0)

    def log_asmx_req(self, method, path, head_bytes, body_len):
        """6zd diagnostic: the guest's Windows Update Agent request to Legacy Update's ClientWebService. Logs only the
        method, path, body length and a few framing headers - never cookies or bodies (an update SOAP has no secret)."""
        def hv(name):
            for ln in head_bytes.split(b'\r\n')[1:]:
                if ln.lower().startswith(name):
                    return ln.split(b':', 1)[1].strip().decode('latin-1', 'replace')[:120]
            return '-'
        self.log('ASMX-REQ', '%s /%s | AE=%s | SOAPAction=%s | CT=%s | UA=%s | body=%d'
                 % (method, path, hv(b'accept-encoding:'), hv(b'soapaction:'), hv(b'content-type:'), hv(b'user-agent:'), body_len))

    @staticmethod
    def refusal(code, en, pl):
        # Internet Explorer replaces an error page shorter than 512 bytes by its own "friendly" page (measured on IE8:
        # the EICAR refusal showed "the site requires you to log in"), so the page always carries an explanation.
        body = ('<html><head><title>Charon Hypervisor</title></head><body><h1>%s</h1><p lang="pl">%s</p>'
                '<p>This page comes from Charon Hypervisor, which protects this old Windows: it checks the addresses and the '
                'downloads of this machine before they reach it. Nothing was changed on this computer.</p>'
                '<p lang="pl">Tę stronę wysłał Charon Hypervisor, który chroni ten stary Windows: sprawdza adresy i pobierane '
                'pliki tej maszyny, zanim do niej dotrą. Na tym komputerze nic nie zmieniono.</p></body></html>' % (en, pl)).encode('utf-8')
        return (b'HTTP/1.0 %d Refused\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n' % (code, len(body))) + body

    @staticmethod
    def relay(a, b):
        a.settimeout(None)
        b.settimeout(None)
        socks = [a, b]
        try:
            while True:
                r, _, _ = select.select(socks, [], [], 300)
                if not r:
                    return
                for s in r:
                    d = s.recv(65536)
                    if not d:
                        return
                    (b if s is a else a).sendall(d)
        finally:
            b.close()

    @staticmethod
    def read_body(c, rest):
        head, _, body = rest.partition(b'\r\n\r\n')
        need = 0
        for h in head.split(b'\r\n'):
            if h.lower().startswith(b'content-length:'):
                need = min(int(h.split(b':', 1)[1].strip() or 0), 8 * 1048576)
        while len(body) < need:
            d = c.recv(65536)
            if not d:
                break
            body += d
        return body

    def local_page(self, method, path, body, original):
        """simple.charon (simple web, /r* = the remote modern browser) or a site on the simple list. 6u: an address
        given to simple web (?u=) is routed per site by hvroute (simple / remote browser / modern browser mode)."""
        import hvroute
        want = urllib.parse.parse_qs(urllib.parse.urlsplit(path).query).get('u', [''])[0] if method == 'GET' and not path.startswith('/r') else ''
        if want:
            kind, site, why = hvroute.route(want)
            if kind == 'modern':
                self.log('NEEDMODERN', '%s|%s|%s' % (site, why, want[:300]))
                data = hvroute.modern_page(want, site, why)
                return (b'HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\nCache-Control: no-store\r\n'
                        b'Connection: close\r\n\r\n' % len(data)) + data
            if kind == 'remote':
                import hvremote
                if hvremote.binary():
                    self.log('ROUTE', 'remote|%s' % site)
                    path = '/r?u=' + urllib.parse.quote(want, safe='')
        if path.startswith('/r'):
            if self.remote is None:
                import hvremote
                self.remote = hvremote.RemoteBrowser(self.mdir, self.log)
            code, ctype, data = self.remote.handle(method, path, body, self.is_blocked)
        elif self.simple is None:
            code, ctype, data = 403, 'text/html; charset=utf-8', b'<html><body><h1>Simple web is off for this machine / Prosta sie\xc4\x87 jest wy\xc5\x82\xc4\x85czona</h1></body></html>'
        elif original:
            code, ctype, data, _ = self.simple.render(original, body if method == 'POST' else None, self.is_blocked)
        else:
            code, ctype, data = self.simple.handle(method, path, body, self.is_blocked)
        if not ctype.startswith('text/html') and self.scan:
            data, code, ctype = self.scan_blob(data, code, ctype, '(simple web)', path)
        return (b'HTTP/1.0 %d OK\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n'
                % (code, ctype.encode('latin-1', 'replace'), len(data))) + data

    def scan_blob(self, data, code, ctype, host, path):
        fd, tmp = tempfile.mkstemp(dir='/hv' if os.path.isdir('/hv') else None, prefix='.scan-')
        try:
            os.write(fd, data)
            os.close(fd)
            virus, err = self.scan_file(tmp, host, path)
            if virus:
                body = self.refusal(403, 'Download stopped by Charon Hypervisor: it contains %s.' % virus,
                                    'Pobieranie zatrzymane przez Charon Hypervisor: zawiera %s.' % virus)
                return body.split(b'\r\n\r\n', 1)[1], 403, 'text/html; charset=utf-8'
            return data, code, ctype
        finally:
            try:
                os.remove(tmp)
            except OSError:
                pass

    def scan_file(self, tmp, host, path):
        name = urllib.parse.unquote(path.split('?')[0].rsplit('/', 1)[-1])[:80] or host
        if not hvscan.clamd_running():
            if not self.said_scanoff:
                self.said_scanoff = True
                self.log('SCANOFF', name)
            return None, 'off'
        virus, err = hvscan.scan_bytes_file(tmp)
        if virus:
            self.log('SCANFOUND', '%s|%s|%s' % (host, name, virus))
        elif err:
            self.log('SCANERR', '%s|%s' % (name, err))
        else:
            self.log('SCANCLEAN', '%s|%s' % (host, name))
        return virus, err

    def forward(self, write, u, host, path):
        """The site's answer to the guest; a download (a program, an archive, a document) is held and scanned first."""
        u.settimeout(60)
        head = b''
        while b'\r\n\r\n' not in head and len(head) < 65536:
            d = u.recv(65536)
            if not d:
                break
            head += d
        h, _, rest = head.partition(b'\r\n\r\n')
        low = h.lower()
        ctype = ''
        disp = ''
        for line in low.split(b'\r\n')[1:]:
            if line.startswith(b'content-type:'):
                ctype = line.split(b':', 1)[1].strip().decode('latin-1')
            elif line.startswith(b'content-disposition:'):
                disp = line.split(b':', 1)[1].strip().decode('latin-1')
        p = path.split('?')[0].lower()
        if 'client.asmx' in p:               # 6zd diagnostic: what Legacy Update returns to the guest's update agent
            def rv(name):
                for ln in low.split(b'\r\n')[1:]:
                    if ln.startswith(name):
                        return ln.split(b':', 1)[1].strip().decode('latin-1', 'replace')[:60]
                return '-'
            self.log('ASMX-RESP', '%s | CE=%s | CT=%s | CL=%s | TE=%s | bodyfirst=%s'
                     % (h.split(b'\r\n', 1)[0].decode('latin-1', 'replace'), rv(b'content-encoding:'), rv(b'content-type:'),
                        rv(b'content-length:'), rv(b'transfer-encoding:'), rest[:12].hex()))
        download = self.scan and b'\r\n\r\n' in head and (
            'attachment' in disp or p.endswith(RISKY) or not ctype.startswith(PAGE_TYPES))
        if not download:
            write(head)
            try:
                while True:
                    d = u.recv(65536)
                    if not d:
                        break
                    write(d)
            finally:
                u.close()
            return
        fd, tmp = tempfile.mkstemp(dir='/hv' if os.path.isdir('/hv') else None, prefix='.scan-')
        size = len(rest)
        too_big = False
        try:
            with os.fdopen(fd, 'wb') as f:
                f.write(rest)
                while True:
                    d = u.recv(1 << 20)
                    if not d:
                        break
                    size += len(d)
                    if size > hvscan.MAXSTREAM:
                        too_big = True
                        break
                    f.write(d)
            if too_big:
                self.log('SCANSKIP', '%s|%s|%d MB' % (host, path.rsplit('/', 1)[-1][:60], size // 1048576))
                write(h + b'\r\n\r\n')
                with open(tmp, 'rb') as f:
                    while True:
                        b = f.read(1 << 20)
                        if not b:
                            break
                        write(b)
                write(d)
                while True:
                    d = u.recv(1 << 20)
                    if not d:
                        break
                    write(d)
                return
            virus, err = self.scan_file(tmp, host, path)
            if virus:
                write(self.refusal(403, 'Download stopped by Charon Hypervisor: it contains %s.' % virus,
                                   'Pobieranie zatrzymane przez Charon Hypervisor: zawiera %s.' % virus))
                return
            write(h + b'\r\n\r\n')
            with open(tmp, 'rb') as f:
                while True:
                    b = f.read(1 << 20)
                    if not b:
                        break
                    write(b)
        finally:
            u.close()
            try:
                os.remove(tmp)
            except OSError:
                pass

    # ---- mail (6q): the old client speaks plain POP3/IMAP/SMTP to 10.0.2.100; the hypervisor speaks TLS to the server
    def serve_mail(self, kind, lport):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(('127.0.0.1', lport))
        s.listen(16)
        while True:
            c, _ = s.accept()
            threading.Thread(target=self.mail_one, args=(kind, c), daemon=True).start()

    def mail_one(self, kind, c):
        target = (self.retro.get('mail') or {}).get(kind, '').strip()
        bye = {'pop': b'-ERR %s\r\n', 'imap': b'* BYE %s\r\n', 'smtp': b'421 %s\r\n'}[kind]
        try:
            if not target:
                self.log('MAILNOCONF', kind)
                c.sendall(bye % b'Charon Hypervisor: no mail server set for this machine (Retro assistant, Mail).')
                return
            host, _, port = target.partition(':')
            port = int(port or MAIL_UPSTREAM[kind])
            if self.is_blocked(host):
                self.log('BLOCKED', host)
                c.sendall(bye % b'Charon Hypervisor: this server is on the list of malicious domains.')
                return
            ctx = ssl.create_default_context()
            ctx.minimum_version = ssl.TLSVersion.TLSv1_2
            raw = socket.create_connection((host, port), 20)
            greeting = b''
            if kind == 'smtp' and port in (25, 587):       # STARTTLS before the old client says anything
                f = raw.makefile('rb')
                greeting = self.smtp_reply(f)
                raw.sendall(b'EHLO charon-hypervisor\r\n')
                self.smtp_reply(f)
                raw.sendall(b'STARTTLS\r\n')
                if not self.smtp_reply(f).startswith(b'220'):
                    raise ssl.SSLError('the server offers no STARTTLS')
            u = ctx.wrap_socket(raw, server_hostname=host)
            if greeting:
                c.sendall(greeting)
            self.log('MAIL', '%s %s %s' % (kind, host, u.version()))
            self.relay(c, u)
        except ssl.SSLCertVerificationError as e:
            self.log('CERTFAIL', '%s %s' % (target, e.verify_message))
            c.sendall(bye % b'Charon Hypervisor: the server certificate is not valid.')
        except Exception as e:
            self.log('MAILFAIL', '%s %s %s' % (kind, target, str(e)[:120]))
            try:
                c.sendall(bye % b'Charon Hypervisor: the mail server was not reached.')
            except OSError:
                pass
        finally:
            try:
                c.close()
            except OSError:
                pass

    @staticmethod
    def smtp_reply(f):
        out = b''
        while True:
            line = f.readline(4096)
            out += line
            if len(line) < 4 or line[3:4] != b'-':
                return out

    def bridge(self, c, host, port, first):
        """Old TLS from the guest ends here (OldTLS); a new, verified TLS 1.2+ connection goes to the site."""
        chain, rsa = self.host_context(host)
        old = OldTLS(c, first, chain, rsa)
        try:
            old.handshake()
        except TLSRefused as e:
            self.log('OLDTLSFAIL', '%s %s' % (host, e))
            return
        uctx = ssl.create_default_context()
        uctx.minimum_version = ssl.TLSVersion.TLSv1_2
        try:
            u = uctx.wrap_socket(socket.create_connection((host, port), 20), server_hostname=host)
        except ssl.SSLCertVerificationError as e:
            self.log('CERTFAIL', '%s %s' % (host, e.verify_message))
            got = b''                   # answer the guest's request (a browser shows the page instead of a reset)
            while b'\r\n\r\n' not in got:
                d = old.read()
                if not d:
                    break
                got += d
            old.write(self.refusal(502, 'The certificate of %s is not valid (%s), so Charon Hypervisor did not connect.' % (host, e.verify_message),
                                   'Certyfikat %s jest nieważny (%s), więc Charon Hypervisor się nie połączył.' % (host, e.verify_message)))
            old.close()
            return
        c.settimeout(None)
        u.settimeout(None)
        try:
            got = b''
            while b'\r\n\r\n' not in got and len(got) < 65536:
                d = old.read()
                if not d:
                    break
                got += d
            head, _, more = got.partition(b'\r\n\r\n')
            lines = head.split(b'\r\n')
            parts = lines[0].decode('latin-1').split()
            if len(parts) >= 3:
                need = 0
                for h in lines[1:]:
                    if h.lower().startswith(b'content-length:'):
                        need = int(h.split(b':', 1)[1].strip() or 0)
                while len(more) < need:
                    d = old.read()
                    if not d:
                        break
                    more += d
                if self.simple and self.simple.wants(host):
                    old.write(self.local_page(parts[0].upper(), parts[1], more, 'https://%s%s' % (host, parts[1])))
                    old.close()
                    u.close()
                    return
                hdrs = [h for h in lines[1:] if h and not h.lower().startswith((b'connection:', b'keep-alive:', b'proxy-connection:'))]
                u.sendall(lines[0] + b'\r\n' + b'\r\n'.join(hdrs) + b'\r\nConnection: close\r\n\r\n' + more)
                self.forward(old.write, u, host, parts[1].lstrip('/'))
                old.close()
                return
            u.sendall(got)
        except (OSError, TLSRefused, ssl.SSLError, ValueError):
            old.close()
            u.close()
            return
        done = threading.Event()

        def up():                       # guest -> site
            try:
                while not done.is_set():
                    d = old.read()
                    if not d:
                        break
                    u.sendall(d)
            except (OSError, TLSRefused, ssl.SSLError):
                pass
            done.set()
        threading.Thread(target=up, daemon=True).start()
        try:
            while not done.is_set():
                r, _, _ = select.select([u], [], [], 1)
                if not r and not u.pending():
                    continue
                d = u.recv(65536)
                if not d:
                    break
                old.write(d)
        except (OSError, ssl.SSLError):
            pass
        done.set()
        old.close()
        u.close()


class TLSRefused(Exception):
    pass


def der_ints(der):
    """The INTEGERs of a DER SEQUENCE (PKCS#1 RSAPrivateKey: version, n, e, d, p, q, ...)."""
    def length(i):
        n = der[i]
        if n < 0x80:
            return n, i + 1
        k = n & 0x7f
        return int.from_bytes(der[i + 1:i + 1 + k], 'big'), i + 1 + k
    assert der[0] == 0x30
    _, i = length(1)
    out = []
    while i < len(der):
        tag = der[i]
        n, i = length(i + 1)
        if tag == 0x02:
            out.append(int.from_bytes(der[i:i + n], 'big'))
        i += n
    return out


def rsa_numbers(der):
    v = der_ints(der)
    return dict(n=v[1], e=v[2], d=v[3], p=v[4], q=v[5])


def prf10(secret, label, seed, n):
    """TLS 1.0 PRF (RFC 2246 5): P_MD5 over the first half of the secret XOR P_SHA1 over the second half."""
    import hmac

    def p_hash(alg, key, data):
        out, a = b'', data
        while len(out) < n:
            a = hmac.new(key, a, alg).digest()
            out += hmac.new(key, a + data, alg).digest()
        return out[:n]
    h = (len(secret) + 1) // 2
    md5 = p_hash('md5', secret[:h], label + seed)
    sha = p_hash('sha1', secret[-h:], label + seed)
    return bytes(x ^ y for x, y in zip(md5, sha))


class RC4:
    def __init__(self, key):
        s = list(range(256))
        j = 0
        for i in range(256):
            j = (j + s[i] + key[i % len(key)]) & 255
            s[i], s[j] = s[j], s[i]
        self.s, self.i, self.j = s, 0, 0

    def crypt(self, data):
        s, i, j = self.s, self.i, self.j
        out = bytearray(len(data))
        for k, b in enumerate(data):
            i = (i + 1) & 255
            si = s[i]
            j = (j + si) & 255
            sj = s[j]
            s[i] = sj
            s[j] = si
            out[k] = b ^ s[(si + sj) & 255]
        self.i, self.j = i, j
        return bytes(out)


class OldTLS:
    """The server side of TLS 1.0 for Windows XP's SChannel, which offers only RSA key exchange with RC4 or 3DES
    (Alpine's OpenSSL has neither cipher in its TLS list any more). Cipher suites TLS_RSA_WITH_RC4_128_SHA and
    TLS_RSA_WITH_RC4_128_MD5 (RFC 2246); full handshake every time, secure renegotiation answered, renegotiation not
    offered. This leg runs between the guest and the hypervisor inside the same computer; the leg to the site is
    modern, verified TLS."""
    SUITES = {0x0005: ('sha1', 20), 0x0004: ('md5', 16)}

    def __init__(self, sock, first, chain, rsa):
        self.s, self.buf, self.chain, self.rsa = sock, bytearray(first), chain, rsa
        self.hs = bytearray()
        self.rseq = self.wseq = 0
        self.rkey = self.wkey = None
        self.plain = bytearray()
        self.wlock = threading.Lock()
        self.closed = False

    # records
    def _need(self, n):
        while len(self.buf) < n:
            d = self.s.recv(16384)
            if not d:
                raise TLSRefused('the guest closed the connection')
            self.buf += d

    def _record(self):
        self._need(5)
        typ, ln = self.buf[0], int.from_bytes(self.buf[3:5], 'big')
        if ln > 18432:
            raise TLSRefused('record too long')
        self._need(5 + ln)
        hdr, body = bytes(self.buf[:5]), bytes(self.buf[5:5 + ln])
        del self.buf[:5 + ln]
        if self.rkey:
            body = self.rkey.crypt(body)
            alg, ml = self.mac
            data, tag = body[:-ml], body[-ml:]
            import hmac
            want = hmac.new(self.rmac, self.rseq.to_bytes(8, 'big') + hdr[:3] + len(data).to_bytes(2, 'big') + data, alg).digest()
            self.rseq += 1
            if not hmac.compare_digest(want, tag):
                self._alert(20)
                raise TLSRefused('bad record MAC')
            body = data
        return typ, body

    def _send(self, typ, body):
        with self.wlock:
            if self.wkey:
                import hmac
                alg, _ = self.mac
                mac = hmac.new(self.wmac, self.wseq.to_bytes(8, 'big') + bytes([typ, 3, 1]) + len(body).to_bytes(2, 'big') + body, alg).digest()
                self.wseq += 1
                body = self.wkey.crypt(body + mac)
            self.s.sendall(bytes([typ, 3, 1]) + len(body).to_bytes(2, 'big') + body)

    def _alert(self, code):
        try:
            self._send(21, bytes([2, code]))
        except OSError:
            pass

    def _handshake_msg(self):
        while True:
            if len(self.hbuf) >= 4:
                n = int.from_bytes(self.hbuf[1:4], 'big')
                if len(self.hbuf) >= 4 + n:
                    msg = bytes(self.hbuf[:4 + n])
                    del self.hbuf[:4 + n]
                    return msg
            typ, body = self._record()
            if typ == 21:
                raise TLSRefused('alert %s from the guest' % body.hex())
            if typ != 22:
                raise TLSRefused('unexpected record %d' % typ)
            self.hbuf += body

    def handshake(self):
        import hashlib
        self.hbuf = bytearray()
        hello = self._handshake_msg()
        if hello[0] != 1:
            raise TLSRefused('no ClientHello')
        self.hs += hello
        b = hello[4:]
        cver = b[0] << 8 | b[1]
        crandom = b[2:34]
        i = 34 + 1 + b[34]
        n = int.from_bytes(b[i:i + 2], 'big')
        suites = [int.from_bytes(b[i + 2 + k:i + 4 + k], 'big') for k in range(0, n, 2)]
        i += 2 + n
        i += 1 + b[i]                                   # compression methods
        reneg = 0x00ff in suites
        if i + 2 <= len(b):
            ext_end = i + 2 + int.from_bytes(b[i:i + 2], 'big')
            j = i + 2
            while j + 4 <= ext_end:
                et, el = int.from_bytes(b[j:j + 2], 'big'), int.from_bytes(b[j + 2:j + 4], 'big')
                reneg = reneg or et == 0xff01
                j += 4 + el
        suite = next((x for x in self.SUITES if x in suites), None)
        if cver < 0x0301 or suite is None:
            self._alert(40)
            raise TLSRefused('no common cipher (version %04x, suites %s)' % (cver, ','.join('%04x' % x for x in suites[:12])))
        self.mac = self.SUITES[suite]
        srandom = int(time.time()).to_bytes(4, 'big') + os.urandom(28)
        ext = b'\xff\x01\x00\x01\x00' if reneg else b''
        sh = b'\x03\x01' + srandom + b'\x00' + suite.to_bytes(2, 'big') + b'\x00' + ((len(ext).to_bytes(2, 'big') + ext) if ext else b'')
        certs = b''.join(len(d).to_bytes(3, 'big') + d for d in self.chain)
        out = b''
        for t, body in ((2, sh), (11, len(certs).to_bytes(3, 'big') + certs), (14, b'')):
            m = bytes([t]) + len(body).to_bytes(3, 'big') + body
            self.hs += m
            out += m
        self._send(22, out)
        cke = self._handshake_msg()
        if cke[0] != 16:
            raise TLSRefused('no ClientKeyExchange')
        self.hs += cke
        enc = cke[6:6 + int.from_bytes(cke[4:6], 'big')]
        k = self.rsa
        m = pow(int.from_bytes(enc, 'big'), k['d'], k['n']).to_bytes((k['n'].bit_length() + 7) // 8, 'big')
        pre = m[-48:]
        # PKCS#1 v1.5 checked without a separate failure path (RFC 5246 7.4.7.1): a bad block uses a random secret
        if not (m[0] == 0 and m[1] == 2 and m[-49] == 0 and 0 not in m[2:-49] and pre[:2] == bytes([cver >> 8, cver & 255])):
            pre = bytes([cver >> 8, cver & 255]) + os.urandom(46)
        master = prf10(pre, b'master secret', crandom + srandom, 48)
        alg, ml = self.mac
        kb = prf10(master, b'key expansion', srandom + crandom, 2 * ml + 32)
        cmac, smac, ckey, skey = kb[:ml], kb[ml:2 * ml], kb[2 * ml:2 * ml + 16], kb[2 * ml + 16:2 * ml + 32]
        typ, body = self._record()
        if typ != 20:
            raise TLSRefused('no ChangeCipherSpec')
        self.rkey, self.rmac = RC4(ckey), cmac
        fin = self._handshake_msg()
        want = prf10(master, b'client finished', hashlib.md5(self.hs).digest() + hashlib.sha1(self.hs).digest(), 12)
        if fin[0] != 20 or fin[4:] != want:
            self._alert(51)
            raise TLSRefused('the guest\'s Finished does not match')
        self.hs += fin
        self._send(20, b'\x01')
        self.wkey, self.wmac = RC4(skey), smac
        sv = prf10(master, b'server finished', hashlib.md5(self.hs).digest() + hashlib.sha1(self.hs).digest(), 12)
        self._send(22, b'\x14\x00\x00\x0c' + sv)

    def read(self):
        """Application data from the guest, b'' when it closed."""
        while True:
            try:
                typ, body = self._record()
            except TLSRefused:
                return b''
            if typ == 23:
                if body:
                    return body
            elif typ == 21:
                return b''
            elif typ == 22:                             # a renegotiation request: refused politely
                self._alert(100)

    def write(self, data):
        for i in range(0, len(data), 16384):
            self._send(23, data[i:i + 16384])

    def close(self):
        if not self.closed:
            self.closed = True
            try:
                self._send(21, b'\x01\x00')             # close_notify
            except OSError:
                pass


def make_ca(sdir):
    """The hypervisor's own certificate authority for the old guests' TLS (made once, kept on /hv/secure)."""
    os.makedirs(sdir, exist_ok=True)
    os.chmod(sdir, 0o700)
    crt = os.path.join(sdir, 'ca.crt')
    if not os.path.exists(crt):
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', os.path.join(sdir, 'ca.key'),
                        '-out', crt + '.new', '-not_before', stamp(-2), '-not_after', stamp(3650), '-sha256',
                        '-subj', '/O=Charon Hypervisor/CN=Charon Hypervisor TLS proxy for old Windows (this computer only)',
                        '-addext', 'basicConstraints=critical,CA:TRUE', '-addext', 'keyUsage=critical,keyCertSign,cRLSign'],
                       check=True, capture_output=True, timeout=120)
        os.replace(crt + '.new', crt)
    return crt


def main():
    a = dict(zip(sys.argv[1::2], sys.argv[2::2]))
    sdir = a.get('--dir', '/hv/secure')
    make_ca(sdir)
    p = Proxy(int(a.get('--port', '3128')), sdir, a.get('--log', '/run/charonhv/proxy.log'), a.get('--block', os.path.join(sdir, 'blocklist.txt')),
              a.get('--machine-dir'))
    for kind in ('pop', 'imap', 'smtp'):
        if a.get('--mail-' + kind):
            threading.Thread(target=p.serve_mail, args=(kind, int(a['--mail-' + kind])), daemon=True).start()
    p.serve()


if __name__ == '__main__':
    main()
