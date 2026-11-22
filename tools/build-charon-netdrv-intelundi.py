"""r407: build Intel's open-source UEFI UNDI network drivers (IntelUndiPkg) for x86-64.

  build-charon-netdrv-intelundi.py DL_DIR EDK2_WORKSPACE NASM_DIR OUT_DIR [gig xgig i40e ice]
  build-charon-netdrv-intelundi.py qemu-test OUT_DIR QEMU_EXE IMAGE.efi DEVICE...   (e1000e igb ...; accel=tcg)
  build-charon-netdrv-intelundi.py licences DL_DIR          (licence header of every file of both source trees)
  build-charon-netdrv-intelundi.py record-tests OUT_DIR     (write what the saved QEMU logs show into intelundi.json)
  build-charon-netdrv-intelundi.py licence-file DL_DIR OUT_DIR   (write OUT_DIR/LICENCE.IntelUndiPkg.txt again)
(qemu-test: IMAGE may be several names joined with +, loaded in that order in one run.)

Two pinned source trees (archive SHA-256 below), downloaded into DL_DIR/intelundi, unpacked
there and only COMPILED (none of their scripts is run; neither contains binaries):

  r24.3  https://github.com/tianocore/edk2-staging branch Intel_UNDI, commit 97599f20
         ("Code aligned to Intel Ethernet driver release 24.3", 2019-12-19): GigUndiDxe 9.0.03,
         XGigUndiDxe 7.4.06, I40eUndiDxe 3.9.11. Every file is under the BSD licence.
  r29.3  https://github.com/bexcran/IntelUndiPkg commit 90dff4e6 (the same branch plus "Add
         unmodified sources from Intel UNDI UEFI release 29.3", 2024-10-25, a third party's
         copy of Intel's source package): GigUndiDxe 9.8.62, XGigUndiDxe 8.2.47, I40eUndiDxe
         5.0.12, IceUndiDxe 4.2.97; licence file IntelUndiPkg/license_bsd.txt (BSD-3-Clause).
         BUT in this release 36 files of GigUndiDxe and XGigUndiDxe (their Hii/ and Forms/
         folders, compiled into those two drivers) carry an "INTEL CONFIDENTIAL" header that
         forbids copying and distribution. I40eUndiDxe and IceUndiDxe have no such file.

Therefore: GigUndiDxe and XGigUndiDxe are built from r24.3, I40eUndiDxe and IceUndiDxe from
r29.3, and before anything is compiled EVERY file of the module folder and of LibC is checked
for a BSD licence header; a module with any other header is refused (licence_check).

Built with MSVC in an existing edk2 workspace (work/ha), found through PACKAGES_PATH, the same
way tools/make-charon-driverpack.py builds the ASIX drivers: one generated platform description
per driver (the upstream one with another name and output folder, one component, and
MdePkg/MdeLibs.dsc.inc for the library classes a current edk2 needs), X64 RELEASE. No source
file is changed. The workspace is shared: the build takes the lock file .charon-build.lock,
touches nothing in Conf/ and removes its own Build/CharonIntelUndi* folders afterwards.

Result in OUT_DIR:
  nic-19-intel-undi-gig.efi    GigUndiDxe   1G server adapters (82571..82583, 82575/6, 82580, I350, I210, I211, I354)
  nic-19-intel-undi-xgig.efi   XGigUndiDxe  10G (82598, 82599, X520, X540, X550, X553)
  nic-19-intel-undi-i40e.efi   I40eUndiDxe  X710, XL710, XXV710, X722
  nic-19-intel-undi-ice.efi    IceUndiDxe   E810, E830
  intelundi.json               provenance, hashes, licence, PCI IDs from each driver's own table
  source-changes/              the generated .dsc files and build commands (the only additions)
  LICENCE.IntelUndiPkg.txt     the BSD licence text of the package
These are UNDI drivers (Network Interface Identifier protocol 3.1): the firmware's or the
pack's SnpDxe has to be above them. See src/efi/NETWORK-DRIVERS.txt.
"""
import hashlib,json,os,re,shutil,struct,subprocess,sys,tarfile,time,urllib.request
from pathlib import Path

