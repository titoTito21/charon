#!/usr/bin/env python3
# Host checks of Charon Hypervisor's pure parts (no QEMU): .vmx / .ovf parsing, the QEMU command of the templates,
# the XP answer file, the guest reader's change detection. Run: python3 tests/charon-r410-hypervisor-host.py
import os, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'src', 'hypervisor', 'overlay', 'usr', 'lib', 'charonhv'))
import hvmachine, hvreader
fails = 0
def check(name, cond):
    global fails
    print(('PASS ' if cond else 'FAIL ') + name)
    fails += 0 if cond else 1

d = tempfile.mkdtemp()
vmx = os.path.join(d, 'XP Test.vmx')
open(vmx, 'w').write('\n'.join(['.encoding = "UTF-8"', 'displayName = "XP Test"', 'memsize = "768"', 'numvcpus = "2"',
    'guestOS = "winxppro"', 'scsi0:0.present = "TRUE"', 'scsi0:0.fileName = "XP Test.vmdk"', 'ide1:0.present = "TRUE"',
    'ide1:0.fileName = "x.iso"', 'ide1:0.deviceType = "cdrom-image"', 'ethernet0.present = "TRUE"', 'ethernet0.virtualDev = "e1000"',
    'sound.present = "TRUE"', 'sound.virtualDev = "es1371"', 'firmware = "efi"']))
v = hvmachine.parse_vmx(vmx)
check('vmx name/memory/cpus', (v['name'], v['memory'], v['cpus']) == ('XP Test', 768, 2))
check('vmx one disk, no cdrom', len(v['disks']) == 1 and v['disks'][0].endswith('XP Test.vmdk'))
check('vmx nic/sound/firmware', (v['nic'], v['sound'], v['firmware']) == ('e1000', 'ES1370', 'uefi'))

ovf = os.path.join(d, 'deb.ovf')
open(ovf, 'w').write('''<?xml version="1.0"?>
<Envelope xmlns="http://schemas.dmtf.org/ovf/envelope/1" xmlns:ovf="http://schemas.dmtf.org/ovf/envelope/1"
 xmlns:rasd="http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_ResourceAllocationSettingData">
 <References><File ovf:id="file1" ovf:href="deb-disk1.vmdk"/></References>
 <DiskSection><Disk ovf:diskId="vmdisk1" ovf:fileRef="file1" ovf:capacity="8589934592"/></DiskSection>
 <VirtualSystem ovf:id="Debian 6 test">
  <OperatingSystemSection ovf:id="96"><Description>Debian_64</Description></OperatingSystemSection>
  <VirtualHardwareSection>
   <Item><rasd:ResourceType>3</rasd:ResourceType><rasd:VirtualQuantity>2</rasd:VirtualQuantity></Item>
   <Item><rasd:AllocationUnits>MegaBytes</rasd:AllocationUnits><rasd:ResourceType>4</rasd:ResourceType><rasd:VirtualQuantity>1536</rasd:VirtualQuantity></Item>
   <Item><rasd:ResourceType>10</rasd:ResourceType><rasd:ResourceSubType>E1000</rasd:ResourceSubType></Item>
   <Item><rasd:ResourceType>35</rasd:ResourceType><rasd:ResourceSubType>ensoniq1371</rasd:ResourceSubType></Item>
   <Item><rasd:HostResource>/disk/vmdisk1</rasd:HostResource><rasd:ResourceType>17</rasd:ResourceType></Item>
  </VirtualHardwareSection>
 </VirtualSystem>
</Envelope>''')
o = hvmachine.parse_ovf(ovf, d)
check('ovf name/cpus/memory', (o['name'], o['cpus'], o['memory']) == ('Debian 6 test', 2, 1536))
check('ovf disk by reference', o['disks'] == [os.path.join(d, 'deb-disk1.vmdk')])
check('ovf nic/sound', (o['nic'], o['sound']) == ('e1000', 'hda'))

hvmachine.MACHINES = os.path.join(d, 'machines')
os.makedirs(hvmachine.MACHINES)
mid = hvmachine.create('Windows XP', 'winxp', dict(memory=768))
m = hvmachine.Machine(mid)
m.cfg['disks'] = ['disk0.qcow2']; m.cfg['cdrom'] = '/hv/images/WINXP.iso'; m.cfg['floppy'] = 'floppy'
os.makedirs(os.path.join(m.dir, 'floppy'))
cmd = ' '.join(m.command(False))
check('XP command: AC97 + rtl8139 + IDE + 768 MB + tcg', all(s in cmd for s in ('AC97,audiodev=snd0', 'rtl8139', 'if=ide', '-m 768', '-accel tcg,thread=multi')))
# 6zc: hypervisor-side performance profile
check('6zc XP: no HPET, RTC driftfix, writeback+threads on IDE, no Hyper-V flags', all(s in cmd for s in (
    'pc,hpet=off', 'base=localtime,driftfix=slew', 'cache=writeback,aio=threads')) and 'hv-' not in cmd)
_p = hvmachine.performance
check('6zc KVM: XP host CPU without enlightenments, Vista its 6.0 set, 7 adds relaxed/synic/stimer, 8.1 tlbflush',
      _p('winxp', True)['cpu'] == 'host' and 'hv-relaxed' not in _p('winvista', True)['cpu'] and 'hv-vapic' in _p('winvista', True)['cpu']
      and 'hv-stimer' in _p('win7', True)['cpu'] and 'hv-tlbflush' not in _p('win7', True)['cpu'] and 'hv-tlbflush' in _p('win81', True)['cpu']
      and 'kvm-pit.lost_tick_policy=delay' in _p('win7', True)['extra'])
check('6zc 9x: no HPET, no driftfix; Linux keeps HPET', _p('win98', False)['machine'] == ',hpet=off' and _p('win98', False)['rtc'] == ''
      and _p('debian13', False)['machine'] == '')
old = ' '.join(m.command(False, 0))
check('6zc profile 0 (snapshots from before 6zc): the old hardware, no hpet=off/driftfix', 'hpet=off' not in old
      and 'driftfix' not in old and '-accel tcg ' in old + ' ' and m.snapshot_profile('daily-2026-11-25') == 0)
m.note_snapshot('daily-new', 1)
check('6zc snapshot keeps its profile; a disk-only snapshot loads with any', m.snapshot_profile('daily-new') == 1
      and hvmachine.Machine(mid).cfg['snapshot_profiles']['daily-new'] == 1)
# 6n Windows Me template: SB16 sound + ne2k_pci NIC + IDE + 256 MB + the 9x performance profile (hpet off, no driftfix)
_meid = hvmachine.create('Windows Me', 'winme', {})
_me = hvmachine.Machine(_meid)
_me.cfg['disks'] = ['disk0.qcow2']
_mecmd = ' '.join(_me.command(False))
check('6n Windows Me template: SB16 + ne2k_pci + IDE + 256 MB + 9x profile (hpet off, no driftfix)',
      'sb16' in _mecmd.lower() and 'ne2k_pci' in _mecmd and 'if=ide' in _mecmd and '-m 256' in _mecmd
      and 'hpet=off' in _mecmd and 'driftfix' not in _mecmd and hvmachine.TEMPLATES['winme']['reader'] == 'hypervisor')


class _FakeQMP:
    def __init__(self): self.sent = []
    def hmp(self, c, timeout=0): self.sent.append(c); return ''


class _Run:
    def poll(self): return None


m.proc, m.qmp, m.profile = _Run(), _FakeQMP(), 1
_starts = []
m.kill = lambda: None
m.start = lambda kvm, ev, profile=1, loadvm=None: _starts.append((profile, loadvm)) or True
m.kvm, m.on_event = False, None
m.snapshot_revert('daily-new')
m.snapshot_revert('daily-2026-11-25')
check('6zc revert: same profile -> loadvm; older profile -> restart on profile 0 with -loadvm',
      m.qmp.sent == ['loadvm daily-new'] and _starts == [(0, 'daily-2026-11-25')])
del m.proc, m.qmp, m.kill, m.start
m.proc = None
check('XP command: answer floppy as FAT', 'fat:floppy:' in cmd)
check('XP command: CD first', '-boot order=dc' in cmd)
check('id from name', mid == 'windows-xp')

import importlib.util
spec = importlib.util.spec_from_file_location('charonhv', os.path.join(HERE, '..', 'src', 'hypervisor', 'overlay', 'usr', 'lib', 'charonhv', 'charonhv.py'))
src = open(spec.origin, encoding='utf-8').read()
ns = {}
exec(compile(src.split('class App(')[0].replace('import hvcompat, hvdisplay, hvkeys, hvmachine, hvproxy, hvreader, hvretro, hvscan, hvsecure, hvsettings, hvshare, hvvoice','import hvcompat, hvmachine, hvreader, hvsecure'), 'charonhv', 'exec'), ns)
exec(compile('import re\n' + 'def winnt_sif' + src.split('def winnt_sif', 1)[1].split('def main')[0], 'charonhv', 'exec'), ns)
sif = ns['winnt_sif'](dict(name='Windows XP'), True)
check('winnt.sif: no product key, unattended, Warsaw time zone', 'ProductKey' not in sif and 'UnattendMode=FullUnattended' in sif and 'TimeZone=95' in sif)
check('winnt.sif computer name', 'ComputerName=WINDOWS-XP' in sif)

