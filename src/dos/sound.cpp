// SPDX-License-Identifier: GPL-2.0-or-later
// CharonDOS - sound (r407): the pseudo PC speaker and a small Sound Blaster.
//
// CharonDOS has no sound hardware of its own; what a program does with the
// classic ports is turned into 48 kHz stereo PCM and handed to the platform
// (plat_pcm_*: under UEFI Charon's PCM sink, where it is mixed under the
// speech; see src/efi/CharonPcmSink.h).
//
// PC speaker
//   43h  control word of PIT channel 2 (other channels are not emulated here)
//   42h  divisor of channel 2 (low byte, high byte, or both, as programmed)
//   61h  bit 0 = gate of channel 2, bit 1 = speaker data (bios.cpp keeps the
//        port and calls snd_gate)
//   BEL  character 07h written to the screen: 896 Hz for 0.2 s
//   A square wave of 1193182 / divisor Hz sounds while both bits of 61h are
//   set and channel 2 is in mode 2 or 3. One uninterrupted tone ends by
//   itself after `toneLimitMs`; it sounds again after the gate was closed or
//   the divisor written. The speaker bit toggled by hand (bit 1 without the
//   timer) is NOT played: CharonDOS has no clock fine enough to place the
//   edges.
//
// Sound Blaster 2.0 at 220h, IRQ 5, DMA 1 (BLASTER=A220 I5 D1 T3)
//   DSP: reset (226h), 10h direct output, 14h single-cycle and 1Ch
//   auto-initialised 8-bit DMA output, 40h time constant, 41h sampling rate,
//   48h block length, D0h/D4h pause/continue, D1h/D3h speaker on/off, DAh end
//   of auto-initialisation, E1h version (2.01), 22Eh acknowledges the
//   interrupt. DMA channel 1: ports 02h, 03h, 0Ah, 0Bh, 0Ch, 83h.
//   Not there: recording, ADPCM, 16-bit and stereo (SB Pro / SB16) commands,
//   the mixer chip (224h/225h), FM (388h) and MIDI.
//
// Time. The machine has a millisecond clock that the platform advances in
// steps (10 ms under UEFI). snd_poll is called whenever it changed and
// renders the frames of the time that passed. A DMA block therefore ends, and
// its interrupt comes, in real time. Direct output (10h) has no rate of its
// own: the samples a program wrote since the last poll are spread over the
// time that passed since then.
#include "dos.h"

