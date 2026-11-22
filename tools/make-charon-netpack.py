"""r407: build Charon's additional network driver pack (charon-drivers-r407-net): wired
network cards the first pack leaves out and Wi-Fi. Works beside make-charon-driverpack.py
(which it does not import or change). See src/efi/NETWORK-DRIVERS.txt.

  make-charon-netpack.py fetch    DL_DIR
  make-charon-netpack.py vm-start DL_DIR QEMU_EXE          start the Alpine build machine (tcg), install gcc, unpack iPXE
  make-charon-netpack.py vm-run   DL_DIR "shell command" [SECONDS]   run a command in it (serial console)
  make-charon-netpack.py vm-push  DL_DIR                   send the patches and the build script again
  make-charon-netpack.py vm-build DL_DIR                   build every group (GROUPS) -> /w/out in the machine
  make-charon-netpack.py vm-get   DL_DIR                   copy /w/out to DL_DIR/ipxe-out
  make-charon-netpack.py vm-stop  DL_DIR                   power the machine off and delete its folder
  make-charon-netpack.py ipxe     DL_DIR QEMU_EXE          vm-start + vm-build + vm-get + vm-stop
  make-charon-netpack.py assemble DL_DIR OUT_DIR           write the pack (OUT_DIR must be new)
  make-charon-netpack.py verify   OUT_DIR                  PE32+/x86-64/EFI driver + hashes equal to manifest-net.json
  make-charon-netpack.py qemu-test DL_DIR QEMU_EXE FOLDER MODEL [NAME]   load FOLDER's nic-*/wifi-* in OVMF (tcg), DHCP on MODEL

Nothing downloaded is run on the host: iPXE is built INSIDE a QEMU machine (Alpine Linux
"virt" ISO, gcc from Alpine's repository). The machine uses accel=tcg (several threads), so
it can run while another machine holds WHPX. Input travels as a tar archive on a read-only
raw disk and, for later changes, over HTTP from a server this tool starts on 127.0.0.1;
output comes back as a tar archive on a second raw disk.
The other drivers are built on the host by their own tools and picked up by assemble from
their output folders when these exist: tools/build-charon-netdrv-rge.py and -alc.py (OpenBSD
rge/alc, LLVM) -> DL_DIR/out-rge, -wifi.py (OpenBSD mwx + net80211, LLVM) -> DL_DIR/out-wifi,
-usbcfg.py (USB CDC configuration selector, LLVM) -> DL_DIR/out-usbcfg, -intelundi.py (Intel
UNDI, MSVC in an edk2 workspace) -> DL_DIR/out-intelundi. Where each file goes in the pack
(top folder = loaded by Charon's rule, manual/, experimental/) is HOST_BUILT and PLACE below.
"""
import hashlib,io,json,os,re,shutil,socket,struct,subprocess,sys,tarfile,time,urllib.request
from pathlib import Path

HERE=Path(__file__).resolve().parent
IPXE_COMMIT='1fa3091dce0609b90e7b79fa16b57b1d11e6b7bd'
IPXE_TAR=f'ipxe/ipxe-{IPXE_COMMIT}.tar.gz'
INPUTS={
 IPXE_TAR:(f'https://github.com/ipxe/ipxe/archive/{IPXE_COMMIT}.tar.gz','3854c13ed038dee4c0736ecec8fb3ada958fc6903638586b06e481eb4b5bd4dd'),
 'alpine/alpine-virt-3.24.4-x86_64.iso':('https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/x86_64/alpine-virt-3.24.4-x86_64.iso','157d353f8e54252cad2d9812a0d56bb7fa2231fc154a110e1d180d7480ef8b4f'),
}
SERIAL_PORT=4497;HTTP_PORT=8767
PATCH_DIR=HERE/'charon-netpack-ipxe'      # files copied over the iPXE tree + *.patch applied with patch -p1
# File name in the pack -> iPXE driver objects linked into that one UEFI driver image.
GROUPS={
 'nic-16-intel-newer':['intel'],
 'nic-18-server':['etherfabric','sfc_hunt','phantom','vxge','myri10ge','exanic','hermon','golan'],
 'wifi-10-atheros':['ath5k','ath9k'],
 'wifi-11-legacy':['rtl8180','rtl8185','prism2_pci','prism2_plx'],
}
GROUP_TEXT={
 'nic-16-intel-newer':'Intel 1G/2.5G from current iPXE: adds I219 (18)-(29) of Meteor/Arrow/Lunar/Panther Lake, Killer E3100G/E3100XG (I225/I226 based) and the remaining I225/I226 variants',
 'nic-18-server':'server cards: Solarflare SFC4000/SFC9xxx (etherfabric, sfc_hunt), NetXen/QLogic Phantom, Neterion X3100 (vxge), Myricom Myri-10G, Exablaze ExaNIC, Mellanox ConnectX-2/3 (hermon) and ConnectX-4/5/6 (golan)',
 'wifi-10-atheros':'Wi-Fi: Atheros AR5xxx (ath5k) and AR9xxx 802.11n (ath9k) with iPXE\'s 802.11 layer (WPA/WPA2-PSK); Charon Wi-Fi face (efi_wifi.c)',
 'wifi-11-legacy':'Wi-Fi: Realtek RTL8180/RTL8185 and Intersil Prism 2/2.5/3 PCI cards with iPXE\'s 802.11 layer',
}

