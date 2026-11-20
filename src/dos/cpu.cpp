// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - 8086 / 80186 real-mode instruction interpreter.
// Written for CharonDOS from the Intel instruction descriptions; no code
// from other emulators.
#include "dos.h"

Cpu g_cpu;
u8* g_mem;
bool g_stop, g_quit;
int g_poll_countdown;

#define C g_cpu
#define FL g_cpu.fl

static u16 s_ovr;            // segment override value
static bool s_has_ovr;
static u8 s_rep;             // 0, 0xF2, 0xF3
static bool s_ea_reg;        // decoded operand is a register
static u8 s_ea_r;
static u16 s_ea_seg, s_ea_off;
static u8 s_reg;             // reg field of ModRM

static inline u8 mrb(u16 seg, u16 off) { return rb(lin(seg, off)); }
static inline u16 mrw(u16 seg, u16 off) { return (u16)(rb(lin(seg, off)) | (rb(lin(seg, (u16)(off + 1))) << 8)); }
static inline void mwb(u16 seg, u16 off, u8 v) { wb(lin(seg, off), v); }
static inline void mww(u16 seg, u16 off, u16 v) { wb(lin(seg, off), (u8)v); wb(lin(seg, (u16)(off + 1)), (u8)(v >> 8)); }
static inline u8 fetch8(void) { u8 v = g_mem[lin(C.s[sCS], C.ip)]; C.ip++; return v; }
static inline u16 fetch16(void) { u16 l = fetch8(); return (u16)(l | (fetch8() << 8)); }

void cpu_push(u16 v) { C.r[rSP] = (u16)(C.r[rSP] - 2); mww(C.s[sSS], C.r[rSP], v); }
u16 cpu_pop(void) { u16 v = mrw(C.s[sSS], C.r[rSP]); C.r[rSP] = (u16)(C.r[rSP] + 2); return v; }

void cpu_interrupt(u8 n)
{
    cpu_push((u16)(FL | 0xF002));
    cpu_push(C.s[sCS]);
    cpu_push(C.ip);
    FL &= (u16)~(F_IF | F_TF);
    C.ip = rw((u32)n * 4);
    C.s[sCS] = rw((u32)n * 4 + 2);
}

static inline u8 gr8(int i) { return i < 4 ? (u8)C.r[i] : (u8)(C.r[i - 4] >> 8); }
static inline void sr8(int i, u8 v) { if (i < 4) setlo(C.r[i], v); else sethi(C.r[i - 4], v); }

static void decode(void)
{
    u8 m = fetch8();
    u8 mod = m >> 6, rm = m & 7;
    s_reg = (m >> 3) & 7;
    if (mod == 3) { s_ea_reg = true; s_ea_r = rm; return; }
    s_ea_reg = false;
    u16 off = 0; int seg = sDS;
    switch (rm) {
    case 0: off = (u16)(C.r[rBX] + C.r[rSI]); break;
    case 1: off = (u16)(C.r[rBX] + C.r[rDI]); break;
    case 2: off = (u16)(C.r[rBP] + C.r[rSI]); seg = sSS; break;
    case 3: off = (u16)(C.r[rBP] + C.r[rDI]); seg = sSS; break;
    case 4: off = C.r[rSI]; break;
    case 5: off = C.r[rDI]; break;
    case 6: if (mod == 0) off = fetch16(); else { off = C.r[rBP]; seg = sSS; } break;
    default: off = C.r[rBX]; break;
    }
    if (mod == 1) off = (u16)(off + (int8_t)fetch8());
    else if (mod == 2) off = (u16)(off + fetch16());
    s_ea_seg = s_has_ovr ? s_ovr : C.s[seg];
    s_ea_off = off;
}
static inline u8 ea_r8(void) { return s_ea_reg ? gr8(s_ea_r) : mrb(s_ea_seg, s_ea_off); }
static inline u16 ea_r16(void) { return s_ea_reg ? C.r[s_ea_r] : mrw(s_ea_seg, s_ea_off); }
static inline void ea_w8(u8 v) { if (s_ea_reg) sr8(s_ea_r, v); else mwb(s_ea_seg, s_ea_off, v); }
static inline void ea_w16(u16 v) { if (s_ea_reg) C.r[s_ea_r] = v; else mww(s_ea_seg, s_ea_off, v); }
static inline u32 ea_r(int w) { return w ? ea_r16() : ea_r8(); }
static inline void ea_w(int w, u32 v) { if (w) ea_w16((u16)v); else ea_w8((u8)v); }
static inline u32 reg_r(int w, int i) { return w ? C.r[i] : gr8(i); }
static inline void reg_w(int w, int i, u32 v) { if (w) C.r[i] = (u16)v; else sr8(i, (u8)v); }

