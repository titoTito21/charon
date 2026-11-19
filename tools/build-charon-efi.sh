#!/bin/sh
# Native x86-64 Haiku build. Real text synthesis is linked into the EFI image;
# the separately built authenticated AudioDxe supplies continuous hardware I/O.
# Converter source pin: acidanthera/audk dfd5516702b4911f1fc1919a429d637b456df913,
# BaseTools/ImageTool. Build recipe/provenance:
# src/efi/audio_stream/driver/{build-host-baseline.cmd,BUILD-NOTES.md}.
# Verified Win64 ImageTool SHA256 (other native builds may differ):
# 59619046046c445afae48a8291f793f4a301bd46cb01b7785008412553810e55.
set -eu
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo 'Usage: build-charon-efi.sh NEW_BUILD_DIRECTORY [--elf-only]' >&2; exit 2
fi
elf_only=no
if [ "$#" = 2 ]; then
    if [ "$2" != --elf-only ]; then echo 'Unknown build mode' >&2; exit 2; fi
    elf_only=yes
fi
cd "$(dirname "$0")/.."
mkdir "$1"
build=$(CDPATH= cd -- "$1" && pwd)
ln -s "$PWD/src/efi/haiku" "$build/efi"
flags='-O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -mcmodel=large -mno-red-zone -maccumulate-outgoing-args -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables'
cxxflags="$flags -std=c++17 -fno-exceptions -fno-rtti"
sh src/tts/runtime/math/build.sh "$build/math"
c++ $cxxflags -c src/tts/tvox_native/tvox_freestanding.cpp -o "$build/frontend.o"
cc -mno-red-zone -c src/tts/tvox_native/tvox_context_x64.S -o "$build/context.o"
cc $flags -std=c11 -c src/tts/runtime/CharonPool.c -o "$build/pool.o"
cc $flags -std=c11 -c src/tts/runtime/CharonMemory.c -o "$build/memory.o"
cc $flags -std=c11 -c src/tts/tvox_native/tvox_dsp_stream.c -o "$build/stream.o"
cc $flags -std=c11 -c src/tts/tvox_native/upstream/tvox_dsp.c -o "$build/dsp.o"
c++ $cxxflags -c src/tts/runtime/CharonLiveVoice.cpp -o "$build/live.o"
# Actual HII browser, bounded exported model and confirmed ConfigAccess writes.
# Use the same freestanding ABI as the menu; no host C++ runtime is linked.
c++ $cxxflags -fshort-wchar -I"$build" -c src/efi/HiiMenu.cpp -o "$build/hii-menu.o"
c++ $cxxflags -fshort-wchar -I"$build" -c src/efi/hii/HiiModel.cpp -o "$build/hii-model.o"
c++ $cxxflags -fshort-wchar -I"$build" -c src/efi/hii/HiiDatabase.cpp -o "$build/hii-database.o"
c++ $cxxflags -fshort-wchar -I"$build" -c src/efi/hii/HiiConfig.cpp -o "$build/hii-config.o"
c++ $cxxflags -fshort-wchar -I"$build" -c src/efi/hii/HiiSettingsView.cpp -o "$build/hii-view.o"
c++ $cxxflags -fshort-wchar -Wall -Wextra -Werror -DHAK_CHARON_LIVE_VOICE \
    -I"$build" -c src/efi/HAKBootEFI.cpp -o "$build/boot.o"
# Resolve/prune symbols with the ELF linker. Direct ELF inputs to the PE
# linker lose symbol definitions; objcopy ELF->COFF loses RELA local addends.
# A final ELF link applies those addends correctly. AUDK ImageTool converts
# retained relocation records to PE DIR64 without changing their meaning.
ld -r --gc-sections --undefined=efi_main -o "$build/closed.o" \
    "$build/boot.o" "$build/hii-menu.o" "$build/hii-model.o" "$build/hii-database.o" \
    "$build/hii-config.o" "$build/hii-view.o" "$build/live.o" "$build/frontend.o" "$build/context.o" \
    "$build/pool.o" "$build/memory.o" "$build/stream.o" "$build/dsp.o" "$build/math/hak-math.o"
nm -u "$build/closed.o" > "$build/undefined.txt"
if [ -s "$build/undefined.txt" ]; then
    cat "$build/undefined.txt" >&2
    echo 'EFI graph has unresolved symbols' >&2
    exit 1
