# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: the SECURE-USE profile for old Windows guests (answers 6i, 6j, 6k, 6l, 6n).

manifests.json (one list per Windows version, read from each vendor's / project's own page with URL, size, SHA-256,
signer, silent switches and a dated support note) is APPLIED here:
  1. every item of the guest's version is fetched from its own source, checked against its size and SHA-256 and
     named aloud as it arrives (an unofficial one is called unofficial); an item that has no download, or whose
     download fails or does not match, is said with its reason and the rest continues;
  2. the verified files, the hardening steps, the proxy settings and the hypervisor's own TLS certificate go onto a
     read-only FAT32 drive with SECURE.CMD, which the guest runs (Windows Setup's first logon, or the menu types it
     into the Run box of a guest that is already installed); SECURE.CMD logs each step, reads back the firewall,
     services, updates and runtimes into C:\\CHARON-SECURE.TXT and opens it for the guest's screen reader;
  3. the hypervisor side (hvmachine.command + hvproxy): QEMU's user network with restrict=on (NAT only, nothing
     reachable from outside, nothing out except through the proxy), the HTTP/HTTPS proxy with the domain filter and
     modern TLS for the guest's old stack, the shared folder read-only, a snapshot before going online with one-key
     revert, and a spoken warning for a machine that goes online without the profile.
