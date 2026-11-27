"""Charon test machines on QMP 4466 (r355).

  charon-vm.py prepare NAME EFI [--wav] [--grub GRUB.EFI] [--paused]
  charon-vm.py keys NAME GAP key [key ...]      (qcodes; GAP seconds between keys)
  charon-vm.py screen NAME FILE.ppm
  charon-vm.py quit NAME
  charon-vm.py gaps WAV                          (zero runs inside sound = underruns)
  charon-vm.py serial NAME [PATTERN]

Each machine is a fresh directory under work/: boot disk and firmware are
copied from the r349 integrated machine, settings slots are removed.
"""
from pathlib import Path
import os,sys,shutil,subprocess,socket,json,time,array
base=Path(__file__).resolve().parent
sys.path[:0]=[str(base/'fat-image-deps'),str(base/'fat-legacy-deps')]
QEMU=os.environ.get('CHARON_QEMU','qemu-system-x86_64')  # QEMU 10.x with SDL and dsound/wav audio
PORT=int(os.environ.get('CHARON_PORT','4466'))   # r402: CHARON_PORT for a second machine beside a long test

# r401: the machine runs on the Windows hypervisor (CHARON_ACCEL=whpx, default: speed of a real computer) with CHARON_SMP processors (default 4);
# CHARON_ACCEL=tcg is the old pure emulation (the timing baselines up to r400 were measured on it).
def qmp():
    s=socket.create_connection(('127.0.0.1',PORT),timeout=10);f=s.makefile('rwb',buffering=0);f.readline()
    def cmd(n,a=None):
        f.write((json.dumps(dict(execute=n,arguments=a or {}))+'\n').encode())
        while True:
            r=json.loads(f.readline())
            if 'return' in r or 'error' in r:return r
    cmd('qmp_capabilities');return cmd

def fat_disk(path,size_mb,files):
    """Fresh FAT32 image with an MBR-less superfloppy layout (OVMF accepts it)."""
    from pyfatfs.PyFatFS import PyFatFS
    import pyfatfs.FATDirectoryEntry
    from pyfatfs import PyFat
    with open(path,'wb') as f:f.truncate(size_mb*1024*1024)
    fat=PyFat.PyFat();fat.mkfs(str(path),fat_type=PyFat.PyFat.FAT_TYPE_FAT32 if size_mb>=64 else PyFat.PyFat.FAT_TYPE_FAT16,size=size_mb*1024*1024,label='GRUBTEST')
    fat.close()
    with PyFatFS(str(path)) as fs:
        for name,data in files.items():
            parts=name.split('/')
            for i in range(1,len(parts)):
                d='/'.join(parts[:i])
                if not fs.exists(d):fs.makedir(d)
            with fs.openbin(name,'w') as out:out.write(data)

def gpt_disk(path,size_mb,files,label='WINTEST'):
    """GPT disk with one EFI system partition (Windows Boot Manager needs a partitioned device)."""
    import struct,zlib,uuid
    part=path.with_suffix('.part');fat_disk(part,size_mb,files)
    body=part.read_bytes();part.unlink()
    first=2048;sectors=len(body)//512;last=first+sectors-1;total=last+1+2048
    esp=uuid.UUID('C12A7328-F81F-11D2-BA4B-00A0C93EC93B').bytes_le
    entry=esp+uuid.UUID('6f1c3a52-5d47-4c11-9a8e-0c4a5b1e7d01').bytes_le+struct.pack('<QQQ',first,last,0)+'EFI system partition'.encode('utf-16-le').ljust(72,b'\0')
    entries=entry.ljust(128*128,b'\0');ecrc=zlib.crc32(entries)
    def header(cur,alt,elba):
        h=struct.pack('<8sIIIIQQQQ16sQIII',b'EFI PART',0x10000,92,0,0,cur,alt,34,total-34,uuid.UUID('0a8bd0a3-7b1c-4e33-9c51-3f2e0d6c4b02').bytes_le,elba,128,128,ecrc)
        return (h[:16]+struct.pack('<I',zlib.crc32(h))+h[20:]).ljust(512,b'\0')
    mbr=bytearray(512);mbr[446:462]=struct.pack('<BBBBBBBBII',0,0,2,0,0xEE,0xFF,0xFF,0xFF,1,min(total-1,0xFFFFFFFF));mbr[510:512]=b'\x55\xaa'
    with open(path,'wb') as f:
        f.truncate(total*512)
        f.write(bytes(mbr));f.write(header(1,total-1,2));f.write(entries)
        f.seek(first*512);f.write(body)
        f.seek((total-33)*512);f.write(entries);f.write(header(total-1,1,total-33))

MANAGER_ENTRIES=(b'menuentry "systemd-boot" {\n  chainloader /EFI/systemd/systemd-bootx64.efi\n}\n'
    b'menuentry "Limine" {\n  chainloader /EFI/limine/BOOTX64.EFI\n}\n'
    b'menuentry "OpenCore" {\n  chainloader /EFI/OC/OpenCore.efi\n}\n'
    b'menuentry "Clover" {\n  chainloader /EFI/CLOVER/CLOVERX64.efi\n}\n'
    b'menuentry "Memtest86+" {\n  chainloader /EFI/memtest/memtest86plus.efi\n}\n')
