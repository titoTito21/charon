#!/bin/sh
# Written by Charon: screen readers in the installed system.
#   speech.sh CONSOLE GUI LIST LOGIN
# CONSOLE speakup|fenrir|emacspeak|none, GUI orca|cthulhu|linda|none, LIST the chosen readers with
# commas (the word i38 in it: the desktop "i3 with I38" was chosen; the word rhvoice: RHVoice is
# to be the voice), LOGIN graphical|console.
C=${1:-speakup}; G=${2:-orca}; L=${3:-$C,$G}; M=${4:-graphical}
# r409: Charon's own voice, for the installed system to start with (the user's decision):
#   CHARON_SYNTH        rhvoice | espeak   Charon speaks with RHVoice: RHVoice is the default here too
#   CHARON_VOICE        RHVoice's own name of the voice Charon speaks with (natan, magda, michal,
#                       alicja, cezary; slt, alan, bdl, clb, lyubov, evgeniy-eng ...), lower case
#   CHARON_RATE         5..20              Charon's rate setting, tenths (10 = 100 percent)
#   CHARON_PITCH        5..20              Charon's pitch setting, the same scale
#   CHARON_PUNCTUATION  none|some|most|all
# from the environment, or from /var/lib/charon/voice.conf (the same names, one NAME=value a
# line), which Charon's installer writes beside this script. None given: nothing is changed.
if [ -f /var/lib/charon/voice.conf ]; then
  while IFS='=' read -r k v; do
    case $k in CHARON_SYNTH|CHARON_VOICE|CHARON_RATE|CHARON_PITCH|CHARON_PUNCTUATION) v=$(printf '%s' "$v" | tr 'A-Z' 'a-z' | tr -cd 'a-z0-9-'); eval "[ -n \"\$$k\" ] || $k=\$v";; esac
  done < /var/lib/charon/voice.conf
fi
case "$CHARON_SYNTH" in rhvoice) case ",$L," in *,rhvoice,*) ;; *) L="$L,rhvoice";; esac;; esac
has() { case ",$L," in *",$1,"*) return 0;; esac; return 1; }
export DEBIAN_FRONTEND=noninteractive
S=/var/lib/charon
mkdir -p $S
pm=none; command -v apt-get >/dev/null 2>&1 && pm=apt; command -v pacman >/dev/null 2>&1 && pm=pacman; command -v dnf >/dev/null 2>&1 && pm=dnf
# r409: what the package managers say is kept (it went to /dev/null: nobody could tell why a
# reader was missing), and whatever could not be done is collected in speech.problems: said
# aloud at the end (a running system), shown at every sign-in until it is put right.
PLOG=$S/speech-packages.log
CHARON_LOG=${CHARON_LOG:-$PLOG}
PROBLEMS=$S/speech.problems
: > $PROBLEMS
problem() { echo "charon: PROBLEM: $*"; echo "$*" >> $PROBLEMS; }
# r406: with Charon's progress tones when its sound library is there (CHARON_P0..CHARON_P1 percent).
[ -f $S/progress.sh ] && { chmod 755 $S/charon-tone 2>/dev/null; . $S/progress.sh; }
tones=no; command -v charon_apt >/dev/null 2>&1 && tones=yes
P0=${CHARON_P0:-0}; P1=${CHARON_P1:-100}
# get PACKAGES...: all of them in one transaction (false when one of them cannot be installed).
get() {
  echo "--- $(date '+%F %T') install: $*" >> $PLOG
  case $pm in
    apt) if [ $tones = yes ]; then charon_apt $P0 $P1 install -y "$@" || { apt-get -o DPkg::Lock::Timeout=900 update >>$PLOG 2>&1; charon_apt $P0 $P1 install -y "$@"; }
         else apt-get -o DPkg::Lock::Timeout=900 install -y "$@" >>$PLOG 2>&1 || { apt-get -o DPkg::Lock::Timeout=900 update >>$PLOG 2>&1; apt-get -o DPkg::Lock::Timeout=900 install -y "$@" >>$PLOG 2>&1; }; fi;;
    # Arch: "pacman -Syu PACKAGES", never -Sy or -S alone on stale lists (Arch Wiki, "System maintenance": partial upgrades are unsupported)
    pacman) if [ $tones = yes ]; then charon_pacman $P0 $P1 -Syu --needed "$@"; else pacman -Syu --needed --noconfirm "$@" >>$PLOG 2>&1; fi;;
    dnf) dnf install -y "$@" >>$PLOG 2>&1;;
    *) false;;
  esac
}
# one PACKAGE / each PACKAGES... / all PACKAGES...: one by one, so that a name one system does
# not have cannot take the others with it; each one that fails is reported.
one() { echo "--- $(date '+%F %T') install: $1" >> $PLOG
  case $pm in apt) apt-get -o DPkg::Lock::Timeout=900 install -y "$1" >>$PLOG 2>&1;; pacman) pacman -Syu --needed --noconfirm "$1" >>$PLOG 2>&1;; dnf) dnf install -y "$1" >>$PLOG 2>&1;; *) false;; esac; }
each() { for p in "$@"; do one "$p" || problem "the package $p could not be installed"; done; }
all() { get "$@" || each "$@"; }
have() { command -v "$1" >/dev/null 2>&1; }
# r409: is this a running system, or an installer's chroot? Debian's in-target has no
# /run/systemd/system; Ubuntu's (curtin in-target) binds the installer's own /run into the
# chroot, so the folder is there and says nothing: both installers pass CHARON_CHROOT=1.
running=no; [ -d /run/systemd/system ] && [ -z "$CHARON_CHROOT" ] && running=yes
# Arch: pacman checks signatures against its keyring, which a fresh system must first make
# (Arch Wiki, "pacman/Package signing": pacman-key --init, pacman-key --populate).
archkeys() {
  [ $pm = pacman ] || return 0
  if [ ! -s /etc/pacman.d/gnupg/pubring.gpg ] && [ ! -s /etc/pacman.d/gnupg/pubring.kbx ]; then
    pacman-key --init >>$PLOG 2>&1 && pacman-key --populate archlinux >>$PLOG 2>&1 || { problem "pacman's keyring could not be initialised (pacman-key --init, --populate)"; return 1; }
  fi
}
# Arch: Fenrir, Cthulhu and i38lock come from the Stormux repository (signed; the key, the stanza
# and its place before [core] exactly as published on https://stormux.org). Arch itself has none
# of them, and neither has the AUR (Cthulhu, I38). Standing before [core], the repository also
# replaces Arch's espeak-ng, yay and python-xlib with its own builds: that is how Stormux means
# it to be used. The whole system is brought up to date with it (pacman -Syu): installing from
# fresh package lists onto an older system is what Arch calls a partial upgrade, and does not support.
stormuxok=
stormux() {
  [ $pm = pacman ] || return 1
  [ -n "$stormuxok" ] && return $stormuxok
  stormuxok=1
  if ! grep -q '^\[stormux\]' /etc/pacman.conf; then
    archkeys || return 1
    have curl || get curl
    curl -fsSL -o $S/stormux_repo.pub https://packages.stormux.org/stormux_repo.pub || { problem "the key of the Stormux repository could not be downloaded (packages.stormux.org; no network?)"; return 1; }
    pacman-key --add $S/stormux_repo.pub >>$PLOG 2>&1 && pacman-key --lsign-key 52ADA49000F1FF0456F8AEEFB4CDE1CD56EF8E82 >>$PLOG 2>&1 || { problem "the key of the Stormux repository could not be added to pacman's keyring"; return 1; }
    sed -i '/^\[core\]/i [stormux]\nSigLevel = Required DatabaseOptional\nServer = https://packages.stormux.org/$arch\n' /etc/pacman.conf
    grep -q '^\[stormux\]' /etc/pacman.conf || { problem "the Stormux repository could not be written into /etc/pacman.conf (no [core] section?)"; return 1; }
  fi
  echo "--- $(date '+%F %T') pacman -Syu with the Stormux repository" >> $PLOG
  if [ $tones = yes ]; then charon_pacman $P0 $P0 -Syu; else pacman -Syu --noconfirm >>$PLOG 2>&1; fi || { problem "the Stormux repository could not be read, or the system could not be brought up to date with it (pacman -Syu)"; return 1; }
  stormuxok=0
}
# Arch: aur NAME builds a package of the Arch User Repository. makepkg refuses to run as root
# (Arch Wiki, "Makepkg", "Arch User Repository"), so an account without a password or a shell,
# charon-build, clones the recipe and builds it; root installs what the recipe depends on (from
# the repositories; what only the AUR has is built the same way first) and then the built package.
aur() (
  [ $pm = pacman ] || exit 1
  n=${1%%[<>=]*}
  pacman -Qq "$n" >/dev/null 2>&1 && exit 0
  case " $CHARON_AUR_SEEN " in *" $n "*) exit 1;; esac
  CHARON_AUR_SEEN="$CHARON_AUR_SEEN $n"; export CHARON_AUR_SEEN
  pacman -Qq base-devel git >/dev/null 2>&1 || get base-devel git || { problem "AUR package $n: base-devel and git could not be installed"; exit 1; }
  id charon-build >/dev/null 2>&1 || useradd -r -m -d $S/aur -s /usr/bin/nologin charon-build >>$PLOG 2>&1 || { problem "AUR package $n: the build account charon-build could not be made"; exit 1; }
  mkdir -p $S/aur && chown charon-build $S/aur
  b=$S/aur/$n; rm -rf "$b"
  asuser() { runuser -u charon-build -- env HOME=$S/aur "$@"; }
  asuser git clone -q --depth 1 "https://aur.archlinux.org/$n.git" "$b" >>$PLOG 2>&1 || { problem "AUR package $n: its recipe could not be fetched (aur.archlinux.org; no network?)"; exit 1; }
  [ -f "$b/PKGBUILD" ] && [ -f "$b/.SRCINFO" ] || { problem "the AUR has no package named $n"; exit 1; }
  for d in $(sed -n 's/^[[:space:]]*depends = //p;s/^[[:space:]]*makedepends = //p' "$b/.SRCINFO"); do
    pacman -T "$d" >/dev/null 2>&1 && continue
    echo "--- $(date '+%F %T') AUR $n needs $d" >> $PLOG
    pacman -S --needed --noconfirm --asdeps "$d" >>$PLOG 2>&1 || aur "$d" || { problem "AUR package $n: $d, which it depends on, could not be installed"; exit 1; }
  done
  echo "--- $(date '+%F %T') makepkg $n" >> $PLOG
  ( cd "$b" && asuser makepkg --noconfirm --nocheck ) >>$PLOG 2>&1 || { problem "AUR package $n could not be built (makepkg; see $PLOG)"; exit 1; }
  r=1
  for f in "$b"/*.pkg.tar.*; do
    case $f in *-debug-*|*.sig) continue;; esac
    [ -f "$f" ] && pacman -U --noconfirm --needed "$f" >>$PLOG 2>&1 && r=0
  done
  [ $r = 0 ] || problem "AUR package $n was built but could not be installed"
  rm -rf "$b"
  exit $r
)
LANGV=$(sed -n 's/^LANG="\{0,1\}\([a-z][a-z]\)[_.].*/\1/p' /etc/default/locale /etc/locale.conf 2>/dev/null | head -n1); [ -n "$LANGV" ] || LANGV=en
echo "charon: $(date '+%F %T') readers console=$C desktop=$G chosen=$L login=$M language=$LANGV package manager=$pm"
echo "$G" > $S/gui-reader; echo "$C $G $L $M" > $S/speech.args
# --- r408: i3 with I38, Storm Dragon's accessible set-up of the i3 window manager ---
# https://git.stormux.org/storm/I38 (GPL 3). Here: i3 itself, what I38's generator asks for
# (its README, "Requirements") and what its default configuration uses (sounds, notifications,
# a terminal, a file manager, an editor, a browser). The configuration is written further down.
i38=no
if has i38 && [ -z "$CHARON_DESKTOP_LATER" ]; then
  i38=yes
  case $pm in
    apt)
      get i3-wm xorg xinit git dialog jq python3 python3-gi gir1.2-gtk-3.0 gir1.2-atk-1.0 python3-i3ipc rsync yad xdotool xdg-utils discount lxterminal || { i38=no; problem "I38: what it requires could not be installed (i3-wm, yad, python3-i3ipc and others; see $PLOG)"; }
      all sox xclip libnotify-bin notification-daemon python3-pyudev pcmanfm mousepad pamixer playerctl
      # i3 brings no sound server: PipeWire with its PulseAudio service (the metapackage of Debian and Ubuntu)
      have pipewire || have pulseaudio || each pipewire-audio
      have firefox || have firefox-esr || have epiphany || get firefox-esr || get epiphany-browser || problem "I38: no web browser could be installed";;
    pacman)
      get i3-wm xorg-server xorg-xinit git dialog jq python python-gobject gtk3 python-i3ipc rsync yad xdotool xdg-utils discount lxterminal || { i38=no; problem "I38: what it requires could not be installed (i3-wm, yad, python-i3ipc and others; see $PLOG)"; }
      all sox xclip libnotify notification-daemon python-pyudev pcmanfm mousepad firefox pamixer playerctl xorg-xset
      have pipewire || have pulseaudio || all pipewire-pulse pipewire-alsa wireplumber
      # I38's own screen locker is packaged by Stormux alone; without it I38 offers no lock.
      if stormux; then get i38lock || echo "charon: I38: i38lock (its screen locker) could not be installed"; fi;;
    *) i38=no; problem "I38 is not set up on this system: Charon has a package list for it on Debian, Ubuntu and Arch only";;
  esac
  # A graphical sign-in needs a display manager; one that speaks (LightDM with its GTK greeter, below).
  if [ $i38 = yes ] && [ "$M" != console ] && [ ! -e /etc/systemd/system/display-manager.service ]; then
    get lightdm lightdm-gtk-greeter || problem "I38: LightDM (the sign-in screen) could not be installed; sign in on the console and type startx"
    [ -e /etc/systemd/system/display-manager.service ] || systemctl enable lightdm >>$PLOG 2>&1
  fi
