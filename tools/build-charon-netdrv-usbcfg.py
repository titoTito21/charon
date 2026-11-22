"""r407: build nic-usb-29-cdc-select.efi (src/netdrv/usb-cdc-select/UsbCdcConfigSelect.c).

  build-charon-netdrv-usbcfg.py DL_DIR OUT_DIR

The driver switches USB Ethernet adapters whose default configuration is maker-specific (Realtek
RTL8152/8153/8156/8157, Aquantia AQC111U/112U ...) to their CDC-ECM/NCM configuration, so the USB
class drivers of the driver pack can use them. See the header of the source file.

DL_DIR   work/netdrv-dl: the Linux sources the list of KNOWN devices is taken from are kept in
         DL_DIR/usbcfg-src (downloaded when missing, SHA-256 pinned, only read as text - never run).
         The image itself carries no ID table: it acts on what the device's descriptors say.
OUT_DIR  nic-usb-29-cdc-select.efi, usbcfg.json (name, version, source, sha256, licence, devices,
         state) and test-only/UsbCdcConfigSelectTest.efi (accepts QEMU's usb-net, prints its log;
         for the emulator test only - never put it into \\EFI\\HAK\\drivers).

Compiler: the LLVM of the Charon build (CHARON_LLVM, default work/llvm-23.1.5/bin) with the MdePkg
headers of the edk2 workspace (CHARON_EDK2, default work/ha); nothing is built inside the workspace.
"""
import hashlib,json,os,re,struct,subprocess,sys,urllib.request
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]            # charon-src-r395
CONTAINER=ROOT.parents[1]                           # kont
LLVM=Path(os.environ.get('CHARON_LLVM',CONTAINER/'work/llvm-23.1.5/bin'))
EDK2=Path(os.environ.get('CHARON_EDK2',CONTAINER/'work/ha'))
SOURCE=ROOT/'src/netdrv/usb-cdc-select/UsbCdcConfigSelect.c'
VERSION='1.0 (r407)'
LINUX_TAG='v7.3'
LINUX_BASE=f'https://raw.githubusercontent.com/torvalds/linux/{LINUX_TAG}/'
LINUX_FILES={   # file in DL_DIR/usbcfg-src -> (path in the Linux tree, SHA-256)
 'r8152.c':('drivers/net/usb/r8152.c','9d766f203c7fdabe6449e8dd2b9e5dd4fccfa20a62aa5dbf00d64c0c475c5a9f'),
 'r8152.h':('include/linux/usb/r8152.h','2d171ccd1779082dfcb4e2663e0127844acd1d67b0358ef1ed2eadb6510f06e0'),
 'aqc111.c':('drivers/net/usb/aqc111.c','57485c587554c995c88bfb8da3e37d1207d359948bba66e106f053db21892e30'),
 'cdc_ether.c':('drivers/net/usb/cdc_ether.c','7dd643be076878b22de9de896848f160b1a1c867013e021a1b91df35277d539d'),
}
# Names only where the Linux sources themselves give one (comments of cdc_ether.c / aqc111.c, the chip names of r8152.c).
NAMES={
 '0bda:8152':'Realtek RTL8152 (USB 2.0 10/100)','0bda:8153':'Realtek RTL8153 (USB 3.0 gigabit)',
 '17ef:721e':'Lenovo Powered USB-C Travel Hub (RTL8153)','17ef:a359':'Lenovo ThinkPad Hybrid USB-C with USB-A Dock (RTL8153)',
 '2eca:c101':'Aquantia AQtion USB to 5GbE (AQC111U)','0b95:2790':'ASIX USB 3.1 Gen1 to 5G Multi-Gigabit Ethernet Adapter (AQC111U)',
 '0b95:2791':'ASIX USB 3.1 Gen1 to 2.5G Multi-Gigabit Ethernet Adapter (AQC112U)','20f4:e05a':'TRENDnet USB-C 3.1 to 5GBASE-T (AQC111U)',
 '1c04:0015':'QNAP QNA-UC5G1T USB to 5GbE (AQC111U)',
}

def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def linux_sources(dl):
    folder=dl/'usbcfg-src';folder.mkdir(parents=True,exist_ok=True);found={}
    for name,(path,digest) in LINUX_FILES.items():
        target=folder/name
        if not target.exists():
            print('download',LINUX_BASE+path)
            with urllib.request.urlopen(urllib.request.Request(LINUX_BASE+path,headers={'User-Agent':'netpack-fetch'})) as r:target.write_bytes(r.read())
        got=sha256(target)
        if digest and got!=digest:raise SystemExit(f'{target}: SHA-256 {got}, expected {digest}')
        found[name]=dict(url=LINUX_BASE+path,sha256=got)
    return folder,found

