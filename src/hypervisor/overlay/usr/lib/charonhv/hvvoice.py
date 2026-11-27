# SPDX-License-Identifier: GPL-2.0-or-later
# Charon Hypervisor: the voice (espeak-ng with Charon's settings), the guest-volume ducking, the log.
import glob, json, lzma, importlib.machinery, os, shutil, struct, subprocess, threading, time, zlib

LOG = '/hv/log/charonhv.log'
SAID = '/hv/log/said.txt'


def stamp():
    return time.strftime('%Y-%m-%d %H:%M:%S')


def log(text):
    line = '%s %s\n' % (stamp(), text)
    for path in (LOG, '/run/charonhv/charonhv.log'):
        try:
            with open(path, 'a', encoding='utf-8') as f:
                f.write(line)
        except OSError:
            pass


def find_charon_hak():
    """\\EFI\\HAK of the Charon that started us: any FAT volume with EFI/HAK/charon-settings-*.bin (mounted read-only)."""
    for d in glob.glob('/media/*/EFI/HAK') + glob.glob('/media/*/efi/hak') + glob.glob('/run/charonhv/esp*/EFI/HAK'):
        return d
    os.makedirs('/run/charonhv', exist_ok=True)
    try:
        out = subprocess.run(['blkid', '-t', 'TYPE=vfat', '-o', 'device'], capture_output=True, text=True, timeout=10).stdout.split()
    except Exception:
        out = []
    for n, dev in enumerate(out):
        mnt = '/run/charonhv/esp%d' % n
        os.makedirs(mnt, exist_ok=True)
        if subprocess.run(['mount', '-t', 'vfat', '-o', 'ro', dev, mnt], capture_output=True).returncode != 0:
            continue
        for cand in ('EFI/HAK', 'efi/hak', 'EFI/hak'):
            if os.path.isdir(os.path.join(mnt, cand)):
                return os.path.join(mnt, cand)
        subprocess.run(['umount', mnt], capture_output=True)
    return None


class Settings:
    """Charon's voice settings (record V14 read with Charon's own tools/charon-os code)."""

    def __init__(self, cfg_language='en'):
        self.rate, self.pitch, self.volume, self.language = 10, 10, 8, 0
        self.source = 'defaults'
        self.synth, self.synth_voice = 'tvox', ''      # Charon's synthesizer: tvox | rhvoice | espeak | <other add-on>
        self.format_flags, self.profile = 252, 0      # Charon's voice formatting (record V14 bytes 40, 38)
        self.profile_items, self.format_groups = 1083, 7
        hak = find_charon_hak()
        if hak:
            try:
                mod = _settings_module()
                store = mod.Store(hak)
                if store.state == 'valid':
                    v = store.values
                    self.rate, self.pitch = v.get('rate', 10), v.get('pitch', 10)
                    self.volume, self.language = v.get('speechVolume', 8), v.get('language', 0)
                    self.format_flags, self.profile = v.get('formatFlags', 252), v.get('profile', 0)
                    self.profile_items, self.format_groups = v.get('profileItems', 1083), v.get('formatGroups', 7)
                    key, index = v.get('synthKey', 0), v.get('synthVoice', 0)
                    if key:
                        self.synth = 'missing add-on %d' % key
                        for info in mod.scan_synths(hak):
                            if info['key'] == key:
                                self.synth = info['manifest'].rsplit('.', 1)[0].lower()
                                voices = info['voices']
                                self.synth_voice = voices[index if index < len(voices) else 0][0]
                    self.source = 'Charon %s (%s)' % (store.version, hak)
                audio = mod.first_line(mod.ci_path(hak, 'audio-language')) or ''
                if self.language == 0:
                    self.language = 2 if audio.lower().startswith('pl') else 1
            except Exception as e:
                log('settings: %r' % e)
        if self.language == 0:
            self.language = 2 if cfg_language.startswith('pl') else 1
        self.polish = self.language == 2
        log('voice settings from %s: synth %s voice %s rate %s pitch %s volume %s language %s formatting %s profile %s' % (
            self.source, self.synth, self.synth_voice or '-', self.rate, self.pitch, self.volume, self.language, self.format_flags, self.profile))


