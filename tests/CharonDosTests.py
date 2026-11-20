"""r400: host tests of CharonDOS (src/dos).

Usage: python CharonDosTests.py PATH/TO/charondos-host.exe [-v] [CASE-NAME-PART ...]

Drives the host executable built by tools/build-charondos.py. Every case gets
a fresh temporary directory as drive C: (and a second one as D:), so effects
on files are checked on the real host file system, not only in the output.
The real-mode programs are assembled here by hand (bytes with label fix-ups);
no assembler is needed."""
import shutil, struct, subprocess, sys, tempfile
from pathlib import Path

exe = Path(sys.argv[1]).resolve()
verbose = '-v' in sys.argv[2:]
only = [a for a in sys.argv[2:] if not a.startswith('-')]


# ---- a byte-level "assembler": bytes, ints, strings and label references ----
def L(name): return ('label', name)
def W(name): return ('abs16', name)        # absolute offset of a label
def J(name): return ('rel8', name)         # displacement of a short jump
def C(name): return ('rel16', name)        # displacement of a near call / jump


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

# AH=09 string, exit code 7 through AH=4C.
HELLO = asm([
    0xB4, 0x09,                     # mov ah,9
    0xBA, W('text'),                # mov dx,text
    INT21,
    0xB8, 0x07, 0x4C,               # mov ax,4C07h
    INT21,
    L('text'), 'Hello from COM\r\n$',
])

# Create, write, close, open, read, close, print what was read.
FILEIO = asm([
    0xB4, 0x3C, 0x31, 0xC9, 0xBA, W('name'), INT21, 0x72, J('fail'),   # create
    0x89, 0xC3,                                                        # mov bx,ax
    0xB4, 0x40, 0xB9, 0x07, 0x00, 0xBA, W('data'), INT21, 0x72, J('fail'),
    0x3D, 0x07, 0x00, 0x75, J('fail'),                                 # cmp ax,7 / jne
    0xB4, 0x3E, INT21, 0x72, J('fail'),
    0xB8, 0x00, 0x3D, 0xBA, W('name'), INT21, 0x72, J('fail'),         # open read-only
    0x89, 0xC3,
    0xB4, 0x3F, 0xB9, 0x20, 0x00, 0xBA, W('buf'), INT21, 0x72, J('fail'),
    0x89, 0xC6,                                                        # mov si,ax
    0xB4, 0x3E, INT21,
    0x89, 0xF1, 0xBB, 0x01, 0x00, 0xBA, W('buf'), 0xB4, 0x40, INT21,   # write to stdout
    0xB8, 0x00, 0x4C, INT21,
    L('fail'), 0xB8, 0x01, 0x4C, INT21,
    L('name'), 'OUT.TXT\0',
    L('data'), 'DATA123',
    L('buf'), bytes(0x20),
])

# Seek / append: open OUT.TXT read-write, seek to the end, write "+END",
# seek to offset 4, read 3 bytes, exit with the file length from the seek.
SEEK = asm([
    0xB8, 0x02, 0x3D, 0xBA, W('name'), INT21, 0x72, J('fail'),
    0x89, 0xC3,
    0xB8, 0x02, 0x42, 0x31, 0xC9, 0x31, 0xD2, INT21, 0x72, J('fail'),  # lseek end -> DX:AX
    0x89, 0xC7,                                                        # mov di,ax
    0xB4, 0x40, 0xB9, 0x04, 0x00, 0xBA, W('tail'), INT21, 0x72, J('fail'),
    0xB8, 0x00, 0x42, 0x31, 0xC9, 0xBA, 0x04, 0x00, INT21, 0x72, J('fail'),
    0xB4, 0x3F, 0xB9, 0x03, 0x00, 0xBA, W('buf'), INT21, 0x72, J('fail'),
    0xB4, 0x3E, INT21,
    0xC6, 0x06, W('buf3'), ord('$'),                                   # mov byte [buf+3],'$'
    0xB4, 0x09, 0xBA, W('buf'), INT21,
    0x89, 0xF8, 0xB4, 0x4C, INT21,                                     # mov ax,di / mov ah,4C
    L('fail'), 0xB8, 0x63, 0x4C, INT21,
    L('name'), 'OUT.TXT\0',
    L('tail'), '+END',
    L('buf'), bytes(3), L('buf3'), 0,
])

