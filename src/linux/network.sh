#!/bin/sh
# Written by Charon (r409): the network connection chosen in Charon, for the installed system.
#   network.sh apply          writes the connection of /var/lib/charon/network.conf into this
#                             system's own network manager and brings it up
#   network.sh wait [SECONDS] waits until the network is usable (a default route and a name
#                             server that answers); 0 when it is, 1 when the time is over
#   network.sh check FILE     only says what "apply" would do with FILE (nothing is written)
# network.conf: NAME=value lines, nothing quoted, the value is everything after the first "=".
# It holds a secret: mode 600, owned by root; never copied to a log. "apply" takes the
# passphrase out of the file once the connection is saved in the system.
#   CHARON_NET_TYPE       ethernet | wifi
#   CHARON_NET_MAC        the adapter Charon used, aa:bb:cc:dd:ee:ff (may be empty: any adapter)
#   CHARON_NET_METHOD     dhcp | static
#   CHARON_NET_ADDRESS    static: 192.168.1.10/24
#   CHARON_NET_GATEWAY    static: 192.168.1.1
#   CHARON_NET_DNS        static: 192.168.1.1,9.9.9.9
#   CHARON_NET_PROXY      http://host:port (may be empty)
#   CHARON_WIFI_SSID      the network's name
#   CHARON_WIFI_SECURITY  open | wpa-psk | enterprise
#   CHARON_WIFI_PSK       the passphrase (8 to 63 characters) or the key (64 hexadecimal digits)
#   CHARON_WIFI_COUNTRY   two letters, PL, US ...: the regulatory domain
# Written for whichever of these the system uses: NetworkManager (a keyfile in
# /etc/NetworkManager/system-connections, mode 600), netplan (/etc/netplan/90-charon.yaml, mode
# 600), or systemd-networkd (a .network file, with iwd for Wi-Fi: /var/lib/iwd/SSID.psk).
# Enterprise Wi-Fi (802.1X) is refused: it needs an identity and certificates Charon does not ask for.
S=/var/lib/charon
CONF=$S/network.conf
LOG=${CHARON_NET_LOG:-$S/network.log}
say() { echo "charon: network: $*"; echo "$(date '+%F %T') $*" >> "$LOG" 2>/dev/null; }
bad() { say "PROBLEM: $*"; echo "network: $*" >> $S/network.problems 2>/dev/null; }
have() { command -v "$1" >/dev/null 2>&1; }

netwait() {
  t=${1:-60}; n=0
  while :; do
    if ip route 2>/dev/null | grep -q '^default '; then
      for h in deb.debian.org archive.ubuntu.com geo.mirror.pkgbuild.com mirrors.fedoraproject.org; do
        getent hosts $h >/dev/null 2>&1 && return 0
      done
    fi
    [ $n -ge "$t" ] && return 1
    sleep 2; n=$((n+2))
  done
}
if [ "$1" = wait ]; then netwait "$2"; exit $?; fi
if [ "$1" = check ]; then CONF=$2; DRY=1; fi
[ "$1" = apply ] || [ "$1" = check ] || { echo "usage: network.sh apply | wait [SECONDS] | check FILE" >&2; exit 2; }
[ -f "$CONF" ] || { say "no network.conf: the system keeps its own network set-up (DHCP on the wired adapter)"; exit 0; }
[ -z "$DRY" ] && { chown root:root "$CONF" 2>/dev/null; chmod 600 "$CONF"; : > $S/network.problems; }

TYPE=; MAC=; METHOD=dhcp; ADDRESS=; GATEWAY=; DNS=; PROXY=; SSID=; SECURITY=; PSK=; COUNTRY=
while IFS= read -r line || [ -n "$line" ]; do
  line=$(printf '%s' "$line" | tr -d '\r')
  k=${line%%=*}; v=${line#*=}
  case $k in
    CHARON_NET_TYPE) TYPE=$v;; CHARON_NET_MAC) MAC=$(printf '%s' "$v" | tr 'A-F' 'a-f');; CHARON_NET_METHOD) METHOD=$v;;
    CHARON_NET_ADDRESS) ADDRESS=$v;; CHARON_NET_GATEWAY) GATEWAY=$v;; CHARON_NET_DNS) DNS=$v;; CHARON_NET_PROXY) PROXY=$v;;
    CHARON_WIFI_SSID) SSID=$v;; CHARON_WIFI_SECURITY) SECURITY=$v;; CHARON_WIFI_PSK) PSK=$v;;
    CHARON_WIFI_COUNTRY) COUNTRY=$(printf '%s' "$v" | tr 'a-z' 'A-Z' | tr -cd 'A-Z');;
  esac