VOICE_FILE = '/hv/voice.json'                 # a choice made in the hypervisor: {"engine": "tvox" | "rhvoice" | "espeak"}
TVOX_BUILT = '/hv/voice/charonhv-tvox'          # 6m: built at first start from Charon's sources, kept on the data partition
TVOX_SHIPPED = '/usr/lib/charonhv/charonhv-tvox'  # a development overlay may carry a ready build
TVOX_SRC = '/usr/lib/charonhv/tvox-src.tar.gz'   # Charon's src/tts + CharonFormatting.h + front end + build-tvox.sh


SCREEN_BUILT = '/hv/voice/charonhv-screen'      # 6g: Charon's screen model (CharonScreenModel.h) for the guest reader


def screen_bin():
    return SCREEN_BUILT if os.path.exists(SCREEN_BUILT) else None


def tvox_bin():
    for p in (TVOX_BUILT, TVOX_SHIPPED):
        if os.path.exists(p):
            return p
    return None


def build_tvox(note=log):
    """6m: TVox for the hypervisor is built at first start. Alpine's build-base comes from the official repository
    (apk checks its signatures with the keys in /etc/apk/keys), Charon's TVox sources come from the small archive in
    the overlay, the program is compiled and checked, kept on the data partition, and the build tools are removed.
    Returns None, or the reason it failed (the voice before it keeps speaking)."""
    import tarfile
    if not os.path.exists(TVOX_SRC):
        return 'the TVox sources are missing from this hypervisor'
    try:
        release = open('/etc/alpine-release').read().strip()
    except OSError:
        return 'the Alpine version is not known'
    repo = 'https://dl-cdn.alpinelinux.org/alpine/v%s/main' % '.'.join(release.split('.')[:2])
    work = '/hv/build/tvox-first-start'
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    started = time.time()
    tools = False
    try:
        note('TVox build: apk add build-base from %s' % repo)
        # ONLY the official repository: P1's local repository holds the signed index of all of main but only the
        # hypervisor's own packages (measured: "patch-2.8.1-r0: package mentioned in index not found")
        r = subprocess.run(['apk', 'add', '--no-progress', '--update-cache', '--repositories-file', '/dev/null', '-X', repo,
                            '--virtual', '.charonhv-tvox-build', 'build-base'],
                           capture_output=True, text=True, timeout=1800)
        tools = True
        if r.returncode:
            return 'the build tools could not be fetched: %s' % (r.stderr.strip().splitlines() or ['apk failed'])[-1]
        with tarfile.open(TVOX_SRC) as t:
            for m in t.getmembers():
                if m.name.startswith('/') or '..' in m.name.split('/') or not (m.isfile() or m.isdir()):
                    return 'the TVox source archive holds a bad name: %s' % m.name
            t.extractall(work)
        if not screen_bin():                  # 6g: one compiler session builds the screen model too
            note('screen model build: compiling')
            sout = os.path.join(work, 'charonhv-screen')
            # no -Werror here: GCC on the user's computer may warn where the -Werror host build (clang) does not;
            # the warnings are logged, a real error stops the build
            r = subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-static', '-s', '-I', os.path.join(work, 'src', 'efi'),
                                os.path.join(work, 'charonhv-screen.cpp'), '-o', sout], capture_output=True, text=True, timeout=900)
            if r.returncode or not os.path.exists(sout):
                note('screen model build FAILED: %s' % ' | '.join([l for l in r.stderr.splitlines() if 'error' in l][:3] or ['no message']))
            else:
                warn = [l for l in r.stderr.splitlines() if 'warning' in l]
                if warn:
                    note('screen model build: %d warnings, first: %s' % (len(warn), warn[0][-200:]))
                os.makedirs(os.path.dirname(SCREEN_BUILT), exist_ok=True)
                shutil.copy2(sout, SCREEN_BUILT + '.new'); os.chmod(SCREEN_BUILT + '.new', 0o755); os.rename(SCREEN_BUILT + '.new', SCREEN_BUILT)
                note('screen model build: ready, %d bytes' % os.path.getsize(SCREEN_BUILT))
        if tvox_bin():
            return None
        note('TVox build: compiling')
        r = subprocess.run(['sh', os.path.join(work, 'build-tvox.sh'), os.path.join(work, 'src', 'tts'),
                            os.path.join(work, 'charonhv-tvox.cpp'), os.path.join(work, 'out')],
                           capture_output=True, text=True, timeout=3600)
        out = os.path.join(work, 'out', 'charonhv-tvox')
        if r.returncode or not os.path.exists(out):
            return 'TVox did not compile: %s' % (r.stderr.strip().splitlines() or ['no message'])[-1]
        pcm = subprocess.run([out, 'en', '1.00', '1.00', '1.00'], input=b'Test.', capture_output=True, timeout=120).stdout
        if len(pcm) < 48000:                          # less than a quarter of a second of 48 kHz stereo
            return 'the new TVox program gave no sound (%d bytes)' % len(pcm)
        os.makedirs(os.path.dirname(TVOX_BUILT), exist_ok=True)
        shutil.copy2(out, TVOX_BUILT + '.new')
        os.chmod(TVOX_BUILT + '.new', 0o755)
        os.rename(TVOX_BUILT + '.new', TVOX_BUILT)
        try:
            info = open(os.path.join(work, 'out', 'build-info.txt')).read().strip().replace('\n', '; ')
        except OSError:
            info = ''
        note('TVox build: ready in %d s, %d bytes, test %d bytes of sound; %s' % (time.time() - started, os.path.getsize(TVOX_BUILT), len(pcm), info))
        return None
    except (OSError, subprocess.SubprocessError, tarfile.TarError) as e:
        return 'the TVox build failed: %s' % e
    finally:
        if tools:
            r = subprocess.run(['apk', 'del', '--no-progress', '.charonhv-tvox-build'], capture_output=True, text=True, timeout=600)
            note('TVox build: build tools removed (apk del, exit %d)' % r.returncode)
        shutil.rmtree(work, ignore_errors=True)
