"""r407: host tests of CharonDOS's sound (src/dos/sound.cpp): PC speaker, BEL, Sound Blaster.

Usage: python CharonDosSoundTests.py PATH/TO/charondos-host.exe [-v] [CASE-NAME-PART ...]

Every case runs a real-mode program (assembled here by hand, as in CharonDosTests.py) in the host
executable with --pcm FILE, which collects what CharonDOS would play as raw 48 kHz stereo 16 bit.
The cases measure that file: where the sound is, how long, and its frequency (zero crossings).
Time in the host build is GetTickCount (steps of about 16 ms), so lengths are checked with a
tolerance of a few steps; frequencies come from the generator and are checked closely.
The case of the tone that is never switched off takes 12 seconds."""
import array, shutil, struct, subprocess, sys, tempfile
from pathlib import Path

exe = Path(sys.argv[1]).resolve()
verbose = '-v' in sys.argv[2:]
only = [a for a in sys.argv[2:] if not a.startswith('-')]
RATE = 48000
TICK = 65536 / 1193180          # seconds per BIOS tick


# ---- the byte-level "assembler" of CharonDosTests.py ----
def L(name): return ('label', name)
def W(name): return ('abs16', name)
def J(name): return ('rel8', name)
def C(name): return ('rel16', name)


def flat(items):
    for i in items:
        if isinstance(i, list): yield from flat(i)
        else: yield i


def asm(items, org=0x100):
    items = list(flat(items))

    def size(i):
        if isinstance(i, tuple): return {'label': 0, 'abs16': 2, 'rel8': 1, 'rel16': 2}[i[0]]
        if isinstance(i, int): return 1
        return len(i)
    labels, pos = {}, org
    for i in items:
        if isinstance(i, tuple) and i[0] == 'label': labels[i[1]] = pos
        pos += size(i)
    out, pos = bytearray(), org
    for i in items:
        pos += size(i)
        if isinstance(i, tuple):
            if i[0] == 'abs16': out += struct.pack('<H', labels[i[1]])
            elif i[0] == 'rel8': out += struct.pack('<b', labels[i[1]] - pos)
            elif i[0] == 'rel16': out += struct.pack('<h', labels[i[1]] - pos)
        elif isinstance(i, int): out.append(i)
        elif isinstance(i, str): out += i.encode('ascii')
        else: out += i
    return bytes(out)


INT21 = b'\xCD\x21'
def out8(port, value): return [0xB0, value & 255, 0xE6, port]                       # mov al,v / out port,al
def outdx(port, value): return [0xBA, port & 255, port >> 8, 0xB0, value & 255, 0xEE]   # mov dx,port / mov al,v / out dx,al
def wait(ticks): return [0xB9, ticks & 255, ticks >> 8, 0xE8, C('ticks')]            # mov cx,n / call ticks
def leave(code=0): return [0xB8, code, 0x4C, INT21]
GATE_ON = [0xE4, 0x61, 0x0C, 0x03, 0xE6, 0x61]                                      # in al,61h / or al,3 / out 61h,al
GATE_OFF = [0xE4, 0x61, 0x24, 0xFC, 0xE6, 0x61]
def divisor(d): return [out8(0x42, d), out8(0x42, d >> 8)]
# waits CX timer ticks: the tick count at 40:6C
TICKS = [L('ticks'), 0x06, 0xB8, 0x40, 0x00, 0x8E, 0xC0, 0xFB,                      # push es / mov ax,40h / mov es,ax / sti
         L('t_next'), 0x26, 0x8B, 0x1E, 0x6C, 0x00,                                 # mov bx,es:[6Ch]
         L('t_same'), 0x26, 0x3B, 0x1E, 0x6C, 0x00, 0x74, J('t_same'),              # cmp bx,es:[6Ch] / je
         0xE2, J('t_next'), 0x07, 0xC3]                                             # loop / pop es / ret

# 1000 Hz for 9 ticks, silence for 4, 500 Hz (only the divisor rewritten) for 9 ticks.
SPEAKER = asm([out8(0x43, 0xB6), divisor(1193), wait(1), GATE_ON, wait(9), GATE_OFF, wait(4),
               divisor(2386), GATE_ON, wait(9), GATE_OFF, wait(3), leave(), TICKS])
