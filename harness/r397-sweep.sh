#!/bin/sh
# r397: every screen of every boot manager read by Charon's reader:
# menus, entry editors, command lines/shells, option pages, help.
E=${1:-charon-win-r397c/CHARON.EFI}; W=$PWD; ONLY=$2
run(){ n=$1; shift; case " $ONLY " in "  "|*" $n "*) ;; *) return;; esac; rm -rf r396-$n; env "$@" 2>&1 | grep -av "pkg_resources\|declare_namespace\|UserWarning"; }
P="timeout 900 python3.15 charon-r396-pick.py $E"
# GRUB: command line help + completion, editor
run sgrubc  WAIT=3 KEYS="c h,e,l,p,ret l,s,spc,tab l,s,spc,minus,l,ret ctrl+a ctrl+e esc" $P sgrubc GRUB
run sgrube  WAIT=3 KEYS="e down down end x backspace up esc" $P sgrube GRUB
# systemd-boot: help line, status page, editor, default, timeout
run ssd     WAIT=8 KEYS="h p esc e end x backspace esc d t shift+t" $P ssd systemd-boot --winboot --sdboot
# Limine: blank entry editor, typing, help line
run slim    WAIT=8 KEYS="b p,r,o backspace esc e f10" $P slim Limine --winboot --limine
# rEFInd: options submenu, line editor, tools pages
run sref    WAIT=12 KEYS="f2 down up f2 end a backspace esc esc" $P sref rEFInd --refind
# Clover: help, options menu, entry submenu
run sclo    WAIT=60 KEYS="f1 esc o down down ret esc esc spc down esc" $P sclo Clover --winboot --clover
# Windows Boot Manager (legacy menu): edit boot options, tools, memory diagnostic options
run swed    CHARON_WINBOOT_BCD=bcd-edit/BCD WAIT=20 KEYS="f10 spc,a backspace esc f8 down esc" $P swed "(Windows" --winboot
run swmd    CHARON_WINBOOT_BCD=bcd-menu/BCD WAIT=20 STEP=6 KEYS="tab ret f1 tab down tab esc" $P swmd "(Windows" --winboot
# UEFI Shell: help, paged output, line edit
run sshell  WAIT=12 KEYS="h,e,l,p,spc,minus,b,ret spc q h,e,l,p,spc,e,d,i,t,ret c,l,s,ret" $P sshell "Shell"
# Syslinux: edit command line, help
run ssys    CHARON_USB=$W/app-sticks/syslinux.img WAIT=10 KEYS="tab a backspace esc f1 esc" $P ssys "Syslinux|USB" --usb
# iPXE: command line and config UI
run sipxe   CHARON_USB=$W/app-sticks/ipxe.img BURST="ctrl+b ctrl+b ctrl+b" WAIT=15 KEYS="h,e,l,p,ret c,o,n,f,i,g,ret down esc" $P sipxe "iPXE|USB" --usb
# FreeBSD loader: options menu, loader prompt
run sfbsd   CHARON_CDROM=$W/freebsd-dl/bootonly.iso CHARON_MEM=2048 BURST="spc" WAIT=25 KEYS="7 backspace 3 shift+slash,ret" $P sfbsd "FreeBSD|CD|DVD|Płyta"
# Haiku loader: safe mode options with check boxes
run shaiku  CHARON_CDROM=${HAIKU_ISO:?set HAIKU_ISO to a Haiku R1beta6 x86_64 ISO} CHARON_MEM=2048 BURST="$(printf 'spc %.0s' $(seq 40))" WAIT=4 KEYS="down ret down ret esc" $P shaiku "Haiku|CD|DVD|Płyta"
# OpenBSD boot> prompt
run sobsd   CHARON_CDROM=$W/openbsd-dl/cd.iso CHARON_MEM=2048 BURST="spc" WAIT=8 KEYS="h,e,l,p,ret" $P sobsd "OpenBSD|CD|DVD|Płyta"
