#!/bin/sh
# Written by Charon. Registers, on the EFI system partition of this system:
#  - the screen reader add-on as a UEFI driver option (Driver####, "Charon voice")
#  - the Charon boot manager as a boot entry ("Charon"), second in the boot order:
#    right after the system's own entry, before everything else.
esp=${CHARON_ESP:-/boot/efi}          # CHARON_ESP, CHARON_STATE: for the host tests only
state=${CHARON_STATE:-/var/lib/charon}
# The EFI partition is not at /boot/efi everywhere (/efi, /boot; the caller may also have
# mounted it at /run/charon-esp).
if [ ! -f $esp/EFI/HAK/CHARONVOICE.EFI ]; then
  for e in /efi /boot /run/charon-esp; do [ -f $e/EFI/HAK/CHARONVOICE.EFI ] && { esp=$e; break; }; done
fi
# Not mounted at all (a ready image whose fstab does not name it): mounted here, for registering only.
if [ "$1" != check ] && [ ! -f $esp/EFI/HAK/CHARONVOICE.EFI ] && command -v blkid >/dev/null 2>&1; then
  mkdir -p /run/charon-esp
  for d in $(blkid -t TYPE=vfat -o device 2>/dev/null); do
    mount "$d" /run/charon-esp 2>/dev/null || continue
    [ -f /run/charon-esp/EFI/HAK/CHARONVOICE.EFI ] && { esp=/run/charon-esp; break; }
    umount /run/charon-esp 2>/dev/null
  done
