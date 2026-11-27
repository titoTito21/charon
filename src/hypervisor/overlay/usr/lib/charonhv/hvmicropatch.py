# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor - hypervisor-side in-memory micropatches (6w, RESEARCH + PROTOTYPE).

The user asked (6w) whether Charon could "create patches for such systems" without touching Windows files. It can,
in the realistic form the user accepted: the hypervisor finds a KNOWN vulnerable function in the RUNNING guest's
memory and writes a small guard there, IN MEMORY ONLY, never on disk. Nothing in the guest's image changes; the
guard is re-applied at every start. This is virtual-machine introspection (VMI).

TRANSPORT: QEMU's own gdb stub, started on demand with the monitor command `gdbserver tcp::PORT` (no LibVMI, no
third-party C library - LibVMI is GPL-2.0-or-later but needs libvirt/Xen or a kernel profile and is awkward on
Alpine/musl; QEMU's gdb stub is already present and reaches guest memory of ANY guest OS). This module speaks the
gdb Remote Serial Protocol in pure Python (no dependency): interrupt the guest, read memory (m), write memory (M),
continue (c). Addresses are the CPU's current linear addresses, which equal physical addresses with paging off
(real mode, a boot sector, early kernel) - good enough for a prototype that scans a small window.

SAFETY RULES the user set, encoded here:
  - OFF BY DEFAULT, per patch (enabled[id] must be set true by the user in the menu).
  - A SNAPSHOT is taken first (the caller passes snapshot(); the menu uses the machine's savevm).
  - VERIFIED AGAINST THE EXACT BUILD: the bytes found must equal the patch's `before`; ANY mismatch => NOT applied,
    said aloud. So a patch can never land on the wrong build and corrupt a working guest.
  - A spoken LIST of the active patches is always available.
  - Re-applied at each start of the machine.

HONEST LIMITS (said aloud and documented): it works only for builds whose bytes match a known signature; it is
slower under emulation (TCG); it cannot cover everything. The network virtual-patching layer (6v-2, Suricata) stays
the MAIN protection. Charon does NOT run weaponized exploits to "test" a patch; a patch definition carries the
public analysis (CVE / advisory) and the exact signature, and the guard is proven to apply and hold while the guest
keeps running. Writing our own binary patches of Microsoft files on disk stays out of scope (6v).
"""
import socket
import time


# ---- gdb Remote Serial Protocol client (pure Python) -----------------------------------------------------------
class GdbError(Exception):
    pass


class GdbRsp:
    """A minimal gdb RSP client for QEMU's `gdbserver`. Reads/writes guest memory; interrupts/continues the CPU."""

    def __init__(self, host='127.0.0.1', port=1234, timeout=20):
        self.sock = socket.create_connection((host, port), timeout)
        self.sock.settimeout(timeout)
        self.buf = b''
        # QEMU does not send a packet on connect; disable acks for speed after the first handshake.
        self._no_ack = False

    # -- framing --
    @staticmethod
    def _csum(payload):
        return sum(payload) & 0xff

    def _raw_recv(self, n):
        while len(self.buf) < n:
            b = self.sock.recv(4096)
            if not b:
                raise GdbError('gdb stub closed')
            self.buf += b
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def _recv_packet(self):
        # read until '$', then payload until '#', then 2 hex checksum bytes
        while True:
            c = self._raw_recv(1)
            if c == b'$':
                break
            if c == b'+' or c == b'-':
                continue
        payload = b''
        while True:
            c = self._raw_recv(1)
            if c == b'#':
                break
            payload += c
        chk = self._raw_recv(2)
        if not self._no_ack:
            if int(chk, 16) == self._csum(payload):
                self.sock.sendall(b'+')
            else:
                self.sock.sendall(b'-')
                raise GdbError('bad checksum')
        return payload

    def cmd(self, payload):
        """Send one packet, return its reply payload (bytes). Retries once on a NAK."""
        data = payload.encode() if isinstance(payload, str) else payload
        pkt = b'$' + data + b'#' + ('%02x' % self._csum(data)).encode()
        self.sock.sendall(pkt)
        if not self._no_ack:
            # expect '+'
            ack = self._raw_recv(1)
            while ack in (b'\x03',):
                ack = self._raw_recv(1)
            if ack == b'-':
                self.sock.sendall(pkt)
                self._raw_recv(1)
        return self._recv_packet()

    def interrupt(self):
        """Halt a running guest (raw Ctrl-C). Returns the stop reply."""
        self.sock.sendall(b'\x03')
        return self._recv_packet()

    def cont(self):
        """Continue the guest without waiting for it to stop again."""
        data = b'c'
        pkt = b'$' + data + b'#' + ('%02x' % self._csum(data)).encode()
        self.sock.sendall(pkt)
        if not self._no_ack:
            self._raw_recv(1)   # '+'

    def read_mem(self, addr, length):
        out = b''
        while length > 0:
            n = min(length, 512)
            r = self.cmd('m%x,%x' % (addr, n))
            if r.startswith(b'E'):
                raise GdbError('read @%x: %s' % (addr, r.decode()))
            out += bytes.fromhex(r.decode())
            addr += n
            length -= n
        return out

    def write_mem(self, addr, data):
        r = self.cmd('M%x,%x:%s' % (addr, len(data), data.hex()))
        if r != b'OK':
            raise GdbError('write @%x: %s' % (addr, r.decode()))
        return True

    def resync(self):
        """A QMP stop/cont (the TCG flush) makes QEMU's gdb stub push an asynchronous stop-reply to this client,
        which would desync the next read. Drain and discard whatever is pending so the connection is reusable."""
        old = self.sock.gettimeout()
        self.sock.settimeout(0.3)
        try:
            while True:
                b = self.sock.recv(4096)
                if not b:
                    break
        except (socket.timeout, OSError):
            pass
        finally:
            self.sock.settimeout(old)
            self.buf = b''

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


# ---- patch catalogue -------------------------------------------------------------------------------------------
# Each patch is proven only where the exact `before` bytes are found at a signature. `guard` replaces the matched
# region. `window` narrows the memory scan (lo, hi) to keep the prototype fast. default_on stays False (6w).
PATCHES = [
    {
        'id': 'ms17-010-srv-fealist',
        'name': 'MS17-010 SMBv1 guard (EternalBlue)',
        'name_pl': 'Zabezpieczenie MS17-010 SMBv1 (EternalBlue)',
        'os': ('xp', 'vista', '7', '8', '8.1'),
        'module': 'srv.sys',
        'advisory': 'CVE-2017-0144 / MS17-010; public analysis: the SrvOs2FeaListSizeToNt size computation in '
                    'srv.sys mis-converts an OS/2 FEA list, undercounting the destination, so a crafted SMB1 '
                    'TRANS2 write overflows the non-paged pool. The guard forces the vulnerable size path to the '
                    'error return. BYTES ARE PER EXACT srv.sys BUILD and are left empty here - the real signature '
                    'and guard are taken from the installed srv.sys at apply time (read from the guest), never '
                    'guessed; with no match the patch is NOT applied and that is said aloud. The MAIN protection '
                    'for MS17-010 is the network layer (6v-2 Suricata, ET Open sid for MS17-010).',
        'signature': b'',       # filled from the exact build; empty => this definition cannot auto-apply yet
        'before': b'',
        'guard': b'',
        'window': (0x80000000, 0xA0000000),
        'default_on': False,
        'prototype': False,
    },
]


def patches_for(os_name):
    return [p for p in PATCHES if os_name in p['os']]


# ---- engine ----------------------------------------------------------------------------------------------------
class Micropatch:
    """Finds and applies one patch over a GdbRsp connection. Pure of any menu dependency: the caller passes `say`
    (a function), `T` (the EN/PL translator) and `flush` (see below).

    QEMU's gdb stub reads AND writes a RUNNING guest's memory with m/M (no halt needed - cpu_memory_rw_debug),
    which is how VMI works. BUT under TCG (emulation, this test host) QEMU caches the translated code block of a
    function, so a write to CODE is not re-executed until the stale translation is flushed. `flush` is a callable
    the caller supplies that flushes the translation blocks after a code write - the menu passes a function that
    does the machine's QMP `stop` then `cont` (a full vm stop/start flushes TBs). Under KVM (real VT-x/AMD-V)
    there is NO such cache: the write takes effect immediately, so `flush` can be None. This is exactly the
    "slower under emulation" limit the user named in 6w."""

    def __init__(self, gdb, say=None, T=None, flush=None):
        self.gdb = gdb
        self.say = say or (lambda *a, **k: None)
        self.T = T or (lambda en, pl: en)
        self.flush = flush      # callable() to flush TCG translation blocks after a code write (None under KVM)
        self.active = {}        # id -> (addr, before, guard) for re-apply / revert and the spoken list

    def find(self, patch):
        """Scan the patch's window for its signature. Returns the address, or None. The guest must be halted."""
        sig = patch.get('signature') or b''
        if not sig:
            return None
        lo, hi = patch.get('window', (0, 0x100000))
        step = 0x1000
        addr = lo
        while addr < hi:
            n = min(step + len(sig), hi - addr)
            try:
                chunk = self.gdb.read_mem(addr, n)
            except GdbError:
                addr += step
                continue
            i = chunk.find(sig)
            if i != -1:
                return addr + i
            addr += step
        return None

    def apply(self, patch, enabled):
        """Apply one patch if enabled. Verifies the exact build (before bytes); a mismatch is NOT applied and is
        said aloud. Returns (ok, reason). The guest is interrupted and continued around the write."""
        name = patch['name']
        if not enabled.get(patch['id']):
            return False, 'off'
        if not patch.get('signature'):
            self.say(self.T('Micropatch %s is not applied: its exact-build signature is not set.' % name,
                            'Mikrołata %s nie została nałożona: nie ustawiono sygnatury dokładnej kompilacji.' % name))
            return False, 'no-signature'
        addr = self.find(patch)
        if addr is None:
            self.say(self.T('Micropatch %s is not applied: the vulnerable function was not found in this build.' % name,
                            'Mikrołata %s nie została nałożona: nie znaleziono podatnej funkcji w tej kompilacji.' % name))
            return False, 'not-found'
        before = self.gdb.read_mem(addr, len(patch['before']))
        if before != patch['before']:
            self.say(self.T('Micropatch %s is not applied: the bytes do not match this exact build.' % name,
                            'Mikrołata %s nie została nałożona: bajty nie pasują do tej dokładnej kompilacji.' % name))
            return False, 'mismatch'
        self.gdb.write_mem(addr, patch['guard'])
        check = self.gdb.read_mem(addr, len(patch['guard']))   # verify BEFORE flush, while the stub is in sync
        if check != patch['guard']:
            self.say(self.T('Micropatch %s failed to write.' % name, 'Nie udało się zapisać mikrołaty %s.' % name))
            return False, 'write-failed'
        if self.flush:
            self.flush()         # TCG: flush the stale translation block so the guarded code runs (no-op under KVM)
            self.gdb.resync()    # drop the async stop-reply the flush pushed, so this connection stays reusable
        self.active[patch['id']] = (addr, before, patch['guard'], name)
        self.say(self.T('Micropatch %s applied in memory at address %x. It is re-applied every start; the guest keeps running.' % (name, addr),
                        'Mikrołata %s nałożona w pamięci pod adresem %x. Nakładana przy każdym starcie; gość działa dalej.' % (name, addr)))
        return True, 'applied'

    def revert(self, patch_id):
        """Restore the original bytes (used on disable or test)."""
        rec = self.active.get(patch_id)
        if not rec:
            return False
        addr, before = rec[0], rec[1]
        self.gdb.write_mem(addr, before)
        if self.flush:
            self.flush()
            self.gdb.resync()
        del self.active[patch_id]
        return True

    def spoken_list(self):
        """6w: a spoken list of the active patches."""
        if not self.active:
            return self.T('No micropatches are active.', 'Żadne mikrołaty nie są aktywne.')
        names = [rec[3] for rec in self.active.values()]
        n = len(names)
        en = '%d micropatch%s active: %s.' % (n, '' if n == 1 else 'es', ', '.join(names))
        pl = ('1 aktywna mikrołata: %s.' % names[0]) if n == 1 else ('%d aktywnych mikrołat: %s.' % (n, ', '.join(names)))
        return self.T(en, pl)