Nothing here bypasses a licence: no POSReady/Embedded registry change, no ESU bypass; Legacy Update's optional
POSReady feature is not selected."""
import hashlib, json, os, re, shutil, subprocess, time, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SECURE = '/hv/secure'
CACHE = '/hv/images/secure-cache'
PROXY_ADDR = '10.0.2.100'
PROXY_PORT = 3128

# template -> manifest version
VERSION = {'winxp': 'xp', 'winvista': 'vista', 'win7': '7', 'win8': '8', 'win81': '8.1', 'win95': '95', 'win98': '98', 'win98se': '98se', 'winme': 'me'}
NINE_X = ('95', '98', '98se', 'me')

GROUPS = [  # (group, English, Polish) - each can be switched off in the wizard's detailed mode
    ('updates', 'updates', 'aktualizacje'),
    ('certificates', 'certificates and TLS', 'certyfikaty i TLS'),
    ('browser', 'a maintained browser', 'utrzymywana przeglądarka'),
    ('security', 'unofficial security fixes', 'nieoficjalne poprawki bezpieczeństwa'),
    ('antimalware', 'anti-malware', 'ochrona przed złośliwym oprogramowaniem'),
    ('compat', 'compatibility layers', 'warstwy zgodności'),
    ('runtime', 'runtimes for programs and games', 'biblioteki dla programów i gier'),
    ('game', 'a free audio game', 'darmowa gra dźwiękowa'),
    ('hardening', 'hardening inside the guest', 'utwardzenie wewnątrz gościa'),
    ('protections', "the hypervisor's protections around it", 'ochrona hipernadzorcy wokół niej'),
    ('improvements', 'optional unofficial improvements (off unless chosen)', 'opcjonalne nieoficjalne ulepszenia (wyłączone, jeśli nie wybrane)'),
]
DEFAULT_GROUPS = [g for g, _, _ in GROUPS if g != 'improvements']   # 6p: improvements only when chosen in detailed mode

# What is said for an item that cannot be installed by itself (no download, or no documented silent switch).
SAY = {
    'tls12': ('Microsoft never released it for desktop XP, so the hypervisor\'s HTTPS proxy speaks modern TLS for it',
              'Microsoft nie wydał go dla zwykłego XP, więc nowoczesny TLS mówi za niego pośrednik HTTPS hipernadzorcy'),
    'antimalware': ('No anti-malware vendor still supports this Windows; none is installed',
                    'Żaden producent ochrony przed złośliwym oprogramowaniem nie wspiera już tego Windows; nic nie instaluję'),
    'dotnet-11sp1': ('Microsoft no longer offers it; most programs for it run on .NET 2.0 from 3.5 SP1',
                     'Microsoft go już nie udostępnia; większość programów dla niego działa na .NET 2.0 z 3.5 SP1'),
    'vcredist-14-27': ('the last for XP; Microsoft gives it only after signing in at my.visualstudio.com; put it in the shared folder yourself',
                       'ostatni dla XP; Microsoft daje go tylko po zalogowaniu na my.visualstudio.com; włóż go sam do folderu wspólnego'),
    'hv-tls-proxy': ('built into the hypervisor and on for this machine: it speaks modern TLS where this Windows cannot',
                     'wbudowany w hipernadzorcę i włączony dla tej maszyny: mówi nowoczesnym TLS tam, gdzie ten Windows nie potrafi'),
    'vxkex': ('there is no clean in-guest compatibility shim for this Windows; programs that need a newer Windows run through the hypervisor\'s Wine route, read by NVDA in Charon\'s voice',
              'nie ma czystej nakładki zgodności wewnątrz tego Windows; programy wymagające nowszego Windows działają przez trasę Wine hipernadzorcy, czyta je NVDA głosem Charona'),
    'esu': ('Extended Security Updates for Windows 7 need your own ESU licence; they are not bypassed',
            'Rozszerzone aktualizacje zabezpieczeń Windows 7 wymagają własnej licencji ESU; nie są obchodzone'),
    'mse': ('Microsoft Security Essentials has ended and is not offered any more', 'Microsoft Security Essentials się skończył i nie jest już udostępniany'),
    '0patch-agent-8': ('0patch has no patches for Windows 8.0', '0patch nie ma poprawek dla Windows 8.0'),
    'ms-trustedr': ('applied by Legacy Update from Microsoft\'s certificate lists', 'stosuje je Legacy Update z list certyfikatów Microsoftu'),
}
NOTE = {   # said after the name of an item that IS installed
    'legacy-update': ('unofficial community project that uses Microsoft\'s own update servers; its POSReady option stays off',
                      'nieoficjalny projekt społeczności, który używa serwerów aktualizacji Microsoftu; jego opcja POSReady zostaje wyłączona'),
    '0patch-agent-7': ('security patches for Windows 7 come only with the paid PRO plan, until January 2028; the free plan has selected zero-day fixes; sign in to your own 0patch account in its console',
                       'poprawki bezpieczeństwa dla Windows 7 są tylko w płatnym planie PRO, do stycznia 2028; darmowy plan ma wybrane poprawki dnia zerowego; zaloguj się na własne konto 0patch w jego konsoli'),
    '0patch-agent': ('very few patches for this Windows, the free plan has selected zero-day fixes; sign in to your own 0patch account in its console',
                     'bardzo mało poprawek dla tego Windows, darmowy plan ma wybrane poprawki dnia zerowego; zaloguj się na własne konto 0patch w jego konsoli'),
}


def lookup(table, iid):
    for k in sorted(table, key=len, reverse=True):
        if iid.startswith(k) or iid.rsplit('-', 1)[0].startswith(k):
            return table[k]
    return None


def manifests():
    d = json.load(open(os.path.join(HERE, 'manifests.json'), encoding='utf-8'))
    for name in ('manifests-9x.json', 'manifests-vista.json', 'manifests-xp-improvements.json', 'manifests-improvements-other.json'):
        p = os.path.join(HERE, name)
        if os.path.exists(p):
            for v, lst in json.load(open(p, encoding='utf-8')).items():
                # 6p: only entries decided "include" (origin and licence confirmed); the others are listed in the results
                d.setdefault(v, []).extend(e for e in lst if e.get('decision', 'include') == 'include')
    return d


def version_of(cfg):
    return cfg.get('windows_version') or VERSION.get(cfg.get('template', ''), '')


def supported(cfg):
    return version_of(cfg) in manifests()


def items(version, arch='x86', groups=None):
    out = []
    for e in manifests().get(version, []):
        if groups is not None and e['group'] not in groups:
            continue
        if arch == 'x86' and re.search(r'(^|-)x64(-|$)|-64-', e['id']):
            continue
        out.append(e)
    return out


def kind(e):
    """none: nothing to download; via: applied by another item; zip-app: unpacked and copied; manual: no documented
    silent switch, copied for the user; run: installed by SECURE.CMD."""
    if e['id'].startswith('ms-trustedr'):
        return 'via'
    if not e.get('url'):
        return 'none'
    if not e.get('sha256'):
        return 'unverified'
    if e['id'].startswith('mypal'):
        return 'zip-app'
    if not e.get('silent_args'):
        return 'manual'
    return 'run'


def spoken_name(e, pl):
    n = e.get('name_pl') if pl else e['name']
    if e.get('official') is False:
        n = ('nieoficjalny: %s' if pl else 'unofficial: %s') % n
    return n


# ---- fetching -------------------------------------------------------------------------------------------------
def fetch(e, progress=None):
    """Download into the cache (once), then check size and SHA-256 against the manifest. Returns the file path."""
    os.makedirs(CACHE, exist_ok=True)
    cached = os.path.join(CACHE, e['sha256'])
    name = e.get('file')
    if os.path.exists(cached) and os.path.exists(cached + '.name'):
        return cached, open(cached + '.name').read().strip()
    req = urllib.request.Request(e['url'], headers={'User-Agent': 'charon-hypervisor'})
    h = hashlib.sha256()
    got = 0
    with urllib.request.urlopen(req, timeout=120) as r, open(cached + '.part', 'wb') as f:
        cd = r.headers.get('Content-Disposition') or ''
        m = re.search(r'filename="?([^";]+)', cd)
        if not name:
            name = m.group(1) if m else os.path.basename(r.geturl().split('?')[0])
        total = int(r.headers.get('Content-Length') or e.get('bytes') or 0)
        said = 0
        while True:
            b = r.read(1 << 20)
            if not b:
                break
            f.write(b); h.update(b); got += len(b)
            if progress and total > 50 << 20 and got * 4 // total > said:
                said = got * 4 // total
                progress(said * 25)
    digest = h.hexdigest()
    if e.get('bytes') and got != e['bytes'] or digest != e['sha256']:
        os.remove(cached + '.part')
        raise ValueError('it does not match the recorded SHA-256 (got %d bytes, %s)' % (got, digest[:16]))
    os.replace(cached + '.part', cached)
    if '.' not in name:
        name += '.exe'
    open(cached + '.name', 'w').write(name)
    return cached, name


# ---- the guest's script ---------------------------------------------------------------------------------------
def ascii_pl(s):
    return s.translate(str.maketrans('ąćęłńóśźżĄĆĘŁŃÓŚŹŻ', 'acelnoszzACELNOSZZ'))


def command_for(e, rel):
    """The command line of one item; rel is its path below the drive's CHARONSEC folder."""
    args = (e.get('silent_args') or '').split(' (', 1)[0].strip()
    path = '"%D%' + rel + '"'
    base = os.path.basename(rel)
    if ' then ' in args:                     # DirectX: "<file> /Q /T:<dir> then <dir>\DXSETUP.exe /silent"
        first, second = args.split(' then ', 1)
        tail = first.split(' ', 1)[1] if ' ' in first else ''
        m = re.search(r'/T:(\S+)', tail)
        return ['start "" /wait %s %s' % (path, tail), 'start "" /wait %s' % second] + (['rd /s /q %s' % m.group(1)] if m else [])
    if args.lower().startswith('msiexec'):
        a = re.sub(r'\S+\.msi', path.replace('\\', '\\\\'), args, count=1)
        if path not in a:
            a = args + ' ' + path
        return ['start "" /wait ' + a]
    if args.lower().startswith('wusa'):
        return ['start "" /wait wusa.exe %s %s' % (path, args.split('.msu', 1)[-1].strip())]
    return ['start "" /wait %s %s' % (path, args)]


