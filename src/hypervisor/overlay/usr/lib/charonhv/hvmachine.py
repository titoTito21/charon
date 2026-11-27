# SPDX-License-Identifier: GPL-2.0-or-later
# Charon Hypervisor: machines (QEMU processes with QMP), templates, snapshots, clones, import of foreign machines.
import json, os, queue, re, shutil, socket, subprocess, tarfile, threading, time, xml.etree.ElementTree as ET

HV = '/hv'
MACHINES = HV + '/machines'
RUN = '/run/charonhv'


def log(msg):
    try:
        with open(RUN + '/charonhv.log', 'a', encoding='utf-8') as f:
            f.write('%s %s\n' % (time.strftime('%Y-%m-%d %H:%M:%S'), msg))
    except OSError:
        pass

# Per-system templates (design part 7). sound/nic are QEMU device names; reader: who reads the guest's screen.
TEMPLATES = {
    'winxp': dict(en='Windows XP', pl='Windows XP', machine='pc', cpus=1, memory=512, disk=10, bus='ide', sound='AC97',
                  nic='rtl8139', firmware='bios', reader='nvda2017', readers=['nvda2017'],
                  unattended='winnt', media='own'),
    'winme': dict(en='Windows Me', pl='Windows Me', machine='pc', cpus=1, memory=256, disk=4, bus='ide', sound='sb16',
                  nic='ne2k_pci', firmware='bios', reader='hypervisor', readers=['hypervisor'], unattended='', media='own'),
    'win98': dict(en='Windows 98', pl='Windows 98', machine='pc', cpus=1, memory=128, disk=4, bus='ide', sound='sb16',
                  nic='ne2k_pci', firmware='bios', reader='hypervisor', readers=['hypervisor'], unattended='', media='own'),
    'win98se': dict(en='Windows 98 Second Edition', pl='Windows 98 Second Edition', machine='pc', cpus=1, memory=128, disk=4, bus='ide',
                    sound='sb16', nic='ne2k_pci', firmware='bios', reader='hypervisor', readers=['hypervisor'], unattended='', media='own'),
    'win95': dict(en='Windows 95', pl='Windows 95', machine='pc', cpus=1, memory=64, disk=2, bus='ide', sound='sb16',
                  nic='ne2k_pci', firmware='bios', reader='hypervisor', readers=['hypervisor'], unattended='', media='own'),
    # Vista: Intel HD Audio has Vista's own class driver (AC'97 has none in the box)
    'winvista': dict(en='Windows Vista', pl='Windows Vista', machine='pc', cpus=2, memory=1536, disk=30, bus='ahci', sound='hda',
                     nic='e1000', firmware='bios', reader='nvda', readers=['nvda', 'narrator', 'hypervisor'], unattended='autounattend', media='own'),
    'win8': dict(en='Windows 8', pl='Windows 8', machine='pc', cpus=2, memory=2048, disk=40, bus='ahci', sound='hda',
                 nic='e1000', firmware='bios', reader='nvda', readers=['nvda', 'narrator', 'hypervisor'], unattended='autounattend', media='own'),
    'win81': dict(en='Windows 8.1', pl='Windows 8.1', machine='pc', cpus=2, memory=2048, disk=40, bus='ahci', sound='hda',
                  nic='e1000', firmware='bios', reader='nvda', readers=['nvda', 'narrator', 'hypervisor'], unattended='autounattend', media='own'),
    'win7': dict(en='Windows 7', pl='Windows 7', machine='pc', cpus=2, memory=2048, disk=40, bus='ahci', sound='hda',
                 nic='e1000', firmware='bios', reader='nvda', readers=['nvda', 'narrator', 'hypervisor'], unattended='autounattend', media='own'),
    'win11': dict(en='Windows 11', pl='Windows 11', machine='q35', cpus=4, memory=4096, disk=64, bus='nvme', sound='hda',
                  nic='e1000e', firmware='uefi', reader='nvda', readers=['nvda', 'narrator', 'hypervisor'], unattended='autounattend', media='network'),
    'debian6': dict(en='Debian 6 (squeeze)', pl='Debian 6 (squeeze)', machine='pc', cpus=1, memory=512, disk=8, bus='ide',
                    sound='ES1370', nic='e1000', firmware='bios', reader='speakup', readers=['speakup', 'hypervisor'], unattended='', media='network'),
    'debian13': dict(en='Debian 13', pl='Debian 13', machine='q35', cpus=2, memory=2048, disk=20, bus='virtio', sound='hda',
                     nic='virtio-net-pci', firmware='bios', reader='speakup', readers=['speakup', 'orca', 'hypervisor'], unattended='', media='network'),
    'ubuntu': dict(en='Ubuntu', pl='Ubuntu', machine='q35', cpus=2, memory=4096, disk=25, bus='virtio', sound='hda',
                   nic='virtio-net-pci', firmware='uefi', reader='orca', readers=['orca', 'hypervisor'], unattended='', media='network'),
    'freedos': dict(en='FreeDOS', pl='FreeDOS', machine='pc', cpus=1, memory=64, disk=1, bus='ide', sound='sb16',
                    nic='ne2k_pci', firmware='bios', reader='hypervisor', readers=['hypervisor'], unattended='', media='network'),
    'reactos': dict(en='ReactOS', pl='ReactOS', machine='pc', cpus=1, memory=1024, disk=8, bus='ide', sound='AC97',
                    nic='pcnet', firmware='bios', reader='hypervisor', readers=['hypervisor'], unattended='', media='network'),
    'haiku': dict(en='Haiku', pl='Haiku', machine='q35', cpus=2, memory=2048, disk=16, bus='ahci', sound='hda',
                  nic='e1000', firmware='bios', reader='hypervisor', readers=['hypervisor'], unattended='', media='network'),
    'freebsd': dict(en='FreeBSD', pl='FreeBSD', machine='q35', cpus=2, memory=2048, disk=16, bus='virtio', sound='hda',
                    nic='virtio-net-pci', firmware='bios', reader='hypervisor', readers=['hypervisor'], unattended='', media='network'),
    'other': dict(en='Another system', pl='Inny system', machine='q35', cpus=2, memory=2048, disk=20, bus='ahci', sound='hda',
                  nic='e1000', firmware='bios', reader='hypervisor', readers=['hypervisor'], unattended='', media='own'),
}
SOUNDS = ['AC97', 'hda', 'sb16', 'ES1370', 'none']
NICS = ['rtl8139', 'e1000', 'e1000e', 'pcnet', 'ne2k_pci', 'virtio-net-pci', 'none']


