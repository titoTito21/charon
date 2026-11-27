#!/bin/sh
# Charon r410: applies the answers of the detailed install mode in the installed system.
#
#   personal.sh install [now]     registers charon-personal.service (runs "apply" at the next start, after the
#                                 network; "now": also starts it at once, without waiting for it)
#   personal.sh apply             applies /var/lib/charon/answers.env; what needs the network and has none is
#                                 tried again at the next start (five starts at most)
#   personal.sh --dry-run FILE    prints what would be done for the answers in FILE; changes nothing
#
# answers.env holds only the answers that differ from the defaults (KEY='value' lines written by Charon).
# Every key has one function apply_<key>; tools/charon-install/charon_questions.py refuses to build when a
# question has no function here or a function here has no question. Each function ends with one of
#   ok TEXT / skip REASON / fail REASON / later REASON
# and the result of every key is written to /var/lib/charon/personal.report (key=status: text) and said in
# the log. Nothing is ignored in silence. GPL-2.0-or-later.
S=/var/lib/charon
A=$S/answers.env
LOG=$S/personal.log
REPORT=$S/personal.report
DRY=
ROOT=${CHARON_ROOT:-}
# r410 (es): the order is the order of work: sources and proxy first (the installs use them), the network change, the
# script and the report to an address last.
KEYS="proxy repo_mirror components u_mirror upgrade_now formats extra_langs tz_other ntp ntp_server sudo_nopw shell groups users grub_wait grub_tune boot_text sd_rate sd_pitch sd_volume sd_punct sd_caps speech_lang braille braille_table flatpak console_font contrast text_size dark sounds clock_fmt numlock sleep_ac screen_lock magnifier login_sound set_everyday set_blind set_tools set_create soft_flatpak u_codecs u_drivers help_tool popcon firewall ssh_pw ssh_key ssh_keys_admin auto_updates auto_reboot lid tlp bluetooth printer sets soft_file first_msg asset_tag root_ca net_static post_script"
# answers whose values are never written to a log or a report (said as "set")
SECRETS=""

say() { echo "charon personal: $*" >> "$LOG"; [ -n "$DRY" ] && echo "$*"; return 0; }
run() { if [ -n "$DRY" ]; then echo "RUN $*"; return 0; fi; "$@" >> "$LOG" 2>&1; }
# write FILE MODE  (content on stdin)
write() {
  if [ -n "$DRY" ]; then echo "WRITE $1 (mode $2)"; sed 's/^/    | /'; return 0; fi
  mkdir -p "$(dirname "$ROOT$1")" && cat > "$ROOT$1.charon-new" && chmod "$2" "$ROOT$1.charon-new" && mv "$ROOT$1.charon-new" "$ROOT$1"
}
result() { status=$1; shift; text=$*; }
ok() { result applied "$@"; }
skip() { result skipped "$@"; }
fail() { result failed "$@"; }
later() { result later "$@"; }

distro() {
  ID=; ID_LIKE=
  [ -r "$ROOT/etc/os-release" ] && . "$ROOT/etc/os-release"
  case " $ID $ID_LIKE " in
    *" debian "*|*" ubuntu "*) PM=apt ;;
    *" fedora "*|*" rhel "*) PM=dnf ;;
    *" arch "*) PM=pacman ;;
    *) PM=none ;;
  esac
}
online() { [ -n "$DRY" ] && return 0; getent hosts deb.debian.org archlinux.org fedoraproject.org >/dev/null 2>&1; }
have() { command -v "$1" >/dev/null 2>&1; }
# pkg NAME...   0 installed, 1 failed, 2 no network yet
pkg() {
  [ -n "$DRY" ] && { echo "INSTALL $*"; return 0; }
  online || return 2
  case $PM in
    apt) DEBIAN_FRONTEND=noninteractive apt-get -o DPkg::Lock::Timeout=900 -y install "$@" >> "$LOG" 2>&1 ;;
    dnf) dnf -y install "$@" >> "$LOG" 2>&1 ;;
    pacman) n=0; while [ -e /var/lib/pacman/db.lck ] && [ $n -lt 180 ]; do sleep 5; n=$((n+1)); done
            pacman -S --needed --noconfirm "$@" >> "$LOG" 2>&1 ;;
    *) return 1 ;;
  esac
}
need() { pkg "$@"; r=$?; [ $r = 2 ] && later "no network yet for: $*"; [ $r = 1 ] && fail "could not install: $*"; return $r; }
# conf_set FILE KEY LINE   replaces the first uncommented or commented line that sets KEY, or appends
conf_set() {
  f=$ROOT$1
  if [ -n "$DRY" ]; then echo "SET $1: $3"; return 0; fi
  [ -f "$f" ] || { mkdir -p "$(dirname "$f")"; : > "$f"; }
  if grep -Eq "^[#[:space:]]*$2([[:space:]=]|\$)" "$f"; then
    awk -v k="$2" -v l="$3" 'BEGIN{d=0} { if(!d && $0 ~ "^[#[:space:]]*" k "([[:space:]=]|$)") {print l; d=1} else print }' "$f" > "$f.charon-new" && cat "$f.charon-new" > "$f" && rm -f "$f.charon-new"
  else echo "$3" >> "$f"; fi
}
grub_regen() {
  [ -n "$DRY" ] && { echo "RUN the distribution's command that writes grub.cfg again"; return 0; }
  if have update-grub; then run update-grub
  elif have grub2-mkconfig; then run grub2-mkconfig -o /boot/grub2/grub.cfg
  elif have grub-mkconfig && [ -d /boot/grub ]; then run grub-mkconfig -o /boot/grub/grub.cfg
  else return 1; fi
}
grub_file() { if [ -d "$ROOT/etc/default/grub.d" ]; then echo /etc/default/grub.d/90-charon.cfg; else echo /etc/default/grub; fi; }
has_grub() { [ -n "$DRY" ] || [ -f /etc/default/grub ]; }
# dconf_default PATH KEY VALUE: a system-wide default for every user (GNOME and other dconf desktops)
dconf_default() {
  [ -n "$DRY" ] || have dconf || return 1
  if [ -n "$DRY" ]; then echo "DCONF [$1] $2=$3"; return 0; fi
  [ -f /etc/dconf/profile/user ] || { mkdir -p /etc/dconf/profile; printf 'user-db:user\nsystem-db:local\n' > /etc/dconf/profile/user; }
  grep -q '^system-db:local$' /etc/dconf/profile/user || echo 'system-db:local' >> /etc/dconf/profile/user
  mkdir -p /etc/dconf/db/local.d; f=/etc/dconf/db/local.d/90-charon
  ini_set "$f" "[$1]" "$2" "$3" && dconf update >> "$LOG" 2>&1
}