def known_devices(folder):
    """IDs from Linux: r8152.c rtl8152_table (devices for which Linux has a configuration selector, because the
    vendor-specific function is one of several configurations) and aqc111.c products (the same devices are in the
    blacklist of cdc_ether.c with their CDC-ECM interface)."""
    vendors={m.group(1):int(m.group(2),16) for m in re.finditer(r'#define\s+(VENDOR_ID_\w+)\s+(0x[0-9a-fA-F]+)',(folder/'r8152.h').read_text(encoding='utf-8'))}
    text=(folder/'r8152.c').read_text(encoding='utf-8')
    table=text[text.index('static const struct usb_device_id rtl8152_table[]'):];table=table[:table.index('};')]
    ids=[]
    for m in re.finditer(r'USB_DEVICE\((VENDOR_ID_\w+),\s*(0x[0-9a-fA-F]+)\)',table):
        ids.append((vendors[m.group(1)],int(m.group(2),16),'r8152.c rtl8152_table'))
    text=(folder/'aqc111.c').read_text(encoding='utf-8')
    for m in re.finditer(r'\{AQC111_USB_ETH_DEV\((0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),',text):
        ids.append((int(m.group(1),16),int(m.group(2),16),'aqc111.c products'))
    out=[]
    for vid,pid,where in ids:
        key=f'{vid:04x}:{pid:04x}';label=NAMES.get(key,'device of the Linux '+where.split()[0][:-2]+' driver table')
        out.append(dict(id='usb:'+key,text=f'usb:{key} {label}',source=where))
    return out

def pe_check(path):
    data=Path(path).read_bytes()
    if data[:2]!=b'MZ':raise SystemExit(f'{path}: not a PE image')
    pe=struct.unpack_from('<I',data,0x3c)[0]
    if data[pe:pe+4]!=b'PE\0\0':raise SystemExit(f'{path}: no PE signature')
    machine,sections=struct.unpack_from('<HH',data,pe+4);optional=pe+24;size=struct.unpack_from('<H',data,pe+20)[0]
    magic=struct.unpack_from('<H',data,optional)[0];subsystem=struct.unpack_from('<H',data,optional+68)[0]
    names=[data[optional+size+40*i:optional+size+40*i+8].rstrip(b'\0').decode() for i in range(sections)]
    reloc_rva,reloc_size=struct.unpack_from('<II',data,optional+112+5*8)
    if (magic,machine,subsystem)!=(0x20b,0x8664,11):raise SystemExit(f'{path}: magic {magic:#x} machine {machine:#x} subsystem {subsystem}')
    if '.reloc' not in names or not reloc_size:raise SystemExit(f'{path}: no base relocations')
    return dict(magic='PE32+',machine='0x8664',subsystem=11,sections=names,relocation_bytes=reloc_size)

def build(out,name,defines):
    include=[f'-I{EDK2/"MdePkg/Include"}',f'-I{EDK2/"MdePkg/Include/X64"}']
    flags=['--target=x86_64-unknown-windows-gnu','-std=c11','-Oz','-ffreestanding','-fshort-wchar','-fno-builtin','-fno-stack-protector','-mno-red-zone',
           '-mno-stack-arg-probe','-mno-implicit-float','-funsigned-char','-fno-asynchronous-unwind-tables','-Wall','-Wextra','-Werror']
    obj=out/(name+'.obj');image=out/(name+'.efi')
    def run(command):
        result=subprocess.run(list(map(str,command)),capture_output=True,text=True)
        if result.returncode:
            sys.stderr.write(result.stdout+result.stderr);raise SystemExit(f'FAILED: {Path(command[0]).name}')
    run([LLVM/'clang.exe',*flags,*include,*defines,'-c',SOURCE,'-o',obj])
    run([LLVM/'lld-link.exe','/nologo','/nodefaultlib','/dll','/noimplib','/machine:x64','/subsystem:efi_boot_service_driver','/entry:UsbCdcConfigSelectEntry',
         '/opt:ref','/merge:.rdata=.data','/timestamp:0','/debug:none',f'/out:{image}',obj])
    obj.unlink()
    return image

