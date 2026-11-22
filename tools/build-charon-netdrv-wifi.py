"""r407: build wifi-50-mediatek-mt7921-mwx.efi, an EXPERIMENTAL UEFI Wi-Fi driver for MediaTek
MT7921 / MT7922 (also the MT7920 and MT7925 IDs of the same driver) made from OpenBSD's mwx(4)
and net80211 (WPA2-PSK 4-way handshake and CCMP in the stack).

  build-charon-netdrv-wifi.py fetch DL_DIR
  build-charon-netdrv-wifi.py DL_DIR OUT_DIR
  build-charon-netdrv-wifi.py DL_DIR OUT_DIR --test-id 8086:100e     (test image for an emulator, see below)

fetch   downloads the pinned OpenBSD sources (one commit, SHA-256 checked) into
        DL_DIR/openbsd-wifi. They are only compiled, never run on the host.
build   checks every source against the SHA-256 table below, compiles sys/dev/pci/if_mwx.c, 17 files
        of sys/net80211, 11 of sys/crypto and sys/kern/subr_tree.c UNMODIFIED together with the
        kernel shim and the UEFI face of src/netdrv/wifi-uefi with the LLVM of the Charon build (ELF
        objects, long = 64 bit as the OpenBSD code expects), links them and converts the ELF image
        to PE32+ with ImageTool exactly as tools/build-charon-windows.py does. Writes
        OUT_DIR/wifi-50-mediatek-mt7921-mwx.efi, OUT_DIR/mwx.json and OUT_DIR/sources.json.

--test-id VVVV:PPPP builds OUT_DIR/build-mwx-test/TEST-ONLY-*.efi instead: the same driver that additionally binds
        the PCI device VVVV:PPPP (8086:100e: the e1000 QEMU emulates) and treats it as an MT7921. mwx_attach() then runs
        against a chip that does not answer, which exercises binding, the DMA buffer pool, register mapping,
        net80211's attach, the firmware file reader and the error path in an emulator. Never put that image into a pack.

THE DRIVER HAS NEVER RUN: no emulator has these cards and no such computer was available. It
compiles and links; nothing more is known. The image belongs in the pack's experimental/ folder,
which Charon does not load by itself. It needs two firmware files of the chip maker in the folder
EFI/HAK/firmware (see FIRMWARE below); they are not downloaded or shipped by this tool.
Environment: CHARON_LLVM, CHARON_IMAGETOOL, CHARON_EDK2 (default ../llvm-23.1.5/bin,
../ha/BaseTools/Bin/Win64/ImageTool.exe, ../ha relative to the folder that holds the source tree).
"""
import hashlib,json,os,re,shutil,struct,subprocess,sys,urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

root=Path(__file__).resolve().parents[1]           # charon-src-r395
container=root.parent                              # work
llvm=Path(os.environ.get('CHARON_LLVM',container/'llvm-23.1.5/bin'))
imagetool=Path(os.environ.get('CHARON_IMAGETOOL',container/'ha/BaseTools/Bin/Win64/ImageTool.exe'))
edk2=Path(os.environ.get('CHARON_EDK2',container/'ha'))
glue=root/'src/netdrv/wifi-uefi'

