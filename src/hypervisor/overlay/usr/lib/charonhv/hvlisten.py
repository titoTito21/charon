# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor 6ze: does the guest's OWN screen reader speak? (Like Charon's CharonPC reader, src/pc/pc_reader.cpp
GuestReaderTakesOver, which counts the guest's sound packets: here QEMU's own capture of the machine's sound output.)

QEMU's monitor command `wavcapture` writes what the machine plays into a file in RAM (mono, 16 bit, 16 kHz); the
hypervisor reads only what was added since the last look and measures its peak. The hypervisor's own voice never goes
through a machine's sound card, so what is heard here is the guest. The file is started again when it grows past
4 MB (about two minutes of sound; nothing is written while the guest plays nothing).

StepBack decides, per machine with its own reader:
  - the guest's screen is text (firmware, boot menu, boot messages) or the machine was reset: the hypervisor reads;
  - on its graphical desktop the hypervisor reads until the guest plays sound, then steps back (said once);
  - it comes back when the guest stays silent although the user pressed keys in it (3 key presses, then 20 seconds
    without any sound): a reader that answers no key is not running (said once);
  - text again (restart, boot menu): it reads again, and steps back again the next time the guest speaks."""
import os, struct, time

RATE = 16000
LIMIT = 4 << 20
THRESHOLD = 0.02            # peak (0..1) that counts as sound: XP's idle mixer plays digital zeros
QUIET_KEYS, QUIET_SECONDS = 3, 20.0   # measured session 9: NVDA in an emulated XP took over 8 s to answer


def peak16(data):
    """Peak of signed 16-bit little-endian mono samples, 0..1."""
    n = len(data) // 2
    if not n:
        return 0.0
    v = struct.unpack('<%dh' % n, data[:n * 2])
    return max(max(v), -min(v)) / 32768.0


class Capture:
    """QEMU's wavcapture of one machine's sound output (audiodev snd0)."""

    def __init__(self, m, rundir='/run/charonhv'):
        self.m, self.path, self.pos, self.qmp = m, os.path.join(rundir, '%s.heard.wav' % m.id), 44, None

    def _start(self):
        # session 10, found live: every menu restart started ANOTHER capture on the same QEMU (the old one kept writing
        # into the deleted file): 27 captures filled /run (1.6 GB) and the menu stopped with "No space left on device".
        # Every capture of this machine is ended first.
        self._stop()
        try:
            os.remove(self.path)
        except OSError:
            pass
        self.m.qmp.hmp('wavcapture %s snd0 %d 16 1' % (self.path, RATE))
        self.qmp, self.pos = self.m.qmp, 44

    def _stop(self):
        # only the hypervisor makes captures, one machine per QEMU: all of them are this listener's. "stopcapture 0"
        # as often as "info capture" lists one (the numbers move up after each stop)
        try:
            # QMP.hmp returns the monitor's TEXT (session 9 took it for a dict: the AttributeError was swallowed and
            # no capture was ever stopped)
            info = self.m.qmp.hmp('info capture') or ''
            info = info.get('return', '') if isinstance(info, dict) else str(info)
            for _ in range(sum(1 for line in info.splitlines() if line.strip().startswith('['))):
                self.m.qmp.hmp('stopcapture 0')
        except Exception:
            pass

    def peak(self):
        """Peak of the sound the machine played since the last call; None when it cannot be heard (no QMP)."""
        q = getattr(self.m, 'qmp', None)
        if q is None:
            return None
        try:
            if self.qmp is not q:          # a new QEMU process (machine started again): a new capture
                self._start()
            size = os.path.getsize(self.path) if os.path.exists(self.path) else 0
            if size <= self.pos:
                return 0.0
            with open(self.path, 'rb') as f:
                f.seek(self.pos)
                data = f.read(min(size - self.pos, RATE * 2 * 10))
            self.pos = size
            p = peak16(data)
            if size > LIMIT:
                self._stop()
                self._start()
            return p
        except Exception:
            return None

    def close(self):
        if self.qmp is not None:
            self._stop()
        try:
            os.remove(self.path)
        except OSError:
            pass


class StepBack:
    """The decision above, without any I/O (host-tested)."""

    def __init__(self):
        self.stepped = False
        self.heard = 0.0
        self.keys = []

    def text_screen(self):
        """Firmware, boot loader, boot messages, a reset: the hypervisor reads; the guest's reader is not up."""
        self.stepped, self.keys = False, []

    def key(self, now):
        self.keys.append(now)
        self.keys = self.keys[-QUIET_KEYS:]

    def update(self, peak, now):
        """Returns 'back' when the hypervisor steps back, 'reads' when it reads again, else None."""
        if peak is not None and peak >= THRESHOLD:
            self.heard, self.keys = now, []
            if not self.stepped:
                self.stepped = True
                return 'back'
            return None
        if (self.stepped and len(self.keys) >= QUIET_KEYS and self.keys[0] > self.heard
                and now - self.keys[0] >= QUIET_SECONDS):
            self.stepped, self.keys = False, []
            return 'reads'
        return None