TREES={
 'r24.3':dict(commit='97599f20fcbf63f653377f0b4dd329c65ad1ac4e',url='https://github.com/tianocore/edk2-staging/archive/97599f20fcbf63f653377f0b4dd329c65ad1ac4e.tar.gz',
    sha256='6799fc3be73b597263b373b68e220644adb9c72377ef3582c9bfc7d6e3f6fa69',archive='edk2-staging-Intel_UNDI-97599f20fcbf63f653377f0b4dd329c65ad1ac4e.tar.gz',
    folder='src243',root='edk2-staging-97599f20fcbf63f653377f0b4dd329c65ad1ac4e',browse='https://github.com/tianocore/edk2-staging/tree/97599f20fcbf63f653377f0b4dd329c65ad1ac4e/IntelUndiPkg',
    release='24.3',origin='tianocore/edk2-staging, branch Intel_UNDI (Intel\'s own publication)'),
 'r29.3':dict(commit='90dff4e613d6da6f35d593844903c34f7abe3da8',url='https://github.com/bexcran/IntelUndiPkg/archive/90dff4e613d6da6f35d593844903c34f7abe3da8.tar.gz',
    sha256='de5ec99c2e918a28cb7112aca36279a0412609eb7dc6d76d3819b58e6e457171',archive='IntelUndiPkg-90dff4e613d6da6f35d593844903c34f7abe3da8.tar.gz',
    folder='src',root='IntelUndiPkg-90dff4e613d6da6f35d593844903c34f7abe3da8',browse='https://github.com/bexcran/IntelUndiPkg/tree/90dff4e613d6da6f35d593844903c34f7abe3da8/IntelUndiPkg',
    release='29.3',origin='bexcran/IntelUndiPkg (a third party\'s copy of Intel\'s release 29.3 source package on top of the edk2-staging branch; one later change to ice_switch.c)'),
}
# key, source tree, module folder, upstream platform description, file in the pack, description
DRIVERS=[
 ('gig','r24.3','GigUndiDxe','IntelOpenSourceUndiPkg.dsc','nic-19-intel-undi-gig.efi','Intel 1G server adapters, Intel\'s own UNDI driver (GigUndiDxe)'),
 ('xgig','r24.3','XGigUndiDxe','IntelOpenSourceUndiPkg.dsc','nic-19-intel-undi-xgig.efi','Intel 10G adapters, Intel\'s own UNDI driver (XGigUndiDxe)'),
 ('i40e','r29.3','I40eUndiDxe','IntelI40eUndiPkg.dsc','nic-19-intel-undi-i40e.efi','Intel X710/XL710/XXV710/X722, Intel\'s own UNDI driver (I40eUndiDxe)'),
 ('ice','r29.3','IceUndiDxe','IntelIceUndiPkg.dsc','nic-19-intel-undi-ice.efi','Intel E810/E830, Intel\'s own UNDI driver (IceUndiDxe)'),
]
# Compiler warnings switched off for one module (edk2 builds with /WX), instead of changing a source file.
#  xgig /wd4005: ixgbe_osdep.h defines ASSERT(x) as empty and the current edk2 DebugLib.h, included later, defines it
#  again (in a RELEASE build as "do { if (FALSE) { (VOID)(Expression); } } while (FALSE)"): macro redefinition. Both
#  forms generate no code.
EXTRA_CC_FLAGS={'xgig':'/wd4005'}
BUILD_CMD=r'''@echo off
setlocal
rem Git Bash sets this; cmd would then not find edksetup.bat in the current directory.
set NoDefaultCurrentDirectoryInExePath=
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PYTHON_COMMAND=python3.15"
set "HOST_ARCH=X64"
set "VS2022_PREFIX=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\"
set "NASM_PREFIX=@NASM@\"
set "PATH=%NASM_PREFIX%;%PATH%"
set "WORKSPACE=@WORKSPACE@"
set "PACKAGES_PATH=@PACKAGES@"
set "EDK_TOOLS_PATH=%WORKSPACE%\BaseTools"
set "BASE_TOOLS_PATH=%EDK_TOOLS_PATH%"
set "EDK_TOOLS_BIN=%EDK_TOOLS_PATH%\Bin\Win64"
set "PATH=%EDK_TOOLS_BIN%;%PATH%"
cd /d "%WORKSPACE%"
call edksetup.bat
if errorlevel 1 exit /b 1
call build -a X64 -b RELEASE -t VS2022 -p IntelUndiPkg/@DSC@ -n 8
exit /b %errorlevel%
'''