check('reader changes', hvreader.changes(['a', 'b', ''], ['a', 'c', 'b']) == ['c'])
# r410 6e/6f: Charon's synthesizer packs (CHSPACK1, LZMA + byte planes) read back by the hypervisor's voice
import lzma, struct, zlib
import hvvoice
words = bytes(range(256)) * 8
planes = b''.join(words[k::4] for k in range(4))
packed = lzma.compress(planes, format=lzma.FORMAT_ALONE)
name = b'data/voices/natan/16000/voice.data'
head = b'CHSPACK1' + struct.pack('<II', 1, 0) + struct.pack('<H', len(name)) + name
table = len(head) + 18 + 8
head += struct.pack('<BBIIII', 1, 1, len(words), len(packed), table, zlib.crc32(words) & 0xffffffff) + struct.pack('<II', 0, len(words))
pack = os.path.join(tempfile.mkdtemp(), 'rhvoice-pl.chsp')
open(pack, 'wb').write(head + packed)
check('Charon pack read back (planes undone, CRC)', hvvoice.read_pack(pack) == {name.decode(): words})
# r410 (user): the hypervisor has ALL of Charon's voices - its list (hvvoice.catalogue, read from Charon's own manifests
# at run time) is compared with Charon's own voice list: src/synth/rhvoice/VOICES.tsv (the RHVoice add-on's table),
# the eSpeak NG add-on's voices in tools/build-charon-synths.py, TVox built in. A missing voice fails here.
import csv, importlib.machinery, re as _re
_root = os.path.join(HERE, '..')
_rows = [r for r in csv.DictReader((l for l in open(os.path.join(_root, 'src/synth/rhvoice/VOICES.tsv'), encoding='utf-8')
                                    if not l.startswith('#')), delimiter='\t')]
_bs = open(os.path.join(_root, 'tools/build-charon-synths.py'), encoding='utf-8').read()
_esp = _re.findall(r"\('(espeak-[a-z]+)', '([^']+)', '([a-z]+)'\)", _bs.split("'espeak':", 1)[1].split("'rhvoice':", 1)[0])
_hak = tempfile.mkdtemp()
os.makedirs(os.path.join(_hak, 'synths', 'rhvoice'))
open(os.path.join(_hak, 'synths', 'espeak.manifest'), 'w', encoding='utf-8').write(
    'abi = 2\nname = eSpeak NG\nmodule = espeak.tts\n' + ''.join('voice = %s | %s | %s\n' % v for v in _esp))
open(os.path.join(_hak, 'synths', 'rhvoice.manifest'), 'w', encoding='utf-8').write(
    'abi = 2\nname = RHVoice\nmodule = rhvoice.tts\n' + ''.join('voice = %s | %s | %s\n' % (r['charon_id'], r['spoken_name'], r['language']) for r in _rows))
def _pack(names):     # a CHSPACK1 table only (names, empty entries): what catalogue() reads of the language packs
    b = b'CHSPACK1' + struct.pack('<II', len(names), 0)
    for n in names:
        b += struct.pack('<H', len(n)) + n.encode() + struct.pack('<BBIIII', 0, 0, 0, 0, 0, 0)
    return b
for _l in ('pl', 'en'):
    open(os.path.join(_hak, 'synths', 'rhvoice', 'rhvoice-%s.chsp' % _l), 'wb').write(_pack(
        ['data/languages/x'] + ['data/voices/%s/voice.info' % r['voice_dir'] for r in _rows if r['base_pack'] == 'yes' and r['language'] == _l]))
for _p in [r['charon_id'] for r in _rows if r['base_pack'] != 'yes']:
    open(os.path.join(_hak, 'synths', 'rhvoice', _p + '.chsp'), 'wb').write(b'CHSPACK1')
_cs = hvvoice.load_source('charon_settings_t', os.path.join(_root, 'tools/charon-os/linux/charon-settings'))
_cat = hvvoice.catalogue(_hak, settings_module=_cs)
_ids = [v['id'] for v in _cat]
_want = ['tvox-pl', 'tvox-en'] + [v[0] for v in _esp] + [r['charon_id'] for r in _rows]
check('voices: Charon lists 13 RHVoice voices and 2 eSpeak NG languages', len(_rows) == 13 and len(_esp) == 2)
check('voices: the hypervisor lists every voice of Charon, in its order (%d)' % len(_want), _ids == _want)
check('voices: every RHVoice voice by its own name', [v['name'] for v in _cat if v['engine'] == 'rhvoice'] == [r['spoken_name'] for r in _rows])
check('voices: every RHVoice voice has its data on the medium', all(v['available'] for v in _cat if v['engine'] == 'rhvoice'))
check('voices: every RHVoice voice has a language profile', all(r['voice_dir'] in hvvoice.RHVOICE_PROFILES[r['language']] for r in _rows))
os.remove(os.path.join(_hak, 'synths', 'rhvoice', 'rhvoice-magda.chsp'))
check('voices: a voice whose data is missing is listed as not available',
      [v['available'] for v in hvvoice.catalogue(_hak, settings_module=_cs) if v['id'] == 'rhvoice-magda'] == [False])
_built = os.path.join(_root, '..', 'synth-modules-r410a', 'EFI', 'HAK')
if os.path.isdir(_built):
    check('voices: the built r410 add-ons (synth-modules-r410a) give the same list',
          [(v['id'], v['available'] or v['engine'] != 'rhvoice') for v in hvvoice.catalogue(_built, settings_module=_cs)] == [(i, True) for i in _want])
# 6h: product-key pages (text as XP's Polish volume-licence GUI Setup shows it; made-up key shapes only)
xp = ['Instalator systemu Windows', 'Twój klucz produktu', 'Wpisz poniżej klucz produktu licencji woluminów.',
      'Klucz produktu: ABCDE - 12345 - FGHIJ - 6789K - LMNOP']
kp = hvreader.key_page(xp)
check('key page found, 5 fields of 5, volume licence', kp == dict(fields=5, size=5, kind='volume'))
check('key page: typed key redacted', 'ABCDE' not in hvreader.redact_key(xp[3]) and '12345' not in hvreader.redact_key(xp[3]))
check('not a key page', hvreader.key_page(['Instalator systemu Windows', 'Kopiowanie plików']) is None)
check('refusal read in full', 'nieprawidłowy' in (hvreader.key_refused(['Wprowadzony klucz produktu jest nieprawidłowy.', 'Spróbuj ponownie.']) or ''))
check('winnt.sif: a typed key goes only into the answer file', 'ProductKey="AAAAA-BBBBB-CCCCC-DDDDD-EEEEE"' in
      ns['winnt_sif'](dict(name='Windows XP', product_key='AAAAA-BBBBB-CCCCC-DDDDD-EEEEE'), True))
# r410 session 3: every overlay module must at least compile (an indentation error in hvdisplay.py once slipped through)
_ovl = os.path.join(HERE, '..', 'src', 'hypervisor', 'overlay', 'usr', 'lib', 'charonhv')
for _f in sorted(os.listdir(_ovl)):
    if _f.endswith('.py'):
        try:
            compile(open(os.path.join(_ovl, _f), encoding='utf-8').read(), _f, 'exec')
            _ok = True
        except SyntaxError as e:
            print(e); _ok = False
        check('overlay module compiles: %s' % _f, _ok)
# 6m: the TVox source archive for the build at first start carries everything build-tvox.sh needs
import importlib.util, io, tarfile
_spec = importlib.util.spec_from_file_location('hvfetch', os.path.join(HERE, '..', 'tools', 'charon-hv-fetch.py'))
_fetch = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_fetch)
_names = tarfile.open(fileobj=io.BytesIO(_fetch.tvox_sources(0))).getnames()
check('TVox source archive (front end, build script, LiveVoice, CharonMemory.c, CharonFormatting.h)',
      all(n in _names for n in ('charonhv-tvox.cpp', 'build-tvox.sh', 'src/tts/runtime/CharonLiveVoice.cpp',
                                'src/tts/runtime/CharonMemory.c', 'src/efi/CharonFormatting.h', 'src/tts/tvox_native/tvox_context_x64.S')))
# 6g Speakup-style tracking: highlight rows and cursor speech
_attrs = ['17' * 80] * 25
_attrs[5] = '17' * 4 + '71' * 30 + '17' * 46
check('highlight row found (a 30-cell bar on row 5)', hvreader.highlighted_rows(_attrs) == (5,))
check('a whole-screen colour is not a highlight', hvreader.highlighted_rows(['71' * 80] * 25) == ())
_ls = ['C:\\>dir', 'hello world  again']
check('cursor: new row -> the line; one step -> the character; a jump -> the word',
      hvreader.cursor_speech(_ls, (0, 3), (1, 0)) == 'hello world  again' and hvreader.cursor_speech(_ls, (1, 0), (1, 1)) == 'e'
      and hvreader.cursor_speech(_ls, (1, 0), (1, 7)) == 'world' and hvreader.cursor_speech(_ls, (1, 1), (1, 1)) is None)
