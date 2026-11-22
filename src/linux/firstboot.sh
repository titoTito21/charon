#!/bin/sh
# Written by Charon (r406): the first start of a ready system image.
#   firstboot.sh setup CONSOLE GUI READERS LOGIN DESKTOPS DUAL
#   firstboot.sh done
# CONSOLE speakup|fenrir|emacspeak|none, GUI orca|cthulhu|none, READERS the
# chosen readers with commas (or none), LOGIN graphical|console (console: a text
# login, the desktop is started with startx), DESKTOPS this system's desktop
# packages with commas (or none), DUAL yes when Charon's boot manager goes
# first (the system was put beside others).
#
# Order matters to a blind person: sound first, speech second, the long
# download of a desktop last. From the first second there are progress tones
# (progress.sh); as soon as the small speech packages are in, the system
# speaks and says what it is doing; only then the desktop is installed.
# Whatever fails for lack of a network is tried again at every start.
S=/var/lib/charon
CHARON_LOG=$S/firstboot.log
export DEBIAN_FRONTEND=noninteractive
chmod 755 $S/charon-tone 2>/dev/null
. $S/progress.sh
pm=none
command -v apt-get >/dev/null 2>&1 && pm=apt
command -v pacman >/dev/null 2>&1 && pm=pacman
command -v dnf >/dev/null 2>&1 && pm=dnf
# pkg FROM TO PACKAGES...: with progress tones over FROM..TO percent.
# A download that breaks (a mirror that stops answering) is tried again, three times in all,
# after refreshing the package lists; pacman's 10-second stall limit is switched off.
pkg() {
  a=$1; b=$2; shift 2
  [ $# -gt 0 ] || return 0
  for try in 1 2 3; do
    case $pm in
      apt) charon_apt $a $b install -y "$@" && return 0;;
      pacman) charon_pacman $a $b -S --needed --disable-download-timeout "$@" && return 0;;
      dnf) charon_busy_start; dnf install -y "$@" >>$CHARON_LOG 2>&1; r=$?; charon_busy_stop; [ $r = 0 ] && return 0;;
      *) return 1;;
    esac
    echo "charon: installing $* failed, try $try" >> $CHARON_LOG
    [ $try = 3 ] && break
    charon_tone 300 120; sleep 10; refresh
  done
  return 1
}
refresh() {
  charon_busy_start
  case $pm in
    apt) apt-get -o DPkg::Lock::Timeout=900 update >>$CHARON_LOG 2>&1;;
    pacman) pacman -Syy --noconfirm --disable-download-timeout >>$CHARON_LOG 2>&1;;
    *) true;;
  esac
  r=$?; charon_busy_stop; return $r
}
if [ "$1" = done ]; then
  [ -f $S/firstboot.failed ] && exit 0
  charon_tone -p 100; charon_tone 784 110 0 30 1175 170
  sleep 2
  charon_say "system gotowy. Zaloguj się." "the system is ready. Log in."
  # The desktop starts only now, when everything it needs is in place.
  if [ -f $S/firstboot.desktop ] && grep -q "^[^ ]* [^ ]* [^ ]* console " $S/firstboot.args 2>/dev/null; then
    charon_say "Pulpit uruchomisz po zalogowaniu poleceniem startx." "After signing in, the command startx starts the desktop."
  elif [ -f $S/firstboot.desktop ]; then systemctl set-default graphical.target >/dev/null 2>&1; systemctl isolate graphical.target --no-block >/dev/null 2>&1; fi
  exit 0
fi
[ "$1" = setup ] || exit 1
C=${2:-none}; G=${3:-none}; L=${4:-none}; M=${5:-graphical}; D=${6:-none}; DUAL=${7:-no}
echo "$C $G $L $M $D $DUAL" > $S/firstboot.args
# r408: the desktop "i3 with I38" is set up by speech.sh, which is told so in its list.
case ",$D," in *,i3-wm,*) case ",$L," in *,i38,*) ;; *) L="$L,i38";; esac;; esac
rm -f $S/firstboot.failed $S/firstboot.desktop
echo "charon: first start, console=$C desktop reader=$G readers=$L desktops=$D package manager=$pm" >> $CHARON_LOG
speech=no; [ "$C" != none ] && speech=yes; [ "$G" != none ] && speech=yes
desktops=$(echo "$D" | tr ',' ' '); [ "$D" = none ] && desktops=
failed=
# The sound card's drivers may sit in a package of their own (Ubuntu: linux-modules-extra).
ls /dev/snd/pcmC* >/dev/null 2>&1 || { modprobe snd_hda_intel 2>/dev/null; modprobe snd_intel8x0 2>/dev/null; modprobe snd_ens1371 2>/dev/null; modprobe snd_usb_audio 2>/dev/null; sleep 1; }
# Rising tones: Charon's work begins.
charon_tone 784 90 0 30 1175 130
charon_beep 0
charon_tls_check
refresh || echo "charon: the package lists could not be refreshed" >> $CHARON_LOG
charon_beep 4
# --- Charon in the firmware: the reader add-on and the boot manager entry ---
case $pm in apt) base="efibootmgr"; command -v setupcon >/dev/null 2>&1 || base="$base console-setup";; *) base="efibootmgr";; esac
pkg 4 10 $base || failed="$failed efibootmgr"
sh $S/install-charon.sh >> $CHARON_LOG 2>&1
# the firmware may not load the reader add-on at all (VMware): then the reader comes with Charon's boot manager
[ "$DUAL" = yes ] || sh $S/install-charon.sh check >> $CHARON_LOG 2>&1
if [ "$DUAL" = yes ]; then
  c=$(efibootmgr | sed -n 's/^Boot\([0-9A-Fa-f]\{4\}\)\*\{0,1\} Charon.*/\1/p' | head -n1); o=$(efibootmgr | sed -n 's/^BootOrder: //p')
  [ -n "$c" ] && efibootmgr --bootorder "$c,$(echo $o | sed "s/$c,\{0,1\}//;s/,$//")" >/dev/null 2>&1
