# Charon — a speaking UEFI boot manager and screen reader

Charon is a UEFI application for blind and visually impaired users. It speaks
from the moment firmware starts it, lets you choose what to boot by voice, and
keeps reading the screen of whatever it starts — other boot managers, firmware
setup, installers and UEFI tools — until the operating system takes over.

Its interface and messages are in Polish and English.

## What it does

**Boot manager (CHARON.EFI)**
- Spoken menu (Polish/English, TVox speech engine, HDA audio), interface sounds and
  optional melodies; settings for rate, pitch, volume, verbosity, welcome.
- Boot device list from `BootOrder` with disk type and the boot manager found on each
  disk; removable and BIOS-only media found by Charon; network (PXE/HTTP) entries.
- Boot order editor (grab/move/drop), firmware timeout, one-time boot, Continue target.
- System tools: computer information (SMBIOS), clock and clock editor, wake-up alarm,
  read-only firmware variables, UEFI drivers (with health), ESRT, connect all drivers,
  start any EFI program from a file, restart, shut down, restart into firmware setup.
- Its own speaking UEFI settings browser (HII) and speaking UEFI Shell.
- An optional "explicit" message mode (deliberately vulgar wording).

**Screen reader (in CHARON.EFI, or CHARONVOICE.EFI as a Driver#### add-on)**
- Text consoles: menus, selection, framed windows, controls, review cursor, say all,
  NVDA-style Insert+numpad and Ctrl+Alt shortcuts.
- Graphical text recognised from pixels: GRUB (gfxterm, themes), rEFInd, Windows Boot
  Manager / Memory Diagnostic / Recovery (Segoe fonts read from the started disk at
  run time), systemd-boot, Limine, OpenCore, Clover, Ventoy, Syslinux, iPXE, the Haiku
  and FreeBSD loaders; progress bars as tones or speech; Windows image loading.
- As an add-on, `CHARONVOICE.EFI` alone gives speech from power-on while the
  system's own boot loader stays the default (`tools/charon-addon`).

**Since r389**
- Formatting read like Emacspeak (voice-lock and sound icons under speech), screen
  regions, speech history and "what changed", a built-in firmware pronunciation
  dictionary, braille dots 7–8 for attributes, boot-manager profiles (their keys in
  Insert+F1, configuration read from disk, hidden menus revealed).
- Optional synthesizer add-ons `\EFI\HAK\synths\*.tts` (`src/synth`, ABI in
  `src/efi/CharonSynthModule.h`): eSpeak NG and RHVoice as UEFI boot service drivers,
  SHA-256 checked; the voice of the other language is loaded the first time text in
  that language is spoken. Without them Charon speaks with its built-in TVox.
- A new boot message interrupts the one still being read (r396).

**Since r397 (current: r410)**
- Formatting is never monotone: every colour, brightness, font size, style,
  inverse/highlight and screen position moves rate, pitch, volume, modulation and
  timbre, also with eSpeak NG (persona ABI 3); Charon's own menu carries a
  position-based modulation marker. Synthetic per-event sound icons, no echo.
- Incremental reading in terminals (only new text after a clear), black screen and
  blue screen detection, description of purely graphical UEFI screens.
- Memtest86+ built in; "Restart into firmware setup" opens the firmware's UiApp or
  its HII forms in-session (full settings editor for any UEFI that publishes forms),
  rebooting only as a last resort.

**r410 (current)**
- Multi-system plans (`CharonPlanText.h`, `CharonPlanModel.h`, `CharonPlanLayout.h`,
  `CharonPlanState.h`, `CharonWsPlan.h`, `CharonWsPlanDialogue.h`, `CharonWsPlanWindows.h`,
  `CharonSeedDisk.h`, `CharonClockZone.h`, `CharonDiskIdentity.h`): several systems on one
  disk after one typed word, resumed by Charon after every restart; five proven sets are
  offered, the rest is behind the test switch `\EFI\HAK\plan-test`. CharonDOS: `LINUX`,
  `WINNT` (`src/dos/launch_select.h`), local time. Host tests
  `tests/charon-r410-plan-host.py`. Design: `docs/charon-multiboot-spec-2026-11-23.md`,
  `docs/charon-detailed-questions-spec-2026-11-23.md` (design documents, not shipped
  features).
- Windows: installation without a stick through a temporary partition
  (`CharonWsWinPart.h`; a firmware RAM disk is invisible to Windows Setup, measured);
  profiles and the unofficial Tiny11 group (`CharonWsInstall2.h`, `CharonWsInstall3.h`,
  `tools/charon-addon/Charon-WindowsProfile.ps1`, `CHARON-WINDOWS-PROFILES.txt`); network
  and voice hand-over (`Charon-Network.ps1`, `Charon-Readers.ps1`, `CharonWpaPsk.h`);
  research notes in `docs/`.
- Linux scripts run on real installations and repaired (`src/linux/*.sh`, new
  `network.sh`; `tools/gen-charon-linux-scripts.py` writes `CharonLinuxScripts.h`);
  Stormux, Linda, RHVoice and Charon's voice settings in the installed system.
- Dialogue defects: private files are never copied (`CharonPrivate.h`), larger answer
  store, host test of the generators (`tests/charon-install-answers-host.py`,
  `tests/CharonInstallAnswersHost.cpp`, golden files in `tests/golden`).
- RHVoice add-on with 13 voices (`src/synth/rhvoice/VOICES.tsv` is the one list:
  repositories, commits, licences; `tools/build-charon-synths.py`); three voices are
  CC BY-NC-ND 4.0 (non-commercial).
- Sound stalls: `CharonAudioStall.h` (StallPolicy), `CharonVoiceRest.h`,
  `CharonVoiceProvider.h`; AudioDxe r410 takes its poll clock after the stream start
  (`src/efi/audio_stream/driver/HakAudioStream.c`). Host tests
  `tests/CharonAudioStallTests.cpp`, `tests/CharonVoiceStallProviderTests.cpp`.
- Not rerun for r410: the regression of the boot managers. Known issues:
  `docs/results/charon-r410-results.txt`.

**r408 (r405-r407 were not released, their changes are part of it)**
- Charon runs from a RAM volume (`CharonSelfCopy.h`, `CharonSelfRam.h`): its files are
  copied into memory after the start, the menu loop then switches the live volume; the
  start medium can be removed or wiped. Settings are mirrored to the start medium.
- Self-installer (`CharonWsSelfInstall.h`): reader alone / spare entry / first; own EFI
  partition in free space when the EFI partition is too small.
- Driver pack (`CharonDriverPack.h`, `CharonWsNet.h`, `tools/make-charon-driverpack.py`,
  `tools/make-charon-netpack.py`, `tools/build-charon-netdrv-*.py`): loading rules per
  view, documents `DRIVER-PACK.txt` and `NETWORK-DRIVERS.txt`.
- Sound: HD Audio codec families and machine table generated from Linux
  (`tools/gen-charon-hda-quirks.py`, `src/efi/audio_stream/driver/HakHdaQuirk*`,
  `src/efi/AUDIO-CODECS.txt`; the driver is GPL-2.0-or-later since r408), USB Audio Class
  1/2/3 (`CharonUsbAudio*.h`, `src/efi/USB-AUDIO.txt`), ES1371 (`CharonEs1371.h`),
  liveness guards for AC'97/ES1371, `HakHdaControllerMayShare`, Busy guard against nested
  voice calls.
- VeraCrypt pre-boot screen (`CharonSecretInput.h`); Wi-Fi scan token fix.
- CharonPC (`src/pc`): unattended Windows 9x Setup (`pc_unattend.cpp`), control reader
  (`pc_winread.cpp`, `pc_reader.cpp`, `pc_winfonts.cpp`), SB16 + PC speaker
  (`pc_sound.cc`), keyboard and power-button keys, per-host speed calibration.
- CharonDOS: `CHARON` command (`src/dos/charon_cmd.cpp`, `CHARON-COMMANDS.txt`), sound.
- NVDA inside Windows Setup (`src/windows`, `tools/gen-charon-nvda-setup.py`,
  `CharonWsNvdaSetup.h`).
- Scripts: `tools/charon-addon` (second boot entry, `Charon-MakeRoom.ps1`,
  `Charon-SecureBoot.ps1`: vendor interface with the owner's password only),
  `tools/gen-charon-linux-scripts.py` (desktops with accessibility), `tools/charon-os`
  (settings and boot-manager programs for Windows and Linux, host tests).
- Results: `docs/results/charon-r405-results.txt` .. `charon-r408-results.txt`,
  regression `docs/results/charon-r408-regression.txt`.

**r404**
- `DisconnectFirmwareHda` (`src/efi/CharonAc97.h`): a firmware HD Audio driver (VMware,
  Apple) is disconnected before AudioDxe connects; sharing the controller crashed VMware.
- HAK stream (`src/efi/audio_stream/driver`): poll gap limit 70 ms (was 40), maximum
  cursor advance 3584 frames, LPIB equal to CBL read as position 0.
- Codec gain at its maximum step (was -12 dB).
- Results: `tests/charon-r404-results.txt`.

**r403**
- CharonDOS as a separate system (`CharonWsDosSystem.h`): GPT/FAT32 writer, firmware
  Boot/Driver options (CharonDOS first, Charon after it, Charon voice driver),
  optional Windows 11 beside it (`Install-Charon.ps1 -FirstEntry`). CharonDOS
  commands `WINNT` (start the installed Windows Boot Manager) and `WIN [profile]`.
- `CHARONPC.EFI` (`src/pc`, `tools/build-charon-bochs.py`): Bochs 3.1 as a UEFI
  application with musl and libc++, text modes on the console, graphics on GOP,
  COM1 Braille 'n Speak speaking with Charon's voice; profiles from `CHARONPC.CFG`
  (`src/pc/PLAN.txt`). Windows 95/98/ME/XP only from the user's own media; reader
  catalogue `src/pc/WIN9X-READERS.txt`.
- Screen reader check boxes in the install dialogues (`CharonWsReaders.h`): Orca,
  Cthulhu, Speakup, Fenrir, Emacspeak, desktops; NVDA, JAWS, Narrator for Windows.
- ReactOS dropped (findings in `tests/charon-r403-results.txt`, points 5-7).
- Results of the trials: `tests/charon-r403-results.txt`.

**r402**
- Fixes: synthesizer switching from Charon's own screens (a nested add-on load
  overflowed the loaded-module table); the voice driver crashing firmware at
  power-on when installed with a "force reconnect" driver option (r400, r401);
  the reader being deaf in programs that stay at `TPL_CALLBACK` (iPXE,
  netboot.xyz) — a rescue timer one level higher takes over.
- The install dialogue is arranged in groups, and every entry states whether
  the system speaks: Windows (11, 10, evaluation images incl. LTSC and Server;
  first-logon profiles), Debian/Ubuntu installers, Arch with speech from the
  network, bootable ISOs to a stick or a RAM disk, ready disk images, tools
  (netboot.xyz and rescue systems), CharonDOS.
- Ready disk images (`CharonWsImages.h`, `CharonWsSeed.h`, `CharonQcow2.h`,
  `CharonSeedDisk.h`): raw or qcow2 images are written while they download,
  the GPT is repaired, and a cloud-init `CIDATA` partition written by Charon
  sets up the user, console speech and Charon itself. Dual boot: the image's
  partitions can be placed into free space beside existing systems.
- A text web browser (`CharonWsBrowser.h`, `CharonHtmlText.h`); 118 trust
  anchors from Mozilla's list.
- CharonDOS downloads tested free DOS programs (`CharonDosCatalog.h`,
  `CharonWsDosApps.h`, `CharonZip.h`).
- Results of the trials: `tests/charon-r402-results.txt`.

**r401**
- Every Charon screen (menu, settings, workspace, install dialogue) answers the
  reader's shortcuts: the reader runs beside Charon in a review-only mode
  (`ResidentOptions.reviewOnly`, work done from the key hooks, `BootMenu.h`
  `ReviewReaderOn`). The install dialogue is written on the screen.
- One progress feedback for the reader and Charon's own tasks (`CharonProgress.h`);
  one position voice marker for the menu and workspace lists (`CharonMenuVoiceMark.h`).
- Ubuntu network install tested end to end; the installer itself speaks
  (`liveSpeechScript` in `CharonWsInstall2.h`).
- Windows 11 install media made by Charon on a FAT32 USB stick: UDF reader
  (`CharonUdf.h`), on-the-fly split of `install.wim` into `.swm` parts
  (`CharonWimSplit.h`), streaming writer (`CharonWsWinUsb.h`); choice between USB and
  RAM disk. Tested with a synthetic image and wimlib; not with Microsoft's image.
  Host tests: `tests/CharonUdfTests.cpp`, `tests/CharonWimSplitTests.cpp`.
- The test harness runs on the Windows hypervisor (`accel=whpx`, 4 processors).

**r400**
- Charon workspace (`src/efi/CharonWorkspace.h`, `CharonWs*.h`, guide in
  `src/efi/WORKSPACE.txt`): launcher for every `*.efi`, Dired-style files, speaking
  editor, hex viewer, calculator, command palette, packages on any volume.
- Network: DHCP, http and https downloads (own TLS client on BearSSL,
  `CharonHttps.h`), optional drivers from `\EFI\HAK\drivers`; disc images started
  from memory through `EFI_RAM_DISK_PROTOCOL`.
- Installing a system from the network: spoken questionnaire, installer loaded into
  RAM, Charon copied to the new EFI partition, reader registered as `Driver####`
  and "Charon" as a boot entry after the system's own. Debian verified end to end
  in QEMU; **Windows 11 and Ubuntu paths are written but untested**.
- CharonDOS (`src/dos`): 8086/80186 real-mode emulator with DOS/BIOS services, text
  mode, as a package.
- Reader add-on: rising chime and "Charon." at power-on, falling chime and "Charon
  is turning off." before a firmware reset; quiet menus while a countdown runs;
  optional cue that keeps playing after `ExitBootServices` (audio stream rev 3).

**Limits** — nothing can speak after `ExitBootServices` (the OS must take over) or
before firmware loads drivers (POST). x64 UEFI only. Tested in QEMU with OVMF, not
yet on physical machines. See `src/efi/FIRMWARE-COVERAGE.txt`.

## Layout

| Path | Contents |
|------|----------|
| `src/efi` | Charon sources (C++17, freestanding UEFI), EFI ABI headers, AudioDxe streaming extension, Shell bridge |
| `src/tts` | TVox freestanding speech runtime (Polish and English) |
| `src/synth` | Synthesizer add-on modules (eSpeak NG, RHVoice) and their freestanding runtime; `tools/build-charon-synths.py` builds them |
| `src/sfx` | Interface sounds |
| `tools` | `build-charon-windows.py` (LLVM build), font/glyph generators, sound preparation, UEFI add-on installers |
| `tests` | Host unit tests (C++) |
| `harness` | QEMU/OVMF test machines: `charon-vm.py`, regression and add-on sweeps |
| `docs/results` | Test records of every release round (Polish) |
| `licenses` | Notices of every third-party component |

## Building

Windows, LLVM/Clang 23 and the AUDK `ImageTool` (see the header of
`tools/build-charon-windows.py` for the paths it expects):

```sh
python tools/build-charon-windows.py build/charon            # CHARON.EFI
python tools/build-charon-windows.py build/voice --driver    # CHARONVOICE.EFI (add-on)
```

The build checks the PE image (relocations, no imports, NX_COMPAT, read-only data).
`AudioDxe.efi` is built from pinned OpenCorePkg sources with the extension in
`src/efi/audio_stream` (see its README); `CharonShell.efi` from `src/efi/shell`.

Host tests build with any C++17 compiler; give an include directory that holds
`src/efi/haiku` under the name `efi`:

```sh
clang++ -std=c++17 -I inc -I src/efi tests/BootPreOSMenuTests.cpp -o menu-tests && ./menu-tests
```

## Boot CD

`harness/make-charon-iso-r410.py` builds `Charon-r410.iso`: a UEFI El Torito CD
(platform EFI, no emulation) with only Charon on it (boot FAT image:
`EFI/BOOT/BOOTX64.EFI`, `EFI/HAK` voice driver, audio driver, Shell, Memtest86+,
sounds, synthesizer add-ons) and an isohybrid MBR so the same file can be written
raw to a USB stick. The FAT image is larger than 32 MB, so the catalogue's sector
count is 1 (edk2 then uses the rest of the disc as the partition).
`harness/test-charon-iso-r410.py cd|usb` boots it on OVMF with no hard disk.

## Testing in QEMU

`harness/charon-vm.py prepare NAME CHARON.EFI [--grub grubx64.efi] [--refind] [--opencore] ...`
starts a q35/OVMF machine with audible output (set `CHARON_QEMU` to your QEMU).
Trace builds (`--trace`) write everything Charon says to the serial log;
`harness/charon-said.py serial.log` prints it. Boot managers, ISOs and Windows boot
files are not included: download them yourself (the harness names the versions used).
`harness/r396-regression.sh` starts every boot manager of the test set, picked by the
name Charon says in its device list (`harness/charon-r396-pick.py`), presses arrows and
prints what is spoken; `harness/make-charon-testvm.py` builds one test machine with all
of them.

## Licenses

Charon's own code is GPL-2.0-or-later (`LICENSE` holds the GPL-2.0 text). TVox is
MIT-0; Charon builds `tvox_dsp_mit.c`, not the GPL-3.0 upstream `tvox_dsp.c`.
Third-party components and generated font data keep their own licenses, listed in
`licenses/COMPONENTS-AND-SOURCES.txt`; the synthesizer add-ons' licences are in
`licenses/synth-addons`. No Microsoft fonts or binaries are included.
