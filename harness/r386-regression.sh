#!/bin/sh
# r386 full regression: every boot manager in the test set, one VM at a time.
E=${1:-charon-win-r386ah/CHARON.EFI}; T=$2; W=$PWD
run(){ echo "### $1"; shift; "$@" 2>&1 | grep -v "pkg_resources\|declare_namespace\|UserWarning" | tail -4 | cut -c1-200; }
rm -rf r383-sweep-*$T r386-entry-*$T r386-debian-$T
run OpenCore env CHARON_SWEEP_UNTIL="Choose the Operating" timeout 280 python3.15 charon-r383-sweep.py $E oc$T --opencore down down
run systemd-boot timeout 280 python3.15 charon-r383-sweep.py $E sd$T --sdboot down
run Limine env CHARON_SWEEP_WAIT=30 timeout 280 python3.15 charon-r383-sweep.py $E lim$T --limine down
run Clover timeout 280 python3.15 charon-r383-sweep.py $E clo$T --clover
run WindowsBootManager timeout 280 python3.15 charon-r383-sweep.py $E win$T none
run rEFInd env WAIT=14 KEYS="right" timeout 300 python3.15 charon-r386-entry.py $E ref$T 2 --refind
run Syslinux env CHARON_USB=$W/app-sticks/syslinux.img WAIT=15 KEYS="down" timeout 300 python3.15 charon-r386-entry.py $E sys$T 2 --usb
run iPXE env CHARON_USB=$W/app-sticks/ipxe.img WAIT=40 timeout 300 python3.15 charon-r386-entry.py $E ipxe$T 2 --usb
run Ventoy env CHARON_USB=$W/app-sticks/ventoy3.img WAIT=45 KEYS="down down" timeout 300 python3.15 charon-r386-entry.py $E vt$T 2 --usb
run Haiku env CHARON_CDROM=${HAIKU_ISO:?set HAIKU_ISO to a Haiku R1beta6 x86_64 ISO} CHARON_MEM=2048 BURST="$(printf 'spc %.0s' $(seq 40))" WAIT=4 KEYS="down" timeout 400 python3.15 charon-r386-entry.py $E haiku$T 2
run FreeBSD env CHARON_CDROM=$W/freebsd-dl/bootonly.iso CHARON_MEM=2048 BURST="spc" WAIT=30 timeout 500 python3.15 charon-r386-entry.py $E fbsd$T 2
run Memtest env WAIT=25 timeout 300 python3.15 charon-r386-entry.py $E mt$T 3 --winboot --memtest
run DebianNetinst env CHARON_ISO=$W/debian-dl/netinst.iso CHARON_MEM=2048 timeout 400 python3.15 charon-r386-debian.py $E $T down
run Media timeout 590 python3.15 charon-r386-media.py $E $T cd