fi
# --- the desktop -------------------------------------------------------------
O=false; [ "$G" = orca ] && O=true
if [ "$G" != none ]; then
  command -v orca >/dev/null 2>&1 || get orca || problem "Orca could not be installed: the desktop will not speak"
  # --- r408: the whole accessibility stack of each desktop, not the reader alone ---
  # AT-SPI for GTK and Qt programs, speech (Speech Dispatcher with eSpeak NG), braille (BRLTTY
  # with BrlAPI, liblouis), event sounds (libcanberra), high-contrast themes; per desktop a
  # magnifier and an on-screen keyboard where they are packaged apart from the desktop (GNOME
  # has both built in), its terminal and its text editor; Firefox as the browser.
  # The names are those of Debian 13, Ubuntu 26.04 and Arch (checked against their package
  # indexes); one that cannot be installed is logged and does not stop the others.
  # Not packaged: a magnifier for MATE on Arch (magnus), any magnifier for LXQt and LXDE.
  plasma=no; { have startplasma-x11 || have startplasma-wayland; } && plasma=yes
  case $pm in
    apt)
      all at-spi2-core libatk-adaptor speech-dispatcher speech-dispatcher-espeak-ng espeak-ng espeak-ng-data brltty xbrlapi python3-brlapi python3-louis liblouis-data libcanberra-gtk3-module sound-theme-freedesktop gnome-accessibility-themes
      have mate-session && all magnus onboard mate-terminal pluma
      have xfce4-session && all onboard xfce4-terminal mousepad
      # Plasma: KMag, KMouseTool; its X11 session, which Orca needs where Plasma's Wayland session does not hand it the keyboard yet
      # (Debian 13: kwin-x11; Ubuntu 26.04: kwin-x11 and plasma-session-x11; Arch has neither package any more)
      [ $plasma = yes ] && { all kmag kmousetool konsole kate; each kwin-x11; grep -qi ubuntu /etc/os-release 2>/dev/null && each plasma-session-x11; }
      # (xed, Cinnamon's editor, is packaged by neither Debian nor Ubuntu: gedit)
      have cinnamon-session && all onboard gnome-terminal gedit
      { have startlxqt || have startlxde; } && all onboard
      # Java programs speak only through the ATK wrapper; LibreOffice through its GTK plug-in
      have java && each libatk-wrapper-java
      have soffice && each libreoffice-gtk3
      # Debian's Firefox; Ubuntu's desktops bring their own (a snap, which an installer's chroot cannot install)
      have firefox || have firefox-esr || grep -qi ubuntu /etc/os-release 2>/dev/null || each firefox-esr;;
    pacman)
      all at-spi2-core speech-dispatcher espeak-ng brltty liblouis libcanberra sound-theme-freedesktop gnome-themes-extra firefox
      have mate-session && all onboard mate-terminal pluma mate-applets
      have xfce4-session && all onboard xfce4-terminal mousepad
      [ $plasma = yes ] && all kmag kmousetool konsole kate dolphin
      have cinnamon-session && all onboard gnome-terminal xed
      have startlxqt && all onboard featherpad openbox
      have java && each java-atk-wrapper-openjdk;;
    dnf) all at-spi2-core speech-dispatcher speech-dispatcher-espeak-ng espeak-ng brltty liblouis python3-brlapi python3-louis;;
  esac
  mkdir -p /etc/dconf/profile /etc/dconf/db/local.d /etc/dconf/db/gdm.d /etc/xdg/autostart
  printf 'user-db:user\nsystem-db:local\n' > /etc/dconf/profile/user
  # GNOME, MATE and Cinnamon: programs offer their contents to the reader (toolkit accessibility)
  # and the desktop starts Orca itself; MATE's session does so through applications/at/visual.
  printf '[org/gnome/desktop/a11y/applications]\nscreen-reader-enabled=%s\n[org/gnome/desktop/interface]\ntoolkit-accessibility=true\n[org/mate/desktop/interface]\naccessibility=true\n[org/mate/desktop/applications/at]\nscreen-reader-enabled=%s\n[org/mate/desktop/applications/at/visual]\nstartup=%s\n[org/cinnamon/desktop/a11y/applications]\nscreen-reader-enabled=%s\n[org/cinnamon/desktop/interface]\ntoolkit-accessibility=true\n' $O $O $O $O > /etc/dconf/db/local.d/00-charon-speech
  printf 'user-db:user\nsystem-db:gdm\nfile-db:/usr/share/gdm/greeter-dconf-defaults\n' > /etc/dconf/profile/gdm
  printf '[org/gnome/desktop/a11y/applications]\nscreen-reader-enabled=true\n[org/gnome/desktop/interface]\ntoolkit-accessibility=true\n' > /etc/dconf/db/gdm.d/00-charon-speech
  # r409: "dconf update" compiles these files into /etc/dconf/db/local and gdm. The tool is a
  # package of its own on Debian and Ubuntu (dconf-cli), which an i3-only or Xfce system does not
  # bring: the profile above then named a database that did not exist - every program warned
  # ("unable to open file /etc/dconf/db/local") and none of the settings above applied.
  if ! have dconf; then case $pm in apt) one dconf-cli;; *) one dconf;; esac; fi
  if have dconf && dconf update && [ -f /etc/dconf/db/local ]; then :
  else
    rm -f /etc/dconf/profile/user /etc/dconf/profile/gdm
    problem "the desktops' accessibility defaults could not be compiled (dconf update): GNOME, MATE and Cinnamon do not start the screen reader by themselves; Charon's own autostart entry still does"
  fi
  # r406: programs must offer their contents to the reader from the first one started:
  # Qt asks for it only through these variables.
  if ! grep -q '^QT_LINUX_ACCESSIBILITY_ALWAYS_ON=' /etc/environment 2>/dev/null; then
    printf 'QT_ACCESSIBILITY=1\nQT_LINUX_ACCESSIBILITY_ALWAYS_ON=1\nGNOME_ACCESSIBILITY=1\n' >> /etc/environment
  fi
  # r409: Chromium and Electron programs build their accessibility tree from the start when
  # ACCESSIBILITY_ENABLED (or GNOME_ACCESSIBILITY) is 1 (Chromium: ui/accessibility/platform/
  # atk_util_auralinux.cc). /etc/environment is read by pam_env for every session: console,
  # X11, Wayland and the display manager's greeter alike. GTK_MODULES=gail:atk-bridge is not
  # written: GTK 3 and 4 have the bridge built in, and GTK 2 programs are gone from these systems.
  grep -q '^ACCESSIBILITY_ENABLED=' /etc/environment 2>/dev/null || printf 'ACCESSIBILITY_ENABLED=1\n' >> /etc/environment
  # r409: Xfce starts the accessibility bus and loads the toolkit's bridge only with its setting
  # "Enable assistive technologies" (xfce4-session: /general/StartAssistiveTechnologies, off by
  # default; docs.xfce.org, xfce4-settings, accessibility). Made the default for every user.
  if have xfce4-session; then
    f=/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xfce4-session.xml
    if [ ! -f $f ]; then
      mkdir -p ${f%/*}
      printf '<?xml version="1.0" encoding="UTF-8"?>\n\n<channel name="xfce4-session" version="1.0">\n  <property name="general" type="empty">\n    <property name="StartAssistiveTechnologies" type="bool" value="true"/>\n  </property>\n</channel>\n' > $f
    elif ! grep -q StartAssistiveTechnologies $f; then
      if grep -q '<property name="general" type="empty">' $f; then sed -i 's|<property name="general" type="empty">|&\n    <property name="StartAssistiveTechnologies" type="bool" value="true"/>|' $f
      else sed -i 's|<channel name="xfce4-session"[^>]*>|&\n  <property name="general" type="empty">\n    <property name="StartAssistiveTechnologies" type="bool" value="true"/>\n  </property>|' $f; fi
    fi
    grep -q 'StartAssistiveTechnologies" type="bool" value="true"' $f || problem "Xfce: assistive technologies could not be made the default ($f); switch them on in Settings, Accessibility"
  fi
  # The reader's language on the desktop (speech-dispatcher speaks English otherwise).
  [ -f /etc/speech-dispatcher/speechd.conf ] && sed -i "s/^#* *DefaultLanguage .*/DefaultLanguage \"$LANGV\"/" /etc/speech-dispatcher/speechd.conf
  # --- the login screen must speak too ---
  # SDDM (Plasma, LXQt) has no screen reader support at all: a blind person cannot
  # sign in there. LightDM with its GTK greeter takes its place; GDM stays when present.
  havedm() { [ -f /usr/lib/systemd/system/$1.service ] || [ -f /lib/systemd/system/$1.service ]; }
  if havedm sddm && ! havedm gdm && ! havedm gdm3; then
    havedm lightdm || get lightdm lightdm-gtk-greeter || echo "charon: LightDM could not be installed"
    if havedm lightdm; then
      [ -f /etc/X11/default-display-manager ] && echo /usr/sbin/lightdm > /etc/X11/default-display-manager
      systemctl disable sddm >/dev/null 2>&1
      rm -f /etc/systemd/system/display-manager.service
      systemctl enable lightdm >/dev/null 2>&1
      # (a unit without an [Install] section "enables" without making the link)
      [ -e /etc/systemd/system/display-manager.service ] || ln -sf /usr/lib/systemd/system/lightdm.service /etc/systemd/system/display-manager.service
      echo "charon: LightDM replaces SDDM, which cannot speak"
    else problem "SDDM, which has no screen reader support, could not be replaced by LightDM: the sign-in screen is silent (type the password blind, or sign in on a console and start the desktop from there)"; fi
  fi
  if havedm lightdm; then
    mkdir -p /etc/lightdm/lightdm-gtk-greeter.conf.d
    printf '[greeter]\nreader=orca\na11y-states=+reader\n' > /etc/lightdm/lightdm-gtk-greeter.conf.d/90-charon-speech.conf
    f=/etc/lightdm/lightdm-gtk-greeter.conf
    if [ -f $f ] && ! grep -q '^reader *=' $f; then printf '\n# Written by Charon: the login screen speaks.\n[greeter]\nreader=orca\na11y-states=+reader\n' >> $f; fi
    # Other greeters of LightDM (Ubuntu MATE, Cinnamon) keep the switch in GSettings.
    if [ -d /usr/share/glib-2.0/schemas ]; then
      printf '[x.dm.slick-greeter]\nscreen-reader=true\n\n[org.ArcticaProject.arctica-greeter]\nscreen-reader=true\n' > /usr/share/glib-2.0/schemas/90_charon-speech.gschema.override
      command -v glib-compile-schemas >/dev/null 2>&1 && glib-compile-schemas /usr/share/glib-2.0/schemas >/dev/null 2>&1
    fi
    # r408: Plasma as the only desktop, and its X11 session exists: new users start in it (LightDM's
    # user-session), because Orca's keyboard commands depend on what KWin's Wayland session offers.
    if [ $plasma = yes ] && [ -f /usr/share/xsessions/plasmax11.desktop ] && ! have mate-session && ! have xfce4-session && ! have cinnamon-session && ! have startlxqt && ! have startlxde && ! have gnome-session; then
      mkdir -p /etc/lightdm/lightdm.conf.d
      printf '# Written by Charon: Plasma on X11 for new users, for the screen reader.\n[Seat:*]\nuser-session=plasmax11\n' > /etc/lightdm/lightdm.conf.d/90-charon-plasma-x11.conf
    fi
  fi
  # Desktops that do not read the GNOME setting (Plasma, Xfce, LXQt, LXDE) start the reader from here;
  # a reader already running is never started twice. Cthulhu speaks on every desktop, Orca until it is built.
  # r409: through one script, which looks for a reader of this user only. The entry used
  # "pgrep -x orca": the sign-in screen's Orca (the user lightdm), still running while the
  # session starts, counted as "a reader is running", and the session stayed silent.
  mkdir -p /usr/local/bin
  cat > /usr/local/bin/charon-reader <<'CHARONEOF'
#!/bin/sh
# Written by Charon: starts this user's desktop screen reader, once.
# charon-reader wait: first gives the desktop four seconds to start its own (GNOME, MATE, Cinnamon).
u=$(id -u)
# (Linda runs as python: .../share/linda/source/linda.py)
mine() { pgrep -U "$u" -x cthulhu >/dev/null 2>&1 || pgrep -U "$u" -x orca >/dev/null 2>&1 || pgrep -U "$u" -f '/linda/source/linda[.]py' >/dev/null 2>&1; }
mine && exit 0
[ "$1" = wait ] && { sleep 4; mine && exit 0; }
want=$(cat /var/lib/charon/gui-reader 2>/dev/null)
if [ "$want" = cthulhu ] && command -v cthulhu >/dev/null 2>&1; then exec cthulhu; fi
# Linda takes its keys from the X server; in a Wayland session only through its service
# linda-keyd, without which it would speak but obey no key: Orca speaks there instead.
if [ "$want" = linda ] && [ -x /usr/local/bin/linda ]; then
  if [ "$XDG_SESSION_TYPE" != wayland ] || [ -S /run/linda-keyd/socket ]; then exec /usr/local/bin/linda; fi
fi
exec orca
CHARONEOF
  chmod 755 /usr/local/bin/charon-reader
  if [ "$G" != orca ]; then
    printf '[Desktop Entry]\nType=Application\nName=Charon: screen reader\nExec=/usr/local/bin/charon-reader\nNoDisplay=true\nX-GNOME-AutoRestart=false\n' > /etc/xdg/autostart/charon-reader.desktop
  else
    printf '[Desktop Entry]\nType=Application\nName=Charon: Orca\nExec=/usr/local/bin/charon-reader wait\nNotShowIn=GNOME;\nNoDisplay=true\n' > /etc/xdg/autostart/charon-reader.desktop
  fi
fi
if has cthulhu && [ "$G" != none ] && ! command -v cthulhu >/dev/null 2>&1; then
  if stormux && get cthulhu && command -v cthulhu >/dev/null 2>&1; then echo "charon: cthulhu from the Stormux repository"
  else
    [ $pm = pacman ] && problem "Cthulhu could not be installed from the Stormux repository; it is built from its source at the next start instead"
    mkdir -p /usr/local/sbin
    cat > /usr/local/sbin/charon-cthulhu-build <<'CHARONEOF'
#!/bin/sh
# Written by Charon: builds Cthulhu from its official source once the network is there.
export DEBIAN_FRONTEND=noninteractive
# A system service has no HOME; Go and build-cthulhu.sh keep their caches (and a downloaded
# Go toolchain) under it and stop without one. Everything stays in one folder, removed at the end.
w=/var/lib/charon/cthulhu-build
export HOME=$w XDG_CACHE_HOME=$w/cache GOPATH=$w/go GOFLAGS=-modcacherw
mkdir -p $w/cache || exit 1
# What the build needs (README, CONTRIBUTING.md) and what the program needs to run (the Arch recipe's depends).
log() { echo "charon: $*"; }
if command -v apt-get >/dev/null; then apt-get -o DPkg::Lock::Timeout=900 update; apt-get -o DPkg::Lock::Timeout=900 install -y git curl ca-certificates bash tar coreutils golang-go gcc pkg-config libgtk-3-dev libatspi2.0-dev libglib2.0-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libx11-dev libxtst-dev libxi-dev libxfixes-dev libxres-dev libxkbcommon-dev liblouis-dev libbrlapi-dev speech-dispatcher at-spi2-core gstreamer1.0-plugins-base gstreamer1.0-plugins-good xclip || { log "cthulhu: what its build needs could not be installed (apt); tried again at the next start"; exit 1; }
elif command -v pacman >/dev/null; then pacman -S --needed --noconfirm git curl go gcc pkgconf gtk3 gdk-pixbuf2 at-spi2-core glib2 gstreamer gst-plugins-base gst-plugins-good libx11 libxtst libxi libxfixes libxres libxkbcommon liblouis brltty speech-dispatcher xclip || { log "cthulhu: what its build needs could not be installed (pacman); tried again at the next start"; exit 1; }
elif command -v dnf >/dev/null; then dnf install -y git curl golang gcc pkgconf-pkg-config gtk3-devel at-spi2-core-devel glib2-devel gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-good libX11-devel libXtst-devel libXi-devel libXfixes-devel libXres-devel libxkbcommon-devel liblouis-devel brlapi-devel speech-dispatcher xclip || { log "cthulhu: what its build needs could not be installed (dnf); tried again at the next start"; exit 1; }
else echo "charon: cthulhu cannot be built here: no known package manager; Orca keeps speaking"; exit 1
fi
d=$w/src; rm -rf $d
git clone --depth 1 https://git.stormux.org/storm/cthulhu.git $d || { echo "charon: cthulhu's source could not be fetched; Orca keeps speaking, the build is tried again at the next start"; exit 1; }
# build-cthulhu.sh (bash) uses the system's Go 1.21 or newer and fetches the version go.mod names when needed.
cd $d && ./build-cthulhu.sh || { echo "charon: cthulhu could not be built; Orca keeps speaking, the build is tried again at the next start"; exit 1; }
# The three programs belong together (README).
for f in cthulhu cthulhu-braille cthulhu-input-helper; do install -Dm755 $f /usr/local/bin/$f || exit 1; done
systemctl disable charon-cthulhu-build.service
cd /; rm -rf $w
echo "charon: cthulhu built; it starts at the next sign-in"
# I38 names its screen reader when its configuration is written: written again, now with Cthulhu.
if [ -x /usr/local/sbin/charon-i38 ] && [ "$(cat /var/lib/charon/gui-reader 2>/dev/null)" = cthulhu ]; then /usr/local/sbin/charon-i38 force; fi
exit 0
CHARONEOF
    chmod 755 /usr/local/sbin/charon-cthulhu-build
    printf '[Unit]\nDescription=Charon: build the Cthulhu screen reader once\nWants=network-online.target\nAfter=network-online.target\n\n[Service]\nType=oneshot\nExecStart=/usr/local/sbin/charon-cthulhu-build\nStandardOutput=append:/var/lib/charon/cthulhu-build.log\nStandardError=append:/var/lib/charon/cthulhu-build.log\nTimeoutStartSec=3600\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-cthulhu-build.service
    systemctl enable charon-cthulhu-build.service >>$PLOG 2>&1 || problem "the service that builds Cthulhu could not be switched on"
    # A running system (a ready image's first start): built now, in the background; an installer's
    # chroot has no network of its own and nothing running: built at the first start.
    if [ $running = yes ]; then systemctl start --no-block charon-cthulhu-build.service >>$PLOG 2>&1; echo "charon: cthulhu is being built from its source now (log: $S/cthulhu-build.log); Orca speaks until it is ready"
    else echo "charon: cthulhu is built at the first start with the network (log: $S/cthulhu-build.log); Orca speaks until it is ready"; fi
  fi
fi
# --- r409: Linda, the NVDA screen reader for the Linux desktop ---
# https://github.com/Mudb0y/linda (GPL 2 or later; no connection with NV Access). Delivered
# through the Nix package manager only, and built on the computer itself: about eleven
# minutes and 7.5 gigabytes in Charon's tests, no ready-made download. Nix comes from each
# system's own packages (Debian 13 and Ubuntu: nix-setup-systemd; Arch and Fedora: nix), as
# Linda's readme says. Installed for everybody (Nix's default profile), started by
# charon-reader. It needs PipeWire and an X11 session; in a Wayland session only with its
# service linda-keyd, which is not installed (its published packages could not be downloaded
# when this was written): there Orca speaks. Orca also speaks until Linda is built.
if has linda && [ "$G" != none ] && [ ! -x /usr/local/bin/linda ]; then
  mkdir -p /usr/local/sbin
  cat > /usr/local/sbin/charon-linda-build <<'CHARONEOF'
#!/bin/sh
# Written by Charon: installs the Linda screen reader through Nix, once the network is there.
export DEBIAN_FRONTEND=noninteractive
log() { echo "charon: $*"; }
free=$(df -Pk /nix 2>/dev/null | awk 'NR==2{print $4}'); [ -n "$free" ] || free=$(df -Pk / | awk 'NR==2{print $4}')
[ "${free:-0}" -ge 10485760 ] || { log "linda: fewer than 10 gigabytes are free; Linda's build needs about 7.5. Not installed; Orca keeps speaking"; exit 1; }
if ! command -v nix >/dev/null 2>&1; then
  if command -v apt-get >/dev/null; then apt-get -o DPkg::Lock::Timeout=900 update; apt-get -o DPkg::Lock::Timeout=900 install -y nix-setup-systemd curl xz-utils at-spi2-core pipewire-audio
  elif command -v pacman >/dev/null; then pacman -Syu --needed --noconfirm nix at-spi2-core pipewire pipewire-pulse wireplumber
  elif command -v dnf >/dev/null; then dnf install -y nix at-spi2-core pipewire pipewire-pulseaudio wireplumber
  fi
fi
command -v nix >/dev/null 2>&1 || { log "linda: the Nix package manager could not be installed; Orca keeps speaking, tried again at the next start"; exit 1; }
systemctl enable --now nix-daemon.service >/dev/null 2>&1 || systemctl enable --now nix-daemon.socket >/dev/null 2>&1
export HOME=/root
nix --extra-experimental-features "nix-command flakes" profile install --profile /nix/var/nix/profiles/default github:Mudb0y/linda \
  || { log "linda could not be built or installed by Nix; Orca keeps speaking, tried again at the next start"; exit 1; }
[ -x /nix/var/nix/profiles/default/bin/linda ] || { log "linda: Nix reported success, but the program is not there; Orca keeps speaking"; exit 1; }
mkdir -p /usr/local/bin
ln -sf /nix/var/nix/profiles/default/bin/linda /usr/local/bin/linda
ln -sf /nix/var/nix/profiles/default/bin/linda-say /usr/local/bin/linda-say
systemctl disable charon-linda-build.service
log "linda installed; it starts at the next sign-in to an X11 session"
exit 0
CHARONEOF
  chmod 755 /usr/local/sbin/charon-linda-build
  printf '[Unit]\nDescription=Charon: install the Linda screen reader once\nWants=network-online.target\nAfter=network-online.target\n\n[Service]\nType=oneshot\nExecStart=/usr/local/sbin/charon-linda-build\nStandardOutput=append:/var/lib/charon/linda-build.log\nStandardError=append:/var/lib/charon/linda-build.log\nTimeoutStartSec=7200\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-linda-build.service
  systemctl enable charon-linda-build.service >>$PLOG 2>&1 || problem "the service that installs Linda could not be switched on"
  if [ $running = yes ]; then systemctl daemon-reload; systemctl start --no-block charon-linda-build.service >>$PLOG 2>&1; echo "charon: linda is being built now, which takes ten minutes or more (log: $S/linda-build.log); Orca speaks until it is ready"
  else echo "charon: linda is built at the first start with the network (log: $S/linda-build.log); Orca speaks until it is ready"; fi
fi
# --- r408: MATE must come up with its menu ---
# Debian's and Arch's MATE panel has the menu bar in its own layout "default" (mate-panel, with
# mate-menus). Ubuntu MATE's layout "familiar" has no menu bar: its only menu is the Brisk Menu
# applet, which ubuntu-mate-desktop merely recommends. It is installed by name; where that
# fails, new users get the panel's own layout with the classic menu bar (org.mate.panel
# default-layout). A user whose panel exists already resets it with: mate-panel --reset
if command -v mate-session >/dev/null 2>&1; then
  command -v mate-panel >/dev/null 2>&1 || get mate-panel mate-menus || echo "charon: MATE: its panel could not be installed"
  if [ $pm = apt ] && [ -f /usr/share/mate-panel/layouts/familiar.layout ] && ! dpkg-query -W -f='${db:Status-Status}' mate-applet-brisk-menu 2>/dev/null | grep -q '^installed$'; then
    if get mate-applet-brisk-menu; then echo "charon: MATE: the Brisk Menu applet installed (the menu of Ubuntu MATE's panel)"
    else
      mkdir -p /etc/dconf/profile /etc/dconf/db/local.d
      [ -f /etc/dconf/profile/user ] || printf 'user-db:user\nsystem-db:local\n' > /etc/dconf/profile/user
      printf "[org/mate/panel/general]\ndefault-layout='default'\n" > /etc/dconf/db/local.d/00-charon-mate-menu
      command -v dconf >/dev/null 2>&1 && dconf update
      echo "charon: MATE: the Brisk Menu applet could not be installed; new users get the panel layout with the classic menu bar"
    fi
  fi
fi
# --- r408: I38's configuration for every user ---
# I38's generator writes one user's ~/.config/i3. Without questions: "i38.sh -d" (the default
# configuration; it names Cthulhu when installed, otherwise Orca), "-D" also ~/.xinitrc and
# ~/.xprofile (startx then starts i3). A user who does not exist yet (Ubuntu's installer makes
# the user at the first start) or a missing network: tried again at the next start (below).
i38miss=no
if [ $i38 = yes ]; then
  mkdir -p /usr/local/sbin
  cat > /usr/local/sbin/charon-i38 <<'CHARONEOF'
#!/bin/sh
# Written by Charon: I38 (https://git.stormux.org/storm/I38) for every user without an i3
# configuration; "charon-i38 force": written again for everybody.
reader=$(cat /var/lib/charon/gui-reader 2>/dev/null)
# (~/.config/autostart is made first: where dex is installed - Debian's and Ubuntu's i3 bring it -
# I38 starts the reader through an autostart entry it writes there, and failed without the folder.)
# i3 alone: startx starts it too (-D). Beside another desktop only i3's configuration (-d).
flag=-D
for s in mate-session startxfce4 cinnamon-session startplasma-x11 startlxqt startlxde gnome-session; do command -v $s >/dev/null 2>&1 && flag=-d; done
r=0; n=0
for h in /home/*; do
  [ -d "$h" ] || continue
  u=$(stat -c %U "$h" 2>/dev/null); uid=$(id -u "$u" 2>/dev/null) || continue
  [ "$uid" -ge 1000 ] || continue
  n=$((n+1))
  if [ "$1" != force ] && [ -f "$h/.config/i3/config" ]; then continue; fi
  if su - "$u" -c "if [ -d ~/I38/.git ]; then git -C ~/I38 pull -q --ff-only; else rm -rf ~/I38; git clone -q --depth 1 https://git.stormux.org/storm/I38.git ~/I38; fi; mkdir -p ~/.config/autostart; cd ~/I38 && ./i38.sh $flag" </dev/null >>/var/lib/charon/i38.log 2>&1 && [ -s "$h/.config/i3/config" ]; then
    # r409: the reader is started in one way only, by Charon's charon-reader (this user's
    # chosen reader, once). I38 would start Cthulhu or Orca itself - by a line in its
    # configuration, or, where dex is installed, by an autostart entry of the user - while
    # Charon's own entry in /etc/xdg/autostart starts one as well: two readers at once.
    c="$h/.config/i3/config"
    if [ -x /usr/local/bin/charon-reader ]; then
      sed -i 's#^exec [^ ]*/\(cthulhu\|orca\)$#exec /usr/local/bin/charon-reader#' "$c"
      for e in "$h"/.config/autostart/cthulhu.desktop "$h"/.config/autostart/orca.desktop; do [ -f "$e" ] && rm -f "$e"; done
      # (dex reads /etc/xdg/autostart, where Charon's entry is; without dex the line above starts it)
      chown --reference="$h" "$c"
    fi
    # r409: with its own locker installed (i38lock), I38's default locks the screen two seconds
    # after i3 starts: made for Stormux, which signs the user in by itself. Here the user has
    # just signed in (LightDM, or the console before startx) and would be asked for the password
    # twice. The lock at start goes; the lock key and the lock after five idle minutes stay.
    if grep -q "^exec --no-startup-id bash -c 'sleep 2; [^ ]*i38lock'" "$h/.config/i3/config" 2>/dev/null; then
      sed -i "/^exec --no-startup-id bash -c 'sleep 2; [^ ]*i38lock'/d" "$h/.config/i3/config"; chown --reference="$h" "$h/.config/i3/config"
    fi
    echo "charon: I38 written for $u ($flag)"
  else r=1; echo "charon: I38 could not be written for $u: $(tail -n 3 /var/lib/charon/i38.log 2>/dev/null | tr '\n' ' ')"; fi
done
[ $n -gt 0 ] || { echo "charon: I38: no user yet"; r=1; }
exit $r
CHARONEOF
  chmod 755 /usr/local/sbin/charon-i38
  /usr/local/sbin/charon-i38 || { i38miss=yes; problem "I38's configuration is not written yet (no user account yet, no network, or its generator failed: $S/i38.log); tried again at the next start"; }
fi
# --- r406: signing in from the console, the desktop started with startx ---
if [ "$M" = console ]; then
  session=
  for s in mate-session startxfce4 cinnamon-session startplasma-x11 startlxqt startlxde startplasma-wayland gnome-session i3; do command -v $s >/dev/null 2>&1 && { session=$s; break; }; done
  if [ -n "$session" ]; then
    command -v startx >/dev/null 2>&1 || { case $pm in pacman) get xorg-xinit;; dnf) get xorg-x11-xinit;; *) get xinit;; esac; } || problem "startx (xinit) could not be installed"
    systemctl set-default multi-user.target >/dev/null 2>&1
    # Debian and Ubuntu: startx runs the system's Xsession, which starts the desktop.
    # Elsewhere (Arch) it would start a bare window manager: an xinitrc names the desktop.
    if [ ! -f /etc/X11/Xsession ]; then
      printf '#!/bin/sh\n# Written by Charon: startx starts the desktop.\nfor s in mate-session startxfce4 cinnamon-session startplasma-x11 startlxqt startlxde i3; do command -v $s >/dev/null 2>&1 && exec $s; done\nexec xterm\n' > /etc/skel/.xinitrc
      for h in /home/*; do [ -d "$h" ] && [ ! -f "$h/.xinitrc" ] && { cp /etc/skel/.xinitrc "$h/.xinitrc"; chown --reference="$h" "$h/.xinitrc"; }; done
    fi
    # GNOME alone has no X session any more: there startx starts its Wayland session.
    if [ "$session" = gnome-session ]; then
      mkdir -p /usr/local/bin
      printf '#!/bin/sh\n# Written by Charon: GNOME runs on Wayland only; startx starts it from the console.\nXDG_SESSION_TYPE=wayland exec dbus-run-session gnome-session\n' > /usr/local/bin/startx
      chmod 755 /usr/local/bin/startx
    fi
    # r408: Plasma without its X11 session (Arch): the same, as Plasma's own session file starts it.
    if [ "$session" = startplasma-wayland ]; then
      mkdir -p /usr/local/bin
      printf '#!/bin/sh\n# Written by Charon: Plasma runs on Wayland only here; startx starts it from the console.\nif [ -z "$DBUS_SESSION_BUS_ADDRESS" ]; then exec dbus-run-session startplasma-wayland; fi\nexec startplasma-wayland\n' > /usr/local/bin/startx
      chmod 755 /usr/local/bin/startx
    fi
    printf '# Written by Charon: a reminder after a console login.\nif [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ] && [ "$(id -u)" != 0 ]; then case "$(tty)" in /dev/tty[0-9]*) case "$LANG" in pl*) echo "Pulpit uruchamia polecenie startx.";; *) echo "The command startx starts the desktop.";; esac;; esac; fi\n' > /etc/profile.d/charon-startx.sh
    echo "charon: console login, startx starts $session"
  else echo "charon: console login asked for, but no desktop is installed yet"; fi
fi
# --- the console -------------------------------------------------------------
if has speakup; then
  # Fedora packages neither espeakup nor any other connection between Speakup and a software synthesizer.
  command -v espeakup >/dev/null 2>&1 || get espeakup || problem "espeakup (the voice of Speakup) could not be installed${pm:+ with $pm}"
  # Ubuntu Server has speakup neither built in nor switched on: its modules come
  # in linux-modules-extra, which a minimal server does not always install.
  for k in /lib/modules/*; do [ -d "$k" ] || continue; v=${k##*/}
    find "$k" -name 'speakup_soft*' 2>/dev/null | grep -q . || { [ $pm = apt ] && apt-get -o DPkg::Lock::Timeout=900 install -y linux-modules-extra-$v >>$PLOG 2>&1; }
  done
  # The language of the console voice: espeakup's service reads default_voice (Debian and Ubuntu
  # from /etc/default/espeakup, which would override a drop-in; Arch from the unit's Environment).
  if [ -f /etc/default/espeakup ]; then
    if grep -q '^default_voice=' /etc/default/espeakup; then sed -i "s/^default_voice=.*/default_voice=$LANGV/" /etc/default/espeakup; else printf 'default_voice=%s\n' $LANGV >> /etc/default/espeakup; fi
  fi
  mkdir -p /etc/systemd/system/espeakup.service.d
  printf '[Service]\nEnvironment=default_voice=%s\n' $LANGV > /etc/systemd/system/espeakup.service.d/charon-voice.conf
