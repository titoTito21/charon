#pragma once
// r402: Charon workspace - the install dialogue in groups.
//
// The user first picks a group (Windows, Linux installed automatically,
// Linux made for blind people, ready system images, tools), then a system.
// Every entry says how it stands with speech: "speaks in the installer",
// "speaks after installation" or "no speech"; a system without speech needs
// one more spoken confirmation. The ways a system gets onto the computer:
//  - classic: the system's own installer with an answer file (CharonWsInstall*.h);
//  - netboot: a kernel and an initial RAM disk started from memory (Arch);
//  - ISO: a bootable image onto a USB stick or into a RAM disk;
//  - image: a ready disk image written onto a disk, then configured by a
//    cloud-init seed partition Charon adds (user, password, speech, Charon).
// Included at the end of CharonWsInstall2.h. GPL-2.0-or-later.
#ifdef HAK_CHARON_HTTPS
#include "CharonWsImages.h"
#include "CharonWsSeed.h"
#include "CharonWsDosApps.h"
#include "CharonWsDosSystem.h"
#endif
namespace hakefi {
namespace ws {
namespace install {
enum class Speech : uint8_t {installer,afterInstall,none,charon};   // charon: no speech of its own, Charon's reader reads its text screen
enum class Way : uint8_t {classic,archNet,iso,customIso,image,qcow,rawNoSeed,netbootXyz,dos,dosWindows,dosApps,dosSystem,dosDual};
struct Entry {
    const char16_t* pl;const char16_t* en;Speech speech;Way way;
    const char* url;uint64_t bytes;       // approximate size (room checks before the server answers)
    size_t classic;                       // Way::classic: 0 Debian, 1 Ubuntu, 2 Windows
    const char16_t* notePl;const char16_t* noteEn;   // said after the entry is chosen (may be null)
    bool wholeOnly=false;                 // an image whose boot loader names its partition by number: it cannot live beside other systems
};
inline const char16_t* SpeechText(Speech s) {
    return s==Speech::installer?T(u"mówi w instalatorze",u"speaks in the installer")
        :s==Speech::afterInstall?T(u"mówi po instalacji",u"speaks after installation"):T(u"bez mowy",u"no speech");
}
constexpr uint64_t megabyte=uint64_t(1)<<20;
inline const Entry windowsGroup[]={
    {u"Windows: wersja do wyboru, 11, 10, LTSC, Server",u"Windows: a version of your choice, 11, 10, LTSC, Server",Speech::installer,Way::classic,nullptr,0,2,
     u"Instalator Windows mówi Narratorem; Charon włącza go w pliku odpowiedzi.",u"Windows Setup speaks with Narrator; Charon turns it on in the answer file."},
};
inline const Entry linuxAutoGroup[]={
    {u"Debian, stabilny",u"Debian, stable",Speech::installer,Way::classic,nullptr,0,0,nullptr,nullptr},
    {u"Ubuntu, LTS",u"Ubuntu, LTS",Speech::installer,Way::classic,nullptr,0,1,nullptr,nullptr},
};
inline const Entry blindGroup[]={
    {u"Arch Linux z mową, start z sieci",u"Arch Linux with speech, started from the network",Speech::installer,Way::archNet,"https://geo.mirror.pkgbuild.com/iso/latest/",0,0,
     u"Oficjalny system instalacyjny Archa z mową w konsoli. Instalację prowadzisz sam, na przykład poleceniem archinstall.",
     u"The official Arch install system with console speech. You drive the installation yourself, for example with the command archinstall."},
    {u"Slint, Slackware z mówiącym instalatorem",u"Slint, Slackware with a speaking installer",Speech::installer,Way::iso,"https://slackware.uk/slint/x86_64/slint-15.0/iso/slint64-15.0-10.iso",4700*megabyte,0,
     u"Obraz ma 4,6 gigabajta. Projekt jest dziś słabo utrzymywany.",u"The image is 4.6 gigabytes. The project is poorly maintained today."},
    {u"Accessible-Coconut, Ubuntu z czytnikiem Orca",u"Accessible-Coconut, Ubuntu with the Orca screen reader",Speech::installer,Way::iso,"https://sourceforge.net/projects/accessible-coconut/files/latest/download",3800*megabyte,0,
     u"Obraz ma 3,7 gigabajta i opiera się na Ubuntu 22.04; projekt od dawna nie był odświeżany.",u"The image is 3.7 gigabytes and is based on Ubuntu 22.04; the project has not been refreshed for a long time."},
    {u"inny obraz ISO spod adresu, który wpiszesz, na przykład Vojtux",u"another ISO image from an address you type, for example Vojtux",Speech::installer,Way::customIso,nullptr,0,0,
     u"Charon nie wie, czy ten obraz mówi. Adres musi prowadzić prosto do pliku ISO.",u"Charon does not know whether this image speaks. The address must lead straight to the ISO file."},
};
inline const Entry imageGroup[]={
    {u"Debian 13, gotowy obraz",u"Debian 13, ready image",Speech::afterInstall,Way::image,"https://cloud.debian.org/images/cloud/trixie/latest/debian-13-generic-amd64.raw",3072*megabyte,0,nullptr,nullptr},
    {u"Ubuntu 26.04, gotowy obraz",u"Ubuntu 26.04, ready image",Speech::afterInstall,Way::qcow,"https://cloud-images.ubuntu.com/releases/26.04/release/ubuntu-26.04-server-cloudimg-amd64.img",3584*megabyte,0,nullptr,nullptr},
    {u"Arch Linux, gotowy obraz",u"Arch Linux, ready image",Speech::afterInstall,Way::qcow,"https://geo.mirror.pkgbuild.com/images/latest/Arch-Linux-x86_64-cloudimg.qcow2",2048*megabyte,0,nullptr,nullptr,true},
    {u"Fedora 45, gotowy obraz",u"Fedora 45, ready image",Speech::none,Way::qcow,"https://download.fedoraproject.org/pub/fedora/linux/releases/45/Cloud/x86_64/images/Fedora-Cloud-Base-Generic-45-1.4.x86_64.qcow2",5120*megabyte,0,
     u"Fedora nie ma pakietu mowy konsoli, więc ten system po instalacji milczy.",u"Fedora has no console speech package, so this system is silent after installation."},
    {u"Haiku R1 beta 6",u"Haiku R1 beta 6",Speech::none,Way::rawNoSeed,"https://haiku-release.cdn.haiku-os.org/r1beta6/haiku-r1beta6-x86_64-anyboot.iso",1500*megabyte,0,
     u"Oficjalne Haiku nie ma czytnika ekranu. Obraz jest nośnikiem startowym z instalatorem Haiku, bez mowy.",u"Official Haiku has no screen reader. The image is start-up media with the Haiku installer, without speech."},
};
inline const Entry toolsGroup[]={
    {u"netboot.xyz, menu kilkuset systemów z sieci",u"netboot.xyz, a menu of hundreds of systems from the network",Speech::none,Way::netbootXyz,"https://boot.netboot.xyz/ipxe/netboot.xyz.efi",2*megabyte,0,
     u"Samo menu netboot.xyz czyta czytnik Charona. Systemy, które z niego uruchomisz, zwykle nie mówią.",u"Charon's reader reads the netboot.xyz menu itself. The systems you start from it usually do not speak."},
    {u"SystemRescue, system ratunkowy",u"SystemRescue, a rescue system",Speech::none,Way::iso,"https://fastly-cdn.system-rescue.org/releases/13.03/systemrescue-13.03-amd64.iso",1500*megabyte,0,nullptr,nullptr},
    {u"GParted Live, partycje",u"GParted Live, partitions",Speech::none,Way::iso,"https://downloads.sourceforge.net/gparted/gparted-live-1.8.1-6-amd64.iso",700*megabyte,0,nullptr,nullptr},
    {u"Clonezilla Live, kopie dysków",u"Clonezilla Live, disk copies",Speech::none,Way::iso,"https://downloads.sourceforge.net/clonezilla/clonezilla-live-3.3.3-37-amd64.iso",600*megabyte,0,nullptr,nullptr},
    {u"inny obraz ISO spod adresu, który wpiszesz",u"another ISO image from an address you type",Speech::none,Way::customIso,nullptr,0,0,nullptr,nullptr},
};
// r402 (user): CharonDOS with profiles. CharonDOS itself starts now; the Windows
// 95, 98 and ME profiles need a full PC emulator (386 protected mode, VGA, disk,
// a serial speech synthesizer for the period screen readers) that CharonDOS does
// not have yet, and Windows media and a key of the user's own: Microsoft never
// made these systems free. The entries say so instead of pretending.
inline const Entry dosGroup[]={
    {u"zainstaluj CharonDOS: programy DOS, profil Windows 95, 98, ME albo XP z czytnikiem, Windows 11 obok",u"install CharonDOS: DOS programs, a Windows 95, 98, ME or XP profile with a screen reader, Windows 11 beside it",Speech::charon,Way::dosSystem,nullptr,0,0,
     u"CharonDOS dostaje własną partycję i wpis rozruchu i startuje jako pierwszy. Zapytam o programy, profil Windows, czytnik ekranu i o Windows 11 obok. Polecenie WIN uruchamia profil Windows, WINNT Windows zainstalowany obok.",
     u"CharonDOS gets its own partition and boot entry and starts first. I will ask about programs, the Windows profile, the screen reader and Windows 11 beside it. The command WIN starts the Windows profile, WINNT a Windows installed beside it."},
    {u"uruchom CharonDOS teraz, bez instalacji",u"start CharonDOS now, without installing",Speech::charon,Way::dos,nullptr,0,0,
     u"CharonDOS uruchamia programy DOS trybu tekstowego. Jego ekran czyta czytnik Charona.",u"CharonDOS runs text-mode DOS programs. Charon's reader reads its screen."},
};
struct Group {const char16_t* pl;const char16_t* en;const Entry* entries;size_t count;};
#define CHARON_GROUP(list) list,sizeof(list)/sizeof(list[0])
inline const Group groups[]={
    {u"Windows",u"Windows",CHARON_GROUP(windowsGroup)},
    {u"Linux, instalacja automatyczna",u"Linux, automatic installation",CHARON_GROUP(linuxAutoGroup)},
    {u"Linux dla niewidomych",u"Linux for blind people",CHARON_GROUP(blindGroup)},
    {u"gotowe obrazy systemu, najszybsze",u"ready system images, the fastest",CHARON_GROUP(imageGroup)},
    {u"narzędzia i netboot.xyz",u"tools and netboot.xyz",CHARON_GROUP(toolsGroup)},
    {u"CharonDOS",u"CharonDOS",CHARON_GROUP(dosGroup)},
};
// A system without speech: the user must say they know.
inline bool ConfirmSilent(const Entry& e) {
    if(e.speech!=Speech::none)return true;
    static char16_t text[500];Set(text,500,Pl()?e.pl:e.en);
    Cat(text,500,T(u": ten system nie mówi. Osoba niewidoma nie obsłuży go bez pomocy albo własnego czytnika. Mimo to kontynuować?",
        u": this system does not speak. A blind person cannot use it without help or a screen reader of their own. Continue anyway?"));
    return Confirm(text);
}
#ifdef HAK_CHARON_HTTPS
inline void Widen(const char* in,char16_t* out,size_t cap) {size_t n=0;for(;in[n] && n+1<cap;++n)out[n]=char16_t(uint8_t(in[n]));out[n]=0;}
inline bool Narrow(const char16_t* in,char* out,size_t cap) {size_t n=0;for(;in[n] && n+1<cap;++n) {if(in[n]<33 || in[n]>126)return false;out[n]=char(in[n]);}out[n]=0;return n>0;}
// What Charon cannot do for a system it only starts: say it plainly.
inline void SayCharonStays() {
    Say(T(u"Uwaga: tego systemu Charon nie instaluje sam, więc nie dopisze do niego czytnika ani swojego menedżera. Po instalacji uruchom komputer z pendrive'a Charona; instalator Charona jest w folderze EFI, HAK, addon.",
          u"Note: Charon does not install this system itself, so it cannot add its reader or its boot manager to it. After the installation start the computer from Charon's stick; Charon's installer is in the folder EFI, HAK, addon."));
    HearOut(250);
}
// ---- Arch Linux from the official mirror, with speech ---------------------------
inline bool RunArchNet(const Entry& e) {
    if(!EnsureNetwork()) {HearOut(200);return false;}
    if(ramdisk::FreeMemory()<(uint64_t(3)<<30)) {
        Fail(T(u"System instalacyjny Archa działa cały w pamięci i potrzebuje co najmniej 3 gigabajtów wolnej pamięci RAM.",u"The Arch install system runs wholly in memory and needs at least 3 gigabytes of free RAM."));return false;}
    static char16_t url[400];static Memory kernel,initrd;
    Widen(e.url,url,300);Cat(url,400,u"arch/boot/x86_64/vmlinuz-linux");
    if(!FetchToMemory(url,kernel,0,T(u"jądro Arch Linux",u"the Arch Linux kernel")))return false;
    Widen(e.url,url,300);Cat(url,400,u"arch/boot/x86_64/initramfs-linux.img");
    if(!FetchToMemory(url,initrd,0,T(u"system startowy Arch Linux",u"the Arch Linux start-up system"))) {Free(kernel);return false;}
    static char16_t arguments[500];Set(arguments,500,u"archiso_http_srv=");{static char16_t base[300];Widen(e.url,base,300);Cat(arguments,500,base);}
    Cat(arguments,500,u" archisobasedir=arch cms_verify=y ip=dhcp net.ifnames=0 accessibility=on");
    SayCharonStays();
    Say(T(u"Uruchamiam Arch Linux z mową. Pobierze jeszcze około gigabajta systemu i odezwie się własnym głosem w konsoli.",
          u"Starting Arch Linux with speech. It downloads about a gigabyte more of the system and then speaks with its own voice in the console."));
    HearOut(200);
    StartFromMemory(kernel,initrd,u"Arch Linux",arguments);
    Free(kernel);Free(initrd);
    return false;
}
// ---- netboot.xyz ---------------------------------------------------------------------
inline bool RunNetbootXyz(const Entry& e) {
    if(!EnsureNetwork()) {HearOut(200);return false;}
    static char16_t url[300];static Memory program,none;Widen(e.url,url,300);
    if(!FetchToMemory(url,program,0,u"netboot.xyz"))return false;
    none.data=nullptr;none.size=0;none.capacity=0;
    Say(T(u"Uruchamiam netboot.xyz. Jego menu czyta czytnik Charona.",u"Starting netboot.xyz. Charon's reader reads its menu."));HearOut(100);
    StartFromMemory(program,none,u"netboot.xyz",nullptr);
    Free(program);
    return false;
}
// The start-up program on disk `d` (after an image was written to it).
inline bool StartDisk(const disks::Disk& d,const char16_t* title) {
    efi_handle list[16];const size_t n=images::VolumesOn(d,list,16);
    static const char16_t* const loaders[]={u"\\EFI\\BOOT\\BOOTX64.EFI",u"\\EFI\\debian\\shimx64.efi",u"\\EFI\\ubuntu\\shimx64.efi",u"\\EFI\\fedora\\shimx64.efi",u"\\EFI\\systemd\\systemd-bootx64.efi"};
    for(size_t i=0;i<n;++i)for(const auto* loader:loaders) {
        FileRef ref;ref.volume=list[i];Set(ref.path,pathUnits,loader);efi_file_protocol* file=nullptr;
        if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS || !file)continue;
        file->Close(file);StartProgram(list[i],loader,title,nullptr);return true;
    }
    Fail(T(u"Na zapisanym dysku nie znalazłem programu rozruchowego UEFI. Uruchom komputer ponownie i wybierz ten dysk w menu urządzeń.",
        u"I found no UEFI boot program on the written disk. Restart the computer and choose this disk in the device menu."));
    return false;
}
// ---- a bootable ISO: onto a USB stick, or into memory --------------------------
inline bool RunIso(const char* address,const char16_t* title,uint64_t bytes,bool linuxLive) {
    if(!EnsureNetwork()) {HearOut(200);return false;}
    const bool ramFits=ramdisk::Find() && ramdisk::Room(bytes?bytes:uint64_t(2)<<30,uint64_t(2)<<30,false);
    static const Choice toUsb={u"zapisz na pendrive, który zostanie wymazany",u"write to a USB stick, which gets erased","usb"};
    static const Choice toRam={u"uruchom z pamięci, jako RAM-dysk",u"start from memory, as a RAM disk","ram"};
    static Choice ways[2];size_t count=0;ways[count++]=toUsb;if(ramFits)ways[count++]=toRam;
    if(ramFits && linuxLive) {
        Say(T(u"Uwaga: system startujący z RAM-dysku musi sam odnaleźć swój nośnik w pamięci; nie każdy Linux to umie. Pewniejszy jest pendrive.",
              u"Note: a system started from a RAM disk must find its media in memory by itself; not every Linux can. A USB stick is the surer way."));HearOut(200);}
    size_t way=0;
    if(!ask::Choose(T(u"Jak uruchomić ten obraz?",u"How to start this image?"),ways,count,way))return false;
    if(ways[way].value[0]=='r') {
        static char16_t wide[1024];Widen(address,wide,1024);
        ramdisk::BootUrl(wide,title,1024);
        return false;
    }
    size_t index=0;
    if(!images::ChooseDisk(bytes?bytes:uint64_t(1)<<30,true,index))return false;
    const auto& d=disks::D().list[index];
    if(!images::ConfirmErase(d,index,title))return false;
    uint64_t written=0;
    if(!images::UrlToDisk(address,d,T(u"Obraz na pendrive",u"Image to the stick"),&written))return false;
    images::Reconnect(d.handle);
    Play(Icon::done);
    static char16_t text[300];Set(text,300,T(u"Pendrive jest gotowy: ",u"The stick is ready: "));Cat(text,300,title);Cat(text,300,T(u". Uruchomić go teraz?",u". Start it now?"));
    if(!Confirm(text))return true;
    return StartDisk(d,title);
}
inline bool RunCustomIso(bool linuxLive) {
    static char16_t wide[1024];static char address[1024];wide[0]=0;
    if(!Prompt(T(u"Adres obrazu ISO, https:",u"The ISO image's address, https:"),wide,1000))return false;
    if(!Narrow(wide,address,sizeof(address))) {Fail(T(u"Adres zawiera niedozwolone znaki.",u"The address holds characters that are not allowed."));return false;}
    return RunIso(address,T(u"obraz ISO",u"ISO image"),0,linuxLive);
}
// ---- ready system images --------------------------------------------------------------
inline const Question imageQuestions[]={
    {"desktop",true,Kind::multi,u"Pulpity:",u"Desktops:","",CHARON_CHOICES(imageDesktops),u"Gotowy obraz to system bez pulpitu; zaznaczone pulpity pobiera przy pierwszym starcie. Nic nie zaznaczone: sama konsola z mową.",u"A ready image is a system without a desktop; the checked desktops are downloaded at its first start. Nothing checked: the console with speech only."},
    {"readers",true,Kind::multi,u"Czytniki ekranu:",u"Screen readers:","orca,speakup",CHARON_CHOICES(linuxReaders),CHARON_READER_HELP_PL,CHARON_READER_HELP_EN},
    {"ssh",false,Kind::yesNo,u"Pozwolić na logowanie hasłem przez SSH?",u"Allow password logins over SSH?","no",nullptr,0,nullptr,nullptr},
};
// r403: which system an image is, by its official address: d Debian, u Ubuntu, a Arch, f Fedora.
inline char DistroOf(const Entry& e) {
    const char* u=e.url?e.url:"";
    auto has=[&](const char* w) {for(size_t i=0;u[i];++i) {size_t k=0;while(w[k] && u[i+k]==w[k])++k;if(!w[k])return true;}return false;};
    return has("debian")?'d':has("ubuntu")?'u':has("pkgbuild")||has("Arch")?'a':has("fedora")?'f':'?';
}
// The packages of one desktop in each system ("" when it has none).
inline const char* DesktopPackages(const char* id,char distro) {
    struct Row {const char* id;const char* debian;const char* ubuntu;const char* arch;};
    static const Row rows[]={
        {"gnome","task-gnome-desktop","ubuntu-desktop-minimal","gnome, gdm"},
        {"kde","task-kde-desktop","kubuntu-desktop","plasma, sddm"},
        {"xfce","task-xfce-desktop","xubuntu-desktop","xfce4, lightdm, lightdm-gtk-greeter"},
        {"mate","task-mate-desktop","ubuntu-mate-desktop","mate, lightdm, lightdm-gtk-greeter"},
        {"cinnamon","task-cinnamon-desktop","ubuntucinnamon-desktop","cinnamon, lightdm, lightdm-gtk-greeter"},
        {"lxqt","task-lxqt-desktop","lubuntu-desktop","lxqt, sddm"}};
    for(const auto& r:rows)if(ask::Same(r.id,id))return distro=='d'?r.debian:distro=='u'?r.ubuntu:distro=='a'?r.arch:"";
    return "";
}
// cloud-init user-data for a ready image: the user, speech, Charon.
inline void ImageUserData(Text& y,bool speech,char distro) {
    y.Line("#cloud-config");
    y.Line("# Written by Charon from spoken answers.");
    y.Line("hostname: ",ask::Get("hostname"));
    y.Line("locale: ",ask::Get("locale"),".UTF-8");
    y.Line("timezone: ",ask::Get("timezone"));
    y.Line("keyboard: {layout: ",ask::Get("keyboard"),"}");
    y.Line("users:");
    y.Line("  - name: ",ask::Get("user"));
    y.Line("    gecos: \"",ask::Get("fullname"),"\"");
    y.Line("    passwd: \"",ask::Get("hash"),"\"");
    y.Line("    lock_passwd: false");
    y.Line("    groups: [sudo, wheel, audio, adm]");
    y.Line("    sudo: \"ALL=(ALL) ALL\"");   // Arch and Fedora give the groups no sudo rule by themselves
    y.Line("    shell: /bin/bash");
    y.Line("ssh_pwauth: ",ask::Is("ssh","yes")?"true":"false");
    y.Line("growpart: {mode: auto, devices: ['/']}");
    y.Line("package_update: true");
    // r403: desktops and readers. One missing package makes cloud-init skip the whole
    // list, so Fenrir (not in Arch's own repositories) is left to speech.sh there.
    const bool desktops=ask::Any("desktop") && distro!='f';
    y.Add("packages: [efibootmgr");
    if(speech) {static char packages[200];ReaderPackages(packages,sizeof(packages),desktops);
        if(distro=='a') {static const char fen[]=", fenrir";for(char* c=packages;*c;++c) {size_t k=0;while(fen[k] && c[k]==fen[k])++k;if(!fen[k]) {char* t=c;char* f=c+k;while((*t++=*f++)){}break;}}}
        y.Add(packages);}
    if(desktops)for(const auto& c:imageDesktops)if(ask::Has("desktop",c.value)) {const char* p=DesktopPackages(c.value,distro);if(p[0]) {y.Add(", ");y.Add(p);}}
    y.Line("]");
    y.Line("runcmd:");
    // The EFI partition holds Charon and its scripts (CharonOnto); it may be mounted at /boot/efi or /efi, or not yet.
    y.Line("  - [sh, -c, 'e=/boot/efi; [ -d /efi/EFI/HAK ] && e=/efi; if [ ! -d $e/EFI/HAK ]; then mkdir -p /run/charon-esp; for d in $(blkid -t TYPE=vfat -o device); do mount $d /run/charon-esp 2>/dev/null || continue; [ -d /run/charon-esp/EFI/HAK ] && { e=/run/charon-esp; break; }; umount /run/charon-esp; done; fi; mkdir -p /var/lib/charon; cp $e/EFI/HAK/install-charon.sh $e/EFI/HAK/speech.sh /var/lib/charon/ 2>/dev/null; sed -i \"s|^esp=.*|esp=$e|\" /var/lib/charon/install-charon.sh; sh /var/lib/charon/install-charon.sh; true']");
    if(ask::Is("dual","yes"))
        y.Line("  - [sh, -c, 'c=$(efibootmgr | sed -n \"s/^Boot\\([0-9A-Fa-f]\\{4\\}\\)\\*\\{0,1\\} Charon.*/\\1/p\" | head -n1); o=$(efibootmgr | sed -n \"s/^BootOrder: //p\"); [ -n \"$c\" ] && efibootmgr --bootorder \"$c,$(echo $o | sed \"s/$c,\\{0,1\\}//;s/,$//\")\" >/dev/null; true']");
    if(desktops)   // the display manager and the graphical start (Arch enables nothing by itself)
        y.Line("  - [sh, -c, 'for d in gdm gdm3 sddm lightdm; do systemctl enable $d >/dev/null 2>&1 && break; done; systemctl set-default graphical.target; systemctl isolate graphical.target --no-block; true']");
    if(speech) {
        static char args[200];ReaderArguments(args,sizeof(args));
        y.Line("  - [sh, -c, 'sh /var/lib/charon/speech.sh ",args," > /var/lib/charon/speech.log 2>&1; [ -f /etc/modules-load.d/charon-speakup.conf ] && modprobe speakup_soft 2>/dev/null; systemctl start charon-unmute.service 2>/dev/null; systemctl is-enabled espeakup >/dev/null 2>&1 && systemctl start espeakup; for s in fenrirscreenreader fenrir; do systemctl is-enabled $s >/dev/null 2>&1 && systemctl start $s; done; true']");
        y.Line("  - [sh, -c, 'sleep 3; command -v espeak-ng >/dev/null && espeak-ng \"Charon: system is ready. Log in.\" 2>/dev/null; true']");
    }
}
inline bool RunImage(const Entry& e) {
    if(!EnsureNetwork()) {HearOut(200);return false;}
    size_t mode=0;
    static const Choice imageModes[]={{u"szybkie pytania",u"quick questions","quick"},{u"dokładne pytania",u"detailed questions","detailed"}};
    const bool seeded=e.way!=Way::rawNoSeed;
    if(seeded) {
        if(!ask::Choose(T(u"Ile pytań?",u"How many questions?"),CHARON_CHOICES(imageModes),mode))return false;
        if(!ask::Run(commonQuestions,sizeof(commonQuestions)/sizeof(commonQuestions[0]),mode==0))return false;
        if(!ask::Run(imageQuestions,sizeof(imageQuestions)/sizeof(imageQuestions[0]),mode==0))return false;
        if(DistroOf(e)=='f' && ask::Any("desktop")) {Say(T(u"Dla obrazu Fedory Charon nie dodaje pulpitów; zostaje system konsolowy.",u"For the Fedora image Charon adds no desktops; it stays a console system."));ask::Put("desktop","");}
        if(!ResolveReaders(ask::Any("desktop")))return false;
        DeriveDefaults();HashPassword();
        if(!ask::Get("hash")[0]) {Fail(T(u"Nie udało się przygotować hasła.",u"The password could not be prepared."));return false;}
    }
    size_t index=0;auto& remap=images::R();remap=images::Remap{};
    // r402: beside the systems already there (dual boot), or the whole disk.
    static const Choice places[]={{u"wymaż cały wybrany dysk",u"erase a whole chosen disk","whole"},
        {u"obok obecnych systemów: użyj tylko wolnego miejsca, nic nie kasuj",u"beside the present systems: use free space only, erase nothing","free"}};
    size_t place=0;
    if(seeded && !ask::Choose(T(u"Co zrobić z dyskiem?",u"What to do with the disk?"),CHARON_CHOICES(places),place))return false;
    if(place==1 && e.wholeOnly) {
        Fail(T(u"Ten obraz ma w programie startowym wpisany numer partycji, więc działa tylko na całym dysku: obok innych systemów nie wystartuje, co sprawdzono. Do instalacji obok innego systemu wybierz obraz Debiana, Ubuntu albo Fedory.",
            u"This image's boot program names its partition by number, so it works on a whole disk only: beside other systems it does not start, which was tested. To install beside another system choose the Debian, Ubuntu or Fedora image."));
        return false;}
    if(place==1) {
        auto& s=disks::D();disks::Load();
        static char16_t labels[disks::maxDisks][300];static Choice list[disks::maxDisks];static size_t map[disks::maxDisks];size_t count=0;
        for(size_t i=0;i<s.count;++i) {
            const auto& c=s.list[i];
            if(c.readOnly || !c.gpt || c.blockSize!=512 || c.freeBlocks*c.blockSize<e.bytes+(uint64_t(64)<<20))continue;
            disks::DiskLabel(c,i,labels[count],300);list[count]={labels[count],labels[count],""};map[count]=i;++count;
        }
        if(!count) {
            static char16_t text[500];Set(text,500,T(u"Na żadnym dysku z tablicą GPT nie ma dość wolnego, niepodzielonego miejsca; potrzeba około ",u"No disk with a GPT table has enough free, unpartitioned space; needed, about "));
            systools::PutSize(text,500,e.bytes+(uint64_t(64)<<20),Pl());
            Cat(text,500,T(u". W Windows zwolnisz miejsce skryptem Charon MakeRoom z folderu Charona albo w Zarządzaniu dyskami; Charon sam nie zmniejsza cudzych partycji.",
                u". In Windows you free space with the script Charon MakeRoom from Charon's folder, or in Disk Management; Charon does not shrink other systems' partitions itself."));
            Fail(text);return false;}
        size_t chosen=0;
        if(!ask::Choose(T(u"Na którym dysku?",u"On which disk?"),list,count,chosen))return false;
        index=map[chosen];const auto& c=s.list[index];
        static char16_t text[900];static char16_t label[300];disks::DiskLabel(c,index,label,300);
        Set(text,900,T(u"Nic nie zostanie skasowane. System ",u"Nothing will be erased. The system "));Cat(text,900,Pl()?e.pl:e.en);
        Cat(text,900,T(u" zajmie wolne miejsce, ",u" takes the free space, "));systools::PutSize(text,900,c.freeBlocks*c.blockSize,Pl());Cat(text,900,T(u", na dysku: ",u", on the disk: "));Cat(text,900,label);
        Cat(text,900,T(u". Dostanie własną partycję startową. Po instalacji pierwszy w kolejności startu będzie menedżer Charon, który pokazuje oba systemy. Uwaga: jeśli dysk Windows jest zaszyfrowany BitLockerem, zmiana kolejności startu może wywołać pytanie o klucz odzyskiwania. Aby zacząć, wpisz słowo TAK i naciśnij Enter.",
            u". It gets a boot partition of its own. After the installation the Charon boot manager, which shows both systems, is first in the boot order. Note: if the Windows disk is encrypted with BitLocker, changing the boot order may bring up the recovery key question. To start, type the word YES and press Enter."));
        Say(text);
        static char16_t word[16];word[0]=0;
        if(!Prompt(T(u"Wpisz TAK, aby zacząć:",u"Type YES to start:"),word,15,nullptr,nullptr,false))return false;
        if(!SameFold(word,T(u"tak",u"yes"))) {Say(T(u"Nie wpisano potwierdzenia. Nic nie zrobiono.",u"The confirmation was not typed. Nothing was done."));return false;}
        remap.on=true;remap.disk=&c;remap.gapFirst=c.freeFirst*c.blockSize;remap.gapEnd=(c.freeFirst+c.freeBlocks)*c.blockSize;
        ask::Put("dual","yes");
    } else {
        if(!images::ChooseDisk(e.bytes+(seeded?uint64_t(1)<<30:0),false,index))return false;
        if(!images::ConfirmErase(disks::D().list[index],index,Pl()?e.pl:e.en))return false;
    }
    const auto& d=disks::D().list[index];
    if(seeded && U().host->ensureReader && !U().host->ensureReader(U().host->context))
        Say(T(u"Uwaga: nie udało się włączyć dodatku czytnika w firmware.",u"Note: the reader add-on could not be turned on in the firmware."));
    uint64_t written=0;const char16_t* what=T(u"Obraz na dysk",u"Image to the disk");
    const bool wrote=e.way==Way::qcow?images::QcowToDisk(e.url,d,what,&written):images::UrlToDisk(e.url,d,what,&written);
    if(!wrote) {remap.on=false;return false;}
    bool configured=true,charon=false;
    if(seeded) {
        static char userData[6000];static char metaData[200];Text y{userData,sizeof(userData),0,false};userData[0]=0;
        const bool speech=ask::Is("speech","yes") && e.speech!=Speech::none;
        ImageUserData(y,speech,DistroOf(e));
        Text m{metaData,sizeof(metaData),0,false};metaData[0]=0;
        {uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));m.Add("instance-id: charon-");m.Number(lo);m.Add("\n");}
        m.Line("local-hostname: ",ask::Get("hostname"));
        Say(T(u"Obraz jest na dysku. Dopisuję konfigurację: użytkownika, mowę i Charona.",u"The image is on the disk. Adding the configuration: the user, speech and Charon."));
        configured=!y.overflow && images::SeedOnto(d,userData,y.size,metaData,m.size);
        for(auto& c:userData)c=0;
        if(!configured)Fail(T(u"Nie udało się dopisać konfiguracji do dysku. System wystartuje, ale bez użytkownika i bez mowy.",
            u"The configuration could not be added to the disk. The system starts, but with no user and no speech."));
    }
    images::Reconnect(d.handle);
    if(seeded) {
        const efi_handle esp=remap.on?(remap.hasEsp?images::VolumeOfPartition(remap.espUnique):nullptr):images::EspOn(d);
        charon=esp && images::CharonOnto(esp) && WriteText(esp,u"\\EFI\\HAK\\speech.sh",speechScript,sizeof(speechScript)-1);
        if(!charon)Say(T(u"Uwaga: nie udało się skopiować Charona na partycję EFI nowego systemu; czytnik i menedżer Charona zostają tylko na pendrivie.",
            u"Note: Charon could not be copied to the new system's EFI partition; Charon's reader and boot manager stay on the stick only."));
    } else SayCharonStays();
    ask::Clear();
    Play(Icon::done);
    static char16_t text[500];Set(text,500,T(u"Gotowe: ",u"Done: "));Cat(text,500,Pl()?e.pl:e.en);
    if(seeded && configured)Cat(text,500,T(u". Przy pierwszym starcie system sam się konfiguruje i pobiera pakiety mowy; trwa to kilka minut i wymaga sieci.",
        u". At its first start the system configures itself and downloads the speech packages; that takes a few minutes and needs the network."));
    Cat(text,500,T(u" Uruchomić ten system teraz?",u" Start this system now?"));
    const bool dual=remap.on;remap.on=false;
    if(!Confirm(text))return true;
    if(dual) {   // the new system's own boot partition, not the first loader on the disk
        const efi_handle esp=remap.hasEsp?images::VolumeOfPartition(remap.espUnique):nullptr;
        static const char16_t* const loaders[]={u"\\EFI\\BOOT\\BOOTX64.EFI",u"\\EFI\\debian\\shimx64.efi",u"\\EFI\\ubuntu\\shimx64.efi",u"\\EFI\\fedora\\shimx64.efi"};
        for(const auto* loader:loaders) {
            FileRef ref;ref.volume=esp;Set(ref.path,pathUnits,loader);efi_file_protocol* file=nullptr;
            if(!esp || OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS || !file)continue;
            file->Close(file);StartProgram(esp,loader,Pl()?e.pl:e.en,nullptr);return true;
        }
        Fail(T(u"Nie znalazłem programu startowego nowego systemu. Uruchom komputer ponownie i wybierz go w menu urządzeń Charona.",u"I did not find the new system's boot program. Restart the computer and choose it in Charon's device menu."));
        return false;
    }
    return StartDisk(d,Pl()?e.pl:e.en);
}
#endif
inline void Classic(size_t system);
inline void Dialogue() {
    ask::Clear();
    Say(T(u"Instalacja systemu z sieci. Najpierw wybierz grupę, potem system. Przy każdym systemie Charon mówi, czy ten system mówi. Dysk zmienia się dopiero po potwierdzeniu, które wpiszesz. Escape cofa o jeden krok.",
        u"Installing a system from the network. First choose a group, then a system. With every system Charon says whether that system speaks. A disk is changed only after a confirmation you type. Escape goes back one step."));
    HearOut(150);
    static Choice groupChoices[sizeof(groups)/sizeof(groups[0])];const size_t groupCount=sizeof(groups)/sizeof(groups[0]);   // the labels and the entries arrays below hold 8 per group
    for(size_t i=0;i<groupCount;++i)groupChoices[i]={groups[i].pl,groups[i].en,""};
    size_t group=0;
    for(;;) {
        if(!ask::Choose(T(u"Która grupa?",u"Which group?"),groupChoices,groupCount,group))return;
        const Group& g=groups[group];
        static char16_t labels[8][2][200];static Choice entries[8];const size_t n=g.count<8?g.count:8;
        for(size_t i=0;i<n;++i) {
            Set(labels[i][0],200,g.entries[i].pl);Cat(labels[i][0],200,u": ");
            Set(labels[i][1],200,g.entries[i].en);Cat(labels[i][1],200,u": ");
            const Speech s=g.entries[i].speech;
            Cat(labels[i][0],200,s==Speech::installer?u"mówi w instalatorze":s==Speech::afterInstall?u"mówi po instalacji":s==Speech::charon?u"czyta go czytnik Charona":u"bez mowy");
            Cat(labels[i][1],200,s==Speech::installer?u"speaks in the installer":s==Speech::afterInstall?u"speaks after installation":s==Speech::charon?u"read by Charon's reader":u"no speech");
            entries[i]={labels[i][0],labels[i][1],""};
        }
        size_t pick=0;
        if(n>1 && !ask::Choose(T(u"Który system?",u"Which system?"),entries,n,pick))continue;
        const Entry& e=g.entries[pick];
        if(e.notePl) {Say(Pl()?e.notePl:e.noteEn);HearOut(250);}
        if(e.way==Way::dosWindows) {   // r403: a Windows profile is a part of CharonDOS installed as a system
#ifdef HAK_CHARON_HTTPS
            dossys::Run();
#endif
            HearOut(200);continue;}
        if(e.way==Way::dosSystem || e.way==Way::dosDual) {
#ifdef HAK_CHARON_HTTPS
            dossys::Run(e.way==Way::dosDual);
#else
            Fail(T(u"Ta wersja Charona nie ma klienta HTTPS.",u"This build of Charon has no HTTPS client."));
#endif
            HearOut(200);continue;}
        if(e.way==Way::dosApps) {
#ifdef HAK_CHARON_HTTPS
            dosapps::Run();
#else
            Fail(T(u"Ta wersja Charona nie ma klienta HTTPS.",u"This build of Charon has no HTTPS client."));HearOut(200);
#endif
            continue;}
        if(e.way==Way::dos) {
            efi_handle volumes[32];const size_t count=Volumes(volumes,32);bool started=false;
            static const char16_t* const places[]={u"\\EFI\\HAK\\packages\\CharonDOS\\CHARONDOS.EFI",u"\\charon-packages\\CharonDOS\\CHARONDOS.EFI"};
            for(size_t i=0;i<count && !started;++i)for(const auto* place:places) {
                FileRef ref;ref.volume=volumes[i];Set(ref.path,pathUnits,place);efi_file_protocol* file=nullptr;
                if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS || !file)continue;
                file->Close(file);ScriptEnd();StartProgram(volumes[i],place,u"CharonDOS",nullptr);started=true;break;
            }
            if(!started) {Fail(T(u"Nie znalazłem pakietu CharonDOS na żadnym dysku: folder EFI, HAK, packages, CharonDOS.",u"I did not find the CharonDOS package on any disk: the folder EFI, HAK, packages, CharonDOS."));HearOut(200);continue;}
            return;}
        if(!ConfirmSilent(e))continue;
        if(e.way==Way::classic) {Classic(e.classic);return;}
#ifdef HAK_CHARON_HTTPS
        bool done=false;
        switch(e.way) {
            case Way::archNet:done=RunArchNet(e);break;
            case Way::netbootXyz:done=RunNetbootXyz(e);break;
            case Way::iso:done=RunIso(e.url,Pl()?e.pl:e.en,e.bytes,group==2);break;
            case Way::customIso:done=RunCustomIso(group==2);break;
            default:done=RunImage(e);break;
        }
        if(!done)HearOut(250);
        return;
#else
        Fail(T(u"Ta wersja Charona nie ma własnego klienta HTTPS, więc tej pozycji nie pobierze.",u"This build of Charon has no HTTPS client of its own, so it cannot download this entry."));HearOut(200);
#endif
    }
}
}  // namespace install
}  // namespace ws
}  // namespace hakefi