done < "$CONF"

# ---- what was asked for must make sense before anything is written --------------------
ipok() { printf '%s' "$1" | grep -Eq '^([0-9]{1,3}\.){3}[0-9]{1,3}$'; }
case "$TYPE" in ethernet|wifi) ;; *) bad "CHARON_NET_TYPE must be ethernet or wifi (it is '$TYPE')"; exit 2;; esac
case "$MAC" in ''|[0-9a-f][0-9a-f]:[0-9a-f][0-9a-f]:[0-9a-f][0-9a-f]:[0-9a-f][0-9a-f]:[0-9a-f][0-9a-f]:[0-9a-f][0-9a-f]) ;; *) bad "CHARON_NET_MAC is not an adapter address"; exit 2;; esac
case "$METHOD" in dhcp) ;; static)
    printf '%s' "$ADDRESS" | grep -Eq '^([0-9]{1,3}\.){3}[0-9]{1,3}/[0-9]{1,2}$' || { bad "a static address needs CHARON_NET_ADDRESS as 192.168.1.10/24"; exit 2; }
    ipok "$GATEWAY" || { bad "a static address needs CHARON_NET_GATEWAY"; exit 2; }
    for d in $(printf '%s' "$DNS" | tr ',' ' '); do ipok "$d" || { bad "CHARON_NET_DNS holds something that is no address"; exit 2; }; done;;
  *) bad "CHARON_NET_METHOD must be dhcp or static"; exit 2;; esac
case "$COUNTRY" in ''|[A-Z][A-Z]) ;; *) bad "CHARON_WIFI_COUNTRY must be two letters"; exit 2;; esac
HEXPSK=
if [ $TYPE = wifi ]; then
  [ -n "$SSID" ] || { bad "Wi-Fi needs CHARON_WIFI_SSID"; exit 2; }
  [ "$(printf '%s' "$SSID" | wc -c)" -le 32 ] || { bad "the Wi-Fi name is longer than 32 bytes"; exit 2; }
  case "$SECURITY" in
    open) PSK=;;
    wpa-psk)
      n=$(printf '%s' "$PSK" | wc -c)
      # run again after the passphrase was taken out of the file: the connection is saved already
      saved=no; for f in /etc/NetworkManager/system-connections/charon-wifi.nmconnection /etc/netplan/90-charon.yaml /var/lib/iwd/*.psk; do [ -f "$f" ] && saved=yes; done
      if [ "$n" = 0 ] && [ -z "$DRY" ] && [ $saved = yes ]; then say "the Wi-Fi connection is saved in this system already"; exit 0; fi
      if [ "$n" = 64 ] && ! printf '%s' "$PSK" | grep -q '[^0-9A-Fa-f]'; then HEXPSK=$PSK
      elif [ "$n" -ge 8 ] && [ "$n" -le 63 ]; then :
      else bad "the Wi-Fi passphrase must have 8 to 63 characters (or be a key of 64 hexadecimal digits)"; exit 2; fi;;
    enterprise) bad "enterprise Wi-Fi (802.1X) is not set up by Charon: it needs an identity and certificates. Connect by cable, or let an administrator add the connection"; exit 3;;
    *) bad "CHARON_WIFI_SECURITY must be open, wpa-psk or enterprise"; exit 2;;
  esac
  # The key instead of the passphrase where it can be computed (PBKDF2, as WPA2 does): no
  # passphrase in the saved file, and no character of it that a file format would misread.
  if [ $SECURITY = wpa-psk ] && [ -z "$HEXPSK" ] && have python3; then
    HEXPSK=$(CHARON_S="$SSID" CHARON_P="$PSK" python3 -c 'import hashlib,os;print(hashlib.pbkdf2_hmac("sha1",os.environb[b"CHARON_P"],os.environb[b"CHARON_S"],4096,32).hex())' 2>/dev/null)
    printf '%s' "$HEXPSK" | grep -Eq '^[0-9a-f]{64}$' || HEXPSK=
  fi
fi

# ---- which network manager this system uses -------------------------------------------
stack=none
if systemctl is-enabled NetworkManager >/dev/null 2>&1 || systemctl is-active NetworkManager >/dev/null 2>&1; then stack=nm
elif have netplan && [ -d /etc/netplan ]; then stack=netplan
elif systemctl is-enabled systemd-networkd >/dev/null 2>&1 || systemctl is-active systemd-networkd >/dev/null 2>&1; then stack=networkd
fi
pm=none; have apt-get && pm=apt; have pacman && pm=pacman; have dnf && pm=dnf
# the adapter: by its address when Charon gave one, otherwise the first of the kind
dev=
for d in /sys/class/net/*; do
  [ -e "$d/device" ] || continue
  n=${d##*/}; w=no; [ -d "$d/wireless" ] || [ -d "$d/phy80211" ] && w=yes
  if [ -n "$MAC" ]; then [ "$(cat $d/address 2>/dev/null)" = "$MAC" ] && { dev=$n; break; }
  elif [ $TYPE = wifi ] && [ $w = yes ]; then dev=$n; break
  elif [ $TYPE = ethernet ] && [ $w = no ]; then dev=$n; break; fi