fi
# --- r410: what Debian's speaking installer sets up by itself ---
# Charon makes Debian's installer speak with the boot option speakup.synth=soft. The installer
# then sets Speakup up in the new system too, whatever was checked in Charon:
#  - espeakup-udeb, finish-install.d/06espeakup: "apt-install espeakup alsa-utils", then
#    /etc/default/espeakup and, for a voice other than English, /etc/modprobe.d/speakup_soft.conf
#    (https://salsa.debian.org/a11y-team/espeakup/-/blob/master/debian/espeakup-udeb.finish-install;
#    debian/rules installs it as 06espeakup, so it runs before the late command, 07preseed of
#    https://salsa.debian.org/installer-team/preseed/-/tree/master/finish-install.d - and this
#    script runs in the late command);
#  - rootskel, debian-installer-startup.d/S37speakup: "register-module -i speakup_soft", which
#    base-installer (library.sh, "modules that have been queued for inclusion in the initrd")
#    writes into /etc/initramfs-tools/modules: the kernel module is loaded at every start
#    (https://salsa.debian.org/installer-team/rootskel/-/blob/master/src/usr/lib/debian-installer-startup.d/S37speakup);
#  - Debian's installation guide, 5.2.4 "Software Speech Synthesis": "support for software speech
#    synthesis will be automatically installed on the target system"
#    (https://www.debian.org/releases/trixie/amd64/ch05s02.en.html).
# A reader that was not checked is neither installed nor loaded. So, in an installer's chroot
# only (in a running system the packages are the user's own business): espeakup goes again when
# Speakup was not checked - only when nothing else would go with it - and the module leaves the
# initramfs when Speakup is not the reader that speaks in the console (a loaded Speakup takes the
# keypad keys for itself, which are Fenrir's keys too).
if [ $pm = apt ] && [ $running = no ]; then
  if ! has speakup; then
    if dpkg-query -W -f='${db:Status-Status}' espeakup 2>/dev/null | grep -q '^installed$'; then
      echo "--- $(date '+%F %T') purge: espeakup (installed by Debian's installer; Speakup was not chosen)" >> $PLOG
      if [ "$(apt-get -s purge espeakup 2>/dev/null | grep -c '^Purg ')" = 1 ] && apt-get -o DPkg::Lock::Timeout=900 purge -y espeakup >>$PLOG 2>&1; then
        echo "charon: espeakup, which Debian's speaking installer installed by itself, is removed again: Speakup was not chosen"
      else problem "espeakup, which Debian's installer installed by itself although Speakup was not chosen, could not be removed (see $PLOG); its service is switched off"; fi
    fi
    if [ -f /etc/modprobe.d/speakup_soft.conf ]; then rm -f /etc/modprobe.d/speakup_soft.conf; echo "charon: /etc/modprobe.d/speakup_soft.conf (written by Debian's installer) removed: Speakup was not chosen"; fi
    # r410: the installer also keeps the boot option speakup.synth=soft as "options speakup synth=soft" in
    # /etc/modprobe.d/local.conf (module options of its own command line); only that line goes, the file only if empty.
    f=/etc/modprobe.d/local.conf
    if [ -f $f ] && grep -q '^options speakup[_a-z]* ' $f; then
      sed -i '/^options speakup[_a-z]* /d' $f
      grep -q '[^[:space:]]' $f || rm -f $f
      echo "charon: the Speakup options of Debian's installer removed from $f: Speakup was not chosen"
    fi
  fi
  f=/etc/initramfs-tools/modules
  if [ "$C" != speakup ] && [ -f $f ] && grep -q '^speakup_' $f; then
    sed -i '/^speakup_/d' $f
    echo "--- $(date '+%F %T') update-initramfs: without the speakup modules Debian's installer put into $f" >> $PLOG
    if grep -q '^speakup_' $f; then problem "the Speakup kernel module, which Debian's installer has loaded at every start, could not be taken out of $f"
    elif update-initramfs -u -k all >>$PLOG 2>&1; then echo "charon: the Speakup kernel module is no longer loaded at every start (Debian's speaking installer had put it into $f): Speakup is not the console reader"
    else problem "the initramfs could not be rebuilt without the Speakup kernel module (update-initramfs; see $PLOG): the module is still loaded at the next start"; fi
  fi