def manager_files():
    """systemd-boot 257.13 (Debian systemd-boot-efi, LGPL-2.1-or-later), Limine 11.4.1
    (BSD-2-Clause) and OpenCore 1.0.8 (BSD-3-Clause, built-in picker) on the GRUB test
    disk, each with entries for the probe loaders."""
    return {'EFI/systemd/systemd-bootx64.efi':(base/'sdboot-dl/x/usr/lib/systemd/boot/efi/systemd-bootx64.efi').read_bytes(),
        'loader/loader.conf':os.environ.get('CHARON_SDBOOT_CONF','timeout 30\nconsole-mode keep\neditor no\n').encode(),
        'loader/entries/probe.conf':b'title Charon probe loader\nefi /EFI/probe/PROBE.EFI\n',
        'loader/entries/gprobe.conf':b'title Charon graphical probe\nefi /EFI/probe/GPROBE.EFI\n',
        'EFI/limine/BOOTX64.EFI':(base/'limine-dl/BOOTX64.EFI').read_bytes(),
        'EFI/OC/OpenCore.efi':(base/'opencore-dl/x/X64/EFI/OC/OpenCore.efi').read_bytes(),'EFI/OC/config.plist':opencore_config(),
        'EFI/CLOVER/CLOVERX64.efi':(base/'clover-dl/x/CloverV2/EFI/CLOVER/CLOVERX64.efi').read_bytes(),'EFI/CLOVER/config.plist':clover_config(),
        'EFI/memtest/memtest86plus.efi':(base/'memtest-dl/x/mt86p_900_x86_64').read_bytes(),
        'limine.conf':b'timeout: 30\n\n/Charon probe loader\n    protocol: efi\n    path: boot():/EFI/probe/PROBE.EFI\n\n/Charon graphical probe\n    protocol: efi\n    path: boot():/EFI/probe/GPROBE.EFI\n'}

def opencore_config():
    """OpenCore 1.0.8's Docs/Sample.plist for this test machine: built-in picker shown
    for 30 s with auxiliary entries, no drivers (OpenCore's AudioDxe would take the
    sound card), any boot volume (ScanPolicy 0), no vault or Apple Secure Boot."""
    import plistlib
    p=plistlib.load(open(base/'opencore-dl/x/Docs/Sample.plist','rb'))
    b=p['Misc']['Boot'];b.update(Timeout=30,ShowPicker=True,PickerMode='Builtin',HideAuxiliary=False,PickerAudioAssist=False)
    p['Misc']['Security'].update(ScanPolicy=0,Vault='Optional',SecureBootModel='Disabled')
    p['UEFI']['Drivers']=[];p['Misc']['Tools']=[]
    if os.environ.get('CHARON_OC_NOAIK'):p['UEFI']['Input']['KeySupport']=False
    if os.environ.get('CHARON_OC_TOOLS'):
        # r396d: auxiliary entries (Space): UEFI Shell, Reset NVRAM, Toggle SIP, firmware settings.
        p['Misc']['Tools']=[{'Arguments':'','Auxiliary':True,'Comment':'','Enabled':True,'Flavour':'OpenShell:UEFIShell:Shell','FullNvramAccess':False,'Name':'UEFI Shell','Path':'OpenShell.efi','RealPath':False,'TextMode':False}]
        p['UEFI']['Drivers']=[{'Path':n,'Enabled':True,'LoadEarly':False,'Arguments':'','Comment':''} for n in ('ResetNvramEntry.efi','ToggleSipEntry.efi','FirmwareSettingsEntry.efi')]
    if os.environ.get('CHARON_OC_TEXT')=='SystemText':p['UEFI']['Output']['TextRenderer']='SystemText'
    if os.environ.get('CHARON_OC_CANOPY'):
        # r384: OpenCanopy graphical picker (icons, labels) with OcBinaryData resources (work/ocbin-dl).
        b['PickerMode']='External';b['PickerVariant']=r'Acidanthera\GoldenGate'
        p['UEFI']['Drivers']=[{'Path':'OpenCanopy.efi','Enabled':True,'LoadEarly':False,'Arguments':'','Comment':''}]
        p['UEFI']['Output']['Resolution']=os.environ.get('CHARON_OC_RES','1280x800');b['Timeout']=90
        if os.environ.get('CHARON_OC_LOG'):p['Misc']['Debug'].update(Target=0x09,DisplayLevel=0x80000042,AppleDebug=False);p['Misc'].setdefault('Serial',{})['Init']=True
    return plistlib.dumps(p)
def opencore_files():
    oc=base/'opencore-dl/x/X64/EFI'
    return {'EFI/BOOT/BOOTX64.EFI':(oc/'BOOT/BOOTx64.efi').read_bytes(),'EFI/OC/OpenCore.efi':(oc/'OC/OpenCore.efi').read_bytes(),
        'EFI/OC/config.plist':opencore_config(),**opencanopy_files(),**opencore_tool_files()}
def opencore_tool_files():
    if not os.environ.get('CHARON_OC_TOOLS'):return {}
    oc=base/'opencore-dl/x/X64/EFI/OC'
    return {'EFI/OC/Tools/OpenShell.efi':(oc/'Tools/OpenShell.efi').read_bytes(),
        **{'EFI/OC/Drivers/'+n:(oc/'Drivers'/n).read_bytes() for n in ('ResetNvramEntry.efi','ToggleSipEntry.efi','FirmwareSettingsEntry.efi')}}
def opencanopy_files():
    if not os.environ.get('CHARON_OC_CANOPY'):return {}
    res=base/'ocbin-dl/x/OcBinaryData-master/Resources'
    files={'EFI/OC/Drivers/OpenCanopy.efi':(base/'opencore-dl/x/X64/EFI/OC/Drivers/OpenCanopy.efi').read_bytes()}
    for f in res.rglob('*'):
        rel=f.relative_to(res).as_posix()
        if f.is_file() and (not rel.startswith('Image/') or rel.startswith('Image/Acidanthera/GoldenGate/')):files['EFI/OC/Resources/'+rel]=f.read_bytes()
    return files

