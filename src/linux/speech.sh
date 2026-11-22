#!/bin/sh
# Written by Charon: screen readers in the installed system.
#   speech.sh CONSOLE GUI LIST LOGIN
# CONSOLE speakup|fenrir|emacspeak|none, GUI orca|cthulhu|none, LIST the chosen readers with
# commas (the word i38 in it: the desktop "i3 with I38" was chosen), LOGIN graphical|console.
C=${1:-speakup}; G=${2:-orca}; L=${3:-$C,$G}; M=${4:-graphical}
has() { case ",$L," in *",$1,"*) return 0;; esac; return 1; }
export DEBIAN_FRONTEND=noninteractive
pm=none; command -v apt-get >/dev/null 2>&1 && pm=apt; command -v pacman >/dev/null 2>&1 && pm=pacman; command -v dnf >/dev/null 2>&1 && pm=dnf
# r406: with Charon's progress tones when its sound library is there (CHARON_P0..CHARON_P1 percent).
[ -f /var/lib/charon/progress.sh ] && { chmod 755 /var/lib/charon/charon-tone 2>/dev/null; . /var/lib/charon/progress.sh; }
tones=no; command -v charon_apt >/dev/null 2>&1 && tones=yes
P0=${CHARON_P0:-0}; P1=${CHARON_P1:-100}
get() {
  case $pm in
    apt) if [ $tones = yes ]; then charon_apt $P0 $P1 install -y "$@" || { apt-get update >/dev/null 2>&1; charon_apt $P0 $P1 install -y "$@"; }
         else apt-get install -y "$@" >/dev/null 2>&1 || { apt-get update >/dev/null 2>&1; apt-get install -y "$@" >/dev/null 2>&1; }; fi;;
    pacman) if [ $tones = yes ]; then charon_pacman $P0 $P1 -S --needed "$@"; else pacman -S --needed --noconfirm "$@" >/dev/null 2>&1; fi;;
    dnf) dnf install -y "$@" >/dev/null 2>&1;;
    *) false;;
  esac
}
# Arch: Fenrir and Cthulhu come from the Stormux repository (signed; the key, the stanza and its
# place before [core] exactly as published on https://stormux.org). Arch itself has neither.
# Standing before [core], the repository also replaces Arch's espeak-ng, yay and python-xlib
# with its own builds at the next upgrade: that is how Stormux means it to be used.
stormux() {
  [ $pm = pacman ] || return 1
  grep -q '^\[stormux\]' /etc/pacman.conf && return 0
  curl -fsSL -o /tmp/stormux_repo.pub https://packages.stormux.org/stormux_repo.pub || return 1
  pacman-key --add /tmp/stormux_repo.pub >/dev/null 2>&1 && pacman-key --lsign-key 52ADA49000F1FF0456F8AEEFB4CDE1CD56EF8E82 >/dev/null 2>&1 || return 1
  sed -i '/^\[core\]/i [stormux]\nSigLevel = Required DatabaseOptional\nServer = https://packages.stormux.org/$arch\n' /etc/pacman.conf
  pacman -Sy >/dev/null 2>&1
}
LANGV=$(sed -n 's/^LANG="\{0,1\}\([a-z][a-z]\)[_.].*/\1/p' /etc/default/locale /etc/locale.conf 2>/dev/null | head -n1); [ -n "$LANGV" ] || LANGV=en
echo "charon: readers console=$C desktop=$G chosen=$L language=$LANGV"
mkdir -p /var/lib/charon; echo "$G" > /var/lib/charon/gui-reader
# --- r408: i3 with I38, Storm Dragon's accessible set-up of the i3 window manager ---
# https://git.stormux.org/storm/I38 (GPL 3). Here: i3 itself, what I38's generator asks for
# (its README, "Requirements") and what its default configuration uses (sounds, notifications,
# a terminal, a file manager, an editor, a browser). The configuration is written further down.
i38=no
if has i38 && [ -z "$CHARON_DESKTOP_LATER" ]; then
  i38=yes
  case $pm in
    apt)
      get i3-wm xorg xinit git dialog jq python3 python3-gi gir1.2-gtk-3.0 gir1.2-atk-1.0 python3-i3ipc rsync yad xdotool xdg-utils discount lxterminal || echo "charon: I38: not everything it requires could be installed"
      get sox xclip libnotify-bin notification-daemon python3-pyudev pcmanfm mousepad || echo "charon: I38: some optional programs could not be installed"
      command -v firefox >/dev/null 2>&1 || command -v epiphany >/dev/null 2>&1 || get firefox-esr || get epiphany-browser || echo "charon: I38: no web browser could be installed";;
    pacman)
      get i3-wm xorg-server xorg-xinit git dialog jq python python-gobject gtk3 python-i3ipc rsync yad xdotool xdg-utils discount lxterminal || echo "charon: I38: not everything it requires could be installed"
      get sox xclip libnotify notification-daemon python-pyudev pcmanfm mousepad firefox || echo "charon: I38: some optional programs could not be installed";;
    *) i38=no; echo "charon: I38 is not set up on this system: Charon has no package list for it";;
  esac
  # A graphical sign-in needs a display manager; one that speaks (LightDM with its GTK greeter, below).
  if [ $i38 = yes ] && [ "$M" != console ] && [ ! -e /etc/systemd/system/display-manager.service ]; then
    get lightdm lightdm-gtk-greeter
    [ -e /etc/systemd/system/display-manager.service ] || systemctl enable lightdm >/dev/null 2>&1
  fi