done
say "asked for: $TYPE, $METHOD${SSID:+, a Wi-Fi network with security $SECURITY}${COUNTRY:+, country $COUNTRY}; this system uses $stack; adapter ${dev:-not found}"
[ -n "$DRY" ] && exit 0
[ $stack = none ] && { bad "no network manager was recognised here (NetworkManager, netplan, systemd-networkd): nothing was written"; exit 1; }

# ---- what Wi-Fi needs on this system ----------------------------------------------------
inst() {
  case $pm in
    apt) DEBIAN_FRONTEND=noninteractive apt-get -o DPkg::Lock::Timeout=600 install -y "$@" >>"$LOG" 2>&1;;
    pacman) pacman -Syu --needed --noconfirm "$@" >>"$LOG" 2>&1;;
    dnf) dnf install -y "$@" >>"$LOG" 2>&1;;
    *) false;;
  esac
}
if [ $TYPE = wifi ]; then
  need=
  case $stack in
    nm) have wpa_supplicant || have iwd || have iwctl || { case $pm in dnf) need="NetworkManager-wifi wpa_supplicant";; pacman) need="wpa_supplicant";; apt) need="wpasupplicant";; esac; };;
    netplan) have wpa_supplicant || need="wpasupplicant";;
    networkd) have iwctl || [ -x /usr/lib/iwd/iwd ] || [ -x /usr/libexec/iwd ] || need="iwd";;
  esac
  have iw || need="$need iw"
  # the adapters' firmware: without it most Wi-Fi cards do not appear at all
  if ! ls /lib/firmware/iwlwifi* /usr/lib/firmware/iwlwifi* >/dev/null 2>&1; then
    case $pm in
      apt) if grep -qi '^ID=ubuntu' /etc/os-release; then need="$need linux-firmware"; else need="$need firmware-iwlwifi firmware-realtek firmware-atheros firmware-brcm80211 firmware-misc-nonfree"; fi;;
      pacman|dnf) need="$need linux-firmware";;
    esac
  fi
  if [ -n "$need" ]; then
    if netwait 4; then
      # Debian keeps the adapters' firmware in its component non-free-firmware, which its
      # ready image does not use (https://www.debian.org/releases/trixie/amd64/ch02s02.en.html)
      if [ $pm = apt ] && grep -q '^ID=debian' /etc/os-release && ! apt-cache show firmware-iwlwifi 2>/dev/null | grep -q '^Package:'; then
        code=$(sed -n 's/^VERSION_CODENAME=//p' /etc/os-release)
        [ -n "$code" ] && printf '# Written by Charon: the component non-free-firmware, for Wi-Fi adapters.
