"""r407: build nic-17-atheros-alc.efi, a UEFI network driver for Qualcomm Atheros AR8131/AR8132/
AR8151/AR8152 (L1C/L2C/L1D), AR8161/AR8162/AR8171/AR8172 and Killer E2200/E2400/E2500, made from
OpenBSD's alc(4) with OpenBSD's MII layer and atphy(4)/ukphy(4).

  build-charon-netdrv-alc.py fetch DL_DIR
  build-charon-netdrv-alc.py DL_DIR OUT_DIR
  build-charon-netdrv-alc.py DL_DIR OUT_DIR --test-id 8086:100e [--force-link]
        TEST image (OUT_DIR/build-alc-test, never ship it) that also binds an emulator's device, here QEMU's
        e1000: the whole driver then runs against registers that answer nonsense. --force-link also pretends
        a link, so that the firmware's network stack transmits into the deaf device.

Same method as tools/build-charon-netdrv-rge.py (whose helpers are used): the pinned OpenBSD sources
of DL_DIR/openbsd-alc are SHA-256 checked and compiled UNMODIFIED against the kernel shim and the
UEFI face of src/netdrv/rge-uefi (built with OBSD_COMPAT_MII: old-style start routine, MII bus, a
miniature autoconf for the PHY). Writes OUT_DIR/nic-17-atheros-alc.efi, alc.json, alc-sources.json.

The driver has NEVER RUN ON THE HARDWARE: no emulator has these cards. The image belongs in the
pack's experimental/ folder, which Charon does not load by itself.
"""
import importlib.util,json,re,shutil,sys,urllib.request
from pathlib import Path

spec=importlib.util.spec_from_file_location('netdrv_rge',Path(__file__).with_name('build-charon-netdrv-rge.py'))
rge=importlib.util.module_from_spec(spec);spec.loader.exec_module(rge)
COMMIT=rge.COMMIT;BASE=rge.BASE;glue=rge.glue;llvm=rge.llvm;imagetool=rge.imagetool;edk2=rge.edk2
BSD2='BSD-2-Clause'
SOURCES={
 'sys/dev/pci/if_alc.c':('547f1056e431f5764a7a35642ac9269219b8c048f534ae545ea678837b8c9ff2',BSD2+' (Pyun YongHyeon)',True),
 'sys/dev/pci/if_alcreg.h':('62d72df0f0f2a509362e1bb8b18dfd4345d379e294c7b587dcec0a4871b3f301',BSD2+' (Pyun YongHyeon)',True),
 'sys/dev/mii/mii.c':('b61a31f0bc697ee1af8f8dd6e491de6366a2800d366db9aa43ce2230ebba8fd0',BSD2+' (NetBSD Foundation)',True),
 'sys/dev/mii/mii_physubr.c':('38f8bc6ab3f0693371536d53f3dcc9761ca06ee22d0591423836b9bd4b488aa4',BSD2+' (NetBSD Foundation)',True),
 'sys/dev/mii/miivar.h':('c5c43f761e2c7c84ae87c1b80135dfa360e0c44900afc69e4d89250fcec132b2',BSD2+' (NetBSD Foundation)',True),
 'sys/dev/mii/miidevs.h':('a6dceec21dd3980ec991ed4c7bcfdcedaa1d01bfdc17dd262992acaafc2eb17c','generated list of PHY IDs (OpenBSD)',True),
 'sys/dev/mii/atphy.c':('02e26037f63df8a4139ead5e171560eb49b0ea287c5981c2c3a5c1842c43fd79',BSD2+' (Pyun YongHyeon)',True),
 'sys/dev/mii/ukphy.c':('a6b4759902ea7c063f2dc1a2e6a0e847676d79463e7e0c63f3c87e3bee2eb554',BSD2+' (NetBSD Foundation) and BSD-2-Clause (Frank van der Linden)',True),
 'sys/dev/mii/ukphy_subr.c':('e5bd2d11b53503fc06b8732b04415efe277bb9d355d789fffc1aeb3560992510',BSD2+' (NetBSD Foundation)',True),
 'sys/dev/mii/mii.h':('0eaae895e30b0c45ae5cb216eeb1513b6e577bf38ebf260af16decfb3569e99f',BSD2,True),
 'sys/dev/pci/pcireg.h':('d40816c6e2bc84eae2c87a867db7474b5edbac874d21e4e62b024d8a91de9e79','BSD-4-Clause (Christopher G. Demetriou, Charles Hannum)',True),
 'sys/dev/pci/pcidevs.h':('009f3ed06349d40e957d22ac8174505ea93881c82838360dda8e0a3a25124314','generated list of PCI IDs (OpenBSD)',True),
 'sys/net/if_media.h':('b9ac532c1cd615e375bc306059552c920dbda3ed4f8602e8345ccdae12078dcc','BSD-2-Clause (NetBSD Foundation) and BSD-4-Clause (Jonathan Stone)',True),
 'sys/sys/queue.h':('a2abb97bd570e63c6f547174d2557d5d8c640bd669c61efdfd20d707e7a8dd13','BSD-3-Clause',True),
}
STUBS=rge.STUBS+['sys/timeout.h','sys/kernel.h','sys/socket.h','sys/errno.h','net/if_dl.h','net/if_var.h','net/bpf.h']
GLUE=['obsd_compat.h','obsd_kern.h','obsd_kern.c','rge_port.h','alc_obsd.c','rge_uefi.c']
MII=['mii','mii_physubr','atphy','ukphy','ukphy_subr']
IMAGE='nic-17-atheros-alc.efi'
CHIPS={0x1062:'AR8132 (L2C) 10/100',0x1063:'AR8131 (L1C) gigabit',0x1073:'AR8151 v1 (L1D) gigabit',0x1083:'AR8151 v2 (L1D) gigabit',0x2060:'AR8152 v1 (L2C) 10/100',
 0x2062:'AR8152 v2 (L2C) 10/100',0x1091:'AR8161 gigabit',0x1090:'AR8162 10/100',0x10a1:'AR8171 gigabit',0x10a0:'AR8172 10/100',0xe091:'Killer E2200',0xe0a1:'Killer E2400',0xe0b1:'Killer E2500'}