def sha256(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for block in iter(lambda:f.read(1<<20),b''):h.update(block)
    return h.hexdigest()

def fetch(dl):
    for rel,(url,digest) in INPUTS.items():
        target=dl/rel
        if not target.exists():
            target.parent.mkdir(parents=True,exist_ok=True)
            print('download',url)
            with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'netpack-fetch'})) as r,open(target,'wb') as f:
                for block in iter(lambda:r.read(1<<20),b''):f.write(block)
        got=sha256(target)
        if got!=digest:raise SystemExit(f'{rel}: SHA-256 {got}, expected {digest}')
        print('ok',rel)

# ---- the build machine -------------------------------------------------------
SETUP_SH=r'''#!/bin/sh
# Runs once inside the Alpine build machine. /w holds this script and the iPXE source archive.
cd /w
ip link set eth0 up
udhcpc -i eth0 -q -n || { echo NETPACK-SETUP rc=10; exit 10; }
printf 'https://dl-cdn.alpinelinux.org/alpine/v3.24/main\n' >/etc/apk/repositories
apk add --no-cache build-base perl xz-dev >apk.log 2>&1 || { tail -5 apk.log; echo NETPACK-SETUP rc=11; exit 11; }
echo "@SHA@  ipxe-@COMMIT@.tar.gz" | sha256sum -c || { echo NETPACK-SETUP rc=12; exit 12; }
tar xzf ipxe-@COMMIT@.tar.gz && mv ipxe-@COMMIT@ ipxe || { echo NETPACK-SETUP rc=13; exit 13; }
# iPXE's Makefile wants the git commit of the tree; an archive has no .git, so name the commit it is.
mkdir -p ipxe/.git && echo @COMMIT@ >ipxe/.git/HEAD
cp -a ipxe ipxe-pristine
mkdir -p /w/out
echo NETPACK-SETUP rc=0
'''
PUSH_SH=r'''#!/bin/sh
# Fetches the current patch set from the host and applies it to a fresh copy of the changed files.
cd /w || exit 1
rm -rf push && mkdir push && cd push
wget -q -O push.tar http://10.0.2.2:@HTTP@/push.tar || { echo NETPACK-PUSH rc=20; exit 20; }
tar xf push.tar
cd /w
# restore every file a previous push touched, then apply again
if [ -f touched.txt ]; then while read f; do if [ -f ipxe-pristine/$f ]; then cp ipxe-pristine/$f ipxe/$f; else rm -f ipxe/$f; fi; done <touched.txt; fi
: >touched.txt
if [ -d push/overlay ]; then (cd push/overlay && find . -type f | sed 's|^\./||') >>touched.txt; cp -a push/overlay/. ipxe/; fi
for p in push/*.patch; do [ -f "$p" ] || continue
  grep '^+++ ' "$p" | sed 's|^+++ [^/]*/||; s|[[:space:]].*$||' >>touched.txt
  patch -p1 -d ipxe <"$p" || { echo NETPACK-PUSH rc=21; exit 21; }
done
cp push/build.sh push/groups.txt /w/
echo NETPACK-PUSH rc=0
'''
BUILD_SH=r'''#!/bin/sh
# Builds every group of groups.txt as one UEFI driver image (*.efidrv). Log: /w/out/make-*.log
cd /w/ipxe/src || exit 1
B=bin-x86_64-efi
: >/w/out/built.txt
while read name members; do
  ok=""
  for m in $members; do
    if make -j@JOBS@ VERSION=@VER@ $B/$m.efidrv >>/w/out/make-$name.log 2>&1; then ok="${ok:+$ok--}$m"; else echo "$name: driver $m FAILED" >>/w/out/built.txt; fi
  done
  [ -n "$ok" ] || continue
  if make -j@JOBS@ VERSION=@VER@ $B/$ok.efidrv >>/w/out/make-$name.log 2>&1; then cp $B/$ok.efidrv /w/out/$name.efi; echo "$name $ok" >>/w/out/built.txt
  else echo "$name FAILED $ok" >>/w/out/built.txt; fi
done </w/groups.txt
# Debug variant of the Atheros Wi-Fi image: the 802.11 layer, the WPA handshake and the Wi-Fi face print their
# messages on the firmware's text console. For a first run on real hardware; kept in experimental/ (not loaded by rule).
if [ -f /w/out/wifi-10-atheros.efi ]; then
  if make -j@JOBS@ VERSION=@VER@ $B/ath5k--ath9k.efidrv DEBUG=efi_wifi,net80211,wpa,wpa_psk >/w/out/make-wifi-debug.log 2>&1; then
    cp $B/ath5k--ath9k.efidrv /w/out/wifi-10-atheros-debug.efi; echo "wifi-10-atheros-debug ath5k--ath9k DEBUG=efi_wifi,net80211,wpa,wpa_psk" >>/w/out/built.txt
  else echo "wifi-10-atheros-debug FAILED" >>/w/out/built.txt; fi
fi
gcc --version | head -1 >/w/out/toolchain.txt
ld --version | head -1 >>/w/out/toolchain.txt
make --version | head -1 >>/w/out/toolchain.txt
apk info -v 2>/dev/null | sort >>/w/out/toolchain.txt
# every file the push changed or added, as one diff against the pinned source
(cd /w && sort -u touched.txt | while read f; do diff -uN ipxe-pristine/$f ipxe/$f; done >/w/out/ipxe-changes.diff 2>/dev/null)
cat /w/out/built.txt
echo NETPACK-BUILD rc=0
'''
def area_of(dl):return dl/'build-vm'
def serial(dl,command,seconds=120,quiet=False):
    """Runs one shell command in the machine through its serial console; returns (rc, output)."""
    s=socket.create_connection(('127.0.0.1',SERIAL_PORT),timeout=5);s.settimeout(2)
    mark=f'NPK{int(time.time()*1000)%100000000}'
    s.sendall((command+f'; echo {mark}=$?=\n').encode())
    seen=b'';end=time.time()+seconds;found=None
    while time.time()<end:
        try:data=s.recv(65536)
        except socket.timeout:data=b''
        except OSError:break
        if data:
            seen+=data
            found=re.search(mark.encode()+rb'=(\d+)=',seen)
            if found:break
    s.close()
    with (area_of(dl)/'serial.log').open('ab') as log:log.write(seen)
    text=seen.decode('utf-8','replace').replace('\r','')
    if not found:
        if not quiet:print(text[-3000:])
        raise SystemExit(f'machine: no answer in {seconds} s to: {command[:80]}')
    body=text[:text.rfind(mark)]
    body=''.join(line for line in body.splitlines(True) if 'echo '+mark not in line)   # drop the echoed command line
    return int(found.group(1)),body