fi
[ -f $esp/EFI/HAK/CHARONVOICE.EFI ] || exit 0
# The numbers of the boot entries named "Charon" that start HAKBOOT.EFI (efibootmgr 18 prints
# the path after a tab, older versions the label alone).
charon_entries() { efibootmgr 2>/dev/null | sed -n 's/^Boot\([0-9A-Fa-f]\{4\}\)\*\{0,1\} Charon$/\1/p;s/^Boot\([0-9A-Fa-f]\{4\}\)\*\{0,1\} Charon[[:space:]].*HAKBOOT.*/\1/p' | tr abcdef ABCDEF; }
# charon_second ORDER NUMBER: the boot order with entry NUMBER second. The first other entry
# (the system's own) stays first, the rest keep their order after Charon, each entry once.
charon_second() {
  out=; i=0
  for n in $(echo "$1" | tr ',abcdef' ' ABCDEF'); do
    case ",$2,$out," in *",$n,"*) continue;; esac
    out="${out:+$out,}$n"; i=$((i+1))
    [ $i = 1 ] && out="$out,$2"
  done
  [ -n "$out" ] || out=$2
  echo "$out"
}
# r409, a multi-system plan (several systems installed one after another). While a plan runs,
# Charon keeps its state in EFI/HAK/plan on the EFI partition and must stay FIRST in the boot
# order: it starts each installer, and each restart has to come back to it.
#   EFI/HAK/plan/plan.txt   the plan is running
#   EFI/HAK/plan/current    the number of the step Charon started last (digits only)
#   EFI/HAK/plan/stepN.done / stepN.failed   written here, read by Charon at its next start
#   EFI/HAK/first-entry     after the plan: the label of the entry that must stay first
#                           ("CharonDOS", or "Charon"); looked at at every start, so that a
#                           later update of a boot loader does not take the first place for good
plan_active() { [ -f $esp/EFI/HAK/plan/plan.txt ]; }
# entry_of LABEL: the number of the boot entry with exactly that label.
entry_of() { efibootmgr 2>/dev/null | sed -n "s/^Boot\\([0-9A-Fa-f]\\{4\\}\\)\\*\\{0,1\\} $1\$/\\1/p;s/^Boot\\([0-9A-Fa-f]\\{4\\}\\)\\*\\{0,1\\} $1[[:space:]].*/\\1/p" | tr abcdef ABCDEF | head -n1; }
# put_first ORDER NUMBER: the boot order with entry NUMBER first, the others as they were.
put_first() {
  out=$2
  for n in $(echo "$1" | tr ',abcdef' ' ABCDEF'); do
    case ",$out," in *",$n,"*) continue;; esac
    out="$out,$n"
  done
  echo "$out"
}
# plan-done [TEXT] / plan-failed TEXT [LOG]: this system reports the end of its step to Charon.
# The step number is argument plan-step=N when given, else the file "current". One line of
# result; on failure the last lines of LOG follow. Nothing happens outside a plan.
if [ "$1" = plan-done ] || [ "$1" = plan-failed ]; then
  kind=${1#plan-}; shift
  step=; case "$1" in plan-step=*) step=${1#plan-step=}; shift;; esac
  plan_active || exit 0
  [ -n "$step" ] || step=$(tr -cd 0-9 < $esp/EFI/HAK/plan/current 2>/dev/null)
  [ -n "$step" ] || { echo "charon: plan: the number of the step is not known; no marker written"; exit 1; }
  marker=$esp/EFI/HAK/plan/step$step.$kind
  rm -f $esp/EFI/HAK/plan/step$step.done $esp/EFI/HAK/plan/step$step.failed
  { printf '%s\r\n' "${1:-ok}" && { if [ $kind = failed ] && [ -n "$2" ] && [ -f "$2" ]; then tail -n 12 "$2" | tr -d '\r' | sed 's/$/\r/'; fi; }; } > $marker.new 2>/dev/null \
    && mv $marker.new $marker && { sync; echo "charon: plan: step $step reported as $kind"; exit 0; }
  echo "charon: plan: the marker $marker could not be written"; exit 1
fi
if [ "$1" = plan-active ]; then plan_active; exit $?; fi
# r406: `charon-register check`, at every start. Some firmware (VMware's, some computers')
# never loads driver options, so the reader add-on registered below would never speak.
# The add-on leaves a mark when firmware did load it: the variable CharonVoiceLoaded. When a
# start that had the driver option shows no mark, the reader is started the other way: the
# Charon boot manager goes first in the boot order with the file reader-only, which makes it
# go straight on to this system, silently, with its screen reader left running.
if [ "$1" = check ]; then
  [ -d /sys/firmware/efi ] || exit 0
  ls /sys/firmware/efi/efivars/Boot* >/dev/null 2>&1 || mount -t efivarfs efivarfs /sys/firmware/efi/efivars 2>/dev/null
  command -v efibootmgr >/dev/null || exit 0
  ls $esp >/dev/null 2>&1
  [ -f $esp/EFI/HAK/HAKBOOT.EFI ] || exit 0
  c=$(charon_entries | head -n1)
  o=$(efibootmgr | sed -n 's/^BootOrder: //p' | tr abcdef ABCDEF)
  [ -n "$c" ] || exit 0
  # r409: a plan is running - Charon first, nothing else is decided here (no reader-only: the
  # reader comes with Charon itself while it is first).
  if plan_active; then
    rm -f $state/order-check
    case "$o" in "$c"|"$c",*) ;; *)
      n=$(put_first "$o" "$c")
      efibootmgr --bootorder "$n" >/dev/null && echo "charon: plan: Charon put back first in the boot order ($n)";;
    esac
    exit 0
  fi
  # r409: after a plan the entry named in first-entry stays first, Charon right behind it.
  if [ -s $esp/EFI/HAK/first-entry ]; then
    label=$(head -n1 $esp/EFI/HAK/first-entry | tr -d '\r\n')
    f=$(entry_of "$label")
    if [ -n "$f" ]; then
      rm -f $state/order-check
      n=$(put_first "$(put_first "$o" "$c")" "$f")
      if [ "$n" != "$o" ] && efibootmgr --bootorder "$n" >/dev/null; then echo "charon: boot order corrected: $label first, as the plan left it ($n)"; fi
      exit 0
    fi
  fi
  # Once, at the first start after registering: an installer, or a grub-install run after this
  # script, may have rewritten the boot order. The system first, Charon second - unless Charon
  # was put first on purpose (beside other systems; the reader started through the manager).
  if [ -f $state/order-check ]; then
    rm -f $state/order-check
    case "$o" in "$c"|"$c",*) ;; *)
      n=$(charon_second "$o" "$c")
      if [ "$n" != "$o" ] && efibootmgr --bootorder "$n" >/dev/null; then o=$n; echo "charon: boot order corrected: the system first, Charon second ($n)"; fi;;
    esac
  fi
  [ -f $esp/EFI/HAK/reader-only ] && exit 0
  efibootmgr --driver 2>/dev/null | grep -q 'Charon voice' || exit 0
  case "$o" in "$c"|"$c",*) exit 0;; esac          # Charon's menu is first already: it reads the screen itself
  if ls /sys/firmware/efi/efivars/CharonVoiceLoaded-7a3c5e21-4b9d-4f1e-a652-1c8e930d27b5 >/dev/null 2>&1; then rm -f $state/driver-seen; exit 0; fi
  boot=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)
  sure=no
  # Known not to load driver options: no need to wait for a second start.
  grep -qi 'vmware' /sys/class/dmi/id/sys_vendor 2>/dev/null && sure=yes
  # Otherwise only a start AFTER the one that first saw the option counts (the option may
  # have been written during this very start, when firmware had no chance to use it).
  if [ $sure = no ]; then
    if [ ! -f $state/driver-seen ]; then echo "$boot" > $state/driver-seen; exit 0; fi
    [ "$(cat $state/driver-seen)" = "$boot" ] && exit 0
  fi
  : > $esp/EFI/HAK/reader-only
  efibootmgr --bootorder "$c,$(echo $o | sed "s/$c,\{0,1\}//;s/,$//")" >/dev/null
  rm -f $state/driver-seen
  echo "charon: this firmware does not load the reader add-on; the reader now starts with the Charon boot manager, first in the boot order, which goes straight on to the system"
  exit 0
