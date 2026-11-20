#pragma once
#include "BootMenuModel.h"
#include "CharonMessageMode.h"
namespace hakefi {
// Explicit-mode labels (r373): three wordings per row, so the same swear word
// does not come back row after row. The wording changes with every key press
// (ExplicitVariety, bumped once per key in the menu loop) and neighbouring
// rows start from different wordings. Role words (podmenu, okno wyboru,
// suwak, lista) stay, because they say how the row works.
// Device category and firmware-supplied identity are never rewritten.
struct ExplicitRow {const char16_t* pl[3];const char16_t* en[3];};
inline const char16_t* PickExplicit(const ExplicitRow* rows,size_t count,size_t row,bool pl) {
    if(row>=count)return nullptr;
    const size_t variant=(ExplicitVariety()+row)%3;
    return pl?rows[row].pl[variant]:rows[row].en[variant];
}
inline const char16_t* ExplicitMenuLabel(MenuPage page,size_t row,bool pl) {
    static const ExplicitRow mainRows[]={
        {{u"Który jebany system odpalamy, podmenu.",u"Z czego, ożeż kurwa, startujemy? Urządzenia, podmenu.",u"Dyski i inne graty do odpalenia, podmenu, kurwa mać."},
         {u"Which fucking system are we booting, submenu.",u"What the hell do we start from? Devices, submenu.",u"Disks and other junk to boot, submenu, fuck me."}},
        {{u"Poustawiaj tę kolejkę rozruchu, do diabła, podmenu, ja jebię.",u"Kto pierwszy, ten lepszy, kurwa: kolejność rozruchu, podmenu.",u"Przetasuj ten bajzel w kolejności rozruchu, podmenu, w chuj."},
         {u"Shuffle the damn boot order, submenu.",u"Who goes first, for fuck’s sake: boot order, submenu.",u"Sort out this boot order mess, submenu, holy shit."}},
        {{u"Pogrzeb w ustawieniach Charona, podmenu, o w mordę.",u"Pokręć gałkami, skoro musisz: ustawienia, podmenu, jebana mać.",u"Ustawienia, bo ci, kurwa, wiecznie coś nie pasuje, podmenu."},
         {u"Dig through Charon’s settings, submenu, fucking hell.",u"Twiddle the knobs if you must: settings, submenu, bollocks.",u"Settings, because something’s always bugging you, submenu, piss off."}},
        {{u"Bebechy tego pierdolonego UEFI, podmenu.",u"Ustawienia firmware, jak masz jaja, podmenu, pierdolę to.",u"Grzebanie w UEFI na własną odpowiedzialność, podmenu, w dupę jeża."},
         {u"UEFI guts, submenu, for fuck’s sake.",u"Firmware settings, if you’ve got the balls, submenu, damn it.",u"Poking UEFI at your own bloody risk, submenu."}},
        {{u"Powłoka UEFI dla twardzieli, chuj z tym.",u"Wiersz poleceń UEFI, stukaj, ile wlezie, do kurwy nędzy.",u"Shell UEFI, czarna magia, kurwa mać."},
         {u"UEFI Shell for hard bastards.",u"UEFI command line, type your heart out, shit.",u"UEFI Shell, black fucking magic."}},
        {{u"Środowisko Charona: programy, pliki i edytor, kurwa, cały ten kram.",u"Warsztat Charona, grzeb w plikach i odpalaj graty, do chuja.",u"Środowisko, czyli mały pierdolony Emacs na dnie firmware."},
         {u"Charon workspace: programs, files and the editor, the whole fucking lot.",u"Charon’s workshop, dig through files and launch junk, damn it.",u"The workspace, a tiny bloody Emacs at the bottom of firmware."}},
        {{u"Narzędzia systemowe, skrzynka z łomem, podmenu, ja pierdolę.",u"Śrubokręty i młotki tego złomu, podmenu, do chuja.",u"Narzędzia, do jasnej anielki, podmenu, jebany świat."},
         {u"System tools, the crowbar box, submenu, you bastard.",u"Screwdrivers and hammers for this heap, submenu, bloody hell.",u"Tools, for crying out loud, submenu, goddammit."}},
        {{u"Jebać czekanie, odpalamy.",u"Dość gadania, startuj, w pizdu.",u"No to w drogę, idzie w pizdu."},
         {u"Fuck the waiting, fire it up.",u"Enough chit-chat, start the damn thing.",u"Off we bloody go."}},
    };
    static const ExplicitRow settingsRows[]={
        {{u"Zawijanie list w kółko, kurwa",u"Czy listy mają się kręcić jak pojebane",u"Zawijanie, żeby z końca skakać na początek, ożeż kurwa"},
         {u"Wrap lists round and round, fuck me",u"Should lists spin like crazy, holy shit",u"Wrapping, so the end jumps back to the fucking start"}},
        {{u"Odśwież te wpisy urządzeń, do licha, ja jebię",u"Przeskanuj te zasrane graty jeszcze raz",u"Sprawdź, o w mordę, co jest podłączone"},
         {u"Refresh the device entries, fucking hell",u"Rescan the junk once more, bollocks",u"Check what the hell is plugged in"}},
        {{u"Jak tam ten Secure Boot, jebana mać",u"Stan Secure Boot, ochroniarza od siedmiu boleści, kurwa no",u"Czy Secure Boot w ogóle, kurwa, działa"},
         {u"How’s that Secure Boot doing, piss off",u"Secure Boot status, the useless bouncer, for fuck’s sake",u"Is Secure Boot even fucking working"}},
        {{u"Odpal czekający wybór rozruchu, w dupę jeża",u"Ten zaległy rozruch, wreszcie go puść, chuj z tym",u"Uruchom to, co tam wisi w kolejce, do kurwy nędzy"},
         {u"Run the pending boot choice, damn it",u"That overdue boot, let it go already, shit",u"Start whatever’s been hanging in the queue, you bastard"}},
        {{u"Wklep czekające ustawienia firmware, kurwa mać",u"Zastosuj te zaległe zmiany w firmware, ja pierdolę",u"Firmware czeka na zmiany, to mu je daj, do chuja"},
         {u"Apply the pending firmware settings, bloody hell",u"Push those overdue firmware changes, goddammit",u"Firmware’s waiting for changes, so give them, fuck me"}},
        {{u"Wgraj zaległą zmianę kluczy, jebany świat",u"Klucze Secure Boot, ta czekająca podmiana, w pizdu",u"Podmień klucze, skoro już przygotowane, kurwa"},
         {u"Apply the pending key update, holy shit",u"Secure Boot keys, that waiting swap, fucking hell",u"Swap the keys, they’re ready anyway, bollocks"}},
        {{u"To ma się odpalać po „Jebać czekanie”",u"Ustaw to jako cel startu, ożeż kurwa",u"To ma ruszać samo, zaznacz je, ja jebię"},
         {u"Boot this one on “Fuck the waiting”",u"Make this the damn start target",u"This starts by itself, mark it, piss off"}},
        {{u"Język, okno wyboru, o w mordę",u"W jakim języku mam ci bluzgać, okno wyboru, jebana mać",u"Mowa, po jakiemu, okno wyboru, kurwa no"},
         {u"Language, popup, for fuck’s sake",u"Which fucking language should I swear in, popup",u"Speech, in what tongue, popup, damn it"}},
        {{u"Dźwięki interfejsu, pikanie i brzęczenie, pierdolę to",u"Te wszystkie pierdolone piknięcia",u"Odgłosy menu, włącz albo wyłącz, w dupę jeża"},
         {u"UI sounds, beeps and bleeps, shit",u"All those bloody beeps",u"Menu noises, on or off, you bastard"}},
        {{u"Jaką melodią zaczynamy? Melodia startu, okno wyboru, chuj z tym",u"Muzyczka na dzień dobry, okno wyboru, do kurwy nędzy",u"Hejnał na start, okno wyboru, kurwa mać"},
         {u"Which tune starts us off? Start melody, popup, bloody hell",u"A little welcome jingle, popup, goddammit",u"Fanfare for the start, popup, fuck me"}},
        {{u"Co ma rzępolić w tle? Melodia w tle, okno wyboru, ja pierdolę",u"Podkład pod ten cyrk, okno wyboru, do chuja",u"Muzyka do windy, okno wyboru, jebany świat"},
         {u"What plays behind this shitshow? Background melody, popup",u"Soundtrack for this circus, popup, holy shit",u"Elevator music, popup, fucking hell"}},
        {{u"Jak szybko mam napierdalać? Tempo mowy, suwak",u"Tempo pierdolenia, suwak",u"Szybciej czy wolniej, ja jebię? Suwak"},
         {u"How fucking fast should I talk? Speech rate, slider",u"Talking speed, slider, bollocks",u"Faster or slower, damn it? Slider"}},
        {{u"Wysokość tego jebanego głosu, suwak",u"Piszcz albo buczę, wybieraj, kurwa, suwak",u"Jak wysoko mam pierdolić, suwak"},
         {u"Pitch of this damn voice, slider",u"Squeak or growl, pick one, slider, fuck it",u"How high should I yap, slider, bloody hell"}},
        {{u"Jak głośno mam się drzeć? Głośność mowy, suwak, w pizdu",u"Głośność gęby, suwak, kurwa",u"Ciszej czy głośniej, kurwa? Głośność mowy, suwak"},
         {u"How fucking loud should I yell? Speech volume, slider",u"Mouth volume, slider, piss off",u"Quieter or louder, for fuck’s sake? Speech volume, slider"}},
        {{u"Jak głośno ma brzęczeć to tło? Efekty, suwak, ożeż kurwa",u"Hałas efektów i melodii, suwak, ja jebię",u"Ile tego jazgotu? Efekty, suwak, o w mordę"},
         {u"How loud should the racket be? Effects, slider, for fuck’s sake",u"Effects and melody noise, slider, damn it",u"How much of that din? Effects, slider, shit"}},
        {{u"Ile mam pieprzyć przy rozruchu? Okno wyboru",u"Gadatliwość przy starcie, okno wyboru, jebana mać",u"Mam milczeć czy nawijać? Komunikaty rozruchu, okno wyboru, kurwa no"},
         {u"How much bloody blabbing while booting? Popup",u"Boot chattiness, popup, you bastard",u"Shut up or keep yapping? Boot messages, popup, bloody hell"}},
        {{u"Pamiętaj, co ostatnio odpaliłem, bo ja chuj wie",u"Zapamiętaj ostatni wybór, żebym nie musiał, pierdolę to",u"Pamięć do ostatniego startu, w dupę jeża"},
         {u"Remember what I last fucking booted",u"Remember the last fucking choice so I don’t have to",u"Memory of the last start, goddammit"}},
        {{u"Ile mam czekać, zanim to ruszy samo? Okno wyboru, chuj z tym",u"Odliczanie do autostartu, okno wyboru, do kurwy nędzy",u"Cierpliwość, w sekundach, okno wyboru, kurwa mać"},
         {u"How long before this shit starts itself? Popup",u"Countdown to the bloody auto-start, popup",u"Patience, in seconds, popup, fuck me"}},
        {{u"Odpal to raz przy następnym starcie, ja pierdolę",u"Jednorazowy skok w bok przy następnym starcie, do chuja",u"Tylko raz, jebany świat, następnym razem to"},
         {u"Boot this once on the next start, holy shit",u"One-off detour on the next start, fucking hell",u"Just once, next time, this bastard"}},
        {{u"Jak mam trąbić o paskach postępu? Okno wyboru, w pizdu",u"Paski postępu, piszczeć czy gadać, okno wyboru, kurwa",u"Postęp, ile mam o nim nadawać, okno wyboru, ożeż kurwa"},
         {u"How should I nag about the damn progress bars? Popup",u"Progress bars, beep or talk, popup, bollocks",u"Progress, how much yapping, popup, piss off"}},
        {{u"Mam gadać od samego włączenia tego pudła, ja jebię",u"Mowa od guzika zasilania, o w mordę",u"Gadanie od pierwszej sekundy, jebana mać"},
         {u"Talk from the damn power button on",u"Speech from the power switch, for fuck’s sake",u"Talking from second one, fuck yes"}},
        {{u"Powitanie, lista, jak ma cię ten złom witać, kurwa",u"Jak cię mam, kurwa, witać, lista",u"Powitanie dla łamagi, lista, ja pierdolę"},
         {u"Welcome, list, how this junk greets you, damn it",u"How the fuck should I greet you, list",u"Welcome for numpties, list, bloody hell"}},
        {{u"Ustawienia czytnika ekranu, podmenu, jak ma, kurwa, gadać",u"Głos czytnika ekranu, podmenu, ja pierdolę",u"Czytnik ekranu, podmenu, pokręć mu gałkami, łamago"},
         {u"Screen reader settings, submenu, how the fuck it talks",u"Screen reader voice, submenu, bloody hell",u"Screen reader, submenu, twiddle its knobs, numpty"}},
        {{u"Szczegółowość, podmenu, ile mam, kurwa, gadać",u"Ile szczegółów, podmenu, ja pierdolę",u"Gadatliwość, podmenu, wybieraj, łamago"},
         {u"Detail, submenu, how much the fuck I say",u"How many details, submenu, bloody hell",u"Chattiness, submenu, pick, numpty"}},
        // r397: the synthesizer rows of r396d (settings page).
        {{u"Syntezator, okno wyboru, kto ma ci, kurwa, gadać",u"Którym gardłem mam pierdolić, syntezator, okno wyboru",u"Syntezator mowy, okno wyboru, wybieraj, łamago"},
         {u"Synthesizer, popup, who the fuck does the talking",u"Which throat should I yap with, synthesizer, popup",u"Speech synthesizer, popup, pick one, numpty"}},
        {{u"Głos syntezatora, okno wyboru, czyim, kurwa, głosem",u"Który głos ma ci bluzgać, okno wyboru, ja pierdolę",u"Głos, okno wyboru, wybierz wreszcie, do chuja"},
         {u"Synthesizer voice, popup, whose fucking voice",u"Which voice should swear at you, popup, bloody hell",u"Voice, popup, just pick one, goddammit"}},
    };
    static const ExplicitRow melodyRows[]={
        {{u"Cisza w chuj, bez melodii",u"Żadnego grania, wyłączone, kurwa no",u"Wyłącz to rzępolenie, pierdolę to"},{u"Silence, damn it, no melody",u"No fucking music, off",u"Kill the noise, off, shit"}},
        {{u"Pierwsza melodia, w dupę jeża",u"Melodia numer jeden, chuj z tym",u"Jedynka z playlisty, do kurwy nędzy"},{u"The first melody, you bastard",u"Melody number one, fuck yeah",u"Track one, bloody hell"}},
        {{u"Druga melodia, kurwa mać",u"Dwójka z playlisty, ja pierdolę",u"Melodia numer dwa, do chuja"},{u"The second bloody melody",u"Track two, goddammit",u"Melody number two, fuck me"}},
        {{u"Trzecia jebana melodia",u"Trójka, ta jebana",u"Melodia numer trzy, jebany świat"},{u"The third melody, holy shit",u"Track three, the fucker",u"Melody number three, fucking hell"}},
        {{u"Czwarta melodia, do diabła, w pizdu",u"Melodia numer cztery, w dupę jeża",u"Czwórka z playlisty, kurwa"},{u"The fourth melody, bollocks",u"Melody number four, piss off",u"Track four, for fuck’s sake"}},
    };
    static const ExplicitRow languageRows[]={
        {{u"Niech język sam się wybierze, ożeż kurwa",u"Automatycznie, nie chce mi się wybierać, ja jebię",u"Język z automatu, o w mordę"},{u"Let the language pick itself, damn it",u"Automatic, can’t be arsed",u"Language on autopilot, shit"}},
        {{u"Angielski, jebana mać",u"English, kurwa, kurde balans",u"Po angielsku, fucking English"},{u"Fucking English",u"English, you bastard",u"The Queen’s bloody English"}},
        {{u"Polski, kurwa mać",u"Po polsku, jak Pan Bóg przykazał, pierdolę to",u"Polszczyzna, ta prawdziwa, kurwa mać"},{u"Polish, bloody hell",u"Polish, goddammit",u"Polish, kurwa"}},
    };
    static const ExplicitRow confirmRows[]={
        {{u"A chuj z tym, anuluj.",u"Nie, w dupę jeża, zostaw.",u"Spierdalam od tego, anuluj."},{u"Sod this shit, cancel.",u"Hell no, leave it.",u"Fuck that, cancel."}},
        {{u"Tak, jedziemy z tym gównem.",u"Dawaj, chuj z tym, potwierdzam.",u"Rób to, w pizdu z wahaniem."},{u"Yes, let’s do this shit.",u"Go on, fucking confirm.",u"Do it, fuck me."}},
    };
    static const ExplicitRow noticeRows[]={
        {{u"Dobra, zamknij ten komunikat, do kurwy nędzy",u"Zamknij to, już wiem, kurwa mać",u"Spierdalaj, komunikacie"},{u"Fine, close the damn message",u"Close it, I get it, holy shit",u"Piss off, message"}},
    };
    static const ExplicitRow toolRows[]={
        {{u"Co to za pierdolnik",u"Na jakim złomie siedzimy, ja pierdolę",u"Metryczka tego pudła, do chuja"},{u"What the hell is this computer",u"What junk are we sitting on, fucking hell",u"This box’s ID card, bollocks"}},
        {{u"Która jest godzina, jebany świat",u"Zegarek firmware, sprawdź, w pizdu",u"Data i godzina, jak już musisz, kurwa"},{u"What fucking time is it",u"Firmware watch, check it, piss off",u"Date and time, if you must, for fuck’s sake"}},
        {{u"Przestaw ten jebany zegar, podmenu",u"Cofnij albo przesuń czas, ożeż kurwa, podmenu",u"Nastaw tego zegarmistrza, podmenu, ja jebię"},{u"Fix the clock, submenu, damn it",u"Turn time back or forth, shit, submenu",u"Set the bloody clock, submenu"}},
        {{u"Kiedy to pudło ma się samo obudzić, podmenu, o w mordę",u"Budzik dla tego chujowego komputera, podmenu",u"Pobudka dla złomu, podmenu, jebana mać"},{u"When should this box wake itself up, submenu, you bastard",u"Alarm clock for the computer, submenu, bloody hell",u"Wake-up call for the junk, submenu, goddammit"}},
        {{u"Zmienne firmware, lista, kurwa no",u"Zasrana szuflada ze zmiennymi firmware, lista",u"Co firmware chowa w zmiennych, lista, pierdolę to"},{u"Firmware variables, list, fuck me",u"The firmware variables drawer, list, holy shit",u"What firmware hides in variables, list, fucking hell"}},
        {{u"Sterowniki UEFI, lista, w dupę jeża",u"Kto tu, chuj z tym, steruje, lista",u"Spis sterowników tego cyrku, lista, do kurwy nędzy"},{u"UEFI drivers, list, bollocks",u"Who the hell is driving, list",u"Driver roll call of this circus, list, piss off"}},
        {{u"Czy ten szmelc ma jakieś aktualizacje, kurwa mać",u"Aktualizacje firmware, jeśli jakieś są, ja pierdolę",u"ESRT, czyli co da się tu załatać, do chuja"},{u"Does this junk have any updates, for fuck’s sake",u"Firmware updates, if any, damn it",u"ESRT, what can be patched here, shit"}},
        {{u"Podłącz wszystkie sterowniki na chama, jebany świat",u"Obudź wszystkie urządzenia, w pizdu",u"Połącz sterowniki ze wszystkim, co się rusza, kurwa"},{u"Force-connect every bloody driver",u"Wake every device up, you bastard",u"Hook drivers to any fucking thing that moves"}},
        {{u"Odpal jakiś plik EFI z dysku, lista, kurwa mać",u"Grzebanie w plikach za programem EFI, lista, ja pierdolę",u"Znajdź sobie ten zasrany program EFI, lista"},{u"Run some EFI file off a disk, list, fuck it",u"Dig through files for an EFI program, list, bloody hell",u"Go find your damn EFI program, list"}},
        {{u"Zrestartuj ten zjebany grat",u"Restart, bo ręce opadają, ożeż kurwa",u"Uruchom ponownie ten złom, ja jebię"},{u"Restart this damn thing",u"Restart, because I fucking give up",u"Reboot this heap, bloody hell"}},
        {{u"Wyłącz to pieprzone pudło",u"Gaś światło, wyłączamy, o w mordę",u"Wyłącz, niech odpocznie, jebana mać"},{u"Switch off this fucking box",u"Lights out, shutting down, goddammit",u"Turn it off, let the bastard rest"}},
        {{u"Restart prosto do ustawień firmware, kurwa no",u"Restart i do bebechów UEFI, pierdolę to",u"Wjeżdżamy w setup firmware po restarcie, w dupę jeża"},{u"Restart straight into firmware setup, fuck me",u"Restart into the UEFI guts, holy shit",u"Reboot and barge into firmware setup, fucking hell"}},
    };
#define HAK_ROWS(a) a,sizeof(a)/sizeof(*(a))
    switch(page) {
        case MenuPage::main:return PickExplicit(HAK_ROWS(mainRows),row,pl);
        case MenuPage::settings:return PickExplicit(HAK_ROWS(settingsRows),row,pl);
        case MenuPage::startMelody:case MenuPage::backgroundMelody:return PickExplicit(HAK_ROWS(melodyRows),row,pl);
        case MenuPage::language:return PickExplicit(HAK_ROWS(languageRows),row,pl);
        case MenuPage::confirm:return PickExplicit(HAK_ROWS(confirmRows),row,pl);
        case MenuPage::notice:return PickExplicit(HAK_ROWS(noticeRows),row,pl);
        case MenuPage::tools:return PickExplicit(HAK_ROWS(toolRows),row,pl);
        default:return nullptr;
    }
#undef HAK_ROWS
}
}
