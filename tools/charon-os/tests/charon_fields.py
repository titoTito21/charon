# Shared specification of Charon's settings record for the OS companion programs.
# Source: src/efi/CharSettings.h (layout, ranges, defaults), src/efi/BootMenu.h
# labelFor() lines 1118-1300 (names, PL/EN), src/efi/BootMenuModel.h (rows, bits).
#
# RECORD (little endian):
#   0..6  "CHARS00", 7 version char '1'..'9','A','B','C','D','E'
#   8..15 sequence (uint64, non-zero)
#   16 language 17 wrap 18 sounds 19 explicitMessages 20 startMelody 21 backgroundMelody
#   22 rate 23 speechVolume 24 effectsVolume 25 verbosity | progress<<2
#   26 rememberLast | welcome<<1   27 autoSeconds
#   28 pitch 29 readerRate 30 readerPitch 31 readerVolume
#   32 detail (bits 0-2) | profileItems bits 0-4 << 3 (format E)
#   33 timers (bits 0-1) | profileItems bits 5-10 << 2 (format E)
#   34 messageRate 35 messagePitch
#   36 punctuation (bits 0-1) | formatGroups << 2 (bits 2-4, formats D and E)
#   37 capitals (bits 0-2) | profileItems bit 11 << 3 | readerBar << 4 | absoluteColours << 5 (format E)
#   38 screenLanguage (bits 0-1) | profile << 2 (bits 2-3, formats D and E; 3 = custom only in E)
#   39 readerFlags 40 formatFlags 41 readerExtra (bit 7 = loading sound) 42 synthKey 43 synthVoice
#   Bits not named here must be zero: DecodeSettings rejects the record otherwise. A reader
#   of the record masks byte 32 with 7, byte 33 with 3, byte 36 with 3, byte 37 with 7, byte 38 with 3.
#   CRC-32 (IEEE, reflected, init/final 0xffffffff = zlib.crc32) of bytes [0, crcAt) at crcAt
#   size / crcAt: versions 1-6: 32 / 28; 7-8: 40 / 36; 9, A, B, C, D, E: 48 / 44
#   slot file A = \EFI\HAK\charon-settings-a.bin holds ODD sequences, B = ...-b.bin EVEN.
VERSIONS = '123456789ABCDE'        # ascending; index = age
NEWEST = 'E'
MEMBERS = ['language', 'wrap', 'sounds', 'explicitMessages', 'startMelody', 'backgroundMelody', 'rate',
           'speechVolume', 'effectsVolume', 'verbosity', 'progress', 'rememberLast', 'autoSeconds', 'welcome',
           'pitch', 'readerRate', 'readerPitch', 'readerVolume', 'detail', 'timers', 'messageRate', 'messagePitch',
           'punctuation', 'capitals', 'screenLanguage', 'readerFlags', 'formatFlags', 'readerExtra',
           'synthKey', 'synthVoice', 'formatGroups', 'profile', 'profileItems', 'readerBar', 'absoluteColours']
# CharSettings{} defaults (CharSettings.h, struct CharSettings): what Charon r410 holds without a
# record and what a new record of format D or E gets. profileItems 1083 = profileItemsNormal.
DEFAULTS = dict(language=0, wrap=0, sounds=1, explicitMessages=0, startMelody=1, backgroundMelody=0, rate=10,
                speechVolume=10, effectsVolume=10, verbosity=1, progress=1, rememberLast=0, autoSeconds=10, welcome=7,
                pitch=10, readerRate=10, readerPitch=10, readerVolume=10, detail=7, timers=2, messageRate=14,
                messagePitch=10, punctuation=1, capitals=1, screenLanguage=0, readerFlags=47, formatFlags=252,
                readerExtra=153, synthKey=0, synthVoice=0, formatGroups=7, profile=0, profileItems=1083,
                readerBar=0, absoluteColours=0)
