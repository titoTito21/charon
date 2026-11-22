#!/bin/sh
# Written by Charon: speech inside the Ubuntu installer (its live system, in memory).
L=$(cat /run/charon/lang.txt 2>/dev/null); V=en; case "$L" in pl*) V=pl;; esac
# r406: progress tones (progress.sh), also while the installer itself works.
CHARON_STATE=/run/charon; chmod 755 /run/charon/charon-tone 2>/dev/null
[ -f /run/charon/progress.sh ] && . /run/charon/progress.sh
command -v charon_tone >/dev/null 2>&1 || { charon_tone() { return 1; }; charon_apt() { shift 2; apt-get "$@" >/dev/null 2>&1; }; }
say() {
  if [ -e /sys/accessibility/speakup/synth ] && pgrep espeakup >/dev/null 2>&1; then echo "Charon: $1" > /dev/tty1
  elif command -v espeak-ng >/dev/null 2>&1; then espeak-ng -v $V "Charon: $1" 2>/dev/null
  fi
}
if [ "$1" = done ]; then
  : > /run/charon-watch-stop; sleep 1; charon_tone -p 100; charon_tone 784 110 0 30 1175 170
  if [ $V = pl ]; then say "koniec instalacji, restart komputera."; else say "installation finished, the computer restarts."; fi
  sleep 7; exit 0
fi
if [ "$1" = tell ]; then
  n=0
  while sleep 120; do n=$((n+2))
    if [ $V = pl ]; then say "instalacja trwa, minut: $n."; else say "still installing, $n minutes."; fi
  done
  exit 0
fi
export DEBIAN_FRONTEND=noninteractive
k=$(uname -r); need=
command -v espeak-ng >/dev/null 2>&1 || need="$need espeak-ng"
command -v espeakup >/dev/null 2>&1 || need="$need espeakup"
command -v amixer >/dev/null 2>&1 || need="$need alsa-utils"
find /lib/modules/$k -name 'speakup_soft*' 2>/dev/null | grep -q . || need="$need linux-modules-extra-$k"
echo "charon: kernel $k, to install:$need"
if [ -n "$need" ]; then
  timeout 180 apt-get update >/dev/null 2>&1 || echo "charon: package lists not refreshed"
  for p in $need; do timeout 300 apt-get install -y --no-install-recommends $p >/dev/null 2>&1 || echo "charon: could not install $p"; done
  # (no tones here: the sound drivers themselves arrive with these packages)
fi
udevadm trigger --action=add --subsystem-match=pci 2>/dev/null; udevadm settle -t 10 2>/dev/null
modprobe snd_hda_intel 2>/dev/null; modprobe snd_intel8x0 2>/dev/null
for c in 0 1 2 3; do for m in Master PCM Speaker Headphone; do amixer -q -c $c sset $m 80% unmute 2>/dev/null; done; done
if modprobe speakup_soft 2>/dev/null; then
  printf 'VOICE=%s\ndefault_voice=%s\n' $V $V >> /etc/default/espeakup
  systemctl restart espeakup 2>/dev/null || espeakup -V $V 2>/dev/null
  sleep 3
else
  echo "charon: no speakup driver, Charon's own notes only"
fi
if [ $V = pl ]; then say "mowa w instalatorze Ubuntu. Start instalacji."; else say "speech in the Ubuntu installer. The installation starts."; fi
charon_tone 784 90 0 30 1175 130
rm -f /run/charon-watch-stop
systemd-run --no-block --unit charon-watch sh /run/charon/progress.sh watch /target >/dev/null 2>&1 || setsid sh /run/charon/progress.sh watch /target </dev/null >/dev/null 2>&1 &
systemd-run --no-block --unit charon-tell sh /run/charon/live-speech.sh tell >/dev/null 2>&1 || setsid sh /run/charon/live-speech.sh tell </dev/null >/dev/null 2>&1 &
exit 0