def push_tar(dl):
    """DL_DIR/vm-in/push.tar: overlay files, patches, build script, group list."""
    served=dl/'vm-in';served.mkdir(exist_ok=True)
    groups=''.join(f'{name} {" ".join(members)}\n' for name,members in GROUPS.items())
    build=BUILD_SH.replace('@JOBS@','10').replace('@VER@','charon-'+IPXE_COMMIT[:8])
    with tarfile.open(served/'push.tar','w') as tar:
        def add(name,data):
            info=tarfile.TarInfo(name);info.size=len(data);info.mode=0o755;tar.addfile(info,io.BytesIO(data))
        add('build.sh',build.encode());add('groups.txt',groups.encode())
        if PATCH_DIR.is_dir():
            for path in sorted(PATCH_DIR.rglob('*')):
                if not path.is_file():continue
                rel=path.relative_to(PATCH_DIR).as_posix()
                data=path.read_bytes().replace(b'\r\n',b'\n')
                if rel.endswith('.patch') and '/' not in rel:add(rel,data)
                elif rel.startswith('overlay/'):add(rel,data)
    return build,groups

def vm_start(dl,qemu):
    fetch(dl)
    area=area_of(dl);area.mkdir(exist_ok=False)
    source=dl/IPXE_TAR
    setup=SETUP_SH.replace('@SHA@',INPUTS[IPXE_TAR][1]).replace('@COMMIT@',IPXE_COMMIT)
    push=PUSH_SH.replace('@HTTP@',str(HTTP_PORT))
    with tarfile.open(area/'in.img','w') as tar:
        for name,data in (('setup.sh',setup.encode()),('push.sh',push.encode())):
            info=tarfile.TarInfo(name);info.size=len(data);info.mode=0o755;tar.addfile(info,io.BytesIO(data))
        tar.add(source,arcname=source.name)
    with open(area/'in.img','ab') as f:f.write(bytes(1<<20))
    with open(area/'out.img','wb') as f:f.truncate(96<<20)
    for port in (SERIAL_PORT,HTTP_PORT):
        with socket.socket() as probe:
            if probe.connect_ex(('127.0.0.1',port))==0:raise SystemExit(f'port {port} is in use')
    push_tar(dl)
    flags=subprocess.DETACHED_PROCESS|subprocess.CREATE_NEW_PROCESS_GROUP if os.name=='nt' else 0
    server=subprocess.Popen([sys.executable,'-I','-m','http.server',str(HTTP_PORT),'--bind','127.0.0.1','--directory',str((dl/'vm-in').resolve())],
        stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=flags)
    command=[str(qemu),'-name','charon-netpack-build','-machine','q35','-accel','tcg,thread=multi,tb-size=1024','-cpu','max','-smp','10','-m','8192','-display','none',
        '-cdrom',str((dl/'alpine/alpine-virt-3.24.4-x86_64.iso').resolve()),'-boot','d',
        '-drive','if=virtio,format=raw,file=in.img,readonly=on','-drive','if=virtio,format=raw,file=out.img,cache=writethrough',
        '-nic','user,model=virtio-net-pci','-serial',f'tcp:127.0.0.1:{SERIAL_PORT},server=on,wait=off']
    child=subprocess.Popen(command,cwd=area,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=(area/'stderr.log').open('wb'),creationflags=flags)
    (area/'pids.json').write_text(json.dumps(dict(qemu=child.pid,http=server.pid)))
    print('qemu pid',child.pid,'http pid',server.pid)
    # log in
    end=time.time()+900;seen=b'';s=None
    while time.time()<end:
        if child.poll() is not None:raise SystemExit('QEMU ended: '+(area/'stderr.log').read_text(errors='replace')[-500:])
        try:
            if s is None:s=socket.create_connection(('127.0.0.1',SERIAL_PORT),timeout=2);s.settimeout(5)
            data=s.recv(65536)
        except socket.timeout:
            s.sendall(b'\n');continue
        except OSError:
            time.sleep(0.5);s=None;continue
        seen=(seen+data)[-65536:]
        with (area/'serial.log').open('ab') as log:log.write(data)
        if re.search(rb'login: $',seen):break
    else:raise SystemExit('no login prompt')
    s.sendall(b'root\n');time.sleep(3);s.close()
    rc,out=serial(dl,'stty -echo cols 250; mkdir -p /w && tar xf /dev/vda -C /w && echo unpacked',120)
    print(out.strip()[-200:])
    rc,out=serial(dl,'sh /w/setup.sh 2>&1 | tail -5',2400)
    print(out.strip()[-600:])
    if 'NETPACK-SETUP rc=0' not in out:raise SystemExit('setup failed')
    vm_push(dl)