fi
# --- the desktop -------------------------------------------------------------
O=false; [ "$G" = orca ] && O=true
if [ "$G" != none ]; then
  command -v orca >/dev/null 2>&1 || get orca
  # --- r408: the whole accessibility stack of each desktop, not the reader alone ---
  # AT-SPI for GTK and Qt programs, speech (Speech Dispatcher with eSpeak NG), braille (BRLTTY
  # with BrlAPI, liblouis), event sounds (libcanberra), high-contrast themes; per desktop a
  # magnifier and an on-screen keyboard where they are packaged apart from the desktop (GNOME
  # has both built in), its terminal and its text editor; Firefox as the browser.
  # The names are those of Debian 13, Ubuntu 26.04 and Arch (checked against their package
  # indexes); one that cannot be installed is logged and does not stop the others.
  # Not packaged: a magnifier for MATE on Arch (magnus), any magnifier for LXQt and LXDE.
  have() { command -v "$1" >/dev/null 2>&1; }
  one() { case $pm in apt) apt-get -o DPkg::Lock::Timeout=900 install -y "$1" >/dev/null 2>&1;; pacman) pacman -S --needed --noconfirm "$1" >/dev/null 2>&1;; dnf) dnf install -y "$1" >/dev/null 2>&1;; *) false;; esac; }
  each() { for p in "$@"; do one "$p" || echo "charon: accessibility: $p could not be installed"; done; }
  all() { get "$@" || each "$@"; }
  plasma=no; { have startplasma-x11 || have startplasma-wayland; } && plasma=yes
  case $pm in
    apt)
      all at-spi2-core libatk-adaptor speech-dispatcher speech-dispatcher-espeak-ng espeak-ng espeak-ng-data brltty xbrlapi python3-brlapi python3-louis liblouis-data libcanberra-gtk3-module sound-theme-freedesktop gnome-accessibility-themes
      have mate-session && all magnus onboard mate-terminal pluma
      have xfce4-session && all onboard xfce4-terminal mousepad
      # Plasma: KMag, KMouseTool; its X11 session, which Orca needs where Plasma's Wayland session does not hand it the keyboard yet
      # (Debian 13: kwin-x11; Ubuntu 26.04: kwin-x11 and plasma-session-x11; Arch has neither package any more)
      [ $plasma = yes ] && { all kmag kmousetool konsole kate; each kwin-x11; grep -qi ubuntu /etc/os-release 2>/dev/null && each plasma-session-x11; }
      have cinnamon-session && all onboard gnome-terminal xed
      { have startlxqt || have startlxde; } && all onboard
      # Debian's Firefox; Ubuntu's desktops bring their own (a snap, which an installer's chroot cannot install)
      have firefox || have firefox-esr || grep -qi ubuntu /etc/os-release 2>/dev/null || each firefox-esr;;
    pacman)
      all at-spi2-core speech-dispatcher espeak-ng brltty liblouis libcanberra sound-theme-freedesktop gnome-themes-extra firefox
      have mate-session && all onboard mate-terminal pluma mate-applets
      have xfce4-session && all onboard xfce4-terminal mousepad
      [ $plasma = yes ] && all kmag kmousetool konsole kate dolphin
      have cinnamon-session && all onboard gnome-terminal xed
      have startlxqt && all onboard featherpad openbox;;
    dnf) all at-spi2-core speech-dispatcher espeak-ng brltty liblouis python3-brlapi python3-louis;;
  esac
  mkdir -p /etc/dconf/profile /etc/dconf/db/local.d /etc/dconf/db/gdm.d /etc/xdg/autostart
  printf 'user-db:user\nsystem-db:local\n' > /etc/dconf/profile/user
  # GNOME, MATE and Cinnamon: programs offer their contents to the reader (toolkit accessibility)
  # and the desktop starts Orca itself; MATE's session does so through applications/at/visual.
  printf '[org/gnome/desktop/a11y/applications]\nscreen-reader-enabled=%s\n[org/gnome/desktop/interface]\ntoolkit-accessibility=true\n[org/mate/desktop/interface]\naccessibility=true\n[org/mate/desktop/applications/at]\nscreen-reader-enabled=%s\n[org/mate/desktop/applications/at/visual]\nstartup=%s\n[org/cinnamon/desktop/a11y/applications]\nscreen-reader-enabled=%s\n[org/cinnamon/desktop/interface]\ntoolkit-accessibility=true\n' $O $O $O $O > /etc/dconf/db/local.d/00-charon-speech
  printf 'user-db:user\nsystem-db:gdm\nfile-db:/usr/share/gdm/greeter-dconf-defaults\n' > /etc/dconf/profile/gdm
  printf '[org/gnome/desktop/a11y/applications]\nscreen-reader-enabled=true\n[org/gnome/desktop/interface]\ntoolkit-accessibility=true\n' > /etc/dconf/db/gdm.d/00-charon-speech
  command -v dconf >/dev/null && dconf update
  # r406: programs must offer their contents to the reader from the first one started:
  # Qt asks for it only through these variables.
  if ! grep -q '^QT_LINUX_ACCESSIBILITY_ALWAYS_ON=' /etc/environment 2>/dev/null; then
    printf 'QT_ACCESSIBILITY=1\nQT_LINUX_ACCESSIBILITY_ALWAYS_ON=1\nGNOME_ACCESSIBILITY=1\n' >> /etc/environment
  fi
  # The reader's language on the desktop (speech-dispatcher speaks English otherwise).
  [ -f /etc/speech-dispatcher/speechd.conf ] && sed -i "s/^#* *DefaultLanguage .*/DefaultLanguage \"$LANGV\"/" /etc/speech-dispatcher/speechd.conf
  # --- the login screen must speak too ---
  # SDDM (Plasma, LXQt) has no screen reader support at all: a blind person cannot
  # sign in there. LightDM with its GTK greeter takes its place; GDM stays when present.
  havedm() { [ -f /usr/lib/systemd/system/$1.service ] || [ -f /lib/systemd/system/$1.service ]; }
  if havedm sddm && ! havedm gdm && ! havedm gdm3; then
    havedm lightdm || get lightdm lightdm-gtk-greeter
    if havedm lightdm; then
      [ -f /etc/X11/default-display-manager ] && echo /usr/sbin/lightdm > /etc/X11/default-display-manager
      systemctl disable sddm >/dev/null 2>&1
      rm -f /etc/systemd/system/display-manager.service
      systemctl enable lightdm >/dev/null 2>&1
      # (a unit without an [Install] section "enables" without making the link)
      [ -e /etc/systemd/system/display-manager.service ] || ln -sf /usr/lib/systemd/system/lightdm.service /etc/systemd/system/display-manager.service
      echo "charon: LightDM replaces SDDM, which cannot speak"
    else echo "charon: SDDM stays; the login screen will be silent"; fi
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
  if [ "$G" = cthulhu ]; then
    printf '[Desktop Entry]\nType=Application\nName=Charon: Cthulhu\nExec=sh -c "pgrep -x cthulhu >/dev/null || pgrep -x orca >/dev/null || { command -v cthulhu >/dev/null && exec cthulhu; exec orca; }"\nNoDisplay=true\nX-GNOME-AutoRestart=false\n' > /etc/xdg/autostart/charon-reader.desktop
  else
    printf '[Desktop Entry]\nType=Application\nName=Charon: Orca\nExec=sh -c "sleep 4; pgrep -x orca >/dev/null || exec orca"\nNotShowIn=GNOME;\nNoDisplay=true\n' > /etc/xdg/autostart/charon-reader.desktop
  fi