def clover_config():
    """Clover 5176's config-sample.plist: wait 30 s in its menu, embedded theme unless
    CHARON_CLOVER_TEXT=1 asks for the text-only menu."""
    import plistlib
    p=plistlib.load(open(base/'clover-dl/x/CloverV2/EFI/CLOVER/config-sample.plist','rb'))
    p['Boot']['Timeout']=30;p['Boot']['DefaultVolume']='LastBootedVolume'
    p['GUI']['TextOnly']=os.environ.get('CHARON_CLOVER_TEXT')=='1'
    return plistlib.dumps(p)
def clover_files():
    cl=base/'clover-dl/x/CloverV2/EFI'
    return {'EFI/BOOT/BOOTX64.EFI':(cl/'BOOT/BOOTX64.efi').read_bytes(),'EFI/CLOVER/CLOVERX64.efi':(cl/'CLOVER/CLOVERX64.efi').read_bytes(),
        'EFI/CLOVER/config.plist':clover_config()}

def win_files():
    """Windows Boot Manager and Memory Diagnostic from this host's own C:/Windows/Boot (never
    redistributed); the BCD is Windows' DVD template, so bootmgr reports the missing boot.wim."""
    wb=Path(os.environ.get('CHARON_WINBOOT',r'C:\Windows\Boot'));files={}
    files['EFI/BOOT/BOOTX64.EFI']=files['EFI/Microsoft/Boot/bootmgfw.efi']=(wb/'EFI/bootmgfw.efi').read_bytes();files['EFI/Microsoft/Boot/memtest.efi']=(wb/'EFI/memtest.efi').read_bytes()
    files['EFI/Microsoft/Boot/BCD']=Path(os.environ.get('CHARON_WINBOOT_BCD',wb/'DVD/EFI/BCD')).read_bytes();files['EFI/Microsoft/Boot/boot.sdi']=(wb/'DVD/EFI/boot.sdi').read_bytes()
    for lang in os.environ.get('CHARON_WINBOOT_LANGS','en-US,pl-PL').split(','):
        for p in (wb/'EFI'/lang).glob('*.mui'):files[f'EFI/Microsoft/Boot/{lang}/'+p.name]=p.read_bytes()
        for p in (wb/'Resources'/lang).glob('*'):files[f'EFI/Microsoft/Boot/Resources/{lang}/'+p.name]=p.read_bytes()
    files['EFI/Microsoft/Boot/Resources/bootres.dll']=(wb/'Resources/bootres.dll').read_bytes()
    if os.environ.get('CHARON_WINBOOT_WIM'):
        # r371: test-only boot.wim with this host's winload.efi (Standard policy: winload draws the Windows 8+ menu); never shipped.
        files['sources/boot.wim']=Path(os.environ['CHARON_WINBOOT_WIM']).read_bytes();files['boot/boot.sdi']=(wb/'DVD/EFI/boot.sdi').read_bytes()
    for p in (wb/'Fonts').glob('*.ttf'):
        if p.stat().st_size<400000:files['EFI/Microsoft/Boot/Fonts/'+p.name]=p.read_bytes()
    if '--opencore' in sys.argv:
        files.update(opencore_files())
    if '--clover' in sys.argv:
        files.update(clover_files())
    if '--memtest' in sys.argv:
        # Memtest86+ 9.00 (GPL-2.0) starts this disk directly.
        files['EFI/BOOT/BOOTX64.EFI']=(base/'memtest-dl/x/mt86p_900_x86_64').read_bytes()
    if '--limine' in sys.argv:
        # Limine 11.4.1 (BSD-2-Clause) starts this disk: probe loader and Windows Boot Manager entries.
        files['EFI/BOOT/BOOTX64.EFI']=(base/'limine-dl/BOOTX64.EFI').read_bytes()
        files['limine.conf']=os.environ.get('CHARON_LIMINE_CONF','timeout: 30\n\n/Charon probe loader\n    protocol: efi\n    path: boot():/EFI/probe/PROBE.EFI\n\n/Windows Boot Manager\n    protocol: efi\n    path: boot():/EFI/Microsoft/Boot/bootmgfw.efi\n').encode()
        files['EFI/probe/PROBE.EFI']=(base/'charon-probe-r355/PROBE.EFI').read_bytes()
    if '--sdboot' in sys.argv:
        # systemd-boot (Debian systemd-boot-efi 257.13) starts this disk: its own
        # entry for the probe loader, Windows Boot Manager found automatically.
        files['EFI/BOOT/BOOTX64.EFI']=files['EFI/systemd/systemd-bootx64.efi']=(base/'sdboot-dl/x/usr/lib/systemd/boot/efi/systemd-bootx64.efi').read_bytes()
        files['loader/loader.conf']=os.environ.get('CHARON_SDBOOT_CONF','timeout 30\nconsole-mode keep\neditor no\n').encode()
        files['loader/entries/probe.conf']=b'title Charon probe loader\nefi /EFI/probe/PROBE.EFI\n'
        files['EFI/probe/PROBE.EFI']=(base/'charon-probe-r355/PROBE.EFI').read_bytes()
    return files

