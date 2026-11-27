#!/bin/sh
# Charon Hypervisor: P1 (boot area, FAT32 CHARONHV) writable, P2 (data, ext4) formatted once and mounted on /hv.
# The outcome goes to /run/charonhv/prepare.state (KEY=value lines); the manager speaks it.
S=/run/charonhv/prepare.state
mkdir -p /run/charonhv /hv
: > $S
say() { echo "$1=$2" >> $S; echo "charonhv: $1=$2" > /dev/kmsg 2>/dev/null; }
date '+%Y-%m-%d %H:%M:%S prepare start'

for m in kvm_intel kvm_amd evdev snd_hda_intel snd_intel8x0 snd_ens1371 snd_usb_audio usbhid xhci_pci ehci_pci; do modprobe -q $m 2>/dev/null; done
[ -e /dev/kvm ] && say kvm yes || say kvm no

# Sound: ALSA starts muted; every playback control of every card on and loud, then the guest soft volume is made
# (it exists only after its first use).
sleep 1
for c in /proc/asound/card[0-9]*; do
	n=${c##*card}
	amixer -c "$n" scontrols 2>/dev/null | sed -n "s/^Simple mixer control '\(.*\)',0$/\1/p" | while read -r ctl; do
		case "$ctl" in *Capture*|*Mic*|*Boost*|*Loopback*) continue;; esac
		amixer -q -c "$n" sset "$ctl" 90% unmute 2>/dev/null || amixer -q -c "$n" sset "$ctl" unmute 2>/dev/null
	done
done
if [ -e /proc/asound/card0 ]; then
	say sound yes
	dd if=/dev/zero bs=19200 count=1 2>/dev/null | aplay -q -D charonguest -f S16_LE -c 2 -r 48000 2>/dev/null
	amixer -q -c 0 sset 'Charon Guest' 100% 2>/dev/null
	# the per-machine mute switches (6q/(a)) exist only after a first use too; all on
	for i in 1 2 3 4 5 6 7 8; do
		dd if=/dev/zero bs=4800 count=1 2>/dev/null | aplay -q -D charonguest$i -f S16_LE -c 2 -r 48000 2>/dev/null
		amixer -q -c 0 cset "name=Charon Machine $i" 1 2>/dev/null
	done
else
	say sound no
fi

# Network: every wired interface asks DHCP in the background (downloads of free systems, NVDA, ISO files).
# The loopback first: the guests' filtering proxies listen on 127.0.0.1 only (measured: lo was down, bind failed).
ip link set lo up 2>/dev/null
for n in /sys/class/net/*; do
	i=${n##*/}
	[ "$i" = lo ] && continue
	[ -d "$n/wireless" ] && continue
	ip link set "$i" up 2>/dev/null
	udhcpc -b -i "$i" -p "/run/udhcpc.$i.pid" -q >/dev/null 2>&1 &
done
say network started

BOOTDEV=$(blkid -L CHARONHV 2>/dev/null)
BOOTMNT=
if [ -n "$BOOTDEV" ]; then
	BOOTMNT=$(awk -v d="$BOOTDEV" '$1==d {print $2; exit}' /proc/mounts)
	if [ -z "$BOOTMNT" ]; then
		BOOTMNT=/media/charonhv-boot; mkdir -p $BOOTMNT
		mount -t vfat -o rw,noatime,utf8,shortname=mixed "$BOOTDEV" $BOOTMNT || BOOTMNT=
	else
		mount -o remount,rw "$BOOTMNT" || say boot readonly
	fi
fi
if [ -z "$BOOTMNT" ]; then say boot missing; else say boot "$BOOTMNT"; ln -sfn "$BOOTMNT" /run/charonhv/boot; fi
# Test machines only: the file charonhv/test-serial on the boot area (never written by Charon) gives a sign-in prompt on
# the first serial port, for the test harness.
# one serial login only: a restart of the service (rc-service charonhv restart) must not start a second getty loop
# (measured r410 session 3: two gettys split the typed characters between them)
if [ -f "$BOOTMNT/charonhv/test-serial" ] && ! grep -q ttyS0 /proc/cmdline && [ ! -e /run/charonhv/serial-login ]; then
	: > /run/charonhv/serial-login     # a marker, not pgrep: while someone is logged in no getty process exists
	( while :; do /sbin/getty -L 115200 ttyS0 vt100; sleep 1; done ) >/dev/null 2>&1 &
fi

CFG=$BOOTMNT/charonhv/charonhv.cfg
GUID=
[ -f "$CFG" ] && GUID=$(sed -n 's/^data=//p' "$CFG" | tr -d '\r' | tr 'A-Z' 'a-z')
# 6m: the computer name - in a multi-boot plan Charon writes the plan's shared name; alone: charon-hypervisor
HOST=
[ -f "$CFG" ] && HOST=$(sed -n 's/^hostname=//p' "$CFG" | tr -d '\r' | tr 'A-Z' 'a-z' | tr -cd 'a-z0-9-' | cut -c1-63)
[ -n "$HOST" ] || HOST=charon-hypervisor
echo "$HOST" > /etc/hostname
hostname "$HOST"
DATADEV=
[ -n "$GUID" ] && DATADEV=$(blkid -t PARTUUID="$GUID" -o device 2>/dev/null | head -n 1)
if [ -z "$DATADEV" ]; then
	DATADEV=$(blkid -L CHARONHVDATA 2>/dev/null)
fi
if [ -z "$DATADEV" ]; then
	say data missing
	mount -t tmpfs -o size=50% tmpfs /hv
else
	if [ -z "$(blkid -p -o value -s TYPE "$DATADEV" 2>/dev/null)" ]; then
		PU=$(blkid -p -o value -s PART_ENTRY_UUID "$DATADEV" 2>/dev/null | tr 'A-Z' 'a-z')
		if [ -n "$GUID" ] && [ "$PU" = "$GUID" ]; then
			say data formatting
			if mkfs.ext4 -q -F -L CHARONHVDATA "$DATADEV"; then say formatted yes; else say formatted failed; fi
		else
			say data unknown
		fi
	fi
	# already mounted (a restart of the service): keep it - a failed second mount must not put an empty RAM
	# file system over the data partition (measured r410 session 3: the TVox build went into RAM and was lost)
	if grep -q " /hv ext4 " /proc/mounts; then say data "$DATADEV"
	elif mount -t ext4 -o noatime "$DATADEV" /hv; then say data "$DATADEV"; else say data mountfailed; mount -t tmpfs -o size=50% tmpfs /hv; fi
fi
mkdir -p /hv/machines /hv/images /hv/shared /hv/log
date '+%Y-%m-%d %H:%M:%S prepare end'