# 6i-6l: the secure-use profile applied from manifests.json
import hvsecure, json, shutil, socket, ssl, subprocess, threading, time
_xp = hvsecure.items('xp', groups=set(hvsecure.DEFAULT_GROUPS))
_imp = [e['id'] for e in hvsecure.items('xp', groups={'improvements'})]
check('6p XP improvements: only decided-include items (7-Zip, VLC, foobar2000, TLS proxy), SP4 and One-Core-API left out', '7zip-xp' in _imp and 'usp4-harkaz-xp' not in _imp and 'one-core-api-xp' not in _imp)
_k = {e['id']: hvsecure.kind(e) for e in _xp}
check('XP set: 25 parts; Legacy Update runs, TLS 1.2 none (proxy), Mypal unpacked, Supermium by hand, root lists via Legacy Update',
      len(_xp) == 25 and _k['legacy-update-xp'] == 'run' and _k['tls12-xp'] == 'none' and _k['mypal-xp'] == 'zip-app'
      and _k['supermium-32-xp'] == 'manual' and _k['ms-trustedr-roots-xp'] == 'via')
check('Windows 7 x86 leaves out the x64 parts', not any('x64' in e['id'] or '-64-' in e['id'] for e in hvsecure.items('7', 'x86'))
      and any('x64' in e['id'] for e in hvsecure.items('7', 'x64')))
_dx = next(e for e in _xp if e['id'].startswith('directx'))
_c = hvsecure.command_for(_dx, '14\\directx_Jun2010_redist.exe')
check('DirectX: unpack, DXSETUP /silent, folder removed', _c == ['start "" /wait "%D%14\\directx_Jun2010_redist.exe" /Q /T:C:\\dxjun2010',
                                                                 'start "" /wait C:\\dxjun2010\\DXSETUP.exe /silent', 'rd /s /q C:\\dxjun2010'])
_op = next(e for e in hvsecure.items('7', 'x64') if e['id'] == '0patch-agent-7')
check('msiexec item gets its own path, the note in brackets is dropped',
      hvsecure.command_for(_op, '05\\0PatchInstaller_22.msi') == ['start "" /wait msiexec /q /i "%D%05\\0PatchInstaller_22.msi"'])
_cmd = hvsecure.secure_cmd('xp', [('Legacy Update 1.14.1', ['start "" /wait "%D%01\\LegacyUpdate-1.14.1.exe" /S /norestart'])], False)
check('XP SECURE.CMD: proxy before the parts, CA, firewall, SMB/RDP/remote registry/autorun off, read-back, no POSReady',
      _cmd.index('proxycfg -u') < _cmd.index('LegacyUpdate') and 'CHARONCA.REG' in _cmd and 'netsh firewall set opmode mode=ENABLE exceptions=DISABLE' in _cmd
      and 'SMBDeviceEnabled' in _cmd and 'fDenyTSConnections' in _cmd and 'RemoteRegistry' in _cmd and 'NoDriveTypeAutoRun' in _cmd
      and 'for %%s in (RemoteRegistry' in _cmd and 'sc qc %%s' in _cmd and 'wmic qfe' in _cmd and 'start notepad' in _cmd and 'posready' not in _cmd.lower() and 'WEPOS' not in _cmd)
_c7 = hvsecure.secure_cmd('7', [], False)
check('Windows 7 SECURE.CMD: advfirewall block inbound, SMB1 off, certutil, winhttp proxy',
      'blockinbound' in _c7 and 'mrxsmb10 start= disabled' in _c7 and 'certutil -addstore -f Root' in _c7 and 'netsh winhttp import proxy' in _c7)
check('hardening can be switched off', 'netsh firewall set' not in hvsecure.secure_cmd('xp', [], False, harden=False))
_der = bytes(range(256)) * 3
_reg = hvsecure.ca_blob_reg(_der)
_hex = ''.join(_reg.split('"Blob"=hex:')[1].replace('\\', '').split())
_blob = bytes(int(x, 16) for x in _hex.split(',') if x)
check('CA registry blob: SHA-1 property 3 then the certificate, property 32',
      _blob[:12] == b'\x03\x00\x00\x00\x01\x00\x00\x00\x14\x00\x00\x00' and _blob[32:36] == b'\x20\x00\x00\x00' and _blob[44:] == _der)
_m = hvmachine.Machine.__new__(hvmachine.Machine)
_m.id, _m.dir = 'xp', tempfile.mkdtemp()
_m.cfg = dict(name='XP', template='winxp', family='windows', nic='rtl8139', secure=True, shared=True, disks=['d.qcow2'])
_m.proxy_port = 40123
open(os.path.join(_m.dir, 'secure.img'), 'wb').close()
_q = ' '.join(_m.command(False))
check('secure machine: restrict=on, guestfwd to its proxy, paused for the snapshot, CHARONSEC read-only, share read-only',
      'restrict=on' in _q and 'guestfwd=tcp:10.0.2.100:3128-cmd:' in _q and '127.0.0.1 40123' in _q and ' -S' in _q
      and 'secure.img,if=none,id=usec,format=raw,readonly=on' in _q and 'fat:' + hvmachine.HV + '/shared' in _q.replace('fat:rw:', 'RW')
      and _m.usb_drives()[0][1] is False)
_m.cfg['internet'] = False
check('internet off: no way out at all', 'guestfwd' not in ' '.join(_m.command(False)))
_m.cfg.update(secure=False, internet=True)
check('without the profile: plain user network, no -S', 'restrict' not in ' '.join(_m.command(False)) and ' -S' not in ' '.join(_m.command(False)))
# build(): fetch replaced by a local file, spoken lines collected
_tmp = tempfile.mkdtemp()
_said = []
def _fake_fetch(e, progress=None):
    p = os.path.join(_tmp, 'f-' + e['id'])
    if e['id'].startswith('mypal'):
        import zipfile
        with zipfile.ZipFile(p, 'w') as z:
            z.writestr('mypal/mypal.exe', b'MZ')
        return p, 'mypal.zip'
    if e['id'].startswith('dotnet-40'):
        raise ValueError('it does not match the recorded SHA-256')
    open(p, 'wb').write(b'MZ')
    return p, (e.get('url') or 'x').rsplit('/', 1)[-1] or 'x.exe'
_orig = hvsecure.fetch
hvsecure.fetch = _fake_fetch
try:
    _sec, _rep = hvsecure.build(dict(name='XP', template='winxp'), _tmp, lambda t, interrupt=True: _said.append(t), lambda en, pl: en)
finally:
    hvsecure.fetch = _orig
_text = open(os.path.join(_sec, 'SECURE.CMD'), encoding='cp1252').read()
check('build: every part named, unofficial ones called so, failure said and the rest continues, Mypal copied with a shortcut',
      any('unofficial: Legacy Update' in s for s in _said) and any('failed' in s and 'rest continues' in s for s in _said)
      and any('no anti-malware vendor' in s.lower() for s in _said) and 'MKLNK.VBS' in _text and 'Mypal' in _text
      and any(r.startswith('failed') for _, r in _rep) and os.path.exists(os.path.join(_sec, 'PROGRAMS')))
# 6n: Windows Me from manifests-9x.json: SECURE.BAT for COMMAND.COM, a REGEDIT4 file instead of reg.exe, parts
# without a checked copy from their own project are said and left out, the drive goes in as an IDE disk
_said.clear()
hvsecure.fetch = _fake_fetch
try:
    _sec9, _rep9 = hvsecure.build(dict(name='Me', template='winme'), tempfile.mkdtemp(), lambda t, interrupt=True: _said.append(t), lambda en, pl: en)
finally:
    hvsecure.fetch = _orig
_bat = open(os.path.join(_sec9, 'SECURE.BAT'), encoding='cp1252').read()
check('Windows Me set: SECURE.BAT with the drive letter, regedit files, no reg.exe, unverified parts said, KernelEx kept for the user',
      '%1:\\CHARONSEC\\SECURE9X.REG' in _bat and 'reg add' not in _bat and not os.path.exists(os.path.join(_sec9, 'SECURE.CMD'))
      and '"ProxyServer"="10.0.2.100:3128"' in open(os.path.join(_sec9, 'SECURE9X.REG')).read()
      and any('not fetched' in s for s in _said) and ('kernelex-me', 'staged manual') in [tuple(r) for r in _rep9])