# DIV / PUSH / POP / LOOP / REP MOVSB / CALL / RET / SHL / ADC / XCHG / INT 20h.
CPU = asm([
    0xB8, 0x39, 0x30,               # mov ax,12345
    0xBB, 0x0A, 0x00,               # mov bx,10
    0x31, 0xC9,                     # xor cx,cx
    L('digit'), 0x31, 0xD2,         # xor dx,dx
    0xF7, 0xF3,                     # div bx
    0x52, 0x41,                     # push dx / inc cx
    0x85, 0xC0, 0x75, J('digit'),   # test ax,ax / jnz
    L('print'), 0x5A,               # pop dx
    0x80, 0xC2, 0x30,               # add dl,'0'
    0xB4, 0x02, INT21,
    0xE2, J('print'),               # loop
    0xBE, W('src'), 0xBF, W('dst'), 0xB9, 0x05, 0x00, 0xFC, 0xF3, 0xA4,   # rep movsb
    0xBA, W('dst'), 0xB4, 0x09, INT21,
    0xE8, C('sub'),                 # call sub -> AL = 'A' + 2*3 + carry(1) = 'H'
    0x88, 0xC2, 0xB4, 0x02, INT21,
    0xB8, 0x34, 0x12, 0xBB, 0x78, 0x56, 0x93,       # mov ax,1234h / mov bx,5678h / xchg ax,bx
    0x3D, 0x78, 0x56, 0x75, J('bad'),               # cmp ax,5678h
    0x81, 0xFB, 0x34, 0x12, 0x75, J('bad'),         # cmp bx,1234h
    0xB8, 0xFF, 0xFF, 0xB9, 0x02, 0x00, 0xF7, 0xE1, # mov ax,FFFFh / mov cx,2 / mul cx -> DX:AX=1:FFFE
    0x83, 0xFA, 0x01, 0x75, J('bad'),               # cmp dx,1
    0x3D, 0xFE, 0xFF, 0x75, J('bad'),
    0xB0, 0xF6, 0xB3, 0x05, 0xF6, 0xEB,             # mov al,-10 / mov bl,5 / imul bl -> AX=-50
    0x3D, 0xCE, 0xFF, 0x75, J('bad'),
    0xB8, 0x9C, 0xFF, 0xB3, 0x07, 0xF6, 0xFB,       # mov ax,-100 / mov bl,7 / idiv bl -> AL=-14 AH=-2
    0x3D, 0xF2, 0xFE, 0x75, J('bad'),
    0xB0, 0x81, 0xD0, 0xF8,                         # mov al,81h / sar al,1 -> C0h, CF=1
    0x73, J('bad'), 0x3C, 0xC0, 0x75, J('bad'),
    0xB0, 0x09, 0x04, 0x08, 0x27,                   # mov al,9 / add al,8 / daa -> 17h
    0x3C, 0x17, 0x75, J('bad'),
    0x9C, 0x58, 0x50, 0x9D,                         # pushf / pop ax / push ax / popf
    0xBA, W('ok'), 0xB4, 0x09, INT21,
    0xCD, 0x20,                     # int 20h
    L('bad'), 0xB8, 0x09, 0x4C, INT21,
    L('sub'), 0xB0, 0x03,           # mov al,3
    0xD0, 0xE0,                     # shl al,1
    0xF9, 0x14, 0x41,               # stc / adc al,'A'
    0xC3,
    L('src'), ' abc ',
    L('dst'), '?????$',
    L('ok'), ' OK$',
])

# A wider instruction check. Every stage stores its number first; a failed
# check ends the program with that number as the exit code, 0 means all passed.
JE, JNE, JC, JNC, JS, JNS, JP, JO, JL, JGE, JA, JBE = 0x74, 0x75, 0x72, 0x73, 0x78, 0x79, 0x7A, 0x70, 0x7C, 0x7D, 0x77, 0x76
def stage(n): return [0xC6, 0x06, W('stage'), n]                 # mov byte [stage],n
def need(cc): return [cc, 0x03, 0xE9, C('bad')]                  # jcc over "jmp near bad"
def ax_is(v): return [0x3D, v & 255, v >> 8, need(JE)]           # cmp ax,v
def al_is(v): return [0x3C, v, need(JE)]                         # cmp al,v

