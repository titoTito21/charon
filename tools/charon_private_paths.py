"""r410: keep the build computer's paths out of built files.

prefix_map(*pairs)  -> compiler options that replace each (directory, name) prefix in __FILE__ and
                       debug information by the neutral name (-ffile-prefix-map), longest first.
check(*files)       -> raises SystemExit when a built file still holds the user's profile directory
                       or the account name inside a typical path, as UTF-8 or UTF-16LE in any letter
                       case. The needles are taken from the running system, so no private string
                       stands in this file.
Used by build-charon-windows.py, build-charondos.py and build-charon-synths.py as their last step:
a build that would need scrubbing afterwards fails instead."""
import os
from pathlib import Path

BS = chr(92)


def prefix_map(*pairs):
    out = []
    for directory, name in sorted(pairs, key=lambda p: -len(str(p[0]))):
        d = str(Path(directory).resolve())
        for form in sorted({d, d.replace(BS, '/')}):
            out.append('-ffile-prefix-map=' + form + '=' + name)
    return out


def needles():
    home = str(Path.home())
    account = os.environ.get('USERNAME') or os.environ.get('USER') or Path(home).name
    words = set()
    for text in (home, home.replace(BS, '/'), 'users' + BS + account, 'users/' + account, 'home/' + account,
                 account + BS + 'documents', account + '/documents', account + BS + 'appdata', account + '/appdata'):
        if len(text) >= 6:
            words.add(text.lower())
    out = []
    for w in sorted(words):
        out.append(w.encode('utf-8'))
        out.append(w.encode('utf-16le'))
    return out


def hits(data):
    low = data.lower()
    return [n for n in needles() if n in low]


def check(*files):
    bad = []
    for f in files:
        f = Path(f)
        if hits(f.read_bytes()):
            bad.append(f.name)
    if bad:
        raise SystemExit('FAILED: private host path inside built file(s): ' + ', '.join(bad))
    return len(files)