apply_formats() {
  loc="$1.UTF-8"
  case $PM in
    apt|pacman) [ -n "$DRY" ] || { grep -q "^$loc UTF-8" /etc/locale.gen 2>/dev/null || { sed -i "s/^# *$loc UTF-8/$loc UTF-8/" /etc/locale.gen; grep -q "^$loc UTF-8" /etc/locale.gen || echo "$loc UTF-8" >> /etc/locale.gen; }; }
                run locale-gen || { fail "locale-gen failed for $loc"; return; } ;;
    dnf) need "glibc-langpack-$(echo "$1" | cut -d_ -f1)" || return ;;
  esac
  # Debian, Ubuntu: update-locale(8) writes /etc/default/locale. localectl set-locale is refused there
  # ("Failed to issue method call: Access denied", seen as root on Debian 13 in the r410 VM proof).
  if [ "$PM" = apt ] && { [ -n "$DRY" ] || have update-locale; }; then
    run update-locale "LC_TIME=$loc" "LC_NUMERIC=$loc" "LC_MONETARY=$loc" "LC_PAPER=$loc" "LC_MEASUREMENT=$loc" && ok "formats of $loc in /etc/default/locale" || fail "update-locale failed"
    return
  fi
  run localectl set-locale "LC_TIME=$loc" "LC_NUMERIC=$loc" "LC_MONETARY=$loc" "LC_PAPER=$loc" "LC_MEASUREMENT=$loc" && ok "formats of $loc in /etc/locale.conf" || fail "localectl set-locale failed"
}
apply_ntp() { [ "$1" = no ] || { skip "default"; return; }; run timedatectl set-ntp false && ok "network time synchronisation off" || fail "timedatectl set-ntp false failed"; }
apply_sudo_nopw() {
  [ "$1" = yes ] || { skip "default"; return; }
  [ -n "$charon_user" ] || { fail "the user name is not known"; return; }
  # r410 (s6a): sudoers takes the LAST matching rule and cloud-init writes 90-cloud-init-users ("ALL"), so the file
  # sorts after it (99-...) and the effect is checked as the user before it is reported applied.
  if [ -n "$DRY" ]; then echo "WRITE /etc/sudoers.d/99-charon-nopasswd (mode 0440): $charon_user ALL=(ALL:ALL) NOPASSWD: ALL"; ok dry; return; fi
  rm -f /etc/sudoers.d/90-charon-nopasswd
  t=$(mktemp) && echo "$charon_user ALL=(ALL:ALL) NOPASSWD: ALL" > "$t" && visudo -cf "$t" >> "$LOG" 2>&1 && install -m 0440 -o root -g root "$t" /etc/sudoers.d/99-charon-nopasswd && rm -f "$t" || { rm -f "$t"; fail "the sudoers file did not pass visudo"; return; }
  if runuser -u "$charon_user" -- sudo -n true >> "$LOG" 2>&1; then ok "sudo without a password for $charon_user"; else fail "the sudoers file is in place but sudo still asks $charon_user for a password"; fi
}
apply_shell() {
  [ "$1" = bash ] && { skip "default"; return; }
  need "$1" || return
  p=$(command -v "$1" 2>/dev/null); [ -n "$DRY" ] && p=/usr/bin/$1
  [ -n "$p" ] || { fail "$1 was not found after its installation"; return; }
  run usermod -s "$p" "$charon_user" && ok "login shell $p for $charon_user" || fail "usermod -s failed"
}
apply_groups() {
  done_g=; missing=
  for g in $(echo "$1" | tr ',' ' '); do
    [ "$g" = dialout ] && [ "$PM" = pacman ] && g=uucp
    if [ -n "$DRY" ] || getent group "$g" >/dev/null; then run usermod -aG "$g" "$charon_user" && done_g="$done_g $g"; else missing="$missing $g"; fi
  done
  if [ -n "$missing" ]; then fail "added to:${done_g:- none}; this system has no group:$missing"; else ok "added to:$done_g"; fi
}
apply_grub_wait() { has_grub || { skip "this system does not start through GRUB"; return; }; conf_set "$(grub_file)" GRUB_TIMEOUT "GRUB_TIMEOUT=$1"; grub_regen && ok "GRUB_TIMEOUT=$1" || fail "the GRUB configuration could not be regenerated"; }
# r410: keep / on / off. Debian's installer, when it spoke, writes a tune of its own (/etc/default/grub.d/brltty.cfg),
# so "no tune" must also switch that one off, and the default is "as the system sets it".
apply_grub_tune() {
  [ "$1" = keep ] && { skip "default"; return; }
  has_grub || { skip "this system does not start through GRUB"; return; }
  if [ "$1" = on ]; then conf_set "$(grub_file)" GRUB_INIT_TUNE 'GRUB_INIT_TUNE="480 440 1"'
  elif [ -n "$DRY" ]; then echo "EDIT every GRUB_INIT_TUNE line of /etc/default/grub and /etc/default/grub.d/*.cfg: commented out"
  else for f in /etc/default/grub /etc/default/grub.d/*.cfg; do [ -f "$f" ] && grep -q '^GRUB_INIT_TUNE=' "$f" && sed -i 's/^GRUB_INIT_TUNE=/# (switched off by Charon) &/' "$f"; done; fi
  grub_regen && ok "GRUB_INIT_TUNE $1" || fail "the GRUB configuration could not be regenerated"
}
apply_boot_text() {
  [ "$1" = yes ] || { skip "default"; return; }
  has_grub || { skip "this system does not start through GRUB"; return; }
  if [ -n "$DRY" ]; then echo "EDIT GRUB_CMDLINE_LINUX_DEFAULT: remove quiet, splash"; ok dry; return; fi
  for f in /etc/default/grub /etc/default/grub.d/*.cfg; do [ -f "$f" ] && sed -i -E '/^GRUB_CMDLINE_LINUX(_DEFAULT)?=/ { s/(^|[" ])(quiet|splash)([" ]|$)/\1\3/g; s/(^|[" ])(quiet|splash)([" ]|$)/\1\3/g; s/  +/ /g }' "$f"; done
  have grubby && run grubby --update-kernel=ALL --remove-args="quiet splash"
  grub_regen && ok "quiet and splash removed from the kernel command line" || fail "the GRUB configuration could not be regenerated"
}
speechd() {  # KEY VALUE (already quoted when a string)
  [ -n "$DRY" ] || [ -f /etc/speech-dispatcher/speechd.conf ] || { skip "Speech Dispatcher is not installed (no desktop reader was chosen)"; return 1; }
  conf_set /etc/speech-dispatcher/speechd.conf "$1" "$1 $2" && ok "$1 $2 in /etc/speech-dispatcher/speechd.conf"
}
apply_sd_rate() { [ "$1" = charon ] && { skip "default"; return; }; speechd DefaultRate "$1"; }
apply_sd_pitch() { [ "$1" = charon ] && { skip "default"; return; }; speechd DefaultPitch "$1"; }
apply_sd_volume() { speechd DefaultVolume "$1"; }
apply_sd_punct() { [ "$1" = charon ] && { skip "default"; return; }; speechd DefaultPunctuationMode "\"$1\""; }
apply_sd_caps() { speechd DefaultCapLetRecognition "\"$1\""; }
apply_braille() {
  [ "$1" = auto ] || { skip "default"; return; }
  need brltty || return
  conf_set /etc/brltty.conf braille-driver "braille-driver auto"
  run systemctl enable brltty.service; ok "BRLTTY installed, braille-driver auto"
}
apply_braille_table() { [ "$1" = auto ] && { skip "default"; return; }; [ -n "$DRY" ] || [ -f /etc/brltty.conf ] || { skip "BRLTTY is not installed"; return; }; conf_set /etc/brltty.conf text-table "text-table $1" && ok "text-table $1"; }
apply_flatpak() {
  [ "$1" = yes ] || { skip "default"; return; }
  need flatpak || return
  online || { later "no network yet for Flathub"; return; }
  run flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo && ok "Flatpak with the Flathub repository" || fail "flatpak remote-add failed"
}
apply_console_font() {
  [ "$1" = yes ] || { skip "default"; return; }
  if [ "$PM" = apt ]; then
    [ -n "$DRY" ] || [ -f /etc/default/console-setup ] || { skip "console-setup is not installed"; return; }
    conf_set /etc/default/console-setup FONTFACE 'FONTFACE="TerminusBold"'; conf_set /etc/default/console-setup FONTSIZE 'FONTSIZE="16x32"'
    run setupcon --save; ok "console font TerminusBold 16x32"
  else
    if [ "$PM" = dnf ]; then need terminus-fonts-console || return; else need terminus-font || return; fi
    conf_set /etc/vconsole.conf FONT "FONT=ter-v32b"; ok "console font ter-v32b in /etc/vconsole.conf"
  fi
}
# ---- desktop settings: a system-wide default on every installed desktop that has the setting ----
# dconf PATHS, not schema names: MATE keeps org.mate.font-rendering, org.mate.sound and org.mate.peripherals-keyboard
# under /org/mate/desktop/ (gsettings list-schemas --print-paths on Debian 13; with the schema name as the path the
# values were written and had no effect, r410 VM proof).
# d_begin; then for each desktop: d_dconf NAME TEST PATH KEY VALUE | d_ini ... | d_none NAME TEST; then d_end.
desk() {  # is this desktop installed?
  [ -n "$DRY" ] && return 0
  case $1 in gnome) have gnome-shell ;; mate) have mate-session ;; cinnamon) have cinnamon-session || have cinnamon ;; xfce) have xfce4-session ;; kde) have plasmashell ;; esac
}
d_begin() { d_did=; d_bad=; }
d_dconf() { desk "$1" || return 0; if dconf_default "$2" "$3" "$4"; then d_did="$d_did $1"; else d_bad="$d_bad $1"; fi; }
# d_ini DESKTOP FILE SECTION KEY VALUE   (KDE: /etc/xdg/<file>, read by every user as the default)
d_ini() { desk "$1" || return 0; if ini_set "$2" "$3" "$4" "$5"; then d_did="$d_did $1"; else d_bad="$d_bad $1"; fi; }
# d_xfce PROPERTY TYPE VALUE   (the xsettings channel's system file)
d_xfce() {
  desk xfce || return 0
  f=/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xsettings.xml
  if [ -n "$DRY" ]; then echo "XFCONF $f $1=$3"; d_did="$d_did xfce"; return 0; fi
  if [ -f "$f" ] && grep -q "name=\"$1\" type=\"$2\"" "$f"; then sed -i "s|\(name=\"$1\" type=\"$2\" value=\"\)[^\"]*\"|\1$3\"|" "$f" && d_did="$d_did xfce" || d_bad="$d_bad xfce"
  else d_bad="$d_bad xfce(the property is not in $f)"; fi
}
d_end() {
  # r410: each desktop once ("for: gnome gnome" when it got two keys)
  d_did=$(for w in $d_did; do echo "$w"; done | awk '!s[$0]++' | tr '\n' ' ' | sed 's/ $//; s/^/ /'); [ "$d_did" = " " ] && d_did=
  if [ -n "$d_bad" ]; then fail "$1; set for:${d_did:- none}; could not be set for:$d_bad"
  elif [ -n "$d_did" ]; then ok "$1; set as the default for:$d_did"
  else skip "none of the installed desktops has this setting"; fi
}
# ini_set FILE SECTION KEY VALUE   ([SECTION] is matched literally, KDE's nested "[AC][SuspendAndShutdown]" too)
ini_set() {
  if [ -n "$DRY" ]; then echo "INI $1 $2 $3=$4"; return 0; fi
  f=$ROOT$1; mkdir -p "$(dirname "$f")"; touch "$f"
  awk -v s="$2" -v k="$3" -v v="$4" 'BEGIN{p=0;d=0}
    /^\[/ { if(p && !d) {print k "=" v; d=1} p=($0==s) }
    { if(p && index($0,k "=")==1) { if(!d) {print k "=" v; d=1} next } print }
    END { if(!d) { if(!p) print s; print k "=" v } }' "$f" > "$f.charon-new" && mv "$f.charon-new" "$f"
}
apply_contrast() { [ "$1" = yes ] || { skip "default"; return; }; d_begin; d_dconf gnome org/gnome/desktop/a11y/interface high-contrast true; d_end "high contrast"; }
apply_text_size() {
  case $1 in 1.25) dpi=120 ;; 1.5) dpi=144 ;; 2.0) dpi=192 ;; *) dpi=96 ;; esac
  d_begin; d_dconf gnome org/gnome/desktop/interface text-scaling-factor "$1"; d_dconf cinnamon org/cinnamon/desktop/interface text-scaling-factor "$1"
  d_dconf mate org/mate/desktop/font-rendering dpi "$dpi.0"; d_xfce DPI int "$dpi"; d_end "text size $1"
}
apply_dark() {
  [ "$1" = yes ] || { skip "default"; return; }
  d_begin; d_dconf gnome org/gnome/desktop/interface color-scheme "'prefer-dark'"; d_dconf cinnamon org/x/apps/portal color-scheme "'prefer-dark'"
  d_ini kde /etc/xdg/kdeglobals "[General]" ColorScheme BreezeDark; d_end "dark theme"
}
apply_sounds() {
  [ "$1" = no ] || { skip "default"; return; }
  d_begin; d_dconf gnome org/gnome/desktop/sound event-sounds false; d_dconf mate org/mate/desktop/sound event-sounds false; d_dconf cinnamon org/cinnamon/desktop/sound event-sounds false
  d_xfce EnableEventSounds bool false; d_ini kde /etc/xdg/kdeglobals "[Sounds]" Enable false; d_end "event sounds off"
}
apply_clock_fmt() {
  b=true; [ "$1" = 12h ] && b=false
  d_begin; d_dconf gnome org/gnome/desktop/interface clock-format "'$1'"; d_dconf cinnamon org/cinnamon/desktop/interface clock-use-24h "$b"; d_end "clock $1"
}
apply_numlock() {
  b=false; [ "$1" = on ] && b=true
  d_begin
  d_dconf gnome org/gnome/desktop/peripherals/keyboard numlock-state "$b"; d_dconf gnome org/gnome/desktop/peripherals/keyboard remember-numlock-state false
  d_dconf cinnamon org/cinnamon/desktop/peripherals/keyboard numlock-state "$b"; d_dconf cinnamon org/cinnamon/desktop/peripherals/keyboard remember-numlock-state false
  d_dconf mate org/mate/desktop/peripherals/keyboard numlock-state "'$1'"; d_dconf mate org/mate/desktop/peripherals/keyboard remember-numlock-state false
  d_end "Num Lock $1 at sign-in"
}
apply_sleep_ac() {
  sec=$(( $1 * 60 )); d_begin
  if [ "$1" = 0 ]; then d_dconf gnome org/gnome/settings-daemon/plugins/power sleep-inactive-ac-type "'nothing'"
  else d_dconf gnome org/gnome/settings-daemon/plugins/power sleep-inactive-ac-type "'suspend'"; d_dconf gnome org/gnome/settings-daemon/plugins/power sleep-inactive-ac-timeout "$sec"; fi
  d_dconf cinnamon org/cinnamon/settings-daemon/plugins/power sleep-inactive-ac-timeout "$sec"
  d_dconf mate org/mate/power-manager sleep-computer-ac "$sec"
  if [ "$1" = 0 ]; then d_ini kde /etc/xdg/powerdevilrc "[AC][SuspendAndShutdown]" AutoSuspendAction 0
  else d_ini kde /etc/xdg/powerdevilrc "[AC][SuspendAndShutdown]" AutoSuspendAction 1; d_ini kde /etc/xdg/powerdevilrc "[AC][SuspendAndShutdown]" AutoSuspendIdleTimeoutSec "$sec"; fi
  d_end "sleep on mains power: $1 minutes (0: never)"
}
apply_screen_lock() {
  d_begin; d_dconf gnome org/gnome/desktop/screensaver lock-enabled false; d_dconf mate org/mate/screensaver lock-enabled false
  d_dconf cinnamon org/cinnamon/desktop/screensaver lock-enabled false; d_end "screen lock off"
}
apply_magnifier() { [ "$1" = yes ] || { skip "default"; return; }; d_begin; d_dconf gnome org/gnome/desktop/a11y/applications screen-magnifier-enabled true; d_end "magnifier at sign-in"; }
apply_login_sound() { [ "$1" = yes ] || { skip "default"; return; }; d_begin; d_dconf cinnamon org/cinnamon/sounds login-enabled true; d_end "sign-in sound"; }
apply_firewall() {
  [ "$1" = on ] || { skip "default"; return; }
  if [ "$PM" = dnf ]; then
    need firewalld || return
    run systemctl enable --now firewalld && ok "firewalld enabled" || fail "firewalld could not be enabled"
  else
    need ufw || return
    run ufw default deny incoming; run ufw default allow outgoing
    [ "$charon_ssh" = yes ] && run ufw allow ssh
    run ufw --force enable && run systemctl enable ufw && ok "ufw on: incoming denied${charon_ssh:+, ssh=$charon_ssh}" || fail "ufw could not be enabled"
  fi
}
apply_ssh_pw() {
  [ "$1" = no ] || { skip "default"; return; }
  [ -n "$DRY" ] || [ -d /etc/ssh/sshd_config.d ] || { fail "this system's sshd has no /etc/ssh/sshd_config.d"; return; }
  echo "PasswordAuthentication no" | write /etc/ssh/sshd_config.d/00-charon.conf 600
  run systemctl try-reload-or-restart ssh.service sshd.service; ok "SSH password sign-in refused (sshd_config.d/00-charon.conf)"
}
apply_auto_updates() {
  case $PM in
    apt)
      need unattended-upgrades || return
      printf 'APT::Periodic::Update-Package-Lists "1";\nAPT::Periodic::Unattended-Upgrade "1";\n' | write /etc/apt/apt.conf.d/20auto-upgrades 644
      # 52charon-origins sorts after the package's 50unattended-upgrades. A list in apt.conf can only be added to
      # or cleared: "all" adds the -updates origin to the shipped list, "security" clears the list and names
      # the security origins alone.
      if [ "$ID" = ubuntu ]; then
        if [ "$1" = all ]; then printf 'Unattended-Upgrade::Allowed-Origins {\n        "${distro_id}:${distro_codename}-updates";\n};\n'
        else printf '#clear Unattended-Upgrade::Allowed-Origins;\nUnattended-Upgrade::Allowed-Origins {\n        "${distro_id}:${distro_codename}-security";\n        "${distro_id}ESMApps:${distro_codename}-apps-security";\n        "${distro_id}ESM:${distro_codename}-infra-security";\n};\n'; fi | write /etc/apt/apt.conf.d/52charon-origins 644
      else
        if [ "$1" = all ]; then printf 'Unattended-Upgrade::Origins-Pattern {\n        "origin=Debian,codename=${distro_codename}-updates";\n};\n'
        else printf '#clear Unattended-Upgrade::Origins-Pattern;\nUnattended-Upgrade::Origins-Pattern {\n        "origin=Debian,codename=${distro_codename},label=Debian-Security";\n        "origin=Debian,codename=${distro_codename}-security,label=Debian-Security";\n};\n'; fi | write /etc/apt/apt.conf.d/52charon-origins 644
      fi
      ok "unattended-upgrades: $1" ;;
    dnf)
      need dnf5-plugin-automatic || return
      t=default; [ "$1" = security ] && t=security
      printf '[commands]\nupgrade_type = %s\napply_updates = yes\n' "$t" | write /etc/dnf/automatic.conf 644
      run systemctl enable --now dnf5-automatic.timer && ok "dnf automatic: upgrade_type=$t, apply_updates=yes" || fail "dnf5-automatic.timer could not be enabled" ;;
    *) skip "Arch has no automatic updates (a rolling release is updated by hand)" ;;
  esac
}
apply_auto_reboot() {
  [ "$1" = yes ] || { skip "default"; return; }
  case $PM in
    apt) printf 'Unattended-Upgrade::Automatic-Reboot "true";\n' | write /etc/apt/apt.conf.d/52charon-reboot 644; ok "Unattended-Upgrade::Automatic-Reboot true" ;;
    dnf) [ -n "$DRY" ] || [ -f /etc/dnf/automatic.conf ] || { skip "automatic updates are not set up"; return; }
         conf_set /etc/dnf/automatic.conf reboot "reboot = when-needed"; ok "dnf automatic: reboot = when-needed" ;;
    *) skip "no automatic updates on this system" ;;
  esac
}
apply_lid() {
  printf '[Login]\nHandleLidSwitch=%s\nHandleLidSwitchExternalPower=%s\n' "$1" "$1" | write /etc/systemd/logind.conf.d/90-charon.conf 644
  run systemctl reload systemd-logind
  ok "HandleLidSwitch=$1, HandleLidSwitchExternalPower=$1"
}
apply_tlp() {
  [ "$1" = yes ] || { skip "default"; return; }
  # TLP's documentation: installing it removes power-profiles-daemon (and tuned on Fedora: --allowerasing),
  # and systemd-rfkill is masked.
  if [ "$PM" = dnf ] && [ -z "$DRY" ]; then online || { later "no network yet for: tlp"; return; }; dnf -y install --allowerasing tlp >> "$LOG" 2>&1 || { fail "could not install: tlp"; return; }
  else need tlp || return; fi
  run systemctl enable tlp.service; run systemctl mask systemd-rfkill.service systemd-rfkill.socket; ok "TLP installed and enabled"
}
apply_bluetooth() {
  [ -n "$DRY" ] || [ -f /etc/bluetooth/main.conf ] || { skip "BlueZ is not installed"; return; }
  v=true; [ "$1" = off ] && v=false
  if [ -n "$DRY" ]; then echo "SET /etc/bluetooth/main.conf [Policy] AutoEnable=$v"; ok dry; return; fi
  ini_set /etc/bluetooth/main.conf "[Policy]" AutoEnable "$v" && ok "[Policy] AutoEnable=$v" || fail "main.conf could not be written"
}
apply_printer() {
  [ "$1" = install ] || { skip "default"; return; }
  need cups cups-browsed || return
  run systemctl enable cups.service cups-browsed.service; ok "CUPS and cups-browsed installed and enabled"
}

# ---- software sets (the catalogue: EFI/HAK/catalogue/charon.ini, which every installation takes along) ----
CAT=
cat_dir() {
  for d in "${CHARON_CATALOGUE:-/nonexistent}" /boot/efi/EFI/HAK/catalogue /efi/EFI/HAK/catalogue /boot/EFI/HAK/catalogue "$S/catalogue"; do [ -f "$d/charon.ini" ] && { CAT=$d; return 0; }; done
  return 1
}
# cat_get ITEM KEY   (site.ini wins over charon.ini)
cat_get() {
  for file in "$CAT/site.ini" "$CAT/charon.ini"; do
    [ -f "$file" ] || continue
    v=$(tr -d '\r' < "$file" | awk -v s="[item $1]" -v k="$2" 'BEGIN{p=0} /^\[/{p=($0==s)} p && index($0,k "=")==1 {print substr($0,length(k)+2); exit}')
    [ -n "$v" ] && { echo "$v"; return 0; }
  done
  return 0
}
# cat_defaults SET   the item ids of the set that are marked default=yes and exist for this system
cat_defaults() {
  tr -d '\r' < "$CAT/charon.ini" | awk -v set="$1" -v distro="${ID:-linux}" '
    function out() { if(id != "" && s == set && d == "yes" && (sys == "" || index("," sys ",", ",linux,") || index("," sys ",", "," distro ","))) print id }
    /^\[/ { out(); id=""; s=""; d=""; sys=""; if($0 ~ /^\[item /) { id=substr($0,7); sub(/\]$/,"",id) } next }
    index($0,"set=")==1 { s=substr($0,5) } index($0,"default=")==1 { d=substr($0,9) } index($0,"systems=")==1 { sys=substr($0,9) }
    END { out() }'
}
# r410: "spd-say -w" never returns on a computer without a working sound card (seen in VMware: the sets
# stalled at "1 of 4"), so it gets 20 seconds. The first line that runs into that limit (timeout's exit
# status 124) is the last one waited for: the later ones are sent without -w, or a list of forty programs
# would wait forty times. No timeout command: without -w from the start. The exit status of spd-say is
# never looked at: speech is an extra here, the installation goes on without it.
# r410 (ls): the sentences go to the Speech Dispatcher of the person in front, which plays through that person's
# sound server. Root's own Speech Dispatcher opens the sound card itself: while a session's sound server (the
# sign-in screen's too) owns the card, it never got it and "spd-say -w" never returned (VM proof fb, task 1), and
# the process it left in this unit's control group made the unit end "failed (timeout)" (fb, task 3).
#  - speaker: the user of seat0's active session (logind), else a user whose sound server holds the card;
#    nobody (a console's sign-in prompt in front, or no desktop at all): root, i.e. the card itself.
#  - spd-say finds a user's server at $XDG_RUNTIME_DIR/speech-dispatcher/speechd.sock (libspeechd; the server is
#    spawned or socket-activated there). Priority "message": not cancelled by the reader's own "text".
#  - reap: what this unit started and left (root's Speech Dispatcher and its modules) is ended, after each sentence
#    spoken as root (the card must be free for a session that starts now) and at the end of the work.
speaker() {
  sid=$(loginctl show-seat seat0 -p ActiveSession --value 2>/dev/null); su_=
  [ -n "$sid" ] && su_=$(loginctl show-session "$sid" -p User --value 2>/dev/null)
  if [ -n "$su_" ] && [ "$su_" != 0 ] && [ -d "/run/user/$su_" ]; then echo "$su_"; return; fi
  for sp_p in $(pgrep -x pipewire; pgrep -x pulseaudio); do
    sp_o=$(ps -o uid= -p "$sp_p" 2>/dev/null | tr -d ' ')
    [ -n "$sp_o" ] && [ "$sp_o" != 0 ] && [ -d "/run/user/$sp_o" ] || continue
    ls -l /proc/$sp_p/fd 2>/dev/null | grep -q -e '/dev/snd/pcmC' -e '/dev/snd/controlC' && { echo "$sp_o"; return; }
  done
}
reap() {
  [ -n "$DRY" ] && return 0
  cg=$(sed -n 's/^0:://p' /proc/self/cgroup 2>/dev/null)
  case "$cg" in */charon-personal.service) ;; *) return 0 ;; esac
  for sig in TERM KILL; do
    left=
    for sp_p in $(cat "/sys/fs/cgroup$cg/cgroup.procs" 2>/dev/null); do
      [ "$sp_p" = $$ ] && continue
      case $(cat /proc/$sp_p/comm 2>/dev/null) in speech-dispatch*|sd_*) kill -$sig "$sp_p" 2>/dev/null; left=1 ;; esac
    done
    [ -n "$left" ] || return 0
    sleep 1
  done
}
speak() {
  say "$*"; [ -n "$DRY" ] && return 0
  have spd-say || return 0
  sp_w=-w; [ -n "$speak_nowait" ] && sp_w=
  sp_u=$(speaker); sp_r=0
  if [ -n "$sp_u" ]; then
    sp_g=$(id -g "$sp_u" 2>/dev/null); sp_h=$(getent passwd "$sp_u" | cut -d: -f6)
    set -- env -i PATH=/usr/local/bin:/usr/bin:/bin HOME="${sp_h:-/}" XDG_RUNTIME_DIR="/run/user/$sp_u" DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$sp_u/bus" spd-say $sp_w -P message -l en "$*"
    if have setpriv && [ -n "$sp_g" ]; then timeout 20 setpriv --reuid "$sp_u" --regid "$sp_g" --init-groups "$@" >/dev/null 2>&1; sp_r=$?
    else timeout 20 runuser -u "$(id -nu "$sp_u")" -- "$@" >/dev/null 2>&1; sp_r=$?; fi
    [ -z "$spoken_to" ] && { spoken_to=$sp_u; say "spoken progress: through the Speech Dispatcher of user $sp_u (the session in front)"; }
  else
    timeout 20 spd-say $sp_w -P message -l en "$*" >/dev/null 2>&1; sp_r=$?
    reap
  fi
  [ $sp_r = 124 ] && [ -z "$speak_nowait" ] && { speak_nowait=1; say "spoken progress: Speech Dispatcher did not finish a sentence in 20 seconds (no working sound?); the next ones are not waited for"; }
  return 0
}
# r410 (user decision 2026-11-24): on a system without a desktop reader nothing spoke the progress. Speech Dispatcher
# and eSpeak NG are installed for it first (package names: verify-linux.txt L8; Debian/Ubuntu speech-dispatcher +
# speech-dispatcher-espeak-ng + espeak-ng, Fedora speech-dispatcher + speech-dispatcher-espeak-ng, Arch
# speech-dispatcher + espeak-ng). A failure is logged and the installation goes on silently.
speech_for_progress() {
  have spd-say && return 0
  [ -n "$DRY" ] && { say "would install Speech Dispatcher for the spoken progress"; return 0; }
  case $PM in
    apt) pkg speech-dispatcher speech-dispatcher-espeak-ng espeak-ng ;;
    # Fedora: spd-say is in speech-dispatcher-utils (https://packages.fedoraproject.org/pkgs/speech-dispatcher/speech-dispatcher-utils/)
    dnf) pkg speech-dispatcher speech-dispatcher-utils speech-dispatcher-espeak-ng espeak-ng ;;
    pacman) pkg speech-dispatcher espeak-ng ;;
  esac
  have spd-say || { say "Speech Dispatcher could not be installed: the progress of the programs is not spoken"; return 1; }
  say "Speech Dispatcher installed for the spoken progress"
}
apply_sets() {
  cat_dir || { fail "the software catalogue (EFI/HAK/catalogue/charon.ini) was not found in the installed system"; return; }
  online || { later "no network yet for the programs"; return; }
  list=
  for set in $(echo "$1" | tr ',' ' '); do
    eval "pick=\${set_$set-}"
    [ "$pick" = none ] && continue
    if [ -n "$pick" ]; then list="$list $(echo "$pick" | tr ',' ' ')"; else list="$list $(cat_defaults "$set" | tr '\n' ' ')"; fi
  done
  # "total", not "n": pkg() counts its wait for pacman in n (the log said "2 of 0", r410 VM proof)
  total=0; for item in $list; do total=$((total+1)); done
  speech_for_progress
  speak "Charon: installing programs: $total."
  good=; bad=; other=; i=0
  for item in $list; do
    i=$((i+1)); name=$(cat_get "$item" name.en); p=
    case $PM in
      apt) [ "$ID" = ubuntu ] && p=$(cat_get "$item" apt.ubuntu); [ -n "$p" ] || p=$(cat_get "$item" apt) ;;
      dnf) p=$(cat_get "$item" dnf) ;;
      pacman) p=$(cat_get "$item" pacman) ;;
    esac
    fp=$(cat_get "$item" flatpak)
    sys=$(cat_get "$item" systems)
    # r410: programs by their names, not by catalogue ids ("installed: rsync, BorgBackup, ...")
    nm=${name:-$item}
    case ",$sys," in *,linux,*|*",$ID,"*|,,) ;; *) other="$other; $nm"; continue ;; esac
    speak "$i of $total: $nm"
    if [ -n "$p" ]; then
      # shellcheck disable=SC2086
      if pkg $p; then good="$good; $nm"; else bad="$bad; $nm (installation failed)"; fi
    elif [ "$soft_flatpak" = yes ] && [ -n "$fp" ]; then
      if { have flatpak || pkg flatpak; } && run flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo && run flatpak install -y --noninteractive flathub "$fp"; then good="$good; $nm (Flatpak)"; else bad="$bad; $nm (Flatpak installation failed)"; fi
    else bad="$bad; $nm (not available here)"; fi
  done
  good=${good#; }; bad=${bad#; }; other=${other#; }   # (names hold commas themselves: "; " between programs)
  if [ -n "$bad" ]; then speak "Charon: some programs were not installed: $bad"; fail "installed: ${good:-none}. NOT installed: $bad"
  else speak "Charon: the programs are installed."; ok "installed: ${good:-none}${other:+. For another system, not installed here: $other}"; fi
}
apply_set_everyday() { skip "used by the answer sets"; }
apply_set_blind() { skip "used by the answer sets"; }
apply_set_tools() { skip "used by the answer sets"; }
apply_set_create() { skip "used by the answer sets"; }
apply_soft_flatpak() { skip "used by the answer sets"; }