fi
if has cthulhu && [ "$G" != none ] && ! command -v cthulhu >/dev/null 2>&1; then
  if stormux && get cthulhu; then echo "charon: cthulhu from the Stormux repository"
  else
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
if command -v apt-get >/dev/null; then apt-get -o DPkg::Lock::Timeout=900 install -y git curl ca-certificates bash golang-go gcc pkg-config libgtk-3-dev libatspi2.0-dev libglib2.0-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libx11-dev libxtst-dev libxi-dev libxfixes-dev libxres-dev libxkbcommon-dev liblouis-dev libbrlapi-dev speech-dispatcher at-spi2-core gstreamer1.0-plugins-base gstreamer1.0-plugins-good xclip || exit 1
elif command -v pacman >/dev/null; then pacman -S --needed --noconfirm git curl go gcc pkgconf gtk3 gdk-pixbuf2 at-spi2-core glib2 gstreamer gst-plugins-base gst-plugins-good libx11 libxtst libxi libxfixes libxres libxkbcommon liblouis brltty speech-dispatcher xclip || exit 1
elif command -v dnf >/dev/null; then dnf install -y git curl golang gcc pkgconf-pkg-config gtk3-devel at-spi2-core-devel glib2-devel gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-good libX11-devel libXtst-devel libXi-devel libXfixes-devel libXres-devel libxkbcommon-devel liblouis-devel brlapi-devel speech-dispatcher xclip || exit 1
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
    printf '[Unit]\nDescription=Charon: build the Cthulhu screen reader once\nWants=network-online.target\nAfter=network-online.target\n\n[Service]\nType=oneshot\nExecStart=/usr/local/sbin/charon-cthulhu-build\nTimeoutStartSec=3600\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-cthulhu-build.service
    systemctl enable charon-cthulhu-build.service 2>/dev/null
    echo "charon: cthulhu is built at the first start with the network"
  fi
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
  if su - "$u" -c "if [ -d ~/I38/.git ]; then git -C ~/I38 pull -q --ff-only; else git clone -q --depth 1 https://git.stormux.org/storm/I38.git ~/I38; fi; cd ~/I38 && ./i38.sh $flag" </dev/null; then
    # Orca was chosen to speak, Cthulhu is installed as well: I38's default would start Cthulhu.
    if [ "$reader" = orca ] && command -v orca >/dev/null 2>&1 && grep -q '^exec .*/cthulhu$' "$h/.config/i3/config" 2>/dev/null; then
      sed -i "s|^exec .*/cthulhu\$|exec $(command -v orca)|" "$h/.config/i3/config"; chown --reference="$h" "$h/.config/i3/config"
    fi
    echo "charon: I38 written for $u ($flag)"
  else r=1; echo "charon: I38 could not be written for $u"; fi
