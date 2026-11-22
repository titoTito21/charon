# Shared specification of Charon's settings record for the OS companion programs.
# Source: src/efi/CharSettings.h (layout, ranges, defaults), src/efi/BootMenu.h
# labelFor() lines 1118-1300 (names, PL/EN), src/efi/BootMenuModel.h (rows, bits).
#
# RECORD (little endian):
#   0..6  "CHARS00", 7 version char '1'..'9','A','B','C'
#   8..15 sequence (uint64, non-zero)
#   16 language 17 wrap 18 sounds 19 explicitMessages 20 startMelody 21 backgroundMelody
#   22 rate 23 speechVolume 24 effectsVolume 25 verbosity | progress<<2
#   26 rememberLast | welcome<<1   27 autoSeconds
#   28 pitch 29 readerRate 30 readerPitch 31 readerVolume 32 detail 33 timers
#   34 messageRate 35 messagePitch 36 punctuation 37 capitals 38 screenLanguage
#   39 readerFlags 40 formatFlags 41 readerExtra 42 synthKey 43 synthVoice
#   CRC-32 (IEEE, reflected, init/final 0xffffffff = zlib.crc32) of bytes [0, crcAt) at crcAt
#   size / crcAt: versions 1-6: 32 / 28; 7-8: 40 / 36; 9, A, B, C: 48 / 44
#   slot file A = \EFI\HAK\charon-settings-a.bin holds ODD sequences, B = ...-b.bin EVEN.
VERSIONS = '123456789ABC'          # ascending; index = age
NEWEST = 'C'
MEMBERS = ['language', 'wrap', 'sounds', 'explicitMessages', 'startMelody', 'backgroundMelody', 'rate',
           'speechVolume', 'effectsVolume', 'verbosity', 'progress', 'rememberLast', 'autoSeconds', 'welcome',
           'pitch', 'readerRate', 'readerPitch', 'readerVolume', 'detail', 'timers', 'messageRate', 'messagePitch',
           'punctuation', 'capitals', 'screenLanguage', 'readerFlags', 'formatFlags', 'readerExtra',
           'synthKey', 'synthVoice']
# CharSettings{} defaults (CharSettings.h lines 50-56). readerExtra's default 153 has bit 7
# (extraLinger) set, which DecodeSettings (line 120, bytes[41]>127) rejects: the programs
# never write bit 7, so the storable default is 25.
DEFAULTS = dict(language=0, wrap=0, sounds=1, explicitMessages=0, startMelody=1, backgroundMelody=0, rate=10,
                speechVolume=10, effectsVolume=10, verbosity=1, progress=1, rememberLast=0, autoSeconds=10, welcome=7,
                pitch=10, readerRate=10, readerPitch=10, readerVolume=10, detail=7, timers=2, messageRate=14,
                messagePitch=10, punctuation=1, capitals=1, screenLanguage=0, readerFlags=47, formatFlags=255,
                readerExtra=25, synthKey=0, synthVoice=0)
# Defaults of the two bytes in formats A and B (CharSettings.h lines 34-39: 60 and 25).
DEFAULTS_AB = dict(formatFlags=60, readerExtra=25)
# member -> first version that stores it
MEMBER_SINCE = dict(language='1', wrap='1', sounds='2', explicitMessages='3', startMelody='4', backgroundMelody='4',
                    rate='5', speechVolume='6', effectsVolume='6', verbosity='6', progress='6', rememberLast='6',
                    autoSeconds='6', welcome='7', pitch='7', readerRate='7', readerPitch='7', readerVolume='7',
                    detail='7', timers='7', messageRate='8', messagePitch='8', punctuation='9', capitals='9',
                    screenLanguage='9', readerFlags='9', formatFlags='A', readerExtra='A', synthKey='B', synthVoice='B')
AUTO_DELAYS = [0, 3, 5, 10, 15, 20, 30, 60, 255]

# UI rows in the order of Charon's menu pages.
# kind: 'check' (one bit: member & mask), 'choice' (value = (member >> shift) & mask, choices listed),
#       'slider' (whole member, low..high; unit 'percent' = VoicePercent(tenths), 'tenth' = value*10 %)
# since: first record format that stores the row.
PAGES = [
    ('general', 'Charon settings', 'Ustawienia Charona'),
    ('welcome', 'Welcome', 'Powitanie'),
    ('detail', 'Detail', 'Szczegółowość'),
    ('reader', 'Screen reader settings', 'Ustawienia czytnika ekranu'),
    ('formatting', 'Text formatting', 'Formatowanie tekstu'),
    ('synth', 'Synthesizer', 'Syntezator'),
]
OFF_MELODIES = [(0, 'Off', 'Wyłączone'), (1, 'Melody 1', 'Melodia 1'), (2, 'Melody 2', 'Melodia 2'),
                (3, 'Melody 3', 'Melodia 3'), (4, 'Melody 4', 'Melodia 4')]
