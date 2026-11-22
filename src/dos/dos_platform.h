// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - a small DOS environment for UEFI (optional Charon package).
//
// Platform interface. Everything the emulator needs from the outside world
// goes through these functions; there are two implementations:
//   backend_uefi.cpp  efi_main, UEFI console / file / time protocols
//   backend_host.cpp  Win32 + stdio, used only by the host tests
// The shared code never calls anything else.
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef uint16_t dchar16;                 // one UTF-16 code unit

// ---- keyboard -----------------------------------------------------------
enum {
    PK_NONE = 0, PK_UP, PK_DOWN, PK_LEFT, PK_RIGHT, PK_HOME, PK_END, PK_PGUP,
    PK_PGDN, PK_INS, PK_DEL, PK_ESC, PK_F1, PK_F2, PK_F3, PK_F4, PK_F5, PK_F6,
    PK_F7, PK_F8, PK_F9, PK_F10, PK_F11, PK_F12, PK_BREAK
};
enum { PM_SHIFT = 1, PM_CTRL = 2, PM_ALT = 4 };
struct PlatKey {
    uint16_t ch;       // Unicode character, 0 when `code` names a special key
    uint8_t code;      // PK_*
    uint8_t mods;      // PM_*
};

// ---- time ---------------------------------------------------------------
struct PlatTime { uint16_t year; uint8_t month, day, hour, minute, second, hsec; };

// ---- files --------------------------------------------------------------
// Paths are UTF-16, components separated by a backslash, relative to the
// root of the drive, with a leading backslash; "" is the root itself. The
// shared code has already resolved "." and ".." and the 8.3 aliases, so a
// backend only ever sees real names.
struct PlatDirEnt {
    dchar16 name[256];
    uint32_t size;
    uint8_t attr;          // DOS attribute bits (01 RO, 02 H, 04 S, 10 DIR, 20 A)
    uint16_t date, time;   // DOS packed date / time
};
enum { PO_READ = 0, PO_WRITE = 1, PO_RW = 2, PO_MODE = 3, PO_CREATE = 0x10, PO_TRUNC = 0x20 };

// Console. The emulated screen is 80x25; the backend console is at least
// that. `tty` output is text at the current cursor that may contain CR, LF
// and relies on the console scrolling by itself at the bottom line; cell
// output never contains control characters.
// Diagnostics (host tests only; the UEFI backend ignores it): one software
// interrupt a program made, with AX/BX/CX/DX before and AX and carry after.
void plat_trace_int(uint8_t number, const uint16_t* before, uint16_t ax_after, bool carry);
void plat_con_size(int* cols, int* rows);
void plat_con_clear(uint8_t attr);
void plat_con_goto(int col, int row);
void plat_con_out(const dchar16* text, int count, uint8_t attr, bool tty);
void plat_con_show_cursor(bool visible);

bool plat_key_poll(PlatKey* key);         // non-blocking
bool plat_quit(void);                     // host only: scripted input is exhausted
void plat_idle(unsigned ms);
uint32_t plat_millis(void);               // monotonic
void plat_time(PlatTime* out);
void* plat_alloc(size_t bytes);           // zero-filled, lives until exit

bool plat_drive_ok(int drive);            // 0 = A:, 2 = C:
// File functions return 0 or a DOS error code (2 file not found, 3 path not
// found, 5 access denied ...).
int plat_open(int drive, const dchar16* path, int flags, void** file);
long plat_read(void* file, void* buffer, uint32_t count);     // -1 on error
long plat_write(void* file, const void* buffer, uint32_t count);
bool plat_seek(void* file, uint32_t position);
uint32_t plat_size(void* file);
bool plat_truncate(void* file, uint32_t size);
bool plat_set_ftime(void* file, uint16_t date, uint16_t time);
void plat_close(void* file);
bool plat_stat(int drive, const dchar16* path, PlatDirEnt* out);
int plat_delete(int drive, const dchar16* path);
int plat_mkdir(int drive, const dchar16* path);
int plat_rmdir(int drive, const dchar16* path);
int plat_rename(int drive, const dchar16* from, const dchar16* to);
bool plat_set_attr(int drive, const dchar16* path, uint8_t attr);
void* plat_opendir(int drive, const dchar16* path);
bool plat_readdir(void* dir, PlatDirEnt* out);
void plat_closedir(void* dir);
bool plat_disk_space(int drive, uint64_t* total, uint64_t* free_bytes);

// ---- sound (r407) ---------------------------------------------------------
// Output of sound.cpp (PC speaker, Sound Blaster): 48000 Hz, two channels,
// signed 16 bit. A sound is opened with plat_pcm_begin (false: there is no
// output now; the next sound asks again), written in small pieces as time
// passes, and closed with plat_pcm_end after its silent tail. Writing never
// blocks; the return value is the number of frames taken.
bool plat_pcm_begin(void);
size_t plat_pcm_write(const int16_t* stereo, size_t frames);
size_t plat_pcm_queued(void);             // frames taken and not yet played
void plat_pcm_end(void);

// ---- leaving CharonDOS for another program -------------------------------
// WINNT starts the installed Windows (its own Windows Boot Manager,
// \EFI\Microsoft\Boot\bootmgfw.efi on any volume); CharonDOS ends and does
// not come back. WIN starts a Windows profile installed into CharonDOS
// (Windows 95 / 98 / ME / XP in the PC emulator CHARONPC.EFI next to
// CHARONDOS.EFI; profile = C:\PC\<name>\CHARONPC.CFG); CharonDOS comes back
// when the emulator ends. The call only checks that the target exists and
// records it; the shell then exits and the backend starts the target after
// the console is restored. `name` may be empty (WIN: the first profile).
// Returns 0, or 2 when the target does not exist (out names the profile
// that was chosen, when any).
enum { PL_WINNT = 1, PL_WIN = 2 };
int plat_request_launch(int kind, const char* name, char* chosen, size_t chosen_size);

// ---- entry point of the shared code --------------------------------------
struct DosOptions {
    bool polish;              // shell messages in Polish
    const char* command;      // run this one command line and exit (host tests); may be null
};
int dos_main(const DosOptions* options);