_m.cfg.update(template='winme', secure=True)
check('Windows Me: CHARONSEC as a second IDE disk with writes thrown away', 'secure.img,if=ide,index=1,media=disk,format=raw,snapshot=on' in ' '.join(_m.command(False)))
# the proxy: refusals, and modern TLS for an old (TLS 1.0) client, against a real site
import hvproxy
_pd = tempfile.mkdtemp()
_ok_ssl = shutil.which('openssl') is not None
if _ok_ssl:
    hvproxy.make_ca(_pd)
    _port = 40000 + os.getpid() % 2000
    _plog = os.path.join(_pd, 'p.log')
    threading.Thread(target=hvproxy.Proxy(_port, _pd, _plog, os.path.join(_pd, 'none')).serve, daemon=True).start()
    time.sleep(1)
    def _req(data):
        s = socket.create_connection(('127.0.0.1', _port), 10)
        s.sendall(data)
        out = b''
        while b'\r\n\r\n' not in out:
            b = s.recv(4096)
            if not b:
                break
            out += b
        return s, out
    check('proxy: blocked test domain over HTTPS', b' 403 ' in _req(b'CONNECT malware.testing.google.test:443 HTTP/1.0\r\n\r\n')[1])
    check('proxy: blocked test domain over HTTP', b' 403 ' in _req(b'GET http://sub.malware.testing.google.test/ HTTP/1.0\r\nHost: x\r\n\r\n')[1])
    check('proxy: other ports refused', b' 403 ' in _req(b'CONNECT example.com:25 HTTP/1.0\r\n\r\n')[1])
    # TLS 1.0 leg for XP: PRF against OpenSSL's TLS1-PRF, RC4 against the published test vector, then a whole
    # RC4-SHA handshake (a minimal TLS 1.0 client written here, RFC 2246 order) and a page from a current site
    _kdf = subprocess.run(['openssl', 'kdf', '-keylen', '16', '-kdfopt', 'digest:MD5-SHA1', '-kdfopt', 'hexsecret:0102',
                           '-kdfopt', 'hexseed:0304', 'TLS1-PRF'], capture_output=True, text=True).stdout.strip().replace(':', '').lower()
    check('TLS 1.0 PRF equals OpenSSL TLS1-PRF', hvproxy.prf10(b'\x01\x02', b'', b'\x03\x04', 16).hex() == _kdf)
    check('RC4 test vector (Key/Plaintext)', hvproxy.RC4(b'Key').crypt(b'Plaintext').hex() == 'bbf316e8d940af0ad3')
    try:
        import hashlib, hmac
        s, h = _req(b'CONNECT www.wikipedia.org:443 HTTP/1.0\r\n\r\n')
        cr = os.urandom(32)
        body = b'\x03\x01' + cr + b'\x00' + b'\x00\x04\x00\x05\x00\xff' + b'\x01\x00'
        ch = b'\x01' + len(body).to_bytes(3, 'big') + body
        s.sendall(b'\x16\x03\x01' + len(ch).to_bytes(2, 'big') + ch)
        tl = hvproxy.OldTLS(s, b'', [], {})       # its record reader, used as a client
        tl.hbuf = bytearray()
        msgs = [tl._handshake_msg() for _ in range(3)]
        hs = ch + b''.join(msgs)
        sr = msgs[0][6:38]
        cert = msgs[1][10:10 + int.from_bytes(msgs[1][7:10], 'big')]
        open(os.path.join(_pd, 'srv.der'), 'wb').write(cert)
        mod = subprocess.run(['openssl', 'x509', '-inform', 'DER', '-in', os.path.join(_pd, 'srv.der'), '-noout', '-modulus'],
                             capture_output=True, text=True).stdout.split('=')[1].strip()
        n = int(mod, 16)
        pre = b'\x03\x01' + os.urandom(46)
        k = (n.bit_length() + 7) // 8
        pad = bytes(b or 1 for b in os.urandom(k - 51))
        enc = pow(int.from_bytes(b'\x00\x02' + pad + b'\x00' + pre, 'big'), 65537, n).to_bytes(k, 'big')
        cke = b'\x10' + (len(enc) + 2).to_bytes(3, 'big') + len(enc).to_bytes(2, 'big') + enc
        hs += cke
        ms = hvproxy.prf10(pre, b'master secret', cr + sr, 48)
        kb = hvproxy.prf10(ms, b'key expansion', sr + cr, 72)
        tl._send(22, cke)
        tl._send(20, b'\x01')
        tl.wkey, tl.wmac, tl.mac = hvproxy.RC4(kb[40:56]), kb[:20], ('sha1', 20)
        fin = b'\x14\x00\x00\x0c' + hvproxy.prf10(ms, b'client finished', hashlib.md5(hs).digest() + hashlib.sha1(hs).digest(), 12)
        tl._send(22, fin)
        hs += fin
        typ, _ = tl._record()
        tl.rkey, tl.rmac = hvproxy.RC4(kb[56:72]), kb[20:40]
        sfin = tl._handshake_msg()
        okfin = typ == 20 and sfin[4:] == hvproxy.prf10(ms, b'server finished', hashlib.md5(hs).digest() + hashlib.sha1(hs).digest(), 12)
        tl.write(b'GET / HTTP/1.1\r\nHost: www.wikipedia.org\r\nUser-Agent: Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 5.1)\r\nConnection: close\r\n\r\n')
        page = b''
        while True:
            chunk = tl.read()
            if not chunk:
                break
            page += chunk
        check('proxy: an XP-like TLS 1.0 RC4-SHA client reads a current HTTPS site through the bridge (%d bytes, %r)' % (len(page), page[:12]),
              okfin and page[:9] == b'HTTP/1.1 ' and 'MODERNTLS www.wikipedia.org' in open(_plog).read())
    except Exception as e:
        check('proxy: TLS 1.0 bridge (%r)' % e, False); print(open(_plog).read()[-800:])
    log_text = open(_plog).read()
    check('proxy log has BLOCKED and DENIED lines', 'BLOCKED malware.testing.google.test' in log_text and 'DENIED example.com:25' in log_text)
else:
    print('SKIP proxy tests: no openssl on PATH')
# 6v: the read-only Downloads share and the downloads watcher
import hvshare
_c = hvshare.config('windows-xp', 40123)
check('share: read only, guest, loopback only, its own port', all(x in _c for x in (
    'read only = yes', 'guest ok = yes', 'interfaces = lo', 'bind interfaces only = yes', 'smb ports = 40123',
    'path = /hv/downloads/windows-xp', 'load printers = no', 'follow symlinks = no')))
_tmp = tempfile.mkdtemp()
hvshare.ROOT = os.path.join(_tmp, 'share')
_srcd = os.path.join(_tmp, 'dl')
os.makedirs(_srcd)
hvshare.SOURCES = (_srcd,)
open(os.path.join(_srcd, 'before.txt'), 'w').write('old')
class _M:
    id, name = 'windows-xp', 'Windows XP'
_said6v = []
_w = hvshare.Watcher.__new__(hvshare.Watcher)
_w.target, _w.say, _w.log, _w.seen = (lambda: _M()), (lambda en, pl: _said6v.append(en)), (lambda t: None), {}
_w.scan = lambda p: ('Eicar-Test-Signature', None) if 'eicar' in p else (None, None)
_w.seen[os.path.join(_srcd, 'before.txt')] = 'old'
open(os.path.join(_srcd, 'good.txt'), 'w').write('hello')
open(os.path.join(_srcd, 'eicar.com'), 'w').write('x')
open(os.path.join(_srcd, 'big.iso.part'), 'w').write('x')
_w.once(); _w.once()
_got = sorted(os.listdir(os.path.join(hvshare.ROOT, 'windows-xp')))
check('share: a clean download reaches the machine, malware and partial files do not, old files stay', _got == ['good.txt'])
check('share: spoken for both', any('good.txt downloaded, no malware found' in s for s in _said6v) and any('eicar.com contains Eicar-Test-Signature' in s for s in _said6v))
_mx = hvmachine.Machine.__new__(hvmachine.Machine)
_mx.id, _mx.dir, _mx.proxy_port, _mx.share_port = 'windows-xp', _tmp, 4000, 4001
_mx.cfg = dict(name='Windows XP', template='winxp', secure=True, nic='rtl8139', disks=[], internet=False)
_cmd = ' '.join(_mx.command(False))
check('share: guestfwd 445 and 139 to the share, even with internet off', 'guestfwd=tcp:10.0.2.100:445-cmd:' in _cmd and '10.0.2.100:139-cmd:' in _cmd and ':3128' not in _cmd)
# 6ze: every setting in one place
import hvsettings, types
_ch = types.SimpleNamespace(rate=12, pitch=9, volume=8, profile=1, format_flags=252 | 1, synth='tvox', synth_voice='')
_sp = os.path.join(d, 'settings.json')
_st = hvsettings.Store(_ch, _sp, fresh=lambda: types.SimpleNamespace(rate=7, pitch=10, volume=8, profile=2, format_flags=252))
check('6ze first start takes Charon\'s values; defaults ON for guests without their own reader',
      (_st.get('rate'), _st.get('profile'), _st.get('format_voice'), _st.get('auto_read'), _st.get('key_echo'), _st.get('tracking'),
       _st.get('read_boot'), _st.get('read_loader')) == (12, 1, True, True, True, True, True, True))
_st.set('rate', 15)
_st.set('key_echo', False, 'windows-xp')
_st2 = hvsettings.Store(_ch, _sp)
check('6ze own values kept; a per-machine exception wins only for that machine and is named',
      _st2.get('rate') == 15 and _st2.get('key_echo', 'windows-xp') is False and _st2.get('key_echo', 'other') is True
      and 'exception' in _st2.label('key_echo', lambda e, p: e, 'windows-xp') and _ch.rate == 15)
