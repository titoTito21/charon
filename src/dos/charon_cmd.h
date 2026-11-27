// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - the CHARON command: query and change Charon itself (its
// pre-boot screen reader and its boot manager) from the CharonDOS prompt,
// spoken by Charon's reader.
//
// CHARON is a native shell built-in, dispatched exactly like WIN / WINNT
// (shell.cpp). The shared logic lives in charon_cmd.cpp; everything that
// needs a UEFI protocol, a firmware variable or a file on Charon's own EFI
// system partition (\EFI\HAK, which is NOT one of CharonDOS's emulated
// drives) goes through the small platform surface below. It has two
// implementations, the same split as dos_platform.h:
//   backend_uefi.cpp   the real one (UEFI protocols, runtime variables, SFS)
//   backend_host.cpp   a shim for the host tests (a directory stands in for
//                       \EFI\HAK; firmware actions are recorded, not done)
// No Charon EFI header is pulled into the DOS build; the CharSettings record
// format is re-derived (read-only) in charon_cmd.cpp.
#pragma once
#include <stddef.h>
#include <stdint.h>

// ---- the clock (r409) ----------------------------------------------------------
// A multi-system plan keeps the hardware clock in universal time and writes
// \EFI\HAK\clock.txt (src/efi/CharonClockZone.h). Both backends pass what the
// clock says through this function, so DATE, TIME, file times and programs
// see local time. Without the file (or with rtc=local) nothing changes. The
// file is read once per run of CharonDOS.
struct PlatTime;
void charon_clock_local(PlatTime* time);

// ---- shell entry -------------------------------------------------------------
// Runs "CHARON ..." (args is the text after the word CHARON). Returns an
// errorlevel (0 ok). polish selects the message language.
int charon_command(char* args, int polish);

// ---- platform surface for the CHARON command ---------------------------------
// What Charon exposes to CharonDOS, discovered live. Unknown fields are 0 / -1.
struct CharonInfo {
    uint32_t release;        // Charon release (ReaderControl), 0 = reader not found
    uint8_t have_voice;      // 1 = live speech (MenuVoice) reachable
    uint8_t have_reader;     // 1 = ReaderControl reachable
    int8_t secure_boot;      // -1 unknown, 0 off, 1 on (read-only status only)
};
void charon_plat_info(CharonInfo* out);

// Speak one phrase now, through Charon's live voice. lang is "pl" or "en".
// Returns 1 if the voice accepted it, 0 otherwise (the screen text is read by
// the resident reader regardless).
int charon_plat_speak(const char* lang, const char* utf8_text);

// A file under \EFI\HAK on Charon's own volume (name is relative to \EFI\HAK,
// e.g. "charon-settings-a.bin" or "synths\\x.manifest").
// read:  bytes read (>=0), -1 = not found, -2 = read error.
long charon_plat_hak_read(const char* name, void* buf, unsigned long cap);
// write: replaces the file (delete, create, write, flush, close). 1 = written,
// 0 = failed. The caller re-reads to verify, as Charon's own saver does.
int charon_plat_hak_write(const char* name, const void* buf, unsigned long len);

// ---- boot manager ------------------------------------------------------------
struct CharonBootEntry { uint16_t id; char desc[64]; };   // desc: UTF-8, trimmed
// Firmware boot entries in BootOrder order. Returns the count (>=0), -1 if
// BootOrder cannot be read. READ ONLY.
int charon_plat_boot_list(CharonBootEntry* out, int cap);

// Set Charon's Continue target to boot entry `id`, byte-for-byte as Charon's
// own SaveContinue does (validate id is in BootOrder, read its Boot#### option,
// write \EFI\HAK\continue-set then \EFI\HAK\continue.bin). Touches no platform
// key and reorders nothing. 0 ok, 1 id not bootable / unreadable, 2 write
// failed, 3 cannot be encoded from here.
int charon_plat_set_continue(uint16_t id);

// Point BootNext at the "Charon" boot-manager entry so its menu shows next
// restart. Reads existing Boot#### entries only; writes the standard BootNext
// variable (no Secure Boot key is touched). 0 ok, 1 no Charon entry found,
// 2 variable not writable from here.
int charon_plat_boot_next_charon(void);

// Ask the firmware to come up in its setup UI next restart, via the standard
// OsIndications variable (EFI_OS_INDICATIONS_BOOT_TO_FW_UI). 0 armed,
// 1 firmware does not offer it, 2 variable not writable from here.
int charon_plat_firmware_setup(void);

// Restart (mode 0, warm) or power off (mode 1). Does not return on success.
void charon_plat_reset(int mode);

// ---- synthesizer add-ons -----------------------------------------------------
// Manifest basenames found in \EFI\HAK\synths (ASCII, including ".manifest").
// Returns the count (>=0), -1 on error.
int charon_plat_synth_list(char out[][64], int cap);
