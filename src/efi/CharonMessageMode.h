#pragma once
#include <stddef.h>
#include <stdint.h>
#include "BootMessages.h"
namespace hakefi {
// Feed only from the menu key loop, never from a future text-editing control.
struct MessageModeSequence {
    unsigned matched=0;
    bool Feed(uint16_t character,bool menuContext=true) {
        if(!menuContext) {matched=0;return false;}
        if(character>=u'A' && character<=u'Z')character+=u'a'-u'A';
        const char16_t sequence[]=u"qrwa";
        if(character!=sequence[matched]) {matched=character==u'q'?1:0;return false;}
        if(++matched==4) {matched=0;return true;}return false;
    }
};
// Match complete authored messages, including inside the initial greeting.
// Appended EFI codes and dismissal help do not cause a second generic prefix.
inline bool ContainsExplicitBootMessage(const char16_t* text) {
    for(unsigned problem=0;problem<=unsigned(BootProblem::loaderReturned);++problem) {
        for(unsigned language=0;language<2;++language) {
            const auto* message=BootMessage(BootProblem(problem),language!=0,true);
            for(const auto* at=text;*at;++at) {
                size_t i=0;while(message[i] && at[i] && message[i]==at[i])++i;
                if(!message[i])return true;
            }
        }
    }
    return false;
}
inline bool StartsWithEntry(const char16_t* text,const char16_t* head,size_t& length) {
    length=0;while(head[length] && text[length]==head[length])++length;
    return !head[length];
}
// Variety counter: bumped once per key in the menu (and per own message in
// the screen reader), so one key press shows and says the same wording, and
// the next press may say it differently.
inline uint32_t& ExplicitVariety() {static uint32_t value=0;return value;}
inline const char16_t* ExplicitAnnouncement(const char16_t* text,size_t& matched) {
    matched=0;
    struct Entry {const char16_t* normal;const char16_t* styled[3];};
    static const Entry entries[]={
        {u"Tryb dosadnych komunikatów włączony.",{u"No i chuj! Skoro tak chcesz, to jedziemy z koksem, kurwa!",u"No i chuj, sam tego chciałeś. Od teraz gadam po ludzku, kurwa.",u"No i chuj! Koniec grzeczności, grzeczność idzie w pizdu."}},
        {u"Explicit messages enabled.",{u"Well, fuck it! If that’s what you want, let’s fucking go!",u"Fuck it, you asked for this. No more Mr Nice Guy.",u"Right, the gloves are off, motherfucker."}},
        {u"Nie potwierdzono zapisu; bieżące ustawienia niezmienione.",{u"Szlag by to trafił. Nie potwierdzono zapisu; ustawienia zostają, jak były.",u"Kurwa, zapis się nie potwierdził. Nic nie zmieniłem.",u"No żesz w mordę, zapis nie przeszedł. Ustawienia bez zmian."}},
        {u"Save not confirmed; current settings unchanged.",{u"Well, bollocks. The save couldn’t be confirmed; settings unchanged.",u"Shit, the save didn’t stick. Nothing changed.",u"Fuck me, the save failed. Settings stay as they were."}},
        {u"Ustawienia zapisane",{u"Zapisane, do chuja. Ustawienia zostają.",u"Klepnięte, kurwa. Zapisane.",u"Poszło do pamięci, nie marudź, kurwa mać."}},
        {u"Settings saved",{u"Fucking saved. Those settings will stick.",u"Saved, damn it.",u"Locked in, stop whining, you twat."}},
        {u"Koniec listy",{u"Dupa, dalej nic nie ma. Koniec listy.",u"Ściana, kurwa. Koniec listy.",u"No i co tak walisz? Tu się lista kończy, ja pierdolę."}},
        {u"List boundary",{u"Shit, that’s the end of the list.",u"Brick wall, mate. End of the list, for fuck’s sake.",u"Stop hammering, there’s fuck all past here."}},
        {u"Urządzenia odświeżone",{u"Wpisy urządzeń odświeżone, kurwa.",u"Przeskanowane, graty na miejscu, kurwa.",u"Odświeżone, ja pierdzielę, możesz wybierać."}},
        {u"Devices refreshed",{u"Those fucking device entries are refreshed.",u"Rescanned, the junk is all there, shit.",u"Refreshed, damn it, pick one."}},
        {u"Wymagane potwierdzenie. Domyślnie Anuluj.",{u"Na pewno chcesz to, kurwa, zrobić? Domyślnie Anuluj.",u"Zastanów się dwa razy. Domyślnie Anuluj, chuj z tym.",u"Serio? Potwierdź, bo domyślnie Anuluj, kurwa mać."}},
        {u"Confirmation required. Cancel is selected.",{u"Are you sure you want to fucking do this? Cancel is selected.",u"Think twice, genius. Cancel is selected, fuck it.",u"Seriously? Confirm, Cancel is selected, for fuck’s sake."}},
        {u"Operacja zakończona",{u"Ta pieprzona operacja zakończona.",u"Zrobione, kurwa.",u"Załatwione, idź dalej, w chuj."}},
        {u"Operation completed",{u"That fucking operation completed.",u"Done, damn it.",u"Sorted, move along, shit."}},
        {u"Witaj w Charon. Menu opcji.",{u"O, kurwa, znowu ty. Menu opcji, grzeb sobie.",u"Witaj w Charonie, łamago. Menu opcji.",u"No i przylazł. Menu opcji, nie spierdol niczego."}},
        {u"Welcome to Charon. Options menu.",{u"Oh fuck, you again. Options menu, dig in.",u"Welcome to Charon, numpty. Options menu.",u"Look who crawled in. Options menu, don’t break shit."}},
        {u"Witaj w Charon.",{u"No i znowu tu jesteś, ja pierdolę.",u"Siema, łamago, Charon słucha.",u"O w mordę, ktoś włączył ten złom."}},
        {u"Welcome to Charon.",{u"And here you are again, for fuck’s sake.",u"Hey, numpty, Charon’s listening.",u"Holy shit, someone switched this junk on."}},
        {u"Strzałki: wybór  Enter: otwórz  Esc: wstecz",{u"Strzałkami wybierasz, Enter otwiera, Esc spierdala wstecz",u"Strzałki do łażenia, Enter do wchodzenia, Esc do ucieczki, cykorze, w pizdu",u"Strzałki, Enter, Esc, nawet małpa by ogarnęła, kurwa jego mać"}},
        {u"Arrows: select  Enter: open  Esc: back",{u"Arrows pick, Enter opens, Esc fucks off back",u"Arrows to wander, Enter to go in, Esc to run away, chicken shit",u"Arrows, Enter, Esc, even a monkey gets it, for fuck’s sake"}},
        {u"Enter: chwyć  Strzałki: wybór  Esc: wstecz",{u"Enter łapie, strzałki łażą, Esc spierdala",u"Chwyć Enterem, nie bój się, nie gryzie, do chuja",u"Enter chwyta, Esc wraca, reszta to strzałki, baranie"}},
        {u"Enter: grab  Arrows: select  Esc: back",{u"Enter grabs, arrows wander, Esc fucks off",u"Grab it with Enter, it won’t bite, damn it",u"Enter grabs, Esc goes back, the rest is arrows, dumbass"}},
        {u"Strzałki: przenieś  Enter: upuść  Esc: anuluj",{u"Strzałkami tachasz, Enter rzuca, Esc odkręca to gówno",u"Nieś to, kurwa, strzałkami i upuść Enterem",u"Przesuwaj, upuszczaj, a jak spierdolisz, to Esc"}},
        {u"Arrows: move  Enter: drop  Esc: cancel",{u"Arrows haul it, Enter drops it, Esc undoes the crap",u"Carry the bastard with arrows, drop with Enter",u"Move, drop, and if you fuck it up, Esc"}},
        {u"Ustawić jako automatyczny cel Kontynuuj:",{u"Mam to, kurwa, odpalać samo po „Kontynuuj”:",u"Ten szajs ma być twoim domyślnym startem? Chodzi o:",u"Zrobić z tego twój jebany cel startu:"}},
        {u"Set as the automatic Continue target:",{u"Should I fucking boot this on “Continue”:",u"Make this crap your default start? That’s:",u"Turn this into your bloody start target:"}},
        {u"Uruchomić raz przy następnym starcie Charona:",{u"Odpalić to gówno raz, przy następnym starcie:",u"Jednorazowy wyskok przy następnym starcie, kurwa:",u"Raz, tylko raz, następnym razem odpalić, kurwa mać:"}},
        {u"Start once on the next Charon start:",{u"Boot this crap once, next start:",u"A one-off fucking detour next start:",u"Just once, next time, boot, fuck:"}},
        {u"Wyłączyć mowę od włączenia komputera?",{u"Mam się, kurwa, zamknąć od włączenia komputera?",u"Zakneblować mnie przy starcie, serio, pojebie?",u"Wyłączyć mi gębę od guzika? No dobra, łamago."}},
        {u"Turn off speech from power-on?",{u"Should I shut the fuck up from power-on?",u"Gag me at start-up, seriously, dumbass?",u"Shut my mouth from the power button? Fine, numpty."}},
        {u"Włączyć mowę od włączenia komputera?",{u"Mam pierdolić od samego włączenia komputera?",u"Gadanie od guzika zasilania, chcesz tego, kurwa?",u"Włączyć mi gębę od pierwszej sekundy, do chuja?"}},
        {u"Turn on speech from power-on?",{u"Should I yap from the fucking power button on?",u"Talking from second one, you sure you want that shit?",u"Open my gob from the very first second, damn it?"}},
        {u"Zapisać nową kolejność rozruchu w firmware?",{u"Wpierdolić tę nową kolejność do firmware?",u"Zapisać ten twój bajzel w kolejności, łamago?",u"Klepnąć nową kolejność, kurwa?"}},
        {u"Save the new boot order to firmware?",{u"Shove this new boot order into firmware, shithead?",u"Save your boot order mess, for fuck’s sake?",u"Lock in the new fucking order?"}},
        {u"Uruchomić oczekujący wybór rozruchu?",{u"Odpalić ten zaległy rozruch, czy dalej będziesz się gapić, pojebie?",u"Puścić czekający rozruch, kurwa?",u"Startujemy z tym, co czeka, ciołku?"}},
        {u"Run the pending boot selection?",{u"Run that overdue boot, or keep staring, you prick?",u"Fire the pending boot, for fuck’s sake?",u"Start what’s waiting, numpty?"}},
        {u"Zastosować oczekujące ustawienia firmware?",{u"Wjebać czekające ustawienia do firmware?",u"Zastosować te zaległe zmiany, łamago?",u"Klepnąć ustawienia firmware, kurwa?"}},
        {u"Apply the pending firmware settings?",{u"Shove the pending settings into firmware, damn it?",u"Apply those overdue changes, numpty?",u"Slap the firmware settings in, damn it?"}},
        {u"Zastosować oczekującą zmianę kluczy Secure Boot?",{u"Podmienić klucze Secure Boot? Jak spierdolisz, to nie płacz.",u"Grzebiemy w kluczach Secure Boot, serio, kurwa?",u"Zmiana kluczy, ostatnia szansa, żeby spękać, ciołku?"}},
        {u"Apply the pending Secure Boot key update?",{u"Swap the Secure Boot keys? If you fuck it up, don’t cry.",u"Messing with Secure Boot keys, seriously, you muppet?",u"Key swap, last chance to chicken out, dipshit?"}},
        {u"Uruchomić ponownie komputer teraz?",{u"Restartujemy ten złom, kurwa?",u"Przeładować to ustrojstwo, łamago?",u"Dać mu kopa i restart, ciołku?"}},
        {u"Restart the computer now?",{u"Restart this fucking junk?",u"Reboot this contraption, dumbass?",u"Kick it and restart, you prick?"}},
        {u"Wyłączyć komputer teraz?",{u"Ubić to pudło, kurwa?",u"Wyłączyć ten szajs, kurwa?",u"Gasimy, i chuj?"}},
        {u"Shut down the computer now?",{u"Kill this box, you muppet?",u"Switch this crap off, damn it?",u"Lights out, fuck it?"}},
        {u"Uruchomić ponownie komputer i otworzyć ustawienia firmware?",{u"Restart i prosto w bebechy firmware, ja pierdolę?",u"Przeładować i wjechać do setupu, kurwa?",u"Kop w restart i do ustawień firmware, łamago?"}},
        {u"Restart and open firmware setup?",{u"Restart and straight into the firmware guts, dumbass?",u"Reboot and barge into setup, damn it?",u"Kick it into firmware setup, you prick?"}},
        {u"Ustawić zegar firmware na:",{u"Przestawić ten jebany zegar na:",u"Mam ci, kurwa, cofnąć czas? Na:",u"Kręcimy zegarem, na, w dupę jeża:"}},
        {u"Set the firmware clock to:",{u"Set this fucking clock to:",u"Turn back time for you, dipshit? To:",u"Twist the fucking clock to:"}},
        {u"Ustawić budzenie komputera na:",{u"Ma się to pudło samo zerwać o, jebana jego mać:",u"Budzik dla złomu na, kurwa:",u"Pobudka, kurwa, o:"}},
        {u"Set the wake-up alarm to:",{u"Should this box jump awake at, damn it:",u"Alarm for the junk at, for fuck’s sake:",u"Wake the fucker up at:"}},
        {u"Wyłączyć budzenie komputera?",{u"Wypierdolić budzik?",u"Niech śpi, kurwa, wyłączyć budzenie?",u"Koniec z pobudkami, do kurwy nędzy?"}},
        {u"Turn the wake-up alarm off?",{u"Bin the fucking alarm?",u"Let it sleep, kill the alarm, you muppet?",u"No more wake-up calls, for fuck’s sake?"}},
        {u"Uruchamiam:",{u"Odpalam to gówno:",u"No to jazda, startuje, kurwa:",u"Kopa w dupę dostaje:"}},
        {u"Starting:",{u"Firing up this crap:",u"Off it fucking goes:",u"Kicking it into gear, for fuck’s sake:"}},
        {u"Powrót z powłoki UEFI.",{u"Wróciłeś z shella. I co, nauczyłeś się czegoś?",u"Spierdoliłeś z powłoki UEFI.",u"Koniec zabawy w hakera, powrót z powłoki, w chuj."}},
        {u"Returned from UEFI Shell.",{u"Back from the shell. Learn anything, genius?",u"You fucked off out of the UEFI Shell.",u"Hacker playtime over, back from the shell."}},
        {u"Chwycono:",{u"Złapane, kurwa:",u"Masz w łapie, w dupę jeża:",u"Trzymasz to gówno:"}},
        {u"Grabbed:",{u"Got the fucker:",u"In your paws, fuck:",u"Holding this crap:"}},
        {u"Upuszczono:",{u"Rzucone, kurwa:",u"Puszczone, kurwa:",u"Wyjebane na miejsce:"}},
        {u"Dropped:",{u"Dumped, for fuck’s sake:",u"Let go, damn it:",u"Chucked into place, shit:"}},
        {u"Anulowano przenoszenie:",{u"Spękałeś, wraca na miejsce, ja pierdolę:",u"Odwrót, kurwa:",u"Nie to nie, wraca, w dupę jeża:"}},
        {u"Move cancelled:",{u"Chickened out, back it goes, damn it:",u"Retreat, damn it:",u"Fine, back where it was, for fuck’s sake:"}},
        {u"Niezapisane zmiany kolejności odrzucone.",{u"Twoje przetasowania poszły w pizdu, nic nie zapisałem.",u"Wyrzucone do kosza, nie zapisałeś, łamago.",u"Szkoda było klikać, zmiany przepadły, chuj z tym."}},
        {u"Unsaved order changes discarded.",{u"Your shuffling went down the shitter, nothing saved.",u"Binned, you didn’t save, numpty.",u"Waste of clicks, changes gone, for fuck’s sake."}},
        {u"Przywrócono kolejność z firmware.",{u"Wróciło, jak było, twoje grzebanie poszło w chuj.",u"Kolejność z firmware z powrotem, cofnięte, do kurwy nędzy.",u"Reset twojego bajzlu, kolejność jak w firmware, ja pierdolę."}},
        {u"Firmware order restored.",{u"Back as it was, your fiddling went to shit.",u"Firmware order back, undone, bloody hell.",u"Your mess reset to the firmware order, fuck it."}},
        {u"Wpis jest już na skraju listy",{u"Dalej się nie da, to już krawędź, kurwa",u"Skraj listy, nie przepchniesz dalej, kurwa jego mać",u"Ściana, ciołku, dalej nie pójdzie"}},
        {u"The entry is already at the end of the list",{u"Can’t go further, that’s the fucking edge",u"End of the list, you can’t shove it further, damn it",u"Wall, dipshit, it won’t go further"}},
        {u"Wartość graniczna",{u"Granica, kurwa, dalej nie",u"Koniec skali, łamago",u"Więcej się nie da, odpuść, ja pierdolę"}},
        {u"Limit reached",{u"Limit, damn it, no further",u"End of the scale, numpty",u"That’s it, give up, for fuck’s sake"}},
        {u"Głośność minimalna",{u"Ciszej to już tylko w trumnie, w pizdu",u"Minimum, kurwa",u"Szepczę już jak mysz pod miotłą, kurwa jego mać"}},
        {u"Minimum volume",{u"Any quieter and I’m fucking dead",u"Minimum, damn it",u"Whispering like a fucking mouse"}},
        {u"Głośność maksymalna, 100%",{u"Głośniej się nie da, ogłuchniesz, 100%, ja pierdolę",u"Na full, kurwa, 100%",u"Maksimum, sąsiedzi już dzwonią, 100%, do chuja"}},
        {u"Maximum volume, 100%",{u"Can’t go louder, you’ll go fucking deaf, 100%",u"Full fucking blast, 100%",u"Max, the neighbours are calling, 100%, shit"}},
        {u"Kolejność rozruchu.",{u"Kolejność rozruchu, ten twój bajzel, ja pierdolę.",u"Kolejność rozruchu, kurwa.",u"No to tasujemy kolejność rozruchu, w chuj."}},
        {u"Boot order.",{u"Boot order, your bloody mess.",u"Boot order, for fuck’s sake.",u"Right, let’s shuffle the boot order, for fuck’s sake."}},
        {u"Ustaw datę i godzinę zegara firmware.",{u"Kręcimy zegarem firmware, kurwa.",u"Ustawianie czasu dla opornych, do kurwy nędzy.",u"Zegar firmware, nie spierdol daty."}},
        {u"Set the firmware clock.",{u"Twisting the firmware clock, damn it.",u"Setting the time for dummies, shit.",u"Firmware clock, don’t fuck up the date."}},
        {u"Zmienne firmware, tylko odczyt.",{u"Zmienne firmware, tylko patrzeć, łapy przy sobie, kurwa mać.",u"Grzebiemy w zmiennych firmware, tylko odczyt, kurwa.",u"Zmienne firmware, słuchać wolno, ruszać nie, do kurwy nędzy."}},
        {u"Firmware variables, read only.",{u"Firmware variables, look but keep your paws off, fuck it.",u"Digging in firmware variables, read only, damn it.",u"Firmware variables, listen, don’t touch, for fuck’s sake."}},
        {u"Sterowniki UEFI, tylko odczyt.",{u"Sterowniki UEFI, tylko odczyt, nie dotykaj, ciołku.",u"Spis sterowników, kurwa, tylko do słuchania.",u"Sterowniki UEFI, słuchaj i nie ruszaj, kurwa."}},
        {u"UEFI drivers, read only.",{u"UEFI drivers, read only, hands off, dipshit.",u"Driver roll call, listen only, damn it.",u"UEFI drivers, listen and don’t touch, shit."}},
        {u"Komputer:",{u"Ten złom to, kurwa mać:",u"Siedzisz, kurwa, przy:",u"Twoje ustrojstwo to, jebana jego mać:"}},
        {u"Computer:",{u"This junk is a, shit:",u"You’re sitting at a fucking:",u"Your contraption is a, you bastard:"}},
        {u"Zegar firmware:",{u"Na zegarze tego złomu, ja pierdolę:",u"Firmware twierdzi, że jest, w dupę jeża:",u"Zegar, kurwa, mówi:"}},
        {u"Firmware clock:",{u"This junk’s clock says, for fuck’s sake:",u"Firmware reckons it’s, shit:",u"The fucking clock says:"}},
        {u"Budzenie wyłączone.",{u"Budzik wyłączony, złom śpi spokojnie, kurwa mać.",u"Budzenie wyłączone, i chuj.",u"Żadnych pobudek, wyłączone, do kurwy nędzy."}},
        {u"Wake-up alarm off.",{u"Alarm off, the junk sleeps tight, goddammit.",u"Alarm off, fuck it.",u"No wake-up calls, off, fuck it."}},
        {u"Budzenie włączone:",{u"Ten złom się sam zerwie, kurwa mać:",u"Budzik nastawiony, kurwa:",u"Pobudka ustawiona na, jebana jego mać:"}},
        {u"Wake-up alarm on:",{u"This junk will jump awake, fuck:",u"Alarm set, damn it:",u"Wake-up call at, damn it:"}},
        {u"Sterowniki połączone.",{u"Sterowniki podpięte na chama, do kurwy nędzy.",u"Połączone, kurwa, wszystko z wszystkim.",u"Spięte do kupy, ja pierdolę."}},
        {u"Drivers connected.",{u"Drivers forced in, bloody hell.",u"Everything hooked to everything, damn it.",u"Bolted together, you twat."}},
        {u"Lista jest pusta",{u"Pusto, kurwa, jak w lodówce studenta",u"Lista pusta, nic tu nie ma, ja pierdolę",u"Gówno tu jest, pusto"}},
        {u"The list is empty",{u"Empty as a student’s fridge, for fuck’s sake",u"Nothing here, fuck all",u"Empty, shit all here"}},
        {u"Firmware nie publikuje tablicy zasobów aktualizacji (ESRT).",{u"To ustrojstwo nie ma żadnej tablicy aktualizacji, chuj z aktualizacjami.",u"ESRT brak, producent ma cię w dupie.",u"Żadnych aktualizacji przez kapsuły, producent olał temat, kurwa mać."}},
        {u"Firmware publishes no update resource table (ESRT).",{u"This contraption has no update table, fuck updates.",u"No ESRT, the maker doesn’t give a shit about you.",u"No capsule updates, the vendor couldn’t be arsed."}},
        {u"Operacja odrzucona lub niedostępna",{u"Chuja, firmware odmówiło",u"Nie da się, kurwa",u"Odrzucone, firmware ma cię w dupie"}},
        {u"Operation rejected or unavailable",{u"Fuck no, firmware refused",u"Can’t be done, damn it",u"Rejected, firmware doesn’t give a shit"}},
        {u"Firmware nie przyjęło tej godziny budzenia.",{u"Firmware wypierdoliło tę godzinę budzenia.",u"Ta godzina? Chuja, firmware jej nie chce.",u"Zjebana godzina, firmware odmawia."}},
        {u"Firmware did not accept this alarm time.",{u"Firmware binned that alarm time, shit.",u"That time? Fuck no, firmware won’t take it.",u"Shit time, firmware refuses."}},
        {u"Program „",{u"Ten twój program pierdolony „",u"Gówno zwane „",u"Programik pierdolony „"}},
        {u"The program “",{u"Your bloody program “",u"The piece of crap called “",u"That little program fucking “"}},
        {u"Góra ekranu",{u"Góra ekranu, wyżej tylko sufit, kurwa",u"Sam szczyt, kurwa, dalej nie",u"Wyżej się nie da, łamago, góra ekranu"}},
        {u"Top of the screen",{u"Top of the screen, only the fucking ceiling above",u"The very top, damn it, no further",u"Can’t go higher, numpty, top of the screen"}},
        {u"Dół ekranu",{u"Dół ekranu, niżej tylko piwnica, kurwa",u"Samo dno, jak ty, ja pierdolę",u"Niżej się nie da, ciołku, dół ekranu"}},
        {u"Bottom of the screen",{u"Bottom of the screen, only the fucking basement below",u"Rock bottom, like you, for fuck’s sake",u"Can’t go lower, dipshit, bottom of the screen"}},
        {u"Pusty wiersz",{u"Pusty wiersz, chuj tu jest",u"Nic, kurwa, pusta linia",u"Pustka, jak w twojej głowie, do chuja"}},
        {u"Blank line",{u"Blank line, fuck all here",u"Nothing, damn it, empty line",u"Empty, like your head, shithead"}},
        {u"Wiersz",{u"Jesteś, kurwa, w wierszu",u"Kursor siedzi, łamago, w wierszu",u"Wiersz, do chuja,"}},
        {u"Line",{u"You’re in fucking line",u"The cursor sits, numpty, on line",u"Line, for fuck’s sake,"}},
        {u"Na ekranie nie ma zaznaczonej pozycji.",{u"Nic nie jest zaznaczone, kurwa, nie ma czego powtarzać.",u"Zaznaczenia brak, ślepy jesteś? Chuj, nie odpowiadaj.",u"Gówno zaznaczone, zero, nul."}},
        {u"Nothing is highlighted on the screen.",{u"Nothing’s fucking highlighted, nothing to repeat.",u"No highlight, shithead, nothing there.",u"Fuck all highlighted, zero, zilch."}},
        {u"Automatyczne czytanie wyłączone.",{u"Dobra, kurwa, zamykam mordę przy zmianach.",u"Cisza na zmianach, ja pierdolę, w końcu spokój.",u"Automat wyłączony, chuj z tym mieleniem."}},
        {u"Automatic reading off.",{u"Fine, I’ll shut the fuck up about changes.",u"Quiet on changes, fucking finally.",u"Auto reading off, fuck that noise."}},
        {u"Automatyczne czytanie włączone.",{u"No i znowu gadam o wszystkim, kurwa.",u"Automat włączony, będę pierdolić o każdej zmianie.",u"Wracam do nawijania, ja pierdolę."}},
        {u"Automatic reading on.",{u"Right, back to yapping about every fucking change.",u"Auto reading on, brace your arse.",u"Back to the bloody chatter."}},
        {u"Tempo",{u"Gadam teraz, kurwa, w tempie",u"Prędkość pierdolenia:",u"Tempo, do chuja,"}},
        {u"Rate",{u"I’m now fucking talking at",u"Fucking yapping speed:",u"Rate, for fuck’s sake,"}},
        {u"Tekst",{u"Ten zasrany tekst jest",u"Tekst, kurwa, jest",u"Literki, do chuja, są"}},
        {u"Text",{u"This shitty text is",u"The fucking text is",u"The bloody letters are"}},
        {u"Kolory tego miejsca nie są znane; tekst pochodzi z obrazu.",{u"Kolorów, kurwa, nie znam, to obrazek.",u"Chuj wie, jakie to kolory, wyczytane z obrazka.",u"Kolory? Z obrazka, ja pierdolę, nie wiem."}},
        {u"Colours here are unknown; the text comes from a picture.",{u"No fucking idea about colours, it’s a picture.",u"Shit knows the colours, read from a picture.",u"Colours? From a picture, damn it, dunno."}},
        {u"Okno",{u"Wyskoczyło jakieś okno, kurwa",u"Okienko, ja pierdolę",u"O w mordę, okno"}},
        {u"Window",{u"Some fucking window popped up",u"A bloody window",u"Holy shit, a window"}},
        {u"Program:",{u"Siedzisz, kurwa, w:",u"Ten szajs to:",u"Program, ja pierdolę:"}},
        {u"Czytnik mówi szczegółowo.",{u"Dobra, będę pierdolić ze szczegółami, sam chciałeś.",u"Szczegółowo, kurwa, jak dla debila.",u"Gadam teraz w chuj szczegółowo."}},
        {u"Reader verbosity detailed.",{u"Fine, I’ll fucking spell out every detail, you asked for it.",u"Detailed, like for an idiot.",u"I’ll now yap in shitloads of detail."}},
        {u"Czytnik mówi krótko.",{u"Krótko, kurwa, bez pierdolenia.",u"Dobra, mniej gadania, ja pierdolę.",u"Krótko i na temat, łamago."}},
        {u"Reader verbosity short.",{u"Short, no fucking waffle.",u"Fine, less yapping, damn it.",u"Short and to the point, numpty."}},
        {u"Tempo minimalne, 50%",{u"Wolniej się, kurwa, nie da. 50%.",u"Żółw by mnie wyprzedził. Minimum, 50%, jebany świat.",u"Już ślimaczę, 50%, chuj z tym."}},
        {u"Tempo maksymalne, 200%",{u"Szybciej już nie, bo się zesram. 200%.",u"Na pełnej kurwie, 200%.",u"Maksimum, 200%, nadążaj, kurwa mać."}},
        {u"Minimum rate, 50%",{u"Can’t go any fucking slower. 50%.",u"A snail would overtake me. 50%, bloody hell.",u"Crawling already, 50%, you twat."}},
        {u"Maximum rate, 200%",{u"Any faster and I’ll shit myself. 200%.",u"Flat fucking out, 200%.",u"Maximum, 200%, keep up, fuck it."}},
    };
    for(const auto& entry:entries) {size_t length=0;
        // Whole message, or the message followed by a code / closing hint.
        if(StartsWithEntry(text,entry.normal,length) && (!text[length] || text[length]==u' ' || text[length]==u'.' || (length && (entry.normal[length-1]==u':' || entry.normal[length-1]==u'„' || entry.normal[length-1]==u'“')))) {matched=length;return entry.styled[ExplicitVariety()%3];}}
    return nullptr;
}
// r373: explicit mode speaks like a person, not a fixed prefix. Authored
// versions match the start of a message (a status code or "Enter closes"
// may follow). Other text gets an interjection chosen by its tone (error,
// success, information) and varied by the text itself, so the screen and the
// voice say the same thing and consecutive messages do not repeat one word.
// Short labels (no sentence end) stay as they are; plain information gets an
// interjection only every other time. The technical message is never cut: if
// the destination cannot hold the interjection, it is dropped, not the cause.
inline bool ContainsText(const char16_t* text,const char16_t* part) {
    for(;*text;++text) {size_t i=0;while(part[i] && text[i]==part[i])++i;if(!part[i])return true;}
    return false;
}
enum class MessageTone {label,information,success,error};
inline MessageTone ToneOf(const char16_t* text) {
    const char16_t* errors[]={u"Błąd",u"błąd",u"odrzuc",u"niedostęp",u"nie obsługuje",u"Nie można",u"Nie udało",u"nie udało",u"Nie odczytano",u"Nie połączono",u"Nie potwierdzono",u"nie przyjęło",u"Nieprawidł",u"UEFI: 0x",
        u"rror",u"ailed",u"rejected",u"unavailable",u"Cannot",u"cannot",u"not supported",u"does not support",u"not read",u"not connected",u"not confirmed",u"did not"};
    for(const auto* word:errors)if(ContainsText(text,word))return MessageTone::error;
    const char16_t* successes[]={u"zakończon",u"Zapisan",u"zapisan",u"połączon",u"odświeżon",u"Przywrócono",u"completed",u"saved",u"connected",u"refreshed",u"restored"};
    for(const auto* word:successes)if(ContainsText(text,word))return MessageTone::success;
    for(const auto* at=text;*at;++at)if(*at==u'.' || *at==u'?' || *at==u'!')return MessageTone::information;
    return MessageTone::label;
}
inline uint32_t MessageHash(const char16_t* text) {
    uint32_t hash=2166136261u;for(;*text;++text) {hash^=*text;hash*=16777619u;}return hash;
}
inline const char16_t* Interjection(const char16_t* text,bool polish) {
    static const char16_t* plError[]={u"Kurwa mać. ",u"Ja pierdolę. ",u"Do kurwy nędzy. ",u"Szlag by to trafił. ",u"No żesz kurwa. ",u"Jasna cholera. ",u"O w mordę. ",
        u"Pierdolone ustrojstwo. ",u"Kurwa jego mać. ",u"Przejebane. ",u"Chuj by to strzelił. ",u"No ja pierdolę, znowu. ",
        u"Ten złom znowu się popisał, chuj z tym. ",u"Firmware, kurwa, jak zwykle na medal. ",u"Brawo, pudło, brawo, kurwa mać. ",
        u"Wszystko idzie w pizdu. ",u"Zjebane w chuj. ",u"Ja jebię, co za pierdolnik. ",u"O w dupę. ",u"Kurwa, no nie. ",u"Chuj wie, co tu się odjebało. ",
        u"Ja pierdolę, ale burdel. ",u"Kurwa, znowu? ",u"W dupę jeża. ",u"Ożeż ty w mordę. ",u"Pięknie, kurwa, po prostu pięknie. ",u"No to pozamiatane, do kurwy nędzy. "};
    static const char16_t* plSuccess[]={u"No i zajebiście. ",u"Załatwione, kurwa. ",u"I chuj, poszło. ",u"Zrobione w chuj dobrze. ",u"Kurwa, działa. ",u"Jest, kurwa! ",u"Poszło jak złoto, do kurwy nędzy. ",u"Zajebiście, jak w mordę strzelił. ",u"Sztos, kurwa. ",u"Ja pierdzielę, udało się. ",u"Ja pierdolę, nawet działa. ",u"No proszę, kurwa, poszło. "};
    static const char16_t* plInfo[]={u"Słuchaj no, kurwa. ",u"No dobra, kurwa. ",u"Uważaj, bo to ważne, ja jebię. ",u"Psiakrew. ",u"Patrz no, do diabła, kurwa mać. ",u"No to tak, kurwa. ",u"Słuchaj, bo nie będę, kurwa, powtarzać. ",u"Ogarnij to, ziom, do kurwy nędzy. ",
        u"Słuchaj, łamago. ",u"Uwaga, ciołku. ",u"Dobra, baranie, słuchaj. ",u"Ej, ty, tak, do ciebie mówię, chuj z tym. ",u"Nadstaw uszu, matole. ",u"Kurwa, skup się. "};
    static const char16_t* enError[]={u"Fucking hell. ",u"Shit. ",u"Oh, for fuck’s sake. ",u"Bloody hell. ",u"Goddammit. ",u"Well, that’s fucked. ",u"Son of a bitch. ",u"Bollocks. ",u"Fuck me. ",u"What a clusterfuck. ",u"Oh, for crying out loud, you twat. ",u"Fucking typical. ",
        u"This heap of junk strikes again, fuck it. ",u"Stellar work, firmware, as fucking usual. "};
    static const char16_t* enSuccess[]={u"Fucking done. ",u"Sorted, damn it. ",u"Hell yeah. ",u"Nailed the bastard. ",u"Fucking brilliant. ",u"Bloody marvellous. ",u"Holy shit, it works. "};
    static const char16_t* enInfo[]={u"Listen up, damn it. ",u"Right then, fuck it. ",u"Pay attention, for fuck’s sake. ",u"Okay, shit. ",u"Right, listen, fuck it. ",u"Listen, numpty. ",u"Oi, you, yes you, for fuck’s sake. ",u"Pay attention, dumbass. ",u"Focus, for fuck’s sake. "};
    auto pick=[](const char16_t* const* list,size_t count,uint32_t value) {return list[value%count];};
    const auto tone=ToneOf(text);const uint32_t hash=MessageHash(text)^(ExplicitVariety()*2654435761u);
    switch(tone) {
        case MessageTone::label:return u"";
        case MessageTone::information:
            return polish?pick(plInfo,sizeof(plInfo)/sizeof(*plInfo),hash>>1):pick(enInfo,sizeof(enInfo)/sizeof(*enInfo),hash>>1);
        case MessageTone::success:return polish?pick(plSuccess,sizeof(plSuccess)/sizeof(*plSuccess),hash):pick(enSuccess,sizeof(enSuccess)/sizeof(*enSuccess),hash);
        default:return polish?pick(plError,sizeof(plError)/sizeof(*plError),hash):pick(enError,sizeof(enError)/sizeof(*enError),hash);
    }
}
// Jabs said after the message, at the hardware's or the user's expense.
inline const char16_t* JabAfter(const char16_t* text,bool polish) {
    // Hardware and the user both get it: the user switched this mode on.
    static const char16_t* plError[]={u" Kto to, kurwa, programował, stażysta po dwóch piwach?",u" Pralka Frani ma lepszy firmware, chuj z tym.",u" Producent tego cudu powinien się wstydzić, kurwa mać.",
        u" Gratulacje dla inżynierów, kurwa ich mać.",u" Ten sprzęt to chyba z odzysku, do kurwy nędzy.",u" A ty coś, kurwa, za szybko klikasz.",u" No i po co to było, geniuszu, ja pierdolę?",
        u" Ty i ten komputer jesteście siebie warci, kurwa mać.",u" Ręce opadają, serio, do kurwy nędzy.",u" Brawo, matole, pięknie to spierdoliłeś.",u" Myślenie nie boli, baranie, spróbuj kiedyś.",
        u" Ćwoku, słuchałeś, co mówię, czy tylko walisz w klawisze?",u" Debil i złom, para idealna.",u" Weź się, kurwa, ogarnij, bo szkoda prądu.",
        u" Co ty w ogóle robisz w boot managerze, łamago?",u" Normalni ludzie po prostu włączają komputer, a ty tu grzebiesz, chuj z tym.",u" Komputer obsługujesz jak małpa granat, kurwa mać.",
        u" Babcia by to ogarnęła szybciej, do kurwy nędzy.",u" Z komputerem jesteś na ty czy na pan, ja pierdolę?",u" Wróć do liczydła, tam ci szło lepiej, kurwa mać.",u" Kto ci w ogóle dał dostęp do firmware, ciołku?"};
    static const char16_t* plSuccess[]={u" Nawet ten złom czasem daje radę, chuj z tym.",u" Kto by pomyślał, że to pudło umie słuchać, kurwa mać.",u" Cud, kurwa, normalnie cud.",
        u" No proszę, umiesz, jak chcesz, do kurwy nędzy.",u" Nawet ty tego nie spieprzyłeś.",u" Ślepej kurze też się trafi ziarno, chuj z tym.",u" Nie przyzwyczajaj się, matole, to fart.",
        u" Nawet małpa z klawiaturą by to zrobiła, ale gratuluję, kurwa mać.",u" Order z ziemniaka już w drodze, do kurwy nędzy.",u" Patrzcie go, informatyk się znalazł, chuj z tym."};
    static const char16_t* plInfo[]={u" Tylko tym razem słuchaj uważnie, dobra, kurwa?",u" Mówię raz, kurwa, nie będę powtarzać.",u" Rusz łbem, baranie.",u" Skup się, ćwoku, to proste jak budowa cepa.",
        u" Znowu tu przylazłeś? Nudzi ci się, łamago?",u" Nie masz co robić, że siedzisz w menu rozruchu, ja pierdolę?",u" Grzeb dalej, grzebaczu, może coś spierdolisz.",
        u" Spokojnie, nikt nie słyszy, jak się tu gubisz, kurwa mać.",u" Ludzie mają życie, a ty masz boot managera, do kurwy nędzy."};
    static const char16_t* enError[]={u" Who coded this shit, a drunk raccoon?",u" A toaster has better firmware, for fuck’s sake.",u" The manufacturer should be fucking ashamed.",u" Hardware of the year, my arse.",
        u" Nice one, genius, you twat.",u" You and this box deserve each other, fuck it.",u" Brilliant move, Einstein, for fuck’s sake.",u" Way to fuck it up, dumbass.",u" Use your fucking head next time, numpty.",
        u" A dipshit and a junk box, perfect match.",u" What are you even doing in a boot manager, you muppet?",u" Normal people just switch the computer on, you twat.",
        u" You handle a computer like a monkey with a grenade, fuck it.",u" My gran would’ve sorted this faster, for fuck’s sake.",u" Who gave you firmware access, dipshit?"};
    static const char16_t* enSuccess[]={u" Even this junk gets lucky sometimes, you twat.",u" Who knew this box could listen, fuck it.",u" A fucking miracle.",u" Look at you, not fucking it up for once.",
        u" Even a blind squirrel finds a nut, for fuck’s sake.",u" Don’t get used to it, dumbass.",u" A monkey with a keyboard could do it, but well fucking done.",u" Look at you, a proper fucking hacker."};
    static const char16_t* enInfo[]={u" Try to keep up this time, shithead.",u" I’m saying this once, so fucking listen.",u" Use your head, numpty.",u" Focus, dipshit, it’s not rocket science.",u" Back here again? Bored, are we, for fuck’s sake?",
        u" Nothing better to do than sit in a boot menu, you sad fuck?",u" Keep poking, maybe you’ll break something, you twat.",u" People have lives; you have a boot manager, fuck it."};
    const auto tone=ToneOf(text);const uint32_t hash=(MessageHash(text)^(ExplicitVariety()*2246822519u))>>8;
    auto pick=[](const char16_t* const* list,size_t count,uint32_t value) {return list[value%count];};
    // Only some messages get a jab, so they stay a surprise; information rarely.
    if(tone==MessageTone::error && hash%2==0)return polish?pick(plError,sizeof(plError)/sizeof(*plError),hash/2):pick(enError,sizeof(enError)/sizeof(*enError),hash/2);
    if(tone==MessageTone::success && hash%2==0)return polish?pick(plSuccess,sizeof(plSuccess)/sizeof(*plSuccess),hash/2):pick(enSuccess,sizeof(enSuccess)/sizeof(*enSuccess),hash/2);
    if(tone==MessageTone::information && hash%3==0)return polish?pick(plInfo,sizeof(plInfo)/sizeof(*plInfo),hash/3):pick(enInfo,sizeof(enInfo)/sizeof(*enInfo),hash/3);
    return u"";
}
// r374: advice inside any message (how to use keys, defaults) is replaced
// wherever it appears, not only at the start; screen text is never passed here.
inline const char16_t* ExplicitHints(const char16_t* text) {
    struct Hint {const char16_t* normal;const char16_t* styled[3];};
    static const Hint hints[]={
        {u"Naciśnij spację, aby otworzyć menu opcji",{u"Wciśnij, kurwa, spację, jak chcesz menu, bo sam się nie domyślę",u"Spacja otwiera menu, nawet ty dasz radę, łamago",u"Walnij w spację, ciołku, jak chcesz grzebać w opcjach"}},
        {u"Press Space to open the options menu",{u"Hit fucking Space if you want the menu, I’m not a mind reader",u"Space opens the menu, even you can manage that, numpty",u"Smack Space, dipshit, if you want to poke the options"}},
        {u"Enter chwyta wpis, strzałki go przenoszą, Enter upuszcza.",{u"Enterem łapiesz, strzałkami tachasz, Enterem rzucasz. Prostsze niż obsługa młotka, matole.",u"Złap Enterem, przesuń strzałkami, puść Enterem. Nawet szympans by ogarnął, kurwa.",u"Enter, strzałki, Enter, kurwa, trzy kroki, nie pomyl ich."}},
        {u"Enter grabs an entry, arrows move it, Enter drops it.",{u"Enter grabs, arrows haul, Enter drops. Easier than a hammer, dumbass.",u"Grab with Enter, shove with arrows, drop with Enter. A chimp could fucking do it.",u"Enter, arrows, Enter, three fucking steps, don’t mix them up."}},
        {u"Strzałki przenoszą, Enter upuszcza, Escape anuluje.",{u"Strzałkami tachasz, Enterem rzucasz, a jak spierdolisz, to Escape.",u"Przesuwaj strzałkami, puść Enterem, spanikujesz, to Escape, cykorze, chuj z tym.",u"Strzałki, Enter, Escape, nie kombinuj, kurwa."}},
        {u"Arrows move, Enter drops, Escape cancels.",{u"Arrows haul, Enter drops, and if you fuck it up, Escape.",u"Shove with arrows, drop with Enter, panic with Escape, chicken, bloody hell.",u"Arrows, Enter, Escape, don’t get clever, damn it."}},
        {u"Wybierz Zapisz kolejność, aby zachować.",{u"A teraz Zapisz kolejność, bo inaczej pójdzie w pizdu.",u"Zapisz kolejność, łamago, bo zaraz wszystko przepadnie.",u"Bez Zapisz kolejność to sobie możesz tak tasować do usranej śmierci, do kurwy nędzy."}},
        {u"Choose Save order to keep it.",{u"Now pick Save order or it all goes down the shitter.",u"Save order, numpty, or it’s all gone.",u"Without Save order you can shuffle till you fucking die."}},
        {u"Charon zawsze pierwszy.",{u"Charon i tak zostaje pierwszy, nie podskakuj, ja pierdolę.",u"Charon zawsze pierwszy, tego nie ruszysz, kurwa.",u"Charon na czele, reszta to twoje grzebanie, w chuj."}},
        {u"Charon always first.",{u"Charon stays first anyway, don’t get cocky, goddammit.",u"Charon’s always first, you can’t fucking touch that.",u"Charon leads, the rest is your mess, shit."}},
        {u"Strzałki w lewo i w prawo zmieniają wartość, Zapisz na końcu listy.",{u"Lewo, prawo, kręcisz wartością, a Zapisz masz na końcu, gamoniu.",u"Strzałkami w boki zmieniasz, na dole Zapisz, nie przegap, kurwa.",u"W lewo mniej, w prawo więcej, proste jak budowa cepa. Zapisz na końcu, chuj z tym."}},
        {u"Left and Right change a value, Save is at the end of the list.",{u"Left, right, twiddle the value, Save is at the bottom, numpty.",u"Arrows sideways change it, Save at the end, don’t miss it, damn it.",u"Left less, right more, dead fucking simple. Save at the end."}},
        {u"Strzałki w lewo i w prawo zmieniają wartość; na końcu Ustaw budzenie i Wyłącz budzenie.",{u"Lewo, prawo, kręcisz godziną, a na dole Ustaw budzenie albo Wyłącz budzenie, gamoniu.",u"Strzałkami w boki zmieniasz, na końcu ustawiasz albo wyłączasz budzik, kurwa.",u"W lewo mniej, w prawo więcej, a na końcu decydujesz, czy złom ma się budzić, kurwa mać."}},
        {u"Left and Right change a value; Set alarm and Turn the alarm off are at the end.",{u"Left, right, twiddle the time, then Set alarm or Turn it off at the bottom, numpty.",u"Arrows sideways change it, set or kill the alarm at the end, damn it.",u"Left less, right more, then decide if this junk wakes up, fuck it."}},
        {u"Enter czyta szczegóły, PageUp i PageDown przeskakują o 10.",{u"Enter gada szczegóły, PageUp i PageDown skaczą o dziesięć, żebyś nie zdechł ze starości, ja pierdolę.",u"Enter na szczegóły, PageDown, jak ci się nudzi, kurwa.",u"Enter, a jak masz dość, to PageUp i PageDown, po dziesięć naraz, leniu, w chuj."}},
        {u"Enter reads details, Page Up and Page Down jump by 10.",{u"Enter spills the details, Page Up and Page Down jump by ten so you don’t die of old age, dumbass.",u"Enter for details, Page Down when you’re fucking bored.",u"Enter, and when you’ve had enough, Page Up or Down, ten at a time, lazy arse."}},
        {u"Charon pozostaje pierwszy.",{u"Charon i tak zostaje na górze, frajerze.",u"Charon pierwszy, kurwa, tego nie przeskoczysz.",u"Charon na szczycie, a ty se tasuj resztę, jebany świat."}},
        {u"Charon stays first.",{u"Charon stays on top anyway, sucker.",u"Charon first, you can’t fucking beat that.",u"Charon on top, you shuffle the rest, you twat."}},
        {u"Domyślnie wybrano Anuluj.",{u"Domyślnie Anuluj, bo ci, kurwa, nie ufam.",u"Na wszelki wypadek stoi Anuluj, znam cię, kurwa mać.",u"Anuluj jest domyślne, żebyś czegoś nie rozpierdolił."}},
        {u"Cancel is selected by default.",{u"Cancel is the default, because I don’t fucking trust you.",u"Cancel’s selected just in case, I know you, dipshit.",u"Cancel by default, so you don’t wreck something, goddammit."}},
        {u"Skróty Charona. Układ jak NVDA na komputerze stacjonarnym: Insert, a potem cyfra z klawiatury numerycznej: 7, 8, 9 wiersze, 4, 5, 6 słowa, 1, 2, 3 znaki, plus czytaj dalej, gwiazdka cały ekran, Tab zaznaczona pozycja, End pasek klawiszy, T gdzie jestem, F kolory, F12 godzina, dwa razy Insert to zwykły Insert. Układ laptopowy: Control Alt i litera: R cały ekran, A czytaj dalej, S ucisza, U, I, O wiersze, J, K, L słowa, M, przecinek, kropka znaki, T i B góra i dół, E zaznaczona pozycja, P położenie, N kolory, C godzina, Q automatyczne czytanie, równa się i minus tempo, W gdzie jestem, Y pasek klawiszy, V szczegółowość, H ta pomoc.",{u"Skróty dla opornych, kurwa. Jak w NVDA na stacjonarce: Insert i cyfra z numpada: 7, 8, 9 wiersze, 4, 5, 6 słowa, 1, 2, 3 znaki, plus czytaj dalej, gwiazdka cały ekran, Tab zaznaczone, End pasek klawiszy, T gdzie się zgubiłeś, F kolory, F12 godzina, dwa razy Insert jak naprawdę chcesz Inserta. Na laptopie Control Alt i litera: R cały ekran, A dalej, S zamknij mi mordę, U, I, O wiersze, J, K, L słowa, M, przecinek, kropka znaki, T i B góra i dół, E zaznaczone, P gdzie jesteś, N kolory, C godzina, Q cisza na zmianach, równa się i minus tempo, W gdzie jestem, Y pasek, V gadatliwość, H ta sama pomoc, bo i tak zapomnisz.",u"Słuchaj, łamago. Insert i numpad jak w NVDA: 7, 8, 9 wiersze, 4, 5, 6 słowa, 1, 2, 3 znaki, plus dalej, gwiazdka całość, Tab zaznaczone, End pasek, T gdzie jestem, F kolory, F12 zegarek, Insert dwa razy przepuszcza Insert. Laptop, kurwa, Control Alt i: R cały ekran, A dalej, S cisza, U, I, O wiersze, J, K, L słowa, M, przecinek, kropka znaki, T góra, B dół, E zaznaczona pozycja, P położenie, N kolory, C zegarek, Q zamknij mordę przy zmianach, równa się szybciej, minus wolniej, W gdzie jestem, Y pasek, V ile mam pierdolić, H znowu to gówno.",u"Ja pierdolę, to nie takie trudne. Stacjonarka: Insert i cyfra, 7 8 9 wiersze, 4 5 6 słowa, 1 2 3 znaki, plus czytaj dalej, gwiazdka całość, Tab zaznaczenie, End pasek, T gdzie jestem, F kolory, F12 godzina, Insert Insert to Insert. Laptop: Control Alt plus litera: R czyta, A od kursora, S ucisza, U I O wiersze, J K L słowa, M przecinek kropka znaki, T B góra i dół, E zaznaczenie, P gdzie stoisz, N kolory, C godzina, Q automat, równa się i minus tempo, W gdzie jestem, Y pasek klawiszy, V szczegóły, H pomoc dla sklerotyków."}},
        {u"Charon shortcuts. NVDA desktop layout: Insert, then a numpad digit: 7, 8, 9 lines, 4, 5, 6 words, 1, 2, 3 characters, plus read on, star the whole screen, Tab the highlighted entry, End the key-hint bar, T where am I, F colours, F12 time, Insert twice sends a real Insert. Laptop layout: Control Alt and a letter: R whole screen, A read on, S stop, U, I, O lines, J, K, L words, M, comma, period characters, T and B top and bottom, E highlighted entry, P position, N colours, C time, Q automatic reading, equals and minus rate, W where am I, Y key-hint bar, V verbosity, H this help.",{u"Shortcuts for dummies, for fuck’s sake. NVDA desktop: Insert and a numpad digit: 7, 8, 9 lines, 4, 5, 6 words, 1, 2, 3 characters, plus read on, star whole screen, Tab the highlight, End the key bar, T where the fuck you are, F colours, F12 time, Insert twice if you really want an Insert. Laptop: Control Alt and a letter: R whole screen, A read on, S shut my gob, U, I, O lines, J, K, L words, M, comma, period characters, T and B top and bottom, E highlight, P where you are, N colours, C time, Q quiet on changes, equals and minus rate, W where am I, Y key bar, V chattiness, H this help again, because you’ll forget.",u"Listen, numpty. Insert and the numpad like NVDA: 7, 8, 9 lines, 4, 5, 6 words, 1, 2, 3 characters, plus read on, star everything, Tab highlight, End bar, T where am I, F colours, F12 clock, Insert twice passes an Insert. Laptop, damn it, Control Alt plus: R whole screen, A read on, S silence, U, I, O lines, J, K, L words, M, comma, period characters, T top, B bottom, E highlighted entry, P position, N colours, C clock, Q shut up on changes, equals faster, minus slower, W where am I, Y bar, V how much I yap, H this shit again.",u"Fucking hell, it’s not hard. Desktop: Insert and a digit, 7 8 9 lines, 4 5 6 words, 1 2 3 characters, plus read on, star everything, Tab highlight, End bar, T where am I, F colours, F12 time, Insert Insert is an Insert. Laptop: Control Alt and a letter: R reads, A from the cursor, S shuts up, U I O lines, J K L words, M comma period characters, T B top and bottom, E highlight, P where you stand, N colours, C time, Q auto reading, equals and minus rate, W where am I, Y key bar, V detail, H help for goldfish."}},
        {u"Ustawienia zmieniły się. Naciśnij F5, aby odświeżyć.",{u"Ustawienia się zmieniły, a ty śpisz. Walnij F5. Ja pierdolę.",u"Coś się zmieniło, kurwa, F5 i odśwież ten burdel.",u"Nieaktualne, łamago, F5."}},
        {u"Settings changed. Press F5 to refresh.",{u"Settings changed while you slept. Smack F5. Fuck it.",u"Something changed, damn it, F5 and refresh this mess.",u"Out of date, numpty, F5."}},
        {u"Widok nieaktualny. Naciśnij F5.",{u"Widok przeterminowany, F5, kurwa.",u"Nieświeże, wciśnij F5, ciołku.",u"Stare jak twój komputer, F5. Kurwa."}},
        {u"View stale. Press F5.",{u"View’s stale, F5, damn it.",u"Gone off, press F5, dipshit.",u"Old as your computer, F5. Shit."}},
        {u"Strzałki: wybór / edycja; Enter / Spacja: otwórz; Esc: wstecz; F1: pomoc; F5: odśwież",{u"Strzałki łażą i kręcą, Enter albo Spacja otwiera, Esc spierdala, F1 pomoc dla zielonych, F5 odświeża",u"Strzałki, Enter, Esc, F1 jak nie ogarniasz, F5 jak się zawiesisz, kurwa",u"Łaź strzałkami, wchodź Enterem, uciekaj Escapem, a F1, jak już nic nie kumasz, kurwa jego mać"}},
        {u"Arrows: select / edit; Enter / Space: open; Esc: back; F1: help; F5: refresh",{u"Arrows wander and twiddle, Enter or Space opens, Esc fucks off, F1 help for noobs, F5 refreshes",u"Arrows, Enter, Esc, F1 if you’re clueless, F5 if you freeze, damn it",u"Wander with arrows, go in with Enter, run with Escape, and F1 when you’re fucking lost"}},
    };
    static char16_t buffer[2400];
    size_t out=0;bool changed=false;
    for(size_t at=0;text[at];) {
        const Hint* found=nullptr;size_t length=0;
        for(const auto& hint:hints) {size_t k=0;while(hint.normal[k] && text[at+k]==hint.normal[k])++k;if(!hint.normal[k]) {found=&hint;length=k;break;}}
        if(found) {
            const auto* styled=found->styled[(ExplicitVariety()+at)%3];
            size_t k=0;while(styled[k] && out+1<sizeof(buffer)/2)buffer[out++]=styled[k++];
            at+=length;changed=true;continue;
        }
        if(out+1>=sizeof(buffer)/2)return text;
        buffer[out++]=text[at++];
    }
    buffer[out]=0;
    return changed?buffer:text;
}
// Tag appended to option labels that have no authored explicit wording
// (device names, lists, popups): the label itself stays intact.
inline const char16_t* ExplicitLabelTag(const char16_t* label,size_t row,bool polish) {
    static const char16_t* pl[]={u", kurwa jego mać",u", ja pierdolę",u", no i chuj",u", w pizdu z tym",u", bierz albo spierdalaj",u", jak ci się, kurwa, chce",u", do kurwy nędzy",
        u", ty łamago",u", ciołku jebany",u", o w mordę",u", szkoda, kurwa, gadać",u", chuj wie po co",u", zjebane, ale jest",u", nie spierdol tego",u", w dupę jeża",u", pierdolony szajs"};
    static const char16_t* en[]={u", fuck it",u", for fuck’s sake",u", you numpty",u", whatever, dumbass",u", goddammit",u", take it or piss off",u", bloody hell",
        u", dipshit",u", don’t fuck it up",u", shit knows why",u", motherfucker",u", fucking crap"};
    const uint32_t value=MessageHash(label)+uint32_t(row)*7u+ExplicitVariety()*13u;
    return polish?pl[value%(sizeof(pl)/sizeof(*pl))]:en[value%(sizeof(en)/sizeof(*en))];
}
// r374: explicit mode, menu left untouched for 20 s. Rotates, never twice in a row.
inline const char16_t* IdleTaunt(bool polish) {
    static const char16_t* pl[]={
        u"O, stoi pojeb. Może potrzebujesz pomocy mamusi, żeby ci ustawiła boot manager? A nie, przepraszam. Masz to po matce, nie znasz się na tym ustrojstwie. Najlepiej rozjeb je w pizdu.",
        u"Halo, kurwa, jest tam kto? Dwadzieścia sekund gapienia się w menu. Nawet ślimak by już coś wybrał.",
        u"Zasnąłeś, łamago? Komputer czeka, prąd leci, a ty se medytujesz nad boot managerem.",
        u"No dawaj, wciśnij cokolwiek. Strzałka, Enter, czoło na klawiaturę, cokolwiek, kurwa.",
        u"Siedzisz tu jak sierota na weselu. Może zadzwoń do szwagra, on się zna na tych ustrojstwach, kurwa.",
        u"Ja pierdolę, ile można myśleć? To nie jest egzamin na doktorat, to jest pierdolone menu.",
        u"Wiesz co? Wyłącz to w chuj i idź na spacer. Ty i to ustrojstwo potrzebujecie przerwy od siebie."};
    static const char16_t* en[]={
        u"Oh look, it’s just standing there, the dipshit. Need mummy to set up the boot manager for you? Oh wait, sorry, you got it from her: neither of you knows shit about this contraption. Best smash it to fucking pieces.",
        u"Hello? Anyone fucking home? Twenty seconds staring at a menu. A snail would’ve picked something by now.",
        u"Fallen asleep, numpty? The computer’s waiting, the power bill’s running, and you’re meditating over a boot manager.",
        u"Come on, press anything. An arrow, Enter, your forehead on the keyboard, any fucking thing.",
        u"Sitting here like a lost puppy. Call your brother-in-law, he knows these contraptions, for fuck’s sake.",
        u"Jesus fuck, how long can you think? It’s not a PhD exam, it’s a fucking menu.",
        u"You know what? Turn the damn thing off and go for a walk. You and this contraption need a break from each other."};
    static uint32_t next=0;
    const auto* text=polish?pl[next%7]:en[next%7];
    ++next;return text;
}
inline bool FormatCharonMessage(const char16_t* source,char16_t* output,size_t capacity,bool enabled,bool polish,bool authored=false) {
    if(!source || !output || !capacity)return false;
    size_t length=0;while(source[length])++length;
    if(length>=capacity) {output[0]=0;return false;}
    if(enabled && !authored && length) {
        const auto* hinted=ExplicitHints(source);
        if(hinted!=source) {
            static char16_t copy[2400];size_t k=0;while(hinted[k] && k+1<2400) {copy[k]=hinted[k];++k;}copy[k]=0;
            source=copy;length=k;
            if(length>=capacity) {output[0]=0;return false;}
            // A message that is nothing but key advice is complete as replaced.
            if(ToneOf(source)==MessageTone::label)authored=true;
        }
        // Blank text (spaces only) gets nothing appended.
        {bool blank=true;for(size_t k=0;k<length;++k)if(source[k]!=u' ')blank=false;if(blank)authored=true;}
    }
    const char16_t* head=u"";const char16_t* tail=u"";const char16_t* rest=source;size_t restLength=length;
    if(enabled && !authored && length) {
        size_t matched=0;
        if(const auto* custom=ExplicitAnnouncement(source,matched)) {head=custom;rest=source+matched;restLength=length-matched;}
        else if(!ContainsExplicitBootMessage(source)) {head=Interjection(source,polish);tail=JabAfter(source,polish);
            if(ToneOf(source)==MessageTone::label)tail=ExplicitLabelTag(source,0,polish);}
    }
    size_t headLength=0;while(head[headLength])++headLength;
    if(headLength+restLength>=capacity) {head=u"";headLength=0;rest=source;restLength=length;}
    // Supports an in-place destination: move the rest first, then the head.
    char16_t* target=output+headLength;
    if(target>rest)for(size_t i=restLength+1;i>0;--i)target[i-1]=rest[i-1];
    else if(target<rest)for(size_t i=0;i<=restLength;++i)target[i]=rest[i];
    for(size_t i=0;i<headLength;++i)output[i]=head[i];
    size_t tailLength=0;while(tail[tailLength])++tailLength;
    if(tailLength && headLength+restLength+tailLength<capacity)
        for(size_t i=0;i<=tailLength;++i)output[headLength+restLength+i]=tail[i];
    return true;
}
}
