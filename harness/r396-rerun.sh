#!/bin/sh
# r396: reruns with timings shorter than each manager's own countdown.
E=${1:-charon-win-r396/CHARON.EFI}; W=$PWD
run(){ n=$1; shift; rm -rf r396-$n; env "$@" 2>&1 | grep -av "pkg_resources\|declare_namespace\|UserWarning"; }
P="timeout 600 python3.15 charon-r396-pick.py $E"
run grub2   WAIT=3 KEYS="down down up" $P grub2 GRUB
run limine2 WAIT=8 KEYS="down up" $P limine2 Limine --winboot --limine
run oc2     WAIT=20 KEYS="down down up" timeout 600 python3.15 charon-r396-pick.py charon-win-r396rt/CHARON.EFI oc2 OpenCore --winboot --opencore
run winbm2  WAIT=30 KEYS="down up" $P winbm2 "(Windows" --winboot
