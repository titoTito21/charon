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
# r409, Arch: always "pacman -Syu PACKAGES" - the form the Arch Wiki gives for installing
# ("System maintenance", "Partial upgrades are unsupported"): packages from fresh lists on a
# system that is not brought up to date with them may not run. The first call upgrades the
# image (a few weeks old at most), the later ones find nothing left to upgrade.
pkg() {
  a=$1; b=$2; shift 2
  [ $# -gt 0 ] || return 0
  for try in 1 2 3; do
    case $pm in
      apt) charon_apt $a $b install -y "$@" && return 0;;
      pacman) charon_pacman $a $b -Syu --needed --disable-download-timeout "$@" && return 0;;
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
    pacman)
      # the keyring first: made when the image has none yet, and brought up to date before
      # anything else, or packages signed with newer keys are refused (Arch Wiki, "pacman/Package signing")
      if [ ! -s /etc/pacman.d/gnupg/pubring.gpg ] && [ ! -s /etc/pacman.d/gnupg/pubring.kbx ]; then { pacman-key --init && pacman-key --populate archlinux; } >>$CHARON_LOG 2>&1; fi
      pacman -Syy --noconfirm --disable-download-timeout >>$CHARON_LOG 2>&1 && pacman -S --needed --noconfirm --disable-download-timeout archlinux-keyring >>$CHARON_LOG 2>&1;;
    *) true;;
  esac
  r=$?; charon_busy_stop; return $r
}
if [ "$1" = done ]; then
  # r409: this first start is a step of a multi-system plan - Charon is told how it ended (a file
  # on the EFI partition) and the computer restarts into Charon, which goes on with the plan.
  if sh $S/install-charon.sh plan-active 2>/dev/null; then
    if [ -f $S/firstboot.failed ]; then
      sh $S/install-charon.sh plan-failed "the first start could not finish: $(tr '\n' ' ' < $S/firstboot.problem 2>/dev/null)" $CHARON_LOG >> $CHARON_LOG 2>&1
      charon_tone 300 200 0 60 220 300
      charon_say "pierwsze uruchomienie się nie udało. Komputer uruchomi się ponownie i Charon powie, co dalej z planem." "the first start did not succeed. The computer restarts now and Charon says how the plan goes on."
    else
      [ -f $S/firstboot.desktop ] && ! grep -q "^[^ ]* [^ ]* [^ ]* console " $S/firstboot.args 2>/dev/null && systemctl set-default graphical.target >/dev/null 2>&1
      sh $S/install-charon.sh plan-done >> $CHARON_LOG 2>&1
      charon_tone -p 100; charon_tone 784 110 0 30 1175 170
      charon_say "ten system jest gotowy. Komputer uruchomi się ponownie i Charon zajmie się następnym krokiem planu." "this system is ready. The computer restarts now and Charon goes on with the next step of the plan."
    fi
    sleep 3; systemctl reboot; exit 0
  fi
  [ -f $S/firstboot.failed ] && exit 0
  charon_tone -p 100; charon_tone 784 110 0 30 1175 170
  sleep 2
  # r409: the first start upgraded the kernel (Arch): the modules of the running one are gone,
  # nothing more can be loaded (a USB sound card, Speakup, a file system). One restart puts it right.
  if [ ! -d /lib/modules/$(uname -r) ] && [ ! -f $S/firstboot.restarted ]; then
    : > $S/firstboot.restarted
    [ -f $S/firstboot.desktop ] && ! grep -q "^[^ ]* [^ ]* [^ ]* console " $S/firstboot.args 2>/dev/null && systemctl set-default graphical.target >/dev/null 2>&1
    charon_say "system gotowy. Zaktualizowano jądro, więc komputer uruchomi się teraz jeszcze raz." "the system is ready. The kernel was updated, so the computer restarts once more now."
    sleep 3; systemctl reboot; exit 0
  fi
  charon_say "system gotowy. Zaloguj się." "the system is ready. Log in."
  # The desktop starts only now, when everything it needs is in place.
  if [ -f $S/firstboot.desktop ] && grep -q "^[^ ]* [^ ]* [^ ]* console " $S/firstboot.args 2>/dev/null; then
    charon_say "Pulpit uruchomisz po zalogowaniu poleceniem startx." "After signing in, the command startx starts the desktop."
  elif [ -f $S/firstboot.desktop ]; then systemctl set-default graphical.target >/dev/null 2>&1
    # r409: "start", not "isolate": isolating stops every unit the target does not pull in, and on
    # Debian that is espeakup (wanted by sound.target only) - Speakup fell silent the moment the
    # sign-in screen came up, until the next start.
    systemctl start graphical.target --no-block >/dev/null 2>&1; fi
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
# r409: a kernel upgrade during this start (Arch, below) removes the running kernel's modules;
# what this start still needs is loaded while it is there.
if [ $pm = pacman ]; then
  for m in vfat nls_cp437 nls_ascii nls_iso8859_1 nls_utf8 uinput snd_seq loop; do modprobe $m 2>/dev/null; done
  case ",$L," in *,speakup,*) modprobe speakup_soft 2>/dev/null || echo "charon: this kernel has no speakup_soft module" >> $CHARON_LOG;; esac