# The gate is opened and never closed; the program goes on for 11.5 s.
STUCK = asm([out8(0x43, 0xB6), divisor(1193), GATE_ON, wait(210), leave(), TICKS])
# BEL through DOS (AH=02) and through the BIOS teletype (INT 10h AH=0Eh), 8 ticks apart.
BELL = asm([wait(1), 0xB4, 0x02, 0xB2, 0x07, INT21, wait(8), 0xB8, 0x07, 0x0E, 0xCD, 0x10, wait(8), leave(), TICKS])
# Channel 2 in mode 0 (a one-shot, no tone) and the speaker bit alone: nothing may sound.
NOTONE = asm([out8(0x43, 0xB0), divisor(1193), GATE_ON, wait(4), GATE_OFF,
              0xE4, 0x61, 0x0C, 0x02, 0xE6, 0x61, wait(4), GATE_OFF, leave(), TICKS])

SB_LEN = 2200                    # bytes of the DMA buffer: 50 periods of 44 samples
SB_TC = 211                      # time constant: 1000000 / (256 - 211) = 22222 Hz
SB_RATE = 1000000 // (256 - SB_TC)
import math
SB_WAVE = bytes(round(128 + 100 * math.sin(2 * math.pi * i / 44)) for i in range(SB_LEN))