def vm_push(dl):
    push_tar(dl)
    rc,out=serial(dl,'sh /w/push.sh 2>&1 | tail -15',300)
    print(out.strip()[-1500:])
    if 'NETPACK-PUSH rc=0' not in out:raise SystemExit('push failed')

def vm_build(dl):
    # Two builds in one tree corrupt its object archive ("file truncated"): never start a second one.
    rc,out=serial(dl,"ps | grep -c '[b]uild.sh'",30,quiet=True)
    if out.strip().splitlines()[-1:]!=['0']:raise SystemExit('a build is still running in the machine; wait for it (vm-run "tail -2 /w/build.console")')
    vm_push(dl)
    serial(dl,'rm -rf /w/out/*; (nohup sh /w/build.sh >/w/build.console 2>&1 &) ; echo started',30)
    while True:
        time.sleep(30)
        rc,out=serial(dl,'tail -2 /w/build.console',60,quiet=True)
        print(out.strip()[-300:],flush=True)
        if 'NETPACK-BUILD rc=' in out:break

def vm_get(dl):
    area=area_of(dl)
    rc,out=serial(dl,'cd /w/out && tar cf /dev/vdb . && sync && echo stored',300)
    if 'stored' not in out:raise SystemExit('could not store the output: '+out[-300:])
    out_dir=dl/'ipxe-out';out_dir.mkdir(exist_ok=True)
    with open(area/'out.img','rb') as raw:data=raw.read()
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        for member in tar:
            name=Path(member.name).name
            if not member.isfile() or not re.fullmatch(r'[A-Za-z0-9._-]+',name):continue
            (out_dir/name).write_bytes(tar.extractfile(member).read());print(name,member.size)
    build,groups=push_tar(dl)
    (out_dir/'build.sh').write_text(build,encoding='utf-8',newline='\n');(out_dir/'groups.txt').write_text(groups,encoding='utf-8',newline='\n')

def vm_stop(dl):
    area=area_of(dl)
    if not area.exists():return
    pids=json.loads((area/'pids.json').read_text()) if (area/'pids.json').exists() else {}
    try:serial(dl,'poweroff',20,quiet=True)
    except (SystemExit,OSError):pass
    time.sleep(8)
    for name in ('qemu','http'):
        pid=pids.get(name)
        if pid:subprocess.run(['taskkill','/F','/PID',str(pid)],capture_output=True)
    time.sleep(1)
    keep=dl/'ipxe-out';keep.mkdir(exist_ok=True)
    if (area/'serial.log').exists():shutil.copyfile(area/'serial.log',keep/'build-machine-serial.log')
    shutil.rmtree(area,ignore_errors=True);shutil.rmtree(dl/'vm-in',ignore_errors=True)
    print('machine stopped and deleted')

