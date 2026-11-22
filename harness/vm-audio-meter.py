"""vm-audio-meter.py VMX_PID SECONDS [OUT.txt]: measures from OUTSIDE whether a VMware test machine plays sound on the host.
Samples the peak meter of that vmware-vmx.exe process's Windows audio session
(IAudioMeterInformation, 10 times a second). Prints one line per second with sound: time, peak (0..1), samples above 0.001.
The session appears only once the guest opens its sound card; "no session" lines say so."""
import subprocess,sys,time,warnings
warnings.simplefilter('ignore')
from pycaw.pycaw import AudioUtilities,IAudioMeterInformation
name,seconds=sys.argv[1],float(sys.argv[2])
out=open(sys.argv[3],'a',encoding='utf-8') if len(sys.argv)>3 else None
def say(t):
    print(t,flush=True)
    if out:out.write(t+'\n');out.flush()
pids=[int(name)]   # the process id of the machine's vmware-vmx.exe (its command line is not readable): list them before and after the start
say('meter %s pid %d start %s'%(name,pids[0],time.strftime('%H:%M:%S')))
end=time.time()+seconds;meter=None;last=0
while time.time()<end:
    if meter is None:
        for s in AudioUtilities.GetAllSessions():
            if s.Process and s.Process.pid==pids[0]:meter=s._ctl.QueryInterface(IAudioMeterInformation);say('session found '+time.strftime('%H:%M:%S'));break
        else:
            if time.time()-last>30:say('no session '+time.strftime('%H:%M:%S'));last=time.time()
            time.sleep(1);continue
    peak=0;loud=0
    for _ in range(10):
        try:v=meter.GetPeakValue()
        except Exception:meter=None;break
        peak=max(peak,v);loud+=v>0.001;time.sleep(0.1)
    if loud:say('%s peak %.3f loud %d/10'%(time.strftime('%H:%M:%S'),peak,loud))
say('meter end '+time.strftime('%H:%M:%S'))