ENGINES = ('tvox', 'rhvoice', 'espeak')
RHVOICE_BIN = '/usr/lib/charonhv/charonhv-rhvoice'
RHVOICE_DATA = '/run/charonhv/rhvoice'
RHVOICE_SOCK = '/run/charonhv/rhvoice.sock'


def rhvoice_up(sock=RHVOICE_SOCK):
    """True when a kept-loaded RHVoice engine answers on its socket."""
    import socket
    s = socket.socket(socket.AF_UNIX)
    try:
        s.connect(sock)
        return True
    except OSError:
        return False
    finally:
        s.close()


def rhvoice_server(data, sock=RHVOICE_SOCK, binary=None):
    """Session 10: keep the RHVoice engine loaded (charonhv-rhvoice --serve) - saves ~90 ms before every utterance
    (measured: 83-95 ms to the first sample when the engine loads, 254 ms cold). One server for the menu and
    sd_charon; it outlives a menu restart. A front end without --serve ends at once, and speech stays as before."""
    if rhvoice_up(sock):
        return True
    try:
        subprocess.Popen([binary or RHVOICE_BIN, '--serve', sock, data], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
    except OSError as e:
        log('RHVoice server not started: %r' % e)
        return False
    for _ in range(40):
        time.sleep(0.05)
        if rhvoice_up(sock):
            return True
    return False


RHVOICE_PROFILES = {'pl': ['natan', 'magda', 'alicja', 'cezary', 'michal'], 'en': ['slt', 'alan', 'bdl', 'clb', 'ksp', 'dasha-eng', 'lyubov', 'evgeniy-eng']}


def read_pack(path):
    """Charon's synthesizer data pack (CHSPACK1, tools/build-charon-synths.py write_pack): {name: bytes}."""
    raw = open(path, 'rb').read()
    if raw[:8] != b'CHSPACK1':
        raise ValueError('not a Charon pack: %s' % path)
    count, = struct.unpack_from('<I', raw, 8)
    at, out = 16, {}
    for _ in range(count):
        n, = struct.unpack_from('<H', raw, at); at += 2
        name = raw[at:at + n].decode(); at += n
        method, nranges, size, psize, offset, crc = struct.unpack_from('<BBIIII', raw, at); at += 18
        ranges = [struct.unpack_from('<II', raw, at + 8 * i) for i in range(nranges)]; at += 8 * nranges
        blob = raw[offset:offset + psize]
        data = bytearray(lzma.decompress(blob, format=lzma.FORMAT_ALONE) if method == 1 else blob)
        for o, cnt in ranges:                          # undo plane_split: 4 byte planes back into 32-bit words
            words = cnt // 4
            planes = [bytes(data[o + k * words:o + (k + 1) * words]) for k in range(4)]
            merged = bytearray(4 * words)
            for k in range(4):
                merged[k::4] = planes[k]
            data[o:o + 4 * words] = merged
        if len(data) != size or (zlib.crc32(data) & 0xffffffff) != crc:
            raise ValueError('pack %s: %s damaged' % (os.path.basename(path), name))
        out[name] = bytes(data)
    return out


def pack_names(path):
    """The file names in a Charon pack's table, without unpacking anything."""
    try:
        with open(path, 'rb') as f:
            raw = f.read(1 << 16)
        if raw[:8] != b'CHSPACK1':
            return []
        count, = struct.unpack_from('<I', raw, 8)
        at, out = 16, []
        for _ in range(count):
            n, = struct.unpack_from('<H', raw, at); at += 2
            out.append(raw[at:at + n].decode('utf-8', 'replace')); at += n
            nranges = raw[at + 1]; at += 18 + 8 * nranges
        return out
    except (OSError, struct.error, IndexError):
        return []


def rhvoice_packs():
    """Charon's RHVoice packs (EFI/HAK/synths/rhvoice/*.chsp on any FAT volume Charon may use)."""
    hak = find_charon_hak()
    for cand in ([hak] if hak else []) + glob.glob('/media/*/EFI/HAK') + glob.glob('/run/charonhv/esp*/EFI/HAK'):
        for sub in ('synths/rhvoice', 'SYNTHS/RHVOICE', 'Synths/rhvoice'):
            packs = sorted(glob.glob(os.path.join(cand, sub, '*.chsp')) + glob.glob(os.path.join(cand, sub, '*.CHSP')))
            if packs:
                return packs
    return []


def rhvoice_prepare():
    """RHVoice's data directory, unpacked from the packs Charon itself keeps (nothing downloaded, nothing bundled).
    Returns (data_dir, [voice directory names]) or raises with the reason."""
    if not os.path.exists(RHVOICE_BIN):
        raise RuntimeError('the RHVoice program is missing from this hypervisor')
    ready = os.path.join(RHVOICE_DATA, 'voices')
    if os.path.isdir(ready) and os.listdir(ready):
        return RHVOICE_DATA, sorted(os.listdir(ready))
    packs = rhvoice_packs()
    if not packs:
        raise RuntimeError('no RHVoice voices: Charon has no RHVoice add-on')
    tmp = RHVOICE_DATA + '.new'
    shutil.rmtree(tmp, ignore_errors=True)
    for pack in packs:
        files = read_pack(pack)
        base = os.path.basename(pack).lower()
        if base in ('rhvoice-pl.chsp', 'rhvoice-en.chsp'):
            root, strip = tmp, 'data/'                     # base packs: data/languages/<L>/..., data/voices/<v>/...
        else:
            info = files.get('voice.info', b'').decode('utf-8', 'replace')
            name = next((l.split('=', 1)[1].strip() for l in info.splitlines() if l.startswith('name=')), '')
            if not name:
                continue
            root, strip = os.path.join(tmp, 'voices', name.lower()), ''
        for rel, data in files.items():
            rel = rel[len(strip):] if strip and rel.startswith(strip) else rel
            dest = os.path.normpath(os.path.join(root, rel))
            if not dest.startswith(tmp + os.sep):
                raise ValueError('pack %s: bad name %s' % (base, rel))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, 'wb') as f:
                f.write(data)
    if not os.path.isdir(os.path.join(tmp, 'voices')):
        raise RuntimeError('the RHVoice packs hold no voice')
    shutil.rmtree(RHVOICE_DATA, ignore_errors=True)
    os.rename(tmp, RHVOICE_DATA)
    return RHVOICE_DATA, sorted(os.listdir(ready))


