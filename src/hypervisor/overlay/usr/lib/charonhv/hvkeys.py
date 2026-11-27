# SPDX-License-Identifier: GPL-2.0-or-later
# Charon Hypervisor: the physical keyboard (evdev, grabbed), key names and QEMU qcodes.
import fcntl, os, queue, re, select, struct, threading, time

EVIOCGRAB = 0x40044590
EV_KEY = 1
EVENT = struct.Struct('llHHi')

# evdev code -> (name, qcode, char, shifted char)
_T = {
    1: ('Escape', 'esc'), 14: ('Backspace', 'backspace'), 15: ('Tab', 'tab'), 28: ('Enter', 'ret'),
    29: ('LeftCtrl', 'ctrl'), 42: ('LeftShift', 'shift'), 54: ('RightShift', 'shift_r'), 56: ('LeftAlt', 'alt'),
    57: ('Space', 'spc'), 58: ('CapsLock', 'caps_lock'), 69: ('NumLock', 'num_lock'), 70: ('ScrollLock', 'scroll_lock'),
    55: ('KpMultiply', 'kp_multiply'), 71: ('Kp7', 'kp_7'), 72: ('Kp8', 'kp_8'), 73: ('Kp9', 'kp_9'),
    74: ('KpMinus', 'kp_subtract'), 75: ('Kp4', 'kp_4'), 76: ('Kp5', 'kp_5'), 77: ('Kp6', 'kp_6'), 78: ('KpPlus', 'kp_add'),
    79: ('Kp1', 'kp_1'), 80: ('Kp2', 'kp_2'), 81: ('Kp3', 'kp_3'), 82: ('Kp0', 'kp_0'), 83: ('KpDecimal', 'kp_decimal'),
    86: ('Less', 'less'), 87: ('F11', 'f11'), 88: ('F12', 'f12'), 96: ('KpEnter', 'kp_enter'), 97: ('RightCtrl', 'ctrl_r'),
    98: ('KpDivide', 'kp_divide'), 99: ('PrintScreen', 'print'), 100: ('RightAlt', 'alt_r'), 102: ('Home', 'home'),
    103: ('Up', 'up'), 104: ('PageUp', 'pgup'), 105: ('Left', 'left'), 106: ('Right', 'right'), 107: ('End', 'end'),
    108: ('Down', 'down'), 109: ('PageDown', 'pgdn'), 110: ('Insert', 'insert'), 111: ('Delete', 'delete'),
    119: ('Pause', 'pause'), 125: ('LeftMeta', 'meta_l'), 126: ('RightMeta', 'meta_r'), 127: ('Menu', 'menu'),
}
for i, f in enumerate(range(59, 69)):
    _T[f] = ('F%d' % (i + 1), 'f%d' % (i + 1))
_CHARS = {}
for code, (c, s) in zip(range(2, 12), zip('1234567890', '!@#$%^&*()')):
    _T[code] = (c, c); _CHARS[code] = (c, s)
for code, c in zip(range(16, 26), 'qwertyuiop'):
    _T[code] = (c, c); _CHARS[code] = (c, c.upper())
for code, c in zip(range(30, 39), 'asdfghjkl'):
    _T[code] = (c, c); _CHARS[code] = (c, c.upper())
for code, c in zip(range(44, 51), 'zxcvbnm'):
    _T[code] = (c, c); _CHARS[code] = (c, c.upper())
for code, name, q, c, s in ((12, 'minus', 'minus', '-', '_'), (13, 'equal', 'equal', '=', '+'),
                             (26, 'bracketleft', 'bracket_left', '[', '{'), (27, 'bracketright', 'bracket_right', ']', '}'),
                             (39, 'semicolon', 'semicolon', ';', ':'), (40, 'apostrophe', 'apostrophe', "'", '"'),
                             (41, 'grave', 'grave_accent', '`', '~'), (43, 'backslash', 'backslash', '\\', '|'),
                             (51, 'comma', 'comma', ',', '<'), (52, 'period', 'dot', '.', '>'), (53, 'slash', 'slash', '/', '?')):
    _T[code] = (name, q); _CHARS[code] = (c, s)
