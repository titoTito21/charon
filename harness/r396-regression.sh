#!/bin/sh
# r396: every boot manager of the test set, picked by name from Charon's device list.
E=${1:-charon-win-r396/CHARON.EFI}; W=$PWD
run(){ n=$1; shift; rm -rf r396-$n; env "$@" 2>&1 | grep -av "pkg_resources\|declare_namespace\|UserWarning"; }
P="timeout 600 python3.15 charon-r396-pick.py $E"
run grub    WAIT=15 KEYS="down down up" $P grub GRUB
run sdboot  WAIT=20 KEYS="down up" $P sdboot systemd-boot --winboot --sdboot
run limine  WAIT=30 KEYS="down up" $P limine Limine --winboot --limine
run opencore WAIT=60 KEYS="down down up" $P opencore OpenCore --winboot --opencore
run clover  WAIT=60 KEYS="right left" $P clover Clover --winboot --clover
run winbm   WAIT=30 KEYS="down up" $P winbm "Windows" --winboot
run refind  WAIT=20 KEYS="right left" $P refind rEFInd --refind
run memtest WAIT=25 $P memtest "Memtest|memtest" --winboot --memtest
run syslinux CHARON_USB=$W/app-sticks/syslinux.img WAIT=15 KEYS="down down up" $P syslinux "Syslinux|USB" --usb
run ipxe    CHARON_USB=$W/app-sticks/ipxe.img WAIT=40 $P ipxe "iPXE|USB" --usb
run ventoy  CHARON_USB=$W/app-sticks/ventoy3.img WAIT=45 KEYS="down down up" $P ventoy "Ventoy|USB" --usb
run haiku   CHARON_CDROM=${HAIKU_ISO:?set HAIKU_ISO to a Haiku R1beta6 x86_64 ISO} CHARON_MEM=2048 BURST="$(printf 'spc %.0s' $(seq 40))" WAIT=4 KEYS="down up" $P haiku "Haiku|CD|DVD|Płyta"
run freebsd CHARON_CDROM=$W/freebsd-dl/bootonly.iso CHARON_MEM=2048 BURST="spc" WAIT=30 KEYS="down up" $P freebsd "FreeBSD|CD|DVD|Płyta"
run debian  CHARON_CDROM=$W/debian-dl/netinst.iso CHARON_MEM=2048 WAIT=30 KEYS="down down up" $P debian "Debian|CD|DVD|Płyta"
