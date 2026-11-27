#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Charon Hypervisor: host reference of the install-time download (design part 2) and test media.
#
#   charon-hv-fetch.py fetch --cache DIR --tree DIR [--branch v3.24]
#       What Charon's install step does (CharonWsHypervisor.h), on the host: the official Alpine netboot tarball (checked
#       against its .sha256), the signed main + community indexes (RSA PKCS#1 SHA-1 checked with Alpine's public keys from
#       src/hypervisor/keys), the dependency closure of src/hypervisor/world.txt, every .apk checked by the index's C:
#       field, and Charon's overlay (src/hypervisor/overlay -> charonhv.apkovl.tar.gz). TREE becomes P1's content.
#   charon-hv-fetch.py disk --tree DIR --out FILE.img [--boot-mib 2048] [--data-gib 16]
#       A GPT disk image: P1 "Hypervisor" FAT32 CHARONHV with TREE, P2 "Hypervisor data" (first MiB zero), as the plan
#       makes them. For QEMU tests; convert with qemu-img afterwards.
#   charon-hv-fetch.py overlay --tree DIR        only rebuild charonhv.apkovl.tar.gz (and charonhv.cfg) in TREE
# Only the Python standard library.
import argparse, base64, gzip, hashlib, io, json, os, re, struct, sys, tarfile, time, urllib.request, uuid, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, '..', 'src', 'hypervisor'))
MIRROR = 'https://dl-cdn.alpinelinux.org/alpine'
ARCH = 'x86_64'
REPOS = ('main', 'community')


def log(*a):
    print(*a, flush=True)


def get(url, dest=None, tries=3):
    for attempt in range(tries):
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'charon-hv-fetch'})
            with urllib.request.urlopen(req, timeout=120) as r:
                if dest is None:
                    return r.read()
                tmp = dest + '.part'
                with open(tmp, 'wb') as f:
                    while True:
                        b = r.read(1 << 20)
                        if not b:
                            break
                        f.write(b)
                os.replace(tmp, dest)
                return dest
        except Exception as e:
            log('  retry %d %s: %s' % (attempt + 1, url, e))
            time.sleep(3)
    raise SystemExit('FAILED to download ' + url)


# ---- RSA PKCS#1 v1.5 verification (public keys in SubjectPublicKeyInfo PEM) ----
def der_read(b, i):
    tag = b[i]; i += 1
    n = b[i]; i += 1
    if n & 0x80:
        k = n & 0x7f
        n = int.from_bytes(b[i:i + k], 'big'); i += k
    return tag, b[i:i + n], i + n


def rsa_key(pem):
    der = base64.b64decode(''.join(l for l in pem.splitlines() if l and not l.startswith('-----')))
    _, spki, _ = der_read(der, 0)
    _, alg, i = der_read(spki, 0)
    _, bits, _ = der_read(spki, i)
    _, seq, _ = der_read(bits[1:], 0)
    _, n, j = der_read(seq, 0)
    _, e, _ = der_read(seq, j)
    return int.from_bytes(n, 'big'), int.from_bytes(e, 'big')


SHA1_PREFIX = bytes.fromhex('3021300906052b0e03021a05000414')
SHA256_PREFIX = bytes.fromhex('3031300d060960864801650304020105000420')


def rsa_verify(key, sig, digest, prefix):
    n, e = key
    k = (n.bit_length() + 7) // 8
    m = pow(int.from_bytes(sig, 'big'), e, n).to_bytes(k, 'big')
    want = b'\x00\x01' + b'\xff' * (k - 3 - len(prefix) - len(digest)) + b'\x00' + prefix + digest
    return m == want


def keys():
    out = {}
    d = os.path.join(SRC, 'keys')
    for name in os.listdir(d):
        out[name] = rsa_key(open(os.path.join(d, name)).read())
    return out


def gzip_streams(data):
    """The raw bytes of each concatenated gzip stream (apk v2 format)."""
    out = []
    at = 0
    while at < len(data):
        d = zlib.decompressobj(31)
        d.decompress(data[at:])
        used = len(data) - at - len(d.unused_data)
        out.append(data[at:at + used])
        at += used
    return out


