Charon programs for the running system (tools/charon-os)
========================================================

Charon is a speaking UEFI boot manager and pre-boot screen reader. These programs let
a blind user change Charon from Windows or Linux instead of in the pre-boot menu:

  Charon screen reader settings   every setting of Charon's menu voice and screen reader
                                  that the settings record holds, the reader add-on, the
                                  reader-only marker, restore defaults, what is installed
  Charon boot manager             the firmware boot order and Charon's place in it, the
                                  Continue target, Charon's menu at the next restart only,
                                  restart into firmware setup, Secure Boot state

Messages are in English and Polish (English by default; Polish when the system or
\EFI\HAK\audio-language is Polish; both programs can switch while running).


Files
-----
windows\Charon-Settings.ps1       Charon screen reader settings
windows\Charon-BootManager.ps1    Charon boot manager
windows\CharonOs.Common.ps1       shared code (EFI partition, firmware variables, questions)
windows\CharonOs.Fields.ps1       the table of settings (generated, see below)
windows\Charon-Settings.cmd       launchers: start the program with administrator rights
windows\Charon-BootManager.cmd
windows\Install-CharonTools.ps1   copies the programs to %ProgramFiles%\Charon and creates
                                  the Start-menu shortcuts; -Uninstall removes them
linux/charon-settings             Charon screen reader settings (Python 3.6+, standard library)
linux/charon-bootmanager          Charon boot manager (Python 3.6+, standard library)
linux/charon-settings.desktop     desktop menu entries (Terminal=true)
linux/charon-bootmanager.desktop
linux/install-charon-tools.sh     installs to /usr/local (POSIX sh); --uninstall removes
tests/                            host tests, see "Tests"