def voice_choice():
    """The engine chosen inside the hypervisor, or None: follow Charon's own synthesizer."""
    try:
        e = json.load(open(VOICE_FILE)).get('engine')
        return e if e in ENGINES else None
    except (OSError, ValueError, AttributeError):
        return None


def voice_id_choice():
    """The one voice chosen inside the hypervisor (an id of catalogue()), or None."""
    try:
        v = json.load(open(VOICE_FILE)).get('voice')
        return v if isinstance(v, str) and v else None
    except (OSError, ValueError, AttributeError):
        return None


# TVox is built into Charon (no manifest): Charon's TVox speaks the interface's language, Polish or English
# Equal loudness (coordinator, session 8). Measured on hvxp1 at the synthesizers' own output (each voice reading one
# sentence at Charon's default settings, speech parts only): RMS and peak of full scale. TVox 0.056-0.064 RMS, eSpeak NG
# 0.155-0.183 with peaks at full scale (clipped), RHVoice 0.056-0.163. Each voice is scaled to the common RMS 0.10 with
# its peak kept below 0.9 (headroom); a voice not listed keeps its own level.
LOUDNESS_TARGET, LOUDNESS_PEAK = 0.10, 0.9
LOUDNESS_MEASURED = {'tvox': (0.060, 0.314), 'espeak': (0.169, 0.999), 'rhvoice-natan': (0.109, 0.545),
                     'rhvoice-slt': (0.122, 0.550), 'rhvoice-alicja': (0.108, 0.603), 'rhvoice-cezary': (0.115, 0.606),
                     'rhvoice-magda': (0.056, 0.378), 'rhvoice-michal': (0.103, 0.588), 'rhvoice-alan': (0.099, 0.547),
                     'rhvoice-bdl': (0.099, 0.557), 'rhvoice-clb': (0.163, 0.525), 'rhvoice-dasha-eng': (0.111, 0.685),
                     'rhvoice-evgeniy-eng': (0.106, 0.611), 'rhvoice-ksp': (0.097, 0.404), 'rhvoice-lyubov': (0.111, 0.531)}


