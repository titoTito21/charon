// Charon key list (r390). GPL-2.0-or-later.
// The user's request: a list of the reader's keys like Fenrir's or
// Speakup's help (Insert+F1 / Alt+F1), walked with the arrows; while it is
// open the boot waits. The same list is a page of Charon's own menu.
#pragma once
#include <stddef.h>
namespace hakefi::keyhelp {
struct Entry {const char16_t* pl;const char16_t* en;};
inline constexpr Entry entries[]={
    {u"Insert F1 albo Alt F1: ta lista klawiszy. Strzałki w górę i w dół przechodzą po liście, Home i End na początek i koniec, Escape zamyka.",
     u"Insert F1 or Alt F1: this key list. Up and Down walk the list, Home and End go to its ends, Escape closes it."},
    {u"Alt strzałka w górę: poprzedni wiersz ekranu",u"Alt Up arrow: previous screen line"},
    {u"Alt strzałka w dół: następny wiersz ekranu",u"Alt Down arrow: next screen line"},
    {u"Alt strzałka w lewo: poprzedni znak",u"Alt Left arrow: previous character"},
    {u"Alt strzałka w prawo: następny znak",u"Alt Right arrow: next character"},
    {u"Alt ukośnik wsteczny: przegląd ekranu ze wstrzymanym rozruchem; strzałki czytają wiersze i znaki, Home i End początek i koniec ekranu, Enter bieżący wiersz, Escape wznawia rozruch",
     u"Alt backslash: screen review with the boot paused; arrows read lines and characters, Home and End the top and bottom, Enter the current line, Escape resumes the boot"},
    {u"Numpad 7, 8, 9: poprzedni, bieżący, następny wiersz; bieżący dwa razy: literowanie",u"Numpad 7, 8, 9: previous, current, next line; current twice: spelled"},
    {u"Numpad 4, 5, 6: poprzednie, bieżące, następne słowo; bieżące dwa razy: literowanie",u"Numpad 4, 5, 6: previous, current, next word; current twice: spelled"},
    {u"Numpad 1, 2, 3: poprzedni, bieżący, następny znak; bieżący dwa razy: alfabet fonetyczny, trzy razy: kod znaku",
     u"Numpad 1, 2, 3: previous, current, next character; current twice: phonetic, three times: character code"},
    {u"Numpad plus: czytaj dalej; gwiazdka: cały ekran; minus: zaznaczona pozycja; Shift numpad 7 i 9: góra i dół ekranu. Sam numpad działa przy wyłączonym NumLocku na klawiaturze USB.",
     u"Numpad plus: read on; star: the whole screen; minus: the highlighted entry; Shift numpad 7 and 9: top and bottom. The numpad alone works with NumLock off on USB keyboards."},
    {u"Insert, a potem cyfra numpadu: to samo co sam numpad, na każdej klawiaturze; dwa razy Insert to zwykły Insert",u"Insert, then a numpad digit: the same as the numpad alone, on every keyboard; Insert twice is a real Insert"},
    {u"Insert Tab albo Control Alt E: zaznaczona pozycja z położeniem",u"Insert Tab or Control Alt E: the highlighted entry with its position"},
    {u"Insert End albo Control Alt Y: pasek klawiszy na dole ekranu",u"Insert End or Control Alt Y: the key-hint bar at the bottom"},
    {u"Insert T albo Control Alt W: gdzie jestem",u"Insert T or Control Alt W: where am I"},
    {u"Insert P albo Control Alt D: poziom interpunkcji, brak, część, większość, wszystko",u"Insert P or Control Alt D: symbol level, none, some, most, all"},
    {u"Insert Z albo Control Alt Z: język tekstu ekranu, automatycznie, jak menu, angielski, polski",u"Insert Z or Control Alt Z: screen text language, automatic, as the menu, English, Polish"},
    {u"Insert 0 albo Control Alt X: pomoc klawiszy, każdy klawisz zostaje nazwany",u"Insert 0 or Control Alt X: input help, every key is named"},
    {u"Insert F albo Control Alt N: kolory",u"Insert F or Control Alt N: colours"},
    {u"Insert F12 albo Control Alt C: godzina",u"Insert F12 or Control Alt C: time"},
    {u"Insert gwiazdka, Insert B albo Control Alt R: cały ekran od nowa",u"Insert star, Insert B or Control Alt R: the whole screen again"},
    {u"Control Alt U, I, O: wiersze; J, K, L: słowa; M, przecinek, kropka: znaki; T i B: góra i dół; P: położenie; A: czytaj dalej",
     u"Control Alt U, I, O: lines; J, K, L: words; M, comma, period: characters; T and B: top and bottom; P: position; A: read on"},
    {u"Insert G: formatowanie tekstu w mowie, wyłączone, głosem, ikony dźwiękowe, głosem i ikony",u"Insert G: text formatting in speech, off, voice, sound icons, voice and sound icons"},
    {u"Insert N albo Control Alt N: formatowanie w miejscu kursora przeglądu, kolory, jasność, podświetlenie, czcionka i rozmiar",u"Insert N or Control Alt N: formatting at the review cursor, colours, brightness, highlight, font and size"},
    {u"Ikony zdarzeń, rodziny głosów i położenie w stereo: Ustawienia czytnika ekranu, Formatowanie tekstu",u"Event icons, voice families and stereo position: Screen reader settings, Text formatting"},
    {u"Insert Page Up: okno jak w Speakup, lewy górny i prawy dolny róg w miejscu kursora przeglądu, trzeci raz usuwa okno; Insert Page Down czyta okno; Insert Delete wycisza je",u"Insert Page Up: a window, as in Speakup, top left and bottom right at the review cursor, a third time clears it; Insert Page Down reads it; Insert Delete silences it"},
    {u"Insert F9: początek zaznaczenia, Insert F10: kopiuj do kursora przeglądu, Insert F11: wklej do programu jako klawisze",u"Insert F9: start mark, Insert F10: copy to the review cursor, Insert F11: paste into the program as keys"},
    {u"Insert Home: wiersz z wcięciem",u"Insert Home: the line with its indentation"},
    {u"Insert H: historia mowy, rozruch wstrzymany; strzałka w górę starsze, w dół nowsze, Escape zamyka",u"Insert H: speech history, boot paused; Up older, Down newer, Escape closes"},
    {u"Insert D: co się zmieniło na ekranie od ostatniego klawisza",u"Insert D: what changed on the screen since the last key"},
    {u"Control strzałka w lewo i w prawo: poprzedni i następny region ekranu, na przykład tytuł, menu, panel pomocy, pasek klawiszy; w przeglądzie ze wstrzymanym rozruchem także Shift strzałki",
     u"Control Left and Right: previous and next screen region, for example title, menu, help panel, key-hint bar; in the paused review also Shift arrows"},
    {u"Control albo Control Alt S: ucisza mowę",u"Control or Control Alt S: stops speech"},
    {u"Control Alt Q: automatyczne czytanie zmian włącza lub wyłącza",u"Control Alt Q: automatic reading of changes on or off"},
    {u"Control Alt V: szczegółowość czytnika",u"Control Alt V: reader verbosity"},
    {u"Control Alt równa się i minus: tempo mowy",u"Control Alt equals and minus: speech rate"},
    {u"Control Alt strzałki w lewo i w prawo: wybór tempa, głośności lub wysokości; w górę i w dół: zmiana",u"Control Alt Left and Right: choose rate, volume or pitch; Up and Down: change it"},
    {u"Control Alt H: skróty jednym ciągiem",u"Control Alt H: all shortcuts in one go"},
};
inline constexpr size_t count=sizeof(entries)/sizeof(entries[0]);
}  // namespace hakefi::keyhelp
