"""r396: drive Charon's own menu and print what is said per step.
  charon-r396-menu.py CHARON_EFI NAME "keys|keys|..." [prepare flags...]
Starts Charon, presses Space (options menu), then each step's keys
(0.5 s apart) and prints the speech that followed (R: reader/menu voice,
M: message voice). Env STEP_WAIT (default 3), KEEP=1 keeps the VM."""
import subprocess,sys,time,re,os
from pathlib import Path
base=Path(__file__).resolve().parent
efi,name,steps,*flags=sys.argv[1:]
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
def said():
    p=base/name/'serial.log'
    t=p.read_bytes().decode('utf-8','replace') if p.exists() else ''
    out=[]
    for m in re.finditer(r'(CHARON speak|CHARON message)\r?\n(?:(?:pl|en)\r?\n)?([^\r\n]*)',t):
        s=re.sub(r'\x1b\[[0-9;=]*[A-Za-z]','',m.group(2)).replace('\x1b','').strip()
        if s and not s.startswith('CHARON'):out.append(('M: ' if 'message' in m.group(1) else 'R: ')+s)
    return out
vm('prepare',name,efi,*flags)
vm('wait',name,'menu announce','150');time.sleep(2)
print('== start');[print('  ',x[:300]) for x in said()]
mark=len(said());vm('keys',name,'1','spc');time.sleep(3)
for step in steps.split('|'):
    keys=step.split()
    mark=len(said());vm('keys',name,'0.5',*keys);time.sleep(float(os.environ.get('STEP_WAIT','3')))
    print('==',step);[print('  ',x[:300]) for x in said()[mark:]]
if not os.environ.get('KEEP'):
    vm('quit',name);time.sleep(2)
    for p in (base/name).glob('*'):
        if p.suffix in ('.img','.fd','.qcow2','.wav'):
            try:p.unlink()
            except OSError:pass
