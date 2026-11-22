#!/bin/sh
# Installs (or with --uninstall removes) the Charon companion programs for Linux:
#   PREFIX/bin/charon-settings            "Charon screen reader settings"
#   PREFIX/bin/charon-bootmanager         "Charon boot manager"
#   PREFIX/share/applications/charon-settings.desktop
#   PREFIX/share/applications/charon-bootmanager.desktop
# from the folder this script lies in (tools/charon-os/linux in the source tree,
# \EFI\HAK\os\linux on Charon's media and on the EFI partition).
#
# Usage: sh install-charon-tools.sh [--prefix DIR] [--uninstall]
# PREFIX is /usr/local unless --prefix is given; DESTDIR is honoured (packaging,
# tests). Running it again changes nothing that is already in place. Nothing
# else is touched: no firmware variable, no file of the EFI partition.
# Messages are in Polish when LANG, LC_ALL or LC_MESSAGES start with pl.
# GPL-2.0-or-later.
set -eu

prefix=/usr/local
uninstall=0
pl=0
case "${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}" in pl*) pl=1 ;; esac
say() { if [ "$pl" = 1 ]; then printf '%s\n' "$2"; else printf '%s\n' "$1"; fi; }

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) [ $# -ge 2 ] || { say '--prefix needs a folder.' 'Opcja --prefix wymaga folderu.'; exit 2; }; prefix=$2; shift ;;
        --prefix=*) prefix=${1#--prefix=} ;;
        --uninstall) uninstall=1 ;;
        -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) say "Unknown option: $1" "Nieznana opcja: $1"; exit 2 ;;
    esac
    shift
done

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
bin=${DESTDIR:-}$prefix/bin
apps=${DESTDIR:-}$prefix/share/applications
programs='charon-settings charon-bootmanager'

if [ "$uninstall" = 1 ]; then
    removed=0
    for p in $programs; do
        for f in "$bin/$p" "$apps/$p.desktop"; do
            if [ -e "$f" ] || [ -L "$f" ]; then rm -f -- "$f"; removed=$((removed + 1)); fi
        done
    done
    if [ "$removed" = 0 ]; then say 'The Charon tools were not installed; nothing to remove.' 'Narzędzia Charona nie były zainstalowane; nie ma czego usuwać.'
    else say "Removed $removed files of the Charon tools from $prefix." "Usunięto $removed plików narzędzi Charona z $prefix."; fi
    command -v update-desktop-database >/dev/null 2>&1 && [ -d "$apps" ] && update-desktop-database -q "$apps" 2>/dev/null || true
    exit 0
fi

for p in $programs; do
    for f in "$here/$p" "$here/$p.desktop"; do
        [ -f "$f" ] || { say "Missing file: $f. Nothing was installed." "Brak pliku: $f. Nic nie zainstalowano."; exit 1; }
    done
done
if ! command -v python3 >/dev/null 2>&1; then
    say 'Notice: python3 was not found; the programs need Python 3.6 or newer.' 'Uwaga: nie znaleziono python3; programy wymagają Pythona 3.6 lub nowszego.'
fi
if ! mkdir -p -- "$bin" "$apps" 2>/dev/null || [ ! -w "$bin" ] || [ ! -w "$apps" ]; then
    say "Cannot write to $bin and $apps. Run as root, for example: sudo sh $0" "Nie można pisać do $bin i $apps. Uruchom jako root, na przykład: sudo sh $0"
    exit 1
fi

copied=0
put() {  # source, target, mode: copy through a temporary file, only when the content or the mode differs
    if [ -f "$2" ] && cmp -s -- "$1" "$2"; then chmod "$3" -- "$2"; return 0; fi
    cp -- "$1" "$2.charon-new"
    chmod "$3" -- "$2.charon-new"
    mv -f -- "$2.charon-new" "$2"
    copied=$((copied + 1))
}
for p in $programs; do
    put "$here/$p" "$bin/$p" 755
    put "$here/$p.desktop" "$apps/$p.desktop" 644
done
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$apps" 2>/dev/null || true

if [ "$copied" = 0 ]; then say "The Charon tools in $prefix are already up to date." "Narzędzia Charona w $prefix są już aktualne."
else say "Installed the Charon tools in $prefix ($copied files written)." "Zainstalowano narzędzia Charona w $prefix (zapisano plików: $copied)."; fi
say 'Start them from the desktop menu or in a terminal: charon-settings (Charon screen reader settings), charon-bootmanager (Charon boot manager).' \
    'Uruchom je z menu pulpitu albo w terminalu: charon-settings (Ustawienia czytnika ekranu Charon), charon-bootmanager (Menedżer rozruchu Charon).'
