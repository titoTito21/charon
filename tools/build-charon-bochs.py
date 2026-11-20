"""Build CharonPC (CHARONPC.EFI): the Bochs 3.1 PC emulator as a UEFI application.

  python build-charon-bochs.py NEW_BUILD_DIRECTORY [--stage NEW_STAGING_DIRECTORY]

CHARONPC.EFI is started by CharonDOS (command WIN [profile]) with LoadOptions
"config=<path>\\CHARONPC.CFG [pl]", or alone, when it reads CHARONPC.CFG from
its own folder (format: src/pc/PLAN.txt). It runs an emulated i440FX PC
(Pentium II class CPU by default, FPU, interpreter only) from disk images on
the boot volume; text modes are written to the UEFI console for Charon's
reader, COM1 is a Braille 'n Speak synthesizer spoken with Charon's voice.

Inputs (below the repository container `kont`):
  work/bochs-dl/bochs-3.1.tar.gz   Bochs 3.1 (LGPL-2.1-or-later), taken from
      bochs-3.1-1.src.rpm of the GitHub release REL_3_1_FINAL (asset digest
      sha256:7e4a84a6...); content identical to the SourceForge bochs-3.1.tar.gz.
      Only sources are compiled; configure and every other script of the
      archive are never run: src/pc/bochs-config.h replaces config.h and the
      file lists below replace the Makefiles.
  work/musl-1.2.5.tar.gz, work/llvm-src-dl/llvm-project-23.1.5.src.tar.xz
      the C library and libc++/libc++abi, as tools/build-charon-synths.py
      (here without exceptions: neither Bochs nor the glue throws).
Toolchain: LLVM 23.1.5 (work/llvm-23.1.5/bin, CHARON_LLVM), AUDK ImageTool
(CHARON_IMAGETOOL). Charon's glue (src/pc) is compiled with
-Wall -Wextra -Werror; Bochs, musl and libc++ sources with warnings off.
Two lines are added to Bochs's plugin.h/plugin.cc (the "charon" display
library entry); nothing else of Bochs is changed.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import hashlib, json, os, re, shutil, struct, subprocess, sys, tarfile

root = Path(__file__).resolve().parents[1]
container = root.parents[1]
work = container / 'work'
llvm = Path(os.environ.get('CHARON_LLVM', work / 'llvm-23.1.5/bin'))
imagetool = Path(os.environ.get('CHARON_IMAGETOOL', work / 'ha/BaseTools/Bin/Win64/ImageTool.exe'))
clang = str(llvm / 'clang.exe'); clangxx = str(llvm / 'clang++.exe')
lld = str(llvm / 'ld.lld.exe'); nm = str(llvm / 'llvm-nm.exe'); readelf = str(llvm / 'llvm-readelf.exe')
objdump = str(llvm / 'llvm-objdump.exe'); ar = str(llvm / 'llvm-ar.exe')
pcsrc = root / 'src/pc'
synth_common = root / 'src/synth/common'
BOCHS_TGZ = work / 'bochs-dl/bochs-3.1.tar.gz'
BOCHS_SHA256 = '7d11839213ad49f8edafd8ad24036f42d0a5c4e80072eecab4e9fc70eecbe7d3'
MUSL_TGZ = work / 'musl-1.2.5.tar.gz'
MUSL_SHA256 = 'a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4'
LLVM_TAR = work / 'llvm-src-dl/llvm-project-23.1.5.src.tar.xz'
LLVM_SHA256 = 'fb50b1eac93ddeb3aa8dab4cfe324dff74f4a9c2bdee6160c9e36188f8d06a6e'
NL = '\n'

argv = sys.argv[1:]
def option(name):
    if name in argv:
        i = argv.index(name); value = argv[i + 1]; del argv[i:i + 2]; return value
    return None
stage_arg = option('--stage')
if len(argv) != 1: sys.exit(__doc__)
build = Path(argv[0]).resolve(); build.mkdir(parents=True, exist_ok=False)
log = (build / 'build.log').open('w', encoding='utf-8')

def run(command, cwd=None):
    command = list(map(str, command))
    log.write('$ ' + ' '.join(command) + '\n'); log.flush()
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, errors='replace')
    log.write(result.stdout + result.stderr); log.flush()
    if result.returncode:
        sys.stderr.write(result.stdout[-4000:] + result.stderr[-8000:])
        raise SystemExit(f'FAILED ({result.returncode}): {command[0]} ... {command[-1]}')
    return result.stdout

def sha256(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def parallel(function, items, workers=os.cpu_count() or 8):
    with ThreadPoolExecutor(max_workers=workers) as pool: return list(pool.map(function, items))
# Object cache (CHARON_PC_CACHE, default work/charon-pc-objcache) for sources
# of the pinned archives only: the key is the source, the flags (build path
# removed) and Bochs's config.h. Charon's own sources are always compiled.
cache = Path(os.environ.get('CHARON_PC_CACHE', work / 'charon-pc-objcache'))
def compile_one(source, output, flags, compiler=None):
    compiler = compiler or (clangxx if Path(source).suffix in ('.cc', '.cpp') else clang)
    source = Path(source)
    # File names compiled into the image (assert messages) do not name the build directory.
    flags = ['-ffile-prefix-map=' + str(build) + '=charonpc', *flags]
    key = None
    if source.is_relative_to(src) and os.environ.get('CHARON_PC_CACHE', '') != 'off':
        h = hashlib.sha256(source.read_bytes())
        h.update(' '.join(map(str, [compiler, *flags])).replace(str(build), '<build>').encode())
        h.update((src / 'bochs-3.1/config.h').read_bytes())
        key = cache / (h.hexdigest() + '.o')
        if key.exists():
            shutil.copyfile(key, output); return output
    run([compiler, *flags, '-c', source, '-o', output])
    if key:
        cache.mkdir(exist_ok=True); tmp = key.with_suffix('.tmp' + str(os.getpid()) + output.name)
        shutil.copyfile(output, tmp); os.replace(tmp, key)
    return output
def archive(name, objects):
    out = build / name; rsp = build / (name + '.rsp')
    rsp.write_text(NL.join(Path(o).as_posix() for o in objects), newline=NL)
    run([ar, 'rcs', out, '@' + str(rsp)]); return out

# ---------------------------------------------------------------- sources
src = build / 'src'; src.mkdir()
for archive_path, digest in ((BOCHS_TGZ, BOCHS_SHA256), (MUSL_TGZ, MUSL_SHA256)):
    assert sha256(archive_path) == digest, f'{archive_path.name}: hash'
    with tarfile.open(archive_path) as t: t.extractall(src, filter='data')
bochs = src / 'bochs-3.1'; musl = src / 'musl-1.2.5'
shutil.copy(pcsrc / 'bochs-config.h', bochs / 'config.h')
(bochs / 'bxversion.h').write_text('#define VERSION "3.1"\n#define VER_DEVFLAG 0\n'
    '#define REL_STRING "CharonPC build of Bochs 3.1"\n#define REL_TIMESTAMP "2026-08-23"\n', newline=NL)

def edit(path, old, new):
    text = path.read_text(encoding='latin-1')
    assert text.count(old) == 1, (path, old)
    path.write_text(text.replace(old, new), encoding='latin-1', newline='')
# The "charon" display library (src/pc/pc_gui.cc) in Bochs's static plugin table.
edit(bochs / 'plugin.h', 'PLUGIN_ENTRY_FOR_GUI_MODULE(x);', 'PLUGIN_ENTRY_FOR_GUI_MODULE(x);\nPLUGIN_ENTRY_FOR_GUI_MODULE(charon);')
edit(bochs / 'plugin.cc', '#if BX_WITH_NOGUI\n  BUILTIN_GUI_PLUGIN_ENTRY(nogui),',
     '#if BX_WITH_CHARON\n  BUILTIN_GUI_PLUGIN_ENTRY(charon),\n#endif\n#if BX_WITH_NOGUI\n  BUILTIN_GUI_PLUGIN_ENTRY(nogui),')

# ---------------------------------------------------------------- flags
target = ['--target=x86_64-unknown-linux-musl']
codegen = ['-O2', '-fno-stack-protector', '-fno-pic', '-fno-pie', '-mcmodel=large', '-mno-red-zone',
           '-ffunction-sections', '-fdata-sections', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
           '-fno-plt', '-fno-jump-tables']
gen = build / 'musl-gen'; (gen / 'bits').mkdir(parents=True)
sed = shutil.which('sed') or r'C:\Program Files\Git\usr\bin\sed.exe'
(gen / 'bits/alltypes.h').write_text(run([sed, '-f', musl / 'tools/mkalltypes.sed', musl / 'arch/x86_64/bits/alltypes.h.in',
                                          musl / 'include/alltypes.h.in']), newline=NL)
syscall_in = (musl / 'arch/x86_64/bits/syscall.h.in').read_text()
(gen / 'bits/syscall.h').write_text(syscall_in + ''.join(l.replace('__NR_', 'SYS_') + NL
                                    for l in syscall_in.splitlines() if '__NR_' in l), newline=NL)
(gen / 'version.h').write_text('#define VERSION "1.2.5"\n', newline=NL)
override = synth_common / 'synth-musl-arch'
libc_public = ['-nostdinc', '-isystem', str(gen), '-isystem', str(musl / 'arch/x86_64'),
               '-isystem', str(musl / 'arch/generic'), '-isystem', str(musl / 'include')]
musl_internal = ['-I' + str(override), '-I' + str(musl / 'arch/x86_64'), '-I' + str(musl / 'arch/generic'), '-I' + str(gen),
                 '-I' + str(musl / 'src/include'), '-I' + str(musl / 'src/internal'), '-I' + str(musl / 'include')]
musl_flags = target + codegen + ['-std=c99', '-ffreestanding', '-nostdinc', '-D_XOPEN_SOURCE=700', '-w',
    '-fexcess-precision=standard', '-frounding-math', '-fno-strict-aliasing'] + musl_internal
glue_warn = ['-Wall', '-Wextra', '-Werror']

# ---------------------------------------------------------------- libc
# Pure-computation musl directories; the operating-system side (files,
# descriptors, time, process) is src/pc/pc_posix.c over pc_efi.cpp.
MUSL_DIRS = ['string', 'ctype', 'multibyte', 'stdlib', 'math', 'locale', 'errno', 'stdio', 'prng', 'search',
             'internal', 'env', 'exit', 'complex', 'fenv', 'setjmp', 'time', 'network']
MUSL_EXCLUDE = {
    'stdio': {'fopen', 'freopen', 'stdout', 'stderr', 'stdin', '__stdio_close', '__stdio_read', '__stdio_seek',
              '__stdio_write', '__stdout_write', '__fdopen', 'popen', 'pclose', 'tmpfile', 'tmpnam', 'tempnam',
              'remove', 'rename', '__fopen_rb_ca', 'fopencookie', 'fgetc', 'fputc', 'getc', 'getchar', 'putc',
              'putchar', '__lockfile', 'flockfile', 'ftrylockfile', 'funlockfile', 'vdprintf', 'dprintf'},
    'locale': {'locale_map', 'dcngettext', 'textdomain', 'bind_textdomain_codeset', 'setlocale', 'catopen',
               'catclose', 'catgets'},
    'internal': {'defsysinfo', 'emulate_wait4', 'procfdname', 'syscall_ret', 'vdso', 'libc_'},
    'env': {'__init_tls', '__libc_start_main', '__reset_tls', '__stack_chk_fail', 'secure_getenv'},
    'exit': {'abort', 'abort_lock', 'exit', '_Exit', 'atexit', 'at_quick_exit', 'quick_exit'},
    'stdlib': {'system'},
}
MUSL_ONLY = {'network': {'htonl', 'htons', 'ntohl', 'ntohs'},
             'time': {'__month_to_secs', '__secs_to_tm', '__tm_to_secs', '__year_to_secs', 'asctime', 'asctime_r',
                      'ctime', 'ctime_r', 'difftime', 'gmtime', 'gmtime_r', 'timegm', 'strftime', 'wcsftime'}}
def musl_sources():
    out = []
    for d in MUSL_DIRS:
        base = musl / 'src' / d; arch = base / 'x86_64'
        arch_files = {p.stem: p for p in arch.glob('*') if p.suffix in ('.c', '.s', '.S')} if arch.is_dir() else {}
        for p in sorted(base.glob('*.c')):
            if p.stem in MUSL_EXCLUDE.get(d, set()) or p.stem in arch_files: continue
            if d in MUSL_ONLY and p.stem not in MUSL_ONLY[d]: continue
            out.append(p)
        out += sorted(arch_files.values())
    return out

def build_libc():
    out = build / 'libc'; out.mkdir()
    def one(item):
        i, s = item
        return compile_one(s, out / f'm{i:04d}-{s.stem}.o', musl_flags if s.suffix == '.c' else target + ['-mno-red-zone'])
    objects = parallel(one, list(enumerate(musl_sources())))
    own = [(synth_common / 'synth_malloc.c', musl_flags + ['-I' + str(synth_common)]),
           (synth_common / 'synth_coro.c', musl_flags + ['-I' + str(synth_common)]),
           (synth_common / 'synth_coro_x64.S', target + ['-mno-red-zone']),
           (synth_common / 'synth_ctors.c', target + codegen + libc_public + ['-std=c11']),
           (pcsrc / 'pc_posix.c', [f for f in musl_flags if f != '-w'] + glue_warn + ['-std=c11', '-Wno-unused-parameter',
                                  '-I' + str(synth_common), '-I' + str(pcsrc)])]
    objects += parallel(lambda item: compile_one(item[0], out / ('c-' + item[0].stem + '.o'), item[1]), own)
    return archive('libpcc.a', objects)

# ---------------------------------------------------------------- C++ runtime
# libc++ / libc++abi 23.1.5 (Apache-2.0 WITH LLVM-exception), unmodified,
# one thread, no exceptions (cxa_noexception.cpp), the "C" locale only.
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
#define _LIBCPP_HAS_FILESYSTEM 0
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
               'memory_resource', 'regex', 'strstream', 'charconv', 'filesystem', 'call_once', 'stacktrace'}
LIBCXXABI = ['cxa_aux_runtime', 'cxa_default_handlers', 'cxa_demangle', 'cxa_exception_storage', 'cxa_guard',
             'cxa_handlers', 'cxa_vector', 'cxa_virtual', 'stdlib_exception', 'stdlib_stdexcept', 'stdlib_typeinfo',
             'abort_message', 'fallback_malloc', 'private_typeinfo', 'stdlib_new_delete', 'cxa_noexception']

def build_cxx_runtime():
    assert sha256(LLVM_TAR) == LLVM_SHA256, 'llvm-project archive hash'
    dest = src / 'llvm'; dest.mkdir()
    top = 'llvm-project-23.1.5.src'
    members = [f'{top}/{p}' for p in ('libcxx/include', 'libcxx/src', 'libcxx/vendor', 'libcxx/LICENSE.TXT',
               'libcxxabi/include', 'libcxxabi/src', 'libcxxabi/LICENSE.TXT', 'libunwind/include')]
    run([shutil.which('tar'), '--force-local', '-xJf', LLVM_TAR.as_posix(), '-C', dest.as_posix(), *members])
    llvm_src = dest / top
    inc = build / 'libcxx-include'
    shutil.copytree(llvm_src / 'libcxx/include', inc)
    (inc / '__config_site').write_text(CONFIG_SITE, newline=NL)
    shutil.copy(llvm_src / 'libcxx/vendor/llvm/default_assertion_handler.in', inc / '__assertion_handler')
    headers = ['-nostdinc++', '-isystem', str(inc), '-isystem', str(llvm_src / 'libcxxabi/include'),
               '-isystem', str(llvm_src / 'libunwind/include')] + libc_public
    cxx = target + codegen + ['-std=c++23', '-fno-exceptions', '-w'] + headers
    out = build / 'cxxrt'; out.mkdir()
    jobs = []
    for p in sorted((llvm_src / 'libcxx/src').glob('*.cpp')):
        if p.stem in LIBCXX_SKIP: continue
        jobs.append((p, cxx + ['-D_LIBCPP_BUILDING_LIBRARY', '-DLIBCXX_BUILDING_LIBCXXABI',
                               '-I' + str(llvm_src / 'libcxx/src'), '-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES']))
    for p in (llvm_src / 'libcxx/src/ryu').glob('*.cpp'):
        jobs.append((p, cxx + ['-D_LIBCPP_BUILDING_LIBRARY', '-I' + str(llvm_src / 'libcxx/src')]))
    for a in LIBCXXABI:
        jobs.append((llvm_src / f'libcxxabi/src/{a}.cpp', cxx + ['-D_LIBCXXABI_BUILDING_LIBRARY',
                     '-D_LIBCPP_BUILDING_LIBRARY', '-D_LIBCXXABI_HAS_NO_THREADS', '-D_LIBCXXABI_NO_EXCEPTIONS',
                     '-I' + str(llvm_src / 'libcxx/src'), '-I' + str(llvm_src / 'libcxxabi/src')]))
    objects = parallel(lambda item: compile_one(item[0], out / (item[0].parent.parent.name + '-' + item[0].stem + '.o'),
                                                item[1], clangxx), jobs)
    return archive('libpccxx.a', objects), inc, llvm_src

# ---------------------------------------------------------------- Bochs
B = bochs
BOCHS_TOP = ['logio', 'main', 'config', 'pc_system', 'osdep', 'plugin', 'crc']
BOCHS_DIRS = {
    'memory': ['memory', 'memory_stub', 'misc_mem'],
    'gui': ['keymap', 'gui', 'siminterface', 'paramtree', 'textconfig'],
    'iodev': ['devices', 'virt_timer', 'slowdown_timer', 'pic', 'pit', 'fw_cfg', 'serial', 'parallel', 'floppy',
              'keyboard', 'biosdev', 'cmos', 'harddrv', 'dma', 'unmapped', 'extfpuirq', 'speaker', 'ioapic',
              'pci', 'pci2isa', 'pci_ide', 'acpi', 'hpet', 'acpi_tables', 'pit82c54', 'scancodes', 'serial_raw'],
    'iodev/display': ['vga', 'vgacore', 'ddc'],
    'iodev/hdimage': ['hdimage', 'crc32c', 'vbox', 'vmware3', 'vmware4', 'vpc', 'vhdx', 'vvfat', 'cdrom', 'cdrom_misc'],
}
def bochs_sources():
    out = [B / f'{n}.cc' for n in BOCHS_TOP]
    for d, names in BOCHS_DIRS.items(): out += [B / d / f'{n}.cc' for n in names]
    cpu_mk = (B / 'cpu/Makefile.in').read_text(encoding='latin-1')
    def objs(var):
        m = re.search(rf'^{var}\s*=(.*?)(?:\n\s*\n|\n[A-Z#])', cpu_mk, re.S | re.M)
        return re.findall(r'([\w/]+)\.o', m.group(1))
    out += [B / 'cpu' / f'{o}.cc' for o in objs('OBJS') + objs('OBJS64')]
    out += sorted((B / 'cpu/fpu').glob('*.cc'))
    out += sorted((B / 'cpu/cpudb').glob('*/*.cc'))
    out += sorted((B / 'cpu/softfloat3e').glob('*.cc')) + sorted((B / 'cpu/softfloat3e/8086-SSE').glob('*.cc'))
    missing = [p for p in out if not p.exists()]
    assert not missing, missing
    return out

def build_bochs(cxx_inc, llvm_src):
    out = build / 'bochs'; out.mkdir()
    headers = ['-nostdinc++', '-isystem', str(cxx_inc), '-isystem', str(llvm_src / 'libcxxabi/include')] + libc_public
    includes = ['-I' + str(B), '-I' + str(B / 'cpu'), '-I' + str(B / 'iodev'), '-I' + str(B / 'instrument/stubs'),
                '-I' + str(B / 'cpu/softfloat3e/include'), '-I' + str(B / 'cpu/softfloat3e')]
    # Not Linux: Bochs must not take its Linux host paths (raw floppy, console beep, block ioctls).
    flags = target + codegen + ['-std=gnu++20', '-w', '-fno-exceptions', '-fno-rtti', '-D_GNU_SOURCE', '-U__linux__', '-U__linux', '-Ulinux', '-U__gnu_linux__',
             '-DSOFTFLOAT_FAST_INT64', '-DINLINE_LEVEL=5', '-DSOFTFLOAT_FAST_DIV32TO16', '-DSOFTFLOAT_FAST_DIV64TO32'] + headers + includes
    sources = bochs_sources()
    objects = parallel(lambda item: compile_one(item[1], out / f'b{item[0]:04d}-{item[1].stem}.o', flags),
                       list(enumerate(sources)))
    # Charon glue that includes Bochs headers: Bochs headers as system headers.
    sys_includes = sum((['-isystem', i[2:]] for i in includes), [])
    glue = target + codegen + ['-std=gnu++20', '-fno-exceptions', '-fno-rtti', '-D_GNU_SOURCE'] + glue_warn + headers + sys_includes + [
        '-I' + str(pcsrc), '-I' + str(root / 'src/efi'), '-I' + str(build / 'include'), '-I' + str(synth_common)]
    for name in ('pc_gui.cc', 'pc_main.cc'):
        objects.append(compile_one(pcsrc / name, out / ('glue-' + Path(name).stem + '.o'), glue))
    return objects, sources

def build_glue(cxx_inc, llvm_src):
    out = build / 'glue'; out.mkdir()
    headers = ['-nostdinc++', '-isystem', str(cxx_inc)] + libc_public
    flags = target + codegen + ['-std=gnu++20', '-fno-exceptions', '-fno-rtti'] + glue_warn + headers + [
        '-I' + str(pcsrc), '-I' + str(root / 'src/efi'), '-I' + str(build / 'include'), '-I' + str(synth_common)]
    objects = [compile_one(pcsrc / n, out / (Path(n).stem + '.o'), flags) for n in ('pc_efi.cpp', 'pc_config.cpp', 'pc_bns.cpp')]
    # The BIOS and VGA BIOS of Bochs 3.1 (LGPL), built into the image.
    roms = out / 'pc_roms.S'
    bios = (B / 'bios/BIOS-bochs-latest').as_posix(); vga = (B / 'bios/VGABIOS-lgpl/VGABIOS-lgpl-latest.bin').as_posix()
    roms.write_text('.section .rodata.pc_roms,"a"\n.balign 16\n.globl pc_rom_bios\npc_rom_bios:\n.incbin "' + bios + '"\n'
                    '.globl pc_rom_bios_end\npc_rom_bios_end:\n.balign 16\n.globl pc_rom_vgabios\npc_rom_vgabios:\n'
                    '.incbin "' + vga + '"\n.globl pc_rom_vgabios_end\npc_rom_vgabios_end:\n'
                    '.section .note.GNU-stack,"",@progbits\n', newline=NL)
    objects.append(compile_one(roms, out / 'pc_roms.o', target))
    return objects

# ---------------------------------------------------------------- link
LINKER_SCRIPT = '''OUTPUT_FORMAT(elf64-x86-64)
ENTRY(efi_main)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x1000;
 .text : ALIGN(0x1000) { *(.text .text.* .ltext .ltext.* .gnu.linkonce.t.*) } :text
 .rodata : ALIGN(0x1000) { *(.rodata .rodata.* .lrodata .lrodata.* .gnu.linkonce.r.*) } :rodata
 .data : ALIGN(0x1000) { *(.data.rel.ro .data.rel.ro.*) *(.got .got.*) *(.data .data.* .ldata .ldata.* .gnu.linkonce.d.*)
   . = ALIGN(8); __init_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
   *(.bss .bss.* .lbss .lbss.*) *(COMMON) *(LARGE_COMMON) } :data
 /DISCARD/ : { *(.group) *(.comment) *(.note.GNU-stack) *(.llvm_addrsig) *(.fini_array .fini_array.*) *(.eh_frame) *(.eh_frame_hdr) }
}
'''

def harden(path):
    """NX_COMPAT, no W+X section, PE checksum (as build-charon-synths.py); no time stamp or host path."""
    image = bytearray(path.read_bytes()); pe = struct.unpack_from('<I', image, 0x3c)[0]
    count = struct.unpack_from('<H', image, pe + 6)[0]; optional = pe + 24; optional_size = struct.unpack_from('<H', image, pe + 20)[0]
    assert struct.unpack_from('<I', image, optional + 32)[0] >= 0x1000, 'section alignment below 4 KiB'
    for i in range(count):
        at = optional + optional_size + i * 40; name = bytes(image[at:at + 8]).rstrip(b'\0'); flags = struct.unpack_from('<I', image, at + 36)[0]
        assert not (flags & 0x20000000 and flags & 0x80000000), ('writable and executable section', name)
    # Reproducible and private: no link time stamp, no host path in the CodeView entry.
    struct.pack_into('<I', image, pe + 8, 0)
    at = image.find(b'NB10')
    if at >= 0:
        end = image.index(0, at + 16)
        name = b'CHARONPC.elf'
        image[at + 16:end] = name + bytes(end - at - 16 - len(name))
        for entry in range(at - 28, at - 400, -4):   # IMAGE_DEBUG_DIRECTORY whose PointerToRawData is this record
            if struct.unpack_from('<I', image, entry + 24)[0] == at and struct.unpack_from('<I', image, entry + 12)[0] == 2:
                struct.pack_into('<I', image, entry + 16, 16 + len(name) + 1)
                struct.pack_into('<I', image, entry + 4, 0)
                break
        else:
            raise AssertionError('CodeView debug directory entry not found')
    struct.pack_into('<H', image, optional + 70, struct.unpack_from('<H', image, optional + 70)[0] | 0x0100)
    struct.pack_into('<I', image, optional + 64, 0)
    total = 0
    for at in range(0, len(image) - len(image) % 2, 2):
        total += struct.unpack_from('<H', image, at)[0]; total = (total & 0xffff) + (total >> 16)
    if len(image) % 2: total += image[-1]; total = (total & 0xffff) + (total >> 16)
    struct.pack_into('<I', image, optional + 64, ((total & 0xffff) + (total >> 16) & 0xffff) + len(image))
    path.write_bytes(bytes(image))

def link(objects):
    out = build / 'link'; out.mkdir()
    rsp = out / 'link.rsp'; rsp.write_text(NL.join(Path(o).as_posix() for o in objects), newline=NL)
    script = out / 'image.lds'; script.write_text(LINKER_SCRIPT, newline=NL)
    elf = out / 'CHARONPC.elf'
    run([lld, '--gc-sections', '--emit-relocs', '-z', 'max-page-size=4096', '--no-undefined', '-T', script,
         '@' + str(rsp), '-o', elf])
    sections = run([readelf, '-S', '-W', elf])
    assert '.ctors' not in sections and '.tdata' not in sections and '.tbss' not in sections, 'ctors/TLS present'
    disassembly = run([objdump, '-d', '--no-show-raw-insn', elf])
    bad = [l for l in disassembly.splitlines() if re.search(r'\tsyscall\b|%fs:|%gs:|\tint\s+\$0x80', l)]
    assert not bad, 'kernel/TLS access in image:\n' + '\n'.join(bad[:20])
    efi = build / 'CHARONPC.EFI'
    run([imagetool, 'GenImage', '-c', 'PE', '-t', 'UEFI_APPLICATION', '-b', '0x100000', '-o', efi, elf])
    harden(efi)
    data = efi.read_bytes(); pe = struct.unpack_from('<I', data, 0x3c)[0]; optional = pe + 24
    report = dict(file=efi.name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                  subsystem=struct.unpack_from('<H', data, optional + 68)[0],
                  nx_compat=bool(struct.unpack_from('<H', data, optional + 70)[0] & 0x100),
                  imports=struct.unpack_from('<II', data, optional + 120))
    assert report['subsystem'] == 10 and report['nx_compat'] and report['imports'] == (0, 0), report
    report['check_charon_pe'] = run([sys.executable, work / 'check-charon-pe.py', efi]).strip()
    symbols = {l.split()[-1]: int(l.split()[0], 16) for l in run([nm, elf]).splitlines() if len(l.split()) == 3}
    report['constructors'] = (symbols['__init_array_end'] - symbols['__init_array_start']) // 8
    return efi, report

shutil.copytree(root / 'src/efi/haiku', build / 'include/efi')
libc = build_libc()
cxxrt, cxx_inc, llvm_src = build_cxx_runtime()
bochs_objects, bochs_files = build_bochs(cxx_inc, llvm_src)
glue_objects = build_glue(cxx_inc, llvm_src)
efi, report = link(glue_objects + bochs_objects + [cxxrt, libc])
report['bochs_sources'] = len(bochs_files)
(build / 'results.json').write_text(json.dumps(report, indent=2))

LICENCES = '''CharonPC (CHARONPC.EFI): licences
=================================
Bochs 3.1 (bochs.sourceforge.io, github.com/bochs-emu/Bochs, release
REL_3_1_FINAL): LGPL-2.1-or-later (licences/bochs-COPYING.txt). Sources
unmodified except two lines that register the "charon" display library in
plugin.h/plugin.cc; config.h written by hand (src/pc/bochs-config.h). The
built-in BIOS-bochs-latest and VGABIOS-lgpl-latest.bin come from the same
release (LGPL-2.1-or-later).
musl 1.2.5 (MIT, licences/musl-COPYRIGHT.txt); libc++ / libc++abi of LLVM
23.1.5 (Apache-2.0 WITH LLVM-exception, licences/llvm-LICENSE.txt).
Charon glue (src/pc, src/synth/common runtime parts): GPL-2.0-or-later; the
combined image is distributed under GPL-2.0-or-later (LGPL-2.1 section 3).
Corresponding source: the pinned archives (see tools/build-charon-bochs.py)
plus src/pc, src/synth/common and tools/build-charon-bochs.py.
'''
if stage_arg:
    stage = Path(stage_arg).resolve(); stage.mkdir(parents=True, exist_ok=False)
    shutil.copy(efi, stage / 'CHARONPC.EFI')
    lic = stage / 'licences'; lic.mkdir()
    shutil.copy(B / 'COPYING', lic / 'bochs-COPYING.txt'); shutil.copy(musl / 'COPYRIGHT', lic / 'musl-COPYRIGHT.txt')
    shutil.copy(llvm_src / 'libcxx/LICENSE.TXT', lic / 'llvm-LICENSE.txt')
    (stage / 'LICENCES.txt').write_text(LICENCES, encoding='utf-8', newline=NL)
print('PASS build-charon-bochs', efi, report['bytes'], 'bytes,', report['bochs_sources'], 'Bochs sources,',
      report['constructors'], 'constructors')