def ca_blob_reg(der):
    """The hypervisor's TLS certificate for the guest's Root store, as Windows keeps it in the registry (a property
    list: SHA-1 hash, property 3; the encoded certificate, property 32)."""
    sha1 = hashlib.sha1(der).digest()
    blob = b''
    for pid, data in ((3, sha1), (32, der)):
        blob += pid.to_bytes(4, 'little') + (1).to_bytes(4, 'little') + len(data).to_bytes(4, 'little') + data
    hexs = ','.join('%02x' % b for b in blob)
    lines, cur = [], '"Blob"=hex:'
    for part in hexs.split(','):
        if len(cur) + 3 > 76:
            lines.append(cur + ',\\')
            cur = '  ' + part
        else:
            cur += ('' if cur.endswith(':') else ',') + part
    lines.append(cur)
    return '\r\n'.join(['REGEDIT4', '', '[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\SystemCertificates\\ROOT\\Certificates\\%s]' % sha1.hex().upper()]
                       + lines + ['', ''])


def hardening(version):
    if version in NINE_X:
        # Windows 9x has no firewall, no services to harden and no real user separation: its protection is the
        # hypervisor around it (restricted network, proxy, read-only share); only autorun and file sharing go off here
        return ['reg add HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer /v NoDriveTypeAutoRun /t REG_DWORD /d 255 /f']
    rdp = ['reg add "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Terminal Server" /v fDenyTSConnections /t REG_DWORD /d 1 /f',
           'reg add "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Remote Assistance" /v fAllowToGetHelp /t REG_DWORD /d 0 /f']
    autorun = ['reg add HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer /v NoDriveTypeAutoRun /t REG_DWORD /d 255 /f',
               'reg add HKCU\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer /v NoDriveTypeAutoRun /t REG_DWORD /d 255 /f',
               'reg add "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\IniFileMapping\\Autorun.inf" /ve /d "@SYS:DoesNotExist" /f']
    au = ['reg add HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU /v NoAutoUpdate /t REG_DWORD /d 0 /f',
          'reg add HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU /v AUOptions /t REG_DWORD /d 4 /f',
          'reg add HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU /v ScheduledInstallDay /t REG_DWORD /d 0 /f',
          'reg add HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU /v ScheduledInstallTime /t REG_DWORD /d 3 /f']
    services = 'RemoteRegistry lanmanserver SSDPSRV upnphost TlntSvr RemoteAccess Messenger NetDDE NetDDEdsdm mnmsrvc'
    svc = ['for %%%%s in (%s) do (sc config %%%%s start= disabled ^>nul ^& sc stop %%%%s ^>nul)' % services]
    if version == 'xp':
        fw = ['netsh firewall set opmode mode=ENABLE exceptions=DISABLE', 'netsh firewall set service type=FILEANDPRINT mode=DISABLE',
              'netsh firewall set service type=REMOTEDESKTOP mode=DISABLE', 'netsh firewall set service type=UPNP mode=DISABLE',
              # SMB (only SMBv1 exists on XP) is off with the Server service; NetBT stops listening on port 445
              'reg add HKLM\\SYSTEM\\CurrentControlSet\\Services\\NetBT\\Parameters /v SMBDeviceEnabled /t REG_DWORD /d 0 /f']
    else:
        fw = ['netsh advfirewall set allprofiles state on', 'netsh advfirewall set allprofiles firewallpolicy blockinbound,allowoutbound',
              # Microsoft's documented way to switch SMBv1 off on Windows 7/8/8.1 (KB2696547)
              'reg add HKLM\\SYSTEM\\CurrentControlSet\\Services\\LanmanServer\\Parameters /v SMB1 /t REG_DWORD /d 0 /f',
              'sc config lanmanworkstation depend= bowser/mrxsmb20/nsi', 'sc config mrxsmb10 start= disabled']
    return fw + [l.replace('^>', '>').replace('^&', '&') for l in svc] + rdp + autorun + au


def readback(version):
    L = '>> %L% 2>&1'
    out = ['echo.' + ' >> %L%', 'echo ==== Firewall / Zapora %s' % L]
    if version == 'xp':
        out += ['netsh firewall show state %s' % L]
    elif version not in NINE_X:
        out += ['netsh advfirewall show allprofiles state %s' % L]
    if version not in NINE_X:
        out += ['echo ==== Services / Uslugi %s' % L,
                'for %%%%s in (RemoteRegistry lanmanserver TermService SSDPSRV upnphost) do (echo %%%%s %s & sc qc %%%%s | find "START_TYPE" %s)' % (L, L),
                'reg query "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Terminal Server" /v fDenyTSConnections %s' % L,
                'reg query HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer /v NoDriveTypeAutoRun %s' % L,
                'echo ==== Updates installed / Zainstalowane aktualizacje %s' % L,
                'wmic qfe get HotFixID,InstalledOn | more %s' % L,
                'echo ==== Runtimes / Biblioteki %s' % L,
                'reg query "HKLM\\SOFTWARE\\Microsoft\\NET Framework Setup\\NDP" %s' % L,
                'reg query HKLM\\SOFTWARE\\Microsoft\\DirectX /v Version %s' % L,
                'dir /b "%%windir%%\\system32\\msvcr*.dll" "%%windir%%\\system32\\d3dx9_*.dll" "%%windir%%\\system32\\xinput*.dll" "%%windir%%\\system32\\OpenAL32.dll" %s' % L,
                'echo ==== Programs / Programy %s' % L,
                'reg query HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall /s | find "DisplayName" %s' % L]
    return out