fi
# r409: the network before anything is downloaded. The connection chosen in Charon (Wi-Fi or
# a static address: network.conf, written by Charon's installer) goes into this system's own
# network manager; then the network is waited for - five minutes, a low double tone every ten
# seconds, a console line and, when a synthesizer is already there, a spoken sentence - instead
# of letting every download fail at once on a machine whose network comes up slowly.
if [ -f $S/network.sh ]; then
  [ -f $S/network.conf ] && sh $S/network.sh apply >> $CHARON_LOG 2>&1
  # the secret does not stay on the EFI partition, which every system on the computer can read
  for e in /boot/efi /efi /boot /run/charon-esp; do [ -f $e/EFI/HAK/network.conf ] && rm -f $e/EFI/HAK/network.conf; done
  if ! sh $S/network.sh wait 6; then
    echo "charon: no network yet; waiting up to five minutes" >> $CHARON_LOG
    charon_say "nie ma jeszcze sieci. Czekam do pięciu minut. Podłącz kabel sieciowy albo sprawdź Wi-Fi." "there is no network yet. Waiting up to five minutes. Connect a network cable or check the Wi-Fi."
    n=0; netok=no
    while [ $n -lt 30 ]; do
      charon_tone 220 80 0 60 220 80
      if sh $S/network.sh wait 8; then netok=yes; break; fi
      n=$((n+1))
    done
    if [ $netok = yes ]; then echo "charon: the network is there" >> $CHARON_LOG; charon_tone 784 90 0 30 1175 130
    else
      echo "charon: still no network after five minutes; the installation is tried anyway and again at the next start" >> $CHARON_LOG
      charon_say "nadal nie ma sieci. Instalacja mowy zostanie dokończona przy następnym uruchomieniu z siecią." "still no network. The speech installation will be finished at the next start with a network."
    fi
  fi
fi
refresh || echo "charon: the package lists could not be refreshed" >> $CHARON_LOG
charon_beep 4
# --- Charon in the firmware: the reader add-on and the boot manager entry ---
case $pm in apt) base="efibootmgr"; command -v setupcon >/dev/null 2>&1 || base="$base console-setup";; *) base="efibootmgr";; esac
pkg 4 10 $base || failed="$failed efibootmgr"
sh $S/install-charon.sh >> $CHARON_LOG 2>&1
# the firmware may not load the reader add-on at all (VMware): then the reader comes with Charon's boot manager
sh $S/install-charon.sh plan-active 2>/dev/null && DUAL=plan          # r409: install-charon.sh itself keeps Charon first during a plan
[ "$DUAL" = yes ] || [ "$DUAL" = plan ] || sh $S/install-charon.sh check >> $CHARON_LOG 2>&1
if [ "$DUAL" = yes ]; then
  c=$(efibootmgr | sed -n 's/^Boot\([0-9A-Fa-f]\{4\}\)\*\{0,1\} Charon.*/\1/p' | head -n1); o=$(efibootmgr | sed -n 's/^BootOrder: //p')
  [ -n "$c" ] && efibootmgr --bootorder "$c,$(echo $o | sed "s/$c,\{0,1\}//;s/,$//")" >/dev/null 2>&1
