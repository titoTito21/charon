#!/usr/bin/env python3
"""r410: dry runs of the two scripts that apply the detailed answers in the installed system.

  python3.15 tests/charon-r410-personal-host.py

For EVERY question of tools/charon-install/charon_questions.py and EVERY value it can take that differs from
the default, an answers file with that one answer is given to
  src/linux/personal.sh --dry-run FILE            (as apt, dnf and pacman systems: CHARON_PM), and to
  tools/charon-addon/Charon-Personal.ps1 -DryRun  (Windows PowerShell 5.1),
and the report line of the key must say applied or skipped with a reason - never "the function gave no
result", never "does not know this answer", never a shell or PowerShell error. Multi-choice questions are tried
with each single value and with all values. Nothing is changed on this computer: a dry run only prints.
The carried files of the golden vectors (tests/golden/charon-install-answers/*.carried) are run as well.
"""
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TREE = HERE.parent
spec = importlib.util.spec_from_file_location('charon_questions', TREE / 'tools/charon-install/charon_questions.py')
table = importlib.util.module_from_spec(spec); spec.loader.exec_module(table)
passed = failed = 0


def check(name, ok, detail=''):
    global passed, failed
    if ok: passed += 1
    else:
        failed += 1; print('FAIL', name, '::', detail[:400])


def values(row):
    if row['kind'] == 'yesNo': return ['no' if row['default'] == 'yes' else 'yes']
    if row['kind'] == 'choice': return [c[0] for c in row['choices'] if c[0] != row['default']]
    if row['kind'] == 'multi':
        one = [c[0] for c in row['choices']]
        return [v for v in one + [','.join(one)] if v != row['default']]
    return [SAMPLES.get(row['key'], 'x')]


# r410 (es): a valid value for every typed (text) question; invalid ones are tried in BAD below (they must say failed)
KEY = 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIEs+test+key+of+the+host+test+only es@host'
SAMPLES = {'tz_other': 'Europe/Vienna', 'ssh_key': KEY, 'ssh_keys_admin': KEY + ';' + KEY.replace('only', 'two'), 'w_ssh_key': KEY,
           'users': 'anna:admin,piotr:standard', 'net_static': '192.168.229.77/24 192.168.229.2 192.168.229.2,1.1.1.1',
           'proxy': 'http://192.168.0.196:8872', 'ntp_server': 'time.example.org', 'repo_mirror': 'http://ftp.de.debian.org/debian/',
           'first_msg': 'Witaj w pracowni', 'asset_tag': 'INW/2026/0412', 'root_ca': 'es-test-ca.crt,second.crt',
           'post_script': 'es-post.sh sha256=' + 'a' * 64, 'phone_home': 'http://192.168.0.196:8871/report',
           'w_key': 'NPPR9-FWDCX-D2C8J-H872K-2YT43', 'w_net_static': '192.168.229.78/24 192.168.229.2 192.168.229.2',
           'w_proxy': '192.168.0.196:8872', 'w_ntp_server': 'time.example.org', 'wsus': 'http://wsus.example.org:8530', 'kms': 'kms.example.org:1688',
           'w_asset_tag': 'INW/2026/0412', 'w_root_ca': 'es-test-ca.cer', 'w_reader_cfg_file': 'nvda-site', 'w_post_script': 'es-post.ps1 sha256=' + 'b' * 64,
           'w_phone_home': 'http://192.168.0.196:8871/report'}
BAD = {'tz_other': '../etc/passwd', 'ssh_key': 'not a key', 'users': 'Bad Name:admin', 'net_static': 'dhcp', 'proxy': 'proxy:3128',
       'repo_mirror': 'ftp.de.debian.org', 'post_script': 'es-post.sh', 'phone_home': 'example.org', 'w_key': '12345',
       'w_ssh_key': 'not a key', 'w_net_static': 'dhcp', 'w_proxy': 'http://x', 'wsus': 'wsus', 'w_post_script': 'x.ps1', 'w_phone_home': 'x'}


sh = shutil.which('sh')
ps = shutil.which('powershell.exe') or shutil.which('powershell')
work = Path(tempfile.mkdtemp(prefix='charon-personal-'))
# r410 (es): the catalogue with a user's own list file software.txt (specification 9.3) next to it
CAT = work / 'catalogue'; CAT.mkdir()
shutil.copyfile(TREE / 'tools/charon-install/catalogue/charon.ini', CAT / 'charon.ini')
(CAT / 'software.txt').write_text('# own list\nrsync\napt: sox\ndnf: sox\npacman: sox\nwinget: Notepad++.Notepad++\n', encoding='utf-8')
rows = [r for r in table.ROWS if r['verified']]
linux = [r for r in rows if r['paths'] not in ('w', 'x')]
windows = [r for r in rows if r['paths'] == 'w']