def reg9x(harden):
    """Windows 9x has no reg.exe: one REGEDIT4 file with the proxy and (when hardening is on) autorun off and the
    file-and-printer-sharing server off (no firewall and no services exist to harden on 9x: its protection is the
    hypervisor around it)."""
    k = '[HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings]'
    out = ['REGEDIT4', '', k, '"ProxyEnable"=dword:00000001', '"ProxyServer"="%s:%d"' % (PROXY_ADDR, PROXY_PORT),
           '"ProxyOverride"="<local>"', '"SecureProtocols"=dword:00000080', '']
    if harden:
        out += ['[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer]', '"NoDriveTypeAutoRun"=dword:000000ff', '',
                '[HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer]', '"NoDriveTypeAutoRun"=dword:000000ff', '']
    return '\r\n'.join(out + [''])


def secure_bat(version, steps, harden=True):
    """SECURE.BAT for COMMAND.COM (Windows 95/98/ME): %1 is the drive letter of CHARONSEC (RUN_LINE_9X passes it)."""
    L = 'C:\\CHARONSE.TXT'
    lines = ['@echo off', 'rem Charon Hypervisor: secure-use set for Windows %s' % version, 'if "%1"=="" goto noarg',
             'echo Charon Hypervisor secure-use set / zestaw bezpiecznego uzywania > ' + L,
             'echo Setting the proxy of the hypervisor / Ustawiam posrednika hipernadzorcy',
             'regedit /s %1:\\CHARONSEC\\SECURE9X.REG', 'regedit /s %1:\\CHARONSEC\\CHARONCA.REG', 'echo proxy and certificate set >> ' + L]
    for title, cmds in steps:
        t = ascii_pl(title).replace('%', '').replace('&', 'and').replace('>', '').replace('<', '').replace('|', '')
        lines += ['echo.', 'echo Installing / Instaluje: %s' % t, 'echo %s >> %s' % (t, L)]
        lines += [c.replace('start "" /wait ', 'start /w ').replace('"%D%', '%1:\\CHARONSEC\\').replace('.exe"', '.exe').replace('.EXE"', '.EXE')
                  .replace('.msi"', '.msi').replace('.zip"', '.zip') for c in cmds]
    lines += ['echo ==== Runtimes / Biblioteki >> ' + L, 'dir C:\\WINDOWS\\SYSTEM\\MSVC*.DLL >> ' + L, 'dir C:\\WINDOWS\\SYSTEM\\D3DX9*.DLL >> ' + L,
              'dir C:\\WINDOWS\\SYSTEM\\MSVBVM60.DLL >> ' + L, 'echo Finished / Zakonczone >> ' + L,
              'echo The list is in C:\\CHARONSE.TXT; restart Windows. / Lista jest w C:\\CHARONSE.TXT; uruchom Windows ponownie.',
              'start notepad ' + L, 'goto end', ':noarg', 'echo Start it with the drive letter: SECURE.BAT E', ':end', '']
    return '\r\n'.join(lines)


RUN_LINE_9X = 'command /c for %d in (D E F G H I J K L M N O P) do if exist %d:\\CHARONSEC\\SECURE.BAT %d:\\CHARONSEC\\SECURE.BAT %d'


