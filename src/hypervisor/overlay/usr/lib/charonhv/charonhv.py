#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Charon Hypervisor: the speaking machine manager and virtual machine wizard (design parts 4-10).
import json, os, queue, re, shutil, socket, subprocess, sys, threading, time, urllib.request

sys.path.insert(0, '/usr/lib/charonhv')
import hvcompat, hvdisplay, hvkeys, hvmachine, hvproxy, hvreader, hvretro, hvscan, hvsecure, hvsettings, hvshare, hvvoice
from hvvoice import log
HERE = '/usr/lib/charonhv'

BOOT = '/run/charonhv/boot'
VERSION = 'r410'


def cmdline():
    try:
        words = open('/proc/cmdline').read().split()
    except OSError:
        words = []
    return dict(w.split('=', 1) for w in words if '=' in w)


def read_state():
    out = {}
    try:
        for line in open('/run/charonhv/prepare.state'):
            if '=' in line:
                k, v = line.strip().split('=', 1)
                out[k] = v
    except OSError:
        pass
    return out


def cfg_language():
    lang = cmdline().get('charonhv.lang')
    if lang:
        return lang
    try:
        for line in open(BOOT + '/charonhv/charonhv.cfg'):
            if line.startswith('language='):
                return line.split('=', 1)[1].strip()
    except OSError:
        pass
    return 'en'


def words(text):
    return re.findall(r'\S+', text)


# Free systems from their official servers (user point 5). 'list' is a directory listing read at download time.
CATALOGUE = {
    'freedos': dict(url='https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.4/FD14-LiveCD.zip',
                    inner='.iso', en='FreeDOS 1.4 live CD from ibiblio.org (FreeDOS project)', pl='FreeDOS 1.4 live CD z ibiblio.org (projekt FreeDOS)'),
    'debian6': dict(url='https://cdimage.debian.org/cdimage/archive/6.0.10/i386/iso-cd/debian-6.0.10-i386-netinst.iso',
                    en='Debian 6.0.10 network installer from cdimage.debian.org (archive; packages from archive.debian.org)',
                    pl='Instalator sieciowy Debian 6.0.10 z cdimage.debian.org (archiwum; pakiety z archive.debian.org)'),
    'debian13': dict(list='https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/', pattern=r'debian-13[.0-9]*-amd64-netinst\.iso',
                     en='Debian 13 network installer from cdimage.debian.org', pl='Instalator sieciowy Debiana 13 z cdimage.debian.org'),
    'reactos': dict(url='https://sourceforge.net/projects/reactos/files/ReactOS/0.4.15/ReactOS-0.4.15-release-1-gdbb43bbaeb2-x86-iso.zip/download',
                    inner='.iso', en='ReactOS 0.4.15 from the ReactOS project on SourceForge', pl='ReactOS 0.4.15 z projektu ReactOS na SourceForge'),
}