Types: deb
URIs: http://deb.debian.org/debian
Suites: %s %s-updates
Components: non-free-firmware
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
' "$code" "$code" > /etc/apt/sources.list.d/charon-non-free-firmware.sources
        apt-get -o DPkg::Lock::Timeout=600 update >>"$LOG" 2>&1
        say "Debian's component non-free-firmware switched on (/etc/apt/sources.list.d/charon-non-free-firmware.sources)"
      fi
      for p in $need; do inst $p || bad "the package $p, needed for Wi-Fi, could not be installed"; done
      # NetworkManager loads its Wi-Fi part only when it starts (its connections stay up over a restart)
      if [ $stack = nm ] && systemctl is-active NetworkManager >/dev/null 2>&1; then systemctl restart NetworkManager >>"$LOG" 2>&1; sleep 5; fi
    else
      bad "Wi-Fi needs packages this system does not have yet ($(echo $need)) and there is no network to fetch them: connect a network cable once and run: sudo sh $S/network.sh apply"
    fi
  fi
  # the regulatory domain: the kernel's own setting, kept for every start
  if [ -n "$COUNTRY" ]; then
    printf '# Written by Charon: the Wi-Fi regulatory domain.\noptions cfg80211 ieee80211_regdom=%s\n' "$COUNTRY" > /etc/modprobe.d/charon-regdom.conf
    have iw && iw reg set "$COUNTRY" 2>/dev/null
  fi
  have rfkill && rfkill unblock wifi 2>/dev/null
fi

# ---- the proxy Charon used --------------------------------------------------------------
if [ -n "$PROXY" ]; then
  if printf '%s' "$PROXY" | grep -Eq '^https?://[A-Za-z0-9._:@%/-]+$'; then
    sed -i '/^\(http\|https\)_proxy=/d' /etc/environment 2>/dev/null
    printf 'http_proxy=%s\nhttps_proxy=%s\n' "$PROXY" "$PROXY" >> /etc/environment
    [ $pm = apt ] && printf '// Written by Charon.\nAcquire::http::Proxy "%s";\nAcquire::https::Proxy "%s";\n' "$PROXY" "$PROXY" > /etc/apt/apt.conf.d/90charon-proxy
    [ $pm = dnf ] && [ -f /etc/dnf/dnf.conf ] && { sed -i '/^proxy=/d' /etc/dnf/dnf.conf; printf 'proxy=%s\n' "$PROXY" >> /etc/dnf/dnf.conf; }
    say "proxy written for the package manager and /etc/environment"
  else bad "CHARON_NET_PROXY is not an http address: not written"; fi
fi

