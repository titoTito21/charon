# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: network virtual patching with Suricata + Emerging Threats Open (6v-2).

A guest's web traffic all passes through the hypervisor's filtering proxy (hvproxy) and its SMB/mail relays, which
appear as connections on the loopback interface (the guestfwd nc <-> proxy sockets). Suricata (GPL-2.0, OISF; Alpine
package `suricata`, fetched the first time virtual patching is switched on) watches `lo` in IDS mode with the
Emerging Threats Open rule set and writes alerts to eve.json. The menu reads eve.json; when a rule fires on a guest's
flow it is spoken ("the hypervisor blocked a known attack ...") and hvproxy adds the source/host to that machine's
block list, so the exploit traffic is stopped before it reaches the old Windows (EternalBlue/MS17-010, BlueKeep, old
browser/SMB exploits). Nothing of the guest is changed; this protection does not depend on the old OS.

ET Open licence (rules.emergingthreats.net/.../LICENSE): sids 2000000-2799999 are BSD, sids 1-3464 and
100000000-100000908 are GPLv2; both are redistributable. The MS17-010 and BlueKeep rules are in the BSD range.
The rule set is pinned by its own version.txt and md5; a mismatch is reported, never silently accepted."""
import json, os, subprocess, time, urllib.request

import hvpkg
from hvvoice import log

PKGS = ('suricata',)
SURDIR = '/hv/suricata'                 # persists on the data partition (P2)
RULEDIR = SURDIR + '/rules'
RUN = '/run/charonhv'
CONF = RUN + '/suricata.yaml'
EVE = RUN + '/suricata-eve.json'
PIDF = RUN + '/suricata.pid'
IFACE = 'lo'
# ET Open for the installed Suricata major.minor (research: suricata-8.0). The downloader picks the version from
# `suricata -V`; the URL and its .md5 are both fetched and checked.
ET_BASE = 'https://rules.emergingthreats.net/open/suricata-%s/emerging.rules.tar.gz'
# A clearly-labelled self-test signature (NOT malware): a benign token the proof sends through the proxy path so the
# whole chain (capture -> rule -> eve.json -> spoken -> block) can be shown safely. The real protection is ET Open.
SELFTEST_TOKEN = 'CHARON-IPS-SELFTEST-PATTERN'
SELFTEST_SID = 3900001
SELFTEST_RULE = ('alert http any any -> any any (msg:"CHARON HV IPS self-test pattern"; '
                 'content:"%s"; classtype:not-suspicious; sid:%d; rev:1;)' % (SELFTEST_TOKEN, SELFTEST_SID))


def installed():
    return hvpkg.installed(PKGS)


def suricata_version():
    try:
        r = subprocess.run(['suricata', '-V'], capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.TimeoutExpired):
        return None
    for tok in r.stdout.replace('\n', ' ').split():
        parts = tok.split('.')
        if len(parts) >= 2 and parts[0].isdigit() and parts[1].isdigit():
            return parts[0] + '.' + parts[1]
    return None


def rules_present():
    try:
        return any(f.endswith('.rules') and f != 'charon-local.rules'
                   and os.path.getsize(os.path.join(RULEDIR, f)) > 0 for f in os.listdir(RULEDIR))
    except OSError:
        return False


def rules_version():
    try:
        return open(os.path.join(RULEDIR, 'version.txt')).read().strip()
    except OSError:
        return None


def _get(url, timeout=120):
    req = urllib.request.Request(url, headers={'User-Agent': 'charon-hv'})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def update_rules(force=False):
    """Fetch and verify ET Open for the installed Suricata version. (None) ok, (reason) not. Once a day at most."""
    ver = suricata_version()
    if not ver:
        return 'Suricata is not installed'
    os.makedirs(RULEDIR, exist_ok=True)
    stamp = os.path.join(SURDIR, '.updated')
    if not force and rules_present() and os.path.exists(stamp) and time.time() - os.path.getmtime(stamp) < 86400:
        return None
    url = ET_BASE % ver
    try:
        want = _get(url + '.md5').decode().split()[0]
        data = _get(url)
    except Exception as e:                                   # noqa: BLE001  (urllib/socket errors)
        if rules_present():
            log('ET Open not refreshed (%s); the rules from before are used' % e)
            return None
        return str(e)
    import hashlib
    have = hashlib.md5(data).hexdigest()
    if have != want:
        return 'ET Open checksum %s, server says %s' % (have, want)
    import io, tarfile
    try:
        with tarfile.open(fileobj=io.BytesIO(data)) as t:
            for mem in t.getmembers():
                if not mem.isfile():
                    continue
                name = os.path.basename(mem.name)
                if name.endswith(('.rules', '.config', '.txt', '.map')) or name in ('version.txt',):
                    open(os.path.join(RULEDIR, name), 'wb').write(t.extractfile(mem).read())
    except (tarfile.TarError, OSError) as e:
        return 'ET Open archive: %s' % e
    open(os.path.join(RULEDIR, 'charon-local.rules'), 'w').write(SELFTEST_RULE + '\n')
    open(stamp, 'w').close()
    log('ET Open %s updated for Suricata %s (%d bytes)' % (rules_version(), ver, len(data)))
    return None


def _system_conf(name):
    """A classification/reference config shipped with the Alpine package, or None."""
    for d in ('/etc/suricata', '/usr/share/suricata'):
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None


def write_conf(home_nets=('127.0.0.0/8', '10.0.2.0/24')):
    """A small, self-contained suricata.yaml: IDS on `lo`, ET Open + the self-test rule, eve.json alerts."""
    os.makedirs(RUN, exist_ok=True)
    classif = _system_conf('classification.config') or os.path.join(RULEDIR, 'classification.config')
    refs = _system_conf('reference.config') or os.path.join(RULEDIR, 'reference.config')
    try:
        present = sorted(f for f in os.listdir(RULEDIR) if f.endswith('.rules'))
    except OSError:
        present = []
    if 'charon-local.rules' in present:         # the self-test rule loads last
        present.remove('charon-local.rules')
        present.append('charon-local.rules')
    y = []
    y.append('%YAML 1.1')
    y.append('---')
    # The standard Suricata/ET Open variable set: the Emerging Threats rules reference these address- and port-groups,
    # so all must be defined or "Loading signatures failed". HOME_NET is the hypervisor loopback + the guest NAT range.
    y.append('vars:')
    y.append('  address-groups:')
    y.append('    HOME_NET: "[%s]"' % ','.join(home_nets))
    y.append('    EXTERNAL_NET: "any"')
    for g in ('HTTP_SERVERS', 'SMTP_SERVERS', 'SQL_SERVERS', 'DNS_SERVERS', 'TELNET_SERVERS', 'DC_SERVERS',
              'DNP3_SERVER', 'DNP3_CLIENT', 'MODBUS_CLIENT', 'MODBUS_SERVER', 'ENIP_CLIENT', 'ENIP_SERVER',
              'FTP_SERVERS', 'SSH_SERVERS'):
        y.append('    %s: "$HOME_NET"' % g)
    y.append('    AIM_SERVERS: "$EXTERNAL_NET"')
    y.append('  port-groups:')
    for p, v in (('HTTP_PORTS', '"[80,81,311,383,591,593,901,1220,1414,1741,1830,2301,2381,2809,3037,3128,3702,'
                                '4343,4848,5250,6988,7000,7001,7144,7145,7510,7777,7779,8000,8008,8014,8028,8080,'
                                '8085,8088,8090,8118,8123,8180,8181,8243,8280,8300,8800,8888,8899,9000,9060,9080,'
                                '9090,9091,9443,9999,11371,34443,34444,41080,50002,55555]"'),
                 ('SHELLCODE_PORTS', '"!80"'), ('ORACLE_PORTS', '1521'), ('SSH_PORTS', '22'), ('DNP3_PORTS', '20000'),
                 ('MODBUS_PORTS', '502'), ('FILE_DATA_PORTS', '"[$HTTP_PORTS,110,143]"'), ('FTP_PORTS', '21'),
                 ('GENEVE_PORTS', '6081'), ('VXLAN_PORTS', '4789'), ('TEREDO_PORTS', '3544')):
        y.append('    %s: %s' % (p, v))
    y.append('default-rule-path: %s' % RULEDIR)
    y.append('rule-files:')
    for r in present:
        y.append('  - %s' % r)
    if os.path.exists(classif):
        y.append('classification-file: %s' % classif)
    if os.path.exists(refs):
        y.append('reference-config-file: %s' % refs)
    y.append('default-log-dir: %s' % RUN)
    y.append('stats:')
    y.append('  enabled: no')
    y.append('outputs:')
    y.append('  - eve-log:')
    y.append('      enabled: yes')
    y.append('      filetype: regular')
    y.append('      filename: %s' % EVE)
    y.append('      types:')
    y.append('        - alert')
    y.append('af-packet:')
    y.append('  - interface: %s' % IFACE)
    y.append('    cluster-id: 97')
    y.append('    cluster-type: cluster_flow')
    y.append('    defrag: yes')
    y.append('app-layer:')
    y.append('  protocols:')
    y.append('    http:')
    y.append('      enabled: yes')
    y.append('    smb:')
    y.append('      enabled: yes')
    y.append('logging:')
    y.append('  default-log-level: error')
    y.append('  outputs:')
    y.append('    - file:')
    y.append('        enabled: yes')
    y.append('        filename: %s/suricata.log' % RUN)
    with open(CONF, 'w') as f:
        f.write('\n'.join(y) + '\n')
    return CONF


def running():
    try:
        pid = int(open(PIDF).read().strip())
    except (OSError, ValueError):
        return False
    try:
        os.kill(pid, 0)
        with open('/proc/%d/comm' % pid) as f:
            # Suricata names its main thread "Suricata-Main" (capital S); match case-insensitively, else the live
            # menu's poll_suricata saw running() == False and never spoke any alert (found session 13).
            return 'suricata' in f.read().lower()
    except OSError:
        return False


def start(home_nets=('127.0.0.0/8', '10.0.2.0/24'), wait=60):
    """Start Suricata in IDS mode on `lo`. (None) ready, (reason) not."""
    if running():
        return None
    if not rules_present():
        return 'the rule set is not present'
    try:
        os.remove(PIDF)      # a stale pidfile from a crashed/killed run makes Suricata abort ("pid file exists")
    except OSError:
        pass
    write_conf(home_nets)
    try:
        os.remove(EVE)
    except OSError:
        pass
    try:
        p = subprocess.Popen(['suricata', '-c', CONF, '--af-packet=%s' % IFACE, '--pidfile', PIDF,
                              '-l', RUN], stdin=subprocess.DEVNULL,
                             stdout=open(RUN + '/suricata.out', 'wb'), stderr=subprocess.STDOUT)
    except OSError as e:
        return str(e)
    end = time.time() + wait
    while time.time() < end:
        if running():        # Suricata writes its pidfile only once the engine is up and loaded
            log('suricata ready (ET Open %s)' % rules_version())
            return None
        if p.poll() is not None:
            try:
                return open(RUN + '/suricata.out').read().strip().splitlines()[-1][:200]
            except (OSError, IndexError):
                return 'suricata exited with %s' % p.returncode
        time.sleep(1)
    return None if running() else 'suricata did not come up in %d seconds' % wait


def _engine_ready():
    try:
        return 'Engine started' in open(RUN + '/suricata.out').read()
    except OSError:
        return False


def stop():
    try:
        pid = int(open(PIDF).read().strip())
        os.kill(pid, 15)
    except (OSError, ValueError):
        return
    for _ in range(30):
        if not running():
            break
        time.sleep(0.2)


def read_alerts(pos):
    """New alerts since byte offset pos: ([(sid, signature, src_ip, src_port, dest_ip, dest_port, action)], new_pos).
    `action` is Suricata's own: 'blocked' when it dropped the packet (IPS mode), 'allowed' when it only detected (IDS)."""
    out = []
    try:
        with open(EVE, encoding='utf-8') as f:
            f.seek(pos)
            new = f.read()
            pos = f.tell()
    except OSError:
        return out, pos
    for line in new.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            e = json.loads(line)
        except ValueError:
            continue
        if e.get('event_type') != 'alert':
            continue
        a = e.get('alert', {})
        out.append((a.get('signature_id'), a.get('signature', ''), e.get('src_ip', ''),
                    e.get('src_port', 0), e.get('dest_ip', ''), e.get('dest_port', 0), a.get('action', 'allowed')))
    return out, pos