LDS='''OUTPUT_FORMAT(elf64-x86-64)
ENTRY(efi_main)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x1000;
 .text : ALIGN(0x1000) { *(.text .text.* .ltext .ltext.* .gnu.linkonce.t.*) } :text
 .rodata : ALIGN(0x1000) { *(.rodata .rodata.* .lrodata .lrodata.* .gnu.linkonce.r.*) } :rodata
 .data : ALIGN(0x1000) { *(.data.rel.ro .data.rel.ro.*) *(.got .got.*) *(.data .data.* .ldata .ldata.* .gnu.linkonce.d.*) *(.bss .bss.* .lbss .lbss.*) *(COMMON) *(LARGE_COMMON) } :data
 /DISCARD/ : { *(.group) *(.comment) *(.note.GNU-stack) *(.llvm_addrsig) }
}
'''

def check(dl):
    for rel,(digest,_,_) in SOURCES.items():
        target=dl/'openbsd-alc'/rel
        if not target.is_file():raise SystemExit(f'{target}: missing (run: fetch DL_DIR)')
        got=rge.sha256(target)
        if got!=digest:raise SystemExit(f'{rel}: SHA-256 {got}, expected {digest}')
    print('sources ok:',len(SOURCES),'files of openbsd/src',COMMIT[:12])

def fetch(dl):
    for rel in SOURCES:
        target=dl/'openbsd-alc'/rel
        if not target.exists():
            target.parent.mkdir(parents=True,exist_ok=True)
            print('download',BASE+rel)
            with urllib.request.urlopen(urllib.request.Request(BASE+rel,headers={'User-Agent':'netpack-fetch'})) as r:target.write_bytes(r.read())
    check(dl)

