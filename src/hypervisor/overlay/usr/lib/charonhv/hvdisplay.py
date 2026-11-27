# SPDX-License-Identifier: GPL-2.0-or-later
# Charon Hypervisor: the screen of the machine that has the keyboard, shown on the computer's own screen (for sighted
# helpers). r410 (user, 6f): the guest gets its OWN virtual console (tty7, empty, no cursor), so the hypervisor's
# messages (tty1) never draw over it; the host key switches between the guest's screen (tty7) and the hypervisor's
# menu (tty1). QEMU's screendump is copied into the Linux framebuffer (/dev/fb0) about twice a second, enlarged by
# scaled to fill the screen (nearest neighbour, proportions kept), centred; only rows that changed are written again.
import operator, os, re, subprocess, threading, time

GUEST_VT = 7
MENU_VT = 1


def fb_info():
    try:
        w, h = open('/sys/class/graphics/fb0/virtual_size').read().strip().split(',')
        bpp = int(open('/sys/class/graphics/fb0/bits_per_pixel').read())
        stride = int(open('/sys/class/graphics/fb0/stride').read())
        return int(w), int(h), bpp, stride
    except (OSError, ValueError):
        return None


def active_vt():
    try:
        return int(open('/sys/class/tty/tty0/active').read().strip()[3:])
    except (OSError, ValueError):
        return 0


def switch_vt(n):
    """The kernel's VT_ACTIVATE + VT_WAITACTIVE on /dev/tty0 (no outside program); chvt only as a fallback."""
    try:
        import fcntl
        fd = os.open('/dev/tty0', os.O_RDWR)
        try:
            fcntl.ioctl(fd, 0x5606, n)
            fcntl.ioctl(fd, 0x5607, n)
        finally:
            os.close(fd)
        return
    except (OSError, ImportError):
        pass
    try:
        subprocess.run(['chvt', str(n)], capture_output=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass


class Display:
    def __init__(self, app):
        self.app = app
        self.info = fb_info()
        self.shown = None              # (machine id, width, height) last drawn, None = must clear first
        self.rows = {}
        self.lock = threading.Lock()
        try:
            with open('/dev/tty%d' % GUEST_VT, 'w') as t:
                # no cursor, nothing written: the guest's picture only; blanking and power-down off (the console's own
                # private sequences, as setterm -blank 0 -powerdown 0 writes them; setterm is not among the packages)
                t.write('\033[?25l\033[2J\033[H\033[9;0]\033[14;0]')
        except OSError:
            pass
        try:
            with open('/sys/module/kernel/parameters/consoleblank', 'w') as b:
                b.write('0')
        except OSError:
            pass
        if self.info and self.info[2] == 32:
            threading.Thread(target=self.run, daemon=True).start()

    def guest(self, on):
        """The host key moved the keyboard: show the guest's console (on) or the hypervisor's menu (off)."""
        if not self.info:
            return
        with self.lock:
            self.shown = None
            self.rows = {}
        switch_vt(GUEST_VT if on else MENU_VT)

    def run(self):
        fw, fh, bpp, stride = self.info
        while True:
            time.sleep(0.5)
            m = self.app.machines.get(self.app.focus) if self.app.focus else None
            if not m or not m.running() or active_vt() != GUEST_VT:
                continue
            path = '/run/charonhv/display.ppm'
            try:
                m.screendump(path)
                d = open(path, 'rb').read()
            except Exception:
                continue
            hd = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', d)
            if not hd:
                continue
            w, h = int(hd.group(1)), int(hd.group(2))
            px = memoryview(d)[hd.end():]
            # r410 (user: "the guest's screen full-size"): the picture fills the screen's height or width, keeping
            # its proportions - nearest neighbour at any factor (640x480 on 1280x800 -> 1066x800), centred
            if self.shown != (m.id, w, h):
                s = min(fw / w, fh / h)
                ow, oh = max(1, min(fw, int(w * s))), max(1, min(fh, int(h * s)))
                self.geom = ((fw - ow) // 2, (fh - oh) // 2, ow, oh,
                             operator.itemgetter(*[(x * w // ow) * 4 + c for x in range(ow) for c in range(4)]),
                             [[oy for oy in range(oh) if oy * h // oh == y] for y in range(h)])
            x0, y0, ow, oh, pick, targets = self.geom
            try:
                with self.lock, open('/dev/fb0', 'r+b') as fb:
                    if self.shown != (m.id, w, h):
                        black = bytes(stride)
                        for y in range(fh):
                            fb.seek(y * stride); fb.write(black[:fw * 4])
                        self.shown, self.rows = (m.id, w, h), {}
                    for y in range(h):
                        src = px[y * w * 3:(y + 1) * w * 3]
                        if self.rows.get(y) == src:
                            continue
                        self.rows[y] = bytes(src)
                        bgr0 = bytearray(w * 4)                 # B, G, R, 0 per pixel
                        bgr0[0::4] = src[2::3]
                        bgr0[1::4] = src[1::3]
                        bgr0[2::4] = src[0::3]
                        row = bytes(pick(bgr0))
                        for oy in targets[y]:
                            fb.seek((y0 + oy) * stride + x0 * 4)
                            fb.write(row)
            except OSError:
                return
