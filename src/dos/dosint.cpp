// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - DOS kernel services: memory arena (MCB chain), PSP and
// environment, program loader (.COM and MZ .EXE), EXEC / terminate,
// console I/O, INT 21h.
#include "dos.h"

enum { DOS_DATA = 0x0060, FIRST_MCB = 0x0070, MEM_TOP = 0xA000, MAXPROC = 12 };
// Offsets of the national tables in the ROM segment (clear of the interrupt stubs).
enum { NLS_UPPER = 0x2000, NLS_COLLATE = 0x2100, NLS_FILECHARS = 0x2300, NLS_DBCS = 0x2340 };

struct Proc { Cpu saved; u16 psp, parent, dta_seg, dta_off; bool native; };

static Proc s_procs[MAXPROC];
static int s_nproc;
static u16 s_psp, s_root_psp, s_dta_seg, s_dta_off, s_exit, s_lasterr;
static int s_pending_scan = -1;
static int s_idle_polls;
static u32 s_break_ms;
u8 g_break_flag;
char g_env[2048];
static char s_hist[16][128];
static int s_hist_n;
static DosPath s_path;                       // scratch for INT 21h (large)
static char s_name[160], s_name2[160];
static u8 s_reloc[1024];

u16 dos_cur_psp(void) { return s_psp; }
u16 dos_root_psp(void) { return s_root_psp; }
bool dos_in_program(void) { return s_nproc > 0; }
u16 dos_last_exit(void) { return s_exit; }

// ---- environment --------------------------------------------------------------------
const char* env_get(const char* name)
{
    const size_t n = d_strlen(name);
    for (const char* p = g_env; *p; p += d_strlen(p) + 1)
        if (!d_strnicmp(p, name, n) && p[n] == '=') return p + n + 1;
    return nullptr;
}
bool env_set(const char* name, const char* value)
{
    const size_t n = d_strlen(name);
    char* p = g_env;
    while (*p) {
        const size_t len = d_strlen(p) + 1;
        if (!d_strnicmp(p, name, n) && p[n] == '=') {
            char* end = p + len;
            while (*end) end += d_strlen(end) + 1;
            d_memmove(p, p + len, (size_t)(end - (p + len)) + 1);
        } else p += len;
    }
    if (!value || !*value) return true;
    if ((size_t)(p - g_env) + n + d_strlen(value) + 3 > sizeof g_env) return false;
    for (size_t i = 0; i < n; i++) *p++ = d_upper(name[i]);
    *p++ = '=';
    for (; *value; value++) *p++ = *value;
    *p++ = 0; *p = 0;
    return true;
}

// ---- memory arena ---------------------------------------------------------------------
static inline u32 ma(u16 seg) { return (u32)seg << 4; }
static inline bool mcb_valid(u16 seg) { return g_mem[ma(seg)] == 'M' || g_mem[ma(seg)] == 'Z'; }
static inline u16 mcb_size(u16 seg) { return rw(ma(seg) + 3); }
static inline u16 mcb_owner(u16 seg) { return rw(ma(seg) + 1); }
static void mcb_set(u16 seg, u8 type, u16 owner, u16 size)
{
    const u32 a = ma(seg);
    g_mem[a] = type; g_mem[a + 1] = (u8)owner; g_mem[a + 2] = (u8)(owner >> 8);
    g_mem[a + 3] = (u8)size; g_mem[a + 4] = (u8)(size >> 8);
}
static bool mcb_merge(void)
{
    u16 seg = FIRST_MCB;
    for (int guard = 0; guard < 4096; guard++) {
        if (!mcb_valid(seg)) return false;
        if (g_mem[ma(seg)] == 'Z') return true;
        const u16 next = (u16)(seg + mcb_size(seg) + 1);
        if (next <= seg || next >= MEM_TOP || !mcb_valid(next)) return false;
        if (mcb_owner(seg) == 0 && mcb_owner(next) == 0) {
            mcb_set(seg, g_mem[ma(next)], 0, (u16)(mcb_size(seg) + mcb_size(next) + 1));
            continue;
        }
        seg = next;
    }
    return false;
}
u16 mcb_largest(void)
{
    u16 best = 0;
    if (!mcb_merge()) return 0;
    for (u16 seg = FIRST_MCB;; seg = (u16)(seg + mcb_size(seg) + 1)) {
        if (mcb_owner(seg) == 0 && mcb_size(seg) > best) best = mcb_size(seg);
        if (g_mem[ma(seg)] == 'Z') break;
    }
    return best;
}
u32 dos_free_bytes(void) { return (u32)mcb_largest() * 16; }
static void mcb_split(u16 seg, u16 paras)
{
    const u16 size = mcb_size(seg);
    if (size <= paras) return;
    const u8 type = g_mem[ma(seg)];
    mcb_set((u16)(seg + paras + 1), type, 0, (u16)(size - paras - 1));
    d_memset(g_mem + ma((u16)(seg + paras + 1)) + 5, 0, 11);
    mcb_set(seg, 'M', mcb_owner(seg), paras);
}
// Returns the segment of the block or 0 (7: chain destroyed, 8: no room).
static u16 mcb_alloc(u16 paras, u16 owner, int* err)
{
    *err = 8;
    if (!mcb_merge()) { *err = 7; return 0; }
    for (u16 seg = FIRST_MCB;; seg = (u16)(seg + mcb_size(seg) + 1)) {
        if (mcb_owner(seg) == 0 && mcb_size(seg) >= paras) {
            mcb_split(seg, paras);
            mcb_set(seg, g_mem[ma(seg)], owner == 0xFFFF ? (u16)(seg + 1) : owner, mcb_size(seg));
            d_memset(g_mem + ma(seg) + 5, 0, 11);
            *err = 0;
            return (u16)(seg + 1);
        }
        if (g_mem[ma(seg)] == 'Z') break;
    }
    return 0;
}
static bool mcb_is_block(u16 block)
{
    if (!mcb_merge()) return false;
    for (u16 seg = FIRST_MCB;; seg = (u16)(seg + mcb_size(seg) + 1)) {
        if ((u16)(seg + 1) == block) return true;
        if (g_mem[ma(seg)] == 'Z') break;
    }
    return false;
}
static int mcb_free(u16 block)
{
    if (!mcb_is_block(block)) return 9;
    mcb_set((u16)(block - 1), g_mem[ma((u16)(block - 1))], 0, mcb_size((u16)(block - 1)));
    return mcb_merge() ? 0 : 7;
}
static int mcb_resize(u16 block, u16 paras, u16* max)
{
    if (!mcb_is_block(block)) return 9;
    const u16 seg = (u16)(block - 1);
    u16 size = mcb_size(seg);
    if (paras > size && g_mem[ma(seg)] == 'M') {
        const u16 next = (u16)(seg + size + 1);
        if (mcb_owner(next) == 0) {
            mcb_set(seg, g_mem[ma(next)], mcb_owner(seg), (u16)(size + mcb_size(next) + 1));
            size = mcb_size(seg);
        }
    }
    *max = size;
    if (paras > size) return 8;
    mcb_split(seg, paras);
    return 0;
}
static void mcb_free_owner(u16 owner)
{
    if (!mcb_merge()) return;
    for (u16 seg = FIRST_MCB;; seg = (u16)(seg + mcb_size(seg) + 1)) {
        if (mcb_owner(seg) == owner) mcb_set(seg, g_mem[ma(seg)], 0, mcb_size(seg));
        if (g_mem[ma(seg)] == 'Z') break;
    }
    mcb_merge();
}