def sha256(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for block in iter(lambda:f.read(1<<20),b''):h.update(block)
    return h.hexdigest()

class WorkspaceLock:
    """work/ha is shared with other builds: one build at a time (file .charon-build.lock in the workspace)."""
    def __init__(self,workspace,purpose,wait=2400):self.path=workspace/'.charon-build.lock';self.purpose=purpose;self.wait=wait
    def __enter__(self):
        end=time.time()+self.wait
        while True:
            try:
                with open(self.path,'x',encoding='utf-8') as f:f.write(f'{self.purpose} pid={os.getpid()} {time.strftime("%Y-%m-%d %H:%M:%S")}\n')
                return self
            except FileExistsError:
                if time.time()>end:raise SystemExit(f'{self.path} still exists: {self.path.read_text(errors="replace").strip()}')
                time.sleep(5)
    def __exit__(self,*_):
        try:self.path.unlink()
        except FileNotFoundError:pass

def fetch(area,tree):
    """Downloads (if absent), checks and unpacks one source tree; returns the folder that holds IntelUndiPkg/."""
    t=TREES[tree];archive=area/t['archive']
    if not archive.exists():
        area.mkdir(parents=True,exist_ok=True)
        with urllib.request.urlopen(urllib.request.Request(t['url'],headers={'User-Agent':'netpack-fetch'})) as r:archive.write_bytes(r.read())
    got=sha256(archive)
    if got!=t['sha256']:raise SystemExit(f'{archive.name}: SHA-256 {got}, expected {t["sha256"]}')
    src=area/t['folder']
    if not (src/t['root']/'IntelUndiPkg/LibC/LibC.inf').exists():
        src.mkdir(exist_ok=True)
        with tarfile.open(archive) as tar:tar.extractall(src,filter='data')
    return src/t['root']

# ---- licence header of every file -----------------------------------------------
def licence_of(path):
    raw=path.read_bytes()
    text=raw.decode('utf-16','replace') if raw[:2] in (b'\xff\xfe',b'\xfe\xff') else raw.decode('utf-8','replace')
    head=' '.join(text[:8000].split())
    if re.search(r'INTEL CONFIDENTIAL|trade secrets|No part of the Material may be',head):return 'INTEL CONFIDENTIAL header (no licence granted)'
    if 'Redistribution and use in source and binary forms' in head:return 'BSD-3-Clause text' if 'Neither the name' in head else 'BSD-2-Clause text'
    if re.search(r'terms and conditions of the BSD License',head):return 'BSD licence by reference (edk2 style)'
    if re.search(r'GNU General Public|GPL',head):return 'GPL mention'
    return 'no licence header'
def licence_check(package,module):
    """Refuses a module unless every file in its folder and in LibC has a BSD header (build metadata without a
    header - .inf, .txt - is covered by the package's licence file). Returns {licence: count}."""
    counts={};bad=[]
    for folder in (package/module,package/'LibC'):
        for path in sorted(folder.rglob('*')):
            if not path.is_file():continue
            kind=licence_of(path);counts[kind]=counts.get(kind,0)+1
            if not kind.startswith('BSD') and not (kind=='no licence header' and path.suffix.lower() in ('.inf','.txt')):bad.append(f'{path.relative_to(package).as_posix()}: {kind}')
    if bad:raise SystemExit(f'{module}: files without a BSD licence header, not built:\n  '+'\n  '.join(bad[:40]))
    return counts
def licences(dl):
    area=dl/'intelundi'
    for tree in TREES:
        package=fetch(area,tree)/'IntelUndiPkg';print(f'== {tree}: {TREES[tree]["origin"]}')
        for module in sorted(p.name for p in package.iterdir() if p.is_dir()):
            counts={}
            for path in sorted((package/module).rglob('*')):
                if path.is_file():kind=licence_of(path);counts[kind]=counts.get(kind,0)+1
            print(f'   {module:14}',', '.join(f'{k}: {v}' for k,v in sorted(counts.items())))

def make_dsc(package,key,module,upstream):
    """The upstream platform description with our name/output folder, this one component and MdeLibs."""
    text=(package/upstream).read_text(encoding='utf-8',errors='replace')
    name=f'CharonIntelUndi{key.capitalize()}'
    text,n1=re.subn(r'(?m)^(\s*PLATFORM_NAME\s*=\s*).*$',lambda m:m.group(1)+name,text)
    text,n2=re.subn(r'(?m)^(\s*OUTPUT_DIRECTORY\s*=\s*).*$',lambda m:m.group(1)+'Build/'+name,text)
    text,n3=re.subn(r'(?m)^\[SkuIds\]',"!include MdePkg/MdeLibs.dsc.inc\n\n[SkuIds]",text,count=1)
    assert n1==1 and n2==1 and n3==1,(n1,n2,n3)
    # MdeLibs.dsc.inc already maps these two; a second mapping in the same section is an error in current BaseTools.
    text=re.sub(r'(?m)^\s*(RegisterFilterLib|SynchronizationLib)\|.*\n','',text)
    # One component. The release 24.3 description builds three drivers and sets PCDs of the I40e package for all.
    head,sep,_=text.partition('[Components]');assert sep
    text=head+f'[Components]\n  IntelUndiPkg/{module}/{module}.inf'
    if key in EXTRA_CC_FLAGS:text+=' {\n    <BuildOptions>\n      MSFT:*_*_*_CC_FLAGS = '+EXTRA_CC_FLAGS[key]+'\n  }'
    text+='\n'
    inf=(package/module/f'{module}.inf').read_text(errors='replace')
    if 'IntelUndiPkg/Intel' not in inf.split('[Packages]')[1].split('[')[0]:
        text=re.sub(r'(?m)^\s*gIntelUndiPkgTokenSpaceGuid\..*\n','',text)
    target=package/f'{name}.dsc';target.write_text(text,encoding='utf-8',newline='\n')
    return name

def defines_of(inf_text,arch='X64'):
    """-D names the INF passes to the compiler for this architecture (for the #ifdef blocks of Brand.c)."""
    names=set();section=None
    for line in inf_text.splitlines():
        line=line.split('#')[0].strip()
        if line.startswith('['):section=line.strip('[]').lower();continue
        if not section or not section.startswith('buildoptions'):continue
        if section not in ('buildoptions','buildoptions.common','buildoptions.'+arch.lower()):continue
        if 'CC_FLAGS' not in line or line.startswith('GCC:'):continue
        names.update(re.findall(r'-D\s*([A-Za-z_][A-Za-z0-9_]*)',line))
    return names

def brand_ids(brand_text,defines):
    """Device IDs of mBrandingTable (what IsDeviceIdSupported accepts), honouring #ifdef/#ifndef/#else/#endif."""
    start=brand_text.index('mBrandingTable[]');stack=[];names={};order=[]
    for line in brand_text[start:].splitlines():
        s=line.strip()
        m=re.match(r'#\s*(ifdef|ifndef|if|else|endif|elif)\b\s*(.*)',s)
        if m:
            kind,rest=m.group(1),m.group(2).split('/*')[0].strip()
            if kind=='ifdef':stack.append(rest in defines)
            elif kind=='ifndef':stack.append(rest not in defines)
            elif kind=='if':
                d=re.fullmatch(r'(!?)\s*defined\s*\(?\s*(\w+)\s*\)?',rest)
                stack.append(((d.group(2) in defines)!=bool(d.group(1))) if d else True)
            elif kind in ('else','elif') and stack:stack[-1]=not stack[-1]
            elif kind=='endif' and stack:stack.pop()
            continue
        if s.startswith('};'):break
        if not all(stack):continue
        m=re.match(r'\{\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*L"([^"]*)"',s)
        if not m:continue
        vendor,subvendor,device,subsystem=(int(m.group(i),16) for i in range(1,5));text=' '.join(m.group(5).split())
        if vendor!=0x8086 or device in (0,0xffff):continue
        if device not in names:names[device]=(subvendor!=0,text);order.append(device)
        elif names[device][0] and subvendor==0:names[device]=(False,text)   # prefer the generic (wildcard) name
    return [f'pci:8086:{d:04x} {names[d][1]}' for d in order]

def pe_info(data):
    if data[:2]!=b'MZ' or len(data)<0x100:return None
    at=struct.unpack_from('<I',data,0x3c)[0]
    if at+0x78>len(data) or data[at:at+4]!=b'PE\0\0':return None
    machine,sections=struct.unpack_from('<HH',data,at+4);magic=struct.unpack_from('<H',data,at+24)[0]
    reloc_rva,reloc_size=struct.unpack_from('<II',data,at+24+112+5*8)
    return dict(machine=machine,sections=sections,magic=magic,subsystem=struct.unpack_from('<H',data,at+24+68)[0],
        entry=struct.unpack_from('<I',data,at+24+16)[0],relocations=reloc_size)

def version_of(module_dir):
    """('9.0.03', Driver Binding Version). The modules compute the latter with their macro VERSION_TO_HEX =
    (MAJOR << 24) + (MINOR << 16) + (BUILD / 10 << 12) + (BUILD % 10 << 8), IceUndiDxe adds 1 (Ice.h)."""
    text=(module_dir/'Version.h').read_text(errors='replace')
    major,minor,build=(int(re.search(rf'#define\s+{n}\s+(\d+)',text).group(1)) for n in ('MAJORVERSION','MINORVERSION','BUILDNUMBER'))
    macro=''
    for path in sorted(module_dir.glob('*.h'))+sorted(module_dir.glob('*.c')):
        found=re.search(r'#define\s+VERSION_TO_HEX\s+((?:.*\\\n)*.*)',path.read_text(errors='replace'))
        if found:macro=' '.join(found.group(1).replace('\\','').split());break
    expected='((MAJORVERSION << 24) + (MINORVERSION << 16) + (BUILDNUMBER / 10 << 12) + (BUILDNUMBER % 10 << 8)'
    if not macro.startswith(expected) or macro[len(expected):] not in (')',' + 1)'):raise SystemExit(f'{module_dir.name}: unexpected VERSION_TO_HEX: {macro}')
    return f'{major}.{minor}.{build:02d}',(major<<24)+(minor<<16)+(build//10<<12)+(build%10<<8)+(1 if macro.endswith('+ 1)') else 0)

def main(dl,workspace,nasm,out,only=None):
    area=dl/'intelundi';out.mkdir(parents=True,exist_ok=True);changes=out/'source-changes';changes.mkdir(exist_ok=True)
    entries=[];failed=[];licence_text=None
    with WorkspaceLock(workspace,'build-charon-netdrv-intelundi.py (Build/CharonIntelUndi*)'):
        for key,tree,module,upstream,target,description in DRIVERS:
            if only and key not in only:continue
            t=TREES[tree];root=fetch(area,tree);package=root/'IntelUndiPkg'
            headers=licence_check(package,module)
            name=make_dsc(package,key,module,upstream);built=workspace/'Build'/name
            script=BUILD_CMD.replace('@NASM@',str(nasm.resolve())).replace('@WORKSPACE@',str(workspace.resolve())).replace('@PACKAGES@',str(root.resolve())).replace('@DSC@',name+'.dsc')
            (area/f'build-{key}.cmd').write_text(script,newline='\r\n')
            try:
                with (area/f'build-{key}.log').open('wb') as log:
                    rc=subprocess.run(['cmd','/c',str((area/f'build-{key}.cmd').resolve())],stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL).returncode
                image=built/f'RELEASE_VS2022/X64/{module}.efi'
                if rc or not image.exists():
                    failed.append(key);print(f'{module}: BUILD FAILED, see {area/f"build-{key}.log"}');continue
                data=image.read_bytes();(out/target).write_bytes(data)
                info=pe_info(data)
                if not info or info['magic']!=0x20b or info['machine']!=0x8664 or info['subsystem'] not in (11,12) or not info['relocations']:
                    raise SystemExit(f'{target}: not a relocatable PE32+ x86-64 EFI driver image: {info}')
                shutil.copyfile(package/f'{name}.dsc',changes/f'{name}.dsc');shutil.copyfile(area/f'build-{key}.cmd',changes/f'build-{key}.cmd')
                inf=(package/module/f'{module}.inf').read_text(errors='replace')
                ids=brand_ids((package/module/'Brand.c').read_text(errors='replace'),defines_of(inf))
                version,binding=version_of(package/module)
                entries.append(dict(file=target,name=f'Intel {module}',description=description,version=f'{version} (Intel network driver release {t["release"]})',
                    driver_binding_version=f'0x{binding:08x}',source=f'{t["browse"]}/{module}',source_origin=t['origin'],source_archive=t['url'],source_archive_sha256=t['sha256'],commit=t['commit'],
                    sha256=hashlib.sha256(data).hexdigest(),size=len(data),licence='BSD-3-Clause (Intel Corporation); edk2-style BSD reference in the package files',
                    licence_headers_of_compiled_folder=headers,
                    kind='UNDI driver (EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL 3.1, GUID 1ACED566-76ED-4218-BC81-767F1F977A89); needs SnpDxe above it',
                    pe=dict(machine=hex(info['machine']),subsystem=info['subsystem']),devices=ids,state='built from unmodified source; see NETWORK-DRIVERS.txt for what was run'))
                print(f'{target:32} {module:12} {tree} {version:8} {len(data):7} {hashlib.sha256(data).hexdigest()}  {len(ids)} device IDs')
            finally:
                shutil.rmtree(built,ignore_errors=True)
    write_licence(area,out)
    known={e['file']:e for e in json.loads((out/'intelundi.json').read_text(encoding='utf-8'))} if (out/'intelundi.json').exists() else {}
    for e in entries:
        if e['file'] in known and known[e['file']].get('sha256')==e['sha256'] and 'qemu' in known[e['file']]:e['qemu']=known[e['file']]['qemu'];e['state']=known[e['file']]['state']
    known.update({e['file']:e for e in entries})
    (out/'intelundi.json').write_text(json.dumps([known[k] for k in sorted(known)],indent=1),encoding='utf-8',newline='\n')
    if failed:raise SystemExit('failed: '+' '.join(failed))

def write_licence(area,out):
    # The BSD licence asks for the copyright notice, the conditions and the disclaimer with binary copies: the package's
    # licence file and, because the years differ, the notice exactly as it stands in the compiled sources of each release.
    parts=['Licence of the Intel UNDI driver images nic-19-intel-undi-*.efi (IntelUndiPkg), as shipped with the sources.\n',
        '==== IntelUndiPkg/license_bsd.txt (release 29.3 tree) ====\n'+(fetch(area,'r29.3')/'IntelUndiPkg/license_bsd.txt').read_text(errors='replace')]
    for tree,module in (('r24.3','GigUndiDxe'),('r24.3','XGigUndiDxe'),('r29.3','I40eUndiDxe'),('r29.3','IceUndiDxe')):
        text=(fetch(area,tree)/'IntelUndiPkg'/module/'Version.h').read_text(errors='replace')
        parts.append(f'==== notice in the source files of {module}, release {TREES[tree]["release"]} (from Version.h) ====\n'+text[:text.index('*/')+2])
    (out/'LICENCE.IntelUndiPkg.txt').write_text('\n\n'.join(part.rstrip() for part in parts)+'\n',encoding='utf-8',newline='\n')

# ---- load test in QEMU (accel=tcg) ---------------------------------------------
NSH=r"""echo CHARON-UNDI-TEST-BEGIN @DEVICE@
fs0:
@LOADS@connect -r
echo CHARON-DRIVERS
drivers
echo CHARON-SNP-HANDLES
dh -p SimpleNetwork
echo CHARON-NII-HANDLES
dh -p NetworkInterfaceIdentifier31
echo CHARON-IFCONFIG-BEFORE
ifconfig -l
ifconfig -s eth0 dhcp
stall 12000000
echo CHARON-IFCONFIG-AFTER-12S
ifconfig -l eth0
stall 20000000
echo CHARON-IFCONFIG-AFTER-32S
ifconfig -l eth0
stall 30000000
echo CHARON-IFCONFIG-AFTER-62S
ifconfig -l eth0
ping -n 2 10.0.2.2
echo CHARON-UNDI-TEST-END
reset -s
"""
def pcap_summary(path):
    """Counts the frames QEMU recorded on the network back-end: from the guest / to the guest, DHCP by UDP port."""
    if not path.exists():return 'no capture'
    data=path.read_bytes();at=24;total=0;kinds={}
    while at+16<=len(data):
        caplen=struct.unpack_from('<I',data,at+8)[0];frame=data[at+16:at+16+caplen];at+=16+caplen;total+=1
        if len(frame)<14:continue
        ethertype=struct.unpack_from('>H',frame,12)[0];kind=f'ethertype {ethertype:04x}'
        if ethertype==0x0806:kind='ARP'
        elif ethertype==0x86dd:kind='IPv6'
        elif ethertype==0x0800 and len(frame)>=42:
            proto=frame[23];ihl=(frame[14]&15)*4;kind=f'IPv4 proto {proto}'
            if proto==17:
                sport,dport=struct.unpack_from('>HH',frame,14+ihl)
                kind='DHCP from the guest' if (sport,dport)==(68,67) else 'DHCP to the guest' if (sport,dport)==(67,68) else 'UDP'
            elif proto==1:kind='ICMP'
        kinds[kind]=kinds.get(kind,0)+1
    return f'{total} frames: '+', '.join(f'{k} {v}' for k,v in sorted(kinds.items())) if total else '0 frames'

def qemu_test(out,qemu,image,devices):
    """One run per QEMU network card model: OVMF, UEFI shell, the card's option ROM switched off (romfile=), the image
    loaded from a FAT folder, connect -r, DHCP from QEMU's user network. -cpu max: without RDRAND this OVMF has no
    EFI_RNG_PROTOCOL and then does not start its network stack at all (no SnpDxe, MnpDxe, Ip4Dxe in "drivers").
    Serial log -> OUT_DIR/qemu-<image>-<device>.serial.txt"""
    share=qemu.parent/'share'
    for device in devices:
        area=out/f'qemu-scratch-{device}'
        if area.exists():shutil.rmtree(area)
        fat=area/'fat';fat.mkdir(parents=True)
        # An item is a file of OUT_DIR or a path to another driver image (for example the pack's net-*.efi).
        sources=[out/one if (out/one).exists() else Path(one) for one in image.split('+')];images=[path.name for path in sources];loads=''
        for path in sources:
            # "load" connects all controllers after every image; CHARON_QEMU_LOAD_NC=1 uses "load -nc", so that all
            # images are started first and "connect -r" then chooses among them - the order of events in Charon's loader.
            one=path.name;shutil.copyfile(path,fat/one);loads+=('load -nc fs0:\\' if os.environ.get('CHARON_QEMU_LOAD_NC') else 'load fs0:\\')+one+'\necho CHARON-LOAD-STATUS '+one+' %lasterror%\n'
        shutil.copyfile(share/'edk2-i386-vars.fd',area/'vars.fd')
        (fat/'startup.nsh').write_text(NSH.replace('@LOADS@',loads).replace('@DEVICE@',device),newline='\r\n')
        tag=os.environ.get('CHARON_QEMU_TAG') or (Path(images[0]).stem if len(images)==1 else 'all-intel-undi')
        cpu=os.environ.get('CHARON_QEMU_CPU','max')   # "qemu64" has no RDRAND: this OVMF then starts no network stack
        log=out/f'qemu-{tag}-{device}.serial.txt'
        command=[str(qemu),'-name','charon-intelundi-test','-machine','q35','-accel','tcg','-cpu',cpu,'-m','512','-display','none','-net','none',
            '-drive',f'if=pflash,format=raw,readonly=on,file={(share/"edk2-x86_64-code.fd").as_posix()}','-drive','if=pflash,format=raw,file=vars.fd',
            '-drive',f'format=raw,file=fat:rw:{fat.as_posix()}','-netdev','user,id=n0','-device',f'{device},netdev=n0,romfile=','-object','filter-dump,id=f0,netdev=n0,file=packets.pcap',
            '-serial','file:serial.txt','-no-reboot']
        # OVMF writes its DEBUG/ASSERT text to I/O port 0x402; CHARON_QEMU_DEBUGCON=1 keeps the last lines in the log.
        if os.environ.get('CHARON_QEMU_DEBUGCON'):command+=['-debugcon','file:debug.txt','-global','isa-debugcon.iobase=0x402']
        child=subprocess.Popen(command,cwd=area,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=(area/'stderr.log').open('wb'))
        stopped=''
        try:child.wait(int(os.environ.get('CHARON_QEMU_TIMEOUT','420')))
        except subprocess.TimeoutExpired:child.kill();child.wait();stopped='# the machine did not finish and was stopped after '+os.environ.get('CHARON_QEMU_TIMEOUT','420')+' s\n'
        if (area/'debug.txt').exists() and os.environ.get('CHARON_QEMU_DEBUGCON')=='keep':shutil.copyfile(area/'debug.txt',out/f'qemu-{tag}-{device}.debug.txt')
        if (area/'debug.txt').exists():
            lines=(area/'debug.txt').read_bytes().decode('utf-8','replace').replace('\r','').splitlines()
            stopped+='# last lines of the firmware debug output (port 0x402):\n'+''.join('#   '+line+'\n' for line in lines[-12:])
        raw=(area/'serial.txt').read_bytes() if (area/'serial.txt').exists() else b''
        text=re.sub(rb'\x1b\[[0-9;?=]*[A-Za-z]',b'',raw).replace(b'\r',b'').decode('utf-8','replace')
        head='# '+' '.join(command)+'\n# image sha256 '+' '.join(sha256(path) for path in sources)+'\n# packets on the card (QEMU filter-dump): '+pcap_summary(area/'packets.pcap')+'\n'+stopped
        log.write_text(head+text,encoding='utf-8',newline='\n')
        print(device,'END' if text.count('CHARON-UNDI-TEST-END')>1 else 'NO END MARK',len(text),'chars ->',log.name)
        shutil.rmtree(area,ignore_errors=True)

def record_tests(out):
    """Reads OUT_DIR/qemu-<image>-<device>.serial.txt and writes what each run showed into intelundi.json ("qemu")."""
    path=out/'intelundi.json';entries=json.loads(path.read_text(encoding='utf-8'))
    for e in entries:
        stem=Path(e['file']).stem;runs={}
        for log in sorted(out.glob(f'qemu-{stem}-*.serial.txt')):
            text=log.read_text(encoding='utf-8',errors='replace');device=log.name[len(f'qemu-{stem}-'):-len('.serial.txt')]
            image_hash=re.search(r'# image sha256 ([0-9a-f]{64})',text);packets=re.search(r'# packets on the card \(QEMU filter-dump\): (.*)',text)
            row=re.search(r'(?m)^[0-9A-F]+ +[0-9A-F]{8} +(\S) +\S +\S +(\S+) +(\S+) .*'+re.escape(e['file']),text)
            addresses=re.findall(r'ipv4 address : (\S+)',text)
            runs[device]=dict(image_sha256=image_hash.group(1) if image_hash else None,same_image=bool(image_hash and image_hash.group(1)==e['sha256']),
                loaded='loaded at' in text and bool(re.search(r'CHARON-LOAD-STATUS (\S+ )?0x0',text)),
                devices_managed=(0 if row.group(2)=='-' else int(row.group(2))) if row else None,
                simple_network_handle=bool(re.search(r'(?m)^[0-9A-F]+: .*NetworkInterfaceIdentifier31',text.split('CHARON-NII-HANDLES')[-1])) and 'eth0' in text,
                dhcp_address=next((a for a in reversed(addresses) if a!='0.0.0.0'),None),ping_answered=text.count('bytes from 10.0.2.2'),
                packets=packets.group(1) if packets else None,log=log.name,finished=text.count('CHARON-UNDI-TEST-END')>1)
        if runs:
            e['qemu']=runs
            parts=[]
            for device,r in runs.items():
                if not r['same_image']:parts.append(f'{device}: log is of another build');continue
                if not r['devices_managed']:parts.append(f'{device}: image loads, binds nothing');continue
                parts.append(f'{device}: adapter + DHCP {r["dhcp_address"]}' if r['dhcp_address'] else f'{device}: binds, NO DHCP lease in 62 s (NOT passed)')
            e['state']='built from unmodified source; QEMU 11.1 (tcg, -cpu max, OVMF): '+'; '.join(parts)+'. Never run on a physical computer.'
        elif 'QEMU' not in e.get('state',''):
            e['state']='built from unmodified source; not run (QEMU emulates no such card). Never run on a physical computer.'
    path.write_text(json.dumps(entries,indent=1),encoding='utf-8',newline='\n')
    for e in entries:print(e['file'],'-',e['state'])

if __name__=='__main__':
    if len(sys.argv)>4 and sys.argv[1]=='qemu-test':
        qemu_test(Path(sys.argv[2]).resolve(),Path(sys.argv[3]),sys.argv[4],sys.argv[5:]);record_tests(Path(sys.argv[2]).resolve());sys.exit(0)
    if len(sys.argv)==3 and sys.argv[1]=='record-tests':
        record_tests(Path(sys.argv[2]).resolve());sys.exit(0)
    if len(sys.argv)==4 and sys.argv[1]=='licence-file':
        write_licence(Path(sys.argv[2]).resolve()/'intelundi',Path(sys.argv[3]).resolve());sys.exit(0)
    if len(sys.argv)==3 and sys.argv[1]=='licences':
        licences(Path(sys.argv[2]).resolve());sys.exit(0)
    if len(sys.argv)<5:sys.exit(__doc__)
    main(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve(),Path(sys.argv[3]).resolve(),Path(sys.argv[4]).resolve(),set(sys.argv[5:]) or None)
