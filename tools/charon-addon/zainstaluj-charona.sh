#!/bin/sh
# Charon installer for Linux (r391). GPL-2.0-or-later.
#
# Three ways to install, chosen in a short dialogue (or with --mode):
#   reader   screen reader from power-on: CHARONVOICE.EFI as a UEFI driver
#            option (Driver####). Reads firmware menus and setup too. With
#            Secure Boot on, firmware checks it against its own db list only
#            (MOK does not apply to firmware drivers): the owner enrolls the
#            certificate in db, or Secure Boot is turned off.
#   shim     screen reader through shim (Secure Boot with MOK): Charon
#            without its menu (\EFI\HAK\reader-only) starts the current
#            default system at once and reads it. It starts after the
#            firmware's own menus, so those are not read.
#   manager  Charon boot manager (menu, settings, device list, reader);
#            through shim when Secure Boot is on. r399: also the reader
#            outside Charon (Driver####, as in reader) unless --reader no.
# "Continue" of Charon is set to the current default boot entry (for
# example Windows Boot Manager) unless Charon already has a Continue target.
# With Secure Boot on, a MOK key of THIS computer is made, Charon's EFI
# files are signed with it and mokutil --import asks for a one-time
# password: at the next start shim's MokManager asks to confirm the key
# (Enroll MOK) with that password. MokManager does not speak: the steps are
# printed below and said by Linux's screen reader here.
#
#   sudo sh zainstaluj-charona.sh [--mode reader|shim|manager] [--default yes|no] [--reader yes|no] [--yes] [--dry-run]
#   sudo sh zainstaluj-charona.sh --uninstall
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src="$here/EFI/HAK"
mode='';default='';withreader='';assume=0;dry=0;uninstall=0
while [ $# -gt 0 ]; do
    case "$1" in
        --mode) mode=$2;shift;; --default) default=$2;shift;; --reader) withreader=$2;shift;; --yes) assume=1;; --dry-run) dry=1;; --uninstall) uninstall=1;;
        *) echo "Nieznana opcja: $1";exit 2;;
    esac;shift
done
say() { printf '%s\n' "$*"; }
run() { if [ $dry = 1 ]; then say "[próba] $*"; else "$@"; fi; }
ask() { # ask "question" default -> answer
    if [ $assume = 1 ]; then REPLY=$2;return; fi
    printf '%s ' "$1";read -r REPLY || REPLY=$2;[ -n "$REPLY" ] || REPLY=$2
}
[ "$(id -u)" = 0 ] || [ $dry = 1 ] || { say "Uruchom jako root (sudo).";exit 1; }
[ -d /sys/firmware/efi ] || { say "System nie wystartował w trybie UEFI; Charon nie zadziała (tryb BIOS).";exit 1; }
command -v efibootmgr >/dev/null || { say "Brak efibootmgr (zainstaluj pakiet efibootmgr).";exit 1; }
esp=$(findmnt -n -o TARGET /boot/efi 2>/dev/null || findmnt -n -o TARGET /efi 2>/dev/null || findmnt -n -o TARGET /boot 2>/dev/null || true)
[ -n "$esp" ] && [ -d "$esp/EFI" ] || { say "Nie znaleziono zamontowanej partycji EFI (/boot/efi, /efi, /boot).";exit 1; }
src_dev=$(findmnt -n -o SOURCE "$esp");disk=/dev/$(lsblk -n -o PKNAME "$src_dev" | head -n1)
part=$(cat "/sys/class/block/$(basename "$src_dev")/partition")
secure=0
if command -v mokutil >/dev/null && mokutil --sb-state 2>/dev/null | grep -q enabled; then secure=1; fi
vars=/sys/firmware/efi/efivars;g=8be4df61-93ca-11d2-aa0d-00e098032b8c
entry_of() { efibootmgr ${2:+"$2"} 2>/dev/null | sed -n "s/^$3\([0-9A-Fa-f]\{4\}\)\*\{0,1\} $1\$/\1/p;s/^$3\([0-9A-Fa-f]\{4\}\)\*\{0,1\} $1	.*/\1/p" | head -n1; }

if [ $uninstall = 1 ]; then
    for l in 'Charon voice'; do n=$(entry_of "$l" --driver Driver);[ -n "$n" ] && run efibootmgr --driver --bootnum "$n" --delete-bootnum >/dev/null && say "Usunięto Driver$n ($l)."; done
    for l in 'Charon' 'Charon czytnik'; do n=$(entry_of "$l" '' Boot);[ -n "$n" ] && run efibootmgr --bootnum "$n" --delete-bootnum >/dev/null && say "Usunięto Boot$n ($l)."; done
    if [ -f "$esp/EFI/HAK/installed-by.txt" ]; then run rm -rf "$esp/EFI/HAK";say "Pliki Charona usunięte z partycji EFI."; fi
    [ -f /var/lib/charon/mok.der ] && command -v mokutil >/dev/null && say "Klucz MOK Charona zostaje w firmware; usuniesz go: sudo mokutil --delete /var/lib/charon/mok.der (potwierdzenie przy starcie)."
    exit 0