done
[ $n -gt 0 ] || { echo "charon: I38: no user yet"; r=1; }
exit $r
CHARONEOF
  chmod 755 /usr/local/sbin/charon-i38
  /usr/local/sbin/charon-i38 || i38miss=yes
fi
# --- r406: signing in from the console, the desktop started with startx ---
if [ "$M" = console ]; then
  session=
  for s in mate-session startxfce4 cinnamon-session startplasma-x11 startlxqt startlxde startplasma-wayland gnome-session i3; do command -v $s >/dev/null 2>&1 && { session=$s; break; }; done
  if [ -n "$session" ]; then
    command -v startx >/dev/null 2>&1 || get xinit || get xorg-xinit
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
  command -v espeakup >/dev/null 2>&1 || get espeakup
  # Ubuntu Server has speakup neither built in nor switched on: its modules come
  # in linux-modules-extra, which a minimal server does not always install.
  for k in /lib/modules/*; do [ -d "$k" ] || continue; v=${k##*/}
    find "$k" -name 'speakup_soft*' 2>/dev/null | grep -q . || { [ $pm = apt ] && apt-get install -y linux-modules-extra-$v >/dev/null 2>&1; }
  done
  # The language of the console voice: espeakup's service reads default_voice (Debian and Ubuntu
  # from /etc/default/espeakup, which would override a drop-in; Arch from the unit's Environment).
  if [ -f /etc/default/espeakup ]; then
    if grep -q '^default_voice=' /etc/default/espeakup; then sed -i "s/^default_voice=.*/default_voice=$LANGV/" /etc/default/espeakup; else printf 'default_voice=%s\n' $LANGV >> /etc/default/espeakup; fi
  fi
  mkdir -p /etc/systemd/system/espeakup.service.d
  printf '[Service]\nEnvironment=default_voice=%s\n' $LANGV > /etc/systemd/system/espeakup.service.d/charon-voice.conf
