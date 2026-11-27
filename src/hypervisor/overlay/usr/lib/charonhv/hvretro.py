# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: the retro assistant (6q) - the hypervisor helping an old Windows every day.

Per machine, settings in /hv/machines/<id>/retro.json (each part switchable; on by default for the old Windows):
simple web and the remote modern browser (through the guest's proxy), download scanning with ClamAV, the mail TLS
bridge, the daily snapshot, printing to PDF, the exchange folder. Hypervisor-wide: the clock (hvclock), the shared
clipboard (text typed into a guest, or read from CLIP.TXT in a machine's exchange folder, always said aloud).
A mixin of charonhv.App: it uses App's say, T, menu, yes_no, ask_text and machines."""
import json, os, re, threading, time

import hvclock, hvpkg, hvprint, hvscan, hvsecure
from hvvoice import log

DAILY_KEEP = 7


def retro_cfg(m):
    try:
        return json.load(open(os.path.join(m.dir, 'retro.json'), encoding='utf-8'))
    except (OSError, ValueError):
        return {}


def retro_save(m, cfg):
    p = os.path.join(m.dir, 'retro.json')
    with open(p + '.new', 'w', encoding='utf-8') as f:
        json.dump(cfg, f, indent=1, ensure_ascii=False)
    os.chmod(p + '.new', 0o600)          # it may hold mail server names (never passwords: those stay in the guest)
    os.replace(p + '.new', p)


class Retro:
    clipboard = ''
    modern_machine = ''
    modern_url = ''     # the last video/DRM address a guest asked for: the modern browser mode opens it (6u)
    spools = {}

    # ---- the clock ------------------------------------------------------------------------------------------
    def clock_start(self):
        hvclock.at_start(log)
        threading.Thread(target=self.clock_sync, args=(False,), daemon=True).start()

    def clock_sync(self, spoken=True):
        if not spoken:
            for _ in range(60):          # the network comes up in the background
                try:
                    if any(l.split()[1] == '00000000' for l in open('/proc/net/route').read().splitlines()[1:] if len(l.split()) > 1):
                        break
                except OSError:
                    pass
                time.sleep(1)
        ok, en, pl = hvclock.sync(log)
        if spoken or not ok:
            self.say(self.T(en, pl), interrupt=False)

    # ---- packages fetched on first use -------------------------------------------------------------------------
    def ensure_packages(self, pkgs, what_en, what_pl):
        if hvpkg.installed(pkgs):
            return True
        self.say(self.T('Checking what %s needs.' % what_en, 'Sprawdzam, czego potrzebuje %s.' % what_pl))
        mb, n, err = hvpkg.plan(pkgs)
        if err:
            log('packages %s: %s' % (' '.join(pkgs), err))
            self.say(self.T('%s cannot be fetched: %s' % (what_en, err), 'Nie można pobrać: %s. Powód: %s' % (what_pl, err)))
            return False
        size_en = 'less than 1 megabyte' if mb < 1 else '%.0f megabytes' % mb
        size_pl = 'mniej niż 1 megabajt' if mb < 1 else '%.0f megabajtów' % mb
        npk = '1 package' if n == 1 else '%d packages' % n
        if n and mb == 0:
            # everything is in the data partition's cache (fetched with the user's yes before a restart): nothing is
            # downloaded, so nothing is asked (session 9: "219 packages, less than 1 megabyte. Fetch them now?")
            self.say(self.T('Installing %s again from the Hypervisor partition.' % what_en, 'Instaluję ponownie z partycji Hipernadzorcy: %s.' % what_pl))
            err = hvpkg.install(pkgs)
            if err:
                log('packages %s failed: %s' % (' '.join(pkgs), err))
                self.say(self.T('Installing %s failed: %s' % (what_en, err), 'Instalacja nie powiodła się: %s. Powód: %s' % (what_pl, err)))
                return False
            return True
        if n and not self.yes_no(self.T('%s needs %s, %s from Alpine Linux. Fetch them now?' % (what_en, npk, size_en),
                                        '%s potrzebuje pakietów: %d, %s z Alpine Linux. Pobrać je teraz?' % (what_pl, n, size_pl))):
            return False
        self.say(self.T('Fetching %s.' % what_en, 'Pobieram: %s.' % what_pl))
        err = hvpkg.install(pkgs)
        if err:
            log('packages %s failed: %s' % (' '.join(pkgs), err))
            self.say(self.T('Fetching %s failed: %s' % (what_en, err), 'Pobieranie nie powiodło się: %s. Powód: %s' % (what_pl, err)))
            return False
        self.say(self.T('%s is installed.' % what_en, 'Zainstalowano: %s.' % what_pl))
        return True

    # ---- ClamAV -------------------------------------------------------------------------------------------------
    def scanner_ready(self, ask=True):
        """Packages + database + daemon; everything said. True when downloads can be scanned now."""
        if not self.ensure_packages(hvscan.PKGS, 'The ClamAV virus scanner', 'skaner wirusów ClamAV') if ask else hvpkg.installed(hvscan.PKGS):
            return False
        if not hvscan.ready() or ask:
            if not hvscan.ready():
                if ask and not self.yes_no(self.T('The ClamAV signature database is about 230 megabytes from ClamAV\'s own mirror. Fetch it now?',
                                                  'Baza sygnatur ClamAV ma około 230 megabajtów z serwera ClamAV. Pobrać ją teraz?')):
                    return False
                self.say(self.T('Fetching the ClamAV database; this takes a few minutes.', 'Pobieram bazę ClamAV; to potrwa kilka minut.'))
            err = hvscan.update_db(log)
            if err and not hvscan.ready():
                self.say(self.T('The ClamAV database could not be fetched: %s' % err, 'Nie udało się pobrać bazy ClamAV: %s' % err))
                return False
            if err:
                self.say(self.T('The ClamAV database was not refreshed (%s); the one from before is used.' % err,
                                'Baza ClamAV nie została odświeżona (%s); używam poprzedniej.' % err), interrupt=False)
        if not hvscan.clamd_running():
            self.say(self.T('Starting the virus scanner, about a minute.', 'Uruchamiam skaner wirusów, około minuty.'), interrupt=False)
            err = hvscan.start_clamd(log)
            if err:
                self.say(self.T('The virus scanner did not start: %s' % err, 'Skaner wirusów się nie uruchomił: %s' % err))
                return False
            self.say(self.T('The virus scanner is ready, database %d megabytes.' % hvscan.db_megabytes(),
                            'Skaner wirusów jest gotowy, baza %d megabajtów.' % hvscan.db_megabytes()), interrupt=False)
        return True

    def say_findings(self, where_en, where_pl, found, err):
        if found:
            names = '; '.join('%s: %s' % (os.path.basename(f) or f, v) for f, v in found[:5])
            self.say(self.T('%s: %d infected files found. %s%s. Nothing was deleted.' % (where_en, len(found), names, ' and more' if len(found) > 5 else ''),
                            '%s: znaleziono zainfekowane pliki: %d. %s%s. Niczego nie usunięto.' % (where_pl, len(found), names, ' i więcej' if len(found) > 5 else '')),
                     interrupt=False)
            for f, v in found:
                log('SCAN FOUND %s %s' % (f, v))
        elif err:
            self.say(self.T('%s: the scan failed: %s' % (where_en, err), '%s: skanowanie nie powiodło się: %s' % (where_pl, err)), interrupt=False)
        else:
            self.say(self.T('%s: no malware found.' % where_en, '%s: nie znaleziono złośliwego oprogramowania.' % where_pl), interrupt=False)

    def scan_folders(self, m=None):
        if not self.scanner_ready():
            return
        dirs = [('/hv/shared', 'The shared folder', 'Folder wspólny')]
        for x in ([m] if m else list(self.machines.values())):
            ex = os.path.join('/hv/exchange', x.id)
            if os.path.isdir(ex):
                dirs.append((ex, 'The exchange folder of %s' % x.name, 'Folder wymiany %s' % x.name))
        for path, en, pl in dirs:
            self.say(self.T('Scanning %s.' % en.lower(), 'Skanuję: %s.' % pl.lower()), interrupt=False)
            found, err = hvscan.scan_tree(path)
            self.say_findings(en, pl, found, err)

    def scan_machine_disk(self, m):
        if m.running():
            self.say(self.T('%s is running; its disk is scanned only while the machine is off.' % m.name,
                            '%s działa; jej dysk skanuję tylko, gdy maszyna jest wyłączona.' % m.name))
            return
        if not self.scanner_ready():
            return
        disk = m.path(m.cfg['disks'][0])
        self.say(self.T('Scanning the disk of %s, read-only. This takes long; the menu answers when it is done.' % m.name,
                        'Skanuję dysk %s, tylko do odczytu. To długo trwa; menu odpowie, gdy skończę.' % m.name))
        found, parts, err = hvscan.scan_disk(disk, log)
        self.say_findings(self.T('The disk of %s' % m.name, 'Dysk %s' % m.name), 'Dysk %s' % m.name, found, err)

    # ---- the daily snapshot -------------------------------------------------------------------------------------
    def daily_snapshot(self, m):
        if not retro_cfg(m).get('daily', bool(hvsecure.supported(m.cfg))) or m.installing():
            return
        tag = 'daily-' + time.strftime('%Y-%m-%d')
        try:
            tags = m.snapshots()
            if tag in tags:
                return
            m.snapshot_save(tag)
            if tag not in m.snapshots():
                raise RuntimeError('not in the list after saving')
            old = sorted(t for t in tags if t.startswith('daily-'))
            for t in old[:max(0, len(old) + 1 - DAILY_KEEP)]:
                m.snapshot_delete(t)
            self.say(self.T("Today's snapshot of %s saved." % m.name, 'Dzisiejsza migawka %s zapisana.' % m.name), interrupt=False)
        except Exception as e:
            log('daily snapshot %s: %r' % (m.id, e))
            self.say(self.T("Today's snapshot of %s failed: %s" % (m.name, e), 'Dzisiejsza migawka %s nie powiodła się: %s' % (m.name, e)), interrupt=False)

    def revert_daily(self, m):
        tags = sorted(t for t in m.snapshots() if t.startswith('daily-'))
        if not tags:
            self.say(self.T('%s has no daily snapshot yet.' % m.name, '%s nie ma jeszcze codziennej migawki.' % m.name))
            return
        items = [(t[6:], t) for t in reversed(tags)]
        t = self.menu(self.T('Go back to which day?', 'Wrócić do którego dnia?'), items)
        if t and self.yes_no(self.T('Go back to %s? Everything done in %s since then is lost.' % (t[6:], m.name),
                                    'Wrócić do %s? Wszystko, co zrobiono w %s od tego czasu, przepadnie.' % (t[6:], m.name)), False):
            out = m.snapshot_revert(t)
            log('revert %s to %s: %s' % (m.id, t, out))
            self.say(self.T('%s is back at %s.' % (m.name, t[6:]), '%s wróciła do %s.' % (m.name, t[6:])) if not out.strip() else
                     self.T('Going back failed: %s' % out.strip(), 'Powrót nie powiódł się: %s' % out.strip()))

    # ---- printing --------------------------------------------------------------------------------------------
    def poll_prints(self):
        for m in list(self.machines.values()):
            if not m.running() or not os.path.exists(hvprint.port_file(m.id)):
                continue
            sp = self.spools.get(m.id)
            if sp is None:
                sp = self.spools[m.id] = hvprint.Spool(m.id, lambda path, pages, err, m=m: self.printed(m, path, pages, err), log)
            try:
                sp.poll()
            except Exception as e:
                log('print spool %s: %r' % (m.id, e))

    def printed(self, m, path, pages, err):
        log('print %s: %s pages=%d err=%s' % (m.id, path, pages, err))
        if err:
            self.say(self.T('%s printed, but no PDF was made: %s. Kept as %s.' % (m.name, err, os.path.basename(path)),
                            '%s wydrukowała, ale PDF nie powstał: %s. Zachowano jako %s.' % (m.name, err, os.path.basename(path))), interrupt=False)
        elif path.endswith('.txt'):
            self.say(self.T('%s printed text; kept as %s.' % (m.name, os.path.basename(path)),
                            '%s wydrukowała tekst; zachowano jako %s.' % (m.name, os.path.basename(path))), interrupt=False)
        else:
            self.say(self.T('%s printed %d %s to a PDF: %s, also in the shared folder, Prints.' % (m.name, pages, 'page' if pages == 1 else 'pages', os.path.basename(path)),
                            '%s wydrukowała stron: %d do PDF: %s, także w folderze wspólnym, Prints.' % (m.name, pages, os.path.basename(path))),
                     interrupt=False)

    # ---- clipboard --------------------------------------------------------------------------------------------
    def clip_from_exchange(self, m):
        raw = None
        try:
            if m.running():
                # the exchange drive of a running machine, read-only and shared (hvexchange)
                import hvexchange
                raw = hvexchange.read_live(m.dir, 'CLIP.TXT')
            else:
                with open(os.path.join('/hv/exchange', m.id, 'CLIP.TXT'), 'rb') as f:
                    raw = f.read(1048576)
        except Exception as e:
            log('clip from exchange %s: %r' % (m.id, e))
        if raw is None:
            self.say(self.T('No CLIP.TXT on the exchange drive of %s. Save the text there in the guest, as CLIP.TXT.' % m.name,
                            'Brak CLIP.TXT na dysku wymiany %s. Zapisz tekst tam w gościu, jako CLIP.TXT.' % m.name))
            return
        if raw[:2] in (b'\xff\xfe', b'\xfe\xff'):
            text = raw.decode('utf-16', 'replace')
        else:
            try:
                text = raw.decode('utf-8-sig')
            except UnicodeDecodeError:
                text = raw.decode('cp1250', 'replace')     # Notepad's "ANSI" on a Polish Windows
        Retro.clipboard = text.replace('\r\n', '\n').strip()
        self.say(self.T('Clipboard from %s, %d characters: %s' % (m.name, len(Retro.clipboard), Retro.clipboard[:400]),
                        'Schowek z %s, znaków: %d: %s' % (m.name, len(Retro.clipboard), Retro.clipboard[:400])))

    def clip_into_guest(self, m):
        if not Retro.clipboard:
            self.say(self.T('The clipboard is empty.', 'Schowek jest pusty.'))
            return
        if not m.running():
            self.say(self.T('%s is not running.' % m.name, '%s nie działa.' % m.name))
            return
        text = Retro.clipboard
        bad = sorted(set(ch for ch in text if ch not in TYPEABLE))
        if bad:
            self.say(self.T('Typing the clipboard into %s; %d characters cannot be typed and are left out.' % (m.name, len(bad)),
                            'Wpisuję schowek do %s; znaków, których nie da się wpisać: %d, pomijam je.' % (m.name, len(bad))))
        else:
            self.say(self.T('Typing the clipboard into %s, %d characters.' % (m.name, len(text)), 'Wpisuję schowek do %s, znaków: %d.' % (m.name, len(text))))
        import hvkeys
        for line_no, line in enumerate(text.split('\n')):
            if line_no:
                m.send_combo(['ret'])
            hvsecure.type_into(m, ''.join(ch for ch in line if ch in TYPEABLE))
        self.say(self.T('Typed.', 'Wpisano.'), interrupt=False)

    # ---- the menu ----------------------------------------------------------------------------------------------
    def retro_menu(self, m):
        T = self.T
        while True:
            r = retro_cfg(m)
            on = lambda k, d=True: bool(r.get(k, d))
            onoff = lambda v: T('on', 'włączone') if v else T('off', 'wyłączone')
            mail = r.get('mail') or {}
            st = lambda k: onoff(self.store.get(k, m.id))
            # 6ze: the three switches are Hypervisor settings now (general + this machine's exception); this item opens them
            items = [(T('Switches - simple web %s, scanning %s, daily snapshot %s: in Hypervisor settings for this machine'
                        % (st('simple_web'), st('scan_downloads'), st('daily_snapshot')),
                        'Przełączniki - prosta sieć %s, skanowanie %s, codzienna migawka %s: w ustawieniach hipernadzorcy dla tej maszyny'
                        % (st('simple_web'), st('scan_downloads'), st('daily_snapshot'))), 'switches'),
                     (T('Sites always shown simple: %d' % len(self.simple_sites(m)), 'Strony zawsze uproszczone: %d' % len(self.simple_sites(m))), 'sites'),
                     (T('Remote modern browser: %s' % (T('installed', 'zainstalowana') if hvpkg.installed(('chromium-headless-shell',)) else T('not installed', 'niezainstalowana')),
                        'Zdalna nowoczesna przeglądarka: %s' % (T('installed', 'zainstalowana') if hvpkg.installed(('chromium-headless-shell',)) else T('not installed', 'niezainstalowana'))), 'remote'),
                     (T('Scan the shared and exchange folders now', 'Skanuj teraz folder wspólny i folder wymiany'), 'scanfolders'),
                     (T("Scan this machine's disk (it must be off)", 'Skanuj dysk tej maszyny (musi być wyłączona)'), 'scandisk'),
                     (T('Mail servers: %s' % (', '.join('%s %s' % (k, v) for k, v in sorted(mail.items()) if v) or T('none set', 'nie ustawione')),
                        'Serwery poczty: %s' % (', '.join('%s %s' % (k, v) for k, v in sorted(mail.items()) if v) or 'nie ustawione')), 'mail'),
                     (T('Go back to a daily snapshot', 'Wróć do codziennej migawki'), 'revert'),
                     (T('Printing to PDF: %s' % (T('ready', 'gotowe') if hvpkg.installed(hvprint.PKGS) else T('needs Ghostscript', 'potrzebuje Ghostscript')),
                        'Drukowanie do PDF: %s' % ('gotowe' if hvpkg.installed(hvprint.PKGS) else 'potrzebuje Ghostscript')), 'print'),
                     (T('Clipboard: take CLIP.TXT from the exchange folder', 'Schowek: weź CLIP.TXT z folderu wymiany'), 'clipin'),
                     (T('Clipboard: type it into this machine', 'Schowek: wpisz go do tej maszyny'), 'clipout'),
                     (T('Clipboard: type new text', 'Schowek: wpisz nowy tekst'), 'clipset'),
                     (T('Clock and time sync', 'Zegar i synchronizacja czasu'), 'clock'),
                     (T('Full speed: %s' % (T('hardware virtualisation on', 'wirtualizacja sprzętowa włączona') if self.kvm else
                                            T('no hardware virtualisation on this computer', 'brak wirtualizacji sprzętowej na tym komputerze')),
                        'Pełna szybkość: %s' % ('wirtualizacja sprzętowa włączona' if self.kvm else 'brak wirtualizacji sprzętowej na tym komputerze')), 'kvm')]
            c = self.menu(T('Retro assistant for %s' % m.name, 'Asystent retro dla %s' % m.name), items)
            if c is None:
                return
            try:
                self.retro_choice(m, r, c)
            except Exception as e:
                log('retro %s %s: %r' % (m.id, c, e))
                self.say(T('That failed: %s' % e, 'Nie udało się: %s' % e))

    def simple_sites(self, m):
        try:
            return [l.strip() for l in open(os.path.join(m.dir, 'web', 'simple-sites.txt')) if l.strip()]
        except OSError:
            return []

    def retro_choice(self, m, r, c):
        T = self.T
        later = T(' It takes effect at the next start of %s.' % m.name, ' Zadziała od następnego uruchomienia %s.' % m.name) if m.running() else ''
        if c == 'switches':
            self.settings_group('network', m)
            if self.store.get('scan_downloads', m.id) and not hvscan.ready():
                self.scanner_ready()        # scanning on: ClamAV and its database (size said and asked)
        elif c == 'sites':
            sites = self.simple_sites(m)
            items = [(T('Add a site', 'Dodaj stronę'), '+')] + [(T('Remove %s' % s, 'Usuń %s' % s), s) for s in sites]
            pick = self.menu(T('Sites always shown simple', 'Strony zawsze uproszczone'), items)
            if pick == '+':
                s = (self.ask_text(T('The site, for example example.com.', 'Strona, na przykład example.com.')) or '').strip().lower()
                s = re.sub(r'^https?://', '', s).split('/')[0]
                if s:
                    sites.append(s)
            elif pick:
                sites.remove(pick)
            else:
                return
            os.makedirs(os.path.join(m.dir, 'web'), mode=0o700, exist_ok=True)
            open(os.path.join(m.dir, 'web', 'simple-sites.txt'), 'w').write('\n'.join(sorted(set(sites))) + '\n')
            self.say(T('Saved; %d sites.' % len(set(sites)), 'Zapisano; stron: %d.' % len(set(sites))) + later)
        elif c == 'remote':
            if self.ensure_packages(('chromium-headless-shell', 'samba-server'), 'The remote modern browser (Chromium) and the Downloads share', 'zdalna nowoczesna przeglądarka (Chromium) i udział Pobrane'):
                self.say(T('In the old browser of %s open http://simple.charon/ and use the remote modern browser link. Facebook and similar '
                           'sites forbid automated access in their terms; DRM video such as Netflix cannot play there.' % m.name,
                           'W starej przeglądarce %s otwórz http://simple.charon/ i użyj odnośnika zdalnej nowoczesnej przeglądarki. Facebook i '
                           'podobne strony zabraniają w regulaminie dostępu automatycznego; wideo z DRM, jak Netflix, tam nie zagra.' % m.name))
        elif c == 'scanfolders':
            self.scan_folders(m)
        elif c == 'scandisk':
            self.scan_machine_disk(m)
        elif c == 'mail':
            mail = dict(r.get('mail') or {})
            for kind, en, pl in (('imap', 'IMAP server for receiving, for example imap.example.com; empty for none', 'Serwer IMAP do odbierania, na przykład imap.example.com; puste, gdy brak'),
                                 ('pop', 'POP3 server for receiving; empty for none', 'Serwer POP3 do odbierania; puste, gdy brak'),
                                 ('smtp', 'SMTP server for sending, with :587 for STARTTLS; empty for none', 'Serwer SMTP do wysyłania, z :587 dla STARTTLS; puste, gdy brak')):
                v = self.ask_text(T(en, pl), mail.get(kind, ''))
                if v is None:
                    return
                mail[kind] = v.strip()
            r['mail'] = mail
            retro_save(m, r)
            self.say(T('Saved. In the mail program of %s use the server 10.0.2.100 for receiving and sending, without SSL; the hypervisor adds TLS. '
                       'The password stays in the guest.' % m.name,
                       'Zapisano. W programie pocztowym %s użyj serwera 10.0.2.100 do odbierania i wysyłania, bez SSL; TLS dodaje hipernadzorca. '
                       'Hasło zostaje w gościu.' % m.name) + later)
        elif c == 'revert':
            self.revert_daily(m)
        elif c == 'print':
            if self.ensure_packages(hvprint.PKGS, 'Printing to PDF (Ghostscript)', 'drukowanie do PDF (Ghostscript)'):
                self.say(T('Print in %s to the printer Charon PDF; the PDF goes to the Hypervisor partition and the shared folder, Prints.' % m.name,
                           'Drukuj w %s na drukarce Charon PDF; PDF trafi na partycję hipernadzorcy i do folderu wspólnego, Prints.' % m.name))
        elif c == 'clipin':
            self.clip_from_exchange(m)
        elif c == 'clipout':
            self.clip_into_guest(m)
        elif c == 'clipset':
            v = self.ask_text(T('Clipboard text.', 'Tekst schowka.'), Retro.clipboard)
            if v is not None:
                Retro.clipboard = v
                self.say(T('Clipboard: %s' % v, 'Schowek: %s' % v))
        elif c == 'clock':
            pick = self.menu(T('Clock', 'Zegar'), [(hvclock.report('pl' if self.T('a', 'b') == 'b' else 'en'), 'r'),
                                                     (T('Set the clock from the internet now', 'Ustaw zegar z internetu teraz'), 's')])
            if pick == 's':
                self.clock_sync(True)
            elif pick == 'r':
                self.say(hvclock.report('pl' if self.T('a', 'b') == 'b' else 'en'))
        elif c == 'kvm':
            self.say(T('This computer gives the hypervisor hardware virtualisation: machines run at full speed.' if self.kvm else
                       'This computer gives the hypervisor no hardware virtualisation (VT-x or AMD-V is missing or switched off in the firmware, '
                       'or this is itself a virtual machine without nested virtualisation): machines are emulated, slowly.',
                       'Ten komputer daje hipernadzorcy wirtualizację sprzętową: maszyny działają z pełną szybkością.' if self.kvm else
                       'Ten komputer nie daje hipernadzorcy wirtualizacji sprzętowej (brak VT-x lub AMD-V albo są wyłączone w oprogramowaniu '
                       'układowym, albo to samo jest maszyną wirtualną bez wirtualizacji zagnieżdżonej): maszyny są emulowane, wolno.'))

    # ---- 6s: the modern browser on the real screen -------------------------------------------------------------
    def choose_reader(self):
        """User (r410): the screen reader of every graphical session, asked when the modern browser opens the first
        time and changed in Sound; kept on the data partition (hvmodern.READER_FILE). Returns 'orca', 'linda' or None."""
        import hvmodern, hvlinda
        T = self.T
        if hvlinda.installed():
            linda = T('Linda, ready', 'Linda, gotowa')
        elif hvlinda.building():
            linda = T('Linda, being built now; Orca speaks meanwhile', 'Linda, właśnie się buduje; w tym czasie mówi Orca')
        else:
            gb = ('%g' % hvlinda.SIZE_GB)
            linda = T('Linda, needs about %s gigabytes and %d minutes the first time; Orca speaks meanwhile' % (gb, hvlinda.MINUTES),
                      'Linda, za pierwszym razem potrzebuje około %s gigabajta i %d minut; w tym czasie mówi Orca' % (gb.replace('.', ','), hvlinda.MINUTES))
        items = [(T('Orca, ready at once', 'Orca, gotowa od razu'), 'orca'), (linda, 'linda')]
        c = self.menu(T('The browser uses a screen reader for Linux. Which screen reader do you choose?',
                        'Przeglądarka używa czytnika ekranu dla systemu Linux. Który czytnik ekranu wybierasz?'), items)
        if c:
            hvmodern.set_reader_choice(c)
        return c

    def linda_prepare(self):
        """Linda chosen and not built: the packages (size said and asked by ensure_packages), then Nix builds Linda in
        the background; the hypervisor says when it is ready (App.background -> hvlinda.just_finished)."""
        import hvlinda
        T = self.T
        if hvlinda.installed():
            # Linda is on /hv, its X11 session's packages are in RAM: after a restart they come back from the cache
            # (session 9: without them the session fell back to Orca with "Linda is not ready yet")
            self.ensure_packages(hvlinda.APK, 'Linda (the Nix package manager and an X11 session)', 'Linda (menedżer pakietów Nix i sesja X11)')
            return
        if hvlinda.building():
            return
        err = hvlinda.space_problem()        # before anything is fetched (session 9, user: check the space first)
        if err:
            log('linda build: %s' % err[0])
            self.say(T('%s. Orca reads meanwhile.' % err[0], '%s. W tym czasie czyta Orca.' % err[1]))
            return
        # the X11 session's packages are in RAM (fetched again from /hv/apk-cache after a restart); Linda is on /hv
        if not self.ensure_packages(hvlinda.APK, 'Linda (the Nix package manager and an X11 session)',
                                    'Linda (menedżer pakietów Nix i sesja X11)'):
            return
        resumed = hvlinda.progress() is not None
        err = hvlinda.build_start()
        if err:
            log('linda build: %s' % err[0])
            self.say(T('Linda cannot be built: %s. Orca reads meanwhile.' % err[0], 'Nie można zbudować Lindy: %s. W tym czasie czyta Orca.' % err[1]))
            return
        self._linda_said = time.time()
        if resumed:
            pct, left = hvlinda.progress()
            self.say(T('Building Linda again from where it stopped, about %d percent done, about %d minutes left. Orca reads until it is ready.' % (pct, left),
                       'Wznawiam budowanie Lindy od miejsca przerwania: gotowe około %d procent, zostało około %d minut. Do tego czasu czyta Orca.' % (pct, left)),
                     interrupt=False)
            return
        gb = '%g' % hvlinda.SIZE_GB
        self.say(T('Linda is being built from its source by Nix: about %s gigabytes and %d minutes. Orca reads until it is ready.' % (gb, hvlinda.MINUTES),
                   'Nix buduje Lindę z jej źródeł: około %s gigabajta i %d minut. Do tego czasu czyta Orca.' % (gb.replace('.', ','), hvlinda.MINUTES)),
                 interrupt=False)

    LINDA_PROGRESS_EVERY = 180      # seconds between the spoken progress lines of a running build

    def linda_poll(self):
        """Once at the menu's start: /nix bound, an interrupted build resumed (packages from the data partition's
        cache, no question: the user agreed when it started). While building: progress said every 3 minutes, but
        never over a running guest's own reader (only when the menu has the screen). At the end: ready or the error."""
        import hvlinda, hvpkg
        T = self.T
        if not getattr(self, '_linda_started', False):
            self._linda_started = True
            if hvlinda.at_start() == 'resume':
                log('linda build interrupted (restart): resuming')
                if hvpkg.installed(hvlinda.APK) or not hvpkg.install(hvlinda.APK):
                    self.linda_prepare()
                else:
                    log('linda resume: packages not available')
            return
        if hvlinda.building():
            now = time.time()
            if now - getattr(self, '_linda_said', 0) >= self.LINDA_PROGRESS_EVERY and getattr(self, 'focus', None) is None:
                self._linda_said = now
                p = hvlinda.progress()
                if p:
                    self.say(T('Linda: about %d percent built, about %d minutes left.' % p,
                               'Linda: zbudowano około %d procent, zostało około %d minut.' % p), interrupt=False)
            return
        r = hvlinda.ended()
        if r is None:
            return
        rc, secs = r
        if rc == 0 and hvlinda.installed():
            self.say(T('Linda is ready after %d minutes; it reads the modern browser from its next start.' % max(1, secs // 60),
                       'Linda jest gotowa po %d minutach; czyta nowoczesną przeglądarkę od jej następnego uruchomienia.' % max(1, secs // 60)),
                     interrupt=False)
        else:
            err = hvlinda.last_error()
            log('linda build failed rc %s: %s' % (rc, err))
            self.say(T('Linda could not be built: %s. Orca keeps reading; it is tried again at the next start of the modern browser.' % err,
                       'Nie udało się zbudować Lindy: %s. Czyta dalej Orca; kolejna próba przy następnym uruchomieniu nowoczesnej przeglądarki.' % err),
                     interrupt=False)

    def modern_browser(self, url=None):
        import hvmodern
        T = self.T
        if url is None:
            url = 'about:home'
            if Retro.modern_url:
                if self.yes_no(T('Open %s, which a machine asked for?' % Retro.modern_url, 'Otworzyć %s, o który prosiła maszyna?' % Retro.modern_url)):
                    url = Retro.modern_url
                Retro.modern_url = ''
            if url == 'about:home':
                # the mode is a kiosk without an address bar: the address is asked here
                v = self.ask_text(T('Address to open; empty for the start page.', 'Adres do otwarcia; puste, aby otworzyć stronę startową.'), '')
                if v is None:
                    return
                if v.strip():
                    url = v.strip() if '://' in v else 'https://' + v.strip()
        reader = hvmodern.reader_choice() or self.choose_reader()
        if reader is None:
            return
        if reader == 'linda':
            self.linda_prepare()
        if not self.ensure_packages(hvmodern.APK, 'The modern browser mode (Flatpak, a screen reader, sound)',
                                    'tryb nowoczesnej przeglądarki (Flatpak, czytnik ekranu, dźwięk)'):
            return
        if not hvmodern.flatpak_ready():
            self.say(T('Asking Flathub for the size of Firefox.', 'Pytam Flathub o rozmiar Firefoksa.'))
            mb, err = hvmodern.flatpak_plan()
            if err:
                log('modern plan: %s' % err)
                self.say(T('Flathub did not answer: %s' % err, 'Flathub nie odpowiedział: %s' % err))
                return
            if not self.yes_no(T("Mozilla's Firefox from Flathub with its runtime is about %d megabytes. Fetch it now?" % mb,
                                 'Firefox Mozilli z Flathub wraz ze środowiskiem to około %d megabajtów. Pobrać teraz?' % mb)):
                return
            self.say(T('Fetching Firefox from Flathub; this takes a while.', 'Pobieram Firefoksa z Flathub; to potrwa.'))
            err = hvmodern.flatpak_install()
            if err:
                log('modern install: %s' % err)
                self.say(T('Firefox could not be installed: %s' % err, 'Nie udało się zainstalować Firefoksa: %s' % err))
                return
        running = [m for m in self.machines.values() if m.running()]
        for m in running:
            try:
                m.qmp.cmd('stop')
            except Exception:
                pass
        who = hvmodern.session_reader()
        name = 'Linda' if who == 'linda' else 'Orca'
        meanwhile = ''
        if who != reader:       # Linda chosen, not built yet: Orca stands in, said aloud
            meanwhile = T(' Linda is not ready yet; Orca reads meanwhile.', ' Linda nie jest jeszcze gotowa; w tym czasie czyta Orca.')
        self.say(T('The modern browser opens on the screen; %s reads it.%s Protected video plays only here, never in a guest. '
                   'Left Control and Left Alt together end it.%s' % (name, meanwhile, ' Running machines are paused.' if running else ''),
                   'Nowoczesna przeglądarka otwiera się na ekranie; czyta ją %s.%s Chronione wideo gra tylko tutaj, nigdy w gościu. '
                   'Lewy Control i lewy Alt razem ją kończą.%s' % (name, meanwhile, ' Działające maszyny wstrzymano.' if running else '')))
        self.voice.wait(20)
        self.kbd.set_grab(False)        # before cage: libinput must be able to open the keyboard
        try:
            cage = hvmodern.start(url, log)
        except Exception as e:
            self.kbd.set_grab(True)
            log('modern start: %r' % e)
            self.say(T('The modern browser did not start: %s' % e, 'Nowoczesna przeglądarka się nie uruchomiła: %s' % e))
            return
        try:
            while cage.poll() is None:
                k = self.kbd.get(0.3)
                if k is not None and k.down and k.ctrl and k.alt and k.code in hvkeys_host():
                    break
        finally:
            hvmodern.stop(cage)
            self.kbd.set_grab(True)
            for m in running:
                try:
                    m.qmp.cmd('cont')
                except Exception:
                    pass
        code = cage.returncode
        if code not in (0, None, -15):
            try:
                tail = open('/run/charonhv/modern.log', errors='replace').read().strip().splitlines()[-1][:160]
            except (OSError, IndexError):
                tail = 'exit %s' % code
            log('modern ended %s: %s' % (code, tail))
            self.say(T('The modern browser ended with an error: %s' % tail, 'Nowoczesna przeglądarka zakończyła się błędem: %s' % tail))
        else:
            self.say(T('Modern browser closed; back in the hypervisor.', 'Nowoczesna przeglądarka zamknięta; z powrotem w hipernadzorcy.'))

    # ---- proxy events said aloud (called by App.poll_proxies) --------------------------------------------------
    def retro_event(self, m, kind, what):
        T = self.T
        if kind == 'SCANFOUND':
            host, name, virus = (what.split('|') + ['', '', ''])[:3]
            self.say(T('%s: download stopped, %s from %s contains %s.' % (m.name, name, host, virus),
                       '%s: pobieranie zatrzymane, %s z %s zawiera %s.' % (m.name, name, host, virus)), interrupt=False)
        elif kind == 'SCANCLEAN':
            host, name = (what.split('|') + ['', ''])[:2]
            # measured session 7: every favicon and Windows Update's wuident.cab (twice a minute) was said aloud; only
            # files the user downloads are said now - page parts and update catalogue files stay in the log
            quiet = name.lower().endswith(('.ico', '.png', '.gif', '.jpg', '.jpeg', '.css', '.js', '.cab', '.xml', '.htm', '.html'))
            if not quiet:
                self.say(T('%s: download %s checked, clean.' % (m.name, name), '%s: pobrany plik %s sprawdzony, czysty.' % (m.name, name)), interrupt=False)
        elif kind == 'SCANOFF':
            self.say(T('%s: a download was not scanned, the virus scanner is not running; switch it on in the retro assistant.' % m.name,
                       '%s: pobrany plik nie został przeskanowany, skaner wirusów nie działa; włącz go w asystencie retro.' % m.name), interrupt=False)
        elif kind in ('SCANERR', 'SCANSKIP'):
            self.say(T('%s: a download was not scanned: %s.' % (m.name, what.replace('|', ', ')),
                       '%s: pobrany plik nie został przeskanowany: %s.' % (m.name, what.replace('|', ', '))), interrupt=False)
        elif kind == 'MAILNOCONF':
            self.say(T('%s: the mail program asked for %s, but no mail server is set; retro assistant, mail servers.' % (m.name, what.upper()),
                       '%s: program pocztowy pyta o %s, ale nie ustawiono serwera; asystent retro, serwery poczty.' % (m.name, what.upper())), interrupt=False)
        elif kind == 'MAILFAIL':
            self.say(T('%s: the mail server was not reached: %s.' % (m.name, what), '%s: nie połączono z serwerem poczty: %s.' % (m.name, what)), interrupt=False)
        elif kind == 'MAIL' and not getattr(self, '_said_mail', False):
            self._said_mail = True
            self.say(T('%s: mail goes out with %s.' % (m.name, what.split()[-1]), '%s: poczta wychodzi przez %s.' % (m.name, what.split()[-1])), interrupt=False)
        elif kind == 'NEEDMODERN':
            # "site|drm|url" from the browser layer (hvroute), or a bare host from the remote browser
            site, why, url = (what.split('|', 2) + ['', ''])[:3]
            Retro.modern_url = url or 'https://' + site
            Retro.modern_machine = m.id      # 6v: its downloads go to this machine's share
            if why == 'video':
                self.say(T("%s: %s's terms forbid converting its videos for an old player. In the hypervisor's main menu, Modern browser "
                           'on the real screen opens it.' % (m.name, site),
                           '%s: regulamin %s zabrania przerabiania filmów dla starego odtwarzacza. W menu głównym hipernadzorcy pozycja '
                           'Nowoczesna przeglądarka na prawdziwym ekranie go otworzy.' % (m.name, site)), interrupt=False)
            else:
                self.say(T("%s: %s plays protected video. In the hypervisor's main menu, Modern browser on the real screen opens it." % (m.name, site),
                           '%s: %s odtwarza chronione wideo. W menu głównym hipernadzorcy pozycja Nowoczesna przeglądarka na prawdziwym '
                           'ekranie go otworzy.' % (m.name, site)), interrupt=False)
        elif kind == 'TERMS':
            self.say(T("%s: %s's terms forbid automated access; you use it at your own risk." % (m.name, what),
                       '%s: regulamin %s zabrania dostępu automatycznego; używasz na własne ryzyko.' % (m.name, what)), interrupt=False)
        elif kind in ('REMOTEFAIL', 'SIMPLEFAIL'):
            self.say(T('%s: %s failed: %s.' % (m.name, 'the remote browser' if kind == 'REMOTEFAIL' else 'simple web', what),
                       '%s: %s zawiodła: %s.' % (m.name, 'zdalna przeglądarka' if kind == 'REMOTEFAIL' else 'prosta sieć', what)), interrupt=False)


def hvkeys_host():
    import hvkeys
    return hvkeys.HOST_MODS


TYPEABLE = set(' abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789`~!@#$%^&*()-_=+[]{}\\|;:\'",.<>/?')