Where the files go (the names other parts of the release rely on):
  in the tree          tools/charon-os/windows/*, tools/charon-os/linux/*
  on Charon's media and on the EFI system partition
                       \EFI\HAK\os\windows\ and \EFI\HAK\os\linux\
  installed, Windows   %ProgramFiles%\Charon\ ; Start menu (all users): "Charon screen
                       reader settings", "Charon boot manager"
  installed, Linux     /usr/local/bin/charon-settings, /usr/local/bin/charon-bootmanager,
                       /usr/local/share/applications/charon-settings.desktop,
                       /usr/local/share/applications/charon-bootmanager.desktop

The system installers call windows\Install-CharonTools.ps1 or linux/install-charon-tools.sh
when the folder exists. Both can be run again at any time with the same result.


Windows
-------
Windows PowerShell 5.1 as shipped with Windows 10 and 11; no modules, no PowerShell 7.
Start a program with its .cmd file or its Start-menu shortcut. Both ask for administrator
rights, because the EFI system partition and firmware variables need them. The launchers
pass "-ExecutionPolicy Bypass" to that one process; the policy of the system is not changed.

Without switches a dialog opens. It is built from standard Windows Forms controls only:
check boxes, drop-down lists, track bars, a list box, buttons with access keys, a default
and a cancel button. Every field has its label directly before it in the tab order and
carries the same text as its accessible name, so NVDA, JAWS and Narrator say it. A track
bar reports its value as the text Charon speaks ("50%"), not as a raw step. The pages of
the settings dialog are tabs (Ctrl+Tab); they follow the pages of Charon's own menu.

  Charon-Settings.cmd -Console        the same functions as numbered text menus
  Charon-BootManager.cmd -Console
  -Language en | pl                   language of the program

The EFI system partition is reached through its volume path (\\?\Volume{...}\), so no
drive letter is taken. Only when Windows offers no such path is the partition attached to
an empty folder under %ProgramData%\Charon, and that is removed when the program ends.

Secure Boot: Charon-BootManager.ps1 itself shows the state only; no maker logic is in this
folder. When Charon-SecureBoot.ps1 (owned by the Windows installer, tools/charon-addon) lies
beside the program (Install-CharonTools.ps1 copies it when it finds it) or in
..\..\charon-addon, it is dot-sourced: Get-CharonSecureBootState says whether this computer
has a maker's interface, and the Secure Boot item offers Disable-CharonSecureBoot, which
explains, asks for a typed word and the owner's firmware supervisor password and suspends
BitLocker itself. Its texts and questions go through this program's dialog or text menu
(-Say, -Ask, -AskSecret); on stand-in firmware it is called with -NoFirmware (trial mode).

BitLocker: before a change of the boot order, of BootNext or of the reader add-on the
programs check BitLocker on the system drive and, when it is on, offer to suspend it for
one restart, go on without suspending, or cancel.


Linux
-----
Python 3.6 or newer, standard library only (Debian, Ubuntu, Fedora, Arch). The programs
are plain line-oriented text programs for a terminal: numbered menus, one prompt per line,
no curses and no cursor addressing, for Speakup, Fenrir, Orca and braille displays. A
menu is listed once; after a change only the changed line is printed, and L lists it again.

They need root. Started as an ordinary user they say why and start themselves again
through sudo (or pkexec). The EFI system partition is looked for in /boot/efi, /efi, /boot
and among the mounted FAT file systems (findmnt); --esp DIR names it directly. Nothing is
ever mounted by the programs.

Firmware variables are read from efivarfs and changed with efibootmgr, the tool the Charon
installers use; the restart into firmware setup uses bootctl or efivarfs.


What the programs change, and what they never touch
---------------------------------------------------
On the EFI system partition, only these files in \EFI\HAK:
  charon-settings-a.bin, charon-settings-b.bin   the settings record
  reader-only                                    the reader-only marker
  continue-set, continue.bin                     the Continue target (boot manager program)
Nothing else is created, rewritten or deleted there.

Firmware variables: DriverOrder and the Driver#### of the option "Charon voice" (reader
add-on on or off), BootOrder, BootNext, OsIndications. Every such change is described
and asked first (the default answer is no) and read back from firmware afterwards. No
Boot#### entry is created or deleted by these programs.


The settings record
-------------------
Established from the sources, not guessed:
  src/efi/CharSettings.h   the record: magic "CHARS00" + format character, sequence number,
                           settings, CRC-32; formats 1-9, A, B, C, D, E (V1..V14), sizes
                           32, 40 and 48 bytes; ranges (EncodeSettings, DecodeSettings),
                           defaults (struct CharSettings), which of two records wins
                           (ChooseSettings)
  src/efi/BootMenu.h       the two slot files and the save procedure (SettingsSlot,
                           ReadSettingsSlot, LoadCharSettings, SaveCharSettings, lines
                           28-61); the names of the settings in Polish and English
                           (labelFor, lines 1118-1300); the reader add-on (lines 839-915);
                           the Continue target (ReadContinue, SaveContinue, lines 334-491)
  src/efi/BootMenuModel.h  which bit belongs to which menu row
  src/efi/ContinueTarget.h, src/efi/BootOption.h   the Continue record and load options
  src/efi/CharonSynthHost.h   synthesizer add-ons: manifests and the key stored in byte 42

The programs follow Charon's own procedure. They read both slots and take the newer valid
one; a slot must hold a sequence number of its own parity (a: odd, b: even). They write the
next sequence number to the slot of its parity only: that file is deleted, created,
written, flushed to the disk, read back and compared; the other slot, which holds the last
complete record, stays as it is. A setting the user did not touch keeps its bits.

Format written: the one the installed boot manager writes, read from \EFI\HAK\HAKBOOT.EFI
(its encoder carries "CHARS00" and the format character; checked on r407 builds), otherwise
the format of the record on the partition, otherwise -Format / --format, otherwise E. A
setting that the format does not store is shown as such and cannot be changed. When a
record or the installed Charon uses a format newer than E, the programs show what they
can and write nothing.

Formats, all read and all written (the format written is chosen as above):
  1-6   32 bytes; 7, 8   40 bytes; 9, A, B, C, D, E   48 bytes (CRC in the last four)
  C     r397 (V12). Byte 41 bit 7, "Loading sound after the system takes over", is
        accepted by Charon since r408; r397 to r407 reject a record that has it. The
        programs read it, keep it as the record has it and never turn it on in formats A
        to C; a new format C record gets bytes 40, 41 = 255, 25.
  D     r410 (V13): byte 36 bits 2-4 formatting groups (kind of element, state and
        warnings, position and structure), byte 38 bits 2-3 verbosity profile 0 normal,
        1 beginner, 2 expert. New records: byte 40 = 252 (formatting off), byte 41 = 153.
  E     r410 (V14), what Charon r410 writes: profile 3 (custom) allowed; the custom
        profile's 12 items in byte 32 bits 3-7 (items 0-4), byte 33 bits 2-7 (items 5-10)
        and byte 37 bit 3 (item 11); byte 37 bit 4 reader bar, bit 5 absolute colours.
The old settings that share those bytes are read through the masks of DecodeSettings:
detail = byte 32 & 7, timers = byte 33 & 3, punctuation = byte 36 & 3, capitals = byte
37 & 7, screen text language = byte 38 & 3. Every bit of a format E record is either one
of the settings in tests/charon_fields.py or must be zero (DecodeSettings rejects the
record otherwise), so a record that the programs rewrite keeps all it held: only the bits
of the setting that was changed, the sequence number and the CRC differ.

A record with a right frame and check sum whose values Charon rejects is reported as
invalid and can be repaired (Repair): values out of range take their defaults and the
record is saved under the next sequence number.

The table of settings (names, bits, ranges, first format) is tests/charon_fields.py.
linux/charon-settings carries a copy; windows\CharonOs.Fields.ps1 is generated from it:
  python tests/gen-windows-fields.py


Options for scripts and tests
-----------------------------
Both systems (Windows spelling in brackets):
  --esp DIR  [-EspPath DIR]            an ordinary folder stands in for the EFI partition
  --no-firmware  [-NoFirmware]         never call the firmware interfaces
  --firmware-dir DIR  [-FirmwareDir]   a folder of files in the Linux efivarfs format
                                       stands in for firmware variables (tests only)
  --lang en|pl  [-Language]            --yes  [-Yes]  answers the confirmation with yes
Settings:
  --show  [-Show]                      print the record as name=value lines
  --set name=value  [-Set a=1,b=2]     change settings and save
  --defaults, --repair, --format X, --reader-only on|off, --addon on|off|status, --synths
Boot manager:
  --list, --move XXXX up|down, --charon first|second|remove|restore, --continue XXXX,
  --bootnext-charon  [-BootNextCharon], --firmware-setup, --restart
Exit codes: 0 done, 1 error, 2 no EFI partition or no Charon on it, 4 invalid record,
5 request refused, 6 newer format than this program knows, 7 no firmware access.


Tests
-----
  python tests/run-tests.py [--work NEW_FOLDER] [--quick] [--only python|powershell]

Runs on the build host with Python 3 and Windows PowerShell 5.1; needs the tree's Clang
(work/llvm-23.1.5/bin, or CHARON_LLVM). Nothing in it touches the firmware or the EFI
system partition of the host: the programs run on scratch folders with --esp and
--firmware-dir.

tests/CharonOsSettingsHost.cpp is Charon's own code on the host: CharSettings.h,
ContinueTarget.h and BootOption.h are included unchanged; SettingsSlot, ReadSettingsSlot,
LoadCharSettings and SaveCharSettings are cut out of BootMenu.h by run-tests.py, Write out
of HAKBootEFI.cpp, KeyOf out of CharonSynthHost.h; the tree's efi_file_protocol
(src/efi/haiku, included as <efi/...>) is served from an ordinary folder. The tests compare,
for both the PowerShell and the Python program: records the program wrote against Charon's
EncodeSettings byte for byte and through LoadCharSettings; records Charon's
SaveCharSettings wrote as the program reads them; both writing in turns; older formats;
formats D and E (the new bits survive a change byte for byte, the masks, which bits each
format accepts, a boot manager image with CHARS00E, an upgrade of a format D record
against SaveCharSettings); refusals (format F); the repair; synthesizer keys; the Continue file through ParseContinueTarget; the
boot manager functions on the firmware stand-in; both installers. tests/normalize-windows.py
gives the Windows files their release form (UTF-8 with BOM and CRLF for .ps1, CRLF for .cmd).
The dialogs are built without being shown (-GuiSelfTest) and their controls, names, tab
order and track bar values are checked. tests/RESULTS.txt is the output of the last run.

Not covered by the host tests, because they need real firmware or a real screen reader:
the Windows firmware calls and the access to a real EFI system partition, BitLocker,
efivarfs, efibootmgr, bootctl, sudo/pkexec, and speech output of NVDA, JAWS, Narrator,
Orca, Speakup and Fenrir with the programs.

GPL-2.0-or-later.
