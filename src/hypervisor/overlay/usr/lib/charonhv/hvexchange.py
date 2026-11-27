# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: the writable exchange drive of a secure machine (6q), as a FAT32 disk image in qcow2.

Measured 2026-11-25 on hvxp1: a writable vvfat drive (fat:rw:DIR) is a migration blocker in QEMU - "The vvfat (rw)
format used by node ... does not support live migration" - so savevm (the daily snapshot, "Take a snapshot now")
failed for every machine that had it. The drive is therefore a real FAT32 image (exchange.qcow2 in the machine's
folder: snapshots include it, so going back to a snapshot also brings the drive's contents back to that moment).

/hv/exchange/<id> stays the hypervisor's side of the exchange:
  - sync_out (machine OFF): the drive's contents are mirrored into the folder (then ClamAV scans the folder);
  - sync_in (machine OFF, just before it starts): files the hypervisor put into the folder that the drive lacks, or
    that are newer than the drive's copy, are written onto the drive (nothing on the drive is deleted by it);
  - read_live: one file read from the drive of a RUNNING machine, read-only and shared (CLIP.TXT).
Only this machine's own image is ever attached; the network block device is always disconnected again."""
import contextlib, os, shutil, subprocess, tempfile, time

RUN = '/run/charonhv'
SIZE_MB = 2048
LABEL = 'CHARONX'


def image(mdir):
    return os.path.join(mdir, 'exchange.qcow2')


def create(mdir):
    """The empty drive, made once: a sparse raw file formatted FAT32 (no partition table: a USB 'superfloppy' XP and
    later read), converted to qcow2 (a few MB on disk)."""
    p = image(mdir)
    if os.path.exists(p):
        return p
    fd, raw = tempfile.mkstemp(prefix='.exchange-', suffix='.raw', dir=mdir)
    os.close(fd)
    try:
        with open(raw, 'r+b') as f:
            f.truncate(SIZE_MB * 1048576)
        r = subprocess.run(['mkfs.vfat', '-F', '32', '-n', LABEL, raw], capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError((r.stderr.strip() or 'mkfs.vfat failed')[:200])
        r = subprocess.run(['qemu-img', 'convert', '-f', 'raw', '-O', 'qcow2', raw, p + '.part'], capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError((r.stderr.strip() or 'qemu-img convert failed')[:200])
        os.replace(p + '.part', p)
    finally:
        for x in (raw, p + '.part'):
            try:
                os.remove(x)
            except OSError:
                pass
    return p


def _free_nbd():
    subprocess.run(['modprobe', 'nbd', 'max_part=16'], capture_output=True)
    for n in range(16):
        try:
            if os.path.exists('/dev/nbd%d' % n) and open('/sys/block/nbd%d/size' % n).read().strip() == '0':
                return '/dev/nbd%d' % n
        except OSError:
            continue
    raise RuntimeError('no free network block device')


@contextlib.contextmanager
def mounted(img, write=False, share=False):
    """Mount the drive's FAT at a private directory. write: read-write (machine must be off); share: read-only access
    to the image of a running machine (qemu-nbd --force-share)."""
    dev = _free_nbd()
    if share:
        # Alpine's qemu-nbd has no --force-share option (measured); the block layer's force-share=on does the same
        cmd = ['qemu-nbd', '-r', '-c', dev, '--image-opts', 'driver=qcow2,force-share=on,file.driver=file,file.filename=' + img]
    else:
        cmd = ['qemu-nbd'] + ([] if write else ['-r']) + ['-f', 'qcow2', '-c', dev, img]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError((r.stderr.strip() or 'qemu-nbd failed')[:200])
    mnt = tempfile.mkdtemp(prefix='exchange-', dir=RUN)
    try:
        ok = False
        for _ in range(20):          # the device answers a moment after qemu-nbd returns
            r = subprocess.run(['mount', '-t', 'vfat', '-o', ('rw' if write else 'ro') + ',utf8,shortname=mixed,uid=0,gid=0', dev, mnt],
                               capture_output=True, text=True)
            if r.returncode == 0:
                ok = True
                break
            time.sleep(0.25)
        if not ok:
            raise RuntimeError((r.stderr.strip() or 'mount failed')[:200])
        try:
            yield mnt
        finally:
            subprocess.run(['umount', mnt], capture_output=True)
    finally:
        subprocess.run(['qemu-nbd', '-d', dev], capture_output=True)
        try:
            os.rmdir(mnt)
        except OSError:
            pass


def _files(root):
    out = {}
    for d, _, names in os.walk(root):
        for n in names:
            p = os.path.join(d, n)
            out[os.path.relpath(p, root)] = p
    return out


def sync_out(mdir, folder):
    """Mirror the drive into FOLDER (machine off). Returns the number of files now in the folder."""
    img = image(mdir)
    if not os.path.exists(img):
        return 0
    os.makedirs(folder, exist_ok=True)
    with mounted(img) as mnt:
        new = folder + '.new'
        shutil.rmtree(new, ignore_errors=True)
        shutil.copytree(mnt, new, copy_function=shutil.copy2)
    old = folder + '.old'
    shutil.rmtree(old, ignore_errors=True)
    os.rename(folder, old)
    os.rename(new, folder)
    shutil.rmtree(old, ignore_errors=True)
    os.chmod(folder, 0o755)
    return len(_files(folder))


def sync_in(mdir, folder, scan=None):
    """Write onto the drive what the hypervisor put into FOLDER and the drive lacks or has older (machine off).
    scan(path) -> (virus or None, error or None): a file with a finding is NOT written. Returns (files written,
    [(relative path, virus)] held back)."""
    img = create(mdir)
    if not os.path.isdir(folder):
        return 0, []
    src = _files(folder)
    if not src:
        return 0, []
    n, held = 0, []
    with mounted(img) as mnt:
        have = _files(mnt)
        todo = [r for r, p in src.items() if r not in have or os.path.getmtime(p) > os.path.getmtime(have[r]) + 2]
    if scan:
        clean = []
        for r in todo:
            virus, _ = scan(src[r])
            if virus:
                held.append((r, virus))
            else:
                clean.append(r)
        todo = clean
    if not todo:
        return 0, held
    with mounted(img, write=True) as mnt:
        for r in todo:
            dst = os.path.join(mnt, r)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src[r], dst)
            n += 1
        subprocess.run(['sync'], capture_output=True)
    return n, held


def read_live(mdir, name, limit=1048576):
    """Bytes of NAME on the drive of a running machine (read-only, shared; the guest may not have flushed yet),
    or None when it is not there. The name is matched case-insensitively at the top of the drive (FAT)."""
    img = image(mdir)
    if not os.path.exists(img):
        return None
    with mounted(img, share=True) as mnt:
        for n in os.listdir(mnt):
            if n.lower() == name.lower():
                with open(os.path.join(mnt, n), 'rb') as f:
                    return f.read(limit)
    return None