fi
# r410: a console reader is worked with on several consoles: kbd brings chvt, openvt, loadkeys and
# setfont (switching consoles from a script, the console's keyboard layout and font). A desktop
# installation does not always bring it (seen: Debian 13 with MATE had no chvt).
if [ "$C" != none ] || has speakup || has fenrir; then
  have chvt || one kbd || echo "charon: the package kbd (chvt, loadkeys, setfont) could not be installed"
fi
if has fenrir; then
  # Debian and Ubuntu have Fenrir in their own repositories (written in Python; its service:
  # fenrir.service, its settings: /etc/fenrirscreenreader/settings/settings.conf). Arch has no
  # package of its own: there Fenrir (now written in Go; fenrirscreenreader.service,
  # settings.toml) comes from the Stormux repository, and when that cannot be reached, from the
  # AUR (the same recipe, built here). Fedora has no Fenrir package: built from its source.
  if ! command -v fenrir >/dev/null 2>&1; then
    case $pm in
      apt) get fenrir;;
      pacman) { stormux && get fenrir; } || aur fenrir;;
      dnf) fenrirbuild=yes; true;;
      *) false;;
    esac || problem "Fenrir could not be installed (see $PLOG)"
  fi
  # Fenrir speaks through Speech Dispatcher (its default speech driver), here with eSpeak NG.
  # Its sounds: the Python Fenrir plays them with sox (play), the Go Fenrir through PulseAudio
  # or ALSA itself. The fenrir packages only recommend or suggest these, so they are named.
  case $pm in
    apt) get speech-dispatcher speech-dispatcher-espeak-ng espeak-ng python3-speechd sox libsox-fmt-alsa libsox-fmt-pulse || problem "Fenrir: speech-dispatcher, python3-speechd or sox (its sounds) could not be installed";;
    pacman) get speech-dispatcher espeak-ng alsa-utils || problem "Fenrir: speech-dispatcher or espeak-ng could not be installed";;
    dnf) get speech-dispatcher speech-dispatcher-espeak-ng espeak-ng alsa-utils || problem "Fenrir: speech-dispatcher or espeak-ng could not be installed";;
  esac
  [ -f /etc/speech-dispatcher/speechd.conf ] && sed -i "s/^#* *DefaultLanguage .*/DefaultLanguage \"$LANGV\"/" /etc/speech-dispatcher/speechd.conf
  # --- r409: Fenrir's sounds ---
  # Fenrir runs as a system service. The Python Fenrir's "play" and the Go Fenrir's sound
  # driver "pulse" both look for a sound server, and a system service has none: no sound icons
  # at all on a console-only system. Charon's charon-audio-share (below) therefore says which
  # way is open at the moment - the signed-in user's sound server, or the sound card itself
  # (ALSA) - and both Fenrirs follow it:
  #  - Python (settings.conf [sound]): enabled, the generic driver, its two commands through
  #    /usr/local/bin/charon-fenrir-play, which runs sox on PulseAudio or on ALSA (one of them, with a time limit);
  #  - Go (settings.toml [presentation.sound]): enabled; driver "pulse" or "alsa" is written
  #    by charon-audio-share whenever the way changes (Fenrir reads it at launch).
  mkdir -p /usr/local/bin
  cat > /usr/local/bin/charon-fenrir-play <<'CHARONEOF'