def tar_members(raw_stream):
    """Members of one gzip stream that holds a tar segment (no end blocks)."""
    plain = gzip.decompress(raw_stream)
    out = {}
    at = 0
    while at + 512 <= len(plain):
        h = plain[at:at + 512]
        if not h.strip(b'\0'):
            break
        name = h[:100].split(b'\0')[0].decode()
        size = int(h[124:136].split(b'\0')[0].strip() or b'0', 8)
        out[name] = plain[at + 512:at + 512 + size]
        at += 512 + (size + 511) // 512 * 512
    return out


def verify_signed(data, allkeys, what):
    streams = gzip_streams(data)
    if len(streams) < 2:
        raise SystemExit('REFUSED %s: not a signed apk archive' % what)
    sig = tar_members(streams[0])
    for name, value in sig.items():
        m = re.match(r'\.SIGN\.RSA(256)?\.(.+)$', name)
        if not m or m.group(2) not in allkeys:
            continue
        if m.group(1):
            ok = rsa_verify(allkeys[m.group(2)], value, hashlib.sha256(streams[1]).digest(), SHA256_PREFIX)
        else:
            ok = rsa_verify(allkeys[m.group(2)], value, hashlib.sha1(streams[1]).digest(), SHA1_PREFIX)
        if ok:
            return streams, m.group(2)
        raise SystemExit('REFUSED %s: signature by %s does not match' % (what, m.group(2)))
    raise SystemExit('REFUSED %s: no signature by a known Alpine key (%s)' % (what, ', '.join(sig)))


# ---- index and dependency closure ----
def parse_index(text, repo):
    pkgs = []
    for block in text.split('\n\n'):
        p = {'repo': repo}
        for line in block.splitlines():
            if len(line) > 2 and line[1] == ':':
                k, v = line[0], line[2:]
                p[k] = v
        if 'P' in p:
            pkgs.append(p)
    return pkgs


def dep_name(tok):
    return re.split(r'[<>=~]', tok, maxsplit=1)[0]


class Index:
    def __init__(self, pkgs):
        self.by_name = {}
        self.providers = {}
        for p in pkgs:
            self.by_name.setdefault(p['P'], p)
            for prov in p.get('p', '').split():
                self.providers.setdefault(dep_name(prov), []).append(p)

    def find(self, name):
        if name in self.by_name:
            return self.by_name[name]
        cands = self.providers.get(name, [])
        if not cands:
            return None
        return sorted(cands, key=lambda p: -int(p.get('k', '0')))[0]

    def satisfied(self, tok, chosen):
        name = dep_name(tok)
        for p in chosen.values():
            if p['P'] == name or any(dep_name(x) == name for x in p.get('p', '').split()):
                return True
        return False

    def closure(self, world):
        chosen = {}
        todo = list(world)
        missing = []
        while True:
            while todo:
                tok = todo.pop()
                if tok.startswith('!'):
                    continue
                if self.satisfied(tok, chosen):
                    continue
                p = self.find(dep_name(tok))
                if p is None:
                    missing.append(tok)
                    continue
                chosen[p['P']] = p
                todo.extend(p.get('D', '').split())
            added = False
            for p in self.by_name.values():           # install_if: apk adds these by itself
                cond = p.get('i', '').split()
                if cond and p['P'] not in chosen and all(self.satisfied(c, chosen) for c in cond if not c.startswith('!')):
                    chosen[p['P']] = p
                    todo.extend(p.get('D', '').split())
                    added = True
            if not added and not todo:
                break
        return chosen, missing