COMMIT='c02cc426d99791a205947bd7252d25c489605ff6'   # openbsd/src, 2026-11-22
BASE=f'https://raw.githubusercontent.com/openbsd/src/{COMMIT}/'
# path -> (SHA-256, licence as the file header states it, use: 'c' compiled, 'h' header, 'ref' reference only)
SOURCES={
 'sys/dev/pci/if_mwx.c':('0a12d78af0264c48157ef8ac9ab3de11c2bbb7342fc63cfff960cf484c1c608d','ISC','c'),
 'sys/net80211/ieee80211.c':('2352bb96d782c335fdeafd9dd55a47f90d6dfd672071b19310e91465c62b66a7','BSD-3-Clause','c'),
 'sys/net80211/ieee80211_node.c':('6842049c5483eba99bf64d6f3898ec60e8c762e9906acbee556ddb5ffcb79af2','BSD-3-Clause','c'),
 'sys/net80211/ieee80211_input.c':('78ee806c2ec6c0b2df70ecd8c5596627e89a2309d1dbbf78ace0da348b36172e','BSD-3-Clause','c'),
 'sys/net80211/ieee80211_output.c':('7ffb88fb0a940703532898f570e494dd5e0b92ec2559286d7d7caecb6df82e02','BSD-3-Clause','c'),
 'sys/net80211/ieee80211_proto.c':('0a2d5e107b1a5370a705eb58f017e98fafe2e2969c4af6decdb88f171edf1458','BSD-3-Clause','c'),
 'sys/net80211/ieee80211_crypto.c':('0f9c541ab0fbcdbeb5000ca8b64432191fc876770a39d389e8d4a07b00cf3627','ISC','c'),
 'sys/net80211/ieee80211_crypto_ccmp.c':('a4d4cd2e819f04d939a2b9f84031597dd7e5f70663804225cf33954553d6a384','ISC','c'),
 'sys/net80211/ieee80211_crypto_tkip.c':('d2d87d5ced0f1d980d72be94213db0fd80601cec533074f303893d6b364f42c6','ISC','c'),
 'sys/net80211/ieee80211_crypto_bip.c':('51fd0257ba00d57e4a8f7db6f4d6edf8e125a44027dc717afc36cb172e034141','ISC','c'),
 'sys/net80211/ieee80211_crypto_wep.c':('13c31d007785f62e569d2edaca4a5f4567ab28da0a60f4d5a6a26ce8c814c039','ISC','c'),
 'sys/net80211/ieee80211_pae_input.c':('c5f57f3946d8fbf5c6466e77e97322e50d839a3504db7414f6dc9a0c455585ee','ISC','c'),
 'sys/net80211/ieee80211_pae_output.c':('059140a66b31da42d170048d4503221577698410bdc35aba4a8c25fbd0b084db','ISC','c'),
 'sys/net80211/ieee80211_regdomain.c':('f03578954688f18518ea276e79cd642f33b2a91acba191b70a7766a9804ac4e1','ISC','c'),
 'sys/net80211/ieee80211_amrr.c':('ff9067e24fa034ddf18b47f2c2d7603e1055694742ce2a236157e821eb7818f2','ISC','c'),
 'sys/net80211/ieee80211_ra.c':('463c01c38918dfa1cac20be93522c166e306543f7bf2a30c5acd8397aac21439','ISC','c'),
 'sys/net80211/ieee80211_ra_vht.c':('c7416e0adb637de6debec5ae5767a90a07888ebd1a9fb7a66808ab26448e163c','ISC','c'),
 'sys/net80211/ieee80211_rssadapt.c':('49f323bded5482990427462a80e61b7587509559902b047dd102e8b3b4c5323f','BSD-2-Clause','c'),
 'sys/crypto/aes.c':('9ca2a8893a10f2cafaaf7992ba2d352021fe89d7a64a047cf0dbfc6cee7e252a','MIT','c'),
 'sys/crypto/cmac.c':('e7894d59d98986aa7992978b87fb48a7d688583df4ef21418135d5100a42f77f','ISC','c'),
 'sys/crypto/hmac.c':('6c7705018ff10f57ba33484e66d4b78b5b55d0ff44de749f3d8356e768225976','ISC','c'),
 'sys/crypto/sha1.c':('7c766579192275230a1c1a97d3e35e04fb61a62bf3914e6de6f9e23d66106107','public domain','c'),
 'sys/crypto/sha2.c':('642c119bfa35fa65e85435325f5f95ea015cd2c37bad2f78950608ae431ac08e','BSD-3-Clause','c'),
 'sys/crypto/md5.c':('68a887bac3d2ffae37f26e5960ca062ca5798b8e543d13c8acbf841080deede4','public domain','c'),
 'sys/crypto/key_wrap.c':('9d8b22e45163d91a3ff22541e23ef6032fb8fad1f104119db6992fa2f3465048','ISC','c'),
 'sys/crypto/michael.c':('30e4618382f747b4187c282a385ef41f072111a6178121608d2b8903051aebc1','ISC','c'),
 'sys/crypto/arc4.c':('fa31eb41c8b94d8170bbd223e0bfa30aded20e18192c83d05c7de57d3bf40a41','ISC','c'),
 'sys/crypto/rijndael.c':('291cb8aa1f73d06112812f4493fa14a328db7329a6412cc4aa84fb86e59017cb','public domain','c'),
 'sys/crypto/gmac.c':('ef19874965872d6b8ae7086dbef07c9d8ef671aaf4a48cbbe57347d90e2cb483','ISC','c'),
 'sys/kern/subr_tree.c':('67bfe1e844d30e9730b9703f4a34c7760a7d9139def7a2e1dfffbad689889a92','ISC','c'),
 'sys/dev/pci/if_mwxreg.h':('5daf2578eaabc62f5616b203544668652b62980e277490454874e25a52c0fecb','ISC','h'),
 'sys/dev/pci/pcireg.h':('d40816c6e2bc84eae2c87a867db7474b5edbac874d21e4e62b024d8a91de9e79','BSD-4-Clause','h'),
 'sys/dev/pci/pcidevs.h':('009f3ed06349d40e957d22ac8174505ea93881c82838360dda8e0a3a25124314','generated list of PCI IDs (OpenBSD)','h'),
 'sys/sys/queue.h':('a2abb97bd570e63c6f547174d2557d5d8c640bd669c61efdfd20d707e7a8dd13','BSD-3-Clause','h'),
 'sys/sys/tree.h':('cbbe65854ef9ee5ec400aa4127c4d87f0a4ed27e699cbdbe92d7db005842f793','BSD-2-Clause','h'),
 'sys/sys/sockio.h':('80258bda56dc85377ea6aee87367dc5c6be590708f820f5ed1946f1eb03ae098','BSD-3-Clause','h'),
 'sys/net/if_media.h':('b9ac532c1cd615e375bc306059552c920dbda3ed4f8602e8345ccdae12078dcc','BSD-4-Clause','h'),
 'sys/net/if_llc.h':('3a3a072df5b845322c4ac26f6b644c996317d3e3418baab62ac5ad4c72ccd94d','BSD-3-Clause','h'),
 'sys/net80211/ieee80211.h':('4b10494ab0322392180ced8d8935656a1cc6749ac1b2f033d4892de0e710cea4','BSD-2-Clause','h'),
 'sys/net80211/ieee80211_amrr.h':('869fd98e42fc7fc10061c8208611b63a6cbf3dcb364c36b4dee29723704c5129','ISC','h'),
 'sys/net80211/ieee80211_crypto.h':('fab3d17527279027b4be7c26a0ee8c565ff783a4cfe6cd8e3050bb6dc81f5f02','ISC','h'),
 'sys/net80211/ieee80211_ioctl.h':('14a0f187818db49f9def357ba0d4d92e703c1b98778b51e5d1e4b549c02d27c9','BSD-3-Clause','h'),
 'sys/net80211/ieee80211_node.h':('8575ae06513ac725b7213642ea90e941de9fa89a41f2a256f26daece6c2704dc','BSD-3-Clause','h'),
 'sys/net80211/ieee80211_priv.h':('cc0ea133d9a17e9288f161e368ef0c256ca7d5269abab5564df216c0b484d00e','ISC','h'),
 'sys/net80211/ieee80211_proto.h':('f67c557aaaaf32a97697b08367609ee8ec7eb6633b4e73a1441f21617505ef09','BSD-3-Clause','h'),
 'sys/net80211/ieee80211_ra.h':('a45e13171a9f60bf9f5768d417f3cda4b0a3bb9cd7bb6603415f54a854a16dd9','ISC','h'),
 'sys/net80211/ieee80211_ra_vht.h':('2f02c9dd8e8dc52d079d7b86c71e05b3fdb323e5bf61bc444d1734f8625a7558','ISC','h'),
 'sys/net80211/ieee80211_radiotap.h':('f0b5a1491820a6281bcf2fb9ac68a4c70229f00f5b28363143083b8f7d1137e6','BSD-2-Clause','h'),
 'sys/net80211/ieee80211_regdomain.h':('55480ba158fdb05f1b45eee1b78748ce455105c4937f76c110df0477bd327220','ISC','h'),
 'sys/net80211/ieee80211_rssadapt.h':('4568760510668391333c5b336532c742bd41613924e5381da53b77731255d349','BSD-2-Clause','h'),
 'sys/net80211/ieee80211_var.h':('9d0a7889a3e1ad9acd875abf31c2f6d4ec2bc8ef6e8326f3970f0a7a46855911','BSD-3-Clause','h'),
 'sys/crypto/aes.h':('307782654e0b8456400482c1cb8e16814dee77a9757fac723d43f3cdf1c3cdc9','MIT','h'),
 'sys/crypto/arc4.h':('a6579093cf5be1ab49d8f6182e795fe32cc96bd54387654f840229a3f1f5c544','ISC','h'),
 'sys/crypto/cmac.h':('36386e0a27ecae801642fee329f1b6defb80a82e940c862372e454f2c02c7533','ISC','h'),
 'sys/crypto/gmac.h':('fb223ffd8b69b1169607e54b0efddc802c44cf6445529c50e8ddc932e5ba2783','ISC','h'),
 'sys/crypto/hmac.h':('813949e998fece5cd258579b9df412356ed957f43dbfe6d861f01a789dd349e0','ISC','h'),
 'sys/crypto/key_wrap.h':('31866ff450d1d44bbb2a4c77d23506f3b23b0cb5b3d3009e8f5de5aecbd4db62','ISC','h'),
 'sys/crypto/md5.h':('88491f39b1b601b61dd6e6f6e499c25b5bd1cc3d6a26959100f92007d7025681','public domain','h'),
 'sys/crypto/michael.h':('dafbc38dfcb68eda7a3662a94066ba7ba653aeef9817e4dcf9d7175257c6e659','ISC','h'),
 'sys/crypto/rijndael.h':('fbd05c5fe8db3a666ee9215ed67ad575753490fd5323137997b7ce5c397e9028','public domain','h'),
 'sys/crypto/sha1.h':('c6c9fbec5445d81fa706cafd786f624d08f690edea024445d0f45e670269788f','public domain','h'),
 'sys/crypto/sha2.h':('56e8736d9f4580b49e4aa3edb33c83e6ea910717b62f61142fcacaefdfbdd34c','BSD-3-Clause','h'),
 'sys/net80211/ieee80211_ioctl.c':('49600030248c0b5932311a40869f48b36744404d1af264fe510c785cad87715e','BSD-3-Clause','ref'),
}
# Headers the OpenBSD code includes that the shim (obsd_compat.h + obsd_wifi.h) stands in for.
STUBS=['bpfilter.h','vlan.h','bridge.h','kstat.h','sys/param.h','sys/systm.h','sys/atomic.h','sys/socket.h','sys/kernel.h','sys/mbuf.h',
 'sys/endian.h','sys/errno.h','sys/malloc.h','sys/sysctl.h','sys/types.h','sys/time.h','sys/task.h','sys/syslog.h','sys/stdint.h',
 'sys/timeout.h','sys/device.h','sys/rwlock.h','sys/mutex.h','sys/ioccom.h','sys/ioctl.h','sys/_null.h','machine/bus.h','machine/intr.h',
 'dev/pci/pcivar.h','net/bpf.h','net/if.h','net/if_dl.h','net/if_arp.h','net/if_var.h','net/route.h','net/if_vlan_var.h','net/if_bridge.h',
 'netinet/in.h','netinet/if_ether.h','netinet/ip.h','netinet/ip6.h']