# ---- r410 (helper es): the remaining easy rows ----
apply_extra_langs() {
  [ -n "$1" ] || { skip "default"; return; }
  langs=$(echo "$1" | tr ',' ' ')
  case $PM in
    apt|pacman)
      for l in $langs; do
        [ -n "$DRY" ] && { echo "ENABLE $l.UTF-8 UTF-8 in /etc/locale.gen"; continue; }
        grep -q "^$l.UTF-8 UTF-8" /etc/locale.gen 2>/dev/null || { sed -i "s/^# *$l.UTF-8 UTF-8/$l.UTF-8 UTF-8/" /etc/locale.gen; grep -q "^$l.UTF-8 UTF-8" /etc/locale.gen || echo "$l.UTF-8 UTF-8" >> /etc/locale.gen; }
      done
      run locale-gen && ok "locales generated: $langs" || fail "locale-gen failed" ;;
    dnf)
      p=; for l in $langs; do p="$p glibc-langpack-$(echo "$l" | cut -d_ -f1)"; done
      # shellcheck disable=SC2086
      need $p || return; ok "language packs installed:$p" ;;
    *) skip "this system has no known way to add locales" ;;
  esac
}
apply_tz_other() {
  [ -n "$1" ] || { skip "default"; return; }
  case $1 in *..*|/*|*" "*) fail "not a time zone name: $1"; return ;; esac
  [ -n "$DRY" ] || [ -f "/usr/share/zoneinfo/$1" ] || { fail "this system knows no time zone $1 (/usr/share/zoneinfo/$1)"; return; }
  run timedatectl set-timezone "$1" && ok "time zone $1" || fail "timedatectl set-timezone $1 failed"
}
apply_speech_lang() { [ "$1" = same ] && { skip "default"; return; }; speechd DefaultLanguage "\"$1\""; }
# apt_entries: the apt source files of this system (one-line and deb822)
apt_files() { for f in "$ROOT/etc/apt/sources.list" "$ROOT"/etc/apt/sources.list.d/*.list "$ROOT"/etc/apt/sources.list.d/*.sources; do [ -f "$f" ] && echo "$f"; done; }
apply_components() {
  [ -n "$1" ] || { skip "default"; return; }
  [ "$PM" = apt ] && [ "$ID" != ubuntu ] || [ -n "$DRY" ] || { skip "only Debian has these components"; return; }
  add=$(echo "$1" | tr ',' ' ')
  if [ -n "$DRY" ]; then echo "EDIT the Debian entries of the apt sources: add components $add"; else
    for f in $(apt_files); do
      for c in $add; do
        # one-line entries: "deb[-src] [opts] URI SUITE COMPONENTS..." of a Debian archive; deb822: the Components: field of a stanza with debian in URIs
        awk -v c="$c" '
          /^[[:space:]]*deb(-src)?[[:space:]]/ && /debian/ { n=split($0,w," "); has=0; for(i=1;i<=n;i++) if(w[i]==c) has=1; if(!has) $0=$0 " " c }
          { print }' "$f" > "$f.charon-new" && cat "$f.charon-new" > "$f" && rm -f "$f.charon-new"
        awk -v c="$c" '
          function flush() { for(i=1;i<=k;i++) { l=buf[i]; if(deb && l ~ /^Components:/) { n=split(l,w," "); has=0; for(j=2;j<=n;j++) if(w[j]==c) has=1; if(!has) l=l " " c }; print l }; k=0; deb=0 }
          /^[[:space:]]*$/ { flush(); print; next }
          { buf[++k]=$0; if($0 ~ /^URIs:/ && $0 ~ /debian/) deb=1 }
          END { flush() }' "$f" > "$f.charon-new" && cat "$f.charon-new" > "$f" && rm -f "$f.charon-new"
      done
    done
  fi
  run apt-get update && ok "components added to the Debian sources: $add" || fail "apt-get update failed after adding: $add"
}
# apt_mirror URL: the archive entries (not the security ones) point to URL
apt_mirror() {
  for f in $(apt_files); do
    sed -i -E "/security/! s#(https?://)(deb\.debian\.org/debian|ftp\.[a-z.]*debian\.org/debian|([a-z][a-z]\.)?archive\.ubuntu\.com/ubuntu)/?#$1#" "$f"
  done
}
apply_u_mirror() {
  [ "$1" = main ] || { skip "default"; return; }
  [ "$ID" = ubuntu ] || [ -n "$DRY" ] || { skip "only for Ubuntu"; return; }
  if [ -n "$DRY" ]; then echo "EDIT the Ubuntu archive entries of the apt sources: http://archive.ubuntu.com/ubuntu/"; else apt_mirror "http://archive.ubuntu.com/ubuntu/"; fi
  run apt-get update && ok "Ubuntu archive entries: http://archive.ubuntu.com/ubuntu/" || fail "apt-get update failed with the main archive"
}
apply_u_codecs() { [ "$1" = yes ] || { skip "default"; return; }; [ "$ID" = ubuntu ] || [ -n "$DRY" ] || { skip "only for Ubuntu"; return; }; need ubuntu-restricted-addons || return; ok "ubuntu-restricted-addons installed"; }
apply_u_drivers() {
  [ "$1" = yes ] || { skip "default"; return; }; [ "$ID" = ubuntu ] || [ -n "$DRY" ] || { skip "only for Ubuntu"; return; }
  need ubuntu-drivers-common || return
  run ubuntu-drivers install && ok "ubuntu-drivers install ran (what it installed: personal.log)" || fail "ubuntu-drivers install failed"
}
apply_upgrade_now() {
  [ "$1" = yes ] || { skip "default"; return; }
  online || { later "no network yet for the upgrade"; return; }
  case $PM in
    apt) run env DEBIAN_FRONTEND=noninteractive apt-get -o DPkg::Lock::Timeout=900 -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold -y dist-upgrade && ok "apt-get dist-upgrade done" || fail "apt-get dist-upgrade failed" ;;
    dnf) run dnf -y upgrade && ok "dnf upgrade done" || fail "dnf upgrade failed" ;;
    pacman) run pacman -Syu --noconfirm && ok "pacman -Syu done" || fail "pacman -Syu failed" ;;
    *) skip "no known package manager" ;;
  esac
}
apply_popcon() {
  [ "$1" = yes ] || { skip "default"; return; }
  [ "$PM" = apt ] && [ "$ID" != ubuntu ] || [ -n "$DRY" ] || { skip "the popularity survey is Debian's"; return; }
  need popularity-contest || return
  conf_set /etc/popularity-contest.conf PARTICIPATE 'PARTICIPATE="yes"' && ok 'PARTICIPATE="yes" in /etc/popularity-contest.conf'
}
# add_keys USER "KEY;KEY"   appends public keys to the user's authorized_keys (refuses a line that is not one)
add_keys() {
  u=$1; [ -n "$u" ] || { fail "the user name is not known"; return 1; }
  keys_n=0; old_ifs=$IFS; IFS=';'
  for ak in $2; do
    IFS=$old_ifs; ak=$(echo "$ak" | sed 's/^ *//; s/ *$//'); [ -n "$ak" ] || continue
    case $ak in ssh-ed25519\ *|ssh-rsa\ *|ecdsa-sha2-*\ *|sk-ssh-ed25519@openssh.com\ *|sk-ecdsa-sha2-*\ *) ;; *) IFS=$old_ifs; fail "not a public SSH key: ${ak%% *} ..."; return 1 ;; esac
    if [ -n "$DRY" ]; then echo "APPEND to ~$u/.ssh/authorized_keys: ${ak%% *} ..."; else
      h=$(getent passwd "$u" | cut -d: -f6); [ -n "$h" ] || { fail "no home folder for $u"; return 1; }
      g=$(id -gn "$u"); mkdir -p "$h/.ssh"; chmod 700 "$h/.ssh"; touch "$h/.ssh/authorized_keys"
      grep -qxF "$ak" "$h/.ssh/authorized_keys" || echo "$ak" >> "$h/.ssh/authorized_keys"
      chmod 600 "$h/.ssh/authorized_keys"; chown -R "$u:$g" "$h/.ssh"
    fi
    keys_n=$((keys_n+1)); IFS=';'
  done
  IFS=$old_ifs; [ $keys_n -gt 0 ] || { fail "no key was given"; return 1; }
}
apply_ssh_key() { [ -n "$1" ] || { skip "default"; return; }; add_keys "$charon_user" "$1" && ok "public key accepted for $charon_user (~/.ssh/authorized_keys)"; }
apply_help_tool() { [ "$1" = yes ] || { skip "default"; return; }; need tmate || return; ok "tmate installed"; }
# ---- the own software list file: EFI/HAK/catalogue/software.txt (specification 9.3) ----
apply_soft_file() {
  [ "$1" = yes ] || { skip "default"; return; }
  cat_dir && [ -f "$CAT/software.txt" ] || { fail "the list file EFI/HAK/catalogue/software.txt was not found in the installed system"; return; }
  online || { later "no network yet for the programs of the list file"; return; }
  good=; bad=; other=
  while IFS= read -r line || [ -n "$line" ]; do
    line=$(echo "$line" | tr -d '\r' | sed 's/^ *//; s/ *$//'); case $line in ''|\#*) continue ;; esac
    case $line in
      apt:*|dnf:*|pacman:*) pm=${line%%:*}; names=${line#*:}
        if [ "$pm" = "$PM" ]; then
          # shellcheck disable=SC2086
          if pkg $names; then good="$good;$names"; else bad="$bad;$names (installation failed)"; fi
        else other="$other;$line"; fi ;;
      flatpak:*) id=$(echo "${line#*:}" | tr -d ' ')
        if { have flatpak || pkg flatpak; } && run flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo && run flatpak install -y --noninteractive flathub "$id"; then good="$good;$id (Flatpak)"; else bad="$bad;$id (Flatpak installation failed)"; fi ;;
      winget:*|msstore:*|nvda-addon:*) other="$other;$line" ;;
      *:*|http*) bad="$bad;$line (not supported by this build)" ;;
      *) p=; sys=$(cat_get "$line" systems)
         case ",$sys," in *,linux,*|*",$ID,"*|,,) ;; *) other="$other;$line"; continue ;; esac
         case $PM in apt) [ "$ID" = ubuntu ] && p=$(cat_get "$line" apt.ubuntu); [ -n "$p" ] || p=$(cat_get "$line" apt) ;; dnf) p=$(cat_get "$line" dnf) ;; pacman) p=$(cat_get "$line" pacman) ;; esac
         if [ -z "$p" ]; then bad="$bad;$line (no catalogue id for this system)"
         # shellcheck disable=SC2086
         elif pkg $p; then good="$good;$line"; else bad="$bad;$line (installation failed)"; fi ;;
    esac
  done < "$CAT/software.txt"
  good=${good#;}; bad=${bad#;}; other=${other#;}
  if [ -n "$bad" ]; then fail "installed: ${good:-none}. NOT installed: $bad"; else ok "installed: ${good:-none}${other:+. For another system: $other}"; fi
}