// ---- PSP ------------------------------------------------------------------------------
static bool fcb_sep(char c) { return c == ' ' || c == '\t' || c == ';' || c == ',' || c == '='; }
static bool fcb_end(char c) { return c <= ' ' || c == '/' || c == '"' || c == '<' || c == '>' || c == '|' || c == '+' || c == ';' || c == ',' || c == '=' || c == '[' || c == ']' || c == ':' || c == '.' || c == '\\'; }
// Parses one file name into an unopened FCB (11 name bytes after the drive).
static u8 parse_fcb(const char** text, u8* fcb)
{
    const char* s = *text;
    u8 result = 0;
    while (*s && fcb_sep(*s)) s++;
    fcb[0] = 0;
    d_memset(fcb + 1, ' ', 11);
    if (s[0] > ' ' && s[1] == ':') {
        const int d = d_upper(s[0]) - 'A';
        fcb[0] = (u8)(d + 1);
        if (!plat_drive_ok(d)) result = 0xFF;
        s += 2;
    }
    for (int part = 0; part < 2; part++) {
        const int max = part ? 3 : 8, base = part ? 9 : 1;
        int i = 0;
        for (; *s && !fcb_end(*s); s++) {
            if (*s == '*') { for (; i < max; i++) fcb[base + i] = '?'; if (result != 0xFF) result = 1; }
            else if (i < max) { fcb[base + i++] = (u8)d_upper(*s); if (*s == '?' && result != 0xFF) result = 1; }
        }
        if (part || *s != '.') break;
        s++;
    }
    *text = s;
    return result;
}

static void make_psp(u16 psp, u16 top, u16 env, const u8* tail, int tail_len, u16 parent)
{
    u8* p = g_mem + ma(psp);
    d_memset(p, 0, 256);
    p[0] = 0xCD; p[1] = 0x20;
    p[2] = (u8)top; p[3] = (u8)(top >> 8);
    p[5] = 0x9A; p[6] = 0xF0; p[7] = 0xFE; p[8] = 0x1D; p[9] = 0xF0;
    d_memcpy(p + 0x0A, g_mem + 0x22 * 4, 12);                 // INT 22h, 23h, 24h
    p[0x16] = (u8)parent; p[0x17] = (u8)(parent >> 8);
    d_memset(p + 0x18, 0xFF, 20);
    p[0x18] = 1; p[0x19] = 1; p[0x1A] = 1; p[0x1B] = 0; p[0x1C] = 2;
    p[0x2C] = (u8)env; p[0x2D] = (u8)(env >> 8);
    p[0x32] = 20; p[0x34] = 0x18; p[0x36] = (u8)psp; p[0x37] = (u8)(psp >> 8);
    p[0x50] = 0xCD; p[0x51] = 0x21; p[0x52] = 0xCB;
    if (tail_len > 126) tail_len = 126;
    p[0x80] = (u8)tail_len;
    d_memcpy(p + 0x81, tail, (size_t)tail_len);
    p[0x81 + tail_len] = 13;
    char text[130];
    d_memcpy(text, tail, (size_t)tail_len);
    text[tail_len] = 0;
    const char* s = text;
    parse_fcb(&s, p + 0x5C);
    parse_fcb(&s, p + 0x6C);
}

// Environment block for a new program: a copy of the caller's block (or of
// the master environment), then 0001h and the full program name.
static u16 make_env(u16 from, const char* program, int* err)
{
    u32 len = 0;
    const u8* src;
    if (from) {
        src = g_mem + ma(from);
        while (len < 0x7F00 && (src[len] || src[len + 1])) len++;
        if (len) len++;
    } else {
        src = (const u8*)g_env;
        while (src[len]) len += (u32)d_strlen((const char*)src + len) + 1;
    }
    const u32 total = len + 1 + 2 + (u32)d_strlen(program) + 1 + (len ? 0 : 1);
    const u16 seg = mcb_alloc((u16)((total + 15) / 16 + 1), 8, err);
    if (!seg) return 0;
    u8* d = g_mem + ma(seg);
    d_memcpy(d, src, len);
    u32 n = len;
    if (!len) d[n++] = 0;
    d[n++] = 0; d[n++] = 1; d[n++] = 0;
    d_strcpy((char*)d + n, program, 128);
    return seg;
}

static u16 hw(const u8* h, int off) { return (u16)(h[off] | (h[off + 1] << 8)); }

