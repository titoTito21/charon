# farewell-test.sh DRIVERDIR : put the driver on deb2's ESP, restart, GRUB "reboot", report trace + tones
export MSYS_NO_PATHCONV=1
python3.15 charon-vm.py quit deb2 >/dev/null 2>&1; sleep 3
python3.15 charon-r400-esp.py deb2/blank-disk.img put /EFI/HAK/CHARONVOICE.EFI $1/CHARONVOICE.EFI 2>&1 | tail -1
python3.15 charon-vm.py restart deb2 >/dev/null 2>&1; python3.15 charon-vm.py wait deb2 "countdown tick" 120 >/dev/null 2>&1
python3.15 charon-vm.py keys deb2 0.3 c >/dev/null; sleep 6; python3.15 charon-vm.py keys deb2 0.25 r e b o o t ret >/dev/null 2>&1; sleep 14
python3.15 farewell-check.py
