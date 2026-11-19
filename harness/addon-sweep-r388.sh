#!/bin/sh
# r388: the UEFI add-on (CHARONVOICE.EFI via Driver####, no Charon menu) with
# every boot manager of the regression set, one VM at a time.
V=${1:-charon-win-r387q-drv/CHARONVOICE.EFI}; T=${2:-a}; W=$PWD
run(){ n=$1; shift; case " $SKIP " in *" $n "*) return;; esac; echo "### $n"; rm -rf "addon-sw-$n$T"
  env "$@" CHARON_ADDON_WAIT=${WAIT:-150} timeout 1500 python3.15 charon-addon-test.py "addon-sw-$n$T" "$V" $FLAGS 2>&1 | grep -v "pkg_resources\|declare_namespace\|UserWarning\|Driver0000\|phase" | tail -2
  python3.15 charon-said.py "addon-sw-$n$T/phase2.log"
  for j in addon-sw-$n$T/phase*.log.json; do pid=$(python3.15 -c "import json,sys;print(json.load(open(sys.argv[1]))['pid'])" "$j" 2>/dev/null); [ -n "$pid" ] && taskkill //F //PID $pid >/dev/null 2>&1; done
}
FLAGS= run WindowsBootManager X=1
FLAGS=--opencore run OpenCore CHARON_ADDON_KEYS="75:down"
FLAGS=--sdboot run systemd-boot CHARON_ADDON_KEYS="60:down"
FLAGS=--limine run Limine CHARON_ADDON_KEYS="55:down"
FLAGS=--clover run Clover X=1
FLAGS= run GRUB CHARON_ADDON_GRUB=1 CHARON_ADDON_KEYS="60:down"
FLAGS= run rEFInd CHARON_ADDON_REFIND=1 CHARON_ADDON_KEYS="70:right"
FLAGS= run Memtest CHARON_ADDON_BOOT=$W/memtest-dl/x/mt86p_900_x86_64
FLAGS= run Syslinux CHARON_ADDON_NOBOOT=1 CHARON_USB=$W/app-sticks/syslinux.img CHARON_ADDON_KEYS="70:down"
FLAGS= run iPXE CHARON_ADDON_NOBOOT=1 CHARON_USB=$W/app-sticks/ipxe.img
FLAGS= run Ventoy CHARON_ADDON_NOBOOT=1 CHARON_USB=$W/app-sticks/ventoy3.img CHARON_ADDON_KEYS="80:down"
FLAGS= WAIT=200 run Haiku CHARON_ADDON_NOBOOT=1 CHARON_MEM=2048 CHARON_CDROM=${HAIKU_ISO:-${HAIKU_ISO:?set HAIKU_ISO to a Haiku R1beta6 x86_64 ISO}} CHARON_ADDON_KEYS="6:spc|8:spc|10:spc|12:spc|14:spc|16:spc|18:spc|20:spc|22:spc|24:spc|26:spc|28:spc|30:spc|32:spc|34:spc|36:spc|38:spc|40:spc|42:spc|44:spc|46:spc|48:spc|50:spc|52:spc|54:spc|56:spc|58:spc|60:spc|62:spc|64:spc|66:spc|68:spc|70:spc|120:down"
FLAGS= WAIT=200 run FreeBSD CHARON_ADDON_NOBOOT=1 CHARON_MEM=2048 CHARON_CDROM=$W/freebsd-dl/bootonly.iso CHARON_ADDON_KEYS="40:spc"
FLAGS= WAIT=200 run DebianNetinst CHARON_ADDON_NOBOOT=1 CHARON_MEM=2048 CHARON_CDROM=$W/debian-dl/netinst.iso CHARON_ADDON_KEYS="90:down"