CPU2 = asm([
    stage(1), 0xFC,                                              # cld
    0xBE, W('s1'), 0xBF, W('s2'), 0xB9, 0x04, 0x00, 0xF3, 0xA6, need(JE),      # repe cmpsb, equal
    0x09, 0xC9, need(JE),                                        # or cx,cx
    0xBE, W('s1'), 0xBF, W('s3'), 0xB9, 0x04, 0x00, 0xF3, 0xA6, need(JNE), need(JA),
    0x83, 0xF9, 0x01, need(JE),                                  # cmp cx,1
    stage(2), 0xBF, W('s1'), 0xB0, ord('c'), 0xB9, 0x0A, 0x00, 0xF2, 0xAE, need(JE),   # repne scasb
    0x81, 0xFF, W('s1end3'), need(JE),                           # cmp di,s1+3
    stage(3), 0xBE, W('s1'), 0xAD, ax_is(0x6261),                # lodsw
    0xBF, W('scratch'), 0xB8, 0x58, 0x59, 0xB9, 0x02, 0x00, 0xF3, 0xAB,        # rep stosw 'XY'
    0xA1, W('scratch2'), ax_is(0x5958),                          # mov ax,[scratch+2]
    0xFD, 0xBE, W('scratch3'), 0xAC, 0xAC, 0xFC, al_is(0x58),    # std / lodsb x2 / cld -> 'X'
    stage(4), 0xBB, W('s1'), 0xB0, 0x02, 0xD7, al_is(ord('c')),  # xlat
    stage(5), 0xB8, 0x11, 0x11, 0xB9, 0x22, 0x22, 0x60, 0x31, 0xC0, 0x31, 0xC9, 0x61,  # pusha / popa
    ax_is(0x1111), 0x81, 0xF9, 0x22, 0x22, need(JE),
    stage(6), 0xB2, 0x00, 0x0E, 0xE8, C('farsub'), 0x80, 0xFA, 0x05, need(JE),          # push cs / call -> retf
    0x8C, 0x0E, W('farseg'), 0xB2, 0x00, 0xFF, 0x1E, W('farptr'), 0x80, 0xFA, 0x05, need(JE),   # call far [farptr]
    stage(7), 0xB0, 0x81, 0xD0, 0xC0, need(JC), al_is(0x03),     # rol al,1
    0xB0, 0x01, 0xF9, 0xD0, 0xD8, need(JC), al_is(0x80),         # stc / rcr al,1
    0xB8, 0x34, 0x12, 0xB1, 0x04, 0xD3, 0xC8, ax_is(0x4123),     # ror ax,cl
    0xC1, 0xE0, 0x04, ax_is(0x1230),                             # shl ax,4 (80186)
    0xBB, 0x00, 0x80, 0xB1, 0x0F, 0xD3, 0xEB, 0x83, 0xFB, 0x01, need(JE),      # shr bx,cl
    0xB0, 0x40, 0xF8, 0xD0, 0xD0, need(JNC), al_is(0x80),        # clc / rcl al,1
    0xB8, 0x00, 0x80, 0xC1, 0xF8, 0x03, ax_is(0xF000),           # sar ax,3
    stage(8), 0xB8, 0x05, 0x00, 0xF7, 0xD8, need(JC), 0xF7, 0xD0, ax_is(0x0004),        # neg / not
    0xB0, 0x80, 0x98, 0x99, ax_is(0xFF80), 0x83, 0xFA, 0xFF, need(JE),         # cbw / cwd
    stage(9), 0xB8, 0xFB, 0xFF, 0x3D, 0x03, 0x00, need(JL), need(JA),          # -5 < 3 signed, above unsigned
    0xB0, 0x7F, 0xFE, 0xC0, need(JO), need(JS),                  # inc al: overflow, sign
    0xB0, 0x03, 0x2C, 0x03, need(JE), need(JP), need(JNC),       # sub al,3: zero, parity even, no carry
    stage(10), 0xBB, 0xFD, 0xFF, 0x6B, 0xC3, 0x07, ax_is(0xFFEB),               # imul ax,bx,7
    0x69, 0xCB, 0xE8, 0x03, 0x81, 0xF9, 0x48, 0xF4, need(JE),    # imul cx,bx,1000
    stage(11), 0xBB, 0x00, 0x01, 0xBE, 0x20, 0x00, 0x8D, 0x40, 0x05, ax_is(0x0125),     # lea ax,[bx+si+5]
    0x89, 0xE5, 0x68, 0xEF, 0xBE, 0x8B, 0x46, 0xFE, ax_is(0xBEEF), 0x58,       # mov bp,sp / push / mov ax,[bp-2]
    0xBB, W('s1'), 0xBF, 0x02, 0x00, 0x8A, 0x41, 0x01, al_is(ord('d')),        # mov al,[bx+di+1]
    stage(12), 0xC4, 0x1E, W('lesptr'), 0x8C, 0xC0, ax_is(0x5678), 0x81, 0xFB, 0x34, 0x12, need(JE),
    0x1E, 0x07,                                                  # push ds / pop es
    stage(13), 0x8C, 0xD8, 0x05, 0x10, 0x00, 0x8E, 0xC0, 0x26, 0xA0, 0x00, 0x00, al_is(0xC6),   # es:[0] = first code byte
    0x26, 0x8A, 0x26, 0x01, 0x00, 0x80, 0xFC, 0x06, need(JE),    # mov ah,es:[1]
    0x1E, 0x07,
    stage(14), 0xB8, 0x61, 0x25, 0xBA, W('handler'), INT21, 0x31, 0xDB, 0xF9, 0xCD, 0x61, need(JC),
    0x81, 0xFB, 0x21, 0x43, need(JE),
    0xB8, 0x61, 0x35, INT21, 0x81, 0xFB, W('handler'), need(JE), 0x1E, 0x07,
    stage(15), 0xBD, 0x77, 0x77, 0x89, 0xE3, 0xC8, 0x04, 0x00, 0x00, 0xC9,     # enter 4,0 / leave
    0x81, 0xFD, 0x77, 0x77, need(JE), 0x39, 0xE3, need(JE),      # cmp bp,7777h / cmp bx,sp
    stage(16), 0xB4, 0xD5, 0x9E, need(JC), need(JE), need(JS), need(JP),       # sahf
    0x30, 0xE4, 0xB4, 0x00, 0xF9, 0x9F, 0xF6, 0xC4, 0x01, need(JNE),           # stc / lahf / test ah,1
    stage(17), 0xB8, 0x00, 0x00, 0x2D, 0x01, 0x00, 0xBA, 0x05, 0x00, 0x83, 0xDA, 0x00,  # sub ax,1 / sbb dx,0
    0x83, 0xFA, 0x04, need(JE), 0x05, 0x01, 0x00, 0x83, 0xD2, 0x00, 0x83, 0xFA, 0x05, need(JE),
    stage(18), 0xB0, 0x2F, 0xD4, 0x0A, ax_is(0x0407), 0xD5, 0x0A, ax_is(0x002F),        # aam / aad
    stage(19), 0xB9, 0x03, 0x00, 0x31, 0xC0, L('ln'), 0x40, 0x3D, 0x02, 0x00, 0xE0, J('ln'),
    0x83, 0xF9, 0x01, need(JE),
    stage(20), 0xB0, 0xF0, 0x24, 0x3C, 0x0C, 0x03, 0x34, 0xFF, 0xA8, 0x0C, need(JNE), al_is(0xCC),
    stage(21), 0xFF, 0x06, W('var'), 0xFF, 0x36, W('var'), 0x8F, 0x06, W('var2'),
    0x81, 0x3E, W('var2'), 0x00, 0x01, need(JE),
    0xFE, 0x0E, W('var'), 0x80, 0x3E, W('var'), 0xFF, need(JE),  # dec byte [var] -> FFh
    stage(22), 0xBB, W('target'), 0xFF, 0xE3, 0xE9, C('bad'), L('target'),
    0xB2, 0x00, 0xFF, 0x16, W('vec'), 0x80, 0xFA, 0x09, need(JE),
    stage(23), 0xB0, 0xC8, 0xB3, 0x03, 0xF6, 0xE3, need(JC), ax_is(0x0258),    # mul bl
    0xB3, 0x07, 0xF6, 0xF3, ax_is(0x0555),                       # div bl
    stage(24), 0xBA, 0xFF, 0xFF, 0xB8, 0x18, 0xFC, 0xBB, 0x07, 0x00, 0xF7, 0xFB, ax_is(0xFF72),
    0x83, 0xFA, 0xFA, need(JE),
    stage(25), 0xCD, 0x12, ax_is(640),                           # int 12h
    0xB4, 0x2A, INT21, 0x81, 0xF9, 0xE4, 0x07, need(JNC),        # year >= 2020
    0xB4, 0x00, 0xCD, 0x1A, 0x89, 0xD0, 0x09, 0xC8, need(JNE),   # tick count is not 0
    stage(26), 0xB8, 0x34, 0x12, 0x86, 0xC4, ax_is(0x3412),      # xchg al,ah
    0x87, 0x06, W('var2'), ax_is(0x0100), 0x81, 0x3E, W('var2'), 0x12, 0x34, need(JE),
    0xB8, 0x00, 0x4C, INT21,
    L('bad'), 0xA0, W('stage'), 0xB4, 0x4C, INT21,
    L('farsub'), 0xB2, 0x05, 0xCB,
    L('nearsub'), 0xB2, 0x09, 0xC3,
    L('handler'), 0xBB, 0x21, 0x43, 0xCF,
    L('stage'), 0,
    L('s1'), 'abc', L('s1end3'), 'd', L('s2'), 'abcd', L('s3'), 'abXd',
    L('scratch'), 0, 0, L('scratch2'), 0, L('scratch3'), 0,
    L('farptr'), W('farsub'), L('farseg'), 0, 0,
    L('lesptr'), 0x34, 0x12, 0x78, 0x56,
    L('var'), 0xFF, 0x00, L('var2'), 0, 0,
    L('vec'), W('nearsub'),
])

