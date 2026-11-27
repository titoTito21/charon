// Charon: the keys of boot managers, by program and kind of screen (r410). GPL-2.0-or-later.
//
// One table for two things the user asked for: the "keys" group of the reader's elements list (Enter
// sends the key to the program) and the spoken key list of the current screen. Every row was taken from
// the program's own documentation or source (named per block; URLs in
// tests/charon-r410-reader-results.txt). A key that is not listed here is not known to Charon: nothing
// is guessed. Enter itself and keys that start a system at once (digits in Limine, OpenCore, rEFInd)
// are named in the spoken list but are never sent from the elements list (scan and character both 0).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "CharonScreenModel.h"
namespace hakefi::managerkeys {
using screenmodel::Manager;using screenmodel::Screen;
struct Key {Manager manager;Screen screen;const char16_t* name;const char16_t* en;const char16_t* pl;uint16_t scan;char16_t ch;};
// UEFI scan codes: Up 1, Down 2, Right 3, Left 4, Home 5, End 6, Insert 7, Delete 8, Page Up 9, Page Down 10,
// F1 0x0b ... F10 0x14, F11 0x15, F12 0x16, Escape 0x17.
inline constexpr Key keys[]={
    // GNU GRUB: grub-core/normal/menu.c run_menu, menu_entry.c, cmdline.c; GRUB manual "Menu interface".
    {Manager::grub,Screen::menu,u"Enter",u"start the entry or open the submenu",u"uruchamia pozycj\x0119 albo otwiera podmenu",0,0},
    {Manager::grub,Screen::menu,u"e",u"edit the commands of the entry",u"edycja polece\x0144 pozycji",0,u'e'},
    {Manager::grub,Screen::menu,u"c",u"command line",u"wiersz polece\x0144",0,u'c'},
    {Manager::grub,Screen::menu,u"Escape",u"back to the previous menu, in a submenu only",u"powr\x00f3t do poprzedniego menu, tylko w podmenu",0x17,0},
    {Manager::grub,Screen::menu,u"Home",u"first entry",u"pierwsza pozycja",5,0},
    {Manager::grub,Screen::menu,u"End",u"last entry",u"ostatnia pozycja",6,0},
    {Manager::grub,Screen::menu,u"Page Up",u"ten entries up",u"dziesi\x0119\x0107 pozycji w g\x00f3r\x0119",9,0},
    {Manager::grub,Screen::menu,u"Page Down",u"ten entries down",u"dziesi\x0119\x0107 pozycji w d\x00f3\x0142",10,0},
    {Manager::grub,Screen::editor,u"Control x or F10",u"start with the edited commands",u"uruchamia ze zmienionymi poleceniami",0,0},
    {Manager::grub,Screen::editor,u"Control c or F2",u"command line",u"wiersz polece\x0144",0x0c,0},
    {Manager::grub,Screen::editor,u"Escape",u"discard the changes and return to the menu",u"porzuca zmiany i wraca do menu",0x17,0},
    {Manager::grub,Screen::editor,u"Tab",u"list completions",u"lista uzupe\x0142nie\x0144",0,9},
    {Manager::grub,Screen::editor,u"Control k",u"delete to the end of the line",u"usuwa do ko\x0144" u"ca wiersza",0,0},
    {Manager::grub,Screen::editor,u"Home, End",u"start and end of the line",u"pocz\x0105tek i koniec wiersza",0,0},
    {Manager::grub,Screen::commandLine,u"Tab",u"list completions: commands for the first word, devices and files after it",u"lista uzupe\x0142nie\x0144: polecenia dla pierwszego s\x0142owa, dalej urz\x0105" u"dzenia i pliki",0,9},
    {Manager::grub,Screen::commandLine,u"Up, Down",u"command history",u"historia polece\x0144",0,0},
    {Manager::grub,Screen::commandLine,u"Escape",u"back to the menu, when the command line was opened from it",u"powr\x00f3t do menu, gdy wiersz polece\x0144 otwarto z menu",0x17,0},
    // systemd-boot: src/boot/boot.c, line-edit.c; man systemd-boot(7) "Key bindings".
    {Manager::systemdBoot,Screen::menu,u"Enter",u"start the entry",u"uruchamia pozycj\x0119",0,0},
    {Manager::systemdBoot,Screen::menu,u"d",u"make the entry the default",u"ustawia pozycj\x0119 jako domy\x015bln\x0105",0,u'd'},
    {Manager::systemdBoot,Screen::menu,u"e",u"edit the kernel command line",u"edycja wiersza polece\x0144 j\x0105" u"dra",0,u'e'},
    {Manager::systemdBoot,Screen::menu,u"t and T",u"longer and shorter menu timeout",u"d\x0142u\x017cszy i kr\x00f3tszy czas menu",0,0},
    {Manager::systemdBoot,Screen::menu,u"v",u"version",u"wersja",0,u'v'},
    {Manager::systemdBoot,Screen::menu,u"p",u"status screen",u"ekran stanu",0,u'p'},
    {Manager::systemdBoot,Screen::menu,u"h",u"help line",u"wiersz pomocy",0,u'h'},
    {Manager::systemdBoot,Screen::menu,u"Home",u"first entry",u"pierwsza pozycja",5,0},
    {Manager::systemdBoot,Screen::menu,u"End",u"last entry",u"ostatnia pozycja",6,0},
    {Manager::systemdBoot,Screen::menu,u"f",u"restart into the firmware setup, after Enter",u"restart do ustawie\x0144 firmware, po Enter",0,0},
    {Manager::systemdBoot,Screen::menu,u"B",u"restart",u"restart",0,0},
    {Manager::systemdBoot,Screen::menu,u"O",u"power off",u"wy\x0142\x0105" u"cza komputer",0,0},
    // rEFInd: refind/menu.c RunGenericMenu; https://www.rodsbooks.com/refind/using.html
    {Manager::refind,Screen::unknown,u"Enter or Space",u"start the selected item",u"uruchamia wybran\x0105 pozycj\x0119",0,0},
    {Manager::refind,Screen::unknown,u"Insert, F2 or Tab",u"options of the selected item",u"opcje wybranej pozycji",0x0c,0},
    {Manager::refind,Screen::unknown,u"Escape",u"back from a submenu; on the main screen scan the disks again",u"powr\x00f3t z podmenu; na g\x0142\x00f3wnym ekranie ponowne szukanie na dyskach",0x17,0},
    {Manager::refind,Screen::unknown,u"Delete or minus",u"hide the selected item",u"ukrywa wybran\x0105 pozycj\x0119",0,0},
    {Manager::refind,Screen::unknown,u"a",u"about rEFInd",u"o programie rEFInd",0,u'a'},
    {Manager::refind,Screen::unknown,u"Home",u"first item",u"pierwsza pozycja",5,0},
    {Manager::refind,Screen::unknown,u"End",u"last item",u"ostatnia pozycja",6,0},
    // Limine: common/menu.c.
    {Manager::limine,Screen::menu,u"Enter",u"start the entry, or open or close a directory",u"uruchamia pozycj\x0119 albo otwiera lub zamyka katalog",0,0},
    {Manager::limine,Screen::menu,u"e",u"edit the entry",u"edycja pozycji",0,u'e'},
    {Manager::limine,Screen::menu,u"b",u"editor with a blank entry",u"edytor z pust\x0105 pozycj\x0105",0,u'b'},
    {Manager::limine,Screen::menu,u"s",u"restart into the firmware setup",u"restart do ustawie\x0144 firmware",0,0},
    {Manager::limine,Screen::menu,u"Home",u"first entry",u"pierwsza pozycja",5,0},
    {Manager::limine,Screen::menu,u"End",u"last entry",u"ostatnia pozycja",6,0},
    {Manager::limine,Screen::editor,u"F10 or Control x",u"start the edited entry",u"uruchamia zmienion\x0105 pozycj\x0119",0,0},
    {Manager::limine,Screen::editor,u"Escape",u"discard and leave",u"porzuca zmiany i wychodzi",0x17,0},
    // OpenCore builtin picker: OcBootManagementLib/HotKeySupport.c, BuiltinPicker.c.
    {Manager::openCore,Screen::menu,u"Enter",u"start the entry",u"uruchamia pozycj\x0119",0,0},
    {Manager::openCore,Screen::menu,u"Space",u"show the auxiliary entries",u"pokazuje pozycje pomocnicze",0,u' '},
    {Manager::openCore,Screen::menu,u"Tab",u"move between the list, Shutdown and Restart",u"przechodzi mi\x0119" u"dzy list\x0105, Wy\x0142\x0105" u"cz i Restart",0,9},
    {Manager::openCore,Screen::menu,u"Escape",u"reload the menu",u"wczytuje menu ponownie",0,0},
    {Manager::openCore,Screen::menu,u"Home",u"first entry",u"pierwsza pozycja",5,0},
    {Manager::openCore,Screen::menu,u"End",u"last entry",u"ostatnia pozycja",6,0},
    // Clover: rEFIt_UEFI, its own help screen (F1) quoted in the recorded screens.
    {Manager::clover,Screen::unknown,u"Enter",u"start the selected item",u"uruchamia wybran\x0105 pozycj\x0119",0,0},
    {Manager::clover,Screen::unknown,u"Space",u"details and options of the selected item",u"szczeg\x00f3\x0142y i opcje wybranej pozycji",0,u' '},
    {Manager::clover,Screen::unknown,u"F1",u"Clover's help",u"pomoc Clovera",0x0b,0},
    {Manager::clover,Screen::unknown,u"o",u"options menu",u"menu opcji",0,u'o'},
    {Manager::clover,Screen::unknown,u"a",u"about Clover",u"o programie Clover",0,u'a'},
    {Manager::clover,Screen::unknown,u"Escape",u"back from a submenu; on the main screen refresh it",u"powr\x00f3t z podmenu; na g\x0142\x00f3wnym ekranie od\x015bwie\x017c" u"a go",0x17,0},
    {Manager::clover,Screen::unknown,u"F3",u"show hidden entries",u"pokazuje ukryte pozycje",0x0d,0},
    {Manager::clover,Screen::unknown,u"r",u"restart",u"restart",0,0},
    {Manager::clover,Screen::unknown,u"u",u"leave Clover",u"wyj\x015b" u"cie z Clovera",0,0},
    // Syslinux menu.c32: com32/menu/menumain.c, doc/menu.txt.
    {Manager::syslinux,Screen::menu,u"Enter",u"start the entry",u"uruchamia pozycj\x0119",0,0},
    {Manager::syslinux,Screen::menu,u"Tab",u"edit the command line of the entry",u"edycja wiersza polece\x0144 pozycji",0,9},
    {Manager::syslinux,Screen::menu,u"Escape",u"parent menu, or the boot prompt where allowed",u"menu nadrz\x0119" u"dne albo wiersz boot, gdzie wolno",0x17,0},
    {Manager::syslinux,Screen::menu,u"Home",u"first entry",u"pierwsza pozycja",5,0},
    {Manager::syslinux,Screen::menu,u"End",u"last entry",u"ostatnia pozycja",6,0},
    {Manager::syslinux,Screen::menu,u"F1",u"help file, when the menu has one",u"plik pomocy, gdy menu go ma",0x0b,0},
    // iPXE: hci/tui/menu_ui.c; https://ipxe.org/cmd/choose
    {Manager::ipxe,Screen::menu,u"Enter",u"choose the item",u"wybiera pozycj\x0119",0,0},
    {Manager::ipxe,Screen::menu,u"Escape or Control c",u"leave the menu without choosing",u"wyj\x015b" u"cie z menu bez wyboru",0x17,0},
    {Manager::ipxe,Screen::menu,u"Home",u"first item",u"pierwsza pozycja",5,0},
    {Manager::ipxe,Screen::menu,u"End",u"last item",u"ostatnia pozycja",6,0},
    {Manager::ipxe,Screen::menu,u"Page Up, Page Down",u"a screen up and down",u"ekran w g\x00f3r\x0119 i w d\x00f3\x0142",0,0},
    // Windows Boot Manager, classic menu: the keys its own screen names (recorded screens), bcdedit /set bootmenupolicy.
    {Manager::windows,Screen::menu,u"Enter",u"start the highlighted system",u"uruchamia pod\x015bwietlony system",0,0},
    {Manager::windows,Screen::menu,u"Tab",u"move between the systems and the tools",u"przechodzi mi\x0119" u"dzy systemami i narz\x0119" u"dziami",0,9},
    {Manager::windows,Screen::menu,u"F8",u"advanced options for the highlighted system",u"opcje zaawansowane pod\x015bwietlonego systemu",0x12,0},
    {Manager::windows,Screen::menu,u"Escape",u"cancel",u"anuluje",0,0},
    {Manager::windows,Screen::error,u"Enter",u"try again",u"ponawia pr\x00f3" u"b\x0119",0,0},
    {Manager::windows,Screen::error,u"F8",u"startup settings",u"ustawienia uruchamiania",0,0},
    {Manager::windows,Screen::error,u"Escape",u"firmware setup",u"ustawienia firmware",0,0},
    // Windows Memory Diagnostic: the key bar of its own screens (recorded screens).
    {Manager::memoryDiagnostic,Screen::unknown,u"F1",u"options of the test",u"opcje testu",0x0b,0},
    {Manager::memoryDiagnostic,Screen::unknown,u"Tab",u"next group of options",u"nast\x0119pna grupa opcji",0,9},
    {Manager::memoryDiagnostic,Screen::unknown,u"F10",u"apply the options and start the test again",u"stosuje opcje i zaczyna test od nowa",0,0},
    {Manager::memoryDiagnostic,Screen::unknown,u"Escape",u"cancel the options, or end the test and restart",u"anuluje opcje albo ko\x0144" u"czy test i uruchamia komputer ponownie",0,0},
    // UEFI Shell: edk2 ShellPkg (UEFI Shell Specification 2.2).
    {Manager::shell,Screen::commandLine,u"Tab",u"complete a file name",u"uzupe\x0142nia nazw\x0119 pliku",0,9},
    {Manager::shell,Screen::commandLine,u"Up, Down",u"command history",u"historia polece\x0144",0,0},
    {Manager::shell,Screen::commandLine,u"Escape",u"clear the line",u"czy\x015b" u"ci wiersz",0x17,0},
    {Manager::shell,Screen::commandLine,u"Page Up, Page Down",u"scroll the screen back and forth",u"przewija ekran wstecz i naprz\x00f3" u"d",0,0},
    {Manager::shell,Screen::commandLine,u"Control c",u"break a running command",u"przerywa dzia\x0142" u"aj\x0105" u"ce polecenie",0,0},
    {Manager::shell,Screen::pager,u"Enter",u"next page",u"nast\x0119pna strona",0,13},
    {Manager::shell,Screen::pager,u"q",u"stop the output",u"ko\x0144" u"czy wypisywanie",0,u'q'},
};
inline constexpr size_t keyCount=sizeof(keys)/sizeof(keys[0]);
// Does row i belong to this program and kind of screen? Rows with Screen::unknown fit every screen of the program.
inline bool Fits(size_t i,Manager manager,Screen screen) {
    return i<keyCount && keys[i].manager==manager && (keys[i].screen==Screen::unknown || keys[i].screen==screen);
}
inline bool Sendable(size_t i) {return i<keyCount && (keys[i].scan || keys[i].ch);}
}  // namespace hakefi::managerkeys