GLUE=['obsd_compat.h','obsd_wifi.h','obsd_kern.h','obsd_kern.c','rge_port.h','wifi_port.h','wifi_kern.c','stubs.c','mwx_obsd.c','wifi_face.inc','wifi_uefi.c']
IMAGE='wifi-50-mediatek-mt7921-mwx.efi'
CHIPS={'MT7920':'MT7920','MT7921':'MT7921 (AMD RZ608)','MT7921K':'MT7921K (AMD RZ608)','MT7922':'MT7922','RZ616':'AMD RZ616 (MT7922)',
       'MT7925':'MT7925','RZ717':'AMD RZ717 (MT7925)'}
# name loadfirmware() asks for (= file name in EFI/HAK/firmware), file of linux-firmware it corresponds to, chip
FIRMWARE=[('mwx-mt7961_patch_mcu_1_2_hdr','mediatek/WIFI_MT7961_patch_mcu_1_2_hdr.bin','MT7921'),
          ('mwx-mt7961_ram_code_1','mediatek/WIFI_RAM_CODE_MT7961_1.bin','MT7921'),
          ('mwx-mt7922_patch_mcu_1_1_hdr','mediatek/WIFI_MT7922_patch_mcu_1_1_hdr.bin','MT7922'),
          ('mwx-mt7922_ram_code_1','mediatek/WIFI_RAM_CODE_MT7922_1.bin','MT7922'),
          ('mwx-mt7961_patch_mcu_1a_2_hdr','mediatek/WIFI_MT7961_patch_mcu_1a_2_hdr.bin','MT7920'),
          ('mwx-mt7961_ram_code_1a','mediatek/WIFI_RAM_CODE_MT7961_1a.bin','MT7920'),
          ('mwx-mt7925_patch_mcu_1_1_hdr','mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin','MT7925'),
          ('mwx-mt7925_ram_code_1_1','mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin','MT7925')]