static int load_image(const DosPath* prog, u16 env_from, const u8* tail, int tail_len, Cpu* regs, u16* psp_out)
{
    void* pf = nullptr;
    int err = plat_open(prog->drive, prog->lpath, PO_READ, &pf);
    if (err) return err;
    const u32 fsize = plat_size(pf);
    u8 h[0x1C];
    d_memset(h, 0, sizeof h);
    const long got = plat_read(pf, h, sizeof h);
    const bool mz = got >= 0x1C && ((h[0] == 'M' && h[1] == 'Z') || (h[0] == 'Z' && h[1] == 'M'));
    u32 image = fsize, header = 0;
    if (mz) {
        const u32 pages = hw(h, 4), last = hw(h, 2);
        if (!pages) { plat_close(pf); return 11; }
        u32 total = pages * 512;
        if (last) total -= 512 - last;
        if (total > fsize) total = fsize;
        header = (u32)hw(h, 8) * 16;
        if (total < header) { plat_close(pf); return 11; }
        image = total - header;
    } else if (fsize > 0xFE00 || fsize == 0) { plat_close(pf); return fsize ? 8 : 11; }

    fs_full_name(prog, s_name2, sizeof s_name2);
    const u16 env = make_env(env_from, s_name2, &err);
    if (!env) { plat_close(pf); return err; }
    const u16 largest = mcb_largest();
    u32 need, want;
    if (mz) {
        const u32 paras = (image + 15) / 16;
        need = paras + hw(h, 0x0A) + 0x10;
        want = paras + hw(h, 0x0C) + 0x10;
        if (want < need) want = need;
    } else {
        need = (fsize + 0x100 + 0x100 + 15) / 16;
        want = 0xFFFF;
    }
    if (need > largest) { mcb_free(env); plat_close(pf); return 8; }
    const u16 paras = (u16)(want > largest ? largest : want);
    const u16 psp = mcb_alloc(paras, 0xFFFF, &err);
    if (!psp) { mcb_free(env); plat_close(pf); return err; }
    mcb_set((u16)(env - 1), g_mem[ma((u16)(env - 1))], psp, mcb_size((u16)(env - 1)));
    // MCB name: the base name of the program.
    for (int i = 0; i < 8 && prog->last[i] && prog->last[i] != '.'; i++) g_mem[ma((u16)(psp - 1)) + 8 + i] = (u8)prog->last[i];
    make_psp(psp, (u16)(psp + paras), env, tail, tail_len, s_psp);

    d_memset(regs, 0, sizeof *regs);
    bool ok = true;
    if (mz) {
        const u16 start = (u16)(psp + 0x10);
        d_memset(g_mem + ma(start), 0, (size_t)(paras - 0x10) * 16);
        ok = plat_seek(pf, header) && plat_read(pf, g_mem + ma(start), image) == (long)image;
        u32 count = hw(h, 6);
        if (ok && count) ok = plat_seek(pf, hw(h, 0x18));
        while (ok && count) {
            const u32 chunk = count > sizeof s_reloc / 4 ? sizeof s_reloc / 4 : count;
            if (plat_read(pf, s_reloc, chunk * 4) != (long)(chunk * 4)) { ok = false; break; }
            for (u32 i = 0; i < chunk; i++) {
                const u32 a = (((u32)(u16)(start + hw(s_reloc, (int)i * 4 + 2)) << 4) + hw(s_reloc, (int)i * 4)) & 0xFFFFF;
                const u16 v = (u16)(rw(a) + start);
                g_mem[a] = (u8)v; g_mem[(a + 1) & 0xFFFFF] = (u8)(v >> 8);
            }
            count -= chunk;
        }
        regs->s[sCS] = (u16)(start + hw(h, 0x16)); regs->ip = hw(h, 0x14);
        regs->s[sSS] = (u16)(start + hw(h, 0x0E)); regs->r[rSP] = hw(h, 0x10);
        regs->s[sDS] = regs->s[sES] = psp;
    } else {
        d_memset(g_mem + ma(psp) + 0x100, 0, paras > 0x1000 ? 0xFF00 : (size_t)paras * 16 - 0x100);
        ok = plat_seek(pf, 0) && plat_read(pf, g_mem + ma(psp) + 0x100, fsize) == (long)fsize;
        regs->s[sCS] = regs->s[sDS] = regs->s[sES] = regs->s[sSS] = psp;
        regs->ip = 0x100;
        regs->r[rSP] = paras >= 0x1000 ? 0xFFFE : (u16)(paras * 16 - 2);
        g_mem[ma(psp) + regs->r[rSP]] = 0; g_mem[ma(psp) + regs->r[rSP] + 1] = 0;
    }
    plat_close(pf);
    if (!ok) { mcb_free_owner(psp); return 11; }
    regs->r[rCX] = 0x00FF; regs->r[rDX] = psp; regs->r[rSI] = regs->ip; regs->r[rDI] = regs->r[rSP]; regs->r[rBP] = 0x091C;
    regs->fl = 0xF202;
    *psp_out = psp;
    return 0;
}

static int load_overlay(const DosPath* prog, u16 seg, u16 reloc)
{
    void* pf = nullptr;
    const int err = plat_open(prog->drive, prog->lpath, PO_READ, &pf);
    if (err) return err;
    const u32 fsize = plat_size(pf);
    u8 h[0x1C];
    d_memset(h, 0, sizeof h);
    const long got = plat_read(pf, h, sizeof h);
    const bool mz = got >= 0x1C && ((h[0] == 'M' && h[1] == 'Z') || (h[0] == 'Z' && h[1] == 'M'));
    u32 image = fsize, header = 0;
    if (mz) {
        u32 total = (u32)hw(h, 4) * 512;
        if (hw(h, 2)) total -= 512 - hw(h, 2);
        if (total > fsize) total = fsize;
        header = (u32)hw(h, 8) * 16;
        if (total < header) { plat_close(pf); return 11; }
        image = total - header;
    }
    if (ma(seg) + image > 0xA0000) { plat_close(pf); return 8; }
    bool ok = plat_seek(pf, header) && plat_read(pf, g_mem + ma(seg), image) == (long)image;
    u32 count = mz ? hw(h, 6) : 0;
    if (ok && count) ok = plat_seek(pf, hw(h, 0x18));
    while (ok && count) {
        const u32 chunk = count > sizeof s_reloc / 4 ? sizeof s_reloc / 4 : count;
        if (plat_read(pf, s_reloc, chunk * 4) != (long)(chunk * 4)) { ok = false; break; }
        for (u32 i = 0; i < chunk; i++) {
            const u32 a = (((u32)(u16)(seg + hw(s_reloc, (int)i * 4 + 2)) << 4) + hw(s_reloc, (int)i * 4)) & 0xFFFFF;
            const u16 v = (u16)(rw(a) + reloc);
            g_mem[a] = (u8)v; g_mem[(a + 1) & 0xFFFFF] = (u8)(v >> 8);
        }
        count -= chunk;
    }
    plat_close(pf);
    return ok ? 0 : 11;
}

// ---- processes ---------------------------------------------------------------------------
static void enter(const Cpu* regs, u16 psp, bool native)
{
    Proc* p = &s_procs[s_nproc++];
    p->saved = g_cpu; p->psp = psp; p->parent = s_psp; p->dta_seg = s_dta_seg; p->dta_off = s_dta_off; p->native = native;
    s_psp = psp; s_dta_seg = psp; s_dta_off = 0x80;
    fs_set_owner(psp);
    g_cpu = *regs;
}

