# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: "simple web" for old guests (6q) - in the spirit of FrogFind and text-only proxies.

The old browser asks the guest's proxy for http://simple.charon/ (or any site the user put on this machine's simple
list): the hypervisor fetches the page itself over modern, VERIFIED TLS and returns light HTML 4.01 that Internet
Explorer 5/6/8, Netscape and old screen readers handle - headings, paragraphs, lists, tables, links, and forms as real
HTML controls; no scripts, styles, frames or images (their alt text stays). Links and forms lead back through here.
Search: DuckDuckGo's HTML page (html.duckduckgo.com, made for browsers without JavaScript).
Cookies of the sites are kept per machine on the Hypervisor partition (0600); nothing of a page, a form or a cookie is
ever logged - the log has the host name only. Sites that need JavaScript go to the remote modern browser (hvremote)."""
import html, html.parser, http.cookiejar, os, re, ssl, urllib.parse, urllib.request

UA = 'Mozilla/5.0 (X11; Linux x86_64; rv:140.0) Gecko/20100101 Firefox/140.0 CharonSimpleWeb/1'
HOSTS = ('simple.charon', 'charon', 'www.simple.charon')
SEARCH = 'https://html.duckduckgo.com/html/?q='
KEEP = {'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'p', 'br', 'ul', 'ol', 'li', 'dl', 'dt', 'dd', 'a', 'form', 'input', 'select',
        'option', 'textarea', 'button', 'label', 'table', 'tr', 'td', 'th', 'caption', 'blockquote', 'pre', 'code', 'strong',
        'em', 'b', 'i', 'hr', 'fieldset', 'legend', 'sup', 'sub', 'abbr', 'q', 'cite'}
DROP = {'script', 'style', 'svg', 'template', 'head', 'math', 'canvas', 'video', 'audio', 'object', 'iframe', 'noembed', 'select-menu'}
BLOCKS = {'div', 'section', 'article', 'main', 'header', 'footer', 'nav', 'aside', 'figure', 'figcaption', 'details', 'summary'}
ATTRS = {'name', 'value', 'type', 'checked', 'selected', 'size', 'maxlength', 'rows', 'cols', 'alt', 'title', 'href',
         'action', 'method', 'for', 'id', 'colspan', 'rowspan', 'multiple', 'lang', 'disabled', 'readonly'}
VOID = {'br', 'hr', 'input'}
INPUT_TYPES = {'text', 'password', 'hidden', 'checkbox', 'radio', 'submit', 'reset', 'file'}


def link(u, prefix='/?u='):
    return prefix + urllib.parse.quote(u, safe='')


class Simplifier(html.parser.HTMLParser):
    def __init__(self, base, js_prefix):
        super().__init__(convert_charrefs=True)
        self.base, self.js = base, js_prefix
        self.out, self.title, self.skip, self.in_title, self.forms, self.text_len = [], '', 0, False, 0, 0
        self.stack = []

    def absu(self, u):
        try:
            return urllib.parse.urljoin(self.base, u.strip())
        except ValueError:
            return ''

    def handle_starttag(self, tag, attrs):
        a = dict((k.lower(), v or '') for k, v in attrs)
        if tag == 'base' and a.get('href'):
            self.base = self.absu(a['href'])
        if tag == 'title':
            self.in_title = True
            return
        if tag == 'meta' and a.get('http-equiv', '').lower() == 'refresh' and 'url=' in a.get('content', '').lower():
            u = self.absu(a['content'].split('=', 1)[1].strip('\'" '))
            self.out.append('<p><a href="%s">%s</a></p>' % (html.escape(link(u)), 'Continue / Dalej'))
        if self.skip or tag in DROP:
            if tag not in VOID and tag in DROP:
                self.skip += 1
            return
        if tag == 'img':
            alt = a.get('alt', '').strip()
            if alt:
                self.out.append(' [%s] ' % html.escape(alt))
            return
        if tag in BLOCKS:
            self.out.append('\n<br>')
            return
        if tag not in KEEP:
            return
        keep = {}
        for k, v in a.items():
            if k not in ATTRS:
                continue
            if k == 'href':
                u = self.absu(v)
                if u.lower().startswith(('http://', 'https://')):
                    keep[k] = link(u)
                elif u.lower().startswith('mailto:'):
                    keep[k] = u
                continue
            keep[k] = v
        if tag == 'form':
            self.forms += 1
            act = self.absu(a.get('action', '') or self.base)
            keep['action'] = '/f?u=' + urllib.parse.quote(act, safe='') + '&m=' + ('post' if a.get('method', '').lower() == 'post' else 'get')
            keep['method'] = 'post' if a.get('method', '').lower() == 'post' else 'get'
        if tag == 'input':
            t = a.get('type', 'text').lower()
            keep['type'] = t if t in INPUT_TYPES else ('submit' if t in ('image', 'button') else 'text')
            if t == 'image' and not keep.get('value'):
                keep['value'] = a.get('alt', 'Submit')
        if tag == 'button':
            t = a.get('type', 'submit').lower()
            keep['type'] = 'submit' if t not in ('reset', 'button') else t
        if tag == 'form' and keep['method'] == 'get':
            keep['action'] = '/f'           # a GET form replaces the action's query: the target goes in hidden fields
            self.out.append('<form method="get" action="/f"><input type="hidden" name="u" value="%s"><input type="hidden" name="m" value="get">'
                            % html.escape(act, quote=True))
            return
        self.out.append('<%s%s>' % (tag, ''.join(' %s="%s"' % (k, html.escape(v, quote=True)) for k, v in keep.items())))

    def handle_endtag(self, tag):
        if tag == 'title':
            self.in_title = False
            return
        if tag in DROP:
            self.skip = max(0, self.skip - 1)
            return
        if self.skip:
            return
        if tag in BLOCKS:
            self.out.append('\n')
        elif tag in KEEP and tag not in VOID:
            self.out.append('</%s>' % tag)

    def handle_data(self, data):
        if self.in_title:
            self.title += data
            return
        if self.skip:
            return
        t = re.sub(r'\s+', ' ', data)
        if t.strip():
            self.text_len += len(t.strip())
        self.out.append(html.escape(t))


def page(title, body, lang='en', url=''):
    bar = ('<form method="get" action="/"><label for="u">Address / Adres:</label> <input id="u" name="u" size="50" value="%s"> '
           '<input type="submit" value="Go / Idź"></form>' % html.escape(url))
    tools = ''
    if url:
        tools = ('<p><a href="/">Charon simple web</a> | <a href="%s">Open in the remote modern browser / W zdalnej nowoczesnej '
                 'przeglądarce</a></p>' % html.escape(link(url, '/r?u=')))
    return ('<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">\n<html lang="%s"><head>'
            '<meta http-equiv="Content-Type" content="text/html; charset=utf-8"><title>%s</title></head><body>\n%s%s<hr>\n%s\n'
            '</body></html>\n' % (lang, html.escape(title), bar, tools, body)).encode('utf-8')


def front(lang='en'):
    body = ('<h1>Charon simple web / Prosta sieć Charona</h1>'
            '<p>Pages come through Charon Hypervisor as light HTML: text, links and forms, no scripts or pictures.<br>'
            '<span lang="pl">Strony przychodzą przez Charon Hypervisor jako lekki HTML: tekst, odnośniki i formularze, bez skryptów i obrazów.</span></p>'
            '<form method="get" action="/s"><label for="q">Search / Szukaj:</label> <input id="q" name="q" size="40"> '
            '<input type="submit" value="Search / Szukaj"></form>'
            '<p>Sites that need JavaScript: open them in the remote modern browser (address box, then the link under it).<br>'
            '<span lang="pl">Strony wymagające JavaScriptu: otwórz w zdalnej nowoczesnej przeglądarce.</span></p>')
    return page('Charon simple web', body, lang)


class SimpleWeb:
    def __init__(self, mdir, log):
        self.dir = os.path.join(mdir, 'web')
        os.makedirs(self.dir, mode=0o700, exist_ok=True)
        self.log = log
        self.jar = http.cookiejar.MozillaCookieJar(os.path.join(self.dir, 'cookies.txt'))
        try:
            self.jar.load(ignore_discard=True)
        except (OSError, http.cookiejar.LoadError):
            pass
        ctx = ssl.create_default_context()
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar), urllib.request.HTTPSHandler(context=ctx))
        self.opener.addheaders = [('User-Agent', UA), ('Accept-Language', 'pl,en;q=0.7')]

    def sites(self):
        try:
            return set(l.strip().lower() for l in open(os.path.join(self.dir, 'simple-sites.txt')) if l.strip())
        except OSError:
            return set()

    def wants(self, host):
        h = host.lower()
        return any(h == s or h.endswith('.' + s) for s in self.sites())

    def fetch(self, url, data=None, is_blocked=None):
        host = urllib.parse.urlsplit(url).hostname or ''
        if is_blocked and is_blocked(host):
            raise PermissionError(host)
        req = urllib.request.Request(url, data=data)
        if data is not None:
            req.add_header('Content-Type', 'application/x-www-form-urlencoded')
        r = self.opener.open(req, timeout=30)
        body = r.read(30 * 1048576)
        try:
            self.jar.save(ignore_discard=True)
            os.chmod(self.jar.filename, 0o600)
        except OSError:
            pass
        self.log('SIMPLE', host)
        return r.geturl(), r.headers.get('Content-Type', ''), body

    def render(self, url, data=None, is_blocked=None):
        """(status, content type, bytes, needs_js)."""
        try:
            final, ctype, body = self.fetch(url, data, is_blocked)
        except PermissionError as e:
            return 403, 'text/html; charset=utf-8', page('Blocked', '<h1>Blocked by Charon Hypervisor: %s</h1>' % html.escape(str(e))), False
        except Exception as e:
            reason = getattr(e, 'reason', None) or e
            self.log('SIMPLEFAIL', '%s %s' % (urllib.parse.urlsplit(url).hostname, str(reason)[:120]))
            return 502, 'text/html; charset=utf-8', page('Not reached', '<h1>The page could not be fetched / Nie udało się pobrać strony</h1><p>%s</p>'
                                                         % html.escape(str(reason)[:300]), url=url), False
        if 'html' not in ctype.lower() and not body.lstrip()[:15].lower().startswith((b'<!doctype', b'<html')):
            return 200, ctype or 'application/octet-stream', body, False
        cs = re.search(r'charset=([\w-]+)', ctype, re.I) or re.search(rb'<meta[^>]+charset=["\']?([\w-]+)', body[:4096], re.I)
        enc = cs.group(1) if cs else 'utf-8'
        try:
            text = body.decode(enc.decode() if isinstance(enc, bytes) else enc, 'replace')
        except LookupError:
            text = body.decode('utf-8', 'replace')
        if urllib.parse.urlsplit(final).hostname == 'html.duckduckgo.com':
            text = re.sub(r'href="(?:https?:)?//duckduckgo\.com/l/\?uddg=([^&"]+)[^"]*"',
                          lambda m: 'href="%s"' % html.escape(urllib.parse.unquote(m.group(1))), text)
        s = Simplifier(final, '/r?u=')
        try:
            s.feed(text)
            s.close()
        except Exception:
            pass
        body = re.sub(r'(\s*<br>\s*){3,}', '\n<br><br>\n', ''.join(s.out))
        needs_js = s.text_len < 200
        if needs_js:
            body = ('<p><strong>This page shows little without JavaScript. <a href="%s">Open it in the remote modern browser</a>.</strong><br>'
                    '<span lang="pl">Ta strona bez JavaScriptu pokazuje mało. <a href="%s">Otwórz w zdalnej nowoczesnej przeglądarce</a>.</span></p>'
                    % (html.escape(link(final, '/r?u=')), html.escape(link(final, '/r?u=')))) + body
            self.log('NEEDJS', urllib.parse.urlsplit(final).hostname or '')
        return 200, 'text/html; charset=utf-8', page(s.title.strip() or final, body, url=final), needs_js

    def handle(self, method, path, body, is_blocked=None):
        """A request to http://simple.charon/...: (status, content type, bytes)."""
        sp = urllib.parse.urlsplit(path)
        q = urllib.parse.parse_qs(sp.query, keep_blank_values=True)
        if sp.path == '/s':
            return self.render(SEARCH + urllib.parse.quote_plus(q.get('q', [''])[0]), None, is_blocked)[:3]
        if sp.path == '/f':
            target = q.get('u', [''])[0]
            fields = urllib.parse.parse_qsl(body.decode('utf-8', 'replace') if method == 'POST' else sp.query, keep_blank_values=True)
            fields = [(k, v) for k, v in fields if not (method != 'POST' and k in ('u', 'm'))]
            enc = urllib.parse.urlencode(fields).encode()
            if q.get('m', ['get'])[0] == 'post':
                return self.render(target, enc, is_blocked)[:3]
            sep = '&' if '?' in target else '?'
            return self.render(target.split('#')[0] + (sep + enc.decode() if fields else ''), None, is_blocked)[:3]
        u = q.get('u', [''])[0].strip()
        if not u:
            return 200, 'text/html; charset=utf-8', front()
        if not re.match(r'^https?://', u, re.I):
            u = ('https://' + u) if '.' in u and ' ' not in u else SEARCH + urllib.parse.quote_plus(u)
        return self.render(u, None, is_blocked)[:3]
