"""r407: build nic-17-realtek-rge.efi, a UEFI network driver for Realtek RTL8125 / RTL8126 /
RTL8127 (2.5G / 5G / 10G; Killer E3000, E5000) made from OpenBSD's rge(4).

  build-charon-netdrv-rge.py fetch DL_DIR
  build-charon-netdrv-rge.py DL_DIR OUT_DIR
  build-charon-netdrv-rge.py DL_DIR OUT_DIR --test-product 8139     (test image for an emulator, see below)

fetch   downloads the pinned OpenBSD sources (one commit, SHA-256 checked) into
        DL_DIR/openbsd-rge. They are only compiled, never run on the host.
build   (two arguments) checks every source against the SHA-256 table below, compiles
        sys/dev/pci/if_rge.c UNMODIFIED together with the kernel shim and the UEFI face of
        src/netdrv/rge-uefi with the LLVM of the Charon build (ELF objects, long = 64 bit as the
        OpenBSD code expects), links them and converts the ELF image to PE32+ with ImageTool
        exactly as tools/build-charon-windows.py does. Writes OUT_DIR/nic-17-realtek-rge.efi,
        OUT_DIR/rge.json (what the pack manifest needs) and OUT_DIR/sources.json.

--test-product HEX builds OUT_DIR/build-rge-test/TEST-ONLY-*.efi instead: the same driver that additionally binds
        the Realtek product HEX (8139: the RTL8139 QEMU emulates). rge_attach() then runs up to the chip revision
        check and gives up, which exercises binding, the DMA buffer pool, register mapping, the log variable and
        the error path in an emulator. Never put that image into a pack.

The driver has NEVER RUN ON THE HARDWARE: no emulator has these cards. The image belongs in the
pack's experimental/ folder, which Charon does not load by itself.
Environment: CHARON_LLVM, CHARON_IMAGETOOL, CHARON_EDK2 (default ../llvm-23.1.5/bin,
../ha/BaseTools/Bin/Win64/ImageTool.exe, ../ha relative to the folder that holds the source tree).
"""
import hashlib,json,os,re,shutil,struct,subprocess,sys,urllib.request
from pathlib import Path

root=Path(__file__).resolve().parents[1]           # charon-src-r395
container=root.parent                              # work
llvm=Path(os.environ.get('CHARON_LLVM',container/'llvm-23.1.5/bin'))
imagetool=Path(os.environ.get('CHARON_IMAGETOOL',container/'ha/BaseTools/Bin/Win64/ImageTool.exe'))
edk2=Path(os.environ.get('CHARON_EDK2',container/'ha'))
glue=root/'src/netdrv/rge-uefi'

COMMIT='c02cc426d99791a205947bd7252d25c489605ff6'   # openbsd/src, 2026-11-22
BASE=f'https://raw.githubusercontent.com/openbsd/src/{COMMIT}/'
ISC='ISC';BSD2='BSD-2-Clause';BSD3='BSD-3-Clause'
REALTEK='Realtek firmware data licence (sys/dev/microcode/rge/rge-license: distribution permitted with the copyright notice)'
# path below sys/ -> (SHA-256, licence, compiled into the image)
SOURCES={
 'sys/dev/pci/if_rge.c':('6b746f5a53886d3801752c8ce43021cab6573d1952f1a1ba3a6177eefea05688',ISC,True),
 'sys/dev/pci/if_rgereg.h':('e82afb07526e3713bdcf43937d22d0f186c9a0ecadcab7fd34a63adfc0ddce46',ISC,True),
 'sys/dev/microcode/rge/rgemcode.h':('5e8f53c4ed97263ae6ac932720cec33128ba6a320055fbfe8f955f21d108ce48',REALTEK,True),
 'sys/dev/microcode/rge/rge-license':('304f78e02ee997c2294baffd3fd367b516d09ba212943e003adc460db26ce1ad',REALTEK,False),
 'sys/dev/microcode/rge/build.c':('a3aa86c38f6438440509ae9f0e9cb90f50c0f2784a9b88e318f3d773bc061f7b',ISC,False),   # reference for rge_fw.c
 'sys/dev/microcode/rge/Makefile':('df43ca568cbe704483db23078c76a61a418b77469da7b311346bb7774c941012','OpenBSD build file',False),
 'sys/dev/pci/pcireg.h':('d40816c6e2bc84eae2c87a867db7474b5edbac874d21e4e62b024d8a91de9e79','BSD-4-Clause (Christopher G. Demetriou, Charles Hannum)',True),
 'sys/dev/pci/pcidevs.h':('009f3ed06349d40e957d22ac8174505ea93881c82838360dda8e0a3a25124314','generated list of PCI IDs (OpenBSD)',True),
 'sys/dev/mii/mii.h':('0eaae895e30b0c45ae5cb216eeb1513b6e577bf38ebf260af16decfb3569e99f',BSD2,True),
 'sys/net/if_media.h':('b9ac532c1cd615e375bc306059552c920dbda3ed4f8602e8345ccdae12078dcc','BSD-2-Clause (NetBSD Foundation) and BSD-4-Clause (Jonathan Stone)',True),
 'sys/sys/queue.h':('a2abb97bd570e63c6f547174d2557d5d8c640bd669c61efdfd20d707e7a8dd13',BSD3,True),
}
# Headers the driver includes that the shim (obsd_compat.h) stands in for.
STUBS=['bpfilter.h','vlan.h','kstat.h','sys/param.h','sys/systm.h','sys/sockio.h','sys/mbuf.h','sys/malloc.h','sys/device.h','sys/endian.h',
 'sys/intrmap.h','sys/_null.h','net/if.h','net/toeplitz.h','netinet/in.h','netinet/ip.h','netinet/if_ether.h','netinet/tcp.h','netinet/tcp_timer.h',
 'netinet/tcp_var.h','netinet/udp.h','machine/bus.h','machine/intr.h','dev/pci/pcivar.h']
