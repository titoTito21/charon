# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: downloads reach a RUNNING guest (6v).

The exchange drive is a disk image and cannot be written while its machine runs. So files downloaded by the
hypervisor's modern browser (6s, Firefox) or remote browser (6r, Chromium) are given to the guest through a small,
READ-ONLY network share of the hypervisor: \\\\10.0.2.100\\Downloads inside the guest.

  - one Samba server (Alpine's samba, GPL-3.0) per running secure machine, listening ONLY on 127.0.0.1:<port>; the
    guest reaches it through its own SLIRP link (guestfwd 10.0.2.100:445 and :139 -> that port), so no other machine
    and no outside network can see it; SMB1 (NT1) is allowed on this private link only because XP and older speak
    nothing newer - the share is read-only, guest access, no passwords, no printers, no other folders;
  - every new download is scanned by ClamAV first (hvscan); a clean file is copied into /hv/downloads/<machine>/ (the
    share), a file with malware is not copied and the voice says so; nothing is changed in the guest itself
    (hypervisor-side, as the user prefers);
  - SECURE.CMD of the per-version set maps it (net use) and puts a shortcut on the desktop; without that the user
    types \\\\10.0.2.100\\Downloads in Run.
Speech: "<machine>: <file> downloaded, no malware found; it is in the Downloads share." / Polish."""
import os, shutil, subprocess, threading, time

ROOT = '/hv/downloads'
RUN = '/run/charonhv'
APK = ('samba-server',)
SOURCES = ('/hv/modern-profile/Downloads', '/hv/remote-downloads')     # Firefox (6s) and Chromium (6r) download here
PARTIAL = ('.part', '.crdownload', '.tmp')


def folder(mid):
    return ROOT + '/' + mid


def config(mid, port):
    """smb.conf of one machine's server (no includes of the system configuration)."""
    base = os.path.join(RUN, 'smb-' + mid)
    return '\n'.join([
        '[global]',
        '  netbios name = CHARONHV', '  workgroup = WORKGROUP', '  server string = Charon Hypervisor',
        '  interfaces = lo', '  bind interfaces only = yes', '  smb ports = %d' % port,
        '  disable netbios = yes', '  server role = standalone server',
        '  server min protocol = NT1',                      # XP/2000/9x: only on this private, read-only link
        '  ntlm auth = ntlmv1-permitted', '  lanman auth = no',
        '  map to guest = Bad User', '  guest account = nobody', '  restrict anonymous = 0',
        '  load printers = no', '  printing = bsd', '  printcap name = /dev/null', '  disable spoolss = yes',
        '  pid directory = %s' % base, '  lock directory = %s' % base, '  state directory = %s' % base,
        '  cache directory = %s' % base, '  private dir = %s' % base, '  ncalrpc dir = %s/ncalrpc' % base,
        '  log file = %s/log.smbd' % base, '  max log size = 200',
        '  unix extensions = no', '  wide links = no', '  follow symlinks = no',
        '[Downloads]',
        '  path = %s' % folder(mid), '  read only = yes', '  guest ok = yes', '  guest only = yes',
        '  browseable = yes', '  comment = Files downloaded by Charon Hypervisor (scanned)', ''])


def start(mid, port, log):
    """Start this machine's share server; returns the process or None (the reason is logged)."""
    smbd = shutil.which('smbd') or '/usr/sbin/smbd'
    if not os.path.exists(smbd):
        log('share %s: samba is not installed' % mid)
        return None
    base = os.path.join(RUN, 'smb-' + mid)
    os.makedirs(base + '/ncalrpc', exist_ok=True)
    os.makedirs(folder(mid), exist_ok=True)
    os.chmod(folder(mid), 0o755)
    conf = base + '/smb.conf'
    # measured session 9: a share server left from an earlier start of this machine (the menu restarted meanwhile)
    # kept smbd.pid, so every new smbd stopped at once ("smbd is already running") and the old one served the OLD
    # port - the guest's share was gone. A machine is started only when it is off, so that server is ended first.
    try:
        old = int(open(base + '/smbd.pid').read().split()[0])
        if conf.encode() in open('/proc/%d/cmdline' % old, 'rb').read():
            os.kill(old, 15)
            for _ in range(50):
                if not os.path.exists('/proc/%d' % old):
                    break
                time.sleep(0.1)
            log('share %s: earlier smbd %d ended' % (mid, old))
    except (OSError, ValueError, IndexError):
        pass
    open(conf, 'w').write(config(mid, port))
    p = subprocess.Popen(['nice', '-n', '5', smbd, '--foreground', '--no-process-group', '--debug-stdout', '-s', conf],
                         stdout=open(base + '/smbd.out', 'ab'), stderr=subprocess.STDOUT, start_new_session=True)
    log('share %s: smbd pid %d on 127.0.0.1:%d' % (mid, p.pid, port))
    return p


def stop(proc):
    if proc is not None and proc.poll() is None:
        if not hasattr(proc, 'terminate'):
            # session 10: a share server taken back after a menu restart (hvmachine.Adopted) - no Popen methods
            proc.send_signal(15)
            return
        proc.terminate()
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            proc.kill()


class Watcher:
    """Watches the browsers' download folders; each finished file is scanned and handed to target() - the machine
    that should get it (the one that asked for the page, else the running secure machine) - via deliver."""

    def __init__(self, target, scan, say, log, poll=2.0):
        self.target, self.scan, self.say, self.log, self.poll = target, scan, say, log, poll
        self.seen = {}
        for d in SOURCES:
            os.makedirs(d, exist_ok=True)
            for n in os.listdir(d):
                self.seen[os.path.join(d, n)] = 'old'                 # what was there before is not delivered again
        threading.Thread(target=self.run, daemon=True).start()

    def run(self):
        while True:
            time.sleep(self.poll)
            try:
                self.once()
            except Exception as e:
                self.log('downloads watcher: %r' % e)

    def once(self):
        for d in SOURCES:
            try:
                names = os.listdir(d)
            except OSError:
                continue
            for n in names:
                p = os.path.join(d, n)
                if n.endswith(PARTIAL) or not os.path.isfile(p) or os.path.exists(p + '.part') or os.path.exists(p + '.crdownload'):
                    continue
                st = os.stat(p)
                key = (st.st_size, int(st.st_mtime))
                prev = self.seen.get(p)
                if prev == 'old' or prev == ('done', key):
                    continue
                if prev != ('stable', key):                          # the size must stay the same for one round
                    self.seen[p] = ('stable', key)
                    continue
                self.seen[p] = ('done', key)
                self.deliver(p)

    def deliver(self, path):
        name = os.path.basename(path)
        m = self.target()
        if m is None:
            self.log('download %s: no running machine to give it to' % name)
            self.say('Downloaded %s; no machine is running to receive it. It stays in the hypervisor.' % name,
                     'Pobrano %s; żadna maszyna nie działa, by go odebrać. Zostaje w hipernadzorcy.' % name)
            return
        virus, err = self.scan(path)
        if virus:
            self.log('download %s for %s: %s, not delivered' % (name, m.id, virus))
            self.say('%s: download %s contains %s; it was not given to the machine.' % (m.name, name, virus),
                     '%s: pobrany plik %s zawiera %s; nie trafił do maszyny.' % (m.name, name, virus))
            return
        os.makedirs(folder(m.id), exist_ok=True)
        dest = os.path.join(folder(m.id), name)
        shutil.copy2(path, dest + '.new')
        os.chmod(dest + '.new', 0o644)
        os.replace(dest + '.new', dest)
        self.log('download %s for %s: %s, in the share' % (name, m.id, 'not scanned: ' + err if err else 'clean'))
        if err:
            self.say('%s: %s downloaded; the virus scanner could not check it (%s). It is in the Downloads share.' % (m.name, name, err),
                     '%s: pobrano %s; skaner nie mógł go sprawdzić (%s). Jest w udziale Pobrane.' % (m.name, name, err))
        else:
            self.say('%s: %s downloaded, no malware found; it is in the Downloads share.' % (m.name, name),
                     '%s: pobrano %s, nie znaleziono szkodliwego oprogramowania; jest w udziale Pobrane.' % (m.name, name))