# ---- r410 (helper es): block E, the administrator layer ----
site_dir() { for d in "${CHARON_SITE:-/nonexistent}" /boot/efi/EFI/HAK/site /efi/EFI/HAK/site /boot/EFI/HAK/site; do [ -d "$d" ] && { SITE=$d; return 0; }; done; return 1; }
apply_users() {
  [ -n "$1" ] || { skip "default"; return; }
  admin_g=sudo; [ "$PM" = apt ] || admin_g=wheel
  made=; bad=
  for u in $(echo "$1" | tr ',' ' '); do
    n=${u%%:*}; role=${u#*:}; [ "$role" = "$u" ] && role=standard
    echo "$n" | grep -Eq '^[a-z_][a-z0-9_-]{0,31}$' || { bad="$bad $n(not a valid name)"; continue; }
    case $role in admin|standard) ;; *) bad="$bad $n(role $role)"; continue ;; esac
    g=; [ "$role" = admin ] && g="-G $admin_g"
    if [ -n "$DRY" ] || ! getent passwd "$n" >/dev/null; then
      # shellcheck disable=SC2086
      run useradd -m -s /bin/bash $g "$n" && run passwd -d "$n" && run chage -d 0 "$n" && made="$made $n($role)" || bad="$bad $n(useradd failed)"
    else made="$made $n(already there)"; fi
  done
  if [ -n "$bad" ]; then fail "made:${made:- none}; not made:$bad"; else ok "accounts made:$made; each sets its password at the first sign-in"; fi
}
apply_net_static() {
  [ -n "$1" ] || { skip "default"; return; }
  set -- $1; addr=$1; gw=$2; dns=$(echo "$3" | tr ',' ' ')
  echo "$addr" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/[0-9]+$' && echo "$gw" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$' || { fail "expected ADDRESS/PREFIX GATEWAY DNS, got: $*"; return; }
  if [ -n "$DRY" ]; then echo "SET the wired connection: ipv4 manual $addr gateway $gw dns $dns"; ok dry; return; fi
  if have nmcli && nmcli -t general status >/dev/null 2>&1; then
    c=$(nmcli -t -f NAME,TYPE connection show --active | awk -F: '$2=="802-3-ethernet"{print $1; exit}')
    [ -n "$c" ] || c=$(nmcli -t -f NAME,TYPE connection show | awk -F: '$2=="802-3-ethernet"{print $1; exit}')
    [ -n "$c" ] || { fail "NetworkManager has no wired connection"; return; }
    run nmcli connection modify "$c" ipv4.method manual ipv4.addresses "$addr" ipv4.gateway "$gw" ipv4.dns "$dns" && run nmcli connection up "$c" && ok "NetworkManager connection $c: $addr, gateway $gw, DNS $dns" || fail "nmcli could not set the address"
  elif [ -f /etc/network/interfaces ]; then
    i=$(ip -o -4 route show default | awk '{print $5; exit}'); [ -n "$i" ] || i=$(ip -o link | awk -F': ' '$2!="lo"{print $2; exit}')
    run ifdown "$i"   # with the old (DHCP) configuration, so its DHCP client is ended
    sed -i -E "s/^(allow-hotplug|auto) $i\$/auto $i/; /^iface $i inet dhcp/d" /etc/network/interfaces
    printf 'iface %s inet static\n    address %s\n    gateway %s\n' "$i" "$addr" "$gw" >> /etc/network/interfaces
    printf '%s\n' $dns | sed 's/^/nameserver /' > /etc/resolv.conf
    run ip addr flush dev "$i"; run ifup "$i"; ok "ifupdown $i static $addr, gateway $gw; /etc/resolv.conf: $dns"
  elif [ -d /etc/netplan ]; then
    i=$(ip -o -4 route show default | awk '{print $5; exit}')
    printf 'network:\n  version: 2\n  ethernets:\n    %s:\n      dhcp4: false\n      addresses: [%s]\n      routes: [{to: default, via: %s}]\n      nameservers: {addresses: [%s]}\n' "$i" "$addr" "$gw" "$(echo "$dns" | tr ' ' ',')" | write /etc/netplan/90-charon.yaml 600
    run netplan apply && ok "netplan $i: $addr, gateway $gw, DNS $dns" || fail "netplan apply failed"
  else fail "no NetworkManager, ifupdown or netplan on this system"; fi
}
apply_proxy() {
  [ -n "$1" ] || { skip "default"; return; }
  case $1 in http://*|https://*) ;; *) fail "expected http://host:port, got: $1"; return ;; esac
  case $PM in
    apt) printf 'Acquire::http::Proxy "%s";\nAcquire::https::Proxy "%s";\n' "$1" "$1" | write /etc/apt/apt.conf.d/90charon-proxy 644 ;;
    dnf) [ -n "$DRY" ] || [ -f /etc/dnf/dnf.conf ] || printf '[main]\n' > /etc/dnf/dnf.conf; ini_set /etc/dnf/dnf.conf "[main]" proxy "$1" ;;
  esac
  conf_set /etc/environment http_proxy "http_proxy=$1"; conf_set /etc/environment https_proxy "https_proxy=$1"
  ok "proxy $1 for $PM and in /etc/environment"
}
apply_ntp_server() {
  [ -n "$1" ] || { skip "default"; return; }
  echo "$1" | grep -Eq '^[A-Za-z0-9.:-]+$' || { fail "not a server name: $1"; return; }
  if [ -z "$DRY" ] && { have chronyd || [ -f /etc/chrony/chrony.conf ] || [ -f /etc/chrony.conf ]; }; then
    if grep -qs '^sourcedir /etc/chrony/sources.d' /etc/chrony/chrony.conf; then echo "server $1 iburst" | write /etc/chrony/sources.d/charon.sources 644
    else f=/etc/chrony.conf; [ -f /etc/chrony/chrony.conf ] && f=/etc/chrony/chrony.conf; sed -i 's/^\(pool\|server\) /# (Charon: replaced by the site time server) &/' "$f"; echo "server $1 iburst" >> "$f"; fi
    run systemctl restart chronyd.service chrony.service; ok "chrony: server $1 iburst"
  else
    printf '[Time]\nNTP=%s\n' "$1" | write /etc/systemd/timesyncd.conf.d/90-charon.conf 644
    run systemctl restart systemd-timesyncd.service; ok "systemd-timesyncd: NTP=$1"
  fi
}
apply_repo_mirror() {
  [ -n "$1" ] || { skip "default"; return; }
  case $1 in http://*|https://*) ;; *) fail "expected an http or https address, got: $1"; return ;; esac
  case $PM in
    apt) if [ -n "$DRY" ]; then echo "EDIT the archive entries of the apt sources: $1"; else apt_mirror "${1%/}/"; fi
         run apt-get update && ok "apt sources: archive entries point to $1 (security entries kept)" || fail "apt-get update failed with the mirror $1" ;;
    pacman) if [ -n "$DRY" ]; then echo "PREPEND Server = ${1%/}/\$repo/os/\$arch to /etc/pacman.d/mirrorlist"; else sed -i "1i Server = ${1%/}/\$repo/os/\$arch" /etc/pacman.d/mirrorlist; fi
         ok "first mirror in /etc/pacman.d/mirrorlist: $1" ;;
    *) skip "not applied on Fedora: its repositories use metalinks (an internal mirror needs its own .repo files)" ;;
  esac
}
apply_ssh_keys_admin() {
  [ -n "$1" ] || { skip "default"; return; }
  add_keys "$charon_user" "$1" || return
  [ -n "$DRY" ] || [ -d /etc/ssh/sshd_config.d ] || { fail "keys added, but this sshd has no /etc/ssh/sshd_config.d to refuse passwords"; return; }
  echo "PasswordAuthentication no" | write /etc/ssh/sshd_config.d/00-charon.conf 600
  run systemctl try-reload-or-restart ssh.service sshd.service; ok "administrator keys: $keys_n for $charon_user; SSH password sign-in refused"
}
apply_first_msg() {
  [ -n "$1" ] || { skip "default"; return; }
  printf '%s\n' "$1" | write /etc/motd 644
  printf '%s\n' "$1" | write /etc/charon-first-msg.txt 644
  printf '#!/bin/sh\n# Charon: the first-sign-in message, spoken once per user\nd=${XDG_CONFIG_HOME:-$HOME/.config}; [ -e "$d/charon-first-msg.done" ] && exit 0\nmkdir -p "$d"; : > "$d/charon-first-msg.done"\nsleep 8; command -v spd-say >/dev/null && spd-say -P message "$(cat /etc/charon-first-msg.txt)"\n' | write /usr/local/bin/charon-first-msg 755
  printf '[Desktop Entry]\nType=Application\nName=Charon first sign-in message\nExec=/usr/local/bin/charon-first-msg\nNoDisplay=true\n' | write /etc/xdg/autostart/charon-first-msg.desktop 644
  ok "message in /etc/motd and spoken once per user at the first desktop sign-in"
}
apply_asset_tag() { [ -n "$1" ] || { skip "default"; return; }; ok "asset tag recorded in the applied-settings report"; }
apply_root_ca() {
  [ -n "$1" ] || { skip "default"; return; }
  [ "$PM" = dnf ] && { skip "Fedora: its anchors folder was not verified, not applied"; return; }
  site_dir || [ -n "$DRY" ] || { fail "EFI/HAK/site was not found in the installed system"; return; }
  done_c=; bad_c=
  for c in $(echo "$1" | tr ',' ' '); do
    case $c in */*|*..*) bad_c="$bad_c $c(not a plain file name)"; continue ;; esac
    if [ -n "$DRY" ]; then echo "TRUST $c"; done_c="$done_c $c"; continue; fi
    [ -f "$SITE/$c" ] || { bad_c="$bad_c $c(not found)"; continue; }
    if [ "$PM" = apt ]; then install -m 644 "$SITE/$c" "/usr/local/share/ca-certificates/${c%.*}.crt" && done_c="$done_c $c" || bad_c="$bad_c $c"
    else install -D -m 644 "$SITE/$c" "/etc/ca-certificates/trust-source/anchors/${c%.*}.crt" && done_c="$done_c $c" || bad_c="$bad_c $c"; fi
  done
  [ -n "$done_c" ] && { if [ "$PM" = apt ]; then run update-ca-certificates; else run update-ca-trust; fi || bad_c="$bad_c (the trust store could not be updated)"; }
  if [ -n "$bad_c" ]; then fail "trusted:${done_c:- none}; not:$bad_c"; else ok "certificates trusted:$done_c"; fi
}
apply_post_script() {
  [ -n "$1" ] || { skip "default"; return; }
  set -- $1; f=$1; want=${2#sha256=}
  case $f in */*|*..*) fail "not a plain file name: $f"; return ;; esac
  echo "$want" | grep -Eqi '^[0-9a-f]{64}$' || { fail "the SHA-256 of the script is missing (FILE sha256=HASH)"; return; }
  [ -n "$DRY" ] && { echo "RUN $f as root when its SHA-256 is $want"; ok dry; return; }
  site_dir && [ -f "$SITE/$f" ] || { fail "the script $f was not found in EFI/HAK/site"; return; }
  mkdir -p "$S/post"; cp "$SITE/$f" "$S/post/$f"
  got=$(sha256sum "$S/post/$f" | cut -d' ' -f1)
  [ "$got" = "$(echo "$want" | tr 'A-F' 'a-f')" ] || { fail "the SHA-256 of $f does not match: not run"; return; }
  sh "$S/post/$f" >> "$LOG" 2>&1; rc=$?
  [ $rc = 0 ] && ok "script $f ran, exit status 0" || fail "script $f ended with exit status $rc"
}
# phone_home runs after every other answer, with the applied-settings report (apply_all)
apply_phone_home() {
  [ -n "$1" ] || { skip "default"; return; }
  case $1 in http://*|https://*) ;; *) fail "expected an http or https address, got: $1"; return ;; esac
  [ -n "$DRY" ] && { echo "POST the applied-settings report to $1"; ok dry; return; }
  have curl || pkg curl || { fail "curl is not available"; return; }
  run curl -fsS --retry 3 --max-time 30 -X POST -H 'Content-Type: text/plain' --data-binary "@$S/install-report.txt" "$1" && ok "report sent to $1" || fail "the report could not be sent to $1"
}

