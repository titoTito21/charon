#!/bin/sh
E=${1:-charon-win-r396frt/CHARON.EFI}; W=$PWD
run(){ n=$1; shift; rm -rf r396-$n; env "$@" 2>&1 | grep -av "pkg_resources\|declare_namespace\|UserWarning"; }
P="timeout 600 python3.15 charon-r396-pick.py $E"
run limed2  WAIT=8 KEYS="e down up" $P limed2 Limine --winboot --limine
run ref6    WAIT=20 KEYS="right left" $P ref6 rEFInd --refind
run clo2    WAIT=60 KEYS="right left" $P clo2 Clover --winboot --clover
run oc5     WAIT=20 KEYS="down up" $P oc5 OpenCore --winboot --opencore
run winbm3  WAIT=30 $P winbm3 "(Windows" --winboot
run vt2     CHARON_USB=$W/app-sticks/ventoy3.img WAIT=45 KEYS="down up" $P vt2 "Ventoy|USB" --usb
run sys2    CHARON_USB=$W/app-sticks/syslinux.img WAIT=12 KEYS="down up" $P sys2 "Syslinux|USB" --usb