fi
mkdir -p $state; : > $state/pending
# Charon's own installers stay in the system, in case it must be set up again:
# sudo sh /var/lib/charon/installer/zainstaluj-charona.sh
if [ -d $esp/EFI/HAK/addon ] && [ ! -d $state/installer ]; then
  mkdir -p $state/installer/EFI && cp $esp/EFI/HAK/addon/* $state/installer/ 2>/dev/null
  ln -s $esp/EFI/HAK $state/installer/EFI/HAK
fi
if [ ! -f /etc/systemd/system/charon-register.service ] && [ -d /etc/systemd/system ]; then
  cp "$0" /usr/local/sbin/charon-register 2>/dev/null && chmod 755 /usr/local/sbin/charon-register
  printf '[Unit]\nDescription=Register Charon in UEFI (once)\nConditionPathExists=/var/lib/charon/pending\nAfter=local-fs.target\n\n[Service]\nType=oneshot\nExecStart=/usr/local/sbin/charon-register\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-register.service
  mkdir -p /etc/systemd/system/multi-user.target.wants
  ln -sf /etc/systemd/system/charon-register.service /etc/systemd/system/multi-user.target.wants/charon-register.service
  # r406: at every start, is the reader add-on really loaded by the firmware? (see "check" above)
  printf '[Unit]\nDescription=Charon: check that the firmware loads the screen reader add-on\nAfter=local-fs.target charon-register.service\n\n[Service]\nType=oneshot\nExecStart=/usr/local/sbin/charon-register check\nStandardOutput=append:/var/lib/charon/reader-check.log\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-reader-check.service
  ln -sf /etc/systemd/system/charon-reader-check.service /etc/systemd/system/multi-user.target.wants/charon-reader-check.service
fi
# Firmware code cannot run while the system shuts down, so the farewell
# (falling tones, the reverse of Charon's start sound, and a sentence)
# is played by the system itself, just before sound is stopped.
if [ -f $state/goodbye.wav ] && [ -d /etc/systemd/system ] && [ ! -f /etc/systemd/system/charon-goodbye.service ]; then
  printf '#!/bin/sh\ncommand -v aplay >/dev/null && aplay -q /var/lib/charon/goodbye.wav 2>/dev/null\nl=$(. /etc/default/locale 2>/dev/null; echo $LANG)\nif command -v espeak-ng >/dev/null; then case "$l" in pl*) espeak-ng -v pl "Wy\305\202\304\205czanie Charona" 2>/dev/null;; *) espeak-ng "Charon is turning off" 2>/dev/null;; esac; fi\nexit 0\n' > /usr/local/sbin/charon-goodbye
  chmod 755 /usr/local/sbin/charon-goodbye
  printf '[Unit]\nDescription=Charon farewell sound at shutdown\nAfter=sound.target alsa-restore.service espeakup.service\n\n[Service]\nType=oneshot\nRemainAfterExit=yes\nExecStart=/bin/true\nExecStop=/usr/local/sbin/charon-goodbye\nTimeoutStopSec=10\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-goodbye.service
  ln -sf /etc/systemd/system/charon-goodbye.service /etc/systemd/system/multi-user.target.wants/charon-goodbye.service
fi
# Where the firmware cannot be reached (an installer's environment without its variables),
# the file "pending" stays and charon-register.service does the work at the first start.
later() { echo "charon: $1; Charon is registered at the first start of the installed system instead"; exit 1; }
[ -d /sys/firmware/efi ] || later "this system was not started by UEFI firmware (no /sys/firmware/efi)"
ls /sys/firmware/efi/efivars/Boot* >/dev/null 2>&1 || mount -t efivarfs efivarfs /sys/firmware/efi/efivars 2>/dev/null
[ -n "$(ls /sys/firmware/efi/efivars 2>/dev/null | head -n1)" ] || later "the firmware's variables cannot be read here (efivarfs is not mounted)"
command -v efibootmgr >/dev/null || later "efibootmgr is not installed"
# r402: an EFI partition mounted on demand (Arch: systemd automount) shows as "systemd-1" until it is used.
ls $esp >/dev/null 2>&1
dev=$(findmnt -n -t vfat -o SOURCE $esp | tail -n1); [ -n "$dev" ] || later "the device of the EFI partition at $esp was not found"
# Partition number and whole disk from sysfs: right for sda1, nvme0n1p1 and mmcblk0p1 alike.
part=$(cat /sys/class/block/${dev#/dev/}/partition 2>/dev/null); [ -n "$part" ] || later "$dev is not a partition of a disk"
disk=/dev/$(basename $(readlink -f /sys/class/block/${dev#/dev/}/..))
# The reader add-on: a driver option (Driver#### in DriverOrder), once.
for n in $(efibootmgr --driver 2>/dev/null | sed -n 's/^Driver\([0-9A-Fa-f]\{4\}\)\*\{0,1\} Charon voice.*/\1/p'); do efibootmgr --driver --bootnum $n --delete-bootnum >/dev/null 2>&1; done
efibootmgr --driver --create --disk "$disk" --part "$part" --loader '\EFI\HAK\CHARONVOICE.EFI' --label 'Charon voice' >/dev/null || later "the firmware did not take the driver option for the reader add-on (variables read-only?)"
echo "charon: reader add-on registered as a firmware driver option ($disk, partition $part)"
if [ -f $esp/EFI/HAK/HAKBOOT.EFI ]; then
  before=$(efibootmgr | sed -n 's/^BootOrder: //p' | tr abcdef ABCDEF)
  old=$(charon_entries)
  # Charon first already (put there on purpose: beside other systems) stays first.
  wasfirst=no; for n in $old; do case "$before" in "$n"|"$n",*) wasfirst=yes;; esac; done
  for n in $old; do efibootmgr --bootnum $n --delete-bootnum >/dev/null 2>&1; done
  order=$(efibootmgr | sed -n 's/^BootOrder: //p' | tr abcdef ABCDEF)
  # --create also puts the new entry first in BootOrder; the order is written again below.
  efibootmgr --create --disk "$disk" --part "$part" --loader '\EFI\HAK\HAKBOOT.EFI' --label 'Charon' >/dev/null || later "the firmware did not take the boot entry for the Charon boot manager"
  new=$(charon_entries | head -n1)
  if [ -n "$new" ]; then
    # The system's own boot manager stays first; Charon is second; the others follow.
    # r409: during a multi-system plan Charon is first whatever the installer did; after one,
    # the entry of first-entry is first and Charon follows it.
    keep=; [ -s $esp/EFI/HAK/first-entry ] && keep=$(entry_of "$(head -n1 $esp/EFI/HAK/first-entry | tr -d '\r\n')")
    if plan_active; then want=$(put_first "$order" "$new")
    elif [ -n "$keep" ]; then want=$(put_first "$(put_first "$order" "$new")" "$keep")
    elif [ $wasfirst = yes ]; then want="$new${order:+,$order}"; else want=$(charon_second "$order" "$new"); fi
    efibootmgr --bootorder "$want" >/dev/null && echo "charon: boot manager registered as Boot$new; boot order $want"
    # Checked once more at the next start: see "check" above.
    : > $state/order-check
  fi
fi
rm -f $state/pending
exit 0