def sb_program(auto):
    block = (SB_LEN // 2 if auto else SB_LEN) - 1
    return asm([
        outdx(0x226, 1), outdx(0x226, 0),
        0xBA, 0x2A, 0x02, 0xEC, 0x3C, 0xAA, 0x74, 0x03, 0xE9, C('fail'),             # in al,22Ah / cmp al,AAh
        outdx(0x22C, 0xE1), 0xBA, 0x2A, 0x02, 0xEC, 0x3C, 0x02, 0x74, 0x03, 0xE9, C('fail'),
        0xEC, 0x3C, 0x01, 0x74, 0x03, 0xE9, C('fail'),                               # version 2.01
        0xB8, 0x0D, 0x25, 0xBA, W('handler'), INT21,                                 # interrupt 0Dh (IRQ 5)
        0x8C, 0xD8, 0x89, 0xC3, 0xB1, 0x0C, 0xD3, 0xEB, 0xB1, 0x04, 0xD3, 0xE0,      # mov ax,ds / mov bx,ax / shr bx,12 / shl ax,4
        0x05, W('buffer'), 0x83, 0xD3, 0x00, 0x50,                                   # add ax,buffer / adc bx,0 / push ax
        out8(0x0A, 0x05), out8(0x0C, 0x00), out8(0x0B, 0x59 if auto else 0x49), 0x58,
        0xE6, 0x02, 0x88, 0xE0, 0xE6, 0x02, 0x88, 0xD8, 0xE6, 0x83,                  # address, page
        out8(0x03, SB_LEN - 1), out8(0x03, (SB_LEN - 1) >> 8), out8(0x0A, 0x01),
        outdx(0x22C, 0xD1), outdx(0x22C, 0x40), outdx(0x22C, SB_TC), wait(1),
        ([outdx(0x22C, 0x48), outdx(0x22C, block), outdx(0x22C, block >> 8), outdx(0x22C, 0x1C)] if auto else
         [outdx(0x22C, 0x14), outdx(0x22C, block), outdx(0x22C, block >> 8)]),
        wait(36 if auto else 6),
        outdx(0x22C, 0xD0), outdx(0x22C, 0xDA), wait(2),
        0xA0, W('irqs'), 0xB4, 0x4C, INT21,                                          # exit code: the interrupts
        L('fail'), leave(0xFF),
        L('handler'), 0x50, 0x52, 0xBA, 0x2E, 0x02, 0xEC, 0x2E, 0xFE, 0x06, W('irqs'),   # in al,22Eh / inc byte cs:[irqs]
        0xB0, 0x20, 0xE6, 0x20, 0x5A, 0x58, 0xCF,
        TICKS, L('irqs'), 0, L('buffer'), SB_WAVE,
    ])


def dac_program(writes_per_half):
    # Direct output: a square wave of `writes_per_half` writes of one level, for 9 ticks; the number of
    # half periods written goes to HALVES.BIN.
    return asm([
        outdx(0x226, 1), outdx(0x226, 0), outdx(0x22C, 0xD1), wait(1),
        0xB8, 0x40, 0x00, 0x8E, 0xC0, 0x26, 0xA1, 0x6C, 0x00, 0xA3, W('start'),      # start = es:[6Ch]
        0xB4, 0x40,                                                                  # mov ah,40h (the level)
        L('half'), 0xB9, writes_per_half & 255, writes_per_half >> 8,
        L('write'), 0xBA, 0x2C, 0x02, 0xB0, 0x10, 0xEE, 0x88, 0xE0, 0xEE, 0xE2, J('write'),
        0x80, 0xF4, 0x80,                                                            # xor ah,80h
        0x83, 0x06, W('halves'), 0x01, 0x83, 0x16, W('halves2'), 0x00,               # 32-bit count
        0x26, 0x8B, 0x36, 0x6C, 0x00, 0x2B, 0x36, W('start'), 0x83, 0xFE, 0x09, 0x72, J('half'),
        0xB4, 0x3C, 0x31, 0xC9, 0xBA, W('name'), INT21, 0x89, 0xC3,                  # create
        0xB4, 0x40, 0xB9, 0x04, 0x00, 0xBA, W('halves'), INT21, 0xB4, 0x3E, INT21,   # write, close
        wait(2), leave(), TICKS,
        L('start'), 0, 0, L('halves'), 0, 0, L('halves2'), 0, 0, L('name'), 'HALVES.BIN', 0,
    ])


PROGRAMS = {'SPEAKER.COM': SPEAKER, 'STUCK.COM': STUCK, 'BELL.COM': BELL, 'NOTONE.COM': NOTONE,
            'SBAUTO.COM': sb_program(True), 'SBONE.COM': sb_program(False)}


class Box:
    def __init__(self):
        self.top = Path(tempfile.mkdtemp(prefix='charondos-'))
        self.c = self.top / 'c'; self.c.mkdir()
        for name, data in PROGRAMS.items(): (self.c / name).write_bytes(data)

    def run(self, command, pcm=True):
        argv = [str(exe), '--root', f'C={self.c}', '-c', command] + (['--pcm', str(self.top / 'out.pcm')] if pcm else [])
        r = subprocess.run(argv, capture_output=True, timeout=40)
        return r.returncode, r.stdout.decode('utf-8', 'replace')

    def sounds(self):
        """[(start s, length s, frequency Hz, peak)] of the collected PCM; both channels must be equal."""
        raw = (self.top / 'out.pcm').read_bytes()
        s = array.array('h'); s.frombytes(raw[:len(raw) // 4 * 4])
        left, right = s[0::2], s[1::2]
        assert left == right, 'the channels differ'
        self.frames = len(left)
        gap = RATE // 50                                   # 20 ms of zero ends a sound
        found, start, last = [], None, None
        for i, v in enumerate(left):
            if v:
                if start is None: start = i
                last = i
            elif start is not None and i - last > gap:
                found.append((start, last + 1)); start = None
        if start is not None: found.append((start, last + 1))
        result = []
        for a, b in found:
            seg = left[a:b]
            crossings = sum(1 for i in range(1, len(seg)) if (seg[i - 1] < 0) != (seg[i] < 0))
            result.append((a / RATE, (b - a) / RATE, crossings / 2 / ((b - a) / RATE), max(abs(v) for v in seg)))
        if verbose: print('   ', self.frames / RATE, 's;', ['%.3f s +%.3f s %.1f Hz peak %d' % r for r in result])
        return result

    def close(self): shutil.rmtree(self.top, ignore_errors=True)


cases, failures = [], []
def case(f): cases.append(f); return f
def near(value, wanted, tolerance): return abs(value - wanted) <= tolerance
STEP = 0.04                      # two steps of the host clock


@case
def speaker_tone_follows_divisor_and_gate(b):
    code, out = b.run('SPEAKER')
    assert code == 0, (code, out)
    s = b.sounds()
    assert len(s) == 2, s
    assert near(s[0][2], 1193182 / 1193, 3) and near(s[0][1], 9 * TICK, STEP), s      # 1000.2 Hz, 0.494 s
    assert near(s[1][2], 1193182 / 2386, 3) and near(s[1][1], 9 * TICK, STEP), s      # 500.1 Hz
    assert near(s[1][0] - (s[0][0] + s[0][1]), 4 * TICK, STEP), s                     # the pause between them
    assert s[0][0] >= 0.079, s                                                         # the silent lead of a new sound
    assert all(x[3] == 5000 for x in s), s


@case
def speaker_tone_left_on_ends_by_itself(b):
    code, out = b.run('STUCK')
    assert code == 0, (code, out)
    s = b.sounds()
    assert len(s) == 1 and near(s[0][2], 1000.2, 2) and near(s[0][1], 10.0, 0.1), s   # the limit: 10 s of 11.5
    assert b.frames / RATE >= s[0][0] + s[0][1] + 0.29, (b.frames, s)                 # and the silent tail after it


@case
def bell_character_sounds(b):
    code, out = b.run('BELL')
    assert code == 0, (code, out)
    s = b.sounds()
    assert len(s) == 2, s
    for x in s: assert near(x[2], 1193182 / 1331, 5) and near(x[1], 0.2, STEP), s      # 896.5 Hz, 0.2 s
    assert near(s[1][0] - s[0][0], 8 * TICK, STEP), s


@case
def timer_mode_without_tone_and_speaker_bit_alone_are_silent(b):
    code, out = b.run('NOTONE')
    assert code == 0, (code, out)
    assert (b.top / 'out.pcm').stat().st_size == 0, 'something was played'


@case
def sound_blaster_auto_initialised_dma(b):
    code, out = b.run('SBAUTO')
    s = b.sounds()
    # 36 ticks = 1.978 s of 22222 Hz in blocks of 1100 bytes: 40 interrupts
    assert 37 <= code <= 43, (code, out)
    assert len(s) == 1 and near(s[0][2], SB_RATE / 44, 3) and near(s[0][1], 36 * TICK, STEP), s   # 505.0 Hz
    assert 7000 <= s[0][3] <= 19300, s                                                 # 100/128 of 3/4 of full scale


@case
def sound_blaster_single_cycle_dma(b):
    code, out = b.run('SBONE')
    s = b.sounds()
    assert code == 1, (code, out)                                                      # one block, one interrupt
    assert len(s) == 1 and near(s[0][2], SB_RATE / 44, 12) and near(s[0][1], SB_LEN / SB_RATE, 0.004), s   # 0.099 s


@case
def sound_blaster_direct_output(b):
    # The program writes as fast as CharonDOS runs it. A first run (no sound collected) counts the
    # half periods of 200 writes; the second gets the number of writes per half period that makes
    # about 400 Hz, and the collected sound must have the frequency its own count says.
    (b.c / 'DAC.COM').write_bytes(dac_program(200))
    code, out = b.run('DAC', pcm=False)
    assert code == 0, (code, out)
    first = struct.unpack('<I', (b.c / 'HALVES.BIN').read_bytes())[0]
    assert first > 20, first
    per_half = max(20, min(60000, round(200 * first / (2 * 9 * TICK * 400))))
    (b.c / 'DAC.COM').write_bytes(dac_program(per_half))
    code, out = b.run('DAC')
    assert code == 0, (code, out)
    halves = struct.unpack('<I', (b.c / 'HALVES.BIN').read_bytes())[0]
    s = b.sounds()
    assert len(s) == 1, s
    expected = halves / 2 / s[0][1]
    assert 150 < expected < 1500, (first, per_half, halves, s)
    assert near(s[0][2], expected, expected * 0.06) and near(s[0][1], 9 * TICK, 2 * STEP), (halves, expected, s)
    assert s[0][3] == 64 * 128, s


if only: cases = [f for f in cases if any(part in f.__name__ for part in only)]
for f in cases:
    box = Box()
    try:
        f(box)
        print('ok  ', f.__name__)
    except Exception as e:          # noqa: BLE001 - report every failing case
        failures.append(f.__name__)
        print('FAIL', f.__name__, '-', type(e).__name__, str(e)[:1500])
    finally:
        box.close()
print(f'{"PASS" if not failures else "FAIL"} {len(cases) - len(failures)}/{len(cases)} cases')
sys.exit(1 if failures else 0)