fi
if has fenrir; then
  # Debian and Ubuntu have Fenrir in their own repositories (its service: fenrir.service).
  # Arch has no package of its own: Fenrir comes from the Stormux repository there (its
  # service: fenrirscreenreader.service). Fedora has no Fenrir package at all.
  if ! command -v fenrir >/dev/null 2>&1; then
    case $pm in
      apt) get fenrir;;
      pacman) get fenrir || { stormux && get fenrir; };;
      *) false;;
    esac || echo "charon: fenrir could not be installed (no package for this system, or no network)"
  fi
  # Fenrir speaks through Speech Dispatcher (its default speech driver), here with eSpeak NG;
  # on Debian and Ubuntu its sounds are played by sox. The fenrir packages only recommend or
  # suggest these, so they are named.
  if command -v fenrir >/dev/null 2>&1; then
    case $pm in
      apt) get speech-dispatcher speech-dispatcher-espeak-ng espeak-ng python3-speechd sox || echo "charon: fenrir: speech-dispatcher, python3-speechd or sox could not be installed";;
      pacman) get speech-dispatcher espeak-ng || echo "charon: fenrir: speech-dispatcher or espeak-ng could not be installed";;
    esac
  fi
  [ -f /etc/speech-dispatcher/speechd.conf ] && sed -i "s/^#* *DefaultLanguage .*/DefaultLanguage \"$LANGV\"/" /etc/speech-dispatcher/speechd.conf
fi
if has emacspeak; then
  command -v emacspeak >/dev/null 2>&1 || get emacspeak emacspeak-espeak-server || get emacspeak || echo "charon: emacspeak could not be installed"
  command -v espeak-ng >/dev/null 2>&1 || get espeak-ng
fi
# fenrirunit enable|disable: Fenrir's service under either name. Debian's and Ubuntu's packages
# switch their service on and start it when they are installed, so a reader that is to stay
# silent is also stopped (in a running system; an installer's chroot has nothing running).
fenrirunit() {
  fr=1
  for s in fenrirscreenreader fenrir; do
    systemctl $1 $s.service >/dev/null 2>&1 || continue
    fr=0
    if [ $1 = disable ] && [ -d /run/systemd/system ]; then systemctl stop $s.service >/dev/null 2>&1; fi
  done
  return $fr
}
espeakupoff() { systemctl disable espeakup >/dev/null 2>&1; if [ -d /run/systemd/system ]; then systemctl stop espeakup >/dev/null 2>&1; fi; }
if [ "$C" = speakup ]; then
  echo speakup_soft > /etc/modules-load.d/charon-speakup.conf
  fenrirunit disable; systemctl enable espeakup 2>/dev/null
