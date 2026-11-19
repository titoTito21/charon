"""r396: builds src/efi/sdk/example/A11yDemoLoader.c into A11YDEMO.EFI.
Usage: python build-a11y-demo.py NEW_BUILD_DIRECTORY
Same toolchain as build-charon-windows.py (Clang/LLD for an ELF, AUDK
ImageTool ELF->PE). Env CHARON_LLVM, CHARON_IMAGETOOL as there."""
import os,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1];container=root.parent.parent
build=Path(sys.argv[1]);build.mkdir(parents=True,exist_ok=False)
llvm=Path(os.environ.get('CHARON_LLVM',container/'work/llvm-23.1.5/bin'))
imagetool=Path(os.environ.get('CHARON_IMAGETOOL',container/'work/ha/BaseTools/Bin/Win64/ImageTool.exe'))
def run(*c):
    r=subprocess.run(list(map(str,c)),capture_output=True,text=True)
    if r.returncode:sys.exit(f'FAILED: {c[0]}\n{r.stdout}{r.stderr}')
(build/'image.lds').write_text('''OUTPUT_FORMAT(elf64-x86-64)
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
src=root/'src/efi/sdk/example/A11yDemoLoader.c'
run(llvm/'clang.exe','--target=x86_64-unknown-elf','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin','-fno-stack-protector',
    '-fno-pic','-fno-pie','-mcmodel=large','-mno-red-zone','-mno-sse','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-c',src,'-o',build/'demo.o')
run(llvm/'ld.lld.exe','--emit-relocs','-z','max-page-size=4096','-T',build/'image.lds',build/'demo.o','-o',build/'demo.elf')
run(imagetool,'GenImage','-c','PE','-t','UEFI_APPLICATION','-b','0x100000','-o',build/'A11YDEMO.EFI',build/'demo.elf')
print('PASS',build/'A11YDEMO.EFI')