# ---- 6zc: the performance profile of each template, hypervisor-side only (nothing changed in the guest) -------------
# Sources and the evidence grade of each choice: work/r410-hv-research-6zc.txt (QEMU's hyperv.html and invocation.html,
# Proxmox qemu-server CPUConfig.pm, qemu-3dfx wiki, patcher9x). Real numbers need a computer with VT-x/AMD-V; under
# emulation (TCG) only the TCG, timer, disk and device parts apply.
HV_VISTA = 'hv-spinlocks=0x1fff,hv-vapic,hv-time,hv-vpindex,hv-runtime'          # Proxmox's set for Windows 6.0
HV_7 = HV_VISTA + ',hv-relaxed,hv-synic,hv-stimer,hv-ipi,hv-frequencies'          # 7 and later
HV_8 = HV_7 + ',hv-tlbflush'
HV_FLAGS = {'winvista': HV_VISTA, 'win7': HV_7, 'win8': HV_8, 'win81': HV_8, 'win11': HV_8}
OLD_9X = ('win95', 'win98', 'win98se', 'winme', 'freedos')


# 6zc: the machine's hardware profile. A snapshot holds the state of every device, and QEMU refuses one saved with
# other hardware (measured session 8 on a copy of XP's disk: a daily snapshot from before 6zc, loaded with hpet=off ->
# "Unknown section or instance 'hpet' 0"). Each snapshot keeps the profile it was saved with (machine.json
# "snapshot_profiles"; a snapshot without an entry is from before 6zc = profile 0); going back to one with another
# profile restarts the machine on that snapshot's profile, and the next cold start uses the current one.
PROFILE_VERSION = 1


def performance(template, kvm, profile=PROFILE_VERSION):
    if profile < 1:
        # profile 0: the machine as the hypervisor started it before 6zc (no HPET change, no CPU or timer settings)
        return dict(machine='', accel='kvm' if kvm else 'tcg', cpu='', disk=',cache=writeback,aio=threads', rtc='', extra=[])
    return _performance(template, kvm)


def _performance(template, kvm):
    """dict(machine, accel, cpu, disk, rtc, extra) for one template.
    - KVM: -cpu host with the Hyper-V enlightenments Windows of that version uses (none for XP and 9x: their kernels do
      not know Hyper-V); 9x keeps the host CPU too (no KVM setting lowers the speed the guest sees - the fast-CPU fault
      of 95/98 needs a guest patch, which is NOT applied: the hypervisor says so);
    - TCG: one host thread per virtual CPU and a large translation cache;
    - Windows: no HPET (Windows then uses the ACPI PM timer/RTC - smoother sound, no idle load), the RTC re-injects lost
      ticks (driftfix=slew, for Windows' ACPI HAL), the PIT delays lost ticks;
    - disks: writeback cache with a thread pool (IDE/AHCI/NVMe), virtio gets its own I/O thread (above)."""
    windows = template.startswith('win')
    out = dict(machine='', accel='kvm' if kvm else 'tcg,thread=multi,tb-size=512', cpu='', disk=',cache=writeback,aio=threads',
               rtc='', extra=[])
    if windows or template in OLD_9X:
        out['machine'] = ',hpet=off'
    if windows and template not in ('win95', 'win98', 'win98se', 'winme'):
        out['rtc'] = ',driftfix=slew'
    if kvm:
        out['cpu'] = 'host' + (',' + HV_FLAGS[template] if template in HV_FLAGS else '')
        out['extra'] += ['-global', 'kvm-pit.lost_tick_policy=delay']
    return out


# Session 10 (user: "simulate a normal PC with normal sound, not 500 ms"): guest sound latency. Measured before on the
# VMware test host (key -> NVDA in XP -> host speakers 515 ms median): ~165 ms guest + emulation, ~350 ms from QEMU's
# mixer to the speakers = QEMU's own ALSA defaults (92.9 ms buffer, mixing buffer of the same size, resampling
# 44.1 -> 48 kHz) + the 170 ms dmix ring + the test host's VMware sound. QEMU 11 options (docs + audio/alsaaudio.c):
# timer-period (us, default 10000), out.buffer-length / out.period-length (us; ALSA defaults 92880 / 5805),
# out.frequency (default 44100; 48000 = the dmix rate, no conversion). dmix forces its own period (512 frames).
SOUND_LOW, SOUND_SAFE, SOUND_LARGE = 0, 1, 2
SOUND_OPTIONS = {
    SOUND_LOW: ',timer-period=5000,out.frequency=48000,out.buffer-length=21333,out.period-length=10667',
    SOUND_SAFE: ',timer-period=10000,out.frequency=48000,out.buffer-length=42667,out.period-length=10667',
    SOUND_LARGE: '',            # QEMU's own defaults (as before session 10)
}


def audiodev(slot, latency=SOUND_LOW):
    return 'alsa,id=snd0,out.dev=charonguest%s,in.dev=null%s' % (slot or '', SOUND_OPTIONS.get(latency, SOUND_OPTIONS[SOUND_LOW]))


def host_tuning():
    """6zc host side while machines run: the CPU governor "performance" (steady speed - no stutter in guest sound
    and speech) and KSM off (with one or two guests it saves almost nothing and costs CPU). Files that do not exist
    (no cpufreq in a VM, no KSM) are skipped; nothing is kept after a restart."""
    import glob
    for f in glob.glob('/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor'):
        try:
            with open(f, 'w') as h:
                h.write('performance')
        except OSError:
            pass
    try:
        with open('/sys/kernel/mm/ksm/run', 'w') as h:
            h.write('0')
    except OSError:
        pass