fi
cat > "$build/image.lds" <<'LDS'
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(efi_main)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x1000;
 .text : ALIGN(0x1000) { *(.text .text.* .gnu.linkonce.t.*) } :text
 .rodata : ALIGN(0x1000) { *(.rodata .rodata.* .data.rel.ro .data.rel.ro.* .gnu.linkonce.r.*) *(.got .got.*) } :rodata
 .data : ALIGN(0x1000) { *(.data .data.* .gnu.linkonce.d.*) *(.bss .bss.*) *(COMMON) } :data
 /DISCARD/ : { *(.group) *(.comment) *(.note.GNU-stack) }
}
LDS
ld --emit-relocs -z max-page-size=4096 -T "$build/image.lds" \
    "$build/closed.o" -o "$build/CHARON.elf"
if [ "$elf_only" = yes ]; then
    echo 'Built linked CHARON.elf; PE conversion and validation are still required.'
    echo 'ImageTool GenImage -c PE -t UEFI_APPLICATION -b 0x100000 -o CHARON.EFI CHARON.elf'
    exit 0
fi
image_tool=${IMAGETOOL:-ImageTool}
if ! command -v "$image_tool" >/dev/null 2>&1; then
    echo 'Set IMAGETOOL to the actual AUDK ImageTool executable, or use --elf-only for host conversion.' >&2
    exit 2
fi
"$image_tool" GenImage -c PE -t UEFI_APPLICATION -b 0x100000 \
    -o "$build/CHARON.EFI" "$build/CHARON.elf"
objdump -p "$build/CHARON.EFI" > "$build/pe.txt"
python3 - "$build/CHARON.EFI" > "$build/image.json" <<'PY'
"""Reject a non-relocatable/import-dependent PE, independent of objdump wording."""
import struct,json,sys,hashlib
from pathlib import Path
p=Path(sys.argv[1]);data=p.read_bytes()
def u16(at):return struct.unpack_from('<H',data,at)[0]
def u32(at):return struct.unpack_from('<I',data,at)[0]
assert data[:2]==b'MZ';pe=u32(0x3c);assert data[pe:pe+4]==b'PE\0\0'
assert u16(pe+4)==0x8664
count=u16(pe+6);optional=pe+24;optionalSize=u16(pe+20)
assert u16(optional)==0x20b and u16(optional+68)==10
assert not u16(pe+22)&1,'Relocations stripped'
entry=u32(optional+16);base=struct.unpack_from('<Q',data,optional+24)[0];imageSize=u32(optional+56)
assert u32(optional+108)>=6 and entry>0
importRva,importSize=u32(optional+120),u32(optional+124)
sections=[]
for i in range(count):
 at=optional+optionalSize+i*40;name=data[at:at+8].rstrip(b'\0').decode('ascii','replace')
 sections.append(dict(name=name,size=u32(at+8),rva=u32(at+12),rawSize=u32(at+16),raw=u32(at+20),flags=u32(at+36)))
assert any(s['rva']<=entry<s['rva']+s['size'] and s['flags']&0x20000000 for s in sections),'Entry outside executable section'
def offset(rva,n):
 for s in sections:
  if s['rva']<=rva and rva+n<=s['rva']+s['rawSize']:
   at=s['raw']+rva-s['rva'];assert at+n<=len(data);return at
 raise AssertionError(('RVA has no file data',hex(rva),n))
if importRva or importSize:
 assert importRva and importSize>=20
 assert not any(data[offset(importRva,importSize):offset(importRva,importSize)+importSize]),'OS import descriptors present'
relocRva=u32(optional+152);relocSize=u32(optional+156)
assert relocRva and relocSize,'No base relocations: image cannot safely move'
at=offset(relocRva,relocSize);end=at+relocSize;relocations=0
while at<end:
 page=u32(at);size=u32(at+4);assert size>=8 and not size%4 and at+size<=end
 for item in range(at+8,at+size,2):
  raw=u16(item);kind=raw>>12
  if not kind:continue
  assert kind==10,('Non-DIR64 relocation',kind)
  target=page+(raw&0xfff);assert target+8<=imageSize
  original=struct.unpack_from('<Q',data,offset(target,8))[0]
  assert base<=original<base+imageSize,('Relocated pointer outside closed image',hex(original))
  relocations+=1
 at+=size
assert relocations
print(json.dumps(dict(file=str(p),bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),sections=count,entryRva=entry,imageBase=base,dir64Relocations=relocations,osImports=0),indent=2))

PY
echo 'Built CHARON.EFI with live TVox; signing, assets, driver and actual audio validation are separate gates.'
