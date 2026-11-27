# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: every setting in one place (user point 6ze).

Main menu > Hypervisor settings, also over a running guest (host key + S), in four groups: voice and sound, screen
reader, boot and boot loaders of machines, network/protection/performance. Values come FROM CHARON at the first start
(its settings file on EFI/HAK, hvvoice.Settings), then they are the hypervisor's own (/hv/settings.json; nothing is
ever written back to Charon); "Back to Charon's settings" takes Charon's values again. General values plus
per-machine exceptions (the machine's own "Settings for this machine"; an exception in force is said aloud).
Settings that already lived elsewhere stay where the code reads them and are reached from here: the voice
(/hv/voice.json, hvvoice), the Linux screen reader (hvmodern.READER_FILE), the retro assistant parts and secure use
(per machine: retro.json, machine.json)."""
import json, os

FILE = '/hv/settings.json'

# kind: bool | choice | number. charon: the value is taken from Charon's settings (hvvoice.Settings attribute).
# machine: an exception for one machine is allowed.
B, C, N = 'bool', 'choice', 'number'
GROUPS = [
    ('voice', 'Voice and sound', 'Głos i dźwięk', [
        dict(key='voice', kind='voice', en='Voice', pl='Głos'),
        dict(key='rate', kind=N, lo=1, hi=20, charon='rate', en='Rate', pl='Tempo'),
        dict(key='pitch', kind=N, lo=1, hi=20, charon='pitch', en='Pitch', pl='Wysokość'),
        dict(key='volume', kind=N, lo=1, hi=16, charon='volume', en='Volume', pl='Głośność'),
        # Charon's own four profiles and their names (BootMenu.h profileEn/profilePl)
        dict(key='profile', kind=C, charon='profile', en='Verbosity profile', pl='Profil mówienia',
             choices=[(0, 'normal', 'zwykły'), (1, 'beginner: full descriptions and key hints', 'dla początkujących: pełne opisy i podpowiedzi klawiszy'),
                      (2, 'expert: short, sounds instead of words', 'dla zaawansowanych: krótko, dźwięki zamiast słów'),
                      (3, 'custom: the items ticked under Text formatting', 'własny: pozycje zaznaczone w Formatowaniu tekstu')]),
        dict(key='format_voice', kind=B, charon='format_flags', bit=1, en='Voice formatting: pitch shows the place in a list',
             pl='Formatowanie głosem: wysokość pokazuje miejsce na liście'),
        dict(key='format_icons', kind=B, charon='format_flags', bit=2, en='Voice formatting: tones for places and kinds',
             pl='Formatowanie głosem: dźwięki miejsc i rodzajów'),
        dict(key='guest_volume', kind=N, lo=0, hi=100, step=10, default=100, en='Guest volume, percent',
             pl='Głośność gościa, procent'),
        dict(key='duck', kind=N, lo=0, hi=100, step=10, default=40, en='Guest volume while the voice speaks, percent',
             pl='Głośność gości, gdy mówi głos, procent'),
        # session 10: guest sound latency (hvmachine.SOUND_OPTIONS), from the machine's next start
        dict(key='sound_latency', kind=C, default=0, machine=True, en="Machines' sound delay (from the next start)",
             pl='Opóźnienie dźwięku maszyn (od następnego uruchomienia)',
             choices=[(0, 'short, as a normal computer', 'krótkie, jak w zwykłym komputerze'),
                      (1, 'safe: a little longer, for a slow or busy computer', 'bezpieczne: trochę dłuższe, dla wolnego lub zajętego komputera'),
                      (2, 'long: as before, if the sound breaks up', 'długie: jak dawniej, gdy dźwięk się rwie')]),
    ]),
    ('reader', 'Screen reader', 'Czytnik ekranu', [
        dict(key='auto_read', kind=B, default=True, machine=True, en='Read new text automatically (guests without their own reader)',
             pl='Czytaj nowy tekst samodzielnie (goście bez własnego czytnika)'),
        dict(key='key_echo', kind=B, default=True, machine=True, en='Key echo (never passwords or product keys)',
             pl='Echo klawiszy (nigdy hasła ani klucze produktu)'),
        dict(key='tracking', kind=B, default=True, machine=True, en='Follow the cursor and the highlight',
             pl='Śledź kursor i podświetlenie'),
        dict(key='step_back', kind=B, default=True, machine=True, en="Step back when the guest's own reader speaks (read again when it does not answer keys)",
             pl='Milknij, gdy mówi własny czytnik gościa (czytaj znów, gdy nie odpowiada na klawisze)'),
        dict(key='linux_reader', kind='linux_reader', en='Screen reader of the graphical sessions', pl='Czytnik ekranu sesji graficznych'),
        # user (6ze): the Linux reader has its own synthesizer and voice (all of Charon's voices), rate and pitch;
        # the same as the hypervisor's until changed (0 / empty = as the hypervisor). Read by sd_charon.
        dict(key='reader_voice', kind='reader_voice', default='', en='Voice of the screen reader of the graphical sessions',
             pl='Głos czytnika ekranu sesji graficznych'),
        dict(key='reader_rate', kind=N, lo=0, hi=20, default=0, same=True, en='Rate of that screen reader', pl='Tempo tego czytnika ekranu'),
        dict(key='reader_pitch', kind=N, lo=0, hi=20, default=0, same=True, en='Pitch of that screen reader', pl='Wysokość tego czytnika ekranu'),
    ]),
    ('boot', 'Boot and boot loaders of machines', 'Rozruch i programy rozruchowe maszyn', [
        dict(key='read_loader', kind=B, default=True, machine=True, en="Read the guest's boot loader menu", pl='Czytaj menu programu rozruchowego gościa'),
        dict(key='read_boot', kind=B, default=True, machine=True,
             en="At a machine's start read its firmware screen, boot loader menu and boot messages (until its own reader takes over)",
             pl='Przy starcie maszyny czytaj ekran oprogramowania, menu programu rozruchowego i komunikaty rozruchu (aż przejmie jej własny czytnik)'),
        dict(key='ticks', kind=B, default=True, machine=True, en='Countdown ticks', pl='Tykanie odliczania'),
        dict(key='key_page', kind=B, default=True, machine=True, en='Announce the product key page', pl='Zapowiadaj stronę klucza produktu'),
        dict(key='state_messages', kind=B, default=True, machine=True, en='Say when a machine starts, restarts or stops',
             pl='Mów, gdy maszyna startuje, restartuje się lub zatrzymuje'),
    ]),
    ('network', 'Network, protection and performance', 'Sieć, ochrona i wydajność', [
        dict(key='performance', kind=B, default=True, machine=True, en='Performance profile (no HPET, timer and disk settings)',
             pl='Profil wydajności (bez HPET, ustawienia zegara i dysków)'),
        dict(key='daily_snapshot', kind=B, default=True, machine=True, en='A snapshot every day', pl='Migawka każdego dnia'),
        # session 9 (6ze: everything scattered moves here): the switches of Secure use and the retro assistant;
        # applied at the machine's next start (charonhv.apply_switches)
        dict(key='internet', kind=B, default=True, machine=True, en='Internet for machines (from the next start)',
             pl='Internet dla maszyn (od następnego uruchomienia)'),
        dict(key='simple_web', kind=B, default=True, machine=True, en='Simple web for old browsers (from the next start)',
             pl='Prosta sieć dla starych przeglądarek (od następnego uruchomienia)'),
        dict(key='scan_downloads', kind=B, default=True, machine=True, en='Scan downloads for viruses with ClamAV (from the next start)',
             pl='Skanuj pobierane pliki programem ClamAV (od następnego uruchomienia)'),
        # 6v-2: Suricata + Emerging Threats Open, network virtual patching in the hypervisor (EternalBlue/MS17-010,
        # BlueKeep, old browser/SMB exploits); watches the hypervisor's loopback where the guests' proxied traffic lives
        dict(key='virtual_patching', kind=B, default=True, machine=True,
             en='Network virtual patching with Suricata (known exploits blocked, spoken)',
             pl='Sieciowe łatanie wirtualne przez Suricata (znane ataki blokowane, wypowiadane)'),
    ]),
]
SPEC = {i['key']: i for _, _, _, items in GROUPS for i in items}


def from_charon(charon):
    """The values Charon gives (hvvoice.Settings), for every setting marked charon."""
    out = {}
    for k, i in SPEC.items():
        src = i.get('charon')
        if not src or not hasattr(charon, src):
            continue
        v = getattr(charon, src)
        out[k] = bool(v & i['bit']) if 'bit' in i else v
    return out


class Store:
    def __init__(self, charon, path=FILE, fresh=None):
        # fresh(): Charon's settings read again from its file (for "Back to Charon's settings")
        self.path, self.charon, self.fresh = path, charon, fresh
        try:
            self.data = json.load(open(path, encoding='utf-8'))
        except (OSError, ValueError):
            self.data = {}
        if 'general' not in self.data:            # the first start: Charon's values
            self.data = {'version': 1, 'general': from_charon(charon), 'machines': {}}
            self.save()
        self.apply()

    def save(self):
        tmp = self.path + '.new'
        with open(tmp, 'w', encoding='utf-8') as f:
            json.dump(self.data, f, indent=1, ensure_ascii=False)
        os.replace(tmp, self.path)

    def default(self, key):
        i = SPEC[key]
        if 'default' in i:
            return i['default']
        c = from_charon(self.fresh() if self.fresh else self.charon)
        return c.get(key, i.get('lo', False))

    def get(self, key, mid=None):
        if mid is not None:
            m = self.data['machines'].get(mid, {})
            if key in m:
                return m[key]
        g = self.data['general']
        return g[key] if key in g else self.default(key)

    def exception(self, key, mid):
        return key in self.data['machines'].get(mid, {})

    def set(self, key, value, mid=None):
        if mid is None:
            self.data['general'][key] = value
        else:
            self.data['machines'].setdefault(mid, {})[key] = value
        self.save()
        self.apply()

    def clear(self, key, mid):
        m = self.data['machines'].get(mid, {})
        if key in m:
            del m[key]
            self.save()

    def back_to_charon(self):
        """Charon's values again for everything Charon sets; the hypervisor's own settings keep theirs."""
        self.data['general'].update(from_charon(self.fresh() if self.fresh else self.charon))
        self.save()
        self.apply()

    def apply(self):
        """Writes the voice values into the Settings object the voice and the menus read."""
        s = self.charon
        for k in ('rate', 'pitch', 'volume', 'profile'):
            if hasattr(s, k):
                setattr(s, k, self.get(k))
        if hasattr(s, 'format_flags'):
            f = s.format_flags & ~3
            f |= (1 if self.get('format_voice') else 0) | (2 if self.get('format_icons') else 0)
            s.format_flags = f

    def label(self, key, T, mid=None):
        i = SPEC[key]
        v = self.get(key, mid)
        if i['kind'] == B:
            state = T('on', 'włączone') if v else T('off', 'wyłączone')
        elif i['kind'] == C:
            state = next((T(e, p) for val, e, p in i['choices'] if val == v), str(v))
        elif i['kind'] == N:
            state = T('as the hypervisor', 'jak hipernadzorca') if i.get('same') and not v else str(v)
        else:
            state = ''
        name = T(i['en'], i['pl'])
        text = '%s: %s' % (name, state) if state else name
        if mid is not None and self.exception(key, mid):
            text += T(', exception for this machine', ', wyjątek dla tej maszyny')
        return text