#!/bin/sh
# Written by Charon: Fenrir's sounds (sox), through the signed-in user's sound server when it
# has the sound card (the link is kept by charon-audio-share), otherwise straight to the card.
# Never both: a sound played to the card while a sound server is starting takes the card away
# from it. Never without a time limit: a "play" that hangs would hold the card for ever.
if [ -S /run/charon-pulse.sock ]; then
  [ -f /run/charon-pulse.cookie ] && export PULSE_COOKIE=/run/charon-pulse.cookie
  AUDIODRIVER=pulseaudio PULSE_SERVER=unix:/run/charon-pulse.sock exec timeout 15 play "$@"
fi
AUDIODRIVER=alsa exec timeout 15 play "$@"
CHARONEOF
  chmod 755 /usr/local/bin/charon-fenrir-play
  f=/etc/fenrirscreenreader/settings/settings.conf
  if [ -f $f ]; then
    sed -i '/^\[sound\]/,/^\[/{s/^#\{0,1\} *enabled *=.*/enabled=True/;s/^driver *=.*/driver=genericDriver/;s|^genericPlayFileCommand *=.*|genericPlayFileCommand=/usr/local/bin/charon-fenrir-play -q -v fenrirVolume fenrirSoundFile|;s|^genericFrequencyCommand *=.*|genericFrequencyCommand=/usr/local/bin/charon-fenrir-play -q -v fenrirVolume -n -c1 synth fenrirDuration sine fenrirFrequence|;}' $f
    sed -i "/^\[speech\]/,/^\[/{s/^#\{0,1\} *language *=.*/language=$LANGV/;}" $f
    grep -q '^genericPlayFileCommand=/usr/local/bin/charon-fenrir-play' $f || problem "Fenrir: its sound settings in $f could not be written"
    command -v play >/dev/null 2>&1 || problem "Fenrir: sox (play) is missing, so Fenrir has no sounds"
    ls /usr/share/sounds/fenrirscreenreader/default/*.wav >/dev/null 2>&1 || problem "Fenrir: its sound theme (/usr/share/sounds/fenrirscreenreader/default) is missing"
  fi
  f=/etc/fenrirscreenreader/settings/settings.toml
  if [ -f $f ]; then
    sed -i '/^\[presentation\.sound\]/,/^\[/{s/^enabled *=.*/enabled = true/;s/^theme *=.*/theme = "default"/;}' $f
    sed -i "/^\[presentation\.speech\]/,/^\[/{s/^language *=.*/language = \"$LANGV\"/;}" $f
    ls /usr/share/sounds/fenrirscreenreader/default/*.wav >/dev/null 2>&1 || problem "Fenrir: its sound theme (/usr/share/sounds/fenrirscreenreader/default) is missing"
  fi
fi
# --- r409: Fenrir from its source, where no package exists (Fedora; Arch without Stormux and AUR) ---
# https://git.stormux.org/storm/fenrir (README: build-fenrir.sh needs a C compiler, pkg-config and
# the development files of libevdev, X11, libvterm and ALSA; it fetches the Go its go.mod names;
# "the recipe must also install the TOML configuration, keyboard layouts, punctuation profiles,
# sounds, scripts, and appropriate service integration": done here as Stormux's Arch package does.)
if [ "$fenrirbuild" = yes ]; then
  mkdir -p /usr/local/sbin
  cat > /usr/local/sbin/charon-fenrir-build <<'CHARONEOF'
#!/bin/sh
# Written by Charon: builds the Fenrir screen reader from its official source.
log() { echo "charon: $*"; }
w=/var/lib/charon/fenrir-build
export HOME=$w XDG_CACHE_HOME=$w/cache GOPATH=$w/go GOFLAGS=-modcacherw
mkdir -p $w/cache || exit 1
if command -v dnf >/dev/null; then dnf install -y git curl tar gcc pkgconf-pkg-config libevdev-devel libX11-devel libvterm-devel alsa-lib-devel speech-dispatcher speech-dispatcher-espeak-ng espeak-ng || { log "fenrir: what its build needs could not be installed (dnf); tried again at the next start"; exit 1; }
elif command -v pacman >/dev/null; then pacman -S --needed --noconfirm git curl tar gcc pkgconf libevdev libx11 libvterm alsa-lib speech-dispatcher espeak-ng || { log "fenrir: what its build needs could not be installed (pacman); tried again at the next start"; exit 1; }
elif command -v apt-get >/dev/null; then apt-get -o DPkg::Lock::Timeout=900 install -y git curl ca-certificates tar gcc pkg-config libevdev-dev libx11-dev libvterm-dev libasound2-dev speech-dispatcher speech-dispatcher-espeak-ng espeak-ng || { log "fenrir: what its build needs could not be installed (apt); tried again at the next start"; exit 1; }
else log "fenrir cannot be built here: no known package manager"; exit 1
fi
d=$w/src; rm -rf $d
git clone --depth 1 https://git.stormux.org/storm/fenrir.git $d || { log "fenrir's source could not be fetched; tried again at the next start"; exit 1; }
cd $d && ./build-fenrir.sh --output $w/fenrir || { log "fenrir could not be built; tried again at the next start"; exit 1; }
install -Dm755 $w/fenrir /usr/bin/fenrir || exit 1
for k in settings keyboard punctuation; do
  mkdir -p /etc/fenrirscreenreader/$k
  for f in config/$k/*.toml; do [ -f /etc/fenrirscreenreader/$k/${f##*/} ] || install -m644 $f /etc/fenrirscreenreader/$k/; done
