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

Install-Charon.ps1 (boot manager and reader), r408
- Zainstaluj-Charona.bat starts it. Modes: -Mode Reader (as above), Shim,
  Manager (Charon boot manager plus the reader from power-on).
- Boot order: without -Default the Charon boot manager is the SECOND boot
  entry. The system stays first: the entry named by -FirstEntry when it
  exists (an unattended Windows installation made by Charon passes
  "Windows Boot Manager"), otherwise the entry that is first now. "Charon"
  follows, the other entries keep their order, each entry once. Running the
  installer again writes nothing. -Default puts Charon first; -FirstEntry
  CharonDOS (Charon's CharonDOS dialogue) puts CharonDOS first.
- Secure Boot on and no shimx64.efi / mmx64.efi next to the installer or in
  EFI\HAK: the boot manager is installed anyway, without shim, signed with
  this computer's certificate like the reader, as the second entry. Windows
  starts as before; Charon works once the certificate is in db or Secure
  Boot is off. (With -Default the installer still stops: a first entry the
  firmware refuses would be in the way.)
- The EFI partition is mounted on a letter that is no drive of any kind (or
  the letter it already has); its number, first block and size for the
  firmware entries are read from the GPT on the disk.
- Log: %ProgramData%\Charon\install-charon.log (English). A boot entry that
  cannot be written is reported and logged, the reader is still installed,
  and the installer ends with exit code 1.

Turning Secure Boot off with the maker's own interface (optional, r408)
- Why: Charon is not signed by Microsoft, and a blind owner cannot use the
  firmware setup screen. Makers of business computers publish an interface
  for changing firmware settings from Windows with the firmware supervisor
  password. Charon-SecureBoot.ps1 uses that and nothing else, for the one
  setting "Secure Boot". It is not a way around Secure Boot: it never
  writes the key databases (PK, KEK, db, dbx), never makes unsigned code
  trusted, never sets or guesses a password, never acts silently.
- How: Install-Charon.ps1 -DisableSecureBoot, or answer yes when the
  installer asks at the end (default: no). Never in an unattended
  installation. It explains the risks, asks for a typed word (DISABLE,
  Polish: WYLACZ) and the supervisor password, suspends BitLocker for one
  restart, calls the maker's interface once, and reads the setting back.
  The change takes effect after a restart.
- Before agreeing: some firmware asks for a confirmation on its own screen
  at the next start (not read by Charon yet: a sighted person may be
  needed); changing Secure Boot can make BitLocker ask for the recovery key
  (aka.ms/myrecoverykey).
- Lenovo ThinkPad, ThinkCentre, ThinkStation: supported, through Lenovo's
  WMI interface built into the firmware (nothing is downloaded). Lenovo:
  Secure Boot can be changed from Enable to Disable only when a supervisor
  password exists, and a password cannot be created from Windows. Without
  one the installer says so and changes nothing.
      https://docs.lenovocdrt.com/ref/bios/wmi/wmi_guide/
      https://docs.lenovocdrt.com/ref/bios/settings/thinkpad/secureboot/
- Lenovo IdeaPad, Legion, Yoga, LOQ, ThinkBook: declined (Lenovo documents
  the interface for the three Think lines only).
- Dell: declined. Dell's own tool cannot do it: "The SecureBoot feature can
  be disabled only from the BIOS setup screen."
      https://dl.dell.com/topicspdf/command-powershell-provider_users-guide_en-us.pdf
- HP: not implemented (a marked stub): the setting names and HP's
  confirmation at the next start could not be confirmed from HP's
  documentation. ASUS, Acer, MSI and others: declined, no documented
  interface.
- Declined means: the installer says why and does nothing. What remains is
  firmware setup (sighted help, or a camera app that reads the screen) or
  shim with a MOK key, which keeps Secure Boot on.
- Not tried on a computer: the Lenovo calls themselves. Detection and the
  dialogue were tried with stand-ins for the firmware interface.

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
