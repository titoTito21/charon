#!/usr/bin/env python3
"""r410: the questions of the detailed install mode - ONE table, the single source of

  1. src/efi/CharonWsQuestions.h          the ask::Question tables (groups, Linux, Windows, CharonDOS);
  2. the key check                         every Linux key must have a handler `apply_<key>()` in
                                           src/linux/personal.sh, every Windows key a handler
                                           `function Apply-<key>` in tools/charon-addon/Charon-Personal.ps1;
                                           a key without a handler FAILS the generation (a question whose
                                           answer nobody applies must not exist);
  3. tests/charon-r410-questions.txt       the table for people: key, systems, default, how it is applied,
                                           and the primary source each row was checked against;
  4. tests/charon-install-host/detail-keys.txt   key lists per system for the host tests.

Usage: python3.15 tools/charon-install/charon_questions.py [--check]     (--check: write nothing)

A row: Q(key, group, paths, kind, en, pl, default, choices, ...).
  paths   which installations ask it: d Debian's installer, u Ubuntu's installer, D U A F the ready images of
          Debian, Ubuntu, Arch, Fedora; w Windows; x CharonDOS.
  when    extra C++ condition (detailed::AnyDesk(), detailed::Is("ssh","yes") ...).
  src     where the setting was verified (item of work/r410a-evidence/verify-linux.txt = L<n>,
          verify-windows.txt = W<n>) and the URL of the primary source.
  verified=False keeps a row OUT of the build; it is listed as unverified in the table for people.
The default of every row is the behaviour of the express mode: an answer equal to its default is not
carried to the installed system at all.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
LINUX = 'dDuUAF'

GROUPS = [  # id, en, pl, level
    ('lang', 'Language and time', 'Język i czas', 0),
    ('acct', 'Accounts and sign-in', 'Konta i logowanie', 0),
    ('boot', 'Start-up', 'Uruchamianie', 0),
    ('speech', 'Speech and braille', 'Mowa i brajl', 0),
    ('desk', 'Desktop and programs', 'Pulpit i programy', 0),
    ('look', 'Appearance and behaviour', 'Wygląd i zachowanie', 0),
    ('net', 'Network', 'Sieć', 0),
    ('upd', 'Updates and privacy', 'Aktualizacje i prywatność', 0),
    ('hw', 'Power and hardware', 'Zasilanie i sprzęt', 0),
    ('adm', 'Administration', 'Administracja', 1),
]
ROWS = []


def Q(key, group, paths, kind, en, pl, default='', choices=(), when='', level=0, help_en='', help_pl='',
      low=0, high=0, risk='', secret=False, apply='', src='', verified=True, hidden=''):
    assert re.fullmatch(r'[a-z][a-z0-9_]*', key), key
    assert kind in ('choice', 'yesNo', 'multi', 'number', 'text', 'secret', 'name'), kind
    assert group in [g[0] for g in GROUPS], group
    if kind == 'choice':
        assert default in [c[0] for c in choices], (key, default)
    if kind == 'yesNo':
        assert default in ('yes', 'no'), key
    ROWS.append(dict(key=key, group=group, paths=paths, kind=kind, en=en, pl=pl, default=default, choices=list(choices),
                     when=when, level=level, help_en=help_en, help_pl=help_pl, low=low, high=high, risk=risk,
                     secret=secret, apply=apply, src=src, verified=verified, hidden=hidden))


KEEP = ('keep', 'leave as the system sets it', 'zostaw tak, jak ustawia system')

# ---------------------------------------------------------------------------------- Linux
Q('formats', 'lang', LINUX, 'choice', 'Dates, numbers and currency as in:', 'Daty, liczby i waluta jak w kraju:', 'same',
  [('same', 'the same as the system language', 'tak jak język systemu'), ('pl_PL', 'Poland', 'Polska'), ('en_US', 'United States', 'Stany Zjednoczone'),
   ('en_GB', 'United Kingdom', 'Wielka Brytania'), ('de_DE', 'Germany', 'Niemcy'), ('fr_FR', 'France', 'Francja'), ('es_ES', 'Spain', 'Hiszpania')],
  apply='locale generated; LC_TIME LC_NUMERIC LC_MONETARY LC_PAPER LC_MEASUREMENT in /etc/default/locale (Debian, Ubuntu) or /etc/locale.conf (Arch, Fedora)', src='L1')
Q('ntp', 'lang', LINUX, 'yesNo', 'Set the clock from the Internet?', 'Ustawiać zegar z Internetu?', 'yes',
  apply='timedatectl set-ntp false', src='L3')
Q('sudo_nopw', 'acct', LINUX, 'yesNo', 'Run administrator commands without asking for the password?', 'Wykonywać polecenia administratora bez pytania o hasło?', 'no',
  risk='yes', apply='/etc/sudoers.d/99-charon-nopasswd, mode 0440, checked with visudo -cf and sudo -n', src='L4')
Q('shell', 'acct', LINUX, 'choice', 'Command shell:', 'Powłoka poleceń:', 'bash',
  [('bash', 'bash', 'bash'), ('zsh', 'zsh', 'zsh'), ('fish', 'fish', 'fish')], apply='package + usermod -s', src='L5')
Q('groups', 'acct', LINUX, 'multi', 'Add the account to extra groups:', 'Dodać konto do grup:', '',
  [('dialout', 'serial ports, for braille displays on a serial line', 'porty szeregowe, dla monitorów brajlowskich na łączu szeregowym'),
   ('lp', 'printers', 'drukarki'), ('audio', 'sound devices directly', 'urządzenia dźwiękowe bezpośrednio'), ('video', 'video devices', 'urządzenia wideo')],
  apply='usermod -aG (dialout is uucp on Arch); a group the system does not have is reported', src='L6')
Q('grub_wait', 'boot', LINUX, 'choice', 'How long the Linux boot menu waits:', 'Jak długo czeka menu rozruchu Linuksa:', 'keep',
  [KEEP, ('0', 'not at all', 'wcale'), ('2', '2 seconds', '2 sekundy'), ('5', '5 seconds', '5 sekund'), ('10', '10 seconds', '10 sekund'), ('30', '30 seconds', '30 sekund')],
  apply='GRUB_TIMEOUT in /etc/default/grub (a drop-in /etc/default/grub.d/90-charon.cfg on Debian and Ubuntu), then the distribution\'s command that regenerates grub.cfg', src='L7')
# r410 (ls): a choice with "keep" as the default. As a yes/no question with the default "no" it promised no tune, but
# Debian's speaking installer writes its own GRUB_INIT_TUNE (/etc/default/grub.d/brltty.cfg, VM proof fb task 1).
Q('grub_tune', 'boot', LINUX, 'choice', 'Short tune when the Linux boot menu appears:', 'Krótki sygnał, gdy pojawi się menu rozruchu Linuksa:', 'keep',
  [('keep', 'as the system sets it (Debian\'s speaking installer plays one)', 'tak, jak ustawia system (instalator Debiana z mową go gra)'),
   ('on', 'play a tune', 'graj sygnał'), ('off', 'no tune', 'bez sygnału')],
  apply='on: GRUB_INIT_TUNE="480 440 1"; off: every GRUB_INIT_TUNE line in /etc/default/grub and grub.d/*.cfg commented out', src='L7')
Q('boot_text', 'boot', LINUX, 'yesNo', 'Show start-up messages as text instead of a splash screen?', 'Pokazywać komunikaty startowe jako tekst zamiast ekranu powitalnego?', 'no',
  apply='"quiet" and "splash" removed from GRUB_CMDLINE_LINUX_DEFAULT', src='L7')
Q('sd_rate', 'speech', LINUX, 'choice', 'Speech rate of the desktop reader:', 'Tempo mowy czytnika pulpitu:', 'charon',
  [('charon', 'as in Charon', 'takie jak w Charonie'), ('-50', 'slow', 'wolne'), ('0', 'normal', 'normalne'), ('25', 'faster', 'szybsze'), ('50', 'fast', 'szybkie'), ('75', 'very fast', 'bardzo szybkie')],
  apply='DefaultRate in /etc/speech-dispatcher/speechd.conf', src='L8')
Q('sd_pitch', 'speech', LINUX, 'choice', 'Voice pitch:', 'Wysokość głosu:', 'charon',
  [('charon', 'as in Charon', 'taka jak w Charonie'), ('-50', 'low', 'niska'), ('0', 'normal', 'normalna'), ('50', 'high', 'wysoka')],
  apply='DefaultPitch in speechd.conf', src='L8')
Q('sd_volume', 'speech', LINUX, 'choice', 'Speech volume:', 'Głośność mowy:', 'keep',
  [KEEP, ('0', 'half', 'połowa'), ('50', 'three quarters', 'trzy czwarte'), ('100', 'full', 'pełna')], apply='DefaultVolume in speechd.conf', src='L8')
Q('sd_punct', 'speech', LINUX, 'choice', 'Punctuation spoken:', 'Wymawiana interpunkcja:', 'charon',
  [('charon', 'as in Charon', 'tak jak w Charonie'), ('none', 'none', 'żadna'), ('some', 'some', 'część'), ('all', 'all', 'cała')],
  apply='DefaultPunctuationMode in speechd.conf', src='L8')
Q('sd_caps', 'speech', LINUX, 'choice', 'Capital letters:', 'Wielkie litery:', 'keep',
  [KEEP, ('none', 'no signal', 'bez sygnału'), ('icon', 'a sound', 'dźwięk'), ('spell', 'the word "capital"', 'słowo „duże”')],
  apply='DefaultCapLetRecognition in speechd.conf', src='L8')
Q('braille', 'speech', LINUX, 'choice', 'Braille display:', 'Monitor brajlowski:', 'keep',
  [KEEP, ('auto', 'install BRLTTY and detect the display automatically', 'zainstaluj BRLTTY i wykryj monitor automatycznie')],
  apply='package brltty, braille-driver auto in /etc/brltty.conf, service enabled', src='L9')
Q('braille_table', 'speech', LINUX, 'choice', 'Braille table:', 'Tablica brajlowska:', 'auto',
  [('auto', 'by the system language', 'według języka systemu'), ('pl', 'Polish', 'polska')],
  when='detailed::Is("braille","auto")', apply='text-table in /etc/brltty.conf', src='L9')
Q('flatpak', 'desk', LINUX, 'yesNo', 'Add Flatpak and the Flathub repository?', 'Dodać Flatpak i repozytorium Flathub?', 'no',
  apply='package flatpak; flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo', src='L10')
Q('console_font', 'desk', LINUX, 'yesNo', 'Large console font for low vision?', 'Duża czcionka konsoli dla słabowidzących?', 'no',
  apply='FONTFACE/FONTSIZE in /etc/default/console-setup (Debian, Ubuntu); FONT in /etc/vconsole.conf (Arch, Fedora)', src='L26')
# Desktop settings. desks: the desktops that HAVE the setting (g GNOME, k KDE Plasma, m MATE, x Xfce, c Cinnamon);
# the question is asked only when one of them was chosen. LXQt and i3 have none of these settings as a system
# default (work/r410a-evidence/verify-desktops.txt, matrix), so nothing of this group is asked for them.
def D(key, desks, kind, en, pl, default, choices=(), apply='', src='DB', **more):
    Q(key, 'look', LINUX, kind, en, pl, default, choices, when='detailed::DeskAny("%s")' % desks, apply=apply, src=src, **more)
    ROWS[-1]['desks'] = desks


D('contrast', 'g', 'yesNo', 'High contrast?', 'Wysoki kontrast?', 'no', apply='GNOME org.gnome.desktop.a11y.interface high-contrast (MATE, Xfce, KDE: only theme names, not verified; Cinnamon: key marked unused)')
D('text_size', 'gcmx', 'choice', 'Text size:', 'Rozmiar tekstu:', '1.0',
  [('1.0', '100 percent', '100 procent'), ('1.25', '125 percent', '125 procent'), ('1.5', '150 percent', '150 procent'), ('2.0', '200 percent', '200 procent')],
  apply='GNOME org.gnome.desktop.interface text-scaling-factor; Cinnamon org.cinnamon.desktop.interface text-scaling-factor; MATE org.mate.font-rendering dpi; Xfce xsettings /Xft/DPI (96 x factor)')
D('dark', 'gc', 'yesNo', 'Dark theme?', 'Ciemny motyw?', 'no',
  apply="GNOME org.gnome.desktop.interface color-scheme 'prefer-dark'; Cinnamon org.x.apps.portal color-scheme; KDE /etc/xdg/kdeglobals [General] ColorScheme=BreezeDark")
D('sounds', 'gmcxk', 'yesNo', 'System event sounds?', 'Dźwięki zdarzeń systemu?', 'yes',
  apply='event-sounds false in org.gnome.desktop.sound / org.mate.sound / org.cinnamon.desktop.sound; Xfce xsettings /Net/EnableEventSounds; KDE kdeglobals [Sounds] Enable=false')
D('clock_fmt', 'gc', 'choice', 'Clock:', 'Zegar:', 'keep', [KEEP, ('24h', '24-hour', '24-godzinny'), ('12h', '12-hour', '12-godzinny')],
  apply='GNOME org.gnome.desktop.interface clock-format; Cinnamon org.cinnamon.desktop.interface clock-use-24h (MATE: per-applet schema, KDE: per-applet key: no system default)')
D('numlock', 'mc', 'choice', 'Num Lock at sign-in:', 'Num Lock przy logowaniu:', 'keep',
  [KEEP, ('off', 'off: the keypad is for the reader\'s commands', 'wyłączony: klawiatura numeryczna służy poleceniom czytnika'), ('on', 'on', 'włączony')],
  apply='numlock-state (+ remember-numlock-state false) in org.gnome.desktop.peripherals.keyboard / org.cinnamon.desktop.peripherals.keyboard; MATE org.mate.peripherals-keyboard numlock-state')
D('sleep_ac', 'gmck', 'choice', 'On mains power, sleep:', 'Na zasilaniu sieciowym usypiaj:', 'keep',
  [KEEP, ('0', 'never', 'nigdy'), ('15', 'after 15 minutes', 'po 15 minutach'), ('30', 'after 30 minutes', 'po 30 minutach'), ('60', 'after an hour', 'po godzinie')],
  apply='GNOME, Cinnamon ...settings-daemon.plugins.power sleep-inactive-ac-timeout (+ -type); MATE org.mate.power-manager sleep-computer-ac; KDE /etc/xdg/powerdevilrc [AC][SuspendAndShutdown]')
D('screen_lock', 'gmc', 'choice', 'Lock the screen when it goes blank:', 'Blokuj ekran, gdy się wygasza:', 'keep',
  [KEEP, ('off', 'no', 'nie')], risk='off', apply='lock-enabled false in org.gnome.desktop.screensaver / org.mate.screensaver / org.cinnamon.desktop.screensaver')
D('magnifier', 'g', 'yesNo', 'Start the screen magnifier at sign-in?', 'Uruchamiać lupę przy logowaniu?', 'no',
  apply='GNOME org.gnome.desktop.a11y.applications screen-magnifier-enabled (no such system setting on the other desktops)')
D('login_sound', 'c', 'yesNo', 'Play a sound at sign-in to the desktop?', 'Odtwarzać dźwięk przy logowaniu do pulpitu?', 'no',
  apply='Cinnamon org.cinnamon.sounds login-enabled (GNOME, MATE, Xfce, KDE: no such key)')
Q('firewall', 'net', LINUX, 'choice', 'Firewall:', 'Zapora sieciowa:', 'keep',
  [KEEP, ('on', 'on: incoming connections blocked, except SSH when you install its server', 'włączona: połączenia przychodzące blokowane, poza SSH, gdy instalujesz jego serwer')],
  apply='ufw (Debian, Ubuntu, Arch): default deny incoming, allow outgoing, allow ssh when ssh=yes, enable; Fedora: firewalld enabled', src='L11')
Q('ssh_pw', 'net', LINUX, 'choice', 'SSH sign-in with a password:', 'Logowanie SSH hasłem:', 'keep',
  [KEEP, ('no', 'refuse it: keys only', 'odmawiaj: tylko klucze')], when='detailed::Is("ssh","yes")', risk='no',
  apply='/etc/ssh/sshd_config.d/90-charon.conf PasswordAuthentication no', src='L20')
Q('auto_updates', 'upd', 'dDuUF', 'choice', 'Automatic updates:', 'Automatyczne aktualizacje:', 'keep',
  [KEEP, ('security', 'install security updates automatically', 'instaluj automatycznie aktualizacje zabezpieczeń'), ('all', 'install all updates automatically', 'instaluj automatycznie wszystkie aktualizacje')],
  apply='Debian, Ubuntu: unattended-upgrades + /etc/apt/apt.conf.d/20auto-upgrades (+ 52charon-origins for "all"); Fedora: dnf automatic plugin, upgrade_type, apply_updates, timer', src='L12')
Q('auto_reboot', 'upd', 'dDuUF', 'yesNo', 'May the computer restart by itself after updates?', 'Czy komputer może sam się uruchomić ponownie po aktualizacjach?', 'no',
  when='!detailed::Is("auto_updates","keep")', apply='Unattended-Upgrade::Automatic-Reboot "true" / dnf automatic reboot = when-needed', src='L12')
Q('lid', 'hw', LINUX, 'choice', 'Closing the laptop lid:', 'Zamknięcie pokrywy laptopa:', 'keep',
  [KEEP, ('suspend', 'sleep', 'uśpij'), ('ignore', 'do nothing: keep working and speaking', 'nic nie rób: pracuj i mów dalej'), ('poweroff', 'shut down', 'wyłącz')],
  apply='HandleLidSwitch and HandleLidSwitchExternalPower in /etc/systemd/logind.conf.d/90-charon.conf', src='L15')
Q('tlp', 'hw', LINUX, 'yesNo', 'Laptop battery saving with TLP?', 'Oszczędzanie baterii laptopa programem TLP?', 'no',
  apply='package tlp (it replaces power-profiles-daemon), service tlp enabled, systemd-rfkill masked, as TLP\'s documentation says', src='L13')
Q('bluetooth', 'hw', LINUX, 'choice', 'Bluetooth at start-up:', 'Bluetooth po uruchomieniu:', 'keep',
  [KEEP, ('on', 'on', 'włączony'), ('off', 'off', 'wyłączony')], apply='[Policy] AutoEnable in /etc/bluetooth/main.conf', src='L16')
Q('printer', 'hw', LINUX, 'choice', 'Printing:', 'Drukowanie:', 'keep',
  [KEEP, ('install', 'install printing and find network printers automatically', 'zainstaluj drukowanie i automatycznie znajduj drukarki sieciowe')],
  apply='packages cups and cups-browsed, services enabled', src='L14')

# ---------------------------------------------------------------------------------- Windows
NVDA = 'detailed::Has("wreaders","nvda")'
Q('wtimezone', 'lang', 'w', 'choice', 'Windows time zone:', 'Strefa czasowa Windows:', 'keep',
  [('keep', 'as Windows sets it for the language', 'tak, jak Windows ustawia dla języka'), ('Central European Standard Time', 'Warsaw', 'Warszawa'), ('GMT Standard Time', 'London', 'Londyn'),
   ('W. Europe Standard Time', 'Berlin', 'Berlin'), ('Romance Standard Time', 'Paris, Madrid', 'Paryż, Madryt'), ('FLE Standard Time', 'Kyiv', 'Kijów'), ('Central Europe Standard Time', 'Prague', 'Praga'),
   ('Eastern Standard Time', 'New York', 'Nowy Jork'), ('Central Standard Time', 'Chicago', 'Chicago'), ('Mountain Standard Time', 'Denver', 'Denver'),
   ('Pacific Standard Time', 'Los Angeles', 'Los Angeles'), ('UTC', 'UTC', 'UTC')], apply='tzutil /s', src='W1')
Q('w_bootmenu', 'boot', 'w', 'choice', 'Windows boot menu:', 'Menu rozruchu Windows:', 'text',
  [('text', 'text menu, which the Charon reader reads', 'menu tekstowe, które czyta czytnik Charona'),
   ('tile', 'tile menu: it is drawn after the firmware has gone, no reader can read it', 'menu kafelkowe: jest rysowane po wyjściu z firmware, żaden czytnik go nie odczyta')],
  risk='tile', apply='bcdedit /set {current} bootmenupolicy legacy (default, also in the express mode: a first-logon command of the answer file) / standard', src='DA')
Q('w_hibernate', 'boot', 'w', 'choice', 'Hibernation:', 'Hibernacja:', 'keep',
  [KEEP, ('on', 'on', 'włączona'), ('off', 'off: fast startup goes off with it', 'wyłączona: razem z nią szybkie uruchamianie')], apply='powercfg /hibernate on|off', src='W4')
Q('fast_startup', 'boot', 'w', 'choice', 'Fast startup (must be off when another system uses the same disk):', 'Szybkie uruchamianie (musi być wyłączone, gdy z tego dysku korzysta inny system):', 'keep',
  [KEEP, ('off', 'off', 'wyłączone')], apply='HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power HiberbootEnabled=0', src='W5')
Q('w_punct', 'speech', 'w', 'choice', 'NVDA: punctuation and symbols:', 'NVDA: interpunkcja i symbole:', '100',
  [('100', 'some', 'część'), ('0', 'none', 'żadne'), ('200', 'most', 'większość'), ('300', 'all', 'wszystkie')], when=NVDA, apply='nvda.ini [speech] symbolLevel', src='W25')
Q('w_layout', 'speech', 'w', 'choice', 'NVDA keyboard layout:', 'Układ klawiatury NVDA:', 'desktop',
  [('desktop', 'desktop', 'desktop'), ('laptop', 'laptop', 'laptop')], when=NVDA, apply='nvda.ini [keyboard] keyboardLayout', src='W25')
Q('w_modifier', 'speech', 'w', 'multi', 'NVDA key:', 'Klawisz NVDA:', 'numpad,extended',
  [('numpad', 'numeric-keypad Insert', 'Insert na klawiaturze numerycznej'), ('extended', 'extended Insert', 'Insert rozszerzony'), ('capslock', 'Caps Lock', 'Caps Lock')],
  when=NVDA, risk='', apply='nvda.ini [keyboard] NVDAModifierKeys (bit mask)', src='W25')
Q('w_typed_chars', 'speech', 'w', 'choice', 'NVDA: speak typed characters:', 'NVDA: wymawiaj wpisywane znaki:', '1',
  [('1', 'only in edit fields', 'tylko w polach edycji'), ('0', 'never', 'nigdy'), ('2', 'always', 'zawsze')], when=NVDA, apply='nvda.ini [keyboard] speakTypedCharacters (integer 0-2)', src='W25')
Q('w_typed_words', 'speech', 'w', 'choice', 'NVDA: speak typed words:', 'NVDA: wymawiaj wpisywane słowa:', '0',
  [('0', 'never', 'nigdy'), ('1', 'only in edit fields', 'tylko w polach edycji'), ('2', 'always', 'zawsze')], when=NVDA, apply='nvda.ini [keyboard] speakTypedWords (integer 0-2)', src='W25')
Q('w_autolang', 'speech', 'w', 'yesNo', 'NVDA: switch the voice language automatically with the text?', 'NVDA: przełączać język głosu automatycznie według tekstu?', 'yes',
  when=NVDA, apply='nvda.ini [speech] autoLanguageSwitching', src='W25')
Q('w_braille', 'speech', 'w', 'choice', 'NVDA: braille display:', 'NVDA: monitor brajlowski:', 'auto',
  [('auto', 'detect automatically', 'wykrywaj automatycznie'), ('noBraille', 'none', 'brak')], when=NVDA, apply='nvda.ini [braille] display', src='W25')
Q('w_ducking', 'speech', 'w', 'choice', 'NVDA: lower other sounds while it speaks?', 'NVDA: ściszać inne dźwięki, gdy mówi?', '0',
  [('0', 'no', 'nie'), ('1', 'while speaking', 'podczas mówienia'), ('2', 'always', 'zawsze')], when=NVDA, apply='nvda.ini [audio] audioDuckingMode', src='W25')
Q('w_nvda_update', 'speech', 'w', 'yesNo', 'Let NVDA check for updates?', 'Pozwolić NVDA sprawdzać aktualizacje?', 'yes', when=NVDA, apply='nvda.ini [update] autoCheck', src='W25')
Q('w_nvda_stats', 'speech', 'w', 'yesNo', 'Send anonymous usage statistics to NV Access?', 'Wysyłać anonimowe statystyki użycia do NV Access?', 'no', when=NVDA,
  apply='nvda.ini [update] allowUsageStats, askedAllowUsageStats', src='W25')
Q('w_nvda_welcome', 'speech', 'w', 'yesNo', 'Show NVDA\'s welcome dialog at its first start?', 'Pokazać okno powitalne NVDA przy pierwszym uruchomieniu?', 'yes', when=NVDA,
  apply='nvda.ini [general] showWelcomeDialogAtStartup', src='W25')
Q('w_ext', 'look', 'w', 'yesNo', 'Show file name extensions?', 'Pokazywać rozszerzenia nazw plików?', 'no', apply='UNOFFICIAL (not documented by Microsoft; named in the sources of Win11Debloat and WinUtil; user decision 2026-11-24: in the build only when a VM run shows the effect): HKCU ...\\Explorer\\Advanced HideFileExt=0', src='W9')
Q('w_hidden', 'look', 'w', 'yesNo', 'Show hidden files?', 'Pokazywać ukryte pliki?', 'no', apply='UNOFFICIAL (not documented by Microsoft; named in the sources of Win11Debloat and WinUtil; user decision 2026-11-24: in the build only when a VM run shows the effect): HKCU ...\\Explorer\\Advanced Hidden=1', src='W9')
Q('w_explorer_start', 'look', 'w', 'choice', 'File Explorer opens:', 'Eksplorator plików otwiera:', 'keep',
  [KEEP, ('pc', 'This PC', 'Ten komputer')], apply='UNOFFICIAL (not documented by Microsoft; named in the sources of Win11Debloat and WinUtil; user decision 2026-11-24: in the build only when a VM run shows the effect): HKCU ...\\Explorer\\Advanced LaunchTo=1', src='W9')
Q('w_taskbar_left', 'look', 'w', 'yesNo', 'Start button on the left of the taskbar?', 'Przycisk Start po lewej stronie paska zadań?', 'no', apply='HKCU ...\\Explorer\\Advanced TaskbarAl=0', src='W9')
Q('w_dark', 'look', 'w', 'yesNo', 'Dark theme?', 'Ciemny motyw?', 'no', apply='HKCU ...\\Themes\\Personalize AppsUseLightTheme=0, SystemUsesLightTheme=0', src='W10')
Q('w_start_sound', 'look', 'w', 'choice', 'Windows start-up sound:', 'Dźwięk startu Windows:', 'keep',
  [KEEP, ('on', 'play it', 'odtwarzaj'), ('off', 'do not play it', 'nie odtwarzaj')], apply='UNOFFICIAL (not documented by Microsoft; named in the sources of Win11Debloat and WinUtil; user decision 2026-11-24: in the build only when a VM run shows the effect): HKLM ...\\Authentication\\LogonUI\\BootAnimation DisableStartupSound and ...\\EditionOverrides UserSetting_DisableStartupSound', src='W11')
Q('w_numlock', 'look', 'w', 'choice', 'Num Lock at sign-in:', 'Num Lock przy logowaniu:', 'keep',
  [KEEP, ('off', 'off: the keypad moves NVDA\'s review cursor', 'wyłączony: klawiatura numeryczna steruje kursorem przeglądu NVDA'), ('on', 'on', 'włączony')],
  apply='UNOFFICIAL (not documented by Microsoft; named in the sources of Win11Debloat and WinUtil; user decision 2026-11-24: in the build only when a VM run shows the effect): HKU\\.DEFAULT\\Control Panel\\Keyboard InitialKeyboardIndicators (0 off, 2 on), also for the user and the default user', src='W12')
Q('w_ssh', 'net', 'w', 'yesNo', 'Install the OpenSSH server in Windows?', 'Zainstalować serwer OpenSSH w Windows?', 'no', risk='yes',
  apply='Add-WindowsCapability OpenSSH.Server~~~~0.0.1.0; sshd automatic and started', src='W16')
Q('w_netprofile', 'net', 'w', 'choice', 'The first network is:', 'Pierwsza sieć jest:', 'keep',
  [KEEP, ('private', 'private: the computer is visible to others in it', 'prywatna: komputer jest w niej widoczny dla innych')], apply='Set-NetConnectionProfile -NetworkCategory Private', src='W17')
Q('w_update', 'upd', 'w', 'choice', 'Windows Update:', 'Windows Update:', 'keep',
  [('keep', 'automatic, as Windows sets it', 'automatycznie, tak jak ustawia Windows'), ('2', 'tell me before downloading', 'powiadom przed pobraniem'),
   ('3', 'download, tell me before installing', 'pobierz, powiadom przed instalacją'), ('off', 'off', 'wyłączone')],
  risk='off', apply='HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU AUOptions / NoAutoUpdate', src='W6')
Q('w_update_restart', 'upd', 'w', 'yesNo', 'Never restart for updates while someone is signed in?', 'Nigdy nie restartować dla aktualizacji, gdy ktoś jest zalogowany?', 'no',
  apply='...\\WindowsUpdate\\AU NoAutoRebootWithLoggedOnUsers=1', src='W6')
Q('w_adid', 'upd', 'w', 'yesNo', 'Turn off the advertising ID?', 'Wyłączyć identyfikator reklamowy?', 'no',
  apply='HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\AdvertisingInfo DisabledByGroupPolicy=1', src='W8')
Q('w_recall', 'upd', 'w', 'yesNo', 'Turn off Recall snapshots?', 'Wyłączyć migawki funkcji Recall?', 'no',
  apply='HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsAI DisableAIDataAnalysis=1', src='W8')
Q('w_widgets', 'upd', 'w', 'yesNo', 'Turn off Widgets (news and interests)?', 'Wyłączyć Widżety (wiadomości i zainteresowania)?', 'no',
  apply='HKLM\\SOFTWARE\\Policies\\Microsoft\\Dsh AllowNewsAndInterests=0', src='W8')
Q('w_onedrive', 'upd', 'w', 'choice', 'OneDrive:', 'OneDrive:', 'keep', [KEEP, ('remove', 'remove', 'usuń')], apply='UNOFFICIAL (not documented by Microsoft; named in the sources of Win11Debloat and WinUtil; user decision 2026-11-24: in the build only when a VM run shows the effect): OneDriveSetup.exe /uninstall (System32 or SysWOW64); reported as skipped when Windows has no OneDrive setup program', src='W22')
SLEEP = [KEEP, ('0', 'never', 'nigdy'), ('15', 'after 15 minutes', 'po 15 minutach'), ('30', 'after 30 minutes', 'po 30 minutach'), ('60', 'after an hour', 'po godzinie'), ('120', 'after two hours', 'po dwóch godzinach')]
Q('w_sleep_ac', 'hw', 'w', 'choice', 'On mains power, sleep:', 'Na zasilaniu sieciowym usypiaj:', 'keep', SLEEP, apply='powercfg /change standby-timeout-ac N', src='W4')
Q('w_sleep_dc', 'hw', 'w', 'choice', 'On battery, sleep:', 'Na baterii usypiaj:', 'keep', SLEEP, apply='powercfg /change standby-timeout-dc N', src='W4')
Q('w_display_off', 'hw', 'w', 'choice', 'Turn the display off:', 'Wyłączaj ekran:', 'keep',
  [KEEP, ('1', 'after a minute', 'po minucie'), ('10', 'after 10 minutes', 'po 10 minutach'), ('0', 'never', 'nigdy')], apply='powercfg /change monitor-timeout-ac N and -dc N', src='W4')
Q('w_lid', 'hw', 'w', 'choice', 'Closing the laptop lid:', 'Zamknięcie pokrywy laptopa:', 'keep',
  [KEEP, ('1', 'sleep', 'uśpij'), ('0', 'do nothing: keep working and speaking', 'nic nie rób: pracuj i mów dalej'), ('2', 'hibernate', 'hibernuj'), ('3', 'shut down', 'wyłącz')],
  apply='powercfg /setacvalueindex and /setdcvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION n; /setactive SCHEME_CURRENT', src='W4')
Q('w_power_plan', 'hw', 'w', 'choice', 'Power plan:', 'Plan zasilania:', 'keep',
  [KEEP, ('SCHEME_BALANCED', 'balanced', 'zrównoważony'), ('SCHEME_MIN', 'high performance', 'wysoka wydajność'), ('SCHEME_MAX', 'power saver', 'oszczędzanie energii')],
  apply='powercfg /setactive ALIAS', src='W4')
Q('w_features', 'hw', 'w', 'multi', 'Optional Windows features:', 'Opcjonalne funkcje Windows:', '',
  [('wsl', 'Windows Subsystem for Linux', 'Podsystem Windows dla Linuksa'), ('netfx3', '.NET Framework 3.5', '.NET Framework 3.5'),
   ('sandbox', 'Windows Sandbox', 'Piaskownica Windows'), ('hyperv', 'Hyper-V', 'Hyper-V')],
  apply='dism /online /enable-feature /featurename:... /all /norestart (Microsoft-Windows-Subsystem-Linux + VirtualMachinePlatform, NetFx3, Containers-DisposableClientVM, Microsoft-Hyper-V)', src='W18')


# ---------------------------------------------------------------------------------- r410 (helper es): the remaining EASY rows
# Sources: E<n> = item n of work/r410a-evidence/es/verify-es.txt (URLs of the primary sources there).
LOCALES = [('pl_PL', 'Polish', 'polski'), ('en_US', 'English (United States)', 'angielski (Stany Zjednoczone)'), ('en_GB', 'English (United Kingdom)', 'angielski (Wielka Brytania)'),
           ('de_DE', 'German', 'niemiecki'), ('fr_FR', 'French', 'francuski'), ('es_ES', 'Spanish', 'hiszpański'), ('it_IT', 'Italian', 'włoski'),
           ('uk_UA', 'Ukrainian', 'ukraiński'), ('cs_CZ', 'Czech', 'czeski')]
Q('extra_langs', 'lang', LINUX, 'multi', 'Additional languages to install:', 'Dodatkowe języki do zainstalowania:', '', LOCALES,
  apply='each locale generated (locale-gen on Debian, Ubuntu, Arch; package glibc-langpack-XX on Fedora); the system language stays', src='L1')
Q('tz_other', 'lang', LINUX, 'text', 'Time zone by its name, for example Europe/Vienna (empty: as chosen before):', 'Strefa czasowa po nazwie, na przykład Europe/Vienna (puste: tak jak wybrano wcześniej):', '',
  apply='timedatectl set-timezone NAME; a name without /usr/share/zoneinfo/NAME is refused and reported', src='E1')
Q('speech_lang', 'speech', LINUX, 'choice', 'Speech language of the desktop reader:', 'Język mowy czytnika pulpitu:', 'same',
  [('same', 'the same as the system language', 'taki jak język systemu')] + [(c[0].replace('_', '-'), c[1].split(' (')[0], c[2].split(' (')[0]) for c in LOCALES if c[0] not in ('en_GB',)],
  apply='DefaultLanguage in /etc/speech-dispatcher/speechd.conf', src='L8, E2')
Q('components', 'desk', 'dD', 'multi', 'Debian package sources besides main and the firmware:', 'Źródła pakietów Debiana poza main i firmware:', '',
  [('contrib', 'contrib', 'contrib'), ('non-free', 'non-free: programs that are not free software', 'non-free: programy, które nie są wolnym oprogramowaniem')],
  apply='the components added to the Debian entries of /etc/apt/sources.list and /etc/apt/sources.list.d/*.sources, then apt-get update', src='E3')
Q('u_mirror', 'desk', 'uU', 'choice', 'Ubuntu package server:', 'Serwer pakietów Ubuntu:', 'keep',
  [KEEP, ('main', 'the main archive, archive.ubuntu.com', 'główne archiwum, archive.ubuntu.com')],
  apply='URIs of the Ubuntu archive entries in /etc/apt/sources.list.d/ubuntu.sources (and sources.list) set to http://archive.ubuntu.com/ubuntu/ (security entries kept)', src='E4', hidden='no VM read-back yet (helper es ran Debian and Windows, no Ubuntu path)')
Q('u_codecs', 'desk', 'uU', 'yesNo', 'Install media codecs (ubuntu-restricted-addons)?', 'Zainstalować kodeki multimedialne (ubuntu-restricted-addons)?', 'no',
  apply='package ubuntu-restricted-addons (what the autoinstall key codecs installs)', src='E5', hidden='no VM read-back yet (helper es ran Debian and Windows, no Ubuntu path)')
Q('u_drivers', 'hw', 'uU', 'yesNo', 'Install the third-party drivers this hardware needs (ubuntu-drivers)?', 'Zainstalować sterowniki producentów, których wymaga ten sprzęt (ubuntu-drivers)?', 'no',
  apply='package ubuntu-drivers-common, then ubuntu-drivers install', src='E5', hidden='no VM read-back yet (helper es ran Debian and Windows, no Ubuntu path)')
Q('upgrade_now', 'upd', LINUX, 'yesNo', 'Upgrade all packages at the first start?', 'Zaktualizować wszystkie pakiety przy pierwszym uruchomieniu?', 'no',
  apply='apt-get -y dist-upgrade (Debian, Ubuntu), dnf -y upgrade (Fedora), pacman -Syu --noconfirm (Arch)', src='X1-X3')
Q('popcon', 'upd', 'dD', 'yesNo', "Take part in Debian's package popularity survey?", 'Brać udział w ankiecie popularności pakietów Debiana?', 'no',
  apply='package popularity-contest; PARTICIPATE="yes" in /etc/popularity-contest.conf', src='E6')
Q('ssh_key', 'net', LINUX, 'text', 'Public SSH key to accept for your account (one line):', 'Klucz publiczny SSH akceptowany dla twojego konta (jeden wiersz):', '',
  when='detailed::Is("ssh","yes")', apply='appended to ~USER/.ssh/authorized_keys (directory 700, file 600, owned by the user); a line that is not a public key is refused', src='E14')
Q('help_tool', 'net', LINUX, 'yesNo', 'Install tmate, to share your terminal with a person you trust when you start it?', 'Zainstalować tmate, aby udostępniać terminal zaufanej osobie, gdy go uruchomisz?', 'no',
  apply='package tmate (nothing listens until the user starts it)', src='E7')
Q('soft_file', 'desk', LINUX, 'yesNo', 'Install the programs of your list file software.txt?', 'Zainstalować programy z twojego pliku listy software.txt?', 'no',
  when='detailed::SoftFile()', apply='EFI/HAK/catalogue/software.txt on the Charon medium (copied with the catalogue): catalogue ids and apt: dnf: pacman: flatpak: lines; lines for another system and unsupported prefixes are reported', src='spec 9.3')
Q('w_key', 'hw', 'w', 'text', 'Windows product key (empty: none):', 'Klucz produktu Windows (puste: brak):', '', secret=True,
  apply='slmgr.vbs /ipk KEY, then /cpky (the key is removed from the registry); never logged; its line is removed from answers.ini after use', src='W3', hidden='VM run es2: the evaluation edition refuses a volume key (slmgr /ipk 0xC004F069, reported as failed, key never logged, its line removed from answers.ini); no successful read-back yet')
Q('w_ssh_key', 'net', 'w', 'text', 'Public SSH key to accept (one line):', 'Klucz publiczny SSH do zaakceptowania (jeden wiersz):', '',
  when='detailed::Is("w_ssh","yes")', apply='%ProgramData%\\ssh\\administrators_authorized_keys with the permissions Microsoft gives (icacls: Administrators and SYSTEM only)', src='W4')
Q('w_help_tool', 'net', 'w', 'yesNo', 'Install TeamTalk 5, for remote help by voice from a person you trust?', 'Zainstalować TeamTalk 5, do zdalnej pomocy głosowej od zaufanej osoby?', 'no',
  apply='winget install --id BearWare.dk.TeamTalk5 -e', src='W5')
Q('w_soft_file', 'desk', 'w', 'yesNo', 'Install the programs of your list file software.txt?', 'Zainstalować programy z twojego pliku listy software.txt?', 'no',
  when='detailed::SoftFile()', apply='EFI\\HAK\\catalogue\\software.txt: catalogue ids and winget: lines; lines for another system and unsupported prefixes are reported', src='spec 9.3')

# ---------------------------------------------------------------------------------- r410 (helper es): block E, the administrator layer
# level 1: asked only after "Show the administration groups" (or set by a site file). Windows keys carry w_ where Linux has
# the same row; a site file's [shared] key reaches Windows under its w_ name (CharonWsAnswerSets.h LoadSite).
ADM = dict(level=1)
SITE = 'a file in EFI/HAK/site on the Charon medium (copied with Charon into the installed system)'
Q('users', 'adm', LINUX, 'text', 'More accounts, as name:admin or name:standard, separated by commas:', 'Kolejne konta, jako nazwa:admin albo nazwa:standard, rozdzielone przecinkami:', '', **ADM,
  apply='useradd -m; admin: group sudo (Debian, Ubuntu) or wheel (Fedora, Arch); no password, which must be set at the first sign-in (passwd -d, chage -d 0)', src='X7')
Q('net_static', 'adm', LINUX, 'text', 'Fixed address as ADDRESS/PREFIX GATEWAY DNS[,DNS] (empty: automatic):', 'Stały adres jako ADRES/PREFIKS BRAMA DNS[,DNS] (puste: automatyczny):', '', **ADM,
  apply='NetworkManager: nmcli connection modify of the wired connection (ipv4.method manual); without NetworkManager: /etc/network/interfaces (ifupdown) or a netplan file', src='X8, X9')
Q('proxy', 'adm', LINUX, 'text', 'Proxy server for packages and programs, http://host:port:', 'Serwer proxy dla pakietów i programów, http://host:port:', '', **ADM,
  apply='apt: Acquire::http::Proxy and Acquire::https::Proxy in /etc/apt/apt.conf.d/90charon-proxy; dnf: proxy= in /etc/dnf/dnf.conf; all: http_proxy https_proxy in /etc/environment', src='X4, X5')
Q('ntp_server', 'adm', LINUX, 'text', 'Time server:', 'Serwer czasu:', '', **ADM,
  apply='chrony: server NAME iburst in /etc/chrony/sources.d/charon.sources (or /etc/chrony.conf); systemd-timesyncd: NTP= in /etc/systemd/timesyncd.conf.d/90-charon.conf', src='E8')
Q('repo_mirror', 'adm', LINUX, 'text', 'Internal package server (address of the mirror of this distribution):', 'Wewnętrzny serwer pakietów (adres kopii tej dystrybucji):', '', **ADM,
  apply='Debian, Ubuntu: the archive entries of the apt sources point to it (security entries kept); Arch: first Server line of /etc/pacman.d/mirrorlist; Fedora: reported as not applied (metalink repositories)', src='E3, E4, E9')
Q('ssh_keys_admin', 'adm', LINUX, 'text', 'Administrator SSH keys, separated by semicolons (password sign-in over SSH is then refused):', 'Klucze SSH administratora, rozdzielone średnikami (logowanie SSH hasłem zostaje wtedy zablokowane):', '', **ADM,
  when='detailed::Is("ssh","yes")', apply='appended to ~USER/.ssh/authorized_keys; PasswordAuthentication no in /etc/ssh/sshd_config.d/00-charon.conf', src='E14, L20')
Q('first_msg', 'adm', LINUX, 'text', 'Message shown and spoken at the first sign-in:', 'Komunikat pokazany i odczytany przy pierwszym logowaniu:', '', **ADM,
  apply='/etc/motd (shown at a console sign-in) and /etc/xdg/autostart/charon-first-msg.desktop, which speaks it once per user with spd-say', src='E11, E12')
Q('asset_tag', 'adm', LINUX, 'text', 'Asset tag to record:', 'Numer inwentarzowy do zapisania:', '', **ADM,
  apply='written to the applied-settings report /var/lib/charon/install-report.txt (machine-info has no field for it)', src='-')
Q('root_ca', 'adm', LINUX, 'text', 'Certificates to trust: file names in EFI/HAK/site, separated by commas:', 'Certyfikaty do zaufania: nazwy plików w EFI/HAK/site, rozdzielone przecinkami:', '', **ADM,
  apply='Debian, Ubuntu: /usr/local/share/ca-certificates/NAME.crt + update-ca-certificates; Arch: /etc/ca-certificates/trust-source/anchors + update-ca-trust; Fedora: reported as not applied (its anchors folder was not verified)', src='E10')
Q('post_script', 'adm', LINUX, 'text', 'Script to run last, as FILE.sh sha256=HASH (the file in EFI/HAK/site):', 'Skrypt uruchamiany na końcu, jako PLIK.sh sha256=SKRÓT (plik w EFI/HAK/site):', '', **ADM,
  apply='copied to /var/lib/charon/post/, run as root only when its SHA-256 matches; its exit status in the report, its output in personal.log', src='X12')
Q('phone_home', 'adm', LINUX, 'text', 'Report the finished installation to this address (http or https):', 'Zgłoś zakończoną instalację pod ten adres (http albo https):', '', **ADM,
  apply='curl -X POST of the applied-settings report (no secrets in it) after every other answer', src='-')
Q('w_users', 'adm', 'w', 'text', 'More accounts, as name:admin or name:standard, separated by commas:', 'Kolejne konta, jako nazwa:admin albo nazwa:standard, rozdzielone przecinkami:', '', **ADM,
  apply='net user NAME "" /add /logonpasswordchg:yes (the password is set at the first sign-in); admin: Add-LocalGroupMember Administrators (/logonpasswordchg is not documented by Microsoft and New-LocalUser has no must-change switch: out of the build)', src='X11', verified=False)
Q('w_net_static', 'adm', 'w', 'text', 'Fixed address as ADDRESS/PREFIX GATEWAY DNS[,DNS] (empty: automatic):', 'Stały adres jako ADRES/PREFIKS BRAMA DNS[,DNS] (puste: automatyczny):', '', **ADM,
  apply='New-NetIPAddress and Set-DnsClientServerAddress on the connected adapter', src='X10')
Q('w_proxy', 'adm', 'w', 'text', 'Proxy server, host:port:', 'Serwer proxy, host:port:', '', **ADM, apply='netsh winhttp set proxy proxy-server="host:port" (documented; Microsoft marks it deprecated in favour of set advproxy)', src='X6')
Q('w_ntp_server', 'adm', 'w', 'text', 'Time server:', 'Serwer czasu:', '', **ADM, apply='w32tm /config /manualpeerlist:NAME /syncfromflags:manual /update', src='W8')
Q('wsus', 'adm', 'w', 'text', 'WSUS server address (http://host:8530):', 'Adres serwera WSUS (http://host:8530):', '', **ADM,
  apply='HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate WUServer, WUStatusServer; ...\\AU UseWUServer=1', src='W6')
Q('no_wu_drivers', 'adm', 'w', 'yesNo', 'Exclude drivers from Windows Update (Charon relies on it for sound and network drivers)?', 'Wyłączyć sterowniki z Windows Update (Charon liczy na nie przy sterownikach dźwięku i sieci)?', 'no', **ADM,
  risk='yes', apply='HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate ExcludeWUDriversInQualityUpdate=1', src='W7')
Q('kms', 'adm', 'w', 'text', 'KMS host for volume activation (host or host:port):', 'Serwer KMS do aktywacji zbiorczej (host albo host:port):', '', **ADM,
  apply='slmgr.vbs /skms HOST (the volume licence key is the product key question)', src='W3')
Q('telemetry_dest', 'adm', 'w', 'yesNo', 'Diagnostic data level 0, security only (Enterprise and Education only)?', 'Poziom danych diagnostycznych 0, tylko zabezpieczenia (tylko Enterprise i Education)?', 'no', **ADM,
  apply='HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection AllowTelemetry=0; reported as not honoured on other editions', src='W2')
Q('owner', 'adm', 'w', 'text', 'Registered owner:', 'Zarejestrowany właściciel:', '', **ADM, apply='HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion RegisteredOwner (no Microsoft page names this registry place: out of the build)', src='W10', verified=False)
Q('organisation', 'adm', 'w', 'text', 'Registered organisation:', 'Zarejestrowana organizacja:', '', **ADM, apply='HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion RegisteredOrganization (no Microsoft page names this registry place: out of the build)', src='W10', verified=False)
Q('w_first_msg', 'adm', 'w', 'text', 'Message shown before every sign-in and spoken at the first one:', 'Komunikat pokazywany przed każdym logowaniem i odczytany przy pierwszym:', '', **ADM,
  apply='LegalNoticeCaption and LegalNoticeText under HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System; spoken once with SAPI (Microsoft Learn names only the policy, not its registry place, and sources disagree: out of the build)', src='W9', verified=False)
Q('w_asset_tag', 'adm', 'w', 'text', 'Asset tag to record:', 'Numer inwentarzowy do zapisania:', '', **ADM,
  apply='written to the applied-settings report %ProgramData%\\Charon\\install-report.txt', src='-')
Q('w_root_ca', 'adm', 'w', 'text', 'Certificates to trust: file names in EFI\\HAK\\site, separated by commas:', 'Certyfikaty do zaufania: nazwy plików w EFI\\HAK\\site, rozdzielone przecinkami:', '', **ADM,
  apply='Import-Certificate -CertStoreLocation Cert:\\LocalMachine\\Root', src='W11')
Q('w_reader_cfg_file', 'adm', 'w', 'text', 'Ready NVDA configuration to deploy: a folder in EFI\\HAK\\site:', 'Gotowa konfiguracja NVDA do wdrożenia: folder w EFI\\HAK\\site:', '', **ADM,
  apply='the folder copied into %APPDATA%\\nvda of the user (replaces the NVDA answers of the speech group)', src='W12')
Q('w_post_script', 'adm', 'w', 'text', 'Script to run last, as FILE.ps1 sha256=HASH (the file in EFI\\HAK\\site):', 'Skrypt uruchamiany na końcu, jako PLIK.ps1 sha256=SKRÓT (plik w EFI\\HAK\\site):', '', **ADM,
  apply='run elevated only when Get-FileHash SHA256 matches; its exit status in the report', src='X12')
Q('w_phone_home', 'adm', 'w', 'text', 'Report the finished installation to this address (http or https):', 'Zgłoś zakończoną instalację pod ten adres (http albo https):', '', **ADM,
  apply='Invoke-WebRequest -Method Post of the applied-settings report (no secrets in it) after every other answer', src='-')


# ---------------------------------------------------------------------------------- software sets (batch 3)
# The catalogue is a data file (tools/charon-install/catalogue/charon.ini -> EFI/HAK/catalogue/charon.ini); every id
# in it was verified against the live package indexes (work/r410a-evidence/verify-catalogue.txt). The programs are
# installed at the first start, after speech, with the network. set_<id> is empty for "the set's default programs",
# a list of item ids when single programs were ticked, "none" when all were cleared.
SETS = [('everyday', 'Everyday', 'Na co dzień'), ('blind', 'For blind users', 'Dla niewidomych'), ('tools', 'System tools', 'Narzędzia systemowe'), ('create', 'Creating and programming', 'Tworzenie i programowanie')]
SET_NAMES = {'everyday': ('Everyday', 'Na co dzień'), 'blind': ('For blind users', 'Dla niewidomych'), 'tools': ('System tools', 'Narzędzia systemowe'), 'create': ('Creating and programming', 'Tworzenie i programowanie')}
for prefix, paths in (('', LINUX), ('w_', 'w')):
    Q(prefix + 'sets', 'desk', paths, 'multi', 'Software sets to install:', 'Zestawy programów do zainstalowania:', '', SETS,
      apply='the installed system reads the catalogue and installs each chosen program at the first start: Linux from the distribution\'s repository (apt / dnf / pacman ids), Windows with winget; a program without an id for this system is reported "not available here"', src='CAT')
    for sid, (en, pl) in SET_NAMES.items():
        Q(prefix + 'set_' + sid, 'desk', paths, 'text', en + ': which programs?', pl + ': które programy?', '',
          when='detailed::Has("%ssets","%s")' % (prefix, sid), apply='item ids of the catalogue (ticked in the dialogue from the catalogue file)', src='CAT')
Q('soft_flatpak', 'desk', LINUX, 'yesNo', 'Take programs that the repository lacks from Flathub?', 'Brać z Flathuba programy, których nie ma w repozytorium?', 'no',
  when='!detailed::Is("sets","")', apply='flatpak install -y --noninteractive flathub APPID for items without a repository id', src='L10, CAT')

# ---------------------------------------------------------------------------------- CharonDOS
Q('dos_placeholder', 'desk', 'x', 'yesNo', '', '', 'no', verified=False, apply='CharonDOS rows: not implemented in this build (see the results file)', src='-')


# ---------------------------------------------------------------------------------- shared in a plan of several systems
# A plan asks these ONCE for all its systems (user request of 2026-11-24). The shared question is the Linux row; SHARED
# says which Windows row takes the answer and how its values are named there (None: the same values). A shared answer
# that a system cannot take (no such row for it, or the row does not apply to its desktop) is named in that system's
# part of the dialogue, never dropped in silence. Asked once already by the plan's own shared block and not in this
# table: language, keyboard, time zone, user, password, computer name (with "-<system>" as its ending), clock in UTC.
SHARED = {
    'lid': ('w_lid', {'suspend': '1', 'ignore': '0', 'poweroff': '3'}),
    'sleep_ac': ('w_sleep_ac', None),
    'dark': ('w_dark', None),
    'sd_punct': ('w_punct', {'none': '0', 'some': '100', 'all': '300'}),
    'sets': ('w_sets', None), 'set_everyday': ('w_set_everyday', None), 'set_blind': ('w_set_blind', None),
    'set_tools': ('w_set_tools', None), 'set_create': ('w_set_create', None),
    # shared for the Linux systems of a plan; Windows has no verified equivalent (it is named as not applied there)
    # (r410, user decision 2026-11-24: speech rate and pitch are NOT shared - they stay per system; Charon writes neither
    # NVDA's nor Orca's own configuration, so a shared rate would not reach every reader.)
    'formats': None, 'braille': None, 'text_size': None, 'sounds': None, 'numlock': None,
    'auto_updates': None, 'firewall': None,
}
# The plan's shared time zone (an IANA name of the main questions) as Windows names it (verify-windows.txt item 1).
ZONES = {'Europe/Warsaw': 'Central European Standard Time', 'Europe/London': 'GMT Standard Time', 'Europe/Berlin': 'W. Europe Standard Time',
         'Europe/Rome': 'W. Europe Standard Time', 'Europe/Paris': 'Romance Standard Time', 'Europe/Madrid': 'Romance Standard Time',
         'Europe/Kyiv': 'FLE Standard Time', 'Europe/Prague': 'Central Europe Standard Time', 'America/New_York': 'Eastern Standard Time',
         'America/Los_Angeles': 'Pacific Standard Time', 'UTC': 'UTC'}


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def ustr(s):
    return 'u' + cstr(s)


def generate():
    # hidden: implemented and host-tested (the scripts must handle it), but not offered until a VM read-back shows its effect
    handled = [r for r in ROWS if r['verified']]
    rows = [r for r in handled if not r['hidden']]
    keys = [r['key'] for r in handled]
    assert len(keys) == len(set(keys)), 'duplicate key'
    out = ['#pragma once', '// Generated by tools/charon-install/charon_questions.py: do not edit; edit the table there.',
           '// r410: the questions of the detailed install mode. Included by CharonWsDetailed.h.', 'namespace hakefi {', 'namespace ws {', 'namespace detailed {',
           'inline const Group groups[]={']
    for g in GROUPS:
        out.append('    {%s,%s,%s,%d},' % (cstr(g[0]), ustr(g[2]), ustr(g[1]), g[3]))
    out.append('};')
    fam = {'linux': LINUX, 'windows': 'w', 'dos': 'x'}
    for r in rows:
        if r['choices']:
            out.append('inline const Choice choices_%s[]={%s};' % (r['key'], ','.join('{%s,%s,%s}' % (ustr(c[2]), ustr(c[1]), cstr(c[0])) for c in r['choices'])))
        family = 'windows' if r['paths'] == 'w' else 'dos' if r['paths'] == 'x' else 'linux'
        conds = []
        if set(r['paths']) != set(fam[family]):
            conds.append('On(%s)' % cstr(r['paths']))
        if r['when']:
            conds.append('(' + r['when'] + ')')
        r['whenName'] = 'nullptr'
        if conds:
            r['whenName'] = 'When_' + r['key']
            out.append('inline bool When_%s() {return %s;}' % (r['key'], ' && '.join(conds)))
    for family in ('linux', 'windows', 'dos'):
        out.append('inline const Question %sRows[]={' % family)
        mine = [r for r in rows if ('windows' if r['paths'] == 'w' else 'dos' if r['paths'] == 'x' else 'linux') == family]
        for r in mine:
            kind = {'choice': 'choice', 'yesNo': 'yesNo', 'multi': 'multi', 'number': 'number', 'text': 'text', 'secret': 'secret', 'name': 'name'}[r['kind']]
            ch = ('choices_%s,sizeof(choices_%s)/sizeof(choices_%s[0])' % ((r['key'],) * 3)) if r['choices'] else 'nullptr,0'
            out.append('    {%s,false,Kind::%s,%s,%s,%s,%s,%s,%s,%s,%d,%s,%d,%d,%d,%s},' % (
                cstr(r['key']), kind, ustr(r['pl']), ustr(r['en']), cstr(r['default']), ch,
                ustr(r['help_pl']) if r['help_pl'] else 'nullptr', ustr(r['help_en']) if r['help_en'] else 'nullptr',
                cstr(r['group']), r['level'], r['whenName'], 1 if r['secret'] else 0, r['low'], r['high'], cstr(r['risk']) if r['risk'] else 'nullptr'))
        if not mine:
            out.append('    {"none",false,Kind::yesNo,u"",u"","no",nullptr,0,nullptr,nullptr,"none",0,[]() {return false;},0,0,0,nullptr},   // no questions for this system yet')
        out.append('};')
    # the shared block of a plan: the flagged Linux rows without their conditions, and the maps onto Windows
    by_key = {r['key']: r for r in rows}
    out.append('inline const Question sharedRows[]={')
    # A condition that speaks only of other SHARED answers is kept (seen in the VMware run of batch 4: "<set>: which
    # programs?" was asked for all four sets, also for sets that were not checked); a condition on the desktop or on
    # the installation path cannot be decided in the shared block and is left to each system's own list.
    shared_when = {}
    for key in SHARED:
        w = by_key[key]['when']
        if w and 'Desk' not in w and 'On(' not in w and all(k in SHARED for k in re.findall(r'detailed::(?:Is|Has)\("([a-z0-9_]+)"', w)):
            shared_when[key] = 'WhenShared_' + key
            out.insert(len(out) - 1, 'inline bool WhenShared_%s() {return %s;}' % (key, w))
    for key in SHARED:
        r = by_key[key]
        ch = ('choices_%s,sizeof(choices_%s)/sizeof(choices_%s[0])' % ((key,) * 3)) if r['choices'] else 'nullptr,0'
        out.append('    {%s,false,Kind::%s,%s,%s,%s,%s,nullptr,nullptr,%s,0,%s,0,0,0,%s},' % (
            cstr(key), r['kind'], ustr(r['pl']), ustr(r['en']), cstr(r['default']), ch, cstr(r['group']), shared_when.get(key, 'nullptr'),
            cstr(r['risk']) if r['risk'] else 'nullptr'))
    out.append('};')
    out.append('struct SharedMap {const char* key;const char* windows;const char* from[8];const char* to[8];};   // from[0] null: the same values')
    out.append('inline const SharedMap sharedMaps[]={')
    for key, target in SHARED.items():
        if target is None:
            continue
        wkey, values = target
        assert wkey in by_key, wkey
        if values:
            for v in values.values():
                assert v in [c[0] for c in by_key[wkey]['choices']], (wkey, v)
            out.append('    {%s,%s,{%s},{%s}},' % (cstr(key), cstr(wkey), ','.join(cstr(k) for k in values), ','.join(cstr(v) for v in values.values())))
        else:
            out.append('    {%s,%s,{},{}},' % (cstr(key), cstr(wkey)))
    out.append('};')
    out.append('struct ZoneMap {const char* iana;const char* windows;};')
    out.append('inline const ZoneMap zoneMaps[]={%s};' % ','.join('{%s,%s}' % (cstr(k), cstr(v)) for k, v in ZONES.items()))
    for v in ZONES.values():
        assert v in [c[0] for c in by_key['wtimezone']['choices']], v
    out += ['}  // namespace detailed', '}  // namespace ws', '}  // namespace hakefi', '']
    header = '\n'.join(out)

    # the key check
    problems = []
    sh = (ROOT / 'src/linux/personal.sh')
    ps = (ROOT / 'tools/charon-addon/Charon-Personal.ps1')
    sh_text = sh.read_text(encoding='utf-8') if sh.exists() else ''
    ps_text = ps.read_text(encoding='utf-8') if ps.exists() else ''
    for r in handled:
        if r['paths'] == 'w':
            if not re.search(r'^function Apply-%s\b' % re.escape(r['key']), ps_text, re.M):
                problems.append('Charon-Personal.ps1 has no "function Apply-%s"' % r['key'])
        elif r['paths'] != 'x':
            if not re.search(r'^apply_%s\(\)' % re.escape(r['key']), sh_text, re.M):
                problems.append('personal.sh has no "apply_%s()"' % r['key'])
    for m in re.finditer(r'^apply_([a-z0-9_]+)\(\)', sh_text, re.M):
        if m.group(1) != 'all' and m.group(1) not in keys:
            problems.append('personal.sh handles "%s", which is not a question' % m.group(1))
    for m in re.finditer(r'^function Apply-([a-z0-9_]+)\b', ps_text, re.M):
        if m.group(1) not in keys:
            problems.append('Charon-Personal.ps1 handles "%s", which is not a question' % m.group(1))

    doc = ['Charon r410 - questions of the detailed install mode (generated by tools/charon-install/charon_questions.py)', '',
           'paths: d Debian installer, u Ubuntu installer, D U A F ready images of Debian, Ubuntu, Arch, Fedora, w Windows, x CharonDOS.',
           'src: L<n> = item n of work/r410a-evidence/verify-linux.txt, W<n> = item n of verify-windows.txt (primary sources, URLs there).', '']
    for g in GROUPS:
        mine = [r for r in ROWS if r['group'] == g[0]]
        if not mine:
            continue
        doc.append('[%s] %s' % (g[0], g[1]))
        for r in mine:
            doc.append('  %-17s %-7s %-6s default=%-10s %s  [%s]%s' % (r['key'], r['paths'], r['kind'], r['default'] or '(none)', r['apply'], r['src'], ('  HIDDEN: ' + r['hidden']) if r['verified'] and r['hidden'] else '' if r['verified'] else '  NOT IN THE BUILD (unverified)'))
            if r['choices']:
                doc.append('  %-17s values: %s' % ('', ', '.join(c[0] for c in r['choices'])))
        doc.append('')
    # question group x system: implemented / hidden (the system or desktop has no such setting) / unverified (out of the build)
    columns = [('Windows 11', 'w', None), ('GNOME', 'l', 'g'), ('KDE Plasma', 'l', 'k'), ('MATE', 'l', 'm'), ('Xfce', 'l', 'x'), ('Cinnamon', 'l', 'c'),
               ('LXQt', 'l', '-'), ('i3 / I38', 'l', '-'), ('Linux console', 'l', '-'), ('CharonDOS', 'x', None)]
    doc.append('Implemented / hidden / unverified, per question group and system (hidden: the question exists for the family, but this')
    doc.append('system or desktop has no such setting, so it is not asked; unverified: specified, no primary source, not in the build):')
    doc.append('  %-26s' % 'group' + ''.join('%-15s' % c[0] for c in columns))
    totals = [[0, 0, 0] for _ in columns]
    for g in GROUPS:
        cells = []
        for i, (name, fam, desk) in enumerate(columns):
            mine = [r for r in ROWS if r['group'] == g[0] and (('w' if r['paths'] == 'w' else 'x' if r['paths'] == 'x' else 'l') == fam)]
            done = [r for r in mine if r['verified'] and not r['hidden'] and (desk is None or 'desks' not in r or desk in r['desks'])]
            hidden = [r for r in mine if r['verified'] and r not in done]
            unverified = [r for r in mine if not r['verified']]
            cells.append('%d/%d/%d' % (len(done), len(hidden), len(unverified)))
            for k, v in enumerate((done, hidden, unverified)): totals[i][k] += len(v)
        doc.append('  %-26s' % g[1] + ''.join('%-15s' % c for c in cells))
    doc.append('  %-26s' % 'TOTAL' + ''.join('%-15s' % ('%d/%d/%d' % tuple(t)) for t in totals))
    doc.append('')
    counts = {f: len([r for r in rows if (('windows' if r['paths'] == 'w' else 'dos' if r['paths'] == 'x' else 'linux') == f)]) for f in ('linux', 'windows', 'dos')}
    doc.append('Counts in the build: Linux %d, Windows %d, CharonDOS %d.' % (counts['linux'], counts['windows'], counts['dos']))
    both = [k for k, t in SHARED.items() if t is not None]
    doc.append('In a plan of several systems, asked ONCE (shared): %d questions, of which %d reach Windows too (%s) and %d are for the Linux systems only (%s);' % (len(SHARED), len(both), ', '.join(both), len(SHARED) - len(both), ', '.join(k for k, t in SHARED.items() if t is None)))
    doc.append('  + the shared time zone of the main questions is mapped to Windows (wtimezone). Per-system remainder: Linux %d, Windows %d.' % (counts['linux'] - len(SHARED), counts['windows'] - len(both) - 1))
    per = {p: len([r for r in rows if p in r['paths']]) for p in 'dDuUAFwx'}
    doc.append('Per installation path: ' + ', '.join('%s %d' % (p, per[p]) for p in 'dDuUAFwx'))
    keyfile = ['linux ' + ' '.join(r['key'] for r in rows if r['paths'] not in ('w', 'x')), 'windows ' + ' '.join(r['key'] for r in rows if r['paths'] == 'w'), '']
    return header, '\n'.join(doc) + '\n', '\n'.join(keyfile), problems, counts


if __name__ == '__main__':
    header, doc, keyfile, problems, counts = generate()
    if problems:
        print('\n'.join('FAIL ' + p for p in problems))
        sys.exit(1)
    if '--check' not in sys.argv:
        # The header is shared with builds that may be running: written only when it changed, and put in place whole.
        import os
        import time
        target = ROOT / 'src/efi/CharonWsQuestions.h'
        if not target.exists() or target.read_text(encoding='utf-8') != header:
            fresh = target.with_name('CharonWsQuestions.h.new')
            fresh.write_text(header, encoding='utf-8', newline='\n')
            for attempt in range(20):
                try:
                    os.replace(fresh, target)
                    break
                except OSError:
                    time.sleep(0.5)
            else:
                sys.exit('FAIL src/efi/CharonWsQuestions.h is in use and could not be replaced')
        (ROOT / 'tests/charon-r410-questions.txt').write_text(doc, encoding='utf-8', newline='\n')
        (ROOT / 'tests/charon-install-host').mkdir(exist_ok=True)
        (ROOT / 'tests/charon-install-host/detail-keys.txt').write_text(keyfile, encoding='utf-8', newline='\n')
    print('PASS charon_questions: Linux %d, Windows %d, CharonDOS %d questions' % (counts['linux'], counts['windows'], counts['dos']))