def prepare(name,efi,wav=False,grub=None,paused=False):
    from pyfatfs.PyFatFS import PyFatFS
    area=base/name;area.mkdir(exist_ok=False)
    with socket.socket() as probe:assert probe.connect_ex(('127.0.0.1',PORT))!=0,'Stop the previous Charon QEMU first'
    prior=base/'charon-machine-integrated-r349'
    parts={'EFI/BOOT/BOOTX64.EFI':Path(efi),'EFI/HAK/HAKBOOT.EFI':Path(efi),
     'EFI/HAK/AudioDxe.efi':Path(__import__('os').environ.get('CHARON_AUDIODXE',base/'charon-audio-driver-r389/AudioDxe-r389.efi')),
     'EFI/HAK/CharonShell.efi':base/'ha/Build/CharonShell/RELEASE_VS2022/X64/CharonShell.efi',
     # r397 (user): Memtest86+ shipped with Charon, always offered in the device list.
     'EFI/HAK/tools/memtest.efi':base/'memtest-dl/x/mt86p_900_x86_64'}
    if __import__('os').environ.get('CHARON_VOICE'):parts['EFI/HAK/CHARONVOICE.EFI']=Path(__import__('os').environ['CHARON_VOICE'])
    # r400: Charon's installers travel with it (a cloud install leaves them in the new system).
    for _p in sorted((base/'charon-src-r395/tools/charon-addon').glob('*')):
        if _p.is_file():parts['EFI/HAK/addon/'+_p.name]=_p
    shutil.copyfile(prior/'boot-disk.img',area/'boot-disk.img')
    shutil.copyfile(prior/'firmware-code.fd',area/'firmware-code.fd')
    shutil.copyfile(base/'charon-machine-r347/firmware-vars.fd',area/'firmware-vars.fd')
    with (PyFatFS(str(area/"boot-disk.img")) if sys.argv[3:] else __import__("contextlib").nullcontext()) as fs:
        for n in ['EFI/HAK/charon-settings-a.bin','EFI/HAK/charon-settings-b.bin','EFI/HAK/once.bin','EFI/HAK/continue.bin','EFI/HAK/continue-set']:
            if fs.exists(n):fs.remove(n)
        if '--quiet-reader' in sys.argv:
            # Control runs: boot messages off, so no resident reader is installed.
            import struct,zlib
            body=b'CHARS006'+struct.pack('<Q',1)+bytes([0,0,1,0,1,0,10,10,10,0|(1<<2),0,10])
            with fs.openbin('EFI/HAK/charon-settings-a.bin','w') as f:f.write(body+struct.pack('<I',zlib.crc32(body)))
        if '--melody' in sys.argv:
            # Measurement only: background melody 1 makes any underrun audible
            # and measurable as zeros inside continuous sound (CHARS006 slot a).
            import struct,zlib
            body=b'CHARS006'+struct.pack('<Q',1)+bytes([0,0,1,0,1,1,10,10,10,1|(1<<2),0,10])
            data=body+struct.pack('<I',zlib.crc32(body))
            with fs.openbin('EFI/HAK/charon-settings-a.bin','w') as f:f.write(data)
        if os.environ.get('CHARON_SETTINGS_V10'):
            # r393: a CHARS00A slot with the defaults plus formatting byte 40 and reader extras byte 41.
            import struct,zlib
            # r393 V11: optional third and fourth values = synthesizer manifest name (e.g. rhvoice.manifest) and voice index.
            v10=os.environ['CHARON_SETTINGS_V10'].split(',')
            fmt,extra=int(v10[0]),int(v10[1])
            synth=0;voice=int(v10[3]) if len(v10)>3 else 0
            if len(v10)>2 and v10[2]:
                h=2166136261
                for ch in v10[2].lower():h=((h^ord(ch))*16777619)&0xffffffff
                synth=(h^(h>>8)^(h>>16)^(h>>24))&0xff or 1
            # r397: fmt > 63 (event icons 64, families 128) or extra > 31 (stereo bits 5-6) need V12 CHARS00C.
            body=(b'CHARS00C' if fmt>63 or extra>31 else b'CHARS00B' if synth else b'CHARS00A')+struct.pack('<Q',1)+bytes([0,0,1,0,1,0,10,10,10,1|(1<<2),7<<1,10,10,10,10,10,7,2,14,10,1,1,0,47,fmt,extra,synth,voice])
            assert len(body)==44
            with fs.openbin('EFI/HAK/charon-settings-a.bin','w') as f:f.write(body+struct.pack('<I',zlib.crc32(body)))
        if os.environ.get('CHARON_SETTINGS_FILE'):   # r410: a ready settings record (work/r410-mkset.cpp)
            with fs.openbin('EFI/HAK/charon-settings-a.bin','w') as f:f.write(Path(os.environ['CHARON_SETTINGS_FILE']).read_bytes())
        # r361: interface sounds from the current cue pack (welcome, system launch, boot manager loaded).
        for pcm in sorted((base/os.environ.get('CHARON_CUES','charon-cues-48k-r361')).glob('*.pcm')):parts['EFI/HAK/audio/'+pcm.name]=pcm
        # r393: optional synthesizer modules, e.g. CHARON_SYNTHS=work/synth-modules-r393
        # copies its EFI/HAK/synths tree (.tts, .manifest, data packs, licences); default: none.
        if os.environ.get('CHARON_SYNTHS','synth-modules-r409')!='none':   # r396d: eSpeak NG + RHVoice by default (CHARON_SYNTHS=none: TVox only)
            synths=(base/os.environ.get('CHARON_SYNTHS','synth-modules-r409')).resolve()/'EFI/HAK/synths'
            assert synths.is_dir(),('CHARON_SYNTHS has no EFI/HAK/synths',synths)
            for p in sorted(synths.rglob('*')):
                if p.is_file():parts['EFI/HAK/synths/'+p.relative_to(synths).as_posix()]=p
        # r400: a tree copied to the boot disk root, e.g. CHARON_PACKAGES=pkg-r400 (holding charon-packages/...)
        if os.environ.get('CHARON_PACKAGES'):
            tree=(base/os.environ['CHARON_PACKAGES']).resolve()
            for p in sorted(tree.rglob('*')):
                if p.is_file():parts[p.relative_to(tree).as_posix()]=p
        # r400: optional UEFI drivers (EFI/HAK/drivers/*.efi), e.g. CHARON_DRIVERS=charon-drivers-r400
        if os.environ.get('CHARON_DRIVERS'):
            for p in sorted((base/os.environ['CHARON_DRIVERS']).resolve().glob('*.efi')):parts['EFI/HAK/drivers/'+p.name]=p
            for sub in ('manual','experimental'):   # r408: the pack's sub-folders (never loaded by a rule)
                for p in sorted(((base/os.environ['CHARON_DRIVERS']).resolve()/sub).glob('*.efi')):parts['EFI/HAK/drivers/'+sub+'/'+p.name]=p
        # r396: boot-manager profile files (EFI/HAK/profiles/*.txt) from a directory.
        if os.environ.get('CHARON_PROFILES'):
            for p in sorted(Path(os.environ['CHARON_PROFILES']).resolve().glob('*.txt')):
                parts['EFI/HAK/profiles/'+p.name]=p
        for n,src in parts.items():
            data=src.read_bytes()
            if '/' in n:fs.makedirs(n.rsplit('/',1)[0],recreate=True)
            if fs.exists(n):fs.remove(n)
            with fs.openbin(n,'w') as f:f.write(data)
            with fs.openbin(n,'r') as f:assert f.read()==data
    command=[QEMU,'-name','Charon-'+name,'-machine','q35,accel='+{'whpx':'whpx,kernel-irqchip=off','tcg':'tcg'}[os.environ.get('CHARON_ACCEL','whpx')],'-cpu',os.environ.get('CHARON_CPU','max'),'-smp',os.environ.get('CHARON_SMP','4'),'-m',os.environ.get('CHARON_MEM','512')]+((['-netdev','user,id=n0','-device','e1000,netdev=n0,romfile='] if os.environ.get('CHARON_NET_NOROM') else ['-nic','user,model='+os.environ.get('CHARON_NIC','e1000')+(',tftp='+str(Path(os.environ['CHARON_TFTP']).resolve())+',bootfile='+os.environ.get('CHARON_BOOTFILE','BOOTX64.EFI') if os.environ.get('CHARON_TFTP') else '')]) if os.environ.get('CHARON_NET') or os.environ.get('CHARON_TFTP') else ['-nic','none'])+['-display','sdl']
    if os.environ.get('CHARON_GDB'):command+=['-gdb','tcp:127.0.0.1:1234']  # r386: watchpoints via gdb-watch.py
    if os.environ.get('CHARON_DEBUGCON'):command+=['-debugcon','file:debugcon.log','-global','isa-debugcon.iobase=0x402']  # r386: firmware debug output / exceptions
    if '--ramfb' in sys.argv:command+=['-vga','none','-device','ramfb']  # GOP with a plain linear frame buffer (no GRUB native driver), like physical machines
    if paused:command.append('-S')
    command+=['-drive','if=pflash,format=raw,readonly=on,file=firmware-code.fd','-drive','if=pflash,format=raw,file=firmware-vars.fd',
     # r396: Charon's disk boots first even with blank firmware variables (OVMF follows bootindex).
     '-drive','if=none,id=charondisk,format=raw,file=boot-disk.img'+(',readonly=on' if os.environ.get('CHARON_BOOT_RO') else '')]   # r410: CHARON_BOOT_RO=1 with CHARON_BOOT_USB=1 = a write-protected stick
    # r401: CHARON_BOOT_USB=1 puts Charon's own disk on USB (Charon started from a stick; the internal disk is only the target)
    command+=['-device','qemu-xhci,id=xhci','-device','usb-storage,bus=xhci.0,drive=charondisk,bootindex=0'] if os.environ.get('CHARON_BOOT_USB') else ['-device','ide-hd,drive=charondisk,bus=ide.0,bootindex=0']
    if grub:
        cfg=(b'set timeout=3\nset timeout_style=hidden\n' if os.environ.get('CHARON_GRUB_HIDDEN') else b'set timeout=8\nset timeout_style=menu\n')+b'menuentry "Charon test entry one" {\n  echo "Entry one selected"\n  sleep 3\n}\nmenuentry "Charon test entry two" {\n  echo "Entry two selected"\n  sleep 3\n}\nmenuentry "UEFI firmware settings" {\n  fwsetup\n}\nmenuentry "Probe loader" {\n  chainloader /EFI/probe/PROBE.EFI\n}\nmenuentry "Graphical probe" {\n  chainloader /EFI/probe/GPROBE.EFI\n}\nmenuentry "Graphical GRUB menu" {\n  insmod all_video\n  insmod gfxterm\n  loadfont /boot/grub/fonts/unicode.pf2\n  set gfxmode=auto\n  terminal_output gfxterm\n  configfile /boot/grub/grub.cfg\n}\n'
        if '--gfxterm' in sys.argv:
            # GRUB draws its menu itself in graphics (gfxterm + unicode.pf2): no console text.
            # --gfxterm-gop: only efi_gop, as on physical machines (GRUB draws straight into the GOP frame buffer).
            cfg=(b'insmod efi_gop\n' if '--gfxterm-gop' in sys.argv else b'insmod all_video\n')+b'insmod gfxterm\nloadfont /boot/grub/fonts/unicode.pf2\nset gfxmode=auto\nterminal_output gfxterm\n'+cfg
        if '--grubtheme' in sys.argv:
            # r367: Debian's starfield theme (DejaVu Sans PF2 fonts, picture menu).
            cfg=b'insmod all_video\ninsmod gfxterm\ninsmod png\nloadfont /boot/grub/fonts/unicode.pf2\nset gfxmode=auto\nterminal_output gfxterm\nloadfont /boot/grub/themes/starfield/dejavu_10.pf2\nloadfont /boot/grub/themes/starfield/dejavu_12.pf2\nloadfont /boot/grub/themes/starfield/dejavu_14.pf2\nloadfont /boot/grub/themes/starfield/dejavu_16.pf2\nloadfont /boot/grub/themes/starfield/dejavu_bold_14.pf2\nset theme=/boot/grub/themes/starfield/theme.txt\n'+cfg
        if '--grub-default' in sys.argv:
            cfg=cfg.replace(b'set timeout=8',b'set timeout=4\nset default='+sys.argv[sys.argv.index('--grub-default')+1].encode())
        files={'EFI/probe/PROBE.EFI':(base/'charon-probe-r355/PROBE.EFI').read_bytes(),'EFI/probe/GPROBE.EFI':(base/'charon-probe-r355/GPROBE.EFI').read_bytes(),'EFI/BOOT/BOOTX64.EFI':Path(grub).read_bytes(),'EFI/BOOT/grub.cfg':cfg,'boot/grub/grub.cfg':cfg,'EFI/debian/grub.cfg':cfg,'boot/grub/fonts/unicode.pf2':(base/'unifont-dl/unicode.pf2').read_bytes()}
        if '--grubtheme' in sys.argv:
            theme=next((base/'grubtheme-dl/x').rglob('theme.txt')).parent
            for p in theme.iterdir():files['boot/grub/themes/starfield/'+p.name]=p.read_bytes()
        if '--managers' in sys.argv:
            # r363: other boot managers chain loaded from GRUB on this disk.
            for n,data in manager_files().items():files[n]=data
            cfg=cfg+MANAGER_ENTRIES
            for n in ('EFI/BOOT/grub.cfg','boot/grub/grub.cfg','EFI/debian/grub.cfg'):files[n]=cfg
        if '--winboot' in sys.argv:
            # Windows programs live on the GPT disk (win_files); GRUB finds them there.
            win=(b'menuentry "Windows Boot Manager (chain loaded)" {\n  search --file --set=root /EFI/Microsoft/Boot/bootmgfw.efi\n  chainloader /EFI/Microsoft/Boot/bootmgfw.efi\n}\n'
                 b'menuentry "Windows Memory Diagnostic" {\n  search --file --set=root /EFI/Microsoft/Boot/memtest.efi\n  chainloader /EFI/Microsoft/Boot/memtest.efi\n}\n')
            cfg=cfg+win
            for n in ('EFI/BOOT/grub.cfg','boot/grub/grub.cfg','EFI/debian/grub.cfg'):files[n]=cfg
        if '--refind' in sys.argv:
            # rEFInd 0.14.2 as the disk's boot program (graphical, anti-aliased font); GRUB moves to EFI/debian.
            rd=base/'refind-dl/x/refind-bin-0.14.2/refind'
            files['EFI/debian/grubx64.efi']=files['EFI/BOOT/BOOTX64.EFI'];files['EFI/BOOT/BOOTX64.EFI']=(rd/'refind_x64.efi').read_bytes()
            conf=os.environ.get('CHARON_REFIND_CONF','timeout 30\nscanfor internal,external,manual\ndont_scan_dirs EFI/BOOT,EFI/HAK\n').encode()
            files['EFI/BOOT/refind.conf']=conf
            for p in (rd/'icons').glob('*.png'):files['EFI/BOOT/icons/'+p.name]=p.read_bytes()
            for p in (base/'refind-dl/x/refind-bin-0.14.2/fonts').glob('*.png'):files['EFI/BOOT/fonts/'+p.name]=p.read_bytes()
            if os.environ.get('CHARON_REFIND_FILES'):
                for p in Path(os.environ['CHARON_REFIND_FILES']).iterdir():files['EFI/BOOT/'+p.name]=p.read_bytes()
        if os.environ.get('CHARON_GRUB_STUB'):
            # r393: an installed Linux - the ESP holds a stub that loads the real grub.cfg elsewhere.
            stub=b'search --file --set=root /linux/grub.cfg\nconfigfile /linux/grub.cfg\n'
            files['linux/grub.cfg']=files['EFI/BOOT/grub.cfg']
            for n in ('EFI/BOOT/grub.cfg','boot/grub/grub.cfg','EFI/debian/grub.cfg'):files[n]=stub
        # r388: extra test programs on the GRUB disk: CHARON_GRUB_EXTRA="EFI/probe/X.EFI=path;..."
        for item in (x for x in os.environ.get('CHARON_GRUB_EXTRA','').split(';') if x):
            target,_,source=item.partition('=');files[target]=Path(source).read_bytes()
        fat_disk(area/'grub-disk.img',96,files)
        command+=['-drive','if=ide,format=raw,file=grub-disk.img,index=1']
    if '--winboot' in sys.argv:
        gpt_disk(area/'win-disk.img',int(os.environ.get('CHARON_WINBOOT_MB','64')),win_files())  # r387: WinRE needs ~1200
        command+=['-drive','if=ide,format=raw,file=win-disk.img,index=3']
    if '--winboot-vvfat' in sys.argv:
        # As the release launcher does: QEMU's virtual FAT disk (MBR partition) of a folder.
        for n,data in win_files().items():
            t=area/'winesp'/n;t.parent.mkdir(parents=True,exist_ok=True);t.write_bytes(data)
        command+=['-drive','if=ide,format=raw,file=fat:rw:winesp,index=3']
    # r386: removable media. --usb adds an xHCI controller (hot-plug with
    # 'usbplug'); CHARON_USB=img[,img] attaches sticks at power-on;
    # CHARON_CDROM=iso adds a SATA CD drive (media swap with 'cdchange').
    if not os.environ.get('CHARON_BOOT_USB') and ('--usb' in sys.argv or os.environ.get('CHARON_USB') or os.environ.get('CHARON_USBKBD') or os.environ.get('CHARON_BRAILLE') or os.environ.get('CHARON_USBAUDIO')):command+=['-device','qemu-xhci,id=xhci']
    # r390: CHARON_USBKBD=1 adds a USB keyboard (numpad review needs its HID reports; QEMU sends keys to the newest keyboard).
    if os.environ.get('CHARON_USBKBD'):command+=['-device','usb-kbd,bus=xhci.0']
    # r390: CHARON_BRAILLE=1 adds a Baum braille display (QEMU usb-braille, FTDI) on a socket for work/charon-braille-sim.py.
    if os.environ.get('CHARON_BRAILLE'):command+=['-chardev','socket,id=brl,host=127.0.0.1,port=4470,server=on,wait=off','-device','usb-braille,chardev=brl,bus=xhci.0']
    # r400: a blank target disk for install tests, e.g. CHARON_BLANK=24 (GB, raw, in the machine folder)
    if os.environ.get('CHARON_BLANK'):
        blank=area/'blank-disk.img'
        # r401: a sparse file - it takes only what the guest really writes (a 64 GB disk used to cost 64 GB at once)
        open(blank,'wb').close()
        subprocess.run(['fsutil','sparse','setflag',str(blank)],capture_output=True)
        with open(blank,'r+b') as f:f.truncate(int(os.environ['CHARON_BLANK'])<<30)
        command+=['-drive',f'if=none,id=blank0,format=raw,file={blank}','-device',{'virtio':'virtio-blk-pci,drive=blank0','sata':'ide-hd,drive=blank0,bus=ide.0','nvme':'nvme,drive=blank0,serial=charonblank'}[os.environ.get('CHARON_BLANK_BUS','virtio')]]
    for i,img in enumerate(x for x in os.environ.get('CHARON_USB','').split(',') if x):
        command+=['-drive',f'if=none,id=stick{i},format=raw,file={Path(img).resolve()}','-device',f'usb-storage,bus=xhci.0,drive=stick{i},removable=on']
    if os.environ.get('CHARON_CDROM') is not None:
        iso=os.environ['CHARON_CDROM'];spec=f',file={Path(iso).resolve()}' if iso else ''
        if os.environ.get('CHARON_CD_BPS'):spec+=',throttling.bps-read='+os.environ['CHARON_CD_BPS']   # r410: a slow drive (bytes per second), to hold a loader's first screen
        command+=['-drive',f'if=none,id=cd0,media=cdrom,readonly=on{spec}','-device','ide-cd,bus=ide.4,drive=cd0']
    command+=['-audiodev',('wav,id=charon_audio,path=live.wav' if wav else 'dsound,id=charon_audio'),
     # r405: CHARON_USBAUDIO=1 adds a USB sound card (QEMU usb-audio, UAC1) on the xHCI; CHARON_SOUNDCARD=none leaves the machine without a built-in card
     *(['-device','usb-audio,bus=xhci.0,audiodev=charon_audio'] if os.environ.get('CHARON_USBAUDIO') else []),
     *([] if os.environ.get('CHARON_SOUNDCARD')=='none' else ['-device','AC97,audiodev=charon_audio'] if os.environ.get('CHARON_SOUNDCARD')=='ac97' else['-device','ich9-intel-hda','-device',os.environ.get('CHARON_SOUNDCARD','hda-duplex')+',audiodev=charon_audio']),   # r389: CHARON_SOUNDCARD=ac97|hda-micro|hda-output
     '-qmp',f'tcp:127.0.0.1:{PORT},server=on,wait=off','-serial','file:serial.log']
    if os.environ.get('CHARON_QEMU_EXTRA'):command+=__import__('shlex').split(os.environ['CHARON_QEMU_EXTRA'])   # r408: raw QEMU arguments (more disks, cards)
    out=(area/'stdout.log').open('xb');err=(area/'stderr.log').open('xb')
    child=subprocess.Popen(command,cwd=area,stdin=subprocess.DEVNULL,stdout=out,stderr=err,creationflags=subprocess.DETACHED_PROCESS)
    (area/'process.json').write_text(json.dumps(dict(pid=child.pid,command=command,parts={k:str(v) for k,v in parts.items()}),indent=2))
    print('PID',child.pid,area)
    if not wav:
        # Audible on the host speakers; record the same stream for analysis.
        for _ in range(40):
            try:
                print(qmp()('human-monitor-command',{'command-line':'wavcapture '+str(area/'live.wav')+' charon_audio'}));break
            except OSError:time.sleep(.5)