// ---- flags ----------------------------------------------------------------
static inline void fset(u16 m, bool on) { if (on) FL |= m; else FL &= (u16)~m; }
static inline bool parity_even(u32 v) { v ^= v >> 4; return ((0x9669u >> (v & 0xF)) & 1) != 0; }
static inline void szp(u32 r, int bits)
{
    r &= bits == 8 ? 0xFFu : 0xFFFFu;
    fset(F_ZF, r == 0);
    fset(F_SF, ((r >> (bits - 1)) & 1) != 0);
    fset(F_PF, parity_even(r & 0xFF));
}
static u32 f_add(u32 a, u32 b, u32 cin, int bits)
{
    u32 r = a + b + cin;
    fset(F_CF, ((r >> bits) & 1) != 0);
    fset(F_AF, ((a ^ b ^ r) & 0x10) != 0);
    fset(F_OF, ((((a ^ r) & (b ^ r)) >> (bits - 1)) & 1) != 0);
    szp(r, bits);
    return r & (bits == 8 ? 0xFFu : 0xFFFFu);
}
static u32 f_sub(u32 a, u32 b, u32 bin, int bits)
{
    u32 r = a - b - bin;
    fset(F_CF, ((r >> bits) & 1) != 0);
    fset(F_AF, ((a ^ b ^ r) & 0x10) != 0);
    fset(F_OF, ((((a ^ b) & (a ^ r)) >> (bits - 1)) & 1) != 0);
    szp(r, bits);
    return r & (bits == 8 ? 0xFFu : 0xFFFFu);
}
static u32 f_logic(u32 r, int bits)
{
    FL &= (u16)~(F_CF | F_OF | F_AF);
    szp(r, bits);
    return r;
}
// ADD OR ADC SBB AND SUB XOR CMP
static u32 alu(int op, u32 a, u32 b, int bits)
{
    switch (op) {
    case 0: return f_add(a, b, 0, bits);
    case 1: return f_logic(a | b, bits);
    case 2: return f_add(a, b, FL & F_CF, bits);
    case 3: return f_sub(a, b, FL & F_CF, bits);
    case 4: return f_logic(a & b, bits);
    case 5: return f_sub(a, b, 0, bits);
    case 6: return f_logic(a ^ b, bits);
    default: f_sub(a, b, 0, bits); return a;
    }
}
static u32 f_inc(u32 a, int bits) { u16 cf = FL & F_CF; u32 r = f_add(a, 1, 0, bits); FL = (u16)((FL & ~F_CF) | cf); return r; }
static u32 f_dec(u32 a, int bits) { u16 cf = FL & F_CF; u32 r = f_sub(a, 1, 0, bits); FL = (u16)((FL & ~F_CF) | cf); return r; }

