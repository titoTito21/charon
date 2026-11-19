#!/bin/sh
# r397: Speakup-style copy and paste into GRUB's command line, a window
# (define, read, silence), indentation, and formatting events in the UEFI Shell.
E=${1:-charon-win-r398c/CHARON.EFI}
filter(){ grep -av "pkg_resources\|declare_namespace\|UserWarning" | cut -c1-240; }
rm -rf r396-p397g
CHARON_SETTINGS_V10=124,25 WAIT=3 STEP=3 KEYS="c insert,t insert,f9 insert,l insert,l insert,f10 insert,f11 ret insert,home insert,pgup insert,o insert,o insert,o,insert,pgup insert,pgdn insert,del" \
  timeout 900 python3.15 charon-r396-pick.py "$E" p397g GRUB 2>&1 | filter
rm -rf r396-p397s
CHARON_SETTINGS_V10=124,25 WAIT=10 STEP=4 KEYS="h,e,l,p,ret c,l,s,ret m,a,p,ret" \
  timeout 900 python3.15 charon-r396-pick.py "$E" p397s "Shell" 2>&1 | filter
