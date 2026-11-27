# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: Linda, the screen reader of the hypervisor's graphical sessions (user, r410).

Linda (https://github.com/Mudb0y/linda, GPL-2.0-or-later, independent of NV Access) is NVDA 2026.3.1 for the Linux
desktop. Its readme delivers it through the Nix package manager only, built on the computer itself; Charon's Linux
installs do the same (src/linux/speech.sh, r409). Here: Nix from Alpine's community repository (nix, LGPL-2.1, checked
by apk like every optional package), its store on the Hypervisor data partition (/hv/nix bound on /nix, never in RAM),
its build folder there too, and `nix profile add github:Mudb0y/linda` into Nix's default profile. Nix brings its own
glibc and Python 3.13, so musl is no obstacle. The size and the time are said before anything is fetched; Orca reads
the modern browser while Linda is being built, and the hypervisor says when Linda is ready.

Linda needs X11 (its Wayland key service needs systemd): the modern browser then runs under Xorg + Openbox
(hvmodern._start_x11). It speaks through speech-dispatcher, whose default module is Charon's own
(sd_charon: every voice of Sound > Voice)."""
import os, subprocess, time

NIX_DATA = '/hv/nix'
BUILD_DIR = '/hv/nix-build'
PROFILE_BIN = '/nix/var/nix/profiles/default/bin'
LINDA = PROFILE_BIN + '/linda'
LOG = '/hv/linda-build.log'
PIDFILE = '/run/charonhv/linda-build.pid'
FLAKE = 'github:Mudb0y/linda'
APK = ('nix', 'nix-openrc', 'xorg-server', 'xf86-input-libinput', 'openbox', 'setxkbmap', 'xkbcomp', 'xkeyboard-config')
# measured: Charon's r409 Linux tests (Fedora 45, Arch: 10 min 44 s / 10 min 47 s, /nix/store 7.5 GB); the
# hypervisor's own run (session 8, hvxp1, 4 virtual CPUs): rc 0 after 1129 s, /nix/store 8.3 GB - the figures said
SIZE_GB, MINUTES = 8.3, 19


def mount():
    """/nix is the data partition's /hv/nix (bound at first use and after every start of the hypervisor)."""
    os.makedirs(NIX_DATA, exist_ok=True)
    os.makedirs('/nix', exist_ok=True)
    if subprocess.run(['mountpoint', '-q', '/nix']).returncode != 0:
        subprocess.run(['mount', '--bind', NIX_DATA, '/nix'], capture_output=True)


def installed():
    if not os.path.isdir(os.path.join(NIX_DATA, 'var')):
        return False
    mount()
    return os.access(LINDA, os.X_OK)


def building():
    try:
        os.kill(int(open(PIDFILE).read()), 0)
        return True
    except (OSError, ValueError):
        return False


def nix_conf():
    os.makedirs('/etc/nix', exist_ok=True)
    path = '/etc/nix/nix.conf'
    text = open(path).read() if os.path.exists(path) else ''
    add = ''
    if 'build-dir' not in text:
        add += 'build-dir = %s\n' % BUILD_DIR       # the build's temporary files on the data partition, not in RAM
    if 'experimental-features' not in text:
        add += 'experimental-features = nix-command flakes\n'
    if add:
        open(path, 'a').write(add)
    os.makedirs(BUILD_DIR, exist_ok=True)


SCRIPT = r'''
echo "=== linda build $(date)"
t0=$(date +%%s)
rc-service nix-daemon start >/dev/null 2>&1 || rc-service nix-daemon restart
HOME=/root nix profile add --profile /nix/var/nix/profiles/default %s
rc=$?
[ -x %s ] || [ $rc -ne 0 ] || rc=99
echo "=== rc $rc after $(( $(date +%%s) - t0 )) s"
''' % (FLAKE, LINDA)


WANTED = '/hv/linda-build.wanted'     # on the data partition: "a build was started and has not ended" (survives a restart)


def _stat():
    """(bytes used, bytes free for us) on the data partition."""
    s = os.statvfs('/hv')
    return (s.f_blocks - s.f_bfree) * s.f_frsize, s.f_bavail * s.f_frsize


def _used():
    return _stat()[0]


def _wanted():
    """(first start time, bytes used on /hv at the first start) or None."""
    try:
        t, u = open(WANTED).read().split()[:2]
        return float(t), int(u)
    except (OSError, ValueError):
        return None