GLUE=['obsd_compat.h','obsd_kern.h','obsd_kern.c','rge_port.h','rge_fw.c','rge_obsd.c','rge_uefi.c']
IMAGE='nic-17-realtek-rge.efi'
CHIPS={0x3000:'Killer E3000 (RTL8125)',0x5000:'Killer E5000 (RTL8126)',0x8125:'RTL8125 / RTL8125B / RTL8125D (2.5G)',0x8126:'RTL8126 (5G)',0x8127:'RTL8127 (10G)'}

def sha256(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for block in iter(lambda:f.read(1<<20),b''):h.update(block)
    return h.hexdigest()

def fetch(dl):
    for rel in SOURCES:
        target=dl/'openbsd-rge'/rel
        if not target.exists():
            target.parent.mkdir(parents=True,exist_ok=True)
            print('download',BASE+rel)
            with urllib.request.urlopen(urllib.request.Request(BASE+rel,headers={'User-Agent':'netpack-fetch'})) as r:target.write_bytes(r.read())
    check(dl)

def check(dl):
    for rel,(digest,_,_) in SOURCES.items():
        target=dl/'openbsd-rge'/rel
        if not target.is_file():raise SystemExit(f'{target}: missing (run: fetch DL_DIR)')
        got=sha256(target)
        if got!=digest:raise SystemExit(f'{rel}: SHA-256 {got}, expected {digest}')
    print('sources ok:',len(SOURCES),'files of openbsd/src',COMMIT[:12])

def run(command,log):
    log.write('$ '+' '.join(map(str,command))+'\n');log.flush()
    result=subprocess.run(list(map(str,command)),capture_output=True,text=True)
    log.write(result.stdout+result.stderr);log.flush()
    if result.returncode:
        sys.stderr.write(result.stdout+result.stderr)
        raise SystemExit(f'FAILED ({result.returncode}): {command[0]} ... {command[-1]}')
    return result.stdout+result.stderr

def harden(path):
    """NX_COMPAT and a correct PE checksum, as build-charon-windows.py does for CHARON.EFI."""
    image=bytearray(path.read_bytes());pe=struct.unpack_from('<I',image,0x3c)[0]
    count=struct.unpack_from('<H',image,pe+6)[0];optional=pe+24;optionalSize=struct.unpack_from('<H',image,pe+20)[0]
    for i in range(count):
        at=optional+optionalSize+i*40;flags=struct.unpack_from('<I',image,at+36)[0]
        assert not (flags&0x20000000 and flags&0x80000000),'writable and executable section'
    struct.pack_into('<H',image,optional+70,struct.unpack_from('<H',image,optional+70)[0]|0x0100)
    struct.pack_into('<I',image,optional+64,0)
    total=0
    for at in range(0,len(image)-len(image)%2,2):
        total+=struct.unpack_from('<H',image,at)[0];total=(total&0xffff)+(total>>16)
    if len(image)%2:total+=image[-1];total=(total&0xffff)+(total>>16)
    struct.pack_into('<I',image,optional+64,((total&0xffff)+(total>>16)&0xffff)+len(image))
    path.write_bytes(bytes(image))

def inspect(path):
    """PE32+ facts of the finished image; raises when it is not a UEFI boot service driver for x86-64."""
    data=path.read_bytes();assert data[:2]==b'MZ','no MZ header'
    pe=struct.unpack_from('<I',data,0x3c)[0];assert data[pe:pe+4]==b'PE\0\0','no PE signature'
    machine,count=struct.unpack_from('<HH',data,pe+4);optional=pe+24;optionalSize=struct.unpack_from('<H',data,pe+20)[0]
    magic=struct.unpack_from('<H',data,optional)[0];subsystem=struct.unpack_from('<H',data,optional+68)[0]
    relocRva,relocSize=struct.unpack_from('<II',data,optional+112+5*8)
    sections=[]
    for i in range(count):
        at=optional+optionalSize+i*40
        sections.append(data[at:at+8].rstrip(b'\0').decode('ascii','replace'))
    facts=dict(format='PE32+' if magic==0x20b else hex(magic),machine=hex(machine),subsystem=subsystem,sections=sections,
        base_relocation_bytes=relocSize,entry_point_rva=hex(struct.unpack_from('<I',data,optional+16)[0]),
        section_alignment=struct.unpack_from('<I',data,optional+32)[0],dll_characteristics=hex(struct.unpack_from('<H',data,optional+70)[0]))
    assert magic==0x20b and machine==0x8664,'not a PE32+ x86-64 image'
    assert subsystem==11,f'subsystem {subsystem}, expected 11 (EFI boot service driver)'
    assert relocRva and relocSize,'no base relocations: the firmware could not load the image at another address'
    return facts

def build(dl,out,test=None):
    check(dl)
    for tool in (llvm/'clang.exe',llvm/'ld.lld.exe',llvm/'llvm-nm.exe',imagetool,edk2/'MdePkg/Include/Uefi.h'):
        if not Path(tool).exists():raise SystemExit(f'{tool}: missing')
    out.mkdir(parents=True,exist_ok=True)
    work=out/('build-rge-test' if test else 'build-rge')
    if work.exists():shutil.rmtree(work)
    (work/'stub').mkdir(parents=True)
    for name in STUBS:
        target=work/'stub'/name;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text('/* provided by the Charon kernel shim */\n#include "obsd_compat.h"\n',newline='\n')
    log=(work/'build.log').open('w',encoding='utf-8')
    clang=llvm/'clang.exe';lld=llvm/'ld.lld.exe';nm=llvm/'llvm-nm.exe'
    flags=['--target=x86_64-unknown-elf']+'-O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -mcmodel=large -mno-red-zone -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -mgeneral-regs-only -std=gnu11 -Wall'.split()   # no SSE: nothing of the firmware's xmm6-15 can be clobbered
    sysdir=dl/'openbsd-rge/sys'
    obsd=flags+['-I',glue,'-I',work/'stub','-I',sysdir]
    # edk2's GCC-family builds define EFIAPI on the command line; without it the headers leave it empty (System V calls).
    uefi=flags+['-Wextra','-Werror','-fshort-wchar','-DEFIAPI=__attribute__((ms_abi))','-I',glue,'-I',edk2/'MdePkg/Include','-I',edk2/'MdePkg/Include/X64']
    if test:obsd=obsd+[f'-DNETDRV_TEST_PRODUCT=0x{test:04x}']
    units=[('rge_obsd',obsd),                               # if_rge.c itself (included unmodified): its own warnings are left alone
           ('obsd_kern',obsd+['-Wextra','-Werror','-Wno-unused-parameter']),
           ('rge_fw',obsd+['-Wextra','-Werror']),
           ('rge_uefi',uefi)]
    objects=[]
    for name,options in units:
        target=work/(name+'.o');run([clang,*options,'-c',glue/(name+'.c'),'-o',target],log);objects.append(target)
    run([lld,'-r','--gc-sections','--undefined=efi_main','-o',work/'closed.o',*objects],log)
    undefined=run([nm,'-u',work/'closed.o'],log).strip()
    if undefined:raise SystemExit('unresolved symbols:\n'+undefined)
    (work/'image.lds').write_text('''OUTPUT_FORMAT(elf64-x86-64)
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
    run([lld,'--emit-relocs','-z','max-page-size=4096','-T',work/'image.lds',work/'closed.o','-o',work/'rge.elf'],log)
    image=work/f'TEST-ONLY-rge-also-binds-10ec-{test:04x}.efi' if test else out/IMAGE
    run([imagetool,'GenImage','-c','PE','-t','UEFI_DRIVER','-b','0x100000','-o',image,work/'rge.elf'],log)
    harden(image)
    facts=inspect(image)
    if test:
        log.close();print('TEST image (never ship it):',image);return
    text=(sysdir/'dev/pci/if_rge.c').read_text(encoding='utf-8',errors='replace')
    devs=(sysdir/'dev/pci/pcidevs.h').read_text(encoding='utf-8',errors='replace')
    table=re.search(r'rge_devices\[\]\s*=\s*\{(.*?)\};',text,re.S).group(1)
    ids=[]
    for vendor,product in re.findall(r'\{\s*PCI_VENDOR_(\w+),\s*PCI_PRODUCT_(\w+)\s*\}',table):
        v=int(re.search(rf'#define\s+PCI_VENDOR_{vendor}\s+(0x[0-9a-fA-F]+)',devs).group(1),16)
        p=int(re.search(rf'#define\s+PCI_PRODUCT_{product}\s+(0x[0-9a-fA-F]+)',devs).group(1),16)
        ids.append(f'pci:{v:04x}:{p:04x} {CHIPS.get(p,product)}')
    version=re.search(r'\$OpenBSD: (if_rge\.c,v [0-9.]+ [0-9/]+)',text).group(1)
    sources=[dict(path=rel,url=BASE+rel,sha256=digest,licence=licence,compiled=compiled) for rel,(digest,licence,compiled) in SOURCES.items()]
    sources+=[dict(path='src/netdrv/rge-uefi/'+name,url='Charon source tree',sha256=sha256(glue/name),licence=ISC,compiled=True) for name in GLUE]
    (out/'sources.json').write_text(json.dumps(sources,indent=1)+'\n',encoding='utf-8',newline='\n')
    manifest=dict(file=IMAGE,kind='network adapter driver (UEFI driver model, Simple Network Protocol)',
        name='rge (OpenBSD) for Realtek RTL8125/RTL8126/RTL8127',
        version=f'OpenBSD {version}, openbsd/src commit {COMMIT}; Charon UEFI shim r407',
        description='Realtek RTL8125/8125B/8125D (2.5G), RTL8126 (5G), RTL8127 (10G), Killer E3000/E5000: OpenBSD rge(4) compiled unmodified on a kernel API shim, polled, with a Simple Network Protocol on a child handle. MAC and PHY patch tables (Realtek) are built in.',
        state='experimental, never run on the hardware (compiled, linked, PE checked; load test in QEMU only)',
        autoload=False,
        source='https://github.com/openbsd/src/tree/'+COMMIT+'/sys/dev/pci (if_rge.c, if_rgereg.h), sys/dev/microcode/rge (rgemcode.h); glue: src/netdrv/rge-uefi',
        source_files='sources.json',sha256=sha256(image),size=image.stat().st_size,
        licence='ISC (driver and glue); Realtek firmware data licence for the patch tables (rge-license); BSD-2/3/4-Clause headers',
        licence_files=['sys/dev/microcode/rge/rge-license'],devices=ids,image=facts,
        toolchain=run([clang,'--version'],log).splitlines()[0],built_with='tools/build-charon-netdrv-rge.py')
    (out/'rge.json').write_text(json.dumps(manifest,indent=1)+'\n',encoding='utf-8',newline='\n')
    log.close()
    print(json.dumps(manifest,indent=1))
    print('PASS build of',image,'- compiled and linked only; the driver has never run on the hardware')

if __name__=='__main__':
    args=sys.argv[1:]
    if len(args)==2 and args[0]=='fetch':fetch(Path(args[1]).resolve())
    elif len(args)==2:build(Path(args[0]).resolve(),Path(args[1]).resolve())
    elif len(args)==4 and args[2]=='--test-product':build(Path(args[0]).resolve(),Path(args[1]).resolve(),int(args[3],16))
    else:sys.exit(__doc__)
