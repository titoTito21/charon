// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - shared internal declarations (machine, BIOS, DOS, shell).
#pragma once
#include "dos_platform.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// ---- machine --------------------------------------------------------------
struct Cpu { u16 r[8]; u16 s[4]; u16 ip; u16 fl; };
enum { rAX, rCX, rDX, rBX, rSP, rBP, rSI, rDI };
enum { sES, sCS, sSS, sDS };
enum { F_CF = 1, F_PF = 4, F_AF = 0x10, F_ZF = 0x40, F_SF = 0x80, F_TF = 0x100,
       F_IF = 0x200, F_DF = 0x400, F_OF = 0x800 };

extern Cpu g_cpu;
extern u8* g_mem;               // 1 MiB + overhead
extern bool g_stop;             // leave the innermost cpu_run()
extern bool g_quit;             // leave everything (EXIT, host input exhausted)
extern int g_poll_countdown;
extern u32 g_vid_dirty;         // one bit per row of the text screen

#define R_AX g_cpu.r[rAX]
#define R_CX g_cpu.r[rCX]
#define R_DX g_cpu.r[rDX]
#define R_BX g_cpu.r[rBX]
#define R_SP g_cpu.r[rSP]
#define R_BP g_cpu.r[rBP]
#define R_SI g_cpu.r[rSI]
#define R_DI g_cpu.r[rDI]
#define R_ES g_cpu.s[sES]
#define R_CS g_cpu.s[sCS]
#define R_SS g_cpu.s[sSS]
#define R_DS g_cpu.s[sDS]
#define R_FL g_cpu.fl

static inline u8 lo8(u16 v) { return (u8)v; }
static inline u8 hi8(u16 v) { return (u8)(v >> 8); }
static inline void setlo(u16& r, u8 v) { r = (u16)((r & 0xFF00) | v); }
static inline void sethi(u16& r, u8 v) { r = (u16)((r & 0x00FF) | (v << 8)); }
static inline void setflag(u16 mask, bool on) { if (on) R_FL |= mask; else R_FL &= (u16)~mask; }
static inline void set_cf(bool on) { setflag(F_CF, on); }

static inline u32 lin(u16 seg, u16 off) { return (((u32)seg << 4) + off) & 0xFFFFF; }
static inline u8 rb(u32 a) { return g_mem[a & 0xFFFFF]; }
static inline u16 rw(u32 a) { return (u16)(g_mem[a & 0xFFFFF] | (g_mem[(a + 1) & 0xFFFFF] << 8)); }
static inline u32 rd(u32 a) { return rw(a) | ((u32)rw(a + 2) << 16); }
static inline void wb(u32 a, u8 v)
{
    a &= 0xFFFFF;
    if (a >= 0xA0000) {
        if (a >= 0xC0000) return;                       // ROM
        if (a >= 0xB8000) g_vid_dirty |= 1u << (((a - 0xB8000) & 0xFFF) / 160);
    }
    g_mem[a] = v;
}
static inline void ww(u32 a, u16 v) { wb(a, (u8)v); wb(a + 1, (u8)(v >> 8)); }
static inline void wd(u32 a, u32 v) { ww(a, (u16)v); ww(a + 2, (u16)(v >> 16)); }

// ---- util.cpp ---------------------------------------------------------------
void d_memcpy(void* d, const void* s, size_t n);
void d_memmove(void* d, const void* s, size_t n);
void d_memset(void* d, int c, size_t n);
int d_memcmp(const void* a, const void* b, size_t n);
size_t d_strlen(const char* s);
int d_strcmp(const char* a, const char* b);
int d_stricmp(const char* a, const char* b);
int d_strnicmp(const char* a, const char* b, size_t n);
void d_strcpy(char* d, const char* s, size_t cap);     // always terminates
void d_strcat(char* d, const char* s, size_t cap);
char d_upper(char c);
void d_strupr(char* s);
size_t d_wlen(const dchar16* s);
void d_wcpy(dchar16* d, const dchar16* s, size_t cap);
void d_wcat(dchar16* d, const dchar16* s, size_t cap);
int d_wicmp(const dchar16* a, const dchar16* b);
int d_utoa(u32 value, char* out, int base);            // returns length
int d_u64toa(u64 value, char* out);
bool d_atou(const char* s, u32* out);
extern const u16 g_cp437[256];
dchar16 cp437_to_uni(u8 c);
u8 uni_to_cp437(dchar16 u, bool* exact);                // '?' never returned; 0 when unmappable
int utf8_next(const char** s);                          // decodes one code point, advances
int day_of_week(int y, int m, int d);
u32 tod_millis(const PlatTime* t);

// ---- cpu.cpp ----------------------------------------------------------------
void cpu_run(void);                                     // until g_stop / g_quit
void cpu_interrupt(u8 number);
void cpu_push(u16 v);
u16 cpu_pop(void);