done
mkdir -p /usr/share/sounds/fenrirscreenreader /usr/share/fenrirscreenreader/scripts /usr/share/fenrirscreenreader/tools
cp -r config/sound/default /usr/share/sounds/fenrirscreenreader/
cp config/scripts/* /usr/share/fenrirscreenreader/scripts/ 2>/dev/null
cp tools/configure_pipewire.sh tools/configure_pulse.sh /usr/share/fenrirscreenreader/tools/ 2>/dev/null
# the service of Stormux's package
printf '[Unit]\nDescription=Fenrir console screen reader\nAfter=sound.target\n\n[Service]\n# The launcher exits after the first screen capture and keyboard poll.\nType=forking\nExecStart=/usr/bin/fenrir\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/fenrirscreenreader.service
# SELinux (Fedora): the new files get the labels of their places
command -v restorecon >/dev/null 2>&1 && restorecon -R /usr/bin/fenrir /etc/fenrirscreenreader /usr/share/sounds/fenrirscreenreader /usr/share/fenrirscreenreader /etc/systemd/system/fenrirscreenreader.service >/dev/null 2>&1
systemctl daemon-reload
systemctl disable charon-fenrir-build.service
cd /; rm -rf $w
log "fenrir built and installed"
# the rest (language, sounds, which console reader speaks) as for a packaged Fenrir
[ -f /var/lib/charon/speech.args ] && sh /var/lib/charon/speech.sh $(cat /var/lib/charon/speech.args) >> /var/lib/charon/speech.log 2>&1
exit 0
CHARONEOF
  chmod 755 /usr/local/sbin/charon-fenrir-build
  printf '[Unit]\nDescription=Charon: build the Fenrir screen reader once\nWants=network-online.target\nAfter=network-online.target\n\n[Service]\nType=oneshot\nExecStart=/usr/local/sbin/charon-fenrir-build\nStandardOutput=append:/var/lib/charon/fenrir-build.log\nStandardError=append:/var/lib/charon/fenrir-build.log\nTimeoutStartSec=3600\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-fenrir-build.service
  systemctl enable charon-fenrir-build.service >>$PLOG 2>&1 || problem "the service that builds Fenrir could not be switched on"
  if [ $running = yes ]; then systemctl daemon-reload; systemctl start --no-block charon-fenrir-build.service >>$PLOG 2>&1; fi
  echo "charon: fenrir is built from its source (log: $S/fenrir-build.log); it speaks when the build is done"
fi
if has emacspeak; then
  # Debian and Ubuntu: emacspeak with its eSpeak speech server (emacspeak-espeak-server; the
  # package only recommends it) and sox for its sound icons; emacs-nox first, so that the
  # dependency "emacs" does not bring a graphical Emacs to a console.
  # Fedora: emacspeak is packaged. Arch: only the AUR has it (and tclx, which it needs): both
  # are built there as the account charon-build; its recipe builds the eSpeak server (make espeak).
  if ! command -v emacspeak >/dev/null 2>&1; then
    case $pm in
      apt) have emacs || get emacs-nox; get emacspeak emacspeak-espeak-server sox;;
      pacman) have emacs || get emacs-nox || get emacs; aur emacspeak;;
      dnf) get emacspeak;;
      *) false;;
    esac || problem "Emacspeak could not be installed (see $PLOG)"
  fi
  command -v espeak-ng >/dev/null 2>&1 || get espeak-ng || problem "Emacspeak: eSpeak NG could not be installed"
  if command -v emacspeak >/dev/null 2>&1; then
    # the eSpeak speech server must really be there: without it Emacspeak starts silent
    find /usr/lib /usr/lib64 /usr/libexec /usr/share/emacs /usr/share/emacspeak -name 'tclespeak.so*' 2>/dev/null | grep -q . \
      || problem "Emacspeak is installed, but its eSpeak speech server (tclespeak.so) is not: it would start silent"
    command -v tclsh >/dev/null 2>&1 || problem "Emacspeak: tclsh is missing, its speech servers cannot run"
  fi
fi
# fenrirunit enable|disable: Fenrir's service under either name. Debian's and Ubuntu's packages
# switch their service on and start it when they are installed, so a reader that is to stay
# silent is also stopped (in a running system; an installer's chroot has nothing running).
fenrirunit() {
  fr=1
  for s in fenrirscreenreader fenrir; do
    systemctl $1 $s.service >/dev/null 2>&1 || continue
    fr=0
    if [ $1 = disable ] && [ $running = yes ]; then systemctl stop $s.service >/dev/null 2>&1; fi
  done
  return $fr
}
espeakupoff() { systemctl disable espeakup >/dev/null 2>&1; if [ $running = yes ]; then systemctl stop espeakup >/dev/null 2>&1; fi; }
if [ "$C" = speakup ]; then
  echo speakup_soft > /etc/modules-load.d/charon-speakup.conf
  fenrirunit disable
  if command -v espeakup >/dev/null 2>&1; then systemctl enable espeakup >>$PLOG 2>&1 || problem "espeakup's service could not be switched on: the console will not speak"; fi
elif [ "$C" = fenrir ]; then
  rm -f /etc/modules-load.d/charon-speakup.conf; espeakupoff
  if [ "$fenrirbuild" = yes ] && ! command -v fenrir >/dev/null 2>&1; then echo "charon: fenrir's service is switched on when its build is done"
  else fenrirunit enable || problem "Fenrir's service could not be switched on (fenrir.service, fenrirscreenreader.service): the console will not speak"; fi
elif [ "$C" = emacspeak ]; then
  # Emacspeak cannot read the login prompt: console 1 signs the first user in and starts it.
  rm -f /etc/modules-load.d/charon-speakup.conf; espeakupoff; fenrirunit disable
  U=$(getent passwd 1000 | cut -d: -f1)
  if [ -n "$U" ] && command -v emacspeak >/dev/null 2>&1; then
    mkdir -p /etc/systemd/system/getty@tty1.service.d
    A=$(command -v agetty)
    printf '[Service]\nExecStart=\nExecStart=-%s --autologin %s --noclear %%I $TERM\n' "$A" "$U" > /etc/systemd/system/getty@tty1.service.d/charon-emacspeak.conf
    printf '# Written by Charon: Emacspeak on console 1, speaking with eSpeak.\nexport DTK_PROGRAM=espeak\nif [ "$(tty)" = /dev/tty1 ] && [ -z "$INSIDE_EMACS" ]; then emacspeak; fi\n' > /etc/profile.d/charon-emacspeak.sh
    echo "charon: emacspeak starts on console 1 for $U"
  else
    if [ -z "$U" ]; then echo "charon: emacspeak is set up for console 1 once the first user exists"; else problem "Emacspeak is not installed, so console 1 does not start it"; fi
    # An installer that makes the user only at the first start (Ubuntu's): tried again then (below).
    [ -z "$U" ] && command -v emacspeak >/dev/null 2>&1 && nouser=yes
  fi
fi
# --- r409: RHVoice as the voice (the word rhvoice in the list of readers) ---
# https://github.com/RHVoice/RHVoice (its Speech Dispatcher module: sd_rhvoice). Packaged by
# Debian (in "non-free": some voices may not be used commercially), Ubuntu and Arch; Fedora has
# no package. Made Speech Dispatcher's default module, so Orca, Cthulhu and Fenrir speak with
# it; Speakup (espeakup) and Emacspeak keep eSpeak NG, which is all they can use. Where
# RHVoice cannot be installed, or has no voice for the system's language, eSpeak NG speaks
# and that is reported.
if has rhvoice; then
  rv=no; rvlang=; rvvoice=
  case $LANGV in
    en) rvlang=english; rvvoice=slt;; pl) rvlang=polish; rvvoice=magda;; ru) rvlang=russian; rvvoice=anna;;
    uk) rvlang=ukrainian; rvvoice=natalia;; cs) rvlang=czech; rvvoice=zdenek;; sk) rvlang=slovak; rvvoice=ondro;;
    sq) rvlang=albanian; rvvoice=hana;; mk) rvlang=macedonian; rvvoice=kiko;; ky) rvlang=kyrgyz; rvvoice=azamat;;
    tt) rvlang=tatar; rvvoice=talgat;; uz) rvlang=uzbek; rvvoice=sevinch;; eo) rvlang=esperanto; rvvoice=spomenka;;
  esac
  # r409: the voice Charon itself speaks with (CHARON_VOICE), when it is a voice of this system's
  # language; otherwise, and when it cannot be installed, another voice of the language (said
  # as a problem), and only then eSpeak NG. Polish and English voices as RHVoice 1.14-1.16 has them.
  rvwant=$(printf '%s' "$CHARON_VOICE" | tr 'A-Z' 'a-z' | tr -cd 'a-z0-9-')
  if [ -n "$rvwant" ] && [ -n "$rvlang" ]; then
    case "$rvlang:$rvwant" in
      polish:alicja|polish:cezary|polish:magda|polish:michal|polish:natan|english:alan|english:bdl|english:clb|english:slt|english:lyubov|english:evgeniy-eng) rvvoice=$rvwant;;
      *) if [ -d /usr/share/RHVoice/voices/$rvwant ] || { [ $pm = pacman ] && pacman -Si rhvoice-voice-$rvwant >/dev/null 2>&1 && [ $rvlang != polish ] && [ $rvlang != english ]; }; then rvvoice=$rvwant
         else problem "RHVoice: Charon's voice $rvwant is not a voice of this system's language ($rvlang); the voice $rvvoice speaks instead"; fi;;
    esac
  fi
  if [ -z "$rvlang" ]; then problem "RHVoice has no voice for the language $LANGV: eSpeak NG speaks instead"
  else
    case $pm in
      apt)
        if ! apt-cache show rhvoice 2>/dev/null | grep -q '^Package:' && grep -q '^ID=debian' /etc/os-release 2>/dev/null; then
          # Debian keeps RHVoice in its component non-free, which a fresh system does not use
          code=$(sed -n 's/^VERSION_CODENAME=//p' /etc/os-release)
          if [ -n "$code" ]; then
            printf '# Written by Charon: the component non-free, for the RHVoice speech synthesizer.\nTypes: deb\nURIs: http://deb.debian.org/debian\nSuites: %s %s-updates\nComponents: non-free\nSigned-By: /usr/share/keyrings/debian-archive-keyring.gpg\n' "$code" "$code" > /etc/apt/sources.list.d/charon-non-free.sources
            apt-get -o DPkg::Lock::Timeout=900 update >>$PLOG 2>&1
            echo "charon: RHVoice: Debian's component non-free switched on (/etc/apt/sources.list.d/charon-non-free.sources)"
          fi
        fi
        get rhvoice speech-dispatcher-rhvoice rhvoice-english && rv=yes
        [ $rv = yes ] && [ $rvlang != english ] && { one rhvoice-$rvlang || { rv=no; problem "RHVoice: its $rvlang voices (rhvoice-$rvlang) could not be installed: eSpeak NG speaks instead"; }; }
        # (Debian's and Ubuntu's language packages hold all the voices of a language)
        if [ $rv = yes ] && [ ! -d /usr/share/RHVoice/voices/$rvvoice ]; then
          other=$(grep -l -i "^language=$rvlang" /usr/share/RHVoice/voices/*/voice.info 2>/dev/null | head -n1); other=${other%/voice.info}; other=${other##*/}
          if [ -n "$other" ]; then problem "RHVoice: the voice $rvvoice is not in this system's packages; the $rvlang voice $other speaks instead"; rvvoice=$other
          else rv=no; problem "RHVoice: no $rvlang voice is installed: eSpeak NG speaks instead"; fi
        fi;;
      pacman)
        get rhvoice rhvoice-language-english rhvoice-voice-slt && rv=yes
        if [ $rv = yes ] && [ $rvvoice != slt ]; then
          if get rhvoice-language-$rvlang rhvoice-voice-$rvvoice; then :
          else
            # another voice of the same language, then eSpeak NG
            case $rvlang in polish) alt="magda natan michal alicja cezary";; english) alt="slt alan bdl clb";; *) alt=;; esac
            got=
            for o in $alt; do [ $o = $rvvoice ] && continue; if get rhvoice-language-$rvlang rhvoice-voice-$o; then got=$o; break; fi; done
            if [ -n "$got" ]; then problem "RHVoice: the voice $rvvoice could not be installed (rhvoice-voice-$rvvoice); the $rvlang voice $got speaks instead"; rvvoice=$got
            else rv=no; problem "RHVoice: no $rvlang voice could be installed (rhvoice-voice-$rvvoice): eSpeak NG speaks instead"; fi
          fi
        fi;;
      dnf) problem "RHVoice is not packaged for Fedora: eSpeak NG speaks instead";;
      *) problem "RHVoice cannot be installed here (no known package manager): eSpeak NG speaks instead";;
    esac
    if [ $rv = yes ] && find /usr/lib /usr/lib64 /usr/libexec -name sd_rhvoice 2>/dev/null | grep -q . && [ -f /etc/speech-dispatcher/speechd.conf ]; then
      f=/etc/speech-dispatcher/speechd.conf
      if grep -q '^ *DefaultModule ' $f; then sed -i 's/^ *DefaultModule .*/DefaultModule rhvoice/' $f
      elif grep -q '^# *DefaultModule ' $f; then sed -i '0,/^# *DefaultModule .*/s//DefaultModule rhvoice/' $f
      else printf '\n# Written by Charon: RHVoice is the voice.\nDefaultModule rhvoice\n' >> $f; fi
      sed -i "s/^#* *DefaultLanguage .*/DefaultLanguage \"$LANGV\"/" $f
      # The voice by name, where a program has a setting for it. RHVoice shows its voices to
      # Speech Dispatcher with a capital letter (Magda, Evgeniy-Eng).
      rvname=$(printf '%s' "$rvvoice" | awk -F- '{for(i=1;i<=NF;i++){$i=toupper(substr($i,1,1)) substr($i,2)}; OFS="-"; $1=$1; print}')
      m=/etc/speech-dispatcher/modules/rhvoice.conf
      # (the module of RHVoice 1.14, Debian and Ubuntu, has a default voice of its own; newer ones choose by language)
      [ -f $m ] && grep -q '^RHVoiceDefaultVoice ' $m && sed -i "s/^RHVoiceDefaultVoice .*/RHVoiceDefaultVoice \"$rvname\"/" $m
      m=/etc/fenrirscreenreader/settings/settings.toml
      [ -f $m ] && sed -i "/^\[presentation\.speech\]/,/^\[/{s/^module *=.*/module = \"rhvoice\"/;s/^voice *=.*/voice = \"$rvname\"/;}" $m
      m=/etc/fenrirscreenreader/settings/settings.conf
      [ -f $m ] && sed -i "/^\[speech\]/,/^\[/{s/^#\{0,1\} *module *=.*/module=rhvoice/;s/^#\{0,1\} *voice *=.*/voice=$rvname/;}" $m
      echo "$rvname" > $S/rhvoice-voice
      echo "charon: RHVoice is Speech Dispatcher's default voice ($rvlang, voice $rvname; Orca and Cthulhu choose their voice in their own per-user settings, which Charon does not write)"
    elif [ $rv = yes ] || { [ $pm != dnf ] && [ $pm != none ]; }; then
      [ $rv = yes ] && problem "RHVoice is installed, but its Speech Dispatcher module (sd_rhvoice) or speechd.conf was not found: eSpeak NG speaks instead"
      [ $rv = no ] && ! grep -q RHVoice $PROBLEMS && problem "RHVoice could not be installed (see $PLOG): eSpeak NG speaks instead"
    fi
  fi
fi
# --- r409: the installed readers start with Charon's rate, pitch and punctuation level ---
# Speech Dispatcher's defaults (speechd.conf: DefaultRate and DefaultPitch -100..100,
# DefaultPunctuationMode), both Fenrirs' own settings, and Speakup's soft synthesizer (0..9, set
# after espeakup starts). Charon's 10 (100 percent) is each synthesizer's normal value; the
# scales are not the same, so the result is close, not identical. Orca and Cthulhu keep their
# own per-user settings (rate, pitch, punctuation), which Charon does not write.
num() { case "$1" in ''|*[!0-9]*) return 1;; esac; [ "$1" -ge 5 ] && [ "$1" -le 20 ]; }
setconf() { # FILE KEY VALUE: a directive of speechd.conf
  if grep -q "^ *$2 " "$1"; then sed -i "s|^ *$2 .*|$2 $3|" "$1"
  elif grep -q "^# \{0,1\}$2 " "$1"; then sed -i "0,/^# \{0,1\}$2 .*/s||$2 $3|" "$1"
  else printf '%s %s\n' "$2" "$3" >> "$1"; fi
}
vr=; vp=; vpunct=
num "$CHARON_RATE" && vr=$(( (CHARON_RATE - 10) * 10 ))
num "$CHARON_PITCH" && vp=$(( (CHARON_PITCH - 10) * 10 ))
case "$CHARON_PUNCTUATION" in none|some|most|all) vpunct=$CHARON_PUNCTUATION;; esac
if [ -n "$vr$vp$vpunct" ]; then
  f=/etc/speech-dispatcher/speechd.conf
  if [ -f $f ]; then
    [ -n "$vr" ] && setconf $f DefaultRate $vr
    [ -n "$vp" ] && setconf $f DefaultPitch $vp
    [ -n "$vpunct" ] && setconf $f DefaultPunctuationMode "\"$vpunct\""
  fi
  f=/etc/fenrirscreenreader/settings/settings.toml
  if [ -f $f ]; then
    [ -n "$vr" ] && sed -i "/^\[presentation\.speech\]/,/^\[/{s/^rate *=.*/rate = $vr/;}" $f
    [ -n "$vp" ] && sed -i "/^\[presentation\.speech\]/,/^\[/{s/^pitch *=.*/pitch = $vp/;}" $f
    [ -n "$vpunct" ] && sed -i "/^\[reading\]/,/^\[/{s/^punctuation_level *=.*/punctuation_level = \"$vpunct\"/;}" $f
  fi
  f=/etc/fenrirscreenreader/settings/settings.conf
  if [ -f $f ]; then
    # the Python Fenrir: 0.0 .. 1.0, 0.5 normal
    [ -n "$vr" ] && sed -i "/^\[speech\]/,/^\[/{s/^rate *=.*/rate=$(LC_ALL=C awk "BEGIN{printf \"%.2f\", ($vr+100)/200}")/;}" $f
    [ -n "$vp" ] && sed -i "/^\[speech\]/,/^\[/{s/^pitch *=.*/pitch=$(LC_ALL=C awk "BEGIN{printf \"%.2f\", ($vp+100)/200}")/;}" $f
    [ -n "$vpunct" ] && sed -i "/^\[general\]/,/^\[/{s/^punctuationLevel *=.*/punctuationLevel=$vpunct/;}" $f
  fi
  if has speakup; then
    sr=; sp2=; spu=
    [ -n "$vr" ] && { if [ $vr -ge 0 ]; then sr=$(( 2 + vr * 7 / 100 )); else sr=$(( 2 + vr * 2 / 50 )); fi; }
    [ -n "$vp" ] && { if [ $vp -ge 0 ]; then sp2=$(( 5 + vp * 4 / 100 )); else sp2=$(( 5 + vp * 5 / 50 )); fi; }
    case "$vpunct" in none) spu=0;; some) spu=1;; most) spu=2;; all) spu=3;; esac
    mkdir -p /usr/local/sbin /etc/systemd/system/espeakup.service.d
    printf '#!/bin/sh\n# Written by Charon: Speakup starts with Charon'"'"'s rate, pitch and punctuation level.\nd=/sys/accessibility/speakup\nn=0; while [ ! -e $d/soft/rate ] && [ $n -lt 20 ]; do sleep 0.5; n=$((n+1)); done\n[ -n "%s" ] && echo %s > $d/soft/rate 2>/dev/null\n[ -n "%s" ] && echo %s > $d/soft/pitch 2>/dev/null\n[ -n "%s" ] && echo %s > $d/punc_level 2>/dev/null\nexit 0\n' "$sr" "${sr:-0}" "$sp2" "${sp2:-0}" "$spu" "${spu:-0}" > /usr/local/sbin/charon-speakup-voice
    chmod 755 /usr/local/sbin/charon-speakup-voice
    printf '[Service]\nExecStartPost=-/usr/local/sbin/charon-speakup-voice\n' > /etc/systemd/system/espeakup.service.d/charon-speakup-voice.conf
  fi
  echo "charon: voice settings from Charon: rate ${vr:-unchanged} pitch ${vp:-unchanged} (Speech Dispatcher scale) punctuation ${vpunct:-unchanged}"