def iso_boot_files(path, names):
    """Files of the ISO's /boot by their Rock Ridge names (the way Charon reads the stream: PVD, root, boot, extents)."""
    f = open(path, 'rb')
    def sec(n, c=1):
        f.seek(n * 2048); return f.read(2048 * c)
    def ls(lba, size):
        d = sec(lba, (size + 2047) // 2048); i = 0; out = {}
        while i < len(d):
            n = d[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048; continue
            r = d[i:i + n]; nl = r[32]; name = r[33:33 + nl].decode('latin-1')
            su = r[33 + nl + ((nl + 1) % 2):]; j = 0
            while j + 4 <= len(su) and su[j + 2]:
                if su[j:j + 2] == b'NM':
                    name = su[j + 5:j + su[j + 2]].decode('latin-1')
                j += su[j + 2]
            out[name] = (struct.unpack('<I', r[2:6])[0], struct.unpack('<I', r[10:14])[0])
            i += n
        return out
    pvd = sec(16)
    if pvd[1:6] != b'CD001':
        raise SystemExit('REFUSED: not an ISO 9660 image')
    root = ls(struct.unpack('<I', pvd[158:162])[0], struct.unpack('<I', pvd[166:170])[0])
    boot = ls(*root['boot'])
    out = {}
    for n in names:
        lba, size = boot[n]
        f.seek(lba * 2048); out[n] = f.read(size)
    return out


def cmd_fetch(a):
    os.makedirs(a.cache, exist_ok=True)
    os.makedirs(a.tree, exist_ok=False)          # the target must be new
    allkeys = keys()
    report = []
    rel = get('%s/%s/releases/%s/latest-releases.yaml' % (MIRROR, a.branch, ARCH)).decode()
    m = re.search(r'(alpine-standard-([0-9.]+)-%s\.iso)' % ARCH, rel)
    isoname, version = m.group(1), m.group(2)
    base = '%s/%s/releases/%s/' % (MIRROR, a.branch, ARCH)
    want = get(base + isoname + '.sha256').decode().split()[0]
    path = os.path.join(a.cache, isoname)
    if not os.path.exists(path) or hashlib.sha256(open(path, 'rb').read()).hexdigest() != want:
        log('download', base + isoname)
        get(base + isoname, path)
    have = hashlib.sha256(open(path, 'rb').read()).hexdigest()
    if have != want:
        raise SystemExit('REFUSED standard ISO: sha256 %s, expected %s' % (have, want))
    report.append('standard ISO %s sha256 %s (from %s.sha256)' % (isoname, have, base + isoname))
    os.makedirs(os.path.join(a.tree, 'boot'))
    for name, data in iso_boot_files(path, ('vmlinuz-lts', 'initramfs-lts', 'modloop-lts')).items():
        open(os.path.join(a.tree, 'boot', name), 'wb').write(data)
        report.append('  boot/%s %d bytes sha256 %s' % (name, len(data), hashlib.sha256(data).hexdigest()))
    pkgs = []
    raw_index = {}
    for repo in REPOS:
        url = '%s/%s/%s/%s/APKINDEX.tar.gz' % (MIRROR, a.branch, repo, ARCH)
        data = get(url)
        streams, key = verify_signed(data, allkeys, url)
        text = tar_members(streams[1])['APKINDEX'].decode()
        raw_index[repo] = data
        pkgs += parse_index(text, repo)
        report.append('index %s signed by %s' % (url, key))
    idx = Index(pkgs)
    world = [l.strip() for l in open(os.path.join(SRC, 'world.txt')) if l.strip() and not l.startswith('#')]
    chosen, missing = idx.closure(world)
    if missing:
        raise SystemExit('REFUSED: not in the repositories: ' + ' '.join(missing))
    total = 0
    for repo in REPOS:
        d = os.path.join(a.tree, 'apks', repo, ARCH)
        os.makedirs(d)
        open(os.path.join(a.tree, 'apks', repo, '.boot_repository'), 'w').close()
        open(os.path.join(d, 'APKINDEX.tar.gz'), 'wb').write(raw_index[repo])
    for name in sorted(chosen):
        p = chosen[name]
        fn = '%s-%s.apk' % (p['P'], p['V'])
        cpath = os.path.join(a.cache, fn)
        url = '%s/%s/%s/%s/%s' % (MIRROR, a.branch, p['repo'], ARCH, fn)
        if not os.path.exists(cpath):
            get(url, cpath)
        data = open(cpath, 'rb').read()
        streams = gzip_streams(data)
        q1 = 'Q1' + base64.b64encode(hashlib.sha1(streams[1]).digest()).decode()
        if q1 != p.get('C'):
            os.remove(cpath)
            raise SystemExit('REFUSED %s: control checksum %s, index says %s' % (fn, q1, p.get('C')))
        open(os.path.join(a.tree, 'apks', p['repo'], ARCH, fn), 'wb').write(data)
        total += len(data)
        report.append('apk %s/%s %d' % (p['repo'], fn, len(data)))
    report.insert(0, 'Alpine %s, %d packages, %d bytes of packages' % (version, len(chosen), total))
    write_overlay(a.tree)
    open(os.path.join(a.tree, 'charonhv', 'fetch-report.txt'), 'w', newline='\n').write('\n'.join(report) + '\n')
    log(report[0])


# ---- Charon's overlay (apkovl) ----
def tvox_sources(now):
    """Charon's TVox sources for the build at first start: src/tts/{tvox_native,runtime}, src/efi/CharonFormatting.h
    (LiveVoice includes ../../efi/CharonFormatting.h), the hosted front end and build-tvox.sh; gzip'd tar."""
    tree = os.path.dirname(SRC)                       # src/
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode='w') as t:
        def add(name, path):
            data = open(path, 'rb').read()
            if name.endswith(('.sh', '.c', '.cpp', '.h', '.S')):
                data = data.replace(b'\r\n', b'\n')
            ti = tarfile.TarInfo(name); ti.size = len(data); ti.mode = 0o644; ti.mtime = now; t.addfile(ti, io.BytesIO(data))
        for sub in ('tvox_native', 'runtime'):
            base = os.path.join(tree, 'tts', sub)
            for f in sorted(os.listdir(base)):
                if os.path.isfile(os.path.join(base, f)):
                    add('src/tts/%s/%s' % (sub, f), os.path.join(base, f))
        add('src/efi/CharonFormatting.h', os.path.join(tree, 'efi', 'CharonFormatting.h'))
        # 6g: Charon's screen model for the guest reader, built in the same compiler session
        add('src/efi/CharonScreenModel.h', os.path.join(tree, 'efi', 'CharonScreenModel.h'))
        add('charonhv-screen.cpp', os.path.join(SRC, 'screen', 'charonhv-screen.cpp'))
        add('charonhv-tvox.cpp', os.path.join(SRC, 'tvox', 'charonhv-tvox.cpp'))
        add('build-tvox.sh', os.path.join(SRC, 'tvox', 'build-tvox.sh'))
    out = io.BytesIO()
    with gzip.GzipFile(fileobj=out, mode='wb', mtime=now, compresslevel=9) as g:
        g.write(buf.getvalue())
    return out.getvalue()


def write_overlay(tree, data_guid=None, language='en', bin_dir=None):
    os.makedirs(os.path.join(tree, 'charonhv'), exist_ok=True)
    cfg = os.path.join(tree, 'charonhv', 'charonhv.cfg')
    if not os.path.exists(cfg):
        open(cfg, 'w', newline='\n').write('# Charon Hypervisor 1\nversion=r410\ndata=%s\nlanguage=%s\n'
                                           % (data_guid or '', language))
    if not os.path.exists(os.path.join(tree, 'charonhv', 'machines.txt')):
        open(os.path.join(tree, 'charonhv', 'machines.txt'), 'w', newline='\n').write('# Charon Hypervisor machines 1\n')
    ovl = os.path.join(SRC, 'overlay')
    world = [l.strip() for l in open(os.path.join(SRC, 'world.txt')) if l.strip() and not l.startswith('#')]
    buf = io.BytesIO()
    now = int(time.time())
    with tarfile.open(fileobj=buf, mode='w') as t:
        def add_dir(name):
            ti = tarfile.TarInfo(name); ti.type = tarfile.DIRTYPE; ti.mode = 0o755; ti.mtime = now; t.addfile(ti)
        def add_file(name, data, mode=0o644):
            ti = tarfile.TarInfo(name); ti.size = len(data); ti.mode = mode; ti.mtime = now; t.addfile(ti, io.BytesIO(data))
        def add_link(name, target):
            ti = tarfile.TarInfo(name); ti.type = tarfile.SYMTYPE; ti.linkname = target; ti.mtime = now; t.addfile(ti)
        seen = set()
        for root, dirs, files in os.walk(ovl):
            dirs.sort()
            rel = os.path.relpath(root, ovl).replace('\\', '/')
            if rel != '.':
                add_dir(rel); seen.add(rel)
            for f in sorted(files):
                name = f if rel == '.' else rel + '/' + f
                data = open(os.path.join(root, f), 'rb').read().replace(b'\r\n', b'\n')
                exe = name.startswith(('etc/init.d/', 'usr/bin/', 'usr/sbin/', 'etc/local.d/')) or data.startswith(b'#!')
                add_file(name, data, 0o755 if exe else 0o644)
        add_file('etc/apk/world', ('\n'.join(world) + '\n').encode())
        add_file('etc/.default_boot_services', b'')
        for d in ('etc/runlevels', 'etc/runlevels/default'):
            if d not in seen:
                add_dir(d)
        add_link('etc/runlevels/default/charonhv', '/etc/init.d/charonhv')
        add_link('etc/runlevels/default/local', '/etc/init.d/local')
        # Charon's own settings reader (tools/charon-os), used for the voice settings
        add_file('usr/lib/charonhv/charon_settings.py',
                 open(os.path.join(HERE, 'charon-os', 'linux', 'charon-settings'), 'rb').read().replace(b'\r\n', b'\n'))
        # r410 6f: the voices built for Alpine from Charon's own sources (src/hypervisor/tvox, src/hypervisor/rhvoice;
        # build-tvox.sh / build-rhvoice.sh), static musl programs, copied unchanged (never line-ending converted)
        # 6m: TVox is NOT shipped as a program; it is built at first start from this small source archive (hvvoice.build_tvox)
        add_file('usr/lib/charonhv/tvox-src.tar.gz', tvox_sources(now))
        for prog in ('charonhv-rhvoice',):
            path = os.path.join(bin_dir, prog) if bin_dir else ''
            if path and os.path.exists(path):
                add_file('usr/lib/charonhv/' + prog, open(path, 'rb').read(), 0o755)
            elif bin_dir:
                raise SystemExit('missing %s in %s' % (prog, bin_dir))
    out = os.path.join(tree, 'charonhv.apkovl.tar.gz')
    with gzip.GzipFile(out, 'wb', mtime=now) as g:
        g.write(buf.getvalue())
    log('overlay', out, os.path.getsize(out))


def cmd_overlay(a):
    write_overlay(a.tree, bin_dir=a.bin)


# ---- GPT + FAT32 image ----
def crc32(b):
    return zlib.crc32(b) & 0xffffffff


def guid_bytes(u):
    return uuid.UUID(u).bytes_le


class Fat32:
    def __init__(self, f, first, sectors, label):
        self.f, self.first, self.sectors = f, first, sectors
        self.spc = 8
        self.reserved = 32
        clusters_max = (sectors - self.reserved) // self.spc
        self.fat_sectors = ((clusters_max + 2) * 4 + 511) // 512
        self.data = self.reserved + 2 * self.fat_sectors
        self.clusters = (sectors - self.data) // self.spc
        self.cs = self.spc * 512
        self.fat = [0x0FFFFFF8, 0x0FFFFFFF]
        self.next = 2
        self.label = label.ljust(11)[:11].encode()
        self.root = self.alloc_dir()

    def alloc(self, nbytes):
        n = max(1, (nbytes + self.cs - 1) // self.cs)
        start = self.next
        for c in range(start, start + n):
            self.fat.append(c + 1)
        self.fat[-1] = 0x0FFFFFFF
        self.next += n
        if self.next - 2 > self.clusters:
            raise SystemExit('FAT volume full')
        return start, n

    def alloc_dir(self):
        return {'entries': [], 'children': {}}

    def cluster_off(self, c):
        return (self.first + self.data + (c - 2) * self.spc) * 512

    def write_at(self, off, data):
        self.f.seek(off)
        self.f.write(data)

    @staticmethod
    def short_name(name, used):
        base, _, ext = name.upper().rpartition('.') if '.' in name else (name.upper(), '', '')
        clean = lambda s: re.sub(r'[^A-Z0-9_]', '_', s)
        base, ext = clean(base)[:8], clean(ext)[:3]
        if name.upper() == (base + ('.' + ext if ext else '')) and (base, ext) not in used and name == name.upper():
            used.add((base, ext))
            return (base.ljust(8) + ext.ljust(3)).encode(), False
        for i in range(1, 999999):
            tail = '~%d' % i
            b = base[:8 - len(tail)] + tail
            if (b, ext) not in used:
                used.add((b, ext))
                return (b.ljust(8) + ext.ljust(3)).encode(), True
        raise SystemExit('names')

    def entries_for(self, name, attr, cluster, size, used):
        short, need_lfn = self.short_name(name, used)
        out = b''
        if need_lfn:
            chk = 0
            for ch in short:
                chk = (((chk & 1) << 7) + (chk >> 1) + ch) & 0xff
            units = list(name.encode('utf-16-le'))
            u16 = [units[i] | (units[i + 1] << 8) for i in range(0, len(units), 2)] + [0]
            while len(u16) % 13:
                u16.append(0xffff)
            parts = [u16[i:i + 13] for i in range(0, len(u16), 13)]
            for seq in range(len(parts), 0, -1):
                p = parts[seq - 1]
                e = bytearray(32)
                e[0] = seq | (0x40 if seq == len(parts) else 0)
                for k in range(5): struct.pack_into('<H', e, 1 + 2 * k, p[k])
                e[11] = 0x0f; e[13] = chk
                for k in range(6): struct.pack_into('<H', e, 14 + 2 * k, p[5 + k])
                for k in range(2): struct.pack_into('<H', e, 28 + 2 * k, p[11 + k])
                out += bytes(e)
        e = bytearray(32)
        e[0:11] = short
        e[11] = attr
        struct.pack_into('<HHHHHHHI', e, 14, 0, 0x5b79, 0x5b79, cluster >> 16, 0, 0x5b79, cluster & 0xffff, size)
        struct.pack_into('<H', e, 20, cluster >> 16)
        struct.pack_into('<H', e, 26, cluster & 0xffff)
        struct.pack_into('<I', e, 28, size)
        return out + bytes(e)

    def build(self, tree):
        def walk(path, parent_cluster, own_cluster, is_root):
            used = set()
            raw = b''
            if is_root:
                e = bytearray(32); e[0:11] = self.label; e[11] = 0x08; raw += bytes(e)
            else:
                e = bytearray(32); e[0:11] = b'.          '; e[11] = 0x10
                struct.pack_into('<H', e, 20, own_cluster >> 16); struct.pack_into('<H', e, 26, own_cluster & 0xffff); raw += bytes(e)
                e = bytearray(32); e[0:11] = b'..         '; e[11] = 0x10
                pc = 0 if parent_cluster == 2 else parent_cluster
                struct.pack_into('<H', e, 20, pc >> 16); struct.pack_into('<H', e, 26, pc & 0xffff); raw += bytes(e)
                used.update({('.', ''), ('..', '')})
            names = sorted(os.listdir(path))
            subdirs = []
            for n in names:
                full = os.path.join(path, n)
                if os.path.isdir(full):
                    c, _ = self.alloc(self.cs * 4)
                    raw += self.entries_for(n, 0x10, c, 0, used)
                    subdirs.append((full, c))
                else:
                    size = os.path.getsize(full)
                    c = 0
                    if size:
                        c, _ = self.alloc(size)
                        with open(full, 'rb') as src:
                            off = self.cluster_off(c)
                            while True:
                                b = src.read(8 << 20)
                                if not b: break
                                self.write_at(off, b); off += len(b)
                    raw += self.entries_for(n, 0x20, c, size, used)
            if len(raw) > (self.cs * (1 if is_root else 4)):
                raise SystemExit('directory too large: ' + path)
            self.write_at(self.cluster_off(own_cluster), raw.ljust(self.cs * (1 if is_root else 4), b'\0'))
            for full, c in subdirs:
                walk(full, own_cluster, c, False)
        root_c, _ = self.alloc(self.cs)
        assert root_c == 2
        walk(tree, 2, 2, True)
        boot = bytearray(512)
        boot[0:3] = b'\xeb\x58\x90'; boot[3:11] = b'CHARON  '
        struct.pack_into('<HBHBHHBHHHII', boot, 11, 512, self.spc, self.reserved, 2, 0, 0, 0xF8, 0, 63, 255, self.first, self.sectors)
        struct.pack_into('<IHHIHH', boot, 36, self.fat_sectors, 0, 0, 2, 1, 6)
        boot[64] = 0x80; boot[66] = 0x29; struct.pack_into('<I', boot, 67, 0x4856c4a4)
        boot[71:82] = self.label; boot[82:90] = b'FAT32   '; boot[510:512] = b'\x55\xaa'
        info = bytearray(512)
        struct.pack_into('<I', info, 0, 0x41615252); struct.pack_into('<III', info, 484, 0x61417272, self.clusters - (self.next - 2), self.next)
        info[510:512] = b'\x55\xaa'
        fat = bytearray(self.fat_sectors * 512)
        for i, v in enumerate(self.fat):
            struct.pack_into('<I', fat, 4 * i, v)
        base = self.first * 512
        self.write_at(base, bytes(boot)); self.write_at(base + 512, bytes(info))
        self.write_at(base + 6 * 512, bytes(boot)); self.write_at(base + 7 * 512, bytes(info))
        self.write_at(base + self.reserved * 512, bytes(fat))
        self.write_at(base + (self.reserved + self.fat_sectors) * 512, bytes(fat))


def cmd_disk(a):
    boot_sectors = a.boot_mib * 2048
    data_sectors = a.data_gib * 1024 * 2048
    first = 2048
    total = first + boot_sectors + data_sectors + 2048
    disk_guid = str(uuid.uuid4()); boot_guid = str(uuid.uuid4()); data_guid = str(uuid.uuid4())
    # charonhv.cfg names P2 as Charon's step does
    cfg = os.path.join(a.tree, 'charonhv', 'charonhv.cfg')
    os.makedirs(os.path.dirname(cfg), exist_ok=True)
    open(cfg, 'w', newline='\n').write('# Charon Hypervisor 1\nversion=r410\ndata=%s\nlanguage=%s\n' % (data_guid, a.language))
    with open(a.out, 'wb') as f:
        if os.name == 'nt':                       # a sparse file: the empty data partition takes no room
            f.close()
            import subprocess
            subprocess.run(['fsutil', 'sparse', 'setflag', a.out], capture_output=True)
            f = open(a.out, 'r+b')
        f.truncate(total * 512)
        Fat32(f, first, boot_sectors, 'CHARONHV').build(a.tree)
        f.seek((first + boot_sectors) * 512); f.write(b'\0' * (1 << 20))
        entries = bytearray(128 * 128)
        def entry(i, type_guid, uniq, lo, hi, name):
            e = bytearray(128)
            e[0:16] = guid_bytes(type_guid); e[16:32] = guid_bytes(uniq)
            struct.pack_into('<QQQ', e, 32, lo, hi, 0)
            n = name.encode('utf-16-le'); e[56:56 + len(n)] = n
            entries[i * 128:(i + 1) * 128] = e
        entry(0, 'EBD0A0A2-B9E5-4433-87C0-68B6B72699C7', boot_guid, first, first + boot_sectors - 1, 'Hypervisor')
        entry(1, '0FC63DAF-8483-4772-8E79-3D69D8477DE4', data_guid, first + boot_sectors, first + boot_sectors + data_sectors - 1, 'Hypervisor data')
        ecrc = crc32(bytes(entries))
        def header(my, alt, ent_lba):
            h = bytearray(92)
            h[0:8] = b'EFI PART'; struct.pack_into('<II', h, 8, 0x10000, 92)
            struct.pack_into('<QQQQ', h, 24, my, alt, 34, total - 34)
            h[56:72] = guid_bytes(disk_guid)
            struct.pack_into('<QIII', h, 72, ent_lba, 128, 128, ecrc)
            struct.pack_into('<I', h, 16, crc32(bytes(h)))
            return bytes(h).ljust(512, b'\0')
        mbr = bytearray(512)
        mbr[446:462] = bytes([0, 0, 2, 0, 0xEE, 0xFF, 0xFF, 0xFF]) + struct.pack('<II', 1, min(total - 1, 0xffffffff))
        mbr[510:512] = b'\x55\xaa'
        f.seek(0); f.write(bytes(mbr)); f.write(header(1, total - 1, 2)); f.write(bytes(entries))
        f.seek((total - 33) * 512); f.write(bytes(entries)); f.write(header(total - 1, 1, total - 33))
    log('disk', a.out, 'P1', boot_guid, 'P2', data_guid)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    f = sub.add_parser('fetch'); f.add_argument('--cache', required=True); f.add_argument('--tree', required=True)
    f.add_argument('--branch', default='v3.24')
    d = sub.add_parser('disk'); d.add_argument('--tree', required=True); d.add_argument('--out', required=True)
    d.add_argument('--boot-mib', type=int, default=2048); d.add_argument('--data-gib', type=int, default=16)
    d.add_argument('--language', default='en')
    o = sub.add_parser('overlay'); o.add_argument('--tree', required=True)
    o.add_argument('--bin', help='folder with charonhv-tvox and charonhv-rhvoice (Alpine builds)')
    a = ap.parse_args()
    {'fetch': cmd_fetch, 'disk': cmd_disk, 'overlay': cmd_overlay}[a.cmd](a)


if __name__ == '__main__':
    main()
