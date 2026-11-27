# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: malware scanning FROM the hypervisor with ClamAV (6q).

ClamAV (GPL-2.0, Cisco Talos; Alpine packages clamav-daemon, clamav-scanner, freshclam) is fetched the first time
scanning is switched on; its signature database comes from ClamAV's own mirror (database.clamav.net) through
freshclam, into /hv/clamav, refreshed once a day. The daemon clamd scans:
  - downloads that pass the guest's proxy (programs, archives, documents) before the guest gets them (INSTREAM);
  - the shared folder and the exchange folders on request and when a machine stops;
  - the disk of a machine that is OFF, read-only (qemu-nbd -r, mounted read-only: NTFS, FAT, ext4).
Findings are lines "FOUND path virus" for the menu to speak; nothing is deleted or moved by the scanner."""
import os, socket, struct, subprocess, time

DB = '/hv/clamav'
RUN = '/run/charonhv'
SOCK = RUN + '/clamd.sock'
CONF = RUN + '/clamd.conf'
FCONF = RUN + '/freshclam.conf'
PKGS = ('clamav-daemon', 'clamav-scanner', 'clamav-clamdscan', 'freshclam', 'clamav-libunrar')
MAXSTREAM = 200 * 1048576


def ready():
    return os.path.exists(os.path.join(DB, 'main.cvd')) or os.path.exists(os.path.join(DB, 'main.cld'))


def signature_age_days(db=DB):
    """Days since the newest ClamAV signature file changed (daily/main, .cvd or .cld); None without signatures."""
    times = [os.path.getmtime(os.path.join(db, n)) for n in ('daily.cvd', 'daily.cld', 'main.cvd', 'main.cld')
             if os.path.exists(os.path.join(db, n))]
    return (time.time() - max(times)) / 86400 if times else None


def _confs():
    os.makedirs(DB, exist_ok=True)
    os.makedirs(RUN, exist_ok=True)
    try:
        import pwd
        pw = pwd.getpwnam('clamav')
        os.chown(DB, pw.pw_uid, pw.pw_gid)
    except (ImportError, KeyError, OSError):
        pass
    open(CONF, 'w').write('LocalSocket %s\nDatabaseDirectory %s\nStreamMaxLength %dM\nMaxFileSize %dM\nMaxScanSize 400M\n'
                          'LogFile %s/clamd.log\nForeground no\nUser root\n' % (SOCK, DB, MAXSTREAM // 1048576, MAXSTREAM // 1048576, RUN))
    open(FCONF, 'w').write('DatabaseDirectory %s\nDatabaseMirror database.clamav.net\nUpdateLogFile %s/freshclam.log\n'
                           'DatabaseOwner root\nNotifyClamd %s\n' % (DB, RUN, CONF))


def update_db(log, timeout=1500):
    """freshclam: None or the reason. Once a day at most (ClamAV's mirror limits frequent downloads)."""
    _confs()
    stamp = os.path.join(DB, '.charon-updated')
    if ready() and os.path.exists(stamp) and time.time() - os.path.getmtime(stamp) < 86400:
        return None
    try:
        r = subprocess.run(['freshclam', '--config-file=' + FCONF, '--stdout'], capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as e:
        return str(e)
    log('freshclam: %s' % ' | '.join(l for l in r.stdout.splitlines() if 'updated' in l or 'ERROR' in l or 'WARNING' in l)[:400])
    if not ready():
        lines = [l for l in (r.stdout + r.stderr).splitlines() if 'ERROR' in l]
        return lines[-1] if lines else 'freshclam exit %d' % r.returncode
    open(stamp, 'w').close()
    return None


def db_megabytes():
    try:
        return round(sum(os.path.getsize(os.path.join(DB, f)) for f in os.listdir(DB)) / 1048576.0)
    except OSError:
        return 0


def clamd_running():
    try:
        s = socket.socket(socket.AF_UNIX)
        s.settimeout(3)
        s.connect(SOCK)
        s.sendall(b'zPING\0')
        ok = s.recv(16).startswith(b'PONG')
        s.close()
        return ok
    except OSError:
        return False


def start_clamd(log, wait=180):
    """None or the reason. The database loads in ~20-60 s and takes about 1 GB of memory."""
    if clamd_running():
        return None
    _confs()
    try:
        subprocess.Popen(['clamd', '--config-file=' + CONF], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=open(RUN + '/clamd.err', 'wb'))
    except OSError as e:
        return str(e)
    end = time.time() + wait
    while time.time() < end:
        if clamd_running():
            log('clamd ready')
            return None
        time.sleep(1)
    try:
        return open(RUN + '/clamd.err').read().strip().splitlines()[-1][:200]
    except (OSError, IndexError):
        return 'clamd did not answer in %d seconds' % wait


def scan_bytes_file(path):
    """Scan one file through clamd INSTREAM: (None, None) clean, (virus, None) found, (None, error) not scanned."""
    try:
        s = socket.socket(socket.AF_UNIX)
        s.settimeout(300)
        s.connect(SOCK)
        s.sendall(b'zINSTREAM\0')
        with open(path, 'rb') as f:
            while True:
                b = f.read(1 << 20)
                if not b:
                    break
                s.sendall(struct.pack('>I', len(b)) + b)
        s.sendall(struct.pack('>I', 0))
        ans = b''
        while not ans.endswith(b'\0'):
            d = s.recv(4096)
            if not d:
                break
            ans += d
        s.close()
    except OSError as e:
        return None, str(e)
    a = ans.rstrip(b'\0').decode('utf-8', 'replace')
    if a.endswith('FOUND'):
        return a.split(':', 1)[1].strip()[:-5].strip(), None
    if a.endswith('OK'):
        return None, None
    return None, a[:200]


def scan_tree(path, timeout=7200):
    """[(file, virus)], error or None. clamdscan when the daemon runs (fast), else clamscan (loads the database)."""
    if clamd_running():
        cmd = ['clamdscan', '--config-file=' + CONF, '--multiscan', '--infected', '--no-summary', '--fdpass', path]
    else:
        cmd = ['clamscan', '--database=' + DB, '-r', '--infected', '--no-summary', path]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as e:
        return [], str(e)
    found = []
    for line in r.stdout.splitlines():
        if line.endswith(' FOUND') and ': ' in line:
            f, v = line[:-6].rsplit(': ', 1)
            found.append((f, v))
    err = None
    if r.returncode not in (0, 1):
        lines = [l for l in (r.stderr + r.stdout).splitlines() if l.strip() and not l.endswith(' FOUND')]
        err = lines[-1][:200] if lines else 'exit %d' % r.returncode
    return found, err


def scan_disk(image, log, timeout=7200):
    """Read-only scan of an OFF machine's disk image: [(file inside the guest, virus)], [partitions scanned], error."""
    subprocess.run(['modprobe', 'nbd', 'max_part=16'], capture_output=True)
    dev = None
    for n in range(16):
        d = '/dev/nbd%d' % n
        try:
            if os.path.exists(d) and open('/sys/block/nbd%d/size' % n).read().strip() == '0':
                dev = d
                break
        except OSError:
            continue
    if not dev:
        return [], [], 'no free network block device'
    fmt = 'qcow2' if image.endswith('.qcow2') else 'raw'
    r = subprocess.run(['qemu-nbd', '-r', '-f', fmt, '-c', dev, image], capture_output=True, text=True)
    if r.returncode != 0:
        return [], [], (r.stderr.strip() or 'qemu-nbd failed')[:200]
    found, parts, err = [], [], None
    mnt = RUN + '/scan-mnt'
    try:
        time.sleep(1.5)
        subprocess.run(['partprobe', dev], capture_output=True)
        cands = sorted(p for p in os.listdir('/dev') if p.startswith(os.path.basename(dev) + 'p')) or [os.path.basename(dev)]
        for p in cands:
            os.makedirs(mnt, exist_ok=True)
            ok = False
            for fs, opt in (('ntfs3', 'ro'), ('vfat', 'ro'), ('ext4', 'ro,noload'), ('exfat', 'ro')):
                if subprocess.run(['mount', '-t', fs, '-o', opt, '/dev/' + p, mnt], capture_output=True).returncode == 0:
                    ok = True
                    break
            if not ok:
                continue
            parts.append(p)
            try:
                f, e = scan_tree(mnt, timeout)
                found += [(x[len(mnt):] or '/', v) for x, v in f]
                err = err or e
            finally:
                subprocess.run(['umount', mnt], capture_output=True)
    finally:
        subprocess.run(['qemu-nbd', '-d', dev], capture_output=True)
    if not parts and not err:
        err = 'no partition could be read'
    log('disk scan %s: %d partitions, %d found, %s' % (image, len(parts), len(found), err))
    return found, parts, err