fi
for f in HAKBOOT.EFI CHARONVOICE.EFI AudioDxe.efi; do [ -f "$src/$f" ] || { say "Brak $src/$f.";exit 1; }; done

say "Charon — instalacja na tym komputerze (dysk $disk, partycja EFI $part)."
[ $secure = 1 ] && say "Secure Boot jest włączony."
if [ -z "$mode" ]; then
    say "Co zainstalować?"
    say "  1. Czytnik ekranu od włączenia komputera (czyta też menu i ustawienia firmware)."
    say "  2. Czytnik ekranu przez shim, bez menu Charona (Secure Boot z kluczem MOK; menu firmware nie są czytane)."
    say "  3. Menedżer rozruchu Charon z czytnikiem ekranu (menu, ustawienia, lista urządzeń), do tego czytnik od włączenia komputera (poza Charonem)."
    ask "Wybierz 1, 2 albo 3 [1]:" 1
    case "$REPLY" in 1) mode=reader;; 2) mode=shim;; 3) mode=manager;; *) say "Nic nie zmieniono.";exit 1;; esac
fi
case "$mode" in reader|shim|manager) ;; *) say "Tryb: reader, shim albo manager.";exit 2;; esac
use_shim=0
if [ "$mode" = shim ] || { [ "$mode" = manager ] && [ $secure = 1 ]; }; then use_shim=1; fi
if [ "$mode" = reader ] && [ $secure = 1 ]; then
    say "Uwaga: firmware sprawdza sterownik Driver#### tylko na swojej liście db, nie w MOK. Bez wpisania certyfikatu do db przez właściciela albo wyłączenia Secure Boot czytnik nie wystartuje (komputer uruchomi się jak dotąd)."
fi
if [ "$mode" != reader ]; then
    if [ "$mode" = shim ]; then default=yes; fi
    if [ -z "$default" ]; then ask "Ustawić Charona jako pierwszego w kolejności rozruchu (domyślny)? [T/n]:" T; case "$REPLY" in [nN]*) default=no;; *) default=yes;; esac; fi
fi
# r399: the boot manager comes with the reader outside Charon (Driver####) by default.
if [ "$mode" = manager ] && [ -z "$withreader" ]; then
    if [ $assume = 1 ]; then withreader=yes; else ask "Zainstalować też czytnik ekranu od włączenia komputera, poza Charonem (menu i ustawienia firmware, F12)? [T/n]:" T; case "$REPLY" in [nN]*) withreader=no;; *) withreader=yes;; esac; fi
fi
[ "$mode" = reader ] && withreader=yes
[ "$mode" = shim ] && withreader=no
if [ "$mode" = manager ] && [ "$withreader" = yes ] && [ $secure = 1 ]; then
    say "Uwaga: czytnik od włączenia komputera (Driver####) firmware sprawdza tylko na liście db, nie w MOK: bez certyfikatu w db albo przy włączonym Secure Boot nie wystartuje; menedżer Charon przez shim działa niezależnie."
fi
shim='';mm=''
if [ $use_shim = 1 ]; then
    for d in /usr/lib/shim /usr/share/shim/x64 /usr/share/shim; do
        [ -z "$shim" ] && [ -f "$d/shimx64.efi.signed" ] && shim="$d/shimx64.efi.signed"
        [ -z "$shim" ] && [ -f "$d/shimx64.efi" ] && shim="$d/shimx64.efi"
        [ -z "$mm" ] && [ -f "$d/mmx64.efi.signed" ] && mm="$d/mmx64.efi.signed"
        [ -z "$mm" ] && [ -f "$d/mmx64.efi" ] && mm="$d/mmx64.efi"
    done
    if [ -z "$shim" ]; then for f in "$esp"/EFI/*/shimx64.efi; do [ -f "$f" ] && shim=$f && break; done; fi
    if [ -z "$mm" ]; then for f in "$esp"/EFI/*/mmx64.efi; do [ -f "$f" ] && mm=$f && break; done; fi
    [ -n "$shim" ] && [ -n "$mm" ] || { say "Nie znaleziono shim podpisanego przez Microsoft (pakiet shim-signed) ani MokManagera. Nic nie zmieniono.";exit 1; }
    if [ $secure = 1 ]; then
        for c in openssl sbsign mokutil; do command -v $c >/dev/null || { say "Brak programu $c (pakiety openssl, sbsigntool, mokutil). Nic nie zmieniono.";exit 1; }; done
    fi