namespace {

enum : u32 {
    outRate = 48000,
    leadFrames = 3840,          // silence before a new sound: 80 ms (the sink starts after 40 ms)
    tailMs = 300,               // silence written after the last sound
    toneLimitMs = 10000,        // the longest uninterrupted speaker tone
    bellMs = 200, bellDivisor = 1331,   // 896 Hz
    pollLimitMs = 200,          // time made up by one poll (the machine stood still longer: skipped)
    queueHigh = 6600,           // frames waiting in the output above this: this poll's frames are dropped
    speakerLevel = 5000, dacGain = 128,   // amplitudes of 32767
    irqVector = 0x0D,           // IRQ 5
};

// ---- output ------------------------------------------------------------------------------
bool s_live;                    // the output is open and being written
bool s_refused;                 // the platform has no output (asked again with the next sound)
u32 s_last_ms, s_quiet_ms;
bool s_clock;

// ---- PC speaker ---------------------------------------------------------------------------
u8 s_access = 3, s_mode = 3;    // channel 2: 1 low byte, 2 high byte, 3 both; counting mode
bool s_write_high, s_read_high;
u16 s_divisor, s_counter;       // 0 = 65536
bool s_loaded;                  // a divisor was written
u8 s_gate;                      // bits 0-1 of port 61h
u32 s_phase;                    // of the square wave, 32-bit fraction of a period
u32 s_tone_ms;                  // how long the present tone has sounded
bool s_tone_spent;              // it reached the limit
u32 s_bell_ms;                  // time left of a BEL

// ---- Sound Blaster ------------------------------------------------------------------------
u8 s_reset_latch, s_test;
u8 s_out[8]; unsigned s_out_n;                    // bytes the DSP has for the program
u8 s_command; u8 s_args[3]; unsigned s_need, s_have;
bool s_speaker_on = true;
u32 s_rate = 11025;
bool s_playing, s_paused, s_auto;
u32 s_block, s_left;                              // DSP block length - 1; bytes left in the block
bool s_irq_wait, s_irq_raised;                    // an interrupt to deliver; delivered, not yet acknowledged
u32 s_source_place;                               // resampler: 16.16 position between two source samples
int s_previous, s_current;
u8 s_dac[2048]; unsigned s_dac_n;                 // direct output since the last poll ...
unsigned s_dac_stride = 1, s_dac_skip;            // ... every s_dac_stride-th sample of it
// DMA channel 1
bool s_dma_flip, s_dma_masked = true;
u8 s_dma_mode, s_dma_page;
u16 s_dma_base, s_dma_base_count, s_dma_addr, s_dma_count;

u32 Divisor() { return s_divisor ? s_divisor : 65536u; }
bool ToneOn()
{
    if ((s_gate & 3) != 3 || !s_loaded || (s_mode & 2) == 0 || s_tone_spent) return false;
    const u32 d = Divisor();
    return d >= 60 && d <= 59659;                 // 20 Hz .. 19.9 kHz
}
void ToneChanged() { s_tone_ms = 0; s_tone_spent = false; }

void DspReset()
{
    s_out_n = 0; s_need = s_have = 0; s_command = 0;
    s_playing = s_paused = s_auto = false;
    s_irq_wait = s_irq_raised = false;
    s_dac_n = 0; s_dac_stride = 1; s_dac_skip = 0;
    s_speaker_on = true;
}
void DspOut(u8 value) { if (s_out_n < sizeof s_out) s_out[s_out_n++] = value; }
unsigned ArgsOf(u8 command)
{
    switch (command) {
    case 0x10: case 0x40: case 0xE0: case 0xE4: return 1;
    case 0x14: case 0x41: case 0x48: case 0x80: return 2;
    default: return 0;
    }
}
void StartDma(bool autoInit, u32 lengthMinusOne)
{
    s_auto = autoInit;
    s_block = lengthMinusOne;
    s_left = lengthMinusOne + 1;
    s_playing = true; s_paused = false;
    s_source_place = 0x10000; s_previous = s_current = 0;
}
void DspCommand()
{
    switch (s_command) {
    case 0x10:
        // More samples than fit (a program that writes as fast as it can): every second one is
        // dropped and from now on only every 2nd, 4th ... is kept, so the buffer still spans the interval.
        if (s_dac_skip++ % s_dac_stride) break;
        if (s_dac_n == sizeof s_dac) {
            for (unsigned i = 0; i < sizeof s_dac / 2; i++) s_dac[i] = s_dac[2 * i];
            s_dac_n = sizeof s_dac / 2; s_dac_stride *= 2; s_dac_skip = 1;
            break;
        }
        s_dac[s_dac_n++] = s_args[0];
        break;
    case 0x14: StartDma(false, (u32)(s_args[0] | s_args[1] << 8)); break;
    case 0x1C: StartDma(true, s_block); break;
    case 0x40: s_rate = 1000000u / (256u - s_args[0]); break;
    case 0x41: s_rate = (u32)(s_args[0] << 8 | s_args[1]); break;
    case 0x48: s_block = (u32)(s_args[0] | s_args[1] << 8); break;
    case 0x80: break;                                         // a pause of silence: not timed
    case 0xD0: s_paused = true; break;
    case 0xD4: s_paused = false; break;
    case 0xD1: s_speaker_on = true; break;
    case 0xD3: s_speaker_on = false; break;
    case 0xD8: DspOut(s_speaker_on ? 0xFF : 0x00); break;
    case 0xDA: s_auto = false; break;                          // the present block is the last
    case 0xE0: DspOut((u8)~s_args[0]); break;
    case 0xE1: DspOut(2); DspOut(1); break;
    case 0xE4: s_test = s_args[0]; break;
    case 0xE8: DspOut(s_test); break;
    case 0xF2: s_irq_wait = true; break;                       // the interrupt, at once (IRQ detection)
    default: break;                                            // unknown: ignored
    }
    if (s_rate < 4000) s_rate = 4000;
    if (s_rate > 48000) s_rate = 48000;
}

// The next source sample of the DMA transfer; false: nothing more (stopped, masked or paused).
bool DmaSample(int* value)
{
    if (!s_playing || s_paused || s_dma_masked) return false;
    *value = ((int)rb(((u32)s_dma_page << 16) | s_dma_addr) - 128) * 256;
    s_dma_addr = (u16)(s_dma_addr + 1);
    if (s_dma_count-- == 0) {                                  // terminal count
        if (s_dma_mode & 0x10) { s_dma_addr = s_dma_base; s_dma_count = s_dma_base_count; }
        else s_dma_masked = true;
    }
    if (--s_left == 0) {
        s_irq_wait = true;
        if (s_auto) s_left = s_block + 1; else s_playing = false;
    }
    return true;
}

// `frames` of everything that sounds, added into mono[]: the frames from `offset` of the `total`
// frames this poll renders (`ms` is their share of its time). True: something sounded.
bool Render(int* mono, u32 frames, u32 ms, u32 offset, u32 total)
{
    bool sound = false;
    // the speaker: the program's tone, or a BEL
    u32 divisor = 0;
    if (ToneOn()) {
        divisor = Divisor();
        s_tone_ms += ms;
        if (s_tone_ms >= toneLimitMs) s_tone_spent = true;
    } else if (s_bell_ms) divisor = bellDivisor;
    if (s_bell_ms) s_bell_ms = s_bell_ms > ms ? s_bell_ms - ms : 0;
    if (divisor) {
        // 1193182 Hz / divisor as a 32-bit fraction of a period per frame
        const u32 step = (u32)((((u64)1193182 << 32) / outRate) / divisor);
        for (u32 i = 0; i < frames; i++) {
            mono[i] += (s_phase & 0x80000000u) ? -(int)speakerLevel : (int)speakerLevel;
            s_phase += step;
        }
        sound = true;
    }
    // the DSP: DMA output, linear interpolation from s_rate to 48 kHz
    if (s_playing && !s_paused && !s_dma_masked) {
        const u32 step = (u32)(((u64)s_rate << 16) / outRate);
        for (u32 i = 0; i < frames; i++) {
            while (s_source_place >= 0x10000) {
                int next;
                if (!DmaSample(&next)) { next = 0; s_source_place &= 0xFFFF; s_previous = s_current; s_current = next; break; }
                s_source_place -= 0x10000;
                s_previous = s_current; s_current = next;
            }
            if (s_speaker_on)
                mono[i] += ((s_previous + (int)(((int64_t)(s_current - s_previous) * (int)s_source_place) >> 16)) * 3) >> 2;
            s_source_place += step;
        }
        sound = true;
    }
    // the DSP: direct output, the samples of this interval spread over all frames of the poll
    if (s_dac_n) {
        if (s_speaker_on)
            for (u32 i = 0; i < frames; i++)
                mono[i] += ((int)s_dac[(u64)(offset + i) * s_dac_n / total] - 128) * (int)dacGain;
        sound = true;
    }
    return sound;
}

}  // namespace

