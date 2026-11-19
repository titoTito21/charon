Charon as a UEFI add-on (screen reader without Charon's menu)
=============================================================

What it is
- Only the driver CHARONVOICE.EFI, loaded by the firmware from a Driver####
  entry at every power-on. The default system (e.g. Windows) stays the
  default: boot order, BCD and Windows Boot Manager are not changed. Charon
  does not have to be the boot program.
- After power-on: a short beep (like the PC speaker after POST), then
  "Charon: speech on from power-on." Charon reads the firmware screens,
  Windows Boot Manager (menu, recovery screen), GRUB, rEFInd, systemd-boot,
  Limine, OpenCore, Clover and other UEFI programs. When a boot manager
  starts it plays "boot manager loaded" and says its name; when the system
  takes over it plays "system will launch" and says "The system is taking
  over." From then on the system's own reader (Narrator, NVDA, Orca) reads.
- Reader keys as in Charon (Ctrl+Alt+R repeat, Ctrl+Alt+S silence,
  Insert + numpad digits, Insert+F1 key list, etc.).

Requirements
- The computer starts in UEFI mode (not BIOS/CSM), GPT disk.
- SECURE BOOT. Charon's driver is not signed by Microsoft. With Secure Boot
  on, the installer signs copies of CHARONVOICE.EFI and AudioDxe.efi with a
  certificate of THIS computer (made once, private key not exportable) and
  saves it as \EFI\HAK\charon-db.cer on the EFI partition and charon-db.cer
  next to the installer. The firmware loads the driver only after that
  certificate is added to the db list in firmware setup (usually Security /
  Secure Boot > Key Management > db > Append Key / Enroll from file > pick
  the file on the EFI partition). Microsoft's keys stay, Secure Boot stays
  on, Windows starts as before. Until then the firmware just skips the
  driver.
  This one step cannot be done from Windows: only the owner (in firmware
  setup) or the manufacturer (with its key) can change db. That is how
  Secure Boot works. Open firmware setup from Windows with
      shutdown /r /fw /t 0
  The firmware screen is not read yet at that point (Charon speaks only once
  the certificate is enrolled), so do this step once with a camera app that
  reads the screen (Be My Eyes, Seeing AI) or a sighted helper. The key
  sequence depends on the manufacturer.
  Without Secure Boot no signature is needed (-Sign forces one anyway).
- BITLOCKER: changing db, turning Secure Boot off or adding a firmware
  driver changes the measured boot, so Windows may ask for the recovery key.
  Before changing firmware settings suspend BitLocker (as administrator):
      Suspend-BitLocker -MountPoint C: -RebootCount 3
  or keep the recovery key at hand (Microsoft account: aka.ms/myrecoverykey).
  The installer does this itself with -SuspendBitLocker (for one restart).
- About 22 MB free on the EFI partition.
- Language: English by default; -Language pl (Linux: CHARON_LANGUAGE=pl)
  for Polish, or Settings -> Language in Charon.

Installing
- Windows: Zainstaluj-dodatek-UEFI.bat (asks for administrator rights).
  Install-CharonAddon.ps1 options: -Language pl|en, -SuspendBitLocker,
  -Status (installed or not), -Uninstall [-KeepFiles].
- Linux: sudo sh zainstaluj-dodatek-uefi.sh (needs efibootmgr; not yet
  tried on a Linux computer - please report the result).
- UEFI Shell (by hand): copy EFI\HAK to the EFI partition, then
      bcfg driver add 0 fsN:\EFI\HAK\CHARONVOICE.EFI "Charon voice"
  (fsN: is the EFI partition).
- Uninstalling: Odinstaluj-dodatek-UEFI.bat (removes the Driver#### entry
  and the add-on files; a full Charon installation on the same partition is
  left alone).

What was tested (QEMU/OVMF, no physical hardware)
- The Driver#### entry built by the installer (-DryRun, short path
  HD(GPT partition)\EFI\HAK\CHARONVOICE.EFI, the same as under Windows)
  written into clean firmware; a disk with Windows Boot Manager as the only
  program: speech from power-on, beep, sound and boot manager name, Windows
  menu and error screen read. Memtest86+ as the default: name, "system will
  launch" and the message at ExitBootServices.
- The installer's Authenticode signature checked by an independent program
  (PE image hash, messageDigest, RSA signature); not checked in firmware with
  Secure Boot on.
- Not tested: the installer on a real Windows computer (it changes that
  computer's firmware variables), the Linux script, physical mainboards (HDA
  sound driver, vendor firmware).
