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

**Since r397 (current: r399)**
- Formatting is never monotone: every colour, brightness, font size, style,
  inverse/highlight and screen position moves rate, pitch, volume, modulation and
  timbre, also with eSpeak NG (persona ABI 3); Charon's own menu carries a
  position-based modulation marker. Synthetic per-event sound icons, no echo.
- Incremental reading in terminals (only new text after a clear), black screen and
  blue screen detection, description of purely graphical UEFI screens.
- Memtest86+ built in; "Restart into firmware setup" opens the firmware's UiApp or
  its HII forms in-session (full settings editor for any UEFI that publishes forms),
  rebooting only as a last resort.

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

`harness/make-charon-iso-r399.py` builds `Charon-r399.iso`: a UEFI El Torito CD
(platform EFI, no emulation) with only Charon on it (boot FAT image:
`EFI/BOOT/BOOTX64.EFI`, `EFI/HAK` voice driver, audio driver, Shell, Memtest86+,
sounds, synthesizer add-ons) and an isohybrid MBR so the same file can be written
raw to a USB stick. The FAT image is larger than 32 MB, so the catalogue's sector
count is 1 (edk2 then uses the rest of the disc as the partition).
`harness/test-charon-iso-r399.py cd|usb` boots it on OVMF with no hard disk.

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