def run_sh(text, pm):
    f = work / 'answers.env'; f.write_text(text, encoding='utf-8', newline='\n')
    env = dict(os.environ, CHARON_PM=pm, TMPDIR=str(work).replace('\\', '/'), CHARON_CATALOGUE=str(CAT).replace('\\', '/'))
    r = subprocess.run([sh, str(TREE / 'src/linux/personal.sh'), '--dry-run', str(f).replace('\\', '/')], capture_output=True, text=True, encoding='utf-8', errors='replace', env=env, timeout=120)
    return r.returncode, r.stdout, r.stderr


def run_ps(text):
    f = work / 'answers.ini'; f.write_text(text, encoding='utf-8', newline='\r\n')
    r = subprocess.run([ps, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(TREE / 'tools/charon-addon/Charon-Personal.ps1'), '-Answers', str(f), '-DryRun'],
                       capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=300, env=dict(os.environ, CHARON_CATALOGUE=str(CAT)))
    return r.returncode, r.stdout, r.stderr


def good(key, out):
    line = [l for l in out.splitlines() if l.startswith(key + '=')]
    return bool(line) and (line[-1].startswith(key + '=applied') or line[-1].startswith(key + '=skipped')), (line[-1] if line else 'no report line')


if sh:
    for pm in ('apt', 'dnf', 'pacman'):
        for r in linux:
            for v in values(r):
                extra = "braille='auto'\n" if r['key'] == 'braille_table' else ''
                code, out, err = run_sh("charon_path='d'\ncharon_user='jan'\ncharon_ssh='yes'\n%s%s='%s'\n" % (extra, r['key'], v), pm)
                ok, line = good(r['key'], out)
                check('personal.sh %s %s=%s' % (pm, r['key'], v), code == 0 and ok and not err.strip(), line + ' | ' + err)
    for key, v in BAD.items():
        if any(r['key'] == key for r in windows): continue
        code, out, err = run_sh("charon_path='d'\ncharon_user='jan'\ncharon_ssh='yes'\n%s='%s'\n" % (key, v), 'apt')
        check('personal.sh: the bad value %s=%s is reported as failed' % (key, v), code == 0 and (key + '=failed') in out and not err.strip(), out + err)
    code, out, err = run_sh("charon_path='d'\nnot_a_question='1'\n", 'apt')
    check('personal.sh: an unknown key is reported as failed', 'not_a_question=failed' in out, out)
else:
    print('SKIP personal.sh: no sh on this host')
if ps:
    # one PowerShell start per question (all its values in turn would hide each other): one file per value, but
    # grouped per key to keep the run short
    for r in windows:
        for v in values(r):
            code, out, err = run_ps('%s=%s\n' % (r['key'], v))
            ok, line = good(r['key'], out)
            check('Charon-Personal.ps1 %s=%s' % (r['key'], v), code == 0 and ok and not err.strip(), line + ' | ' + err)
    for key, v in BAD.items():
        if not any(r['key'] == key for r in windows): continue
        code, out, err = run_ps('%s=%s\n' % (key, v))
        check('Charon-Personal.ps1: the bad value %s=%s is reported as failed' % (key, v), code == 0 and (key + '=failed') in out and not err.strip(), out + err)
    code, out, err = run_ps('w_key=%s\n' % SAMPLES['w_key'])
    check('Charon-Personal.ps1: the product key is never printed', SAMPLES['w_key'] not in out and SAMPLES['w_key'][-5:] not in out, out)
    code, out, err = run_ps('not_a_question=1\n')
    check('Charon-Personal.ps1: an unknown key is reported as failed', 'not_a_question=failed' in out, out)
else:
    print('SKIP Charon-Personal.ps1: no powershell on this host')
for carried in sorted((HERE / 'golden/charon-install-answers').glob('*.carried')):
    text = carried.read_text(encoding='utf-8')
    body = text.split('====\n', 1)[1].split('\n==== a saved', 1)[0] if '====\n' in text else ''
    if 'answers.env' in text.splitlines()[0] and sh:
        code, out, err = run_sh(body, 'apt')
        check('%s through personal.sh' % carried.name, code == 0 and '=failed' not in out and not err.strip(), out + err)
    elif 'answers.ini' in text.splitlines()[0] and ps:
        code, out, err = run_ps(body)
        check('%s through Charon-Personal.ps1' % carried.name, code == 0 and '=failed' not in out and not err.strip(), out + err)
shutil.rmtree(work, ignore_errors=True)
print('%d passed, %d failed' % (passed, failed))
sys.exit(1 if failed else 0)
