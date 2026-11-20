// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - BIOS side: interrupt stubs, BIOS data area, keyboard, timer,
// INT 11h/12h/15h/16h/1Ah, I/O ports, native interrupt dispatch.
//
// Every interrupt vector points at a four byte stub in segment F000:
//     0F FF nn   native service nn (only honoured inside this segment)
//     CF         IRET
// so programs can hook and chain vectors like on a real machine. The native
// service works on the registers of the caller and then returns through the
// stacked frame itself, keeping the flags it computed (CF, ZF).
#include "dos.h"

bool g_break, g_trap_retry, g_no_iret;
u16 g_stub_seg = 0xF000;
enum { STUB_BASE = 0x1000, TIMER_STUB = 0x0100, TICKS_PER_DAY = 0x1800B0 };

static u32 s_last_ms, s_last_sync_ms, s_last_kbd_ms, s_last_key_ms;
static u64 s_tick_acc;
static int s_pending_irq0;
static u16 s_pit;
static u8 s_crtc_index, s_port61, s_port3da;
static int s_empty_polls;

u16 stub_off(u8 n) { return (u16)(STUB_BASE + n * 4); }

// ---- keyboard -----------------------------------------------------------------
static const char s_plain[] = "\0\x1b" "1234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char s_shift[] = "\0\x1b" "!@#$%^&*()_+\b\tQWERTYUIOP{}\r\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

static u8 scan_of(u8 ch)
{
    for (int i = 1; i < 0x3A; i++) if (ch && ((u8)s_plain[i] == ch || (u8)s_shift[i] == ch)) return (u8)i;
    return 0;
}