# Prints its command tail (PSP:80h) between brackets.
ARGS = asm([
    0xB2, ord('['), 0xB4, 0x02, INT21,
    0x8A, 0x0E, 0x80, 0x00,         # mov cl,[80h]
    0x30, 0xED,                     # xor ch,ch
    0xBE, 0x81, 0x00,               # mov si,81h
    0xE3, J('done'),                # jcxz
    L('next'), 0xAC, 0x88, 0xC2, 0xB4, 0x02, INT21, 0xE2, J('next'),
    L('done'), 0xB2, ord(']'), 0xB4, 0x02, INT21,
    0xB8, 0x00, 0x4C, INT21,
])

# Reads one key without echo (AH=08) and exits with it as the exit code.
KEY = asm([0xB4, 0x08, INT21, 0xB4, 0x4C, INT21])

# Reads a line (AH=0A) and prints it back in upper case through AH=40.
LINE = asm([
    0xBA, W('buf'), 0xB4, 0x0A, INT21,
    0xB2, 0x0A, 0xB4, 0x02, INT21,                  # line feed
    0x8A, 0x0E, W('len'), 0x30, 0xED,               # mov cl,[len] / xor ch,ch
    0xE3, J('done'),
    0xBE, W('text'),
    L('up'), 0x8A, 0x04,                            # mov al,[si]
    0x3C, ord('a'), 0x72, J('keep'), 0x3C, ord('z'), 0x77, J('keep'),
    0x80, 0x2C, 0x20,                               # sub byte [si],20h
    L('keep'), 0x46, 0xE2, J('up'),                 # inc si / loop
    0x8A, 0x0E, W('len'), 0x30, 0xED, 0xBA, W('text'), 0xBB, 0x01, 0x00, 0xB4, 0x40, INT21,
    L('done'), 0xB8, 0x00, 0x4C, INT21,
    L('buf'), 40, L('len'), 0, L('text'), bytes(41),
])

# Direct video memory + INT 10h teletype.
VIDEO = asm([
    0xB8, 0x00, 0xB8, 0x8E, 0xC0,   # mov ax,B800h / mov es,ax
    0xBF, 0x00, 0x0F,               # mov di,24*160
    0xB8, 0x41, 0x1E,               # mov ax,1E41h
    0xB9, 0x05, 0x00, 0xFC, 0xF3, 0xAB,             # rep stosw
    0xB8, 0x5A, 0x0E, 0xCD, 0x10,   # teletype 'Z'
    0xB4, 0x0F, 0xCD, 0x10,         # get mode -> AL=3, AH=80
    0x3D, 0x03, 0x50, 0x75, J('bad'),
    0xB8, 0x00, 0x4C, INT21,
    L('bad'), 0xB8, 0x01, 0x4C, INT21,
])

# FindFirst / FindNext over *.TXT, one name per line.
FIND = asm([
    0xB4, 0x4E, 0x31, 0xC9, 0xBA, W('spec'), INT21, 0x72, J('done'),
    L('entry'), 0xBE, 0x9E, 0x00,
    L('char'), 0xAC, 0x08, 0xC0, 0x74, J('eol'),
    0x88, 0xC2, 0xB4, 0x02, INT21, 0xEB, J('char'),
    L('eol'), 0xB2, 0x0D, 0xB4, 0x02, INT21, 0xB2, 0x0A, 0xB4, 0x02, INT21,
    0xB4, 0x4F, INT21, 0x73, J('entry'),
    L('done'), 0xB8, 0x00, 0x4C, INT21,
    L('spec'), '*.TXT\0',
])