# Polish programmer's layout: Right Alt + letter
_POLISH = {'a': 'ą', 'c': 'ć', 'e': 'ę', 'l': 'ł', 'n': 'ń', 'o': 'ó', 's': 'ś', 'z': 'ż', 'x': 'ź'}
MODIFIERS = {29: 'ctrl', 97: 'rctrl', 42: 'shift', 54: 'shift', 56: 'alt', 100: 'ralt', 125: 'meta', 126: 'meta'}
HOST_MODS = (29, 56)                 # LEFT Control + LEFT Alt: the host key (Right Alt = AltGr stays the guest's)
HOST_COMMANDS = ('Space', 'PageUp', 'PageDown', 'r', 'a', 's', 'e', 'Delete', 'F1', 'Backspace')
REVIEW_KEYS = ('u', 'i', 'o', 'j', 'k', 'l', 'm', 'comma', 'period')
NAME_TO_CODE ={v[0]: k for k, v in _T.items()}
QCODE = {k: v[1] for k, v in _T.items()}


class Key:
    __slots__ = ('code', 'name', 'ctrl', 'rctrl', 'shift', 'alt', 'ralt', 'char', 'down', 'repeat')

    def __init__(self, code, down, repeat, mods):
        self.code = code
        self.name = _T.get(code, ('Key%d' % code, ''))[0]
        self.down, self.repeat = down, repeat
        self.ctrl, self.rctrl = 'ctrl' in mods, 'rctrl' in mods
        self.shift, self.alt, self.ralt = 'shift' in mods, 'alt' in mods, 'ralt' in mods
        self.char = None
        if code in _CHARS and not self.ctrl and not self.alt:
            c, s = _CHARS[code]
            self.char = s if self.shift else c
            if self.ralt and c in _POLISH:
                self.char = _POLISH[c].upper() if self.shift else _POLISH[c]
        elif code == 57 and not self.ctrl:
            self.char = ' '

    def combo(self):
        parts = []
        if self.ctrl: parts.append('Ctrl')
        if self.alt: parts.append('Alt')
        if self.shift: parts.append('Shift')
        return '+'.join(parts + [self.name])


def keyboards():
    """Event devices of keyboards, from /proc/bus/input/devices."""
    out = []
    try:
        text = open('/proc/bus/input/devices').read()
    except OSError:
        return out
    for block in text.split('\n\n'):
        h = re.search(r'H: Handlers=(.*)', block)
        ev = re.search(r'B: EV=([0-9a-f]+)', block)
        if not h or not ev:
            continue
        if 'kbd' in h.group(1).split() and (int(ev.group(1), 16) & 0x100000):  # EV_REP: a real keyboard
            m = re.search(r'\bevent(\d+)\b', h.group(1))
            if m:
                out.append('/dev/input/event' + m.group(1))
    return out


class Keyboard:
    """All keyboards, grabbed; Key objects (presses and repeats; releases too, for the guest) on .events."""

    def __init__(self, log):
        self.log = log
        self.events = queue.Queue()
        self.open = {}
        self.held = set()
        self.lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()

    def mods(self):
        return {MODIFIERS[c] for c in self.held if c in MODIFIERS}

    def _scan(self):
        for path in keyboards():
            if path in self.open:
                continue
            try:
                fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
                if getattr(self, 'grabbed', True):
                    fcntl.ioctl(fd, EVIOCGRAB, 1)
                self.open[path] = fd
                self.log('keyboard %s grabbed' % path)
            except OSError as e:
                self.log('keyboard %s: %s' % (path, e))

    def _run(self):
        last = 0
        while True:
            if time.time() - last > 2:
                self._scan(); last = time.time()
            fds = list(self.open.values())
            if not fds:
                time.sleep(0.5); continue
            r, _, _ = select.select(fds, [], [], 1.0)
            for fd in r:
                try:
                    data = os.read(fd, EVENT.size * 64)
                except OSError:
                    for p, f in list(self.open.items()):
                        if f == fd:
                            del self.open[p]; os.close(fd)
                    continue
                for i in range(0, len(data) - EVENT.size + 1, EVENT.size):
                    _, _, typ, code, value = EVENT.unpack_from(data, i)
                    if typ != EV_KEY:
                        continue
                    if value == 1:
                        self.held.add(code)
                    elif value == 0:
                        self.held.discard(code)
                    self.events.put(Key(code, value != 0, value == 2, self.mods()))

    def set_grab(self, on):
        """6s: the modern browser's compositor needs the keyboards too; the hypervisor keeps reading them (host key)."""
        self.grabbed = on
        for p, fd in list(self.open.items()):
            try:
                fcntl.ioctl(fd, EVIOCGRAB, 1 if on else 0)
            except OSError as e:
                self.log('keyboard %s grab %s: %s' % (p, on, e))

    def get(self, timeout=None):
        try:
            return self.events.get(timeout=timeout)
        except queue.Empty:
            return None
