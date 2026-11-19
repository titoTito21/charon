"""Build Charon's loadable synthesizer modules (r393) on a Windows host.

  python build-charon-synths.py NEW_BUILD_DIRECTORY [--stage NEW_STAGING_DIRECTORY [--test]] [--only espeak|rhvoice]

Produces espeak.tts / rhvoice.tts (PE32+ x64 boot service drivers, subsystem 11, no imports,
NX_COMPAT, no W+X section), their compressed data packs and manifests
(CharonSynthModule.h). --stage writes <STAGE>/EFI/HAK/synths/... ready to
copy onto Charon's disk.

Inputs (all below the repository container `kont`, see README in
src/synth): work/espeak-<commit>.zip (pinned eSpeak NG), work/musl-1.2.5.tar.gz,
work/lzma-sdk-dl/lzma2606.7z (LZMA SDK 26.06, public domain), and
work/rhvoice-dl/RHVoice (git checkout of RHVoice 1.18.5, core.autocrlf=false,
with the submodules data/languages/{Polish,English} and data/voices/{natan,slt}),
work/rhvoice-dl/{lgpl-2.1.txt,cmu_us_slt_arctic-COPYING.txt} (licence texts),
work/llvm-src-dl/llvm-project-23.1.5.src.tar.xz (libc++/libc++abi/libunwind).
--test runs the staged .tts files in tts-host-loader and compares their audio
sample by sample with hosted builds of the same engine sources. Archives are
extracted into the build directory; no script from them is ever executed.
eSpeak NG data is compiled with a host build of the unmodified compilers
(src/synth/host/espeak-compile-data.c), as upstream data.cmake does.

Toolchain: LLVM 23.1.5 (work/llvm-23.1.5/bin, CHARON_LLVM) and AUDK ImageTool
(CHARON_IMAGETOOL), as tools/build-charon-windows.py; host helper programs
use the same clang with the installed MSVC/Windows SDK.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import hashlib, json, lzma, os, re, shutil, struct, subprocess, sys, tarfile, zipfile

root = Path(__file__).resolve().parents[1]           # outputs/nvda-haiku
container = root.parents[1]                          # kont
work = container / 'work'
llvm = Path(os.environ.get('CHARON_LLVM', work / 'llvm-23.1.5/bin'))
imagetool = Path(os.environ.get('CHARON_IMAGETOOL', work / 'ha/BaseTools/Bin/Win64/ImageTool.exe'))
clang = str(llvm / 'clang.exe'); clangxx = str(llvm / 'clang++.exe')
lld = str(llvm / 'ld.lld.exe'); nm = str(llvm / 'llvm-nm.exe'); readelf = str(llvm / 'llvm-readelf.exe')
objdump = str(llvm / 'llvm-objdump.exe')
synth = root / 'src/synth'
ESPEAK_COMMIT = 'b97de3cf2ad32c1d48c6bd36dfbf57a9c587fedc'
ESPEAK_ZIP = work / f'espeak-{ESPEAK_COMMIT}.zip'
ESPEAK_VARIANTS = ['m3', 'f3', 'm6', 'm7', 'f4', 'klatt', 'f1']   # r397 families 1..7 (espeak_module.cpp familyVariants)
MUSL_TGZ = work / 'musl-1.2.5.tar.gz'
MUSL_SHA256 = 'a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4'
LZMA_7Z = work / 'lzma-sdk-dl/lzma2606.7z'
LZMA_SHA256 = '1d9da340870b12712e9068be178a4436fbad785486b62aed6e2aa88ea1e3bb65'
RHVOICE = work / 'rhvoice-dl/RHVoice'
SEVENZIP = shutil.which('7z') or r'C:\Program Files\7-Zip\7z.exe'
NL = '\n'

argv = sys.argv[1:]
def option(name):
    if name in argv:
        i = argv.index(name); value = argv[i + 1]; del argv[i:i + 2]; return value
    return None
stage_arg = option('--stage'); only = option('--only')
run_tests = '--test' in argv
if run_tests: argv.remove('--test')
if len(argv) != 1: sys.exit(__doc__)
build = Path(argv[0]).resolve(); build.mkdir(parents=True, exist_ok=False)
log = (build / 'build.log').open('w', encoding='utf-8')
targets = [only] if only else ['espeak', 'rhvoice']

def run(command, cwd=None, env=None):
    command = list(map(str, command))
    log.write('$ ' + ' '.join(command) + '\n'); log.flush()
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, env=env, errors='replace')
    log.write(result.stdout + result.stderr); log.flush()
    if result.returncode:
        sys.stderr.write(result.stdout[-4000:] + result.stderr[-8000:])
        raise SystemExit(f'FAILED ({result.returncode}): {command[0]} ... {command[-1]}')
    return result.stdout

def sha256(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def parallel(function, items, workers=16):
    with ThreadPoolExecutor(max_workers=workers) as pool: return list(pool.map(function, items))

def safe_extract_zip(archive, destination):
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            assert (destination / name).resolve().is_relative_to(destination.resolve()), name
        z.extractall(destination)

# ---------------------------------------------------------------- sources
src = build / 'src'; src.mkdir()
assert sha256(MUSL_TGZ) == MUSL_SHA256, 'musl archive hash'
with tarfile.open(MUSL_TGZ) as t: t.extractall(src, filter='data')
musl = src / 'musl-1.2.5'
assert sha256(LZMA_7Z) == LZMA_SHA256, 'LZMA SDK archive hash'
run([SEVENZIP, 'x', '-y', '-o' + str(src / 'lzma-sdk'), LZMA_7Z, 'C/LzmaDec.c', 'C/LzmaDec.h', 'C/7zTypes.h',
     'C/Compiler.h', 'C/Precomp.h', 'C/7zWindows.h', 'DOC/lzma-sdk.txt'])
lzma_sdk = src / 'lzma-sdk/C'

# ---------------------------------------------------------------- flags
target = ['--target=x86_64-unknown-linux-musl']
codegen = ['-O2', '-fno-stack-protector', '-fno-pic', '-fno-pie', '-mcmodel=large', '-mno-red-zone',
           '-ffunction-sections', '-fdata-sections', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
           '-fno-plt', '-fno-jump-tables']
gen = build / 'musl-gen'; (gen / 'bits').mkdir(parents=True)
sed = shutil.which('sed') or r'C:\Program Files\Git\usr\bin\sed.exe'
(gen / 'bits/alltypes.h').write_text(run([sed, '-f', musl / 'tools/mkalltypes.sed', musl / 'arch/x86_64/bits/alltypes.h.in',
                                          musl / 'include/alltypes.h.in']), newline='\n')
syscall_in = (musl / 'arch/x86_64/bits/syscall.h.in').read_text()
(gen / 'bits/syscall.h').write_text(syscall_in + ''.join(l.replace('__NR_', 'SYS_') + '\n'
                                    for l in syscall_in.splitlines() if '__NR_' in l), newline='\n')
(gen / 'linux').mkdir()
(gen / 'linux/limits.h').write_text('#include <limits.h>' + chr(10), newline=chr(10))  # eSpeak's PATH_MAX source
(gen / 'version.h').write_text('#define VERSION "1.2.5"\n', newline='\n')
override = synth / 'common/synth-musl-arch'
libc_public = ['-nostdinc', '-isystem', str(gen), '-isystem', str(musl / 'arch/x86_64'),
               '-isystem', str(musl / 'arch/generic'), '-isystem', str(musl / 'include')]
musl_flags = target + codegen + ['-std=c99', '-ffreestanding', '-nostdinc', '-D_XOPEN_SOURCE=700', '-w',
    '-fexcess-precision=standard', '-frounding-math', '-fno-strict-aliasing',
    '-I' + str(override), '-I' + str(musl / 'arch/x86_64'), '-I' + str(musl / 'arch/generic'), '-I' + str(gen),
    '-I' + str(musl / 'src/include'), '-I' + str(musl / 'src/internal'), '-I' + str(gen), '-I' + str(musl / 'include')]

# ---------------------------------------------------------------- libc closure
# Whole musl directories whose code is pure computation; files that need the
# kernel (files, time zones, locale files, threads, process) are replaced
# by src/synth/common/synth_posix.c or left out. Unreferenced code is
# dropped by --gc-sections; the final image is scanned for syscall/%fs use.
MUSL_DIRS = ['string', 'ctype', 'multibyte', 'stdlib', 'math', 'locale', 'errno', 'stdio', 'prng', 'search',
             'internal', 'env', 'exit', 'complex', 'fenv']
MUSL_EXCLUDE = {
    'stdio': {'fopen', 'freopen', 'stdout', 'stderr', 'stdin', '__stdio_close', '__stdio_read', '__stdio_seek',
              '__stdio_write', '__stdout_write', '__fdopen', 'popen', 'pclose', 'tmpfile', 'tmpnam', 'tempnam',
              'remove', 'rename', '__fopen_rb_ca', 'fopencookie', 'fgetc', 'fputc', 'getc', 'getchar', 'putc',
              'putchar', '__lockfile', 'flockfile', 'ftrylockfile', 'funlockfile'},
    'locale': {'locale_map', 'dcngettext', 'textdomain', 'bind_textdomain_codeset', 'setlocale', 'catopen',
               'catclose', 'catgets'},
    'internal': {'defsysinfo', 'emulate_wait4', 'procfdname', 'syscall_ret', 'vdso', 'libc_'},
    'env': {'__init_tls', '__libc_start_main', '__reset_tls', '__stack_chk_fail', 'secure_getenv'},
    'exit': {'abort', 'abort_lock', 'exit', '_Exit', 'atexit', 'at_quick_exit', 'quick_exit'},
    'errno': set(), 'string': set(), 'ctype': set(), 'multibyte': set(), 'stdlib': {'system'},
    'math': set(), 'prng': set(), 'search': set(), 'complex': set(), 'fenv': set(),
}
def musl_sources():
    out = []
    for d in MUSL_DIRS:
        base = musl / 'src' / d; arch = base / 'x86_64'
        arch_files = {p.stem: p for p in arch.glob('*') if p.suffix in ('.c', '.s', '.S')} if arch.is_dir() else {}
        for p in sorted(base.glob('*.c')):
            if p.stem in MUSL_EXCLUDE.get(d, set()) or p.stem in arch_files: continue
            out.append(p)
        out += sorted(arch_files.values())
    return out

def compile_c(source, output, flags, compiler=None):
    compiler = compiler or (clangxx if str(source).endswith('.cpp') else clang)
    run([compiler, *flags, '-c', source, '-o', output]); return output

def build_libc():
    out = build / 'libc'; out.mkdir()
    sources = musl_sources()
    def one(item):
        i, s = item
        return compile_c(s, out / f'm{i:04d}-{s.stem}.o', musl_flags if s.suffix == '.c' else target + ['-mno-red-zone'])
    objects = parallel(one, list(enumerate(sources)))
    common = synth / 'common'
    mine = [(common / 'synth_posix.c', musl_flags), (common / 'synth_malloc.c', musl_flags),
            (common / 'synth_coro.c', musl_flags), (common / 'synth_coro_x64.S', target + ['-mno-red-zone']),
            (common / 'synth_pack.c', target + codegen + libc_public + ['-std=c11', '-Wall', '-Wextra', '-Werror',
                                                                       '-I' + str(common), '-I' + str(lzma_sdk)]),
            (lzma_sdk / 'LzmaDec.c', target + codegen + libc_public + ['-std=c11', '-I' + str(lzma_sdk)])]
    objects += parallel(lambda item: compile_c(item[0], out / ('c-' + item[0].stem + '.o'),
                        item[1] + ['-I' + str(common)]), mine)
    archive = build / 'libsynthc.a'
    rsp = out / 'objects.rsp'; rsp.write_text(NL.join(o.as_posix() for o in objects), newline=NL)
    run([llvm / 'llvm-ar.exe', 'rcs', archive, '@' + str(rsp)])
    return [archive]

# ---------------------------------------------------------------- linking
LINKER_SCRIPT = '''OUTPUT_FORMAT(elf64-x86-64)
ENTRY(efi_main)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x1000;
 .text : ALIGN(0x1000) { *(.text .text.* .ltext .ltext.* .gnu.linkonce.t.*) } :text
 .rodata : ALIGN(0x1000) { *(.rodata .rodata.* .lrodata .lrodata.* .gnu.linkonce.r.*) *(.gcc_except_table .gcc_except_table.*) } :rodata
%(EH)s
 .data : ALIGN(0x1000) { *(.data.rel.ro .data.rel.ro.*) *(.got .got.*) *(.data .data.* .ldata .ldata.* .gnu.linkonce.d.*)
   . = ALIGN(8); __init_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
   *(.bss .bss.* .lbss .lbss.*) *(COMMON) *(LARGE_COMMON) } :data
 /DISCARD/ : { *(.group) *(.comment) *(.note.GNU-stack) *(.llvm_addrsig) *(.fini_array .fini_array.*) %(DISCARD)s }
}
'''
EH_SECTIONS = ''' .eh_frame_hdr : ALIGN(0x1000) { __eh_frame_hdr_start = .; *(.eh_frame_hdr) __eh_frame_hdr_end = .; } :rodata
 .eh_frame : ALIGN(0x1000) { __eh_frame_start = .; KEEP(*(.eh_frame)) __eh_frame_end = .; } :rodata'''
LINKER_SYMBOLS = {'__init_array_start', '__init_array_end', '__eh_frame_start', '__eh_frame_end',
                  '__eh_frame_hdr_start', '__eh_frame_hdr_end'}

def harden(path):
    image = bytearray(path.read_bytes()); pe = struct.unpack_from('<I', image, 0x3c)[0]
    count = struct.unpack_from('<H', image, pe + 6)[0]; optional = pe + 24; optional_size = struct.unpack_from('<H', image, pe + 20)[0]
    assert struct.unpack_from('<I', image, optional + 32)[0] >= 0x1000, 'section alignment below 4 KiB'
    for i in range(count):
        at = optional + optional_size + i * 40; name = bytes(image[at:at + 8]).rstrip(b'\0'); flags = struct.unpack_from('<I', image, at + 36)[0]
        assert not (flags & 0x20000000 and flags & 0x80000000), ('writable and executable section', name)
        if name == b'.rodata': assert not flags & 0x80000000, '.rodata is writable'
    struct.pack_into('<H', image, optional + 70, struct.unpack_from('<H', image, optional + 70)[0] | 0x0100)
    struct.pack_into('<I', image, optional + 64, 0)
    total = 0
    for at in range(0, len(image) - len(image) % 2, 2):
        total += struct.unpack_from('<H', image, at)[0]; total = (total & 0xffff) + (total >> 16)
    if len(image) % 2: total += image[-1]; total = (total & 0xffff) + (total >> 16)
    struct.pack_into('<I', image, optional + 64, ((total & 0xffff) + (total >> 16) & 0xffff) + len(image))
    path.write_bytes(bytes(image))

def verify_pe(path):
    data = path.read_bytes(); u16 = lambda a: struct.unpack_from('<H', data, a)[0]; u32 = lambda a: struct.unpack_from('<I', data, a)[0]
    pe = u32(0x3c); optional = pe + 24; count = u16(pe + 6); optional_size = u16(pe + 20)
    report = dict(file=path.name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                  machine=hex(u16(pe + 4)), subsystem=u16(optional + 68), nx_compat=bool(u16(optional + 70) & 0x100),
                  imports=(u32(optional + 120), u32(optional + 124)), sections=[])
    assert report['machine'] == '0x8664' and report['subsystem'] == 11 and report['nx_compat']
    assert report['imports'] == (0, 0), 'import directory present'
    for i in range(count):
        at = optional + optional_size + i * 40; flags = u32(at + 36)
        name = data[at:at + 8].rstrip(b'\0').decode()
        report['sections'].append(dict(name=name, virtual=u32(at + 8), flags=hex(flags),
                                       w=bool(flags & 0x80000000), x=bool(flags & 0x20000000)))
        assert not (flags & 0x80000000 and flags & 0x20000000), ('W+X section', name)
    report['check_charon_pe'] = json.loads(run([sys.executable, work / 'check-charon-pe.py', path]))
    return report

def uleb(data, at):
    value = shift = 0
    while True:
        b = data[at]; at += 1; value |= (b & 0x7f) << shift; shift += 7
        if not b & 0x80: return value, at

def add_eh_frame_relocations(elf):
    """lld --emit-relocs does not re-emit relocations of the synthetic
    .eh_frame. With -fno-pic -mcmodel=large the CIE personality pointer and
    the FDE LSDA pointers are absolute 64-bit addresses (DW_EH_PE_udata8), so
    without relocations they would keep their link-time value when firmware
    loads the image elsewhere. This parses .eh_frame and adds a .rela.eh_frame
    section (R_X86_64_64 against a symbol of the target section) for exactly
    those fields, so ImageTool turns them into PE DIR64 base relocations."""
    image = bytearray(elf.read_bytes())
    shoff, = struct.unpack_from('<Q', image, 0x28); shentsize, shnum, shstrndx = struct.unpack_from('<HHH', image, 0x3a)
    sections = [list(struct.unpack_from('<IIQQQQIIQQ', image, shoff + i * shentsize)) for i in range(shnum)]
    strtab_off = sections[shstrndx][4]
    secname = lambda s: image[strtab_off + s[0]:image.index(0, strtab_off + s[0])].decode()
    index = {secname(s): i for i, s in enumerate(sections)}
    eh = sections[index['.eh_frame']]; eh_data = bytes(image[eh[4]:eh[4] + eh[5]]); eh_addr = eh[3]
    sizes = {0x0: 8, 0x2: 2, 0x3: 4, 0x4: 8, 0xa: 2, 0xb: 4, 0xc: 8}
    absolute = []; cies = {}; at = 0
    def pointer(enc, at):
        if enc == 0xff: return None, at
        fmt = enc & 0x0f
        if fmt in (0x1, 0x9): _, end = uleb(eh_data, at); return None, end
        n = sizes[fmt]
        if (enc & 0x70) == 0:
            assert n == 8, ('absolute pointer narrower than 64 bits', hex(enc))
            absolute.append(at)
        return None, at + n
    while at + 4 <= len(eh_data):
        length, = struct.unpack_from('<I', eh_data, at)
        if length == 0: break
        assert length != 0xffffffff
        start = at + 4; end = start + length
        cie_id, = struct.unpack_from('<I', eh_data, start)
        if cie_id == 0:
            p = start + 5; aug_end = eh_data.index(0, p); aug = eh_data[p:aug_end].decode(); p = aug_end + 1
            _, p = uleb(eh_data, p); _, p = uleb(eh_data, p)
            if eh_data[start + 4] == 1: p += 1
            else: _, p = uleb(eh_data, p)
            info = dict(aug=aug, lsda=0xff, fde=0x00)
            if aug.startswith('z'):
                _, p = uleb(eh_data, p)
                for c in aug[1:]:
                    if c == 'P': enc = eh_data[p]; _, p = pointer(enc, p + 1)
                    elif c == 'L': info['lsda'] = eh_data[p]; p += 1
                    elif c == 'R': info['fde'] = eh_data[p]; p += 1
            cies[at] = info
        else:
            cie = cies[start - cie_id]; p = start + 4
            fde_size = sizes[cie['fde'] & 0x0f]
            assert cie['fde'] & 0x70 == 0x10, 'FDE addresses must be pc-relative'
            p += 2 * fde_size
            if cie['aug'].startswith('z'):
                _, p = uleb(eh_data, p)
                if 'L' in cie['aug']: _, p = pointer(cie['lsda'], p)
        at = end
    # A symbol of the section that contains each absolute target.
    symtab = sections[index['.symtab']]
    symbols = [struct.unpack_from('<IBBHQQ', image, symtab[4] + k * 24) for k in range(symtab[5] // 24)]
    def symbol_for(value):
        for k, (n, info, other, shndx, sv, sz) in enumerate(symbols):
            if 0 < shndx < len(sections):
                s = sections[shndx]
                if s[2] & 2 and s[3] <= value < s[3] + s[5] and sv <= value: return k, value - sv
        raise AssertionError(('no symbol for', hex(value)))
    rela = bytearray()
    for off in absolute:
        value, = struct.unpack_from('<Q', eh_data, off)
        if value == 0: continue
        k, addend = symbol_for(value)
        rela += struct.pack('<QQq', eh_addr + off, (k << 32) | 1, addend)   # R_X86_64_64
    names = bytes(image[strtab_off:strtab_off + sections[shstrndx][5]]) + b'.rela.eh_frame\0'
    name_offset = sections[shstrndx][5]
    def align(n): return (n + 7) & ~7
    rela_off = align(len(image)); image += b'\0' * (rela_off - len(image)) + rela
    str_off = len(image); image += names
    sections[shstrndx][4] = str_off; sections[shstrndx][5] = len(names)
    sections.append([name_offset, 4, 0x40, 0, rela_off, len(rela), index['.symtab'], index['.eh_frame'], 8, 24])
    new_shoff = align(len(image)); image += b'\0' * (new_shoff - len(image))
    for s in sections: image += struct.pack('<IIQQQQIIQQ', *s)
    struct.pack_into('<Q', image, 0x28, new_shoff); struct.pack_into('<H', image, 0x3c, len(sections))
    elf.write_bytes(bytes(image))
    return dict(cies=len(cies), absolute_fields=len(absolute), relocations=len(rela) // 24)

def link_module(name, objects, keep_eh=False, constructors=False):
    out = build / name
    rsp = out / 'link.rsp'; rsp.write_text(NL.join(Path(o).as_posix() for o in objects), newline=NL)
    script = out / 'image.lds'
    script.write_text(LINKER_SCRIPT % dict(EH=EH_SECTIONS if keep_eh else '',
                      DISCARD='' if keep_eh else '*(.eh_frame) *(.eh_frame_hdr)'), newline=NL)
    elf = out / (name + '.elf')
    # One closed static link: lld fails on any unresolved symbol, and garbage
    # collection keeps the .eh_frame entries of the live functions only.
    run([lld, '--gc-sections', '--emit-relocs', '-z', 'max-page-size=4096', '--no-undefined', '-T', script,
         '@' + str(rsp), '-o', elf] + (['--eh-frame-hdr'] if keep_eh else []))
    sections = run([readelf, '-S', '-W', elf])
    assert '.ctors' not in sections and '.tdata' not in sections and '.tbss' not in sections, f'{name}: ctors/TLS present'
    symbols = {l.split()[-1]: int(l.split()[0], 16) for l in run([nm, elf]).splitlines() if len(l.split()) == 3}
    constructor_bytes = symbols['__init_array_end'] - symbols['__init_array_start']
    # Static constructors exist only where the module runs them itself
    # (synth_run_constructors, rhvoice); no firmware or loader runs them.
    assert constructors or constructor_bytes == 0, f'{name}: global constructor present'
    if keep_eh:
        assert symbols['__eh_frame_end'] > symbols['__eh_frame_start'] and symbols['__eh_frame_hdr_end'] > symbols['__eh_frame_hdr_start'], 'no unwind tables'
    disassembly = run([objdump, '-d', '--no-show-raw-insn', elf])
    bad = [l for l in disassembly.splitlines() if re.search(r'\tsyscall\b|%fs:|%gs:|\tint\s+\$0x80', l)]
    assert not bad, f'{name}: kernel/TLS access in image:\n' + '\n'.join(bad[:20])
    if keep_eh:
        report_eh = add_eh_frame_relocations(elf)
        (out / 'eh-relocations.json').write_text(json.dumps(report_eh))
    tts = out / (name + '.tts')
    run([imagetool, 'GenImage', '-c', 'PE', '-t', 'UEFI_DRIVER', '-b', '0x100000', '-o', tts, elf])
    harden(tts)
    report = verify_pe(tts)
    (out / 'pe.json').write_text(json.dumps(report, indent=2))
    return tts, report

# ---------------------------------------------------------------- packs
def lzma_alone(data, lc=3, lp=0, pb=2):
    return lzma.compress(data, format=lzma.FORMAT_ALONE, filters=[{'id': lzma.FILTER_LZMA1,
        'preset': 9 | lzma.PRESET_EXTREME, 'lc': lc, 'lp': lp, 'pb': pb, 'dict_size': 1 << 25, 'nice_len': 273}])

def plane_split(data, ranges):
    data = bytearray(data)
    for offset, n in ranges:
        chunk = bytes(data[offset:offset + n]); words = n // 4
        data[offset:offset + n] = b''.join(chunk[k::4][:words] for k in range(4))
    return bytes(data)

def best_compression(data, ranges):
    transformed = plane_split(data, ranges)
    best = None
    for lc, lp, pb in ((3, 0, 2), (0, 2, 2), (4, 0, 0), (0, 0, 0), (1, 2, 2)):
        packed = lzma_alone(transformed, lc, lp, pb)
        if best is None or len(packed) < len(best): best = packed
    return (1, best) if len(best) < len(data) else (0, data)

def write_pack(path, files):
    """files: list of (name, bytes, plane ranges). Returns statistics."""
    entries = []
    def one(item):
        name, data, ranges = item
        method, packed = best_compression(data, ranges) if data else (0, b'')
        if method == 0: ranges = []
        return name, data, ranges, method, packed
    results = parallel(one, files, workers=8)
    header = bytearray(b'CHSPACK1' + struct.pack('<II', len(results), 0))
    table_size = len(header) + sum(2 + len(n.encode()) + 18 + 8 * len(r) for n, _, r, _, _ in results)
    offset = table_size; blobs = []
    for name, data, ranges, method, packed in results:
        nb = name.encode()
        header += struct.pack('<H', len(nb)) + nb + struct.pack('<BBIIII', method, len(ranges), len(data), len(packed),
                                                                offset, zlib_crc(data))
        for o, n in ranges: header += struct.pack('<II', o, n)
        blobs.append(packed); offset += len(packed)
    assert len(header) == table_size
    path.write_bytes(bytes(header) + b''.join(blobs))
    return dict(files=len(results), raw=sum(len(d) for _, d, _, _, _ in results), packed=path.stat().st_size,
                entries=[dict(name=n, raw=len(d), packed=len(p), method=m, planes=len(r)) for n, d, r, m, p in results])

def zlib_crc(data):
    import zlib
    return zlib.crc32(data) & 0xffffffff

def manifest(path, fields, voices, tts, abi=2):
    lines = [f'# Charon synthesizer module manifest (CharonSynthModule.h, ABI {abi})', f'abi = {abi}']
    lines += [f'{k} = {v}' for k, v in fields]
    lines += [f'voice = {i} | {n} | {l}' for i, n, l in voices]
    lines.append(f'sha256 = {sha256(tts)}')
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')

results = {}
host_inputs = {}
host_cc = [clang, '-O2', '-w']

# ================================================================ eSpeak NG
def build_espeak():
    ex = src / 'espeak'; ex.mkdir(); safe_extract_zip(ESPEAK_ZIP, ex)
    e = ex / f'espeak-ng-{ESPEAK_COMMIT}'
    lib = e / 'src/libespeak-ng'
    units = ['common', 'mnemonics', 'error', 'ieee80', 'compiledata', 'compiledict', 'dictionary', 'encoding',
             'intonation', 'langopts', 'numbers', 'phoneme', 'phonemelist', 'readclause', 'setlengths', 'soundicon',
             'spect', 'ssml', 'synthdata', 'synthesize', 'tr_languages', 'translate', 'translateword', 'voices',
             'wavegen', 'speech', 'espeak_api', 'klatt']
    ucd = sorted((e / 'src/ucd-tools/src').glob('*.c'))
    cfg = build / 'espeak-config'; cfg.mkdir()
    (cfg / 'config.h').write_text('#define PACKAGE_VERSION "1.53.0"\n#define HAVE_MKSTEMP 0\n#define USE_ASYNC 0\n'
        '#define USE_KLATT 1\n#define USE_LIBPCAUDIO 0\n#define USE_LIBSONIC 0\n#define USE_MBROLA 0\n'
        '#define USE_SPEECHPLAYER 0\n#define _CRT_SECURE_NO_WARNINGS\n#define _CRT_NONSTDC_NO_DEPRECATE\n', newline='\n')
    includes = ['-I' + str(cfg), '-I' + str(e / 'src/include'), '-I' + str(e / 'src/include/compat'),
                '-I' + str(e / 'src/ucd-tools/src/include'), '-I' + str(lib), '-DLIBESPEAK_NG_EXPORT=1']
    # -- data, compiled on the host by the unmodified compilers
    host = build / 'espeak-host'; host.mkdir()
    run(host_cc + includes + [lib / f'{u}.c' for u in units] + ucd +
        [synth / 'host/espeak-compile-data.c', '-Wl,binmode.obj', '-ladvapi32', '-o', host / 'espeak-compile-data.exe'])
    data_root = host / 'data'; ng = data_root / 'espeak-ng-data'; (ng / 'voices').mkdir(parents=True)
    shutil.copytree(e / 'espeak-ng-data/lang', ng / 'lang')
    shutil.copytree(e / 'phsource', data_root / 'phsource')
    dictsource = data_root / 'dictsource'; dictsource.mkdir()
    for lang in ('pl', 'en'):
        for p in (e / 'dictsource').glob(f'{lang}_*'): shutil.copy(p, dictsource)
    run([host / 'espeak-compile-data.exe', data_root.as_posix(), dictsource.as_posix() + '/', 'pl', 'en'])
    keep = ['phontab', 'phonindex', 'phondata', 'intonations', 'pl_dict', 'en_dict', 'lang/zlw/pl', 'lang/gmw/en']
    files = [(k, (ng / k).read_bytes(), []) for k in keep]
    # r397: voice variants for Charon's voice families (ABI 3 SetPersona, espeak_module.cpp).
    files += [(f'voices/!v/{v}', (e / 'espeak-ng-data/voices/!v' / v).read_bytes(), []) for v in ESPEAK_VARIANTS]
    # -- module
    out = build / 'espeak'; out.mkdir()
    flags = target + codegen + libc_public + includes + ['-w', '-fwrapv', '-fno-exceptions', '-D_GNU_SOURCE']
    objects = parallel(lambda p: compile_c(p, out / ('e-' + Path(p).stem + '.o'), flags + ['-std=c11']),
                       [lib / f'{u}.c' for u in units] + ucd)
    glue = target + codegen + libc_public + ['-std=c++17', '-fno-exceptions', '-fno-rtti', '-Wall', '-Wextra',
        '-Werror', '-Wno-misleading-indentation', '-I' + str(root / 'src/efi'), '-I' + str(build / 'include'),
        '-I' + str(synth / 'common')] + includes[1:2]
    objects.append(compile_c(synth / 'espeak/espeak_module.cpp', out / 'glue-module.o', glue))
    objects.append(compile_c(synth / 'common/synth_efi.cpp', out / 'glue-efi.o', glue + ['-fshort-wchar']))
    tts, report = link_module('espeak', objects + libc_objects)
    groups = {'espeak-ng-data.chsp': ['phontab', 'phonindex', 'phondata', 'intonations'] + [f'voices/!v/{v}' for v in ESPEAK_VARIANTS],
              'espeak-pl.chsp': ['pl_dict', 'lang/zlw/pl'], 'espeak-en.chsp': ['en_dict', 'lang/gmw/en']}
    stats = {}
    for pack, names in groups.items():
        stats[pack] = write_pack(out / pack, [f for f in files if f[0] in names])
    stats = dict(raw=sum(v['raw'] for v in stats.values()), packed=sum(v['packed'] for v in stats.values()), packs=stats)
    results['espeak'] = dict(pe=report, pack=stats)
    host_inputs['espeak'] = dict(includes=includes, sources=[lib / f'{u}.c' for u in units] + ucd, data=data_root, tree=e)
    return tts, [out / g for g in groups]

# ================================================================ C++ runtime
# libc++, libc++abi and libunwind 23.1.5 (Apache-2.0 WITH LLVM-exception),
# unmodified, configured for one thread, no file system/clock/random device,
# the "C" locale only, and real exceptions: libunwind in bare-metal mode reads
# the image's own .eh_frame/.eh_frame_hdr (linker-script symbols).
LLVM_TAR = work / 'llvm-src-dl/llvm-project-23.1.5.src.tar.xz'
LLVM_SHA256 = 'fb50b1eac93ddeb3aa8dab4cfe324dff74f4a9c2bdee6160c9e36188f8d06a6e'
CONFIG_SITE = '''#ifndef _LIBCPP___CONFIG_SITE
#define _LIBCPP___CONFIG_SITE
#define _LIBCPP_ABI_VERSION 1
#define _LIBCPP_ABI_NAMESPACE __1
#define _LIBCPP_ABI_FORCE_ITANIUM 0
#define _LIBCPP_ABI_FORCE_MICROSOFT 0
#define _LIBCPP_HAS_THREADS 0
#define _LIBCPP_HAS_MONOTONIC_CLOCK 0
#define _LIBCPP_HAS_MUSL_LIBC 1
#define _LIBCPP_HAS_THREAD_API_PTHREAD 0
#define _LIBCPP_HAS_THREAD_API_EXTERNAL 0
#define _LIBCPP_HAS_THREAD_API_WIN32 0
#define _LIBCPP_HAS_THREAD_API_C11 0
#define _LIBCPP_HAS_VENDOR_AVAILABILITY_ANNOTATIONS 0
#define _LIBCPP_HAS_FILESYSTEM 1
#define _LIBCPP_HAS_RANDOM_DEVICE 0
#define _LIBCPP_HAS_LOCALIZATION 1
#define _LIBCPP_HAS_UNICODE 1
#define _LIBCPP_HAS_WIDE_CHARACTERS 1
#define _LIBCPP_HAS_TIME_ZONE_DATABASE 0
#define _LIBCPP_INSTRUMENTED_WITH_ASAN 0
#define _LIBCPP_PSTL_BACKEND_SERIAL
#define _LIBCPP_HARDENING_MODE_DEFAULT _LIBCPP_HARDENING_MODE_NONE
#define _LIBCPP_ASSERTION_SEMANTIC_DEFAULT _LIBCPP_ASSERTION_SEMANTIC_HARDENING_DEPENDENT
#define _LIBCPP_LIBC_PICOLIBC 0
#define _LIBCPP_LIBC_NEWLIB 0
#define _LIBCPP_LIBC_LLVM_LIBC 0
#endif
'''
LIBCXX_SKIP = {'atomic', 'barrier', 'condition_variable', 'condition_variable_destructor', 'future', 'mutex',
               'mutex_destructor', 'shared_mutex', 'thread', 'print', 'text_encoding', 'random', 'chrono',
               'memory_resource', 'regex', 'strstream', 'charconv'}

def build_cxx_runtime():
    assert sha256(LLVM_TAR) == LLVM_SHA256, 'llvm-project archive hash'
    dest = src / 'llvm'; dest.mkdir()
    top = 'llvm-project-23.1.5.src'
    members = [f'{top}/{p}' for p in ('libcxx/include', 'libcxx/src', 'libcxx/vendor', 'libcxx/LICENSE.TXT',
               'libcxxabi/include', 'libcxxabi/src', 'libcxxabi/LICENSE.TXT', 'libunwind/include', 'libunwind/src',
               'libunwind/LICENSE.TXT')]
    run([shutil.which('tar'), '--force-local', '-xJf', LLVM_TAR.as_posix(), '-C', dest.as_posix(), *members])
    llvm_src = dest / top
    inc = build / 'libcxx-include'
    shutil.copytree(llvm_src / 'libcxx/include', inc)
    (inc / '__config_site').write_text(CONFIG_SITE, newline=NL)
    shutil.copy(llvm_src / 'libcxx/vendor/llvm/default_assertion_handler.in', inc / '__assertion_handler')
    cxx_codegen = [f for f in codegen if f not in ('-fno-unwind-tables', '-fno-asynchronous-unwind-tables')]
    cxx_headers = ['-nostdinc++', '-isystem', str(inc), '-isystem', str(llvm_src / 'libcxxabi/include'),
                   '-isystem', str(llvm_src / 'libunwind/include')] + libc_public
    flags = dict(
        cxx=target + cxx_codegen + ['-std=c++23', '-fexceptions', '-funwind-tables', '-w'] + cxx_headers,
        c=target + cxx_codegen + ['-std=c11', '-funwind-tables', '-w'] + libc_public)
    out = build / 'cxxrt'; out.mkdir()
    jobs = []
    for p in sorted((llvm_src / 'libcxx/src').glob('*.cpp')):
        if p.stem in LIBCXX_SKIP: continue
        jobs.append((p, flags['cxx'] + ['-D_LIBCPP_BUILDING_LIBRARY', '-DLIBCXX_BUILDING_LIBCXXABI',
                                        '-I' + str(llvm_src / 'libcxx/src'), '-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES']))
    for p in (llvm_src / 'libcxx/src/ryu').glob('*.cpp'):
        jobs.append((p, flags['cxx'] + ['-D_LIBCPP_BUILDING_LIBRARY', '-I' + str(llvm_src / 'libcxx/src')]))
    abi = ['cxa_aux_runtime', 'cxa_default_handlers', 'cxa_demangle', 'cxa_exception_storage', 'cxa_guard',
           'cxa_handlers', 'cxa_vector', 'cxa_virtual', 'stdlib_exception', 'stdlib_stdexcept', 'stdlib_typeinfo',
           'abort_message', 'fallback_malloc', 'private_typeinfo', 'stdlib_new_delete', 'cxa_exception',
           'cxa_personality']
    for a in abi:
        jobs.append((llvm_src / f'libcxxabi/src/{a}.cpp', flags['cxx'] + ['-D_LIBCXXABI_BUILDING_LIBRARY',
                     '-D_LIBCPP_BUILDING_LIBRARY', '-D_LIBCXXABI_HAS_NO_THREADS', '-DLIBCXXABI_USE_LLVM_UNWINDER',
                     '-D_LIBCXXABI_LINK_PTHREAD_LIB=0', '-I' + str(llvm_src / 'libcxx/src'),
                     '-I' + str(llvm_src / 'libcxxabi/src')]))
    unwind = ['-D_LIBUNWIND_IS_BAREMETAL', '-D_LIBUNWIND_HAS_NO_THREADS', '-D_LIBUNWIND_IS_NATIVE_ONLY', '-DNDEBUG',
              '-I' + str(llvm_src / 'libunwind/include')]
    jobs.append((llvm_src / 'libunwind/src/libunwind.cpp', flags['cxx'] + unwind + ['-fno-exceptions', '-fno-rtti']))
    for c in ('UnwindLevel1.c', 'UnwindLevel1-gcc-ext.c'):
        jobs.append((llvm_src / 'libunwind/src' / c, flags['c'] + unwind))
    for sfile in ('UnwindRegistersSave.S', 'UnwindRegistersRestore.S'):
        jobs.append((llvm_src / 'libunwind/src' / sfile, target + ['-mno-red-zone'] + unwind + libc_public))
    objects = parallel(lambda item: compile_c(item[0], out / (item[0].parent.parent.name + '-' + item[0].stem + '.o'),
                                              item[1], clangxx if item[0].suffix == '.cpp' else clang), jobs)
    archive = build / 'libsynthcxx.a'
    rsp = out / 'objects.rsp'; rsp.write_text(NL.join(o.as_posix() for o in objects), newline=NL)
    run([llvm / 'llvm-ar.exe', 'rcs', archive, '@' + str(rsp)])
    return archive, inc, llvm_src

# ================================================================ RHVoice
RHVOICE_LANGUAGES = {'Polish': {'license'}, 'English': set()}   # names left out of the data pack
RHVOICE_VOICES = {'natan': 'Natan', 'slt': 'Slt'}
RHVOICE_RATE = '16000'   # one quality level; RHVoice falls back to it when 24 kHz is absent

def hts_plane_ranges(data):
    # HTS voice (version 1.0): float32 pdf blocks listed in [POSITION] after
    # [DATA]; trees and windows stay text. Byte-plane only the pdf ranges.
    start = data.find(b'[DATA]\n')
    if start < 0: return []
    base = start + len(b'[DATA]\n'); ranges = []
    for m in re.finditer(rb'^(DURATION_PDF|STREAM_PDF\[\w+\]):(\d+)-(\d+)$', data[:start], re.M):
        a, b = int(m.group(2)), int(m.group(3)) + 1
        n = (b - a) // 4 * 4
        if n: ranges.append((base + a, n))
    return ranges

def rhvoice_data_files():
    files = []; skipped = []; data = RHVOICE / 'data'
    for lang, drop in RHVOICE_LANGUAGES.items():
        for p in sorted((data / 'languages' / lang).rglob('*')):
            if p.is_dir() or '.git' in p.parts: continue
            rel = p.relative_to(data / 'languages' / lang).as_posix()
            if rel in drop or rel == '.git': skipped.append(f'languages/{lang}/{rel}'); continue
            files.append((f'data/languages/{lang}/{rel}', p.read_bytes(), []))
    for voice in RHVOICE_VOICES:
        vdir = data / 'voices' / voice
        for p in sorted(vdir.rglob('*')):
            if p.is_dir() or '.git' in p.parts: continue
            rel = p.relative_to(vdir).as_posix()
            keep = rel in ('voice.info', 'voice.params') or rel in (f'{RHVOICE_RATE}/voice.data', f'{RHVOICE_RATE}/bpf.txt',
                                                                    f'{RHVOICE_RATE}/eq.txt')
            if not keep: skipped.append(f'voices/{voice}/{rel}'); continue
            raw = p.read_bytes()
            files.append((f'data/voices/{voice}/{rel}', raw, hts_plane_ranges(raw) if rel.endswith('voice.data') else []))
    return files, skipped

def rhvoice_sources():
    tree = src / 'rhvoice'; tree.mkdir()
    shutil.copytree(RHVOICE / 'src', tree / 'src', ignore=shutil.ignore_patterns('.git'))
    for patch in sorted((synth / 'rhvoice/patches').glob('*.patch')):
        run(['git', '-c', 'core.autocrlf=false', 'apply', '-p1', patch], cwd=tree)
    s = tree / 'src'
    core = sorted(p for p in (s / 'core').glob('*.cpp') if p.stem not in ('emoji_data', 'unidata', 'mage_hts_engine_impl'))
    return s, core + [s / 'lib/lib.cpp'], sorted((s / 'hts_engine').glob('HTS_*.c'))

def rhvoice_config(directory, version='1.18.5'):
    (directory / 'core').mkdir(parents=True)
    (directory / 'config.h').write_text('#pragma once\n#ifndef RHVOICE_GLOBAL_CONFIG_INCLUDED\n#define RHVOICE_GLOBAL_CONFIG_INCLUDED\n'
        '#define ENABLE_SONIC 0\n#define ENABLE_PKG 0\n'
        f'const char VERSION[] = "{version}";\nconst char CONFIG_PATH[] = "";\nconst char DATA_PATH[] = "";\n#endif\n', newline=NL)
    (directory / 'core/config.h').write_text('#include "../config.h"\n', newline=NL)

def build_rhvoice():
    s, cxx_sources, c_sources = rhvoice_sources()
    cfg = build / 'rhvoice-config'; rhvoice_config(cfg)
    cxxrt, cxx_inc, llvm_src = build_cxx_runtime()
    out = build / 'rhvoice'; out.mkdir()
    cxx_codegen = [f for f in codegen if f not in ('-fno-unwind-tables', '-fno-asynchronous-unwind-tables')]
    includes = ['-I' + str(cfg), '-I' + str(s / 'include'), '-I' + str(s / 'third-party/utf8'),
                '-I' + str(s / 'third-party/rapidxml'), '-I' + str(s / 'hts_engine')]
    cxx_headers = ['-nostdinc++', '-isystem', str(cxx_inc), '-isystem', str(llvm_src / 'libcxxabi/include')] + libc_public
    cxx = target + cxx_codegen + ['-std=c++17', '-fexceptions', '-funwind-tables', '-w', '-D_GNU_SOURCE',
          '-D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION', '-D_LIBCPP_ENABLE_CXX20_REMOVED_NEGATORS'] + cxx_headers + includes
    cc = target + cxx_codegen + ['-std=c11', '-funwind-tables', '-w', '-D_GNU_SOURCE'] + libc_public + includes
    objects = parallel(lambda p: compile_c(p, out / ('r-' + p.stem + '.o'), cxx, clangxx), cxx_sources)
    objects += parallel(lambda p: compile_c(p, out / ('h-' + p.stem + '.o'), cc, clang), c_sources)
    glue = target + cxx_codegen + ['-std=c++17', '-fexceptions', '-Wall', '-Wextra', '-Werror'] + cxx_headers + [
        '-I' + str(root / 'src/efi'), '-I' + str(build / 'include'), '-I' + str(synth / 'common'),
        '-D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION', '-D_LIBCPP_ENABLE_CXX20_REMOVED_NEGATORS',
        '-isystem', str(cfg), '-isystem', str(s / 'include'), '-isystem', str(s / 'third-party/utf8'),
        '-isystem', str(s / 'third-party/rapidxml')]
    objects.append(compile_c(synth / 'rhvoice/rhvoice_module.cpp', out / 'glue-module.o', glue))
    objects.append(compile_c(synth / 'common/synth_efi.cpp', out / 'glue-efi.o',
                             target + codegen + libc_public + ['-std=c++17', '-fno-exceptions', '-fno-rtti', '-Wall',
                             '-Wextra', '-Werror', '-fshort-wchar', '-I' + str(root / 'src/efi'), '-I' + str(build / 'include'),
                             '-I' + str(synth / 'common')]))
    objects.append(compile_c(synth / 'common/synth_ctors.c', out / 'glue-ctors.o', target + codegen + libc_public + ['-std=c11']))
    tts, report = link_module('rhvoice', objects + [cxxrt] + libc_objects, keep_eh=True, constructors=True)
    files, skipped = rhvoice_data_files()
    # One pack per voice: its language and its voice (ABI 2 Prepare loads one).
    groups = {'rhvoice-pl.chsp': ('data/languages/Polish/', 'data/voices/natan/'),
              'rhvoice-en.chsp': ('data/languages/English/', 'data/voices/slt/')}
    per = {pack: write_pack(out / pack, [f for f in files if f[0].startswith(prefixes)]) for pack, prefixes in groups.items()}
    assert sum(v['files'] for v in per.values()) == len(files)
    stats = dict(raw=sum(v['raw'] for v in per.values()), packed=sum(v['packed'] for v in per.values()), packs=per)
    stats['skipped'] = skipped
    upstream = [RHVOICE / 'data/languages' / l for l in RHVOICE_LANGUAGES] + [RHVOICE / 'data/voices' / v for v in RHVOICE_VOICES]
    stats['upstream_bytes'] = sum(p.stat().st_size for d in upstream for p in d.rglob('*') if p.is_file() and '.git' not in p.parts)
    results['rhvoice'] = dict(pe=report, pack=stats)
    host_inputs['rhvoice'] = dict(tree=s, cxx=cxx_sources, c=c_sources, files=files, config=cfg)
    return tts, [out / g for g in groups]

# EFI headers are included as <efi/...> (as in build-charon-windows.py).
shutil.copytree(root / 'src/efi/haiku', build / 'include/efi')
libc_objects = build_libc()
products = {}
if 'espeak' in targets: products['espeak'] = build_espeak()
if 'rhvoice' in targets: products['rhvoice'] = build_rhvoice()

# ---------------------------------------------------------------- staging
MANIFESTS = {
    'espeak': ([('name', 'eSpeak NG'), ('version', f'1.53.0-dev {ESPEAK_COMMIT[:12]}'), ('module', 'espeak.tts'),
                ('data', 'espeak'), ('licence', 'GPL-3.0-or-later')],
               [('espeak-pl', 'eSpeak NG polski', 'pl'), ('espeak-en', 'eSpeak NG English', 'en')]),
    'rhvoice': ([('name', 'RHVoice'), ('version', '1.18.5'), ('module', 'rhvoice.tts'), ('data', 'rhvoice'),
                 ('licence', 'GPL-2.0-or-later')],
                [('rhvoice-natan', 'Natan', 'pl'), ('rhvoice-slt', 'Slt', 'en')]),
}

LICENCES_TXT = '''Charon synthesizer modules (r393): licences
===========================================

Both modules are optional add-ons. Charon works without them (TVox).

espeak.tts + espeak/{{espeak-ng-data,espeak-pl,espeak-en}}.chsp
  eSpeak NG {espeak} (github.com/espeak-ng/espeak-ng, GPL-3.0-or-later;
  licences/espeak-ng-COPYING.txt). Algorithms unmodified; data compiled from
  the same commit by its own compilers (phontab, phonindex, phondata,
  intonations, pl_dict, en_dict, lang/zlw/pl, lang/gmw/en), LZMA-compressed.
  The whole module is distributed under GPL-3.0-or-later.

rhvoice.tts + rhvoice/{{rhvoice-pl,rhvoice-en}}.chsp
  RHVoice 1.18.5 core (github.com/RHVoice/RHVoice, commit {rhvoice}):
  LGPL-2.1-or-later (licences/lgpl-2.1.txt); its HTS engine files and the
  language data are GPL-2.0-or-later (licences/gpl-2.0.txt,
  licences/rhvoice-Polish-license.txt). The module as a whole:
  GPL-2.0-or-later. Built without MAGE (MAGE would make it GPL-3.0).
  Two small source patches (Boost-free voice id, standard `typename`):
  outputs/nvda-haiku/src/synth/rhvoice/patches in the Charon sources.
  Voice Natan (Polish, github.com/RHVoice/natan-pol, commit {natan}):
  CC0-1.0 / public domain (licences/natan-LICENSE.txt). Team: Edwin Tarka
  (speaker), Tomasz Bilecki (audio editing), Zvonimir Stanecic (training).
  Voice Slt (English, github.com/RHVoice/slt-eng, commit {slt}): CMU ARCTIC
  licence (licences/cmu_us_slt_arctic-COPYING.txt), Carnegie Mellon
  University; RHVoice voice by Artem Plaksin, Zvonimir Stanecic.
  Voice data is shipped byte-identical to upstream (16 kHz model only), only
  losslessly compressed: LZMA, with a reversible float32 byte-plane
  reordering of the HTS pdf blocks in voice.data; every file is checked
  against its CRC-32 after decompression. Neither voice has a
  no-derivatives or non-commercial condition (Alan was not used because its
  repository carries no licence statement).

Runtime parts in both modules
  musl 1.2.5 (MIT, licences/musl-COPYRIGHT.txt), LZMA SDK 26.06 LzmaDec
  (public domain, licences/lzma-sdk.txt). rhvoice.tts also contains libc++,
  libc++abi and libunwind of LLVM 23.1.5 (Apache-2.0 WITH LLVM-exception,
  licences/llvm-LICENSE.txt). Charon glue: GPL-2.0-or-later (espeak glue:
  GPL-3.0-or-later).

Corresponding source: the pinned archives listed above plus
outputs/nvda-haiku/src/synth and tools/build-charon-synths.py.
'''

def git_head(path):
    try: return subprocess.run(['git', '-C', str(path), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    except Exception: return 'unknown'

def stage(directory):
    synths = Path(directory).resolve() / 'EFI/HAK/synths'; synths.mkdir(parents=True, exist_ok=False)
    for name, (tts, packs) in products.items():
        shutil.copy(tts, synths / f'{name}.tts'); (synths / name).mkdir()
        for pack in packs: shutil.copy(pack, synths / name)
        fields, voices = MANIFESTS[name]
        manifest(synths / f'{name}.manifest', fields, voices, synths / f'{name}.tts', abi=3 if name == 'espeak' else 2)   # r397: eSpeak has SetPersona
    lic = synths / 'licences'; lic.mkdir()
    sources = {'musl-COPYRIGHT.txt': musl / 'COPYRIGHT', 'lzma-sdk.txt': src / 'lzma-sdk/DOC/lzma-sdk.txt'}
    if 'espeak' in products: sources['espeak-ng-COPYING.txt'] = host_inputs['espeak']['tree'] / 'COPYING'
    if 'rhvoice' in products:
        sources.update({'gpl-2.0.txt': RHVOICE / 'licenses/gpl-2.0.txt', 'lgpl-2.1.txt': work / 'rhvoice-dl/lgpl-2.1.txt',
                        'rhvoice-Polish-license.txt': RHVOICE / 'data/languages/Polish/license',
                        'natan-LICENSE.txt': RHVOICE / 'data/voices/natan/LICENSE',
                        'cmu_us_slt_arctic-COPYING.txt': work / 'rhvoice-dl/cmu_us_slt_arctic-COPYING.txt',
                        'slt-README.md': RHVOICE / 'data/voices/slt/README.md',
                        'llvm-LICENSE.txt': src / 'llvm/llvm-project-23.1.5.src/libcxx/LICENSE.TXT'})
    for k, p in sources.items(): shutil.copy(p, lic / k)
    (synths / 'LICENCES.txt').write_text(LICENCES_TXT.format(espeak=ESPEAK_COMMIT, rhvoice=git_head(RHVOICE),
        natan=git_head(RHVOICE / 'data/voices/natan'), slt=git_head(RHVOICE / 'data/voices/slt')), encoding='utf-8', newline=NL)
    return synths

# ---------------------------------------------------------------- host tests
def wav_samples(path):
    import array
    return array.array('h', Path(path).read_bytes()[44:])

def host_tests(stage_dir):
    """Runs the shipped .tts files from the staging folder in a Windows
    process (PE loader + fake boot services, src/synth/host/tts-host-loader.cpp)
    and compares their audio with hosted builds of the same engine sources."""
    tests = build / 'host-tests'; tests.mkdir()
    wavs = Path(stage_dir).resolve() / 'test-wav'; wavs.mkdir()
    loader = tests / 'tts-host-loader.exe'
    run([clangxx, '-O2', '-std=c++17', '-D_CRT_SECURE_NO_WARNINGS', '-w', synth / 'host/tts-host-loader.cpp', '-o', loader])
    report = {}
    sentences = {'pl': 'Menu rozruchu, pozycja 2 z 5.', 'en': 'Boot menu, item 2 of 5.'}
    for name, (tts, _) in products.items():
        symbols = {l.split()[-1]: l.split()[0] for l in run([nm, tts.with_suffix('.elf')]).splitlines() if len(l.split()) == 3}
        report[name] = {}
        # One process per scenario: each voice prepared alone (start/Prepare
        # time, memory of that voice), then both voices with WAV output.
        for voices_arg, out in (('0', '-'), ('1', '-'), ('0,1', str(wavs))):
            result = subprocess.run([str(loader), str(Path(stage_dir).resolve()), name, out, voices_arg,
                                     symbols['synth_diagnostic'], symbols['synth_stderr_tail']], capture_output=True, text=True)
            log.write(result.stdout + result.stderr)
            lines = [json.loads(l) for l in result.stdout.splitlines() if l.startswith('{')]
            report[name]['voices ' + voices_arg] = dict(returncode=result.returncode, lines=lines)
            assert result.returncode == 0 and result.stdout.strip().endswith('PASS'), \
                f'{name} host test ({voices_arg}) failed:' + NL + result.stdout + result.stderr
    # hosted references (same sources, MSVC C/C++ runtime)
    if 'espeak' in products:
        h = host_inputs['espeak']
        exe = tests / 'espeak-reference.exe'
        run(host_cc + h['includes'] + h['sources'] + [synth / 'host/espeak-reference.c', '-Wl,binmode.obj', '-ladvapi32', '-o', exe])
        run([exe, h['data'].as_posix(), 'pl', sentences['pl'], tests / 'espeak-pl.raw', 'en', sentences['en'], tests / 'espeak-en.raw'])
    if 'rhvoice' in products:
        h = host_inputs['rhvoice']; refdata = tests / 'rhvoice-data'
        for rel, data, _ in h['files']:
            (refdata / rel).parent.mkdir(parents=True, exist_ok=True); (refdata / rel).write_bytes(data)
        (tests / 'rhvoice-config').mkdir()
        s = h['tree']; obj = tests / 'rhvoice-obj'; obj.mkdir()
        flags = ['-O2', '-w', '-DWIN32', '-D_CRT_SECURE_NO_WARNINGS', '-DNOMINMAX', '-I' + str(h['config']),
                 '-I' + str(s / 'include'), '-I' + str(s / 'third-party/utf8'), '-I' + str(s / 'third-party/rapidxml'),
                 '-I' + str(s / 'hts_engine')]
        cxxf = flags + ['-std=c++17', '-D_HAS_AUTO_PTR_ETC=1', '-fms-compatibility', '-fdelayed-template-parsing']
        objs = parallel(lambda p: compile_c(p, obj / (('x-' if p.suffix == '.cpp' else 'c-') + p.stem + '.o'), cxxf if p.suffix == '.cpp' else flags + ['-std=c11'],
                                            clangxx if p.suffix == '.cpp' else clang), h['cxx'] + h['c'])
        exe = tests / 'rhvoice-reference.exe'
        run([clangxx] + cxxf + [synth / 'host/rhvoice-reference.cpp'] + objs + ['-o', exe])
        run([exe, refdata / 'data', tests / 'rhvoice-config', 'Natan', sentences['pl'], tests / 'rhvoice-pl.raw',
             'Slt', sentences['en'], tests / 'rhvoice-en.raw'])
    for name in products:
        for lang in ('pl', 'en'):
            ref = __import__('array').array('h', (tests / f'{name}-{lang}.raw').read_bytes())
            mod = wav_samples(wavs / f'{name}-{lang}.wav')
            same = ref == mod
            report[name][f'reference_{lang}'] = dict(samples=len(mod), reference_samples=len(ref), identical=same)
            assert same, f'{name} {lang}: module audio differs from the hosted reference build'
    (Path(stage_dir).resolve() / 'test-wav/host-test-results.json').write_text(json.dumps(report, indent=2))
    results['host_tests'] = report
    return report

if stage_arg:
    stage(stage_arg)
    if run_tests: host_tests(stage_arg)
(build / 'results.json').write_text(json.dumps(results, indent=2))
for k, v in ((k, results[k]) for k in products):
    print(k, 'tts', v['pe']['bytes'], 'bytes; pack', v['pack']['raw'], '->', v['pack']['packed'], 'bytes')
print('PASS build-charon-synths', ' '.join(products), '(host tests passed)' if run_tests and stage_arg else '')