void dos_terminate(u8 code, u8 type, bool keep)
{
    if (!s_nproc) return;
    Proc* p = &s_procs[s_nproc - 1];
    const u16 psp = p->psp;
    d_memcpy(g_mem + 0x22 * 4, g_mem + ma(psp) + 0x0A, 12);
    if (keep) {
        u16 max;
        mcb_resize(psp, R_DX < 0x11 ? 0x11 : R_DX, &max);
    } else {
        fs_close_owned(psp);
        mcb_free_owner(psp);
    }
    s_exit = (u16)(code | (type << 8));
    s_psp = p->parent; s_dta_seg = p->dta_seg; s_dta_off = p->dta_off;
    fs_set_owner(s_psp);
    g_cpu = p->saved;
    s_nproc--;
    s_pending_scan = -1;
    if (p->native) g_stop = true;
    else {
        // The parent resumes at the IRET of its INT 21h/4Bh; report success.
        const u32 a = lin(R_SS, (u16)(R_SP + 4));
        g_mem[a] &= 0xFE;
    }
    g_no_iret = true;
}

static void memory_reset(void);

int dos_run(const DosPath* program, const char* args, int* exit_code)
{
    u8 tail[130];
    int n = 0;
    if (args && *args) {
        tail[n++] = ' ';
        for (; *args && n < 126; args++) tail[n++] = (u8)*args;
    }
    Cpu regs;
    u16 psp;
    if (s_nproc >= MAXPROC) return 8;
    const int err = load_image(program, 0, tail, n, &regs, &psp);
    if (err) return err;
    const int depth = s_nproc;
    enter(&regs, psp, true);
    g_stop = false;
    g_poll_countdown = 0;
    cpu_run();
    while (s_nproc > depth) dos_terminate(0, 1, false);       // only when quitting
    g_stop = false; g_no_iret = false; g_trap_retry = false;
    vid_sync();
    *exit_code = s_exit & 0xFF;
    if (!s_nproc && !mcb_merge()) {
        shell_notice(MSG_MCB, "", 0);
        memory_reset();
    }
    return 0;
}

// ---- console -----------------------------------------------------------------------------
void con_putc(u8 ch)
{
    if (ch == 9) {
        int x, y;
        do { vid_tty(' ', 0, -1); vid_get_cursor(&x, &y); } while (x & 7);
    } else vid_tty(ch, 0, -1);
}
void con_put_uni(dchar16 u)
{
    if (u == 9 || u == 10 || u == 13 || u == 8 || u == 7) { con_putc((u8)u); return; }
    bool exact;
    u8 c = uni_to_cp437(u, &exact);
    if (!c) c = '?';
    vid_tty(c, exact ? 0 : u, -1);
}
void dos_out(int handle, u8 ch)
{
    if (fs_is_con(handle)) con_putc(ch);
    else { int err; fs_write(handle, &ch, 1, &err); }
}

// Ctrl+C at a DOS console call. Returns true when the program was ended.
static bool ctrl_c(void)
{
    con_putc('^'); con_putc('C'); con_putc(13); con_putc(10);
    if (!dos_in_program()) return false;
    if (rw(0x23 * 4) != stub_off(0x23) || rw(0x23 * 4 + 2) != g_stub_seg) return false;   // the program handles it
    dos_terminate(0, 1, false);
    return true;
}
bool dos_check_break(void)
{
    const u32 now = plat_millis();
    if (now == s_break_ms && !g_break) return false;
    s_break_ms = now;
    u16 key;
    if (g_break) { g_break = false; return ctrl_c(); }
    if (kbd_peek(&key) && lo8(key) == 3) { kbd_get(&key); return ctrl_c(); }
    return false;
}

struct LineEdit { char* buf; int len, cur, sx, sy; u32 scrolls; };
static void le_place(const LineEdit* e, int index)
{
    const int cell = e->sx + index;
    int y = e->sy - (int)(vid_scroll_count() - e->scrolls) + cell / 80;
    if (y < 0) y = 0;
    vid_set_cursor(cell % 80, y);
}
static void le_redraw(const LineEdit* e, int from, int blanks)
{
    le_place(e, from);
    for (int i = from; i < e->len; i++) vid_tty((u8)e->buf[i], 0, -1);
    for (int i = 0; i < blanks; i++) vid_tty(' ', 0, -1);
    le_place(e, e->cur);
}
static void le_set(LineEdit* e, const char* text, int cap)
{
    const int old = e->len;
    d_strcpy(e->buf, text, (size_t)cap);
    e->len = e->cur = (int)d_strlen(e->buf);
    le_redraw(e, 0, old > e->len ? old - e->len : 0);
}

int con_readline(char* buffer, int cap, bool history, bool crlf)
{
    LineEdit e;
    e.buf = buffer; e.len = e.cur = 0;
    vid_get_cursor(&e.sx, &e.sy);
    e.scrolls = vid_scroll_count();
    int browse = s_hist_n;
    if (cap < 1) return 0;
    if (cap > 256) cap = 256;
    buffer[0] = 0;
    for (;;) {
        u16 key;
        if (!kbd_wait(&key)) {
            if (g_break) { g_break = false; key = 0x2E03; }
            else return -1;
        }
        const u8 c = lo8(key), scan = hi8(key);
        if (c == 13) {
            le_place(&e, e.len);
            con_putc(13);
            if (crlf) con_putc(10);
            buffer[e.len] = 0;
            if (history && e.len && e.len < 127) {
                if (s_hist_n == 16) { d_memmove(s_hist[0], s_hist[1], sizeof s_hist[0] * 15); s_hist_n--; }
                d_strcpy(s_hist[s_hist_n++], buffer, sizeof s_hist[0]);
            }
            return e.len;
        }
        if (c == 3) { le_place(&e, e.len); ctrl_c(); buffer[0] = 0; return -1; }
        if (c == 8) {
            if (e.cur > 0) {
                d_memmove(buffer + e.cur - 1, buffer + e.cur, (size_t)(e.len - e.cur));
                e.cur--; e.len--;
                le_redraw(&e, e.cur, 1);
            }
            continue;
        }
        if (c == 27) { le_set(&e, "", cap); continue; }
        if (c == 0) {
            switch (scan) {
            case 0x4B: if (e.cur > 0) e.cur--; le_place(&e, e.cur); break;
            case 0x4D: if (e.cur < e.len) e.cur++; le_place(&e, e.cur); break;
            case 0x47: e.cur = 0; le_place(&e, e.cur); break;
            case 0x4F: e.cur = e.len; le_place(&e, e.cur); break;
            case 0x53:
                if (e.cur < e.len) {
                    d_memmove(buffer + e.cur, buffer + e.cur + 1, (size_t)(e.len - e.cur - 1));
                    e.len--;
                    le_redraw(&e, e.cur, 1);
                }
                break;
            case 0x48:
                if (history && browse > 0) { browse--; le_set(&e, s_hist[browse], cap); }
                break;
            case 0x50:
                if (history && browse < s_hist_n) { browse++; le_set(&e, browse < s_hist_n ? s_hist[browse] : "", cap); }
                break;
            default: break;
            }
            continue;
        }
        if ((c >= 0x20 || c == 0x1A) && e.len < cap - 1) {
            d_memmove(buffer + e.cur + 1, buffer + e.cur, (size_t)(e.len - e.cur));
            buffer[e.cur] = (char)c;
            e.len++; e.cur++;
            if (e.cur == e.len) vid_tty(c, 0, -1);
            else le_redraw(&e, e.cur - 1, 0);
        }
    }
}