# State of each file for the manifest (what was actually run); file name -> text. Files not named get 'built; not run'.
STATE={
 'nic-16-intel-newer':'built; run in QEMU 11.1/OVMF (tcg), loaded together with nic-18 and wifi-10/11 the way Charon loads them: e1000 (82540EM), e1000-82545em, e1000e (82574L): adapter + DHCP lease 10.0.2.15 each; igb (82576): binds, link stays down, no lease (NOT passed, as with the first pack); the IDs added over nic-10 (I219 18-29, I225/I226 variants, Killer E3100) were never run',
 'nic-18-server':'built; loads in QEMU/OVMF and binds nothing; never run with such a card',
 'wifi-10-atheros':'built; loads in QEMU/OVMF without such a card (binds nothing; an Ethernet card beside it still gets its lease); THE WI-FI FUNCTION WAS NEVER RUN (no emulator has these cards)',
 'wifi-11-legacy':'built; loads in QEMU/OVMF without such a card (binds nothing); THE WI-FI FUNCTION WAS NEVER RUN (no emulator has these cards)',
 'wifi-10-atheros-debug':'built, not loaded anywhere; debug variant of wifi-10-atheros for a first run on real hardware (prints the 802.11 layer\'s messages on the firmware console)',
}
# host-built file name -> sub-folder of the pack, overriding HOST_BUILT. manual/ = by hand only (like the first pack's manual/).
PLACE={'nic-19-intel-undi-gig.efi':'manual','nic-19-intel-undi-xgig.efi':'manual','nic-19-intel-undi-i40e.efi':'manual','nic-19-intel-undi-ice.efi':'manual'}
# Built here but NOT put into the pack: file name -> reason (recorded in manifest-net.json under "not_included").
EXCLUDE={}
LICENCE_TEXTS={}  # further files for licences/: name -> text
README_NET=HERE.parent/'src/efi/NETWORK-DRIVERS.txt'
def extra_sources(dl):
    """(folder, name under source/) of what is shipped as source besides iPXE."""
    return [(HERE.parent/'src/netdrv','netdrv-glue'),(dl/'openbsd-rge','openbsd-rge'),(dl/'openbsd-alc','openbsd-alc'),(dl/'openbsd-wifi','openbsd-wifi'),(dl/'qemu-tests','qemu-tests')]
# ---- the pack ---------------------------------------------------------------
# Folders of the host-built drivers (see the build-charon-netdrv-*.py tools) -> sub-folder of the pack.
# experimental/ is NOT read by Charon (it lists \EFI\HAK\drivers only, not its sub-folders).
HOST_BUILT={'out-intelundi':'','out-usbcfg':'','out-rge':'experimental','out-wifi':'experimental'}
IPXE_SOURCES={'intel':'drivers/net/intel.c','etherfabric':'drivers/net/etherfabric.c','sfc_hunt':'drivers/net/sfc/sfc_hunt.c','phantom':'drivers/net/phantom/phantom.c',
 'vxge':'drivers/net/vxge/vxge_main.c','myri10ge':'drivers/net/myri10ge.c','exanic':'drivers/net/exanic.c','hermon':'drivers/infiniband/hermon.c','golan':'drivers/infiniband/golan.c',
 'ath5k':'drivers/net/ath/ath5k/ath5k.c','ath9k':'drivers/net/ath/ath9k/ath9k.c','rtl8180':'drivers/net/rtl818x/rtl8180.c','rtl8185':'drivers/net/rtl818x/rtl8185.c',
 'prism2_pci':'drivers/net/prism2_pci.c','prism2_plx':'drivers/net/prism2_plx.c'}
PCI_ROM=re.compile(r'PCI_ROM\s*\(\s*0x([0-9a-fA-F]{4})\s*,\s*0x([0-9a-fA-F]{4})\s*,\s*"[^"]*"\s*,\s*"([^"]*)"')

def pe_info(path):
    data=Path(path).read_bytes()
    if data[:2]!=b'MZ':raise SystemExit(f'{path}: no MZ header')
    pe=struct.unpack_from('<I',data,0x3c)[0]
    if data[pe:pe+4]!=b'PE\0\0':raise SystemExit(f'{path}: no PE signature')
    machine,sections=struct.unpack_from('<HH',data,pe+4)
    magic=struct.unpack_from('<H',data,pe+24)[0];subsystem=struct.unpack_from('<H',data,pe+24+68)[0]
    optional=struct.unpack_from('<H',data,pe+20)[0]
    names=[data[pe+24+optional+40*i:pe+24+optional+40*i+8].rstrip(b'\0').decode('latin-1') for i in range(sections)]
    reloc_size=struct.unpack_from('<II',data,pe+24+112+8*5)[1] if magic==0x20b else 0
    return dict(machine=machine,magic=magic,subsystem=subsystem,sections=names,reloc_size=reloc_size,size=len(data))
