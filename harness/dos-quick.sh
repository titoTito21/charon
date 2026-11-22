# dos-quick.sh NAME VNCPORT APP.EFI DRIVER.EFI PCBUILD : new VMware machine, Charon's dialogue installs CharonDOS alone
# (no programs, no Windows profile) on the whole disk, the machine is restarted into CharonDOS and the driver's audio
# trace after the restart is printed. Serial port = VNCPORT+10.
N=$1; P=$2; APP=$3; DRV=$4; PC=$5; VMR="/c/Program Files (x86)/VMware/VMware Workstation/vmrun.exe"
cd "$(dirname "$0")"
mkdir $N || exit 1
ISO_LANGUAGE=pl TRACE_AUDIODXE=${AUDIODXE:-charon-audio-driver-r407/AudioDxe-r407.efi} TRACE_CHARONPC=$PC/CHARONPC.EFI python3.15 make-charon-iso-trace.py $APP $DRV $N/charon.iso 2>/dev/null | tail -1
python3.15 make-vmware-vm.py $N $N/charon.iso 8 $P hdaudio 4096 >/dev/null
: > $N/serial.txt
"$VMR" -T ws start "$(cygpath -w $N/$N.vmx)" gui >/dev/null 2>&1
(python3.15 serial-pipe.py $N $((P+10)) > $N/pipe.log 2>&1 &)
n=0; until grep -a -q "Firmware nie znalaz" $N/serial.txt 2>/dev/null; do sleep 2; n=$((n+1)); [ $n -gt 90 ] && { echo "no menu"; exit 1; }; done; sleep 3
python3.15 vmw-step.py $N $P 3 "ret|down|down|down|down|down" >/dev/null
python3.15 vmw-step.py $N $P 5 "ret|down down down down|ret" >/dev/null
python3.15 vmw-step.py $N $P 3 "down down down down down" >/dev/null
python3.15 vmw-step.py $N $P 8 "ret" >/dev/null
python3.15 vmw-step.py $N $P 8 "ret" >/dev/null
python3.15 vmw-step.py $N $P 6 "ret" >/dev/null
python3.15 vmw-step.py $N $P 3 "down down down down down down down down" >/dev/null
python3.15 vmw-step.py $N $P 8 "ret" >/dev/null
python3.15 vmw-step.py $N $P 8 "ret|ret" | tail -1 | cut -c1-160
python3.15 vmw-step.py $N $P 10 "type:tak|ret" > /dev/null
n=0; until tr -d '\033' < $N/serial.txt | grep -a -q "Zainstalowa. teraz Windows 11\|Nie skopiowano\|Nie zapisano\|Nie utworzono"; do sleep 5; n=$((n+1)); [ $n -gt 60 ] && break; done
python3.15 vmw-step.py $N $P 6 "ret" | tail -1 | cut -c1-120
"$VMR" -T ws stop "$(cygpath -w $N/$N.vmx)" hard >/dev/null 2>&1; sleep 5
wc -c < $N/serial.txt > $N/restart-offset.txt
"$VMR" -T ws start "$(cygpath -w $N/$N.vmx)" gui >/dev/null 2>&1; sleep 2
(python3.15 serial-pipe.py $N $((P+10)) > $N/pipe2.log 2>&1 &)
sleep 55
python3.15 vnc-keys.py $P --shot $N/after.png 2>&1 | tail -1
tail -c +$(cat $N/restart-offset.txt) $N/serial.txt | tr -d '\033\r' | grep -a -i "CHARON audio\|speak$\|first PCM\|Exception\|usb audio" | uniq -c | head -40 | cut -c1-150