# EXEC (AH=4B00) of HELLO.COM, then the child's exit code (AH=4D) + 1.
EXEC = asm([
    0xBC, 0x00, 0x08,               # mov sp,800h
    0xBB, 0x80, 0x00, 0xB4, 0x4A, INT21, 0x72, J('fail'),   # shrink to 80h paragraphs
    0x8C, 0x0E, W('tailseg'),       # mov [tailseg],cs
    0xBA, W('name'), 0xBB, W('pb'), 0xB8, 0x00, 0x4B, INT21, 0x72, J('fail'),
    0xB4, 0x4D, INT21,              # AL = 7, AH = 0
    0x80, 0xFC, 0x00, 0x75, J('fail'),
    0xFE, 0xC0, 0xB4, 0x4C, INT21,  # inc al / exit
    L('fail'), 0xB8, 0x63, 0x4C, INT21,
    L('name'), 'HELLO.COM\0',
    L('tail'), 0, 0x0D,
    L('pb'), 0, 0, W('tail'), L('tailseg'), 0, 0, bytes(8),
])

# Memory: allocate 100h paragraphs must fail first (a .COM owns all memory),
# after shrinking it works; free it again; hook and read back a vector.
MEM = asm([
    0xBC, 0x00, 0x08,
    0xBB, 0x00, 0x01, 0xB4, 0x48, INT21, 0x73, J('fail'),   # must fail
    0x3D, 0x08, 0x00, 0x75, J('fail'),                      # error 8
    0xBB, 0x80, 0x00, 0xB4, 0x4A, INT21, 0x72, J('fail'),
    0xBB, 0x00, 0x01, 0xB4, 0x48, INT21, 0x72, J('fail'),
    0x8E, 0xC0, 0xB4, 0x49, INT21, 0x72, J('fail'),         # mov es,ax / free
    0xB8, 0x60, 0x25, 0xBA, 0x34, 0x12, INT21,              # set vector 60h = DS:1234h
    0xB8, 0x60, 0x35, INT21,                                # get vector -> ES:BX
    0x81, 0xFB, 0x34, 0x12, 0x75, J('fail'),
    0x8C, 0xC0, 0x8C, 0xDB, 0x39, 0xD8, 0x75, J('fail'),    # mov ax,es / mov bx,ds / cmp ax,bx
    0xB4, 0x30, INT21, 0x3C, 0x05, 0x75, J('fail'),         # DOS version 5
    0xB8, 0x00, 0x4C, INT21,
    L('fail'), 0xB8, 0x01, 0x4C, INT21,
])

# Hooks INT 1Ch, waits for two timer ticks, puts the old vector back.
TIMER = asm([
    0xB8, 0x1C, 0x35, INT21, 0x06, 0x53,                    # get vector / push es / push bx
    0xB8, 0x1C, 0x25, 0xBA, W('handler'), INT21, 0xFB,      # set vector / sti
    L('wait'), 0x83, 0x3E, W('count'), 0x02, 0x72, J('wait'),
    0x5A, 0x1F, 0xB8, 0x1C, 0x25, INT21,                    # pop dx / pop ds / restore
    0xB8, 0x00, 0x4C, INT21,
    L('handler'), 0x2E, 0xFF, 0x06, W('count'), 0xCF,       # inc word cs:[count] / iret
    L('count'), 0, 0,
])

# Stays resident (AH=31h) with a handler on INT 61h; CALLTSR uses it.
TSR = asm([
    0xB8, 0x61, 0x25, 0xBA, W('handler'), INT21,
    0xB8, 0x00, 0x31, 0xBA, 0x20, 0x00, INT21,              # keep 20h paragraphs
    L('handler'), 0xB0, 0x2A, 0xCF,                         # mov al,42 / iret
])
CALLTSR = asm([0x31, 0xC0, 0xCD, 0x61, 0xB4, 0x4C, INT21])

# MKDIR, CHDIR, current directory, RENAME, DELETE through INT 21h.
DIRS = asm([
    0xB4, 0x39, 0xBA, W('dir'), INT21, 0x72, J('fail'),
    0xB4, 0x3B, 0xBA, W('dir'), INT21, 0x72, J('fail'),
    0xB4, 0x47, 0xB2, 0x00, 0xBE, W('buf'), INT21, 0x72, J('fail'),
    0xBE, W('buf'),
    L('char'), 0xAC, 0x08, 0xC0, 0x74, J('shown'), 0x88, 0xC2, 0xB4, 0x02, INT21, 0xEB, J('char'),
    L('shown'), 0xB4, 0x3B, 0xBA, W('up'), INT21, 0x72, J('fail'),
    0xB4, 0x56, 0xBA, W('old'), 0xBF, W('new'), INT21, 0x72, J('fail'),
    0xB4, 0x41, 0xBA, W('gone'), INT21, 0x72, J('fail'),
    0xB4, 0x41, 0xBA, W('gone'), INT21, 0x73, J('fail'),    # the second delete must fail ...
    0x3D, 0x02, 0x00, 0x75, J('fail'),                      # ... with "file not found"
    0xB8, 0x00, 0x4C, INT21,
    L('fail'), 0xB8, 0x01, 0x4C, INT21,
    L('dir'), 'NEWDIR\0', L('up'), '..\0', L('old'), 'A.TXT\0', L('new'), 'B.TXT\0', L('gone'), 'C.TXT\0',
    L('buf'), bytes(68),
])

# Undefined opcode (0F 0B outside the BIOS stub segment).
ILLEGAL = bytes([0x0F, 0x0B])
# Divide by zero.
DIVZERO = bytes([0x31, 0xDB, 0xF7, 0xF3, 0xB8, 0x00, 0x4C, 0xCD, 0x21])