fi
# --- r406: the console reader and the desktop share the sound card ---
# A console reader runs as a system service and plays straight to the sound card; a
# desktop's sound server (PipeWire or PulseAudio of the signed-in user) wants the same
# card for itself, and whoever comes second stays silent. So the console reader is sent
# through the sound server of whoever is signed in (root may use any user's socket), and
# plays to the card itself only while nobody is. A small watcher keeps the link
# /run/charon-pulse.sock on the right socket and restarts the console reader when it
# changes; before a user's session starts, the console reader lets go of the card.
# r410: "whoever is signed in" is the session in front on the seat (logind), or a user whose
# sound server demonstrably has the card; with a console's sign-in prompt in front and a
# graphical sign-in screen behind it, that is nobody, and the reader plays to the card itself
# (PipeWire and PulseAudio of a session that is not in front have let go of it: the
# sign-in screen's account is not in the group audio and has the card through logind only).
if [ "$C" != none ] && [ -d /etc/systemd/system ]; then
  # A desktop brings a package that makes PipeWire the default ALSA output for everybody.
  # A system service has no PipeWire of its own, so the console reader then got "Host is
  # down" and stayed silent (seen: espeakup on Arch with Xfce). Root's own ALSA default
  # goes straight to the sound card; users' programs are not touched.
  if [ ! -f /root/.asoundrc ] || grep -q 'Written by Charon' /root/.asoundrc; then
    printf '# Written by Charon: system services (console screen readers) play straight to the sound card.\npcm.!default { type plug slave.pcm "sysdefault" }\nctl.!default { type hw card 0 }\n' > /root/.asoundrc
  fi
  # r409: ALSA's pulse plug-in, through which espeakup and eSpeak NG reach a sound server (see setlink below)
  if have pipewire || have pulseaudio; then
    plug=no; for f in /usr/lib/*/alsa-lib/libasound_module_pcm_pulse.so /usr/lib/alsa-lib/libasound_module_pcm_pulse.so /usr/lib64/alsa-lib/libasound_module_pcm_pulse.so; do [ -f "$f" ] && plug=yes; done
    [ $plug = yes ] || case $pm in
      apt) one libasound2-plugins;; pacman) one alsa-plugins;; dnf) one alsa-plugins-pulseaudio;; esac || problem "ALSA's PulseAudio plug-in could not be installed: Speakup is silent while a desktop's sound server has the sound card"
  fi
  mkdir -p /usr/local/sbin
  cat > /usr/local/sbin/charon-audio-share <<'CHARONEOF'
#!/bin/sh
# Written by Charon: one sound card for the console reader and the desktop.
link=/run/charon-pulse.sock
readers="espeakup fenrirscreenreader fenrir"
# Nothing here may wait long: a session start hangs on it. (Fenrir takes up to 90 seconds
# to stop by itself; its unit gets 3 seconds from Charon, and here it is not waited for.)
quiet() {
  pkill -KILL -u root -x speech-dispatcher 2>/dev/null
  pkill -KILL -u root -f /speech-dispatcher-modules/ 2>/dev/null
  # (a sound of Fenrir still being played to the card would keep the card)
  pkill -KILL -u root -x play 2>/dev/null
}
restart() {
  quiet
  for s in $readers; do systemctl --no-block try-restart $s.service 2>/dev/null; done
}
installed() { command -v pipewire >/dev/null 2>&1 || command -v pulseaudio >/dev/null 2>&1; }
# r409: does a sound server really have the sound card? Its socket alone says nothing: the
# socket is there before the server has looked for a card, and it is there when the server
# found none (because a console reader was holding it). A reader sent to such a server is
# silent; a reader that then falls back to the card takes it away from the server for good.
# r410: asked of each user's sound server by itself. The question used to be "does any sound
# server hold anything under /dev/snd", and the answer was then given to the first socket found.
# Seen on Debian 13 with MATE and LightDM: with a text console in front, the sign-in screen's
# session is not the active one, logind has taken its access to the sound card away
# (sd-login(3), logind's "uaccess" device ACLs follow the active session of the seat) and
# PipeWire has dropped the card (spa/plugins/alsa/alsa-udev.c, check_access(): a card whose
# /dev/snd/controlC* and pcmC* it may not open is removed) - but its process still held
# /dev/snd/seq, the sequencer, which is not a card. Fenrir was sent to that server and was
# silent on the console until somebody signed in there.
#  - hascard UID: a sound server of this user holds a control or PCM device of a card (the
#    sequencer and the timer do not count), and the user may open the card at this moment
#    (PulseAudio keeps the card's mixer open while it is suspended for an inactive session;
#    setpriv, not runuser: no PAM session, nothing in the journal every second).
#  - seatuser: the user of the session in front on seat0 (logind: the seat's ActiveSession);
#    nothing while a console's sign-in prompt is in front - that is no session.
mayopen() {
  command -v setpriv >/dev/null 2>&1 || return 0
  g=$(id -g "$1" 2>/dev/null) || return 0
  for c in /dev/snd/controlC*; do
    [ -e "$c" ] || continue
    setpriv --reuid "$1" --regid "$g" --init-groups test -w "$c" 2>/dev/null && return 0
  done
  return 1
}
hascard() {
  for p in $(pgrep -U "$1" -x pipewire; pgrep -U "$1" -x wireplumber; pgrep -U "$1" -x pulseaudio); do
    if ls -l /proc/$p/fd 2>/dev/null | grep -q -e '/dev/snd/controlC' -e '/dev/snd/pcmC'; then mayopen "$1" && return 0; return 1; fi
  done
  return 1
}
# r410: playing: somebody holds a playback PCM of a card. hassink UID: that user's PipeWire has an
# ALSA output node (unknown -> yes, so nothing is done). reprobe UID: restart that user's WirePlumber.
playing() {
  for d in /dev/snd/pcmC*p; do
    [ -e "$d" ] || continue
    for p in /proc/[0-9]*/fd/*; do [ "$(readlink "$p" 2>/dev/null)" = "$d" ] && return 0; done
  done
  return 1
}
asuser() {
  g=$(id -g "$1" 2>/dev/null) || return 1
  u1=$1; shift
  setpriv --reuid "$u1" --regid "$g" --init-groups env XDG_RUNTIME_DIR=/run/user/$u1 "$@"
}
hassink() {
  command -v setpriv >/dev/null 2>&1 || return 0
  if command -v pw-cli >/dev/null 2>&1; then
    o=$(asuser "$1" pw-cli ls Node 2>/dev/null) || return 0
    [ -n "$o" ] || return 0
    echo "$o" | grep -q 'alsa_output' && return 0
    return 1
  fi
  if command -v pactl >/dev/null 2>&1; then
    o=$(asuser "$1" pactl list short sinks 2>/dev/null) || return 0
    echo "$o" | grep -q 'alsa_output' && return 0
    return 1
  fi
  return 0
}
reprobe() {
  pgrep -U "$1" -x wireplumber >/dev/null 2>&1 || return 1
  asuser "$1" systemctl --user restart wireplumber.service 2>/dev/null && return 0
  pkill -U "$1" -x wireplumber 2>/dev/null
}
seatuser() {
  a=$(loginctl show-seat seat0 -p ActiveSession --value 2>/dev/null)
  [ -n "$a" ] && loginctl show-session "$a" -p User --value 2>/dev/null
}
target() {
  # the user in front first; then anybody whose sound server really has the card (a user in
  # the group audio keeps it while another console is in front: playing straight to the card
  # would then find it busy); nobody: nothing, and the reader plays to the card itself
  u=$(seatuser)
  [ -n "$u" ] && [ -S /run/user/$u/pulse/native ] && hascard "$u" && { echo /run/user/$u/pulse/native; return; }
  for s in /run/user/*/pulse/native; do
    [ -S "$s" ] || continue
    o=${s#/run/user/}; o=${o%%/*}
    [ "$o" = "$u" ] && continue
    hascard "$o" && { echo "$s"; return; }
  done
}
# r409: setlink SOCKET (or nothing): the link, the server's cookie, and root's ALSA default.
#  - PipeWire lets root in; PulseAudio (Debian's Xfce, MATE ...) answers "Access denied" without
#    its owner's cookie: a copy of it is kept for the readers (PULSE_COOKIE in their units).
#  - Speakup's espeakup and eSpeak NG play through ALSA, not PulseAudio. While a sound server has
#    the card, ALSA's "sysdefault" is busy ("Device or resource busy"): root's ALSA default is
#    then the sound server (ALSA's pulse plug-in), and the card itself only while nobody has it.
setlink() {
  if [ -n "$1" ]; then
    ln -sfn "$1" $link
    uid=${1#/run/user/}; uid=${uid%%/*}; home=$(getent passwd "$uid" | cut -d: -f6)
    if [ -n "$home" ] && [ -f "$home/.config/pulse/cookie" ]; then
      ( umask 077; cat "$home/.config/pulse/cookie" > $link.cookie.new ) && mv -f $link.cookie.new /run/charon-pulse.cookie
    else rm -f /run/charon-pulse.cookie; fi
  else rm -f $link /run/charon-pulse.cookie; fi
  a=/root/.asoundrc
  if [ ! -f $a ] || grep -q 'Written by Charon' $a; then
    plug=no; for f in /usr/lib/*/alsa-lib/libasound_module_pcm_pulse.so /usr/lib/alsa-lib/libasound_module_pcm_pulse.so /usr/lib64/alsa-lib/libasound_module_pcm_pulse.so; do [ -f "$f" ] && plug=yes; done
    if [ -n "$1" ] && [ $plug = yes ]; then
      printf '# Written by Charon: system services (console screen readers) play through the signed-in user'"'"'s sound server.\npcm.!default { type pulse server "unix:/run/charon-pulse.sock" }\nctl.!default { type hw card 0 }\n' > $a.new.$$
    else
      printf '# Written by Charon: system services (console screen readers) play straight to the sound card.\npcm.!default { type plug slave.pcm "sysdefault" }\nctl.!default { type hw card 0 }\n' > $a.new.$$
    fi
    mv -f $a.new.$$ $a   # r410: one name per process (the watcher and a reader's start wrote at once: "mv: cannot stat /root/.asoundrc.new")
  fi
}
# someone's session is up or coming up (its sound server will want the card)
session() { for d in /run/user/[0-9]*; do [ -d "$d" ] && return 0; done; return 1; }
# r410: is a sound server about to take the card? Only the session in front gets the card, so
# only that one is waited for: with a console's sign-in prompt in front (nobody's session) the
# reader waited 15 seconds for a server that would never get the card. Root in front: only when
# root has a sound server's socket. Without logind's answer: as before, any session.
expected() {
  command -v loginctl >/dev/null 2>&1 || { session; return; }
  u=$(seatuser)
  [ -n "$u" ] && [ -d /run/user/$u ] || return 1
  [ "$u" != 0 ] || [ -S /run/user/0/pulse/native ]
}
# r410: getty ttyN. logind starts a sign-in prompt (autovt@ttyN) on a console that is switched to only when the console
# is not "busy" (logind.conf(5) NAutoVTs: "previously unused"; logind-core.c vt_is_busy(): VT_GETSTATE, i.e. the tty
# is held open by someone). A console reader that keeps the consoles open makes every one of them busy, so only
# tty1 had a prompt (r410 VM proof fp, D5). The same rule as logind's, without the busy test: consoles 2 to
# NAutoVTs, nobody's session on it, no getty or autovt there yet.
getty() {
  n=${1#tty}
  case "$n" in ''|*[!0-9]*) return 0;; esac
  max=$(busctl get-property org.freedesktop.login1 /org/freedesktop/login1 org.freedesktop.login1.Manager NAutoVTs 2>/dev/null | awk '{print $2}')
  [ "$n" -ge 2 ] && [ "$n" -le "${max:-6}" ] || return 0
  systemctl -q is-active "getty@tty$n.service" "autovt@tty$n.service" 2>/dev/null && return 0
  for s in $(loginctl list-sessions --no-legend 2>/dev/null | awk '{print $1}'); do
    [ "$(loginctl show-session "$s" -p VTNr --value 2>/dev/null)" = "$n" ] && return 0
  done
  systemctl --no-block start "autovt@tty$n.service" 2>/dev/null && echo "charon-audio-share: sign-in prompt started on tty$n (logind did not: the console reader keeps it open)"
}
case "$1" in
  sound)
    # Before a console reader starts (ExecStartPre). The session in front has a sound server
    # coming: the reader waits, up to 15 seconds, until that server has the card, and then
    # speaks through it - it must not take the card first. Nobody in front: the card itself,
    # or the sound server of a user who keeps the card (target).
    if installed && expected; then
      n=0
      while [ $n -lt 30 ]; do
        t=$(target); [ -n "$t" ] && break
        sleep 0.5; n=$((n+1))
      done
    fi
    setlink "$(target)"
    # the Go Fenrir (settings.toml): its sound driver, read at launch
    f=/etc/fenrirscreenreader/settings/settings.toml
    [ -f $f ] || exit 0
    d=alsa; [ -S $link ] && d=pulse
    sed -i '/^\[presentation\.sound\]/,/^\[/{s/^driver *=.*/driver = "'$d'"/;}' $f
    exit 0;;
  release)
    # a session is about to start its sound server: free the card for it
    [ -L $link ] && exit 0
    # no sound server installed (a console-only system): nobody will ask for the card
    installed || exit 0
    for s in $readers; do
      systemctl is-active -q $s.service || continue
      echo $s >> /run/charon-audio-stopped
      systemctl --no-block stop $s.service 2>/dev/null; systemctl kill -s KILL $s.service 2>/dev/null
    done
    quiet
    exit 0;;
  watch)
    last=none; vt=; held=3
    while :; do
      t=$(target)
      if [ "$t" != "$last" ]; then
        setlink "$t"
        if [ -f /run/charon-audio-stopped ]; then
          for s in $(sort -u /run/charon-audio-stopped); do systemctl start $s.service 2>/dev/null; done
          rm -f /run/charon-audio-stopped
        fi
        [ "$last" != none ] && restart
        last=$t
      elif [ -f /run/charon-audio-stopped ] && [ $(( $(date +%s) - $(stat -c %Y /run/charon-audio-stopped) )) -gt 20 ]; then
        # no sound server took the card after all: the console reader takes it again
        for s in $(sort -u /run/charon-audio-stopped); do systemctl start $s.service 2>/dev/null; done
        rm -f /run/charon-audio-stopped
      fi
      # r410 (known issue 3, agent 1): back from a text console to the graphical sign-in screen, the greeter's sound
      # server may open the card again (logind gave its ACL back) but finds it busy: root's Speech Dispatcher, which
      # spoke for the console reader straight to the card, keeps the PCM open between messages (speechd
      # src/audio/alsa.c), and PipeWire retries a busy card only when a /dev/snd node is closed or its attributes
      # change (spa/plugins/alsa/alsa-udev.c). target() then stays "" for ever and the greeter's Orca is silent.
      # So: the session in front may open the card, has a sound server, but does not hold the card - root's
      # speech is stopped (quiet) after 2 s, again every 10 s while that lasts; the card closes, the server takes it,
      # target() names its socket, and the readers are restarted through it (above).
      # r410 (agent 1, VMware s7b): back on GDM's greeter, its WirePlumber had probed the card while root's espeakup
      # still played to it: the playback side was busy, so only the profile "input:analog-stereo" was offered and
      # chosen (no output profile, default sink = Dummy Output). hascard() is then true (the server holds the card's
      # control and capture devices), the readers go through that server, the card's playback side is free - and
      # Orca and Speakup are silent for good. So: the session in front has the card, its PipeWire has no ALSA output
      # and nobody holds a playback PCM: its WirePlumber is restarted (it probes the card again and now finds the
      # output), at most once in 20 seconds.
      # r410 (s7 RUN E): ONCE per return of that session to the front (it looped every 20 s: restarting WirePlumber
      # never let the sink settle and the session's speech streams stayed in state "init").
      u=$(seatuser)
      { [ -n "$u" ] && [ "$u" != 0 ]; } || reprobedfor=   # a console in front: the next return is a new one
      if [ -n "$t" ]; then
        rp=$(( ${rp:-0} + 1 ))
        front="$u:$a"
        if [ -n "$u" ] && [ "$u" != 0 ] && [ "$t" = /run/user/$u/pulse/native ] && [ "${reprobedfor:-}" != "$front" ] && [ $((rp % 5)) = 3 ] && ! playing && ! hassink "$u"; then
          reprobedfor=$front
          # (proven in VMware, agent 1 RUN H: Orca heard after 6 of 6 returns with this restart alone)
          reprobe "$u" && echo "charon-audio-share: the sound server of the session in front (uid $u) listed no output on the sound card; its WirePlumber was restarted to look at the card again"
        fi
      else rp=0; fi
      if [ -z "$t" ]; then
        u=$(seatuser)
        if [ -n "$u" ] && [ "$u" != 0 ] && [ -S /run/user/$u/pulse/native ] && mayopen "$u" && ! hascard "$u"; then
          freewait=$((${freewait:-0}+1))
          [ $((freewait % 10)) = 2 ] && { quiet; echo "charon-audio-share: root's speech stopped so that the session in front (uid $u) can take the sound card"; }
        else freewait=0; fi
      else freewait=0; fi
      # r410: a sign-in prompt on the console that comes in front (see getty below)
      # (after 3 seconds in front: a display manager that switches to a console for a new session registers it first)
      a=$(cat /sys/class/tty/tty0/active 2>/dev/null)
      if [ "$a" != "$vt" ]; then vt=$a; held=0
      elif [ "$held" -lt 3 ]; then held=$((held+1)); [ $held = 3 ] && getty "$a"; fi
      # r410: every second (it was two): the way also changes when another console comes in front
      sleep 1
    done;;