_st.back_to_charon()
check("6ze back to Charon's settings: Charon's values again, exceptions and own items stay",
      _st.get('rate') == 7 and _st.get('profile') == 2 and _st.get('key_echo', 'windows-xp') is False and _ch.rate == 7)
_st.clear('key_echo', 'windows-xp')
check('6ze exception cleared -> general value; every setting has English and Polish',
      _st.get('key_echo', 'windows-xp') is True and all(i.get('en') and i.get('pl') for i in hvsettings.SPEC.values()))
import hvvoice
check('loudness: every voice to RMS 0.10 with peak <= 0.9; unknown voice 1.0',
      abs(hvvoice.loudness('rhvoice-Michał') * 0.103 - 0.10) < 0.002 and hvvoice.loudness('espeak') * 0.999 <= 0.9 + 1e-9
      and hvvoice.loudness('nothing') == 1.0)
import ast, glob
_dups = []
for _f in glob.glob(os.path.join(os.path.dirname(hvlinda_path := hvmachine.__file__), '*.py')):
    for _cls in [n for n in ast.walk(ast.parse(open(_f, encoding='utf-8').read())) if isinstance(n, ast.ClassDef)]:
        _names = [n.name for n in _cls.body if isinstance(n, ast.FunctionDef) and not n.decorator_list]   # property setters share a name
        _dups += ['%s.%s' % (_cls.name, n) for n in set(_names) if _names.count(n) > 1]
check('no method defined twice in a class (session 9: a second settings_menu hid the new Hypervisor settings) %s' % _dups, not _dups)
import hvsecure as _hs, urllib.parse as _up
_cu = _hs.secure_cmd('xp', [], False, updates=True)
_c7u = _hs.secure_cmd('7', [], False, updates=True)
check('6zd update channel: AU download+notify, status task hourly + at start as System, XP antivirus nag off with the reason; not without updates',
      'AUOptions /t REG_DWORD /d 3' in _cu and '/sc hourly /ru System' in _cu and '/sc onstart /ru System' in _cu
      and 'AntiVirusDisableNotify' in _cu and 'AntiVirusDisableNotify' not in _c7u and 'CHARONWU.VBS' in _c7u
      and 'CHARONWU' not in _hs.secure_cmd('xp', [], False) and '10.0.2.100:3128' in _hs.UPDATE_STATUS_VBS
      and _cu.index('update status') < _cu.index('wuauclt /detectnow') < _cu.index('wscript.exe //B "%SystemRoot%'))
# the proxy's side: a report as the VBS sends it (UTF-8 percent-encoded Polish title) -> updates.json + the sentence
import hvproxy as _hp, json, ast
_chv_src = open(os.path.join(os.path.dirname(hvmachine.__file__), 'charonhv.py'), encoding='utf-8').read()
_pd = tempfile.mkdtemp()
_px = _hp.Proxy.__new__(_hp.Proxy)
_px.mdir, _px.logpath = _pd, os.path.join(_pd, 'p.log')
_resp = _px.update_report('/report?pending=2&installed=41&last=2026-11-25&titles=' + _up.quote('Aktualizacja zabezpieczeń KB4012598|x|'))
_uj = json.load(open(os.path.join(_pd, 'updates.json'), encoding='utf-8'))
_line = open(_px.logpath, encoding='utf-8').read().split(' ', 2)
_chv_ns = {'json': json}
exec(compile(ast.Module(body=[n for n in ast.parse(_chv_src).body if isinstance(n, ast.FunctionDef) and n.name == 'update_status_text'],
                        type_ignores=[]), 'ust', 'exec'), _chv_ns)
_T = lambda en, pl: en
_sent = _chv_ns['update_status_text']('Windows XP', _line[2].strip(), _T)
check('6zd spoken update status from the guest report: %r' % _sent,
      _resp.startswith(b'HTTP/1.0 200') and _uj['pending'] == 2 and _uj['titles'][0] == 'Aktualizacja zabezpieczeń KB4012598'
      and _line[1] == 'UPDATES' and _sent.startswith('Windows XP: 2 updates waiting in Legacy Update, first: Aktualizacja')
      and 'Last installed 2026-11-25' in _sent
      and _chv_ns['update_status_text']('XP', json.dumps({'pending': 0}), _T) == 'XP is up to date through Legacy Update.'
      and 'error 80072EFD' in _chv_ns['update_status_text']('XP', json.dumps({'error': '80072EFD'}), _T))
import socket as _so
_a, _b = _so.socketpair()
_b.sendall(b'7654321')
_got = _hp.Proxy.read_body(_a, b'Host: x\r\nContent-Length: 10\r\n\r\nabc')
_a.close(); _b.close()
check('proxy forwards the WHOLE request body (Windows Update sends it after the headers: 0x8024400A before)',
      _got == b'abc7654321' and 'body = self.read_body(c, rest)\n            req =' in open(_hp.__file__, encoding='utf-8').read())
# session 11 FIX: the proxy must NOT strip Accept-Encoding: xpress. WUA asks for xpress and expects an xpress-encoded
# GetConfig reply; stripping it made Legacy Update (Cloudflare, Vary: accept-encoding) return plain XML, which WUA
# rejected with 0x8024400A (soap client error 10) and retried forever. Measured live: forwarding it stops the loop.
_px_src = open(_hp.__file__, encoding='utf-8').read()
check('6zd FIX: proxy forwards Accept-Encoding: xpress to Legacy Update (not stripped; the 0x8024400A cause)',
      "b'xpress' in h.lower()" not in _px_src
      and "not h.lower().startswith((b'proxy-connection:', b'connection:', b'keep-alive:'))]" in _px_src
      and 'WUA gets the xpress reply it expects' in _px_src and 'log_asmx_req' in _px_src)
import hvretro as _hvr
_md = tempfile.mkdtemp()
_hvr.retro_save(types.SimpleNamespace(dir=_md), {'simple': False, 'mail': {'imap': 'x'}})
_mach = types.SimpleNamespace(id='old-xp', dir=_md, cfg={'internet': False}, save=lambda: None)
_st3 = hvsettings.Store(_ch, os.path.join(_md, 's.json'))
_App = types.SimpleNamespace(store=_st3, SWITCHES=None)
# charonhv.py itself needs the hypervisor's devices to import: only App.apply_switches is taken out of it
_chv_src = open(os.path.join(os.path.dirname(hvmachine.__file__), 'charonhv.py'), encoding='utf-8').read()
_cls_node = [n for n in ast.parse(_chv_src).body if isinstance(n, ast.ClassDef) and n.name == 'App'][0]
_fn = [n for n in _cls_node.body if isinstance(n, ast.FunctionDef) and n.name == 'apply_switches'][0]
_sw = [n for n in _cls_node.body if isinstance(n, ast.Assign) and getattr(n.targets[0], 'id', '') == 'SWITCHES'][0]
_ns = {'hvretro': _hvr}
exec(compile(ast.Module(body=[_sw, _fn], type_ignores=[]), 'apply_switches', 'exec'), _ns)
_App.SWITCHES = _ns['SWITCHES']
_ns['apply_switches'](_App, _mach)
_r3 = _hvr.retro_cfg(_mach)
_st3.set('scan_downloads', False)
_ns['apply_switches'](_App, _mach)
_r4 = _hvr.retro_cfg(_mach)
check('6ze switches moved: old per-machine values become exceptions once; values in force written for the proxy at start',
      _st3.exception('internet', 'old-xp') and _st3.exception('simple_web', 'old-xp') and not _st3.exception('scan_downloads', 'old-xp')
      and _mach.cfg['internet'] is False and _r3['simple'] is False and _r3['scan'] is True and _r3['mail'] == {'imap': 'x'}
      and _r4['scan'] is False and _st3.get('internet') is True)
_fp = tempfile.mkdtemp()
os.makedirs(os.path.join(_fp, '4242'))
_stat_line = lambda st, t: '4242 (qemu-system-x86) %s 1 4242 4242 0 -1 4194560 0 0 0 0 1 2 0 0 20 0 5 0 %d 0 0\n' % (st, t)
open(os.path.join(_fp, '4242', 'stat'), 'w').write(_stat_line('S', 777))
_ad = hvmachine.Adopted(4242, _fp)
_ra = [hvmachine.Adopted.start_time(4242, _fp), _ad.poll()]
open(os.path.join(_fp, '4242', 'stat'), 'w').write(_stat_line('S', 999))      # the pid reused by another process
_ra += [_ad.poll()]
_ad2 = hvmachine.Adopted(4242, _fp)
open(os.path.join(_fp, '4242', 'stat'), 'w').write(_stat_line('Z', 999))
_ra += [_ad2.poll()]
_src_hm = open(hvmachine.__file__, encoding='utf-8').read()
_src_cv = open(os.path.join(os.path.dirname(hvmachine.__file__), 'charonhv.py'), encoding='utf-8').read()
check('menu crash keeps guests: adopted QEMU followed by pid + start time (reuse/zombie = ended); QEMU and proxy in '
      'their own sessions; restarted menu reattaches and says so; SIGTERM is a clean end %r' % _ra,
      _ra == [777, None, 0, 0] and 'cwd=self.dir,\n                                     start_new_session=True)' in _src_hm
      and "start_new_session=True)     # outlives" in _src_cv and 'self.reattached, self.orphans = self.reattach()' in _src_cv
      and 'Still running, taken back' in _src_cv and 'signal.SIGTERM, clean_exit' in _src_cv and 'qemu_start' in _src_hm)