apply_all() {
  distro
  [ -r "$A" ] || { say "no answers file $A: nothing to do"; return 0; }
  . "$A"
  : > "$REPORT.new"; again=0
  # r410 (es): the applied-settings report of the installed system (specification 6.5): one line per answer, secrets as "set"
  INSTALLED=$S/install-report.txt; [ -n "$DRY" ] && INSTALLED=${TMPDIR:-/tmp}/charon-install-report-dry.$$
  { echo "# Charon: settings of the detailed mode applied in this system: key=value -> applied | skipped | failed | later: text"
    echo "# path=$charon_path; written $(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null); host $(cat /etc/hostname 2>/dev/null)"; } > "$INSTALLED.new"
  for k in $KEYS; do
    eval "set=\${$k+x}; v=\${$k-}"
    [ -n "$set" ] || continue
    status=failed; text="the function gave no result"
    "apply_$k" "$v"
    shown=$v; case " $SECRETS " in *" $k "*) shown=set ;; esac
    echo "$k=$status: $text" >> "$REPORT.new"; say "$k=$shown -> $status: $text"
    echo "$k=$shown -> $status: $text" >> "$INSTALLED.new"
    [ "$status" = later ] && again=1
  done
  # keys in the file that this script does not know are said, not dropped
  for k in $(sed -n "s/^\([a-z][a-z0-9_]*\)='.*/\1/p" "$A"); do
    case " $KEYS phone_home charon_path charon_user charon_ssh " in *" $k "*) ;; *) echo "$k=failed: this version of personal.sh does not know this answer" >> "$REPORT.new"; echo "$k -> failed: this version of personal.sh does not know this answer" >> "$INSTALLED.new"; say "unknown key $k";; esac
  done
  mv "$INSTALLED.new" "$INSTALLED"
  # the report to an address: after everything else, with the report itself
  if [ -n "${phone_home+x}" ]; then
    status=failed; text="the function gave no result"; apply_phone_home "$phone_home"
    echo "phone_home=$status: $text" >> "$REPORT.new"; echo "phone_home=$phone_home -> $status: $text" >> "$INSTALLED"; say "phone_home=$phone_home -> $status: $text"
    [ "$status" = later ] && again=1
  fi
  [ -n "$DRY" ] && rm -f "$INSTALLED"
  [ -n "$DRY" ] && { cat "$REPORT.new"; rm -f "$REPORT.new"; return 0; }
  mv "$REPORT.new" "$REPORT"
  return $again
}