// ROL ROR RCL RCR SHL SHR SAL SAR; the 80186 masks the count to five bits.
static u32 f_shift(int op, u32 v, unsigned count, int bits)
{
    count &= 0x1F;
    if (!count) return v;
    const u32 mask = bits == 8 ? 0xFFu : 0xFFFFu, msb = 1u << (bits - 1);
    u32 cf = FL & F_CF, n;
    switch (op) {
    case 0:
        for (unsigned i = 0; i < count; i++) { cf = (v & msb) ? 1 : 0; v = ((v << 1) | cf) & mask; }
        fset(F_CF, cf != 0); fset(F_OF, (((v & msb) != 0) ? 1u : 0u) != cf);
        break;
    case 1:
        for (unsigned i = 0; i < count; i++) { cf = v & 1; v = (v >> 1) | (cf ? msb : 0); }
        fset(F_CF, cf != 0); fset(F_OF, ((v & msb) != 0) != ((v & (msb >> 1)) != 0));
        break;
    case 2:
        for (unsigned i = 0; i < count; i++) { n = (v & msb) ? 1 : 0; v = ((v << 1) | cf) & mask; cf = n; }
        fset(F_CF, cf != 0); fset(F_OF, (((v & msb) != 0) ? 1u : 0u) != cf);
        break;
    case 3:
        for (unsigned i = 0; i < count; i++) { n = v & 1; v = (v >> 1) | (cf ? msb : 0); cf = n; }
        fset(F_CF, cf != 0); fset(F_OF, ((v & msb) != 0) != ((v & (msb >> 1)) != 0));
        break;
    case 4: case 6:
        for (unsigned i = 0; i < count; i++) { cf = (v & msb) ? 1 : 0; v = (v << 1) & mask; }
        fset(F_CF, cf != 0); fset(F_OF, (((v & msb) != 0) ? 1u : 0u) != cf);
        szp(v, bits); FL &= (u16)~F_AF;
        break;
    case 5:
        fset(F_OF, (v & msb) != 0);
        for (unsigned i = 0; i < count; i++) { cf = v & 1; v >>= 1; }
        fset(F_CF, cf != 0); szp(v, bits); FL &= (u16)~F_AF;
        break;
    default:
        for (unsigned i = 0; i < count; i++) { cf = v & 1; v = (v >> 1) | (v & msb); }
        fset(F_CF, cf != 0); fset(F_OF, false); szp(v, bits); FL &= (u16)~F_AF;
        break;
    }
    return v;
}

static bool cond(int cc)
{
    bool r;
    switch (cc >> 1) {
    case 0: r = (FL & F_OF) != 0; break;
    case 1: r = (FL & F_CF) != 0; break;
    case 2: r = (FL & F_ZF) != 0; break;
    case 3: r = (FL & (F_CF | F_ZF)) != 0; break;
    case 4: r = (FL & F_SF) != 0; break;
    case 5: r = (FL & F_PF) != 0; break;
    case 6: r = ((FL & F_SF) != 0) != ((FL & F_OF) != 0); break;
    default: r = (FL & F_ZF) || (((FL & F_SF) != 0) != ((FL & F_OF) != 0)); break;
    }
    return (cc & 1) ? !r : r;
}

static void string_op(u8 op)
{
    const int w = op & 1, bits = w ? 16 : 8;
    const u16 step = (u16)((FL & F_DF) ? -(1 + w) : (1 + w));
    const u16 sseg = s_has_ovr ? s_ovr : C.s[sDS];
    const u8 kind = op & 0xFE;
    if (s_rep && C.r[rCX] == 0) return;
    for (;;) {
        u32 a, b;
        switch (kind) {
        case 0xA4:
            if (w) mww(C.s[sES], C.r[rDI], mrw(sseg, C.r[rSI])); else mwb(C.s[sES], C.r[rDI], mrb(sseg, C.r[rSI]));
            C.r[rSI] = (u16)(C.r[rSI] + step); C.r[rDI] = (u16)(C.r[rDI] + step);
            break;
        case 0xA6:
            a = w ? mrw(sseg, C.r[rSI]) : mrb(sseg, C.r[rSI]);
            b = w ? mrw(C.s[sES], C.r[rDI]) : mrb(C.s[sES], C.r[rDI]);
            f_sub(a, b, 0, bits);
            C.r[rSI] = (u16)(C.r[rSI] + step); C.r[rDI] = (u16)(C.r[rDI] + step);
            break;
        case 0xAA:
            if (w) mww(C.s[sES], C.r[rDI], C.r[rAX]); else mwb(C.s[sES], C.r[rDI], (u8)C.r[rAX]);
            C.r[rDI] = (u16)(C.r[rDI] + step);
            break;
        case 0xAC:
            if (w) C.r[rAX] = mrw(sseg, C.r[rSI]); else setlo(C.r[rAX], mrb(sseg, C.r[rSI]));
            C.r[rSI] = (u16)(C.r[rSI] + step);
            break;
        case 0xAE:
            b = w ? mrw(C.s[sES], C.r[rDI]) : mrb(C.s[sES], C.r[rDI]);
            f_sub(w ? C.r[rAX] : (u8)C.r[rAX], b, 0, bits);
            C.r[rDI] = (u16)(C.r[rDI] + step);
            break;
        case 0x6C:
            if (w) mww(C.s[sES], C.r[rDI], (u16)(port_in(C.r[rDX]) | (port_in((u16)(C.r[rDX] + 1)) << 8)));
            else mwb(C.s[sES], C.r[rDI], port_in(C.r[rDX]));
            C.r[rDI] = (u16)(C.r[rDI] + step);
            break;
        default:   // 0x6E OUTS
            port_out(C.r[rDX], mrb(sseg, C.r[rSI]));
            if (w) port_out((u16)(C.r[rDX] + 1), mrb(sseg, (u16)(C.r[rSI] + 1)));
            C.r[rSI] = (u16)(C.r[rSI] + step);
            break;
        }
        if (!s_rep) break;
        C.r[rCX]--;
        if (kind == 0xA6 || kind == 0xAE) {
            if (s_rep == 0xF3 && !(FL & F_ZF)) break;
            if (s_rep == 0xF2 && (FL & F_ZF)) break;
        }
        if (C.r[rCX] == 0) break;
    }
}