static u16 translate(const PlatKey* k)
{
    const bool shift = k->mods & PM_SHIFT, ctrl = k->mods & PM_CTRL, alt = k->mods & PM_ALT;
    if (k->code) {
        static const u8 nav[] = {0, 0x48, 0x50, 0x4B, 0x4D, 0x47, 0x4F, 0x49, 0x51, 0x52, 0x53};
        static const u8 nav_ctrl[] = {0, 0x8D, 0x91, 0x73, 0x74, 0x77, 0x75, 0x84, 0x76, 0x92, 0x93};
        if (k->code >= PK_UP && k->code <= PK_DEL) return (u16)((ctrl ? nav_ctrl[k->code] : nav[k->code]) << 8);
        if (k->code == PK_ESC) return 0x011B;
        if (k->code >= PK_F1 && k->code <= PK_F10) {
            const u8 base = alt ? 0x68 : ctrl ? 0x5E : shift ? 0x54 : 0x3B;
            return (u16)((base + (k->code - PK_F1)) << 8);
        }
        if (k->code == PK_F11 || k->code == PK_F12) {
            const u8 base = alt ? 0x8B : ctrl ? 0x89 : shift ? 0x87 : 0x85;
            return (u16)((base + (k->code - PK_F11)) << 8);
        }
        return 0;
    }
    u16 ch = k->ch;
    if (!ch) return 0;
    if (ch == 10) ch = 13;
    if (alt && ch < 0x80) {
        const u8 lower = (u8)((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch);
        if (lower >= 'a' && lower <= 'z') return (u16)(scan_of(lower) << 8);
        if (lower >= '1' && lower <= '9') return (u16)((0x78 + lower - '1') << 8);
        if (lower == '0') return 0x8100;
        if (lower == '-') return 0x8200;
        if (lower == '=') return 0x8300;
    }
    if (ctrl && ch >= 0x20 && ch < 0x80) {
        const u8 lower = (u8)((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch);
        if (lower >= 'a' && lower <= 'z') return (u16)((scan_of(lower) << 8) | (lower & 0x1F));
        if (lower == '[') return 0x1A1B;
        if (lower == '\\') return 0x2B1C;
        if (lower == ']') return 0x1B1D;
        if (lower == '6') return 0x071E;
        if (lower == '-') return 0x0C1F;
        if (lower == '2' || lower == '@') return 0x0300;
    }
    if (ch < 0x20) {
        switch (ch) {
        case 8: return ctrl ? 0x0E7F : 0x0E08;
        case 9: return shift ? 0x0F00 : 0x0F09;
        case 13: return ctrl ? 0x1C0A : 0x1C0D;
        case 27: return 0x011B;
        default: return (u16)((scan_of((u8)(ch + 0x60)) << 8) | ch);     // Ctrl+letter delivered as a control code
        }
    }
    if (ch < 0x7F) return (u16)((scan_of((u8)ch) << 8) | ch);
    const u8 c = uni_to_cp437(ch, nullptr);
    return c;
}

static bool buffer_put(u16 key)
{
    const u16 tail = rw(0x41C);
    u16 next = (u16)(tail + 2);
    if (next >= rw(0x482)) next = rw(0x480);
    if (next == rw(0x41A)) return false;
    g_mem[0x400 + tail] = (u8)key; g_mem[0x401 + tail] = (u8)(key >> 8);
    g_mem[0x41C] = (u8)next; g_mem[0x41D] = (u8)(next >> 8);
    return true;
}
static bool buffer_full(void)
{
    u16 next = (u16)(rw(0x41C) + 2);
    if (next >= rw(0x482)) next = rw(0x480);
    return next == rw(0x41A);
}

void kbd_pump(void)
{
    PlatKey k;
    while (!buffer_full() && plat_key_poll(&k)) {
        if (k.code == PK_BREAK) { g_break = true; g_mem[0x471] |= 0x80; continue; }
        const u16 key = translate(&k);
        if (!key) continue;
        u8 flags = g_mem[0x417] & 0xF0;
        if (k.mods & PM_SHIFT) flags |= 2;
        if (k.mods & PM_CTRL) flags |= 4;
        if (k.mods & PM_ALT) flags |= 8;
        g_mem[0x417] = flags;
        s_last_key_ms = plat_millis();
        buffer_put(key);
    }
}
bool kbd_peek(u16* key)
{
    kbd_pump();
    const u16 head = rw(0x41A);
    if (head == rw(0x41C)) return false;
    *key = rw(0x400 + head);
    return true;
}
bool kbd_get(u16* key)
{
    if (!kbd_peek(key)) return false;
    u16 head = (u16)(rw(0x41A) + 2);
    if (head >= rw(0x482)) head = rw(0x480);
    g_mem[0x41A] = (u8)head; g_mem[0x41B] = (u8)(head >> 8);
    return true;
}
void kbd_flush(void)
{
    kbd_pump();
    g_mem[0x41A] = g_mem[0x41C]; g_mem[0x41B] = g_mem[0x41D];
}

static void update_ticks(void)
{
    const u32 now = plat_millis();
    const u32 delta = now - s_last_ms;
    if (!delta) return;
    s_last_ms = now;
    s_tick_acc += (u64)delta * 1193180u;
    const u32 n = (u32)(s_tick_acc / 65536000u);
    if (!n) return;
    s_tick_acc %= 65536000u;
    u32 ticks = rd(0x46C) + n;
    if (ticks >= TICKS_PER_DAY) { ticks %= TICKS_PER_DAY; g_mem[0x470] = 1; }
    g_mem[0x46C] = (u8)ticks; g_mem[0x46D] = (u8)(ticks >> 8); g_mem[0x46E] = (u8)(ticks >> 16); g_mem[0x46F] = (u8)(ticks >> 24);
    s_pending_irq0 += (int)n;
    if (s_pending_irq0 > 4) s_pending_irq0 = 4;
}

bool kbd_wait(u16* key)
{
    for (;;) {
        if (kbd_get(key)) return true;
        if (g_break || g_quit) return false;
        if (plat_quit()) { g_quit = true; return false; }
        vid_sync();
        plat_idle(2);
        update_ticks();
    }
}

// Called between instructions every thousand instructions or so.
void machine_poll(void)
{
    update_ticks();
    const u32 now = s_last_ms;
    if (now != s_last_kbd_ms) { s_last_kbd_ms = now; kbd_pump(); }
    if (now - s_last_sync_ms >= 40) { s_last_sync_ms = now; vid_sync(); }
    if (g_break) {
        g_break = false;
        if (dos_in_program()) { con_putc('^'); con_putc('C'); con_putc(13); con_putc(10); dos_terminate(0, 1, false); return; }
    }
    if (s_pending_irq0 > 0 && (R_FL & F_IF)) {
        const bool hooked = rw(8 * 4) != TIMER_STUB || rw(8 * 4 + 2) != g_stub_seg
            || rw(0x1C * 4) != stub_off(0x1C) || rw(0x1C * 4 + 2) != g_stub_seg;
        if (hooked) { s_pending_irq0--; cpu_interrupt(8); }
        else s_pending_irq0 = 0;
    }
}

// ---- ports ----------------------------------------------------------------------
u8 port_in(u16 port)
{
    switch (port) {
    case 0x21: case 0xA1: return 0;
    case 0x71: return 0;                                  // CMOS: no extended memory, no clock data
    case 0x40: s_pit = (u16)(s_pit - 19); return (u8)((s_pit & 1) ? (s_pit >> 8) : s_pit);
    case 0x60: return 0;
    case 0x61: s_port61 ^= 0x10; return s_port61;
    case 0x64: return 0x14;
    case 0x3D4: return s_crtc_index;
    case 0x3D5: {
        int x, y;
        vid_get_cursor(&x, &y);
        const u16 pos = (u16)(y * 80 + x);
        if (s_crtc_index == 0x0E) return (u8)(pos >> 8);
        if (s_crtc_index == 0x0F) return (u8)pos;
        return 0;
    }
    case 0x3DA: s_port3da ^= 0x09; return s_port3da;
    default: return 0xFF;
    }
}
void port_out(u16 port, u8 value)
{
    static u16 cursor;
    switch (port) {
    case 0x61: s_port61 = (u8)((s_port61 & 0x10) | (value & 0x0F)); break;
    case 0x3D4: s_crtc_index = value; break;
    case 0x3D5:
        if (s_crtc_index == 0x0E) cursor = (u16)((cursor & 0x00FF) | (value << 8));
        else if (s_crtc_index == 0x0F) { cursor = (u16)((cursor & 0xFF00) | value); vid_flush(); vid_set_cursor(cursor % 80, cursor / 80); }
        break;
    default: break;
    }
}

// ---- BIOS services ---------------------------------------------------------------
static u8 bcd(unsigned v) { return (u8)(((v / 10) << 4) | (v % 10)); }

static void int16h(void)
{
    u16 key;
    switch (hi8(R_AX)) {
    case 0x00: case 0x10:
        if (kbd_get(&key)) { R_AX = key; s_empty_polls = 0; break; }
        if (plat_quit()) { g_quit = true; break; }
        vid_sync();
        plat_idle(2);
        g_trap_retry = true;                  // come back here; timer interrupts can run in between
        break;
    case 0x01: case 0x11:
        if (kbd_peek(&key)) { R_AX = key; setflag(F_ZF, false); s_empty_polls = 0; }
        else {
            setflag(F_ZF, true);
            if (++s_empty_polls > 64) { s_empty_polls = 0; vid_sync(); plat_idle(1); if (plat_quit()) g_quit = true; }
        }
        break;
    case 0x02: case 0x12:
        if (plat_millis() - s_last_key_ms > 400) g_mem[0x417] &= 0xF0;
        setlo(R_AX, g_mem[0x417]);
        if (hi8(R_AX) == 0x12) sethi(R_AX, 0);
        break;
    case 0x05: setlo(R_AX, buffer_put(R_CX) ? 0 : 1); break;
    default: break;
    }
}

static void int1ah(void)
{
    PlatTime t;
    switch (hi8(R_AX)) {
    case 0x00:
        update_ticks();
        R_CX = rw(0x46E); R_DX = rw(0x46C); setlo(R_AX, g_mem[0x470]); g_mem[0x470] = 0;
        break;
    case 0x01:
        g_mem[0x46C] = lo8(R_DX); g_mem[0x46D] = hi8(R_DX); g_mem[0x46E] = lo8(R_CX); g_mem[0x46F] = hi8(R_CX);
        break;
    case 0x02:
        plat_time(&t);
        R_CX = (u16)((bcd(t.hour) << 8) | bcd(t.minute)); R_DX = (u16)(bcd(t.second) << 8); set_cf(false);
        break;
    case 0x04:
        plat_time(&t);
        R_CX = (u16)((bcd(t.year / 100u) << 8) | bcd(t.year % 100u)); R_DX = (u16)((bcd(t.month) << 8) | bcd(t.day)); set_cf(false);
        break;
    default: set_cf(true); break;
    }
}

static void int15h(void)
{
    switch (hi8(R_AX)) {
    case 0x86: {
        u32 ms = ((((u32)R_CX << 16) | R_DX) + 999) / 1000;
        const u32 start = plat_millis();
        vid_sync();
        while (plat_millis() - start < ms && !g_quit) { plat_idle(1); update_ticks(); if (plat_quit()) break; }
        set_cf(false);
        break; }
    case 0x88: R_AX = 0; set_cf(false); break;
    default: sethi(R_AX, 0x86); set_cf(true); break;
    }
}

void illegal_instruction(u16 cs, u16 ip, u8 opcode)
{
    char text[24], num[8];
    text[0] = 0;
    for (int part = 0; part < 3; part++) {
        const u32 v = part == 0 ? cs : part == 1 ? ip : opcode;
        const int width = part == 2 ? 2 : 4;
        const int n = d_utoa(v, num, 16);
        for (int i = n; i < width; i++) d_strcat(text, "0", sizeof text);
        d_strcat(text, num, sizeof text);
        if (part == 0) d_strcat(text, ":", sizeof text);
        if (part == 1) d_strcat(text, " ", sizeof text);
    }
    shell_notice(MSG_ILLEGAL, text, 0);
    dos_terminate(0xFF, 2, false);
}
void divide_error(void) { cpu_interrupt(0); }

void bios_trap(u8 n)
{
    // The stack holds IP, CS and FLAGS of the caller.
    R_FL = (u16)((rw(lin(R_SS, (u16)(R_SP + 4))) & 0x0FD5) | 0xF002);
    g_no_iret = false; g_trap_retry = false;
    const u16 before[4] = { R_AX, R_BX, R_CX, R_DX };
    switch (n) {
    case 0x00: shell_notice(MSG_DIVIDE, "", 0); dos_terminate(0xFF, 2, false); break;
    case 0x10: int10h(); break;
    case 0x11: R_AX = rw(0x410); break;
    case 0x12: R_AX = rw(0x413); break;
    case 0x13: sethi(R_AX, 0x01); set_cf(true); break;
    case 0x14: sethi(R_AX, 0x80); break;
    case 0x15: int15h(); break;
    case 0x16: int16h(); break;
    case 0x17: sethi(R_AX, 0x30); break;
    case 0x19: dos_terminate(0, 2, false); break;
    case 0x1A: int1ah(); break;
    case 0x20: dos_terminate(0, 0, false); break;
    case 0x21: int21h(); break;
    case 0x24: setlo(R_AX, 3); break;
    case 0x25: case 0x26: R_AX = 0x0207; set_cf(true); break;
    case 0x27: {
        // Terminate and stay resident: DX bytes from the PSP in the caller's CS.
        const u16 keep = (u16)((R_DX + 15) >> 4);
        R_DX = keep;
        dos_terminate(0, 3, true);
        break; }
    case 0x28: plat_idle(0); break;
    case 0x29: con_putc(lo8(R_AX)); break;
    case 0x2F:
        if (R_AX == 0x1680) { vid_sync(); plat_idle(1); setlo(R_AX, 0); }
        break;
    default: break;
    }
    if (!g_trap_retry) plat_trace_int(n, before, R_AX, (R_FL & F_CF) != 0);
    const bool no_iret = g_no_iret, retry = g_trap_retry;
    g_no_iret = false; g_trap_retry = false;
    if (no_iret) return;
    if (retry) { g_cpu.ip = (u16)(g_cpu.ip - 3); g_poll_countdown = 0; return; }
    const u16 flags = R_FL;
    g_cpu.ip = cpu_pop();
    R_CS = cpu_pop();
    if (n != 0x25 && n != 0x26) (void)cpu_pop();      // INT 25h/26h leave the flags word on the stack
    R_FL = flags;
}

void bios_init(void)
{
    const u32 rom = (u32)g_stub_seg << 4;
    for (int n = 0; n < 256; n++) {
        const u32 a = rom + stub_off((u8)n);
        g_mem[a] = 0x0F; g_mem[a + 1] = 0xFF; g_mem[a + 2] = (u8)n; g_mem[a + 3] = 0xCF;
        g_mem[n * 4] = (u8)stub_off((u8)n); g_mem[n * 4 + 1] = (u8)(stub_off((u8)n) >> 8);
        g_mem[n * 4 + 2] = (u8)g_stub_seg; g_mem[n * 4 + 3] = (u8)(g_stub_seg >> 8);
    }
    // Timer interrupt: INT 1Ch, IRET.
    g_mem[rom + TIMER_STUB] = 0xCD; g_mem[rom + TIMER_STUB + 1] = 0x1C; g_mem[rom + TIMER_STUB + 2] = 0xCF;
    g_mem[8 * 4] = (u8)TIMER_STUB; g_mem[8 * 4 + 1] = (u8)(TIMER_STUB >> 8);
    g_mem[rom + 0xF0] = 0xCB; g_mem[rom + 0xF1] = 0xCF;
    // Reset vector, BIOS date, model byte.
    static const u8 reset[] = {0xEA, 0xF1, 0x00, 0x00, 0xF0, '0', '1', '/', '0', '1', '/', '9', '9', 0x00, 0xFC, 0x00};
    d_memcpy(g_mem + rom + 0xFFF0, reset, sizeof reset);

    // BIOS data area.
    g_mem[0x410] = 0x20; g_mem[0x411] = 0x00;           // 80x25 colour, no floppy, no 8087
    g_mem[0x413] = 0x80; g_mem[0x414] = 0x02;           // 640 KiB
    g_mem[0x41A] = 0x1E; g_mem[0x41C] = 0x1E;
    g_mem[0x480] = 0x1E; g_mem[0x482] = 0x3E;
    g_mem[0x496] = 0x10;                                // enhanced keyboard
    PlatTime t;
    plat_time(&t);
    const u32 ticks = (u32)(((u64)tod_millis(&t) * 1193180u) / 65536000u);
    g_mem[0x46C] = (u8)ticks; g_mem[0x46D] = (u8)(ticks >> 8); g_mem[0x46E] = (u8)(ticks >> 16); g_mem[0x46F] = (u8)(ticks >> 24);
    s_last_ms = s_last_sync_ms = plat_millis();
    s_pit = 0xFFFF;
}
