#pragma once
// r400: Charon workspace - cloud install, part 2: Ubuntu (autoinstall),
// Windows 11 (answer file on the user's install media) and the dialogue that
// ties the systems together. See CharonWsInstall.h. GPL-2.0-or-later.
#include "CharonWsInstall.h"
#include "CharonWsWinUsb.h"
namespace hakefi {
namespace ws {
namespace install {
// r403: speechScript (the readers in the installed system) is in CharonWsReaders.h.
// Speech inside the Ubuntu installer itself (its live system runs in memory):
// espeakup on the speakup console driver, so a blind person hears what the
// installer prints, plus Charon's own spoken notes (start, every two minutes,
// the end). Runs from early-commands, before any disk is touched.
inline const char liveSpeechScript[]=
    "#!/bin/sh\n"
    "# Written by Charon: speech inside the Ubuntu installer (its live system, in memory).\n"
    "L=$(cat /run/charon/lang.txt 2>/dev/null); V=en; case \"$L\" in pl*) V=pl;; esac\n"
    "say() {\n"
    "  if [ -e /sys/accessibility/speakup/synth ] && pgrep espeakup >/dev/null 2>&1; then echo \"Charon: $1\" > /dev/tty1\n"
    "  elif command -v espeak-ng >/dev/null 2>&1; then espeak-ng -v $V \"Charon: $1\" 2>/dev/null\n"
    "  fi\n"
    "}\n"
    "if [ \"$1\" = done ]; then\n"
    "  if [ $V = pl ]; then say \"koniec instalacji, restart komputera.\"; else say \"installation finished, the computer restarts.\"; fi\n"
    "  sleep 7; exit 0\n"
    "fi\n"
    "if [ \"$1\" = tell ]; then\n"
    "  n=0\n"
    "  while sleep 120; do n=$((n+2))\n"
    "    if [ $V = pl ]; then say \"instalacja trwa, minut: $n.\"; else say \"still installing, $n minutes.\"; fi\n"
    "  done\n"
    "  exit 0\n"
    "fi\n"
    "export DEBIAN_FRONTEND=noninteractive\n"
    "k=$(uname -r); need=\n"
    "command -v espeak-ng >/dev/null 2>&1 || need=\"$need espeak-ng\"\n"
    "command -v espeakup >/dev/null 2>&1 || need=\"$need espeakup\"\n"
    "command -v amixer >/dev/null 2>&1 || need=\"$need alsa-utils\"\n"
    "find /lib/modules/$k -name 'speakup_soft*' 2>/dev/null | grep -q . || need=\"$need linux-modules-extra-$k\"\n"
    "echo \"charon: kernel $k, to install:$need\"\n"
    "if [ -n \"$need\" ]; then\n"
    "  timeout 180 apt-get update >/dev/null 2>&1 || echo \"charon: package lists not refreshed\"\n"
    "  for p in $need; do timeout 300 apt-get install -y --no-install-recommends $p >/dev/null 2>&1 || echo \"charon: could not install $p\"; done\n"
    "fi\n"
    "udevadm trigger --action=add --subsystem-match=pci 2>/dev/null; udevadm settle -t 10 2>/dev/null\n"
    "modprobe snd_hda_intel 2>/dev/null; modprobe snd_intel8x0 2>/dev/null\n"
    "for c in 0 1 2 3; do for m in Master PCM Speaker Headphone; do amixer -q -c $c sset $m 80% unmute 2>/dev/null; done; done\n"
    "if modprobe speakup_soft 2>/dev/null; then\n"
    "  printf 'VOICE=%s\\ndefault_voice=%s\\n' $V $V >> /etc/default/espeakup\n"
    "  systemctl restart espeakup 2>/dev/null || espeakup -V $V 2>/dev/null\n"
    "  sleep 3\n"
    "else\n"
    "  echo \"charon: no speakup driver, Charon's own notes only\"\n"
    "fi\n"
    "if [ $V = pl ]; then say \"mowa w instalatorze Ubuntu. Start instalacji.\"; else say \"speech in the Ubuntu installer. The installation starts.\"; fi\n"
    "systemd-run --no-block --unit charon-tell sh /run/charon/live-speech.sh tell >/dev/null 2>&1 || setsid sh /run/charon/live-speech.sh tell </dev/null >/dev/null 2>&1 &\n"
    "exit 0\n";
// ---- Ubuntu ---------------------------------------------------------------------
inline const Question ubuntuQuestions[]={
    {"desktop",true,Kind::multi,u"Pulpity:",u"Desktops:","ubuntu-desktop-minimal",CHARON_CHOICES(ubuntuDesktops),u"Można zaznaczyć kilka pulpitów albo żadnego: wtedy serwer z mową w konsoli.",u"Several desktops may be checked, or none: then a server with console speech."},
    {"readers",true,Kind::multi,u"Czytniki ekranu:",u"Screen readers:","orca,speakup",CHARON_CHOICES(linuxReaders),CHARON_READER_HELP_PL,CHARON_READER_HELP_EN},
    {"ssh",false,Kind::yesNo,u"Zainstalować serwer SSH?",u"Install the SSH server?","no",nullptr,0,nullptr,nullptr},
    {"updates",false,Kind::yesNo,u"Pobrać aktualizacje podczas instalacji? Trwa dłużej.",u"Download updates during the installation? It takes longer.","no",nullptr,0,nullptr,nullptr},
};
inline constexpr char16_t ubuntuBase[]=u"http://releases.ubuntu.com/26.04/";
// The system image must be the very one the network kernel and installer
// were built for (the installer compares identifiers and refuses another -
// seen with a 26.04 image against the 26.04.1 installer). Ubuntu names it in
// its own network boot configuration next to them; Charon reads it there.
inline bool UbuntuImageAddress(char* out,size_t capacity) {
    static char16_t url[300];static Memory config;out[0]=0;
    Set(url,300,ubuntuBase);Cat(url,300,u"netboot/amd64/grub/grub.cfg");
    if(!FetchToMemory(url,config,0,T(u"opis instalatora Ubuntu",u"the Ubuntu installer's description")))return false;
    static const char key[]="iso-url=";const size_t keyLength=sizeof(key)-1;
    for(size_t i=0;i+keyLength<config.size && !out[0];++i) {
        size_t m=0;while(m<keyLength && char(config.data[i+m])==key[m])++m;
        if(m!=keyLength)continue;
        size_t k=0;
        for(size_t j=i+keyLength;j<config.size && config.data[j]>' ' && config.data[j]<127 && k+1<capacity;++j)out[k++]=char(config.data[j]);
        out[k]=0;
    }
    Free(config);
    size_t n=0;while(out[n])++n;
    const bool http=out[0]=='h' && out[1]=='t' && out[2]=='t' && out[3]=='p';
    return http && n>12 && out[n-4]=='.' && out[n-3]=='i' && out[n-2]=='s' && out[n-1]=='o';
}
// cloud-init user-data with the autoinstall section.
inline void UbuntuUserData(Text& y,bool largest) {
    y.Line("#cloud-config");
    y.Line("# Written by Charon from spoken answers.");
    y.Line("autoinstall:");
    y.Line("  version: 1");
    y.Line("  locale: ",ask::Get("locale"),".UTF-8");
    y.Line("  keyboard: {layout: ",ask::Get("keyboard"),"}");
    y.Line("  timezone: ",ask::Get("timezone"));
    y.Line("  identity:");
    y.Line("    hostname: ",ask::Get("hostname"));
    y.Line("    realname: \"",ask::Get("fullname"),"\"");
    y.Line("    username: ",ask::Get("user"));
    y.Line("    password: \"",ask::Get("hash"),"\"");
    y.Line("  ssh: {install-server: ",ask::Is("ssh","yes")?"true":"false","}");
    y.Line("  updates: ",ask::Is("updates","yes")?"all":"security");
    y.Line("  storage:");
    y.Line("    layout:");
    y.Line("      name: direct");
    if(ask::Is("diskmode","free"))y.Line("      mode: use_gap");
    y.Line("      match: {size: ",largest?"largest":"smallest","}");
    y.Add("  packages: [efibootmgr");
    {static char packages[200];ReaderPackages(packages,sizeof(packages),ask::Any("desktop"));y.Add(packages);}
    for(const auto& c:ubuntuDesktops)if(ask::Has("desktop",c.value)) {y.Add(", ");y.Add(c.value);}
    y.Line("]");
    // Before any disk is touched: Charon and the scripts go into memory
    // (/run is a RAM file system), because the disk they are on may be the
    // one that gets erased. The borrowed volume label is given back here too.
    y.Line("  early-commands:");
    y.Line("    - mkdir -p /run/charon/EFI /mnt/charon; for d in $(blkid -t TYPE=vfat -o device); do mount -o ro $d /mnt/charon 2>/dev/null || continue; [ -f /mnt/charon/EFI/HAK/CHARONVOICE.EFI ] && [ ! -d /run/charon/EFI/HAK ] && cp -r /mnt/charon/EFI/HAK /run/charon/EFI/; [ -d /mnt/charon/charon-install/ubuntu ] && cp /mnt/charon/charon-install/ubuntu/*.sh /mnt/charon/charon-install/ubuntu/*.wav /mnt/charon/charon-install/ubuntu/label.txt /run/charon/ 2>/dev/null; umount /mnt/charon; done; true");
    y.Line("    - rm -f /run/charon/EFI/HAK/continue.bin /run/charon/EFI/HAK/continue-set /run/charon/EFI/HAK/installed-by.txt /run/charon/EFI/HAK/reader-only; true");
    if(ask::Is("speech","yes"))y.Line("    - echo ",ask::Get("locale")," > /run/charon/lang.txt; sh /run/charon/live-speech.sh > /run/charon/live-speech.log 2>&1; true");
    y.Line("    - L=$(cat /run/charon/label.txt 2>/dev/null); for d in /dev/disk/by-label/CIDATA; do [ -e $d ] && [ -n \"$L\" ] && fatlabel $d \"$L\"; done; true");
    y.Line("  late-commands:");
    y.Line("    - mkdir -p /target/boot/efi/EFI/HAK /target/var/lib/charon");
    y.Line("    - cp -r /run/charon/EFI/HAK/. /target/boot/efi/EFI/HAK/ 2>/dev/null; cp /run/charon/*.sh /run/charon/*.wav /target/var/lib/charon/ 2>/dev/null; true");
    y.Line("    - curtin in-target -- sh /var/lib/charon/install-charon.sh || true");
    if(ask::Is("speech","yes")) {static char args[200];ReaderArguments(args,sizeof(args));
        y.Line("    - curtin in-target -- sh /var/lib/charon/speech.sh ",args," > /target/var/lib/charon/speech.log 2>&1 || true");}
    if(ask::Is("speech","yes"))y.Line("    - sh /run/charon/live-speech.sh done; true");
    y.Line("  shutdown: reboot");
}
inline efi_status SetVolumeLabel(efi_handle volume,const char16_t* label) {
    static efi_guid labelGuid={0xdb47d7d3,0xfe81,0x11d3,{0x9a,0x35,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
    efi_file_protocol* root=OpenRoot(volume);if(!root)return EFI_NOT_FOUND;
    static char16_t text[16];Set(text,12,label);
    const auto status=root->SetInfo(root,&labelGuid,(Len(text)+1)*2,text);
    root->Close(root);return status;
}
inline bool WriteText(efi_handle volume,const char16_t* path,const char* text,size_t size) {
    static FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,path);
    return WriteWhole(ref,reinterpret_cast<const uint8_t*>(text),size)==EFI_SUCCESS;
}
inline bool CopyReader(efi_handle volume,const char16_t* folder) {
    static const char16_t* const names[]={u"CHARONVOICE.EFI",u"AudioDxe.efi"};bool all=true;
    for(const auto* name:names) {
        static FileRef from,to;from.volume=U().host->selfDevice;Set(from.path,pathUnits,u"\\EFI\\HAK\\");Cat(from.path,pathUnits,name);
        to.volume=volume;if(!JoinPath(to.path,pathUnits,folder,name) || CopyFile(from,to)!=EFI_SUCCESS)all=false;
    }
    return all;
}
inline bool RunUbuntu() {
    auto& s=disks::D();const auto& t=Chosen();
    // The installer can be told "the largest disk" or "the smallest disk" only.
    bool largest=true,smallest=true;
    for(size_t i=0;i<s.count;++i)if(i!=t.disk) {
        if(disks::Bytes(s.list[i])>=disks::Bytes(s.list[t.disk]))largest=false;
        if(disks::Bytes(s.list[i])<=disks::Bytes(s.list[t.disk]))smallest=false;
    }
    if(!largest && !smallest) {
        Fail(T(u"Instalator Ubuntu umie wskazać tylko największy albo najmniejszy dysk, a wybrany nie jest żadnym z nich. Odłącz inne dyski albo wybierz Debiana.",
            u"The Ubuntu installer can only be pointed at the largest or the smallest disk, and the chosen one is neither. Unplug other disks or choose Debian."));return false;}
    // The Ubuntu installer keeps the whole system image (2.9 GB) in a RAM
    // file system that may take half of the memory: with less than 8 GB it
    // runs out of room near the end and stops at a silent rescue prompt
    // (seen with 7 GB). Refuse before anything is downloaded.
    {uint64_t bytes=0;
     if(systools::MemoryBytes(U().host->boot,bytes)==EFI_SUCCESS && bytes && bytes<(uint64_t(7600)<<20)) {
        Fail(T(u"Instalator Ubuntu potrzebuje co najmniej 8 gigabajtów pamięci RAM, bo trzyma w niej cały obraz systemu. Ten komputer ma za mało. Wybierz Debiana, któremu wystarczą 2 gigabajty.",
            u"The Ubuntu installer needs at least 8 gigabytes of RAM, because it keeps the whole system image there. This computer has too little. Choose Debian, which is content with 2 gigabytes."));return false;}}
    // r400: the kernel and the installer go to memory (an EFI partition is
    // often too small for them); only the small answer files need a volume.
    efi_handle volume=nullptr;if(!WorkVolume(uint64_t(4)<<20,volume))return false;
    if(MakeFolders(volume,u"\\charon-install\\ubuntu")!=EFI_SUCCESS) {Fail(T(u"Nie utworzono folderu na pliki instalatora.",u"The folder for the installer's files could not be created."));return false;}
    static char16_t url[300];
    static Memory kernel,initrd;static char image[220];
    if(!UbuntuImageAddress(image,sizeof(image))) {
        Fail(T(u"Nie udało się ustalić adresu obrazu systemu Ubuntu pasującego do instalatora. Spróbuj później albo wybierz Debiana.",
            u"The address of the Ubuntu system image that matches the installer could not be found. Try later or choose Debian."));return false;}
    Set(url,300,ubuntuBase);Cat(url,300,u"netboot/amd64/linux");
    if(!FetchToMemory(url,kernel,0,T(u"jądro instalatora Ubuntu",u"the Ubuntu installer's kernel")))return false;
    Set(url,300,ubuntuBase);Cat(url,300,u"netboot/amd64/initrd");
    if(!FetchToMemory(url,initrd,0,T(u"instalator Ubuntu, około 100 megabajtów",u"the Ubuntu installer, about 100 megabytes"))) {Free(kernel);return false;}
    static char yaml[8000];Text text{yaml,sizeof(yaml),0,false};yaml[0]=0;UbuntuUserData(text,largest);
    static char16_t oldLabel[64];VolumeLabel(volume,oldLabel,64);static uint8_t labelUtf8[64];const size_t labelBytes=Utf16ToUtf8(oldLabel,Len(oldLabel),labelUtf8,60);
    bool ok=!text.overflow && WriteText(volume,u"\\user-data",yaml,text.size) && WriteText(volume,u"\\meta-data","instance-id: charon-install\n",28)
        && WriteText(volume,u"\\charon-install\\ubuntu\\install-charon.sh",charonScript,sizeof(charonScript)-1)
        && WriteText(volume,u"\\charon-install\\ubuntu\\speech.sh",speechScript,sizeof(speechScript)-1)
        && WriteText(volume,u"\\charon-install\\ubuntu\\live-speech.sh",liveSpeechScript,sizeof(liveSpeechScript)-1)
        && [&]() {static uint8_t wav[16384];const size_t n=GoodbyeWav(wav,sizeof(wav));return n && WriteText(volume,u"\\charon-install\\ubuntu\\goodbye.wav",reinterpret_cast<const char*>(wav),n);}()
        && WriteText(volume,u"\\charon-install\\ubuntu\\label.txt",reinterpret_cast<const char*>(labelUtf8),labelBytes);
    for(auto& c:yaml)c=0;
    if(!ok) {Free(kernel);Free(initrd);Fail(T(u"Nie zapisano plików odpowiedzi.",u"The answer files could not be written."));return false;}
    if(!MeasureCharon().reader)Say(T(u"Uwaga: brak plików czytnika na dysku Charona.",u"Note: no reader files on Charon's disk."));
    // cloud-init finds its seed on a FAT volume named CIDATA: the volume lends its label.
    const auto labelled=SetVolumeLabel(volume,u"CIDATA");
    if(labelled!=EFI_SUCCESS) {Fail(T(u"Nie udało się nazwać woluminu CIDATA; instalator Ubuntu nie znalazłby odpowiedzi.",u"The volume could not be named CIDATA; the Ubuntu installer would not find the answers."),labelled);Free(kernel);Free(initrd);return false;}
    static char16_t arguments[500];
    Set(arguments,500,u"ip=dhcp iso-url=");
    {static char16_t wide[220];ask::Wide(image,wide,220);Cat(arguments,500,wide);}
    Cat(arguments,500,u" autoinstall ds=nocloud ---");
    if(ask::Is("speech","yes"))
        Say(T(u"Uruchamiam instalator Ubuntu. Najpierw sam pobiera obraz systemu, prawie 3 gigabajty, do pamięci; to kilka minut ciszy. Potem instalator zaczyna mówić: czyta to, co wypisuje, a Charon co dwie minuty dopowiada, że instalacja trwa, i zapowiada jej koniec. Po zakończeniu komputer uruchomi się ponownie i odezwie się nowy system.",
            u"Starting the Ubuntu installer. It first downloads the system image, almost 3 gigabytes, into memory; that is a few minutes of silence. Then the installer starts to speak: it reads what it prints, and every two minutes Charon adds that the installation goes on, and announces its end. When it is done the computer restarts and the new system speaks."));
    else
        Say(T(u"Uruchamiam instalator Ubuntu. Najpierw sam pobiera obraz systemu, prawie 3 gigabajty, do pamięci. Mowa jest wyłączona, więc instalator pracuje w ciszy; po zakończeniu komputer uruchomi się ponownie.",
            u"Starting the Ubuntu installer. It first downloads the system image, almost 3 gigabytes, into memory. Speech is off, so the installer works in silence; when it is done the computer restarts."));
    HearOut(200);
    StartFromMemory(kernel,initrd,u"Ubuntu installer",arguments);
    Free(kernel);Free(initrd);
    return false;
}
// ---- Windows 11 ------------------------------------------------------------------
inline const Choice windowsLocales[]={
    {u"polski",u"Polish","pl-PL"},{u"angielski, Stany Zjednoczone",u"English, United States","en-US"},{u"angielski, Wielka Brytania",u"English, United Kingdom","en-GB"},
    {u"niemiecki",u"German","de-DE"},{u"francuski",u"French","fr-FR"},{u"hiszpański",u"Spanish","es-ES"},{u"włoski",u"Italian","it-IT"},
    {u"ukraiński",u"Ukrainian","uk-UA"},{u"czeski",u"Czech","cs-CZ"}};
inline const Choice windowsEditions[]={
    {u"taka, jaką wybierze nośnik lub klucz w firmware",u"whatever the media or the firmware key selects","auto"},
    {u"Windows 11 Home",u"Windows 11 Home","YTMG3-N6DKC-DKB77-7M9GH-8HVX7"},{u"Windows 11 Pro",u"Windows 11 Pro","VK7JG-NPHTM-C97JM-9MPGT-3V66T"}};
inline const Choice windowsProfiles[]={
    {u"bez zmian",u"unchanged","none"},{u"odchudzony, bez aplikacji fabrycznych",u"slim, without the factory apps","Slim"},
    {u"ReviOS",u"ReviOS","ReviOS"},{u"AtlasOS",u"AtlasOS","AtlasOS"},{u"skrypt Win11Debloat",u"the Win11Debloat script","Script"}};
// r402: the Windows versions Charon can fetch. The first two come from
// Microsoft's download page (any language, the image holds Home, Pro and
// Education); the others are Microsoft's public evaluation images: fixed
// addresses, English only, they run 90 days (Server: 180) and cannot be
// turned into a full licence.
struct WindowsVersion {const char16_t* pl;const char16_t* en;const char* id;const char* url;unsigned index;};   // index: which system of the image is installed (evaluation images hold two, e.g. LTSC and LTSC N; Setup stopped at "Select Image" without it)
inline const WindowsVersion windowsVersions[]={
    {u"Windows 11, wydanie 26H2",u"Windows 11, release 26H2","11",nullptr,0},
    {u"Windows 10, wydanie 22H2",u"Windows 10, release 22H2","10",nullptr,0},
    {u"Windows 11 Enterprise, wersja próbna 90 dni, po angielsku",u"Windows 11 Enterprise, 90-day evaluation, English","ent","https://go.microsoft.com/fwlink/?linkid=2382600&clcid=0x409&culture=en-us&country=us",1},
    {u"Windows 11 Enterprise LTSC 2024, wersja próbna 90 dni, po angielsku",u"Windows 11 Enterprise LTSC 2024, 90-day evaluation, English","ltsc","https://go.microsoft.com/fwlink/?linkid=2289029&clcid=0x409&culture=en-us&country=us",1},
    {u"Windows 11 IoT Enterprise LTSC 2024, wersja próbna 90 dni, po angielsku",u"Windows 11 IoT Enterprise LTSC 2024, 90-day evaluation, English","iot","https://go.microsoft.com/fwlink/?linkid=2270353&clcid=0x409&culture=en-us&country=us",1},
    {u"Windows Server 2025, wersja próbna 180 dni, po angielsku",u"Windows Server 2025, 180-day evaluation, English","server","https://go.microsoft.com/fwlink/?linkid=2345730&clcid=0x409&culture=en-us&country=us",2},
    {u"Windows 10 Enterprise LTSC 2021, wersja próbna 90 dni, po angielsku",u"Windows 10 Enterprise LTSC 2021, 90-day evaluation, English","ltsc10","https://go.microsoft.com/fwlink/?linkid=2195404&clcid=0x409&culture=en-us&country=us",1},
};
// r403: the Windows screen readers, as check boxes. NVDA speaks already in
// Setup (a portable copy made on the install media); JAWS is installed at the
// first logon (Polish: the Altix installer if the user put one in
// \charon\readers, else the JAWS of Freedom Scientific); Narrator is built in.
inline const Choice windowsReaders[]={
    {u"NVDA, mówi już w instalatorze Windows",u"NVDA, speaks already in Windows Setup","nvda"},
    {u"JAWS: po polsku od Altixa, w innych językach od Freedom Scientific; bez licencji 40 minut na jedno uruchomienie Windows",
     u"JAWS: in Polish from Altix, in other languages from Freedom Scientific; without a licence 40 minutes per Windows start","jaws"},
    {u"Narrator, wbudowany w Windows",u"Narrator, built into Windows","narrator"}};
inline constexpr char nvdaFile[]="nvda_2026.3.exe";
inline constexpr char16_t nvdaUrl[]=u"https://github.com/nvaccess/nvda/releases/download/release-2026.3/nvda_2026.3.exe";
inline constexpr char nvdaSha256[]="a8a87cb823da0c62cfbae4a444ae5ae238d54dc82ad6d46d3fdb3fef082799b2";
// After the questions: which reader starts after sign-in. False: Escape.
inline bool ResolveWindowsReaders() {
    static Choice chosen[3];size_t n=0;
    for(const auto& c:windowsReaders)if(ask::Has("wreaders",c.value))chosen[n++]=c;
    ask::Put("speech",n?"yes":"no");
    if(n==0) {ask::Put("wfirst","none");return true;}
    if(n==1) {ask::Put("wfirst",chosen[0].value);return true;}
    // Narrator is part of Windows and is only switched on when nothing else is
    // chosen: it never competes with NVDA or JAWS, so only NVDA plus JAWS asks.
    const bool nvda=ask::Has("wreaders","nvda"),jaws=ask::Has("wreaders","jaws");
    if(!(nvda && jaws)) {ask::Put("wfirst",nvda?"nvda":"jaws");return true;}
    static const Choice names[]={{u"NVDA",u"NVDA","nvda"},{u"JAWS",u"JAWS","jaws"}};
    size_t i=0;
    if(!ask::Choose(T(u"Który czytnik ma startować po zalogowaniu i na ekranie logowania? Drugi zostanie zainstalowany.",
        u"Which reader starts after sign-in and on the sign-in screen? The other is installed too."),names,2,i))return false;
    ask::Put("wfirst",names[i].value);
    return true;
}
// NVDA for Setup: downloaded onto the install media, SHA-256 checked.
inline bool NvdaOntoMedia(efi_handle media) {
    if(!EnsureNetwork())return false;
    static Memory data;data.data=nullptr;data.capacity=0;data.size=0;data.extra=0;data.reserved=false;
    static FileRef none;none.volume=nullptr;none.path[0]=0;uint64_t received=0;
    Say(T(u"Pobieram NVDA na nośnik instalacyjny, żeby mówiło już w instalatorze.",u"Downloading NVDA to the install media, so it speaks already in Setup."));
    if(!net::DownloadAny(nvdaUrl,none,&received,&data,u"NVDA"))return false;
    Sha256 hash;hash.Update(data.data,data.size);uint8_t got[32];hash.Final(got);static const char hex[]="0123456789abcdef";
    bool same=true;for(int i=0;i<32 && same;++i)same=nvdaSha256[i*2]==hex[got[i]>>4] && nvdaSha256[i*2+1]==hex[got[i]&15];
    if(!same) {Free(data);Fail(T(u"Pobrane NVDA różni się od sprawdzonego pliku. Nie zapisano.",u"The downloaded NVDA differs from the checked file. Not saved."));return false;}
    MakeFolders(media,u"\\charon\\readers");
    static FileRef target;target.volume=media;Set(target.path,pathUnits,u"\\charon\\readers\\");CatAscii(target.path,pathUnits,nvdaFile);
    const efi_status st=WriteWhole(target,data.data,data.size);Free(data);
    if(st!=EFI_SUCCESS) {Fail(T(u"Nie zapisano NVDA na nośniku.",u"NVDA was not saved on the media."),st);return false;}
    ask::Put("nvdaready","yes");
    return true;
}
inline const Question windowsQuestions[]={
    {"wlocale",true,Kind::choice,u"Język Windows:",u"Windows language:","en-US",CHARON_CHOICES(windowsLocales),u"Język musi być na nośniku instalacyjnym.",u"The language must be on the install media."},
    {"hostname",false,Kind::name,u"Nazwa komputera:",u"Computer name:","charon-pc",nullptr,0,nullptr,nullptr},
    {"user",true,Kind::name,u"Nazwa użytkownika:",u"User name:","user",nullptr,0,nullptr,nullptr},
    {"password",true,Kind::secret,u"Hasło użytkownika:",u"User's password:","",nullptr,0,u"Hasło trafia do pliku odpowiedzi na nośniku; instalator kasuje je z niego po użyciu.",u"The password goes into the answer file on the media; Setup removes it after use."},
    {"edition",false,Kind::choice,u"Wydanie Windows:",u"Windows edition:","auto",CHARON_CHOICES(windowsEditions),u"Klucze na liście są ogólne, tylko do wyboru wydania; nie aktywują systemu.",u"The keys in the list are generic, for choosing the edition only; they do not activate Windows."},
    {"bypass",false,Kind::yesNo,u"Pominąć sprawdzanie TPM, Secure Boot i pamięci?",u"Skip the TPM, Secure Boot and memory checks?","no",nullptr,0,nullptr,nullptr},
    {"localonly",false,Kind::yesNo,u"Konto lokalne, bez konta Microsoft i bez wymagania sieci?",u"A local account, with no Microsoft account and no network requirement?","yes",nullptr,0,nullptr,nullptr},
    {"privacy",false,Kind::yesNo,u"Wyłączyć zbędną telemetrię i reklamy w ustawieniach prywatności?",u"Turn off optional telemetry and ads in the privacy settings?","yes",nullptr,0,nullptr,nullptr},
    {"wreaders",true,Kind::multi,u"Czytniki ekranu:",u"Screen readers:","nvda,narrator",CHARON_CHOICES(windowsReaders),
     u"Można zaznaczyć kilka; jedno pytanie ustali, który startuje. W instalatorze mówi NVDA, jeśli jest zaznaczone, a inaczej Narrator.",
     u"Several may be checked; one question settles which one starts. In Setup NVDA speaks if it is checked, otherwise Narrator."},
    // r401: Windows 11 encrypts the disk by itself on many computers; an encrypted
    // disk cannot be shared with a second system and locks the user out when the
    // boot order changes. Prevented unless the user wants it.
    {"nobitlocker",false,Kind::yesNo,u"Zapobiec automatycznemu szyfrowaniu dysku BitLockerem? Zalecane przy dwóch systemach.",u"Prevent automatic BitLocker disk encryption? Recommended with two systems.","yes",nullptr,0,nullptr,nullptr},
    // r402: dual boot with Linux - both systems must read the hardware clock the same way.
    {"clockutc",false,Kind::yesNo,u"Czy obok Windows będzie Linux? Tak: Windows trzyma zegar sprzętowy w czasie uniwersalnym, jak Linux.",u"Will Linux live beside Windows? Yes: Windows keeps the hardware clock in universal time, as Linux does.","no",nullptr,0,nullptr,nullptr},
    // r402: what happens to the installed Windows at the first logon (Charon-WindowsProfile.ps1).
    {"profile",true,Kind::choice,u"Profil Windows:",u"Windows profile:","none",CHARON_CHOICES(windowsProfiles),
     u"Profil działa po instalacji, przy pierwszym logowaniu, i potrzebuje sieci. Odchudzony usuwa aplikacje fabryczne, tak jak oficjalny skrypt tiny11builder. ReviOS i AtlasOS to cudze zestawy zmian, po angielsku; AtlasOS działa tylko na kompilacjach 26100 i 26200. Narrator i dźwięk zostają.",
     u"The profile runs after installation, at the first logon, and needs the network. Slim removes the factory apps, as the official tiny11builder script does. ReviOS and AtlasOS are third-party sets of changes, in English; AtlasOS works only on builds 26100 and 26200. Narrator and sound stay."},
};
inline void XmlText(Text& x,const char* value) {
    for(;*value;++value) {
        const char c=*value;
        if(c=='&')x.Add("&amp;");else if(c=='<')x.Add("&lt;");else if(c=='>')x.Add("&gt;");else if(c=='"')x.Add("&quot;");
        else {char one[2]={c,0};x.Add(one);}
    }
}
inline void Component(Text& x,const char* name) {
    x.Add("    <component name=\"");x.Add(name);
    x.Line("\" processorArchitecture=\"amd64\" publicKeyToken=\"31bf3856ad364e35\" language=\"neutral\" versionScope=\"nonSxS\">");
}
inline void RunCommand(Text& x,unsigned order,const char* command,bool firstLogon=false) {
    x.Add(firstLogon?"        <SynchronousCommand wcm:action=\"add\"><Order>":"        <RunSynchronousCommand wcm:action=\"add\"><Order>");x.Number(order);
    x.Add(firstLogon?"</Order><CommandLine>":"</Order><Path>");XmlText(x,command);
    x.Line(firstLogon?"</CommandLine></SynchronousCommand>":"</Path></RunSynchronousCommand>");
}
// Autounattend.xml. partitionId: the Windows partition number to install to.
inline void WindowsAnswers(Text& x,unsigned partitionId,bool hasEsp) {
    const char* locale=ask::Get("wlocale");const bool speech=ask::Is("speech","yes");
    x.Line("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
    x.Line("<unattend xmlns=\"urn:schemas-microsoft-com:unattend\" xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\">");
    x.Line("  <settings pass=\"windowsPE\">");
    Component(x,"Microsoft-Windows-International-Core-WinPE");
    x.Line("      <SetupUILanguage><UILanguage>",locale,"</UILanguage></SetupUILanguage>");
    x.Line("      <InputLocale>",locale,"</InputLocale><SystemLocale>",locale);x.size-=1;   // keep on one line
    x.Line("</SystemLocale><UILanguage>",locale,"</UILanguage><UserLocale>",locale);x.size-=1;
    x.Line("</UserLocale>");
    x.Line("    </component>");
    Component(x,"Microsoft-Windows-Setup");
    x.Line("      <RunSynchronous>");
    unsigned order=1;
    if(speech && ask::Is("nvdaready","yes")) {
        // A portable NVDA on the media (Setup's RAM drive is too small for it), then
        // started; Narrator only when no NVDA could be made.
        RunCommand(x,order++,"cmd.exe /c for %d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do @if exist %d:\\charon\\readers\\nvda_2026.3.exe if not exist %d:\\charon\\nvda-portable\\nvda.exe (mkdir %d:\\charon\\tmp & set TEMP=%d:\\charon\\tmp& set TMP=%d:\\charon\\tmp& start \"\" /wait %d:\\charon\\readers\\nvda_2026.3.exe --create-portable-silent --portable-path=%d:\\charon\\nvda-portable)");
        RunCommand(x,order++,"cmd.exe /c (for %d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do @if exist %d:\\charon\\nvda-portable\\nvda.exe (start \"\" %d:\\charon\\nvda-portable\\nvda.exe & exit 0)) & start X:\\Windows\\System32\\Narrator.exe");
    } else if(speech)RunCommand(x,order++,"cmd.exe /c start X:\\Windows\\System32\\Narrator.exe");
    if(ask::Is("bypass","yes")) {
        RunCommand(x,order++,"reg.exe add \"HKLM\\SYSTEM\\Setup\\LabConfig\" /v BypassTPMCheck /t REG_DWORD /d 1 /f");
        RunCommand(x,order++,"reg.exe add \"HKLM\\SYSTEM\\Setup\\LabConfig\" /v BypassSecureBootCheck /t REG_DWORD /d 1 /f");
        RunCommand(x,order++,"reg.exe add \"HKLM\\SYSTEM\\Setup\\LabConfig\" /v BypassRAMCheck /t REG_DWORD /d 1 /f");
    }
    if(order==1)RunCommand(x,order++,"cmd.exe /c rem");
    x.Line("      </RunSynchronous>");
    x.Line("      <DiskConfiguration><WillShowUI>OnError</WillShowUI>");
    if(ask::Is("diskmode","whole")) {
        x.Line("        <Disk wcm:action=\"add\"><DiskID>0</DiskID><WillWipeDisk>true</WillWipeDisk>");
        x.Line("          <CreatePartitions>");
        x.Line("            <CreatePartition wcm:action=\"add\"><Order>1</Order><Type>EFI</Type><Size>300</Size></CreatePartition>");
        x.Line("            <CreatePartition wcm:action=\"add\"><Order>2</Order><Type>MSR</Type><Size>16</Size></CreatePartition>");
        x.Line("            <CreatePartition wcm:action=\"add\"><Order>3</Order><Type>Primary</Type><Extend>true</Extend></CreatePartition>");
        x.Line("          </CreatePartitions>");
        x.Line("          <ModifyPartitions>");
        x.Line("            <ModifyPartition wcm:action=\"add\"><Order>1</Order><PartitionID>1</PartitionID><Format>FAT32</Format><Label>System</Label></ModifyPartition>");
        x.Line("            <ModifyPartition wcm:action=\"add\"><Order>2</Order><PartitionID>3</PartitionID><Format>NTFS</Format><Label>Windows</Label><Letter>C</Letter></ModifyPartition>");
        x.Line("          </ModifyPartitions>");
        x.Line("        </Disk>");
    } else if(ask::Is("diskmode","free")) {
        x.Line("        <Disk wcm:action=\"add\"><DiskID>0</DiskID><WillWipeDisk>false</WillWipeDisk>");
        x.Line("          <CreatePartitions>");
        if(!hasEsp)x.Line("            <CreatePartition wcm:action=\"add\"><Order>1</Order><Type>EFI</Type><Size>300</Size></CreatePartition>");
        x.Add("            <CreatePartition wcm:action=\"add\"><Order>");x.Number(hasEsp?1:2);x.Line("</Order><Type>Primary</Type><Extend>true</Extend></CreatePartition>");
        x.Line("          </CreatePartitions>");
        x.Line("          <ModifyPartitions>");
        x.Add("            <ModifyPartition wcm:action=\"add\"><Order>1</Order><PartitionID>");x.Number(partitionId);
        x.Line("</PartitionID><Format>NTFS</Format><Label>Windows</Label></ModifyPartition>");
        x.Line("          </ModifyPartitions>");
        x.Line("        </Disk>");
    } else {
        x.Line("        <Disk wcm:action=\"add\"><DiskID>0</DiskID><WillWipeDisk>false</WillWipeDisk>");
        x.Line("          <ModifyPartitions>");
        x.Add("            <ModifyPartition wcm:action=\"add\"><Order>1</Order><PartitionID>");x.Number(partitionId);
        x.Line("</PartitionID><Format>NTFS</Format><Label>Windows</Label></ModifyPartition>");
        x.Line("          </ModifyPartitions>");
        x.Line("        </Disk>");
    }
    x.Line("      </DiskConfiguration>");
    x.Add("      <ImageInstall><OSImage>");
    // r402: an image with several systems (Windows Server) needs the index named.
    if(const char* index=ask::Get("windex");index[0]) {x.Add("<InstallFrom><MetaData wcm:action=\"add\"><Key>/IMAGE/INDEX</Key><Value>");x.Add(index);x.Add("</Value></MetaData></InstallFrom>");}
    x.Add("<InstallTo><DiskID>0</DiskID><PartitionID>");x.Number(partitionId);
    x.Line("</PartitionID></InstallTo><WillShowUI>OnError</WillShowUI></OSImage></ImageInstall>");
    x.Add("      <UserData><AcceptEula>true</AcceptEula>");
    if(!ask::Is("edition","auto")) {x.Add("<ProductKey><Key>");x.Add(ask::Get("edition"));x.Add("</Key><WillShowUI>OnError</WillShowUI></ProductKey>");}
    x.Line("</UserData>");
    x.Line("    </component>");
    x.Line("  </settings>");
    x.Line("  <settings pass=\"specialize\">");
    Component(x,"Microsoft-Windows-Shell-Setup");
    x.Add("      <ComputerName>");XmlText(x,ask::Get("hostname"));x.Line("</ComputerName>");
    x.Line("    </component>");
    Component(x,"Microsoft-Windows-Deployment");
    x.Line("      <RunSynchronous>");
    order=1;
    if(ask::Is("localonly","yes"))RunCommand(x,order++,"reg.exe add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OOBE\" /v BypassNRO /t REG_DWORD /d 1 /f");
    if(ask::Is("wfirst","narrator"))RunCommand(x,order++,"reg.exe add \"HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Accessibility\" /v Configuration /t REG_SZ /d narrator /f");
    // r401: no automatic device encryption (the same switch Rufus sets).
    if(!ask::Is("nobitlocker","no"))RunCommand(x,order++,"reg.exe add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\BitLocker\" /v PreventDeviceEncryption /t REG_DWORD /d 1 /f");
    if(ask::Is("clockutc","yes"))RunCommand(x,order++,"reg.exe add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\TimeZoneInformation\" /v RealTimeIsUniversal /t REG_DWORD /d 1 /f");
    if(ask::Is("privacy","yes")) {
        RunCommand(x,order++,"reg.exe add \"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection\" /v AllowTelemetry /t REG_DWORD /d 1 /f");
        RunCommand(x,order++,"reg.exe add \"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\CloudContent\" /v DisableWindowsConsumerFeatures /t REG_DWORD /d 1 /f");
    }
    if(order==1)RunCommand(x,order++,"cmd.exe /c rem");
    x.Line("      </RunSynchronous>");
    x.Line("    </component>");
    x.Line("  </settings>");
    x.Line("  <settings pass=\"oobeSystem\">");
    Component(x,"Microsoft-Windows-International-Core");
    x.Line("      <InputLocale>",locale,"</InputLocale><SystemLocale>",locale);x.size-=1;
    x.Line("</SystemLocale><UILanguage>",locale,"</UILanguage><UserLocale>",locale);x.size-=1;
    x.Line("</UserLocale>");
    x.Line("    </component>");
    Component(x,"Microsoft-Windows-Shell-Setup");
    x.Line("      <OOBE><HideEULAPage>true</HideEULAPage><HideOnlineAccountScreens>",ask::Is("localonly","yes")?"true":"false","</HideOnlineAccountScreens>");
    x.Line("        <HideWirelessSetupInOOBE>",ask::Is("localonly","yes")?"true":"false","</HideWirelessSetupInOOBE><ProtectYourPC>",ask::Is("privacy","yes")?"3":"1");x.size-=1;
    x.Line("</ProtectYourPC></OOBE>");
    x.Add("      <UserAccounts><LocalAccounts><LocalAccount wcm:action=\"add\"><Name>");XmlText(x,ask::Get("user"));
    x.Add("</Name><Group>Administrators</Group><Password><Value>");XmlText(x,ask::Get("password"));
    x.Line("</Value><PlainText>true</PlainText></Password></LocalAccount></LocalAccounts></UserAccounts>");
    x.Line("      <FirstLogonCommands>");
    order=1;
    if(ask::Is("wfirst","narrator")) {
        RunCommand(x,order++,"reg.exe add \"HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Accessibility\" /v Configuration /t REG_SZ /d narrator /f",true);
        RunCommand(x,order++,"cmd.exe /c start Narrator.exe",true);
    }
    // The reader add-on for the boot manager: its installer lies in \charon on the media.
    // The installers stay in Windows too (ProgramData), in case Charon must be set up again.
    RunCommand(x,order++,"cmd.exe /c for %d in (D E F G H I J K L M N) do if exist %d:\\charon\\Install-Charon.ps1 xcopy /E /I /Y /Q %d:\\charon %ProgramData%\\Charon",true);
    RunCommand(x,order++,"cmd.exe /c for %d in (D E F G H I J K L M N) do if exist %d:\\charon\\Install-Charon.ps1 powershell.exe -NoProfile -ExecutionPolicy Bypass -File %d:\\charon\\Install-Charon.ps1 -Mode Manager -Yes -Source %d:\\charon\\EFI\\HAK",true);
    // r403: Windows installed beside CharonDOS: CharonDOS stays the default entry (its WINNT starts Windows).
    if(ask::Is("dosdefault","yes"))RunCommand(x,order++,"cmd.exe /c for %d in (D E F G H I J K L M N) do if exist %d:\\charon\\Install-Charon.ps1 powershell.exe -NoProfile -ExecutionPolicy Bypass -File %d:\\charon\\Install-Charon.ps1 -Mode Reader -Yes -Source %d:\\charon\\EFI\\HAK -FirstEntry CharonDOS",true);
    // r403: the chosen screen readers (before the profile, which keeps what it finds).
    if(speech && !ask::Is("wreaders","narrator")) {
        static char command[400];Text c{command,sizeof(command),0,false};command[0]=0;
        c.Add("powershell.exe -NoProfile -ExecutionPolicy Bypass -File %ProgramData%\\Charon\\Charon-Readers.ps1 -Readers ");c.Add(ask::Get("wreaders"));
        c.Add(" -First ");c.Add(ask::Get("wfirst"));c.Add(" -Language ");c.Add(locale);c.Add(" -Source %ProgramData%\\Charon -Yes");
        RunCommand(x,order++,command,true);
    }
    // r402: the chosen profile, after Charon is in place (the script keeps Narrator and sound on).
    if(!ask::Is("profile","none") && ask::Get("profile")[0]) {
        static char command[400];Text c{command,sizeof(command),0,false};command[0]=0;
        c.Add("powershell.exe -NoProfile -ExecutionPolicy Bypass -File %ProgramData%\\Charon\\Charon-WindowsProfile.ps1 -Profile ");c.Add(ask::Get("profile"));
        c.Add(Pl()?" -Language pl -Yes":" -Language en -Yes");
        RunCommand(x,order++,command,true);
    }
    x.Line("      </FirstLogonCommands>");
    x.Line("    </component>");
    x.Line("  </settings>");
    x.Line("</unattend>");
}
// The system image Windows Setup installs from: \sources\install.wim, .esd or
// the first part of a split image. It must be a real image (WIM signature and
// a size no empty stand-in has), not just a name.
inline bool HasWindowsImage(efi_handle volume) {
    static const char16_t* const images[]={u"\\sources\\install.wim",u"\\sources\\install.esd",u"\\sources\\install.swm"};
    for(const auto* name:images) {
        efi_file_protocol* file=nullptr;FileRef ref;ref.volume=volume;Set(ref.path,pathUnits,name);
        if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS)continue;
        const auto* info=InfoOf(file);const uint64_t bytes=info?info->FileSize:0;
        uint8_t head[8]={};size_t got=sizeof(head);const bool read=file->Read(file,&got,head)==EFI_SUCCESS && got==sizeof(head);
        file->Close(file);
        static const uint8_t magic[8]={'M','S','W','I','M',0,0,0};bool wim=read;
        for(size_t k=0;k<8 && wim;++k)if(head[k]!=magic[k])wim=false;
        if(wim && bytes>=(uint64_t(256)<<20))return true;
    }
    return false;
}
// Windows install media: a volume with \sources\boot.wim, a boot manager AND the
// system image. A volume with only the boot manager (a recovery or test stick,
// a leftover EFI folder) is not install media: Charon writes nothing to it and
// does not start it. Such a volume is reported in bootOnly.
inline efi_handle& WindowsBootOnly() {static efi_handle volume=nullptr;return volume;}
inline bool FindWindowsMedia(efi_handle& media,bool& writable,const char16_t*& loader) {
    efi_handle volumes[32];const size_t n=Volumes(volumes,32);media=nullptr;WindowsBootOnly()=nullptr;
    // r401: the Windows boot manager under its own name first: on Charon's own
    // stick the removable-media loader (bootx64.efi) is Charon itself and must
    // never be taken for Windows.
    static const char16_t* const loaders[]={u"\\efi\\microsoft\\boot\\bootmgfw.efi",u"\\efi\\boot\\bootx64.efi"};
    for(size_t i=0;i<n && !media;++i) {
        efi_file_protocol* file=nullptr;FileRef ref;ref.volume=volumes[i];Set(ref.path,pathUnits,u"\\sources\\boot.wim");
        if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS)continue;
        file->Close(file);
        if(!HasWindowsImage(volumes[i])) {if(!WindowsBootOnly())WindowsBootOnly()=volumes[i];continue;}
        for(const auto* l:loaders) {
            if(l==loaders[1] && volumes[i]==U().host->selfDevice)continue;
            Set(ref.path,pathUnits,l);if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)==EFI_SUCCESS) {file->Close(file);media=volumes[i];loader=l;break;}}
    }
    if(media)writable=!VolumeReadOnly(media);
    return media!=nullptr;
}
inline void WindowsMediaMissing() {
    if(WindowsBootOnly()) {
        static char16_t label[64];static char16_t text[600];VolumeLabel(WindowsBootOnly(),label,64);
        Set(text,600,T(u"Nośnik ",u"The media "));Cat(text,600,label);
        Cat(text,600,T(u" ma tylko menedżer rozruchu Windows, bez obrazu systemu: brak pliku sources install wim albo install esd. To nie jest nośnik instalacyjny, więc Charon nic na nim nie zapisał i go nie uruchomi. Podłącz pendrive z pełnym instalatorem Windows 11.",
            u" holds only the Windows boot manager, with no system image: the file sources install wim or install esd is missing. It is not install media, so Charon wrote nothing to it and will not start it. Plug in a USB stick with the complete Windows 11 installer."));
        Fail(text);return;
    }
    Fail(T(u"Nie znaleziono nośnika instalacyjnego Windows. Podłącz pendrive z instalatorem Windows 11 albo skopiuj zawartość obrazu ISO na wolumin FAT32 i wybierz instalację ponownie.",
        u"No Windows install media was found. Plug in a USB stick with the Windows 11 installer, or copy the contents of the ISO image to a FAT32 volume, and choose the installation again."));
}
inline bool RunWindows(bool guided) {
    efi_handle media=nullptr;bool writable=false;const char16_t* loader=nullptr;
    if(!FindWindowsMedia(media,writable,loader)) {WindowsMediaMissing();return false;}
    static char16_t label[64];VolumeLabel(media,label,64);
#ifdef HAK_CHARON_HTTPS
    if(winusb::EspOnGpt(media)) {   // r402
        Say(T(u"Uwaga: ten nośnik ma partycję oznaczoną jako systemowa EFI. Instalator Windows może nie znaleźć na niej swoich plików i poprosić o sterownik nośnika.",
              u"Note: this media's partition is marked as an EFI system partition. Windows Setup may not find its files on it and ask for a media driver."));HearOut(200);}
#endif
    if(!guided) {
        if(!writable) {Fail(T(u"Nośnik instalacyjny jest tylko do odczytu, więc nie da się na nim zapisać pliku odpowiedzi. Użyj pendrive'a.",u"The install media is read only, so the answer file cannot be written to it. Use a USB stick."));return false;}
        auto& s=disks::D();const auto& t=Chosen();const auto& d=s.list[t.disk];
        // Windows Setup numbers disks itself; the answer file can only say "disk 0".
        size_t internal=0;for(size_t i=0;i<s.count;++i)if(!s.list[i].usb && !s.list[i].removable)++internal;
        if(internal!=1 || d.usb || d.removable) {
            Fail(T(u"Tryb automatyczny dla Windows działa tylko wtedy, gdy komputer ma dokładnie jeden dysk wewnętrzny i to on jest celem: instalator Windows sam numeruje dyski. Użyj trybu z przewodnikiem.",
                u"The automatic mode for Windows works only when the computer has exactly one internal disk and it is the target: Windows Setup numbers the disks itself. Use the guided mode."));return false;}
        bool hasEsp=false;for(size_t i=0;i<d.partCount;++i)if(d.parts[i].kind==1)hasEsp=true;
        unsigned partitionId=3;
        if(ask::Is("diskmode","partition")) {
            if(!hasEsp) {Fail(T(u"Na dysku nie ma partycji systemowej EFI; wybierz wymazanie całego dysku.",u"The disk has no EFI system partition; choose erasing the whole disk."));return false;}
            partitionId=unsigned(t.part+1);   // Windows counts partitions by position
        } else if(ask::Is("diskmode","free")) {
            // The new partition lands in the gap; its number is its position.
            unsigned before=0;for(size_t i=0;i<d.partCount;++i)if(d.parts[i].first<d.freeFirst)++before;
            partitionId=before+(hasEsp?1:2);
        }
        // r403: NVDA for Setup itself, on the media; without it Setup speaks with Narrator.
        ask::Put("nvdaready","no");
        if(ask::Has("wreaders","nvda") && !NvdaOntoMedia(media))
            Say(T(u"Uwaga: NVDA nie trafiło na nośnik; w instalatorze mówi Narrator, a NVDA zainstaluje się przy pierwszym logowaniu.",
                  u"Note: NVDA did not reach the media; Narrator speaks in Setup, and NVDA is installed at the first logon."));
        static char xml[16000];Text text{xml,sizeof(xml),0,false};xml[0]=0;WindowsAnswers(text,partitionId,hasEsp);
        const bool ok=!text.overflow && WriteText(media,u"\\Autounattend.xml",xml,text.size);
        for(auto& c:xml)c=0;
        if(!ok) {Fail(T(u"Nie zapisano pliku odpowiedzi na nośniku.",u"The answer file could not be written to the media."));return false;}
        // The reader add-on and its Windows installer, if Charon's disk has them.
        // The whole of Charon goes to the install media (the internal disk may
        // be erased); Windows puts it back on the EFI partition at first logon.
        MakeFolders(media,u"\\charon\\EFI");
        Say(T(u"Kopiuję Charona na nośnik instalacyjny.",u"Copying Charon to the install media."));
        static FileRef from,to;from.volume=U().host->selfDevice;Set(from.path,pathUnits,u"\\EFI\\HAK");
        to.volume=media;Set(to.path,pathUnits,u"\\charon\\EFI\\HAK");size_t copied=0;
        bool reader=CopyTree(from,to,0,&copied)==EFI_SUCCESS;
        static const char16_t* const scripts[]={u"Install-Charon.ps1",u"Install-CharonAddon.ps1",u"CharonEfiSign.ps1"};
        for(const auto* name:scripts) {
            Set(from.path,pathUnits,u"\\EFI\\HAK\\addon\\");Cat(from.path,pathUnits,name);Set(to.path,pathUnits,u"\\charon\\");Cat(to.path,pathUnits,name);
            if(CopyFile(from,to)!=EFI_SUCCESS)reader=false;
        }
        if(!reader)Say(T(u"Uwaga: na dysku Charona brak plików dodatku czytnika; Windows zainstaluje się bez niego.",u"Note: Charon's disk lacks the reader add-on files; Windows installs without it."));
        // r402: the profile and dual-boot scripts (optional).
        static const char16_t* const extras[]={u"Charon-WindowsProfile.ps1",u"Charon-MakeRoom.ps1",u"Charon-Readers.ps1"};bool profileScript=true;
        for(const auto* name:extras) {
            Set(from.path,pathUnits,u"\\EFI\\HAK\\addon\\");Cat(from.path,pathUnits,name);Set(to.path,pathUnits,u"\\charon\\");Cat(to.path,pathUnits,name);
            if(CopyFile(from,to)!=EFI_SUCCESS && name==extras[0])profileScript=false;
        }
        if(!profileScript && !ask::Is("profile","none"))Say(T(u"Uwaga: na dysku Charona brak skryptu profili; Windows zainstaluje się bez zmian.",u"Note: Charon's disk lacks the profile script; Windows installs unchanged."));
    }
    static char16_t text[500];Set(text,500,T(u"Uruchamiam instalator Windows z nośnika ",u"Starting Windows Setup from the media "));Cat(text,500,label);
    Cat(text,500,guided?T(u". Narratora włącza Control, Windows, Enter.",u". Control, Windows, Enter turns Narrator on.")
        :T(u". Instalacja przebiega sama i kilka razy uruchamia komputer ponownie; plik odpowiedzi zawiera hasło, więc po instalacji usuń go z nośnika.",
           u". The installation runs by itself and restarts the computer several times; the answer file holds the password, so delete it from the media afterwards."));
    Say(text);
    HearOut(150);
    StartProgram(media,loader,u"Windows Setup",nullptr);
    return true;
}
// ---- the dialogue -------------------------------------------------------------------
inline void HashPassword() {
    // Salt from the clock and the time-stamp counter.
    static const char alphabet[]="./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
    uint64_t seed=(uint64_t(hi)<<32)|lo;efi_time now{};
    auto* runtime=U().host->system->RuntimeServices;
    if(runtime && runtime->GetTime && runtime->GetTime(&now,nullptr)==EFI_SUCCESS)seed^=(uint64_t(now.Nanosecond)<<20)^(uint64_t(now.Second)<<8)^now.Minute^(uint64_t(now.Day)<<40);
    char salt[17];for(int i=0;i<16;++i) {seed=seed*6364136223846793005ull+1442695040888963407ull;salt[i]=alphabet[(seed>>58)&63];}
    salt[16]=0;static char hash[96];
    if(crypt::Sha256Crypt(ask::Get("password"),salt,hash,sizeof(hash)))ask::Put("hash",hash);
}
inline void Dialogue();   // CharonWsInstall3.h: groups of systems
// r401: the dialogue stands on the screen (everything said is written there)
// and the reader's review keys read it.
inline void Run() {
    ScriptBegin(T(u" Charon: instalacja systemu z sieci",u" Charon: installing a system from the network"));
    ReviewMode(true);
    Dialogue();
    ScriptEnd();
}
// A system installed by its own installer: 0 Debian, 1 Ubuntu, 2 Windows.
inline void Classic(size_t system) {
    size_t mode=0;
    if(!ask::Choose(T(u"Jak instalować?",u"How to install?"),CHARON_CHOICES(modes),mode))return;
    const bool guided=mode==2,quick=mode==0;const bool windows=system==2;
    if(!windows && !EnsureNetwork()) {HearOut(200);return;}
    bool languageAsked=false;
    if(windows) {   // no point in nine questions when there is nothing to install from
        efi_handle media=nullptr;bool writable=false;const char16_t* loader=nullptr;
        if(!FindWindowsMedia(media,writable,loader)) {
#ifdef HAK_CHARON_HTTPS
            // r401: no media. Charon gets Windows 11 from Microsoft itself, either onto
            // a USB stick (any computer) or into a RAM disk (computers with much
            // memory). Charon looks at the computer and puts the fitting way first;
            // the user still chooses.
            WindowsMediaMissing();HearOut(300);
            static winusb::Target sticks[16];
            const bool usbFits=winusb::Targets(sticks,16,uint64_t(9)<<30)>0;
            const bool ramFits=ramdisk::Find() && ramdisk::Room(uint64_t(9)<<30,uint64_t(3)<<30,false);
            static const Choice toUsb={u"pobierz na nośnik USB",u"download to a USB stick","usb"};
            static const Choice toRam={u"pobierz do RAM-dysku",u"download to a RAM disk","ram"};
            static Choice ways[2];const bool ramFirst=ramFits;ways[0]=ramFirst?toRam:toUsb;ways[1]=ramFirst?toUsb:toRam;
            Say(ramFits?T(u"Ten komputer ma dość pamięci na obraz Windows, więc pierwszy na liście jest RAM-dysk: nic nie trzeba podłączać, ale instalator prowadzisz sam z Narratorem. Druga możliwość to pendrive.",
                    u"This computer has enough memory for the Windows image, so the RAM disk is first in the list: nothing needs plugging in, but you drive Setup yourself with Narrator. The other way is a USB stick.")
                :usbFits?T(u"Ten komputer ma za mało pamięci na RAM-dysk, więc pierwszy na liście jest pendrive: jest podłączony i ma dość miejsca.",
                    u"This computer has too little memory for a RAM disk, so the USB stick is first in the list: one is plugged in and has enough room.")
                :T(u"Ten komputer ma za mało pamięci na RAM-dysk i nie widzę pendrive'a z 9 gigabajtami wolnego miejsca na woluminie FAT. Podłącz taki pendrive i wybierz pobieranie na nośnik USB.",
                    u"This computer has too little memory for a RAM disk and I see no USB stick with 9 gigabytes free on a FAT volume. Plug one in and choose downloading to a USB stick."));
            HearOut(200);
            size_t way=0;
            if(!ask::Choose(T(u"Skąd wziąć instalator Windows 11?",u"Where to get the Windows 11 installer?"),ways,2,way))return;
            if(!EnsureNetwork()) {HearOut(200);return;}
            // As in Rufus: the version here, the language from Microsoft's own list
            // (Charon's language first); x64, the image with Home, Pro and Education.
            static Choice versions[sizeof(windowsVersions)/sizeof(windowsVersions[0])];const size_t versionCount=sizeof(windowsVersions)/sizeof(windowsVersions[0]);
            for(size_t i=0;i<versionCount;++i)versions[i]={windowsVersions[i].pl,windowsVersions[i].en,windowsVersions[i].id};
            size_t version=0;
            if(!ask::Choose(T(u"Która wersja Windows?",u"Which Windows version?"),versions,versionCount,version))return;
            const WindowsVersion& chosen=windowsVersions[version];
            auto& pick=ramdisk::P();pick.windows10=version==1;pick.language[0]=0;pick.locale[0]=0;
            winusb::Preset()=chosen.url;winusb::Product()=Pl()?chosen.pl:chosen.en;
            ask::Put("wversion",chosen.id);
            if(chosen.index) {char digits[2]={char('0'+chosen.index),0};ask::Put("windex",digits);}
            if(chosen.url) {
                Say(T(u"To oficjalny obraz próbny Microsoftu: tylko po angielsku, działa 90 dni, Server 180, i nie da się go zamienić na pełną licencję.",
                      u"This is Microsoft's official evaluation image: English only, it runs 90 days, Server 180, and cannot be turned into a full licence."));HearOut(200);
            }
            const char* mine=Pl()?"pl-PL":"en-US";   // the default in Microsoft's list: Charon's own language
            if(ways[way].value[0]=='r') {
                if(chosen.url) {static char16_t wide[400];size_t k=0;for(;chosen.url[k] && k<399;++k)wide[k]=char16_t(uint8_t(chosen.url[k]));wide[k]=0;
                    Say(T(u"Windows z RAM-dysku: instalator prowadzisz sam. Narratora włącza Control, Windows, Enter.",u"Windows from a RAM disk: you drive Setup yourself. Control, Windows, Enter turns Narrator on."));HearOut(150);
                    ramdisk::BootUrl(wide,u"Windows Setup",3072);HearOut(300);return;}
                if(!ramdisk::WindowsFromMicrosoft(mine))HearOut(300);
                return;
            }
            if(!winusb::ChooseAndMake(mine)) {HearOut(300);return;}
            if(!FindWindowsMedia(media,writable,loader)) {WindowsMediaMissing();HearOut(300);return;}
            if(pick.locale[0]) {ask::Put("wlocale",pick.locale);languageAsked=true;}   // (an image from the address file: the language is asked below)
            if(chosen.url) {ask::Put("wlocale","en-US");ask::Put("edition","auto");languageAsked=true;}   // an evaluation image: one language, one edition
            // The media exists now: on to the questions, the answer file and Setup.
#else
            WindowsMediaMissing();HearOut(300);return;
#endif
        }
    }
    if(guided) {
        if(system==1) {Fail(T(u"Instalator sieciowy Ubuntu nie ma mowy, więc tryb z przewodnikiem nie byłby dostępny. Wybierz tryb automatyczny albo Debiana, którego instalator mówi.",
            u"The Ubuntu network installer has no speech, so the guided mode would not be accessible. Choose an automatic mode, or Debian, whose installer speaks."));return;}
        if(system==0)RunDebian(true);else RunWindows(true);
        return;
    }
    if(windows) {const size_t skip=languageAsked?1:0;if(!ask::Run(windowsQuestions+skip,sizeof(windowsQuestions)/sizeof(windowsQuestions[0])-skip,quick))return;
        if(!ResolveWindowsReaders())return;}   // the language was asked before the download
    else {
        if(!ask::Run(commonQuestions,sizeof(commonQuestions)/sizeof(commonQuestions[0]),quick))return;
        if(system==0?!ask::Run(debianQuestions,sizeof(debianQuestions)/sizeof(debianQuestions[0]),quick)
                    :!ask::Run(ubuntuQuestions,sizeof(ubuntuQuestions)/sizeof(ubuntuQuestions[0]),quick))return;
        if(!ResolveReaders(ask::Any("desktop")))return;
        DeriveDefaults();HashPassword();
        if(!ask::Get("hash")[0]) {Fail(T(u"Nie udało się przygotować hasła.",u"The password could not be prepared."));return;}
    }
    size_t diskMode=0;
    for(;;) {   // a refused choice is explained and the question comes again
    if(!ask::Choose(T(u"Co zrobić z dyskiem?",u"What to do with the disk?"),CHARON_CHOICES(diskModes),diskMode))return;
    ask::Put("diskmode",diskModes[diskMode].value);
    if(diskMode==1 && !windows) {
        Fail(T(u"Instalatory Debiana i Ubuntu nie umieją same zastąpić jednej wskazanej partycji; Charon nie będzie tego zgadywał na twoim dysku. Wybierz wolne miejsce albo cały dysk, albo Debiana w trybie z przewodnikiem, gdzie partycję wskazujesz w mówiącym instalatorze.",
            u"The Debian and Ubuntu installers cannot replace one named partition by themselves; Charon will not guess on your disk. Choose free space or the whole disk, or Debian in the guided mode, where you pick the partition in the speaking installer."));
        HearOut(300);continue;
    }
    if(!ChooseTarget()) {HearOut(200);continue;}
    const char16_t* planName=Pl()?systems[system].pl:systems[system].en;
#ifdef HAK_CHARON_HTTPS
    if(windows && ask::Get("wversion")[0])planName=winusb::Product();
#endif
    if(!ConfirmPlan(planName)) {HearOut(100);continue;}
    break;
    }
    // The screen reader add-on in the firmware, so the next boot manager speaks.
    if(U().host->ensureReader && !U().host->ensureReader(U().host->context))
        Say(T(u"Uwaga: nie udało się włączyć dodatku czytnika w firmware.",u"Note: the reader add-on could not be turned on in the firmware."));
    const bool started=system==0?RunDebian(false):system==1?RunUbuntu():RunWindows(false);
    // The password does not stay in memory.
    ask::Clear();
    if(!started) {HearOut(200);Say(T(u"Instalacja nie została uruchomiona. Na dysku docelowym nic się nie zmieniło.",u"The installation was not started. Nothing changed on the target disk."));HearOut(100);}
}
}  // namespace install
}  // namespace ws
}  // namespace hakefi
#include "CharonWsInstall3.h"