def ids():
    try:
        return sorted(d for d in os.listdir(MACHINES) if os.path.isfile(os.path.join(MACHINES, d, 'machine.json')))
    except OSError:
        return []


def new_id(name):
    base = re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-')[:24] or 'machine'
    i, cand = 1, base
    while os.path.exists(os.path.join(MACHINES, cand)):
        i += 1
        cand = '%s-%d' % (base, i)
    return cand


class Adopted:
    """A process the menu did not start itself (a machine's QEMU, proxy or share server from before the menu
    restarted): the parts of subprocess.Popen the menu uses, through /proc and signals."""

    def __init__(self, pid, proc='/proc'):
        self.pid, self.returncode, self._proc = pid, None, proc
        self._start = Adopted.start_time(pid, proc)

    @staticmethod
    def start_time(pid, proc='/proc'):
        """Field 22 of /proc/PID/stat (clock ticks since boot): with the pid it names one process for good."""
        try:
            stat = open('%s/%d/stat' % (proc, int(pid))).read()
            return int(stat[stat.rindex(')') + 2:].split()[19])
        except (OSError, ValueError, IndexError):
            return None

    def poll(self):
        if self.returncode is None:
            st = Adopted.start_time(self.pid, self._proc)
            try:
                zombie = open('%s/%d/stat' % (self._proc, self.pid)).read().rsplit(')', 1)[1].split()[0] == 'Z'
            except (OSError, IndexError):
                zombie = True
            if st is None or st != self._start or zombie:
                self.returncode = 0
        return self.returncode

    def send_signal(self, sig):
        if self.poll() is None:
            try:
                os.kill(self.pid, sig)
            except OSError:
                pass

    def terminate(self):
        self.send_signal(15)

    def kill(self):
        self.send_signal(9)

    def wait(self, timeout=None):
        end = time.time() + (timeout if timeout is not None else 1e9)
        while self.poll() is None:
            if time.time() > end:
                raise subprocess.TimeoutExpired('pid %d' % self.pid, timeout)
            time.sleep(0.1)
        return self.returncode


def state_path(m):
    return os.path.join(RUN, '%s.state.json' % m.id)


def load_state(m):
    try:
        return json.load(open(state_path(m)))
    except (OSError, ValueError):
        return {}


def save_state(m, **kw):
    """What a restarted menu needs to take a running machine back (RAM only: after a reboot nothing runs)."""
    st = load_state(m)
    st.update(kw)
    os.makedirs(RUN, exist_ok=True)
    with open(state_path(m) + '.new', 'w') as f:
        json.dump(st, f)
    os.replace(state_path(m) + '.new', state_path(m))


class QMP:
    def __init__(self, path, on_event):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.file = self.sock.makefile('rwb', buffering=0)
        self.replies = queue.Queue()
        self.on_event = on_event
        self.lock = threading.Lock()
        json.loads(self.file.readline())
        threading.Thread(target=self._read, daemon=True).start()
        self.cmd('qmp_capabilities')

    def _read(self):
        while True:
            try:
                line = self.file.readline()
            except OSError:
                line = b''
            if not line:
                self.on_event({'event': 'QMP_CLOSED'})
                self.replies.put({'error': {'desc': 'closed'}})
                return
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            if 'event' in msg:
                self.on_event(msg)
            else:
                self.replies.put(msg)

    def cmd(self, name, timeout=10, **args):
        with self.lock:
            msg = {'execute': name}
            if args:
                msg['arguments'] = args
            self.file.write((json.dumps(msg) + '\n').encode())
            try:
                r = self.replies.get(timeout=timeout)
            except queue.Empty:
                raise TimeoutError(name)
        if 'error' in r:
            raise RuntimeError(r['error'].get('desc', 'error'))
        return r.get('return')

    def hmp(self, line, timeout=60):
        return self.cmd('human-monitor-command', timeout=timeout, **{'command-line': line})


