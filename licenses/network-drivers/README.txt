Charon optional network drivers (\EFI\HAK\drivers on any FAT volume).
net-*.efi: edk2 NetworkPkg modules, BSD-2-Clause-Patent (see edk2-licenses.txt of QEMU).
nic-*.efi: iPXE UEFI network card drivers, GPL-2.0-or-later (https://ipxe.org, source: https://github.com/ipxe/ipxe).
wifi-*.efi: put a UEFI Wi-Fi driver of your card here if its maker ships one; Charon loads it.
Charon loads nic-*/wifi-* only when the firmware drives no adapter, and net-* only when adapters have no IPv4 service.
