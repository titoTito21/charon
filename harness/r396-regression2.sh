#!/bin/sh
# r396 part 2: NetBSD, OpenBSD loaders (CD) and ZFSBootMenu (USB).
E=${1:-charon-win-r396/CHARON.EFI}; W=$PWD
run(){ n=$1; shift; rm -rf r396-$n; env "$@" 2>&1 | grep -av "pkg_resources\|declare_namespace\|UserWarning"; }
P="timeout 600 python3.15 charon-r396-pick.py $E"
run netbsd  CHARON_CDROM=$W/netbsd-dl/boot.iso CHARON_MEM=2048 WAIT=30 KEYS="down up" $P netbsd "NetBSD|CD|DVD|Płyta"
run openbsd CHARON_CDROM=$W/openbsd-dl/cd.iso CHARON_MEM=2048 WAIT=30 $P openbsd "OpenBSD|CD|DVD|Płyta"
run zbm     CHARON_USB=$W/app-sticks/zbm.img CHARON_MEM=2048 WAIT=90 $P zbm "ZFSBootMenu|USB|zfs" --usb