def mz_exe():
    # Code segment 0, data segment 1 (needs one relocation), stack segment 2.
    code = bytes([0xB8, 0x01, 0x00,     # mov ax,SEG data   <- relocated
                  0x8E, 0xD8,           # mov ds,ax
                  0x31, 0xD2,           # xor dx,dx
                  0xB4, 0x09, 0xCD, 0x21,
                  0xB8, 0x03, 0x4C, 0xCD, 0x21])
    image = code.ljust(16, b'\x90') + b'Hello from EXE\r\n$'
    header = struct.pack('<2s13H', b'MZ', (0x20 + len(image)) % 512, 1, 1, 2, 0x20, 0xFFFF, 2, 0x100, 0, 0, 0, 0x1C, 0)
    header += struct.pack('<HH', 1, 0)
    return header.ljust(0x20, b'\0') + image


PROGRAMS = {'HELLO.COM': HELLO, 'FILEIO.COM': FILEIO, 'SEEK.COM': SEEK, 'CPU.COM': CPU, 'CPU2.COM': CPU2, 'ARGS.COM': ARGS, 'KEY.COM': KEY,
            'LINE.COM': LINE, 'VIDEO.COM': VIDEO, 'FIND.COM': FIND, 'EXEC.COM': EXEC, 'MEM.COM': MEM,
            'TIMER.COM': TIMER, 'TSR.COM': TSR, 'CALLTSR.COM': CALLTSR, 'DIRS.COM': DIRS, 'ILLEGAL.COM': ILLEGAL, 'DIVZERO.COM': DIVZERO, 'HELLOEXE.EXE': mz_exe()}


# ---- harness --------------------------------------------------------------------
class Box:
    def __init__(self):
        self.top = Path(tempfile.mkdtemp(prefix='charondos-'))
        self.c = self.top / 'c'; self.d = self.top / 'd'
        self.c.mkdir(); self.d.mkdir()
        for name, data in PROGRAMS.items(): (self.c / name).write_bytes(data)

    def run(self, command=None, keys=None, extra=()):
        argv = [str(exe), '--root', f'C={self.c}', '--root', f'D={self.d}', *extra]
        if command is not None: argv += ['-c', command]
        if keys is not None: argv += ['--keys', keys]
        r = subprocess.run(argv, capture_output=True, timeout=30)
        text = r.stdout.decode('utf-8', 'replace')
        if verbose: print(f'--- {command or keys!r} -> {r.returncode}\n{text}')
        return r.returncode, text

    def bat(self, name, lines): (self.c / name).write_bytes('\r\n'.join(lines).encode('cp437') + b'\r\n')
    def close(self): shutil.rmtree(self.top, ignore_errors=True)


cases, failures = [], []
def case(f): cases.append(f); return f
def lines_of(text): return [l.strip() for l in text.replace('\r', '').split('\n') if l.strip()]


# ---- real-mode programs ---------------------------------------------------------
@case
def com_print_string_and_exit_code(b):
    code, out = b.run('HELLO')
    assert 'Hello from COM' in out, out
    assert code == 7, code

@case
def com_file_create_write_read(b):
    code, out = b.run('FILEIO.COM')
    assert code == 0, (code, out)
    assert 'DATA123' in out, out
    assert (b.c / 'OUT.TXT').read_bytes() == b'DATA123'

@case
def com_seek_append_reread(b):
    b.run('FILEIO')
    code, out = b.run('SEEK')
    assert code == 7, (code, out)                    # length before the append
    assert (b.c / 'OUT.TXT').read_bytes() == b'DATA123+END'
    assert '123' in out, out

@case
def com_cpu_instructions(b):
    code, out = b.run('CPU')
    assert '12345 abc H OK' in out, out
    assert code == 0, code

@case
def com_cpu_instructions_wide(b):
    code, out = b.run('CPU2')
    assert code == 0, f'stage {code} failed'

@case
def com_command_tail(b):
    code, out = b.run('ARGS one two /x')
    assert '[ one two /x]' in out, out

@case
def exe_mz_with_relocation(b):
    code, out = b.run('HELLOEXE')
    assert 'Hello from EXE' in out, out
    assert code == 3, code

@case
def com_video_memory_and_teletype(b):
    code, out = b.run('VIDEO', extra=['--dump-screen'])
    assert code == 0, (code, out)
    screen = [l[7:] for l in out.replace('\r', '').split('\n') if l.startswith('SCREEN|')]
    assert len(screen) == 25, len(screen)
    assert screen[24] == 'AAAAA', screen[24]
    assert screen[0] == 'Z', screen[0]

@case
def com_find_first_next(b):
    for n in ('A.TXT', 'B.TXT', 'C.DAT'): (b.c / n).write_bytes(b'x')
    code, out = b.run('FIND')
    assert lines_of(out) == ['A.TXT', 'B.TXT'], out

@case
def com_exec_child_and_exit_code(b):
    code, out = b.run('EXEC')
    assert 'Hello from COM' in out, out
    assert code == 8, code

@case
def com_memory_vectors_version(b):
    code, out = b.run('MEM')
    assert code == 0, (code, out)

@case
def com_timer_interrupt_hook(b):
    code, out = b.run('TIMER')
    assert code == 0, (code, out)