bool snd_owns_port(u16 port)
{
    return port == 0x42 || port == 0x43 || (port >= 0x220 && port <= 0x22F)
        || port == 0x02 || port == 0x03 || port == 0x0A || port == 0x0B || port == 0x0C || port == 0x83;
}

u8 snd_port_in(u16 port)
{
    switch (port) {
    case 0x42: {
        // No real counter: every read finds it a little further down.
        if (s_access == 3 && s_read_high) { s_read_high = false; return (u8)(s_counter >> 8); }
        const u32 d = Divisor();
        s_counter = (u16)((s_counter >= 37 ? (u32)s_counter - 37 : d - 1) % d);
        if (s_access == 2) return (u8)(s_counter >> 8);
        if (s_access == 3) s_read_high = true;
        return (u8)s_counter;
    }
    case 0x02: s_dma_flip = !s_dma_flip; return s_dma_flip ? (u8)s_dma_addr : (u8)(s_dma_addr >> 8);
    case 0x03: s_dma_flip = !s_dma_flip; return s_dma_flip ? (u8)s_dma_count : (u8)(s_dma_count >> 8);
    case 0x83: return s_dma_page;
    case 0x22A: {
        if (!s_out_n) return 0xFF;
        const u8 value = s_out[0];
        for (unsigned i = 1; i < s_out_n; i++) s_out[i - 1] = s_out[i];
        s_out_n--;
        return value;
    }
    case 0x22C: return 0x7F;                                   // ready for a command
    case 0x22E: s_irq_raised = false; return s_out_n ? 0xFF : 0x7F;   // data waiting; acknowledges the interrupt
    default: return 0xFF;
    }
}