# 6zd: the guest's update status for the hypervisor. Windows Update Agent (which Legacy Update points at its own
# server through the WUServer policy) is asked what is waiting; the answer goes to http://updates.charon/report
# through the hypervisor's proxy (hvproxy.update_report) - nothing leaves the hypervisor, no agent in the guest.
AV_GUID = '{8C0A2D1E-4F1B-4B6E-9C1A-7A3E5D2B0410}'     # session 10: Charon's entry in XP's Security Center
UPDATE_STATUS_VBS = '\r\n'.join([
    "' Charon Hypervisor: the update status of this Windows for the hypervisor (6zd). Runs at start and every hour.",
    'On Error Resume Next',
    'Dim s, r, n, t, i, c, h, e, d, q, x, p, v, o, a',
    'Set s = CreateObject("Microsoft.Update.Session")',
    'Set r = s.CreateUpdateSearcher().Search("IsInstalled=0 and Type=\'Software\' and IsHidden=0")',
    'If Err.Number <> 0 Then',
    '  q = "error=" & Hex(Err.Number)',
    'Else',
    '  n = r.Updates.Count',
    '  t = ""',
    '  For i = 0 To n - 1',
    '    If i < 3 Then t = t & r.Updates.Item(i).Title & "|"',
    '  Next',
    '  Set h = s.CreateUpdateSearcher()',
    '  c = h.GetTotalHistoryCount()',
    '  d = ""',
    '  If c > 0 Then',
    '    Set e = h.QueryHistory(0, 1)',
    '    d = Year(e.Item(0).Date) & "-" & Right("0" & Month(e.Item(0).Date), 2) & "-" & Right("0" & Day(e.Item(0).Date), 2)',
    '  End If',
    '  q = "pending=" & n & "&installed=" & c & "&last=" & d & "&titles=" & U(t)',
    'End If',
    'Err.Clear',
    'Set x = CreateObject("MSXML2.ServerXMLHTTP")',
    'x.setProxy 2, "%s:%d"' % (PROXY_ADDR, PROXY_PORT),
    'x.open "GET", "http://updates.charon/report?" & q, False',
    'x.send',
    # session 10 (user: can the machine see the hypervisor's protection as an antivirus?): the proxy's answer is the
    # REAL state (scan=1: downloads are scanned now; fresh=1: ClamAV signatures at most 3 days old). Only on XP /
    # Server 2003 (Security Center's WMI store root\\SecurityCenter: undocumented for non-vendors but read by XP's
    # Security Center; Microsoft stopped supporting writes from Vista SP1 and 8+ needs Microsoft's MVI signing - there
    # nothing is registered and Defender stays on). Never a constant "on": off or old signatures show as such.
    'p = ""',
    # measured live (session 10): XP's MSXML leaves Err 438 set by setProxy although the request works - so the
    # answer is taken by its HTTP status, not by Err
    'If x.status = 200 Then p = x.responseText',
    'Err.Clear',
    'v = ""',
    'For Each o In GetObject("winmgmts:\\\\.\\root\\cimv2").ExecQuery("Select Version from Win32_OperatingSystem")',
    '  v = o.Version',
    'Next',
    'If Left(v, 4) = "5.1." Or Left(v, 4) = "5.2." Then',
    '  Set a = GetObject("winmgmts:\\\\.\\root\\SecurityCenter").Get("AntiVirusProduct").SpawnInstance_()',
    '  a.instanceGuid = "%s"' % AV_GUID,
    '  a.displayName = "Charon Hypervisor protection (network and exchange drive, not files inside Windows)"',
    '  a.companyName = "Charon Hypervisor"',
    '  a.versionNumber = "r410"',
    '  a.onAccessScanningEnabled = (InStr(p, "scan=1") > 0)',
    '  a.productUptoDate = (InStr(p, "fresh=1") > 0)',
    '  a.Put_',
    'End If',
    '',
    "' UTF-8 percent-encoding (titles are in the language of this Windows)",
    'Function U(v)',
    '  Dim k, a, o',
    '  o = ""',
    '  For k = 1 To Len(v)',
    '    a = AscW(Mid(v, k, 1))',
    '    If a < 0 Then a = a + 65536',
    '    If (a >= 48 And a <= 57) Or (a >= 65 And a <= 90) Or (a >= 97 And a <= 122) Then',
    '      o = o & ChrW(a)',
    '    ElseIf a < 128 Then',
    '      o = o & "%" & Right("0" & Hex(a), 2)',
    '    ElseIf a < 2048 Then',
    '      o = o & "%" & Hex(192 + (a \\ 64)) & "%" & Hex(128 + (a And 63))',
    '    Else',
    '      o = o & "%" & Hex(224 + (a \\ 4096)) & "%" & Hex(128 + ((a \\ 64) And 63)) & "%" & Hex(128 + (a And 63))',
    '    End If',
    '  Next',
    '  U = o',
    'End Function', ''])


def update_channel(version):
    """6zd: Legacy Update is the update channel (its setup points Windows Update and Automatic Updates at its server
    and replaces the Start menu's Windows Update shortcut by its own). Here: Automatic Updates on (download, then
    notify - the hypervisor says what waits), the status task, and on XP Security Center's antivirus nag off - the
    hypervisor says what covers that part (ClamAV scans downloads in the hypervisor) and that nothing scans inside."""
    task = 'Charon update status'
    lines = ['echo Update channel: Legacy Update / Kanal aktualizacji: Legacy Update',
             'reg add "HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU" /v NoAutoUpdate /t REG_DWORD /d 0 /f >> %L% 2>&1',
             'reg add "HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU" /v AUOptions /t REG_DWORD /d 3 /f >> %L% 2>&1',
             'copy /y "%D%CHARONWU.VBS" "%SystemRoot%\\CHARONWU.VBS" >> %L% 2>&1',
             'schtasks /delete /tn "%s" /f >nul 2>&1' % task,
             'schtasks /delete /tn "%s at start" /f >nul 2>&1' % task,
             'schtasks /create /tn "%s" /tr "wscript.exe //B %%SystemRoot%%\\CHARONWU.VBS" /sc hourly /ru System >> %%L%% 2>&1' % task,
             'schtasks /create /tn "%s at start" /tr "wscript.exe //B %%SystemRoot%%\\CHARONWU.VBS" /sc onstart /ru System >> %%L%% 2>&1' % task]
    if version == 'xp':
        # session 10: no more silencing of the nag (an undocumented value); Security Center sees "Charon Hypervisor
        # protection" with its real state, written by CHARONWU.VBS (at start and every hour)
        lines += ['reg delete "HKLM\\SOFTWARE\\Microsoft\\Security Center" /v AntiVirusDisableNotify /f >nul 2>&1',
                  'echo ==== Security Center: Charon Hypervisor protection, its real state (on / off / signatures old). It scans what comes through the network and the exchange drive; it does NOT watch files inside Windows. >> %L%',
                  'echo      Centrum zabezpieczen: ochrona Charon Hypervisor, jej prawdziwy stan (wlaczona / wylaczona / stare sygnatury). Skanuje to, co przychodzi przez siec i dysk wymiany; NIE pilnuje plikow wewnatrz Windows. >> %L%']
    return lines


