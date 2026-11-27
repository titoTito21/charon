# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: the remote modern browser for old guests (6r).

Research (work/r410-remote-browser-research.txt): WRP (Apache-2.0) and Browservice (MIT) send PICTURES of pages with
image maps - an old screen reader gets nothing from them; no existing project gives accessible HTML with forms. So this
is Charon's own: Alpine's chromium-headless-shell (Chromium, BSD-3-Clause; Alpine community package) runs in the
hypervisor under the unprivileged user "charonweb", driven over --remote-debugging-pipe (no network port), one browser
per machine with its own profile in /hv/machines/<id>/remote-profile (cookies and logins stay there, 0700, never logged).
The page the old guest gets is built from Chromium's ACCESSIBILITY TREE (Accessibility.getFullAXTree): headings, text,
links, lists, tables, and every text box, check box, list box and button as a REAL HTML 4.01 control in one form.
Following a link or pressing a button sends the click to Chromium (DOM.resolveNode + Runtime.callFunctionOn), typed
values are put into the page's own fields (focus, select, Input.insertText - also for script-driven fields), then
the new state of the page comes back as HTML.
Limits said on the page: DRM video (Netflix and the like) cannot play here - Widevine is not in Alpine's Chromium (musl)
and protected video is never passed into a guest; the modern browser mode (6s) plays it on the real screen."""
import html, json, os, pwd, re, select, shutil, subprocess, threading, time, urllib.parse

DRM_SITES = ('netflix.com', 'disneyplus.com', 'primevideo.com', 'max.com', 'hbomax.com', 'spotify.com', 'canalplus.com',
             'player.pl', 'polsatboxgo.pl', 'skyshowtime.com', 'tv.apple.com', 'crunchyroll.com', 'paramountplus.com')
TERMS_SITES = {'facebook.com': "Facebook's terms (section 3.2.3) forbid accessing it by automated means without its permission; "
                                'you use it here at your own risk.',
               'instagram.com': "Instagram's terms forbid automated access without permission; at your own risk.",
               'linkedin.com': "LinkedIn's terms forbid automated access; at your own risk."}
PKGS = ('chromium-headless-shell',)
USER = 'charonweb'


def binary():
    for b in ('chromium-headless-shell', 'chrome-headless-shell', '/usr/lib/chromium/chromium-headless-shell', '/usr/lib/chromium/chrome-headless-shell',
              '/usr/lib/chromium-headless-shell/chrome-headless-shell'):
        p = shutil.which(b) or (b if os.path.isfile(b) else None)
        if p:
            return p
    return None


def ensure_user():
    try:
        return pwd.getpwnam(USER)
    except KeyError:
        subprocess.run(['adduser', '-D', '-H', '-s', '/sbin/nologin', USER], capture_output=True)
        return pwd.getpwnam(USER)


def site_of(host, table):
    h = (host or '').lower()
    return next((s for s in table if h == s or h.endswith('.' + s)), None)


class CDP:
    """Chrome DevTools Protocol over the pipe (fd 3 = commands to Chromium, fd 4 = its answers, NUL-separated JSON)."""

    def __init__(self, argv, uid, gid):
        r_cmd, w_cmd = os.pipe()
        r_ans, w_ans = os.pipe()
        # The pipe ends become fd 3 and 4 by the shell's own redirection. Measured: pass_fds=(3, 4) with dup2 in a
        # preexec_fn failed with "[Errno 9] Bad file descriptor" inside the proxy process, where fd 3 and 4 were not
        # open (subprocess marks pass_fds inheritable before preexec_fn runs). The user switch is Popen's own.
        moves = []
        if w_ans == 3:      # fd 3 is taken by the answer pipe: move it away first
            moves.append('9>&3')
            w_src = 9
        else:
            w_src = w_ans
        moves += ['3<&%d' % r_cmd, '4>&%d' % w_src] + ['%d>&-' % f for f in {r_cmd, w_src} if f not in (3, 4)]
        wrap = ['/bin/sh', '-c', 'exec %s; exec "$@"' % ' '.join(moves), 'chromium'] + list(argv)
        self.proc = subprocess.Popen(wrap, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                     pass_fds=(r_cmd, w_ans), close_fds=True, user=uid, group=gid, extra_groups=[])
        os.close(r_cmd)
        os.close(w_ans)
        self.w, self.r = w_cmd, r_ans
        self.buf = b''
        self.n = 0
        self.events = []
        self.lock = threading.Lock()

    def call(self, method, timeout=30, session=None, **params):
        with self.lock:
            self.n += 1
            mid = self.n
            msg = {'id': mid, 'method': method, 'params': params}
            if session:
                msg['sessionId'] = session
            os.write(self.w, json.dumps(msg).encode() + b'\0')
            end = time.time() + timeout
            while time.time() < end:
                while b'\0' in self.buf:
                    one, self.buf = self.buf.split(b'\0', 1)
                    m = json.loads(one)
                    if m.get('id') == mid:
                        if 'error' in m:
                            raise RuntimeError(m['error'].get('message', 'error'))
                        return m.get('result', {})
                    if 'method' in m:
                        self.events.append(m['method'])
                        del self.events[:-50]
                r, _, _ = select.select([self.r], [], [], 0.5)
                if r:
                    d = os.read(self.r, 1 << 20)
                    if not d:
                        raise RuntimeError('the browser ended')
                    self.buf += d
            raise RuntimeError('the browser did not answer %s' % method)

    def close(self):
        try:
            self.proc.terminate()
            self.proc.wait(5)
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass


class RemoteBrowser:
    def __init__(self, mdir, log):
        self.profile = os.path.join(mdir, 'remote-profile')
        self.log = log
        self.cdp = None
        self.session = None
        self.lock = threading.Lock()
        self.fields = {}

    # ---- the browser ----------------------------------------------------------------------------------------
    def start(self):
        if self.cdp and self.cdp.proc.poll() is None:
            return
        b = binary()
        if not b:
            raise RuntimeError('the remote browser is not installed yet (Charon Hypervisor menu, the machine, Retro assistant)')
        pw = ensure_user()
        os.makedirs(self.profile, exist_ok=True)
        os.chmod(self.profile, 0o700)
        for root, dirs, files in os.walk(self.profile):
            for n in dirs + files:
                try:
                    os.lchown(os.path.join(root, n), pw.pw_uid, pw.pw_gid)
                except OSError:
                    pass
        os.chown(self.profile, pw.pw_uid, pw.pw_gid)
        argv = [b, '--headless', '--remote-debugging-pipe', '--user-data-dir=' + self.profile, '--no-first-run',
                '--no-sandbox', '--disable-gpu', '--mute-audio', '--disable-dev-shm-usage', '--window-size=1280,2000',
                '--force-renderer-accessibility', '--lang=pl-PL,en-US', 'about:blank']
        self.cdp = CDP(argv, pw.pw_uid, pw.pw_gid)
        targets = self.cdp.call('Target.getTargets')['targetInfos']
        page = next((t for t in targets if t['type'] == 'page'), None)
        tid = page['targetId'] if page else self.cdp.call('Target.createTarget', url='about:blank')['targetId']
        self.session = self.cdp.call('Target.attachToTarget', targetId=tid, flatten=True)['sessionId']
        for d in ('Page.enable', 'DOM.enable', 'Accessibility.enable', 'Runtime.enable'):
            self.cdp.call(d, session=self.session)
        # 6v: a link the guest follows that is a download is saved here; the hypervisor scans it and gives it to the
        # machine's read-only Downloads share (hvshare)
        try:
            dl = '/hv/remote-downloads'
            os.makedirs(dl, exist_ok=True)
            os.chown(dl, pw.pw_uid, pw.pw_gid)
            self.cdp.call('Browser.setDownloadBehavior', behavior='allow', downloadPath=dl)
        except Exception as e:
            self.log('REMOTE', 'downloads not set: %r' % e)
        self.log('REMOTE', 'browser started')

    def call(self, method, timeout=30, **p):
        return self.cdp.call(method, timeout, self.session, **p)

    def stop(self):
        if self.cdp:
            self.cdp.close()
        self.cdp = None

    def settle(self, seconds=8.0):
        """Wait for the page to load and its scripts to calm down (no load event needed: single-page sites)."""
        end = time.time() + seconds
        time.sleep(0.8)
        last = None
        while time.time() < end:
            try:
                st = self.call('Runtime.evaluate', 5, expression='document.readyState + ":" + document.body.innerText.length',
                               returnByValue=True)['result'].get('value', '')
            except RuntimeError:
                st = ''
            if st.startswith('complete') and st == last:
                return
            last = st
            time.sleep(0.7)

    # ---- actions ----------------------------------------------------------------------------------------------
    def node_call(self, backend, fn, *args):
        obj = self.call('DOM.resolveNode', backendNodeId=int(backend))['object']['objectId']
        return self.call('Runtime.callFunctionOn', objectId=obj, functionDeclaration=fn,
                         arguments=[{'value': a} for a in args], returnByValue=True, awaitPromise=False)

    def navigate(self, url):
        self.call('Page.navigate', url=url)
        self.settle()

    def set_text(self, backend, value):
        self.node_call(backend, 'function(){this.scrollIntoView({block:"center"});this.focus();'
                                'if(this.select)this.select();else{var r=document.createRange();r.selectNodeContents(this);'
                                'var s=getSelection();s.removeAllRanges();s.addRange(r);}}')
        if value:
            self.call('Input.insertText', text=value)
        else:
            for t in ('keyDown', 'keyUp'):
                self.call('Input.dispatchKeyEvent', type=t, key='Delete', code='Delete', windowsVirtualKeyCode=46)

    def click(self, backend):
        self.node_call(backend, 'function(){this.scrollIntoView({block:"center"});this.click();}')
        self.settle()

    def set_checked(self, backend, on):
        self.node_call(backend, 'function(v){if(!!this.checked!==v)this.click();}', bool(on))

    def choose(self, backend, label):
        self.node_call(backend, 'function(v){for(var i=0;i<this.options.length;i++){if(this.options[i].text.trim()===v)'
                                '{this.selectedIndex=i;this.dispatchEvent(new Event("input",{bubbles:true}));'
                                'this.dispatchEvent(new Event("change",{bubbles:true}));return;}}}', label)

    # ---- the page as HTML from the accessibility tree --------------------------------------------------------
    def render(self, note=''):
        nodes = self.call('Accessibility.getFullAXTree', 60)['nodes']
        url = self.call('Runtime.evaluate', expression='location.href', returnByValue=True)['result'].get('value', '')
        byid = dict((n['nodeId'], n) for n in nodes)
        out = []
        self.fields = {}
        val = lambda n, k: (n.get(k) or {}).get('value', '')
        props = lambda n: dict((p['name'], (p.get('value') or {}).get('value')) for p in n.get('properties', []))

        def walk(n, depth):
            if depth > 200:
                return
            role = val(n, 'role')
            name = str(val(n, 'name') or '').strip()
            b = n.get('backendDOMNodeId')
            pr = props(n)
            kids = [byid[c] for c in n.get('childIds', []) if c in byid]
            esc = html.escape
            if n.get('ignored'):
                for k in kids:
                    walk(k, depth + 1)
                return
            if role in ('StaticText', 'text'):
                out.append(esc(name) + ' ')
                return
            if role == 'InlineTextBox':
                return
            if role == 'heading':
                lvl = min(6, max(1, int(pr.get('level') or 2)))
                out.append('<h%d>%s</h%d>\n' % (lvl, esc(name) or ' ', lvl))
                return
            if role == 'link' and b:
                out.append('<a href="/r/click?n=%d">%s</a> ' % (b, esc(name) or '(link)'))
                return
            if role in ('button', 'menuitem', 'tab', 'switch', 'PopUpButton', 'menuitemcheckbox', 'menuitemradio') and b:
                st = ' (%s)' % ('on' if pr.get('pressed') in ('true', True) or pr.get('checked') in ('true', True) else
                               'selected' if pr.get('selected') else '') if (pr.get('pressed') or pr.get('checked') or pr.get('selected')) else ''
                out.append('<input type="submit" name="b%d" value="%s%s"> ' % (b, esc(name or role), esc(st)))
                return
            if role in ('textbox', 'searchbox', 'TextField', 'textField', 'combobox') and b and not kids_are_options(kids):
                v = str(val(n, 'value') or '')
                self.fields['t%d' % b] = v
                lab = esc(name or role)
                if pr.get('multiline'):
                    out.append('<label for="t%d">%s</label><br><textarea id="t%d" name="t%d" rows="5" cols="60">%s</textarea><br>\n' % (b, lab, b, b, esc(v)))
                else:
                    pw = 'password' if 'password' in name.lower() or 'hasło' in name.lower() or pr.get('protected') else 'text'
                    out.append('<label for="t%d">%s</label> <input type="%s" id="t%d" name="t%d" size="40" value="%s"><br>\n'
                               % (b, lab, pw, b, b, '' if pw == 'password' else esc(v)))
                return
            if role in ('checkbox', 'radio') and b:
                on = pr.get('checked') in ('true', True, 'mixed')
                self.fields['c%d' % b] = 'on' if on else ''
                out.append('<input type="checkbox" id="c%d" name="c%d"%s> <label for="c%d">%s</label><br>\n' % (b, b, ' checked' if on else '', b, esc(name)))
                return
            if role in ('combobox', 'listbox') and b:
                opts = [str(val(k, 'name') or '').strip() for k in descend(kids) if val(k, 'role') in ('option', 'MenuListOption', 'menuitem')]
                cur = str(val(n, 'value') or '')
                self.fields['s%d' % b] = cur
                out.append('<label for="s%d">%s</label> <select id="s%d" name="s%d">%s</select><br>\n' % (
                    b, esc(name or role), b, b, ''.join('<option%s>%s</option>' % (' selected' if o == cur else '', esc(o)) for o in opts)))
                return
            if role == 'image' or role == 'img':
                if name:
                    out.append('[%s] ' % esc(name))
                return
            tag = {'list': 'ul', 'listitem': 'li', 'paragraph': 'p', 'table': 'table', 'row': 'tr', 'cell': 'td',
                   'gridcell': 'td', 'columnheader': 'th', 'rowheader': 'th', 'blockquote': 'blockquote',
                   'navigation': 'div', 'main': 'div', 'banner': 'div', 'contentinfo': 'div', 'article': 'div',
                   'region': 'div', 'complementary': 'div', 'form': 'div', 'dialog': 'div', 'alertdialog': 'div'}.get(role)
            if role in ('navigation', 'main', 'banner', 'contentinfo', 'complementary', 'region', 'dialog', 'alertdialog', 'article') and name:
                out.append('<h6>%s: %s</h6>\n' % (esc(role), esc(name)))
            elif role in ('navigation', 'main', 'banner', 'contentinfo', 'complementary', 'dialog', 'alertdialog'):
                out.append('<h6>%s</h6>\n' % esc(role))
            if tag:
                out.append('<%s>' % tag)
            for k in kids:
                walk(k, depth + 1)
            if tag:
                out.append('</%s>\n' % tag)
            elif role in ('generic', 'group', 'section', 'LineBreak'):
                out.append('<br>\n' if role == 'LineBreak' else '\n')

        def descend(ks):
            for k in ks:
                yield k
                for x in descend([byid[c] for c in k.get('childIds', []) if c in byid]):
                    yield x

        def kids_are_options(ks):
            return any(val(k, 'role') in ('option', 'MenuListOption') for k in descend(ks))

        root = next((n for n in nodes if val(n, 'role') == 'RootWebArea'), nodes[0] if nodes else None)
        title = str(val(root, 'name') or url) if root else url
        if root:
            walk(root, 0)
        body = ''.join(out)
        body = re.sub(r'(\s*<br>\s*){3,}', '<br><br>\n', body)
        host = urllib.parse.urlsplit(url).hostname
        warn = ''
        if site_of(host, DRM_SITES):
            self.log('NEEDMODERN', host)
            warn += ('<p><strong>Video of this service is protected (DRM): it cannot play here or in this old Windows. Open the '
                     'modern browser of Charon Hypervisor on the real screen for it.</strong><br><span lang="pl">Wideo tej usługi jest '
                     'chronione (DRM): nie zagra tutaj ani w tym starym Windows. Otwórz nowoczesną przeglądarkę Charon Hypervisor na '
                     'prawdziwym ekranie.</span></p>')
        t = site_of(host, TERMS_SITES)
        if t:
            self.log('TERMS', host)
            warn += '<p><em>%s</em></p>' % html.escape(TERMS_SITES[t])
        form = ('<form method="post" action="/r/act">%s%s<p><input type="submit" name="send" value="Send typed text / Wyślij wpisany tekst"> '
                '<a href="/r/reload">Read again / Odczytaj ponownie</a> | <a href="/r/back">Back / Wstecz</a></p></form>'
                % (('<p>%s</p>' % html.escape(note)) if note else '', body))
        bar = ('<form method="get" action="/r"><label for="u">Remote browser address / Adres w zdalnej przeglądarce:</label> '
               '<input id="u" name="u" size="50" value="%s"> <input type="submit" value="Go / Idź"></form>'
               '<p><a href="/">Charon simple web</a></p>' % html.escape(url))
        self.log('REMOTE', host or '')
        return ('<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">\n<html><head><meta http-equiv="Content-Type" '
                'content="text/html; charset=utf-8"><title>%s</title></head><body>\n%s%s<hr>\n%s\n</body></html>\n'
                % (html.escape(title), bar, warn, form)).encode('utf-8')

    # ---- requests from the guest -----------------------------------------------------------------------------
    def handle(self, method, path, body, is_blocked=None):
        with self.lock:
            try:
                return self._handle(method, path, body, is_blocked)
            except Exception as e:
                self.log('REMOTEFAIL', str(e)[:200])
                if self.cdp and self.cdp.proc.poll() is not None:
                    self.stop()
                msg = html.escape(str(e)[:300])
                return 502, 'text/html; charset=utf-8', ('<html><body><h1>The remote browser failed / Zdalna przeglądarka zawiodła</h1>'
                                                         '<p>%s</p><p><a href="/">Charon simple web</a></p></body></html>' % msg).encode('utf-8')

    def _handle(self, method, path, body, is_blocked):
        sp = urllib.parse.urlsplit(path)
        q = urllib.parse.parse_qs(sp.query, keep_blank_values=True)
        self.start()
        if sp.path == '/r':
            u = q.get('u', [''])[0].strip()
            if not re.match(r'^https?://', u, re.I):
                u = 'https://' + u
            host = urllib.parse.urlsplit(u).hostname or ''
            if is_blocked and is_blocked(host):
                self.log('BLOCKED', host)
                return 403, 'text/html; charset=utf-8', b'<html><body><h1>Blocked by Charon Hypervisor</h1></body></html>'
            self.navigate(u)
            return 200, 'text/html; charset=utf-8', self.render()
        if sp.path == '/r/click':
            self.click(int(q['n'][0]))
            return 200, 'text/html; charset=utf-8', self.render()
        if sp.path == '/r/back':
            self.call('Runtime.evaluate', expression='history.back()')
            self.settle()
            return 200, 'text/html; charset=utf-8', self.render()
        if sp.path == '/r/act':
            form = urllib.parse.parse_qs(body.decode('utf-8', 'replace'), keep_blank_values=True)
            old = dict(self.fields)
            typed = None
            for k, v in old.items():           # only what the user changed is put into the page
                new = form.get(k, [''])[0]
                if k.startswith('t') and new != v and not (new == '' and v):
                    self.set_text(int(k[1:]), new)
                    typed = int(k[1:])
                elif k.startswith('t') and new == '' and v and k in form:
                    self.set_text(int(k[1:]), '')
                elif k.startswith('c') and (new == 'on') != (v == 'on'):
                    self.set_checked(int(k[1:]), new == 'on')
                elif k.startswith('s') and new != v:
                    self.choose(int(k[1:]), new)
            pressed = [k for k in form if re.match(r'^b\d+$', k)]
            if pressed:
                self.click(int(pressed[0][1:]))
            elif typed is not None:         # no button: Enter in the last field typed into (a search box, a chat line)
                self.node_call(typed, 'function(){this.focus();}')
                for t in ('keyDown', 'keyUp'):
                    self.call('Input.dispatchKeyEvent', type=t, key='Enter', code='Enter', windowsVirtualKeyCode=13,
                              **({'text': chr(13)} if t == 'keyDown' else {}))
                self.settle()
            else:
                self.settle(3)
            return 200, 'text/html; charset=utf-8', self.render()
        self.settle(2)
        return 200, 'text/html; charset=utf-8', self.render()