fi
# The current default entry becomes Charon's Continue target.
order=$(efibootmgr | sed -n 's/^BootOrder: //p' | tr ',' ' ')
first='';for n in $order; do l=$(efibootmgr | sed -n "s/^Boot$n\*\{0,1\} \(.*\)/\1/p" | cut -f1);case "$l" in Charon|'Charon czytnik') ;; *) first=$n;break;; esac; done

say ""
say "Podsumowanie:"
case "$mode" in
    reader) say "- czytnik ekranu jako sterownik firmware (Driver####); kolejność rozruchu bez zmian";;
    shim) say "- czytnik ekranu przez shim jako pierwszy wpis rozruchu; potem od razu uruchamia Boot$first";;
    manager) say "- menedżer rozruchu Charon$( [ $use_shim = 1 ] && printf ' przez shim')$( [ "$default" = yes ] && printf ', pierwszy w kolejności rozruchu')";;
esac
[ "$mode" = manager ] && [ "$withreader" = yes ] && say "- czytnik ekranu od włączenia komputera, poza Charonem (Driver####)"
[ "$mode" != reader ] && [ -n "$first" ] && say "- „Kontynuuj” w Charonie: Boot$first ($(efibootmgr | sed -n "s/^Boot$first\*\{0,1\} \(.*\)/\1/p" | cut -f1))"
[ $use_shim = 1 ] && [ $secure = 1 ] && say "- klucz MOK tego komputera; przy następnym starcie trzeba go potwierdzić w MokManagerze hasłem, które zaraz podasz"
say "- pliki w $esp/EFI/HAK"
if [ $assume = 0 ]; then
    printf 'Aby kontynuować, wpisz TAK: ';read -r REPLY || REPLY=''
    [ "$REPLY" = TAK ] || [ "$REPLY" = tak ] || { say "Nic nie zmieniono.";exit 1; }
fi

