#!/bin/sh
# Charon UEFI screen reader add-on (r385) for Linux. GPL-2.0-or-later.
# Copies EFI/HAK (CHARONVOICE.EFI, AudioDxe.efi, cues) to the EFI system
# partition and adds a UEFI driver option (Driver####) with efibootmgr.
# BootOrder and the installed boot loaders are never changed.
#   sudo sh zainstaluj-dodatek-uefi.sh            install
#   sudo sh zainstaluj-dodatek-uefi.sh --uninstall
# Not yet tried on a Linux host by the author: report problems.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
label='Charon voice'
[ "$(id -u)" = 0 ] || { echo "Uruchom jako root (sudo)."; exit 1; }
[ -d /sys/firmware/efi ] || { echo "System nie wystartował w trybie UEFI; dodatek nie zadziała."; exit 1; }
command -v efibootmgr >/dev/null || { echo "Brak efibootmgr (zainstaluj pakiet efibootmgr)."; exit 1; }
esp=$(findmnt -n -o TARGET /boot/efi 2>/dev/null || findmnt -n -o TARGET /efi 2>/dev/null || findmnt -n -o TARGET /boot 2>/dev/null || true)
[ -n "$esp" ] && [ -d "$esp/EFI" ] || { echo "Nie znaleziono zamontowanej partycji EFI (/boot/efi, /efi, /boot)."; exit 1; }
existing=$(efibootmgr --driver 2>/dev/null | sed -n "s/^Driver\([0-9A-Fa-f]\{4\}\)\*\{0,1\} $label.*/\1/p" | head -n1)
if [ "${1:-}" = --uninstall ]; then
    [ -n "$existing" ] && efibootmgr --driver --bootnum "$existing" --delete-bootnum >/dev/null && echo "Usunięto Driver$existing."
    if [ -f "$esp/EFI/HAK/addon.txt" ] && [ ! -f "$esp/EFI/HAK/HAKBOOT.EFI" ]; then rm -rf "$esp/EFI/HAK"; echo "Pliki dodatku usunięte."; fi
    exit 0
fi
if command -v mokutil >/dev/null && mokutil --sb-state 2>/dev/null | grep -q enabled; then
    echo "Secure Boot jest włączony: firmware nie załaduje niepodpisanego sterownika Charona."
    echo "Wyłącz Secure Boot w ustawieniach firmware na czas próby i uruchom skrypt ponownie. Nic nie zmieniono."; exit 1
fi
src="$here/EFI/HAK"
for f in CHARONVOICE.EFI AudioDxe.efi; do [ -f "$src/$f" ] || { echo "Brak $src/$f."; exit 1; }; done
mkdir -p "$esp/EFI/HAK/audio"
for f in CHARONVOICE.EFI AudioDxe.efi; do cp "$src/$f" "$esp/EFI/HAK/$f.new" && cmp -s "$src/$f" "$esp/EFI/HAK/$f.new" && mv "$esp/EFI/HAK/$f.new" "$esp/EFI/HAK/$f"; done
for f in "$src"/audio/*.pcm; do case "$(basename "$f")" in background-*|start-*) ;; *) cp "$f" "$esp/EFI/HAK/audio/";; esac; done
[ -f "$esp/EFI/HAK/audio-language" ] || { case "${CHARON_LANGUAGE:-en}" in pl) printf pl;; *) printf en;; esac > "$esp/EFI/HAK/audio-language"; }
printf 'Charon UEFI screen reader add-on r385; installed by zainstaluj-dodatek-uefi.sh\r\n' > "$esp/EFI/HAK/addon.txt"
sync
src_dev=$(findmnt -n -o SOURCE "$esp")
disk=/dev/$(lsblk -n -o PKNAME "$src_dev" | head -n1)
part=$(cat "/sys/class/block/$(basename "$src_dev")/partition")
[ -n "$existing" ] && efibootmgr --driver --bootnum "$existing" --delete-bootnum >/dev/null
efibootmgr --driver --create --disk "$disk" --part "$part" --loader '\EFI\HAK\CHARONVOICE.EFI' --label "$label" >/dev/null
efibootmgr --driver | grep -q "$label" || { echo "Firmware nie przyjęło wpisu sterownika."; exit 1; }
echo "Zainstalowano: firmware załaduje \\EFI\\HAK\\CHARONVOICE.EFI z $disk, partycja $part. Kolejność rozruchu bez zmian."
