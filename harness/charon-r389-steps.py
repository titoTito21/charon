"""r389: drive a program started from Charon's device list and print, per
step, what the reader says (R:) and what the boot-message voice says (M:).
  charon-r389-steps.py CHARON_EFI NAME POS "keys1|keys2|..." [prepare flags...]
Keys of a step are sent 0.4 s apart ('text:abc' types letters), then
STEP_WAIT seconds (default 4). Env KEEP=1 leaves the machine running,
CHARON_AUDIODXE as for charon-vm.py."""
import subprocess,sys,time,re,os
from pathlib import Path
base=Path(__file__).resolve().parent
efi,name,pos,steps,*flags=sys.argv[1:];pos=int(pos)
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
def said():
    t=(base/name/'serial.log').read_bytes().decode('utf-8','replace')
    out=[]
    for m in re.finditer(r'(CHARON speak|CHARON message)\r?\n(?:(?:pl|en)\r?\n)?([^\r\n]*)',t):
        if m.group(2).strip() and not m.group(2).startswith('CHARON'):out.append(('M: ' if 'message' in m.group(1) else 'R: ')+m.group(2))
    return out
vm('prepare',name,efi,*flags)
vm('wait',name,'menu announce','150');time.sleep(3)
vm('keys',name,'1.5','spc');time.sleep(3)
vm('keys',name,'1.2','ret',*(['down']*(pos-1)));time.sleep(3)
mark=len(said());vm('keys',name,'0.3','ret');time.sleep(float(os.environ.get('START_WAIT','8')))
print('== start');[print('  ',x[:300]) for x in said()[mark:]]
keymap={' ':'spc','-':'minus','/':'slash','.':'dot','=':'equal'}
for step in steps.split('|'):
    step=step.strip();wait=float(os.environ.get('STEP_WAIT','4'))
    if step.startswith('wait:'):head,_,step=step.partition(' ');wait=float(head[5:])
    keys=[]
    for k in step.split():
        if k.startswith('text:'):keys+=[keymap.get(c,c) for c in k[5:]]
        else:keys.append(k)
    m=len(said())
    if keys:vm('keys',name,'0.4',*keys)
    time.sleep(wait)
    print('==',step);[print('  ',x[:300]) for x in said()[m:]]
    sys.stdout.flush()
vm('screen',name,'end.ppm')
if not os.environ.get('KEEP'):vm('quit',name)
