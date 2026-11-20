"""Build CHARON.EFI on a Windows host with LLVM (r355).

Same recipe as tools/build-charon-efi.sh (freestanding x86-64 ELF objects,
closed ELF link, AUDK ImageTool ELF->PE), but with Clang/LLD instead of the
Haiku GCC toolchain, so no guest VM is needed.

Usage: python build-charon-windows.py NEW_BUILD_DIRECTORY [--trace]
Environment: CHARON_LLVM (default ../work/llvm-23.1.5/bin relative to the
repository container), CHARON_IMAGETOOL (default work/ha/BaseTools/Bin/Win64/ImageTool.exe).
Clang does not implement GCC's -maccumulate-outgoing-args; on x86-64 it
already stores outgoing arguments with mov into a fixed frame, so it is omitted.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import hashlib,json,os,shutil,subprocess,sys

root=Path(__file__).resolve().parents[1]           # outputs/nvda-haiku
container=root.parents[1]                          # kont
llvm=Path(os.environ.get('CHARON_LLVM',container/'work/llvm-23.1.5/bin'))
imagetool=Path(os.environ.get('CHARON_IMAGETOOL',container/'work/ha/BaseTools/Bin/Win64/ImageTool.exe'))
args=[a for a in sys.argv[1:] if not a.startswith('--')]
if len(args)!=1:sys.exit(__doc__)
build=Path(args[0]).resolve();build.mkdir(parents=True,exist_ok=False)
trace='--trace' in sys.argv
# --driver: CHARONVOICE.EFI, the resident voice/screen reader loaded by firmware
# from Driver#### (same sources, HAK_CHARON_VOICE_DRIVER entry point).
driver='--driver' in sys.argv
imageName='CHARONVOICE' if driver else 'CHARON'
clang=str(llvm/'clang.exe');clangxx=str(llvm/'clang++.exe');lld=str(llvm/'ld.lld.exe');nm=str(llvm/'llvm-nm.exe')
target=['--target=x86_64-unknown-elf']
flags=target+'-O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -mcmodel=large -mno-red-zone -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables'.split()
cxx=flags+'-std=c++17 -fno-exceptions -fno-rtti'.split()
log=(build/'build.log').open('w',encoding='utf-8')
def run(command,cwd=root):
    log.write('$ '+' '.join(map(str,command))+'\n');log.flush()
    result=subprocess.run(list(map(str,command)),cwd=cwd,capture_output=True,text=True)
    log.write(result.stdout+result.stderr);log.flush()
    if result.returncode:
        sys.stderr.write(result.stdout+result.stderr)
        raise SystemExit(f'FAILED ({result.returncode}): {command[0]} ... {command[-1]}')
    return result.stdout

# EFI headers are included as <efi/...>.
shutil.copytree(root/'src/efi/haiku',build/'include/efi')

# ---- musl math (unchanged sources, same flags as math/build.sh) -------------
math=root/'src/tts/runtime/math';mathOut=build/'math';(mathOut/'include/bits').mkdir(parents=True)
sed=shutil.which('sed') or r'C:\Program Files\Git\usr\bin\sed.exe'
alltypes=run([sed,'-f',math/'musl/tools/mkalltypes.sed',math/'musl/arch/x86_64/bits/alltypes.h.in',math/'musl/include/alltypes.h.in'])
(mathOut/'include/bits/alltypes.h').write_text(alltypes,encoding='utf-8',newline='\n')
mathFlags=target+['-std=c11','-O2','-Wall','-Wextra','-Wno-unused-parameter','-Wno-unused-but-set-variable','-Wno-parentheses',
    '-ffreestanding','-fno-builtin','-nostdinc','-fno-stack-protector','-fno-pic','-fno-pie','-mcmodel=large','-mno-red-zone',
    '-fno-fast-math','-frounding-math','-ffp-contract=off','-fexcess-precision=standard','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
    '-I'+str(mathOut/'include'),'-I'+str(math/'musl/src/include'),'-I'+str(math/'musl/include'),'-I'+str(math/'musl/src/internal'),
    '-I'+str(math/'musl/arch/x86_64'),'-I'+str(math/'musl/arch/generic')]
sources=[l.strip() for l in (math/'sources.txt').read_text().splitlines() if l.strip()]
def mathUnit(source):
    out=mathOut/(Path(source).stem+'.o');run([clang,*mathFlags,'-c',math/source,'-o',out]);return out
with ThreadPoolExecutor(max_workers=8) as pool:mathObjects=list(pool.map(mathUnit,sources))
run([clang,*target,'-c','-mno-red-zone',math/'fenv-no-red-zone.s','-o',mathOut/'fenv.o'])
run([lld,'-r',*mathObjects,mathOut/'fenv.o','-o',mathOut/'hak-math.o'])
assert not run([nm,'-u',mathOut/'hak-math.o']).strip(),'math closure has unresolved symbols'

# ---- Charon units --------------------------------------------------------
inc=['-I'+str(build/'include')]
compat=['-I'+str(root/'tools/charon-compat-include')]
musl=['-I'+str(mathOut/'include'),'-I'+str(math/'musl/include'),'-I'+str(math/'musl/arch/x86_64'),'-I'+str(math/'musl/arch/generic')]
# Haiku's hosted math.h made size_t and labs visible transitively; musl's does
# not. Same declarations, resolved by the linked musl objects (labs.c).
dspPrelude=['-include','stddef.h','-include','stdlib.h']
boot=['-fshort-wchar','-Wall','-Wextra','-Werror','-DHAK_CHARON_LIVE_VOICE']+(['-DHAK_CHARON_TRACE'] if trace else [])+(['-DHAK_CHARON_VOICE_DRIVER','-Wno-unused-function'] if driver else [])
boot+=os.environ.get('CHARON_EXTRA','').split()  # e.g. -DHAK_CHARON_READER_TRACE
# r400: Charon's own HTTPS (BearSSL built by tools/build-charon-bearssl.py); the boot manager only, not the voice driver.
bearssl=Path(os.environ.get('CHARON_BEARSSL',container/'work/charon-bearssl-uefi'))/'libcharon-bearssl.a'
https=bearssl.exists() and not driver
if https:boot+=['-DHAK_CHARON_HTTPS']
units=[
 (clangxx,cxx+compat,'src/tts/tvox_native/tvox_freestanding.cpp','frontend'),
 (clang,target+['-mno-red-zone'],'src/tts/tvox_native/tvox_context_x64.S','context'),
 (clang,flags+['-std=c11'],'src/tts/runtime/CharonPool.c','pool'),
 (clang,flags+['-std=c11'],'src/tts/runtime/CharonMemory.c','memory'),
 (clang,flags+['-std=c11']+musl+compat,'src/tts/tvox_native/tvox_dsp_stream.c','stream'),
 (clang,flags+['-std=c11']+musl+dspPrelude,'src/tts/tvox_native/tvox_dsp_mit.c','dsp'),
 (clangxx,cxx+compat,'src/tts/runtime/CharonLiveVoice.cpp','live'),
 (clangxx,cxx+['-fshort-wchar']+inc+compat,'src/efi/HiiMenu.cpp','hii-menu'),
 (clangxx,cxx+['-fshort-wchar']+inc+compat,'src/efi/hii/HiiModel.cpp','hii-model'),
 (clangxx,cxx+['-fshort-wchar']+inc+compat,'src/efi/hii/HiiDatabase.cpp','hii-database'),
 (clangxx,cxx+['-fshort-wchar']+inc+compat,'src/efi/hii/HiiConfig.cpp','hii-config'),
 (clangxx,cxx+['-fshort-wchar']+inc+compat,'src/efi/hii/HiiSettingsView.cpp','hii-view'),
 (clang,flags+['-std=c11','-Wall','-Wextra','-Werror'],'src/efi/CharonTtf.c','ttf'),
 (clangxx,cxx+boot+inc+compat,'src/efi/HAKBootEFI.cpp','boot'),
]
def unit(item):
    compiler,options,source,name=item;out=build/(name+'.o')
    run([compiler,*options,'-c',root/source,'-o',out]);return out
with ThreadPoolExecutor(max_workers=6) as pool:objects=list(pool.map(unit,units))
run([lld,'-r','--gc-sections','--undefined=efi_main','-o',build/'closed.o',*objects,mathOut/'hak-math.o',*([bearssl] if https else [])])
undefined=run([nm,'-u',build/'closed.o']).strip()
assert not undefined,'EFI graph has unresolved symbols:\n'+undefined
# No C runtime runs global constructors in Charon: any .init_array/.ctors
# would silently leave an object unconstructed.
sections=run([str(llvm/'llvm-readelf.exe'),'-S','-W',build/'closed.o'])
assert 'INIT_ARRAY' not in sections and '.ctors' not in sections,'dynamic initialization present (global constructor)'
# Clang's -mcmodel=large uses the large-section names (.ltext/.lrodata/.ldata/.lbss).
(build/'image.lds').write_text('''OUTPUT_FORMAT(elf64-x86-64)
ENTRY(efi_main)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x1000;
 .text : ALIGN(0x1000) { *(.text .text.* .ltext .ltext.* .gnu.linkonce.t.*) } :text
 .rodata : ALIGN(0x1000) { *(.rodata .rodata.* .lrodata .lrodata.* .gnu.linkonce.r.*) } :rodata
 .data : ALIGN(0x1000) { *(.data.rel.ro .data.rel.ro.*) *(.got .got.*) *(.data .data.* .ldata .ldata.* .gnu.linkonce.d.*) *(.bss .bss.* .lbss .lbss.*) *(COMMON) *(LARGE_COMMON) } :data
 /DISCARD/ : { *(.group) *(.comment) *(.note.GNU-stack) *(.llvm_addrsig) }
}
''',newline='\n')
run([lld,'--emit-relocs','-z','max-page-size=4096','-T',build/'image.lds',build/'closed.o','-o',build/'CHARON.elf'])
run([imagetool,'GenImage','-c','PE','-t','UEFI_DRIVER' if driver else 'UEFI_APPLICATION','-b','0x100000','-o',build/(imageName+'.EFI'),build/'CHARON.elf'])
# r387: memory-protection requirements of the Microsoft UEFI CA (and of
# firmware with image protection): no section both writable and executable,
# read-only data really read-only (.data.rel.ro and .got moved to .data
# above), 4 KiB section alignment, and IMAGE_DLLCHARACTERISTICS_NX_COMPAT.
# ImageTool has no option for the flag, so it is set here and the PE
# checksum recomputed.
def harden(path):
    import struct
    image=bytearray(path.read_bytes());pe=struct.unpack_from('<I',image,0x3c)[0]
    count=struct.unpack_from('<H',image,pe+6)[0];optional=pe+24;optionalSize=struct.unpack_from('<H',image,pe+20)[0]
    assert struct.unpack_from('<I',image,optional+32)[0]>=0x1000,'section alignment below 4 KiB'
    for i in range(count):
        at=optional+optionalSize+i*40;name=bytes(image[at:at+8]).rstrip(b'\0');flags=struct.unpack_from('<I',image,at+36)[0]
        assert not (flags&0x20000000 and flags&0x80000000),('writable and executable section',name)
        if name==b'.rodata':assert not flags&0x80000000,'.rodata is writable'
    struct.pack_into('<H',image,optional+70,struct.unpack_from('<H',image,optional+70)[0]|0x0100)
    struct.pack_into('<I',image,optional+64,0)
    total=0
    for at in range(0,len(image)-len(image)%2,2):
        total+=struct.unpack_from('<H',image,at)[0];total=(total&0xffff)+(total>>16)
    if len(image)%2:total+=image[-1];total=(total&0xffff)+(total>>16)
    struct.pack_into('<I',image,optional+64,((total&0xffff)+(total>>16)&0xffff)+len(image))
    path.write_bytes(bytes(image))
harden(build/(imageName+'.EFI'))
report=run([sys.executable,container/'work/check-charon-pe.py',build/(imageName+'.EFI')])
(build/'image.json').write_text(report)
print(report)
print('PASS Windows LLVM build of CHARON.EFI; audio and boot behaviour still need a VM test')