void snd_port_out(u16 port, u8 value)
{
    switch (port) {
    case 0x43:
        if ((value >> 6) != 2) break;                          // another channel
        if (((value >> 4) & 3) == 0) { s_read_high = false; break; }   // latch command
        s_access = (u8)((value >> 4) & 3);
        s_mode = (u8)((value >> 1) & 7);
        s_write_high = s_read_high = false;
        s_loaded = false;
        ToneChanged();
        break;
    case 0x42:
        if (s_access == 1) s_divisor = (u16)((s_divisor & 0xFF00) | value);
        else if (s_access == 2) s_divisor = (u16)((s_divisor & 0x00FF) | (value << 8));
        else if (!s_write_high) { s_divisor = (u16)((s_divisor & 0xFF00) | value); s_write_high = true; break; }
        else { s_divisor = (u16)((s_divisor & 0x00FF) | (value << 8)); s_write_high = false; }
        s_loaded = true;
        s_counter = s_divisor;
        ToneChanged();
        break;
    case 0x02: case 0x03: {
        u16& base = port == 0x02 ? s_dma_base : s_dma_base_count;
        s_dma_flip = !s_dma_flip;
        base = s_dma_flip ? (u16)((base & 0xFF00) | value) : (u16)((base & 0x00FF) | (value << 8));
        s_dma_addr = s_dma_base; s_dma_count = s_dma_base_count;
        break;
    }
    case 0x0A: if ((value & 3) == 1) s_dma_masked = (value & 4) != 0; break;
    case 0x0B: if ((value & 3) == 1) s_dma_mode = value; break;
    case 0x0C: s_dma_flip = false; break;
    case 0x83: s_dma_page = (u8)(value & 0x0F); break;
    case 0x226:
        if (value & 1) s_reset_latch = 1;
        else if (s_reset_latch == 1) { s_reset_latch = 0; DspReset(); DspOut(0xAA); }
        break;
    case 0x22C:
        if (!s_need && !s_have) {
            s_command = value;
            s_need = ArgsOf(value);
            if (!s_need) DspCommand();
        } else {
            s_args[s_have++] = value;
            if (s_have == s_need) { s_need = s_have = 0; DspCommand(); }
        }
        break;
    default: break;
    }
}

void snd_gate(u8 port61)
{
    const u8 gate = (u8)(port61 & 3);
    if ((gate & 3) != 3 && (s_gate & 3) == 3) ToneChanged();   // closed: the next tone starts anew
    s_gate = gate;
}

void snd_bell(void) { s_bell_ms = bellMs; }

// The vector of an interrupt to deliver now (the program has a handler and
// acknowledged the one before), or 0.
u8 snd_take_irq(void)
{
    if (!s_irq_wait || s_irq_raised) return 0;
    s_irq_wait = false;
    if (rw(irqVector * 4) == stub_off(irqVector) && rw(irqVector * 4 + 2) == g_stub_seg) return 0;   // nobody listens
    s_irq_raised = true;
    return irqVector;
}

void snd_poll(u32 now)
{
    if (!s_clock) { s_clock = true; s_last_ms = now; return; }
    u32 ms = now - s_last_ms;
    if (!ms) return;
    s_last_ms = now;
    if (ms > pollLimitMs) ms = pollLimitMs;
    const bool active = ToneOn() || s_bell_ms || s_dac_n || (s_playing && !s_paused && !s_dma_masked);
    if (!s_live) {
        if (!active) return;
        // A new sound. Without an output everything still runs in time (DMA blocks end, interrupts come).
        s_refused = !plat_pcm_begin();
        s_live = true; s_quiet_ms = 0;
        if (!s_refused) {
            static const int16_t silence[512 * 2] = {};
            for (u32 left = leadFrames; left; ) {
                const u32 n = left < 512 ? left : 512;
                if (plat_pcm_write(silence, n) != n) break;
                left -= n;
            }
        }
    }
    static int mono[512];
    static int16_t stereo[512 * 2];
    const bool drop = s_refused || plat_pcm_queued() > queueHigh;
    bool sound = false;
    const u32 total = ms * (outRate / 1000);
    for (u32 left = total; left; ) {
        const u32 n = left < 512 ? left : 512;
        // the share of the time these frames stand for (the limits count in milliseconds)
        const u32 share = (n + outRate / 2000) / (outRate / 1000);
        d_memset(mono, 0, sizeof mono);
        if (Render(mono, n, share, total - left, total)) sound = true;
        if (!drop) {
            for (u32 i = 0; i < n; i++) {
                const int v = mono[i] > 32767 ? 32767 : mono[i] < -32768 ? -32768 : mono[i];
                stereo[2 * i] = stereo[2 * i + 1] = (int16_t)v;
            }
            plat_pcm_write(stereo, n);
        }
        left -= n;
    }
    s_dac_n = 0; s_dac_stride = 1; s_dac_skip = 0;   // the direct output of this interval was played
    if (sound) s_quiet_ms = 0;
    else if ((s_quiet_ms += ms) >= tailMs) {
        s_live = false;
        if (!s_refused) plat_pcm_end();
    }
}
