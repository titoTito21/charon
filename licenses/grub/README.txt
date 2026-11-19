GRUB 2.12 (Debian 13 package grub-efi-amd64-signed 1+2.12+9+deb13u4), file
usr/lib/grub/x86_64-efi-signed/grubx64.efi.signed, used unchanged as
templates/grub-disk.img EFI/BOOT/BOOTX64.EFI and components/grub/grubx64.efi.
License: GNU GPL version 3 or later (GPL-3.0.txt). Complete corresponding
source: source/grub2_2.12.orig.tar.xz, source/grub2_2.12-9+deb13u4.debian.tar.xz
and source/grub2_2.12-9+deb13u4.dsc (SHA-256 sums inside the .dsc), from
https://deb.debian.org/debian/pool/main/g/grub2/. The Debian signature is
Debian's; Charon does not use Secure Boot in this machine.
PROBE.EFI is Charon's own test program, source tests/CharonPostbootProbe.cpp
in the corresponding source of Charon (GPL-2.0-or-later).
grub.cfg on that disk was written for this test machine.
GRUB SHA-256: 970bccf30d50a10b6f8559474a8de0f61218431c0f969a22231f8c8542573565

unicode.pf2 (templates/grub-disk.img boot/grub/fonts/unicode.pf2 and
components/grub/unicode.pf2) is usr/share/grub/unicode.pf2 from Debian 13
grub-common 2.12-9+deb13u4 (copyright: debian-grub-common-copyright.txt),
built by GRUB from GNU Unifont; GNU Unifont is GPL-2.0-or-later with the
GNU font embedding exception, or OFL-1.1 (see ../unifont/README.txt).
unicode.pf2 SHA-256: 662eb86fc1ea89635e4bba8413590b79eb714ade5a2b5a5aa3dc5cf5f53a4463