def check_pe(path):
    info=pe_info(path)
    if info['magic']!=0x20b or info['machine']!=0x8664 or info['subsystem'] not in (11,12):
        raise SystemExit(f'{path}: not a PE32+ x86-64 EFI driver image: {info}')
    return info

def ipxe_ids(dl,members):
    """PCI IDs of the iPXE drivers of one image: PCI_ROM lines of the pinned source plus those the patches add."""
    ids=[]
    with tarfile.open(dl/IPXE_TAR) as tar:
        for member in members:
            rel=IPXE_SOURCES[member]
            text=tar.extractfile(f'ipxe-{IPXE_COMMIT}/src/{rel}').read().decode('utf-8','replace')
            for patch in sorted(PATCH_DIR.glob('*.patch')):
                body=patch.read_text(encoding='utf-8')
                if f'b/src/{rel}' in body:text+=''.join(line[1:]+'\n' for line in body.splitlines() if line.startswith('+') and not line.startswith('+++'))
            for vendor,device,name in PCI_ROM.findall(text):
                entry=f'pci:{vendor.lower()}:{device.lower()} {name}'
                if entry not in ids:ids.append(entry)
    return ids

def assemble(dl,out):
    out.mkdir(parents=True,exist_ok=False)
    built=dl/'ipxe-out';entries=[];skipped=[]
    lic=out/'licences';src=out/'source';lic.mkdir();(src/'ipxe').mkdir(parents=True)
    # iPXE images
    lines=(built/'built.txt').read_text(encoding='utf-8').splitlines() if (built/'built.txt').exists() else []
    done={line.split()[0]:line.split()[1].split('--') for line in lines if len(line.split())==2 and 'FAILED' not in line}
    for name,members in GROUPS.items():
        image=built/f'{name}.efi'
        if name not in done or not image.exists():print('NOT BUILT, left out:',name);continue
        check_pe(image);shutil.copyfile(image,out/f'{name}.efi')
        wifi=name.startswith('wifi-')
        entries.append(dict(file=f'{name}.efi',kind='Wi-Fi adapter driver' if wifi else 'network adapter driver',name='iPXE '+' + '.join(done[name]),
            version=f'iPXE git {IPXE_COMMIT} (2.0.0+), built with VERSION=charon-{IPXE_COMMIT[:8]}',description=GROUP_TEXT[name],
            source=INPUTS[IPXE_TAR][0],source_sha256=INPUTS[IPXE_TAR][1],changes='source/ipxe/changes (patches and added files), source/ipxe/ipxe-changes.diff',
            licence='GPL-2.0-or-later (iPXE as a whole; see licences/ipxe-COPYING*)',sha256=sha256(out/f'{name}.efi'),size=(out/f'{name}.efi').stat().st_size,
            ids=ipxe_ids(dl,done[name]),not_built=[m for m in members if m not in done[name]],
            state=STATE.get(name,'built; not run'),loaded_by_charon=True))
    debug=built/'wifi-10-atheros-debug.efi'
    if debug.exists() and 'wifi-10-atheros' in done:   # same sources with iPXE's debug messages on; never loaded by rule
        (out/'experimental').mkdir(exist_ok=True);check_pe(debug);shutil.copyfile(debug,out/'experimental'/debug.name)
        entries.append(dict(file='experimental/'+debug.name,kind='Wi-Fi adapter driver, debug build',name='iPXE ath5k + ath9k, DEBUG=efi_wifi,net80211,wpa,wpa_psk',
            version=f'iPXE git {IPXE_COMMIT} (2.0.0+), built with VERSION=charon-{IPXE_COMMIT[:8]}',description='wifi-10-atheros.efi with the messages of the 802.11 layer, the WPA handshake and the Wi-Fi face printed on the firmware text console',
            source=INPUTS[IPXE_TAR][0],source_sha256=INPUTS[IPXE_TAR][1],licence='GPL-2.0-or-later (iPXE as a whole; see licences/ipxe-COPYING*)',
            sha256=sha256(out/'experimental'/debug.name),size=debug.stat().st_size,ids=ipxe_ids(dl,done['wifi-10-atheros']),state=STATE['wifi-10-atheros-debug'],loaded_by_charon=False))
    with tarfile.open(dl/IPXE_TAR) as tar:
        for name in ('COPYING','COPYING.GPLv2','COPYING.UBDL'):
            (lic/f'ipxe-{name}.txt').write_bytes(tar.extractfile(f'ipxe-{IPXE_COMMIT}/{name}').read())
    shutil.copyfile(dl/IPXE_TAR,src/'ipxe'/Path(IPXE_TAR).name)
    shutil.copytree(PATCH_DIR,src/'ipxe/changes')
    for name in ('build.sh','groups.txt','built.txt','toolchain.txt','ipxe-changes.diff'):
        if (built/name).exists():shutil.copyfile(built/name,src/'ipxe'/name)
    shutil.copyfile(Path(__file__),src/'make-charon-netpack.py')
    for tool in sorted(HERE.glob('build-charon-netdrv-*.py')):shutil.copyfile(tool,src/tool.name)   # the host builds
    # host-built drivers: every folder has one or more *.json describing its *.efi
    for folder,sub in HOST_BUILT.items():
        base=dl/folder
        if not base.is_dir():continue
        for meta in sorted(base.glob('*.json')):
            data=json.loads(meta.read_text(encoding='utf-8'))
            if isinstance(data,dict) and isinstance(data.get('files'),list):data=data['files']
            items=data if isinstance(data,list) else [data]
            for item in items:
                if not isinstance(item,dict):continue
                file=item.get('file') or item.get('image') or item.get('name')
                if not file or not str(file).endswith('.efi') or not (base/Path(str(file)).name).exists():continue
                file=Path(str(file)).name
                if file in EXCLUDE:skipped.append(dict(file=file,sha256_of_local_build=sha256(base/file),reason=EXCLUDE[file]));continue
                place=PLACE.get(file,sub);target=out/place/file if place else out/file
                target.parent.mkdir(exist_ok=True);check_pe(base/file);shutil.copyfile(base/file,target)
                entry=dict(item);entry['file']=(place+'/' if place else '')+file;entry['sha256']=sha256(target);entry['size']=target.stat().st_size
                entry['loaded_by_charon']=not place
                if 'ids' not in entry and isinstance(entry.get('devices'),list):entry['ids']=entry['devices']   # one key for every entry
                if file in STATE:entry['state']=STATE[file]
                entries.append(entry)
        extra=src/folder.replace('out-','')
        for path in sorted(base.rglob('*')):   # provenance, evidence and changes of the host-built drivers
            if path.is_file() and len(path.relative_to(base).parts)<=2 and (path.name.endswith('sources.json') or path.name=='NOTES.txt' or path.suffix in ('.patch','.nsh')
                    or path.name.endswith('.serial.txt') or path.name.upper().startswith(('LICENCE','LICENSE'))):
                extra.mkdir(exist_ok=True);shutil.copyfile(path,extra/path.name)
    for folder,target in extra_sources(dl):
        if Path(folder).exists():shutil.copytree(folder,src/target,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.o','*.obj','*.efi','*.elf','*.map','*.pdb','*.tar.gz'))
    for name,text in LICENCE_TEXTS.items():(lic/name).write_text(text,encoding='utf-8',newline='\n')
    for path in sorted(src.rglob('*')):   # licence files that came with the sources also go to licences/
        if path.is_file() and 'ipxe' not in path.relative_to(src).parts[:1] and (path.name.upper().startswith(('LICENCE','LICENSE')) or path.name.endswith('-license')):
            shutil.copyfile(path,lic/(path.relative_to(src).parts[0]+'-'+path.name+('' if path.suffix=='.txt' else '.txt')))
    manifest=dict(pack='charon-drivers-r407-net',made=time.strftime('%Y-%m-%d'),tool='tools/make-charon-netpack.py',
        note='Additional network drivers for Charon. Files in the top folder follow the nic-*/nic-usb-*/wifi-*/net-* rule of CharonWsNet.h LoadDrivers; files in experimental/ are never loaded by Charon by itself. Nothing here was run on a physical computer.',
        files=entries,not_included=skipped)
    (out/'manifest-net.json').write_text(json.dumps(manifest,indent=1,ensure_ascii=False)+'\n',encoding='utf-8',newline='\n')
    if README_NET.exists():shutil.copyfile(README_NET,out/'NETWORK-DRIVERS.txt')
    verify(out)

def verify(out):
    manifest=json.loads((out/'manifest-net.json').read_text(encoding='utf-8'))
    listed={entry['file']:entry for entry in manifest['files']}
    found=sorted(p.relative_to(out).as_posix() for p in out.rglob('*.efi') if 'source' not in p.relative_to(out).parts)
    for rel in found:
        if rel not in listed:raise SystemExit(f'{rel}: not in manifest-net.json')
        info=check_pe(out/rel)
        if sha256(out/rel)!=listed[rel]['sha256']:raise SystemExit(f'{rel}: SHA-256 differs from manifest-net.json')
        print(f'ok {rel}: PE32+ x86-64, subsystem {info["subsystem"]}, {info["size"]} bytes, relocations {info["reloc_size"]}, {len(listed[rel].get("ids") or listed[rel].get("devices") or [])} ids')
    for rel in listed:
        if rel not in found:raise SystemExit(f'{rel}: in manifest-net.json but missing')
    print(f'{len(found)} images, manifest consistent')

# ---- one run in QEMU/OVMF (tcg) -----------------------------------------------
def qemu_test(dl,qemu,folder,model,name=None):
    """Loads every top-level nic-*/wifi-* image of `folder` in the UEFI shell of QEMU's OVMF as Charon does: all of
    them in name order WITHOUT connecting (load -nc), then one connect pass over everything, asks DHCP on the card `model` (its option ROM switched off) and keeps the serial
    log as DL_DIR/qemu-tests/<name>.serial.txt. model 'none' = a machine without any network card."""
    share=Path(qemu).parent/'share';name=name or model.split(',')[0]
    area=dl/'qemu-test-tmp';shutil.rmtree(area,ignore_errors=True);(area/'fat').mkdir(parents=True)
    images=sorted(p.name for p in Path(folder).glob('*.efi') if p.name.startswith(('nic-','wifi-')))
    for image in images:shutil.copyfile(Path(folder)/image,area/'fat'/image)
    script=['echo -off','fs0:']+[f'load -nc {image}' for image in images]+['connect -r','echo NETPACK-DRIVERS','drivers','echo NETPACK-BEFORE','ifconfig -l']
    if model!='none':script+=['ifconfig -s eth0 dhcp','stall 12000000','echo NETPACK-AFTER','ifconfig -l eth0']
    script+=['echo NETPACK-TEST-DONE','reset -s']
    (area/'fat/startup.nsh').write_text('\r\n'.join(script)+'\r\n',encoding='ascii')
    shutil.copyfile(share/'edk2-i386-vars.fd',area/'vars.fd')
    command=[str(qemu),'-name','charon-netpack-test','-machine','q35','-accel','tcg,thread=multi','-cpu','max','-smp','2','-m','1024','-display','none',
        '-drive',f'if=pflash,format=raw,readonly=on,file={share/"edk2-x86_64-code.fd"}','-drive','if=pflash,format=raw,file=vars.fd',
        '-drive','format=raw,file=fat:rw:fat','-serial','file:serial.txt','-net','none']
    if model!='none':command+=['-netdev','user,id=n0','-device',f'{model},netdev=n0,romfile=']
    child=subprocess.Popen(command,cwd=area,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=(area/'stderr.log').open('wb'))
    try:
        end=time.time()+420
        while time.time()<end and child.poll() is None:
            time.sleep(3)
            log=(area/'serial.txt').read_bytes() if (area/'serial.txt').exists() else b''
            if b'NETPACK-TEST-DONE\r' in log.replace(b'echo NETPACK-TEST-DONE',b''):time.sleep(3);break
    finally:
        if child.poll() is None:child.kill();child.wait()
    raw=(area/'serial.txt').read_bytes() if (area/'serial.txt').exists() else b''
    text=re.sub(r'\x1b\[[0-9;?]*[A-Za-z]','',raw.decode('utf-8','replace')).replace('\r','')
    out=dl/'qemu-tests';out.mkdir(exist_ok=True)
    (out/f'{name}.serial.txt').write_text(f'# {" ".join(command)}\n# images: {" ".join(images)}\n'+text,encoding='utf-8',newline='\n')
    shutil.rmtree(area,ignore_errors=True)
    tail=text[text.find('NETPACK-BEFORE'):] if 'NETPACK-BEFORE' in text else text[-1500:]
    print(tail[-1800:])
    lease=re.search(r'ipv4 address\s*:\s*(\d+\.\d+\.\d+\.\d+)',text[text.find('NETPACK-AFTER'):]) if 'NETPACK-AFTER' in text else None
    print('RESULT',name,'lease',lease.group(1) if lease else None,'errors',len(re.findall(r'(?i)load error|image error|unsupported',text)))

if __name__=='__main__':
    args=sys.argv[1:]
    if not args:sys.exit(__doc__)
    cmd=args[0];dl=Path(args[1]).resolve() if len(args)>1 else None
    if cmd=='fetch':fetch(dl)
    elif cmd=='vm-start':vm_start(dl,Path(args[2]))
    elif cmd=='vm-run':
        rc,out=serial(dl,args[2],int(args[3]) if len(args)>3 else 120);sys.stdout.write(out);sys.exit(rc)
    elif cmd=='vm-push':vm_push(dl)
    elif cmd=='vm-build':vm_build(dl)
    elif cmd=='vm-get':vm_get(dl)
    elif cmd=='vm-stop':vm_stop(dl)
    elif cmd=='ipxe':
        vm_start(dl,Path(args[2]))
        try:vm_build(dl);vm_get(dl)
        finally:vm_stop(dl)
    elif cmd=='assemble':assemble(dl,Path(args[2]).resolve())
    elif cmd=='verify':verify(dl)
    elif cmd=='qemu-test':qemu_test(dl,Path(args[2]),Path(args[3]),args[4],args[5] if len(args)>5 else None)
    else:sys.exit(__doc__)