// ---- video.cpp --------------------------------------------------------------
void vid_init(void);
void vid_set_mode(u8 mode, bool clear);
void vid_tty(u8 ch, dchar16 uni, int attr);             // teletype at the cursor of the active page
void vid_flush(void);                                   // pending teletype text -> console
void vid_sync(void);                                    // changed rows -> console
void vid_get_cursor(int* x, int* y);
void vid_set_cursor(int x, int y);
u32 vid_scroll_count(void);
void int10h(void);

// ---- bios.cpp ---------------------------------------------------------------
void bios_init(void);
void machine_poll(void);
void bios_trap(u8 number);                              // native part of an interrupt
u8 port_in(u16 port);
void port_out(u16 port, u8 value);
void kbd_pump(void);
bool kbd_peek(u16* key);
bool kbd_get(u16* key);
bool kbd_wait(u16* key);                                // false: quitting
void kbd_flush(void);
void illegal_instruction(u16 cs, u16 ip, u8 opcode);
void divide_error(void);
extern bool g_break;                                    // Ctrl+Break seen
extern bool g_trap_retry;                               // handler asks to be re-entered
extern bool g_no_iret;                                  // handler replaced the CPU state
extern u16 g_stub_seg;
u16 stub_off(u8 number);

// ---- dosfs.cpp --------------------------------------------------------------
struct DosPath {
    int drive;
    char dos[160];            // canonical "\DIR\FILE.EXT" ("" is the root)
    char last[13];            // last component (may hold wildcards)
    dchar16 lpath[400];       // real path of the object (or the name it would get)
    dchar16 lparent[400];     // real path of the containing directory
    bool exists, isdir, isdev;
    u8 dev;                   // device type when isdev
    u8 attr;
    u32 size;
    u16 date, time;
};
enum { DEV_NONE = 0, DEV_CON = 1, DEV_NUL = 2, DEV_AUX = 3 };
struct FindData { u8 attr; u16 time, date; u32 size; char name[13]; dchar16 lname[256]; };

void fs_init(void);
int fs_cur_drive(void);
bool fs_set_drive(int drive);
int fs_last_drive(void);
const char* fs_cwd(int drive);                          // "\DIR" or ""
int fs_resolve(const char* path, DosPath* out, bool allow_wild);
int fs_chdir(const char* path);
int fs_mkdir(const char* path);
int fs_rmdir(const char* path);
int fs_delete(const char* path);
int fs_rename(const char* from, const char* to);
int fs_getattr(const char* path, u8* attr);
int fs_setattr(const char* path, u8 attr);
int fs_open(const char* path, int mode, int* handle);
int fs_create(const char* path, u8 attr, bool must_be_new, int* handle);
int fs_close(int handle);
long fs_read(int handle, void* buffer, u32 count, int* err);
long fs_write(int handle, const void* buffer, u32 count, int* err);
int fs_seek(int handle, int whence, int32_t offset, u32* newpos);
int fs_dup(int handle, int* out);
int fs_dup2(int handle, int target);
int fs_devinfo(int handle, u16* info);
int fs_ftime(int handle, bool set, u16* date, u16* time);
bool fs_is_con(int handle);
bool fs_at_eof(int handle);
void fs_close_owned(u16 psp);
void fs_set_owner(u16 psp);
int fs_redirect(int handle, int to);                    // returns the saved slot for fs_restore
void fs_restore(int handle, int saved);
int fs_find_first(const char* spec, u8 attr, u8* dta);  // dta: 43 bytes of native memory
int fs_find_next(u8* dta);
bool fs_find_long(const u8* dta, dchar16* out, int cap);
void fs_full_name(const DosPath* p, char* out, size_t cap);   // "C:\DIR\FILE.EXT"
bool fs_match(const char* name83, const char* pattern);
void fs_invalidate(void);

// ---- dosint.cpp -------------------------------------------------------------
void dos_init(void);
void int21h(void);
void dos_terminate(u8 code, u8 type, bool keep);        // type: 0 normal, 1 ^C, 2 error, 3 TSR
int dos_run(const DosPath* program, const char* args, int* exit_code);   // from the shell
void con_putc(u8 ch);                                    // CP437 byte to the console (tabs expanded)
void con_put_uni(dchar16 u);
void dos_out(int handle, u8 ch);
int con_readline(char* buffer, int cap, bool history, bool crlf);        // -1: Ctrl+C
bool dos_check_break(void);                              // true: the program was terminated
u16 dos_cur_psp(void);
u16 dos_root_psp(void);
bool dos_in_program(void);
u32 dos_free_bytes(void);
u16 dos_last_exit(void);
u16 mcb_largest(void);
extern u8 g_break_flag;
extern char g_env[2048];                                 // master environment "A=B\0C=D\0\0"
const char* env_get(const char* name);
bool env_set(const char* name, const char* value);

// ---- shell.cpp --------------------------------------------------------------
void shell_init(bool polish);
void shell_interactive(void);
int shell_command(const char* line);                     // one command line, returns errorlevel
int shell_from_program(const char* tail);                // COMMAND.COM [/C command] run by a program
void shell_notice(int id, const char* text, u32 number);
enum { MSG_ILLEGAL = 0, MSG_DIVIDE, MSG_MCB, MSG_FIRST_SHELL };