class Machine:
    def __init__(self, mid):
        self.id = mid
        self.dir = os.path.join(MACHINES, mid)
        self.cfg = json.load(open(os.path.join(self.dir, 'machine.json'), encoding='utf-8'))
        self.proc = None
        self.qmp = None
        self.state = 'off'
        self.stderr = b''

    @property
    def name(self):
        return self.cfg.get('name', self.id)

    def save(self):
        tmp = os.path.join(self.dir, 'machine.json.new')
        json.dump(self.cfg, open(tmp, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
        os.replace(tmp, os.path.join(self.dir, 'machine.json'))

    def path(self, name):
        return name if name.startswith('/') else os.path.join(self.dir, name)

    def command(self, kvm, profile=PROFILE_VERSION):
        c = self.cfg
        perf = performance(c.get('template', ''), kvm, profile)
        q = ['qemu-system-x86_64', '-name', c['name'][:60] + ',debug-threads=on',
             '-machine', c.get('machine', 'pc') + perf['machine'], '-accel', perf['accel'],
             '-m', str(c.get('memory', 512)), '-smp', str(c.get('cpus', 1)), '-display', 'none',
             '-qmp', 'unix:%s/%s.qmp,server=on,wait=off' % (RUN, self.id),
             '-audiodev', audiodev(getattr(self, 'sound_slot', 0), getattr(self, 'sound_latency', SOUND_LOW)),
             '-device', 'pvpanic', '-k', c.get('keymap', 'en-us') if False else 'en-us']
        if c.get('cpu'):
            q += ['-cpu', c['cpu']]                     # a CPU model chosen for this machine wins
        elif perf['cpu']:
            q += ['-cpu', perf['cpu']]
        if c.get('firmware') == 'uefi':
            vars_ = os.path.join(self.dir, 'OVMF_VARS.fd')
            if not os.path.exists(vars_):
                for src in ('/usr/share/ovmf/x64/OVMF_VARS.fd', '/usr/share/OVMF/OVMF_VARS.fd'):
                    if os.path.exists(src):
                        shutil.copy(src, vars_)
                        break
            code = next((p for p in ('/usr/share/ovmf/x64/OVMF_CODE.fd', '/usr/share/OVMF/OVMF_CODE.fd') if os.path.exists(p)), None)
            if code:
                q += ['-drive', 'if=pflash,format=raw,readonly=on,file=' + code, '-drive', 'if=pflash,format=raw,file=' + vars_]
        bus = c.get('bus', 'ide')
        if bus == 'ahci':
            q += ['-device', 'ahci,id=ahci0']
        for n, disk in enumerate(c.get('disks', [])):
            f = self.path(disk)
            io = perf['disk']
            if bus == 'ide':
                q += ['-drive', 'file=%s,if=ide,index=%d,media=disk,format=qcow2,werror=stop,rerror=stop%s' % (f, n, io)]
            elif bus == 'virtio':
                # 6zc: virtio disks get their own I/O thread (iothreads exist only for virtio-blk/scsi)
                if n == 0:
                    q += ['-object', 'iothread,id=io0']
                q += ['-drive', 'file=%s,if=none,id=d%d,format=qcow2,werror=stop,rerror=stop%s,discard=unmap' % (f, n, io),
                      '-device', 'virtio-blk-pci,drive=d%d,iothread=io0' % n]
            elif bus == 'nvme':
                q += ['-drive', 'file=%s,if=none,id=d%d,format=qcow2,werror=stop,rerror=stop%s' % (f, n, io), '-device', 'nvme,drive=d%d,serial=CHV%04d' % (n, n)]
            else:
                q += ['-drive', 'file=%s,if=none,id=d%d,format=qcow2,werror=stop,rerror=stop%s' % (f, n, io), '-device', 'ide-hd,drive=d%d,bus=ahci0.%d' % (n, n)]
        if c.get('cdrom'):
            if bus == 'ahci':
                q += ['-drive', 'file=%s,if=none,id=cd0,media=cdrom,readonly=on' % self.path(c['cdrom']), '-device', 'ide-cd,drive=cd0,bus=ahci0.5']
            else:
                q += ['-drive', 'file=%s,if=ide,index=2,media=cdrom,readonly=on' % self.path(c['cdrom'])]
        if c.get('floppy'):
            fl = self.path(c['floppy'])
            if os.path.isdir(fl):
                q += ['-drive', 'file=fat:floppy:%s,if=floppy,format=vvfat,readonly=on' % fl]
            else:
                q += ['-drive', 'file=%s,if=floppy,format=raw' % fl]
        snd = c.get('sound', 'none')
        if snd == 'hda':
            q += ['-device', 'intel-hda', '-device', 'hda-duplex,audiodev=snd0']
        elif snd in ('AC97', 'ES1370'):
            q += ['-device', '%s,audiodev=snd0' % snd]
        elif snd == 'sb16':
            q += ['-device', 'sb16,audiodev=snd0', '-machine', 'pcspk-audiodev=snd0']
        nic = c.get('nic', 'none')
        if nic != 'none' and c.get('secure'):
            # secure-use profile (6j): NAT with restrict=on - nothing reaches the guest from outside and nothing leaves
            # it except 10.0.2.100:3128, which leads to this machine's filtering proxy (hvproxy.py) on the hypervisor;
            # "internet off" leaves out even that
            net = 'user,id=n0,restrict=on'
            if c.get('internet', True) and getattr(self, 'proxy_port', 0):
                nc = shutil.which('nc') or '/usr/bin/nc'
                net += ',guestfwd=tcp:10.0.2.100:3128-cmd:%s 127.0.0.1 %d' % (nc, self.proxy_port)
                # 6q mail: plain POP3/IMAP/SMTP to 10.0.2.100 inside the guest, TLS to the real server from the hypervisor
                for gport, kind in ((110, 'pop'), (143, 'imap'), (25, 'smtp'), (587, 'smtp')):
                    lp = getattr(self, 'mail_ports', {}).get(kind)
                    if lp:
                        net += ',guestfwd=tcp:10.0.2.100:%d-cmd:%s 127.0.0.1 %d' % (gport, nc, lp)
            # 6v: \\10.0.2.100\Downloads, the read-only share of files downloaded for this machine (hvshare); it is
            # reachable even with "internet off" (it is the hypervisor itself)
            if getattr(self, 'share_port', 0):
                nc = shutil.which('nc') or '/usr/bin/nc'
                for gport in (445, 139):
                    net += ',guestfwd=tcp:10.0.2.100:%d-cmd:%s 127.0.0.1 %d' % (gport, nc, self.share_port)
            q += ['-netdev', net, '-device', '%s,netdev=n0' % nic]
        elif nic != 'none':
            q += ['-netdev', 'user,id=n0', '-device', '%s,netdev=n0' % nic]
        else:
            q += ['-nic', 'none']
        # USB controller the guest has a driver for: EHCI for XP (SP3), UHCI for 98/Me, xHCI for the others
        old_windows = c.get('template') in ('win95', 'win98', 'win98se', 'winme')
        usbbus = 'uhci0.0' if old_windows else 'ehci0.0' if c.get('template') == 'winxp' else 'xhci0.0'
        if old_windows:
            q += ['-device', 'piix3-usb-uhci,id=uhci0']
        elif c.get('template') == 'winxp':
            q += ['-device', 'usb-ehci,id=ehci0']
        else:
            q += ['-device', 'qemu-xhci,id=xhci0']
        self.usbbus = usbbus
        if c.get('tablet'):
            q += ['-device', 'usb-tablet,bus=' + usbbus]
        # USB drives (the shared folder, the setup files of a guest screen reader): while Windows Setup runs from the
        # disc they are plugged in only after its first restart (a drive seen by text-mode Setup takes the letter C:)
        if not self.installing():
            for i, (path, rw) in enumerate(self.usb_drives()):
                q += ['-drive', 'file=fat:%s%s,if=none,id=ud%d,format=vvfat%s' % ('rw:' if rw else '', path, i, ',cache=unsafe' if rw else ',readonly=on'),
                      '-device', 'usb-storage,drive=ud%d,bus=%s,removable=on' % (i, usbbus)]
            import hvexchange
            if self.has_exchange() and os.path.exists(hvexchange.image(self.dir)):
                q += ['-drive', 'file=%s,if=none,id=uex,format=qcow2' % hvexchange.image(self.dir),
                      '-device', 'usb-storage,drive=uex,bus=%s,removable=on' % usbbus]
            img = self.secure_image()
            self.secure_plugged = bool(img)     # attached from the start: never hot-plugged again (measured: node-name conflict)
            if img and old_windows:     # 9x has no USB storage driver of its own: a second IDE disk, writes discarded
                q += ['-drive', 'file=%s,if=ide,index=1,media=disk,format=raw,snapshot=on' % img]
            elif img:
                q += ['-drive', 'file=%s,if=none,id=usec,format=raw,readonly=on' % img,
                      '-device', 'usb-storage,drive=usec,bus=%s,removable=on' % usbbus]
        if c.get('family') == 'windows' and c.get('print', True) and c.get('template') not in ('win95',):
            # 6q printing: LPT1 output to a file the print spooler of the hypervisor turns into PDFs (hvprint)
            import hvprint
            q += ['-chardev', 'file,id=lpt0,path=%s,append=on' % hvprint.port_file(self.id), '-device', 'isa-parallel,chardev=lpt0']
        if c.get('secure') and not self.installing():
            q += ['-S']        # paused: the snapshot before going online is taken first, then it runs
        if c.get('shared') and c.get('family') == 'linux':
            q += ['-virtfs', 'local,path=%s/shared,mount_tag=charonshared,security_model=mapped-xattr,id=fs0' % HV]
        boot = c.get('boot', 'cd' if c.get('cdrom') else 'c')
        q += ['-boot', 'order=' + ('dc' if boot == 'cd' else 'cd'),
              '-rtc', 'base=' + ('localtime' if c.get('family') == 'windows' else 'utc') + perf['rtc']] + perf['extra']
        q += c.get('extra', [])
        return q

    def installing(self):
        return bool(self.cfg.get('installing')) and self.cfg.get('family') == 'windows'

    def secure_image(self):
        p = os.path.join(self.dir, 'secure.img')
        return p if self.cfg.get('secure') and os.path.exists(p) else ''

    def usb_drives(self):
        out = []
        if self.cfg.get('shared') and self.cfg.get('family') != 'linux':
            # read-only under the secure-use profile unless the user made it writable on purpose
            out.append((HV + '/shared', not self.cfg.get('secure') or bool(self.cfg.get('shared_rw'))))
        setup = os.path.join(self.dir, 'setup')
        if os.path.isdir(setup):
            out.append((setup, False))
        return out

    def exchange_dir(self):
        ex = os.path.join(HV, 'exchange', self.id)
        os.makedirs(ex, exist_ok=True)
        return ex

    def has_exchange(self):
        """6q file exchange: the shared folder stays read-only, this machine's own exchange drive is writable (a FAT32
        image, hvexchange: a writable vvfat drive blocks savevm); scanned by ClamAV when the machine stops."""
        return bool(self.cfg.get('secure')) and self.cfg.get('family') != 'linux' and self.cfg.get('exchange', True)

    def plug_usb_drives(self):
        """After Windows Setup's first restart: the same drives, hot-plugged through QMP."""
        for i, (path, rw) in enumerate(self.usb_drives()):
            node = 'ud%d' % i
            self.qmp.cmd('blockdev-add', driver='vvfat', **{'node-name': node, 'dir': path, 'fat-type': 16, 'rw': rw, 'read-only': not rw})
            self.qmp.cmd('device_add', driver='usb-storage', drive=node, bus=self.usbbus, id='usb' + node, removable=True)
        self.plug_secure_image()

    def plug_secure_image(self):
        img = self.secure_image()
        if img and not getattr(self, 'secure_plugged', False):
            self.qmp.cmd('blockdev-add', driver='raw', **{'node-name': 'usechot', 'read-only': True,
                                                          'file': {'driver': 'file', 'filename': img, 'read-only': True}})
            self.qmp.cmd('device_add', driver='usb-storage', drive='usechot', bus=self.usbbus, id='usbsec', removable=True)
            self.secure_plugged = True

    SLOTS = {}      # sound slot 1..8 (asound.conf charonguestN, switch "Charon Machine N") -> the machine using it

    def take_sound_slot(self):
        for n in range(1, 9):
            m = Machine.SLOTS.get(n)
            if m is None or m is self or not m.running():
                Machine.SLOTS[n] = self
                return n
        return 0        # more than 8 machines at once: the shared guest volume only (no own mute)

    def set_muted(self, muted):
        """6q/(a): a real per-machine mute (silence, not -40 dB), kept in machine.json; the ducking never undoes it.
        Returns False when the switch could not be set (no own sound slot, or ALSA refused)."""
        self.cfg['muted'] = bool(muted)
        self.save()
        return self.apply_mute()

    def apply_mute(self):
        slot = getattr(self, 'sound_slot', 0)
        if not slot:
            return False
        r = subprocess.run(['amixer', '-q', '-c', '0', 'cset', 'name=Charon Machine %d' % slot, '0' if self.cfg.get('muted') else '1'],
                           capture_output=True)
        return r.returncode == 0

    def start(self, kvm, on_event, profile=PROFILE_VERSION, loadvm=None):
        os.makedirs(RUN, exist_ok=True)
        self.kvm, self.on_event, self.profile = kvm, on_event, profile
        host_tuning()
        self.sound_slot = self.take_sound_slot()
        self.apply_mute()       # before QEMU opens the slot: a muted machine never makes a sound
        self.exchange_note, self.exchange_held = '', []
        if self.has_exchange() and not self.installing():
            import hvexchange, hvscan
            try:
                n, self.exchange_held = hvexchange.sync_in(self.dir, self.exchange_dir(),
                                                           hvscan.scan_bytes_file if hvscan.clamd_running() else None)
                self.exchange_note = '%d' % n if n else ''
            except Exception as e:
                self.exchange_note = 'error: %s' % e
        sock = '%s/%s.qmp' % (RUN, self.id)
        if os.path.exists(sock):
            os.remove(sock)
        try:
            os.remove(state_path(self))  # a new QEMU, a new state (the menu adds its proxy and share after the start)
        except OSError:
            pass
        cmd = self.command(kvm, profile)
        if loadvm:
            # straight into the snapshot, running (the secure profile's pause is for a fresh start only)
            cmd = [a for a in cmd if a != '-S'] + ['-loadvm', loadvm]
        with open(os.path.join(self.dir, 'qemu-command.txt'), 'w') as f:
            f.write(' '.join(cmd) + '\n')
        self.errfile = open(os.path.join(self.dir, 'qemu-stderr.txt'), 'wb')
        # its own session: a crash or restart of the menu never takes the machine down (session 9); a restarted menu
        # attaches to it again through its QMP socket (attach)
        self.proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=self.errfile, stderr=self.errfile, cwd=self.dir,
                                     start_new_session=True)
        for _ in range(100):
            if self.proc.poll() is not None:
                break
            if os.path.exists(sock):
                try:
                    self.qmp = QMP(sock, lambda ev: on_event(self, ev))
                    self.state = 'running'
                    save_state(self, qemu_pid=self.proc.pid, qemu_start=Adopted.start_time(self.proc.pid),
                               sound_slot=self.sound_slot, profile=profile, kvm=bool(kvm))
                    return True
                except OSError:
                    pass
            time.sleep(0.1)
        self.state = 'off'
        return False

    def attach(self, on_event):
        """A restarted menu takes this machine back: its QEMU (still running in its own session) is checked against the
        state file (pid AND the process's start time, so a reused pid is never taken), its QMP socket connected again;
        the sound slot and hardware profile come back from the state file. True when attached."""
        st = load_state(self)
        pid = st.get('qemu_pid')
        if not pid or Adopted.start_time(pid) != st.get('qemu_start') or Adopted.start_time(pid) is None:
            return False
        sock = '%s/%s.qmp' % (RUN, self.id)
        try:
            self.qmp = QMP(sock, lambda ev: on_event(self, ev))
        except OSError:
            return False
        self.proc = Adopted(pid)
        self.kvm, self.on_event, self.profile = st.get('kvm', False), on_event, st.get('profile', PROFILE_VERSION)
        self.sound_slot = st.get('sound_slot', 0)
        if self.sound_slot:
            Machine.SLOTS[self.sound_slot] = self
        self.exchange_note, self.exchange_held = '', []
        self.state = 'running'
        return True

    def error_text(self):
        try:
            text = open(os.path.join(self.dir, 'qemu-stderr.txt'), encoding='utf-8', errors='replace').read()
        except OSError:
            return ''
        lines = [l for l in text.splitlines() if l.strip() and 'warning' not in l.lower()]
        return lines[-1][:200] if lines else ''

    def running(self):
        return self.proc is not None and self.proc.poll() is None

    def powerdown(self):
        self.qmp.cmd('system_powerdown')

    def kill(self):
        if self.running():
            try:
                self.qmp.cmd('quit', timeout=3)
            except Exception:
                pass
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.state = 'off'

    def snapshots(self):
        if self.running():
            text = self.qmp.hmp('info snapshots')
        else:
            text = subprocess.run(['qemu-img', 'snapshot', '-l', self.path(self.cfg['disks'][0])], capture_output=True, text=True).stdout
        out = []
        for line in text.splitlines():
            m = re.match(r'\s*(\d+|--)\s+(\S+)\s+', line)
            if m and m.group(2) not in ('TAG', 'Tag'):
                out.append(m.group(2))
        return out

    def snapshot_profile(self, tag):
        """The hardware profile snapshot tag was saved with (0: before 6zc; None: a disk-only snapshot)."""
        p = self.cfg.get('snapshot_profiles', {})
        return p[tag] if tag in p else 0

    def note_snapshot(self, tag, profile):
        self.cfg.setdefault('snapshot_profiles', {})[tag] = profile
        self.save()

    def snapshot_save(self, tag):
        if self.running():
            out = self.qmp.hmp('savevm ' + tag, timeout=600)
            if not (out or '').strip():
                self.note_snapshot(tag, getattr(self, 'profile', PROFILE_VERSION))
            return out
        r = subprocess.run(['qemu-img', 'snapshot', '-c', tag, self.path(self.cfg['disks'][0])], capture_output=True, text=True)
        if not r.stderr.strip():
            self.note_snapshot(tag, None)
        return r.stderr

    def snapshot_revert(self, tag):
        if self.running():
            need = self.snapshot_profile(tag)
            if need is not None and need != getattr(self, 'profile', PROFILE_VERSION):
                # saved with other hardware: loadvm would refuse it (and leave the disk switched under a running
                # guest) - the machine is restarted on that snapshot's profile instead
                log('snapshot %s of %s has profile %s, running %s: restart on it' % (tag, self.id, need, self.profile))
                self.restarting_until = time.time() + 30          # its SHUTDOWN is not announced as a shutdown
                self.kill()
                if not self.start(self.kvm, self.on_event, profile=need, loadvm=tag):
                    return self.error_text() or 'QEMU did not start'
                return ''
            return self.qmp.hmp('loadvm ' + tag, timeout=600)
        r = subprocess.run(['qemu-img', 'snapshot', '-a', tag, self.path(self.cfg['disks'][0])], capture_output=True, text=True)
        return r.stderr

    def snapshot_delete(self, tag):
        if self.running():
            out = self.qmp.hmp('delvm ' + tag, timeout=600)
        else:
            out = subprocess.run(['qemu-img', 'snapshot', '-d', tag, self.path(self.cfg['disks'][0])], capture_output=True, text=True).stderr
        if tag in self.cfg.get('snapshot_profiles', {}):
            del self.cfg['snapshot_profiles'][tag]
            self.save()
        return out

    def send_key(self, qcode, down):
        self.qmp.cmd('input-send-event', events=[{'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': qcode}}}])

    def send_combo(self, qcodes):
        self.qmp.cmd('send-key', keys=[{'type': 'qcode', 'data': k} for k in qcodes])

    def screendump(self, path):
        self.qmp.cmd('screendump', filename=path, timeout=20)


