// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - text video: memory at B800:0000 mirrored to the console,
// teletype pass-through, INT 10h.
//
// The video memory is the truth. Teletype output (INT 10h/0Eh, DOS console
// output) is written to video memory AND handed to the console as running
// text, so a screen reader that hooks the console output hears whole lines.
// Everything else (direct writes, INT 10h/09h, scrolling windows) marks rows
// dirty; vid_sync() later redraws only the changed span of each changed row.
#include "dos.h"

enum { COLS = 80, ROWS = 25, CELLS = COLS * ROWS };

u32 g_vid_dirty;
static u8 s_shadow_ch[CELLS], s_shadow_at[CELLS];   // what the console shows
static dchar16 s_uni[CELLS];                        // exact character when CP437 was only a best fit
static u8 s_uni_fit[CELLS];
static bool s_native_scroll;                        // the console is exactly 80x25
static int s_con_x = -1, s_con_y = -1;              // believed console cursor
static bool s_cursor_on = true, s_con_cursor_on = true;
static dchar16 s_tty[160];
static int s_tty_n, s_tty_x, s_tty_y, s_tty_nx, s_tty_ny;
static u8 s_tty_attr;
static u32 s_scrolls;

static inline u8 page(void) { return g_mem[0x462] & 7; }
static inline u32 vbase(void) { return 0xB8000u + (u32)page() * 0x1000u; }
void vid_get_cursor(int* x, int* y) { *x = g_mem[0x450 + page() * 2]; *y = g_mem[0x451 + page() * 2]; }
void vid_set_cursor(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= COLS) x = COLS - 1;
    if (y < 0) y = 0;
    if (y >= ROWS) y = ROWS - 1;
    g_mem[0x450 + page() * 2] = (u8)x; g_mem[0x451 + page() * 2] = (u8)y;
}
u32 vid_scroll_count(void) { return s_scrolls; }

static void con_goto(int x, int y)
{
    if (s_con_x != x || s_con_y != y) { plat_con_goto(x, y); s_con_x = x; s_con_y = y; }
}

void vid_flush(void)
{
    if (!s_tty_n) return;
    con_goto(s_tty_x, s_tty_y);
    plat_con_out(s_tty, s_tty_n, s_tty_attr, true);
    s_tty_n = 0;
    s_con_x = s_tty_nx; s_con_y = s_tty_ny;
    if (s_con_x >= COLS) s_con_x = s_con_y = -1;      // wrapped: do not trust the firmware
}

static void tty_emit(dchar16 u, u8 attr, int x, int y)
{
    if (s_tty_n && (attr != s_tty_attr || x != s_tty_nx || y != s_tty_ny || s_tty_n >= 158)) vid_flush();
    if (!s_tty_n) { s_tty_x = x; s_tty_y = y; s_tty_attr = attr; }
    s_tty[s_tty_n++] = u;
    if (u == '\r') { s_tty_nx = 0; s_tty_ny = y; }
    else if (u == '\n') { s_tty_nx = x; s_tty_ny = y < ROWS - 1 ? y + 1 : y; }
    else { s_tty_nx = x + 1; s_tty_ny = y; }
}

static void invalidate_shadow(void)
{
    d_memset(s_shadow_ch, 0, CELLS); d_memset(s_shadow_at, 0xFF, CELLS);
    g_vid_dirty = 0x1FFFFFF;
}

// One line up on the whole active page, caused by teletype output at the
// bottom line. The console does the same by itself when it gets the LF.
static void tty_scroll(void)
{
    const u32 v = vbase();
    d_memmove(g_mem + v, g_mem + v + COLS * 2, (CELLS - COLS) * 2);
    for (int i = 0; i < COLS; i++) { g_mem[v + (CELLS - COLS + i) * 2] = ' '; g_mem[v + (CELLS - COLS + i) * 2 + 1] = 0x07; }
    d_memmove(s_uni, s_uni + COLS, (CELLS - COLS) * sizeof(dchar16));
    d_memmove(s_uni_fit, s_uni_fit + COLS, CELLS - COLS);
    d_memset(s_uni + CELLS - COLS, 0, COLS * sizeof(dchar16));
    s_scrolls++;
    if (s_native_scroll) {
        d_memmove(s_shadow_ch, s_shadow_ch + COLS, CELLS - COLS);
        d_memmove(s_shadow_at, s_shadow_at + COLS, CELLS - COLS);
        d_memset(s_shadow_ch + CELLS - COLS, ' ', COLS);
        d_memset(s_shadow_at + CELLS - COLS, 0x07, COLS);
        if (g_vid_dirty) g_vid_dirty = 0x1FFFFFF;
    } else {
        vid_flush();
        invalidate_shadow();
    }
}