def secure_cmd(version, steps, pl, harden=True, updates=False):
    """SECURE.CMD: proxy + certificate first (Legacy Update and the others download through it), then the items,
    then hardening, then the read-back for the guest's screen reader."""
    L = 'C:\\CHARON-SECURE.TXT'
    nt = version not in NINE_X
    lines = ['@echo off', 'rem Charon Hypervisor: secure-use set for Windows %s (generated %s)' % (version, time.strftime('%Y-%m-%d %H:%M')),
             'set D=%~dp0', 'set L=' + L,
             'echo Charon Hypervisor secure-use set / zestaw bezpiecznego uzywania, %DATE% %TIME% > %L%']
    if nt:
        lines += ['echo Setting the proxy of the hypervisor / Ustawiam posrednika hipernadzorcy',
                  'reg add "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings" /v ProxyEnable /t REG_DWORD /d 1 /f >> %L%',
                  'reg add "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings" /v ProxyServer /d %s:%d /f >> %%L%%' % (PROXY_ADDR, PROXY_PORT),
                  'reg add "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings" /v ProxyOverride /d "<local>" /f >> %L%',
                  ('proxycfg -u >> %L% 2>&1' if version == 'xp' else 'netsh winhttp import proxy source=ie >> %L% 2>&1')]
        if version == 'xp':
            # Internet Explorer on XP: TLS 1.0 only (SSL 2.0/3.0 off); the proxy's TLS 1.0 leg answers it
            lines += ['reg add "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings" /v SecureProtocols /t REG_DWORD /d 128 /f >> %L%']
            lines += ['echo Trusting the TLS certificate of this hypervisor / Ufam certyfikatowi TLS tego hipernadzorcy',
                      'regedit /s "%D%CHARONCA.REG"', 'echo hypervisor TLS certificate added >> %L%']
        else:
            lines += ['certutil -addstore -f Root "%D%CHARONCA.CER" >> %L% 2>&1']
    for title, cmds in steps:
        t = ascii_pl(title).replace('%', '%%').replace('&', 'and').replace('>', '').replace('<', '').replace('|', '')
        lines += ['echo.', 'echo Installing / Instaluje: %s' % t, 'echo [%%TIME%%] %s >> %%L%%' % t] + cmds + \
                 ['echo   exit code / kod wyjscia %ERRORLEVEL% >> %L%', 'echo   done, exit code %ERRORLEVEL% / gotowe']
    lines += ['echo.', 'echo Hardening / Utwardzanie >> %L%', 'echo Hardening Windows / Utwardzam Windows'] + \
             [l + ' >> %L% 2>&1' for l in (hardening(version) if harden else [])]
    if nt and updates:
        lines += update_channel(version)
    if nt:
        lines += ['wuauclt /detectnow']
        if updates:
            lines += ['start "" wscript.exe //B "%SystemRoot%\\CHARONWU.VBS"']
        # 6q printing to PDF: a PostScript printer with Windows' in-box "MS Publisher Imagesetter" driver on LPT1, whose
        # output the hypervisor turns into a PDF on its partition (hvprint)
        lines += ['echo Adding the printer Charon PDF / Dodaje drukarke Charon PDF',
                  'rundll32 printui.dll,PrintUIEntry /if /b "Charon PDF" /f "%windir%\\inf\\ntprint.inf" /r "LPT1:" /m "MS Publisher Imagesetter" /u /q',
                  'echo ==== Printer / Drukarka Charon PDF >> %L%',
                  'reg query "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Print\\Printers\\Charon PDF" /v Port >> %L% 2>&1',
                  'echo ==== Mail / Poczta: POP3 and IMAP and SMTP server 10.0.2.100, no SSL in the program; the hypervisor adds TLS >> %L%',
                  'echo      serwer POP3, IMAP i SMTP 10.0.2.100, bez SSL w programie; TLS dodaje hipernadzorca >> %L%',
                  # 6v: the hypervisor's read-only Downloads share (files from its browsers, scanned) as drive P:,
                  # kept across restarts, and a desktop shortcut "Charon Downloads"
                  'echo Connecting the Downloads share / Podlaczam udzial Pobrane',
                  'net use P: \\\\10.0.2.100\\Downloads /persistent:yes >> %L% 2>&1',
                  '(echo [InternetShortcut]& echo URL=file://10.0.2.100/Downloads) > "%USERPROFILE%\\Desktop\\Charon Downloads.url"',
                  'echo ==== Downloads share / Udzial Pobrane: P: = \\\\10.0.2.100\\Downloads >> %L%']
    lines += readback(version)
    lines += ['echo Finished / Zakonczone %DATE% %TIME% >> %L%', 'echo The list is in C:\\CHARON-SECURE.TXT; restart Windows to finish the updates.',
              'echo Lista jest w C:\\CHARON-SECURE.TXT; uruchom Windows ponownie, aby dokonczyc aktualizacje.',
              'start notepad %L%', '']
    return '\r\n'.join(lines)


