"""r400: builds CharonDOS (src/dos) on a Windows host.

Usage: python build-charondos.py NEW_BUILD_DIR [--host-only | --uefi-only]

Products in NEW_BUILD_DIR (which must not exist yet):
  charondos-host.exe   host test executable (backend_host.cpp, Win32 + stdio);
                       driven by tests/CharonDosTests.py
  CHARONDOS.EFI        UEFI application (backend_uefi.cpp, freestanding, no libc)

Same toolchain as build-charon-probe.py: clang++ (ELF) + ld.lld + image.lds +
AUDK ImageTool GenImage. CHARON_LLVM and CHARON_IMAGETOOL override the tool
locations. Both builds use -std=c++17 -Wall -Wextra -Werror."""
import os, shutil, subprocess, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import charon_private_paths
container = root.parent.parent
args = [a for a in sys.argv[1:] if not a.startswith('--')]
opts = [a for a in sys.argv[1:] if a.startswith('--')]
if len(args) != 1 or any(o not in ('--host-only', '--uefi-only') for o in opts) or len(opts) > 1:
    sys.exit(__doc__)
build = Path(args[0])
build.mkdir(parents=True, exist_ok=False)
build = build.resolve()
llvm = Path(os.environ.get('CHARON_LLVM', container / 'work/llvm-23.1.5/bin'))
imagetool = Path(os.environ.get('CHARON_IMAGETOOL', container / 'work/ha/BaseTools/Bin/Win64/ImageTool.exe'))
clangxx = llvm / 'clang++.exe'
lld = llvm / 'ld.lld.exe'
dos = root / 'src/dos'
shared = ['cpu', 'bios', 'video', 'dosfs', 'dosint', 'shell', 'util', 'sound', 'charon_cmd']   # r407: sound.cpp; charon_cmd: the CHARON command
warn = ['-std=c++17', '-Wall', '-Wextra', '-Werror']


def run(*command, cwd=None):
    r = subprocess.run(list(map(str, command)), capture_output=True, text=True, cwd=cwd)
    if r.returncode:
        sys.exit(f'FAILED: {" ".join(map(str, command))}\n{r.stdout}{r.stderr}')


if '--uefi-only' not in opts:
    # Hosted build: the default target of this clang (Windows), its own C runtime.
    out = build / 'charondos-host.exe'
    run(clangxx, *warn, '-O2', '-DDOS_HOST', '-D_CRT_SECURE_NO_WARNINGS', '-fno-exceptions', '-fno-rtti',
        *[dos / f'{name}.cpp' for name in shared], dos / 'backend_host.cpp', '-o', out)
    print('PASS', out)

if '--host-only' not in opts:
    obj = build / 'uefi'
    obj.mkdir()
    shutil.copytree(root / 'src/efi/haiku', obj / 'include/efi')
    (obj / 'image.lds').write_text('''OUTPUT_FORMAT(elf64-x86-64)
ENTRY(efi_main)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x1000;
 .text : ALIGN(0x1000) { *(.text .text.* .ltext .ltext.*) } :text
 .rodata : ALIGN(0x1000) { *(.rodata .rodata.* .lrodata .lrodata.*) } :rodata
 .data : ALIGN(0x1000) { *(.data.rel.ro .data.rel.ro.*) *(.got .got.*) *(.data .data.* .ldata .ldata.*) *(.bss .bss.* .lbss .lbss.*) *(COMMON) *(LARGE_COMMON) } :data
 /DISCARD/ : { *(.comment) *(.note.GNU-stack) *(.llvm_addrsig) }
}
''')
    flags = ['--target=x86_64-unknown-elf', *warn, '-O2', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
             '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics', '-fshort-wchar', '-fno-pic', '-fno-pie',
             '-mcmodel=large', '-mno-red-zone', '-mno-sse', '-mno-mmx', '-ffunction-sections', '-fdata-sections',
             '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
             '-I', obj / 'include', '-I', root / 'tools/charon-compat-include',
             *charon_private_paths.prefix_map((build, 'build'), (root, 'charon'), (container, 'host'))]
    objects = []
    for name in shared + ['backend_uefi']:
        o = obj / f'{name}.o'
        run(clangxx, *flags, '-c', dos / f'{name}.cpp', '-o', o)
        objects.append(o)
    # One closed object first, so an unresolved symbol (a libc call the
    # compiler slipped in) is an error here and not a silent zero address.
    run(lld, '-r', '--gc-sections', '--undefined=efi_main', '-o', obj / 'closed.o', *objects)
    run(lld, '--emit-relocs', '--no-undefined', '-z', 'max-page-size=4096', '-T', obj / 'image.lds', obj / 'closed.o',
        '-o', obj / 'CHARONDOS.elf')
    out = build / 'CHARONDOS.EFI'
    # r410: a bare ELF name (ImageTool copies the path it is given into the PE debug entry).
    run(imagetool, 'GenImage', '-c', 'PE', '-t', 'UEFI_APPLICATION', '-b', '0x100000', '-o', '../CHARONDOS.EFI', 'CHARONDOS.elf', cwd=obj)
    data = out.read_bytes()
    if data[:2] != b'MZ':
        sys.exit('FAILED: the image is not a PE file')
    charon_private_paths.check(out)
    # r410: Charon names a boot entry after the boot manager whose text it finds in the entry's image
    # (src/efi/CharonBootManagers.h, texts stored one letter up). CharonDOS must carry none of them:
    # r409's image held Windows' name and the device list said "CharonDOS (Windows Boot Manager)".
    import re
    literal = r'\{"((?:[^"\\]|\\.)+)",u"((?:[^"\\]|\\.)*)"\}'
    table = (root / 'src/efi/CharonBootManagers.h').read_text(encoding='utf-8')
    found = []
    for raised, name in re.findall(literal, table):
        if '\\' in raised:
            continue
        text = bytes(ord(c) - 1 for c in raised)
        if text in data or b''.join(bytes([c, 0]) for c in text) in data:
            found.append(name or text.decode('latin-1'))
    if found:
        sys.exit('FAILED: CHARONDOS.EFI holds the text Charon recognises a boot manager by: ' + ', '.join(found))
    print('PASS', out, len(data), 'bytes')