elif [ "$C" = fenrir ]; then
  rm -f /etc/modules-load.d/charon-speakup.conf; espeakupoff
  fenrirunit enable || echo "charon: no fenrir service found (fenrir.service, fenrirscreenreader.service): Fenrir will not speak"
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
    echo "charon: emacspeak not set up (no user or no emacspeak)"
    # An installer that makes the user only at the first start (Ubuntu's): tried again then (below).
    [ -z "$U" ] && command -v emacspeak >/dev/null 2>&1 && nouser=yes
  fi
fi
# --- r406: the console reader and the desktop share the sound card ---
# A console reader runs as a system service and plays straight to the sound card; a
# desktop's sound server (PipeWire or PulseAudio of the signed-in user) wants the same
# card for itself, and whoever comes second stays silent. So the console reader is sent
# through the sound server of whoever is signed in (root may use any user's socket), and
# plays to the card itself only while nobody is. A small watcher keeps the link
# /run/charon-pulse.sock on the right socket and restarts the console reader when it
# changes; before a user's session starts, the console reader lets go of the card.
if [ "$C" != none ] && [ -d /etc/systemd/system ]; then
  # A desktop brings a package that makes PipeWire the default ALSA output for everybody.
  # A system service has no PipeWire of its own, so the console reader then got "Host is
  # down" and stayed silent (seen: espeakup on Arch with Xfce). Root's own ALSA default
  # goes straight to the sound card; users' programs are not touched.
  if [ ! -f /root/.asoundrc ] || grep -q 'Written by Charon' /root/.asoundrc; then
    printf '# Written by Charon: system services (console screen readers) play straight to the sound card.\npcm.!default { type plug slave.pcm "sysdefault" }\nctl.!default { type hw card 0 }\n' > /root/.asoundrc
  fi
  mkdir -p /usr/local/sbin
  cat > /usr/local/sbin/charon-audio-share <<'CHARONEOF'