case "$1" in
  install)
    mkdir -p "$S"
    # Not "After=cloud-final.service": that unit is itself After=multi-user.target, so with WantedBy=multi-user.target
    # this unit was in an ordering cycle and systemd deleted its start job at every start (Arch image, r410 VM proof).
    cat > /etc/systemd/system/charon-personal.service <<'UNIT'
[Unit]
Description=Charon: answers of the detailed install mode
After=network-online.target
Wants=network-online.target
ConditionPathExists=/var/lib/charon/answers.env

[Service]
Type=oneshot
ExecStart=/bin/sh /var/lib/charon/personal.sh apply
TimeoutStartSec=3600
# r410: what the script leaves behind (a Speech Dispatcher that spd-say spawned) gets SIGKILL at once when the script
# has ended and the unit stays "succeeded" (systemd.kill(5), KillMode=mixed). With the default control-group it got
# SIGTERM, outlived TimeoutStopSec and the unit ended "failed (timeout)" (VM proof fb, task 3). reap ends it first.
KillMode=mixed

[Install]
WantedBy=multi-user.target
UNIT
    systemctl enable charon-personal.service >> "$LOG" 2>&1
    # "now" is called from a ready image's first start, which may end with a restart a few seconds later (a plan
    # step, a new kernel on Arch). So the work begins a minute from now: a restart before that drops the timer and
    # the enabled unit runs at the next start instead; without a restart it runs in this start.
    if [ "$2" = now ]; then
      systemd-run --on-active=60 --unit=charon-personal-now systemctl start --no-block charon-personal.service >> "$LOG" 2>&1 ||
        systemctl start --no-block charon-personal.service >> "$LOG" 2>&1
    fi
    exit 0 ;;
  apply)
    # a system that is shutting down has no bus and no network left: not a try, the next start does the work
    [ "$(systemctl is-system-running 2>/dev/null)" = stopping ] && { say "the system is shutting down: left for the next start"; exit 0; }
    mkdir -p "$S"; n=$(cat "$S/personal.tries" 2>/dev/null || echo 0); n=$((n+1)); echo "$n" > "$S/personal.tries"
    say "apply, try $n"
    apply_all; rc=$?
    reap
    if [ $rc = 0 ] || [ "$n" -ge 5 ]; then
      [ "$n" -ge 5 ] && grep -q '=later:' "$REPORT" 2>/dev/null && sed -i 's/=later:/=failed: no network in five starts;/' "$REPORT"
      systemctl disable charon-personal.service >> "$LOG" 2>&1
      f=$(grep -c '=failed' "$REPORT" 2>/dev/null); say "done; failed: ${f:-0}"
      [ "${f:-0}" != 0 ] && [ -x "$S/charon-tone" ] && "$S/charon-tone" -p 100 330 200 0 60 262 300 >/dev/null 2>&1
    fi
    exit 0 ;;
  --dry-run)
    DRY=1; A=$2; LOG=/dev/null; REPORT=${TMPDIR:-/tmp}/charon-personal-dry.$$
    [ -n "$CHARON_PM" ] && { distro() { PM=$CHARON_PM; }; }
    apply_all; exit 0 ;;
  *) echo "usage: personal.sh install [now] | apply | --dry-run FILE" >&2; exit 2 ;;
esac
