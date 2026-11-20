"""r385: Charon as a UEFI add-on only (no Charon boot manager), Windows default.

  charon-addon-test.py NAME VOICE.EFI

Disk: GPT with one EFI system partition holding the host's Windows Boot
Manager as the removable default (EFI/BOOT/BOOTX64.EFI) and \\EFI\\HAK with the
add-on files exactly as Install-CharonAddon.ps1 copies them. Firmware vars
start blank. Phase 1 (only a tiny FAT disk with startup.nsh): the UEFI Shell
writes Driver0000/DriverOrder with the bytes Install-CharonAddon.ps1 -DryRun
computes for that partition (the same short-form HD() device path the
installer writes under Windows) and powers off. Phase 2: only the Windows
disk; firmware loads the driver from Driver####, then boots bootmgr by
itself. Nothing in BootOrder is Charon.

"""
from pathlib import Path
import importlib.util,json,os,shutil,socket,subprocess,sys,time
base=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('cvm',base/'charon-vm.py');cvm=importlib.util.module_from_spec(spec)
argv=sys.argv;sys.argv=['charon-vm.py'];spec.loader.exec_module(cvm);sys.argv=argv
name,voice=argv[1],Path(argv[2]);secure=None
area=base/name;area.mkdir(exist_ok=False)
share=Path(cvm.QEMU).parent/'share'
installer=Path(os.environ.get('CHARON_SRC',base/'charon-src-r395'))/'tools/charon-addon/Install-CharonAddon.ps1'
with socket.socket() as probe:assert probe.connect_ex(('127.0.0.1',cvm.PORT))!=0,'Stop the previous Charon QEMU first'

files=cvm.win_files()
sign=False
if os.environ.get('CHARON_ADDON_BOOT'):files['EFI/BOOT/BOOTX64.EFI']=Path(os.environ['CHARON_ADDON_BOOT']).read_bytes()  # another default program (e.g. Memtest86+, which ends boot services)
files['EFI/HAK/CHARONVOICE.EFI']=voice.read_bytes()
# r393: synthesizer add-ons and a settings slot choosing one (CHARON_SYNTHS, CHARON_SETTINGS_V10=fmt,extra,manifest,voice).
if os.environ.get('CHARON_SYNTHS'):
    syn=Path(os.environ['CHARON_SYNTHS']).resolve()/'EFI/HAK/synths'
    for q in syn.rglob('*'):
        if q.is_file():files['EFI/HAK/synths/'+q.relative_to(syn).as_posix()]=q.read_bytes()
if os.environ.get('CHARON_SETTINGS_V10'):
    import struct,zlib
    v10=os.environ['CHARON_SETTINGS_V10'].split(',');fmt,extra=int(v10[0]),int(v10[1]);synth=0;sv=int(v10[3]) if len(v10)>3 else 0
    if len(v10)>2 and v10[2]:
        h=2166136261
        for ch in v10[2].lower():h=((h^ord(ch))*16777619)&0xffffffff
        synth=(h^(h>>8)^(h>>16)^(h>>24))&0xff or 1
    body=(b'CHARS00B' if synth else b'CHARS00A')+struct.pack('<Q',1)+bytes([0,0,1,0,1,0,10,10,10,1|(1<<2),7<<1,10,10,10,10,10,7,2,14,10,1,1,0,47,fmt,extra,synth,sv])
    files['EFI/HAK/charon-settings-a.bin']=body+struct.pack('<I',zlib.crc32(body))
# r388: other boot managers for the add-on sweep.
if os.environ.get('CHARON_ADDON_GRUB'):
    cfg=('set timeout=30\nset timeout_style=menu\nmenuentry "Charon test entry one" {\n  echo "Entry one selected"\n  sleep 3\n}\n'
         'menuentry "Charon test entry two" {\n  echo "Entry two selected"\n  sleep 3\n}\n'
         'menuentry "Windows Boot Manager" {\n  chainloader /EFI/Microsoft/Boot/bootmgfw.efi\n}\n').encode()
    files['EFI/BOOT/BOOTX64.EFI']=(base/'grub-dl/grubx64.efi').read_bytes()
    for n in ('EFI/BOOT/grub.cfg','boot/grub/grub.cfg','EFI/debian/grub.cfg'):files[n]=cfg