dnslist=$(printf '%s' "$DNS" | tr ',' ' ')
r=0
case $stack in
  nm)
    # https://networkmanager.dev/docs/api/latest/nm-settings-keyfile.html : a keyfile, mode 600, owned by root
    f=/etc/NetworkManager/system-connections/charon-$TYPE.nmconnection
    mkdir -p /etc/NetworkManager/system-connections
    umask 077
    {
      printf '[connection]\nid=charon-%s\ntype=%s\nautoconnect=true\nautoconnect-priority=10\n' $TYPE "$([ $TYPE = wifi ] && echo wifi || echo ethernet)"
      if [ $TYPE = wifi ]; then
        # the name as a list of bytes: any name is safe that way
        printf '\n[wifi]\nmode=infrastructure\nssid=%s\n' "$(printf '%s' "$SSID" | od -An -v -tu1 | tr -s ' \n' ';;' | sed 's/^;//')"
        [ -n "$MAC" ] && printf 'mac-address=%s\n' "$MAC"
        if [ $SECURITY = wpa-psk ]; then
          printf '\n[wifi-security]\nkey-mgmt=wpa-psk\n'
          if [ -n "$HEXPSK" ]; then printf 'psk=%s\n' "$HEXPSK"; else printf 'psk=%s\n' "$(printf '%s' "$PSK" | sed 's/\\/\\\\/g;s/^ /\\s/')"; fi
        fi
      else
        printf '\n[ethernet]\n'; [ -n "$MAC" ] && printf 'mac-address=%s\n' "$MAC"
      fi
      if [ $METHOD = static ]; then
        printf '\n[ipv4]\nmethod=manual\naddress1=%s,%s\n' "$ADDRESS" "$GATEWAY"
        [ -n "$dnslist" ] && printf 'dns=%s;\n' "$(echo $dnslist | tr ' ' ';')"
      else printf '\n[ipv4]\nmethod=auto\n'; fi
      printf '\n[ipv6]\nmethod=auto\n'
    } > $f
    chmod 600 $f; chown root:root $f
    if have nmcli && systemctl is-active NetworkManager >/dev/null 2>&1; then
      nmcli connection load $f >>"$LOG" 2>&1 || { r=1; bad "NetworkManager did not accept the connection file $f"; }
      # brought up only when it is not what already carries the system (a wired DHCP link is left alone)
      if [ $r = 0 ] && { [ $TYPE = wifi ] || [ $METHOD = static ]; }; then nmcli connection up charon-$TYPE >>"$LOG" 2>&1 || say "the connection is saved; it could not be brought up now (out of range, or no adapter yet)"; fi
    fi
    say "NetworkManager connection written: $f (mode 600)";;
  netplan)
    # https://netplan.readthedocs.io/en/stable/netplan-yaml/ : version 2, ethernets / wifis with access-points
    f=/etc/netplan/90-charon.yaml
    q() { printf '%s' "$1" | sed 's/\\/\\\\/g;s/"/\\"/g'; }
    name=${dev:-charon0}
    # netplan's Wi-Fi (wpa_supplicant under systemd-networkd) is bound to an interface by its
    # name: "match:" is not accepted there. No adapter visible (its firmware was installed a
    # moment ago, or it is not plugged in): nothing can be written yet; tried again at the next start.
    if [ $TYPE = wifi ] && [ -z "$dev" ]; then
      bad "no Wi-Fi adapter is visible yet, and netplan needs its name: the connection is written at the next start (firmware that was just installed is loaded then)"
      r=1
    else
    umask 077
    {
      printf '# Written by Charon: the connection chosen in Charon.\nnetwork:\n  version: 2\n'
      if [ $TYPE = wifi ]; then printf '  wifis:\n'; else printf '  ethernets:\n'; fi
      printf '    %s:\n' "$name"
      [ $TYPE = ethernet ] && [ -n "$MAC" ] && printf '      match:\n        macaddress: "%s"\n' "$MAC"
      [ $TYPE = wifi ] && [ -n "$COUNTRY" ] && printf '      regulatory-domain: "%s"\n' "$COUNTRY"
      if [ $METHOD = static ]; then
        printf '      dhcp4: false\n      addresses: ["%s"]\n      routes:\n        - to: default\n          via: %s\n' "$ADDRESS" "$GATEWAY"
        [ -n "$dnslist" ] && printf '      nameservers:\n        addresses: [%s]\n' "$(echo $dnslist | sed 's/ /, /g')"
      else printf '      dhcp4: true\n      dhcp6: true\n'; fi
      if [ $TYPE = wifi ]; then
        printf '      access-points:\n        "%s":' "$(q "$SSID")"
        if [ $SECURITY = wpa-psk ]; then printf '\n          password: "%s"\n' "$(if [ -n "$HEXPSK" ]; then printf '%s' "$HEXPSK"; else q "$PSK"; fi)"; else printf ' {}\n'; fi
      fi
    } > $f
    chmod 600 $f; chown root:root $f
    if netplan generate >>"$LOG" 2>&1; then
      netplan apply >>"$LOG" 2>&1 || say "netplan apply reported an error (see $LOG); the file is kept and used at the next start"
      say "netplan connection written: $f (mode 600)"
    else
      r=1; mv $f $f.rejected; bad "netplan did not accept the connection file (kept as $f.rejected, mode 600)"
    fi
    fi;;
  networkd)
    # https://www.freedesktop.org/software/systemd/man/latest/systemd.network.html ; Wi-Fi: iwd,
    # https://iwd.wiki.kernel.org/networkconfigurationsettings (/var/lib/iwd/SSID.psk, PreSharedKey or Passphrase)
    # (the first file that matches an adapter wins: before the image's own 10-cloud-init-*.network)
    f=/etc/systemd/network/05-charon.network
    {
      printf '# Written by Charon: the connection chosen in Charon.\n[Match]\n'
      if [ -n "$MAC" ]; then printf 'MACAddress=%s\n' "$MAC"; elif [ $TYPE = wifi ]; then printf 'Type=wlan\n'; else printf 'Type=ether\n'; fi
      printf '\n[Network]\n'
      if [ $METHOD = static ]; then
        printf 'Address=%s\nGateway=%s\n' "$ADDRESS" "$GATEWAY"; for d in $dnslist; do printf 'DNS=%s\n' $d; done
      else printf 'DHCP=yes\n'; fi
    } > $f
    chmod 644 $f
    if [ $TYPE = wifi ]; then
      mkdir -p /var/lib/iwd; chmod 700 /var/lib/iwd
      # the file's name: the network's name when it has only letters, digits, "-", "_" and
      # spaces; otherwise "=" and the name in hexadecimal
      if printf '%s' "$SSID" | grep -q '[^A-Za-z0-9_ -]'; then base="=$(printf '%s' "$SSID" | od -An -v -tx1 | tr -d ' \n')"; else base=$SSID; fi
      umask 077
      if [ $SECURITY = wpa-psk ]; then
        w="/var/lib/iwd/$base.psk"
        if [ -n "$HEXPSK" ]; then printf '[Security]\nPreSharedKey=%s\n\n[Settings]\nAutoConnect=true\n' "$HEXPSK" > "$w"
        else printf '[Security]\nPassphrase=%s\n\n[Settings]\nAutoConnect=true\n' "$(printf '%s' "$PSK" | sed 's/\\/\\\\/g;s/^ /\\s/')" > "$w"; fi
      else w="/var/lib/iwd/$base.open"; printf '[Settings]\nAutoConnect=true\n' > "$w"; fi
      chmod 600 "$w"; chown root:root "$w"
      if [ -n "$COUNTRY" ]; then mkdir -p /etc/iwd; grep -q '^Country=' /etc/iwd/main.conf 2>/dev/null || printf '[General]\nCountry=%s\n' "$COUNTRY" >> /etc/iwd/main.conf; fi
      systemctl enable iwd >>"$LOG" 2>&1 && systemctl restart iwd >>"$LOG" 2>&1 || bad "iwd, which connects Wi-Fi beside systemd-networkd, could not be started (is it installed?)"
      say "iwd network written: $w (mode 600)"
    fi
    networkctl reload >>"$LOG" 2>&1 || systemctl restart systemd-networkd >>"$LOG" 2>&1
    say "systemd-networkd connection written: $f";;