import hvlisten, struct as _struct
_sb = hvlisten.StepBack()
_r = [_sb.update(0.0, 100), _sb.update(0.3, 101), _sb.update(0.5, 102), _sb.stepped]
for _t in (110, 111, 112):
    _sb.key(_t)
_r += [_sb.update(0.0, 115), _sb.update(0.0, 130.5), _sb.stepped]
_sb.update(0.4, 140); _sb.text_screen()
_r += [_sb.stepped, hvlisten.peak16(_struct.pack('<4h', 0, 100, -16384, 5)) == 0.5]
check('6ze step back by HEARING the guest reader: sound -> back (once); 3 keys + 8 s silence -> reads; text/reset -> reads %r' % _r,
      _r == [None, 'back', None, True, None, 'reads', False, False, True])
import hvlinda
_ld = tempfile.mkdtemp()
hvlinda.WANTED, hvlinda.LOG, hvlinda.PIDFILE = (os.path.join(_ld, n) for n in ('w', 'log', 'pid'))
hvlinda.LINDA = os.path.join(_ld, 'no-linda')
hvlinda.mount = lambda: None
_disk = {'used': 10e9, 'free': 5e9}
hvlinda._stat = lambda: (_disk['used'], _disk['free'])
_sp1 = hvlinda.space_problem()
check('Nix route: free space checked first (11.3 GB needed, 5 free) and said in both languages',
      _sp1 and '6.3 more gigabytes' in _sp1[0] and '6,3 gigabajta' in _sp1[1])
open(hvlinda.WANTED, 'w').write('1000 10000000000\n')
open(hvlinda.LOG, 'w').write('=== linda build x\n=== rc 1 after 5 s\n=== linda build y\ncopying path ...\n')
_disk.update(used=14.15e9, free=8e9)
check('Nix route: a build cut by a reboot is resumed (an older attempt\'s rc does not count); what is in the store counts',
      hvlinda.at_start() == 'resume' and hvlinda.result() is None and hvlinda.space_problem() is None
      and hvlinda.progress() == (50, 10) and hvlinda.ended() is None)
open(hvlinda.LOG, 'a').write('=== rc 0 after 1129 s\n')
check('Nix route: the end is said once (marker removed); no resume afterwards',
      hvlinda.ended() == (0, 1129) and hvlinda.ended() is None and hvlinda.at_start() is None and hvlinda.progress() is None)
# session 10: guest sound delay - short by default, per-machine choice, QEMU's defaults only on request; dmix ring small
_ad = [hvmachine.audiodev(1), hvmachine.audiodev(2, 1), hvmachine.audiodev(0, 2), hvmachine.audiodev(0, 9)]
_asc = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'hypervisor', 'overlay', 'etc', 'asound.conf')).read()
_sl = hvsettings.SPEC['sound_latency']
_src = open(os.path.join(os.path.dirname(hvmachine.__file__), 'charonhv.py'), encoding='utf-8').read()
check('session 10 sound delay: low default (48 kHz, 21 ms buffer, 5 ms timer), safe, QEMU defaults; dmix 2048/512; '
      'setting EN+PL with machine exceptions, passed to the start %r' % _ad,
      _ad[0] == 'alsa,id=snd0,out.dev=charonguest1,in.dev=null,timer-period=5000,out.frequency=48000,out.buffer-length=21333,out.period-length=10667'
      and 'buffer-length=42667' in _ad[1] and _ad[2] == 'alsa,id=snd0,out.dev=charonguest,in.dev=null' and _ad[3] == _ad[2].replace(
          'in.dev=null', 'in.dev=null' + hvmachine.SOUND_OPTIONS[0])
      and 'buffer_size 2048' in _asc and 'period_size 512' in _asc and 'buffer_size 8192' not in _asc
      and _sl['machine'] and _sl['default'] == 0 and all(e and p for _, e, p in _sl['choices'])
      and "m.sound_latency = self.store.get('sound_latency', m.id)" in _src and "('choice', val)" in _src)
# session 10: RHVoice kept loaded - the client form only when the server's socket exists; the front end has all modes
import hvvoice as _hvv
_cpp = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'hypervisor', 'rhvoice', 'charonhv-rhvoice.cpp')).read()
_vsrc = open(_hvv.__file__, encoding='utf-8').read()
_tmp_sock = os.path.join(tempfile.mkdtemp(), 'rhv.sock')
check('session 10 RHVoice kept loaded: --serve/--client in the front end (client speaks by itself without a server); '
      'hvvoice uses --client only when the socket exists, starts the server with RHVoice',
      '--serve' in _cpp and 'return local(a[1], a[2], a[3], a[4], a[5], text);' in _cpp and 'MSG_NOSIGNAL' in _cpp
      and "client = ['--client', RHVOICE_SOCK] if os.path.exists(RHVOICE_SOCK) else []" in _vsrc
      and 'rhvoice_server(data)' in _vsrc and (not hasattr(socket, 'AF_UNIX') or _hvv.rhvoice_up(_tmp_sock) is False))
# session 10 (6zd, user question): XP's Security Center sees the hypervisor's protection with its REAL state; nothing
# is registered on Vista and later; the nag is no longer silenced
import hvscan as _hs, hvproxy as _hp
_dbd = tempfile.mkdtemp()
_age0 = _hs.signature_age_days(_dbd)
open(os.path.join(_dbd, 'daily.cld'), 'w').close()
_os_t = time.time() - 5 * 86400
os.utime(os.path.join(_dbd, 'daily.cld'), (_os_t, _os_t))
_age5 = _hs.signature_age_days(_dbd)
_px = _hp.Proxy.__new__(_hp.Proxy); _px.scan = False
_xp = '\n'.join(hvsecure.update_channel('xp'))
_v = hvsecure.UPDATE_STATUS_VBS
check('session 10 Security Center: XP/2003 only (version 5.1./5.2.), state from the proxy (scan/fresh), not constant; '
      'nag no longer silenced; signature age %r %r; off -> %r' % (_age0, _age5, _px.protection()),
      'If Left(v, 4) = "5.1." Or Left(v, 4) = "5.2." Then' in _v and 'a.onAccessScanningEnabled = (InStr(p, "scan=1") > 0)' in _v
      and 'a.productUptoDate = (InStr(p, "fresh=1") > 0)' in _v and hvsecure.AV_GUID in _v
      and 'reg add "HKLM\\SOFTWARE\\Microsoft\\Security Center"' not in _xp and 'NOT watch files inside Windows' in _xp
      and 'Security Center' not in '\n'.join(hvsecure.update_channel('7'))
      and _age0 is None and 4.9 < _age5 < 5.1 and _px.protection() == 'scan=0\nfresh=0\n')
_src = open(os.path.join(os.path.dirname(hvmachine.__file__), 'charonhv.py'), encoding='utf-8').read()
check('session 10 orphans: proxies/share servers nobody owns are ended at a menu start (smbd workers of an owned one '
      'kept); a fresh start drops the old step-back listener; a share without proxy stops with its machine',
      "helper.endswith(b'hvproxy.py')" in _src and 'ppid not in owned' in _src
      and "getattr(self, 'listeners', {}).pop(m.id, None)" in _src and 'if k not in self.proxies]' in _src)
# session 10: a new capture ends every older capture of the QEMU first (27 leaked captures filled /run)
class _FQ:
    def __init__(self):
        self.caps, self.sent = 3, []
    def hmp(self, line, timeout=60):
        self.sent.append(line)
        if line == 'info capture':
            return ''.join('[%d]: Capturing audio(1,16,16000) to x: 0 bytes\r\n' % i for i in range(self.caps))   # QMP.hmp gives text
        if line == 'stopcapture 0':
            self.caps -= 1
        if line.startswith('wavcapture'):
            self.caps += 1
        return ''
_fm = type('FM', (), {})(); _fm.id = 'cap'; _fm.qmp = _FQ()
_cap = hvlisten.Capture(_fm, rundir=tempfile.mkdtemp())
_cap.peak()
check('session 10 capture: the old captures are stopped before a new one starts %r' % _fm.qmp.sent,
      _fm.qmp.caps == 1 and _fm.qmp.sent.count('stopcapture 0') == 3 and _fm.qmp.sent[-1].startswith('wavcapture'))
# 6v-2: Suricata + Emerging Threats Open, network virtual patching in the proxy path
import hvsuricata as _su
_sud = tempfile.mkdtemp()
_su.RULEDIR = os.path.join(_sud, 'rules'); os.makedirs(_su.RULEDIR)
_su.RUN = _sud; _su.CONF = os.path.join(_sud, 'suricata.yaml'); _su.EVE = os.path.join(_sud, 'eve.json')
open(os.path.join(_su.RULEDIR, 'emerging-all.rules'), 'w').write('alert tcp any any -> any any (msg:"x"; sid:1;)\n')
open(os.path.join(_su.RULEDIR, 'charon-local.rules'), 'w').write(_su.SELFTEST_RULE + '\n')
_conf = open(_su.write_conf(('127.0.0.0/8', '10.0.2.0/24'))).read()
check('6v-2 Suricata config: IDS on lo, HOME_NET, ET Open + self-test rule files, eve.json alerts',
      'interface: lo' in _conf and 'HOME_NET: "[127.0.0.0/8,10.0.2.0/24]"' in _conf and 'emerging-all.rules' in _conf
      and 'charon-local.rules' in _conf and 'eve-log' in _conf and '- alert' in _conf)
