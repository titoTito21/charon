# SPDX-License-Identifier: GPL-2.0-or-later
# Charon Hypervisor: the guest screen reader for guests without a reader of their own.
# Text mode (DOS, Linux consoles, the text phase of Windows Setup): the VGA text buffer at 0xB8000 read with QEMU's
# pmemsave - the exact characters, no OCR. Graphic screens: QEMU's screendump read with tesseract (eng+pol).
import os, re, subprocess, time

CP437 = bytes(range(256)).decode('cp437')
# Polish Windows/DOS text screens use code page 852
CP852 = bytes(range(256)).decode('cp852')


def ppm_size(path):
    with open(path, 'rb') as f:
        head = f.read(64)
    m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', head)
    return (int(m.group(1)), int(m.group(2))) if m else (0, 0)


SCREEN_BIN = '/hv/voice/charonhv-screen'


def screen_model(cells, cursor, counting, name='-'):
    """Charon's screen model (charonhv-screen = CharonScreenModel.h) of a text screen: {key: value} or None when the
    program is not built (yet). Keys: manager, screen, title, list.*, item.N, item.N.flags, countdown.*, help.N ..."""
    if not os.path.exists(SCREEN_BIN):
        return None
    text, attrs = cells
    cr, cc = cursor if cursor else (0, 0)
    inp = '%d %d %d %d %d %d %s\n' % (len(text), len(text[0]) if text else 80, cr, cc, 1 if cursor else 0, 1 if counting else 0, name)
    inp += '\n'.join(text) + '\n' + '\n'.join(attrs) + '\n'
    try:
        r = subprocess.run([SCREEN_BIN], input=inp.encode('utf-8'), capture_output=True, timeout=10)
    except Exception:
        return None
    if r.returncode:
        return None
    out = {}
    for line in r.stdout.decode('utf-8', 'replace').splitlines():
        k, _, v = line.partition('=')
        out[k] = v
    return out


def highlighted_rows(attrs):
    """Rows of a text screen that carry a highlight (Speakup's highlight tracking): at least 4 cells whose background
    differs from the screen's most common background. attrs: one hex string per row (2 digits per cell)."""
    count = {}
    rows = []
    for a in attrs:
        bgs = [(int(a[i:i + 2], 16) >> 4) & 7 for i in range(0, len(a), 2)]
        rows.append(bgs)
        for b in bgs:
            count[b] = count.get(b, 0) + 1
    if not count:
        return ()
    dom = max(count, key=count.get)
    return tuple(r for r, bgs in enumerate(rows) if sum(1 for b in bgs if b != dom) >= 4 and sum(1 for b in bgs if b != dom) < len(bgs))


def cursor_speech(lines, old_cur, cur):
    """What Speakup says when the cursor moves on an unchanged screen: the line when the row changes, the character
    for a one-column step, otherwise the word under the cursor. None when it did not move."""
    if not cur or not old_cur or cur == old_cur:
        return None
    r, c = cur
    line = lines[r] if r < len(lines) else ''
    if r != old_cur[0]:
        return line.strip() or ''
    if abs(c - old_cur[1]) == 1:
        return line[c] if c < len(line) else ' '
    m = [w for w in re.finditer(r'\S+', line) if w.start() <= c < w.end()]
    return m[0].group(0) if m else ' '


