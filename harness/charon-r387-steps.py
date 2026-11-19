"""r387: drive Charon step by step and print what it says after each step.
  charon-r387-steps.py CHARON_EFI NAME "keys1|keys2|..." [prepare flags...]
Each step's keys are sent 0.6 s apart, then the script waits STEP_WAIT
seconds (default 5; a step starting with 'wait:N ' waits N seconds).
Env: KEEP=1 leaves the machine running. Speech comes from the trace build's
serial log (CHARON speak lines)."""
import subprocess,sys,time,re,os
from pathlib import Path
base=Path(__file__).resolve().parent
efi,name,steps,*flags=sys.argv[1:]
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
def said():
    t=(base/name/'serial.log').read_bytes().decode('utf-8','replace')
    return [m.group(1) for m in re.finditer(r'CHARON speak\r?\n(?:(?:pl|en)\r?\n)?([^\r\n]*)',t) if m.group(1).strip() and not m.group(1).startswith('CHARON')]
vm('prepare',name,efi,*flags)
vm('wait',name,'menu announce','150');time.sleep(3)
vm('keys',name,'1.5','spc');time.sleep(4)
for step in steps.split('|'):
    step=step.strip();wait=float(os.environ.get('STEP_WAIT','5'))
    if step.startswith('wait:'):head,_,step=step.partition(' ');wait=float(head[5:])
    m=len(said());
    if step:vm('keys',name,'0.6',*step.split())
    time.sleep(wait)
    print('==',step);[print('  ',x[:400]) for x in said()[m:]]
    sys.stdout.flush()
vm('screen',name,'end.ppm')
if not os.environ.get('KEEP'):vm('quit',name)
