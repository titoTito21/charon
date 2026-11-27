# SPDX-License-Identifier: GPL-2.0-or-later
"""Charon Hypervisor: the time source (6q time sync, the r410 session-4 clock fault).

The PC's real-time clock is usually kept in LOCAL time (Windows on the same disk does that; measured on the VMware test
machine: Alpine read the RTC as UTC and its "UTC" was the host's CET time, so fresh certificates were an hour in the
future for Windows XP). The hypervisor never writes the RTC (etc/conf.d/hwclock: clock_systohc=NO), so Windows keeps
its clock. Instead:
  - when the network is up, the true UTC comes from NTP (busybox ntpd, pool.ntp.org; Alpine's own client);
  - the difference RTC - UTC, rounded to 15 minutes, is the RTC's offset (0 for a UTC clock, +60 for CET ...); it is kept
    in /hv/clock.json, so without a network the next start corrects the clock from the RTC with the last known offset;
  - TZ is set for the hypervisor and everything it starts, so QEMU's "-rtc base=localtime" gives Windows guests the
    local time and Linux guests (base=utc) the real UTC.
A summer/winter time change without a network is NOT seen (the stored offset stays until the next NTP answer) - said
in the clock report."""
import json, os, subprocess, time

STATE = '/hv/clock.json'
APPLIED = '/run/charonhv/clock-applied'
SERVERS = ('pool.ntp.org', 'time.cloudflare.com')


def _load():
    try:
        return json.load(open(STATE))
    except (OSError, ValueError):
        return {}


def _save(d):
    try:
        with open(STATE + '.new', 'w') as f:
            json.dump(d, f)
        os.replace(STATE + '.new', STATE)
    except OSError:
        pass


def tz_string(offset_min):
    """POSIX TZ for a fixed offset east of UTC (POSIX writes the sign the other way round)."""
    if not offset_min:
        return 'UTC0'
    s = '-' if offset_min > 0 else '+'
    a = abs(offset_min)
    return 'CHV%s%d:%02d' % (s, a // 60, a % 60)


def applied_offset():
    try:
        return int(open(APPLIED).read().strip())
    except (OSError, ValueError):
        return 0


def _set_clock(epoch):
    subprocess.run(['date', '-u', '-s', '@%d' % int(epoch)], capture_output=True)


def at_start(log):
    """Once per boot, before any machine starts: correct the clock from the last known offset; set TZ."""
    d = _load()
    off = int(d.get('rtc_offset_min', 0))
    if not os.path.exists(APPLIED):
        if off:
            _set_clock(time.time() - off * 60)
            log('clock: the real-time clock runs %+d minutes from UTC (last NTP %s); corrected' % (off, d.get('when', '?')))
        try:
            open(APPLIED, 'w').write(str(off))
        except OSError:
            pass
    os.environ['TZ'] = tz_string(int(d.get('tz_offset_min', off)))
    try:
        time.tzset()
    except AttributeError:
        pass
    return d


def sync(log, timeout=25):
    """Ask NTP for the true time. Returns (ok, text EN, text PL)."""
    before = time.time()
    rtc_as_utc = before + applied_offset() * 60     # what the clock read before any correction
    for server in SERVERS:
        try:
            r = subprocess.run(['ntpd', '-n', '-q', '-p', server], capture_output=True, text=True, timeout=timeout)
        except (OSError, subprocess.TimeoutExpired) as e:
            log('clock: ntp %s: %r' % (server, e))
            continue
        if r.returncode == 0:
            utc = time.time()
            elapsed = 0  # ntpd -q returns right after setting the clock: the sub-second run time is not counted
            off = int(round((rtc_as_utc - utc + elapsed) / 900.0)) * 15
            d = _load()
            d.update({'rtc_offset_min': off, 'tz_offset_min': off if off else d.get('tz_offset_min', 0),
                      'when': time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime(utc)), 'server': server})
            _save(d)
            try:
                open(APPLIED, 'w').write(str(off))
            except OSError:
                pass
            os.environ['TZ'] = tz_string(d['tz_offset_min'])
            try:
                time.tzset()
            except AttributeError:
                pass
            step = utc - before
            log('clock: NTP %s, clock stepped %+.0f s, RTC offset %+d min' % (server, step, off))
            return (True, 'Clock set from the internet time server %s; the computer clock runs %s.' % (server, describe(off, 'en')),
                    'Zegar ustawiony z internetowego serwera czasu %s; zegar komputera chodzi %s.' % (server, describe(off, 'pl')))
        log('clock: ntp %s failed: %s' % (server, (r.stderr or r.stdout).strip()[:200]))
    return (False, 'No internet time server answered; the clock stays as the computer keeps it%s.' % (
                ', corrected by %+d minutes' % -applied_offset() if applied_offset() else ''),
            'Żaden internetowy serwer czasu nie odpowiedział; zegar zostaje taki, jak trzyma go komputer%s.' % (
                ', poprawiony o %+d minut' % -applied_offset() if applied_offset() else ''))


def describe(off, lang):
    if not off:
        return 'in UTC' if lang == 'en' else 'w UTC'
    h = '%d:%02d' % (abs(off) // 60, abs(off) % 60)
    if lang == 'en':
        return 'in local time, %s hours %s UTC' % (h, 'ahead of' if off > 0 else 'behind')
    return 'w czasie lokalnym, %s godzin %s UTC' % (h, 'przed' if off > 0 else 'za')


def report(lang):
    d = _load()
    off = int(d.get('rtc_offset_min', 0))
    t = time.strftime('%H:%M', time.localtime())
    u = time.strftime('%H:%M', time.gmtime())
    if lang == 'en':
        return ('Local time %s, UTC %s. The computer clock runs %s; last internet time %s. A summer or winter time change is '
                'seen only at the next internet time.' % (t, u, describe(off, 'en'), d.get('when', 'never')))
    return ('Czas lokalny %s, UTC %s. Zegar komputera chodzi %s; ostatni czas z internetu %s. Zmianę czasu letniego lub zimowego '
            'widać dopiero przy następnym czasie z internetu.' % (t, u, describe(off, 'pl'), d.get('when', 'nigdy')))
