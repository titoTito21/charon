// Charon: words of boot loader entries and kernel command lines the reader can explain (r410).
// GPL-2.0-or-later.
//
// The "describe" shortcut (Insert+semicolon, Ctrl+Alt+6) explains the known words of the line under the
// review cursor: in GRUB's entry editor and command line, systemd-boot's line editor, Limine's editor,
// or anywhere else. ONLY words of this table are explained; each row was checked against the document
// named for its group (URLs in tests/charon-r410-reader-results.txt). A word that is not here is not
// explained and is counted ("2 words Charon does not know") - nothing is guessed from its spelling.
//
//   kernel: Linux Documentation/admin-guide/kernel-parameters.txt
//   grub:   GNU GRUB manual, docs/grub.texi ("@deffn Command ...")
//   limine: Limine CONFIG.md
// r410 (decision F5): words of real distribution boot entries that those three documents do not have
// (splash, single, linuxefi, load_video ...) were added from distribution documentation: systemd and dracut
// manual pages, Arch Wiki, Debian live-boot(7) and Installation Guide, Fedora Wiki, the Ubuntu grub2 package.
// Every row names its document in `source`; URLs and the quoted sentences are in
// work/r410-evidence/web/words-distro-sources.txt.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hakefi::loaderwords {
// prefix: the word on the line starts with `word` (root=/dev/sda2); otherwise it must be the whole word.
// source: the document the row was checked against (ASCII).
struct Word {const char16_t* word;bool prefix;const char16_t* en;const char16_t* pl;const char16_t* source;};
inline constexpr Word words[]={
    // ---- Linux kernel command line -----------------------------------------------------------------
    {u"quiet",false,u"kernel: most log messages are not shown",u"j\x0105" u"dro: wi\x0119kszo\x015b\x0107 komunikat\x00f3w nie jest pokazywana",u"Linux kernel-parameters.txt"},
    {u"debug",false,u"kernel: debugging messages are shown",u"j\x0105" u"dro: pokazuje komunikaty diagnostyczne",u"Linux kernel-parameters.txt"},
    {u"ro",false,u"kernel: the root file system is mounted read-only at start",u"j\x0105" u"dro: g\x0142\x00f3wny system plik\x00f3w jest na starcie montowany tylko do odczytu",u"Linux kernel-parameters.txt"},
    {u"rw",false,u"kernel: the root file system is mounted read-write at start",u"j\x0105" u"dro: g\x0142\x00f3wny system plik\x00f3w jest na starcie montowany do odczytu i zapisu",u"Linux kernel-parameters.txt"},
    {u"root=",true,u"kernel: the root file system, usually a disk partition",u"j\x0105" u"dro: g\x0142\x00f3wny system plik\x00f3w, zwykle partycja dysku",u"Linux kernel-parameters.txt"},
    {u"init=",true,u"kernel: this program is run instead of /sbin/init as the first process",u"j\x0105" u"dro: ten program jest uruchamiany jako pierwszy proces zamiast /sbin/init",u"Linux kernel-parameters.txt"},
    {u"rdinit=",true,u"kernel: this program is run instead of /init from the initial RAM disk",u"j\x0105" u"dro: ten program jest uruchamiany zamiast /init z pocz\x0105tkowego dysku RAM",u"Linux kernel-parameters.txt"},
    {u"nomodeset",false,u"kernel: kernel mode setting is off; graphics drivers that would replace the firmware's display mode are not loaded",
        u"j\x0105" u"dro: ustawianie trybu graficznego przez j\x0105" u"dro wy\x0142\x0105" u"czone; sterowniki grafiki, kt\x00f3re zast\x0105pi\x0142yby tryb ekranu z firmware, nie s\x0105 \x0142" u"adowane",u"Linux kernel-parameters.txt"},
    {u"noapic",false,u"kernel: the I/O APIC interrupt controllers are not used",u"j\x0105" u"dro: kontrolery przerwa\x0144 I/O APIC nie s\x0105 u\x017cywane",u"Linux kernel-parameters.txt"},
    {u"nolapic",false,u"kernel: the local APIC is not used",u"j\x0105" u"dro: lokalny APIC nie jest u\x017cywany",u"Linux kernel-parameters.txt"},
    {u"nosmp",false,u"kernel: only one processor is used",u"j\x0105" u"dro: u\x017cywany jest tylko jeden procesor",u"Linux kernel-parameters.txt"},
    {u"maxcpus=",true,u"kernel: the largest number of processors started at boot",u"j\x0105" u"dro: najwi\x0119ksza liczba procesor\x00f3w uruchamianych przy starcie",u"Linux kernel-parameters.txt"},
    {u"mem=",true,u"kernel: only this amount of memory is used",u"j\x0105" u"dro: u\x017cywana jest tylko taka ilo\x015b\x0107 pami\x0119" u"ci",u"Linux kernel-parameters.txt"},
    {u"loglevel=",true,u"kernel: messages more important than this level are printed on the console",u"j\x0105" u"dro: komunikaty wa\x017cniejsze ni\x017c ten poziom s\x0105 wypisywane na konsoli",u"Linux kernel-parameters.txt"},
    {u"console=",true,u"kernel: the output console device and its options",u"j\x0105" u"dro: urz\x0105" u"dzenie konsoli wyj\x015b" u"ciowej i jego opcje",u"Linux kernel-parameters.txt"},
    {u"acpi=",true,u"kernel: how ACPI, the power and configuration interface, is used",u"j\x0105" u"dro: spos\x00f3" u"b u\x017cycia ACPI, interfejsu zasilania i konfiguracji",u"Linux kernel-parameters.txt"},
    {u"resume=",true,u"kernel: the partition that holds the image of a suspended system",u"j\x0105" u"dro: partycja z obrazem u\x015bpionego systemu",u"Linux kernel-parameters.txt"},
    {u"rootdelay=",true,u"kernel: seconds to wait before the root file system is mounted",u"j\x0105" u"dro: liczba sekund czekania przed zamontowaniem g\x0142\x00f3wnego systemu plik\x00f3w",u"Linux kernel-parameters.txt"},
    {u"rootwait",false,u"kernel: waits, without a limit, for the root device to appear",u"j\x0105" u"dro: czeka bez ograniczenia, a\x017c pojawi si\x0119 urz\x0105" u"dzenie g\x0142\x00f3wnego systemu plik\x00f3w",u"Linux kernel-parameters.txt"},
    {u"nokaslr",false,u"kernel: the kernel is not loaded at a random address",u"j\x0105" u"dro: j\x0105" u"dro nie jest \x0142" u"adowane pod losowy adres",u"Linux kernel-parameters.txt"},
    {u"panic=",true,u"kernel: what happens after a kernel panic: seconds before a restart, 0 waits for ever",u"j\x0105" u"dro: co po b\x0142\x0119" u"dzie krytycznym j\x0105" u"dra: liczba sekund do restartu, 0 czeka bez ko\x0144" u"ca",u"Linux kernel-parameters.txt"},
    {u"initrd=",true,u"kernel: the place of the initial RAM disk",u"j\x0105" u"dro: po\x0142o\x017c" u"enie pocz\x0105tkowego dysku RAM",u"Linux kernel-parameters.txt"},
    {u"S",false,u"kernel: init is run in single user mode",u"j\x0105" u"dro: init startuje w trybie jednego u\x017cytkownika",u"Linux kernel-parameters.txt"},
    // ---- GNU GRUB commands ---------------------------------------------------------------------------
    {u"linux",false,u"GRUB command: loads a Linux kernel from the file; the rest of the line is the kernel's command line",u"polecenie GRUB: wczytuje j\x0105" u"dro Linux z pliku; reszta wiersza to wiersz polece\x0144 j\x0105" u"dra",u"GNU GRUB manual"},
    {u"initrd",false,u"GRUB command: loads the initial RAM disks for the kernel loaded before",u"polecenie GRUB: wczytuje pocz\x0105tkowe dyski RAM dla wcze\x015bniej wczytanego j\x0105" u"dra",u"GNU GRUB manual"},
    {u"chainloader",false,u"GRUB command: loads another boot program from the file",u"polecenie GRUB: wczytuje z pliku inny program rozruchowy",u"GNU GRUB manual"},
    {u"search",false,u"GRUB command: looks for a device by file, label or file system identifier",u"polecenie GRUB: szuka urz\x0105" u"dzenia po pliku, etykiecie albo identyfikatorze systemu plik\x00f3w",u"GNU GRUB manual"},
    {u"insmod",false,u"GRUB command: loads a GRUB module",u"polecenie GRUB: \x0142" u"aduje modu\x0142 GRUB",u"GNU GRUB manual"},
    {u"set",false,u"GRUB command: sets a variable; alone it prints all variables",u"polecenie GRUB: ustawia zmienn\x0105; samo wypisuje wszystkie zmienne",u"GNU GRUB manual"},
    {u"export",false,u"GRUB command: makes a variable visible to configuration files loaded later",u"polecenie GRUB: udost\x0119pnia zmienn\x0105 plikom konfiguracji wczytywanym p\x00f3\x017aniej",u"GNU GRUB manual"},
    {u"echo",false,u"GRUB command: shows the text",u"polecenie GRUB: wy\x015bwietla tekst",u"GNU GRUB manual"},
    {u"boot",false,u"GRUB command: starts the system or boot program that was loaded",u"polecenie GRUB: uruchamia wczytany system albo program rozruchowy",u"GNU GRUB manual"},
    {u"configfile",false,u"GRUB command: loads a configuration file and shows its menu",u"polecenie GRUB: wczytuje plik konfiguracji i pokazuje jego menu",u"GNU GRUB manual"},
    {u"menuentry",false,u"GRUB command: defines a menu entry",u"polecenie GRUB: definiuje pozycj\x0119 menu",u"GNU GRUB manual"},
    {u"submenu",false,u"GRUB command: defines a submenu",u"polecenie GRUB: definiuje podmenu",u"GNU GRUB manual"},
    {u"fwsetup",false,u"GRUB command: restarts into the firmware setup",u"polecenie GRUB: restart do ustawie\x0144 firmware",u"GNU GRUB manual"},
    {u"halt",false,u"GRUB command: turns the computer off",u"polecenie GRUB: wy\x0142\x0105" u"cza komputer",u"GNU GRUB manual"},
    {u"reboot",false,u"GRUB command: restarts the computer",u"polecenie GRUB: uruchamia komputer ponownie",u"GNU GRUB manual"},
    {u"normal",false,u"GRUB command: enters normal mode and shows the menu",u"polecenie GRUB: przechodzi do trybu normalnego i pokazuje menu",u"GNU GRUB manual"},
    {u"ls",false,u"GRUB command: lists devices or files",u"polecenie GRUB: wypisuje urz\x0105" u"dzenia albo pliki",u"GNU GRUB manual"},
    {u"cat",false,u"GRUB command: shows the contents of a file",u"polecenie GRUB: pokazuje zawarto\x015b\x0107 pliku",u"GNU GRUB manual"},
    {u"loadfont",false,u"GRUB command: loads font files",u"polecenie GRUB: wczytuje pliki czcionek",u"GNU GRUB manual"},
    {u"terminal_output",false,u"GRUB command: lists or chooses the output terminal",u"polecenie GRUB: wypisuje albo wybiera terminal wyj\x015b" u"ciowy",u"GNU GRUB manual"},
    {u"sleep",false,u"GRUB command: waits the given number of seconds",u"polecenie GRUB: czeka podan\x0105 liczb\x0119 sekund",u"GNU GRUB manual"},
    {u"probe",false,u"GRUB command: gets information about a device",u"polecenie GRUB: pobiera informacje o urz\x0105" u"dzeniu",u"GNU GRUB manual"},
    // ---- Limine entry keys -------------------------------------------------------------------------
    {u"protocol:",false,u"Limine: the boot protocol used to start the entry",u"Limine: protok\x00f3\x0142 rozruchu u\x017cywany do uruchomienia pozycji",u"Limine CONFIG.md"},
    {u"path:",false,u"Limine: the path of the kernel or program the entry starts",u"Limine: \x015b" u"cie\x017cka j\x0105" u"dra albo programu, kt\x00f3ry pozycja uruchamia",u"Limine CONFIG.md"},
    {u"kernel_path:",false,u"Limine: the path of the kernel or program the entry starts",u"Limine: \x015b" u"cie\x017cka j\x0105" u"dra albo programu, kt\x00f3ry pozycja uruchamia",u"Limine CONFIG.md"},
    {u"module_path:",false,u"Limine: the path of a module loaded with the kernel, for example the initial ramdisk",u"Limine: \x015b" u"cie\x017cka modu\x0142u \x0142" u"adowanego z j\x0105" u"drem, na przyk\x0142" u"ad pocz\x0105tkowego dysku RAM",u"Limine CONFIG.md"},
    {u"cmdline:",false,u"Limine: the command line passed to the kernel or program",u"Limine: wiersz polece\x0144 przekazywany do j\x0105" u"dra albo programu",u"Limine CONFIG.md"},
    {u"kernel_cmdline:",false,u"Limine: the command line passed to the kernel or program",u"Limine: wiersz polece\x0144 przekazywany do j\x0105" u"dra albo programu",u"Limine CONFIG.md"},
    {u"comment:",false,u"Limine: a comment shown with the entry",u"Limine: komentarz pokazywany przy pozycji",u"Limine CONFIG.md"},
    // ---- systemd and its services (what Debian, Ubuntu, Fedora and Arch start as init) -----------------------
    {u"single",false,u"systemd: the system starts in rescue mode, the same as systemd.unit=rescue.target",u"systemd: system startuje w trybie ratunkowym, tak jak systemd.unit=rescue.target",u"systemd(1)"},
    {u"rescue",false,u"systemd: the system starts in rescue mode, the same as systemd.unit=rescue.target",u"systemd: system startuje w trybie ratunkowym, tak jak systemd.unit=rescue.target",u"systemd(1)"},
    {u"emergency",false,u"systemd: the system starts in emergency mode, the same as systemd.unit=emergency.target",u"systemd: system startuje w trybie awaryjnym, tak jak systemd.unit=emergency.target",u"systemd(1)"},
    {u"systemd.unit=",true,u"systemd: the unit started at boot instead of default.target",u"systemd: jednostka uruchamiana przy starcie zamiast default.target",u"systemd(1)"},
    {u"systemd.show_status",true,u"systemd: whether short service status lines are shown on the console during start",u"systemd: czy podczas startu na konsoli s\x0105 pokazywane kr\x00f3tkie wiersze stanu us\x0142ug",u"systemd(1)"},
    {u"fsck.mode=",true,u"systemd: file system checks: auto when needed, force always, skip never",u"systemd: sprawdzanie system\x00f3w plik\x00f3w: auto w razie potrzeby, force zawsze, skip nigdy",u"systemd-fsck@.service(8)"},
    {u"net.ifnames=",true,u"systemd-udevd: predictable network interface names; 0 turns them off",u"systemd-udevd: przewidywalne nazwy interfejs\x00f3w sieciowych; 0 je wy\x0142\x0105" u"cza",u"systemd-udevd.service(8)"},
    {u"plymouth.enable=",true,u"systemd: 0 turns the Plymouth boot splash off",u"systemd: 0 wy\x0142\x0105" u"cza ekran startowy Plymouth",u"systemd kernel-command-line(7)"},
    {u"rd.luks.name=",true,u"systemd: the LUKS encrypted device with this UUID is activated at boot under the given name",u"systemd: zaszyfrowane urz\x0105" u"dzenie LUKS o tym UUID jest aktywowane przy starcie pod podan\x0105 nazw\x0105",u"systemd-cryptsetup-generator(8)"},
    // ---- dracut initial RAM disk (Fedora; optional in Debian, Ubuntu and Arch) -------------------------------
    {u"rd.break",true,u"dracut: the initial RAM disk stops and opens a shell, at the end or before the named stage",u"dracut: pocz\x0105tkowy dysk RAM zatrzymuje si\x0119 i otwiera pow\x0142ok\x0119, na ko\x0144" u"cu albo przed podanym etapem",u"dracut.cmdline(7)"},
    {u"rd.shell",false,u"dracut: a shell is opened if the root file system cannot be mounted",u"dracut: gdy nie uda si\x0119 zamontowa\x0107 g\x0142\x00f3wnego systemu plik\x00f3w, otwierana jest pow\x0142oka",u"dracut.cmdline(7)"},
    {u"rd.luks.uuid=",true,u"dracut: only the LUKS encrypted partitions with this UUID are activated",u"dracut: aktywowane s\x0105 tylko zaszyfrowane partycje LUKS o tym UUID",u"dracut.cmdline(7)"},
    {u"rd.lvm.lv=",true,u"dracut: only the logical volumes with this name are activated",u"dracut: aktywowane s\x0105 tylko woluminy logiczne o tej nazwie",u"dracut.cmdline(7)"},
    // ---- boot screen -----------------------------------------------------------------------------------------
    {u"splash",false,u"Plymouth: the graphical boot screen is shown",u"Plymouth: pokazywany jest graficzny ekran startowy",u"Arch Wiki: Plymouth"},
    {u"rhgb",false,u"Fedora: graphical boot screen (Plymouth); without this word the start is shown as text",u"Fedora: graficzny ekran startowy (Plymouth); bez tego s\x0142owa start jest pokazywany jako tekst",u"Fedora Wiki: Features/BetterStartup"},
    // ---- Arch Linux ------------------------------------------------------------------------------------------
    {u"cryptdevice=",true,u"Arch encrypt hook: the device that holds the encrypted root file system, and its device mapper name",u"hak encrypt w Arch: urz\x0105" u"dzenie z zaszyfrowanym g\x0142\x00f3wnym systemem plik\x00f3w i jego nazwa w device mapperze",u"Arch Wiki: dm-crypt/System configuration"},
    {u"modprobe.blacklist=",true,u"modprobe: these kernel modules are not loaded automatically; they can still be loaded by hand",u"modprobe: te modu\x0142y j\x0105" u"dra nie s\x0105 \x0142" u"adowane automatycznie; nadal mo\x017cna je za\x0142" u"adowa\x0107 r\x0119" u"cznie",u"Arch Wiki: Kernel module"},
    {u"nouveau.modeset=",true,u"Nouveau driver for NVIDIA cards: 0 turns kernel mode setting off for this driver",u"sterownik Nouveau kart NVIDIA: 0 wy\x0142\x0105" u"cza w tym sterowniku ustawianie trybu graficznego przez j\x0105" u"dro",u"Arch Wiki: Kernel mode setting"},
    {u"zswap.enabled=",true,u"kernel: 0 turns zswap off, 1 turns it on; zswap is a compressed cache in memory for swapped pages",u"j\x0105" u"dro: 0 wy\x0142\x0105" u"cza zswap, 1 w\x0142\x0105" u"cza; zswap to skompresowana pami\x0119\x0107 podr\x0119" u"czna w RAM dla stron wymiany",u"Arch Wiki: Zswap"},
    {u"archisobasedir=",true,u"Arch install medium: the directory on the medium where all its files are",u"no\x015bnik instalacyjny Arch: katalog na no\x015bniku, w kt\x00f3rym s\x0105 wszystkie jego pliki",u"mkinitcpio-archiso README.bootparams"},
    {u"archisosearchuuid=",true,u"Arch install medium: the UUID used to look for the device that holds the medium's files",u"no\x015bnik instalacyjny Arch: UUID, po kt\x00f3rym szukane jest urz\x0105" u"dzenie z plikami no\x015bnika",u"mkinitcpio-archiso README.bootparams"},
    // ---- Debian live systems and installer -------------------------------------------------------------------
    {u"boot=live",false,u"Debian live: turns live-boot on, which starts the system from a live medium",u"Debian live: w\x0142\x0105" u"cza live-boot, kt\x00f3ry uruchamia system z no\x015bnika live",u"Debian live-boot(7)"},
    {u"toram",false,u"Debian live: the whole read-only medium is copied to memory before the root file system is mounted",u"Debian live: ca\x0142y no\x015bnik tylko do odczytu jest kopiowany do pami\x0119" u"ci przed zamontowaniem g\x0142\x00f3wnego systemu plik\x00f3w",u"Debian live-boot(7)"},
    {u"persistence",false,u"Debian live: live-boot looks on the devices for persistence media: partitions, file systems or image files named persistence",u"Debian live: live-boot szuka na urz\x0105" u"dzeniach no\x015bnik\x00f3w trwa\x0142o\x015b" u"ci: partycji, system\x00f3w plik\x00f3w albo plik\x00f3w obraz\x00f3w o nazwie persistence",u"Debian live-boot(7)"},
    {u"---",false,u"Debian installer: options after this mark are also copied to the boot loader configuration of the installed system",u"instalator Debiana: opcje po tym znaku s\x0105 te\x017c kopiowane do konfiguracji programu rozruchowego zainstalowanego systemu",u"Debian Installation Guide 5.3"},
    // ---- Ubuntu's GRUB (patches of the Ubuntu grub2 package) -------------------------------------------------
    {u"$vt_handoff",false,u"Ubuntu: a GRUB variable that becomes vt.handoff=7 when the graphics mode is kept, for a start without flicker",u"Ubuntu: zmienna GRUB, kt\x00f3ra staje si\x0119 vt.handoff=7, gdy tryb graficzny jest zachowywany, dla startu bez migotania",u"Ubuntu grub2: vt-handoff.patch"},
    {u"vt.handoff=",true,u"Ubuntu: kernel option for a start without flicker when GRUB's graphics mode is kept",u"Ubuntu: opcja j\x0105" u"dra dla startu bez migotania, gdy zachowany jest tryb graficzny GRUB",u"Ubuntu grub2: vt-handoff.patch"},
    {u"recordfail",false,u"Ubuntu GRUB function: saves a mark that a start has begun; if that start fails, the menu is shown next time",u"funkcja GRUB w Ubuntu: zapisuje znacznik rozpocz\x0119" u"cia startu; gdy start si\x0119 nie uda, nast\x0119pnym razem pokazywane jest menu",u"Ubuntu grub2: quick-boot.patch"},
    {u"gfxmode",false,u"Ubuntu GRUB function: sets gfxpayload, the video mode in which Linux starts",u"funkcja GRUB w Ubuntu: ustawia gfxpayload, tryb obrazu, w kt\x00f3rym startuje Linux",u"Ubuntu grub2: vt-handoff.patch"},
    // ---- GRUB of Fedora and Ubuntu: Secure Boot loader commands ----------------------------------------------
    {u"linuxefi",false,u"GRUB command of Fedora and Ubuntu: loads a Linux kernel through the EFI entry point; with Secure Boot its signature is checked by shim",u"polecenie GRUB w Fedorze i Ubuntu: wczytuje j\x0105" u"dro Linux przez punkt wej\x015b" u"cia EFI; przy Secure Boot jego podpis sprawdza shim",u"Ubuntu grub2: ubuntu-linuxefi.patch; rhboot/grub2 linux.c"},
    {u"initrdefi",false,u"GRUB command of Fedora and Ubuntu: loads the initial RAM disk for a kernel loaded with linuxefi",u"polecenie GRUB w Fedorze i Ubuntu: wczytuje pocz\x0105tkowy dysk RAM dla j\x0105" u"dra wczytanego przez linuxefi",u"Ubuntu grub2: ubuntu-linuxefi.patch; rhboot/grub2 linux.c"},
    // ---- more of GNU GRUB: variables, modules and the functions grub-mkconfig writes -------------------------
    {u"gfxpayload=",true,u"GRUB variable: the video mode in which the Linux kernel starts: text, keep or a resolution",u"zmienna GRUB: tryb obrazu, w kt\x00f3rym startuje j\x0105" u"dro Linux: text, keep albo rozdzielczo\x015b\x0107",u"GNU GRUB manual"},
    {u"gfxmode=",true,u"GRUB variable: the resolution of GRUB's graphical terminal",u"zmienna GRUB: rozdzielczo\x015b\x0107 terminala graficznego GRUB",u"GNU GRUB manual"},
    {u"blscfg",false,u"GRUB command: loads Boot Loader Specification entries into the menu",u"polecenie GRUB: wczytuje do menu pozycje Boot Loader Specification",u"GNU GRUB manual; Fedora Wiki: Changes/BootLoaderSpecByDefault"},
    {u"gzio",false,u"GRUB module: decompression of files compressed with gzip (GZ)",u"modu\x0142 GRUB: dekompresja plik\x00f3w skompresowanych algorytmem gzip (GZ)",u"GNU GRUB manual"},
    {u"part_gpt",false,u"GRUB module: reading of disks with a GPT partition table",u"modu\x0142 GRUB: odczyt dysk\x00f3w z tablic\x0105 partycji GPT",u"GNU GRUB manual"},
    {u"savedefault",false,u"GRUB function: saves the current entry as the new default",u"funkcja GRUB: zapisuje bie\x017c\x0105" u"c\x0105 pozycj\x0119 jako now\x0105 domy\x015bln\x0105",u"GNU GRUB manual"},
    {u"load_video",false,u"GRUB function written by grub-mkconfig: loads the video modules",u"funkcja GRUB zapisywana przez grub-mkconfig: \x0142" u"aduje modu\x0142y obrazu",u"GRUB util/grub.d/00_header.in"},
    // ---- more of the Linux kernel command line, as used by Fedora, Ubuntu and Arch entries -------------------
    {u"selinux=",true,u"kernel: 0 turns SELinux off at start, 1 turns it on",u"j\x0105" u"dro: 0 wy\x0142\x0105" u"cza SELinux przy starcie, 1 w\x0142\x0105" u"cza",u"Linux kernel-parameters.txt"},
    {u"enforcing=",true,u"kernel: SELinux at start: 0 is permissive, only logs; 1 is enforcing, denies and logs",u"j\x0105" u"dro: SELinux przy starcie: 0 to tryb pob\x0142" u"a\x017cliwy, tylko zapisuje; 1 to tryb wymuszaj\x0105" u"cy, odmawia i zapisuje",u"Linux kernel-parameters.txt"},
    {u"apparmor=",true,u"kernel: 0 turns AppArmor off at start, 1 turns it on",u"j\x0105" u"dro: 0 wy\x0142\x0105" u"cza AppArmor przy starcie, 1 w\x0142\x0105" u"cza",u"Linux kernel-parameters.txt"},
    {u"crashkernel=",true,u"kernel: memory reserved for a crash kernel, which is started after a kernel panic",u"j\x0105" u"dro: pami\x0119\x0107 zarezerwowana dla j\x0105" u"dra awaryjnego, uruchamianego po b\x0142\x0119" u"dzie krytycznym j\x0105" u"dra",u"Linux kernel-parameters.txt"},
    {u"intel_iommu=",true,u"kernel: on turns the Intel IOMMU driver on, off turns it off",u"j\x0105" u"dro: on w\x0142\x0105" u"cza sterownik Intel IOMMU, off go wy\x0142\x0105" u"cza",u"Linux kernel-parameters.txt"},
};
inline constexpr size_t wordCount=sizeof(words)/sizeof(words[0]);
// The row that explains `text[0..length)`, or wordCount. Case matters: these are commands and options.
inline size_t Find(const char16_t* text,size_t length) {
    for(size_t i=0;i<wordCount;++i) {
        const char16_t* w=words[i].word;size_t n=0;while(w[n])++n;
        if(words[i].prefix?length<n:length!=n)continue;
        size_t k=0;while(k<n && text[k]==w[k])++k;
        if(k==n)return i;
    }
    return wordCount;
}
}  // namespace hakefi::loaderwords