@case
def com_resident_program(b):
    b.bat('T.BAT', ['@ECHO OFF', 'TSR', 'MEM', 'CALLTSR'])
    code, out = b.run('T')
    assert code == 42, (code, out)
    free = [int(l.split()[0]) for l in lines_of(out) if 'bytes free' in l][0]
    code, out = b.run('MEM')
    assert [int(l.split()[0]) for l in lines_of(out) if 'bytes free' in l][0] > free, out

@case
def com_directory_calls(b):
    for n in ('A.TXT', 'C.TXT'): (b.c / n).write_bytes(b'x')
    code, out = b.run('DIRS')
    assert code == 0 and 'NEWDIR' in out, (code, out)
    assert (b.c / 'NEWDIR').is_dir() and (b.c / 'B.TXT').exists() and not (b.c / 'A.TXT').exists() and not (b.c / 'C.TXT').exists()

@case
def com_keyboard_input(b):
    code, out = b.run(keys='KEY{ENTER}Q')
    assert code == ord('Q'), (code, out)

@case
def com_buffered_line_input(b):
    code, out = b.run(keys='LINE{ENTER}helxx{BS}{BS}lo{ENTER}EXIT{ENTER}')
    assert 'HELLO' in out, out

@case
def illegal_instruction_ends_program(b):
    code, out = b.run('ILLEGAL')
    assert 'Illegal instruction at' in out and ':0100 0F' in out, out
    code, out = b.run('HELLO')                      # the machine is still usable in a new run
    assert code == 7

@case
def divide_error_ends_program(b):
    b.bat('T.BAT', ['@ECHO OFF', 'DIVZERO', 'ECHO after', 'HELLO'])
    code, out = b.run('T')
    assert 'Divide overflow' in out and 'after' in out and 'Hello from COM' in out, out


# ---- shell --------------------------------------------------------------------------
@case
def shell_echo_ver_set(b):
    code, out = b.run('ECHO Hello World')
    assert lines_of(out) == ['Hello World'], out
    code, out = b.run('VER')
    assert 'CharonDOS version' in out, out
    b.bat('T.BAT', ['@ECHO OFF', 'SET FOO=bar baz', 'ECHO [%FOO%]', 'SET FOO=', 'ECHO [%FOO%]', 'SET'])
    code, out = b.run('T')
    assert '[bar baz]' in out and '[]' in out and 'COMSPEC=C:\\COMMAND.COM' in out and 'FOO=' not in out, out

@case
def shell_dir(b):
    (b.c / 'ALPHA.TXT').write_bytes(b'12345')
    (b.c / 'SUB').mkdir()
    code, out = b.run('DIR')
    assert 'Directory of C:\\' in out, out
    row = [l for l in lines_of(out) if l.startswith('ALPHA ')][0].split()
    assert row[:3] == ['ALPHA', 'TXT', '5'], row
    assert any(l.startswith('SUB') and '<DIR>' in l for l in lines_of(out)), out
    code, out = b.run('DIR /B *.TXT')
    assert lines_of(out) == ['ALPHA.TXT'], out
    code, out = b.run('DIR /W')
    assert '[SUB]' in out and 'ALPHA.TXT' in out, out
    code, out = b.run('DIR NOTHERE.*')
    assert 'File not found' in out and code == 1, (code, out)

@case
def shell_type(b):
    (b.c / 'T.TXT').write_bytes(b'line one\r\nline two\r\n\x1Ahidden')
    code, out = b.run('TYPE T.TXT')
    assert lines_of(out) == ['line one', 'line two'], out
    code, out = b.run('TYPE MISSING.TXT')
    assert 'File not found' in out and code == 1, (code, out)

@case
def shell_md_cd_rd(b):
    b.bat('T.BAT', ['@ECHO OFF', 'MD WORK', 'CD WORK', 'CD', 'MD DEEP', 'CD DEEP', 'CD', 'CD ..', 'CD', 'RD DEEP', 'CD \\', 'CD'])
    code, out = b.run('T')
    assert lines_of(out) == ['C:\\WORK', 'C:\\WORK\\DEEP', 'C:\\WORK', 'C:\\'], out
    assert (b.c / 'WORK').is_dir() and not (b.c / 'WORK' / 'DEEP').exists()
    code, out = b.run('RD WORK')
    assert not (b.c / 'WORK').exists(), out
    code, out = b.run('CD NOWHERE')
    assert 'Path not found' in out and code == 1, (code, out)

@case
def shell_copy(b):
    data = bytes(range(256)) * 40                    # more than one copy buffer
    (b.c / 'SRC.BIN').write_bytes(data)
    (b.c / 'DEST').mkdir()
    code, out = b.run('COPY SRC.BIN DST.BIN')
    assert '1 file(s) copied' in out, out
    assert (b.c / 'DST.BIN').read_bytes() == data
    code, out = b.run('COPY SRC.BIN DEST')
    assert (b.c / 'DEST' / 'SRC.BIN').read_bytes() == data, out
    code, out = b.run('COPY *.BIN D:')
    assert '2 file(s) copied' in out, out
    assert (b.d / 'SRC.BIN').read_bytes() == data and (b.d / 'DST.BIN').read_bytes() == data
    code, out = b.run('COPY SRC.BIN SRC.BIN')
    assert 'Access denied' in out and (b.c / 'SRC.BIN').read_bytes() == data, out
    code, out = b.run('COPY NONE.BIN X.BIN')
    assert 'File not found' in out and not (b.c / 'X.BIN').exists(), out