fi
# --- speech first ---
top=92; [ -n "$desktops" ] && top=30
if [ $speech = yes ]; then
  sp="espeak-ng alsa-utils"
  # (Fedora packages no espeakup: one name that does not exist would cancel the whole step)
  case ",$L," in *,speakup,*) [ $pm = dnf ] || sp="$sp espeakup";; esac
  if [ $pm = apt ]; then
    # Ubuntu's cloud kernel keeps speakup and the sound drivers in linux-modules-extra.
    k=$(uname -r)
    find /lib/modules/$k -name 'snd-hda-intel*' 2>/dev/null | grep -q . || { apt-cache show linux-modules-extra-$k >/dev/null 2>&1 && sp="$sp linux-modules-extra-$k"; }
  fi
  if [ $pm = dnf ]; then
    # r409: Fedora's cloud image has kernel-core and kernel-modules-core only: no sound card
    # driver at all (snd_hda_intel is in kernel-modules), so no /dev/snd and no speech.
    k=$(uname -r)
    find /lib/modules/$k -name 'snd-hda-intel*' 2>/dev/null | grep -q . || sp="$sp kernel-modules-$k"
  fi
  pkg 10 $top $sp || failed="$failed speech"
  if [ $pm = dnf ] && ! ls /dev/snd/pcmC* >/dev/null 2>&1; then modprobe snd_hda_intel 2>/dev/null; modprobe snd_intel8x0 2>/dev/null; modprobe snd_ens1371 2>/dev/null; modprobe snd_usb_audio 2>/dev/null; fi
  udevadm trigger --action=add --subsystem-match=pci >/dev/null 2>&1; udevadm settle -t 10 >/dev/null 2>&1
  # The console readers now; the desktop part again after the desktop is in.
  CHARON_QUIET=1 CHARON_DESKTOP_LATER=${desktops:+1} CHARON_P0=$top CHARON_P1=$top sh $S/speech.sh "$C" none "$L" "$M" >> $S/speech.log 2>&1
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
  CHARON_QUIET=1 CHARON_P0=92 CHARON_P1=97 sh $S/speech.sh "$C" "$G" "$L" "$M" >> $S/speech.log 2>&1
fi
# r409: what speech.sh could not install or set up is said aloud (it is also shown at every
# sign-in: /etc/profile.d/charon-problems.sh), not left in a log nobody reads.
if [ -s $S/speech.problems ]; then
  n=$(wc -l < $S/speech.problems)
  echo "charon: speech.sh reported $n problem(s): $(tr '\n' ';' < $S/speech.problems)" >> $CHARON_LOG
  charon_tone 300 200 0 60 220 300
  charon_say "uwaga, problemów z instalacją czytników ekranu: $n. Pierwszy: $(head -n1 $S/speech.problems). Pełna lista pojawi się po zalogowaniu w konsoli." \
             "note, problems while installing the screen readers: $n. The first: $(head -n1 $S/speech.problems). The full list is shown after signing in on a console."
fi
charon_beep 98
# r408: the packages installed above may have registered the system's boot loader again and so
# rewritten the boot order. It is looked at once more at the next start (install-charon.sh
# check, from charon-reader-check.service): the system first, Charon second.
[ "$DUAL" = yes ] || [ "$DUAL" = plan ] || : > $S/order-check
# --- what could not be done: again at the next start with the network ---
if [ -n "$failed" ]; then
  echo "charon: not finished:$failed" >> $CHARON_LOG
  : > $S/firstboot.failed
  echo "not installed:$failed" > $S/firstboot.problem          # r409: said by Charon when this start is a step of a plan
  printf '[Unit]\nDescription=Charon: finish the first start (packages that could not be downloaded)\nWants=network-online.target\nAfter=network-online.target\nConditionPathExists=/var/lib/charon/firstboot.failed\n\n[Service]\nType=oneshot\nExecStart=/bin/sh -c "/bin/sh /var/lib/charon/firstboot.sh setup $$(cat /var/lib/charon/firstboot.args) && /bin/sh /var/lib/charon/firstboot.sh done"\nTimeoutStartSec=7200\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-firstboot.service
  systemctl enable charon-firstboot.service >/dev/null 2>&1
  charon_tone 300 200 0 60 220 300
  charon_say "nie udało się pobrać wszystkiego, pewnie brakuje sieci. Spróbuję ponownie przy następnym starcie." \
             "not everything could be downloaded, probably the network is missing. I will try again at the next start."
else
  systemctl disable charon-firstboot.service >/dev/null 2>&1; rm -f /etc/systemd/system/charon-firstboot.service
fi
exit 0
