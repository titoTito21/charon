"""make-vmware-vm.py NAME ISO DISK_GB VNC_PORT [SOUND] [MEM_MB]: a VMware Workstation test machine in
work/NAME (UEFI, e1000 NAT, NVMe disk of DISK_GB (0 = none), CD = ISO, serial port = TCP 127.0.0.1:VNC_PORT+10 (run serial-pipe.py NAME VNC_PORT+10: log in serial.txt, input from serial.in),
VNC on 127.0.0.1:VNC_PORT without a password for vnc-keys.py). SOUND: hdaudio (default) | es1371 | sb16.
Start:  vmrun -T ws start work/NAME/NAME.vmx gui      Stop: vmrun -T ws stop work/NAME/NAME.vmx hard
"""
import subprocess,sys
from pathlib import Path
base=Path(__file__).resolve().parent
name,iso,gb,port=sys.argv[1],Path(sys.argv[2]).resolve(),int(sys.argv[3]),int(sys.argv[4])
sound=sys.argv[5] if len(sys.argv)>5 else 'hdaudio';mem=sys.argv[6] if len(sys.argv)>6 else '4096'
d=base/name;d.mkdir(exist_ok=True)
vmx=d/(name+'.vmx');assert not vmx.exists(),vmx
lines={'.encoding':'windows-1250','config.version':'8','virtualHW.version':'21','guestOS':'other6xlinux-64','displayName':'charon-'+name,
 'firmware':'efi','numvcpus':'4','cpuid.coresPerSocket':'2','memsize':mem,'vmci0.present':'TRUE','hpet0.present':'TRUE',
 'pciBridge0.present':'TRUE','pciBridge4.present':'TRUE','pciBridge4.virtualDev':'pcieRootPort','pciBridge4.functions':'8',
 'pciBridge5.present':'TRUE','pciBridge5.virtualDev':'pcieRootPort','pciBridge5.functions':'8',
 'pciBridge6.present':'TRUE','pciBridge6.virtualDev':'pcieRootPort','pciBridge6.functions':'8',
 'pciBridge7.present':'TRUE','pciBridge7.virtualDev':'pcieRootPort','pciBridge7.functions':'8',
 'sound.present':'TRUE','sound.autoDetect':'TRUE','sound.fileName':'-1','sound.virtualDev':sound,
 'sata0.present':'TRUE','sata0:1.present':'TRUE','sata0:1.deviceType':'cdrom-image','sata0:1.fileName':str(iso),
 'usb.present':'TRUE','ehci.present':'TRUE','usb_xhci.present':'TRUE',
 'ethernet0.present':'TRUE','ethernet0.connectionType':'nat','ethernet0.addressType':'generated','ethernet0.virtualDev':'e1000',
 'floppy0.present':'FALSE','serial0.present':'TRUE','serial0.fileType':'network','serial0.fileName':'tcp://127.0.0.1:%d'%(port+10),'serial0.network.endPoint':'server','serial0.yieldOnMsrRead':'TRUE',
 'RemoteDisplay.vnc.enabled':'TRUE','RemoteDisplay.vnc.port':str(port),
 'nvram':name+'.nvram','uuid.action':'create','msg.autoAnswer':'TRUE','tools.syncTime':'FALSE','workingDir':'.'}
if gb:
    disk=d/'disk.vmdk'
    subprocess.run([r'C:\Program Files (x86)\VMware\VMware Workstation\vmware-vdiskmanager.exe','-c','-s',str(gb)+'GB','-a','nvme','-t','0',str(disk)],check=True,capture_output=True)
    lines.update({'nvme0.present':'TRUE','nvme0:0.present':'TRUE','nvme0:0.fileName':'disk.vmdk'})
vmx.write_text(''.join('%s = "%s"\n'%kv for kv in lines.items()),encoding='cp1250')
print(vmx)