class App(hvretro.Retro):
    def __init__(self):
        os.makedirs('/hv/log', exist_ok=True)
        self.args = cmdline()
        self.state = read_state()
        self.settings = hvvoice.Settings(cfg_language())
        self.pl = self.settings.polish
        self.voice = hvvoice.Voice(self.settings)
        # 6ze: every setting in one place - Charon's values at the first start, then the hypervisor's own
        self.store = hvsettings.Store(self.settings, fresh=lambda: hvvoice.Settings(cfg_language()))
        self.voice.duck, self.voice.guest_volume = self.store.get('duck'), self.store.get('guest_volume')
        self.kbd = hvkeys.Keyboard(log)
        self.kvm = os.path.exists('/dev/kvm')
        self.machines = {}
        self.focus = None            # machine id that has the keyboard, or None = the hypervisor
        self.events = queue.Queue()
        self.review = []             # lines the review keys read
        self.rline = self.rword = self.rchar = 0
        self.auto_read = {}
        self.screens = {}
        self.forwarded = set()
        for mid in hvmachine.ids():
            try:
                self.machines[mid] = hvmachine.Machine(mid)
            except Exception as e:
                log('machine %s unreadable: %r' % (mid, e))
        # session 9: a menu that ended by a fault (no clean exit, so its marker is still there) is said at the start;
        # supervise-daemon starts it again (init.d/charonhv), the machines kept running and are taken back
        self.after_fault = os.path.exists(ALIVE)
        open(ALIVE, 'w').write(str(os.getpid()))
        self.reattached, self.orphans = self.reattach()
        threading.Thread(target=self.watch, daemon=True).start()
        self.display = hvdisplay.Display(self)

    @property
    def focus(self):
        return self.__dict__.get('_focus')

    @focus.setter
    def focus(self, value):
        old = self.__dict__.get('_focus')
        self.__dict__['_focus'] = value
        display = self.__dict__.get('display')
        if display and old != value:
            display.guest(value is not None)     # 6f: the guest's screen on its own console, the menu on tty1

    def T(self, en, pl):
        return pl if self.pl else en

    def say(self, text, interrupt=True, pitch=1.0, private=False):
        if not private:
            log('say: ' + text)
        self.voice.say(text, interrupt, pitch, private)

    # ---- Charon's voice formatting (CharonFormatting.h: flags byte 40, profile byte 38, items, groups) -------------
    FLAG_VOICE, FLAG_ICONS = 1, 2
    ITEM_POSITION, ITEM_KIND, ITEM_HINTS, ITEM_INTRO, ITEM_HELP, ITEM_SOUNDS, ITEM_STRUCTURE = 1, 2, 4, 8, 16, 64, 128
    ITEMS = {0: 1 | 2 | 8 | 16 | 32 | 1024, 1: 1 | 2 | 8 | 16 | 32 | 1024 | 4 | 128 | 256 | 2048, 2: 64 | 512}

    def fmt_item(self, item):
        s = self.settings
        items = (s.profile_items & 4095) if s.profile == 3 else self.ITEMS.get(s.profile, self.ITEMS[0])
        return bool(items & item)

    def fmt_place(self, index, count):
        """Charon's formatting of a list position: (pitch factor, place tone in Hz or None)."""
        s = self.settings
        if count < 2:
            return 1.0, None
        step = 4 - (index * 8 + (count - 1) // 2) // (count - 1)          # PositionPitch: +4 at the top .. -4 at the bottom
        pitch = 1.0 + 0.03 * step if (s.format_flags & self.FLAG_VOICE) and (s.format_groups & 4) else 1.0
        tone = None
        if ((s.format_flags & self.FLAG_ICONS) or self.fmt_item(self.ITEM_SOUNDS)) and (s.format_groups & 4):
            place = (index * 24 + (count - 1) // 2) // (count - 1)        # PlaceChar / PlaceHertz: 1760 Hz .. 440 Hz
            octave = [1760, 1661, 1568, 1480, 1397, 1319, 1245, 1175, 1109, 1047, 988, 932]
            tone = 440 if place >= 24 else octave[place % 12] >> (place // 12)
        return pitch, tone

    # ---- keys -----------------------------------------------------------------------------------------------
    def key(self, timeout=None):
        end = None if timeout is None else time.time() + timeout
        while True:
            self.background()
            k = self.kbd.get(0.2)
            if k is None:
                if end is not None and time.time() > end:
                    return None
                continue
            # The host key: LEFT Control + LEFT Alt (evdev 29 + 56). Right Alt (AltGr, 100) and Right Control (97)
            # are ordinary keys and reach the guest, so Polish AltGr letters still work in a guest.
            if k.code in hvkeys.HOST_MODS:
                both = k.ctrl and k.alt
                if k.down and not k.repeat and both:
                    self.chord, self.chord_used = True, False
                elif not k.down and self.chord:
                    self.chord = False
                    if not self.chord_used:            # Left Ctrl + Left Alt alone: the keyboard moves
                        had = self.focus
                        if self.focus:
                            self.forward(k)
                        if self.focus or any(m.running() for m in self.machines.values()):
                            self.host_key(hvkeys.Key(57, True, False, set()))
                        if had and not self.focus:
                            return hvkeys.Key(0, True, False, set())  # name Key0: the menu says where it is again
                        continue
                if self.focus is not None:
                    self.forward(k)
                continue
            if self.chord and k.down and k.ctrl and k.alt:
                self.chord_used = True
                if not k.repeat and (k.name in hvkeys.HOST_COMMANDS or (self.focus and k.name in hvkeys.REVIEW_KEYS)):
                    had = self.focus
                    if self.focus:
                        self.release_guest_keys()
                    self.host_key(k)
                    if had and not self.focus:
                        return hvkeys.Key(0, True, False, set())
                    continue
            if self.focus is not None:
                self.forward(k)
                continue
            if not k.down:
                continue
            if k.ctrl and k.alt and k.name in ('u', 'i', 'o', 'j', 'k', 'l', 'm', 'comma', 'period'):
                self.review_key(k.name)
                continue
            return k

    def forward(self, k):
        m = self.machines.get(self.focus)
        if not m or not m.running():
            self.focus = None
            return
        q = hvkeys.QCODE.get(k.code)
        if not q:
            return
        try:
            if k.down:
                self.forwarded.add(q)
            else:
                self.forwarded.discard(q)
            m.send_key(q, k.down)
        except Exception as e:
            log('forward %s: %r' % (q, e))
        self.key_page_echo(m, k)
        self.key_echo(m, k)
        if k.down and (m.cfg.get('reader') == 'hypervisor' or m.cfg.get('setup_phase')):
            # the screen is read 0.3 s after a key (not up to 1.5 s later), so cursor and highlight follow the typing
            self._last_auto = min(getattr(self, '_last_auto', 0), time.time() - 1.2)

    def auto_on(self, m):
        """6ze: does the hypervisor read machine m's screen by itself? The host key A of this run wins; a guest read by the
        hypervisor follows 'auto_read'; a guest with its own reader is read while its screen is text (firmware screen,
        boot loader menu, boot messages: 'read_boot') and the hypervisor steps back once its graphical desktop - where
        its own reader runs - is up ('step_back'); a restart brings the text phase back."""
        if m.id in self.auto_read:
            return self.auto_read[m.id]
        if m.cfg.get('reader') == 'hypervisor' or m.cfg.get('setup_phase'):
            return bool(self.store.get('auto_read', m.id))
        sc = self.screens.get(m.id)
        if sc is None or getattr(sc, 'mode', 'text') != 'graphic':
            return bool(self.store.get('read_boot', m.id))
        if not self.store.get('step_back', m.id):
            return True
        # session 9: by HEARING the guest's reader (listen_guest), not by the screen mode alone
        lst = getattr(self, 'listeners', {}).get(m.id)
        return not (lst and lst[1].stepped)

    def key_echo(self, m, k):
        """6g, Speakup-style key echo: only where the HYPERVISOR is the guest's reader (DOS, 9x, boot menus, Setup) -
        a guest reader (NVDA, Speakup, Orca) echoes keys itself - and never on a product-key page (its own private echo)."""
        if k.down and not k.repeat and m.id in getattr(self, 'listeners', {}):
            self.listeners[m.id][1].key(time.time())      # 6ze: does the guest's own reader answer the keys?
        if not k.down or k.repeat or not self.store.get('key_echo', m.id):
            return
        sc = self.screens.get(m.id)
        if sc is not None and getattr(sc, 'key_page', None):
            return
        if not (m.cfg.get('reader') == 'hypervisor' or m.cfg.get('setup_phase')):
            return
        ch = k.char or ''
        if len(ch) == 1 and ch.isprintable():
            self.say({' ': self.T('space', 'spacja')}.get(ch, ch))
        elif k.name in ('Enter', 'Backspace', 'Tab', 'Escape', 'Delete'):
            self.say({'Enter': self.T('enter', 'enter'), 'Backspace': self.T('backspace', 'backspace'), 'Tab': self.T('tab', 'tab'),
                      'Escape': self.T('escape', 'escape'), 'Delete': self.T('delete', 'delete')}[k.name])

    def key_page_echo(self, m, k):
        """6h: while the guest shows a product-key page, what is typed is followed per field ("field 2 of 5") and
        echoed - privately: never in said.txt, the log or on the console (the key is not a secret, but not ours)."""
        sc = self.screens.get(m.id)
        kp = getattr(sc, 'key_page', None) if sc else None
        if not kp or not k.down:
            return
        fields, size = kp['fields'], kp['size']
        if k.name == 'Backspace':
            kp['typed'] = max(0, kp['typed'] - 1)
            self.say(self.T('deleted, field %d of %d, %d of %d characters' % (kp['typed'] // size + 1, fields, kp['typed'] % size, size),
                            'usunięto, pole %d z %d, znaków %d z %d' % (kp['typed'] // size + 1, fields, kp['typed'] % size, size)), private=True)
            return
        ch = k.char or ''
        if len(ch) != 1 or not ch.isalnum() or kp['typed'] >= fields * size:
            return
        kp['typed'] += 1
        n = kp['typed']
        text = ch.upper()
        if n % size == 1 or size == 1:
            text = self.T('field %d of %d, ' % ((n - 1) // size + 1, fields), 'pole %d z %d, ' % ((n - 1) // size + 1, fields)) + text
        if n == fields * size:
            text += self.T(', all %d characters typed' % n, ', wpisano wszystkie %d znaków' % n)
        self.say(text, private=True)

    def release_guest_keys(self):
        m = self.machines.get(self.focus)
        for q in list(self.forwarded):
            try:
                m.send_key(q, False)
            except Exception:
                pass
        self.forwarded.clear()

    def give_keyboard(self, m):
        if not m.running():
            # measured session 9: a machine menu opened while it ran, the guest shut itself down, then "Give it the
            # keyboard" handed the keyboard to a machine that was off (the menu went silent)
            self.say(self.T('%s is off.' % m.name, '%s jest wyłączony.' % m.name))
            return
        if self.focus and self.focus != m.id:
            self.release_guest_keys()
        self.focus = m.id
        self.say(self.T('Keyboard to %s. Left Control and Left Alt together give it back to the hypervisor.' % m.name,
                        'Klawiatura dla %s. Lewy Control i lewy Alt razem oddają ją hipernadzorcy.' % m.name))

    def host_key(self, k):
        running = [m for m in self.machines.values() if m.running()]
        cur = self.machines.get(self.focus) if self.focus else None
        if k.name == 'Space':
            if self.focus:
                self.release_guest_keys()
                self.focus = None
                self.say(self.T('Keyboard to the hypervisor.', 'Klawiatura dla hipernadzorcy.'))
            elif self.last_machine and self.last_machine.running():
                self.give_keyboard(self.last_machine)
            elif running:
                self.give_keyboard(running[0])
            else:
                self.say(self.T('No machine is running.', 'Żadna maszyna nie działa.'))
        elif k.name in ('PageDown', 'PageUp'):
            if not running:
                self.say(self.T('No machine is running.', 'Żadna maszyna nie działa.'))
                return
            ids = [m.id for m in running]
            at = ids.index(self.focus) if self.focus in ids else -1
            at = (at + (1 if k.name == 'PageDown' else -1)) % len(ids)
            self.last_machine = running[at]
            self.give_keyboard(running[at])
        elif k.name == 'r':
            m = cur or self.last_machine
            if m and m.running():
                self.read_guest(m, whole=True)
        elif k.name == 'a':
            m = cur or self.last_machine
            if m:
                on = not self.auto_on(m)
                self.auto_read[m.id] = on
                self.say(self.T('Automatic reading of %s %s.' % (m.name, 'on' if on else 'off'),
                                'Automatyczne czytanie %s %s.' % (m.name, 'włączone' if on else 'wyłączone')))
        elif k.name == 'Delete':
            if cur:
                cur.send_combo(['ctrl', 'alt', 'delete'])
                self.say(self.T('Control Alt Delete sent to %s.' % cur.name, 'Wysłano Control Alt Delete do %s.' % cur.name))
        elif k.name == 'e':
            # 6ze: the settings over a running guest; the guest keeps running, the keyboard comes back to it after
            had = self.focus
            self.focus = None
            self.say(self.T('Hypervisor settings; %s keeps running.' % cur.name if cur else 'Hypervisor settings.',
                            'Ustawienia hipernadzorcy; %s działa dalej.' % cur.name if cur else 'Ustawienia hipernadzorcy.'))
            self.settings_menu(over=had)
            if had and self.machines.get(had) and self.machines[had].running():
                self.give_keyboard(self.machines[had])
        elif k.name == 's':
            m = cur or self.last_machine
            self.say(self.status_text(m) if m else self.T('No machine.', 'Brak maszyny.'))
        elif k.name == 'Backspace':
            # 6j one-key revert: pressed twice within 4 seconds (once only explains, so it is never done by accident)
            m = cur or self.last_machine
            if not (m and m.running()):
                self.say(self.T('No machine is running.', 'Żadna maszyna nie działa.'))
            elif time.time() - getattr(self, '_revert_armed', 0) < 4:
                self._revert_armed = 0
                self.revert_online(m)
            else:
                self._revert_armed = time.time()
                self.say(self.T('Press it again to take %s back to the snapshot before going online; everything since then is lost.' % m.name,
                                'Naciśnij ponownie, aby przywrócić %s do migawki sprzed wyjścia do sieci; wszystko od tamtej chwili przepadnie.' % m.name))
        elif k.name in ('u', 'i', 'o', 'j', 'k', 'l', 'm', 'comma', 'period'):
            m = cur or self.last_machine
            if m and m.running():
                sc = self.screens.get(m.id)
                if sc is None or not sc.lines:
                    sc = self.screens.setdefault(m.id, hvreader.GuestScreen(m, self.pl))
                    sc.read()
                if self.review is not sc.lines:
                    self.review = sc.lines
                    self.rline = max(0, min(self.rline, len(self.review) - 1))
            self.review_key(k.name)
        elif k.name == 'F1':
            self.say(self.T('Left Control and Left Alt together, alone or with Space: keyboard. With Page Up or Page Down next machine, R read the screen, A automatic reading, '
                            'S state, Backspace twice back to the snapshot before going online, Delete sends Control Alt Delete, U I O lines, J K L words, M comma period characters.',
                            'Lewy Control i lewy Alt razem, same lub ze Spacją: klawiatura. Z Page Up lub Page Down następna maszyna, R czytaj ekran, A automatyczne czytanie, '
                            'S stan, Backspace dwa razy powrót do migawki sprzed wyjścia do sieci, Delete wysyła Control Alt Delete, U I O wiersze, J K L słowa, M przecinek kropka znaki.'))

    last_machine = None
    chord = chord_used = False

    def review_key(self, name):
        lines = [l for l in self.review if l.strip()] or ['']
        self.rline = max(0, min(self.rline, len(lines) - 1))
        if name in ('u', 'i', 'o'):
            self.rline = max(0, min(len(lines) - 1, self.rline + {'u': -1, 'i': 0, 'o': 1}[name]))
            self.rword = self.rchar = 0
            self.say(lines[self.rline] or self.T('blank', 'pusty'))
        elif name in ('j', 'k', 'l'):
            w = words(lines[self.rline]) or ['']
            self.rword = max(0, min(len(w) - 1, self.rword + {'j': -1, 'k': 0, 'l': 1}[name]))
            self.say(w[self.rword] or self.T('blank', 'pusty'))
        else:
            line = lines[self.rline]
            self.rchar = max(0, min(max(0, len(line) - 1), self.rchar + {'m': -1, 'comma': 0, 'period': 1}[name]))
            ch = line[self.rchar] if line else ''
            self.say({' ': self.T('space', 'spacja'), '': self.T('blank', 'pusty')}.get(ch, ch))

    # ---- guest events, state messages, automatic reading ----------------------------------------------------
    def on_event(self, m, ev):
        self.events.put((m, ev))

    def background(self):
        while True:
            try:
                m, ev = self.events.get_nowait()
            except queue.Empty:
                break
            self.state_message(m, ev)
        if self.proxies:
            self.poll_proxies()
        for mid in [k for k in getattr(self, 'shares', {}) if k not in self.proxies]:
            # session 10, found live: a share server whose proxy was not taken back stayed after its machine stopped
            sm = self.machines.get(mid)
            if not sm or not sm.running():
                hvshare.stop(self.shares.pop(mid, None))
        if time.time() - getattr(self, '_last_print', 0) > 1.0:
            self._last_print = time.time()
            self.poll_prints()
            self.linda_poll()
            self.poll_suricata()        # 6v-2: network virtual patching alerts said aloud
        now = time.time()
        if now - getattr(self, '_last_auto', 0) > 1.5:
            self._last_auto = now
            for m in list(self.machines.values()):
                if m.running():
                    self.listen_guest(m, now)
                if m.running() and self.auto_on(m) and self.focus == m.id:
                    self.read_guest(m, whole=False)

    def listen_guest(self, m, now):
        """6ze: a guest with its OWN reader - the hypervisor hears whether it speaks (hvlisten, QEMU's capture of the
        machine's sound) and steps back / reads again; said once each time."""
        import hvlisten
        if m.cfg.get('reader') == 'hypervisor' or m.cfg.get('setup_phase') or not self.store.get('step_back', m.id):
            return
        if not hasattr(self, 'listeners'):
            self.listeners = {}
        if m.id not in self.listeners:
            sb = hvlisten.StepBack()
            # a machine taken back after a restart of the menu keeps its state (measured session 9: a fresh state made
            # the hypervisor read over NVDA on XP's desktop until NVDA spoke again)
            sb.stepped = bool(hvmachine.load_state(m).get('stepped'))
            self.listeners[m.id] = (hvlisten.Capture(m), sb)
        cap, sb = self.listeners[m.id]
        sc = self.screens.get(m.id)
        if sc is None:
            cap.peak()               # the screen is not known yet (first look after a start or a take-back)
            return
        if getattr(sc, 'mode', 'text') != 'graphic':
            if sb.stepped:
                hvmachine.save_state(m, stepped=False)
            sb.text_screen()
            cap.peak()               # what was played during the text phase is not the reader's
            return
        was = sb.stepped
        what = sb.update(cap.peak(), now)
        if sb.stepped != was:
            hvmachine.save_state(m, stepped=sb.stepped)
        if what == 'back':
            log('step back: %s plays sound on its desktop' % m.id)
            self.say(self.T("%s's own screen reader speaks; the hypervisor steps back. Left Control, Left Alt and A read again." % m.name,
                            'Własny czytnik ekranu %s mówi; hipernadzorca milknie. Lewy Control, lewy Alt i A znów czytają.' % m.name))
        elif what == 'reads':
            log('step back ended: %s silent after key presses' % m.id)
            self.say(self.T("%s's own screen reader does not answer the keys; the hypervisor reads again." % m.name,
                            'Własny czytnik ekranu %s nie odpowiada na klawisze; hipernadzorca znów czyta.' % m.name))

    def state_message(self, m, ev):
        e = ev.get('event')
        log('event %s %s %s' % (m.id, e, json.dumps(ev.get('data', {}))[:300]))
        n = m.name
        if e == 'SHUTDOWN' and getattr(m, 'restarting_until', 0) > time.time():
            pass        # 6zc: restarted onto a snapshot saved with another hardware profile (hvmachine.snapshot_revert)
        elif e == 'SHUTDOWN':
            guest = ev.get('data', {}).get('guest')
            if self.store.get('state_messages', m.id):
                self.say(self.T('%s shut down%s.' % (n, '' if guest else ' by the hypervisor'),
                                '%s wyłączony%s.' % (n, '' if guest else ' przez hipernadzorcę')))
        elif e == 'RESET':
            if self.store.get('state_messages', m.id):
                self.say(self.T('%s restarts.' % n, '%s uruchamia się ponownie.' % n))
            sc = self.screens.get(m.id)
            if sc is not None:
                sc.mode = 'unknown'      # 6ze: a restart is a text phase again (firmware, boot loader, boot messages)
            # XP: text phase -> GUI phase -> desktop: two restarts end Setup. QEMU reports one restart as two RESET
            # events a second apart (measured 17:20:41/42), so events closer than 20 s count once.
            if m.cfg.get('setup_phase') and time.time() - getattr(m, 'last_reset', 0) > 20:
                m.last_reset = time.time()
                m.cfg['setup_resets'] = m.cfg.get('setup_resets', 0) + 1
                if m.cfg['setup_resets'] >= 2:
                    m.cfg['setup_phase'] = False
                m.save()
            if m.installing() and not getattr(m, 'plugged', False) and m.usb_drives():
                try:
                    m.plug_usb_drives()
                    m.plugged = True
                    m.cfg['installing'] = False
                    m.save()
                    log('usb drives plugged into %s after Setup restarted' % m.id)
                except Exception as ex:
                    log('plug usb %s: %r' % (m.id, ex))
                    self.say(self.T('The setup drive could not be given to %s: %s' % (n, ex), 'Nie udało się podłączyć dysku instalacyjnego do %s: %s' % (n, ex)))
        elif e == 'STOP' and self.store.get('state_messages', m.id):
            self.say(self.T('%s paused.' % n, '%s wstrzymany.' % n))
        elif e == 'RESUME' and self.store.get('state_messages', m.id):
            self.say(self.T('%s runs.' % n, '%s działa.' % n))
        elif e == 'BLOCK_IO_ERROR':
            d = ev.get('data', {})
            self.say(self.T('Disk error in %s: %s. The machine is stopped; free space on the data partition, then resume it.' % (n, d.get('reason', d.get('operation', ''))),
                            'Błąd dysku w %s: %s. Maszyna wstrzymana; zwolnij miejsce na partycji danych i wznów ją.' % (n, d.get('reason', d.get('operation', '')))))
        elif e == 'GUEST_PANICKED':
            self.say(self.T('%s crashed: the guest system reported a fatal error.' % n, '%s uległ awarii: system gościa zgłosił błąd krytyczny.' % n))
        elif e == 'QMP_CLOSED':
            time.sleep(0.3)
            code = m.proc.poll() if m.proc else None
            oom = self.oom_killed(m)
            if self.focus == m.id:
                self.focus = None
            if oom:
                self.say(self.T('%s was stopped: the computer ran out of memory.' % n, '%s zatrzymany: komputerowi zabrakło pamięci.' % n))
            elif code not in (0, None):
                self.say(self.T('%s ended with an error: %s' % (n, m.error_text() or code), '%s zakończył się błędem: %s' % (n, m.error_text() or code)))
            else:
                self.say(self.T('%s is off.' % n, '%s jest wyłączony.' % n))
            m.state = 'off'

    def oom_killed(self, m):
        try:
            text = subprocess.run(['dmesg'], capture_output=True, text=True, timeout=5).stdout
        except Exception:
            return False
        return m.proc is not None and ('Killed process %d' % m.proc.pid) in text

    def watch(self):
        """A guest that does not answer is named (hang)."""
        silent = {}
        while True:
            time.sleep(5)
            for m in list(self.machines.values()):
                if not m.running() or not m.qmp:
                    continue
                try:
                    m.qmp.cmd('query-status', timeout=8)
                    if silent.pop(m.id, None):
                        self.events.put((m, {'event': 'RESUME'}))
                except TimeoutError:
                    if not silent.get(m.id):
                        silent[m.id] = True
                        self.say(self.T('%s does not answer.' % m.name, '%s nie odpowiada.' % m.name))
                except Exception:
                    pass

    def speak_model(self, m, sc):
        """6g: a guest's boot manager or text menu (SeaBIOS boot menu, GRUB, NTLDR's "Please select the operating
        system to start", Syslinux, FreeDOS menus...) read with Charon's screen model, as Charon's resident reader
        reads boot managers: the list once with its title and size, then "entry, k of N" on every move, "default",
        the countdown once in words and then as ticks. True when the model spoke for this screen (sure list)."""
        md = sc.model
        if not md or md.get('list.sure') != '1' or md.get('list.found') != '1':
            sc.seconds = []
            sc.last_list = None
            return False
        count, sel = int(md.get('list.count', 0) or 0), int(md.get('list.selected', -1) or -1)
        if sel < 0 or not count:
            return False
        title = md.get('title', '')
        item = md.get('item.%d' % sel, '')
        flags = int(md.get('item.%d.flags' % sel, 0) or 0)
        T = self.T
        parts = []
        if getattr(sc, 'last_list', None) is None or sc.last_list[0] != title or sc.last_list[1] != count:
            parts.append(T('%s. Menu, %d entries.' % (title, count) if title else 'Menu, %d entries.' % count,
                           '%s. Menu, pozycji: %d.' % (title, count) if title else 'Menu, pozycji: %d.' % count))
            sel_changed = True
        else:
            sel_changed = sc.last_list[2] != sel
        if sel_changed:
            extra = (T(', default', ', domyślna') if flags & 2 else '') + (T(', submenu', ', podmenu') if flags & 4 else '') + \
                    (T(', unavailable', ', niedostępna') if flags & 8 else '')
            parts.append(T('%s%s, %d of %d.' % (item, extra, sel + 1, count), '%s%s, %d z %d.' % (item, extra, sel + 1, count)))
        sc.last_list = (title, count, sel)
        secs = md.get('countdown.seconds')
        if secs is not None and md.get('countdown.sure') == '1':
            s = int(secs)
            if not sc.seconds:
                parts.append(T('Starts the selected entry in %d seconds.' % s, 'Uruchomi wybraną pozycję za %d s.' % s))
            elif s < sc.seconds[-1] and not parts and self.store.get('ticks', m.id):
                self.voice.tone(1320 if s <= 3 else 880, 40)     # a tick per second, as Charon's countdown ticks
            sc.seconds = (sc.seconds + [s])[-4:]
        else:
            sc.seconds = []
        if parts:
            self.say(' '.join(parts), interrupt=sel_changed)
        return True

    def read_guest(self, m, whole):
        sc = self.screens.setdefault(m.id, hvreader.GuestScreen(m, self.pl))
        old = list(sc.lines)
        old_cur, old_cells = sc.cursor, sc.cells
        lines = sc.read()
        self.review = lines
        # 6h: a product-key page is announced once, with its fields; its refusal message is read in full as an error
        kp = hvreader.key_page(lines)
        if kp and not getattr(sc, 'key_page', None):
            sc.key_page = dict(kp, typed=0)
        if kp and sc.key_page.get('typed') == 0 and not sc.key_page.get('said') and self.store.get('key_page', m.id):
            sc.key_page['said'] = True
            kind ={'volume': self.T(' It wants a volume-licence key.', ' Strona chce klucza licencji zbiorczej (woluminów).'),
                    'oem': self.T(' It wants an OEM key.', ' Strona chce klucza OEM.')}.get(kp['kind'], '')
            self.say(self.T('%s Setup is asking for the product key now: %d fields of %d characters, the cursor is in field 1.%s'
                            % (m.name, kp['fields'], kp['size'], kind),
                            '%s: Instalator prosi teraz o klucz produktu: pól %d, w każdym %d znaków, kursor w polu 1.%s'
                            % (m.name, kp['fields'], kp['size'], kind)))
        elif not kp and getattr(sc, 'key_page', None) and lines:
            sc.key_page = None
        err = hvreader.key_refused(lines)
        if err and err != getattr(sc, 'key_error', None):
            self.say(self.T('Error: ', 'Błąd: ') + err)
            if getattr(sc, 'key_page', None):
                sc.key_page['typed'] = 0
        sc.key_error = err
        if not whole and self.store.get('read_loader', m.id) and self.speak_model(m, sc):
            return
        if not whole and self.store.get('tracking', m.id) and sc.mode == 'text' and old_cells and old == lines:
            # 6g Speakup-style tracking on an unchanged text screen: a moved highlight is read (menus that the screen
            # model does not know), else a moved cursor (line / character / word)
            hl, old_hl = hvreader.highlighted_rows(sc.cells[1]), hvreader.highlighted_rows(old_cells[1])
            if hl and hl != old_hl:
                self.say(' '.join(lines[r].strip() for r in hl[:2] if r < len(lines)) or self.T('blank', 'pusty'))
                return
            said = hvreader.cursor_speech(lines, old_cur, sc.cursor)
            if said is not None:
                self.say({'': self.T('blank', 'pusty'), ' ': self.T('space', 'spacja')}.get(said, said))
                return
        if whole:
            text = ' '.join(l.strip() for l in lines if l.strip())
            self.say(text or self.T('The screen of %s is empty.' % m.name, 'Ekran %s jest pusty.' % m.name))
        else:
            new = hvreader.changes(old, lines)
            # a bare percentage (XP's text Setup progress "51%", read as "51x" by the font) is said every 10 percent,
            # as Charon's own progress speech does, not every time it moves
            keep = []
            for l in new:
                p = re.fullmatch(r'\s*(\d{1,3})\s*[%xX]\s*', l)
                if p:
                    tens = int(p.group(1)) // 10
                    if tens == getattr(sc, 'progress_tens', None):
                        continue
                    sc.progress_tens = tens
                    l = self.T('%s percent' % p.group(1), '%s procent' % p.group(1))
                keep.append(l)
            new = keep
            if new and old:
                self.say(' '.join(l.strip() for l in new[:12]), interrupt=False)

    def status_text(self, m):
        st = self.T('running', 'działa') if m.running() else self.T('off', 'wyłączona')
        try:
            if m.running() and m.qmp.cmd('query-status').get('status') == 'paused':
                st = self.T('paused', 'wstrzymana')
        except Exception:
            pass
        return '%s, %s, %s.' % (m.name, st, self.T('%d MB memory' % m.cfg.get('memory', 0), '%d MB pamięci' % m.cfg.get('memory', 0)))

    # ---- menus and fields -------------------------------------------------------------------------------------
    def menu(self, title, items, start=0, help_text=None):
        """items: list of (label, value). Returns value, or None for Escape."""
        if not items:
            return None
        at = max(0, min(start, len(items) - 1))
        def announce(first=False):
            label = items[at][0]
            n = len(items)
            parts = [label]
            if self.fmt_item(self.ITEM_POSITION):
                parts.append(self.T('%d of %d' % (at + 1, n), '%d z %d' % (at + 1, n)))
            if self.fmt_item(self.ITEM_STRUCTURE) and n > 1 and at in (0, n - 1):
                parts.append(self.T('start of list', 'początek listy') if at == 0 else self.T('end of list', 'koniec listy'))
            self.review = [title] + [l for l, _ in items]
            self.rline = at + 1
            head = ''
            if first:
                head = title.rstrip()
                if self.fmt_item(self.ITEM_INTRO):
                    head += self.T(', menu, %d items' % n, ', menu, pozycji: %d' % n)
                head = (head if head[-1:] in '.?!:' else head + '.') + ' '
            tail = ''
            if first and self.fmt_item(self.ITEM_HINTS):
                tail = ' ' + self.T('Arrows move, Enter chooses, Escape goes back.', 'Strzałki przesuwają, Enter wybiera, Escape cofa.')
            pitch, tone = self.fmt_place(at, n)
            if tone:
                self.voice.tone(tone, 40)
                time.sleep(0.05)
            self.say(head + ', '.join(parts) + tail, pitch=pitch if not first else 1.0)
        if self.focus is None:
            announce(True)
        while True:
            k = self.key()
            n = k.name
            if n == 'Key0':
                self.voice.wait(5)
                announce(True)
                continue
            if n == 'Down':
                at = min(len(items) - 1, at + 1); announce()
            elif n == 'Up':
                at = max(0, at - 1); announce()
            elif n in ('Home', 'PageUp'):
                at = 0; announce()
            elif n in ('End', 'PageDown'):
                at = len(items) - 1; announce()
            elif n in ('Enter', 'KpEnter'):
                return items[at][1]
            elif n == 'Escape':
                return None
            elif n == 'F1':
                self.say(help_text or self.T('Arrows move, Enter chooses, Escape goes back. Left Control, Left Alt and F1: keys of the machines.',
                                             'Strzałki przesuwają, Enter wybiera, Escape cofa. Lewy Control, lewy Alt i F1: klawisze maszyn.'))
            elif n == 'Space' and k.shift is False and False:
                pass
            elif k.char and k.char.strip():
                c = k.char.lower()
                order = list(range(at + 1, len(items))) + list(range(0, at + 1))
                for i in order:
                    if items[i][0].lower().startswith(c):
                        at = i; announce(); break

    def ask_text(self, prompt, default='', private=False):
        """private (6h product key): what is typed is spoken but never logged or shown on the console."""
        text = default
        fresh = bool(default)
        self.say('%s %s' % (prompt, (self.T('Now: %s. Typing replaces it, Enter keeps it.' % default, 'Teraz: %s. Pisanie to zastępuje, Enter zostawia.' % default)) if default else ''))
        while True:
            k = self.key()
            if k.name in ('Enter', 'KpEnter'):
                return text
            if k.name == 'Escape':
                return None
            if k.name == 'Backspace':
                fresh = False
                if text:
                    gone, text = text[-1], text[:-1]
                    self.say(self.T('deleted %s' % gone, 'usunięto %s' % gone), private=private)
                continue
            if k.name == 'Delete':
                text = ''
                fresh = False
                self.say(self.T('cleared', 'wyczyszczono'))
                continue
            if k.ctrl and k.name == 'i' or k.name == 'F2':
                self.say(text or self.T('empty', 'puste'), private=private)
                continue
            if k.char:
                if fresh:                 # the first typed character replaces the proposed answer (as a selected field)
                    text = ''
                fresh = False
                text += k.char
                self.say(k.char if k.char != ' ' else self.T('space', 'spacja'), private=private)

    def ask_number(self, prompt, default, low, high):
        while True:
            t = self.ask_text(prompt + ' ' + self.T('From %d to %d.' % (low, high), 'Od %d do %d.' % (low, high)), str(default))
            if t is None:
                return None
            try:
                v = int(t.strip())
                if low <= v <= high:
                    return v
            except ValueError:
                pass
            self.say(self.T('Not a number from %d to %d.' % (low, high), 'To nie jest liczba od %d do %d.' % (low, high)))

    def yes_no(self, title, default=True):
        items = [(self.T('yes', 'tak'), True), (self.T('no', 'nie'), False)]
        return self.menu(title, items, 0 if default else 1)

    # ---- machines.txt for Charon's device list ---------------------------------------------------------------
    def write_machines_txt(self):
        path = BOOT + '/charonhv/machines.txt'
        lines = ['# Charon Hypervisor machines 1']
        for mid in hvmachine.ids():
            m = self.machines.get(mid)
            if m and m.cfg.get('charon_entry', True):
                lines.append('%s|%s' % (mid, m.name.replace('|', ' ')))
        try:
            tmp = path + '.new'
            with open(tmp, 'w', encoding='utf-8', newline='\n') as f:
                f.write('\n'.join(lines) + '\n')
                f.flush(); os.fsync(f.fileno())
            os.replace(tmp, path)
            log('machines.txt: %d machines' % (len(lines) - 1))
        except OSError as e:
            log('machines.txt: %r' % e)
            self.say(self.T('The list of machines for Charon could not be written: %s' % e.strerror,
                            'Nie udało się zapisać listy maszyn dla Charona: %s' % e.strerror))

    # ---- start / stop -----------------------------------------------------------------------------------------
    def mem_available(self):
        try:
            for line in open('/proc/meminfo'):
                if line.startswith('MemAvailable:'):
                    return int(line.split()[1]) // 1024
        except OSError:
            pass
        return 0

    SWITCHES = (('internet', None), ('simple_web', 'simple'), ('scan_downloads', 'scan'), ('daily_snapshot', 'daily'))

    def apply_switches(self, m):
        """6ze: the on/off switches of Secure use and the retro assistant live in Hypervisor settings (general value +
        this machine's exception). A machine switched before keeps its value as an exception (moved once); at every
        start the values in force are written where the machine's parts read them (machine.json for the internet,
        retro.json for the proxy's simple web and scanning)."""
        r = hvretro.retro_cfg(m)
        if not m.cfg.get('switches_in_settings'):
            for key, rk in self.SWITCHES:
                old = m.cfg.get('internet') if rk is None else r.get(rk)
                if old is not None and bool(old) != bool(self.store.get(key)):
                    self.store.set(key, bool(old), m.id)
            m.cfg['switches_in_settings'] = True
        m.cfg['internet'] = bool(self.store.get('internet', m.id))
        m.save()
        for key, rk in self.SWITCHES:
            if rk:
                r[rk] = bool(self.store.get(key, m.id))
        hvretro.retro_save(m, r)

    def start(self, m, keyboard=True):
        if m.running():
            self.give_keyboard(m) if keyboard else None
            return True
        need = int(m.cfg.get('memory', 512)) + 128
        have = self.mem_available()
        if have and have < need:
            self.say(self.T('%s needs %d MB of memory, only %d MB are free. Stop another machine or give it less memory.' % (m.name, need, have),
                            '%s potrzebuje %d MB pamięci, wolnych jest tylko %d MB. Zatrzymaj inną maszynę albo daj jej mniej pamięci.' % (m.name, need, have)))
            return False
        self.say(self.T('Starting %s.' % m.name, 'Uruchamiam %s.' % m.name))
        self.apply_switches(m)
        if m.cfg.get('secure') and m.cfg.get('nic', 'none') != 'none' and m.cfg.get('internet', True):
            self.start_proxy(m)
            # session 10, found live: scanning was ON but clamd was not running after a reboot - downloads passed
            # unscanned (said once as SCANOFF) and XP's Security Center rightly showed the protection off
            if self.store.get('scan_downloads', m.id) and hvscan.ready() and not hvscan.clamd_running():
                threading.Thread(target=self.scanner_back, daemon=True).start()
            if self.store.get('virtual_patching', m.id):     # 6v-2: Suricata network virtual patching
                threading.Thread(target=self.suricata_back, args=(m,), daemon=True).start()
        m.sound_latency = self.store.get('sound_latency', m.id)      # session 10: guest sound delay
        if not m.start(self.kvm, self.on_event, profile=hvmachine.PROFILE_VERSION if self.store.get('performance', m.id) else 0):
            self.say(self.T('%s did not start: %s' % (m.name, m.error_text() or 'QEMU ended'),
                            '%s się nie uruchomił: %s' % (m.name, m.error_text() or 'QEMU zakończył pracę')))
            self.stop_proxy(m)
            return False
        log('started %s pid %d kvm=%s' % (m.id, m.proc.pid, self.kvm))
        self.save_helpers(m)
        self.screens.pop(m.id, None)        # 6ze: a fresh start begins with its text phase
        # session 10, found live: the listener of the machine's PREVIOUS run stayed (stepped back, capture of the old
        # QEMU) -> auto_on() was off, the screen was never looked at, nothing of the boot was read and the step back was
        # never said; a fresh start begins with a fresh listener
        getattr(self, 'listeners', {}).pop(m.id, None)
        own = sorted(self.store.data['machines'].get(m.id, {}))
        if own:
            self.say(self.T('Settings for this machine in force: ', 'Obowiązują ustawienia dla tej maszyny: ') +
                     '; '.join(self.store.label(k, self.T, m.id).split(',')[0] for k in own if k in hvsettings.SPEC), interrupt=False)
        note = getattr(m, 'exchange_note', '')
        if note.startswith('error'):
            log('exchange sync in %s: %s' % (m.id, note))
            self.say(self.T('The exchange drive of %s could not be prepared: %s' % (m.name, note[7:]),
                            'Nie udało się przygotować dysku wymiany %s: %s' % (m.name, note[7:])), interrupt=False)
        elif note:
            self.say(self.T('%s new %s put on the exchange drive of %s.' % (note, 'file' if note == '1' else 'files', m.name),
                            'Nowe pliki na dysku wymiany %s: %s.' % (m.name, note)), interrupt=False)
        for rel, virus in getattr(m, 'exchange_held', []):
            log('exchange held back %s: %s %s' % (m.id, rel, virus))
            self.say(self.T('Not put on the exchange drive of %s: %s contains %s.' % (m.name, rel, virus),
                            'Nie trafiło na dysk wymiany %s: %s zawiera %s.' % (m.name, rel, virus)), interrupt=False)
        if m.cfg.get('secure') and not m.installing():
            self.snapshot_before_online(m)
            if self.store.get('daily_snapshot', m.id):
                self.daily_snapshot(m)
        elif not m.cfg.get('secure') and m.cfg.get('nic', 'none') != 'none' and hvsecure.supported(m.cfg):
            self.say(self.T('Warning: %s goes online without the secure-use profile. This Windows gets no support any more; '
                            'switch the profile on in the machine\'s menu, Secure use.' % m.name,
                            'Uwaga: %s wychodzi do sieci bez profilu bezpiecznego używania. Ten Windows nie ma już wsparcia; '
                            'włącz profil w menu maszyny, Bezpieczne używanie.' % m.name), interrupt=False)
        self.voice.wait(10)
        self.say(self.T('%s runs%s.' % (m.name, '' if self.kvm else ', emulated without hardware virtualisation, slowly'),
                        '%s działa%s.' % (m.name, '' if self.kvm else ', emulowany bez wirtualizacji sprzętowej, wolno')), interrupt=False)
        self.last_machine = m
        if keyboard:
            self.voice.wait(10)
            self.give_keyboard(m)
        return True

    # ---- secure-use profile, hypervisor side (6j) ------------------------------------------------------------
    proxies = {}
    shares = {}
    downloads = None

    def start_proxy(self, m):
        self.stop_proxy(m)
        bl = os.path.join(hvsecure.SECURE, 'blocklist.txt')
        if not os.path.exists(bl) or time.time() - os.path.getmtime(bl) > 86400:
            hvsecure.update_blocklist(self.say, self.T)
        s = socket.socket()
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
        s.close()
        logp = os.path.join(hvmachine.RUN, m.id + '.proxy.log')
        open(logp, 'w').close()
        mail = {}
        for kind in ('pop', 'imap', 'smtp'):         # 6q mail TLS bridge: one local port each, forwarded from 10.0.2.100
            s = socket.socket()
            s.bind(('127.0.0.1', 0))
            mail[kind] = s.getsockname()[1]
            s.close()
        m.mail_ports = mail
        # 6v: the read-only Downloads share of this machine (\\10.0.2.100\Downloads), when Samba is there
        m.share_port = 0
        if shutil.which('smbd') or os.path.exists('/usr/sbin/smbd'):
            s = socket.socket()
            s.bind(('127.0.0.1', 0))
            sp = s.getsockname()[1]
            s.close()
            proc = hvshare.start(m.id, sp, log)
            if proc is not None:
                m.share_port, self.shares[m.id] = sp, proc
        extra = ['--machine-dir', m.dir] + sum((['--mail-' + k, str(v)] for k, v in mail.items()), [])
        p = subprocess.Popen(['nice', '-n', '5', sys.executable, os.path.join(HERE, 'hvproxy.py'), '--port', str(port), '--dir', hvsecure.SECURE, '--log', logp] + extra,
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=open(logp + '.err', 'wb'),
                             start_new_session=True)     # outlives a restart of the menu with its machine
        for _ in range(100):            # the CA is made on the first start (openssl, a few seconds)
            if 'READY' in open(logp).read() or p.poll() is not None:
                break
            time.sleep(0.1)
        if p.poll() is not None:
            self.say(self.T('The filtering proxy for %s did not start, so it has no internet.' % m.name,
                            'Pośrednik z filtrem dla %s się nie uruchomił, więc nie ma internetu.' % m.name))
            m.proxy_port = 0
            return
        m.proxy_port = port
        self.proxies[m.id] = (p, logp, [0])
        log('proxy for %s on 127.0.0.1:%d pid %d' % (m.id, port, p.pid))

    def download_target(self):
        """The machine that receives a browser download: the one that asked for the modern browser, else the only
        running secure machine."""
        asked = getattr(hvretro.Retro, 'modern_machine', '')
        run = [m for m in self.machines.values() if m.running() and m.cfg.get('secure')]
        for m in run:
            if m.id == asked:
                return m
        return run[0] if len(run) == 1 else None

    def start_downloads(self):
        def scan(path):
            if not hvscan.ready():
                return None, self.T('the virus scanner is not installed', 'skaner antywirusowy nie jest zainstalowany')
            if not hvscan.clamd_running():
                hvscan.start_clamd(log)
            return hvscan.scan_bytes_file(path)
        try:
            self.downloads = hvshare.Watcher(self.download_target, scan,
                                             lambda en, pl: self.say(self.T(en, pl), interrupt=False), log)
        except OSError as e:
            log('downloads watcher: %r' % e)

    def scanner_back(self):
        """Session 10: scanning is on but ClamAV is not running (after a reboot its packages are not in RAM): the
        packages again from the Hypervisor partition's cache (nothing downloaded, so nothing asked), then clamd.
        When something would have to be downloaded, it is said instead (scanning is switched on again in settings)."""
        import hvpkg
        if not hvpkg.installed(hvscan.PKGS):
            mb, n, err = hvpkg.plan(hvscan.PKGS)
            if err or mb > 0:
                log('scanner back: %s' % (err or '%.1f MB to fetch' % mb))
                self.say(self.T('Downloads are not scanned now: ClamAV is not installed in this start. Hypervisor settings, '
                                'Network: switch scanning off and on again to fetch it.',
                                'Pobierane pliki nie są teraz skanowane: ClamAV nie jest zainstalowany w tym uruchomieniu. '
                                'Ustawienia hipernadzorcy, Sieć: wyłącz i włącz skanowanie, aby go pobrać.'), interrupt=False)
                return
            self.say(self.T('Installing the ClamAV virus scanner again from the Hypervisor partition.',
                            'Instaluję ponownie z partycji Hipernadzorcy: skaner wirusów ClamAV.'), interrupt=False)
            err = hvpkg.install(hvscan.PKGS)
            if err:
                log('scanner back: %s' % err)
                return
        err = hvscan.start_clamd(log)
        log('scanner back: clamd %s' % (err or 'running'))

    def suricata_back(self, m):
        """6v-2: start Suricata's network virtual patching for a protected machine, quietly. Packages from the
        Hypervisor partition's cache (nothing downloaded is asked in the background); the ET Open rules refreshed
        when online. If a download would be needed it is said instead; the protection then starts at the next online."""
        import hvpkg, hvsuricata
        try:
            if hvsuricata.running():
                return
            if not hvpkg.installed(hvsuricata.PKGS):
                mb, n, err = hvpkg.plan(hvsuricata.PKGS)
                if err or mb > 0:
                    log('suricata back: %s' % (err or '%.1f MB to fetch' % mb))
                    self.say(self.T('Network virtual patching is not on in this start: Suricata is not installed. Hypervisor '
                                    'settings, Network: switch it off and on again to fetch it.',
                                    'Sieciowe łatanie wirtualne nie działa w tym uruchomieniu: Suricata nie jest zainstalowana. '
                                    'Ustawienia hipernadzorcy, Sieć: wyłącz i włącz je ponownie, aby ją pobrać.'), interrupt=False)
                    return
                self.say(self.T('Installing Suricata again from the Hypervisor partition.',
                                'Instaluję ponownie z partycji Hipernadzorcy: Suricata.'), interrupt=False)
                if hvpkg.install(hvsuricata.PKGS):
                    return
            err = hvsuricata.update_rules()
            if err and not hvsuricata.rules_present():
                log('suricata back: rules %s' % err)
                self.say(self.T("Network virtual patching has no rule set yet: %s." % err,
                                'Sieciowe łatanie wirtualne nie ma jeszcze zestawu reguł: %s.' % err), interrupt=False)
                return
            err = hvsuricata.start()
            log('suricata back: %s' % (err or 'running, ET Open %s' % hvsuricata.rules_version()))
        except Exception as e:        # noqa: BLE001
            log('suricata back: %r' % e)

    def stop_proxy(self, m):
        p = self.proxies.pop(m.id, None)
        if p and p[0].poll() is None:
            p[0].terminate()
        hvshare.stop(self.shares.pop(m.id, None))

    def poll_proxies(self):
        """Refusals of the proxy are said aloud (BLOCKED, DENIED, CERTFAIL); a stopped machine's proxy ends."""
        for mid, (p, logp, pos) in list(self.proxies.items()):
            m = self.machines.get(mid)
            if not m or not m.running():
                self.stop_proxy(m or type('M', (), {'id': mid})())
                if m and m.has_exchange():
                    threading.Thread(target=self.exchange_after_stop, args=(m,), daemon=True).start()
                continue
            try:
                with open(logp, encoding='utf-8') as f:
                    f.seek(pos[0])
                    new = f.read()
                    pos[0] = f.tell()
            except OSError:
                continue
            for line in new.splitlines():
                parts = line.split(' ', 2)
                if len(parts) < 3:
                    continue
                kind, what = parts[1], parts[2]
                if kind == 'BLOCKED':
                    self.say(self.T('%s: blocked, %s is on the list of malicious domains.' % (m.name, what),
                                    '%s: zablokowano, %s jest na liście złośliwych domen.' % (m.name, what)), interrupt=False)
                elif kind == 'DENIED':
                    self.say(self.T('%s: refused, only web ports go out; %s.' % (m.name, what),
                                    '%s: odmowa, wychodzą tylko porty WWW; %s.' % (m.name, what)), interrupt=False)
                elif kind == 'CERTFAIL':
                    self.say(self.T('%s: the site\'s certificate is not valid, not connected: %s.' % (m.name, what),
                                    '%s: certyfikat strony jest nieważny, nie połączono: %s.' % (m.name, what)), interrupt=False)
                elif kind == 'UPDATES':
                    self.say_updates(m, what)
                elif kind == 'MODERNTLS' and not getattr(self, '_said_tls', False):
                    self._said_tls = True
                    self.say(self.T('The hypervisor speaks modern TLS for the old Windows of %s.' % m.name,
                                    'Hipernadzorca mówi nowoczesnym TLS za stary Windows %s.' % m.name), interrupt=False)
                else:
                    self.retro_event(m, kind, what)

    def say_updates(self, m, what):
        """6zd: the guest's Legacy Update status, said when it changes (and the first time in a run)."""
        text = update_status_text(m.name, what, self.T)
        if text and text != getattr(m, 'updates_said', None):
            m.updates_said = text
            self.say(text, interrupt=False)

    def suricata_machine(self, sport, dport):
        """Which running machine a loopback flow belongs to: its proxied/relayed traffic uses the machine's proxy,
        share or mail ports, which appear as one side of the connection Suricata sees on `lo`."""
        for m in self.machines.values():
            ports = [getattr(m, 'proxy_port', 0), getattr(m, 'share_port', 0)]
            ports += list((getattr(m, 'mail_ports', {}) or {}).values())
            if (sport in ports) or (dport in ports):
                return m
        return None

    def poll_suricata(self):
        """6v-2: network virtual patching. Suricata watches the hypervisor's loopback, where every guest's proxied web
        traffic and SMB/mail relays live, with the Emerging Threats Open rule set (EternalBlue/MS17-010, BlueKeep, old
        browser and SMB exploits). A rule that fires on a guest's flow is spoken; the signature is read aloud."""
        try:
            import hvsuricata
        except Exception:        # noqa: BLE001
            return
        if not hvsuricata.running():
            return
        if not hasattr(self, '_sur_pos'):
            try:
                self._sur_pos = os.path.getsize(hvsuricata.EVE)
            except OSError:
                self._sur_pos = 0
        alerts, self._sur_pos = hvsuricata.read_alerts(self._sur_pos)
        for sid, sig, sip, sport, dip, dport, action in alerts:
            m = self.suricata_machine(sport, dport)
            name = m.name if m else self.T('a machine', 'maszyna')
            if action == 'blocked':
                self.say(self.T('%s: the hypervisor blocked a known attack in its traffic, %s.' % (name, sig),
                                '%s: hipernadzorca zablokował znany atak w jej ruchu, %s.' % (name, sig)), interrupt=False)
            else:
                self.say(self.T('%s: the hypervisor detected a known attack in its traffic, %s.' % (name, sig),
                                '%s: hipernadzorca wykrył znany atak w jej ruchu, %s.' % (name, sig)), interrupt=False)
            hvvoice.log('suricata alert sid=%s action=%s for %s: %s' % (sid, action, name, sig))

    def snapshot_before_online(self, m):
        T = self.T
        online = m.cfg.get('nic', 'none') != 'none' and m.cfg.get('internet', True)
        try:
            if online:
                self.say(T('Taking the snapshot before %s goes online.' % m.name, 'Robię migawkę, zanim %s wyjdzie do sieci.' % m.name), interrupt=False)
                if 'before-online' in m.snapshots():
                    m.snapshot_delete('before-online')
                out = m.snapshot_save('before-online')
                if 'before-online' in m.snapshots():
                    self.say(T('Snapshot before going online saved. Left Control, Left Alt and Backspace, pressed twice, go back to it.',
                               'Migawka przed wyjściem do sieci zapisana. Lewy Control, lewy Alt i Backspace, naciśnięte dwa razy, wracają do niej.'), interrupt=False)
                else:
                    self.say(T('The snapshot before going online failed: %s' % (out.strip() or 'no reason given'),
                               'Migawka przed wyjściem do sieci się nie udała: %s' % (out.strip() or 'bez podanej przyczyny')), interrupt=False)
            else:
                self.say(T('%s runs without internet.' % m.name, '%s działa bez internetu.' % m.name), interrupt=False)
        except Exception as e:
            log('snapshot before online %s: %r' % (m.id, e))
            self.say(T('The snapshot before going online failed: %s' % e, 'Migawka przed wyjściem do sieci się nie udała: %s' % e), interrupt=False)
        finally:
            m.qmp.cmd('cont')

    def revert_online(self, m):
        T = self.T
        if 'before-online' not in m.snapshots():
            self.say(T('%s has no snapshot from before going online.' % m.name, '%s nie ma migawki sprzed wyjścia do sieci.' % m.name))
            return
        self.say(T('Going back to the snapshot before going online.', 'Wracam do migawki sprzed wyjścia do sieci.'))
        out = m.snapshot_revert('before-online')
        self.say(T('%s is back as it was before going online.' % m.name, '%s jest z powrotem taka, jak przed wyjściem do sieci.' % m.name)
                 if not out.strip() else T('Going back failed: %s' % out.strip(), 'Powrót się nie udał: %s' % out.strip()), interrupt=False)

    def secure_menu(self, m):
        T = self.T
        while True:
            on = bool(m.cfg.get('secure'))
            built = m.cfg.get('secure_built')
            items = []
            if not on:
                items.append((T('Switch the secure-use profile on', 'Włącz profil bezpiecznego używania'), 'on'))
            else:
                items.append((T('Prepare the secure-use set again (downloads, checks)' if built else 'Prepare the secure-use set (downloads, checks)',
                                'Przygotuj zestaw bezpiecznego używania ponownie (pobiera, sprawdza)' if built else 'Przygotuj zestaw bezpiecznego używania (pobiera, sprawdza)'), 'build'))
                if built and m.running():
                    items.append((T('Run the set in the guest now (it must show its desktop)', 'Uruchom zestaw w gościu teraz (musi pokazywać pulpit)'), 'run'))
                if built:
                    rep = m.cfg.get('secure_report', [])
                    items.append((T('What the set holds: %d parts, %d failed' % (len(rep), sum(1 for _, r in rep if r.startswith('failed'))),
                                    'Co zawiera zestaw: %d części, %d nieudanych' % (len(rep), sum(1 for _, r in rep if r.startswith('failed')))), 'report'))
                net = self.store.get('internet', m.id)
                items.append((T('Internet for this machine: %s (in Hypervisor settings for this machine)' % ('on' if net else 'off'),
                                'Internet dla tej maszyny: %s (w ustawieniach hipernadzorcy dla tej maszyny)' % ('włączony' if net else 'wyłączony')), 'net'))
                if m.running():
                    items.append((T('Go back to the snapshot before going online', 'Wróć do migawki sprzed wyjścia do sieci'), 'revert'))
                items.append((T('Switch the secure-use profile off', 'Wyłącz profil bezpiecznego używania'), 'off'))
            c = self.menu(T('Secure use', 'Bezpieczne używanie'), items)
            if c is None:
                return
            if c == 'on':
                self.say(self.secure_warning())
                self.voice.wait(90)
                m.cfg['secure'] = True
                m.cfg.setdefault('secure_groups', list(hvsecure.DEFAULT_GROUPS))
                m.save()
                self.say(T('On; it takes effect at the next start of %s.' % m.name, 'Włączony; działa od następnego uruchomienia %s.' % m.name))
            elif c == 'off':
                m.cfg['secure'] = False
                m.save()
                self.say(T('Off at the next start; %s then goes online unprotected.' % m.name, 'Wyłączony od następnego uruchomienia; %s wyjdzie wtedy do sieci bez ochrony.' % m.name))
            elif c == 'build':
                if self.prepare_secure(m) and m.running():
                    m.plug_secure_image()
            elif c == 'run':
                m.plug_secure_image()
                time.sleep(3)
                hvsecure.run_in_guest(m)
                self.say(T('Typed into the Run box of %s; its screen reader follows the set from here.' % m.name,
                           'Wpisano w okno Uruchom %s; dalej zestaw śledzi jej czytnik ekranu.' % m.name))
            elif c == 'report':
                self.say('; '.join('%s %s' % (i, r) for i, r in m.cfg.get('secure_report', [])))
            elif c == 'net':
                self.settings_group('network', m)       # 6ze: the switch lives in Hypervisor settings
            elif c == 'revert':
                self.revert_online(m)

    def stop(self, m, hard=False):
        if not m.running():
            self.say(self.T('%s is not running.' % m.name, '%s nie działa.' % m.name))
            return
        if hard:
            m.kill()
            return
        try:
            m.powerdown()
            self.say(self.T('Power button pressed in %s.' % m.name, 'Naciśnięto przycisk zasilania w %s.' % m.name))
        except Exception as e:
            self.say(self.T('Could not press the power button: %s' % e, 'Nie udało się nacisnąć przycisku zasilania: %s' % e))
            return
        threading.Thread(target=self.stop_fallback, args=(m,), daemon=True).start()

    def exchange_after_stop(self, m):
        """6q: the exchange drive of a stopped machine is copied into /hv/exchange/<id>, which ClamAV then scans."""
        import hvexchange
        time.sleep(1)       # QEMU has released the image
        try:
            n = hvexchange.sync_out(m.dir, m.exchange_dir())
        except Exception as e:
            log('exchange sync out %s: %r' % (m.id, e))
            self.say(self.T('The exchange drive of %s could not be read: %s' % (m.name, e),
                            'Nie udało się odczytać dysku wymiany %s: %s' % (m.name, e)), interrupt=False)
            return
        log('exchange sync out %s: %d files' % (m.id, n))
        if n and hvscan.clamd_running():
            self.say_findings(self.T('The exchange folder of %s' % m.name, 'Folder wymiany %s' % m.name), 'Folder wymiany %s' % m.name,
                              *hvscan.scan_tree(m.exchange_dir()))

    def stop_fallback(self, m, wait=45):
        """Windows ignores the ACPI power button in some states (XP while a program blocks it, a power policy of "Do
        nothing" / "Ask me", no ACPI HAL): when the machine still runs after WAIT seconds, type Windows' own shutdown
        command into its Run box (no /f: programs may still ask to save). 9x: the Run box cannot shut down quietly
        everywhere, so it is only said. Other systems: said, with the forced stop as the way out."""
        import hvsecure
        end = time.time() + wait
        while time.time() < end:
            if not m.running():
                return
            time.sleep(1)
        ver = hvsecure.version_of(m.cfg)
        if ver and ver not in hvsecure.NINE_X:
            self.say(self.T('%s did not react to the power button. Typing Windows\' shutdown command into its Run box.' % m.name,
                            '%s nie zareagował na przycisk zasilania. Wpisuję polecenie zamknięcia Windows w okno Uruchom.' % m.name))
            try:
                m.send_combo(['meta_l', 'r'])
                time.sleep(2)
                hvsecure.type_into(m, 'shutdown -s -t 0')
                m.send_combo(['ret'])
            except Exception as e:
                self.say(self.T('Could not type into %s: %s' % (m.name, e), 'Nie udało się pisać w %s: %s' % (m.name, e)))
                return
            end = time.time() + 90
            while time.time() < end:
                if not m.running():
                    return
                time.sleep(1)
        self.say(self.T('%s is still running. Shut it down from its own menu, or use "Turn it off at once".' % m.name,
                        '%s nadal działa. Zamknij go z jego własnego menu albo użyj „Wyłącz ją natychmiast”.' % m.name))

    # ---- the main menu ----------------------------------------------------------------------------------------
    def save_helpers(self, m):
        """The machine's proxy and share server, for a restarted menu (hvmachine state file)."""
        p = self.proxies.get(m.id)
        s = self.shares.get(m.id)
        hvmachine.save_state(m, proxy_pid=p[0].pid if p else 0, proxy_start=hvmachine.Adopted.start_time(p[0].pid) if p else None,
                             proxy_log=p[1] if p else '', proxy_port=getattr(m, 'proxy_port', 0),
                             mail_ports=getattr(m, 'mail_ports', {}), share_pid=s.pid if s else 0,
                             share_start=hvmachine.Adopted.start_time(s.pid) if s else None, share_port=getattr(m, 'share_port', 0))

    def reattach(self):
        """Session 9 (coordinator: a crash or restart of the menu must not take the guests down): every machine whose
        QEMU still runs is taken back with its proxy and share server; returns (taken back, ended). A QEMU that cannot be
        taken back (no state, QMP refused) still holds its disk: it is ended cleanly and named, as before."""
        back, ended = [], []
        for mid, m in self.machines.items():
            try:
                ok = m.attach(self.on_event)
            except Exception as e:
                log('attach %s: %r' % (mid, e))
                ok = False
            if not ok:
                continue
            st = hvmachine.load_state(m)
            if st.get('proxy_pid') and hvmachine.Adopted.start_time(st['proxy_pid']) == st.get('proxy_start'):
                logp = st.get('proxy_log', '')
                pos = os.path.getsize(logp) if logp and os.path.exists(logp) else 0
                self.proxies[mid] = (hvmachine.Adopted(st['proxy_pid']), logp, [pos])
                m.proxy_port, m.mail_ports = st.get('proxy_port', 0), st.get('mail_ports', {})
            if st.get('share_pid') and hvmachine.Adopted.start_time(st['share_pid']) == st.get('share_start'):
                self.shares[mid] = hvmachine.Adopted(st['share_pid'])
                m.share_port = st.get('share_port', 0)
            log('reattached %s: QEMU %d, proxy %s, share %s' % (mid, m.proc.pid, mid in self.proxies, mid in self.shares))
            back.append(m.name)
        return back, self.stop_orphans()

    def stop_orphans(self):
        """A machine still running from before the hypervisor restarted (its QEMU outlived the menu) holds its disk:
        it is ended (QEMU's own clean exit on SIGTERM) and named in the intro, never silently. r410: measured
        "Is another process using the image" after a restart of the service."""
        names = []
        for pid in os.listdir('/proc'):
            if not pid.isdigit():
                continue
            try:
                args = open('/proc/%s/cmdline' % pid, 'rb').read().split(b'\0')
            except OSError:
                continue
            # session 10, found live: five filtering proxies of earlier runs (1-2 hours old) were still running - a
            # proxy or share server that no taken-back machine owns is ended too
            helper = b' '.join(args[:2])
            if helper.endswith(b'hvproxy.py') or (args and args[0].endswith(b'smbd') and b'/run/charonhv/smb-' in b' '.join(args)):
                owned = [p.pid for p, _, _ in self.proxies.values()] + [s.pid for s in getattr(self, 'shares', {}).values()]
                try:
                    ppid = int(open('/proc/%s/stat' % pid).read().rsplit(')', 1)[1].split()[1])
                except (OSError, ValueError, IndexError):
                    ppid = 0
                if int(pid) not in owned and ppid not in owned:      # smbd's own workers belong to their parent
                    try:
                        os.kill(int(pid), 15)
                        log('orphan helper %s ended: %s' % (pid, b' '.join(args)[:120].decode('utf-8', 'replace')))
                    except OSError:
                        pass
                continue
            if not args or not os.path.basename(args[0].decode('utf-8', 'replace')).startswith('qemu-system'):
                continue
            line = b' '.join(args).decode('utf-8', 'replace')
            for mid, m in self.machines.items():
                if m.running() and m.proc.pid == int(pid):
                    continue                    # taken back by reattach()
                if '/hv/machines/%s/' % mid in line:
                    try:
                        os.kill(int(pid), 15)
                        for _ in range(50):
                            if not os.path.exists('/proc/%s' % pid):
                                break
                            time.sleep(0.1)
                        names.append(m.name)
                        log('orphan QEMU %s of %s ended' % (pid, mid))
                    except OSError as e:
                        log('orphan QEMU %s of %s: %r' % (pid, mid, e))
        return names

    def screen_first_start(self):
        time.sleep(20)                             # the network comes up in the background
        err = hvvoice.build_tvox()                 # TVox is there: builds only the screen model
        ok = hvvoice.screen_bin() is not None
        hvvoice.log('screen model at first start: %s' % ('ready' if ok else 'FAILED %s' % err))
        self.say(self.T("Charon's screen model is ready." if ok else "Charon's screen model could not be built: %s." % (err or 'see the log'),
                        'Model ekranu Charona jest gotowy.' if ok else 'Nie udało się zbudować modelu ekranu Charona: %s.' % (err or 'zobacz dziennik')),
                 interrupt=False)

    def tvox_first_start(self):
        """6m: build TVox once (hvvoice.build_tvox), then switch to it and say so; a failure is said and logged."""
        for _ in range(60):                        # the network comes up in the background (prepare.sh udhcpc)
            try:
                if any(l.split()[1] == '00000000' for l in open('/proc/net/route').read().splitlines()[1:] if len(l.split()) > 1):
                    break
            except OSError:
                pass
            time.sleep(1)
        err = hvvoice.build_tvox()
        if err is None and self.voice.use('tvox') is None:
            self.voice.failed = None
            hvvoice.log('TVox built at first start; the voice is TVox now')
            self.say(self.T('TVox is ready and speaks from now on. The build tools were removed.',
                            'TVox jest gotowy i od teraz mówi. Narzędzia budowania usunięto.'), interrupt=False)
        else:
            reason = err or 'the new program does not start'
            hvvoice.log('TVox build FAILED: %s' % reason)
            self.voice.failed = ('tvox', reason)
            self.say(self.T('TVox could not be built: %s. eSpeak NG keeps speaking.' % reason,
                            'Nie udało się zbudować TVox: %s. Nadal mówi eSpeak NG.' % reason), interrupt=False)

    def run(self):
        st = self.state
        self.clock_start()          # 6q / session-4 clock fault: before any machine gets its clock
        self.start_downloads()      # 6v: browser downloads -> scanned -> the running machine's Downloads share
        intro = [self.T('Charon Hypervisor.', 'Charon Hypervisor.')]
        if st.get('data') == 'missing':
            intro.append(self.T('No data partition was found; machines are kept in memory only and lost at power off.',
                                'Nie znaleziono partycji danych; maszyny są tylko w pamięci i znikną po wyłączeniu.'))
        elif st.get('formatted') == 'yes':
            intro.append(self.T('The data partition was prepared.', 'Przygotowano partycję danych.'))
        elif st.get('formatted') == 'failed' or st.get('data') == 'mountfailed':
            intro.append(self.T('The data partition could not be prepared; machines are kept in memory only.',
                                'Nie udało się przygotować partycji danych; maszyny są tylko w pamięci.'))
        elif st.get('data') == 'unknown':
            intro.append(self.T('The data partition holds no file system and is not the one named by this hypervisor; it was not touched.',
                                'Partycja danych nie ma systemu plików i nie jest tą wskazaną przez ten hipernadzorca; nie została zmieniona.'))
        if st.get('boot') == 'missing':
            intro.append(self.T('The boot area was not found: changes to the machine list do not reach Charon.',
                                'Nie znaleziono obszaru startowego: zmiany listy maszyn nie dotrą do Charona.'))
        if not self.kvm:
            intro.append(self.T('No hardware virtualisation: machines run emulated, slowly.',
                                'Brak wirtualizacji sprzętowej: maszyny działają emulowane, wolno.'))
        if self.orphans:
            intro.append(self.T('Still running from before the hypervisor restarted, and now stopped: %s.' % ', '.join(self.orphans),
                                'Działały jeszcze sprzed ponownego uruchomienia hipernadzorcy i zostały zatrzymane: %s.' % ', '.join(self.orphans)))
        tvox_first = (self.voice.failed and self.voice.failed[0] == 'tvox' and not hvvoice.tvox_bin()
                      and os.path.exists(hvvoice.TVOX_SRC))
        if tvox_first:
            intro.append(self.T('TVox, the voice Charon uses, is being built now from Charon\'s sources; eSpeak NG speaks until it is ready.',
                                'TVox, głos, którego używa Charon, jest teraz budowany ze źródeł Charona; do tego czasu mówi eSpeak NG.'))
            threading.Thread(target=self.tvox_first_start, daemon=True).start()
        elif not hvvoice.screen_bin() and os.path.exists(hvvoice.TVOX_SRC):
            # 6g: Charon's screen model for the guest reader, built once like TVox (a hypervisor from before it)
            intro.append(self.T("Charon's screen model for the guest reader is being built from Charon's sources.",
                                'Model ekranu Charona dla czytnika gości jest budowany ze źródeł Charona.'))
            threading.Thread(target=self.screen_first_start, daemon=True).start()
        if self.voice.failed and not tvox_first:
            want, err = self.voice.failed
            intro.append(self.T('The voice %s cannot speak here: %s. eSpeak NG speaks instead.' % (want, err),
                                'Głos %s nie może tu mówić: %s. Zamiast niego mówi eSpeak NG.' % (want, err)))
        if self.after_fault:
            intro.insert(0, self.T('The hypervisor menu stopped by a fault and started again.',
                                   'Menu hipernadzorcy zatrzymało się przez błąd i uruchomiło ponownie.'))
        if self.reattached:
            intro.append(self.T('Still running, taken back: %s; choose it in the main menu to give it the keyboard.' % ', '.join(self.reattached),
                                'Nadal działają, przejęte z powrotem: %s; wybierz w menu głównym, aby dać klawiaturę.' % ', '.join(self.reattached)))
        n = len(self.machines)
        intro.append(self.T('%d machines.' % n, 'Maszyny: %d.' % n))
        self.say(' '.join(intro))
        self.voice.wait(30)
        self.write_machines_txt()
        want = self.args.get('charonhv.machine')
        if want:
            m = self.machines.get(want)
            if m:
                self.start(m)
            else:
                self.say(self.T('Charon asked for the machine %s, which does not exist.' % want, 'Charon wybrał maszynę %s, której nie ma.' % want))
        elif not self.machines:
            self.wizard()
        at = 0
        while True:
            items = [(self.T('New virtual machine', 'Nowa maszyna wirtualna'), ('new',)),
                     (self.T('Modern browser on the real screen (DRM video)', 'Nowoczesna przeglądarka na prawdziwym ekranie (wideo z DRM)'), ('modern',))]
            for mid in hvmachine.ids():
                m = self.machines.get(mid)
                if m:
                    items.append(('%s, %s' % (m.name, self.T('running', 'działa') if m.running() else self.T('off', 'wyłączona')), ('machine', mid)))
            items += [(self.T('Import a machine or disk image', 'Importuj maszynę albo obraz dysku'), ('import',)),
                      (self.T('Hypervisor settings', 'Ustawienia hipernadzorcy'), ('settings',)),
                      (self.T('About this hypervisor', 'O tym hipernadzorcy'), ('about',)),
                      (self.T('Restart the computer (back to Charon)', 'Uruchom komputer ponownie (powrót do Charona)'), ('reboot',)),
                      (self.T('Turn the computer off', 'Wyłącz komputer'), ('poweroff',))]
            choice = self.menu(self.T('Charon Hypervisor, main menu', 'Charon Hypervisor, menu główne'), items, at)
            if choice is None:
                continue
            at = items.index(next(i for i in items if i[1] == choice))
            kind = choice[0]
            if kind == 'new':
                self.wizard()
            elif kind == 'modern':
                try:
                    self.modern_browser()
                except Exception as e:
                    log('modern browser: %r' % e)
                    self.say(self.T('The modern browser failed: %s' % e, 'Nowoczesna przeglądarka zawiodła: %s' % e))
            elif kind == 'machine':
                self.machine_menu(self.machines[choice[1]])
            elif kind == 'import':
                self.wizard(source='import')
            elif kind == 'settings':
                self.settings_menu()
            elif kind == 'about':
                self.say(self.T('Charon Hypervisor %s on Alpine Linux, QEMU %s. Hardware virtualisation %s. %d MB of memory free.' %
                                (VERSION, self.qemu_version(), 'on' if self.kvm else 'off', self.mem_available()),
                                'Charon Hypervisor %s na Alpine Linux, QEMU %s. Wirtualizacja sprzętowa %s. Wolna pamięć %d MB.' %
                                (VERSION, self.qemu_version(), 'włączona' if self.kvm else 'wyłączona', self.mem_available())))
            elif kind in ('reboot', 'poweroff'):
                if any(m.running() for m in self.machines.values()):
                    if not self.yes_no(self.T('Machines are running and will be turned off hard. Continue?',
                                              'Maszyny działają i zostaną wyłączone na twardo. Kontynuować?'), False):
                        continue
                for m in self.machines.values():
                    m.kill()
                self.say(self.T('Restarting.' if kind == 'reboot' else 'Turning off.', 'Uruchamiam ponownie.' if kind == 'reboot' else 'Wyłączam.'))
                self.voice.wait(10)
                subprocess.run(['reboot' if kind == 'reboot' else 'poweroff'])
                time.sleep(30)

    def qemu_version(self):
        try:
            out = subprocess.run(['qemu-system-x86_64', '--version'], capture_output=True, text=True, timeout=10).stdout
            return out.split()[3]
        except Exception:
            return '?'

    # ---- 6ze: Hypervisor settings, one place --------------------------------------------------------------------
    def settings_menu(self, m=None, over=None):
        """m: the machine whose exceptions are set (its "Settings for this machine"), else the general settings.
        The general menu also offers the running machine's exceptions when opened over a guest (host key + E)."""
        T = self.T
        mid = m.id if m else None
        at = 0
        while True:
            items = [(T(e, p), ('group', g)) for g, e, p, _ in hvsettings.GROUPS]
            if m is None:
                cur = self.machines.get(over or self.last_machine_id()) if (over or self.last_machine_id()) else None
                if cur:
                    items.append((T('Settings for %s' % cur.name, 'Ustawienia dla %s' % cur.name), ('machine', cur.id)))
                items.append((T("Back to Charon's settings", 'Powrót do ustawień Charona'), ('charon',)))
            title = T('Settings for %s' % m.name, 'Ustawienia dla %s' % m.name) if m else T('Hypervisor settings', 'Ustawienia hipernadzorcy')
            c = self.menu(title, items, at)
            if c is None:
                return
            at = [v for _, v in items].index(c)
            if c[0] == 'machine':
                self.settings_menu(self.machines[c[1]])
            elif c[0] == 'charon':
                if self.yes_no(T("Take Charon's voice and formatting settings again? The hypervisor's own settings stay.",
                                 'Przejąć ponownie ustawienia głosu i formatowania Charona? Własne ustawienia hipernadzorcy zostają.'), False):
                    self.store.back_to_charon()
                    self.say(T("Charon's settings again.", 'Znowu ustawienia Charona.'))
            else:
                self.settings_group(c[1], m)

    def last_machine_id(self):
        lm = getattr(self, 'last_machine', None)
        return lm.id if lm is not None and lm.running() else None

    def settings_group(self, group, m=None):
        T = self.T
        mid = m.id if m else None
        spec = next(i for g, _, _, i in hvsettings.GROUPS if g == group)
        if m is not None:
            spec = [i for i in spec if i.get('machine')]
            if not spec:
                self.say(T('This group has no settings for one machine.', 'Ta grupa nie ma ustawień dla jednej maszyny.'))
                return
        at = 0
        while True:
            items = []
            for i in spec:
                if i['kind'] == 'voice':
                    mine = [v for v in hvvoice.catalogue() if v['id'] == self.voice.voice_id]
                    items.append((T('Voice: %s' % (mine[0]['name'] if mine else T('as Charon', 'jak Charon')),
                                    'Głos: %s' % (mine[0]['name'] if mine else 'jak Charon')), i['key']))
                elif i['kind'] == 'reader_voice':
                    own = [v for v in hvvoice.catalogue() if v['id'] == self.store.get('reader_voice')]
                    items.append(('%s: %s' % (T(i['en'], i['pl']), own[0]['name'] if own else T('as the hypervisor', 'jak hipernadzorca')), i['key']))
                elif i['kind'] == 'linux_reader':
                    import hvmodern
                    rd = {'orca': 'Orca', 'linda': 'Linda'}.get(hvmodern.reader_choice(), T('not chosen yet', 'jeszcze nie wybrany'))
                    items.append(('%s: %s' % (T(i['en'], i['pl']), rd), i['key']))
                else:
                    items.append((self.store.label(i['key'], T, mid), i['key']))
            if group == 'reader' and m is None:
                items.append((T('Braille: not ported to the hypervisor yet', 'Brajl: jeszcze nie przeniesiony do hipernadzorcy'), 'braille'))
            name = next(T(e, p) for g, e, p, _ in hvsettings.GROUPS if g == group)
            c = self.menu(name, items, at)
            if c is None:
                return
            at = [v for _, v in items].index(c)
            if c == 'braille':
                self.say(T('Braille is not ported to the hypervisor yet; Charon itself has it.', 'Brajl nie jest jeszcze przeniesiony do hipernadzorcy; ma go sam Charon.'))
                continue
            i = hvsettings.SPEC[c]
            if i['kind'] == 'voice':
                self.choose_voice()
                continue
            if i['kind'] == 'reader_voice':
                vs = [v for v in hvvoice.catalogue() if v['available']]
                e = self.menu(T(i['en'], i['pl']), [(T('As the hypervisor', 'Jak hipernadzorca'), '')] + [(v['name'], v['id']) for v in vs])
                if e is not None:
                    self.store.set('reader_voice', e)
                    self.say(T('From the next start of the screen reader: ', 'Od następnego uruchomienia czytnika: ') +
                             (next(v['name'] for v in vs if v['id'] == e) if e else T('as the hypervisor', 'jak hipernadzorca')))
                continue
            if i['kind'] == 'linux_reader':
                r = self.choose_reader()
                if r == 'linda':
                    self.linda_prepare()
                continue
            if m is not None:
                # an exception: its own value, or back to the general one
                general = self.store.label(c, T)
                opts = [(T('Use the general setting (%s)' % general, 'Użyj ustawienia ogólnego (%s)' % general), 'general')]
                if i['kind'] == 'bool':
                    opts += [(T('On for this machine', 'Włączone dla tej maszyny'), True), (T('Off for this machine', 'Wyłączone dla tej maszyny'), False)]
                elif i['kind'] == 'choice':
                    # session 10 (sound delay): each choice as this machine's own value
                    opts += [(T('For this machine: %s' % e, 'Dla tej maszyny: %s' % p), ('choice', val)) for val, e, p in i['choices']]
                else:
                    opts += [(T('Own value for this machine', 'Własna wartość dla tej maszyny'), 'own')]
                v = self.menu(T(i['en'], i['pl']), opts)
                if v is None:
                    continue
                if v == 'general':
                    self.store.clear(c, mid)
                elif v == 'own':
                    n = self.ask_number(T(i['en'], i['pl']), self.store.get(c, mid), i['lo'], i['hi'])
                    if n is not None:
                        self.store.set(c, n, mid)
                elif isinstance(v, tuple):
                    self.store.set(c, v[1], mid)
                else:
                    self.store.set(c, v, mid)
                self.say(self.store.label(c, T, mid))
                continue
            if i['kind'] == 'bool':
                self.store.set(c, not self.store.get(c))
            elif i['kind'] == 'choice':
                v = self.menu(T(i['en'], i['pl']), [(T(e, p), val) for val, e, p in i['choices']],
                              [val for val, _, _ in i['choices']].index(self.store.get(c)) if self.store.get(c) in [x[0] for x in i['choices']] else 0)
                if v is None:
                    continue
                self.store.set(c, v)
            else:
                v = self.ask_number(T(i['en'], i['pl']), self.store.get(c), i['lo'], i['hi'])
                if v is None:
                    continue
                self.store.set(c, v)
            if c == 'duck':
                self.voice.duck = self.store.get('duck')
            elif c == 'guest_volume':
                self.voice.guest_volume = self.store.get('guest_volume')
                self.voice.set_guest_volume(self.voice.guest_volume)
            self.say(self.store.label(c, T))

    def choose_voice(self):
        names = {'tvox': 'TVox', 'rhvoice': 'RHVoice', 'espeak': 'eSpeak NG'}
        voices = hvvoice.catalogue()
        label = lambda v: '%s, %s, %s%s' % (v['name'], self.T({'pl': 'Polish', 'en': 'English'}.get(v['lang'], v['lang']),
                                                               {'pl': 'polski', 'en': 'angielski'}.get(v['lang'], v['lang'])),
                                           names.get(v['engine'], v['engine']),
                                           '' if v['available'] else self.T(', not available', ', niedostępny'))
        # r410 (user): ALL of Charon's voices, by name, from Charon's own files (hvvoice.catalogue); the default
        # stays "as Charon"; each voice says its own name when chosen
        charon = names.get(self.settings.synth, self.settings.synth)
        if self.settings.synth_voice:
            charon = '%s, %s' % (charon, next((v['name'] for v in voices if v['id'] == self.settings.synth_voice), self.settings.synth_voice))
        choices = [(self.T('As Charon: %s' % charon, 'Jak Charon: %s' % charon), 'charon')] + [(label(v), v['id']) for v in voices]
        log('voice list: %d voices: %s' % (len(voices), ', '.join(v['id'] for v in voices)))
        e = self.menu(self.T('Voice of the hypervisor, %d voices' % len(voices), 'Głos hipernadzorcy, %d głosów' % len(voices)), choices)
        if e is None:
            return
        entry = next((v for v in voices if v['id'] == e), None)
        err = self.voice.set_voice(entry)
        if err:
            self.say(self.T('This voice cannot speak: %s. The voice stays as it was.' % err,
                            'Ten głos nie może mówić: %s. Głos zostaje bez zmian.' % err))
        else:
            self.say(entry['name'] + '.' if entry else self.T('As Charon: %s.' % charon, 'Jak Charon: %s.' % charon))

    def sound_menu(self):
        while True:
            items = [(self.T('Guest volume while the voice speaks: %d percent' % self.voice.duck, 'Głośność gości, gdy mówi głos: %d procent' % self.voice.duck), 'duck'),
                     (self.T('Guest volume: %d percent' % self.voice.guest_volume, 'Głośność gości: %d procent' % self.voice.guest_volume), 'guest')]
            names = {'tvox': 'TVox', 'rhvoice': 'RHVoice', 'espeak': 'eSpeak NG'}
            voices = hvvoice.catalogue()
            label = lambda v: '%s, %s, %s%s' % (v['name'], self.T({'pl': 'Polish', 'en': 'English'}.get(v['lang'], v['lang']),
                                                                   {'pl': 'polski', 'en': 'angielski'}.get(v['lang'], v['lang'])),
                                               names.get(v['engine'], v['engine']),
                                               '' if v['available'] else self.T(', not available', ', niedostępny'))
            mine = [v for v in voices if v['id'] == self.voice.voice_id]
            now = label(mine[0]) if mine else names.get(self.voice.engine, self.voice.engine)
            how = self.T('as Charon', 'jak Charon') if not self.voice.chosen else self.T('chosen here', 'wybrany tutaj')
            items.insert(0, (self.T('Voice: %s, %s' % (now, how), 'Głos: %s, %s' % (now, how)), 'voice'))
            import hvmodern
            rd = {'orca': 'Orca', 'linda': 'Linda'}.get(hvmodern.reader_choice(), self.T('not chosen yet', 'jeszcze nie wybrany'))
            items.append((self.T('Screen reader of the graphical sessions: %s' % rd, 'Czytnik ekranu sesji graficznych: %s' % rd), 'reader'))
            c = self.menu(self.T('Sound', 'Dźwięk'), items)
            if c is None:
                return
            if c == 'reader':
                r = self.choose_reader()
                if r == 'linda':
                    self.linda_prepare()
                if r:
                    self.say({'orca': 'Orca.', 'linda': 'Linda.'}[r])
                continue
            if c == 'voice':
                self.choose_voice()
                continue
            if c == 'duck':
                v = self.ask_number(self.T('Guest volume while the voice speaks, in percent.', 'Głośność gości, gdy mówi głos, w procentach.'), self.voice.duck, 0, 100)
                if v is not None:
                    self.voice.duck = v
                    self.store.set('duck', v)
            else:
                v = self.ask_number(self.T('Guest volume in percent.', 'Głośność gości w procentach.'), self.voice.guest_volume, 0, 100)
                if v is not None:
                    self.voice.guest_volume = v
                    self.voice.set_guest_volume(v)
                    self.store.set('guest_volume', v)

    # ---- one machine ------------------------------------------------------------------------------------------
    def micropatch_menu(self, m):
        """6w prototype: hypervisor-side in-memory micropatches via QEMU's gdb stub. Each patch guards a known
        vulnerable function in the RUNNING guest's memory only (never on disk); off by default, per patch; a
        snapshot is taken first; verified against the exact build (a mismatch is NOT applied and is said aloud);
        re-applied at each start; a spoken list of the active ones. The network layer (Suricata, 6v-2) stays the
        MAIN protection. On this emulated host (TCG) a code write needs a translation-block flush (QMP stop/cont);
        under KVM there is no such cache."""
        import hvmicropatch
        T = self.T
        pats = hvmicropatch.patches_for(hvsecure.version_of(m.cfg))
        cfgp = os.path.join(m.dir, 'micropatch.json')
        try:
            enabled = json.load(open(cfgp, encoding='utf-8'))
        except (OSError, ValueError):
            enabled = {}
        at = 0
        while True:
            items = []
            for p in pats:
                on = bool(enabled.get(p['id']))
                items.append(('%s: %s' % (T(p['name'], p['name_pl']), T('on', 'wł.') if on else T('off', 'wył.')), ('toggle', p['id'])))
            items.append((T('Apply the enabled micropatches now', 'Nałóż teraz włączone mikrołaty'), 'apply'))
            items.append((T('List the active micropatches', 'Wypisz aktywne mikrołaty'), 'list'))
            c = self.menu(T('Security micropatches, a prototype: memory-only guards for known bugs, off by default, '
                            'a snapshot first, re-applied each start; the network layer stays the main protection.',
                            'Mikrołaty bezpieczeństwa, prototyp: osłony tylko w pamięci dla znanych błędów, domyślnie '
                            'wyłączone, najpierw migawka, nakładane przy każdym starcie; główną ochroną zostaje sieć.'), items, at)
            if c is None:
                return
            at = [v for _, v in items].index(c)
            if isinstance(c, tuple) and c[0] == 'toggle':
                enabled[c[1]] = not enabled.get(c[1])
                open(cfgp, 'w', encoding='utf-8').write(json.dumps(enabled))
                self.say(T('%s is now %s.' % (next(p['name'] for p in pats if p['id'] == c[1]), 'on' if enabled[c[1]] else 'off'),
                           '%s teraz %s.' % (next(p['name_pl'] for p in pats if p['id'] == c[1]), 'wł.' if enabled[c[1]] else 'wył.')))
            elif c == 'apply':
                self.apply_micropatches(m, enabled)
            elif c == 'list':
                eng = getattr(self, 'micropatch_engines', {}).get(m.id)
                self.say(eng.spoken_list() if eng else T('No micropatches are active.', 'Żadne mikrołaty nie są aktywne.'))

    def apply_micropatches(self, m, enabled=None):
        """Apply the machine's enabled micropatches over QEMU's gdb stub. Called from the menu and (re-apply) at
        start. Takes a snapshot first, starts the gdb stub on the machine's own QMP, and flushes TCG blocks with
        the machine's QMP stop/cont. A failure of any one patch is spoken; the others continue."""
        import hvmicropatch
        T = self.T
        if not m.running():
            self.say(T('Start %s first; micropatches work on the running guest.' % m.name,
                       'Najpierw uruchom %s; mikrołaty działają na działającym gościu.' % m.name))
            return
        if enabled is None:
            try:
                enabled = json.load(open(os.path.join(m.dir, 'micropatch.json'), encoding='utf-8'))
            except (OSError, ValueError):
                enabled = {}
        pats = [p for p in hvmicropatch.patches_for(hvsecure.version_of(m.cfg)) if enabled.get(p['id'])]
        if not pats:
            return
        try:
            m.qmp.hmp('savevm before-micropatch')      # snapshot first (6w)
        except Exception as e:
            log('micropatch snapshot: %r' % e)
        s = socket.socket()
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
        s.close()
        try:
            m.qmp.hmp('gdbserver tcp::%d' % port)
        except Exception as e:
            self.say(T('Micropatches: the gdb stub did not start: %s.' % e, 'Mikrołaty: debuger pamięci się nie uruchomił: %s.' % e))
            return
        try:
            gdb = hvmicropatch.GdbRsp('127.0.0.1', port, timeout=20)
        except Exception as e:
            self.say(T('Micropatches: could not reach the guest memory: %s.' % e, 'Mikrołaty: brak dostępu do pamięci gościa: %s.' % e))
            return
        eng = hvmicropatch.Micropatch(gdb, self.say, T, flush=lambda: (m.qmp.cmd('stop'), m.qmp.cmd('cont')))
        for p in pats:
            try:
                eng.apply(p, enabled)
            except Exception as e:
                self.say(T('Micropatch %s failed: %s.' % (p['name'], e), 'Mikrołata %s zawiodła: %s.' % (p['name'], e)))
        self.say(eng.spoken_list())
        gdb.close()
        if not hasattr(self, 'micropatch_engines'):
            self.micropatch_engines = {}
        self.micropatch_engines[m.id] = eng

    def compat_run(self, m):
        """6z/6za/6zg: run a Windows program with the compatibility layer. The layer decides the route from the
        program file itself (PE header, imports, known-program list) and says it once. XP/Vista programs the
        in-guest shims cannot run go through the hypervisor's Wine route with NVDA inside the prefix (6zg); on
        Windows 7/8/8.1 the program is a normal guest window read by the guest's own reader (VxKex in the guest)."""
        version = hvsecure.version_of(m.cfg)
        route = hvcompat.route_for(version)
        # candidate programs: the hypervisor side of this machine's exchange folder, and a drop folder
        dirs = [os.path.join('/hv/exchange', m.id), '/hv/compat/programs']
        exes = []
        for d in dirs:
            try:
                for f in sorted(os.listdir(d)):
                    if f.lower().endswith('.exe') and os.path.isfile(os.path.join(d, f)):
                        exes.append(os.path.join(d, f))
            except OSError:
                pass
        if not exes:
            self.say(self.T('No Windows program found. Put the .exe on the exchange drive of %s, or in the '
                            'hypervisor folder compat programs, then try again.' % m.name,
                            'Nie znalazłem programu Windows. Włóż plik .exe na dysk wymiany %s albo do folderu '
                            'hipernadzorcy compat programs i spróbuj ponownie.' % m.name))
            return
        pick = self.menu(self.T('Which program?', 'Który program?'),
                         [(os.path.basename(p), p) for p in exes])
        if pick is None:
            return
        name = os.path.basename(pick)
        try:
            with open(pick, 'rb') as f:
                data = f.read(2 << 20)
        except OSError as e:
            self.say(self.T('The program file could not be read: %s' % e, 'Nie udało się odczytać pliku: %s' % e))
            return
        d = hvcompat.decide(pick, version, m.dir, data=data)
        pl = self.T('en', 'pl') == 'pl'
        if not d['needs_newer']:
            if not self.yes_no(self.T('%s already runs on %s; the compatibility layer is not needed. Run it anyway?' % (name, m.name),
                                      '%s już działa na %s; warstwa zgodności nie jest potrzebna. Uruchomić mimo to?' % (name, m.name)), False):
                return
        elif d['off']:
            if self.yes_no(self.T('The compatibility layer is switched off for %s. Switch it on again?' % name,
                                  'Warstwa zgodności jest wyłączona dla %s. Włączyć ją ponownie?' % name), True):
                hvcompat.set_off(m.dir, pick, False)
                d = hvcompat.decide(pick, version, m.dir, data=data)
            else:
                return
        if d['reason_en']:
            log('compat %s: %s -> route %s reader %s' % (name, d['reason_en'], route, d['reader']))
        self.say(hvcompat.spoken_route(route, pl, name, reader=d['reader']))
        if route == 'hypervisor':
            self.say(self.T('Starting %s through Wine. It appears on the screen; the keyboard goes to it.' % name,
                            'Uruchamiam %s przez Wine. Pojawia się na ekranie; klawiatura przechodzi do niego.' % name),
                     interrupt=False)
            threading.Thread(target=self._compat_wine, args=(pick, name, d['reader']), daemon=True).start()
        else:
            # in-guest route (7/8/8.1): VxKex is installed in the guest by the secure-use set; the guest's own
            # launcher "run with Charon compatibility" enables it per program and the guest's reader reads it.
            self.say(self.T('On %s the program runs inside the guest with VxKex; use "run with Charon compatibility" '
                            'on the program in the guest, and the guest\'s own screen reader reads it.' % m.name,
                            'Na %s program działa w gościu z VxKex; użyj „uruchom z warstwą zgodności Charona” na '
                            'programie w gościu, a czyta go własny czytnik ekranu gościa.' % m.name))

    def _compat_wine(self, path, name, reader=None):
        err = hvcompat.run(path, self.say, self.T, log, name, reader=reader)
        if err:
            log('compat wine %s: %s' % (name, err[0]))
            self.say(err[1] if self.T('en', 'pl') == 'pl' else err[0])
        else:
            self.say(self.T('%s has closed.' % name, '%s został zamknięty.' % name))

    def machine_menu(self, m):
        at = 0
        while True:
            run = m.running()
            items = []
            if run:
                items += [(self.T('Give it the keyboard', 'Daj jej klawiaturę'), 'keyboard'),
                          (self.T('Read its screen', 'Czytaj jej ekran'), 'read'),
                          (self.T('Pause or resume', 'Wstrzymaj albo wznów'), 'pause'),
                          (self.T('Press its power button', 'Naciśnij jej przycisk zasilania'), 'stop'),
                          (self.T('Turn it off at once', 'Wyłącz ją natychmiast'), 'kill'),
                          (self.T('USB devices', 'Urządzenia USB'), 'usb'),
                          (self.T('Change the disc in its drive', 'Zmień płytę w jej napędzie'), 'disc')]
            else:
                items += [(self.T('Start', 'Uruchom'), 'start')]
            items.append((self.T('Hypervisor settings for this machine', 'Ustawienia hipernadzorcy dla tej maszyny'), 'hvsettings'))
            muted = bool(m.cfg.get('muted'))
            items += [(self.T('Its sound: %s' % ('muted' if muted else 'on'), 'Jej dźwięk: %s' % ('wyciszony' if muted else 'włączony')), 'mute'),
                      (self.T('Snapshots', 'Migawki'), 'snap')]
            if hvsecure.supported(m.cfg):
                items += [(self.T('Secure use', 'Bezpieczne używanie'), 'secure'),
                          (self.T('Retro assistant', 'Asystent retro'), 'retro')]
                if run and hvcompat.route_for(hvsecure.version_of(m.cfg)):
                    items += [(self.T('Run a Windows program with Charon compatibility', 'Uruchom program Windows z warstwą zgodności Charona'), 'compat')]
                import hvmicropatch
                if hvmicropatch.patches_for(hvsecure.version_of(m.cfg)):
                    items += [(self.T('Security micropatches (prototype)', 'Mikrołaty bezpieczeństwa (prototyp)'), 'micropatch')]
            if not run:
                items += [(self.T('Settings', 'Ustawienia'), 'settings'), (self.T('Clone', 'Sklonuj'), 'clone'),
                          (self.T('Delete this machine', 'Usuń tę maszynę'), 'delete')]
            c = self.menu(self.status_text(m), items, at)
            if c is None:
                return
            at = [v for _, v in items].index(c)
            try:
                if c == 'start':
                    self.start(m)
                elif c == 'keyboard':
                    self.give_keyboard(m)
                elif c == 'read':
                    self.read_guest(m, True)
                elif c == 'pause':
                    if m.qmp.cmd('query-status').get('status') == 'paused':
                        m.qmp.cmd('cont')
                    else:
                        m.qmp.cmd('stop')
                elif c == 'stop':
                    self.stop(m)
                elif c == 'kill':
                    if self.yes_no(self.T('Turn %s off at once? Unsaved work in it is lost.' % m.name, 'Wyłączyć %s natychmiast? Niezapisana praca w niej przepadnie.' % m.name), False):
                        self.stop(m, hard=True)
                elif c == 'mute':
                    ok = m.set_muted(not m.cfg.get('muted'))
                    if m.cfg.get('muted'):
                        self.say(self.T('%s muted: its sound is silence now, the voice still speaks.' % m.name,
                                        '%s wyciszona: jej dźwięk to teraz cisza, głos nadal mówi.' % m.name)
                                 if ok or not run else self.T('%s will be muted from its next start; the switch failed now.' % m.name,
                                                              '%s będzie wyciszona od następnego uruchomienia; przełącznik teraz zawiódł.' % m.name))
                    else:
                        self.say(self.T('%s sound on.' % m.name, 'Dźwięk %s włączony.' % m.name))
                    if not ok and run:
                        log('mute switch of %s failed (slot %s)' % (m.id, getattr(m, 'sound_slot', 0)))
                elif c == 'snap':
                    self.snapshot_menu(m)
                elif c == 'secure':
                    self.secure_menu(m)
                elif c == 'retro':
                    self.retro_menu(m)
                elif c == 'compat':
                    self.compat_run(m)
                elif c == 'micropatch':
                    self.micropatch_menu(m)
                elif c == 'usb':
                    self.usb_menu(m)
                elif c == 'hvsettings':
                    self.settings_menu(m)
                elif c == 'disc':
                    isos = hvmachine.find_isos()
                    pick = self.menu(self.T('Which disc?', 'Która płyta?'), [(self.T('no disc', 'bez płyty'), '')] + [(os.path.basename(p), p) for p in isos])
                    if pick is not None:
                        if pick:
                            m.qmp.hmp('change ide1-cd0 %s' % pick)
                        else:
                            m.qmp.hmp('eject -f ide1-cd0')
                        self.say(self.T('Done.', 'Gotowe.'))
                elif c == 'settings':
                    self.machine_settings_menu(m)
                elif c == 'clone':
                    name = self.ask_text(self.T('Name of the clone.', 'Nazwa klonu.'), m.name + ' 2')
                    if name:
                        self.say(self.T('Cloning %s, this takes a while.' % m.name, 'Klonuję %s, to chwilę potrwa.' % m.name))
                        mid = hvmachine.clone(m, name)
                        self.machines[mid] = hvmachine.Machine(mid)
                        self.write_machines_txt()
                        self.say(self.T('Clone %s made.' % name, 'Klon %s gotowy.' % name))
                elif c == 'delete':
                    w = self.ask_text(self.T('Type DELETE to delete %s and its disks.' % m.name, 'Wpisz USUŃ, aby usunąć %s i jej dyski.' % m.name))
                    if w and w.strip().upper() in ('DELETE', 'USUŃ', 'USUN'):
                        shutil.rmtree(m.dir)
                        del self.machines[m.id]
                        self.write_machines_txt()
                        self.say(self.T('%s deleted.' % m.name, '%s usunięta.' % m.name))
                        return
                    self.say(self.T('Nothing deleted.', 'Nic nie usunięto.'))
            except Exception as e:
                log('machine menu %s %s: %r' % (m.id, c, e))
                self.say(self.T('That failed: %s' % e, 'Nie udało się: %s' % e))

    def snapshot_menu(self, m):
        while True:
            tags = m.snapshots()
            items = [(self.T('Take a snapshot now', 'Zrób migawkę teraz'), ('save',))]
            items += [(self.T('Go back to %s' % t, 'Wróć do %s' % t), ('revert', t)) for t in tags]
            items += [(self.T('Delete %s' % t, 'Usuń %s' % t), ('delete', t)) for t in tags]
            c = self.menu(self.T('Snapshots of %s: %d' % (m.name, len(tags)), 'Migawki %s: %d' % (m.name, len(tags))), items)
            if c is None:
                return
            if c[0] == 'save':
                tag = time.strftime('s%Y%m%d-%H%M%S')
                self.say(self.T('Taking snapshot %s.' % tag, 'Robię migawkę %s.' % tag))
                out = m.snapshot_save(tag)
                ok = tag in m.snapshots()
                self.say(self.T('Snapshot %s saved.' % tag, 'Migawka %s zapisana.' % tag) if ok else self.T('The snapshot failed: %s' % out, 'Migawka się nie udała: %s' % out))
            elif c[0] == 'revert':
                out = m.snapshot_revert(c[1])
                self.say(self.T('%s is back at %s.' % (m.name, c[1]), '%s wrócił do %s.' % (m.name, c[1])) if not (out or '').strip() else
                         self.T('Going back failed: %s' % out, 'Powrót się nie udał: %s' % out))
            else:
                m.snapshot_delete(c[1])
                self.say(self.T('Deleted.', 'Usunięto.'))

    def usb_menu(self, m):
        devs = hvmachine.usb_devices()
        attached = m.cfg.setdefault('usb_attached', [])
        items = []
        for bus, addr, name in devs:
            key = '%d-%d' % (bus, addr)
            on = key in attached
            items.append(('%s, %s' % (name, self.T('given to the machine' if on else 'not given', 'przekazane maszynie' if on else 'nieprzekazane')), (key, bus, addr, name)))
        if not items:
            self.say(self.T('No USB device to give.', 'Brak urządzenia USB do przekazania.'))
            return
        c = self.menu(self.T('USB devices: Enter gives one to %s or takes it back.' % m.name, 'Urządzenia USB: Enter przekazuje je %s albo odbiera.' % m.name), items)
        if c is None:
            return
        key, bus, addr, name = c
        devid = 'usb' + key.replace('-', 'x')
        if key in attached:
            m.qmp.cmd('device_del', id=devid)
            attached.remove(key)
            self.say(self.T('%s taken back from %s.' % (name, m.name), '%s odebrane od %s.' % (name, m.name)))
        else:
            bus_ = getattr(m, 'usbbus', 'xhci0.0')
            m.qmp.cmd('device_add', driver='usb-host', hostbus=bus, hostaddr=addr, id=devid, bus=bus_)
            attached.append(key)
            self.say(self.T('%s given to %s.' % (name, m.name), '%s przekazane %s.' % (name, m.name)))

    def machine_settings_menu(self, m):
        """The machine's hardware (memory, processors, cards, shared folder ...) - not the hypervisor settings."""
        c = m.cfg
        while True:
            items = [(self.T('Memory: %d MB' % c['memory'], 'Pamięć: %d MB' % c['memory']), 'memory'),
                     (self.T('Processors: %d' % c['cpus'], 'Procesory: %d' % c['cpus']), 'cpus'),
                     (self.T('Sound card: %s' % c['sound'], 'Karta dźwiękowa: %s' % c['sound']), 'sound'),
                     (self.T('Network card: %s' % c['nic'], 'Karta sieciowa: %s' % c['nic']), 'nic'),
                     (self.T('Shared folder: %s' % ('yes' if c.get('shared') else 'no'), 'Folder wspólny: %s' % ('tak' if c.get('shared') else 'nie')), 'shared'),
                     (self.T('Screen reading by the hypervisor: %s' % ('yes' if c.get('reader') == 'hypervisor' else 'no'),
                             'Czytanie ekranu przez hipernadzorcę: %s' % ('tak' if c.get('reader') == 'hypervisor' else 'nie')), 'reader'),
                     (self.T('Entry in Charon\'s device list: %s' % ('yes' if c.get('charon_entry', True) else 'no'),
                             'Pozycja na liście urządzeń Charona: %s' % ('tak' if c.get('charon_entry', True) else 'nie')), 'entry'),
                     (self.T('Name: %s' % c['name'], 'Nazwa: %s' % c['name']), 'name')]
            k = self.menu(self.T('Settings of %s' % m.name, 'Ustawienia %s' % m.name), items)
            if k is None:
                m.save(); self.write_machines_txt()
                return
            if k == 'memory':
                v = self.ask_number(self.T('Memory in megabytes.', 'Pamięć w megabajtach.'), c['memory'], 16, 65536)
                if v: c['memory'] = v
            elif k == 'cpus':
                v = self.ask_number(self.T('Processors.', 'Procesory.'), c['cpus'], 1, max(1, os.cpu_count() or 1))
                if v: c['cpus'] = v
            elif k == 'sound':
                v = self.menu(self.T('Sound card', 'Karta dźwiękowa'), [(s, s) for s in hvmachine.SOUNDS], hvmachine.SOUNDS.index(c['sound']) if c['sound'] in hvmachine.SOUNDS else 0)
                if v: c['sound'] = v
            elif k == 'nic':
                v = self.menu(self.T('Network card', 'Karta sieciowa'), [(s, s) for s in hvmachine.NICS], hvmachine.NICS.index(c['nic']) if c['nic'] in hvmachine.NICS else 0)
                if v: c['nic'] = v
            elif k == 'shared':
                c['shared'] = not c.get('shared')
            elif k == 'reader':
                c['reader'] = 'guest' if c.get('reader') == 'hypervisor' else 'hypervisor'
            elif k == 'entry':
                c['charon_entry'] = not c.get('charon_entry', True)
            elif k == 'name':
                v = self.ask_text(self.T('Name.', 'Nazwa.'), c['name'])
                if v: c['name'] = v.strip()

    # ---- the virtual machine wizard (user points 6b, 6c) ------------------------------------------------------
    def wizard(self, source=None):
        T = self.T
        mode = self.menu(T('New virtual machine. Which mode?', 'Nowa maszyna wirtualna. Który tryb?'),
                         [(T('express: only the system and where it comes from', 'ekspresowy: tylko system i skąd pochodzi'), 'express'),
                          (T('detailed: every question', 'szczegółowy: wszystkie pytania'), 'detailed')])
        if mode is None:
            return
        if source == 'import':
            tid = 'other'
        else:
            tid = self.menu(T('Which system?', 'Jaki system?'), [(t['pl' if self.pl else 'en'], k) for k, t in hvmachine.TEMPLATES.items()])
            if tid is None:
                return
        t = hvmachine.TEMPLATES[tid]
        tname = t['pl' if self.pl else 'en']
        sources = []
        if source != 'import':
            sources.append((T('your own installation disc (ISO file)', 'twoja płyta instalacyjna (plik ISO)'), 'iso'))
            if tid in CATALOGUE:
                cat = CATALOGUE[tid]
                sources.append((T('download: %s' % cat['en'], 'pobierz: %s' % cat['pl']), 'download'))
        sources.append((T('import a machine or disk image (vmx, ova, ovf, vmdk, vdi, vhd, vhdx, qcow2)',
                          'import maszyny albo obrazu dysku (vmx, ova, ovf, vmdk, vdi, vhd, vhdx, qcow2)'), 'import'))
        sources.append((T('an empty disk, no installation medium', 'pusty dysk, bez nośnika instalacyjnego'), 'empty'))
        src = self.menu(T('Where does %s come from?' % tname, 'Skąd pochodzi %s?' % tname), sources, 0)
        if src is None:
            return
        plan = dict(name=tname, memory=t['memory'], cpus=t['cpus'], disk=t['disk'], sound=t['sound'], nic=t['nic'], reader=t['reader'],
                    unattended=bool(t['unattended']), shared=False, usb=True, snapshot=False, charon_entry=True,
                    firmware=t['firmware'], bus=t['bus'], machine=t['machine'])
        if hvsecure.supported({'template': tid}):
            # 6k: on by default for the old Windows versions, every part, no extra question in express mode
            plan['secure'] = True
            plan['secure_groups'] = list(hvsecure.DEFAULT_GROUPS)
        defaults = dict(plan)
        iso = imported = None
        if src == 'iso':
            isos = hvmachine.find_isos()
            if not isos:
                self.say(T('No ISO file was found in the images folder of the data partition or on a connected disk or stick.',
                           'Nie znaleziono pliku ISO w folderze images partycji danych ani na podłączonym dysku lub pendrivie.'))
                return
            iso = self.menu(T('Which disc? %d found.' % len(isos), 'Która płyta? Znalezione: %d.' % len(isos)), [(self.iso_label(p), p) for p in isos])
            if iso is None:
                return
        elif src == 'import':
            files = hvmachine.find_importable()
            if not files:
                self.say(T('No machine or disk image was found.', 'Nie znaleziono maszyny ani obrazu dysku.'))
                return
            f = self.menu(T('Which file? %d found.' % len(files), 'Który plik? Znalezione: %d.' % len(files)), [(os.path.basename(p) + ', ' + os.path.dirname(p), p) for p in files])
            if f is None:
                return
            try:
                imported = hvmachine.import_machine(f, '/hv/images/.import-%d' % int(time.time()))
            except Exception as e:
                self.say(T('That file cannot be imported: %s' % e, 'Tego pliku nie da się zaimportować: %s' % e))
                return
            for k in ('name', 'memory', 'cpus', 'nic', 'sound', 'firmware', 'bus'):
                if imported.get(k):
                    plan[k] = imported[k]
            defaults = dict(plan)
            self.say(T('Read from the file: %s, %d MB memory, %d processors, %d disks, network card %s, sound card %s.' %
                       (plan['name'], plan['memory'], plan['cpus'], len(imported['disks']), plan['nic'], plan['sound']),
                       'Odczytano z pliku: %s, pamięć %d MB, procesory %d, dyski %d, karta sieciowa %s, karta dźwiękowa %s.' %
                       (plan['name'], plan['memory'], plan['cpus'], len(imported['disks']), plan['nic'], plan['sound'])))
            self.voice.wait(30)
        if mode == 'detailed':
            if not self.detailed_questions(plan, t, src, imported):
                return
        # summary: only what differs from the template
        diffs = []
        names = dict(name=T('name', 'nazwa'), memory=T('memory', 'pamięć'), cpus=T('processors', 'procesory'), disk=T('disk size in gigabytes', 'rozmiar dysku w gigabajtach'),
                     sound=T('sound card', 'karta dźwiękowa'), nic=T('network card', 'karta sieciowa'), reader=T('screen reader', 'czytnik ekranu'),
                     unattended=T('installation without questions', 'instalacja bez pytań'), shared=T('shared folder', 'folder wspólny'),
                     usb=T('USB pass-through', 'przekazywanie USB'), snapshot=T('snapshot after the installation', 'migawka po instalacji'),
                     charon_entry=T("entry in Charon's device list", 'pozycja na liście urządzeń Charona'),
                     secure=T('secure-use profile', 'profil bezpiecznego używania'))
        if plan.get('secure') and mode != 'detailed':
            self.say(self.secure_warning())
            self.voice.wait(90)
        if plan.get('secure') and plan.get('secure_groups') != defaults.get('secure_groups'):
            diffs.append(T('secure-use parts: %s' % ', '.join(en for g, en, _ in hvsecure.GROUPS if g in plan['secure_groups']),
                           'części bezpiecznego używania: %s' % ', '.join(pl for g, _, pl in hvsecure.GROUPS if g in plan['secure_groups'])))
        def val(v):
            if v is True: return T('yes', 'tak')
            if v is False: return T('no', 'nie')
            return str(v)
        for k, label in names.items():
            if plan.get(k) != defaults.get(k):
                diffs.append(T('%s %s instead of %s' % (label, val(plan[k]), val(defaults[k])), '%s %s zamiast %s' % (label, val(plan[k]), val(defaults[k]))))
        what = {'iso': T('from %s' % self.iso_label(iso or ''), 'z: %s' % self.iso_label(iso or '')),
                'download': T('downloaded from its official server', 'pobrany z oficjalnego serwera'),
                'import': T('imported', 'zaimportowany'), 'empty': T('with an empty disk', 'z pustym dyskiem')}[src]
        summary = T('Summary: %s, %s. ' % (tname, what), 'Podsumowanie: %s, %s. ' % (tname, what))
        if plan.get('product_key'):                 # said as given, never the key itself
            diffs.append(T('product key given', 'klucz produktu podany'))
        summary += (T('Different from the template: ', 'Inaczej niż w szablonie: ') + '; '.join(diffs) + '.') if diffs else T('Everything as in the template.', 'Wszystko jak w szablonie.')
        self.say(summary)
        self.voice.wait(120)
        word = self.ask_text(T('Type YES and press Enter to create the disk image of the machine; Escape cancels.',
                               'Wpisz TAK i naciśnij Enter, aby utworzyć obraz dysku maszyny; Escape anuluje.'))
        if not word or word.strip().upper() not in ('YES', 'TAK'):
            self.say(T('Nothing was created.', 'Nic nie utworzono.'))
            return
        before = set(hvmachine.ids())
        try:
            m = self.create_machine(tid, plan, src, iso, imported)
        except Exception as e:
            log('wizard create: %r' % e)
            for mid in set(hvmachine.ids()) - before:      # nothing half-made stays behind
                shutil.rmtree(os.path.join(hvmachine.MACHINES, mid), ignore_errors=True)
            self.say(T('The machine could not be created: %s' % e, 'Nie udało się utworzyć maszyny: %s' % e))
            return
        self.machines[m.id] = m
        self.write_machines_txt()
        self.say(T('Machine %s created.' % m.name, 'Maszyna %s utworzona.' % m.name))
        self.voice.wait(10)
        if src != 'empty' or True:
            self.start(m)

    def iso_label(self, p):
        if '|' in p:
            dev, label = p.split('|', 1)
            return self.T('the disc in %s, %s' % (os.path.basename(dev), label or 'no label'), 'płyta w %s, %s' % (os.path.basename(dev), label or 'bez etykiety'))
        return os.path.basename(p) + ', ' + os.path.dirname(p)

    def detailed_questions(self, plan, t, src, imported):
        T = self.T
        v = self.ask_text(T('Name of the machine.', 'Nazwa maszyny.'), plan['name'])
        if v is None: return False
        plan['name'] = v.strip() or plan['name']
        v = self.ask_number(T('Memory in megabytes.', 'Pamięć w megabajtach.'), plan['memory'], 16, max(64, self.mem_available()))
        if v is None: return False
        plan['memory'] = v
        v = self.ask_number(T('Processors.', 'Procesory.'), plan['cpus'], 1, max(1, os.cpu_count() or 1))
        if v is None: return False
        plan['cpus'] = v
        if src != 'import':
            v = self.ask_number(T('Disk size in gigabytes.', 'Rozmiar dysku w gigabajtach.'), plan['disk'], 1, 2048)
            if v is None: return False
            plan['disk'] = v
        v = self.menu(T('Sound card', 'Karta dźwiękowa'), [(s, s) for s in hvmachine.SOUNDS], hvmachine.SOUNDS.index(plan['sound']) if plan['sound'] in hvmachine.SOUNDS else 0)
        if v is None: return False
        plan['sound'] = v
        v = self.menu(T('Network card', 'Karta sieciowa'), [(s, s) for s in hvmachine.NICS], hvmachine.NICS.index(plan['nic']) if plan['nic'] in hvmachine.NICS else 0)
        if v is None: return False
        plan['nic'] = v
        rn = dict(hypervisor=T('the hypervisor reads the screen', 'hipernadzorca czyta ekran'), nvda2017=T('NVDA 2017.3 in the guest', 'NVDA 2017.3 w gościu'),
                  nvda=T('NVDA in the guest', 'NVDA w gościu'), narrator=T('Narrator of Windows', 'Narrator Windows'),
                  speakup=T('Speakup in the guest', 'Speakup w gościu'), orca=T('Orca in the guest', 'Orca w gościu'))
        v = self.menu(T('Screen reader', 'Czytnik ekranu'), [(rn.get(r, r), r) for r in t['readers']], t['readers'].index(plan['reader']) if plan['reader'] in t['readers'] else 0)
        if v is None: return False
        plan['reader'] = v
        if t['unattended'] and src == 'iso':
            v = self.yes_no(T('Install without questions where the system allows it?', 'Instalować bez pytań, gdzie system na to pozwala?'), plan['unattended'])
            if v is None: return False
            plan['unattended'] = v
        if t['unattended'] == 'winnt' and src == 'iso' and plan['unattended']:
            # 6h: OPTIONAL product key - typed, spoken back only as it is typed, never logged or shown; written only
            # into this machine's own answer file (WINNT.SIF on its floppy folder on the data partition)
            while True:
                v = self.ask_text(T('Product key, optional: type the 25 characters, dashes may be left out; Enter without '
                                    'a key leaves it out, and Setup then asks for it and the reader says so.',
                                    'Klucz produktu, opcjonalnie: wpisz 25 znaków, myślniki można pominąć; Enter bez klucza '
                                    'go pomija, wtedy Instalator o niego zapyta, a czytnik to powie.'), '', private=True)
                if v is None: return False
                key = re.sub(r'[^0-9A-Za-z]', '', v).upper()
                if not key:
                    plan.pop('product_key', None)
                    break
                if len(key) == 25:
                    plan['product_key'] = '-'.join(key[i:i + 5] for i in range(0, 25, 5))
                    self.say(T('Product key taken, 5 groups of 5 characters.', 'Klucz produktu przyjęty, 5 grup po 5 znaków.'))
                    break
                self.say(T('That is %d characters, a product key has 25. Type it again.' % len(key),
                           'To jest %d znaków, klucz produktu ma 25. Wpisz go ponownie.' % len(key)))
        for key, q_en, q_pl in (('shared', 'A shared folder with the data partition?', 'Folder wspólny z partycją danych?'),
                                ('usb', 'USB pass-through for sticks and disks?', 'Przekazywanie USB pendrive\'ów i dysków?'),
                                ('snapshot', 'A snapshot when the installation is finished?', 'Migawka po zakończeniu instalacji?'),
                                ('charon_entry', "An entry of its own in Charon's device list?", 'Własna pozycja na liście urządzeń Charona?')):
            v = self.yes_no(T(q_en, q_pl), plan[key])
            if v is None: return False
            plan[key] = v
        if 'secure' in plan:
            self.say(self.secure_warning())
            self.voice.wait(60)
            v = self.yes_no(T('The secure-use profile, with all its parts?', 'Profil bezpiecznego używania, ze wszystkimi częściami?'), plan['secure'])
            if v is None: return False
            plan['secure'] = v
            if v and self.yes_no(T('Choose its parts one by one?', 'Wybrać jego części po kolei?'), False):
                chosen = []
                for g, en, pl in hvsecure.GROUPS:
                    v = self.yes_no(T('Part: %s?' % en, 'Część: %s?' % pl), g in plan['secure_groups'])
                    if v is None: return False
                    if v:
                        chosen.append(g)
                plan['secure_groups'] = chosen
        return True

    def secure_warning(self):
        return self.T('This Windows no longer gets support from Microsoft and stays unsafe on the internet even when protected. '
                      'The secure-use profile fetches every update Microsoft still offers for it through Legacy Update, an unofficial '
                      'project, current certificates, a maintained browser, runtimes for programs and games, switches off exposed services, '
                      'and around the machine the hypervisor allows only web traffic through its own filtering proxy, takes a snapshot '
                      'before it goes online and reverts to it with Left Control, Left Alt and Backspace pressed twice. Nothing bypasses a licence.',
                      'Ten Windows nie ma już wsparcia Microsoftu i nawet chroniony pozostaje niebezpieczny w internecie. '
                      'Profil bezpiecznego używania pobiera każdą aktualizację, którą Microsoft jeszcze dla niego udostępnia, przez Legacy Update, '
                      'nieoficjalny projekt, aktualne certyfikaty, utrzymywaną przeglądarkę, biblioteki dla programów i gier, wyłącza wystawione usługi, '
                      'a wokół maszyny hipernadzorca przepuszcza tylko ruch WWW przez własnego pośrednika z filtrem, robi migawkę przed '
                      'wyjściem do sieci i wraca do niej po dwukrotnym naciśnięciu lewego Controla, lewego Alta i Backspace. Nic nie obchodzi licencji.')

    def download(self, tid):
        T = self.T
        cat = CATALOGUE[tid]
        url = cat.get('url')
        if not url:
            listing = urllib.request.urlopen(cat['list'], timeout=60).read().decode('utf-8', 'replace')
            m = re.findall(cat['pattern'], listing)
            if not m:
                raise RuntimeError('not found on ' + cat['list'])
            url = cat['list'] + sorted(set(m))[-1]
        name = os.path.basename(url.split('?')[0].replace('/download', ''))
        dest = os.path.join('/hv/images', name)
        if not os.path.exists(dest):
            self.say(T('Downloading %s.' % name, 'Pobieram %s.' % name))
            req = urllib.request.Request(url, headers={'User-Agent': 'charon-hypervisor'})
            with urllib.request.urlopen(req, timeout=120) as r, open(dest + '.part', 'wb') as f:
                total = int(r.headers.get('Content-Length') or 0)
                got, said = 0, 0
                while True:
                    b = r.read(1 << 20)
                    if not b:
                        break
                    f.write(b); got += len(b)
                    if total and got * 10 // total > said:
                        said = got * 10 // total
                        self.say(T('%d percent' % (said * 10), '%d procent' % (said * 10)), interrupt=False)
                    self.background()
            os.replace(dest + '.part', dest)
            log('downloaded %s %d bytes' % (url, os.path.getsize(dest)))
        if cat.get('inner') and dest.lower().endswith('.zip') or name.lower().endswith('.zip'):
            import zipfile
            with zipfile.ZipFile(dest) as z:
                inner = [n for n in z.namelist() if n.lower().endswith(cat.get('inner', '.iso'))]
                if not inner:
                    raise RuntimeError('no ISO inside ' + name)
                out = os.path.join('/hv/images', os.path.basename(inner[0]))
                if not os.path.exists(out):
                    with z.open(inner[0]) as s, open(out, 'wb') as o:
                        shutil.copyfileobj(s, o, 1 << 20)
                return out
        return dest

    NVDA2017 = ('https://download.nvaccess.org/releases/2017.3/nvda_2017.3.exe', 21216304,
                'a0ad10a9f33bc5bcde629524e873c3141976f1698915d38640c46f2c140c3972')
    NVDA2017_CMD = ['@echo off', 'rem Charon Hypervisor: NVDA 2017.3 for Windows XP', 'echo %DATE% %TIME% start > C:\\charon-nvda.log', '"%~dp0NVDA2017.EXE" --install-silent --enable-start-on-logon=True >> C:\\charon-nvda.log 2>&1', 'echo exit %ERRORLEVEL% >> C:\\charon-nvda.log', 'if exist "%ProgramFiles%\\NVDA\\nvda.exe" start "" "%ProgramFiles%\\NVDA\\nvda.exe"', 'echo %DATE% %TIME% started >> C:\\charon-nvda.log', '']

    def prepare_nvda2017(self, m):
        """NVDA 2017.3, the last NVDA for Windows XP, from NV Access's server (size and SHA-256 as Charon r405 recorded them:
        NV Access publishes no hash for that release), on a read-only USB drive with INSTALL.CMD, which Windows Setup runs at
        the first logon ([GuiRunOnce]): silent installation, start at logon, then NVDA is started."""
        import hashlib
        T = self.T
        url, size, sha = self.NVDA2017
        cache = '/hv/images/nvda_2017.3.exe'
        if not (os.path.exists(cache) and os.path.getsize(cache) == size):
            self.say(T('Downloading NVDA 2017.3 from NV Access.', 'Pobieram NVDA 2017.3 od NV Access.'))
            req = urllib.request.Request(url, headers={'User-Agent': 'charon-hypervisor'})
            with urllib.request.urlopen(req, timeout=120) as r, open(cache + '.part', 'wb') as f:
                shutil.copyfileobj(r, f, 1 << 20)
            os.replace(cache + '.part', cache)
        h = hashlib.sha256(open(cache, 'rb').read()).hexdigest()
        if h != sha:
            os.remove(cache)
            raise RuntimeError(T('NVDA 2017.3 does not have the expected SHA-256 (%s)' % h, 'NVDA 2017.3 ma inną sumę SHA-256 niż oczekiwana (%s)' % h))
        setup = os.path.join(m.dir, 'setup', 'CHARON')
        os.makedirs(setup, exist_ok=True)
        shutil.copy(cache, os.path.join(setup, 'NVDA2017.EXE'))
        with open(os.path.join(setup, 'INSTALL.CMD'), 'w', encoding='cp1250', newline='\r\n') as f:
            f.write('\n'.join(self.NVDA2017_CMD))
        m.cfg['installing'] = True
        log('NVDA 2017.3 prepared for %s (sha256 %s)' % (m.id, h))

    def create_machine(self, tid, plan, src, iso, imported):
        T = self.T
        extra = {k: plan[k] for k in ('memory', 'cpus', 'sound', 'nic', 'reader', 'shared', 'usb', 'charon_entry', 'firmware', 'bus', 'machine')}
        extra['snapshot_after_install'] = plan['snapshot']
        if plan.get('secure'):
            extra['secure'] = True
            extra['secure_groups'] = plan.get('secure_groups') or list(hvsecure.DEFAULT_GROUPS)
        mid = hvmachine.create(plan['name'], tid, extra)
        m = hvmachine.Machine(mid)
        if src == 'download':
            iso = self.download(tid)
        if src == 'import':
            disks = []
            for n, d in enumerate(imported['disks']):
                out = os.path.join(m.dir, 'disk%d.qcow2' % n)
                self.say(T('Converting disk %d of %d.' % (n + 1, len(imported['disks'])), 'Przekształcam dysk %d z %d.' % (n + 1, len(imported['disks']))))
                hvmachine.convert(d, out)
                disks.append('disk%d.qcow2' % n)
            m.cfg['disks'] = disks
            m.cfg['imported_from'] = imported.get('guest', '')
        else:
            ok, err = hvmachine.make_disk(os.path.join(m.dir, 'disk0.qcow2'), plan['disk'])
            if not ok:
                shutil.rmtree(m.dir, ignore_errors=True)
                raise RuntimeError(err)
            m.cfg['disks'] = ['disk0.qcow2']
        if iso:
            if not iso.startswith('/hv/'):
                self.say(T('Copying the disc to the data partition.', 'Kopiuję płytę na partycję danych.'))
                if '|' in iso:
                    dev, label = iso.split('|', 1)
                    src_path = dev
                    dst = os.path.join('/hv/images', (re.sub(r'[^A-Za-z0-9_.-]', '_', label) or os.path.basename(dev)) + '.iso')
                else:
                    src_path = iso
                    dst = os.path.join('/hv/images', os.path.basename(iso))
                if not os.path.exists(dst):
                    with open(src_path, 'rb') as s, open(dst + '.part', 'wb') as o:
                        shutil.copyfileobj(s, o, 4 << 20)
                    os.replace(dst + '.part', dst)
                log('disc copied %s -> %s %d bytes' % (src_path, dst, os.path.getsize(dst)))
                iso = dst
            m.cfg['cdrom'] = iso
            m.cfg['boot'] = 'cd'
            if plan['unattended'] and hvmachine.TEMPLATES[tid]['unattended'] == 'winnt':
                fl = os.path.join(m.dir, 'floppy')
                os.makedirs(fl, exist_ok=True)
                open(os.path.join(fl, 'WINNT.SIF'), 'w', encoding='cp1250', newline='\r\n').write(winnt_sif(plan, self.pl))
                m.cfg['floppy'] = 'floppy'
                m.cfg['installing'] = True
                m.cfg['setup_phase'] = True     # 6h: the hypervisor reads Setup (key page) until the guest's reader runs
            if plan['reader'] == 'nvda2017':
                self.prepare_nvda2017(m)
        if m.cfg.get('secure'):
            m.save()
            self.prepare_secure(m)      # failures inside are spoken per part; the machine is made anyway
        m.save()
        return m

    def prepare_secure(self, m):
        """6k: fetch, verify and stage the machine's secure-use set onto its read-only CHARONSEC drive."""
        T = self.T
        try:
            ca = hvproxy.make_ca(hvsecure.SECURE)
            if 'protections' in m.cfg.get('secure_groups', ['protections']):
                hvsecure.update_blocklist(self.say, T)
            work = os.path.join(m.dir, 'secure-set')
            sec, report = hvsecure.build(m.cfg, work, self.say, T, self.background, m.cfg.get('secure_groups'), ca)
            self.say(T('Writing the CHARONSEC drive.', 'Zapisuję dysk CHARONSEC.'), interrupt=False)
            hvsecure.make_image(sec, os.path.join(m.dir, 'secure.img'), 16 if hvsecure.version_of(m.cfg) in hvsecure.NINE_X else 32)
            shutil.rmtree(work, ignore_errors=True)
            failed = [i for i, r in report if r.startswith('failed')]
            m.cfg['secure_report'] = report
            m.cfg['secure_built'] = time.strftime('%Y-%m-%d %H:%M')
            m.save()
            log('secure set for %s: %s' % (m.id, json.dumps(report)))
            self.say(T('The secure-use set for %s is ready%s.' % (m.name, ', %d parts failed' % len(failed) if failed else ''),
                       'Zestaw bezpiecznego używania dla %s jest gotowy%s.' % (m.name, ', %d części się nie udało' % len(failed) if failed else '')), interrupt=False)
            return True
        except Exception as e:
            log('secure set for %s failed: %r' % (m.id, e))
            self.say(T('The secure-use set could not be made: %s' % e, 'Nie udało się zrobić zestawu bezpiecznego używania: %s' % e))
            return False


def update_status_text(name, what, T):
    """The sentence for an UPDATES line of the proxy (JSON from hvproxy.update_report); None when unreadable."""
    try:
        r = json.loads(what)
    except ValueError:
        return None
    if r.get('error'):
        return T('%s: Legacy Update could not check for updates (error %s); it tries again within an hour.' % (name, r['error']),
                 '%s: Legacy Update nie mógł sprawdzić aktualizacji (błąd %s); spróbuje ponownie w ciągu godziny.' % (name, r['error']))
    n = r.get('pending')
    if n is None:
        return None
    if n == 0:
        en, pl = '%s is up to date through Legacy Update.' % name, '%s jest aktualny dzięki Legacy Update.' % name
    else:
        first = r.get('titles') or []
        en = '%s: %d %s waiting in Legacy Update%s; Windows installs %s from its Automatic Updates icon.' % (
            name, n, 'update' if n == 1 else 'updates', (', first: ' + first[0]) if first else '', 'it' if n == 1 else 'them')
        pl = '%s: aktualizacje czekające w Legacy Update: %d%s; Windows instaluje je z ikony Aktualizacji automatycznych.' % (
            name, n, (', pierwsza: ' + first[0]) if first else '')
    if r.get('last'):
        en += ' Last installed %s.' % r['last']
        pl += ' Ostatnio zainstalowano %s.' % r['last']
    return T(en, pl)


def winnt_sif(plan, pl):
    """Windows XP Setup answers (Microsoft's ref.chm / unattend.txt keys). ProductKey only when the user typed one in
    the wizard (6h); without it Setup stops at its key page and the guest reader announces it."""
    name = re.sub(r'[^A-Za-z0-9-]', '', plan['name'].upper().replace(' ', '-'))[:15] or 'CHARON-XP'
    key = plan.get('product_key')
    return '\n'.join([
        '[Data]', 'AutoPartition=1', 'MsDosInitiated="0"', 'UnattendedInstall="Yes"', '',
        '[Unattended]', 'UnattendMode=FullUnattended', 'OemSkipEula=Yes', 'OemPreinstall=No', 'TargetPath=\\WINDOWS',
        'Repartition=Yes', 'UnattendSwitch="Yes"', 'WaitForReboot="No"', 'DriverSigningPolicy=Ignore', '',
        '[GuiUnattended]', 'AdminPassword="*"', 'EncryptedAdminPassword=NO', 'OEMSkipRegional=1', 'TimeZone=95', 'OemSkipWelcome=1',
        'AutoLogon=Yes', 'AutoLogonCount=1', '',
        '[UserData]', 'FullName="Charon"', 'OrgName=""', 'ComputerName=%s' % name] + (['ProductKey="%s"' % key] if key else []) + ['',
        '[Display]', 'BitsPerPel=16', 'Xresolution=800', 'YResolution=600', '',
        '[Identification]', 'JoinWorkgroup=WORKGROUP', '',
        '[Networking]', 'InstallDefaultComponents=Yes', '',
        '[GuiRunOnce]', '"cmd /c for %d in (D E F G H I J K L M) do if exist %d:\\CHARON\\INSTALL.CMD call %d:\\CHARON\\INSTALL.CMD"'] +
        (['"%s"' % hvsecure.RUN_LINE] if plan.get('secure') else []) + [''])


ALIVE = '/run/charonhv/menu.alive'     # removed only by a clean end (service stop/restart): left after a fault


def clean_exit(signum, frame):
    try:
        os.remove(ALIVE)
    except OSError:
        pass
    os._exit(0)          # the machines run in their own sessions and keep running (session 9)


def main():
    import signal
    signal.signal(signal.SIGTERM, clean_exit)
    try:
        App().run()
    except Exception as e:
        import traceback
        log('FATAL %s' % traceback.format_exc())
        try:
            st = hvvoice.Settings(cfg_language())
            hvvoice.Voice(st).say('Menu Charon Hypervisor przerwało pracę z błędem: %s. Uruchamia się ponownie; maszyny działają dalej.' % e
                                  if st.polish else 'The Charon Hypervisor menu failed: %s. It starts again; the machines keep running.' % e)
            time.sleep(6)
        except Exception:
            pass
        raise


if __name__ == '__main__':
    main()