// One character from standard input; -1 when the program ended meanwhile.
static int stdin_char(bool check_break)
{
    if (!fs_is_con(0)) {
        u8 c; int err;
        return fs_read(0, &c, 1, &err) == 1 ? c : 0x1A;
    }
    if (s_pending_scan >= 0) { const int c = s_pending_scan; s_pending_scan = -1; return c; }
    for (;;) {
        u16 key;
        if (!kbd_wait(&key)) {
            if (!g_break) return -1;
            g_break = false;
            if (ctrl_c()) return -1;
            continue;
        }
        const u8 c = lo8(key);
        if (c == 0) { s_pending_scan = hi8(key); return 0; }
        if (check_break && c == 3) { if (ctrl_c()) return -1; }
        return c;
    }
}
static bool stdin_ready(void)
{
    if (!fs_is_con(0)) return !fs_at_eof(0);
    u16 key;
    if (s_pending_scan >= 0 || kbd_peek(&key)) { s_idle_polls = 0; return true; }
    if (++s_idle_polls > 64) { s_idle_polls = 0; vid_sync(); plat_idle(1); if (plat_quit()) g_quit = true; }
    return false;
}

// ---- INT 21h --------------------------------------------------------------------------------
static void fail(u16 code) { R_AX = code; s_lasterr = code; set_cf(true); }
static void zstr(u16 seg, u16 off, char* out, int cap)
{
    int n = 0;
    for (; n < cap - 1; n++) { const u8 c = rb(lin(seg, (u16)(off + n))); if (!c) break; out[n] = (char)c; }
    out[n] = 0;
}
static void put_zstr(u16 seg, u16 off, const char* s)
{
    for (;; s++, off++) { wb(lin(seg, off), (u8)*s); if (!*s) break; }
}
static const char* base_name(const char* path)
{
    const char* b = path;
    for (const char* p = path; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') b = p + 1;
    return b;
}

static void do_exec(void)
{
    const u8 mode = lo8(R_AX);
    const u32 pb = lin(R_ES, R_BX);
    zstr(R_DS, R_DX, s_name, sizeof s_name);
    int err = fs_resolve(s_name, &s_path, false);
    const bool comspec = mode == 0 && !d_stricmp(base_name(s_name), "COMMAND.COM") && (err || !s_path.exists || s_path.isdir);
    if (err && !comspec) { fail((u16)err); return; }
    if (mode == 3) {
        if (!s_path.exists || s_path.isdir || s_path.isdev) { fail(2); return; }
        err = load_overlay(&s_path, rw(pb), rw(pb + 2));
        if (err) fail((u16)err); else set_cf(false);
        return;
    }
    if (mode != 0) { fail(1); return; }
    u8 tail[128];
    const u32 tp = lin(rw(pb + 4), rw(pb + 2));
    int n = rb(tp);
    if (n > 126) n = 126;
    for (int i = 0; i < n; i++) tail[i] = rb(tp + 1 + (u32)i);
    if (comspec) {
        // The command interpreter is native.
        tail[n] = 0;
        s_exit = (u16)(shell_from_program((const char*)tail) & 0xFF);
        set_cf(false);
        return;
    }
    if (!s_path.exists || s_path.isdir || s_path.isdev) { fail(2); return; }
    if (s_nproc >= MAXPROC) { fail(8); return; }
    Cpu regs;
    u16 psp;
    u16 env = rw(pb);
    if (!env) env = rw(ma(s_psp) + 0x2C);
    err = load_image(&s_path, env, tail, n, &regs, &psp);
    if (err) { fail((u16)err); return; }
    enter(&regs, psp, false);
    g_no_iret = true;
}

static void country_info(u32 a)
{
    static const u8 info[34] = {0, 0, '$', 0, 0, 0, 0, ',', 0, '.', 0, '-', 0, ':', 0, 0, 2, 0,
                                0xF0, 0x00, 0x00, 0xF0, ',', 0};
    for (int i = 0; i < 34; i++) wb(a + (u32)i, info[i]);
}

void int21h(void)
{
    const u8 ah = hi8(R_AX), al = lo8(R_AX);
    int err = 0, handle = 0, c;
    u32 pos;
    u16 w1, w2;
    PlatTime t;
    switch (ah) {
    case 0x00: dos_terminate(0, 0, false); break;
    case 0x01:
        c = stdin_char(true);
        if (c < 0) break;
        dos_out(1, (u8)c); setlo(R_AX, (u8)c);
        break;
    case 0x02:
        if (dos_check_break()) break;
        dos_out(1, lo8(R_DX)); setlo(R_AX, lo8(R_DX));
        break;
    case 0x03: setlo(R_AX, 0x1A); break;
    case 0x04: case 0x05: break;
    case 0x06:
        if (lo8(R_DX) == 0xFF) {
            if (stdin_ready()) { c = stdin_char(false); if (c < 0) break; setlo(R_AX, (u8)c); setflag(F_ZF, false); }
            else { setlo(R_AX, 0); setflag(F_ZF, true); }
        } else { dos_out(1, lo8(R_DX)); setlo(R_AX, lo8(R_DX)); }
        break;
    case 0x07: case 0x08:
        c = stdin_char(ah == 0x08);
        if (c >= 0) setlo(R_AX, (u8)c);
        break;
    case 0x09: {
        if (dos_check_break()) break;
        u16 off = R_DX;
        for (int guard = 0; guard < 0x10000; guard++, off++) {
            const u8 ch = rb(lin(R_DS, off));
            if (ch == '$') break;
            dos_out(1, ch);
        }
        setlo(R_AX, '$');
        break; }
    case 0x0A: {
        const u32 a = lin(R_DS, R_DX);
        const int max = rb(a);
        if (!max) break;
        static char line[258];
        int n;
        if (fs_is_con(0)) {
            n = con_readline(line, max, true, false);
            if (n < 0) { if (g_no_iret || g_quit) break; n = 0; }
        } else {
            n = 0;
            for (;;) {
                c = stdin_char(false);
                if (c == 13 || c == 0x1A) break;
                if (c != 10 && n < max - 1) line[n++] = (char)c;
            }
        }
        wb(a + 1, (u8)n);
        for (int i = 0; i < n; i++) wb(a + 2 + (u32)i, (u8)line[i]);
        wb(a + 2 + (u32)n, 13);
        break; }
    case 0x0B:
        if (dos_check_break()) break;
        setlo(R_AX, stdin_ready() ? 0xFF : 0);
        break;
    case 0x0C:
        if (fs_is_con(0)) { kbd_flush(); s_pending_scan = -1; }
        if (al == 1 || al == 6 || al == 7 || al == 8 || al == 0x0A) { sethi(R_AX, al); int21h(); }
        else setlo(R_AX, 0);
        break;
    case 0x0D: break;
    case 0x0E:
        fs_set_drive(lo8(R_DX));
        setlo(R_AX, (u8)(fs_last_drive() + 1 < 5 ? 5 : fs_last_drive() + 1));
        break;
    case 0x19: setlo(R_AX, (u8)fs_cur_drive()); break;
    case 0x1A: s_dta_seg = R_DS; s_dta_off = R_DX; break;
    case 0x25:
        g_mem[al * 4] = lo8(R_DX); g_mem[al * 4 + 1] = hi8(R_DX);
        g_mem[al * 4 + 2] = lo8(R_DS); g_mem[al * 4 + 3] = hi8(R_DS);
        break;
    case 0x29: {
        u8 fcb[12];
        char text[80];
        zstr(R_DS, R_SI, text, sizeof text);
        for (char* p = text; *p; p++) if (*p == 13) { *p = 0; break; }
        const char* s = text;
        const u8 r = parse_fcb(&s, fcb);
        const u32 d = lin(R_ES, R_DI);
        if (fcb[0] || !(al & 2)) wb(d, fcb[0]);
        for (int i = 1; i < 12; i++) wb(d + (u32)i, fcb[i]);
        R_SI = (u16)(R_SI + (s - text));
        setlo(R_AX, r);
        break; }
    case 0x2A:
        plat_time(&t);
        R_CX = t.year; R_DX = (u16)((t.month << 8) | t.day);
        setlo(R_AX, (u8)day_of_week(t.year, t.month, t.day));
        break;
    case 0x2B: case 0x2D: setlo(R_AX, 0xFF); break;         // the firmware clock is not written
    case 0x2C:
        plat_time(&t);
        R_CX = (u16)((t.hour << 8) | t.minute); R_DX = (u16)((t.second << 8) | t.hsec);
        break;
    case 0x2E: break;
    case 0x2F: R_ES = s_dta_seg; R_BX = s_dta_off; break;
    case 0x30: R_AX = 0x0005; R_BX = 0xFF00; R_CX = 0; break;
    case 0x31: dos_terminate(al, 3, true); break;
    case 0x33:
        if (al == 0) setlo(R_DX, g_break_flag);
        else if (al == 1) g_break_flag = lo8(R_DX) & 1;
        else if (al == 5) setlo(R_DX, 3);
        else if (al == 6) { R_BX = 0x0005; R_DX = 0; }
        break;
    case 0x34: R_ES = DOS_DATA; R_BX = 0; break;
    case 0x35: R_BX = rw((u32)al * 4); R_ES = rw((u32)al * 4 + 2); break;
    case 0x36: {
        const int drive = lo8(R_DX) ? lo8(R_DX) - 1 : fs_cur_drive();
        u64 total = 0, free_bytes = 0;
        if (!plat_drive_ok(drive) || !plat_disk_space(drive, &total, &free_bytes)) { R_AX = 0xFFFF; break; }
        u64 tc = total / 32768, fc = free_bytes / 32768;
        if (tc > 0xFFFF) tc = 0xFFFF;
        if (fc > 0xFFFF) fc = 0xFFFF;
        R_AX = 64; R_BX = (u16)fc; R_CX = 512; R_DX = (u16)tc;
        break; }
    case 0x37:
        if (al == 0) { setlo(R_DX, '/'); setlo(R_AX, 0); }
        else setlo(R_AX, 0xFF);
        break;
    case 0x38:
        if (R_DX == 0xFFFF) { fail(1); break; }
        country_info(lin(R_DS, R_DX));
        R_BX = 1; set_cf(false);
        break;
    case 0x39: case 0x3A: case 0x3B: case 0x41:
        zstr(R_DS, R_DX, s_name, sizeof s_name);
        err = ah == 0x39 ? fs_mkdir(s_name) : ah == 0x3A ? fs_rmdir(s_name) : ah == 0x3B ? fs_chdir(s_name) : fs_delete(s_name);
        if (err) fail((u16)err); else set_cf(false);
        break;
    case 0x3C: case 0x5B:
        zstr(R_DS, R_DX, s_name, sizeof s_name);
        err = fs_create(s_name, lo8(R_CX), ah == 0x5B, &handle);
        if (err) fail((u16)err); else { R_AX = (u16)handle; set_cf(false); }
        break;
    case 0x3D:
        zstr(R_DS, R_DX, s_name, sizeof s_name);
        err = fs_open(s_name, al & 3, &handle);
        if (err) fail((u16)err); else { R_AX = (u16)handle; set_cf(false); }
        break;
    case 0x3E:
        // r402: the standard handles 0-4 are shared by every process here (one handle
        // table). Programs that close them on exit (FreeDOS EDIT, MORE, LESS, FC...)
        // silenced the shell and everything run after them. They stay open.
        if (R_BX <= 4) { set_cf(false); break; }
        err = fs_close(R_BX);
        if (err) fail((u16)err); else set_cf(false);
        break;
    case 0x3F: {
        const u32 a = ((u32)R_DS << 4) + R_DX;
        const long n = fs_read(R_BX, g_mem + a, R_CX, &err);
        if (g_no_iret) break;
        if (n < 0) { fail((u16)err); break; }
        if (a + (u32)n > 0xB8000 && a < 0xC0000) g_vid_dirty = 0x1FFFFFF;
        R_AX = (u16)n; set_cf(false);
        break; }
    case 0x40: {
        if (fs_is_con(R_BX) && dos_check_break()) break;
        const u32 a = ((u32)R_DS << 4) + R_DX;
        const long n = fs_write(R_BX, g_mem + a, R_CX, &err);
        if (n < 0) { fail((u16)err); break; }
        R_AX = (u16)n; set_cf(false);
        break; }
    case 0x42:
        err = fs_seek(R_BX, al, (int32_t)(((u32)R_CX << 16) | R_DX), &pos);
        if (err) fail((u16)err); else { R_AX = (u16)pos; R_DX = (u16)(pos >> 16); set_cf(false); }
        break;
    case 0x43: {
        zstr(R_DS, R_DX, s_name, sizeof s_name);
        u8 attr = 0;
        if (al == 0) { err = fs_getattr(s_name, &attr); if (!err) R_CX = attr; }
        else if (al == 1) err = fs_setattr(s_name, lo8(R_CX));
        else err = 1;
        if (err) fail((u16)err); else set_cf(false);
        break; }
    case 0x44:
        switch (al) {
        case 0x00:
            err = fs_devinfo(R_BX, &w1);
            if (err) fail((u16)err); else { R_DX = w1; R_AX = w1; set_cf(false); }
            break;
        case 0x01: set_cf(false); break;
        case 0x06:
            err = fs_devinfo(R_BX, &w1);
            if (err) { fail((u16)err); break; }
            if (fs_is_con(R_BX)) setlo(R_AX, stdin_ready() ? 0xFF : 0);
            else setlo(R_AX, fs_at_eof(R_BX) ? 0 : 0xFF);
            set_cf(false);
            break;
        case 0x07: setlo(R_AX, 0xFF); set_cf(false); break;
        case 0x08: R_AX = 1; set_cf(false); break;
        case 0x09: case 0x0A: R_DX = 0; set_cf(false); break;
        case 0x0E: setlo(R_AX, 0); set_cf(false); break;
        default: fail(1); break;
        }
        break;
    case 0x45:
        err = fs_dup(R_BX, &handle);
        if (err) fail((u16)err); else { R_AX = (u16)handle; set_cf(false); }
        break;
    case 0x46:
        err = fs_dup2(R_BX, R_CX);
        if (err) fail((u16)err); else set_cf(false);
        break;
    case 0x47: {
        const int drive = lo8(R_DX) ? lo8(R_DX) - 1 : fs_cur_drive();
        if (!plat_drive_ok(drive)) { fail(15); break; }
        const char* cwd = fs_cwd(drive);
        put_zstr(R_DS, R_SI, cwd[0] == '\\' ? cwd + 1 : cwd);
        R_AX = 0x0100; set_cf(false);
        break; }
    case 0x48: {
        const u16 seg = mcb_alloc(R_BX, s_psp, &err);
        if (seg) { R_AX = seg; set_cf(false); }
        else { fail((u16)err); R_BX = mcb_largest(); }
        break; }
    case 0x49:
        err = mcb_free(R_ES);
        if (err) fail((u16)err); else set_cf(false);
        break;
    case 0x4A:
        err = mcb_resize(R_ES, R_BX, &w1);
        if (err) { fail((u16)err); if (err == 8) R_BX = w1; } else set_cf(false);
        break;
    case 0x4B: do_exec(); break;
    case 0x4C: dos_terminate(al, 0, false); break;
    case 0x4D: R_AX = s_exit; s_exit = 0; set_cf(false); break;
    case 0x4E:
        zstr(R_DS, R_DX, s_name, sizeof s_name);
        err = fs_find_first(s_name, lo8(R_CX), g_mem + lin(s_dta_seg, s_dta_off));
        if (err) fail((u16)err); else { R_AX = 0; set_cf(false); }
        break;
    case 0x4F:
        err = fs_find_next(g_mem + lin(s_dta_seg, s_dta_off));
        if (err) fail((u16)err); else { R_AX = 0; set_cf(false); }
        break;
    case 0x50: s_psp = R_BX; break;
    case 0x51: case 0x62: R_BX = s_psp; break;
    case 0x52: R_ES = DOS_DATA; R_BX = 0x10; break;
    case 0x54: setlo(R_AX, 0); break;
    case 0x56:
        zstr(R_DS, R_DX, s_name, sizeof s_name);
        zstr(R_ES, R_DI, s_name2, sizeof s_name2);
        err = fs_rename(s_name, s_name2);
        if (err) fail((u16)err); else set_cf(false);
        break;
    case 0x57:
        w1 = R_DX; w2 = R_CX;
        err = fs_ftime(R_BX, al == 1, &w1, &w2);
        if (al > 1) err = 1;
        if (err) fail((u16)err); else { if (al == 0) { R_DX = w1; R_CX = w2; } set_cf(false); }
        break;
    case 0x58:
        if (al == 0) R_AX = 0;
        else if (al == 2) setlo(R_AX, 0);
        else if (al == 3) { fail(1); break; }                 // no upper memory blocks to link
        set_cf(false);
        break;
    case 0x59: R_AX = s_lasterr; R_BX = 0x0105; sethi(R_CX, 1); break;
    case 0x5A: {
        zstr(R_DS, R_DX, s_name, 140);
        const size_t n = d_strlen(s_name);
        if (n && s_name[n - 1] != '\\' && s_name[n - 1] != '/' && s_name[n - 1] != ':') d_strcat(s_name, "\\", sizeof s_name);
        const size_t stem = d_strlen(s_name);
        u32 seed = rd(0x46C);
        err = 5;
        for (int tries = 0; tries < 32 && err; tries++, seed += 7919) {
            char num[12];
            s_name[stem] = 0;
            d_strcat(s_name, "T", sizeof s_name);
            d_utoa(seed & 0xFFFFFF, num, 16);
            d_strcat(s_name, num, sizeof s_name);
            d_strcat(s_name, ".TMP", sizeof s_name);
            err = fs_create(s_name, 0, true, &handle);
            if (err && err != 80) break;
        }
        if (err) { fail((u16)err); break; }
        put_zstr(R_DS, (u16)(R_DX + stem), s_name + stem);
        R_AX = (u16)handle; set_cf(false);
        break; }
    case 0x60:
        zstr(R_DS, R_SI, s_name, sizeof s_name);
        err = fs_resolve(s_name, &s_path, true);
        if (err) { fail((u16)err); break; }
        fs_full_name(&s_path, s_name2, sizeof s_name2);
        put_zstr(R_ES, R_DI, s_name2);
        set_cf(false);
        break;
    case 0x63:
        if (al == 0) { R_DS = DOS_DATA; R_SI = 2; setlo(R_AX, 0); set_cf(false); }
        else fail(1);
        break;
    case 0x65:
        if (al == 1) {
            // The caller says how many bytes its buffer holds (CX); programs
            // ask for as few as 7. Never write past that.
            const u32 scratch = ma(DOS_DATA) + 0xC0, a = lin(R_ES, R_DI);
            wb(scratch, 1); ww(scratch + 1, 38); ww(scratch + 3, 1); ww(scratch + 5, 437);
            country_info(scratch + 7);
            if (R_CX < 5) { fail(1); break; }
            const u16 n = R_CX < 41 ? R_CX : 41;
            for (u16 i = 0; i < n; i++) wb(a + i, rb(scratch + i));
            R_CX = n; set_cf(false);
        } else if (al == 2 || al == 4 || al == 5 || al == 6 || al == 7) {
            // National tables (upper case, file-name upper case, file-name
            // terminators, collating sequence, DBCS lead bytes): code page 437.
            static const u16 where[8] = {0, 0, NLS_UPPER, 0, NLS_UPPER, NLS_FILECHARS, NLS_COLLATE, NLS_DBCS};
            if (R_CX < 5) { fail(1); break; }
            const u32 a = lin(R_ES, R_DI);
            wb(a, al); ww(a + 1, where[al]); ww(a + 3, g_stub_seg);
            R_CX = 5; set_cf(false);
        } else if (al == 0x20) { setlo(R_DX, (u8)d_upper((char)lo8(R_DX))); set_cf(false); }
        else if (al == 0x21 || al == 0x22) {
            u32 a = lin(R_DS, R_DX);
            for (u32 n = al == 0x21 ? R_CX : 0xFFFF; n; n--, a++) {
                const u8 ch = rb(a);
                if (al == 0x22 && !ch) break;
                wb(a, (u8)d_upper((char)ch));
            }
            set_cf(false);
        } else fail(1);
        break;
    case 0x66:
        if (al == 1) { R_BX = 437; R_DX = 437; }
        set_cf(false);
        break;
    case 0x67: case 0x68: case 0x5C: set_cf(false); break;
    case 0x6C: {
        zstr(R_DS, R_SI, s_name, sizeof s_name);
        u8 attr;
        const int ge = fs_getattr(s_name, &attr);
        const u8 action = lo8(R_DX);
        if (ge && ge != 2) { fail((u16)ge); break; }
        if (!ge) {
            if ((action & 0x0F) == 0) { fail(80); break; }
            if ((action & 0x0F) == 1) { err = fs_open(s_name, lo8(R_BX) & 3, &handle); w1 = 1; }
            else { err = fs_create(s_name, lo8(R_CX), false, &handle); w1 = 3; }
        } else {
            if (!(action & 0x10)) { fail(2); break; }
            err = fs_create(s_name, lo8(R_CX), false, &handle); w1 = 2;
        }
        if (err) fail((u16)err); else { R_AX = (u16)handle; R_CX = w1; set_cf(false); }
        break; }
    case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
    case 0x1B: case 0x1C: case 0x1F: case 0x21: case 0x22: case 0x23: case 0x24: case 0x27: case 0x28: case 0x32:
        setlo(R_AX, 0xFF);                                    // FCB file functions and drive tables: not provided
        break;
    case 0x71: case 0x73:
        // Long file names and FAT32 calls: "function not supported" the way
        // DOS 7 callers expect it (AX=7100h), so they fall back to the old calls.
        R_AX = 0x7100; s_lasterr = 1; set_cf(true);
        break;
    default: fail(1); break;
    }
}

static void memory_reset(void)
{
    // National tables in the ROM segment (INT 21h AH=65h).
    {
        const u32 rom = (u32)g_stub_seg << 4;
        g_mem[rom + NLS_UPPER] = 128; g_mem[rom + NLS_UPPER + 1] = 0;   // direct: the ROM is write-protected for programs
        for (int i = 0; i < 128; i++) g_mem[rom + NLS_UPPER + 2 + (u32)i] = (u8)(0x80 + i);
        g_mem[rom + NLS_COLLATE] = 0; g_mem[rom + NLS_COLLATE + 1] = 1;
        for (int i = 0; i < 256; i++) g_mem[rom + NLS_COLLATE + 2 + (u32)i] = (u8)(i >= 'a' && i <= 'z' ? i - 32 : i);
        static const u8 filechars[] = {22, 0, 1, 0x00, 0xFF, 0, 0x00, 0x20, 2, 14,
                                       '.', '"', '/', '\\', '[', ']', ':', '|', '<', '>', '+', '=', ';', ','};
        for (u32 i = 0; i < sizeof filechars; i++) g_mem[rom + NLS_FILECHARS + i] = filechars[i];
        d_memset(g_mem + rom + NLS_DBCS, 0, 4);
    }
    d_memset(g_mem + ma(DOS_DATA), 0, 0x100);
    g_mem[ma(DOS_DATA) + 0x0E] = (u8)FIRST_MCB; g_mem[ma(DOS_DATA) + 0x0F] = (u8)(FIRST_MCB >> 8);
    d_memset(g_mem + ma(FIRST_MCB), 0, 16);
    mcb_set(FIRST_MCB, 'Z', 0, (u16)(MEM_TOP - FIRST_MCB - 1));
    int err;
    const u16 env = make_env(0, "C:\\COMMAND.COM", &err);
    s_root_psp = mcb_alloc(0x10, 0xFFFF, &err);
    mcb_set((u16)(env - 1), 'M', s_root_psp, mcb_size((u16)(env - 1)));
    d_memcpy(g_mem + ma((u16)(s_root_psp - 1)) + 8, "COMMAND", 7);
    s_psp = s_root_psp;
    make_psp(s_root_psp, MEM_TOP, env, (const u8*)"", 0, s_root_psp);
    s_dta_seg = s_root_psp; s_dta_off = 0x80;
    fs_set_owner(s_root_psp);
}

void dos_init(void)
{
    g_env[0] = 0; g_env[1] = 0;
    env_set("COMSPEC", "C:\\COMMAND.COM");
    env_set("PATH", "C:\\");
    env_set("PROMPT", "$P$G");
    env_set("BLASTER", "A220 I5 D1 T3");                // r407: sound.cpp
    memory_reset();
}