def build(dl,out,test=None,force=False):
    check(dl)
    out.mkdir(parents=True,exist_ok=True)
    work=out/('build-alc-test' if test else 'build-alc')
    if work.exists():shutil.rmtree(work)
    (work/'stub').mkdir(parents=True)
    for name in STUBS:
        target=work/'stub'/name;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text('/* provided by the Charon kernel shim */\n#include "obsd_compat.h"\n',newline='\n')
    log=(work/'build.log').open('w',encoding='utf-8')
    clang=llvm/'clang.exe';lld=llvm/'ld.lld.exe';nm=llvm/'llvm-nm.exe'
    flags=['--target=x86_64-unknown-elf']+'-O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -mcmodel=large -mno-red-zone -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -mgeneral-regs-only -std=gnu11 -Wall'.split()
    sysdir=dl/'openbsd-alc/sys'
    obsd=flags+['-DOBSD_COMPAT_MII','-I',glue,'-I',work/'stub','-I',sysdir]
    uefi=flags+['-Wextra','-Werror','-fshort-wchar','-DEFIAPI=__attribute__((ms_abi))','-DNETDRV_NAME=L"Charon alc (OpenBSD) Atheros AR81xx / Killer E2x00"',
        '-DNETDRV_LOGVAR=L"CharonAlcLog"','-DNETDRV_STATVAR=L"CharonAlcStat"','-I',glue,'-I',edk2/'MdePkg/Include','-I',edk2/'MdePkg/Include/X64']
    if test:obsd=obsd+[f'-DNETDRV_TEST_VENDOR=0x{test[0]:04x}',f'-DNETDRV_TEST_PRODUCT=0x{test[1]:04x}']+(['-DNETDRV_TEST_FORCE_LINK'] if force else [])
    units=[(glue/'alc_obsd.c',obsd),(glue/'obsd_kern.c',obsd+['-Wextra','-Werror','-Wno-unused-parameter']),(glue/'rge_uefi.c',uefi)]
    units+=[(sysdir/'dev/mii'/(name+'.c'),obsd) for name in MII]      # OpenBSD files: their own warnings are left alone
    objects=[]
    for source,options in units:
        target=work/(source.stem+'.o');rge.run([clang,*options,'-c',source,'-o',target],log);objects.append(target)
    rge.run([lld,'-r','--gc-sections','--undefined=efi_main','-o',work/'closed.o',*objects],log)
    undefined=rge.run([nm,'-u',work/'closed.o'],log).strip()
    if undefined:raise SystemExit('unresolved symbols:\n'+undefined)
    (work/'image.lds').write_bytes(LDS.encode('ascii'))
    rge.run([lld,'--emit-relocs','-z','max-page-size=4096','-T',work/'image.lds',work/'closed.o','-o',work/'alc.elf'],log)
    image=work/f'TEST-ONLY-alc-also-binds-{test[0]:04x}-{test[1]:04x}.efi' if test else out/IMAGE
    rge.run([imagetool,'GenImage','-c','PE','-t','UEFI_DRIVER','-b','0x100000','-o',image,work/'alc.elf'],log)
    rge.harden(image)
    facts=rge.inspect(image)
    if test:
        log.close();print('TEST image (never ship it):',image);return
    text=(sysdir/'dev/pci/if_alc.c').read_text(encoding='utf-8',errors='replace')
    devs=(sysdir/'dev/pci/pcidevs.h').read_text(encoding='utf-8',errors='replace')
    table=re.search(r'alc_devices\[\]\s*=\s*\{(.*?)\};',text,re.S).group(1)
    ids=[]
    for vendor,product in re.findall(r'\{\s*PCI_VENDOR_(\w+),\s*PCI_PRODUCT_(\w+)\s*\}',table):
        v=int(re.search(rf'#define\s+PCI_VENDOR_{vendor}\s+(0x[0-9a-fA-F]+)',devs).group(1),16)
        p=int(re.search(rf'#define\s+PCI_PRODUCT_{product}\s+(0x[0-9a-fA-F]+)',devs).group(1),16)
        ids.append(f'pci:{v:04x}:{p:04x} {CHIPS.get(p,product)}')
    version=re.search(r'\$OpenBSD: (if_alc\.c,v [0-9.]+ [0-9/]+)',text).group(1)
    sources=[dict(path=rel,url=BASE+rel,sha256=digest,licence=licence,compiled=compiled) for rel,(digest,licence,compiled) in SOURCES.items()]
    sources+=[dict(path='src/netdrv/rge-uefi/'+name,url='Charon source tree',sha256=rge.sha256(glue/name),licence='ISC',compiled=True) for name in GLUE]
    (out/'alc-sources.json').write_text(json.dumps(sources,indent=1)+'\n',encoding='utf-8',newline='\n')
    manifest=dict(file=IMAGE,kind='network adapter driver (UEFI driver model, Simple Network Protocol)',
        name='alc (OpenBSD) for Qualcomm Atheros AR813x/AR815x/AR816x/AR817x and Killer E2200/E2400/E2500',
        version=f'OpenBSD {version}, openbsd/src commit {COMMIT}; Charon UEFI shim r407',
        description='Qualcomm Atheros AR8131/AR8132/AR8151/AR8152, AR8161/AR8162/AR8171/AR8172, Killer E2200/E2400/E2500: OpenBSD alc(4) with OpenBSD mii, atphy and ukphy, all compiled unmodified on a kernel API shim, polled, with a Simple Network Protocol on a child handle.',
        state='experimental, never run on the hardware (compiled, linked, PE checked; load test in QEMU only)',
        autoload=False,
        source='https://github.com/openbsd/src/tree/'+COMMIT+'/sys/dev/pci (if_alc.c, if_alcreg.h), sys/dev/mii (mii.c, mii_physubr.c, atphy.c, ukphy.c, ukphy_subr.c); glue: src/netdrv/rge-uefi',
        source_files='alc-sources.json',sha256=rge.sha256(image),size=image.stat().st_size,
        licence='BSD-2-Clause (driver, MII layer, PHY drivers); ISC (glue); BSD-2/3/4-Clause headers',
        licence_files=[],devices=ids,image=facts,
        toolchain=rge.run([clang,'--version'],log).splitlines()[0],built_with='tools/build-charon-netdrv-alc.py')
    (out/'alc.json').write_text(json.dumps(manifest,indent=1)+'\n',encoding='utf-8',newline='\n')
    log.close()
    print(json.dumps(manifest,indent=1))
    print('PASS build of',image,'- compiled and linked only; the driver has never run on the hardware')

if __name__=='__main__':
    args=sys.argv[1:]
    if len(args)==2 and args[0]=='fetch':fetch(Path(args[1]).resolve())
    elif len(args)==2:build(Path(args[0]).resolve(),Path(args[1]).resolve())
    elif len(args) in (4,5) and args[2]=='--test-id' and args[4:] in ([],['--force-link']):
        build(Path(args[0]).resolve(),Path(args[1]).resolve(),tuple(int(x,16) for x in args[3].split(':')),len(args)==5)
    else:sys.exit(__doc__)
