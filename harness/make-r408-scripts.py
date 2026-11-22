"""r408: derive the ISO / installer / GitHub / test-VM scripts and their tests from r404's.
(package-charon-r408.py and readme-r408/ are written by hand.)"""
from pathlib import Path
w=Path(__file__).resolve().parent
def conv(t):
    t=t.replace('charon-audio-driver-r404/AudioDxe-r404.efi','charon-audio-driver-r408/AudioDxe-r408.efi')
    t=t.replace("CHARON_DRIVERS='charon-drivers-r400'","CHARON_DRIVERS='charon-drivers-r408'")
    t=t.replace("CHARON_PACKAGES='pkg-r403final-dos'","CHARON_PACKAGES='pkg-r408-dos'")
    return t.replace('r404','r408')
for old in ('make-charon-iso-r404.py','make-charon-installer-r404.py','make-charon-github-r404.py','make-charon-testvm-r404all.py','test-charon-iso-r404.py','test-testvm-r404all.py'):
    new=old.replace('r404','r408');assert not (w/new).exists(),new
    t=conv((w/old).read_text(encoding='utf-8'))
    if old.startswith('make-charon-iso'):
        a="keep=('audio-driver','charon-shell',"
        assert a in t
        t=t.replace(a,"keep=('audio-driver','bearssl','charondos','charonpc','driver-pack','network-drivers','charon-shell',")
        a="    for p in sorted((rel/'licenses'/d).rglob('*')):"
        assert a in t
        t=t.replace(a,"    if not (rel/'licenses'/d).exists():continue\n"+a)
    if old.startswith('make-charon-installer'):
        a="for d in ('Instalator','Dodatek-UEFI','Dodatki-syntezy','licenses'):"
        assert a in t
        t=t.replace(a,"for d in ('Instalator','Dodatek-UEFI','Dodatki-syntezy','OS-tools','licenses'):")
    if old.startswith('make-charon-github'):
        a="'farewell-test.sh','farewell-check.py'):"
        assert a in t
        t=t.replace(a,"'farewell-test.sh','farewell-check.py','make-charon-iso-trace.py','make-vmware-vm.py','vnc-keys.py','serial-pipe.py','vm-rawdisk.py','vm-audio-meter.py','dos-quick.sh','dos-me.sh','dos-me2.sh','make-r408-scripts.py'):")
        a="put(work/'r398q-regression.txt',out/'docs/results/charon-r398q-regression.txt')"
        assert a in t
        t=t.replace(a,a+"\nput(work/'regression-r408-tcg.out',out/'docs/results/charon-r408-regression.txt')")
    (w/new).write_text(t,encoding='utf-8');print(new)