def main():
    if len(sys.argv)!=3:sys.exit(__doc__)
    dl=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True);(out/'test-only').mkdir(exist_ok=True)
    folder,linux=linux_sources(dl)
    devices=known_devices(folder)
    image=build(out,'nic-usb-29-cdc-select',[])
    test=build(out/'test-only','UsbCdcConfigSelectTest',['-DCHARON_USBCFG_TEST'])
    info=pe_check(image);pe_check(test)
    clang=subprocess.run([str(LLVM/'clang.exe'),'--version'],capture_output=True,text=True).stdout.splitlines()[0]
    manifest=dict(
        file=image.name,name='UsbCdcConfigSelect: Charon USB CDC configuration selector',version=VERSION,
        description='Switches USB Ethernet adapters whose default configuration is maker-specific to their CDC-ECM (else CDC-NCM) configuration, '
                    'so the USB class drivers can drive them. Acts only on an unmanaged vendor-specific interface of a device whose active configuration '
                    'is vendor-specific only and that has another configuration with a CDC-ECM/NCM interface. No ID table in the image.',
        source='charon-src-r395/src/netdrv/usb-cdc-select/UsbCdcConfigSelect.c (this repository; own code)',
        source_sha256=sha256(SOURCE),sha256=sha256(image),size=image.stat().st_size,licence='BSD-2-Clause-Patent',
        pe=info,built_with=clang+'; lld-link; edk2 MdePkg headers of the workspace work/ha',
        devices=[d['text'] for d in devices],
        devices_note=f'Known devices the rule is EXPECTED to match, taken from Linux {LINUX_TAG}: r8152.c rtl8152_table (Linux registers a configuration selector for them, '
                     'because the vendor-specific function is only one of the device\'s configurations) and aqc111.c (the same IDs are blacklisted in cdc_ether.c with their '
                     'CDC-ECM interface). No physical device was checked; which configurations a given adapter really has is read from its descriptors at run time.',
        devices_source={name:value for name,value in linux.items()},
        needs='USB class drivers after the switch: nic-usb-33-class-common.efi with nic-usb-35-class-cdc-ecm.efi / nic-usb-36-class-cdc-ncm.efi of the driver pack; '
              'a firmware USB bus driver that follows SET_CONFIGURATION sent through EFI_USB_IO (edk2 UsbBusDxe does; others unknown)',
        state='built; mechanism run in QEMU 11.1 OVMF (tcg) with the emulated usb-net device and the TEST variant (switch RNDIS -> CDC-ECM, new interface handles, '
              'ECM class driver bound, and with an ECM class driver build whose descriptor lookup is corrected: adapter + DHCP lease 10.0.2.15); the shipped image was '
              'loaded in the same machine and left the device alone; never run with a physical adapter, never on firmware other than OVMF',
        known_limits=['edk2 UsbNetwork class drivers (UsbCdcEcm/UsbCdcNcm/UsbRndis, LoadAllDescriptor) read configuration descriptor number bConfigurationValue-1; '
                      'a device whose configuration values are not 1,2,3.. in descriptor order (QEMU usb-net: RNDIS=2 first, ECM=1 second) gets no adapter with the '
                      'unmodified class driver (test-only/UsbCdcEcm-descriptor-index.patch shows the correction used in the test)',
                      'edk2 XhciDxe answers SET_CONFIGURATION with a Configure Endpoint command that only ADDS the endpoints of the new configuration (XhciSched.c XhcSetConfigCmd); '
                      "accepted by QEMU's xHCI model, not tried on a physical controller",
                      'firmware whose USB bus driver does not follow SET_CONFIGURATION keeps the old interface; the driver then restores the old configuration and logs it',
                      'Charon loads nic-* files only when the firmware drives no adapter at all (CharonWsNet.h LoadDrivers); the new adapter appears a moment AFTER the connect '
                      '(timer + USB transfers), so the adapter list has to be read again'],
        evidence=['qemu-usb-net-test.serial.txt (TEST variant + pack NetworkCommon + ECM test build: switch, SNP, DHCP)',
                  "qemu-usb-net-pack-ecm.serial.txt (TEST variant + the pack's unmodified NetworkCommon and CDC-ECM: switch and ECM binding, no adapter)",
                  'qemu-shipped-image-load.serial.txt (shipped image: loads, binds nothing, usb-net untouched)'],
        log_variable='CharonUsbCdcSelect, GUID 6b1f4d2a-3c5e-4f7b-9a1d-52c80e7f31a6, boot-services only, ASCII text, one line per device looked at',
        test_only=dict(file='test-only/'+test.name,sha256=sha256(test),note='accepts QEMU usb-net 0525:a4a2 and prints its log; emulator test only, do not ship'))
    (out/'usbcfg.json').write_text(json.dumps(manifest,indent=1)+'\n',encoding='utf-8',newline='\n')
    print(image.name,image.stat().st_size,manifest['sha256']);print('devices',len(devices));print('test variant',test.name,test.stat().st_size)

if __name__=='__main__':main()