def sha256(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for block in iter(lambda:f.read(1<<20),b''):h.update(block)
    return h.hexdigest()

def fetch(dl):
    for rel in SOURCES:
        target=dl/'openbsd-wifi'/rel
        if not target.exists():
            target.parent.mkdir(parents=True,exist_ok=True)
            print('download',BASE+rel)
            with urllib.request.urlopen(urllib.request.Request(BASE+rel,headers={'User-Agent':'netpack-fetch'})) as r:target.write_bytes(r.read())
    check(dl)

def check(dl):
    for rel,(digest,_,_) in SOURCES.items():
        target=dl/'openbsd-wifi'/rel
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

LDS="""OUTPUT_FORMAT(elf64-x86-64)
ENTRY(efi_main)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x1000;
 .text : ALIGN(0x1000) { *(.text .text.* .ltext .ltext.* .gnu.linkonce.t.*) } :text
 .rodata : ALIGN(0x1000) { *(.rodata .rodata.* .lrodata .lrodata.* .gnu.linkonce.r.*) } :rodata
 .data : ALIGN(0x1000) { *(.data.rel.ro .data.rel.ro.*) *(.got .got.*) *(.data .data.* .ldata .ldata.* .gnu.linkonce.d.*) *(.bss .bss.* .lbss .lbss.*) *(COMMON) *(LARGE_COMMON) } :data
 /DISCARD/ : { *(.group) *(.comment) *(.note.GNU-stack) *(.llvm_addrsig) }
}
"""

def build(dl,out,test=None):
    check(dl)
    for tool in (llvm/'clang.exe',llvm/'ld.lld.exe',llvm/'llvm-nm.exe',imagetool,edk2/'MdePkg/Include/Protocol/WiFi2.h'):
        if not Path(tool).exists():raise SystemExit(f'{tool}: missing')
    out.mkdir(parents=True,exist_ok=True)
    work=out/('build-mwx-test' if test else 'build-mwx')
    if work.exists():shutil.rmtree(work)
    (work/'stub').mkdir(parents=True)
    for name in STUBS:
        target=work/'stub'/name;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text('/* provided by the Charon kernel shim */\n#include "obsd_compat.h"\n',newline='\n')
    log=(work/'build.log').open('w',encoding='utf-8')
    clang=llvm/'clang.exe';lld=llvm/'ld.lld.exe';nm=llvm/'llvm-nm.exe'
    # -mgeneral-regs-only: no SSE, so nothing of the firmware's xmm6-15 can be clobbered across the ms_abi boundary.
    flags=['--target=x86_64-unknown-elf']+'-O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -mcmodel=large -mno-red-zone -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -mgeneral-regs-only -std=gnu11 -Wall'.split()
    sysdir=dl/'openbsd-wifi/sys'
    # The OpenBSD files keep their own warnings (pointer signedness, packed members); they are not changed.
    obsd=flags+['-Wno-unknown-attributes','-Wno-pointer-sign','-Wno-address-of-packed-member','-I',glue,'-I',work/'stub','-I',sysdir]
    # edk2's GCC-family builds define EFIAPI on the command line; without it the headers leave it empty (System V calls).
    uefi=flags+['-Wextra','-Werror','-fshort-wchar','-DEFIAPI=__attribute__((ms_abi))','-I',glue,'-I',edk2/'MdePkg/Include','-I',edk2/'MdePkg/Include/X64']
    units=[(glue/'mwx_obsd.c','mwx_obsd',obsd+([f'-DNETDRV_TEST_ID=0x{test:08x}u'] if test else [])),                 # includes sys/dev/pci/if_mwx.c unmodified
           (glue/'obsd_kern.c','obsd_kern',obsd+['-Wno-unused-parameter']),
           (glue/'wifi_kern.c','wifi_kern',obsd+['-Wextra','-Werror','-Wno-unused-parameter']),
           (glue/'stubs.c','stubs',obsd+['-Wextra','-Werror']),
           (glue/'wifi_uefi.c','wifi_uefi',uefi)]
    for rel,(_,_,use) in SOURCES.items():
        if use=='c' and not rel.endswith('if_mwx.c'):
            units.append((dl/'openbsd-wifi'/rel,rel[4:-2].replace('/','_'),obsd))
    def unit(item):
        source,name,options=item;target=work/(name+'.o')
        run([clang,*options,'-c',source,'-o',target],log);return target
    with ThreadPoolExecutor(max_workers=8) as pool:objects=list(pool.map(unit,units))
    run([lld,'-r','--gc-sections','--undefined=efi_main','-o',work/'closed.o',*objects],log)
    undefined=run([nm,'-u',work/'closed.o'],log).strip()
    if undefined:raise SystemExit('unresolved symbols:\n'+undefined)
    (work/'image.lds').write_text(LDS,newline='\n')
    run([lld,'--emit-relocs','-z','max-page-size=4096','-T',work/'image.lds',work/'closed.o','-o',work/'mwx.elf'],log)
    image=work/f'TEST-ONLY-mwx-also-binds-{test>>16:04x}-{test&0xffff:04x}.efi' if test else out/IMAGE
    run([imagetool,'GenImage','-c','PE','-t','UEFI_DRIVER','-b','0x100000','-o',image,work/'mwx.elf'],log)
    harden(image)
    facts=inspect(image)
    if test:
        log.close();print('TEST image (never ship it):',image);return
    text=(sysdir/'dev/pci/if_mwx.c').read_text(encoding='utf-8',errors='replace')
    devs=(sysdir/'dev/pci/pcidevs.h').read_text(encoding='utf-8',errors='replace')
    table=re.search(r'mwx_devices\[\]\s*=\s*\{(.*?)\};',text,re.S).group(1)
    ids=[]
    for vendor,product in re.findall(r'\{\s*PCI_VENDOR_(\w+),\s*PCI_PRODUCT_(\w+)\s*\}',table):
        v=int(re.search(rf'#define\s+PCI_VENDOR_{vendor}\s+(0x[0-9a-fA-F]+)',devs).group(1),16)
        p=int(re.search(rf'#define\s+PCI_PRODUCT_{product}\s+(0x[0-9a-fA-F]+)',devs).group(1),16)
        ids.append(f'pci:{v:04x}:{p:04x} {CHIPS.get(product.split("_",1)[1],product)}')
    version=re.search(r'\$OpenBSD: (if_mwx\.c,v [0-9.]+ [0-9/]+)',text).group(1)
    stubs=re.findall(r'^([a-z_0-9]+)\(',(glue/'stubs.c').read_text(encoding='utf-8'),re.M)
    sources=[dict(path=rel,url=BASE+rel,sha256=digest,licence=licence,use={'c':'compiled','h':'header','ref':'reference only'}[use]) for rel,(digest,licence,use) in SOURCES.items()]
    sources+=[dict(path='src/netdrv/wifi-uefi/'+name,url='Charon source tree',sha256=sha256(glue/name),licence='ISC',use='compiled') for name in GLUE]
    (out/'sources.json').write_text(json.dumps(sources,indent=1)+'\n',encoding='utf-8',newline='\n')
    manifest=dict(file=IMAGE,kind='Wi-Fi adapter driver (UEFI driver model: Simple Network Protocol, EFI_WIRELESS_MAC_CONNECTION_II, EFI_SUPPLICANT)',
        name='mwx + net80211 (OpenBSD) for MediaTek MT7921/MT7922',
        version=f'OpenBSD {version}, openbsd/src commit {COMMIT}; Charon UEFI shim r407',
        description='MediaTek MT7921/MT7922 (AMD RZ608/RZ616) PCIe Wi-Fi: OpenBSD mwx(4) and net80211 compiled unmodified on a kernel API shim, polled. WPA2-PSK (4-way handshake, CCMP) and open networks only. Needs the chip firmware files in the folder EFI/HAK/firmware.',
        state='experimental, never run on the hardware (compiled, linked, PE checked; loaded twice in QEMU/OVMF without a matching device: starts, relocates, driver binding listed)',
        autoload=False,
        source='https://github.com/openbsd/src/tree/'+COMMIT+'/sys (dev/pci/if_mwx.c, net80211, crypto, kern/subr_tree.c); glue: src/netdrv/wifi-uefi',
        source_files='sources.json',sha256=sha256(image),size=image.stat().st_size,
        licence='ISC (driver, most of net80211 and crypto, glue); BSD-2/3/4-Clause, MIT and public domain files as listed in sources.json',
        devices=ids,
        firmware=[dict(name=name,linux_firmware=lf,chip=chip,folder='EFI/HAK/firmware',shipped=False) for name,lf,chip in FIRMWARE],
        firmware_licence='linux-firmware LICENCE.mediatek (redistributable binary) / OpenBSD package mwx-firmware. Not shipped and not downloaded by this tool.',
        stubbed_kernel_functions=stubs,image=facts,
        toolchain=run([clang,'--version'],log).splitlines()[0],built_with='tools/build-charon-netdrv-wifi.py')
    (out/'mwx.json').write_text(json.dumps(manifest,indent=1)+'\n',encoding='utf-8',newline='\n')
    log.close()
    print(IMAGE,image.stat().st_size,'bytes, SHA-256',manifest['sha256'])
    print('PE:',facts)
    print('NEVER RUN ON THE HARDWARE. Experimental; keep it out of the automatically loaded set.')

if __name__=='__main__':
    args=sys.argv[1:]
    if len(args)==2 and args[0]=='fetch':fetch(Path(args[1]).resolve())
    elif len(args)==2:build(Path(args[0]).resolve(),Path(args[1]).resolve())
    elif len(args)==4 and args[2]=='--test-id':build(Path(args[0]).resolve(),Path(args[1]).resolve(),int(args[3].replace(':',''),16))
    else:sys.exit(__doc__)
