"""r386 removable-media test.

  charon-r386-media.py CHARON_EFI LABEL [cd|usb]

Power-on: FreeDOS 1.4 LiteUSB as a USB stick (BIOS only) and, with 'cd',
Debian mini.iso in a SATA CD drive. Then Debian mini.iso is plugged in as a
USB stick while the device list is open (hot-plug), the FreeDOS entry is
opened (BIOS-only notice) and the Debian stick is started; the reader's
speech of Debian's GRUB is printed.
"""
import subprocess,sys,time,re,os
from pathlib import Path
base=Path(__file__).resolve().parent
efi,label,*mode=sys.argv[1:]
name='r386-media-'+label
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
def said():
    t=(base/name/'serial.log').read_bytes().decode('utf-8','replace')
    return [m.group(1) for m in re.finditer(r'CHARON speak\r?\n(?:(?:pl|en)\r?\n)?([^\r\n]*)',t)
            if m.group(1).strip() and not m.group(1).startswith('CHARON')]
def last():s=said();return s[-1] if s else ''
env=dict(os.environ,CHARON_USB=str(base/'freedos-img/FD14LITE.img'))
if 'cd' in mode:env['CHARON_CDROM']=str(base/'debian-dl/mini.iso')
subprocess.run([sys.executable,str(base/'charon-vm.py'),'prepare',name,efi,'--grub','grub-dl/grubx64.efi','--usb'],env=env,capture_output=True,text=True)
vm('wait',name,'spacj','150');time.sleep(1)
vm('keys',name,'1.5','spc','ret');time.sleep(4)
mark=len(said())
print('== device list');seen=[]
for _ in range(12):
    x=last()
    if x in seen:break
    seen.append(x);print('  ',x[:200]);vm('keys',name,'0.1','down');time.sleep(2.5)
# hot-plug a copy of Debian mini.iso as a stick
stick=base/name/'debian-stick.img';stick.write_bytes((base/'debian-dl/mini.iso').read_bytes())
vm('usbplug',name,str(stick),'stick9')
r=vm('wait',name,'Nowy no','40');print('== hot-plug:',r.strip())
time.sleep(4)
for x in said()[mark:]:
    if 'nośnik' in x:print('  ',x[:300])
# FreeDOS entry: BIOS-only notice
def goto(text,limit=14):
    for _ in range(limit):
        if text in last():return True
        vm('keys',name,'0.1','down');time.sleep(2.5)
    return False
if goto('FreeDOS'):
    vm('keys',name,'0.1','ret');time.sleep(5);print('== FreeDOS notice:',last()[:400]);vm('keys',name,'0.1','esc');time.sleep(3)
else:print('== FreeDOS entry not found')
vm('keys',name,'0.1','home');time.sleep(2)
if goto('znalazł Charon'):
    print('== start:',last()[:200]);mark=len(said())
    vm('keys',name,'0.1','ret');vm('wait',name,'Uruchamiam','20');time.sleep(40)
    for x in said()[mark:]:print('  ',x[:300])
else:print('== Debian stick entry not found')
vm('screen',name,'end.ppm')
if not os.environ.get('KEEP'):vm('quit',name)
