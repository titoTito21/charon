# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: optional Alpine packages fetched only when a feature is first used (ClamAV, Ghostscript, the
headless browser of the remote modern browser, Flatpak). They come from Alpine's official mirror, checked by apk
against Alpine's signing keys (/etc/apk/keys, the same keys as the packages on P1); the files are kept in
/hv/apk-cache, so a later start installs them again without the internet. The size is said before the first fetch."""
import os, re, subprocess

CACHE = '/hv/apk-cache'
REPOS = '/run/charonhv/online-repositories'
MIRROR = 'https://dl-cdn.alpinelinux.org/alpine'


def branch():
    try:
        v = open('/etc/alpine-release').read().strip().split('.')
        return 'v%s.%s' % (v[0], v[1])
    except (OSError, IndexError):
        return 'latest-stable'


def _repos():
    os.makedirs(CACHE, exist_ok=True)
    os.makedirs(os.path.dirname(REPOS), exist_ok=True)
    # Only the official mirror (its indexes are cached in CACHE, so apk resolves again offline later). P1's own
    # repositories are NOT listed: they carry Alpine's full signed APKINDEX but only the packages the hypervisor
    # needs, so apk chose them and failed - measured: "clamav-scanner-1.4.7-r0: package mentioned in index not found".
    b = branch()
    with open(REPOS, 'w') as f:
        f.write('%s/%s/main\n%s/%s/community\n' % (MIRROR, b, MIRROR, b))
    return REPOS


def _apk(args, timeout):
    return subprocess.run(['apk', '--repositories-file', _repos(), '--cache-dir', CACHE] + args,
                          capture_output=True, text=True, timeout=timeout)


def installed(pkgs):
    r = subprocess.run(['apk', 'info', '-e'] + list(pkgs), capture_output=True, text=True)
    have = set(r.stdout.split())
    return all(p in have for p in pkgs)


def _index_sizes():
    """{(name, version): download bytes} from the APKINDEX files apk keeps in the cache (field S)."""
    import tarfile
    sizes = {}
    for f in os.listdir(CACHE):
        if not (f.startswith('APKINDEX.') and f.endswith('.tar.gz')):
            continue
        try:
            with tarfile.open(os.path.join(CACHE, f)) as t:
                text = t.extractfile('APKINDEX').read().decode('utf-8', 'replace')
        except (OSError, KeyError, tarfile.TarError):
            continue
        for block in text.split('\n\n'):
            d = dict(l.split(':', 1) for l in block.splitlines() if len(l) > 2 and l[1] == ':')
            if 'P' in d and 'V' in d and d.get('S', '').isdigit():
                sizes[(d['P'], d['V'])] = int(d['S'])
    return sizes


def plan(pkgs):
    """(megabytes to fetch, number of packages, None) or (0, 0, reason): what installing pkgs would bring."""
    try:
        _apk(['update', '-q'], 180)
        r = _apk(['add', '--simulate'] + list(pkgs), 180)
    except (OSError, subprocess.TimeoutExpired) as e:
        return 0, 0, str(e)
    if r.returncode != 0:
        lines = (r.stderr or r.stdout).strip().splitlines()
        return 0, 0, lines[-1] if lines else 'apk exit %d' % r.returncode
    names = re.findall(r'Installing (\S+) \(([^)]+)\)', r.stdout)
    cached = set(os.listdir(CACHE))
    sizes = _index_sizes()
    total = sum(sizes.get((n, v), 0) for n, v in names if not any(f.startswith('%s-%s.' % (n, v)) for f in cached))
    return round(total / 1048576.0, 1), len(names), None

def install(pkgs, timeout=1800):
    """None, or the reason it failed."""
    try:
        r = _apk(['add', '-q'] + list(pkgs), timeout)
    except (OSError, subprocess.TimeoutExpired) as e:
        return str(e)
    if r.returncode != 0:
        lines = (r.stderr or r.stdout).strip().splitlines()
        return lines[-1] if lines else 'apk exit %d' % r.returncode
    return None