void vid_tty(u8 ch, dchar16 uni, int attr)
{
    int x, y;
    vid_get_cursor(&x, &y);
    const u32 v = vbase();
    switch (ch) {
    case 7: return;
    case 8: if (x > 0) x--; break;
    case 13:
        if (s_native_scroll) tty_emit('\r', (u8)((s_tty_n && !(s_tty_attr & 0x70)) ? s_tty_attr : 0x07), x, y);
        x = 0;
        break;
    case 10:
        if (s_native_scroll) {
            // The LF must leave the console with a black new line.
            const u8 a = (u8)((s_tty_n && !(s_tty_attr & 0x70) && s_tty_nx == x && s_tty_ny == y) ? s_tty_attr : 0x07);
            tty_emit('\n', a, x, y);
        }
        if (y >= ROWS - 1) tty_scroll(); else y++;
        if (s_native_scroll) { vid_set_cursor(x, y); vid_flush(); return; }
        break;
    default: {
        const int i = y * COLS + x;
        g_mem[v + i * 2] = ch;
        if (attr >= 0) g_mem[v + i * 2 + 1] = (u8)attr;
        const u8 a = g_mem[v + i * 2 + 1] & 0x7F;
        dchar16 u = cp437_to_uni(ch);
        if (uni && uni != u) { s_uni[i] = uni; s_uni_fit[i] = ch; u = uni; } else s_uni[i] = 0;
        if (x == COLS - 1 && y == ROWS - 1) {
            // The bottom right cell is never written as running text: what
            // the firmware does after it differs. An explicit LF scrolls
            // instead and the cell is drawn by the next vid_sync().
            g_vid_dirty |= 1u << y;
        } else {
            s_shadow_ch[i] = ch; s_shadow_at[i] = a;
            tty_emit(u, a, x, y);
        }
        if (++x >= COLS) {
            x = 0;
            if (y >= ROWS - 1) {
                if (s_native_scroll) tty_emit('\n', 0x07, COLS - 1, ROWS - 1);
                vid_flush();
                tty_scroll();
            } else {
                vid_flush();
                y++;
            }
        }
        break; }
    }
    vid_set_cursor(x, y);
}

static void draw_span(int row, int from, int to)
{
    const u32 v = vbase();
    dchar16 run[COLS];
    int x = from;
    while (x <= to) {
        const u8 attr = g_mem[v + (row * COLS + x) * 2 + 1] & 0x7F;
        int n = 0;
        const int start = x;
        while (x <= to && (g_mem[v + (row * COLS + x) * 2 + 1] & 0x7F) == attr) {
            const int i = row * COLS + x;
            const u8 ch = g_mem[v + i * 2];
            run[n++] = (s_uni[i] && s_uni_fit[i] == ch) ? s_uni[i] : cp437_to_uni(ch);
            s_shadow_ch[i] = ch; s_shadow_at[i] = attr;
            x++;
        }
        int count = n;
        if (row == ROWS - 1 && start + n == COLS) count--;   // the last cell would scroll the console
        if (count > 0) {
            con_goto(start, row);
            plat_con_out(run, count, attr, false);
            s_con_x = start + count; s_con_y = row;
            if (s_con_x >= COLS) s_con_x = s_con_y = -1;
        }
    }
}

void vid_sync(void)
{
    vid_flush();
    if (g_vid_dirty) {
        const u32 v = vbase();
        const u32 dirty = g_vid_dirty;
        g_vid_dirty = 0;
        for (int row = 0; row < ROWS; row++) {
            if (!(dirty & (1u << row))) continue;
            int first = -1, last = -1;
            for (int x = 0; x < COLS; x++) {
                const int i = row * COLS + x;
                if (g_mem[v + i * 2] != s_shadow_ch[i] || (g_mem[v + i * 2 + 1] & 0x7F) != s_shadow_at[i]) {
                    if (first < 0) first = x;
                    last = x;
                }
            }
            if (first >= 0) draw_span(row, first, last);
        }
    }
    int x, y;
    vid_get_cursor(&x, &y);
    if (s_cursor_on != s_con_cursor_on) { plat_con_show_cursor(s_cursor_on); s_con_cursor_on = s_cursor_on; }
    con_goto(x, y);
}

