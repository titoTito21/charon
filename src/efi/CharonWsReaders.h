#pragma once
// r403: Charon workspace - desktops and screen readers for the Linux systems,
// as check boxes: anyone may want several desktops or several readers. Two
// readers that would speak over each other (Speakup and Fenrir in the
// console, Orca and Cthulhu on the desktop) are both installed; one question
// says which of the two starts, the other stays installed and switched off.
// The packages come from each system's own repositories; Cthulhu (a fork of
// Orca, written in Go) has no Debian or Ubuntu package, so it is built from
// its official source at the first start with the network (on Arch: from the
// Stormux repository first), and Orca speaks until it is there.
// GPL-2.0-or-later.
#include "CharonWsAsk.h"
namespace hakefi {
namespace ws {
namespace install {
using ask::Choice;
inline const Choice linuxReaders[]={
    {u"Orca, czytnik pulpitu",u"Orca, the desktop screen reader","orca"},
    {u"Cthulhu, czytnik pulpitu wywodzący się z Orki; budowany ze źródeł przy pierwszym starcie z siecią",
     u"Cthulhu, a desktop screen reader derived from Orca; built from source at the first start with the network","cthulhu"},
    {u"Speakup z espeakup, mowa w konsoli od startu systemu",u"Speakup with espeakup, console speech from system start","speakup"},
    {u"Fenrir, czytnik konsoli",u"Fenrir, the console screen reader","fenrir"},
    {u"Emacspeak, pulpit dźwiękowy w Emacsie; gdy to on mówi, konsola 1 loguje sama i uruchamia Emacspeak",
     u"Emacspeak, the audio desktop in Emacs; when it speaks, console 1 signs in by itself and starts Emacspeak","emacspeak"}};
inline const Choice debianDesktops[]={
    {u"GNOME",u"GNOME","gnome-desktop"},{u"MATE",u"MATE","mate-desktop"},{u"KDE Plasma",u"KDE Plasma","kde-desktop"},
    {u"Xfce",u"Xfce","xfce-desktop"},{u"Cinnamon",u"Cinnamon","cinnamon-desktop"},{u"LXQt",u"LXQt","lxqt-desktop"},{u"LXDE",u"LXDE","lxde-desktop"}};
inline const Choice ubuntuDesktops[]={
    {u"pulpit Ubuntu, GNOME",u"Ubuntu desktop, GNOME","ubuntu-desktop-minimal"},{u"Kubuntu, KDE Plasma",u"Kubuntu, KDE Plasma","kubuntu-desktop"},
    {u"Xubuntu, Xfce",u"Xubuntu, Xfce","xubuntu-desktop"},{u"Ubuntu MATE",u"Ubuntu MATE","ubuntu-mate-desktop"},
    {u"Lubuntu, LXQt",u"Lubuntu, LXQt","lubuntu-desktop"},{u"Ubuntu Cinnamon",u"Ubuntu Cinnamon","ubuntucinnamon-desktop"}};
// Ready images: the same desktops by their package names in each system.
inline const Choice imageDesktops[]={
    {u"GNOME",u"GNOME","gnome"},{u"KDE Plasma",u"KDE Plasma","kde"},{u"Xfce",u"Xfce","xfce"},{u"MATE",u"MATE","mate"},{u"Cinnamon",u"Cinnamon","cinnamon"},{u"LXQt",u"LXQt","lxqt"}};
#define CHARON_READER_HELP_PL u"Można zaznaczyć kilka. Gdy dwa mówiłyby naraz, jedno pytanie ustali, który startuje."
#define CHARON_READER_HELP_EN u"Several may be checked. When two would speak at once, one question settles which one starts."
// After the questions: which console reader and which desktop reader start,
// and whether there is any speech at all. False: the user left (Escape).
inline bool ResolveReaders(bool desktops) {
    const bool orca=ask::Has("readers","orca"),cthulhu=ask::Has("readers","cthulhu"),speakup=ask::Has("readers","speakup"),fenrir=ask::Has("readers","fenrir");
    const bool emacspeak=ask::Has("readers","emacspeak");
    // The console has room for one speaker: asked only when two or three were checked.
    static const Choice consoleAll[]={{u"Speakup z espeakup",u"Speakup with espeakup","speakup"},{u"Fenrir",u"Fenrir","fenrir"},{u"Emacspeak",u"Emacspeak","emacspeak"}};
    static Choice console[3];size_t consoles=0;
    if(speakup)console[consoles++]=consoleAll[0];
    if(fenrir)console[consoles++]=consoleAll[1];
    if(emacspeak)console[consoles++]=consoleAll[2];
    if(consoles>1) {size_t i=0;
        if(!ask::Choose(T(u"Który czytnik ma mówić w konsoli? Pozostałe zostaną zainstalowane i wyłączone.",u"Which reader speaks in the console? The others are installed and switched off."),console,consoles,i))return false;
        ask::Put("console",console[i].value);
    } else ask::Put("console",consoles?console[0].value:"none");
    if(desktops && orca && cthulhu) {
        static const Choice both[]={{u"Orca",u"Orca","orca"},{u"Cthulhu",u"Cthulhu","cthulhu"}};size_t i=0;
        if(!ask::Choose(T(u"Który czytnik ma startować na pulpicie? Drugi zostanie zainstalowany i wyłączony.",u"Which reader starts on the desktop? The other is installed and switched off."),both,2,i))return false;
        ask::Put("gui",both[i].value);
    } else ask::Put("gui",!desktops?"none":orca?"orca":cthulhu?"cthulhu":"none");
    if(!desktops && (orca || cthulhu))
        Say(T(u"Bez pulpitu czytniki Orca i Cthulhu nie mają gdzie działać, więc ich nie instaluję.",u"Without a desktop Orca and Cthulhu have nowhere to run, so they are not installed."));
    ask::Put("speech",(speakup || fenrir || emacspeak || (desktops && (orca || cthulhu)))?"yes":"no");
    return true;
}
// The readers' packages for a distribution's package list (", a, b").
inline void ReaderPackages(char* out,size_t capacity,bool desktops) {
    size_t n=0;out[0]=0;
    auto add=[&](const char* s) {while(*s && n+1<capacity)out[n++]=*s++;out[n]=0;};
    const bool any=ask::Is("speech","yes");
    if(any)add(", espeak-ng, alsa-utils");
    if(ask::Has("readers","speakup"))add(", espeakup");
    if(ask::Has("readers","fenrir"))add(", fenrir, speech-dispatcher");
    if(desktops && (ask::Has("readers","orca") || ask::Has("readers","cthulhu")))add(", orca");   // Orca also speaks until Cthulhu is built
}
// The arguments of speechScript: CONSOLE GUI LIST.
inline void ReaderArguments(char* out,size_t capacity) {
    size_t n=0;out[0]=0;
    auto add=[&](const char* s) {while(*s && n+1<capacity)out[n++]=*s++;out[n]=0;};
    add(ask::Get("console")[0]?ask::Get("console"):"none");add(" ");add(ask::Get("gui")[0]?ask::Get("gui"):"none");add(" ");
    add(ask::Get("readers")[0]?ask::Get("readers"):"none");
}
// Runs in the installed system (Debian late_command, Ubuntu late-commands,
// cloud-init runcmd of a ready image): installs what is missing and decides
// what starts. speech.sh CONSOLE GUI LIST - CONSOLE speakup|fenrir|none,
// GUI orca|cthulhu|none, LIST every chosen reader with commas. Without
// arguments (older callers): Speakup in the console and Orca on the desktop.
inline const char speechScript[]=
    "#!/bin/sh\n"
    "# Written by Charon: screen readers in the installed system.\n"
    "C=${1:-speakup}; G=${2:-orca}; L=${3:-$C,$G}\n"
    "has() { case \",$L,\" in *\",$1,\"*) return 0;; esac; return 1; }\n"
    "export DEBIAN_FRONTEND=noninteractive\n"
    "pm=none; command -v apt-get >/dev/null 2>&1 && pm=apt; command -v pacman >/dev/null 2>&1 && pm=pacman; command -v dnf >/dev/null 2>&1 && pm=dnf\n"
    "get() { case $pm in apt) apt-get install -y \"$@\" >/dev/null 2>&1;; pacman) pacman -S --needed --noconfirm \"$@\" >/dev/null 2>&1;; dnf) dnf install -y \"$@\" >/dev/null 2>&1;; *) false;; esac; }\n"
    "# Arch: Fenrir and Cthulhu come from the Stormux repository (signed; key as published on stormux.org).\n"
    "stormux() {\n"
    "  [ $pm = pacman ] || return 1\n"
    "  grep -q '^\\[stormux\\]' /etc/pacman.conf && return 0\n"
    "  curl -fsSL -o /tmp/stormux_repo.pub https://packages.stormux.org/stormux_repo.pub || return 1\n"
    "  pacman-key --add /tmp/stormux_repo.pub >/dev/null 2>&1 && pacman-key --lsign-key 52ADA49000F1FF0456F8AEEFB4CDE1CD56EF8E82 >/dev/null 2>&1 || return 1\n"
    "  sed -i '/^\\[core\\]/i [stormux]\\nSigLevel = Required DatabaseOptional\\nServer = https://packages.stormux.org/$arch\\n' /etc/pacman.conf\n"
    "  pacman -Sy >/dev/null 2>&1\n"
    "}\n"
    "LANGV=$(sed -n 's/^LANG=\"\\{0,1\\}\\(..\\).*/\\1/p' /etc/default/locale /etc/locale.conf 2>/dev/null | head -n1); [ -n \"$LANGV\" ] || LANGV=en\n"
    "echo \"charon: readers console=$C desktop=$G chosen=$L language=$LANGV\"\n"
    "# --- the desktop -------------------------------------------------------------\n"
    "O=false; [ \"$G\" = orca ] && O=true\n"
    "if [ \"$G\" != none ]; then\n"
    "  command -v orca >/dev/null 2>&1 || get orca\n"
    "  mkdir -p /etc/dconf/profile /etc/dconf/db/local.d /etc/dconf/db/gdm.d /etc/xdg/autostart\n"
    "  printf 'user-db:user\\nsystem-db:local\\n' > /etc/dconf/profile/user\n"
    "  printf '[org/gnome/desktop/a11y/applications]\\nscreen-reader-enabled=%s\\n[org/mate/desktop/interface]\\naccessibility=true\\n[org/cinnamon/desktop/a11y/applications]\\nscreen-reader-enabled=%s\\n' $O $O > /etc/dconf/db/local.d/00-charon-speech\n"
    "  printf 'user-db:user\\nsystem-db:gdm\\nfile-db:/usr/share/gdm/greeter-dconf-defaults\\n' > /etc/dconf/profile/gdm\n"
    "  printf '[org/gnome/desktop/a11y/applications]\\nscreen-reader-enabled=true\\n' > /etc/dconf/db/gdm.d/00-charon-speech\n"
    "  command -v dconf >/dev/null && dconf update\n"
    "  # Desktops that do not read the GNOME setting (Plasma, Xfce, LXQt, LXDE) start the reader from here;\n"
    "  # a reader already running is never started twice. Cthulhu speaks on every desktop, Orca until it is built.\n"
    "  if [ \"$G\" = cthulhu ]; then\n"
    "    printf '[Desktop Entry]\\nType=Application\\nName=Charon: Cthulhu\\nExec=sh -c \"pgrep -x cthulhu >/dev/null || pgrep -x orca >/dev/null || { command -v cthulhu >/dev/null && exec cthulhu; exec orca; }\"\\nNoDisplay=true\\nX-GNOME-AutoRestart=false\\n' > /etc/xdg/autostart/charon-reader.desktop\n"
    "  else\n"
    "    printf '[Desktop Entry]\\nType=Application\\nName=Charon: Orca\\nExec=sh -c \"pgrep -x orca >/dev/null || exec orca\"\\nOnlyShowIn=KDE;XFCE;LXQt;LXDE;\\nNoDisplay=true\\n' > /etc/xdg/autostart/charon-reader.desktop\n"
    "  fi\n"
    "fi\n"
    "if has cthulhu && [ \"$G\" != none ] && ! command -v cthulhu >/dev/null 2>&1; then\n"
    "  if stormux && get cthulhu; then echo \"charon: cthulhu from the Stormux repository\"\n"
    "  else\n"
    "    mkdir -p /usr/local/sbin\n"
    "    cat > /usr/local/sbin/charon-cthulhu-build <<'CHARONEOF'\n"
    "#!/bin/sh\n"
    "# Written by Charon: builds Cthulhu from its official source once the network is there.\n"
    "export DEBIAN_FRONTEND=noninteractive\n"
    "if command -v apt-get >/dev/null; then apt-get install -y git curl golang-go gcc pkg-config libgtk-3-dev libatspi2.0-dev libglib2.0-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libx11-dev libxtst-dev libxi-dev libxfixes-dev libxres-dev libxkbcommon-dev liblouis-dev libbrlapi-dev speech-dispatcher at-spi2-core || exit 1\n"
    "elif command -v pacman >/dev/null; then pacman -S --needed --noconfirm git curl go gcc pkgconf gtk3 at-spi2-core glib2 gstreamer gst-plugins-base libx11 libxtst libxi libxfixes libxres libxkbcommon liblouis brltty speech-dispatcher || exit 1\n"
    "elif command -v dnf >/dev/null; then dnf install -y git curl golang gcc pkgconf gtk3-devel at-spi2-core-devel glib2-devel gstreamer1-devel gstreamer1-plugins-base-devel libX11-devel libXtst-devel libXi-devel libXfixes-devel libXres-devel libxkbcommon-devel liblouis-devel brlapi-devel speech-dispatcher || exit 1\n"
    "fi\n"
    "d=/var/lib/charon/cthulhu-src; rm -rf $d\n"
    "git clone --depth 1 https://git.stormux.org/storm/cthulhu.git $d && cd $d && ./build-cthulhu.sh || exit 1\n"
    "for f in cthulhu cthulhu-braille cthulhu-input-helper; do install -Dm755 $f /usr/local/bin/$f || exit 1; done\n"
    "systemctl disable charon-cthulhu-build.service\n"
    "echo \"charon: cthulhu built\"\n"
    "CHARONEOF\n"
    "    chmod 755 /usr/local/sbin/charon-cthulhu-build\n"
    "    printf '[Unit]\\nDescription=Charon: build the Cthulhu screen reader once\\nWants=network-online.target\\nAfter=network-online.target\\n\\n[Service]\\nType=oneshot\\nExecStart=/usr/local/sbin/charon-cthulhu-build\\nTimeoutStartSec=3600\\n\\n[Install]\\nWantedBy=multi-user.target\\n' > /etc/systemd/system/charon-cthulhu-build.service\n"
    "    systemctl enable charon-cthulhu-build.service 2>/dev/null\n"
    "    echo \"charon: cthulhu is built at the first start with the network\"\n"
    "  fi\n"
    "fi\n"
    "# --- the console -------------------------------------------------------------\n"
    "if has speakup; then\n"
    "  command -v espeakup >/dev/null 2>&1 || get espeakup\n"
    "  # Ubuntu Server has speakup neither built in nor switched on: its modules come\n"
    "  # in linux-modules-extra, which a minimal server does not always install.\n"
    "  for k in /lib/modules/*; do [ -d \"$k\" ] || continue; v=${k##*/}\n"
    "    find \"$k\" -name 'speakup_soft*' 2>/dev/null | grep -q . || { [ $pm = apt ] && apt-get install -y linux-modules-extra-$v >/dev/null 2>&1; }\n"
    "  done\n"
    "  printf 'VOICE=%s\\n' $LANGV >> /etc/default/espeakup 2>/dev/null\n"
    "fi\n"
    "if has fenrir; then\n"
    "  command -v fenrir >/dev/null 2>&1 || get fenrir || { stormux && get fenrir; } || echo \"charon: fenrir could not be installed\"\n"
    "  command -v speech-dispatcher >/dev/null 2>&1 || get speech-dispatcher\n"
    "  [ -f /etc/speech-dispatcher/speechd.conf ] && sed -i \"s/^#* *DefaultLanguage .*/DefaultLanguage \\\"$LANGV\\\"/\" /etc/speech-dispatcher/speechd.conf\n"
    "fi\n"
    "if has emacspeak; then\n"
    "  command -v emacspeak >/dev/null 2>&1 || get emacspeak emacspeak-espeak-server || get emacspeak || echo \"charon: emacspeak could not be installed\"\n"
    "  command -v espeak-ng >/dev/null 2>&1 || get espeak-ng\n"
    "fi\n"
    "fenrirunit() { for s in fenrirscreenreader fenrir; do systemctl $1 $s.service >/dev/null 2>&1 && return 0; done; return 1; }\n"
    "if [ \"$C\" = speakup ]; then\n"
    "  echo speakup_soft > /etc/modules-load.d/charon-speakup.conf\n"
    "  fenrirunit disable; systemctl enable espeakup 2>/dev/null\n"
    "elif [ \"$C\" = fenrir ]; then\n"
    "  rm -f /etc/modules-load.d/charon-speakup.conf; systemctl disable espeakup 2>/dev/null\n"
    "  fenrirunit enable || echo \"charon: no fenrir service found\"\n"
    "elif [ \"$C\" = emacspeak ]; then\n"
    "  # Emacspeak cannot read the login prompt: console 1 signs the first user in and starts it.\n"
    "  rm -f /etc/modules-load.d/charon-speakup.conf; systemctl disable espeakup 2>/dev/null; fenrirunit disable\n"
    "  U=$(getent passwd 1000 | cut -d: -f1)\n"
    "  if [ -n \"$U\" ] && command -v emacspeak >/dev/null 2>&1; then\n"
    "    mkdir -p /etc/systemd/system/getty@tty1.service.d\n"
    "    A=$(command -v agetty)\n"
    "    printf '[Service]\\nExecStart=\\nExecStart=-%s --autologin %s --noclear %%I $TERM\\n' \"$A\" \"$U\" > /etc/systemd/system/getty@tty1.service.d/charon-emacspeak.conf\n"
    "    printf '# Written by Charon: Emacspeak on console 1, speaking with eSpeak.\\nexport DTK_PROGRAM=espeak\\nif [ \"$(tty)\" = /dev/tty1 ] && [ -z \"$INSIDE_EMACS\" ]; then emacspeak; fi\\n' > /etc/profile.d/charon-emacspeak.sh\n"
    "    echo \"charon: emacspeak starts on console 1 for $U\"\n"
    "  else echo \"charon: emacspeak not set up (no user or no emacspeak)\"; fi\n"
    "fi\n"
    "# A fresh system often starts with the sound card muted: unmute before speech starts.\n"
    "if [ \"$C\" != none ] || [ \"$G\" != none ] || has emacspeak; then\n"
    "  command -v amixer >/dev/null 2>&1 || get alsa-utils\n"
    "  printf '[Unit]\\nDescription=Charon: unmute the sound card for speech\\nAfter=sound.target\\nBefore=espeakup.service fenrirscreenreader.service fenrir.service\\n\\n[Service]\\nType=oneshot\\nExecStart=/usr/local/sbin/charon-unmute\\n\\n[Install]\\nWantedBy=multi-user.target\\n' > /etc/systemd/system/charon-unmute.service\n"
    "  mkdir -p /usr/local/sbin\n"
    "  printf '#!/bin/sh\\nfor c in Master PCM Speaker Headphone; do amixer -q sset $c 80%% unmute 2>/dev/null; done\\nexit 0\\n' > /usr/local/sbin/charon-unmute\n"
    "  chmod 755 /usr/local/sbin/charon-unmute\n"
    "  systemctl enable charon-unmute.service 2>/dev/null\n"
    "fi\n"
    "exit 0\n";
}  // namespace install
}  // namespace ws
}  // namespace hakefi