if os.environ.get('CHARON_ADDON_REFIND'):
    rd=base/'refind-dl/x/refind-bin-0.14.2/refind'
    files['EFI/BOOT/BOOTX64.EFI']=(rd/'refind_x64.efi').read_bytes()
    files['EFI/BOOT/refind.conf']='timeout 30\nscanfor internal,external,manual\ndont_scan_dirs EFI/BOOT,EFI/HAK\n'.encode()
    for q in (rd/'icons').glob('*.png'):files['EFI/BOOT/icons/'+q.name]=q.read_bytes()
    for q in (base/'refind-dl/x/refind-bin-0.14.2/fonts').glob('*.png'):files['EFI/BOOT/fonts/'+q.name]=q.read_bytes()
if os.environ.get('CHARON_ADDON_NOBOOT'):files.pop('EFI/BOOT/BOOTX64.EFI',None)   # firmware boots the next device
files['EFI/HAK/AudioDxe.efi']=Path(os.environ.get('CHARON_AUDIODXE',base/'charon-audio-driver-r389/AudioDxe-r389.efi')).read_bytes()   # r390: the r389 stream driver (r355's times out with the current stream)
for pcm in sorted((base/'charon-cues-48k-r361').glob('*.pcm')):files['EFI/HAK/audio/'+pcm.name]=pcm.read_bytes()
files['EFI/HAK/audio-language']=b'pl'
files['EFI/HAK/addon.txt']=b'Charon UEFI screen reader add-on r385 (test)\r\n'
cvm.gpt_disk(area/'win-disk.img',int(os.environ.get('CHARON_WINBOOT_MB','64')),files)  # r388: WinRE needs ~1300
sectors=((area/'win-disk.img').stat().st_size//512)-1-2048-2048   # gpt_disk: first 2048, last = first+sectors-1, total = last+1+2048
option=subprocess.run(['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',str(installer),'-DryRun','-PartitionNumber','1','-StartLba','2048',
    '-SizeLba',str(sectors+1),'-PartitionGuid','6f1c3a52-5d47-4c11-9a8e-0c4a5b1e7d01'],capture_output=True,text=True,check=True).stdout.strip()
assert option and all(c in '0123456789abcdef' for c in option),option
print('Driver0000',option)
g='8BE4DF61-93CA-11D2-AA0D-00E098032B8C'
nsh=[f'setvar Driver0000 -guid {g} -bs -rt -nv ={option}',f'setvar DriverOrder -guid {g} -bs -rt -nv =0000']
if os.environ.get('CHARON_ADDON_FWUI'):nsh.append(f'setvar OsIndications -guid {g} -bs -rt -nv =0100000000000000')   # r390: next start opens the firmware menu
nsh+=['setvar DriverOrder -guid '+g,'echo CHARON-ADDON-PHASE1-DONE','reset -s']
cvm.fat_disk(area/'shell-disk.img',16,{'startup.nsh':('\r\n'.join(nsh)+'\r\n').encode()})
code=share/'edk2-x86_64-code.fd'
shutil.copyfile(code,area/'firmware-code.fd');shutil.copyfile(share/'edk2-i386-vars.fd',area/'firmware-vars.fd')
QMPC={}
def run(disk,log,wait):
    command=[cvm.QEMU,'-name','Charon-'+name,'-machine','q35,accel='+{'whpx':'whpx,kernel-irqchip=off','tcg':'tcg'}[os.environ.get('CHARON_ACCEL','whpx')],'-cpu','max','-smp',os.environ.get('CHARON_SMP','4'),'-m',os.environ.get('CHARON_MEM','512'),'-nic','none','-display','sdl']
    command+=['-drive','if=pflash,format=raw,readonly=on,file=firmware-code.fd','-drive','if=pflash,format=raw,file=firmware-vars.fd',
        '-drive',f'if=ide,format=raw,file={disk},index=0','-audiodev','dsound,id=charon_audio','-device','ich9-intel-hda','-device','hda-duplex,audiodev=charon_audio',
        '-qmp',f'tcp:127.0.0.1:{cvm.PORT},server=on,wait=off','-serial',f'file:{log}',*(['-smbios',os.environ['CHARON_SMBIOS']] if os.environ.get('CHARON_SMBIOS') else []),*(['-smbios',os.environ['CHARON_SMBIOS0']] if os.environ.get('CHARON_SMBIOS0') else []),'-debugcon','file:'+log.replace('.log','-ovmf.log'),'-global','isa-debugcon.iobase=0x402']
    if log=='phase2.log':
        # r388: removable media for the sweep (USB sticks, a SATA CD).
        if os.environ.get('CHARON_USB'):command+=['-device','qemu-xhci,id=xhci']
        for i,img in enumerate(x for x in os.environ.get('CHARON_USB','').split(',') if x):
            command+=['-drive',f'if=none,id=stick{i},format=raw,file={Path(img).resolve()}','-device',f'usb-storage,bus=xhci.0,drive=stick{i},removable=on'+(',bootindex=0' if i==0 else '')]
        if os.environ.get('CHARON_CDROM'):
            command+=['-drive',f'if=none,id=cd0,media=cdrom,readonly=on,file={Path(os.environ["CHARON_CDROM"]).resolve()}','-device','ide-cd,bus=ide.4,drive=cd0,bootindex=0']   # firmware starts the medium first
    child=subprocess.Popen(command,cwd=area,stdin=subprocess.DEVNULL,stdout=(area/(log+'.out')).open('xb'),stderr=(area/(log+'.err')).open('xb'),creationflags=subprocess.DETACHED_PROCESS)
    (area/(log+'.json')).write_text(json.dumps(dict(pid=child.pid,command=command),indent=2))
    for _ in range(40):
        try:QMPC['q']=cvm.qmp();QMPC['q']('human-monitor-command',{'command-line':f'wavcapture {area/(log+".wav")} charon_audio'});break
        except OSError:time.sleep(.5)
    if log=='phase2.log' and os.environ.get('CHARON_ADDON_KEYS'):
        # r388: "AT:key key|AT:key" - keys sent at AT seconds after power-on (0.15 s apart).
        started=time.time();shared=QMPC
        for part in os.environ['CHARON_ADDON_KEYS'].split('|'):
            at,_,keys=part.partition(':')
            delay=float(at)-(time.time()-started)
            if delay>0:time.sleep(delay)
            try:
                q=shared.get("q") or cvm.qmp();shared["q"]=q   # r390: one QMP client
                for k in keys.split():
                    for down in (True,False):q('input-send-event',{'events':[{'type':'key','data':{'down':down,'key':{'type':'qcode','data':k}}}]});time.sleep(.05)
                    time.sleep(.15)
            except OSError as e:print("  keys not sent:",e);shared.pop("q",None)
        wait=max(1.0,wait-(time.time()-started))
    try:child.wait(timeout=wait)
    except subprocess.TimeoutExpired:
        (QMPC.get('q') or cvm.qmp())('screendump',{'filename':str(area/(log+'.ppm'))});time.sleep(1)
        try:(QMPC.get('q') or cvm.qmp())('quit')
        except OSError:pass
        child.wait(timeout=30)
        print('  stopped after',wait,'s')
print('phase 1: UEFI Shell writes the driver option');run('shell-disk.img','phase1.log',600)
print('phase 2: Windows disk only');run('win-disk.img','phase2.log',float(os.environ.get('CHARON_ADDON_WAIT','240')))