# ---- files -------------------------------------------------------------------
hak="$esp/EFI/HAK";run mkdir -p "$hak/audio"
copy() { run cp "$1" "$2.new";[ $dry = 1 ] || cmp -s "$1" "$2.new" || { say "Kopia $(basename "$1") uszkodzona. Przerwano.";exit 1; };run mv "$2.new" "$2"; }
for f in CHARONVOICE.EFI AudioDxe.efi; do copy "$src/$f" "$hak/$f"; done
[ -f "$src/CharonShell.efi" ] && copy "$src/CharonShell.efi" "$hak/CharonShell.efi"
if [ "$mode" != reader ]; then copy "$src/HAKBOOT.EFI" "$hak/HAKBOOT.EFI"; fi
for f in "$src"/audio/*.pcm; do [ -f "$f" ] && run cp "$f" "$hak/audio/"; done
[ -f "$hak/audio-language" ] || { case "${CHARON_LANGUAGE:-en}" in pl) l=pl;; *) l=en;; esac;[ $dry = 1 ] || printf %s $l > "$hak/audio-language"; }
if [ "$mode" = shim ]; then [ $dry = 1 ] || printf 'reader-only\r\n' > "$hak/reader-only"; else run rm -f "$hak/reader-only"; fi
[ $dry = 1 ] || printf 'Charon r391; installed by zainstaluj-charona.sh, mode %s\r\n' "$mode" > "$hak/installed-by.txt"
# Continue target: HAKCONT1, id (u16), size (u32), the Boot#### option, HEND.
if [ "$mode" != reader ] && [ -n "$first" ] && [ ! -f "$hak/continue-set" ] && [ $dry = 0 ]; then
    v="$vars/Boot$first-$g"
    if [ -r "$v" ]; then
        size=$(( $(stat -c %s "$v") - 4 ))
        lo=$(( 0x$first & 255 ));hi=$(( 0x$first >> 8 ))
        { printf 'HAKCONT1';printf "\\$(printf %03o $lo)\\$(printf %03o $hi)"
          for s in 0 8 16 24; do printf "\\$(printf %03o $(( (size >> s) & 255 )))"; done
          tail -c +5 "$v";printf 'HEND'; } > "$hak/continue.bin"
        printf '\001' > "$hak/continue-set"
        say "„Kontynuuj” ustawione na Boot$first."
    fi
fi
# Secure Boot: shim loads \EFI\HAK\grubx64.efi (Charon) and checks it against MOK.
loader='\EFI\HAK\HAKBOOT.EFI'
if [ $use_shim = 1 ]; then
    copy "$shim" "$hak/shimx64.efi";copy "$mm" "$hak/mmx64.efi"
    target="$hak/grubx64.efi";loader='\EFI\HAK\shimx64.efi'
    if [ $secure = 1 ]; then
        mkdir -p /var/lib/charon;chmod 700 /var/lib/charon
        if [ ! -f /var/lib/charon/mok.key ]; then
            run openssl req -new -x509 -newkey rsa:2048 -nodes -days 36500 -subj "/CN=Charon (this computer)/" \
                -keyout /var/lib/charon/mok.key -out /var/lib/charon/mok.pem
            run openssl x509 -in /var/lib/charon/mok.pem -outform DER -out /var/lib/charon/mok.der
        fi
        for f in HAKBOOT.EFI:grubx64.efi AudioDxe.efi:AudioDxe.efi CharonShell.efi:CharonShell.efi; do
            a=${f%%:*};b=${f##*:};[ -f "$src/$a" ] || continue
            run sbsign --key /var/lib/charon/mok.key --cert /var/lib/charon/mok.pem --output "$hak/$b" "$src/$a"
        done
        [ $dry = 1 ] || cp /var/lib/charon/mok.der "$hak/charon-mok.cer"
        say "Teraz mokutil poprosi dwa razy o jednorazowe hasło do potwierdzenia klucza przy starcie."
        run mokutil --import /var/lib/charon/mok.der
    else copy "$src/HAKBOOT.EFI" "$target"; fi
fi
sync
# ---- firmware entries ----------------------------------------------------------
if [ "$mode" = reader ]; then
    n=$(entry_of 'Charon voice' --driver Driver);[ -n "$n" ] && run efibootmgr --driver --bootnum "$n" --delete-bootnum >/dev/null
    run efibootmgr --driver --create --disk "$disk" --part "$part" --loader '\EFI\HAK\CHARONVOICE.EFI' --label 'Charon voice' >/dev/null
    say "Zainstalowano czytnik ekranu: firmware załaduje go przy każdym włączeniu komputera."
else
    label=Charon;[ "$mode" = shim ] && label='Charon czytnik'
    n=$(entry_of "$label" '' Boot);[ -n "$n" ] && run efibootmgr --bootnum "$n" --delete-bootnum >/dev/null
    before=$(efibootmgr | sed -n 's/^BootOrder: //p')
    run efibootmgr --create --disk "$disk" --part "$part" --loader "$loader" --label "$label" >/dev/null
    # efibootmgr --create puts the new entry first; keep the old order when not default.
    if [ "$default" = no ] && [ -n "$before" ] && [ $dry = 0 ]; then
        new=$(entry_of "$label" '' Boot);efibootmgr --bootorder "$before,$new" >/dev/null
    fi
    say "Zainstalowano: $label$( [ "$default" = yes ] && printf ', pierwszy w kolejności rozruchu')."
    if [ "$withreader" = yes ]; then
        n=$(entry_of 'Charon voice' --driver Driver);[ -n "$n" ] && run efibootmgr --driver --bootnum "$n" --delete-bootnum >/dev/null
        run efibootmgr --driver --create --disk "$disk" --part "$part" --loader '\EFI\HAK\CHARONVOICE.EFI' --label 'Charon voice' >/dev/null
        say "Zainstalowano też czytnik od włączenia komputera: mówi i czyta menu oraz ustawienia firmware, zanim wystartuje Charon (wyłączenie: w Charonie opcja Mowa od włączenia komputera)."
    fi
fi
if [ $use_shim = 1 ] && [ $secure = 1 ]; then
    say ""
    say "Przy następnym starcie pojawi się niebieski ekran MokManagera (bez mowy)."
    say "  1. W ciągu 10 sekund naciśnij dowolny klawisz."
    say "  2. Wybierz Enroll MOK: strzałka w dół, Enter."
    say "  3. Continue: strzałka w dół, Enter (pierwsza pozycja to podgląd klucza)."
    say "  4. Pytanie Enroll the key(s)?: strzałka w dół na Yes, Enter."
    say "  5. Wpisz hasło podane mokutil i Enter."
    say "  6. Reboot: Enter. Od tego startu Charon mówi."
    say "Bez potwierdzenia shim nie uruchomi Charona; firmware zwykle przechodzi wtedy do następnego wpisu (dotychczasowy system). Kolejność pozycji MokManagera bywa różna w wersjach shim: sprawdzone tylko z dokumentacji, nie na komputerze."
fi
