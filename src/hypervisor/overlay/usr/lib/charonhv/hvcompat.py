# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: the compatibility layer so programs made for Windows 10/11 run on the old Windows guests
(answers 6x, 6y, 6z, 6za, 6zb, 6zg; 6zf picks the last version that still supports each Windows).

ONE launcher, "run with Charon compatibility", on by default (6za): when a program is known not to run on this
Windows - its PE header asks for a newer Windows (subsystem / OS version), it imports a DLL or function the old
Windows does not have, or it is on a known-program list - the layer is applied automatically and the route is
spoken ONCE; it is switchable off per program.

Two routes, chosen by the guest's Windows version (6zb, 6zg):
  IN-GUEST (Windows 7, 8, 8.1): the VxKex NEXT fork adds the newer Windows APIs as DLLs loaded per program through
    the Image File Execution Options "VerifierDlls" value; the program is a normal guest window, so the GUEST'S OWN
    reader (NVDA etc.) reads it. Preferred wherever it works. VxKex NEXT supports 7/8/8.1; there is no clean
    open-source in-guest shim for XP or Vista (One-Core-API replaces system DLLs and is built on ReactOS; it is not
    installed automatically - see r410-hv-research-6u-6z.txt section 3).
  HYPERVISOR (Windows XP, Vista): the program runs under Wine (an Alpine package) in the hypervisor, its window
    shown on the real screen over the guest, files through the exchange drive. A LINUX reader cannot see a Wine
    window (Wine bridges no accessibility API to AT-SPI, in either direction - WineHQ / orca-list, long-standing),
    so the reader is the WINDOWS NVDA installed INSIDE the Wine prefix, reading the program through Windows'
    own accessibility (MSAA / UI Automation) exactly as on real Windows, speaking with Charon's voice (NVDA's
    bundled eSpeak NG is a Charon voice; the Charon synth server can reach all 17). Technique from
    dnl-nash/wineinstall (commit a28affa4, 2026-10-18, no licence - LICENSES.txt); the script itself is not shipped
    (it is unlicensed, renames ~/.wine and has unbounded download loops) - only the technique, built cleanly here
    with pinned versions.

