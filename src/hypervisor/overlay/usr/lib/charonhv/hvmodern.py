# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: the modern browser mode on the real screen (6s) - DRM video (Widevine) the old guests cannot have.

Alpine's own browsers are built on musl and Widevine's CDM (a glibc library from Google) does not load in them. Flatpak
(Alpine community package, LGPL-2.1) runs Flathub's glibc runtime instead: Mozilla's own Firefox from Flathub
(org.mozilla.firefox, verified publisher Mozilla, MPL-2.0) downloads Google's Widevine CDM the way it does on every
Linux system (Firefox's media plug-in updater, after the user allows DRM) - nothing is patched or bypassed.
It runs full screen on the hypervisor's own screen under cage (a Wayland kiosk compositor, MIT), sound goes through
PipeWire to the hypervisor's mixer, Orca (LGPL-2.1+, GNOME's screen reader) reads it with speech-dispatcher and
eSpeak NG. Protected video is shown ONLY here, never re-encoded or passed into a guest. Everything is fetched the first
time the mode is opened, after the size is said; the Flatpak files live on the Hypervisor partition (/hv/flatpak).
The host key (Left Control + Left Alt) ends the mode and gives the keyboard back to the hypervisor."""
import os, re, shutil, subprocess, time

APK = ('flatpak', 'cage', 'pipewire', 'pipewire-pulse', 'pipewire-alsa', 'wireplumber', 'dbus', 'dbus-x11', 'mesa-dri-gallium', 'mesa-egl', 'mesa-gbm',
       'font-dejavu', 'orca', 'speech-dispatcher', 'py3-speech-dispatcher', 'at-spi2-core', 'xwayland', 'seatd', 'seatd-launch', 'eudev', 'pulseaudio-utils', 'orca-lang', 'alsa-utils', 'samba-server')
APP = 'org.mozilla.firefox'
FLATHUB = 'https://dl.flathub.org/repo/flathub.flatpakrepo'
ROOT = '/hv/flatpak'
PROFILE = '/hv/modern-profile'
RUNDIR = '/run/charonhv/modern'
ENV = {'FLATPAK_USER_DIR': ROOT, 'XDG_RUNTIME_DIR': RUNDIR, 'HOME': PROFILE, 'XDG_DATA_HOME': PROFILE + '/.local/share',
       'XDG_CONFIG_HOME': PROFILE + '/.config', 'LANG': 'pl_PL.UTF-8'}
PREFS = ('user_pref("media.eme.enabled", true);\n'                      # the user asked for DRM video: Firefox's own switch
         'user_pref("media.gmp-widevinecdm.enabled", true);\n'
         'user_pref("media.gmp-widevinecdm.visible", true);\n'
         'user_pref("accessibility.force_disabled", 0);\n'
         'user_pref("browser.shell.checkDefaultBrowser", false);\n'
         'user_pref("datareporting.policy.dataSubmissionEnabled", false);\n'
         'user_pref("media.autoplay.default", 0);\n'               # the user opened the page to watch it
         'user_pref("media.autoplay.blocking_policy", 0);\n'
         # 6v: downloads land in one folder; the hypervisor scans them and gives them to the machine (hvshare)
         'user_pref("browser.download.folderList", 2);\n'
         'user_pref("browser.download.dir", "/hv/modern-profile/Downloads");\n'
         'user_pref("browser.download.useDownloadDir", true);\n'
         'user_pref("browser.download.always_ask_before_handling_new_types", false);\n')


def env():
    e = dict(os.environ)
    e.update(ENV)
    return e


def _run(cmd, timeout=3600):
    os.makedirs(RUNDIR, mode=0o700, exist_ok=True)
    os.makedirs(PROFILE, exist_ok=True)
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env())


def flatpak_ready():
    return shutil.which('flatpak') is not None and os.path.isdir(os.path.join(ROOT, 'app', APP))


def flatpak_plan():
    """(megabytes, None) or (0, reason): Firefox and its runtime from Flathub, from `flatpak remote-info`."""
    r = _run(['flatpak', '--user', 'remote-add', '--if-not-exists', 'flathub', FLATHUB], 300)
    if r.returncode != 0:
        return 0, (r.stderr.strip() or 'flatpak remote-add failed')[:200]
    total = 0.0
    refs = [APP]
    r = _run(['flatpak', '--user', 'remote-info', 'flathub', APP], 300)
    if r.returncode != 0:
        return 0, (r.stderr.strip() or 'flatpak remote-info failed')[:200]
    m = re.search(r'Runtime:\s*(\S+)', r.stdout)
    if m:
        refs.append(m.group(1))
    for ref in refs:
        r = _run(['flatpak', '--user', 'remote-info', 'flathub', ref], 300)
        d = re.search(r'Download:\s*([\d.,]+)\s*([kMG]B)', r.stdout)
        if d:
            total += float(d.group(1).replace(',', '.')) * {'kB': 0.001, 'MB': 1, 'GB': 1000}[d.group(2)]
    return round(total), None


def flatpak_install():
    r = _run(['flatpak', '--user', 'install', '-y', '--noninteractive', 'flathub', APP], 7200)
    if r.returncode != 0:
        return (r.stderr.strip().splitlines() or ['flatpak install failed'])[-1][:200]
    prof = os.path.join(PROFILE, 'charon.default')
    os.makedirs(prof, exist_ok=True)
    open(os.path.join(prof, 'user.js'), 'w').write(PREFS)
    return None


def speech_config():
    """6u: the screen reader speaks with CHARON'S voices. speech-dispatcher's default module "charon" is
    /usr/lib/charonhv/sd_charon (speech-dispatcher's module protocol, the hypervisor's own voice: TVox, every RHVoice
    voice of Charon's packs, eSpeak NG - Sound > Voice's list), linked into the user module folder speech-dispatcher
    searches (XDG_DATA_HOME/../libexec/speech-dispatcher-modules). Measured 6s: the generic module crashed on Alpine
    at the first message; 'GenericStripPunctChars ""' was also a syntax error. Measured 6s: Alpine's speech-dispatcher
    has no ALSA plug-in (only spd_pulse and spd_oss) - "alsa" made every module fail "Can't initialize audio"; "pulse"
    reaches this session's PipeWire (its eSpeak NG fallback), the charon module plays through Charon's mixer itself."""
    d = PROFILE + '/.config/speech-dispatcher'
    os.makedirs(d + '/modules', exist_ok=True)
    open(d + '/speechd.conf', 'w').write(
        'AddModule "charon" "sd_charon" ""\nDefaultModule charon\nLanguageDefaultModule "en" "charon"\n'
        'LanguageDefaultModule "pl" "charon"\nAudioOutputMethod "pulse"\n')
    mods = PROFILE + '/.local/libexec/speech-dispatcher-modules'
    os.makedirs(mods, exist_ok=True)
    link = mods + '/sd_charon'
    if os.path.lexists(link):
        os.remove(link)
    os.symlink('/usr/lib/charonhv/sd_charon', link)


def start(url, log):
    """Start the session: D-Bus, PipeWire (to the hypervisor's mixer), Orca, and Firefox under cage. Returns the
    cage process (the session ends when it ends), or raises with the reason."""
    os.makedirs(RUNDIR, mode=0o700, exist_ok=True)
    cleanup()
    e = env()
    procs = []
    logf = open('/run/charonhv/modern.log', 'ab')
    dbus = subprocess.run(['dbus-launch', '--sh-syntax'], capture_output=True, text=True, env=e)
    for k, v in re.findall(r"(DBUS_SESSION_BUS_\w+)='([^']*)'", dbus.stdout):
        e[k] = v
    m = re.search(r"DBUS_SESSION_BUS_PID=(\d+)", dbus.stdout)
    if m:
        open(RUNDIR + '/dbus.pid', 'w').write(m.group(1))
    os.makedirs(PROFILE + '/.config/pipewire/pipewire.conf.d', exist_ok=True)
    open(PROFILE + '/.config/pipewire/pipewire.conf.d/charon.conf', 'w').write(
        'context.objects = [ { factory = adapter args = { factory.name = api.alsa.pcm.sink node.name = charon '
        'node.description = "Charon" media.class = Audio/Sink api.alsa.path = "charonguest" priority.session = 3000 } } ]\n')
    # measured 6s: WirePlumber's own ALSA monitor opened hw:0,0 directly as the default sink, past Charon's dmix (the
    # hypervisor's voice and the guests share the card through it) - Firefox's stream then never started (stuck at
    # 0:00, no sound). Only the "charon" sink above exists; the ALSA, Bluetooth and camera monitors are off.
    os.makedirs(PROFILE + '/.config/wireplumber/wireplumber.conf.d', exist_ok=True)
    open(PROFILE + '/.config/wireplumber/wireplumber.conf.d/charon.conf', 'w').write(
        'wireplumber.profiles = { main = { monitor.alsa = disabled monitor.alsa-midi = disabled monitor.bluez = disabled '
        'monitor.bluez-midi = disabled monitor.libcamera = disabled monitor.v4l2 = disabled } }\n')
    for cmd in (['pipewire'], ['wireplumber'], ['pipewire-pulse']):
        procs.append(subprocess.Popen(cmd, env=e, stdout=logf, stderr=logf))
        time.sleep(1.0)
    subprocess.run(['pactl', 'set-default-sink', 'charon'], env=e, stdin=subprocess.DEVNULL, capture_output=True, timeout=10)
    # measured 6s: Orca "AT-SPI: Unable to open bus connection ... /run/user/0/at-spi2-socket-2" - the accessibility
    # bus is started here, on this session's D-Bus, and its address is given to Orca and Firefox explicitly
    if os.path.exists('/usr/libexec/at-spi-bus-launcher'):
        procs.append(subprocess.Popen(['/usr/libexec/at-spi-bus-launcher', '--launch-immediately'], env=e,
                                      stdout=logf, stderr=logf))
        for _ in range(20):
            if os.path.exists(RUNDIR + '/at-spi/bus'):
                break
            time.sleep(0.25)
        e['AT_SPI_BUS_ADDRESS'] = 'unix:path=%s/at-spi/bus' % RUNDIR
    speech_config()
    prof = os.path.join(PROFILE, 'charon.default')     # preferences at every start (they were only written at install)
    os.makedirs(prof, exist_ok=True)
    open(os.path.join(prof, 'user.js'), 'w').write(PREFS)
    # measured 6s: the autospawned speech-dispatcher ran sd_espeak-ng, not "charon" - it is started here with the
    # mode's own configuration directory (Charon's voices) and stays for the session
    procs.append(subprocess.Popen(['speech-dispatcher', '-s', '-t', '0', '-C', PROFILE + '/.config/speech-dispatcher'],
                                  env=e, stdout=logf, stderr=logf))
    time.sleep(1.0)
    # wlroots finds keyboards and screens through libinput, which needs udev's device database; the hypervisor runs
    # mdev. Measured: "libinput initialization failed, no input devices". eudev's daemon is started for the mode
    # (input and drm devices only); the hypervisor's own keyboard grab is released before cage starts.
    # a seatd ended by the host key leaves its socket behind; seatd-launch then refuses ("Socket file found")
    # (measured twice: a seatd of the previous session still held it). Only this mode uses seatd in the hypervisor.
    subprocess.run(['pkill', '-x', 'seatd'], capture_output=True)
    time.sleep(0.3)
    try:
        os.remove('/run/seatd.sock')
    except OSError:
        pass
    if shutil.which('udevd') and subprocess.run(['pgrep', '-x', 'udevd'], capture_output=True).returncode != 0:
        subprocess.run(['udevd', '--daemon'], capture_output=True)
    if shutil.which('udevadm'):
        # measured 6s: triggering "sound" made eudev's alsa-restore rule load an old mixer state (Master -6 dB -> -20 dB,
        # the hypervisor's voice and every guest a third as loud); cage needs only input and drm. The mixer is kept
        # as the hypervisor set it, also against any other rule.
        state = RUNDIR + '/mixer.state'
        subprocess.run(['alsactl', '-f', state, 'store', '0'], capture_output=True)
        for sub in ('input', 'drm'):
            subprocess.run(['udevadm', 'trigger', '--subsystem-match=' + sub, '--action=add'], capture_output=True)
        subprocess.run(['udevadm', 'settle', '-t', '5'], capture_output=True)
        if os.path.exists(state):
            subprocess.run(['alsactl', '-f', state, 'restore', '0'], capture_output=True)
    # Orca's and Firefox's own words in the language Charon speaks (measured 6s: an English Charon heard Polish
    # role words because the mode was fixed to pl_PL)
    try:
        import hvvoice
        polish = hvvoice.Settings().polish
    except Exception:
        polish = False
    e['LANG'] = e['LC_ALL'] = 'pl_PL.UTF-8' if polish else 'en_US.UTF-8'
    e.update({'GTK_USE_PORTAL': '0', 'MOZ_ENABLE_WAYLAND': '1', 'GTK_MODULES': 'gail:atk-bridge', 'NO_AT_BRIDGE': '0'})
    if session_reader() == 'linda':
        return _start_x11(url, e, procs, logf, log, polish)
    script = ('GDK_BACKEND=wayland,x11 orca --replace >/run/charonhv/orca.log 2>&1 & exec flatpak --user run --talk-name=org.a11y.Bus --filesystem=%s %s '
              '--no-remote --profile %s/charon.default --kiosk "$0"' % (PROFILE, APP, PROFILE))
    soft = PROFILE + '/software-renderer'
    if os.path.exists(soft):
        e['WLR_RENDERER'] = 'pixman'                     # this computer needed it before (no second wait)
    cage = subprocess.Popen(['seatd-launch', '--', 'cage', '-s', '--', 'sh', '-c', script, url], env=e, stdout=logf, stderr=logf)
    time.sleep(3)
    if cage.poll() is not None and 'WLR_RENDERER' not in e:
        open(soft, 'w').write('measured: the GPU renderer did not start\n')
        # measured on VMware's SVGA: "Could not initialize EGL ... Unable to create the wlroots renderer"; the
        # software renderer (pixman) works on every screen, so the mode starts again with it
        log('modern: cage ended at once (%s), software renderer' % cage.returncode)
        e['WLR_RENDERER'] = 'pixman'
        cage = subprocess.Popen(['seatd-launch', '--', 'cage', '-s', '--', 'sh', '-c', script, url], env=e, stdout=logf, stderr=logf)
    cage.helpers = procs
    log('modern browser started pid %d' % cage.pid)
    return cage


READER_FILE = PROFILE + '/screen-reader'
READERS = ('orca', 'linda')


def reader_choice():
    """The screen reader the user chose for the hypervisor's graphical sessions ('orca' | 'linda'), or None (not asked
    yet). Kept on the data partition."""
    try:
        v = open(READER_FILE).read().strip()
        return v if v in READERS else None
    except OSError:
        return None


def set_reader_choice(v):
    os.makedirs(PROFILE, exist_ok=True)
    open(READER_FILE, 'w').write(v + '\n')


def session_reader():
    """The reader this session really uses: Linda when chosen AND built; otherwise Orca (also while Linda is built)."""
    if reader_choice() == 'linda':
        import hvlinda
        if hvlinda.installed() and shutil.which('Xorg') and shutil.which('openbox'):
            return 'linda'
    return 'orca'


def _start_x11(url, e, procs, logf, log, polish):
    """Linda's session: Xorg on its own virtual terminal (vt7), Openbox (so the browser window is the active one),
    Linda, and Firefox as an X11 client. Linda reads the keyboard from the X display itself (XInput2), no service.
    Measured session 8 (hvxp1, VMware): Linda read Firefox aloud with Charon's voice - page, Tab focus, the search
    form - in THIS session. OPEN: the VMware picture stayed black (also afterwards under cage, so possibly the SVGA
    left in a bad state by the first Xorg attempt with the vmware driver - recheck after a reboot). Tried and not
    working yet: cage's Xwayland for Linda (Firefox "no DISPLAY" in Flatpak; Linda needs XDG_SESSION_TYPE=x11)."""
    import hvlinda
    hvlinda.mount()
    for f in ('/tmp/.X11-unix/X0', '/tmp/.X0-lock'):
        try:
            os.remove(f)
        except OSError:
            pass
    # measured: the vmware driver segfaulted ("Unable to map mmio BAR": the kernel's vmwgfx owns the card); the
    # generic modesetting driver of the X server runs on every KMS screen, here without acceleration
    conf = RUNDIR + '/xorg.conf'
    open(conf, 'w').write('Section "Device"\n Identifier "kms"\n Driver "modesetting"\n Option "AccelMethod" "none"\n'
                          ' Option "ShadowFB" "on"\nEndSection\nSection "ServerFlags"\n Option "AutoAddGPU" "off"\nEndSection\n')
    x = subprocess.Popen(['Xorg', ':0', 'vt7', '-config', conf, '-nolisten', 'tcp', '-noreset', '-logfile', '/run/charonhv/xorg.log'],
                         env=e, stdout=logf, stderr=logf)
    procs.append(x)
    for _ in range(60):
        if os.path.exists('/tmp/.X11-unix/X0') or x.poll() is not None:
            break
        time.sleep(0.25)
    if x.poll() is not None:
        raise RuntimeError('Xorg ended at once (exit %s, /run/charonhv/xorg.log)' % x.returncode)
    e['DISPLAY'] = ':0'
    e.pop('WAYLAND_DISPLAY', None)
    # measured: LINDA_AUDIO_SINK=pulse made Linda (glibc, from Nix) load the hypervisor's musl libpulse and stop at
    # once; its own PipeWire sink (Nix's libpipewire, the session's pipewire-0 socket) is used
    e.update({'MOZ_ENABLE_WAYLAND': '0', 'GDK_BACKEND': 'x11', 'LINDA_AUDIO_SINK': 'pipewire', 'XDG_SESSION_TYPE': 'x11'})
    subprocess.run(['setxkbmap', 'us'], env=e, capture_output=True)
    # Openbox without any key or mouse binding of its own (its defaults take Control+Alt+arrows, which Linda's table
    # commands need), windows undecorated and maximised
    rc = RUNDIR + '/openbox-rc.xml'
    open(rc, 'w').write('<?xml version="1.0"?>\n<openbox_config xmlns="http://openbox.org/3.4/rc"><keyboard></keyboard>'
                        '<mouse></mouse><desktops><number>1</number></desktops><applications><application class="*">'
                        '<decor>no</decor><maximized>yes</maximized><focus>yes</focus></application></applications>'
                        '</openbox_config>\n')
    procs.append(subprocess.Popen(['openbox', '--config-file', rc], env=e, stdout=logf, stderr=logf))
    time.sleep(0.5)
    hvlinda.config(PROFILE, polish)
    lf = open('/run/charonhv/linda.log', 'ab')
    procs.append(subprocess.Popen([hvlinda.LINDA], env=e, stdout=lf, stderr=lf))
    ff = subprocess.Popen(['flatpak', '--user', 'run', '--talk-name=org.a11y.Bus', '--filesystem=' + PROFILE, APP,
                           '--no-remote', '--profile', PROFILE + '/charon.default', '--kiosk', url], env=e, stdout=logf, stderr=logf)
    ff.helpers = procs
    log('modern browser started under X11 with Linda, pid %d' % ff.pid)
    return ff