def build(cfg, outdir, say, T, background=lambda: None, groups=None, ca_crt=None):
    """Fetch, verify and stage the machine's set into outdir/CHARONSEC; returns (staged, report) where report is a
    list of (item id, result) for the log. say(text) speaks, T(en, pl) picks the language."""
    pl = T('en', 'pl') == 'pl'
    version = version_of(cfg)
    arch = cfg.get('arch', 'x86' if version in ('xp',) + NINE_X else 'x64')
    groups = set(groups if groups is not None else DEFAULT_GROUPS)
    sec = os.path.join(outdir, 'CHARONSEC')
    shutil.rmtree(sec, ignore_errors=True)
    os.makedirs(os.path.join(sec, 'PROGRAMS'))
    steps, report, manual = [], [], []
    todo = items(version, arch, groups)
    say(T('Preparing the secure-use set for %s: %d parts.' % (cfg.get('name', version), len(todo)),
          'Przygotowuję zestaw bezpiecznego używania dla %s: %d części.' % (cfg.get('name', version), len(todo))))
    for n, e in enumerate(todo, 1):
        background()
        name = spoken_name(e, pl) if kind(e) not in ('none', 'via') else (e.get('name_pl') if pl else e['name'])
        k = kind(e)
        if k == 'unverified':
            say(T('%s: not fetched, because no copy from its own project could be checked; its page: %s.' % (name, e.get('page') or e['url']),
                  '%s: nie pobieram, bo nie dało się sprawdzić kopii od samego projektu; jego strona: %s.' % (name, e.get('page') or e['url'])))
            report.append((e['id'], k))
            continue
        if k in ('none', 'via'):
            why = lookup(SAY, e['id'])
            text = why[1 if pl else 0] if why else T('no official download exists', 'nie ma oficjalnego pobrania')
            say('%s: %s.' % (name, text))
            report.append((e['id'], k))
            continue
        note = lookup(NOTE, e['id'])
        say(T('%s, part %d of %d.' % (name, n, len(todo)), '%s, część %d z %d.' % (name, n, len(todo))) +
            (' ' + note[1 if pl else 0] + '.' if note else ''))
        try:
            path, fname = fetch(e, lambda p: say(T('%d percent' % p, '%d procent' % p), interrupt=False))
            stage(e, k, n, path, fname, sec, steps, manual)
        except Exception as ex:
            shutil.rmtree(os.path.join(sec, '%02d' % n), ignore_errors=True)
            say(T('%s failed: %s. The rest continues.' % (name, ex), '%s nie powiódł się: %s. Reszta jest kontynuowana.' % (name, ex)))
            report.append((e['id'], 'failed: %s' % ex))
            continue
        say(T('%s verified.' % e['name'], '%s sprawdzony.' % e['name']), interrupt=False)
        report.append((e['id'], 'staged ' + k))
    finish(sec, version, steps, manual, groups, ca_crt, pl, say, T, report)
    return sec, report


def stage(e, k, n, path, fname, sec, steps, manual):
    """Copy one fetched, verified part into the drive tree and add its step (or list it for the user)."""
    rel = '%02d\\%s' % (n, fname)
    os.makedirs(os.path.join(sec, '%02d' % n))
    if k == 'zip-app':
        app = re.sub(r'[^A-Za-z0-9]', '', e['name'].split()[0]) or 'App'
        with zipfile.ZipFile(path) as z:
            z.extractall(os.path.join(sec, '%02d' % n, app))
        steps.append((e['name'], ['xcopy /e /i /y /q "%%D%%%02d\\%s" "%%ProgramFiles%%\\%s" >> %%L%%' % (n, app, app),
                                  'for /r "%%ProgramFiles%%\\%s" %%%%f in (%s.exe) do if exist "%%%%f" cscript //nologo "%%D%%MKLNK.VBS" "%%%%f" "%s"'
                                  % (app, app.lower(), app)]))
    elif k == 'manual':
        dst = os.path.join(sec, 'PROGRAMS', fname)
        if fname.lower().endswith('.zip'):
            with zipfile.ZipFile(path) as z:
                z.extractall(os.path.join(sec, 'PROGRAMS'))
        else:
            shutil.copy(path, dst)
        manual.append(e['name'])
        os.rmdir(os.path.join(sec, '%02d' % n))
    else:
        shutil.copy(path, os.path.join(sec, rel.replace('\\', '/')))
        steps.append((e['name'], command_for(e, rel)))


def finish(sec, version, steps, manual, groups, ca_crt, pl, say, T, report):
    if manual:
        steps.append(('programs to install by hand', ['echo Without silent installation, in the folder PROGRAMS / Bez cichej instalacji, w folderze PROGRAMS: %s >> %%L%%' % ascii_pl(', '.join(manual)),
                                                      'xcopy /e /i /y /q "%D%PROGRAMS" "C:\\CHARON-PROGRAMS" >> %L%']))
        say(T('These have no silent installation and wait in C:\\CHARON-PROGRAMS for you: %s.' % ', '.join(manual),
              'Te nie mają cichej instalacji i czekają na ciebie w C:\\CHARON-PROGRAMS: %s.' % ', '.join(manual)))
    if ca_crt:
        der = subprocess.run(['openssl', 'x509', '-in', ca_crt, '-outform', 'DER'], capture_output=True, check=True).stdout
        open(os.path.join(sec, 'CHARONCA.CER'), 'wb').write(der)
        open(os.path.join(sec, 'CHARONCA.REG'), 'w', newline='').write(ca_blob_reg(der))
    open(os.path.join(sec, 'MKLNK.VBS'), 'w', newline='').write(
        'Set s = CreateObject("WScript.Shell")\r\nSet l = s.CreateShortcut(s.SpecialFolders("AllUsersDesktop") & "\\" & WScript.Arguments(1) & ".lnk")\r\n'
        'l.TargetPath = WScript.Arguments(0)\r\nl.Save\r\n')
    # 6z/6za: the guest's "run with Charon compatibility" launcher (COMPAT.VBS + shell verb), for the Windows the
    # compatibility layer covers (7/8/8.1 in the guest via VxKex; XP/Vista through the hypervisor's Wine route).
    if 'compat' in groups and version in ('7', '8', '8.1', 'xp', 'vista'):
        import hvcompat
        open(os.path.join(sec, 'COMPAT.VBS'), 'w', encoding='cp1252', newline='').write(hvcompat.guest_launcher_vbs(version))
        if version not in NINE_X:
            steps.append(('Charon compatibility launcher', hvcompat.guest_shell_reg_cmd()))
    if version in NINE_X:
        open(os.path.join(sec, 'SECURE9X.REG'), 'w', newline='').write(reg9x('hardening' in groups))
        open(os.path.join(sec, 'SECURE.BAT'), 'w', encoding='cp1252', newline='').write(secure_bat(version, steps, 'hardening' in groups))
    else:
        text = secure_cmd(version, steps, pl, harden='hardening' in groups, updates='updates' in groups)
        open(os.path.join(sec, 'SECURE.CMD'), 'w', encoding='cp1252', newline='').write(text)
        if 'updates' in groups:
            open(os.path.join(sec, 'CHARONWU.VBS'), 'w', encoding='ascii', newline='').write(UPDATE_STATUS_VBS)
    json.dump(report, open(os.path.join(sec, 'report.json'), 'w'), indent=0)
    return sec, report