This module's pure functions (PE parsing, detection, route and the spoken line) are covered by the host test; the
Wine runner needs the guest and is proven live."""
import json, os, struct, subprocess, time

# ---- which Windows a PE needs -------------------------------------------------------------------------------------
# NT version number of each guest Windows (major, minor). A PE whose MajorOperatingSystemVersion / MajorSubsystem
# version is higher will not load on it ("not a valid Win32 application").
GUEST_NT = {'xp': (5, 1), 'vista': (6, 0), '7': (6, 1), '8': (6, 2), '8.1': (6, 3),
            '95': (4, 0), '98': (4, 10), '98se': (4, 10), 'me': (4, 90)}

# DLLs and import styles that the old Windows does not ship, so a program importing them cannot start there. The
# API-set stubs (api-ms-win-*, ext-ms-win-*) first appear on Windows 7/8; the Universal CRT (ucrtbase, vcruntime140)
# and the modern C++ runtime are Windows-10 era (downlevel only with the redistributable, which most programs do not
# carry); kernelbase.dll holds functions XP/Vista lack. The set is deliberately small and sure, not a full database.
MODERN_IMPORT_DLLS = ('api-ms-win-', 'ext-ms-win-', 'ucrtbase.dll', 'vcruntime140', 'vcruntime150',
                      'kernelbase.dll', 'd3d12.dll', 'dcomp.dll', 'windows.ui', 'windows.applicationmodel')
# a few functions that, if imported from kernel32, mark a Vista+/7+/8+ program (XP lacks them)
MODERN_FUNCS_XP = (b'GetTickCount64', b'InitializeConditionVariable', b'CreateThreadpool', b'GetFinalPathNameByHandle',
                   b'InitializeSRWLock', b'InetNtopW', b'SetThreadErrorMode', b'K32GetProcessMemoryInfo')

# Known programs with the earliest Windows they run on (lower-case basename). The project pages are the source;
# this list only seeds "known not to run", the PE check does the rest.
KNOWN_PROGRAMS = {
    'sumatrapdf.exe': (6, 1),       # SumatraPDF >= 3.4 dropped Windows XP/Vista (needs Windows 7+)
    'vlc.exe': (6, 1),              # VLC >= 3.0.18 32-bit still runs on XP; VLC 4 needs 7+ (kept as 7)
    'code.exe': (10, 0),            # Visual Studio Code: Windows 10/11 only since 2023
    'obs64.exe': (10, 0),           # OBS Studio: Windows 10/11 only
    'krita.exe': (10, 0),           # Krita current: Windows 10/11
    'blender.exe': (10, 0),
    'audacity.exe': (10, 0),        # Audacity 3.4+: Windows 10/11
    'mpv.exe': (10, 0),
}


def _u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def _u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def parse_pe(data):
    """Read the parts of a PE file the detection needs. Returns a dict (machine, magic, subsystem, os_major/minor,
    subsystem_major/minor, bits, imports: a lower-case list of imported DLL names) or None when it is not a PE."""
    try:
        if data[:2] != b'MZ':
            return None
        pe = _u32(data, 0x3C)
        if data[pe:pe + 4] != b'PE\0\0':
            return None
        coff = pe + 4
        machine = _u16(data, coff)
        nsec = _u16(data, coff + 2)
        opt_size = _u16(data, coff + 16)
        opt = coff + 20
        magic = _u16(data, opt)
        plus = magic == 0x20B                      # PE32+ (64-bit)
        info = {'machine': machine, 'magic': magic, 'bits': 64 if plus else 32,
                'os_major': _u16(data, opt + 40), 'os_minor': _u16(data, opt + 42),
                'subsystem_major': _u16(data, opt + 48), 'subsystem_minor': _u16(data, opt + 50),
                'subsystem': _u16(data, opt + 68), 'imports': []}
        # data directories: index 1 is the import table (RVA, size). The directory array starts after the fixed
        # part of the optional header (96 for PE32, 112 for PE32+). Import parsing is best-effort: if the import
        # table lies beyond the bytes we were given, the header is still returned (imports empty), never None.
        try:
            dd = opt + (112 if plus else 96)
            imp_rva = _u32(data, dd + 8)
            if imp_rva:
                info['imports'] = _import_dlls(data, coff + 20 + opt_size, nsec, imp_rva)
        except (struct.error, IndexError):
            info['imports'] = []
        return info
    except (struct.error, IndexError):
        return None


def _rva_to_off(rva, sections):
    for va, vsz, raw, rsz in sections:
        if va <= rva < va + max(vsz, rsz):
            return raw + (rva - va)
    return None


def _import_dlls(data, sec_off, nsec, imp_rva):
    sections = []
    for i in range(nsec):
        s = sec_off + i * 40
        sections.append((_u32(data, s + 12), _u32(data, s + 8), _u32(data, s + 20), _u32(data, s + 16)))
    off = _rva_to_off(imp_rva, sections)
    out = []
    if off is None:
        return out
    for i in range(256):
        e = off + i * 20
        name_rva = _u32(data, e + 12)
        if name_rva == 0 and _u32(data, e) == 0:
            break
        no = _rva_to_off(name_rva, sections)
        if no is None:
            continue
        end = data.find(b'\0', no, no + 256)
        if end > no:
            out.append(data[no:end].decode('latin-1').lower())
    return out


def needs_newer_windows(pe, version, data=b''):
    """(True, reason_en, reason_pl) when a program with this PE cannot run on guest Windows `version`, else
    (False, '', ''). Signals, in order: the PE asks for a newer Windows (OS / subsystem major.minor); it imports a
    DLL the old Windows lacks; (XP only) it imports a known Vista+/7+ kernel32 function; the known-program list."""
    if pe is None:
        return (False, '', '')
    g = GUEST_NT.get(version)
    if not g:
        return (False, '', '')
    want = max((pe['os_major'], pe['os_minor']), (pe['subsystem_major'], pe['subsystem_minor']))
    if want > g:
        return (True, 'its program file is built for Windows %d.%d, newer than this %s' % (want[0], want[1], version),
                'jego plik programu jest zbudowany dla Windows %d.%d, nowszego niż ten %s' % (want[0], want[1], version))
    for d in pe['imports']:
        if any(d.startswith(p) or p in d for p in MODERN_IMPORT_DLLS):
            return (True, 'it needs %s, which this Windows does not have' % d,
                    'potrzebuje %s, którego ten Windows nie ma' % d)
    if version == 'xp' and data:
        for fn in MODERN_FUNCS_XP:
            if fn in data:
                return (True, 'it uses the function %s, added after Windows XP' % fn.decode(),
                        'używa funkcji %s, dodanej po Windows XP' % fn.decode())
    return (False, '', '')


# ---- the route and the spoken line -------------------------------------------------------------------------------
IN_GUEST_VERSIONS = ('7', '8', '8.1')         # VxKex NEXT; the guest's own reader reads the window
WINE_VERSIONS = ('xp', 'vista')               # no clean in-guest shim -> Wine in the hypervisor

# 6zh (resolved): the hypervisor Wine route's reader. DEFAULT is Cthulhu's native Wine bridge (reads arbitrary
# standard Win32/MSAA controls over Cthulhu's own D-Bus, speaks Charon's voices through speech-dispatcher);
# FALLBACK is NVDA inside the Wine prefix (fuller MSAA+UIA, reader-independent, its own eSpeak NG). A general
# Wine->AT-SPI bridge that would let Orca/Linda read Wine apps does not exist off the shelf and is post-r410.
WINE_READER_DEFAULT = 'cthulhu'
WINE_READER_FALLBACK = 'nvda-in-wine'
READER_NAME = {'cthulhu': ('Cthulhu', 'Cthulhu'), 'nvda-in-wine': ('NVDA', 'NVDA'), 'guest': ('your screen reader', 'twój czytnik ekranu')}


def route_for(version):
    """'in-guest' (VxKex, the guest's reader) or 'hypervisor' (Wine in the hypervisor), or None when the layer does
    not cover this Windows (9x: no shim, no Wine route offered - said)."""
    if version in IN_GUEST_VERSIONS:
        return 'in-guest'
    if version in WINE_VERSIONS:
        return 'hypervisor'
    return None


def spoken_route(route, pl, program='', reader=None):
    """The one line the launcher speaks (6zb wording; 6zg/6zh for the Wine route). `reader` names which reader fired
    for the hypervisor route ('cthulhu' default, 'nvda-in-wine' fallback)."""
    p = (' %s:' % program) if program else ''
    if route == 'in-guest':
        return ('%s this program needs a newer Windows; Charon\'s compatibility layer is used. It runs in the guest '
                'with the compatibility shim, read by your screen reader.' % (p or ' This program'),
                '%s ten program potrzebuje nowszego Windows; użyta jest warstwa zgodności Charona. Działa w gościu '
                'z nakładką zgodności, czyta go twój czytnik ekranu.' % (p or ' Ten program'))[1 if pl else 0]
    if route == 'hypervisor':
        who = {'cthulhu': ('Cthulhu', 'Cthulhu'), 'nvda-in-wine': ('NVDA', 'NVDA')}.get(reader or WINE_READER_DEFAULT, ('Cthulhu', 'Cthulhu'))
        return ('%s this program needs a newer Windows; Charon\'s compatibility layer is used. It runs through Wine '
                'in the hypervisor, read by %s in Charon\'s voice.' % (p or ' This program', who[0]),
                '%s ten program potrzebuje nowszego Windows; użyta jest warstwa zgodności Charona. Działa przez Wine '
                'w hipernadzorcy, czyta go %s głosem Charona.' % (p or ' Ten program', who[1]))[1 if pl else 0]
    return ('This program is not covered by the compatibility layer on this Windows.',
            'Tego programu nie obejmuje warstwa zgodności na tym Windows.')[1 if pl else 0]


def reader_of(route, reader=None):
    if route == 'in-guest':
        return 'guest'
    if route == 'hypervisor':
        return reader or WINE_READER_DEFAULT
    return None


# ---- on by default, switchable off per program (6za) -------------------------------------------------------------
def _off_file(mdir):
    return os.path.join(mdir, 'compat-off.json')


def is_off(mdir, program):
    try:
        return os.path.basename(program).lower() in set(json.load(open(_off_file(mdir))))
    except (OSError, ValueError):
        return False


def set_off(mdir, program, off):
    try:
        cur = set(json.load(open(_off_file(mdir))))
    except (OSError, ValueError):
        cur = set()
    b = os.path.basename(program).lower()
    cur.add(b) if off else cur.discard(b)
    tmp = _off_file(mdir) + '.new'
    json.dump(sorted(cur), open(tmp, 'w'))
    os.replace(tmp, _off_file(mdir))


def decide(path, version, mdir, data=None):
    """The whole decision for one program file at `path` on a guest of `version`. Returns a dict:
      {apply: bool, route: str|None, reader: str|None, reason_en, reason_pl, off: bool}.
    `data` lets the host test pass PE bytes without a file."""
    if data is None:
        try:
            with open(path, 'rb') as f:
                data = f.read(64 << 20)      # enough to reach the import table even in a large program
        except OSError:
            data = b''
    pe = parse_pe(data)
    need, en, pl = needs_newer_windows(pe, version, data)
    off = is_off(mdir, path)
    route = route_for(version) if need else None
    return {'apply': bool(need and route and not off), 'route': route, 'reader': reader_of(route),
            'reason_en': en, 'reason_pl': pl, 'off': off, 'needs_newer': bool(need)}


# ---- the guest's launcher (6z/6za): "run with Charon compatibility" --------------------------------------------
# Staged into the secure-use set (CHARONSEC) by hvsecure when the 'compat' group is on. COMPAT.VBS reads the
# target program's PE header, decides as needs_newer_windows() does, and acts by route:
#   7/8/8.1: enable VxKex for the program (its IFEO VerifierDlls value, VxKex's documented mechanism, set to the
#            loader DLL found in VxKex's install folder) and launch it; the guest's own reader reads it.
#   XP/Vista: write the program name to CHARON-COMPAT.REQ on the exchange drive; the hypervisor runs it through
#            the Wine route (NVDA in the prefix) and says the route.
# The exact KexCfg invocation of VxKex NEXT is confirmed on live Windows 7 (blocked here on the user's media); the
# IFEO VerifierDlls path below is VxKex's own documented mechanism and does not depend on KexCfg.
def guest_launcher_vbs(version):
    g = GUEST_NT.get(version, (6, 1))
    wine = version in WINE_VERSIONS
    return '\r\n'.join([
        "' Charon Hypervisor: run with Charon compatibility (6z/6za). Argument: the program's full path.",
        'Option Explicit',
        'Dim a, p, sh, fso, maj, mn, needs, msg',
        'Set sh = CreateObject("WScript.Shell"): Set fso = CreateObject("Scripting.FileSystemObject")',
        'If WScript.Arguments.Count = 0 Then WScript.Quit',
        'p = WScript.Arguments(0)',
        'maj = PeWord(p, 48) : mn = PeWord(p, 50)   \' MajorSubsystemVersion, Minor',
        'Dim omaj : omaj = PeWord(p, 40)            \' MajorOperatingSystemVersion',
        'If omaj > maj Then maj = omaj',
        'needs = (maj > %d) Or (maj = %d And mn > %d)' % (g[0], g[0], g[1]),
        'If Not needs Then',
        '  sh.Run """" & p & """", 1, False : WScript.Quit',
        'End If',
        ('  Dim ex, rq' if wine else '  Dim kex, dll, f'),
    ] + ([
        # XP / Vista: the hypervisor's Wine route. Write the request to the exchange drive (the hypervisor reads it).
        'ex = ExchangeDrive()',
        'If ex = "" Then',
        '  MsgBox "Charon: put this program on the exchange drive to run it through the hypervisor.", 48, "Charon compatibility"',
        '  WScript.Quit',
        'End If',
        'Dim rqf : Set rqf = fso.CreateTextFile(ex & "CHARON-COMPAT.REQ", True)',
        'rqf.WriteLine fso.GetFileName(p) : rqf.Close',
        'MsgBox "Charon runs this program through Wine in the hypervisor; NVDA in Charon\'s voice reads it.", 64, "Charon compatibility"',
    ] if wine else [
        # 7 / 8 / 8.1: enable VxKex for this program (IFEO VerifierDlls = the VxKex loader DLL) and launch it.
        'kex = FindVxKexDll()',
        'If kex = "" Then',
        '  MsgBox "VxKex is not installed. The secure-use set installs it; run the set again.", 48, "Charon compatibility"',
        '  WScript.Quit',
        'End If',
        'Dim key : key = "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" & fso.GetFileName(p) & "\\"',
        'sh.RegWrite key & "GlobalFlag", 256, "REG_DWORD"',
        'sh.RegWrite key & "VerifierDlls", fso.GetFileName(kex), "REG_SZ"',
        'sh.RegWrite key & "VerifierFlags", 0, "REG_DWORD"',
        'MsgBox "This program needs a newer Windows; Charon\'s compatibility layer (VxKex) is used. Your screen reader reads it.", 64, "Charon compatibility"',
        'sh.Run """" & p & """", 1, False',
    ]) + [
        '',
        "Function PeWord(path, ofsInOpt)",
        '  On Error Resume Next',
        '  Dim st, b, lfa, opt',
        '  Set st = CreateObject("ADODB.Stream") : st.Type = 1 : st.Open : st.LoadFromFile path',
        '  b = st.Read',
        '  lfa = U8(b,60) + U8(b,61)*256 + U8(b,62)*65536 + U8(b,63)*16777216',
        '  opt = lfa + 24',
        '  PeWord = U8(b, opt+ofsInOpt) + U8(b, opt+ofsInOpt+1)*256',
        '  st.Close',
        'End Function',
        'Function U8(b, i)',
        '  U8 = AscB(MidB(b, i+1, 1))',
        'End Function',
    ] + ([
        'Function ExchangeDrive()',
        '  Dim d, dl : ExchangeDrive = ""',
        '  For Each d In fso.Drives',
        '    If d.IsReady Then If d.VolumeName = "CHARONEXCH" Or d.VolumeName = "EXCHANGE" Then ExchangeDrive = d.DriveLetter & ":\\" : Exit Function',
        '  Next',
        'End Function',
    ] if wine else [
        'Function FindVxKexDll()',
        '  Dim roots, r, d, fl : FindVxKexDll = ""',
        '  roots = Array(sh.ExpandEnvironmentStrings("%ProgramFiles%") & "\\VxKex", sh.ExpandEnvironmentStrings("%ProgramFiles%") & "\\VxKexNext", sh.ExpandEnvironmentStrings("%SystemRoot%") & "\\VxKex")',
        '  For Each r In roots',
        '    If fso.FolderExists(r) Then',
        '      For Each d In fso.GetFolder(r).Files',
        '        If LCase(fso.GetExtensionName(d.Name)) = "dll" Then If InStr(LCase(d.Name), "kex") > 0 Then FindVxKexDll = d.Path : Exit Function',
        '      Next',
        '    End If',
        '  Next',
        'End Function',
    ]) + [''])


def guest_shell_reg_cmd():
    """SECURE.CMD lines that copy COMPAT.VBS to a permanent place (%SystemRoot%\\CHARON-COMPAT.VBS - the CHARONSEC
    drive is removed later) and register the shell verb 'Run with Charon compatibility' on every .exe. Explorer
    substitutes the clicked file for %1. %D% is the CHARONSEC folder set by SECURE.CMD (set D=%~dp0)."""
    return ['copy /y "%D%COMPAT.VBS" "%SystemRoot%\\CHARON-COMPAT.VBS" >> %L% 2>&1',
            'reg add "HKCR\\exefile\\shell\\CharonCompat" /ve /d "Run with Charon compatibility" /f >> %L% 2>&1',
            'reg add "HKCR\\exefile\\shell\\CharonCompat\\command" /ve /d "wscript.exe //B \\"%SystemRoot%\\CHARON-COMPAT.VBS\\" \\"%%1\\"" /f >> %L% 2>&1']


# ================================================================================================================
# Runtime: the hypervisor Wine route (NVDA inside the prefix). Needs the guest; proven live, not in the host test.
# ================================================================================================================
COMPAT = '/hv/compat'
PREFIX = COMPAT + '/prefix'
NVDA_DIR = COMPAT + '/nvda'                      # the portable NVDA copy, created inside the prefix once
RUNDIR = '/run/charonhv/compat'
LOG = '/run/charonhv/compat.log'
# pinned versions (6zf: the last that still runs where it must; recorded, not invented). NVDA runs UNDER WINE here,
# not on the old Windows, so the pin is the NVDA that still runs under Wine - 2024.1 (dnl-nash/wineinstall's tested
# fallback). Its SHA-256 is verified on download and written to compat.log; the version is pinned in code.
NVDA_VERSION = '2024.1'
NVDA_URL = 'https://download.nvaccess.org/releases/%s/nvda_%s.exe' % (NVDA_VERSION, NVDA_VERSION)
# winetricks verbs (dnl-nash/wineinstall's set, trimmed to what a reader needs; dotnet/vb left to the per-program
# set so a plain Win32 program does not wait for them): MSAA so NVDA sees the controls (native oleacc, better than
# Wine's builtin), and a stable Wine (no crash dialog stealing focus). Applied best-effort: a verb that cannot
# download its component is logged and the rest continues (Wine's builtins are used).
WINETRICKS_BASE = ('nocrashdialog', 'usetakefocus=y', 'msaa')
# winetricks is NOT an Alpine package (checked v3.24: "no such package"); the official script is fetched and run
# instead (Winetricks/winetricks on GitHub, LGPL-3.0). Pinned by the SHA-256 recorded on first fetch.
WINETRICKS_URL = 'https://raw.githubusercontent.com/Winetricks/winetricks/20260125/src/winetricks'
WINETRICKS = COMPAT + '/winetricks'
APK = ('wine', 'cabextract', 'xorg-server', 'xf86-input-libinput', 'xf86-video-vmware',
       'openbox', 'setxkbmap', 'xkbcomp', 'xkeyboard-config', 'font-dejavu', 'imagemagick', 'alsa-utils')


def ready():
    return os.path.exists(os.path.join(NVDA_DIR, 'nvda.exe'))


def plan():
    """(megabytes, None) or (0, reason): the Alpine packages the Wine route needs (Wine itself is the big one)."""
    import hvpkg
    return hvpkg.plan(APK)


def _sh(cmd, e, timeout, logf):
    return subprocess.run(cmd, env=e, stdin=subprocess.DEVNULL, stdout=logf, stderr=logf, timeout=timeout).returncode


def install_packages(say, T, log):
    """Fetch and install the Alpine packages the Wine route needs (no X server required). Returns None or the reason."""
    import hvpkg
    os.makedirs(RUNDIR, exist_ok=True)
    os.makedirs(COMPAT, exist_ok=True)
    if hvpkg.installed(('wine',)):
        return None
    mb, _n, err = hvpkg.plan(APK)
    if err:
        return ('Wine could not be prepared: %s' % err, 'Nie udało się przygotować Wine: %s' % err)
    say(T('The compatibility layer fetches Wine and its helpers, about %d megabytes.' % mb,
          'Warstwa zgodności pobiera Wine i pomocników, około %d megabajtów.' % mb))
    err = hvpkg.install(APK, timeout=3600)
    if err:
        return ('Wine could not be installed: %s' % err, 'Nie udało się zainstalować Wine: %s' % err)
    return None


def prepare(e, say, T, log):
    """Create the Wine prefix once (needs the X server already up, env `e` with DISPLAY): 32-bit prefix, the base
    winetricks verbs best-effort, and a portable Windows NVDA with synth = its own eSpeak NG (a Charon voice) and no
    welcome dialog. Returns None, or the reason it could not."""
    logf = open(LOG, 'ab')
    if not os.path.isdir(PREFIX):
        say(T('Preparing the Windows environment; this is done once.',
              'Przygotowuję środowisko Windows; robię to raz.'))
        try:
            _sh(['wineboot', '--init'], e, 900, logf)
        except subprocess.TimeoutExpired:
            return ('Preparing Wine took too long', 'Przygotowanie Wine trwało zbyt długo')
        _winetricks(e, say, T, logf, log)
    if not ready():
        err = _install_nvda(e, say, T, logf, log)
        if err:
            return err
    return None


def _winetricks(e, say, T, logf, log):
    """Fetch the official winetricks script (pinned, SHA-256 recorded) and apply the base verbs best-effort; a verb
    that cannot fetch its component is logged and skipped (Wine's builtins are used)."""
    import hashlib, urllib.request
    if not os.path.exists(WINETRICKS):
        try:
            req = urllib.request.Request(WINETRICKS_URL, headers={'User-Agent': 'charon-hypervisor'})
            data = urllib.request.urlopen(req, timeout=120).read()
            open(WINETRICKS, 'wb').write(data)
            os.chmod(WINETRICKS, 0o755)
            log('compat: winetricks fetched, sha256 %s, %d bytes' % (hashlib.sha256(data).hexdigest(), len(data)))
        except Exception as ex:
            log('compat: winetricks not fetched (%s); Wine builtins only' % ex)
            return
    for verb in WINETRICKS_BASE:
        try:
            rc = _sh(['sh', WINETRICKS, '-q', verb], e, 600, logf)
            log('compat: winetricks %s rc %s' % (verb, rc))
        except subprocess.TimeoutExpired:
            log('compat: winetricks %s timed out, skipped' % verb)


def _install_nvda(e, say, T, logf, log):
    """Download the pinned Windows NVDA, verify and record its SHA-256, create a portable copy inside the prefix,
    and write its configuration (synth = NVDA's own eSpeak NG = a Charon voice; no welcome dialog)."""
    import hashlib, urllib.request, hvvoice
    os.makedirs(COMPAT, exist_ok=True)
    exe = os.path.join(COMPAT, 'nvda_%s.exe' % NVDA_VERSION)
    if not os.path.exists(exe):
        say(T('Fetching NVDA %s, the screen reader that reads the program inside Wine.' % NVDA_VERSION,
              'Pobieram NVDA %s, czytnik ekranu, który czyta program w środowisku Wine.' % NVDA_VERSION))
        try:
            req = urllib.request.Request(NVDA_URL, headers={'User-Agent': 'charon-hypervisor'})
            data = urllib.request.urlopen(req, timeout=300).read()
        except Exception as ex:
            return ('NVDA could not be fetched: %s' % ex, 'Nie udało się pobrać NVDA: %s' % ex)
        open(exe + '.part', 'wb').write(data)
        os.replace(exe + '.part', exe)
        log('compat: NVDA %s fetched, sha256 %s, %d bytes' % (NVDA_VERSION, hashlib.sha256(data).hexdigest(), len(data)))
    # NVDA's launcher makes a portable copy without starting it: --create-portable-silent --portable-path=PATH
    # --minimal (NVDA command-line options). The path is a Windows path (Z: is mapped to /).
    say(T('Setting up NVDA inside Wine.', 'Ustawiam NVDA w środowisku Wine.'))
    os.makedirs(NVDA_DIR, exist_ok=True)
    try:
        _sh(['wine', exe, '--create-portable-silent', '--portable-path=' + _w(NVDA_DIR), '--minimal'], e, 1800, logf)
    except subprocess.TimeoutExpired:
        return ('Setting up NVDA took too long', 'Ustawianie NVDA trwało zbyt długo')
    cfg = os.path.join(NVDA_DIR, 'userConfig')
    os.makedirs(cfg, exist_ok=True)
    polish = False
    try:
        polish = hvvoice.Settings().polish
    except Exception:
        pass
    # synth = espeak: NVDA's bundled eSpeak NG, which is one of Charon's voices (6zg floor). A Charon-synth-server
    # driver reaching all 17 voices is the later improvement; eSpeak NG is the acceptable floor.
    open(os.path.join(cfg, 'nvda.ini'), 'w').write(
        '[general]\nlanguage = %s\nshowWelcomeDialogAtStartup = False\naskToExit = False\n'
        '[speech]\nsynth = espeak\n[update]\nallowUsageStats = False\nautoCheck = False\n'
        % ('pl' if polish else 'en'))
    return None


def _w(path):
    """A hypervisor path as Wine sees it (Z: is the drive mapped to /)."""
    return 'Z:' + path.replace('/', '\\')


def _xorg(e, logf):
    """Xorg on vt7 with the modesetting driver (the vmware driver segfaults; see hvmodern) + Openbox so the
    program window is the active, maximised one. Returns the Xorg process or raises."""
    for f in ('/tmp/.X11-unix/X0', '/tmp/.X0-lock'):
        try:
            os.remove(f)
        except OSError:
            pass
    conf = RUNDIR + '/xorg.conf'
    open(conf, 'w').write('Section "Device"\n Identifier "kms"\n Driver "modesetting"\n Option "AccelMethod" "none"\n'
                          ' Option "ShadowFB" "on"\nEndSection\nSection "ServerFlags"\n Option "AutoAddGPU" "off"\nEndSection\n')
    x = subprocess.Popen(['Xorg', ':0', 'vt7', '-config', conf, '-nolisten', 'tcp', '-noreset',
                          '-logfile', '/run/charonhv/compat-xorg.log'], env=e, stdout=logf, stderr=logf)
    for _ in range(60):
        if os.path.exists('/tmp/.X11-unix/X0') or x.poll() is not None:
            break
        time.sleep(0.25)
    if x.poll() is not None:
        raise RuntimeError('Xorg ended at once (/run/charonhv/compat-xorg.log)')
    rc = RUNDIR + '/openbox-rc.xml'
    open(rc, 'w').write('<?xml version="1.0"?>\n<openbox_config xmlns="http://openbox.org/3.4/rc"><keyboard></keyboard>'
                        '<mouse></mouse><desktops><number>1</number></desktops><applications><application class="*">'
                        '<decor>no</decor><maximized>yes</maximized><focus>yes</focus></application></applications>'
                        '</openbox_config>\n')
    subprocess.run(['setxkbmap', 'us'], env=e, capture_output=True)
    return x


def run(exe_path, say, T, log, program_name='', reader=None):
    """Run a Windows program under Wine in the hypervisor, read by the reader the launcher chose (6zh): CTHULHU by
    default (its native Wine bridge, reads standard Win32/MSAA controls, speaks Charon's voices through
    speech-dispatcher), or NVDA inside the prefix as the fallback (6zg). Blocks until the program's window ends.
    Returns None or (reason_en, reason_pl)."""
    reader = reader or WINE_READER_DEFAULT
    if reader == 'cthulhu':
        err = run_cthulhu(exe_path, say, T, log, program_name)
        if err is None:
            return None
        # Cthulhu could not be prepared or started: fall back to NVDA in the prefix (6zh), saying so once.
        log('compat: Cthulhu route failed (%s); falling back to NVDA in the prefix' % (err[0],))
        say(T('Cthulhu could not start; falling back to NVDA inside Wine.',
              'Cthulhu nie wystartował; przełączam na NVDA w środowisku Wine.'))
    return _run_nvda(exe_path, say, T, log, program_name)


def _run_nvda(exe_path, say, T, log, program_name=''):
    """Run a Windows program under Wine with NVDA inside the prefix reading it. Blocks until the program's window
    (and NVDA) end. Returns None or the reason. Audio goes out through the hypervisor's normal ALSA path (the shared
    dmix 'charonguest'), not through PipeWire - NVDA's eSpeak NG mixes with the hypervisor's voice."""
    os.makedirs(RUNDIR, exist_ok=True)
    logf = open(LOG, 'ab')
    err = install_packages(say, T, log)
    if err:
        return err
    e = dict(os.environ)
    # Wine 11 on Alpine is a new-WoW64 build: WINEARCH=win32 is rejected ("not supported in wow64 mode"), so the
    # prefix is win64 and runs the 32-bit Windows programs (Notepad++, NVDA) through the new WoW64.
    e.update({'WINEPREFIX': PREFIX, 'WINEARCH': 'win64', 'HOME': COMPAT, 'WINEDEBUG': '-all', 'DISPLAY': ':0'})
    procs = []
    try:
        x = _xorg(e, logf)
        procs.append(x)
        procs.append(subprocess.Popen(['openbox', '--config-file', RUNDIR + '/openbox-rc.xml'], env=e, stdout=logf, stderr=logf))
        time.sleep(0.5)
        err = prepare(e, say, T, log)       # the prefix + NVDA (needs the X server now up), done once
        if err:
            return err
        name = program_name or os.path.basename(exe_path)
        say(T('%s opens in Wine; NVDA in Charon\'s voice reads it. Left Control and Left Alt give the keyboard back.' % name,
              '%s otwiera się w Wine; NVDA głosem Charona go czyta. Lewy Control i lewy Alt oddają klawiaturę.' % name),
            interrupt=False)
        # NVDA first (so it is reading when the program's window appears), then the program in the same prefix
        nvda = os.path.join(NVDA_DIR, 'nvda.exe')
        procs.append(subprocess.Popen(['wine', nvda, '-c', os.path.join(NVDA_DIR, 'userConfig'), '--minimal'],
                                      env=e, stdout=logf, stderr=logf))
        time.sleep(6)                             # NVDA loads under TCG slowly
        prog = subprocess.Popen(['wine', exe_path], env=e, stdout=logf, stderr=logf)
        log('compat: wine %s pid %d, NVDA in the prefix' % (exe_path, prog.pid))
        prog.wait()
        return None
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as ex:
        return ('The Wine route failed: %s' % ex, 'Trasa Wine zawiodła: %s' % ex)
    finally:
        stop(procs)


def stop(procs):
    for p in procs:
        try:
            p.terminate()
        except Exception:
            pass
    subprocess.run(['pkill', '-f', 'nvda.exe'], capture_output=True)
    subprocess.run(['wineserver', '-k'], env=dict(os.environ, WINEPREFIX=PREFIX), capture_output=True)
    x11 = subprocess.run(['pgrep', '-x', 'Xorg'], capture_output=True).returncode == 0
    for name in ('openbox', 'Xorg'):
        subprocess.run(['pkill', '-x', name], capture_output=True)
    if x11:
        for _ in range(40):
            if subprocess.run(['pgrep', '-x', 'Xorg'], capture_output=True).returncode != 0:
                break
            time.sleep(0.25)
        else:
            subprocess.run(['pkill', '-9', '-x', 'Xorg'], capture_output=True)
            time.sleep(0.5)
        subprocess.run(['chvt', '1'], capture_output=True)       # the hypervisor's own screen again


# ================================================================================================================
# Runtime: the hypervisor Wine route's DEFAULT reader - Cthulhu's native Wine accessibility bridge (6zh resolved).
# Cthulhu (an Orca fork, LGPL-2.1) runs as a Linux reader; its WineAccessibility plugin starts one
# cthulhu-wine-access.exe helper (built here with Winegcc, 64-bit) inside the same Wine prefix as the program. The
# helper reads standard Win32/MSAA controls (focus/name/value/state/selection, with a Win32-class fallback adapted
# from Mudb0y/wine-a11y) and forwards them over Cthulhu's own D-Bus (org.stormux.Cthulhu1.Service) to Cthulhu,
# which speaks through speech-dispatcher's "charon" module (sd_charon) in Charon's voices - the same speech path as
# the modern browser mode (hvmodern). Limit said aloud and documented: STANDARD controls only; custom-drawn,
# DirectX and game UIs are not read and fall to the NVDA-in-the-prefix fallback. Needs the guest; proven live.
# ================================================================================================================
CTHULHU_TAG = '2026.08.06'                       # pinned tag (6zf: a maintained release); recorded in LICENSES.txt
CTHULHU_COMMIT = 'a02388c7fe5c79605b386a379eabc339d2e2c5fc'
CTHULHU_URL = 'https://git.stormux.org/storm/cthulhu.git'
CTHULHU_BUILD = COMPAT + '/build/cthulhu'        # the pinned source checkout
CTHULHU_SRC = CTHULHU_BUILD + '/src'             # run from source (pure Python; only the helper is compiled)
CTHULHU_PREFIXDIR = COMPAT + '/cthulhu'          # install prefix for the generated config + the helper
CTHULHU_LAUNCHER = CTHULHU_PREFIXDIR + '/bin/cthulhu'   # the generated launcher, OUTSIDE the package (else the
#   script dir shadows the "cthulhu" package with its own cthulhu.py and relative imports break)
CTHULHU_HELPER = CTHULHU_PREFIXDIR + '/libexec/cthulhu/wine/cthulhu-wine-access.exe'
CTHULHU_HOME = COMPAT + '/cthulhu-home'          # Cthulhu's own config/prefs (never Charon's)
CTHULHU_LOG = '/run/charonhv/compat-cthulhu.out'
SPEECHD_DIR = CTHULHU_HOME + '/.config/speech-dispatcher'
SPEECHD_LOGDIR = '/run/charonhv/compat-sd'       # speech-dispatcher writes speech-dispatcher.log here (LogLevel 4)
# the extra Alpine packages the Cthulhu route needs beyond APK (the Wine set). pygobject + AT-SPI + GTK typelibs for
# the Orca-fork runtime, speech-dispatcher + its Python binding + dasbus for the D-Bus service and speech, and the
# winegcc toolchain (gcc/g++/cmake/make/glib-dev/pkgconf/widl-from-wine-dev) to build the helper once.
APK_CTHULHU = ('py3-gobject3', 'py3-pluggy', 'py3-tomlkit', 'py3-dasbus', 'py3-speech-dispatcher',
               'speech-dispatcher', 'at-spi2-core', 'gtk+3.0', 'gobject-introspection', 'libwnck3', 'dbus', 'dbus-x11',
               'git', 'wine-dev', 'cmake', 'make', 'gcc', 'g++', 'musl-dev', 'glib-dev', 'pkgconf')


CTHULHU_GENERATED = ('cthulhu_i18n.py', 'cthulhu_platform.py')   # into the package dir


def cthulhu_ready():
    return os.access(CTHULHU_HELPER, os.X_OK) and os.path.exists(CTHULHU_LAUNCHER) and all(
        os.path.exists(os.path.join(CTHULHU_SRC, 'cthulhu', f)) for f in CTHULHU_GENERATED)


def _run(cmd, env=None, timeout=1800, cwd=None):
    logf = open(CTHULHU_LOG + '.build', 'ab')
    return subprocess.run(cmd, env=env, cwd=cwd, stdin=subprocess.DEVNULL, stdout=logf, stderr=logf, timeout=timeout).returncode


def build_cthulhu(say, T, log):
    """Fetch Cthulhu's pinned source, generate the meson configure_file() outputs needed to run it from source, and
    build the 64-bit cthulhu-wine-access.exe helper with Winegcc. Done once; returns None or (reason_en, reason_pl)."""
    import hvpkg
    if cthulhu_ready():
        return None
    os.makedirs(COMPAT + '/build', exist_ok=True)
    if not hvpkg.installed(('wine',)):
        return ('Wine is not installed yet', 'Wine nie jest jeszcze zainstalowane')
    # the build + runtime packages (fetched once; the size is said by install_packages for the Wine set already)
    if not hvpkg.installed(APK_CTHULHU):
        mb, _n, err = hvpkg.plan(APK_CTHULHU)
        if err:
            return ('Cthulhu could not be prepared: %s' % err, 'Nie udało się przygotować Cthulhu: %s' % err)
        say(T('Fetching Cthulhu\'s reader and build tools, about %d megabytes.' % mb,
              'Pobieram czytnik Cthulhu i narzędzia budowania, około %d megabajtów.' % mb))
        err = hvpkg.install(APK_CTHULHU, timeout=3600)
        if err:
            return ('Cthulhu\'s packages could not be installed: %s' % err,
                    'Nie udało się zainstalować pakietów Cthulhu: %s' % err)
    say(T('Building Cthulhu\'s Wine accessibility bridge; this is done once.',
          'Buduję most dostępności Wine dla Cthulhu; robię to raz.'))
    # 1) the pinned checkout
    if not os.path.isdir(os.path.join(CTHULHU_BUILD, '.git')):
        import shutil
        shutil.rmtree(CTHULHU_BUILD, ignore_errors=True)
        e = dict(os.environ, GIT_TERMINAL_PROMPT='0')
        if _run(['git', 'clone', '--depth', '1', '--branch', CTHULHU_TAG, CTHULHU_URL, CTHULHU_BUILD], e, 1200):
            return ('Cthulhu source could not be fetched', 'Nie udało się pobrać źródeł Cthulhu')
    head = subprocess.run(['git', '-C', CTHULHU_BUILD, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    log('compat: Cthulhu %s at %s' % (CTHULHU_TAG, head))
    # 2) the meson configure_file() outputs (run from source without a full meson install); always (re)written - cheap
    _generate_cthulhu_config(head)
    import shutil
    # 3) the 64-bit helper, with Winegcc (the project's own CMake build) - only if not already built
    if not os.access(CTHULHU_HELPER, os.X_OK):
        wa = CTHULHU_BUILD + '/wine-access'
        bd = COMPAT + '/build/wa-build'
        shutil.rmtree(bd, ignore_errors=True)
        if _run(['cmake', '-S', wa, '-B', bd, '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
                 '-DCMAKE_TOOLCHAIN_FILE=' + wa + '/winelib-toolchain.cmake'], dict(os.environ), 900):
            return ('Cthulhu\'s Wine bridge could not be configured', 'Nie udało się skonfigurować mostu Wine Cthulhu')
        if _run(['cmake', '--build', bd, '--parallel'], dict(os.environ), 1800):
            return ('Cthulhu\'s Wine bridge could not be built', 'Nie udało się zbudować mostu Wine Cthulhu')
        dest = os.path.dirname(CTHULHU_HELPER)
        os.makedirs(dest, exist_ok=True)
        for f in ('cthulhu-wine-access.exe', 'cthulhu-wine-access.exe.so'):
            shutil.copy2(os.path.join(bd, f), os.path.join(dest, f))
            os.chmod(os.path.join(dest, f), 0o755)
        log('compat: cthulhu-wine-access.exe built and installed at %s' % CTHULHU_HELPER)
    return None if cthulhu_ready() else ('Cthulhu bridge build incomplete', 'Budowa mostu Cthulhu niepełna')


def _generate_cthulhu_config(revision):
    """Write the three files meson's configure_file() would generate (cthulhu_bin.py, cthulhu_i18n.py,
    cthulhu_platform.py), substituting the project's own templates so Cthulhu runs from the source tree."""
    src = os.path.join(CTHULHU_SRC, 'cthulhu')
    datadir = CTHULHU_PREFIXDIR + '/share'
    # the two files that live INSIDE the package (gettext aliases, platform/version)
    pkg_subs = {
        'cthulhu_i18n.py.in': {'@GETTEXT_PACKAGE@': 'cthulhu', '@prefix@': CTHULHU_PREFIXDIR,
                               '@datadir@': datadir, '@localedir@': datadir + '/locale'},
        'cthulhu_platform.py.in': {'@REVISION@': revision, '@prefix@': CTHULHU_PREFIXDIR, '@PACKAGE@': 'cthulhu',
                                   '@datadir@': datadir, '@LOUIS_TABLE_DIR@': '/usr/share/liblouis/tables',
                                   '@bindir@': 'bin', '@libdir@': 'lib', '@sysconfdir@': '/etc',
                                   '@localstatedir@': '/var'},
    }
    for infile, table in pkg_subs.items():
        t = open(os.path.join(src, infile)).read()
        for k, v in table.items():
            t = t.replace(k, v)
        open(os.path.join(src, infile[:-3]), 'w').write(t)
    # the launcher goes OUTSIDE the package (bin/), with pythondir -> the source tree
    bin_subs = {'@PYTHON@': '/usr/bin/python3', '@prefix@': CTHULHU_PREFIXDIR,
                '@pythondir@': CTHULHU_SRC, '@PYTHON_PREFIX@': CTHULHU_SRC,
                '@bindir@': 'bin', '@libdir@': 'lib', '@datadir@': datadir}
    t = open(os.path.join(src, 'cthulhu_bin.py.in')).read()
    for k, v in bin_subs.items():
        t = t.replace(k, v)
    os.makedirs(os.path.dirname(CTHULHU_LAUNCHER), exist_ok=True)
    open(CTHULHU_LAUNCHER, 'w').write(t)
    os.chmod(CTHULHU_LAUNCHER, 0o755)


def _cthulhu_speech_config():
    """Point Cthulhu's speech-dispatcher at Charon's voices (the sd_charon module), exactly as the modern browser
    mode does (hvmodern.speech_config): default module "charon", linked into the user module folder."""
    os.makedirs(SPEECHD_DIR + '/modules', exist_ok=True)
    os.makedirs(SPEECHD_LOGDIR, exist_ok=True)
    open(SPEECHD_DIR + '/speechd.conf', 'w').write(
        'AddModule "charon" "sd_charon" ""\nDefaultModule charon\nLanguageDefaultModule "en" "charon"\n'
        'LanguageDefaultModule "pl" "charon"\nAudioOutputMethod "pulse"\nLogLevel 4\nLogDir "%s"\n' % SPEECHD_LOGDIR)
    mods = CTHULHU_HOME + '/.local/libexec/speech-dispatcher-modules'
    os.makedirs(mods, exist_ok=True)
    link = mods + '/sd_charon'
    if os.path.lexists(link):
        os.remove(link)
    os.symlink('/usr/lib/charonhv/sd_charon', link)


def run_cthulhu(exe_path, say, T, log, program_name=''):
    """Run a Windows program under Wine, read by Cthulhu's native Wine bridge speaking Charon's voices. Blocks until
    the program's window ends. Returns None or (reason_en, reason_pl). The chain, under the hypervisor's own Xorg on
    vt7: a session D-Bus, the AT-SPI bus, speech-dispatcher (module sd_charon = Charon's voices), Cthulhu (its
    WineAccessibility plugin auto-manages the prefix), then the Wine program. Cthulhu starts the helper inside the
    program's prefix; the helper reads standard Win32/MSAA controls and forwards them over Cthulhu's D-Bus."""
    os.makedirs(RUNDIR, mode=0o700, exist_ok=True)
    logf = open(LOG, 'ab')
    err = install_packages(say, T, log)             # the Wine set (Wine itself)
    if err:
        return err
    err = build_cthulhu(say, T, log)                # the Cthulhu reader + the helper, once
    if err:
        return err
    e = dict(os.environ)
    e.update({'WINEPREFIX': PREFIX, 'WINEARCH': 'win64', 'HOME': COMPAT, 'WINEDEBUG': '-all', 'DISPLAY': ':0',
              'XDG_RUNTIME_DIR': RUNDIR,
              # speech-dispatcher finds the "charon" output module (sd_charon = Charon's voices) in the user module
              # dir XDG_DATA_HOME/../libexec/speech-dispatcher-modules (as the modern browser mode does); without
              # this it uses the system dir, does not find sd_charon and falls back to plain eSpeak NG.
              'XDG_DATA_HOME': CTHULHU_HOME + '/.local/share'})
    procs = []
    try:
        if not os.path.isdir(PREFIX):               # the prefix (shared with the NVDA fallback), created once
            say(T('Preparing the Windows environment; this is done once.',
                  'Przygotowuję środowisko Windows; robię to raz.'))
            _sh(['wineboot', '--init'], e, 900, logf)
        x = _xorg(e, logf)
        procs.append(x)
        procs.append(subprocess.Popen(['openbox', '--config-file', RUNDIR + '/openbox-rc.xml'], env=e, stdout=logf, stderr=logf))
        time.sleep(0.5)
        # session D-Bus (the helper and Cthulhu share it; the program is started with its address so the manager
        # copies it to the helper)
        import re as _re
        dbus = subprocess.run(['dbus-launch', '--sh-syntax'], capture_output=True, text=True, env=e)
        for k, v in _re.findall(r"(DBUS_SESSION_BUS_\w+)='([^']*)'", dbus.stdout):
            e[k] = v
        m = _re.search(r"DBUS_SESSION_BUS_PID=(\d+)", dbus.stdout)
        if m:
            open(RUNDIR + '/dbus-cthulhu.pid', 'w').write(m.group(1))
        # the AT-SPI bus on this session (Cthulhu registers AT-SPI listeners at start-up)
        if os.path.exists('/usr/libexec/at-spi-bus-launcher'):
            procs.append(subprocess.Popen(['/usr/libexec/at-spi-bus-launcher', '--launch-immediately'], env=e, stdout=logf, stderr=logf))
            for _ in range(20):
                if os.path.exists(RUNDIR + '/at-spi/bus'):
                    break
                time.sleep(0.25)
            if os.path.exists(RUNDIR + '/at-spi/bus'):
                e['AT_SPI_BUS_ADDRESS'] = 'unix:path=%s/at-spi/bus' % RUNDIR
        # speech-dispatcher with Charon's voices (sd_charon), started on this session's XDG_RUNTIME_DIR so Cthulhu's
        # speechd client reaches it; LogLevel high so the spoken lines are recorded for the proof
        _cthulhu_speech_config()
        procs.append(subprocess.Popen(['speech-dispatcher', '-s', '-t', '0', '-C', SPEECHD_DIR],
                                      env=e, stdout=logf, stderr=logf))
        time.sleep(1.0)
        # Cthulhu itself (from the source tree via the generated launcher). Its WineAccessibility plugin is on by
        # default and finds the helper through CTHULHU_WINE_ACCESS_HELPER.
        polish = False
        try:
            import hvvoice
            polish = hvvoice.Settings().polish
        except Exception:
            pass
        ce = dict(e)
        ce.update({'HOME': CTHULHU_HOME, 'XDG_SESSION_TYPE': 'x11', 'PYTHONPATH': CTHULHU_SRC,
                   'CTHULHU_WINE_ACCESS_HELPER': CTHULHU_HELPER, 'NO_AT_BRIDGE': '0',
                   'LANG': 'pl_PL.UTF-8' if polish else 'en_US.UTF-8',
                   'GTK_MODULES': 'gail:atk-bridge'})
        os.makedirs(CTHULHU_HOME, exist_ok=True)
        clog = open(CTHULHU_LOG, 'ab')
        procs.append(subprocess.Popen(['python3', CTHULHU_LAUNCHER,
                                       '--debug', '--debug-file', CTHULHU_LOG + '.debug', '--replace'],
                                      env=ce, stdout=clog, stderr=clog, cwd=CTHULHU_HOME))
        time.sleep(8)                                # Cthulhu loads and owns the D-Bus service (slow under TCG)
        name = program_name or os.path.basename(exe_path)
        say(T('%s opens in Wine; Cthulhu in Charon\'s voice reads it. Left Control and Left Alt give the keyboard back. '
              'Cthulhu reads standard controls; custom-drawn or game screens fall to NVDA.' % name,
              '%s otwiera się w Wine; czyta go Cthulhu głosem Charona. Lewy Control i lewy Alt oddają klawiaturę. '
              'Cthulhu czyta standardowe kontrolki; ekrany rysowane ręcznie albo gry przechodzą do NVDA.' % name),
            interrupt=False)
        # the program, carrying the session D-Bus/DISPLAY so the manager hands them to the helper it starts
        prog = subprocess.Popen(['wine', exe_path], env=e, stdout=logf, stderr=logf)
        log('compat: cthulhu route, wine %s pid %d; helper managed by Cthulhu' % (exe_path, prog.pid))
        prog.wait()
        return None
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as ex:
        return ('The Cthulhu Wine route failed: %s' % ex, 'Trasa Wine z Cthulhu zawiodła: %s' % ex)
    finally:
        _cthulhu_stop(procs)


def _cthulhu_stop(procs):
    for p in procs:
        try:
            p.terminate()
        except Exception:
            pass
    subprocess.run(['pkill', '-f', 'cthulhu_bin.py'], capture_output=True)
    subprocess.run(['pkill', '-f', 'cthulhu-wine-access'], capture_output=True)
    subprocess.run(['wineserver', '-k'], env=dict(os.environ, WINEPREFIX=PREFIX), capture_output=True)
    for pat in ('speech-dispatcher', '/usr/libexec/at-spi'):
        subprocess.run(['pkill', '-f', pat], capture_output=True)
    try:
        os.kill(int(open(RUNDIR + '/dbus-cthulhu.pid').read()), 15)
    except (OSError, ValueError):
        pass
    import shutil
    shutil.rmtree(RUNDIR + '/at-spi', ignore_errors=True)
    x11 = subprocess.run(['pgrep', '-x', 'Xorg'], capture_output=True).returncode == 0
    for name in ('openbox', 'Xorg'):
        subprocess.run(['pkill', '-x', name], capture_output=True)
    if x11:
        for _ in range(40):
            if subprocess.run(['pgrep', '-x', 'Xorg'], capture_output=True).returncode != 0:
                break
            time.sleep(0.25)
        else:
            subprocess.run(['pkill', '-9', '-x', 'Xorg'], capture_output=True)
            time.sleep(0.5)
        subprocess.run(['chvt', '1'], capture_output=True)