def loudness(key):
    """The factor that brings voice key (engine, or rhvoice-<profile>) to the common level."""
    m = LOUDNESS_MEASURED.get(key.lower().replace('ł', 'l'))
    return min(LOUDNESS_TARGET / m[0], LOUDNESS_PEAK / m[1]) if m else 1.0

TVOX_VOICES = (('tvox-pl', 'TVox polski', 'pl'), ('tvox-en', 'TVox English', 'en'))


def load_source(name, path):
    """A module from a file without the .py ending (load_module is gone in newer Pythons)."""
    import importlib.util
    loader = importlib.machinery.SourceFileLoader(name, path)
    mod = importlib.util.module_from_spec(importlib.util.spec_from_loader(name, loader))
    loader.exec_module(mod)
    return mod


def _settings_module():
    return load_source('charon_settings', '/usr/lib/charonhv/charon_settings.py')


def catalogue(hak=None, settings_module=None):
    """EVERY voice Charon itself offers, read at run time from Charon's own files so the list never drifts: TVox
    (built in, its two languages), then each synthesizer add-on's manifest in EFI/HAK/synths (eSpeak NG's
    languages, all RHVoice voices - 13 in r410), in Charon's order (charon_settings.scan_synths, the same reader
    Charon's Linux tools use). The add-on .tts modules are UEFI code; their voice DATA is used by the hypervisor's
    Linux front ends (charonhv-rhvoice reads the .chsp packs, eSpeak NG is Alpine's build of the same engine).
    Each entry: dict(id, name, lang, engine, available, note)."""
    out = [dict(id=i, name=n, lang=l, engine='tvox', available=bool(tvox_bin() or os.path.exists(TVOX_SRC)), note='')
           for i, n, l in TVOX_VOICES]
    hak = hak if hak is not None else find_charon_hak()
    if not hak:
        return out
    try:
        mod = settings_module or _settings_module()
        synths = mod.scan_synths(hak)
    except Exception as e:
        log('voice catalogue: %r' % e)
        return out
    packs = {}
    for root, _, files in os.walk(hak):
        packs.update((f.lower(), os.path.join(root, f)) for f in files if f.lower().endswith('.chsp'))
    for info in synths:
        engine = info['manifest'].rsplit('.', 1)[0].lower()
        for ident, spoken, lang in info['voices']:
            lang = (lang or '')[:2].lower()
            if engine == 'rhvoice':
                # a voice's own pack, or (Natan, Slt) its directory inside the language pack
                base = packs.get('rhvoice-%s.chsp' % lang)
                ok = ('rhvoice-%s.chsp' % ident[8:]) in packs or bool(
                    base and any(n.startswith('data/voices/%s/' % ident[8:]) for n in pack_names(base)))
                note = '' if ok else 'its data file is not on this Charon medium'
            elif engine == 'espeak':
                ok, note = shutil.which('espeak-ng') is not None, ''
            else:
                ok, note = False, 'a Charon add-on without a Linux front end in the hypervisor'
            out.append(dict(id=ident, name=spoken or ident, lang=lang, engine=engine, available=ok, note=note))
    return out


