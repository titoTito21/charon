"""Build the vendored BearSSL 0.6 for Charon's UEFI target (r401).

Compiles src/efi/third_party/bearssl/src/**/*.c with the same code generation
flags as tools/build-charon-windows.py (freestanding x86-64 ELF, large code
model, no red zone) into OUTDIR/obj/*.o and OUTDIR/libcharon-bearssl.a, then
proves the archive imports nothing from an operating system: the only
undefined symbols allowed are the five memory functions Charon already
defines in src/tts/runtime/CharonMemory.c.

Usage: python build-charon-bearssl.py NEW_OUTPUT_DIRECTORY [--host]
  --host   build for the Windows host instead (tests/CharonHttpsHostTest.cpp):
           same sources, operating system hooks still off, host object format.
Environment: CHARON_LLVM (default work/llvm-23.1.5/bin next to the repository).
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import json,os,subprocess,sys

root=Path(__file__).resolve().parents[1]
container=root.parents[1]
llvm=Path(os.environ.get('CHARON_LLVM',container/'work/llvm-23.1.5/bin'))
args=[a for a in sys.argv[1:] if not a.startswith('--')]
if len(args)!=1:sys.exit(__doc__)
host='--host' in sys.argv
out=Path(args[0]).resolve();out.mkdir(parents=True,exist_ok=False);(out/'obj').mkdir()
bearssl=root/'src/efi/third_party/bearssl'
clang=str(llvm/'clang.exe');ar=str(llvm/'llvm-ar.exe');nm=str(llvm/'llvm-nm.exe');lld=str(llvm/'ld.lld.exe');size=str(llvm/'llvm-readelf.exe')
# No operating system services inside BearSSL on either target: the clock for
# certificate validation and the entropy are supplied by CharonHttps.h.
hooks=['-DBR_USE_UNIX_TIME=0','-DBR_USE_WIN32_TIME=0','-DBR_USE_URANDOM=0','-DBR_USE_WIN32_RAND=0','-DBR_USE_GETENTROPY=0']
include=['-I'+str(bearssl/'src'),'-I'+str(bearssl/'inc')]
if host:
    flags=['-O2','-std=c99','-ffunction-sections','-fdata-sections','-D_CRT_SECURE_NO_WARNINGS','-DBR_MSC=0']+hooks+include
else:
    flags=['--target=x86_64-unknown-elf']+('-O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -mcmodel=large -mno-red-zone '
        '-ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -std=c99').split()+hooks+['-I'+str(bearssl/'compat')]+include
log=(out/'build.log').open('w',encoding='utf-8')
def run(command):
    log.write('$ '+' '.join(map(str,command))+'\n');log.flush()
    result=subprocess.run(list(map(str,command)),capture_output=True,text=True)
    log.write(result.stdout+result.stderr);log.flush()
    if result.returncode:
        sys.stderr.write(result.stdout+result.stderr)
        raise SystemExit(f'FAILED ({result.returncode}): {command[-3]}')
    return result.stdout
sources=sorted((bearssl/'src').rglob('*.c'))
assert len(sources)>200,'BearSSL sources missing'
def unit(source):
    target=out/'obj'/(source.stem+('.obj' if host else '.o'));run([clang,*flags,'-c',source,'-o',target]);return target
with ThreadPoolExecutor(max_workers=8) as pool:objects=list(pool.map(unit,sources))
assert len({o.name for o in objects})==len(objects),'object name collision'
archive=out/('charon-bearssl.lib' if host else 'libcharon-bearssl.a')
(out/'objects.rsp').write_text('\n'.join('"'+str(o).replace('\\','/')+'"' for o in objects),newline='\n')
run([ar,'rcs',archive,'@'+str(out/'objects.rsp')])
report={'archive':str(archive),'objects':len(objects),'archiveBytes':archive.stat().st_size,'host':host}
if not host:
    allowed={'memcpy','memmove','memset','memcmp','strlen'}
    run([lld,'-r','--whole-archive',archive,'-o',out/'whole.o'])
    undefined={line.split()[-1] for line in run([nm,'-u',out/'whole.o']).splitlines() if line.strip()}
    assert undefined<=allowed,'operating system imports: '+', '.join(sorted(undefined-allowed))
    sections=run([size,'-S','-W',out/'whole.o'])
    assert 'INIT_ARRAY' not in sections and '.ctors' not in sections,'dynamic initialization present'
    total={'text':0,'rodata':0,'data':0,'bss':0}
    for line in sections.splitlines():
        parts=line.replace('[',' ').replace(']',' ').split()
        if len(parts)<6 or not parts[0].isdigit():continue
        name,kind,length=parts[1],parts[2],int(parts[5],16)
        if name.startswith(('.text','.ltext')):total['text']+=length
        elif name.startswith(('.rodata','.lrodata')):total['rodata']+=length
        elif kind=='NOBITS':total['bss']+=length
        elif name.startswith(('.data','.ldata')):total['data']+=length
    report.update({'undefined':sorted(undefined),'wholeLibrary':total})
(out/'bearssl.json').write_text(json.dumps(report,indent=1))
print(json.dumps(report,indent=1))
print('PASS BearSSL built for '+('the Windows host' if host else 'x86_64 UEFI (ELF objects); no operating system imports'))