ROWS = [
    # ---- general (BootMenu.h settings page) ----
    dict(id='wrap', page='general', kind='check', member='wrap', mask=1, since='1',
         en='Wrap lists', pl='Zawijanie list'),
    dict(id='language', page='general', kind='choice', member='language', shift=0, mask=255, since='1',
         en='Language', pl='Język',
         choices=[(0, 'Automatic', 'Automatyczny'), (1, 'English', 'English'), (2, 'Polski', 'Polski')]),
    dict(id='sounds', page='general', kind='check', member='sounds', mask=1, since='2',
         en='UI sounds', pl='Dźwięki interfejsu'),
    dict(id='startMelody', page='general', kind='choice', member='startMelody', shift=0, mask=255, since='4',
         en='Start melody', pl='Melodia startu', choices=OFF_MELODIES),
    dict(id='backgroundMelody', page='general', kind='choice', member='backgroundMelody', shift=0, mask=255, since='4',
         en='Background melody', pl='Melodia w tle', choices=OFF_MELODIES),
    dict(id='rate', page='general', kind='slider', member='rate', low=5, high=20, unit='percent', since='5',
         en='Speech rate', pl='Tempo mowy'),
    dict(id='pitch', page='general', kind='slider', member='pitch', low=5, high=20, unit='percent', since='7',
         en='Voice pitch', pl='Wysokość głosu'),
    dict(id='speechVolume', page='general', kind='slider', member='speechVolume', low=1, high=10, unit='tenth', since='6',
         en='Speech volume', pl='Głośność mowy'),
    dict(id='effectsVolume', page='general', kind='slider', member='effectsVolume', low=0, high=10, unit='tenth', since='6',
         en='Effects and melody volume', pl='Głośność efektów i melodii'),
    dict(id='verbosity', page='general', kind='choice', member='verbosity', shift=0, mask=3, since='6',
         en='Boot messages (reader verbosity)', pl='Komunikaty rozruchu (szczegółowość czytnika)',
         choices=[(0, 'Off: no messages, no screen reading', 'Wyłączone: bez komunikatów i bez czytania programu'),
                  (1, 'Short: what starts, program screen reading', 'Krótkie: co uruchamiam, czytanie ekranu programu'),
                  (2, 'Detailed: also file, size, Secure Boot and counters', 'Szczegółowe: także plik, rozmiar, Secure Boot i liczniki')]),
    dict(id='rememberLast', page='general', kind='check', member='rememberLast', mask=1, since='6',
         en='Remember last choice', pl='Zapamiętaj ostatni wybór'),
    dict(id='autoSeconds', page='general', kind='choice', member='autoSeconds', shift=0, mask=255, since='6',
         en='Auto-continue time', pl='Czas autostartu',
         choices=[(0, 'Immediately', 'Natychmiast'), (3, '3 seconds', '3 sekund'), (5, '5 seconds', '5 sekund'),
                  (10, '10 seconds', '10 sekund'), (15, '15 seconds', '15 sekund'), (20, '20 seconds', '20 sekund'),
                  (30, '30 seconds', '30 sekund'), (60, '60 seconds', '60 sekund'),
                  (255, 'Never: always open the menu', 'Nigdy: zawsze otwórz menu')]),
    dict(id='progress', page='general', kind='choice', member='progress', shift=0, mask=3, since='6',
         en='Progress bars', pl='Paski postępu',
         choices=[(0, 'Off', 'Wyłączone'), (1, 'Beeps, as in NVDA', 'Sygnały dźwiękowe jak w NVDA'),
                  (2, 'Speech every 10 percent', 'Mowa co 10 procent'), (3, 'Beeps and speech', 'Sygnały i mowa')]),
    dict(id='explicitMessages', page='general', kind='check', member='explicitMessages', mask=1, since='3',
         en='Explicit messages', pl='Tryb dosadnych komunikatów'),
    # ---- welcome ----
    dict(id='welcomeHint', page='welcome', kind='check', member='welcome', mask=1, since='7',
         en='Hint how to open the options menu', pl='Porada, jak przejść do menu opcji'),
    dict(id='welcomeSound', page='welcome', kind='check', member='welcome', mask=2, since='7',
         en='Welcome beep, as when the add-on starts', pl='Pisk powitania, jak przy starcie dodatku'),
    dict(id='welcomeSpeech', page='welcome', kind='check', member='welcome', mask=4, since='7',
         en='Speech: Welcome to Charon', pl='Mowa: Witaj w Charon'),
    # ---- detail ----
    dict(id='detailDisk', page='detail', kind='check', member='detail', mask=1, since='7',
         en='Disk name', pl='Nazwa dysku'),
    dict(id='detailManager', page='detail', kind='check', member='detail', mask=2, since='7',
         en='Boot manager name, for example GRUB', pl='Nazwa menedżera rozruchu, na przykład GRUB'),
    dict(id='detailPosition', page='detail', kind='check', member='detail', mask=4, since='7',
         en='Position in lists, for example 2 of 5', pl='Pozycja na liście, na przykład 2 z 5'),
    # ---- reader ----
    dict(id='readerRate', page='reader', kind='slider', member='readerRate', low=5, high=20, unit='percent', since='7',
         en='Reader rate', pl='Tempo czytnika'),
    dict(id='readerPitch', page='reader', kind='slider', member='readerPitch', low=5, high=20, unit='percent', since='7',
         en='Reader pitch', pl='Wysokość głosu czytnika'),
    dict(id='readerVolume', page='reader', kind='slider', member='readerVolume', low=1, high=10, unit='tenth', since='7',
         en='Reader volume', pl='Głośność czytnika'),
    dict(id='timerSpeech', page='reader', kind='check', member='timers', mask=1, since='7',
         en='Countdowns: speak every 10 seconds', pl='Odliczanie: mowa co 10 sekund'),
    dict(id='timerSound', page='reader', kind='check', member='timers', mask=2, since='7',
         en='Countdowns: tick every second, long tone at the end', pl='Odliczanie: dźwięk co sekundę, długi na końcu'),
    dict(id='messageRate', page='reader', kind='slider', member='messageRate', low=5, high=20, unit='percent', since='8',
         en='Boot message rate', pl='Tempo komunikatów startowych'),
    dict(id='messagePitch', page='reader', kind='slider', member='messagePitch', low=5, high=20, unit='percent', since='8',
         en='Boot message pitch', pl='Wysokość głosu komunikatów startowych'),
    dict(id='punctuation', page='reader', kind='choice', member='punctuation', shift=0, mask=3, since='9',
         en='Symbol level', pl='Interpunkcja',
         choices=[(0, 'none', 'brak'), (1, 'some', 'część'), (2, 'most', 'większość'), (3, 'all', 'wszystko')]),
    dict(id='screenLanguage', page='reader', kind='choice', member='screenLanguage', shift=0, mask=3, since='9',
         en='Screen text language', pl='Język tekstu ekranu',
         choices=[(0, 'automatic', 'automatycznie'), (1, 'as the menu', 'jak menu'), (2, 'English', 'angielski'),
                  (3, 'Polish', 'polski')]),
    dict(id='capSay', page='reader', kind='check', member='capitals', mask=1, since='9',
         en='Capitals: say cap', pl='Wielkie litery: mów duże'),
    dict(id='capBeep', page='reader', kind='check', member='capitals', mask=2, since='9',
         en='Capitals: beep', pl='Wielkie litery: dźwięk'),
    dict(id='capPitch', page='reader', kind='check', member='capitals', mask=4, since='9',
         en='Capitals: higher pitch', pl='Wielkie litery: wyższy głos'),
    dict(id='flagMessages', page='reader', kind='check', member='readerFlags', mask=1, since='9',
         en='Text outside the selection on the second voice', pl='Tekst poza zaznaczeniem drugim głosem'),
    dict(id='flagTyped', page='reader', kind='check', member='readerFlags', mask=2, since='9',
         en='Speak typed characters', pl='Czytaj wpisywane znaki'),
    dict(id='flagWords', page='reader', kind='check', member='readerFlags', mask=16, since='9',
         en='Speak typed words', pl='Czytaj wpisywane słowa'),
    dict(id='flagCursor', page='reader', kind='check', member='readerFlags', mask=8, since='9',
         en='Speak the character at the cursor after arrows', pl='Czytaj znak pod kursorem po strzałkach'),
    dict(id='flagAutoRead', page='reader', kind='check', member='readerFlags', mask=4, since='9',
         en='Automatic reading of screen changes', pl='Automatyczne czytanie zmian ekranu'),
    dict(id='flagHints', page='reader', kind='check', member='readerFlags', mask=32, since='9',
         en="Hints in Charon's menu", pl='Porady w menu Charona'),
    dict(id='hiddenMenus', page='reader', kind='choice', member='readerExtra', shift=0, mask=3, since='A',
         en='Hidden boot-manager menus', pl='Ukryte menu menedżerów rozruchu',
         choices=[(0, 'off', 'wyłączone'), (1, 'announce and wait 2 seconds', 'ogłoś i czekaj 2 sekundy'),
                  (2, 'always reveal', 'zawsze odsłaniaj')]),
    dict(id='stopCountdown', page='reader', kind='check', member='readerExtra', mask=4, since='A',
         en='Stop boot-manager countdowns', pl='Zatrzymuj odliczanie menedżerów rozruchu'),
    # ---- formatting ----
    dict(id='formatVoice', page='formatting', kind='check', member='formatFlags', mask=1, since='A',
         en='Voice formatting: pitch, rate, volume and timbre, as in Emacspeak',
         pl='Formatowanie głosem: wysokość, tempo, głośność i barwa jak w Emacspeaku'),
    dict(id='formatIcons', page='formatting', kind='check', member='formatFlags', mask=2, since='A',
         en='Sound icons: oscillator sounds together with speech', pl='Ikony dźwiękowe: dźwięki oscylatora razem z mową'),
    dict(id='formatColours', page='formatting', kind='check', member='formatFlags', mask=4, since='A',
         en='Text and background colours', pl='Kolory tekstu i tła'),
    dict(id='formatBright', page='formatting', kind='check', member='formatFlags', mask=8, since='A',
         en='Bright and bold text', pl='Jasny i pogrubiony tekst'),
    dict(id='formatFont', page='formatting', kind='check', member='formatFlags', mask=16, since='A',
         en='Font and size', pl='Czcionka i rozmiar'),
    dict(id='formatInverse', page='formatting', kind='check', member='formatFlags', mask=32, since='A',
         en='Inverse and highlight', pl='Odwrócenie i podświetlenie'),
    dict(id='formatEvents', page='formatting', kind='check', member='formatFlags', mask=64, since='C',
         en='Event icons, as in Emacspeak: open, close, select, scroll, warning',
         pl='Ikony zdarzeń jak w Emacspeaku: otwarcie, zamknięcie, wybór, przewinięcie, ostrzeżenie'),
    dict(id='formatFamilies', page='formatting', kind='check', member='formatFlags', mask=128, since='C',
         en='Voice families: another voice for another style', pl='Rodziny głosów: inny głos dla innego stylu'),
    dict(id='stereo', page='formatting', kind='choice', member='readerExtra', shift=5, mask=3, since='C',
         en='Stereo position', pl='Położenie w stereo',
         choices=[(0, 'off', 'wyłączone'), (1, 'icons', 'ikony'), (2, 'speech and icons', 'mowa i ikony')]),
    dict(id='braille', page='formatting', kind='check', member='readerExtra', mask=8, since='A',
         en='Attributes in braille, dots 7 and 8', pl='Atrybuty na brajlu, punkty 7 i 8'),
    dict(id='dictionary', page='formatting', kind='check', member='readerExtra', mask=16, since='A',
         en='Firmware pronunciation dictionary, for example TPM, XHCI', pl='Słownik wymowy firmware, na przykład TPM, XHCI'),
    # ---- synth (rows built at run time from \EFI\HAK\synths\*.manifest) ----
    # synthKey: 0 = 'TVox, built in' / 'TVox, wbudowany'; add-on = '<name>, add-on' / '<name>, dodatek'
    # synthVoice: index into the manifest's voice lines (spoken name); a new synthesizer resets it to 0.
]
# Not offered as a row: readerExtra bit 7 'Loading sound after the system takes over' /
# 'Dźwięk wczytywania po przekazaniu sterowania systemowi' (BootMenu.h line 1229): the
# record reader of this tree rejects byte 41 > 127 (CharSettings.h line 120), so the bit
# cannot be stored. The programs say so in the information page and always write it as 0.
LINGER_NOTE_EN = ("'Loading sound after the system takes over' cannot be changed here: Charon's settings reader "
                  "rejects a record with this option on, so it is always written as off.")
LINGER_NOTE_PL = ("Opcji „Dźwięk wczytywania po przekazaniu sterowania systemowi” nie da się tu zmienić: Charon odrzuca "
                  "zapis ustawień z włączoną tą opcją, więc zawsze jest zapisywana jako wyłączona.")


def voice_percent(tenths):
    if tenths <= 5:
        return 0
    if tenths <= 10:
        return (tenths - 5) * 10
    return 100 if tenths >= 20 else 50 + (tenths - 10) * 5