class Voice:
    failed = None

    def __init__(self, settings, duck_percent=35):
        self.s = settings
        self.engine = 'espeak'
        self.rhvoice = None
        self.chosen = voice_choice()
        self.voice_id = voice_id_choice() if self.chosen else None
        want = self.chosen or settings.synth
        err = self.use(want) if want in ENGINES else 'Charon speaks with %s, which this hypervisor does not have' % want
        if err:
            log('%s cannot speak, eSpeak NG speaks: %s' % (want, err))
            self.failed = (want, err)
        self.proc = None
        self.lock = threading.Lock()
        self.duck = duck_percent
        self.guest_volume = 100
        self.speaking_until = 0
        self.ducked = False
        threading.Thread(target=self._unduck, daemon=True).start()

    def use_rhvoice(self):
        """Switch to RHVoice; returns None, or the reason it cannot speak (eSpeak NG then keeps speaking)."""
        try:
            data, voices = rhvoice_prepare()
        except Exception as e:
            return str(e)
        self.rhvoice = (data, voices)
        self.engine = 'rhvoice'
        log('RHVoice voices: %s' % ', '.join(voices))
        rhvoice_server(data)
        return None

    def use(self, engine):
        """Speak with engine from now on; returns None, or why it cannot (the voice before it keeps speaking)."""
        if engine == 'rhvoice':
            return self.use_rhvoice()
        if engine == 'tvox':
            if not tvox_bin():
                return 'TVox is not built yet' if os.path.exists(TVOX_SRC) else 'the TVox program is missing from this hypervisor'
            self.engine = 'tvox'
            return None
        self.engine = 'espeak'
        return None

    def set_voice(self, entry, keep=True):
        """One voice of catalogue() (None = as Charon), kept on the data partition (keep=False: for this process only -
        the screen reader's own voice, sd_charon). Returns None or the reason."""
        if entry is None:
            err = self.set_engine(None)
            if err is None:
                self.voice_id = None
            return err
        if entry['engine'] not in ENGINES:
            return entry.get('note') or 'this hypervisor has no front end for %s' % entry['engine']
        before = self.engine
        err = self.use(entry['engine'])
        if err is None and entry['engine'] == 'rhvoice' and entry['id'][8:] not in self.rhvoice[1]:
            err = "the RHVoice voice %s is not in Charon's packs" % entry['name']
            self.use(before)
        if err is None:
            self.chosen, self.voice_id = entry['engine'], entry['id']
        if err is None and keep:
            try:
                with open(VOICE_FILE, 'w') as f:
                    json.dump({'engine': entry['engine'], 'voice': entry['id']}, f)
            except OSError as e:
                log('voice choice not kept: %r' % e)
        return err

    def voice_lang(self, want=None):
        """The language spoken: text in a known language (want, e.g. a page's) is spoken in it; otherwise the chosen
        voice's own language; otherwise Charon's."""
        if want in ('pl', 'en'):
            return want
        vid = self.voice_id or ''
        for i, _, l in TVOX_VOICES:
            if i == vid:
                return l
        if vid.startswith('espeak-'):
            return vid[7:9]
        if vid.startswith('rhvoice-') and self.engine == 'rhvoice':
            return 'pl' if vid[8:] in RHVOICE_PROFILES['pl'] else 'en'
        return 'pl' if self.s.polish else 'en'

    def set_engine(self, engine):
        """A choice from the sound menu (None = follow Charon), kept on the data partition. Returns None or the reason."""
        err = self.use(engine or self.s.synth if (engine or self.s.synth) in ENGINES else 'espeak')
        if err is None:
            self.chosen = engine
            try:
                if engine:
                    with open(VOICE_FILE, 'w') as f:
                        json.dump({'engine': engine}, f)
                elif os.path.exists(VOICE_FILE):
                    os.remove(VOICE_FILE)
            except OSError as e:
                log('voice choice not kept: %r' % e)
        return err

    def rhvoice_profile(self, lang=None):
        voices = self.rhvoice[1]
        vid = self.voice_id or ''
        mine = vid[8:] if vid.startswith('rhvoice-') else ''
        if mine in voices and (lang is None or (mine in RHVOICE_PROFILES['pl']) == (lang == 'pl')):
            return mine                                     # the voice chosen in the hypervisor
        if lang in ('pl', 'en'):
            for v in RHVOICE_PROFILES[lang]:
                if v in voices:
                    return v
        own = self.s.synth_voice[8:] if self.s.synth == 'rhvoice' and self.s.synth_voice.startswith('rhvoice-') else ''
        lang = 'pl' if self.s.polish else 'en'
        if own in voices and own in RHVOICE_PROFILES[lang]:
            return own                                      # Charon's own RHVoice voice, when it fits the language
        for v in RHVOICE_PROFILES[lang]:
            if v in voices:
                return v
        return voices[0]

    def command(self, text, pitch=1.0, lang=None, rate=1.0):
        """(shell script, its arguments) that speak text through the voice's ALSA device; pitch: a factor (formatting);
        lang: the text's language when known (speech-dispatcher's); rate: a factor (a screen reader's rate)."""
        rel = lambda v, d, f=1.0: "%.2f" % max(0.25, min(3.0, v / float(d) * f))
        lang = self.voice_lang(lang)
        if self.engine == 'tvox':
            args = [tvox_bin() or TVOX_BUILT, lang, rel(self.s.rate, 10, rate), rel(self.s.pitch, 10, pitch), rel(self.s.volume, 8, loudness('tvox'))]
            return ('printf %s "$0" | "$@" 2>/dev/null | aplay -q -D charonvoice -t raw -f S16_LE -c 2 -r 48000', [text] + args)
        if self.engine == 'rhvoice':
            prof = self.rhvoice_profile(lang)
            # the kept-loaded engine when its socket exists (only a front end with --serve makes it); the client
            # speaks by itself when the server cannot be reached
            client = ['--client', RHVOICE_SOCK] if os.path.exists(RHVOICE_SOCK) else []
            args = [RHVOICE_BIN] + client + [self.rhvoice[0], prof, rel(self.s.rate, 10, rate), rel(self.s.pitch, 10, pitch), rel(self.s.volume, 8, loudness('rhvoice-' + prof))]
            return ('printf %s "$0" | "$@" 2>/dev/null | aplay -q -D charonvoice -t raw -f S16_LE -c 1 -r 16000', [text] + args)
        a = self.args(lang, rate)
        if pitch != 1.0:
            a[a.index('-p') + 1] = str(max(0, min(99, int(int(a[a.index('-p') + 1]) * pitch))))
        return ('"$@" | aplay -q -D charonvoice', ['say'] + a + [text])

    def args(self, lang=None, rate_factor=1.0):
        rate = max(80, min(450, int(175 * self.s.rate / 10 * rate_factor)))
        pitch = max(0, min(99, int(50 * self.s.pitch / 10)))
        amp = max(10, min(200, int(20 * self.s.volume * loudness('espeak'))))
        return ['espeak-ng', '-v', lang or ('pl' if self.s.polish else 'en'), '-s', str(rate), '-p', str(pitch), '-a', str(amp), '--stdout']

    def set_guest_volume(self, percent):
        subprocess.run(['amixer', '-q', '-c', '0', 'sset', 'Charon Guest', '%d%%' % percent], capture_output=True)

    def _unduck(self):
        while True:
            time.sleep(0.2)
            with self.lock:
                busy = self.proc is not None and self.proc.poll() is None
                if self.ducked and not busy and time.time() > self.speaking_until:
                    self.set_guest_volume(self.guest_volume)
                    self.ducked = False

    def stop(self):
        with self.lock:
            if self.proc and self.proc.poll() is None:
                self._kill()
            self.proc = None

    def say(self, text, interrupt=True, pitch=1.0, private=False):
        """private: spoken only - not written to said.txt, the log or the console (6h: characters of a product key)."""
        if not text:
            return
        for path, line in (() if private else ((SAID, '%s %s\n' % (stamp(), text)), ('/dev/tty1', text + '\n'))):
            try:
                with open(path, 'a' if path == SAID else 'w', encoding='utf-8') as f:
                    f.write(line)
            except OSError:
                pass
        with self.lock:
            if interrupt and self.proc and self.proc.poll() is None:
                self._kill()
            elif not interrupt and self.proc and self.proc.poll() is None:
                self.proc.wait()
            if not self.ducked:
                self.set_guest_volume(min(self.duck, self.guest_volume))     # ducking never raises a lowered guest
                self.ducked = True
            self.speaking_until = time.time() + 0.4
            try:
                script, argv = self.command(text, pitch)
                self.proc = subprocess.Popen(['sh', '-c', script] + argv,
                                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
            except OSError as e:
                log('voice failed: %r' % e)

    def _kill(self):
        try:
            os.killpg(self.proc.pid, 9)
        except OSError:
            pass

    def wait(self, limit=60):
        end = time.time() + limit
        while time.time() < end:
            with self.lock:
                p = self.proc
            if p is None or p.poll() is not None:
                return
            time.sleep(0.05)

    def tone(self, freq=880, ms=120):
        """A short tone through the voice path (no text)."""
        subprocess.Popen(['sh', '-c', 'speaker-test -D charonvoice -t sine -f %d -l 1 -p %d >/dev/null 2>&1' % (freq, ms)],
                         start_new_session=True)