fi
# --- speech first ---
top=92; [ -n "$desktops" ] && top=30
if [ $speech = yes ]; then
  sp="espeak-ng alsa-utils"
  case ",$L," in *,speakup,*) sp="$sp espeakup";; esac
  if [ $pm = apt ]; then
    # Ubuntu's cloud kernel keeps speakup and the sound drivers in linux-modules-extra.
    k=$(uname -r)
    find /lib/modules/$k -name 'snd-hda-intel*' 2>/dev/null | grep -q . || { apt-cache show linux-modules-extra-$k >/dev/null 2>&1 && sp="$sp linux-modules-extra-$k"; }
  fi
  pkg 10 $top $sp || failed="$failed speech"
  udevadm trigger --action=add --subsystem-match=pci >/dev/null 2>&1; udevadm settle -t 10 >/dev/null 2>&1
  # The console readers now; the desktop part again after the desktop is in.
  CHARON_DESKTOP_LATER=${desktops:+1} CHARON_P0=$top CHARON_P1=$top sh $S/speech.sh "$C" none "$L" "$M" >> $S/speech.log 2>&1
  sleep 2
  if ! command -v espeak-ng >/dev/null 2>&1; then
    echo "charon: no speech synthesizer could be installed" >> $CHARON_LOG
  elif [ -n "$desktops" ]; then
    charon_say "mowa działa. Teraz pobieram i instaluję pulpit. To potrwa od kilku do kilkudziesięciu minut. Coraz wyższe dźwięki pokazują postęp." \
               "speech works. Now the desktop is downloaded and installed. That takes from a few minutes to some dozens. The rising tones show the progress."
  else
    charon_say "mowa działa." "speech works."
  fi
fi
# --- the desktops ---
if [ -n "$desktops" ]; then
  extra=
  if [ $pm = pacman ]; then
    # Arch's desktop groups bring neither a display server nor a sound server.
    extra="xorg-server pipewire-pulse pipewire-alsa wireplumber"
  fi
  if [ "$G" != none ]; then extra="$extra orca speech-dispatcher espeak-ng"; fi
  if pkg $top 90 $desktops $extra; then : > $S/firstboot.desktop
  else
    # One desktop that failed must not take the others with it: one by one.
    ok=no
    for d in $desktops; do pkg 90 90 $d && ok=yes; done
    pkg 90 90 $extra
    if [ $ok = yes ]; then : > $S/firstboot.desktop; else failed="$failed desktop"; fi
  fi
  # Arch enables no display manager by itself.
  systemctl is-enabled display-manager.service >/dev/null 2>&1 || for d in gdm gdm3 lightdm sddm; do systemctl enable $d >/dev/null 2>&1 && break; done
  # also without any reader: the sign-in mode is set up there
  charon_beep 92
  CHARON_P0=92 CHARON_P1=97 sh $S/speech.sh "$C" "$G" "$L" "$M" >> $S/speech.log 2>&1
fi
charon_beep 98
# r408: the packages installed above may have registered the system's boot loader again and so
# rewritten the boot order. It is looked at once more at the next start (install-charon.sh
# check, from charon-reader-check.service): the system first, Charon second.
[ "$DUAL" = yes ] || : > $S/order-check
# --- what could not be done: again at the next start with the network ---
if [ -n "$failed" ]; then
  echo "charon: not finished:$failed" >> $CHARON_LOG
  : > $S/firstboot.failed
  printf '[Unit]\nDescription=Charon: finish the first start (packages that could not be downloaded)\nWants=network-online.target\nAfter=network-online.target\nConditionPathExists=/var/lib/charon/firstboot.failed\n\n[Service]\nType=oneshot\nExecStart=/bin/sh -c "/bin/sh /var/lib/charon/firstboot.sh setup $$(cat /var/lib/charon/firstboot.args) && /bin/sh /var/lib/charon/firstboot.sh done"\nTimeoutStartSec=7200\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-firstboot.service
  systemctl enable charon-firstboot.service >/dev/null 2>&1
  charon_tone 300 200 0 60 220 300
  charon_say "nie udało się pobrać wszystkiego, pewnie brakuje sieci. Spróbuję ponownie przy następnym starcie." \
             "not everything could be downloaded, probably the network is missing. I will try again at the next start."
else
  systemctl disable charon-firstboot.service >/dev/null 2>&1; rm -f /etc/systemd/system/charon-firstboot.service
fi
exit 0