static void clear_all(u8 attr)
{
    const u32 v = vbase();
    vid_flush();
    for (int i = 0; i < CELLS; i++) { g_mem[v + i * 2] = ' '; g_mem[v + i * 2 + 1] = attr; }
    d_memset(s_uni, 0, sizeof s_uni);
    plat_con_clear(attr & 0x7F);
    s_con_x = s_con_y = -1;
    d_memset(s_shadow_ch, ' ', CELLS); d_memset(s_shadow_at, attr & 0x7F, CELLS);
    g_vid_dirty = 0;
}

void vid_set_mode(u8 mode, bool clear)
{
    g_mem[0x449] = mode; g_mem[0x44A] = COLS; g_mem[0x44B] = 0;
    g_mem[0x44C] = 0x00; g_mem[0x44D] = 0x10;           // page size 4096
    g_mem[0x44E] = 0; g_mem[0x44F] = 0;
    for (int i = 0; i < 16; i++) g_mem[0x450 + i] = 0;
    g_mem[0x460] = 0x07; g_mem[0x461] = 0x06;           // cursor shape
    g_mem[0x462] = 0;
    g_mem[0x463] = 0xD4; g_mem[0x464] = 0x03;
    g_mem[0x465] = 0x29; g_mem[0x466] = 0x30;
    g_mem[0x484] = ROWS - 1; g_mem[0x485] = 16; g_mem[0x486] = 0;
    g_mem[0x487] = 0x60; g_mem[0x488] = 0xF9; g_mem[0x489] = 0x51; g_mem[0x48A] = 0x08;
    s_cursor_on = true;
    if (clear) clear_all(0x07);
}

void vid_init(void)
{
    int cols = 80, rows = 25;
    plat_con_size(&cols, &rows);
    s_native_scroll = cols == COLS && rows == ROWS;
    vid_set_mode(3, true);
}

// AL lines (0 = clear) in the window CH,CL - DH,DL of the active page.
static void scroll_window(bool up, int lines, u8 attr, int top, int left, int bottom, int right)
{
    if (bottom >= ROWS) bottom = ROWS - 1;
    if (right >= COLS) right = COLS - 1;
    if (top > bottom || left > right) return;
    const int height = bottom - top + 1;
    if (lines <= 0 || lines > height) lines = height;
    if (left == 0 && right == COLS - 1 && top == 0 && bottom == ROWS - 1) {
        if (lines == height) {
            clear_all(attr);
            return;
        }
        if (up && s_native_scroll && !(attr & 0x70)) {
            // Whole screen up: let the console scroll instead of redrawing 25 rows.
            vid_sync();
            for (int i = 0; i < lines; i++) {
                tty_emit('\n', (u8)(attr & 0x7F), 0, ROWS - 1);
                vid_flush();
                tty_scroll();
            }
            const u32 v = vbase();
            for (int i = (ROWS - lines) * COLS; i < CELLS; i++) {
                g_mem[v + i * 2 + 1] = attr; s_shadow_at[i] = attr & 0x7F;
            }
            s_con_x = s_con_y = -1;
            return;
        }
    }
    const u32 v = vbase();
    const int width = right - left + 1;
    for (int n = 0; n < height; n++) {
        const int row = up ? top + n : bottom - n;
        const int src = up ? row + lines : row - lines;
        const u32 d = v + (u32)(row * COLS + left) * 2;
        if (src >= top && src <= bottom) {
            d_memmove(g_mem + d, g_mem + v + (u32)(src * COLS + left) * 2, (size_t)width * 2);
            d_memmove(s_uni + row * COLS + left, s_uni + src * COLS + left, (size_t)width * sizeof(dchar16));
            d_memmove(s_uni_fit + row * COLS + left, s_uni_fit + src * COLS + left, (size_t)width);
        } else {
            for (int i = 0; i < width; i++) { g_mem[d + i * 2] = ' '; g_mem[d + i * 2 + 1] = attr; s_uni[row * COLS + left + i] = 0; }
        }
        g_vid_dirty |= 1u << row;
    }
}

