# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: printing to PDF on the Hypervisor partition (6q).

The guest gets a parallel port whose output goes to /hv/print/<machine>/lpt1.raw; the secure-use set installs a
PostScript printer "Charon PDF" on LPT1 with Windows' own in-box driver (XP: "Apple LaserWriter 12/640 PS" from
ntprint.inf). Every finished PostScript job (a %!PS start ... %%EOF end, or quiet for 5 seconds) is turned into a PDF
with Ghostscript (AGPL-3.0, Artifex; Alpine package ghostscript, fetched the first time printing is switched on) and
kept as /hv/print/<machine>/<date time>.pdf - also copied to the shared folder's Prints folder for other systems.
Output that is not PostScript (a text-only printer) is kept as a .txt file instead."""
import os, shutil, subprocess, time

PKGS = ('ghostscript',)
ROOT = '/hv/print'


def port_file(mid):
    d = os.path.join(ROOT, mid)
    os.makedirs(d, exist_ok=True)
    return os.path.join(d, 'lpt1.raw')


class Spool:
    def __init__(self, mid, say_done, log):
        self.mid, self.say_done, self.log = mid, say_done, log
        self.path = port_file(mid)
        self.pos = os.path.getsize(self.path) if os.path.exists(self.path) else 0
        self.buf = b''
        self.last = 0

    def poll(self):
        try:
            size = os.path.getsize(self.path)
        except OSError:
            return
        if size < self.pos:
            self.pos = 0
        if size > self.pos:
            with open(self.path, 'rb') as f:
                f.seek(self.pos)
                self.buf += f.read(size - self.pos)
            self.pos = size
            self.last = time.time()
        while self.buf:
            start = self.buf.find(b'%!PS')
            if start < 0:
                if time.time() - self.last > 5:
                    self.finish(self.buf, False)
                    self.buf = b''
                return
            if start > 0 and self.buf[:start].strip(b'\x04\x1b%-12345X@PJL \r\n'):
                self.finish(self.buf[:start], False)
            end = self.buf.find(b'%%EOF', start)
            if end < 0:
                if time.time() - self.last > 5:
                    self.finish(self.buf[start:], True)
                    self.buf = b''
                return
            end += 5
            self.finish(self.buf[start:end], True)
            self.buf = self.buf[end:].lstrip(b'\x04\r\n')

    def finish(self, data, ps):
        if not data.strip():
            return
        name = time.strftime('%Y-%m-%d %H-%M-%S')
        d = os.path.dirname(self.path)
        if not ps:
            out = os.path.join(d, name + '.txt')
            open(out, 'wb').write(data)
            self.say_done(out, 0, None)
            return
        src = os.path.join(d, name + '.ps')
        out = os.path.join(d, name + '.pdf')
        open(src, 'wb').write(data)
        gs = shutil.which('gs')
        if not gs:
            self.say_done(src, 0, 'Ghostscript is not installed; the job is kept as PostScript')
            return
        r = subprocess.run([gs, '-q', '-dSAFER', '-dBATCH', '-dNOPAUSE', '-sDEVICE=pdfwrite', '-sOutputFile=' + out, src],
                           capture_output=True, text=True, timeout=300)
        if r.returncode != 0 or not os.path.exists(out):
            self.say_done(src, 0, (r.stderr.strip().splitlines() or ['Ghostscript failed'])[-1][:160])
            return
        os.remove(src)
        pages = 0
        try:
            pages = open(out, 'rb').read().count(b'/Type /Page') - open(out, 'rb').read().count(b'/Type /Pages')
        except OSError:
            pass
        try:
            sd = os.path.join('/hv/shared', 'Prints')
            os.makedirs(sd, exist_ok=True)
            shutil.copy(out, os.path.join(sd, '%s %s.pdf' % (self.mid, name)))
        except OSError as e:
            self.log('print copy to shared: %r' % e)
        self.say_done(out, max(pages, 1), None)
