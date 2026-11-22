# Written by Charon (r406): the sounds of progress for install and set-up scripts,
# as NVDA's progress bar beeps: the higher the tone, the further the work
# (110 Hz at 0 percent, two octaves per fifty percent, 40 ms).
# Sourced by Charon's other scripts; plain POSIX shell, also busybox.
#   charon_tone ARGS...            charon-tone, through aplay when the card is in use
#   charon_beep PERCENT            the progress beep, only when the percentage changed
#   charon_apt FROM TO ARGS...     apt-get ARGS with its real progress mapped to FROM..TO percent
#   charon_pacman FROM TO ARGS...  pacman ARGS, the same
#   charon_busy_start / _stop      a quiet tick every two seconds for work without a percentage
#   charon_say POLISH ENGLISH      one spoken sentence, when a synthesizer is installed
#   sh progress.sh watch DIR       beeps every five seconds while an installer fills DIR
CHARON_STATE=${CHARON_STATE:-/var/lib/charon}
# Run by itself (watch): the tone program lies beside this file.
case $0 in */progress.sh) [ -f "${0%/*}/charon-tone" ] && CHARON_TONE=${CHARON_TONE:-${0%/*}/charon-tone};; esac
CHARON_TONE=${CHARON_TONE:-$CHARON_STATE/charon-tone}
# Some places do not let programs run (Ubuntu's /run, a FAT partition): then a
# copy is made where they may. Without arguments the program answers 1.
chmod 755 "$CHARON_TONE" 2>/dev/null
"$CHARON_TONE" >/dev/null 2>&1
if [ $? != 1 ] && [ -f "$CHARON_TONE" ]; then
  for charon_d in /usr/local/lib /var/tmp /tmp /root; do
    cp "$CHARON_TONE" $charon_d/charon-tone 2>/dev/null && chmod 755 $charon_d/charon-tone 2>/dev/null || continue
    $charon_d/charon-tone >/dev/null 2>&1
    if [ $? = 1 ]; then CHARON_TONE=$charon_d/charon-tone; break; fi
    rm -f $charon_d/charon-tone
  done
fi
CHARON_LOG=${CHARON_LOG:-/dev/null}
# Networks that lose the new, large TLS greeting (see charon_tls_check): the setting made
# there is used by every script that sources this file.
[ -f /var/lib/charon/openssl-small-hello.cnf ] && export OPENSSL_CONF=/var/lib/charon/openssl-small-hello.cnf
# Some networks (seen: a virtual machine's NAT; also old routers and filters) silently drop
# secure connections whose first message is larger than one packet, and a current OpenSSL
# sends such a message (a post-quantum key share): every HTTPS download then times out,
# while plain HTTP works. When exactly that is measured - the default greeting fails, the
# classic one works - the classic key exchange is set for the system's package tools, and
# the log says so. Nothing is changed when HTTPS works, or fails either way.
charon_tls_check() {
  command -v curl >/dev/null 2>&1 || return 0
  [ -f /var/lib/charon/openssl-small-hello.cnf ] && return 0
  for charon_u in https://geo.mirror.pkgbuild.com/ https://deb.debian.org/ https://archive.ubuntu.com/; do
    curl -s -o /dev/null -m 8 "$charon_u" && return 0
  done
  charon_ok=no
  for charon_u in https://geo.mirror.pkgbuild.com/ https://deb.debian.org/; do
    curl -s -o /dev/null -m 8 --curves X25519 "$charon_u" && { charon_ok=yes; break; }
  done
  [ $charon_ok = yes ] || return 0
  mkdir -p /var/lib/charon
  printf '# Written by Charon: this network drops the large TLS greeting; the classic key exchange is used.\n# Delete this file and the OPENSSL_CONF line in /etc/environment to undo.\nopenssl_conf = charon_init\n[charon_init]\nssl_conf = charon_ssl\n[charon_ssl]\nsystem_default = charon_default\n[charon_default]\nGroups = X25519:P-256:P-384\n' > /var/lib/charon/openssl-small-hello.cnf
  export OPENSSL_CONF=/var/lib/charon/openssl-small-hello.cnf
  grep -q '^OPENSSL_CONF=' /etc/environment 2>/dev/null || echo 'OPENSSL_CONF=/var/lib/charon/openssl-small-hello.cnf' >> /etc/environment
  echo "charon: this network drops HTTPS connections with the large TLS greeting; the classic key exchange is now used (OPENSSL_CONF in /etc/environment)" >> "$CHARON_LOG"
}
CHARON_LASTP=-1
CHARON_BUSY=
charon_tone() {
  [ -x "$CHARON_TONE" ] || return 1
  "$CHARON_TONE" "$@" 2>/dev/null; charon_r=$?
  [ $charon_r = 0 ] && return 0
  # The sound card is held by a mixer or by the speech synthesizer: go through it.
  if [ $charon_r = 2 ] && command -v aplay >/dev/null 2>&1; then
    "$CHARON_TONE" -w "$@" 2>/dev/null | aplay -q 2>/dev/null && return 0
  fi
  return 1
}
charon_beep() {
  [ "$1" = "$CHARON_LASTP" ] && return 0
  CHARON_LASTP=$1
  charon_tone -p "$1"
}
charon_busy_start() {
  charon_busy_stop
  ( while :; do charon_tone 330 25; sleep 2; done ) >/dev/null 2>&1 &
  CHARON_BUSY=$!
}
charon_busy_stop() {
  [ -n "$CHARON_BUSY" ] && kill "$CHARON_BUSY" 2>/dev/null && wait "$CHARON_BUSY" 2>/dev/null
  CHARON_BUSY=
}
# The language of spoken notes: pl or en, from the system's locale.
charon_lang() {
  charon_l=$(sed -n 's/^LANG="\{0,1\}\([a-z][a-z]\)[_.].*/\1/p' /etc/default/locale /etc/locale.conf 2>/dev/null | head -n1)
  [ -n "$charon_l" ] || charon_l=en
  echo "$charon_l"
}
charon_say() {
  charon_v=$(charon_lang); charon_t=$2; [ "$charon_v" = pl ] && charon_t=$1
  echo "charon: $charon_t" >> "$CHARON_LOG"
  if [ -e /sys/accessibility/speakup/synth ] && pgrep espeakup >/dev/null 2>&1; then
    # Speakup reads what appears on the console.
    echo "Charon: $charon_t" > /dev/tty1 2>/dev/null
  elif command -v espeak-ng >/dev/null 2>&1; then
    [ "$charon_v" = pl ] || charon_v=en
    espeak-ng -v $charon_v "Charon: $charon_t" >/dev/null 2>&1
  else
    echo "Charon: $charon_t" > /dev/tty1 2>/dev/null
  fi
  return 0
}
# apt-get reports its progress on a file descriptor: "dlstatus:N:PERCENT:text"
# while downloading, "pmstatus:PACKAGE:PERCENT:text" while installing.
# Downloading takes the first half of FROM..TO, installing the second.
charon_apt() {
  charon_a=$1; charon_b=$2; shift 2
  { apt-get -o APT::Status-Fd=3 -o Dpkg::Use-Pty=0 -o DPkg::Lock::Timeout=900 "$@" 3>&1 >>"$CHARON_LOG" 2>&1; echo "charon-rc:$?"; } | {
    rc=1; last=-1
    while IFS=: read -r kind name pct rest; do
      case $kind in
        charon-rc) rc=$name; continue;;
        dlstatus) half=0;;
        pmstatus) half=1;;
        *) continue;;
      esac
      pct=${pct%%.*}
      case $pct in ''|*[!0-9]*) continue;; esac
      [ "$pct" -gt 100 ] && pct=100
      v=$(( charon_a + (charon_b - charon_a) * (half * 100 + pct) / 200 ))
      if [ "$v" != "$last" ]; then last=$v; charon_tone -p "$v"; fi
    done
    exit "$rc"
  }
}
# pacman has no such report: its lines are counted (in English: LC_ALL=C). "Packages (N)" gives the
# number; "NAME downloading..." and "(I/N) installing NAME" move the count.
charon_pacman() {
  charon_a=$1; charon_b=$2; shift 2
  { LC_ALL=C pacman --noconfirm --noprogressbar "$@" 2>&1; echo "charon-rc:$?"; } | {
    rc=1; last=-1; total=0; got=0
    while IFS= read -r line; do
      v=
      case $line in
        charon-rc:*) rc=${line#charon-rc:}; continue;;
      esac
      echo "$line" >> "$CHARON_LOG"
      case $line in
        Packages\ \(*) total=${line#Packages (}; total=${total%%)*}
          case $total in ''|*[!0-9]*) total=0;; esac;;
        *downloading...) got=$((got + 1))
          [ "$total" -gt 0 ] && [ "$got" -le "$total" ] && v=$(( charon_a + (charon_b - charon_a) * got / (2 * total) ));;
        \(*/*\)\ installing*|\(*/*\)\ upgrading*|\(*/*\)\ reinstalling*)
          i=${line#(}; n=${i#*/}; n=${n%%)*}; i=${i%%/*}; i=$(echo $i)
          case "$i$n" in ''|*[!0-9]*) ;; *) [ "$n" -gt 0 ] && v=$(( charon_a + (charon_b - charon_a) / 2 + (charon_b - charon_a) * i / (2 * n) ));; esac;;
      esac
      if [ -n "$v" ] && [ "$v" != "$last" ]; then last=$v; charon_tone -p "$v"; fi
    done
    exit "$rc"
  }
}
# An installer that reports nothing a script could read (debian-installer,
# Ubuntu's): the amount of data on the new system's disk is the measure. The
# tone rises with it and never reaches the top before the installer is done:
# percent = 95 * used / (used + 1.5 GB). Before DIR is mounted: the lowest tone.
charon_watch() {
  dir=$1
  while [ ! -e /run/charon-watch-stop ] && [ ! -e /tmp/charon-watch-stop ]; do
    p=0
    if grep -q " $dir " /proc/mounts 2>/dev/null; then
      set -- $(df -k "$dir" 2>/dev/null | tail -n1)
      used=$3
      case $used in ''|*[!0-9]*) used=0;; esac
      p=$(( 95 * used / (used + 1572864) ))
    fi
    charon_tone -p "$p"
    sleep 5
  done
}
if [ "$1" = watch ] && [ -n "$2" ]; then charon_watch "$2"; fi