open(_su.EVE, 'w').write(
    '{"event_type":"stats"}\n'
    '{"event_type":"alert","src_ip":"127.0.0.1","src_port":52000,"dest_ip":"127.0.0.1","dest_port":45839,'
    '"alert":{"signature_id":2024297,"signature":"ET EXPLOIT ETERNALBLUE Exploit M2 MS17-010","action":"allowed"}}\n')
_al, _pos = _su.read_alerts(0)
check('6v-2 eve.json alert parsing: sid, signature, ports, action; non-alert lines ignored %r' % _al,
      len(_al) == 1 and _al[0][0] == 2024297 and 'ETERNALBLUE' in _al[0][1] and _al[0][3] == 52000
      and _al[0][5] == 45839 and _al[0][6] == 'allowed')
check('6v-2 eve.json read position advances (no re-reading)', _su.read_alerts(_pos)[0] == [])
_sursrc = open(os.path.join(os.path.dirname(hvmachine.__file__), 'charonhv.py'), encoding='utf-8').read()
check('6v-2 menu speaks Suricata alerts EN+PL (blocked in IPS / detected in IDS), mapped to the machine by its ports',
      'def poll_suricata' in _sursrc and 'self.poll_suricata()' in _sursrc and 'def suricata_machine' in _sursrc
      and 'hipernadzorca zablokował znany atak' in _sursrc and 'the hypervisor detected a known attack' in _sursrc)
check('6v-2 self-test signature is a labelled benign self-test, not malware',
      'self-test' in _su.SELFTEST_RULE and 'classtype:not-suspicious' in _su.SELFTEST_RULE and _su.SELFTEST_SID == 3900001)
check('6v-2 running() matches Suricata\'s "Suricata-Main" thread name case-insensitively (session-13 fix: else the menu never spoke an alert)',
      ".read().lower()" in open(_su.__file__, encoding='utf-8').read())
# 6x/6z/6za/6zb/6zg: the compatibility layer (hvcompat) - PE detection, route, spoken line, the per-program switch
import json as _json, struct as _struct
import hvcompat as _cp


def _build_pe(os_major=6, os_minor=0, sub_major=6, sub_minor=0, subsystem=2, dlls=()):
    pe_off, opt_size, raw_base, va_base = 0x80, 0xE0, 0x400, 0x1000
    desc_len = (len(dlls) + 1) * 20
    cur = va_base + desc_len
    name_rvas, names = [], b''
    for d in dlls:
        name_rvas.append(cur)
        names += d.encode() + b'\0'
        cur += len(d) + 1
    desc = b''
    for i in range(len(dlls)):
        desc += _struct.pack('<IIIII', 0, 0, 0, name_rvas[i], 0)
    desc += b'\0' * 20
    idata = desc + names
    coff = pe_off + 4
    sec_off = coff + 20 + opt_size
    size = max(raw_base + len(idata), sec_off + 40 + 64)
    buf = bytearray(size + 16)
    buf[0:2] = b'MZ'
    _struct.pack_into('<I', buf, 0x3C, pe_off)
    buf[pe_off:pe_off + 4] = b'PE\0\0'
    _struct.pack_into('<HH', buf, coff, 0x14C, 1 if dlls else 0)
    _struct.pack_into('<H', buf, coff + 16, opt_size)
    opt = coff + 20
    _struct.pack_into('<H', buf, opt, 0x10B)
    for off, val in ((40, os_major), (42, os_minor), (48, sub_major), (50, sub_minor), (68, subsystem)):
        _struct.pack_into('<H', buf, opt + off, val)
    if dlls:
        _struct.pack_into('<II', buf, opt + 96 + 8, va_base, len(desc))
        s = sec_off
        buf[s:s + 8] = b'.idata\0\0'
        _struct.pack_into('<IIII', buf, s + 8, max(len(idata), 0x200), va_base, len(idata), raw_base)
        buf[raw_base:raw_base + len(idata)] = idata
    return bytes(buf)


_pe10 = _build_pe(os_major=10, os_minor=0, sub_major=10, sub_minor=0)
_pe7 = _build_pe(os_major=6, os_minor=1, sub_major=6, sub_minor=1)
_peimp = _build_pe(dlls=['KERNEL32.dll', 'api-ms-win-core-synch-l1-2-0.dll'])
check('compat parse_pe reads the subsystem version and imports',
      _cp.parse_pe(_pe10)['subsystem_major'] == 10 and 'api-ms-win-core-synch-l1-2-0.dll' in _cp.parse_pe(_peimp)['imports'])
check('compat needs_newer: a Windows-10 program fails on XP and on 7',
      _cp.needs_newer_windows(_cp.parse_pe(_pe10), 'xp')[0] and _cp.needs_newer_windows(_cp.parse_pe(_pe10), '7')[0])
check('compat needs_newer: a Windows-7 program is fine on 7 but not on XP',
      (not _cp.needs_newer_windows(_cp.parse_pe(_pe7), '7')[0]) and _cp.needs_newer_windows(_cp.parse_pe(_pe7), 'xp')[0])
check('compat needs_newer: a missing modern import (api-ms-win-*) is detected on 7',
      _cp.needs_newer_windows(_cp.parse_pe(_peimp), '7')[0])
check('compat needs_newer: an XP-missing kernel32 function in the bytes is detected on XP',
      _cp.needs_newer_windows(_cp.parse_pe(_build_pe(os_major=5, os_minor=1, sub_major=5, sub_minor=1)), 'xp', b'...GetTickCount64...')[0])
check('compat route: 7/8/8.1 in-guest (guest reader), XP/Vista hypervisor (Cthulhu default, NVDA fallback)',
      _cp.route_for('7') == 'in-guest' and _cp.route_for('8') == 'in-guest' and _cp.route_for('8.1') == 'in-guest'
      and _cp.route_for('xp') == 'hypervisor' and _cp.route_for('vista') == 'hypervisor' and _cp.route_for('98') is None
      and _cp.reader_of('in-guest') == 'guest' and _cp.reader_of('hypervisor') == 'cthulhu'
      and _cp.reader_of('hypervisor', 'nvda-in-wine') == 'nvda-in-wine')
_sr_en = _cp.spoken_route('hypervisor', False, 'Prog.exe')                          # default reader (Cthulhu)
_sr_nvda = _cp.spoken_route('hypervisor', False, 'Prog.exe', reader='nvda-in-wine')  # fallback reader
_sr_pl = _cp.spoken_route('in-guest', True, 'Prog.exe')
check('compat spoken line: Wine route says Cthulhu by default / NVDA as fallback (6zh), in Charon\'s voice; in-guest says the guest\'s reader (6zb)',
      'through Wine in the hypervisor, read by Cthulhu in Charon\'s voice' in _sr_en
      and 'read by NVDA in Charon\'s voice' in _sr_nvda
      and 'czyta go twój czytnik ekranu' in _sr_pl and 'nakładką zgodności' in _sr_pl)
_cd = tempfile.mkdtemp()
_pf = os.path.join(_cd, 'Prog.exe')
open(_pf, 'wb').write(_pe10)
_de = _cp.decide(_pf, 'xp', _cd)
check('compat decide: Windows-10 program on XP -> apply, hypervisor route, Cthulhu reader (default)',
      _de['apply'] and _de['route'] == 'hypervisor' and _de['reader'] == 'cthulhu' and _de['needs_newer'])
_cp.set_off(_cd, _pf, True)
check('compat on by default, switchable off per program (6za): off -> not applied; on again -> applied',
      (not _cp.decide(_pf, 'xp', _cd)['apply']) and _cp.decide(_pf, 'xp', _cd)['off']
      and (_cp.set_off(_cd, _pf, False) or _cp.decide(_pf, 'xp', _cd)['apply']))
_vbs7 = _cp.guest_launcher_vbs('7')
_vbsxp = _cp.guest_launcher_vbs('xp')
check('compat guest launcher: 7 enables VxKex via IFEO VerifierDlls and launches; XP writes the exchange request',
      'VerifierDlls' in _vbs7 and 'FindVxKexDll' in _vbs7 and 'CHARON-COMPAT.REQ' in _vbsxp and 'ExchangeDrive' in _vbsxp
      and 'MajorSubsystemVersion' in _vbs7)
check('compat guest shell verb copies COMPAT.VBS to %SystemRoot% and registers "Run with Charon compatibility"',
      any('CharonCompat' in l and 'command' in l and 'CHARON-COMPAT.VBS' in l for l in _cp.guest_shell_reg_cmd()))