esac
# Not done yet (no adapter, a package missing): tried again at every start until it is.
if [ -d /etc/systemd/system ]; then
  if [ $r != 0 ] || [ -s $S/network.problems ]; then
    printf '[Unit]\nDescription=Charon: the network connection chosen in Charon\nAfter=systemd-udev-settle.service local-fs.target\nBefore=network-online.target\nConditionPathExists=/var/lib/charon/network.conf\n\n[Service]\nType=oneshot\nExecStart=/bin/sh /var/lib/charon/network.sh apply\nTimeoutStartSec=900\n\n[Install]\nWantedBy=multi-user.target\n' > /etc/systemd/system/charon-network.service
    systemctl enable charon-network.service >>"$LOG" 2>&1
  elif [ -f /etc/systemd/system/charon-network.service ]; then
    systemctl disable charon-network.service >>"$LOG" 2>&1; rm -f /etc/systemd/system/charon-network.service
  fi
fi
# The passphrase leaves network.conf once the system has the connection.
if [ $r = 0 ] && [ -n "$PSK" ]; then
  t=$(mktemp $S/network.XXXXXX) && { grep -v '^CHARON_WIFI_PSK=' "$CONF" > "$t"; echo 'CHARON_WIFI_PSK=' >> "$t"; cat "$t" > "$CONF"; rm -f "$t"; chmod 600 "$CONF"; }
fi
exit $r