def make_image(srcdir, img, fat=32):
    """A FAT image of srcdir: FAT32 for XP and newer (a read-only USB disk named CHARONSEC); FAT16 under 2 GB for
    Windows 9x, which gets it as a second IDE disk whose writes are thrown away (snapshot=on)."""
    size = 64 << 20
    for root, _, files in os.walk(srcdir):
        size += sum(os.path.getsize(os.path.join(root, f)) + 65536 for f in files)
    size = max(size, 128 << 20)
    if fat == 16 and size > (2047 << 20):
        raise RuntimeError('the set is larger than the 2 GB that FAT16 holds for Windows 9x')
    with open(img + '.new', 'wb') as f:
        f.truncate(size)
    r = subprocess.run(['mkfs.vfat', '-F', str(fat), '-n', 'CHARONSEC', img + '.new'], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError('mkfs.vfat: ' + (r.stderr.strip() or 'failed'))
    mnt = '/run/charonhv/secure-mnt'
    os.makedirs(mnt, exist_ok=True)
    subprocess.run(['modprobe', 'loop'], capture_output=True)
    r = subprocess.run(['mount', '-o', 'loop', img + '.new', mnt], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError('mount: ' + (r.stderr.strip() or 'failed'))
    try:
        shutil.copytree(srcdir, os.path.join(mnt, os.path.basename(srcdir)))
        with open(os.path.join(mnt, 'AUTORUN.INF'), 'w', newline='') as f:
            f.write('[autorun]\r\nlabel=Charon Hypervisor secure-use set\r\n')
    finally:
        subprocess.run(['umount', mnt], capture_output=True)
    os.replace(img + '.new', img)
    return img


RUN_LINE = 'cmd /c for %d in (D E F G H I J K L M N O P) do if exist %d:\\CHARONSEC\\SECURE.CMD call %d:\\CHARONSEC\\SECURE.CMD'


def type_into(m, text):
    """Type ASCII text into the guest (US layout codes, which Polish programmer's layout shares)."""
    import hvkeys
    table = {' ': ('spc', False)}
    for code, (c, s) in hvkeys._CHARS.items():
        q = hvkeys.QCODE[code]
        table.setdefault(c, (q, False))
        table.setdefault(s, (q, True))
    for ch in text:
        q, sh = table[ch]
        m.send_combo(['shift', q] if sh else [q])
        time.sleep(0.03)


def run_in_guest(m):
    """Open the Run box (Windows + R) and start SECURE.CMD from the CHARONSEC drive."""
    m.send_combo(['meta_l', 'r'])
    time.sleep(2)
    type_into(m, RUN_LINE_9X if version_of(m.cfg) in NINE_X else RUN_LINE)
    m.send_combo(['ret'])


def update_blocklist(say, T):
    """HaGeZi's Threat Intelligence Feeds, mini version (malware, phishing, scam domains; GPL-3.0), from the project's own
    GitHub repository (abuse.ch URLhaus's host file now answers 410 Gone and its feeds need an account key). Kept when it fails."""
    os.makedirs(SECURE, exist_ok=True)
    dst = os.path.join(SECURE, 'blocklist.txt')
    try:
        req = urllib.request.Request('https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/tif.mini-onlydomains.txt', headers={'User-Agent': 'charon-hypervisor'})
        data = urllib.request.urlopen(req, timeout=60).read()
        if b"# Title: HaGeZi's Threat Intelligence Feeds" not in data[:1024]:
            raise ValueError('not a host list')
        open(dst + '.new', 'wb').write(data)
        os.replace(dst + '.new', dst)
        n = sum(1 for l in data.splitlines() if l and not l.startswith(b'#'))
        say(T('The list of malicious domains is up to date: %d domains, from HaGeZi Threat Intelligence Feeds.' % n,
              'Lista złośliwych domen jest aktualna: %d domen, z HaGeZi Threat Intelligence Feeds.' % n))
    except Exception as e:
        say(T('The list of malicious domains could not be updated: %s. The old one stays.' % e,
              'Nie udało się zaktualizować listy złośliwych domen: %s. Zostaje stara.' % e))