# the manifest now carries the VxKex NEXT fork for 7/8/8.1 (6zb/6zf); XP points at the Wine route
_man = _json.load(open(os.path.join(os.path.dirname(hvmachine.__file__), 'manifests.json'), encoding='utf-8'))
_byid = {e['id']: e for lst in _man.values() for e in lst}
check('manifest: VxKex NEXT 1.2.3.2466 for Windows 7, 8 and 8.1 with the fetched URL, size and SHA-256',
      all(_byid[i]['url'].endswith('KexSetup_Release_1_2_3_2466.exe') and _byid[i]['bytes'] == 5025016
          and _byid[i]['sha256'] == 'e8fa07f6919f48bd3c381dedcbd54dabc147c824522f1ac2a8fc60366f43f535'
          and 'YuZhouRen86/VxKex-NEXT' in _byid[i]['page'] for i in ('vxkex-7', 'vxkex-8', 'vxkex-8.1')))
check('manifest: VxKex NEXT recorded as having NO licence, fetched at the user\'s request, not bundled',
      'NO licence' in _byid['vxkex-7']['notes'] and 'vxiiduu' in _byid['vxkex-7']['notes']
      and '406b0f87b8533b76015f8c93037571307c5185ef' in _byid['vxkex-7']['notes'])
check('manifest: XP has no in-guest shim and points at the hypervisor Wine route',
      'Wine route' in _byid['vxkex-xp']['notes'] and _byid['vxkex-xp']['url'] == '')
_lic = open(os.path.join(HERE, '..', 'src', 'hypervisor', 'LICENSES.txt'), encoding='utf-8').read()
check('LICENSES: VxKex NEXT (no licence), Wine LGPL, NVDA GPL-2, eSpeak NG GPL-3, wineinstall technique no licence',
      'VxKex NEXT' in _lic and 'Wine (Alpine community package' in _lic and 'LGPL-2.1' in _lic
      and 'NVDA' in _lic and 'GPL-2.0' in _lic and 'eSpeak NG' in _lic and 'dnl-nash/wineinstall' in _lic)
_chsrc = open(os.path.join(os.path.dirname(hvmachine.__file__), 'charonhv.py'), encoding='utf-8').read()
check('menu: "Run a Windows program with Charon compatibility" wired into the machine menu, hvcompat imported',
      'import hvcompat' in _chsrc and "'compat'" in _chsrc and 'def compat_run' in _chsrc and 'def _compat_wine' in _chsrc)
# r410 session 14: the Cthulhu Wine-route runtime (6zh default) is built and wired; run() dispatches cthulhu/nvda
_cpsrc = open(os.path.join(os.path.dirname(hvmachine.__file__), 'hvcompat.py'), encoding='utf-8').read()
check('6zh runtime: run() dispatches to run_cthulhu by default, NVDA-in-prefix as the fallback',
      hasattr(_cp, 'run_cthulhu') and hasattr(_cp, 'build_cthulhu') and hasattr(_cp, '_run_nvda')
      and _cp.WINE_READER_DEFAULT == 'cthulhu' and "reader == 'cthulhu'" in _cpsrc
      and 'return _run_nvda(' in _cpsrc)
check('6zh runtime: Cthulhu pinned tag 2026.08.06 / commit recorded; helper built with Winegcc; sd_charon voices',
      _cp.CTHULHU_TAG == '2026.08.06' and _cp.CTHULHU_COMMIT == 'a02388c7fe5c79605b386a379eabc339d2e2c5fc'
      and _cp.CTHULHU_HELPER.endswith('cthulhu-wine-access.exe') and 'sd_charon' in _cpsrc
      and 'winelib-toolchain.cmake' in _cpsrc)
check('6zh runtime: the menu passes the chosen reader to the Wine route',
      "reader=d['reader']" in _chsrc and 'def _compat_wine(self, path, name, reader' in _chsrc)
check('LICENSES: Cthulhu route records tag 2026.08.06, the built commit, Winegcc and prism MPL-2.0',
      '2026.08.06' in _lic and 'a02388c7fe5c79605b386a379eabc339d2e2c5fc' in _lic
      and 'Winegcc' in _lic and 'prism' in _lic and 'MPL-2.0' in _lic)
# 6za: a modern browser (Supermium) in the guest tunnels a DRM site directly; the proxy offers the modern-browser
# mode once per site (NEEDMODERN), without blocking the tunnel. Deduped per proxy run; non-DRM host stays silent.
import hvproxy
_pd = tempfile.mkdtemp()
open(os.path.join(_pd, 'block.txt'), 'w').write('')
_px = hvproxy.Proxy(0, _pd, os.path.join(_pd, 'proxy.log'), os.path.join(_pd, 'block.txt'))
_px.offer_modern('www.netflix.com')
_px.offer_modern('www.netflix.com')   # second time: deduped, no new line
_px.offer_modern('example.com')       # not DRM/video: silent
_px.offer_modern('www.youtube.com')   # video site: offered too
_plog = open(os.path.join(_pd, 'proxy.log')).read()
check('6za: Supermium DRM tunnel offers modern mode once per site (NEEDMODERN netflix drm), youtube video, dedup, non-DRM silent',
      _plog.count('NEEDMODERN netflix.com|drm|') == 1 and 'NEEDMODERN youtube.com|video|' in _plog
      and 'example.com' not in _plog and _plog.count('NEEDMODERN') == 2)

# 6w: the hypervisor-side in-memory micropatch engine (hvmicropatch). Pure logic tested over a fake gdb backed by
# a byte buffer (no QEMU): signature scan, off-by-default, exact-build verify/mismatch-refused, apply+flush,
# spoken list, revert. The one shipped definition (MS17-010) has no exact-build signature yet, so it is NOT applied.
import hvmicropatch
class _FakeGdb:
    def __init__(self, base, data):
        self.base, self.mem, self.resynced = base, bytearray(data), 0
    def read_mem(self, addr, length):
        o = addr - self.base
        return bytes(self.mem[o:o + length])
    def write_mem(self, addr, data):
        o = addr - self.base
        self.mem[o:o + len(data)] = data
        return True
    def resync(self):
        self.resynced += 1
_sector = bytearray(32)
_sector[14:17] = b'\xB0\x56\xC3'                 # decide(): mov al,'V'; ret - the "vulnerable" path, at 0x7c0e
_fg = _FakeGdb(0x7c00, _sector)
_flushes = []
_said = []
_demo = {'id': 'demo', 'name': 'demo guard', 'name_pl': 'x', 'os': ('xp',),
         'signature': b'\xB0\x56\xC3', 'before': b'\xB0\x56\xC3', 'guard': b'\xB0\x53\xC3', 'window': (0x7c00, 0x7e00)}
_eng = hvmicropatch.Micropatch(_fg, say=lambda s, **k: _said.append(s), T=lambda en, pl: en, flush=lambda: _flushes.append(1))
check('6w micropatch: off by default (not enabled -> not applied)', _eng.apply(dict(_demo, id='a'), {}) == (False, 'off'))
check('6w micropatch: wrong exact build refused + said aloud', _eng.apply(dict(_demo, id='b', before=b'\xB0\x99\xC3'), {'b': True}) == (False, 'mismatch') and any('do not match' in s for s in _said))
_ok, _why = _eng.apply(_demo, {'demo': True})
check('6w micropatch: applied, guard written, flush called, exact byte in buffer',
      (_ok, _why) == (True, 'applied') and bytes(_fg.mem[14:17]) == b'\xB0\x53\xC3' and _flushes == [1] and _fg.resynced == 1)
check('6w micropatch: spoken list names the active patch', 'demo guard' in _eng.spoken_list() and '1 micropatch' in _eng.spoken_list())
check('6w micropatch: revert restores the original bytes', _eng.revert('demo') and bytes(_fg.mem[14:17]) == b'\xB0\x56\xC3' and 'No micropatches' in _eng.spoken_list())
check('6w micropatch: a definition with no exact-build signature is not applied',
      hvmicropatch.Micropatch(_fg, say=lambda s, **k: None).apply(hvmicropatch.PATCHES[0], {hvmicropatch.PATCHES[0]['id']: True}) == (False, 'no-signature'))
check('6w micropatch: not-found when the signature is absent in the window',
      hvmicropatch.Micropatch(_FakeGdb(0x7c00, bytearray(32)), say=lambda s, **k: None).apply(_demo, {'demo': True}) == (False, 'not-found'))
check('6w micropatch: patches_for filters by OS (MS17-010 applies to xp..8.1, not to 9x)',
      hvmicropatch.patches_for('xp') and not hvmicropatch.patches_for('win98'))
_chv = open(os.path.join(HERE, '..', 'src', 'hypervisor', 'overlay', 'usr', 'lib', 'charonhv', 'charonhv.py'), encoding='utf-8').read()
check('6w micropatch: menu item + handler + snapshot-first + gdbserver + QMP flush wired into charonhv',
      "'micropatch'" in _chv and 'def micropatch_menu' in _chv and 'def apply_micropatches' in _chv
      and 'savevm before-micropatch' in _chv and "hmp('gdbserver tcp::%d'" in _chv
      and "flush=lambda: (m.qmp.cmd('stop'), m.qmp.cmd('cont'))" in _chv)

print('%d failed' % fails)
sys.exit(1 if fails else 0)
