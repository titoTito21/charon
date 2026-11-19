"""r391: reader-only mode. Phase 1 sets Charon's Continue target to the
Windows Boot Manager disk through the menu; phase 2 restarts with
\\EFI\\HAK\\reader-only on Charon's disk (as the installer's shim mode
writes it) and records what is said.
  charon-r391-readeronly.py CHARON_EFI NAME"""
import subprocess,sys,time,re
from pathlib import Path
base=Path(__file__).resolve().parent
efi,name=sys.argv[1:3]
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout+subprocess.run(['true'],capture_output=True,text=True).stdout
def said():
    t=(base/name/'serial.log').read_bytes().decode('utf-8','replace');out=[]
    for m in re.finditer(r'(CHARON speak|CHARON message)\r?\n(?:(?:pl|en)\r?\n)?([^\r\n]*)',t):
        if m.group(2).strip() and not m.group(2).startswith('CHARON'):out.append(('M: ' if 'message' in m.group(1) else 'R: ')+m.group(2).replace('\x1b','|'))
    return out
def step(label,keys,wait=3.0,gap='0.8'):
    mark=len(said())
    if keys:vm('keys',name,gap,*keys)
    time.sleep(wait);print('==',label)
    for x in said()[mark:]:print('   ',x[:300])
    sys.stdout.flush()
vm('prepare',name,efi,'--winboot')
print(vm('wait',name,'menu announce','150').strip());time.sleep(4)
step('welcome',['spc'])
step('devices: last one (Windows)',['ret','end'],5)
step('back',['esc'],3)
step('settings',['down','down','ret'],4)
step('to Continue target (row 7)',['down']*6,5,'0.6')
step('enter',['ret'],4)
step('confirm (move to OK)',['down'],3)
step('enter',['ret'],4)
step('close',['ret'],3)
vm('quit',name);time.sleep(3)
mark=0
print(vm('restart',name,'EFI/HAK/reader-only='+str(base/'charon-r391-marker.txt')))
time.sleep(60)
print('== phase 2 (reader-only)')
t=(base/name/'serial.log').read_bytes().decode('utf-8','replace')
for x in said()[mark:]:print('   ',x[:300])
vm('screen',name,str(base/name/'phase2.ppm'))
vm('quit',name)
