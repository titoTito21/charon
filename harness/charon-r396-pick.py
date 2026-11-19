"""r396: start a boot manager from Charon's device list by the name the reader
says (not by position), then press KEYS and print what is spoken.

  charon-r396-pick.py CHARON_EFI LABEL PICK [prepare flags...]
PICK: text the reader says for the wanted list entry (e.g. "systemd-boot"),
or "list" to only read the whole device list.
Env: WAIT (s after Enter, default 40), KEYS ("down down up", 4 s apart),
BURST (keys sent right after Enter), KEEP=1 keeps the VM and its disks.
Prints the reader (R:) and boot-message (M:) speech, how many boot messages
interrupted an earlier one (trace "CHARON message interrupted"), and deletes
the machine's disk images afterwards unless KEEP is set.
"""
import subprocess,sys,time,re,os,shutil
from pathlib import Path
base=Path(__file__).resolve().parent
efi,label,pick,*flags=sys.argv[1:];name='r396-'+label
def vm(*a):return subprocess.run([sys.executable,str(base/'charon-vm.py'),*a],capture_output=True,text=True).stdout
def text():
    p=base/name/'serial.log'
    return p.read_bytes().decode('utf-8','replace') if p.exists() else ''
def said():
    out=[]
    for m in re.finditer(r'(CHARON speak|CHARON message)\r?\n(?:(?:pl|en)\r?\n)?([^\r\n]*)',text()):
        s=re.sub(r'\x1b\[[0-9;=]*[A-Za-z]','',m.group(2)).replace('\x1b','').strip()
        if s and not s.startswith('CHARON'):out.append(('M: ' if 'message' in m.group(1) else 'R: ')+s)
    return out
vm('prepare',name,efi,'--grub','grub-dl/grubx64.efi',*flags)
vm('wait',name,'menu announce','150');time.sleep(3)
vm('keys',name,'1.5','spc','ret');time.sleep(5)
print('==',label)
found=False;seen=[]
for step in range(10):
    last=[x for x in said() if x.startswith('R: ')]
    item=last[-1] if last else ''
    seen.append(item)
    if pick!='list' and any(w.lower() in item.lower() for w in pick.split('|')):found=True;break
    if step and item==seen[-2] or 'Koniec listy' in item:break
    vm('keys',name,'0.1','down');time.sleep(3)
print('  lista:',' | '.join(x[3:100] for x in seen))
if pick=='list' or not found:
    print('  NIE ZNALEZIONO' if pick!='list' else '  (tylko lista)')
else:
    mark=len(said());vm('keys',name,'0.15','ret',*os.environ.get('BURST','').split());time.sleep(float(os.environ.get('WAIT','40')))
    for x in said()[mark:]:print('  ',x[:220])
    for k in os.environ.get('KEYS','').split():
        m=len(said());vm('keys',name,'0.12',*k.split(','));time.sleep(float(os.environ.get('STEP','4')));print('  ==',k)
        for x in said()[m:]:print('    ',x[:220])
print('  przerwane komunikaty:',text().count('CHARON message interrupted'))
vm('screen',name,'end.ppm')
if not os.environ.get('KEEP'):
    vm('quit',name);time.sleep(2)
    for p in (base/name).glob('*'):
        if p.suffix in ('.img','.fd','.qcow2','.wav'):
            try:p.unlink()
            except OSError:pass
