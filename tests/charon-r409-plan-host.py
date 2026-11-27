#!/usr/bin/env python3
"""r409: host tests of the multi-system plan (spec T0). One command, from anywhere:

    python3.15 tests/charon-r409-plan-host.py [-v] [--work DIR] [--skip-dos]

What runs (nothing touches firmware, a disk or the network):
  1. tests/charon-r409-plan-tests.cpp, compiled with the tree's Clang (-Wall -Wextra -Werror):
     placeholder partitions (reserve / retype / remove / free gaps, both sector sizes, failed
     writes), step order of every combination, pre-flight refusals EN/PL, the layout for every
     combination and disk scenario, the answer-set file and the disk pre-authorisation by serial,
     the plan file (round trip, torn writes, RAM-held state, erased answer files), the state
     machine (restarts, markers, budgets, retry / skip / stop, unattended policy), finish rule,
     registry, report.
  2. The plan headers compiled for the EFI target with the EFI build's flags (freestanding, no
     library): COMPILE ONLY - proves they can be included by the EFI program, not that they run.
  3. tests/CharonSeedDiskTests.cpp (the existing checks of the header that got Part 3).
  4. CharonDOS: tools/build-charondos.py (host executable and CHARONDOS.EFI, the latter compile
     and link only) and tests/CharonDosTests.py, which holds the WINNT / LINUX cases.

Environment: CHARON_LLVM (folder with clang++; default work/llvm-23.1.5/bin next to the tree).
Exit code 0 when everything passed.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
TREE = HERE.parent
VERBOSE = '-v' in sys.argv
results = []


def run(command, timeout=900, cwd=None):
    r = subprocess.run([str(c) for c in command], capture_output=True, timeout=timeout, cwd=cwd)
    return r.returncode, r.stdout.decode('utf-8', 'replace'), r.stderr.decode('utf-8', 'replace')


def find_clang():
    for candidate in (os.environ.get('CHARON_LLVM'), TREE.parent / 'llvm-23.1.5' / 'bin', TREE.parent.parent / 'work' / 'llvm-23.1.5' / 'bin'):
        if candidate and (Path(candidate) / 'clang++.exe').exists(): return Path(candidate) / 'clang++.exe'
        if candidate and (Path(candidate) / 'clang++').exists(): return Path(candidate) / 'clang++'
    return shutil.which('clang++')


def note(name, ok, detail):
    results.append((name, ok, detail))
    print(('PASS ' if ok else 'FAIL ') + name + ': ' + detail)


def shell_checks(work):
    """install-charon.sh: the markers stepN.done / stepN.failed and the boot-order helpers. The
    sub-commands run for real on a scratch folder that stands in for the EFI partition; the two
    order functions are taken out of the script and run with a made-up efibootmgr listing. The
    parts that talk to firmware (efibootmgr itself, /sys/firmware/efi) cannot run on this host."""
    sh = shutil.which('sh')
    if not sh:
        note('Linux plan hooks (sh)', False, 'no sh on this host: NOT RUN')
        return
    script = TREE / 'src' / 'linux' / 'install-charon.sh'
    esp = work / 'esp'
    if esp.exists(): shutil.rmtree(esp)
    plan = esp / 'EFI' / 'HAK' / 'plan'
    plan.mkdir(parents=True)
    (esp / 'EFI' / 'HAK' / 'CHARONVOICE.EFI').write_bytes(b'x')
    env = dict(os.environ, CHARON_ESP=esp.as_posix(), CHARON_STATE=(work / 'state').as_posix())
    bad = []

    def call(*args):
        r = subprocess.run([sh, script.as_posix(), *args], capture_output=True, env=env, timeout=60)
        return r.returncode, r.stdout.decode('utf-8', 'replace')

    def expect(name, ok, detail=''):
        if not ok: bad.append(f'{name} {detail}')

    code, out = call('plan-active')
    expect('no plan: plan-active says no', code != 0, code)
    code, out = call('plan-done')
    expect('no plan: no marker, exit 0', code == 0 and not list(plan.glob('step*')), (code, out))
    (plan / 'plan.txt').write_bytes(b'[charon-plan]\r\n')
    code, out = call('plan-active')
    expect('plan running: plan-active says yes', code == 0, code)
    code, out = call('plan-done')
    expect('no step number known: no marker, exit 1', code == 1 and not list(plan.glob('step*')), (code, out))
    (plan / 'current').write_bytes(b'3')
    code, out = call('plan-done')
    expect('plan-done writes step3.done', code == 0 and (plan / 'step3.done').read_bytes() == b'ok\r\n', (code, out))
    log = work / 'first.log'
    log.write_bytes(b''.join(b'line %d\n' % i for i in range(1, 21)))
    code, out = call('plan-failed', 'cloud-init failed: no network', log.as_posix())
    text = (plan / 'step3.failed').read_bytes() if (plan / 'step3.failed').exists() else b''
    expect('plan-failed writes step3.failed with the reason first and the log tail',
           code == 0 and text.startswith(b'cloud-init failed: no network\r\n') and text.endswith(b'line 20\r\n') and b'line 9\r\n' in text and b'line 8\r\n' not in text, (code, text))
    expect('a later result replaces the earlier marker', not (plan / 'step3.done').exists())
    code, out = call('plan-done', 'plan-step=5', 'fine')
    expect('plan-step=N names the step', code == 0 and (plan / 'step5.done').read_bytes() == b'fine\r\n' and (plan / 'step3.failed').exists(), (code, out))

    source = script.read_text(encoding='utf-8')
    functions = '\n'.join(re.search(r'^' + name + r'\(\) \{.*?^\}$|^' + name + r'\(\) \{.*\}$', source, re.M | re.S).group(0) for name in ('entry_of', 'put_first'))
    listing = ('BootCurrent: 0003\nBootOrder: 0003,0001,0004,0000\n'
               'Boot0000* Windows Boot Manager\tHD(1,GPT,x)/File(\\EFI\\Microsoft\\Boot\\bootmgfw.efi)\n'
               'Boot0001* Charon\tHD(1,GPT,x)/File(\\EFI\\HAK\\HAKBOOT.EFI)\nBoot0003* debian\tHD(1,GPT,x)/File(\\EFI\\debian\\shimx64.efi)\n'
               'Boot0004* CharonDOS\tHD(1,GPT,x)/File(\\EFI\\CharonDOS\\CHARONDOS.EFI)\nBoot000A  Charon voice old\n')
    (work / 'listing.txt').write_bytes(listing.encode('utf-8'))
    probe = work / 'order-probe.sh'
    probe.write_bytes(('efibootmgr() { cat "' + (work / 'listing.txt').as_posix() + '"; }\n' + functions + '\n'
                       'echo "charon=$(entry_of Charon)"\necho "dos=$(entry_of CharonDOS)"\necho "none=$(entry_of Fedora)"\n'
                       'echo "plan=$(put_first 0003,0001,0004,0000 0001)"\n'
                       'echo "after=$(put_first "$(put_first 0003,0001,0004,0000 0001)" 0004)"\n'
                       'echo "same=$(put_first 0001,0003 0001)"\necho "new=$(put_first 0003,000a 000B)"\n').encode('utf-8'))
    r = subprocess.run([sh, probe.as_posix()], capture_output=True, timeout=60)
    got = dict(l.split('=', 1) for l in r.stdout.decode().split('\n') if '=' in l)
    want = {'charon': '0001', 'dos': '0004', 'none': '', 'plan': '0001,0003,0004,0000', 'after': '0004,0001,0003,0000', 'same': '0001,0003', 'new': '000B,0003,000A'}
    expect('entry_of / put_first: Charon first during a plan; CharonDOS, Charon, the others after it', got == want, (got, r.stderr.decode()[-300:]))
    for name in ('install-charon.sh', 'firstboot.sh'):
        r = subprocess.run([sh, '-n', (TREE / 'src' / 'linux' / name).as_posix()], capture_output=True, timeout=60)
        expect('sh -n ' + name, r.returncode == 0, r.stderr.decode()[-300:])
    # The header the EFI program embeds must hold exactly these scripts.
    header = (TREE / 'src' / 'efi' / 'CharonLinuxScripts.h').read_text(encoding='ascii')
    expect('CharonLinuxScripts.h was regenerated from the scripts', 'plan-failed' in header and 'put_first' in header and 'plan-active' in header)
    note('Linux plan hooks (sh): markers and boot-order helpers ran', not bad, '11 checks passed' if not bad else '; '.join(bad))


def main():
    work = None
    if '--work' in sys.argv: work = Path(sys.argv[sys.argv.index('--work') + 1])
    made = work is None
    if made: work = Path(tempfile.mkdtemp(prefix='charon-r409-plan-'))
    work.mkdir(parents=True, exist_ok=True)
    clang = find_clang()
    if not clang: raise SystemExit('clang++ not found (set CHARON_LLVM)')
    warn = ['-std=c++17', '-Wall', '-Wextra', '-Werror']

    # 1. the plan core
    exe = work / 'charon-r409-plan-tests.exe'
    code, out, err = run([clang, *warn, '-O1', '-fno-exceptions', HERE / 'charon-r409-plan-tests.cpp', '-o', exe])
    if code: note('plan core: build', False, (out + err)[-3000:])
    else:
        code, out, err = run([exe, *(['-v'] if VERBOSE else [])])
        if VERBOSE or code: print(out)
        last = out.strip().splitlines()[-1] if out.strip() else err[-500:]
        m = re.search(r'(\d+) passed, (\d+) failed', last)
        note('plan core: tests ran', code == 0 and bool(m) and m.group(2) == '0', last)

        # 1b. prepared for phase B2b: the Windows plan-step fragments against their golden files
        golden = HERE / 'golden' / 'charon-r409-plan'
        emitted = work / 'emitted'
        if emitted.exists(): shutil.rmtree(emitted)
        emitted.mkdir()
        run([exe, '--emit', emitted])
        if '--update' in sys.argv:
            golden.mkdir(parents=True, exist_ok=True)
            for f in emitted.iterdir(): shutil.copyfile(f, golden / f.name)
        problems = []
        for f in sorted(emitted.iterdir()):
            want = golden / f.name
            if not want.exists() or want.read_bytes() != f.read_bytes(): problems.append(f.name + ' differs from its golden file')
            if f.suffix == '.xml':
                import xml.dom.minidom
                try: xml.dom.minidom.parseString(b'<r xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">' + f.read_bytes() + b'</r>')
                except Exception as e: problems.append(f.name + ' is not well-formed XML: ' + str(e))
                for line in f.read_bytes().decode().splitlines():
                    command = line.split('<CommandLine>')[1].split('</CommandLine>')[0]
                    if len(command) > 1024: problems.append(f.name + ': a command line is longer than 1024 characters')
        note('Windows plan-step fragments (prepared for B2b): golden files', not problems and len(list(emitted.iterdir())) == 4,
             '4 files identical, XML well-formed, command lines within 1024 characters' if not problems else '; '.join(problems))

    # 2. the same headers for the EFI target: compile only
    probe = work / 'efi-probe.cpp'
    probe.write_text('#include "CharonSeedDisk.h"\n#include "CharonPlanState.h"\n#include "CharonPlanSets.h"\n'
                     'int Probe(hakefi::plan::Run& run) { return int(hakefi::plan::Next(run).what); }\n', encoding='utf-8')
    efi = ['--target=x86_64-unknown-elf', '-O2', '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-fno-pic', '-fno-pie', '-mcmodel=large',
           '-mno-red-zone', '-fno-exceptions', '-fno-rtti', '-fshort-wchar', '-I', TREE / 'src' / 'efi', '-I', TREE / 'tools' / 'charon-compat-include']
    code, out, err = run([clang, *efi, *warn, '-c', probe, '-o', work / 'efi-probe.o'])
    note('plan headers for the EFI target: COMPILED ONLY', code == 0, 'object built, not run' if code == 0 else (out + err)[-3000:])

    # 3. the existing seed-disk checks
    seed = work / 'CharonSeedDiskTests.exe'
    code, out, err = run([clang, '-std=c++17', '-O1', '-Wall', '-Wextra', HERE / 'CharonSeedDiskTests.cpp', '-o', seed])
    if code: note('seed disk (existing checks): build', False, (out + err)[-3000:])
    else:
        code, out, err = run([seed])
        passed, failed = out.count('\nPASS ') + out.startswith('PASS '), out.count('FAIL ')
        note('seed disk (existing checks): ran', code == 0, f'{passed - 1} checks passed, {failed} failed' if code == 0 else out[-1500:])

    # 4. CharonDOS
    if '--skip-dos' not in sys.argv:
        dos = work / 'charondos'
        if dos.exists(): shutil.rmtree(dos)
        code, out, err = run([sys.executable, TREE / 'tools' / 'build-charondos.py', dos])
        note('CharonDOS: host executable built, CHARONDOS.EFI COMPILED AND LINKED ONLY', code == 0, out.strip().splitlines()[-1] if code == 0 and out.strip() else (out + err)[-3000:])
        if code == 0:
            code, out, err = run([sys.executable, HERE / 'CharonDosTests.py', dos / 'charondos-host.exe'])
            lines = out.strip().splitlines()
            launch = [l for l in lines if ' launch_' in l]
            if code: print('\n'.join(l for l in lines if not l.startswith('ok')))
            note('CharonDOS: host tests ran', code == 0, (lines[-1] if lines else err[-500:]) + f'; of these WINNT/LINUX cases: {sum(l.startswith("ok") for l in launch)}/{len(launch)}')

    # 5. the plan hooks of the installed Linux systems (src/linux/install-charon.sh), run by sh
    shell_checks(work)

    failed = [r for r in results if not r[1]]
    print(('PASS' if not failed else 'FAIL') + f' charon-r409-plan-host: {len(results) - len(failed)}/{len(results)} parts')
    if made and not failed: shutil.rmtree(work, ignore_errors=True)
    elif failed: print('work directory kept:', work)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