def space_problem():
    """None, or the reason in English and Polish: checked BEFORE anything is fetched or built. What an interrupted
    build already put into the store counts as done."""
    w = _wanted()
    done = max(0, _used() - w[1]) if w else 0
    need = max(0.0, (SIZE_GB + 3) * 1e9 - done)
    free = _stat()[1]
    if free < need:
        gb = '%.1f' % ((need - free) / 1e9)
        return ('Linda needs %s more gigabytes free on the Hypervisor partition' % gb,
                'Linda potrzebuje jeszcze %s gigabajta wolnego miejsca na partycji Hipernadzorcy' % gb.replace('.', ','))
    return None


def build_start():
    """Starts (or RESUMES) the build in the background; it survives a restart of the menu, and after a restart of the
    hypervisor at_start() resumes it: Nix keeps every store path already fetched or built, only the rest is done.
    Returns None or the reason (English, Polish)."""
    if building():
        return None
    err = space_problem()
    if err:
        return err
    mount()
    nix_conf()
    # an interrupted build leaves its temporary folders: removed, Nix starts those derivations again cleanly
    for n in os.listdir(BUILD_DIR):
        subprocess.run(['rm', '-rf', os.path.join(BUILD_DIR, n)], capture_output=True)
    if _wanted() is None:
        open(WANTED, 'w').write('%d %d\n' % (time.time(), _used()))
    with open(LOG, 'ab') as f:
        p = subprocess.Popen(['sh', '-c', SCRIPT], stdin=subprocess.DEVNULL, stdout=f, stderr=f, start_new_session=True)
    open(PIDFILE, 'w').write(str(p.pid))
    build_start.proc = p
    return None


def at_start():
    """At every start of the menu (so also after a reboot): /nix bound to /hv/nix when anything of Nix is there, and
    'resume' when a build was started and neither finished nor running (the caller fetches the packages from the
    data partition's cache and calls build_start again)."""
    if os.path.isdir(os.path.join(NIX_DATA, 'var')):
        mount()
    if _wanted() and not building() and not os.access(LINDA, os.X_OK):
        try:
            last = [l for l in open(LOG, errors='replace').read().splitlines() if l.startswith('=== ')][-1]
        except (OSError, IndexError):
            last = ''
        if not last.startswith('=== rc '):        # it did not end by itself: interrupted (reboot, power)
            return 'resume'
    return None


def progress():
    """(percent, minutes left) estimated from what the store has grown since the first start against the measured
    8.3 GB - 'about', never more than 99 % before the build says it has ended."""
    w = _wanted()
    if not w:
        return None
    pct = int(min(99, max(0, round((_used() - w[1]) * 100 / (SIZE_GB * 1e9)))))
    return pct, max(1, int(round(MINUTES * (100 - pct) / 100.0)))


def ended():
    """(rc, seconds) once when a started build has ended - also one started before a restart of the menu; the
    marker is removed so it is said only once."""
    if _wanted() is None or building():
        return None
    p = getattr(build_start, 'proc', None)
    if p is not None and p.poll() is None:
        return None
    r = result()
    if r is None and p is not None:
        r = (p.returncode, 0)
    if r is None:
        return None
    try:
        os.remove(WANTED)
    except OSError:
        pass
    return r


def result():
    """None while building or never built; (rc, seconds) of the last build."""
    try:
        lines = [l for l in open(LOG, errors='replace').read().splitlines() if l.startswith('=== ')]
    except OSError:
        return None
    # the LAST attempt only: an attempt cut by a reboot ends with its start line, not with an older attempt's rc
    if not lines or not lines[-1].startswith('=== rc ') or building():
        return None
    parts = lines[-1].split()
    try:
        return int(parts[2]), int(parts[4])
    except (IndexError, ValueError):
        return None


def last_error():
    try:
        lines = [l for l in open(LOG, errors='replace').read().splitlines() if l.strip() and not l.startswith('===')]
        return lines[-1][:160] if lines else ''
    except OSError:
        return ''


def config(home, polish):
    """Linda's own configuration (NVDA's nvda.ini) on first use: speech-dispatcher (Charon's voices through sd_charon),
    no welcome dialog, Charon's language. Later changes the user makes in Linda stay."""
    d = os.path.join(home, '.config', 'linda')
    os.makedirs(d, exist_ok=True)
    ini = os.path.join(d, 'nvda.ini')
    if os.path.exists(ini):
        return ini
    open(ini, 'w').write('[general]\n\tlanguage = %s\n\tshowWelcomeDialogAtStartup = False\n\taskToExit = False\n'
                         '[update]\n\taskedAllowUsageStats = True\n\tallowUsageStats = False\n'
                         '[speech]\n\tsynth = speechDispatcher\n' % ('pl' if polish else 'en'))
    return ini
