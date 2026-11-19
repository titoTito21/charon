"""r390: Charon's own menu - key list (F1), new screen reader settings rows,
hints after 10 s (message voice). Prints what Charon says per step.
  charon-r390-menu.py CHARON_EFI NAME [prepare flags...]"""
import subprocess,sys,time,re
from pathlib import Path
base=Path(__file__).resolve().parent
efi,name,*flags=sys.argv[1:]
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
def said():
    t=(base/name/'serial.log').read_bytes().decode('utf-8','replace')
    out=[]
    for m in re.finditer(r'(CHARON speak|CHARON message|menu announce)\r?\n(?:(?:pl|en)\r?\n)?([^\r\n]*)',t):
        if m.group(2).strip() and not m.group(2).startswith('CHARON'):out.append(m.group(1)[7:9].upper()+': '+m.group(2).replace('\x1b','<ESC>'))
    return out
vm('prepare',name,efi,*flags)
print(vm('wait',name,'menu announce','150').strip());time.sleep(4)
def step(label,keys,wait=3.0,gap='0.8'):
    mark=len(said())
    if keys:vm('keys',name,gap,*keys)
    time.sleep(wait)
    print('==',label)
    for x in said()[mark:]:print('   ',x[:400])
step('welcome dismissed',['spc'])
step('F1: key list',['f1'])
step('list: down down',['down','down'])
step('list: end',['end'])
step('list: Esc back to the menu',['esc'])
step('settings: open, End, Up (reader settings), Enter',['down','down','ret','end','up','ret'],4)
step('down to verbosity (7)',['down']*7,4,'0.5')
step('punctuation row, right x2',['down','right','right'],4)
step('language row, right',['down','right'],3)
step('capitals: say (space toggles), beep row',['down','spc','down'],4)
step('to hints row',['down']*6,4,'0.5')
step('idle 12 s: a hint on the message voice',[],12)
step('idle 11 s: the next hint',[],11)
vm("quit",name)