class GuestScreen:
    def __init__(self, machine, polish=False):
        self.m = machine
        self.polish = polish
        self.work = '/run/charonhv/screen-%s' % machine.id
        self.lines = []
        self.mode = 'unknown'
        self.model = None
        self.cells = None
        self.cursor = None
        self.seconds = []          # the last countdown values seen (a falling number = counting down)

    def text_shape(self, ppm, w, h, raw):
        """(cols, rows) when the guest shows a VGA text mode, else None. The picture's size alone does not tell it
        (r410: XP's "Advanced Options" menu is 80x30 text = a 640x480 picture, like a graphic screen), so the text
        buffer at 0xB8000 is compared with the picture: a cell holds a character exactly where its block of pixels
        is not all background. No guest register is touched."""
        try:
            d = open(ppm, 'rb').read()
            hd = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', d)
            px = d[hd.end():]
        except Exception:
            return None
        bg = px[0:3]
        for cw in (9, 8):
            if w % cw or w // cw != 80:
                continue
            for chh in (16, 14, 8):
                if h % chh:
                    continue
                cols, rows = 80, h // chh
                try:
                    self.m.qmp.hmp('pmemsave 0xb8000 %d %s' % (cols * rows * 2, raw))
                    data = open(raw, 'rb').read()
                except Exception:
                    return None
                if len(data) < cols * rows * 2:
                    continue
                agree = marked = 0
                blank_row = bg * cw
                for r in range(rows):
                    for c in range(cols):
                        ch = data[(r * cols + c) * 2]
                        a = data[(r * cols + c) * 2 + 1]
                        text_cell = ch not in (0, 32, 255) or (a >> 4) & 7 != 0
                        x = c * cw * 3
                        ink = any(px[((r * chh + y) * w) * 3 + x:((r * chh + y) * w) * 3 + x + cw * 3] != blank_row for y in range(0, chh, 2))
                        agree += text_cell == ink
                        marked += text_cell
                if marked >= 10 and agree >= cols * rows * 0.97:
                    return cols, rows
        return None

    def counting(self):
        return len(self.seconds) >= 2 and self.seconds[-1] < self.seconds[-2]

    def read(self):
        """The guest's screen as lines; also sets .mode to text or graphic."""
        ppm = self.work + '.ppm'
        try:
            self.m.screendump(ppm)
        except Exception:
            return self.lines
        w, h = ppm_size(ppm)
        raw = self.work + '.txt'
        shape = self.text_shape(ppm, w, h, raw)
        if shape:
            self.mode = 'text'
            cols, rows = shape
            data = open(raw, 'rb').read()
            table = CP852 if self.polish else CP437
            out, attrs = [], []
            for r in range(rows):
                row = data[r * cols * 2:(r + 1) * cols * 2:2]
                out.append(''.join(table[b] for b in row).rstrip())
                attrs.append(data[r * cols * 2 + 1:(r + 1) * cols * 2:2].hex())
            self.lines = out
            self.cells = ([''.join(table[b] for b in data[r * cols * 2:(r + 1) * cols * 2:2]) for r in range(rows)], attrs)
            # the BIOS's cursor of page 0 (0x450: column, row) - right for BIOS screens, DOS and boot managers
            try:
                self.m.qmp.hmp('pmemsave 0x450 2 %s' % (raw + '.cur'))
                c = open(raw + '.cur', 'rb').read()
                self.cursor = (c[1], c[0]) if len(c) == 2 and c[1] < rows and c[0] < cols else None
            except Exception:
                self.cursor = None
            self.model = screen_model(self.cells, self.cursor, self.counting())
        else:
            self.mode = 'graphic'
            self.model = None
            self.lines = self._ocr(ppm)
        if key_page(self.lines):
            self.lines = [redact_key(l) for l in self.lines]
        return self.lines

    def _ocr(self, ppm):
        """Two passes: the picture as it is (dark text on light) and inverted (light text on dark: title bars, the
        taskbar, desktop icon labels, XP's welcome screen tiles - r410: the first pass alone missed them). Words
        below 40 % confidence are dropped; lines of both passes are merged in screen order (top, then left), a line
        of the inverted pass only where the first pass found nothing at that place."""
        big, inv = self.work + '-2x.ppm', self.work + '-2xi.ppm'
        try:
            subprocess.run(['python3', '-c', SCALE, ppm, big, inv], timeout=30, check=True, capture_output=True)
            passes = [big, inv]
        except Exception:
            passes = [ppm]
        found = []
        for k, src in enumerate(passes):
            try:
                r = subprocess.run(['tesseract', src, '-', '-l', 'pol+eng' if self.polish else 'eng+pol', '--psm', '11' if k else '3', 'tsv'],
                                   capture_output=True, text=True, timeout=90)
            except Exception:
                continue
            for line in tsv_lines(r.stdout, 60 if k else 40):
                # the light-text pass also sees icons and gradients: keep a line only with a real word or a time
                if k and (any(overlap(line[1], f[1]) for f in found)
                          or not re.search(r'\w{3,}|\d{1,2}:\d{2}', line[0])):
                    continue
                found.append(line)
        if not found:
            return self.lines
        found.sort(key=lambda f: (f[1][1] // 12, f[1][0]))
        return [f[0] for f in found]


def tsv_lines(tsv, min_conf=40):
    """tesseract TSV -> [(text, (left, top, right, bottom))] per text line, words under min_conf dropped."""
    lines = {}
    for row in tsv.splitlines()[1:]:
        p = row.split('\t')
        if len(p) < 12 or p[0] != '5':
            continue
        try:
            conf = float(p[10])
        except ValueError:
            continue
        word = p[11].strip()
        if not word or conf < min_conf:
            continue
        key = (p[2], p[3], p[4])
        l, t, w, h = int(p[6]), int(p[7]), int(p[8]), int(p[9])
        if key in lines:
            words, (L, T, R, B) = lines[key]
            words.append(word)
            lines[key] = (words, (min(L, l), min(T, t), max(R, l + w), max(B, t + h)))
        else:
            lines[key] = ([word], (l, t, l + w, t + h))
    return [(' '.join(w), box) for w, box in lines.values()]


def overlap(a, b):
    """Two boxes share more than half of the smaller one."""
    w = min(a[2], b[2]) - max(a[0], b[0])
    h = min(a[3], b[3]) - max(a[1], b[1])
    if w <= 0 or h <= 0:
        return False
    small = min((a[2] - a[0]) * (a[3] - a[1]), (b[2] - b[0]) * (b[3] - b[1])) or 1
    return w * h * 2 > small


# Doubles a PPM (tesseract reads small UI fonts much better at 2x).
SCALE = r'''
import sys,re
d=open(sys.argv[1],'rb').read()
m=re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s',d)
w,h=int(m.group(1)),int(m.group(2));px=d[m.end():]
out=bytearray()
for y in range(h):
    row=px[y*w*3:(y+1)*w*3]
    wide=bytearray()
    for x in range(w):
        p=row[x*3:x*3+3];wide+=p+p
    out+=wide+wide
open(sys.argv[2],'wb').write(b'P6 %d %d 255\n'%(w*2,h*2)+bytes(out))
if len(sys.argv)>3:
    # light text on dark or coloured ground (title bars, taskbar, icon labels): light pixels black, the rest white
    mask=bytearray()
    for i in range(0,len(out),3):
        mask+=b'\x00\x00\x00' if out[i]+out[i+1]+out[i+2]>600 else b'\xff\xff\xff'
    open(sys.argv[3],'wb').write(b'P6 %d %d 255\n'%(w*2,h*2)+bytes(mask))
'''


# 6h: product-key pages of Windows Setup (XP/2000 GUI phase "Twój klucz produktu" / "Your Product Key", 98/Me
# "Product Key", Vista-11 "Enter the product key" / "Wprowadź klucz produktu", activation). Every one of them asks
# for 25 characters in 5 groups of 5 (Windows 95's 10/20-character CD keys are not handled: no such template).
KEY_PAGE = re.compile(r'klucz(a|em)? produktu|product key|wprowad[zź] klucz|enter the product key', re.I)
KEY_VOLUME = re.compile(r'licencji (zbiorczej|woluminów)|volume licen', re.I)
KEY_OEM = re.compile(r'\bOEM\b')
KEY_REFUSED = re.compile(r'(nieprawidłow|niepoprawn|nie jest prawidłow|is not valid|invalid|not valid)', re.I)


def key_page(lines):
    """None, or {fields, size, kind} when the screen is a product-key page."""
    text = ' '.join(lines)
    if not KEY_PAGE.search(text):
        return None
    kind = 'volume' if KEY_VOLUME.search(text) else 'oem' if KEY_OEM.search(text) else ''
    return dict(fields=5, size=5, kind=kind)


def redact_key(line):
    """On a key page the typed key is on the screen: groups of up to 5 letters/digits in a row of two or more such
    groups become '•••••' before anything is spoken, logged or kept for review (the key is never logged)."""
    tokens = re.split(r'([\s\-]+)', line)
    groups = [t for t in tokens if re.fullmatch(r'[A-Z0-9]{1,5}', t or '') and (re.search(r'\d', t) or len(t) == 5)]
    if len(groups) < 2:
        return line
    return ''.join('•' * len(t) if t in groups else t for t in tokens)


def key_refused(lines):
    """The whole refusal message (a line naming the key with 'not valid' and the lines right after it), or None."""
    for i, l in enumerate(lines):
        if KEY_REFUSED.search(l) and re.search(r'klucz|key', ' '.join(lines[max(0, i - 1):i + 2]), re.I):
            return ' '.join(x.strip() for x in lines[max(0, i - 1):i + 3] if x.strip())
    return None


def changes(old, new):
    """Lines of `new` that are new or changed against `old` (what Charon's reader says by itself)."""
    oldset = {}
    for l in old:
        oldset[l] = oldset.get(l, 0) + 1
    out = []
    for l in new:
        # a clock that ticks over (the taskbar's "17:06") is not news: Charon's reader does not read it by itself
        if not l.strip() or re.fullmatch(r'\s*\d{1,2}:\d{2}(:\d{2})?\s*', l):
            continue
        if oldset.get(l, 0):
            oldset[l] -= 1
        else:
            out.append(l)
    return out