#!/bin/sh
# Written by Charon: one sound card for the console reader and the desktop.
link=/run/charon-pulse.sock
readers="espeakup fenrirscreenreader fenrir"
# Nothing here may wait: a session start hangs on it. (Fenrir takes up to 90 seconds
# to stop by itself; its unit gets 3 seconds from Charon, and here it is not waited for.)
quiet() {
  pkill -KILL -u root -x speech-dispatcher 2>/dev/null
  pkill -KILL -u root -f /speech-dispatcher-modules/ 2>/dev/null
}
restart() {
  quiet
  for s in $readers; do systemctl --no-block try-restart $s.service 2>/dev/null; done
}
target() {
  # the signed-in user of the active session first, then any running sound server
  u=$(loginctl list-sessions --no-legend 2>/dev/null | while read -r id uid rest; do
        [ "$(loginctl show-session "$id" -p Active --value 2>/dev/null)" = yes ] && { echo "$uid"; break; }
      done)
  [ -n "$u" ] && [ -S /run/user/$u/pulse/native ] && { echo /run/user/$u/pulse/native; return; }
  for s in /run/user/*/pulse/native; do [ -S "$s" ] && { echo "$s"; return; }; done
}
case "$1" in
  release)
    # a session is about to start its sound server: free the card for it
    [ -L $link ] && exit 0
    # no sound server installed (a console-only system): nobody will ask for the card
    command -v pipewire >/dev/null 2>&1 || command -v pulseaudio >/dev/null 2>&1 || exit 0
    for s in $readers; do
      systemctl is-active -q $s.service || continue
      echo $s >> /run/charon-audio-stopped
      systemctl --no-block stop $s.service 2>/dev/null; systemctl kill -s KILL $s.service 2>/dev/null
    done
    quiet
    exit 0;;
  watch)
    last=none
    while :; do
      t=$(target)
      if [ "$t" != "$last" ]; then
        if [ -n "$t" ]; then ln -sfn "$t" $link; else rm -f $link; fi
        if [ -f /run/charon-audio-stopped ]; then
          for s in $(sort -u /run/charon-audio-stopped); do systemctl start $s.service 2>/dev/null; done
          rm -f /run/charon-audio-stopped
        fi
        [ "$last" != none ] && restart
        last=$t
      elif [ -f /run/charon-audio-stopped ] && [ $(( $(date +%s) - $(stat -c %Y /run/charon-audio-stopped) )) -gt 8 ]; then
        # no sound server came up after all: the console reader takes the card again
        for s in $(sort -u /run/charon-audio-stopped); do systemctl start $s.service 2>/dev/null; done
        rm -f /run/charon-audio-stopped
      fi
      sleep 2
    done;;
esac
CHARONEOF
  chmod 755 /usr/local/sbin/charon-audio-share
  printf '[Unit]\nDescription=Charon: the console reader and the desktop share the sound card\nAfter=systemd-logind.service\n\n[Service]\nExecStart=/usr/local/sbin/charon-audio-share watch\nRestart=always\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-audio-share.service
  mkdir -p /etc/systemd/system/user@.service.d
  printf '[Service]\nExecStartPre=-+/usr/local/sbin/charon-audio-share release\n' > /etc/systemd/system/user@.service.d/10-charon-audio.conf
  for s in espeakup fenrirscreenreader fenrir; do
    mkdir -p /etc/systemd/system/$s.service.d
    printf '[Service]\nEnvironment=PULSE_SERVER=unix:/run/charon-pulse.sock\nTimeoutStopSec=3\n' > /etc/systemd/system/$s.service.d/charon-audio.conf
  done
  # speech-dispatcher (Fenrir's and Orca's voice): the sound server first, the card itself otherwise.
  if [ -f /etc/speech-dispatcher/speechd.conf ]; then
    if grep -q '^ *AudioOutputMethod' /etc/speech-dispatcher/speechd.conf; then sed -i 's/^ *AudioOutputMethod .*/AudioOutputMethod "pulse,alsa"/' /etc/speech-dispatcher/speechd.conf
    else printf '\n# Written by Charon: the sound server first, the sound card itself otherwise.\nAudioOutputMethod "pulse,alsa"\n' >> /etc/speech-dispatcher/speechd.conf; fi
  fi
  systemctl enable charon-audio-share.service >/dev/null 2>&1
  [ -d /run/systemd/system ] && { systemctl daemon-reload; systemctl restart charon-audio-share.service; } >/dev/null 2>&1
fi
# A fresh system often starts with the sound card muted: unmute before speech starts.
if [ "$C" != none ] || [ "$G" != none ] || has emacspeak; then
  command -v amixer >/dev/null 2>&1 || get alsa-utils
  printf '[Unit]\nDescription=Charon: unmute the sound card for speech\nAfter=sound.target\nBefore=espeakup.service fenrirscreenreader.service fenrir.service\n\n[Service]\nType=oneshot\nExecStart=/usr/local/sbin/charon-unmute\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-unmute.service
  mkdir -p /usr/local/sbin
  printf '#!/bin/sh\nfor c in Master PCM Speaker Headphone; do amixer -q sset $c 80%% unmute 2>/dev/null; done\nexit 0\n' > /usr/local/sbin/charon-unmute
  chmod 755 /usr/local/sbin/charon-unmute
  systemctl enable charon-unmute.service 2>/dev/null
fi
# --- what is still missing (no network at the first start): tried again at every start ---
miss=''
has speakup && ! command -v espeakup >/dev/null 2>&1 && miss="$miss espeakup"
has fenrir && ! command -v fenrir >/dev/null 2>&1 && miss="$miss fenrir"
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
if [ -d /run/systemd/system ]; then
  systemctl daemon-reload >/dev/null 2>&1
  [ -f /etc/modules-load.d/charon-speakup.conf ] && modprobe speakup_soft 2>/dev/null
  systemctl start charon-unmute.service 2>/dev/null
  if systemctl is-enabled espeakup >/dev/null 2>&1; then systemctl restart espeakup 2>/dev/null; fi
  # restart, not start: Debian's package started Fenrir before its language and sound were set above
  for s in fenrirscreenreader fenrir; do systemctl is-enabled $s >/dev/null 2>&1 && systemctl restart $s 2>/dev/null; done
fi
exit 0