def unique_name(name):
    """Two machines never share a spoken name (measured: two "Windows XP" in one menu): "Windows XP 2", "... 3"."""
    taken = set()
    if os.path.isdir(MACHINES):
        for mid in os.listdir(MACHINES):
            try:
                taken.add(json.load(open(os.path.join(MACHINES, mid, 'machine.json'), encoding='utf-8')).get('name', '').lower())
            except (OSError, ValueError):
                pass
    if name.lower() not in taken:
        return name
    n = 2
    while ('%s %d' % (name, n)).lower() in taken:
        n += 1
    return '%s %d' % (name, n)


def create(name, template, extra):
    t = TEMPLATES[template]
    name = unique_name(name)
    mid = new_id(name)
    d = os.path.join(MACHINES, mid)
    os.makedirs(d)
    cfg = dict(name=name, template=template, family='windows' if template.startswith('win') else 'dos' if template == 'freedos' else 'linux',
               machine=t['machine'], cpus=t['cpus'], memory=t['memory'], bus=t['bus'], sound=t['sound'], nic=t['nic'],
               firmware=t['firmware'], reader=t['reader'], disks=[], shared=False, usb=True, created=time.strftime('%Y-%m-%d %H:%M'))
    cfg.update(extra)
    json.dump(cfg, open(os.path.join(d, 'machine.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
    return mid


def make_disk(path, gib):
    r = subprocess.run(['qemu-img', 'create', '-f', 'qcow2', path, '%dG' % gib], capture_output=True, text=True)
    return r.returncode == 0, r.stderr.strip()


def clone(src, name):
    name = unique_name(name)
    mid = new_id(name)
    d = os.path.join(MACHINES, mid)
    os.makedirs(d)
    cfg = dict(src.cfg)
    cfg['name'] = name
    disks = []
    for n, disk in enumerate(src.cfg.get('disks', [])):
        out = 'disk%d.qcow2' % n
        r = subprocess.run(['qemu-img', 'convert', '-O', 'qcow2', src.path(disk), os.path.join(d, out)], capture_output=True, text=True)
        if r.returncode:
            shutil.rmtree(d, ignore_errors=True)
            raise RuntimeError(r.stderr.strip())
        disks.append(out)
    cfg['disks'] = disks
    json.dump(cfg, open(os.path.join(d, 'machine.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
    return mid


# ---- import of foreign machines and disks ----
DISK_EXT = ('.vmdk', '.vdi', '.vhd', '.vhdx', '.qcow2', '.img', '.raw')


def disk_info(path):
    r = subprocess.run(['qemu-img', 'info', '--output=json', path], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr.strip() or 'qemu-img info failed')
    return json.loads(r.stdout)


def convert(src, dst):
    r = subprocess.run(['qemu-img', 'convert', '-p', '-O', 'qcow2', src, dst], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr.strip() or 'qemu-img convert failed')


def parse_vmx(path):
    vals = {}
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'\s*([^=#]+?)\s*=\s*"(.*)"\s*$', line)
        if m:
            vals[m.group(1).lower()] = m.group(2)
    disks = []
    for k, v in vals.items():
        m = re.match(r'((?:scsi|sata|ide|nvme)\d+:\d+)\.filename$', k)
        if m and v.lower().endswith('.vmdk') and vals.get(m.group(1) + '.present', 'TRUE').upper() == 'TRUE' \
                and vals.get(m.group(1) + '.devicetype', 'disk').lower() in ('disk', 'scsi-hardDisk'.lower()):
            disks.append((m.group(1), os.path.join(os.path.dirname(path), v)))
    disks.sort()
    nic = vals.get('ethernet0.virtualdev', 'e1000').lower()
    nicmap = {'e1000': 'e1000', 'e1000e': 'e1000e', 'vlance': 'pcnet', 'vmxnet3': 'e1000', 'vmxnet': 'e1000'}
    snd = vals.get('sound.virtualdev', 'hdaudio' if vals.get('sound.present', 'FALSE').upper() == 'TRUE' else 'none').lower()
    sndmap = {'hdaudio': 'hda', 'es1371': 'ES1370', 'sb16': 'sb16', 'none': 'none'}
    return dict(name=vals.get('displayname', os.path.splitext(os.path.basename(path))[0]),
                memory=int(vals.get('memsize', '1024')), cpus=int(vals.get('numvcpus', '1')), guest=vals.get('guestos', ''),
                firmware='uefi' if vals.get('firmware', 'bios').lower() == 'efi' else 'bios',
                disks=[d for _, d in disks], nic=nicmap.get(nic, 'e1000') if vals.get('ethernet0.present', 'TRUE').upper() == 'TRUE' else 'none',
                sound=sndmap.get(snd, 'hda'), bus='nvme' if any(k.startswith('nvme') for k, _ in disks) else 'ahci')


def parse_ovf(path, base):
    tree = ET.parse(path)
    root = tree.getroot()
    def local(tag):
        return tag.rsplit('}', 1)[-1]
    files, disks_ref = {}, {}
    for el in root.iter():
        if local(el.tag) == 'File':
            a = {local(k): v for k, v in el.attrib.items()}
            files[a.get('id')] = os.path.join(base, a.get('href', ''))
        if local(el.tag) == 'Disk':
            a = {local(k): v for k, v in el.attrib.items()}
            disks_ref[a.get('diskId')] = files.get(a.get('fileRef'))
    info = dict(name=os.path.splitext(os.path.basename(path))[0], memory=1024, cpus=1, disks=[], nic='none', sound='none',
                firmware='bios', bus='ahci', guest='')
    for el in root.iter():
        t = local(el.tag)
        if t in ('VirtualSystem',):
            vid = {local(k): v for k, v in el.attrib.items()}.get('id')
            if vid:
                info['name'] = vid
        if t == 'OperatingSystemSection':
            for c in el:
                if local(c.tag) == 'Description' and c.text:
                    info['guest'] = c.text
        if t == 'Item' or t == 'StorageItem' or t == 'EthernetPortItem':
            d = {local(c.tag): (c.text or '') for c in el}
            rt = d.get('ResourceType', '')
            if rt == '3':
                info['cpus'] = int(d.get('VirtualQuantity', '1') or 1)
            elif rt == '4':
                q = int(d.get('VirtualQuantity', '1024') or 1024)
                units = d.get('AllocationUnits', 'byte * 2^20').replace(' ', '')
                info['memory'] = q * 1024 if '2^30' in units else q
            elif rt == '10':
                sub = d.get('ResourceSubType', '').lower()
                info['nic'] = 'pcnet' if 'pcnet' in sub else 'e1000e' if 'e1000e' in sub else 'e1000'
            elif rt == '35':
                info['sound'] = 'AC97' if 'ac97' in d.get('ResourceSubType', '').lower() else 'hda'
            elif rt == '17':
                host = d.get('HostResource', '')
                m = re.search(r'(?:disk|file)/(.+)$', host)
                if m:
                    ref = disks_ref.get(m.group(1)) or files.get(m.group(1))
                    if ref:
                        info['disks'].append(ref)
    for el in root.iter():
        if local(el.tag) == 'Config':
            a = {local(k): v for k, v in el.attrib.items()}
            if a.get('key') == 'firmware' and a.get('value') == 'efi':
                info['firmware'] = 'uefi'
    return info


def import_machine(path, workdir):
    """A machine description from a .vmx, .ovf, .ova or a lone disk image; disks are converted later."""
    low = path.lower()
    if low.endswith('.vmx'):
        return parse_vmx(path)
    if low.endswith('.ovf'):
        return parse_ovf(path, os.path.dirname(path))
    if low.endswith('.ova'):
        os.makedirs(workdir, exist_ok=True)
        with tarfile.open(path) as t:
            for m in t.getmembers():
                if m.isfile() and '/' not in m.name.strip('/') and '..' not in m.name:
                    t.extract(m, workdir, filter='data')
        ovf = [f for f in os.listdir(workdir) if f.lower().endswith('.ovf')]
        if not ovf:
            raise RuntimeError('no .ovf inside')
        return parse_ovf(os.path.join(workdir, ovf[0]), workdir)
    if low.endswith(DISK_EXT):
        info = disk_info(path)
        return dict(name=os.path.splitext(os.path.basename(path))[0], memory=1024, cpus=1, disks=[path], nic='e1000',
                    sound='hda', firmware='bios', bus='ahci', guest=info.get('format', ''))
    raise RuntimeError('unknown file type')


def find_importable(roots=('/hv/images', '/media', '/run/charonhv/usb')):
    mount_media()
    out = []
    for r in roots:
        for dp, dn, fn in os.walk(r):
            if dp.count(os.sep) - r.count(os.sep) > 3:
                dn[:] = []
                continue
            for f in fn:
                if f.lower().endswith(('.vmx', '.ovf', '.ova') + DISK_EXT) and not dp.startswith('/hv/machines'):
                    out.append(os.path.join(dp, f))
    return sorted(set(out))


def block_devices():
    """{device: {TYPE, LABEL, ...}} from blkid."""
    out = {}
    try:
        text = subprocess.run(['blkid', '-o', 'export'], capture_output=True, text=True, timeout=20).stdout
    except Exception:
        return out
    for block in text.strip().split('\n\n'):
        d = dict(l.split('=', 1) for l in block.splitlines() if '=' in l)
        if 'DEVNAME' in d:
            out[d['DEVNAME']] = d
    return out


def mount_media():
    """Every other disk, stick or disc the computer has, read-only under /run/charonhv/usb/<device> (for ISO files,
    machine files and disk images). The hypervisor's own partitions and anything already mounted are left alone."""
    mounted = {l.split()[0] for l in open('/proc/mounts')}
    for dev, d in block_devices().items():
        if dev in mounted or d.get('LABEL') in ('CHARONHV', 'CHARONHVDATA') or d.get('TYPE') not in ('vfat', 'exfat', 'ntfs', 'ext4', 'ext3', 'ext2', 'iso9660', 'udf', 'btrfs', 'xfs'):
            continue
        mnt = '/run/charonhv/usb/' + os.path.basename(dev)
        os.makedirs(mnt, exist_ok=True)
        fs = 'ntfs3' if d.get('TYPE') == 'ntfs' else d.get('TYPE')
        subprocess.run(['mount', '-t', fs, '-o', 'ro', dev, mnt], capture_output=True)


def disc_devices():
    """(device, label) of ISO 9660 / UDF media: an optical disc, or an ISO written to a stick."""
    return sorted((dev, d.get('LABEL', '')) for dev, d in block_devices().items() if d.get('TYPE') in ('iso9660', 'udf'))


def find_isos(roots=('/hv/images', '/media', '/run/charonhv/usb')):
    mount_media()
    out = []
    for r in roots:
        for dp, dn, fn in os.walk(r):
            if dp.count(os.sep) - r.count(os.sep) > 3:
                dn[:] = []
                continue
            out += [os.path.join(dp, f) for f in fn if f.lower().endswith('.iso')]
    return sorted(set(out)) + ['%s|%s' % (dev, label) for dev, label in disc_devices()]


def usb_devices():
    """(bus, address, name) of the computer's USB devices that are not hubs nor the keyboard in use."""
    out = []
    base = '/sys/bus/usb/devices'
    try:
        names = os.listdir(base)
    except OSError:
        return out
    for n in names:
        p = os.path.join(base, n)
        try:
            cls = open(os.path.join(p, 'bDeviceClass')).read().strip()
            busnum = int(open(os.path.join(p, 'busnum')).read())
            dev = int(open(os.path.join(p, 'devnum')).read())
        except OSError:
            continue
        if cls == '09':
            continue
        def rd(f):
            try:
                return open(os.path.join(p, f)).read().strip()
            except OSError:
                return ''
        name = ' '.join(x for x in (rd('manufacturer'), rd('product')) if x) or '%s:%s' % (rd('idVendor'), rd('idProduct'))
        out.append((busnum, dev, name))
    return sorted(out)