static inline void jump_rel8(bool taken) { int8_t d = (int8_t)fetch8(); if (taken) C.ip = (u16)(C.ip + d); }

// Executes one instruction. Returns false for an undefined opcode.
static bool step(void)
{
    s_has_ovr = false; s_rep = 0;
    const u16 start_ip = C.ip;
    u8 op;
    for (int guard = 0;; guard++) {
        op = fetch8();
        if (op == 0x26) { s_ovr = C.s[sES]; s_has_ovr = true; }
        else if (op == 0x2E) { s_ovr = C.s[sCS]; s_has_ovr = true; }
        else if (op == 0x36) { s_ovr = C.s[sSS]; s_has_ovr = true; }
        else if (op == 0x3E) { s_ovr = C.s[sDS]; s_has_ovr = true; }
        else if (op == 0xF2 || op == 0xF3) s_rep = op;
        else if (op == 0xF0) {}
        else break;
        if (guard > 14) { C.ip = start_ip; return false; }
    }
    const int w = op & 1, bits = w ? 16 : 8;
    u32 a, b, r;

    if (op < 0x40) {
        const int kind = op & 7, aop = (op >> 3) & 7;
        if (kind < 6) {
            switch (kind) {
            case 0: case 1:
                decode(); a = ea_r(w); b = reg_r(w, s_reg); r = alu(aop, a, b, bits);
                if (aop != 7) ea_w(w, r);
                break;
            case 2: case 3:
                decode(); a = reg_r(w, s_reg); b = ea_r(w); r = alu(aop, a, b, bits);
                if (aop != 7) reg_w(w, s_reg, r);
                break;
            default:
                b = w ? fetch16() : fetch8(); a = reg_r(w, 0); r = alu(aop, a, b, bits);
                if (aop != 7) reg_w(w, 0, r);
                break;
            }
            return true;
        }
        if (op < 0x20) {
            if (op == 0x0F) {
                // Native service trap, valid only inside the BIOS stub segment.
                if (C.s[sCS] == g_stub_seg && mrb(C.s[sCS], C.ip) == 0xFF) {
                    C.ip++;
                    bios_trap(fetch8());
                    return true;
                }
                C.ip = start_ip;
                return false;
            }
            const int sr = (op >> 3) & 3;
            if (kind == 6) cpu_push(C.s[sr]);
            else { C.s[sr] = cpu_pop(); if (sr == sSS && g_poll_countdown < 2) g_poll_countdown = 2; }
            return true;
        }
        u8 al = (u8)C.r[rAX], ah = (u8)(C.r[rAX] >> 8);
        const bool af = (FL & F_AF) != 0, cf = (FL & F_CF) != 0;
        switch (op) {
        case 0x27: {   // DAA
            const u8 old = al; bool ncf = false;
            if ((al & 0xF) > 9 || af) { al = (u8)(al + 6); fset(F_AF, true); } else fset(F_AF, false);
            if (old > 0x99 || cf) { al = (u8)(al + 0x60); ncf = true; }
            fset(F_CF, ncf); szp(al, 8); setlo(C.r[rAX], al);
            break; }
        case 0x2F: {   // DAS
            const u8 old = al; bool ncf = false;
            if ((al & 0xF) > 9 || af) { ncf = cf || al < 6; al = (u8)(al - 6); fset(F_AF, true); } else fset(F_AF, false);
            if (old > 0x99 || cf) { al = (u8)(al - 0x60); ncf = true; }
            fset(F_CF, ncf); szp(al, 8); setlo(C.r[rAX], al);
            break; }
        case 0x37:     // AAA
            if ((al & 0xF) > 9 || af) { al = (u8)(al + 6); ah = (u8)(ah + 1); FL |= F_AF | F_CF; } else FL &= (u16)~(F_AF | F_CF);
            C.r[rAX] = (u16)((ah << 8) | (al & 0xF));
            break;
        default:       // 0x3F AAS
            if ((al & 0xF) > 9 || af) { al = (u8)(al - 6); ah = (u8)(ah - 1); FL |= F_AF | F_CF; } else FL &= (u16)~(F_AF | F_CF);
            C.r[rAX] = (u16)((ah << 8) | (al & 0xF));
            break;
        }
        return true;
    }

    switch (op) {
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        C.r[op & 7] = (u16)f_inc(C.r[op & 7], 16); break;
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        C.r[op & 7] = (u16)f_dec(C.r[op & 7], 16); break;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x55: case 0x56: case 0x57:
        cpu_push(C.r[op & 7]); break;
    case 0x54: C.r[rSP] = (u16)(C.r[rSP] - 2); mww(C.s[sSS], C.r[rSP], C.r[rSP]); break;   // 8086/186: the new SP
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        C.r[op & 7] = cpu_pop(); break;
    case 0x60: {
        const u16 sp = C.r[rSP];
        cpu_push(C.r[rAX]); cpu_push(C.r[rCX]); cpu_push(C.r[rDX]); cpu_push(C.r[rBX]);
        cpu_push(sp); cpu_push(C.r[rBP]); cpu_push(C.r[rSI]); cpu_push(C.r[rDI]);
        break; }
    case 0x61:
        C.r[rDI] = cpu_pop(); C.r[rSI] = cpu_pop(); C.r[rBP] = cpu_pop(); (void)cpu_pop();
        C.r[rBX] = cpu_pop(); C.r[rDX] = cpu_pop(); C.r[rCX] = cpu_pop(); C.r[rAX] = cpu_pop();
        break;
    case 0x62: decode(); break;                                   // BOUND: no check
    case 0x68: cpu_push(fetch16()); break;
    case 0x6A: cpu_push((u16)(int8_t)fetch8()); break;
    case 0x69: case 0x6B: {
        decode();
        const int32_t m1 = (int16_t)ea_r16();
        const int32_t m2 = op == 0x69 ? (int16_t)fetch16() : (int8_t)fetch8();
        const int32_t p = m1 * m2;
        C.r[s_reg] = (u16)p;
        const bool ovf = p != (int16_t)p;
        fset(F_CF, ovf); fset(F_OF, ovf);
        break; }
    case 0x6C: case 0x6D: case 0x6E: case 0x6F: string_op(op); break;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
        jump_rel8(cond(op & 0xF)); break;
    case 0x80: case 0x81: case 0x82: case 0x83:
        decode(); a = ea_r(w);
        if (op == 0x81) b = fetch16();
        else if (op == 0x83) b = (u16)(int8_t)fetch8();
        else b = fetch8();
        r = alu(s_reg, a, b, bits);
        if (s_reg != 7) ea_w(w, r);
        break;
    case 0x84: case 0x85: decode(); f_logic(ea_r(w) & reg_r(w, s_reg), bits); break;
    case 0x86: case 0x87: decode(); a = ea_r(w); ea_w(w, reg_r(w, s_reg)); reg_w(w, s_reg, a); break;
    case 0x88: case 0x89: decode(); ea_w(w, reg_r(w, s_reg)); break;
    case 0x8A: case 0x8B: decode(); reg_w(w, s_reg, ea_r(w)); break;
    case 0x8C: decode(); ea_w16(C.s[s_reg & 3]); break;
    case 0x8D: decode(); if (s_ea_reg) { C.ip = start_ip; return false; } C.r[s_reg] = s_ea_off; break;
    case 0x8E:
        decode(); C.s[s_reg & 3] = ea_r16();
        if ((s_reg & 3) == sSS && g_poll_countdown < 2) g_poll_countdown = 2;
        break;
    case 0x8F: { const u16 v = cpu_pop(); decode(); ea_w16(v); break; }
    case 0x90: break;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        a = C.r[rAX]; C.r[rAX] = C.r[op & 7]; C.r[op & 7] = (u16)a; break;
    case 0x98: C.r[rAX] = (u16)(int16_t)(int8_t)C.r[rAX]; break;
    case 0x99: C.r[rDX] = (C.r[rAX] & 0x8000) ? 0xFFFF : 0; break;
    case 0x9A: { const u16 ip = fetch16(), cs = fetch16(); cpu_push(C.s[sCS]); cpu_push(C.ip); C.s[sCS] = cs; C.ip = ip; break; }
    case 0x9B: break;
    case 0x9C: cpu_push((u16)(FL | 0xF002)); break;
    case 0x9D: FL = (u16)((cpu_pop() & 0x0FD5) | 0xF002); break;
    case 0x9E: FL = (u16)((FL & 0xFF00) | ((C.r[rAX] >> 8) & 0xD5) | 2); break;
    case 0x9F: sethi(C.r[rAX], (u8)((FL & 0xD5) | 2)); break;
    case 0xA0: setlo(C.r[rAX], mrb(s_has_ovr ? s_ovr : C.s[sDS], fetch16())); break;
    case 0xA1: C.r[rAX] = mrw(s_has_ovr ? s_ovr : C.s[sDS], fetch16()); break;
    case 0xA2: mwb(s_has_ovr ? s_ovr : C.s[sDS], fetch16(), (u8)C.r[rAX]); break;
    case 0xA3: mww(s_has_ovr ? s_ovr : C.s[sDS], fetch16(), C.r[rAX]); break;
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
        string_op(op); break;
    case 0xA8: f_logic((u8)C.r[rAX] & fetch8(), 8); break;
    case 0xA9: f_logic(C.r[rAX] & fetch16(), 16); break;
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        sr8(op & 7, fetch8()); break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        C.r[op & 7] = fetch16(); break;
    case 0xC0: case 0xC1: decode(); a = ea_r(w); b = fetch8(); ea_w(w, f_shift(s_reg, a, b, bits)); break;
    case 0xC2: { const u16 n = fetch16(); C.ip = cpu_pop(); C.r[rSP] = (u16)(C.r[rSP] + n); break; }
    case 0xC3: C.ip = cpu_pop(); break;
    case 0xC4: case 0xC5:
        decode();
        if (s_ea_reg) { C.ip = start_ip; return false; }
        C.r[s_reg] = mrw(s_ea_seg, s_ea_off);
        C.s[op == 0xC4 ? sES : sDS] = mrw(s_ea_seg, (u16)(s_ea_off + 2));
        break;
    case 0xC6: case 0xC7: decode(); ea_w(w, w ? fetch16() : fetch8()); break;
    case 0xC8: {
        const u16 size = fetch16(); int level = fetch8() & 0x1F;
        cpu_push(C.r[rBP]);
        const u16 frame = C.r[rSP];
        if (level > 0) {
            for (int i = 1; i < level; i++) { C.r[rBP] = (u16)(C.r[rBP] - 2); cpu_push(mrw(C.s[sSS], C.r[rBP])); }
            cpu_push(frame);
        }
        C.r[rBP] = frame;
        C.r[rSP] = (u16)(C.r[rSP] - size);
        break; }
    case 0xC9: C.r[rSP] = C.r[rBP]; C.r[rBP] = cpu_pop(); break;
    case 0xCA: { const u16 n = fetch16(); C.ip = cpu_pop(); C.s[sCS] = cpu_pop(); C.r[rSP] = (u16)(C.r[rSP] + n); break; }
    case 0xCB: C.ip = cpu_pop(); C.s[sCS] = cpu_pop(); break;
    case 0xCC: cpu_interrupt(3); break;
    case 0xCD: { const u8 n = fetch8(); cpu_interrupt(n); break; }
    case 0xCE: if (FL & F_OF) cpu_interrupt(4); break;
    case 0xCF: C.ip = cpu_pop(); C.s[sCS] = cpu_pop(); FL = (u16)((cpu_pop() & 0x0FD5) | 0xF002); break;
    case 0xD0: case 0xD1: decode(); ea_w(w, f_shift(s_reg, ea_r(w), 1, bits)); break;
    case 0xD2: case 0xD3: decode(); ea_w(w, f_shift(s_reg, ea_r(w), (u8)C.r[rCX], bits)); break;
    case 0xD4: {
        const u8 base = fetch8();
        if (!base) { divide_error(); break; }
        const u8 al = (u8)C.r[rAX];
        C.r[rAX] = (u16)(((al / base) << 8) | (al % base));
        szp(C.r[rAX] & 0xFF, 8);
        break; }
    case 0xD5: {
        const u8 base = fetch8();
        const u8 al = (u8)((u8)C.r[rAX] + (u8)(C.r[rAX] >> 8) * base);
        C.r[rAX] = al;
        szp(al, 8);
        break; }
    case 0xD6: setlo(C.r[rAX], (FL & F_CF) ? 0xFF : 0); break;     // SALC
    case 0xD7: setlo(C.r[rAX], mrb(s_has_ovr ? s_ovr : C.s[sDS], (u16)(C.r[rBX] + (u8)C.r[rAX]))); break;
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        decode(); break;                                           // no 8087: ESC does nothing
    case 0xE0: C.r[rCX]--; jump_rel8(C.r[rCX] != 0 && !(FL & F_ZF)); break;
    case 0xE1: C.r[rCX]--; jump_rel8(C.r[rCX] != 0 && (FL & F_ZF)); break;
    case 0xE2: C.r[rCX]--; jump_rel8(C.r[rCX] != 0); break;
    case 0xE3: jump_rel8(C.r[rCX] == 0); break;
    case 0xE4: setlo(C.r[rAX], port_in(fetch8())); break;
    case 0xE5: { const u8 p = fetch8(); C.r[rAX] = (u16)(port_in(p) | (port_in((u16)(p + 1)) << 8)); break; }
    case 0xE6: port_out(fetch8(), (u8)C.r[rAX]); break;
    case 0xE7: { const u8 p = fetch8(); port_out(p, (u8)C.r[rAX]); port_out((u16)(p + 1), (u8)(C.r[rAX] >> 8)); break; }
    case 0xE8: { const u16 d = fetch16(); cpu_push(C.ip); C.ip = (u16)(C.ip + d); break; }
    case 0xE9: { const u16 d = fetch16(); C.ip = (u16)(C.ip + d); break; }
    case 0xEA: { const u16 ip = fetch16(), cs = fetch16(); C.s[sCS] = cs; C.ip = ip; break; }
    case 0xEB: jump_rel8(true); break;
    case 0xEC: setlo(C.r[rAX], port_in(C.r[rDX])); break;
    case 0xED: C.r[rAX] = (u16)(port_in(C.r[rDX]) | (port_in((u16)(C.r[rDX] + 1)) << 8)); break;
    case 0xEE: port_out(C.r[rDX], (u8)C.r[rAX]); break;
    case 0xEF: port_out(C.r[rDX], (u8)C.r[rAX]); port_out((u16)(C.r[rDX] + 1), (u8)(C.r[rAX] >> 8)); break;
    case 0xF4: plat_idle(1); g_poll_countdown = 0; break;          // HLT: wait for the next tick
    case 0xF5: FL ^= F_CF; break;
    case 0xF6: case 0xF7:
        decode(); a = ea_r(w);
        switch (s_reg) {
        case 0: case 1: f_logic(a & (w ? fetch16() : fetch8()), bits); break;
        case 2: ea_w(w, ~a); break;
        case 3: ea_w(w, f_sub(0, a, 0, bits)); break;
        case 4:
            if (w) { r = (u32)C.r[rAX] * a; C.r[rAX] = (u16)r; C.r[rDX] = (u16)(r >> 16); fset(F_CF, C.r[rDX] != 0); fset(F_OF, C.r[rDX] != 0); }
            else { r = (u32)(u8)C.r[rAX] * a; C.r[rAX] = (u16)r; fset(F_CF, (r >> 8) != 0); fset(F_OF, (r >> 8) != 0); }
            break;
        case 5:
            if (w) {
                const int32_t p = (int32_t)(int16_t)C.r[rAX] * (int16_t)a;
                C.r[rAX] = (u16)p; C.r[rDX] = (u16)((u32)p >> 16);
                fset(F_CF, p != (int16_t)p); fset(F_OF, p != (int16_t)p);
            } else {
                const int32_t p = (int32_t)(int8_t)C.r[rAX] * (int8_t)a;
                C.r[rAX] = (u16)p;
                fset(F_CF, p != (int8_t)p); fset(F_OF, p != (int8_t)p);
            }
            break;
        case 6:
            if (!a) { divide_error(); break; }
            if (w) {
                const u32 n = ((u32)C.r[rDX] << 16) | C.r[rAX], q = n / a;
                if (q > 0xFFFF) { divide_error(); break; }
                C.r[rAX] = (u16)q; C.r[rDX] = (u16)(n % a);
            } else {
                const u32 n = C.r[rAX], q = n / a;
                if (q > 0xFF) { divide_error(); break; }
                C.r[rAX] = (u16)(((n % a) << 8) | q);
            }
            break;
        default:
            if (!a) { divide_error(); break; }
            if (w) {
                const int64_t n = (int32_t)(((u32)C.r[rDX] << 16) | C.r[rAX]), d = (int16_t)a;
                const int64_t q = n / d, m = n % d;
                if (q > 32767 || q < -32768) { divide_error(); break; }
                C.r[rAX] = (u16)q; C.r[rDX] = (u16)m;
            } else {
                const int32_t n = (int16_t)C.r[rAX], d = (int8_t)a;
                const int32_t q = n / d, m = n % d;
                if (q > 127 || q < -128) { divide_error(); break; }
                C.r[rAX] = (u16)(((u32)(m & 0xFF) << 8) | (u32)(q & 0xFF));
            }
            break;
        }
        break;
    case 0xF8: FL &= (u16)~F_CF; break;
    case 0xF9: FL |= F_CF; break;
    case 0xFA: FL &= (u16)~F_IF; break;
    case 0xFB: FL |= F_IF; break;
    case 0xFC: FL &= (u16)~F_DF; break;
    case 0xFD: FL |= F_DF; break;
    case 0xFE:
        decode();
        if (s_reg == 0) ea_w8((u8)f_inc(ea_r8(), 8));
        else if (s_reg == 1) ea_w8((u8)f_dec(ea_r8(), 8));
        else { C.ip = start_ip; return false; }
        break;
    case 0xFF:
        decode();
        switch (s_reg) {
        case 0: ea_w16((u16)f_inc(ea_r16(), 16)); break;
        case 1: ea_w16((u16)f_dec(ea_r16(), 16)); break;
        case 2: a = ea_r16(); cpu_push(C.ip); C.ip = (u16)a; break;
        case 3:
            if (s_ea_reg) { C.ip = start_ip; return false; }
            a = mrw(s_ea_seg, s_ea_off); b = mrw(s_ea_seg, (u16)(s_ea_off + 2));
            cpu_push(C.s[sCS]); cpu_push(C.ip); C.ip = (u16)a; C.s[sCS] = (u16)b;
            break;
        case 4: C.ip = ea_r16(); break;
        case 5:
            if (s_ea_reg) { C.ip = start_ip; return false; }
            a = mrw(s_ea_seg, s_ea_off); b = mrw(s_ea_seg, (u16)(s_ea_off + 2));
            C.ip = (u16)a; C.s[sCS] = (u16)b;
            break;
        case 6: cpu_push(ea_r16()); break;
        default: C.ip = start_ip; return false;
        }
        break;
    default:       // 63-67, F1 and the rest: undefined on the 80186
        C.ip = start_ip;
        return false;
    }
    return true;
}

void cpu_run(void)
{
    while (!g_stop && !g_quit) {
        if (--g_poll_countdown <= 0) {
            g_poll_countdown = 1024;
            machine_poll();
            if (g_stop || g_quit) break;
        }
        if (!step()) illegal_instruction(C.s[sCS], C.ip, rb(lin(C.s[sCS], C.ip)));
    }
}