def keys(name,gap,codes):
    cmd=qmp()
    for k in codes:
        # 'ctrl+alt+r': modifiers held, the last key pressed and released.
        parts=k.split('+')
        for m in parts[:-1]:cmd('input-send-event',{'events':[{'type':'key','data':{'down':True,'key':{'type':'qcode','data':m}}}]});time.sleep(.05)
        for down in (True,False):
            cmd('input-send-event',{'events':[{'type':'key','data':{'down':down,'key':{'type':'qcode','data':parts[-1]}}}]});time.sleep(.05)
        for m in reversed(parts[:-1]):cmd('input-send-event',{'events':[{'type':'key','data':{'down':False,'key':{'type':'qcode','data':m}}}]});time.sleep(.05)
        time.sleep(gap)

def gaps(path):
    data=Path(path).read_bytes();a=array.array('h');a.frombytes(data[44:len(data)//4*4])
    left=a[0::2];n=len(left);rate=48000
    # Inside sounding audio (melody is continuous) a run of >=1.5 ms of exact
    # zeros is an underrun: the DMA ring played silence it was never given.
    first=next((i for i,x in enumerate(left) if x),n);last=n-next((i for i,x in enumerate(reversed(left)) if x),n)
    runs=[];i=first
    while i<last:
        if left[i]==0:
            j=i
            while j<last and left[j]==0:j+=1
            if j-i>=72 and j<last:runs.append((i/rate,(j-i)/rate*1000))
            i=j
        else:i+=1
    print(f'{n/rate:.1f} s total, sound {first/rate:.1f}-{last/rate:.1f} s, underrun gaps {len(runs)}, total {sum(r[1] for r in runs):.0f} ms')
    for t,ms in runs[:40]:print(f'  at {t:7.2f} s  {ms:6.1f} ms')

if __name__=='__main__':
    op=sys.argv[1]
    if op=='prepare':
        a=sys.argv[2:];grub=a[a.index('--grub')+1] if '--grub' in a else None
        prepare(a[0],a[1],'--wav' in a,grub,'--paused' in a)
    elif op=='package':  # package NAME RELEASE_DIR : run the packaged templates unchanged
        name,rel=sys.argv[2],Path(sys.argv[3]);area=base/name;area.mkdir(exist_ok=False)
        for f in ['templates/boot-disk.img','templates/grub-disk.img','templates/firmware-vars.fd','firmware-code.fd']:
            shutil.copyfile(rel/f,area/Path(f).name)
        command=[str(rel/'qemu/qemu-system-x86_64.exe'),'-name','Charon-'+name,'-machine','q35,accel=whpx,kernel-irqchip=off','-cpu','max','-smp','4','-m','512','-nic','none','-display','sdl',
            '-drive','if=pflash,format=raw,readonly=on,file=firmware-code.fd','-drive','if=pflash,format=raw,file=firmware-vars.fd',
            '-drive','if=ide,format=raw,file=boot-disk.img,index=0','-drive','if=ide,format=raw,file=grub-disk.img,index=1',
            '-audiodev','dsound,id=charon_audio','-device','ich9-intel-hda','-device','hda-duplex,audiodev=charon_audio',
            '-qmp',f'tcp:127.0.0.1:{PORT},server=on,wait=off','-serial','file:serial.log']
        if '--winboot-vvfat' in sys.argv:  # what the r361 launcher adds on a Windows host
            for n,data in win_files().items():
                t=area/'windows-esp'/n;t.parent.mkdir(parents=True,exist_ok=True);t.write_bytes(data)
            command+=['-drive','if=ide,format=raw,file=fat:rw:windows-esp,index=3']
        child=subprocess.Popen(command,cwd=area,stdin=subprocess.DEVNULL,stdout=(area/'stdout.log').open('xb'),stderr=(area/'stderr.log').open('xb'),creationflags=subprocess.DETACHED_PROCESS)
        print('PID',child.pid)
        for _ in range(40):
            try:print(qmp()('human-monitor-command',{'command-line':'wavcapture '+str(area/'live.wav')+' charon_audio'}));break
            except OSError:time.sleep(.5)
    elif op=='keys':keys(sys.argv[2],float(sys.argv[3]),sys.argv[4:])
    elif op=='restart':  # restart NAME [DISKPATH=FILE ...]: stopped machine, same firmware variables
        from pyfatfs.PyFatFS import PyFatFS
        area=base/sys.argv[2]
        with socket.socket() as probe:assert probe.connect_ex(('127.0.0.1',PORT))!=0,'Stop the running Charon QEMU first'
        with (PyFatFS(str(area/"boot-disk.img")) if sys.argv[3:] else __import__("contextlib").nullcontext()) as fs:
            for spec in sys.argv[3:]:
                name,src=spec.split('=',1);data=Path(src).read_bytes()
                if fs.exists(name):fs.remove(name)
                with fs.openbin(name,'w') as f:f.write(data)
        command=json.loads((area/'process.json').read_text())['command']
        for log in ['stdout.log','stderr.log']:
            if (area/log).exists():(area/log).unlink()
        child=subprocess.Popen(command,cwd=area,stdin=subprocess.DEVNULL,stdout=(area/'stdout.log').open('xb'),stderr=(area/'stderr.log').open('xb'),creationflags=subprocess.DETACHED_PROCESS)
        print('PID',child.pid)
        for _ in range(40):
            try:print(qmp()('human-monitor-command',{'command-line':'wavcapture '+str(area/'live2.wav')+' charon_audio'}));break
            except OSError:time.sleep(.5)
    elif op=='wait':  # wait NAME PATTERN TIMEOUT [COUNT]
        log=base/sys.argv[2]/'serial.log';deadline=time.time()+float(sys.argv[4]);count=int(sys.argv[5]) if len(sys.argv)>5 else 1
        while time.time()<deadline:
            if log.exists() and log.read_bytes().count(sys.argv[3].encode())>=count:print('seen',sys.argv[3]);break
            time.sleep(.5)
        else:sys.exit('timeout waiting for '+sys.argv[3])
    elif op=='screen':print(qmp()('screendump',{'filename':str(base/sys.argv[2]/sys.argv[3])}))
    elif op=='quit':print(qmp()('quit'))
    elif op=='usbplug':  # usbplug NAME IMAGE ID
        q=qmp();print(q('human-monitor-command',{'command-line':f'drive_add 0 if=none,id={sys.argv[4]},format=raw,file={Path(sys.argv[3]).resolve()}'}))
        print(q('device_add',{'driver':'usb-storage','bus':'xhci.0','drive':sys.argv[4],'id':'dev'+sys.argv[4],'removable':True}))
    elif op=='usbunplug':print(qmp()('device_del',{'id':'dev'+sys.argv[3]}))
    elif op=='cdchange':  # cdchange NAME ISO|-  (- ejects)
        q=qmp()
        print(q('human-monitor-command',{'command-line':'eject -f cd0' if sys.argv[3]=='-' else f'change cd0 {Path(sys.argv[3]).resolve()} raw'}))
    elif op=='gaps':gaps(sys.argv[2])
    elif op=='serial':
        text=(base/sys.argv[2]/'serial.log').read_bytes().decode('utf-8','replace')
        pat=sys.argv[3] if len(sys.argv)>3 else None
        for line in text.splitlines():
            if not pat or pat in line:print(line)