esac
CHARONEOF
  chmod 755 /usr/local/sbin/charon-audio-share
  printf '[Unit]\nDescription=Charon: the console reader and the desktop share the sound card\nAfter=systemd-logind.service\n\n[Service]\nExecStart=/usr/local/sbin/charon-audio-share watch\nRestart=always\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-audio-share.service
  mkdir -p /etc/systemd/system/user@.service.d
  printf '[Service]\nExecStartPre=-+/usr/local/sbin/charon-audio-share release\n' > /etc/systemd/system/user@.service.d/10-charon-audio.conf
  for s in espeakup fenrirscreenreader fenrir; do
    mkdir -p /etc/systemd/system/$s.service.d
    # HOME: a system service has none, and root's ALSA default (above) is found through it
    printf '[Service]\nEnvironment=HOME=/root\nEnvironment=PULSE_SERVER=unix:/run/charon-pulse.sock\nEnvironment=PULSE_COOKIE=/run/charon-pulse.cookie\nExecStartPre=-/usr/local/sbin/charon-audio-share sound\nTimeoutStartSec=120\nTimeoutStopSec=3\n' > /etc/systemd/system/$s.service.d/charon-audio.conf
  done
  # speech-dispatcher (Fenrir's and Orca's voice): the sound server first, the card itself otherwise.
  if [ -f /etc/speech-dispatcher/speechd.conf ]; then
    if grep -q '^ *AudioOutputMethod' /etc/speech-dispatcher/speechd.conf; then sed -i 's/^ *AudioOutputMethod .*/AudioOutputMethod "pulse,alsa"/' /etc/speech-dispatcher/speechd.conf
    else printf '\n# Written by Charon: the sound server first, the sound card itself otherwise.\nAudioOutputMethod "pulse,alsa"\n' >> /etc/speech-dispatcher/speechd.conf; fi
  fi
  systemctl enable charon-audio-share.service >>$PLOG 2>&1 || problem "charon-audio-share.service could not be switched on: the console reader and the desktop may fight over the sound card"
  /usr/local/sbin/charon-audio-share sound
  [ $running = yes ] && { systemctl daemon-reload; systemctl restart charon-audio-share.service; } >/dev/null 2>&1
fi
# A fresh system often starts with the sound card muted: unmute before speech starts.
if [ "$C" != none ] || [ "$G" != none ] || has emacspeak; then
  command -v amixer >/dev/null 2>&1 || get alsa-utils || problem "alsa-utils could not be installed: a muted sound card stays muted"
  printf '[Unit]\nDescription=Charon: unmute the sound card for speech\nAfter=sound.target\nBefore=espeakup.service fenrirscreenreader.service fenrir.service\n\n[Service]\nType=oneshot\nExecStart=/usr/local/sbin/charon-unmute\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-unmute.service
  mkdir -p /usr/local/sbin
  printf '#!/bin/sh\nfor c in Master PCM Speaker Headphone; do amixer -q sset $c 80%% unmute 2>/dev/null; done\nexit 0\n' > /usr/local/sbin/charon-unmute
  chmod 755 /usr/local/sbin/charon-unmute
  systemctl enable charon-unmute.service 2>/dev/null
fi
# --- what is still missing (no network at the first start): tried again at every start ---
miss=''
has speakup && ! command -v espeakup >/dev/null 2>&1 && miss="$miss espeakup"
has fenrir && [ "$fenrirbuild" != yes ] && ! command -v fenrir >/dev/null 2>&1 && miss="$miss fenrir"
has emacspeak && ! command -v emacspeak >/dev/null 2>&1 && miss="$miss emacspeak"
[ "$G" != none ] && ! command -v orca >/dev/null 2>&1 && miss="$miss orca"
[ $i38miss = yes ] && miss="$miss i38"
[ "$nouser" = yes ] && miss="$miss emacspeak-user"
if [ -d /etc/systemd/system ] && [ -f /var/lib/charon/speech.sh ]; then
  if [ -n "$miss" ]; then
    echo "charon: still missing:$miss; trying again at the next start with the network"
    printf '[Unit]\nDescription=Charon: finish installing the screen readers\nWants=network-online.target\nAfter=network-online.target\n\n[Service]\nType=oneshot\nExecStart=/bin/sh -c "/bin/sh /var/lib/charon/speech.sh %s %s %s %s >> /var/lib/charon/speech.log 2>&1"\nTimeoutStartSec=1800\n\n[Install]\nWantedBy=multi-user.target\n' "$C" "$G" "$L" "$M" > /etc/systemd/system/charon-speech.service
    systemctl enable charon-speech.service >/dev/null 2>&1
  elif [ -f /etc/systemd/system/charon-speech.service ]; then
    systemctl disable charon-speech.service >/dev/null 2>&1; rm -f /etc/systemd/system/charon-speech.service
    echo "charon: every chosen reader is installed"
  fi
fi
# --- a running system (not an installer's chroot): speech starts now, not at the next start ---
if [ $running = yes ]; then
  systemctl daemon-reload >/dev/null 2>&1
  [ -f /etc/modules-load.d/charon-speakup.conf ] && { modprobe speakup_soft 2>>$PLOG || problem "the kernel module speakup_soft could not be loaded: Speakup stays silent until the next start (a new kernel was installed), or this kernel has no Speakup"; }
  systemctl start charon-unmute.service 2>/dev/null
  if systemctl is-enabled espeakup >/dev/null 2>&1; then systemctl restart espeakup >>$PLOG 2>&1 || problem "espeakup did not start (systemctl status espeakup)"; fi
  # restart, not start: Debian's package started Fenrir before its language and sound were set above
  for s in fenrirscreenreader fenrir; do if systemctl is-enabled $s >/dev/null 2>&1; then systemctl restart $s >>$PLOG 2>&1 || problem "Fenrir did not start (systemctl status $s)"; fi; done
fi
# --- r409: what could not be done is said, not hidden ---
# In the log; at every sign-in on a console (a console reader reads it) until a later run
# succeeds; and aloud now, when this is a running system and firstboot.sh is not the one to say it.
printf '# Written by Charon: what its speech set-up could not do, until it is put right.\nif [ -s /var/lib/charon/speech.problems ] && [ -z "$CHARON_PROBLEMS_SHOWN" ]; then export CHARON_PROBLEMS_SHOWN=1; echo "Charon: not everything for speech could be set up:"; cat /var/lib/charon/speech.problems; echo "Again: sudo sh /var/lib/charon/speech.sh $(cat /var/lib/charon/speech.args 2>/dev/null). Log: /var/lib/charon/speech.log"; fi\n' > /etc/profile.d/charon-problems.sh
if [ -s $PROBLEMS ]; then
  echo "charon: $(wc -l < $PROBLEMS) problem(s):"; sed 's/^/charon:   /' $PROBLEMS
  if [ $running = yes ] && [ -z "$CHARON_QUIET" ] && command -v charon_say >/dev/null 2>&1; then
    charon_tone 300 200 0 60 220 300
    charon_say "nie wszystko dla mowy udało się zainstalować. Problemów: $(wc -l < $PROBLEMS). Pierwszy: $(head -n1 $PROBLEMS). Lista jest w pliku /var/lib/charon/speech.problems." \
               "not everything for speech could be installed. Problems: $(wc -l < $PROBLEMS). The first: $(head -n1 $PROBLEMS). The list is in the file /var/lib/charon/speech.problems."
  fi
else
  echo "charon: done, no problems"
fi
exit 0