# Defaults of the two bytes in formats A and B (CharSettings.h V10 comment: 60 and 25).
DEFAULTS_AB = dict(formatFlags=60, readerExtra=25)
# Defaults of the two bytes in a NEW record of format C (r397 to r409: formatting on, 255; byte 41
# without bit 7, because the readers of r397 to r407 reject a format C record that has it).
DEFAULTS_C = dict(formatFlags=255, readerExtra=25)
# member -> first version that stores it
MEMBER_SINCE = dict(language='1', wrap='1', sounds='2', explicitMessages='3', startMelody='4', backgroundMelody='4',
                    rate='5', speechVolume='6', effectsVolume='6', verbosity='6', progress='6', rememberLast='6',
                    autoSeconds='6', welcome='7', pitch='7', readerRate='7', readerPitch='7', readerVolume='7',
                    detail='7', timers='7', messageRate='8', messagePitch='8', punctuation='9', capitals='9',
                    screenLanguage='9', readerFlags='9', formatFlags='A', readerExtra='A', synthKey='B', synthVoice='B',
                    formatGroups='D', profile='D', profileItems='E', readerBar='E', absoluteColours='E')
AUTO_DELAYS = [0, 3, 5, 10, 15, 20, 30, 60, 255]

# UI rows in the order of Charon's menu pages.
# kind: 'check' (one bit: member & mask), 'choice' (value = (member >> shift) & mask, choices listed),
#       'slider' (whole member, low..high; unit 'percent' = VoicePercent(tenths), 'tenth' = value*10 %)
# since: first record format that stores the row. The custom profile (profile = 3) needs format E;
# format D takes 0..2 (the programs refuse 3 when they write format D).
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
    dict(id='linger', page='reader', kind='check', member='readerExtra', mask=128, since='D',
         en='Loading sound after the system takes over', pl='Dźwięk wczytywania po przekazaniu sterowania systemowi'),
    dict(id='profile', page='reader', kind='choice', member='profile', shift=0, mask=3, since='D',
         en='Verbosity profile', pl='Profil szczegółowości',
         choices=[(0, 'normal', 'zwykły'),
                  (1, 'beginner: full descriptions and key hints', 'dla początkujących: pełne opisy i podpowiedzi klawiszy'),
                  (2, 'expert: short, sounds instead of words', 'dla zaawansowanych: krótko, dźwięki zamiast słów'),
                  (3, 'custom: the items ticked under Text formatting', 'własny: pozycje zaznaczone w Formatowaniu tekstu')]),
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
    dict(id='groupKind', page='formatting', kind='check', member='formatGroups', mask=1, since='D',
         en='Kind of element: another tone for the default entry, a submenu, an edit field, a help line',
         pl='Rodzaj elementu: inny ton dla pozycji domyślnej, podmenu, pola edycji, wiersza pomocy'),
    dict(id='groupState', page='formatting', kind='check', member='formatGroups', mask=2, since='D',
         en='State and warnings: another voice and sound for errors, warnings, countdowns, messages that disappear',
         pl='Stan i ostrzeżenia: inny głos i dźwięk dla błędów, ostrzeżeń, odliczania, znikających komunikatów'),
    dict(id='groupPosition', page='formatting', kind='check', member='formatGroups', mask=4, since='D',
         en='Position and structure: a sound at the start and end of a list, submenu depth, pitch by place in the list',
         pl='Położenie i struktura: dźwięk początku i końca listy, wcięcie podmenu, wysokość głosu według miejsca na liście'),
    dict(id='absoluteColours', page='formatting', kind='check', member='absoluteColours', mask=1, since='E',
         en='Absolute colours: every colour has its own pitch, on a uniform screen too',
         pl='Kolory bezwzględnie: każdy kolor ma swoją wysokość głosu, także na jednolitym ekranie'),
    dict(id='readerBar', page='formatting', kind='check', member='readerBar', mask=1, since='E',
         en='Reader bar: the bottom screen line shows what the reader says',
         pl='Pasek czytnika: dolny wiersz ekranu pokazuje, co mówi czytnik'),
    # the custom verbosity profile's 12 items (CharonFormatting.h item*, bit = 1 << n)
    dict(id='itemPosition', page='formatting', kind='check', member='profileItems', mask=1, since='E',
         en='Custom profile: position in the list, for example 2 of 5',
         pl='Profil własny: położenie na liście, na przykład 2 z 5'),
    dict(id='itemKind', page='formatting', kind='check', member='profileItems', mask=2, since='E',
         en='Custom profile: kind and state of an entry in words, submenu, default',
         pl='Profil własny: rodzaj i stan pozycji słowami, podmenu, domyślna'),
    dict(id='itemHints', page='formatting', kind='check', member='profileItems', mask=4, since='E',
         en='Custom profile: key hints', pl='Profil własny: podpowiedzi klawiszy'),
    dict(id='itemIntro', page='formatting', kind='check', member='profileItems', mask=8, since='E',
         en="Custom profile: menu introduction with the program's name and the number of entries",
         pl='Profil własny: zapowiedź menu z nazwą programu i liczbą pozycji'),
    dict(id='itemHelp', page='formatting', kind='check', member='profileItems', mask=16, since='E',
         en='Custom profile: the help line read with a new screen',
         pl='Profil własny: wiersz pomocy czytany przy nowym ekranie'),
    dict(id='itemScreen', page='formatting', kind='check', member='profileItems', mask=32, since='E',
         en='Custom profile: the kind of screen in words, entry editor, command line',
         pl='Profil własny: nazwa rodzaju ekranu, edytor wpisu, wiersz poleceń'),
    dict(id='itemSounds', page='formatting', kind='check', member='profileItems', mask=64, since='E',
         en='Custom profile: sounds instead of words', pl='Profil własny: dźwięki zamiast słów'),
    dict(id='itemStructure', page='formatting', kind='check', member='profileItems', mask=128, since='E',
         en='Custom profile: structure in words, start of list, end of list, level',
         pl='Profil własny: struktura słowami, początek listy, koniec listy, poziom'),
    dict(id='itemWaitLong', page='formatting', kind='check', member='profileItems', mask=256, since='E',
         en='Custom profile: hidden menu, long wait, 5 seconds with the full hint',
         pl='Profil własny: ukryte menu, długie czekanie, 5 sekund z pełną podpowiedzią'),
    dict(id='itemWaitShort', page='formatting', kind='check', member='profileItems', mask=512, since='E',
         en='Custom profile: hidden menu, short wait, 1 second',
         pl='Profil własny: ukryte menu, krótkie czekanie, 1 sekunda'),
    dict(id='itemCountdown', page='formatting', kind='check', member='profileItems', mask=1024, since='E',
         en='Custom profile: the countdown sentence, what starts and when',
         pl='Profil własny: zdanie o odliczaniu, co wystartuje i kiedy'),
    dict(id='itemLineNumbers', page='formatting', kind='check', member='profileItems', mask=2048, since='E',
         en='Custom profile: line number in an editor', pl='Profil własny: numer wiersza w edytorze'),
    # ---- synth (rows built at run time from \EFI\HAK\synths\*.manifest) ----
    # synthKey: 0 = 'TVox, built in' / 'TVox, wbudowany'; add-on = '<name>, add-on' / '<name>, dodatek'
    # synthVoice: index into the manifest's voice lines (spoken name); a new synthesizer resets it to 0.
]
# readerExtra bit 7, 'Loading sound after the system takes over' (row 'linger'): DecodeSettings
# accepts it in formats C and later since r408; the readers of r397 to r407 reject a format C
# record that has it. The programs therefore offer the row from format D on; when they write
# format A, B or C they keep the bit as the record on the partition has it and never turn it on.
LINGER_NOTE_EN = ("'Loading sound after the system takes over' can be changed here when the installed Charon writes "
                  "settings format D or later. In an older format this program keeps the option as the record has it: "
                  "Charon releases before r408 reject a record with it on.")
LINGER_NOTE_PL = ("Opcję „Dźwięk wczytywania po przekazaniu sterowania systemowi” można tu zmienić, gdy zainstalowany Charon "
                  "zapisuje ustawienia w formacie D lub nowszym. W starszym formacie program zostawia ją tak, jak jest w "
                  "zapisie: wydania Charona sprzed r408 odrzucają zapis z włączoną tą opcją.")


def voice_percent(tenths):
    if tenths <= 5:
        return 0
    if tenths <= 10:
        return (tenths - 5) * 10
    return 100 if tenths >= 20 else 50 + (tenths - 10) * 5