void int10h(void)
{
    const u8 ah = hi8(R_AX), al = lo8(R_AX), bh = hi8(R_BX), bl = lo8(R_BX);
    int x, y;
    switch (ah) {
    case 0x00:
        vid_set_mode((al & 0x7F) == 7 ? 7 : ((al & 0x7F) <= 3 ? 3 : (al & 0x7F)), !(al & 0x80));
        break;
    case 0x01:
        g_mem[0x460] = lo8(R_CX); g_mem[0x461] = hi8(R_CX);
        s_cursor_on = (hi8(R_CX) & 0x60) != 0x20;
        break;
    case 0x02:
        if ((bh & 7) == page()) { vid_flush(); vid_set_cursor(lo8(R_DX), hi8(R_DX)); }
        else { g_mem[0x450 + (bh & 7) * 2] = lo8(R_DX); g_mem[0x451 + (bh & 7) * 2] = hi8(R_DX); }
        break;
    case 0x03:
        R_DX = (u16)(g_mem[0x450 + (bh & 7) * 2] | (g_mem[0x451 + (bh & 7) * 2] << 8));
        R_CX = (u16)(g_mem[0x460] | (g_mem[0x461] << 8));
        break;
    case 0x05:
        vid_flush();
        g_mem[0x462] = al & 7;
        g_mem[0x44E] = 0; g_mem[0x44F] = (u8)((al & 7) << 4);
        g_vid_dirty = 0x1FFFFFF;
        break;
    case 0x06: case 0x07:
        scroll_window(ah == 0x06, al, bh, hi8(R_CX), lo8(R_CX), hi8(R_DX), lo8(R_DX));
        break;
    case 0x08: {
        const u32 a = 0xB8000u + (u32)(bh & 7) * 0x1000u + (u32)(g_mem[0x451 + (bh & 7) * 2] * COLS + g_mem[0x450 + (bh & 7) * 2]) * 2;
        R_AX = (u16)(g_mem[a] | (g_mem[a + 1] << 8));
        break; }
    case 0x09: case 0x0A: {
        vid_flush();
        const u32 base = 0xB8000u + (u32)(bh & 7) * 0x1000u;
        int i = g_mem[0x451 + (bh & 7) * 2] * COLS + g_mem[0x450 + (bh & 7) * 2];
        for (u32 n = R_CX; n && i < CELLS; n--, i++) {
            wb(base + (u32)i * 2, al);
            if (ah == 0x09) wb(base + (u32)i * 2 + 1, bl);
        }
        break; }
    case 0x0E: vid_tty(al, 0, -1); break;
    case 0x0F: R_AX = (u16)((COLS << 8) | g_mem[0x449]); sethi(R_BX, page()); break;
    case 0x10: break;                                       // palette / blink: accepted
    case 0x11:
        if (al == 0x30) { R_CX = 16; setlo(R_DX, ROWS - 1); R_ES = 0xC000; R_BP = 0; }
        break;
    case 0x12:
        if (bl == 0x10) { R_BX = 0x0003; R_CX = 0x0009; }
        else if (bl == 0x30 || bl == 0x31 || bl == 0x32 || bl == 0x33 || bl == 0x34 || bl == 0x36) setlo(R_AX, 0x12);
        break;
    case 0x13: {
        vid_flush();
        int ox, oy;
        vid_get_cursor(&ox, &oy);
        vid_set_cursor(lo8(R_DX), hi8(R_DX));
        u32 p = lin(R_ES, R_BP);
        for (u32 n = R_CX; n; n--) {
            const u8 ch = rb(p++);
            int attr = bl;
            if (al & 2) attr = rb(p++);
            vid_tty(ch, 0, (ch == 7 || ch == 8 || ch == 10 || ch == 13) ? -1 : attr);
        }
        if (!(al & 1)) { vid_flush(); vid_set_cursor(ox, oy); }
        break; }
    case 0x1A:
        if (al == 0) { setlo(R_AX, 0x1A); R_BX = 0x0008; }  // VGA with a colour display
        break;
    default: break;
    }
    (void)x; (void)y;
}