@case
def shell_del_ren(b):
    for n in ('A.TMP', 'B.TMP', 'KEEP.TXT'): (b.c / n).write_bytes(b'x')
    code, out = b.run('REN KEEP.TXT KEPT.TXT')
    assert (b.c / 'KEPT.TXT').exists() and not (b.c / 'KEEP.TXT').exists(), out
    code, out = b.run('DEL *.TMP')
    assert not (b.c / 'A.TMP').exists() and not (b.c / 'B.TMP').exists() and (b.c / 'KEPT.TXT').exists(), out
    code, out = b.run('DEL A.TMP')
    assert 'File not found' in out and code == 1, (code, out)
    code, out = b.run('ERASE KEPT.TXT')
    assert not (b.c / 'KEPT.TXT').exists()

@case
def shell_redirection(b):
    code, out = b.run('ECHO first > R.TXT')
    assert out.strip() == '', out
    b.run('ECHO second >> R.TXT')
    assert (b.c / 'R.TXT').read_bytes() == b'first\r\nsecond\r\n', (b.c / 'R.TXT').read_bytes()
    b.run('HELLO > H.TXT')
    assert (b.c / 'H.TXT').read_bytes() == b'Hello from COM\r\n'
    (b.c / 'IN.TXT').write_bytes(b'typed\r\n')
    code, out = b.run('LINE < IN.TXT')
    assert 'TYPED' in out, out

@case
def shell_drives(b):
    (b.d / 'ONLYD.TXT').write_bytes(b'd drive\r\n')
    b.bat('T.BAT', ['@ECHO OFF', 'D:', 'CD', 'TYPE ONLYD.TXT', 'C:', 'CD', 'TYPE D:ONLYD.TXT', 'Q:'])
    code, out = b.run('T')
    assert lines_of(out) == ['D:\\', 'd drive', 'C:\\', 'd drive', 'Invalid drive'], out

@case
def shell_batch_flow(b):
    b.bat('SUB.BAT', ['@ECHO OFF', 'ECHO sub %1 %2', 'SHIFT', 'ECHO shifted %1'])
    b.bat('T.BAT', ['@ECHO OFF', 'REM a comment', 'IF EXIST HELLO.COM ECHO exists', 'IF NOT EXIST NOPE.X ECHO missing',
                    'HELLO', 'IF ERRORLEVEL 7 ECHO ge7', 'IF ERRORLEVEL 8 ECHO ge8', 'IF NOT ERRORLEVEL 8 ECHO lt8',
                    'IF %1==go GOTO there', 'ECHO skipped', ':there', 'CALL SUB one two', 'IF "%2"=="" ECHO noarg2', 'ECHO end'])
    code, out = b.run('T go')
    assert lines_of(out) == ['exists', 'missing', 'Hello from COM', 'ge7', 'lt8', 'sub one two', 'shifted two', 'noarg2', 'end'], out

@case
def shell_path_search_and_bad_command(b):
    (b.c / 'BIN').mkdir()
    shutil.move(str(b.c / 'HELLO.COM'), str(b.c / 'BIN' / 'HELLO.COM'))
    code, out = b.run('HELLO')
    assert 'Bad command or file name' in out and code == 1, (code, out)
    b.bat('T.BAT', ['@ECHO OFF', 'PATH C:\\BIN', 'PATH', 'HELLO'])
    code, out = b.run('T')
    assert 'PATH=C:\\BIN' in out and 'Hello from COM' in out, out
    code, out = b.run('BIN\\HELLO')
    assert 'Hello from COM' in out, out

@case
def shell_long_names_get_aliases(b):
    (b.c / 'Long File Name.txt').write_bytes(b'long one')
    (b.c / 'Long File Other.txt').write_bytes(b'long two')
    (b.c / 'readme.md').write_bytes(b'lower')
    code, out = b.run('DIR /B')
    names = lines_of(out)
    assert 'LONGFI~1.TXT' in names and 'LONGFI~2.TXT' in names and 'README.MD' in names, out
    code, out = b.run('TYPE LONGFI~1.TXT')
    assert 'long one' in out, out
    code, out = b.run('TYPE readme.md')
    assert 'lower' in out, out
    code, out = b.run('DIR LONGFI~2.TXT')
    assert 'Long File Other.txt' in out, out

@case
def shell_interactive_editing_history_exit(b):
    code, out = b.run(keys='ECHO abX{BS}c{ENTER}{UP}{ENTER}ECHO ad{LEFT}c{HOME}{DEL}E{ENTER}EXIT{ENTER}ECHO never{ENTER}')
    assert 'CharonDOS version' in out and 'C:\\>' in out, out
    assert lines_of(out).count('abc') == 2, out
    assert 'acd' in lines_of(out), out
    assert 'never' not in lines_of(out), out

@case
def shell_polish_messages(b):
    code, out = b.run('TYPE BRAK.TXT', extra=['--pl'])
    assert 'Nie znaleziono pliku' in out, out
    code, out = b.run('NIEMA', extra=['--pl'])
    assert 'Nieznane polecenie lub nazwa pliku' in out, out

@case
def shell_autoexec_mem_help(b):
    b.bat('AUTOEXEC.BAT', ['@ECHO OFF', 'ECHO from autoexec'])
    code, out = b.run('MEM')
    assert 'from autoexec' in out and '655360 bytes total' in out, out
    free = [int(l.split()[0]) for l in lines_of(out) if 'bytes free' in l][0]
    assert 600000 < free < 655360, free
    code, out = b.run('HELP')
    assert 'COPY src dst' in out, out

@case
def shell_ctrl_c_stops_waiting_program(b):
    code, out = b.run(keys='KEY{ENTER}{CTRLC}ECHO back{ENTER}EXIT{ENTER}')
    assert '^C' in out and 'back' in lines_of(out), out


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