def stop(cage):
    for p in [cage] + getattr(cage, 'helpers', []):
        try:
            p.terminate()
            p.wait(5)
        except Exception:
            try:
                p.kill()
            except Exception:
                pass
    cleanup()


def cleanup():
    """Everything a modern session starts: Firefox inside Flatpak, cage, the seat daemon, Orca, speech-dispatcher,
    PipeWire, the accessibility bus and the session's D-Bus. Measured 6s: a PipeWire of an earlier session (whose
    stop never ran) kept pipewire-0 and pulse/native, every later pipewire-pulse failed "Address in use" and the old
    one no longer answered. Called before every start as well."""
    subprocess.run(['pkill', '-f', 'orca'], capture_output=True)
    subprocess.run(['pkill', '-f', 'org.mozilla.firefox'], capture_output=True)
    subprocess.run(['pkill', '-f', '/nix/store/.*linda'], capture_output=True)
    x11 = subprocess.run(['pgrep', '-x', 'Xorg'], capture_output=True).returncode == 0
    for name in ('cage', 'seatd', 'Xwayland', 'openbox', 'Xorg', 'pipewire-pulse', 'wireplumber', 'pipewire'):
        subprocess.run(['pkill', '-x', name], capture_output=True)
    # long names: the kernel's process name is cut at 15 characters, so these are matched on the command line
    for pat in ('speech-dispatcher', '/usr/libexec/at-spi', 'at-spi2/accessibility.conf'):
        subprocess.run(['pkill', '-f', pat], capture_output=True)
    try:
        os.kill(int(open(RUNDIR + '/dbus.pid').read()), 15)
    except (OSError, ValueError):
        pass
    time.sleep(0.5)
    for n in ('pipewire-0', 'pipewire-0.lock', 'pipewire-0-manager', 'pipewire-0-manager.lock', 'dbus.pid'):
        try:
            os.remove(os.path.join(RUNDIR, n))
        except OSError:
            pass
    shutil.rmtree(RUNDIR + '/pulse', ignore_errors=True)
    shutil.rmtree(RUNDIR + '/at-spi', ignore_errors=True)
    if x11:
        # the VT is switched back only after Xorg has ENDED (it restores the console's mode itself on exit); a switch
        # while it still holds the DRM master left the screen black (session 9)
        for _ in range(40):
            if subprocess.run(['pgrep', '-x', 'Xorg'], capture_output=True).returncode != 0:
                break
            time.sleep(0.25)
        else:
            subprocess.run(['pkill', '-9', '-x', 'Xorg'], capture_output=True)
            time.sleep(0.5)
        subprocess.run(['chvt', '1'], capture_output=True)      # Xorg was on vt7: the hypervisor's own screen again
