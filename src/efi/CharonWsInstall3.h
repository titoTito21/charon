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
    bool unofficial=false;                // r409: a Windows image not made by Microsoft: its own group, a warning and a typed word (ConfirmUnofficial)
};
inline const char16_t* SpeechText(Speech s) {
    return s==Speech::installer?T(u"mówi w instalatorze",u"speaks in the installer")
        :s==Speech::afterInstall?T(u"mówi po instalacji",u"speaks after installation"):T(u"bez mowy",u"no speech");
}
constexpr uint64_t megabyte=uint64_t(1)<<20;
inline const Entry windowsGroup[]={
    {u"Windows: wersja do wyboru: 11, 10, LTSC albo Server",u"Windows: the version you choose: 11, 10, LTSC or Server",Speech::installer,Way::classic,nullptr,0,2,
     u"W instalatorze Windows mówi NVDA, jeśli je zaznaczysz, a w przeciwnym razie Narrator; Charon włącza czytnik ekranu w pliku odpowiedzi.",u"In Windows Setup NVDA speaks if you check it, otherwise Narrator; Charon turns the screen reader on in the answer file."},
};
inline const Entry linuxAutoGroup[]={
    {u"Debian, stabilny",u"Debian, stable",Speech::installer,Way::classic,nullptr,0,0,nullptr,nullptr},
    {u"Ubuntu, LTS",u"Ubuntu, LTS",Speech::installer,Way::classic,nullptr,0,1,nullptr,nullptr},
};
inline const Entry blindGroup[]={
    {u"Arch Linux z mową, uruchamiany z sieci",u"Arch Linux with speech, started from the network",Speech::installer,Way::archNet,"https://geo.mirror.pkgbuild.com/iso/latest/",0,0,
     u"Oficjalny system instalacyjny Archa z mową w konsoli. Instalację przeprowadzasz sam, na przykład poleceniem archinstall.",
     u"The official Arch installation system with console speech. You carry out the installation yourself, for example with the command archinstall."},
    {u"Slint, Slackware z mówiącym instalatorem",u"Slint, Slackware with a speaking installer",Speech::installer,Way::iso,"https://slackware.uk/slint/x86_64/slint-15.0/iso/slint64-15.0-10.iso",4700*megabyte,0,
     u"Obraz ma 4,6 gigabajta. Projekt jest dziś rzadko aktualizowany.",u"The image is 4.6 gigabytes. The project is rarely updated these days."},
    // r409: Stormux (https://stormux.org), an Arch-based system for blind people: Fenrir in the console, Cthulhu and I38 on
    // the desktop. Its x86_64 image is an ISO since 2026 (https://stormux.org/downloads/, 2 043 084 800 bytes, SHA-1
    // 3b79025b1c2fab1deb74a3e67b89771cf27bc7b1 in stormux-x86_64-2026-11-01.iso.sha1sum). Looked into (r409): an archiso image,
    // \EFI\BOOT\BOOTx64.EFI is systemd-boot whose default entry is "with speech" (accessibility=on); its root image
    // enables stormux-speech.service, which starts Fenrir, and holds /usr/local/bin/install-stormux. The address names a
    // dated file: a new image needs a new address here.
    {u"Stormux, Arch Linux dla niewidomych: Fenrir, Cthulhu i I38",u"Stormux, Arch Linux for blind people: Fenrir, Cthulhu and I38",Speech::installer,Way::iso,"https://stormux.org/downloads/stormux-x86_64-2026-11-01.iso",1950*megabyte,0,
     u"Obraz ma 1,9 gigabajta. Po uruchomieniu w konsoli odzywa się czytnik ekranu Fenrir; system instalujesz na dysku poleceniem install-stormux.",u"The image is 1.9 gigabytes. After starting, the Fenrir screen reader speaks in the console; you install the system to a disk with the command install-stormux."},
    {u"Accessible-Coconut, Ubuntu z czytnikiem Orca",u"Accessible-Coconut, Ubuntu with the Orca screen reader",Speech::installer,Way::iso,"https://sourceforge.net/projects/accessible-coconut/files/latest/download",3800*megabyte,0,
     u"Obraz ma 3,7 gigabajta i opiera się na Ubuntu 22.04; projekt od dawna nie był aktualizowany.",u"The image is 3.7 gigabytes and is based on Ubuntu 22.04; the project has not been updated for a long time."},
    {u"inny obraz ISO z adresu, który wpiszesz, na przykład Vojtux",u"another ISO image from an address you type, for example Vojtux",Speech::installer,Way::customIso,nullptr,0,0,
     u"Charon nie wie, czy ten obraz mówi. Adres musi prowadzić bezpośrednio do pliku ISO.",u"Charon does not know whether this image speaks. The address must lead straight to the ISO file."},
};
inline const Entry imageGroup[]={
    {u"Debian 13, gotowy obraz",u"Debian 13, ready-made image",Speech::afterInstall,Way::image,"https://cloud.debian.org/images/cloud/trixie/latest/debian-13-generic-amd64.raw",3072*megabyte,0,nullptr,nullptr},
    {u"Ubuntu 26.04, gotowy obraz",u"Ubuntu 26.04, ready-made image",Speech::afterInstall,Way::qcow,"https://cloud-images.ubuntu.com/releases/26.04/release/ubuntu-26.04-server-cloudimg-amd64.img",3584*megabyte,0,nullptr,nullptr},
    {u"Arch Linux, gotowy obraz",u"Arch Linux, ready-made image",Speech::afterInstall,Way::qcow,"https://geo.mirror.pkgbuild.com/images/latest/Arch-Linux-x86_64-cloudimg.qcow2",2048*megabyte,0,nullptr,nullptr,true},
    {u"Fedora 45, gotowy obraz",u"Fedora 45, ready-made image",Speech::none,Way::qcow,"https://download.fedoraproject.org/pub/fedora/linux/releases/45/Cloud/x86_64/images/Fedora-Cloud-Base-Generic-45-1.4.x86_64.qcow2",5120*megabyte,0,
     // r409: checked against Fedora 45's package index: neither espeakup nor Fenrir is packaged (Emacspeak, Orca, eSpeak NG and Speech Dispatcher are)
     u"Fedora nie ma pakietu espeakup ani Fenrir, więc Charon nie konfiguruje tam czytnika ekranu w konsoli i po instalacji ten system nie mówi.",u"Fedora has no package of espeakup or Fenrir, so Charon sets up no console screen reader there and this system does not speak after installation."},
    {u"Haiku R1 beta 6",u"Haiku R1 beta 6",Speech::none,Way::rawNoSeed,"https://haiku-release.cdn.haiku-os.org/r1beta6/haiku-r1beta6-x86_64-anyboot.iso",1500*megabyte,0,
     u"Oficjalne Haiku nie ma czytnika ekranu. Obraz to nośnik rozruchowy z instalatorem Haiku, bez mowy.",u"Official Haiku has no screen reader. The image is bootable media with the Haiku installer, without speech."},
};
inline const Entry toolsGroup[]={
    {u"netboot.xyz, menu kilkuset systemów z sieci",u"netboot.xyz, a menu of hundreds of systems from the network",Speech::none,Way::netbootXyz,"https://boot.netboot.xyz/ipxe/netboot.xyz.efi",2*megabyte,0,
     u"Samo menu netboot.xyz czyta czytnik ekranu Charona. Systemy, które z niego uruchomisz, zwykle nie mówią.",u"Charon's screen reader reads the netboot.xyz menu itself. The systems you start from it usually do not speak."},
    {u"SystemRescue, system ratunkowy",u"SystemRescue, a rescue system",Speech::none,Way::iso,"https://fastly-cdn.system-rescue.org/releases/13.03/systemrescue-13.03-amd64.iso",1500*megabyte,0,nullptr,nullptr},
    {u"GParted Live, edytor partycji",u"GParted Live, partition editor",Speech::none,Way::iso,"https://downloads.sourceforge.net/gparted/gparted-live-1.8.1-6-amd64.iso",700*megabyte,0,nullptr,nullptr},
    {u"Clonezilla Live, klonowanie i kopie zapasowe dysków",u"Clonezilla Live, disk cloning and backup",Speech::none,Way::iso,"https://downloads.sourceforge.net/clonezilla/clonezilla-live-3.3.3-37-amd64.iso",600*megabyte,0,nullptr,nullptr},
    {u"inny obraz ISO z adresu, który wpiszesz",u"another ISO image from an address you type",Speech::none,Way::customIso,nullptr,0,0,nullptr,nullptr},
};
// r402 (user): CharonDOS with profiles. CharonDOS itself starts now; the Windows
// 95, 98 and ME profiles need a full PC emulator (386 protected mode, VGA, disk,
// a serial speech synthesizer for the period screen readers) that CharonDOS does
// not have yet, and Windows media and a key of the user's own: Microsoft never
// made these systems free. The entries say so instead of pretending.
inline const Entry dosGroup[]={
    {u"zainstaluj CharonDOS: programy DOS, profil Windows 95, 98, ME albo XP z czytnikiem ekranu, Windows 11 obok",u"install CharonDOS: DOS programs, a Windows 95, 98, ME or XP profile with a screen reader, Windows 11 alongside",Speech::charon,Way::dosSystem,nullptr,0,0,
     u"CharonDOS dostaje własną partycję i wpis rozruchowy i uruchamia się jako pierwszy. Zapytam o programy, profil Windows, czytnik ekranu i o Windows 11 obok. Polecenie WIN uruchamia profil Windows, a polecenie WINNT uruchamia Windows zainstalowany obok.",
     u"CharonDOS gets its own partition and boot entry and starts first. I will ask about programs, the Windows profile, the screen reader and Windows 11 alongside. The command WIN starts the Windows profile, and the command WINNT starts the Windows installed alongside."},
    {u"uruchom CharonDOS teraz, bez instalacji",u"start CharonDOS now, without installing",Speech::charon,Way::dos,nullptr,0,0,
     u"CharonDOS uruchamia programy DOS działające w trybie tekstowym. Jego ekran czyta czytnik ekranu Charona.",u"CharonDOS runs text-mode DOS programs. Charon's screen reader reads its screen."},
};
// r409 (user): Windows images made by someone other than Microsoft, in a group of their own, each behind a spoken
// warning and a typed word. One image qualifies: Tiny11 25H2 by NTDEV (address and SHA-256: CharonWsInstall2.h,
// windowsUnofficial and tiny11Sha256). It is installed like Windows (Way::classic, system 2): Charon's own answer
// file, screen readers, BitLocker prevention, Charon's reader and boot entry. Not here, on purpose: Tiny11 Core,
// Nano11, X-Lite, Ghost Spectre and any image that is pre-activated or cannot be checked against its author's hash.
inline const Entry unofficialGroup[]={
    {u"Tiny11 25H2 od NTDEV, nieoficjalny obraz Windows 11, po angielsku",u"Tiny11 25H2 by NTDEV, an unofficial Windows 11 image, in English",Speech::installer,Way::classic,nullptr,0,2,nullptr,nullptr,false,true},
};
struct Group {const char16_t* pl;const char16_t* en;const Entry* entries;size_t count;};
#define CHARON_GROUP(list) list,sizeof(list)/sizeof(list[0])
inline const Group groups[]={
    {u"Windows",u"Windows",CHARON_GROUP(windowsGroup)},
    {u"Linux: Debian i Ubuntu",u"Linux: Debian and Ubuntu",CHARON_GROUP(linuxAutoGroup)},
    {u"Linux dla niewidomych",u"Linux for blind people",CHARON_GROUP(blindGroup)},
    {u"gotowe obrazy systemów, najszybsza instalacja",u"ready-made system images, the fastest installation",CHARON_GROUP(imageGroup)},
    {u"narzędzia i netboot.xyz",u"tools and netboot.xyz",CHARON_GROUP(toolsGroup)},
    {u"CharonDOS",u"CharonDOS",CHARON_GROUP(dosGroup)},
    {u"Windows, obrazy nieoficjalne",u"Windows, unofficial images",CHARON_GROUP(unofficialGroup)},   // r409: last, after everything official
};
// A system without speech: the user must say they know.
inline bool ConfirmSilent(const Entry& e) {
    if(e.speech!=Speech::none)return true;
    static char16_t text[500];Set(text,500,Pl()?e.pl:e.en);
    Cat(text,500,T(u": ten system nie mówi. Osoba niewidoma nie obsłuży go bez czyjejś pomocy albo bez własnego czytnika ekranu. Kontynuować mimo to?",
        u": this system does not speak. A blind person cannot use it without someone's help or their own screen reader. Continue anyway?"));
    return Confirm(text);
}
// r409: an unofficial Windows image: the warning, then a typed word (as in ConfirmPlan), so that Enter alone
// cannot pass it. The word is UNOFFICIAL, in Polish NIEOFICJALNY.
inline bool ConfirmUnofficial(const Entry& e) {
    if(!e.unofficial)return true;
    Say(T(u"Uwaga: to jest nieoficjalny obraz Windows. Nie zrobił go Microsoft i Microsoft nie może go zweryfikować; Microsoft nie zezwala na rozpowszechnianie zmienionych nośników Windows. Charon sprawdzi tylko, czy plik jest tym, który opublikował autor, a nie co jest w środku. Może brakować części systemu, także tych potrzebnych osobie niewidomej. To nie jest licencja: potrzebujesz własnej licencji albo klucza Windows. Obraz pochodzi z października 2025 i po instalacji trzeba go zaktualizować. Oficjalny Windows z profilem odchudzonym daje podobny wynik. Aby iść dalej, wpisz słowo NIEOFICJALNY i naciśnij Enter. Escape anuluje.",
          u"Warning: this is an unofficial Windows image. It was not made by Microsoft and Microsoft cannot verify it; Microsoft does not permit redistribution of modified Windows media. Charon can only check that the file is the one its author published, not what is inside. Parts may be missing, including parts a blind user needs. It is not a licence: you need your own Windows licence or key. This image is from October 2025 and must be updated after installation. Official Windows with the slim profile gives a similar result. To go on, type the word UNOFFICIAL and press Enter. Escape cancels."));
    static char16_t word[24];word[0]=0;
    if(!Prompt(T(u"Wpisz NIEOFICJALNY, aby iść dalej:",u"Type UNOFFICIAL to go on:"),word,23,nullptr,nullptr,false))return false;
    if(!SameFold(word,T(u"nieoficjalny",u"unofficial"))) {Say(T(u"Nie wpisano potwierdzenia. Nic nie zostało zmienione.",u"No confirmation was typed. Nothing was changed."));return false;}
    return true;
}
#ifdef HAK_CHARON_HTTPS
inline void Widen(const char* in,char16_t* out,size_t cap) {size_t n=0;for(;in[n] && n+1<cap;++n)out[n]=char16_t(uint8_t(in[n]));out[n]=0;}
inline bool Narrow(const char16_t* in,char* out,size_t cap) {size_t n=0;for(;in[n] && n+1<cap;++n) {if(in[n]<33 || in[n]>126)return false;out[n]=char(in[n]);}out[n]=0;return n>0;}
// What Charon cannot do for a system it only starts: say it plainly.
inline void SayCharonStays() {
    // r409: the advice names the place Charon really runs from: a stick or disc that is still there, or this computer's
    // own disk (Charon then keeps starting from it, unless the new system erases that disk).
    const bool removable=!hakefi::selfram::Active() || hakefi::selfram::OriginalDevice();
    bool internal=false;
    if(removable) {disks::Load();auto& s=disks::D();for(size_t i=0;i<s.count;++i)if(s.list[i].hasCharon && !s.list[i].usb && !s.list[i].removable)internal=true;}
    if(internal)Say(T(u"Uwaga: tego systemu Charon nie instaluje sam, więc nie doda do niego swojego czytnika ekranu ani menedżera rozruchu. Charon zostaje na dysku tego komputera, o ile nowy system nie wymaże tego dysku; po instalacji wybierz w firmware wpis rozruchowy Charon. Instalator Charona dla nowego systemu jest w folderze EFI, HAK, addon.",
          u"Note: Charon does not install this system itself, so it cannot add its screen reader or its boot manager to it. Charon stays on this computer's disk, unless the new system erases that disk; after the installation choose the boot entry Charon in the firmware. Charon's installer for the new system is in the folder EFI, HAK, addon."));
    else Say(T(u"Uwaga: tego systemu Charon nie instaluje sam, więc nie doda do niego swojego czytnika ekranu ani menedżera rozruchu. Po instalacji uruchom komputer z nośnika, z którego uruchomiono Charona; instalator Charona jest w folderze EFI, HAK, addon.",
          u"Note: Charon does not install this system itself, so it cannot add its screen reader or its boot manager to it. After the installation start the computer from the medium Charon was started from; Charon's installer is in the folder EFI, HAK, addon."));
    HearOut(250);
}
// ---- Arch Linux from the official mirror, with speech ---------------------------
inline bool RunArchNet(const Entry& e) {
    if(!EnsureNetwork()) {HearOut(200);return false;}
    if(ramdisk::FreeMemory()<(uint64_t(3)<<30)) {
        Fail(T(u"System instalacyjny Archa działa w całości w pamięci i potrzebuje co najmniej 3 gigabajtów wolnej pamięci RAM.",u"The Arch installation system runs entirely in memory and needs at least 3 gigabytes of free RAM."));return false;}
    static char16_t url[400];static Memory kernel,initrd;
    static char16_t url2[400];   // r410 (agent 3): kernel and initrd at once
    Widen(e.url,url,300);Cat(url,400,u"arch/boot/x86_64/vmlinuz-linux");
    Widen(e.url,url2,300);Cat(url2,400,u"arch/boot/x86_64/initramfs-linux.img");
    if(!FetchPairToMemory(url,kernel,0,T(u"jądro Arch Linux",u"the Arch Linux kernel"),url2,initrd,0,T(u"obraz rozruchowy Arch Linux",u"the Arch Linux boot image")))return false;
    static char16_t arguments[500];Set(arguments,500,u"archiso_http_srv=");{static char16_t base[300];Widen(e.url,base,300);Cat(arguments,500,base);}
    Cat(arguments,500,u" archisobasedir=arch cms_verify=y ip=:::::eth0:dhcp net.ifnames=0 accessibility=on");
    SayCharonStays();
    Say(T(u"Uruchamiam Arch Linux z mową. System pobierze jeszcze około gigabajta danych, a potem odezwie się własnym głosem w konsoli.",
          u"Starting Arch Linux with speech. It downloads about another gigabyte of data and then speaks with its own voice in the console."));
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
    Say(T(u"Uruchamiam netboot.xyz. Jego menu czyta czytnik ekranu Charona.",u"Starting netboot.xyz. Charon's screen reader reads its menu."));HearOut(100);
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
    Fail(T(u"Na zapisanym przed chwilą dysku nie znalazłem programu rozruchowego UEFI. Uruchom komputer ponownie i wybierz ten dysk w menu urządzeń Charona.",
        u"I found no UEFI boot program on the disk just written. Restart the computer and choose this disk in Charon's device menu."));
    return false;
}
// ---- a bootable ISO: onto a USB stick, or into memory --------------------------
inline bool RunIso(const char* address,const char16_t* title,uint64_t bytes,bool linuxLive) {
    if(!EnsureNetwork()) {HearOut(200);return false;}
    const bool ramFits=ramdisk::Find() && ramdisk::Room(bytes?bytes:uint64_t(2)<<30,uint64_t(2)<<30,false);
    static const Choice toUsb={u"zapisz na pendrive, który zostanie wymazany",u"write to a USB stick, which will be erased","usb"};
    static const Choice toRam={u"uruchom z pamięci, jako RAM-dysk",u"start from memory, as a RAM disk","ram"};
    static Choice ways[2];size_t count=0;ways[count++]=toUsb;if(ramFits)ways[count++]=toRam;
    if(ramFits && linuxLive) {
        Say(T(u"Uwaga: system uruchamiany z RAM-dysku musi sam odnaleźć swój nośnik w pamięci; nie każdy Linux to potrafi. Pewniejszy jest pendrive.",
              u"Note: a system started from a RAM disk has to find its media in memory by itself; not every Linux can. A USB stick is the safer choice."));HearOut(200);}
    size_t way=0;
    if(!ask::Choose(T(u"Jak uruchomić ten obraz?",u"How should this image be started?"),ways,count,way))return false;
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
    if(!images::UrlToDisk(address,d,T(u"Obraz na pendrive",u"Image to the USB stick"),&written))return false;
    images::Reconnect(d.handle);
    Play(Icon::done);
    static char16_t text[300];Set(text,300,T(u"Pendrive jest gotowy: ",u"The USB stick is ready: "));Cat(text,300,title);Cat(text,300,T(u". Uruchomić go teraz?",u". Start it now?"));
    if(!Confirm(text))return true;
    return StartDisk(d,title);
}
inline bool RunCustomIso(bool linuxLive) {
    static char16_t wide[1024];static char address[1024];wide[0]=0;
    if(!Prompt(T(u"Adres obrazu ISO, https:",u"Address of the ISO image, https:"),wide,1000))return false;
    if(!Narrow(wide,address,sizeof(address))) {Fail(T(u"Adres zawiera niedozwolone znaki.",u"The address contains characters that are not allowed."));return false;}
    return RunIso(address,T(u"obraz ISO",u"ISO image"),0,linuxLive);
}
// ---- ready system images --------------------------------------------------------------
inline const Question imageQuestions[]={
    {"desktop",true,Kind::multi,u"Pulpity:",u"Desktops:","",CHARON_CHOICES(imageDesktops),u"Gotowy obraz to system bez pulpitu; zaznaczone pulpity pobierze przy pierwszym uruchomieniu. Jeśli nic nie zaznaczysz, zostaje sama konsola z mową.",u"A ready-made image is a system without a desktop; it downloads the checked desktops at its first start. If you check nothing, you get just the console with speech."},
    {"readers",true,Kind::multi,u"Czytniki ekranu:",u"Screen readers:","orca,speakup",CHARON_CHOICES(linuxReaders),CHARON_READER_HELP_PL,CHARON_READER_HELP_EN},
    {"ssh",false,Kind::yesNo,u"Pozwolić na logowanie hasłem przez SSH?",u"Allow signing in with a password over SSH?","no",nullptr,0,nullptr,nullptr},
};
// r410 (ls): the Fedora image gets no desktop and no screen reader (its entry says so: Speech::none, no espeakup or
// Fenrir package), so neither question is asked for it - the reader question offered Speakup after saying there is none.
inline const Question imageQuestionsFedora[]={
    {"ssh",false,Kind::yesNo,u"Pozwolić na logowanie hasłem przez SSH?",u"Allow signing in with a password over SSH?","no",nullptr,0,nullptr,nullptr},
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
        // r408: Arch's groups are the bare desktops; a terminal, an editor, the applets and (LXQt) a window manager are named
        {"kde","task-kde-desktop","kubuntu-desktop","plasma, sddm, konsole, dolphin"},
        {"xfce","task-xfce-desktop","xubuntu-desktop","xfce4, xfce4-goodies, lightdm, lightdm-gtk-greeter"},
        {"mate","task-mate-desktop","ubuntu-mate-desktop","mate, mate-extra, lightdm, lightdm-gtk-greeter"},
        {"cinnamon","task-cinnamon-desktop","ubuntucinnamon-desktop","cinnamon, gnome-terminal, xed, lightdm, lightdm-gtk-greeter"},
        {"lxqt","task-lxqt-desktop","lubuntu-desktop","lxqt, openbox, sddm"},
        // r408: i3 with I38; what I38 itself needs is installed by speech.sh
        {CHARON_I38_VALUE,"i3-wm, lightdm, lightdm-gtk-greeter","i3-wm, lightdm, lightdm-gtk-greeter","i3-wm, lightdm, lightdm-gtk-greeter"}};
    for(const auto& r:rows)if(ask::Same(r.id,id))return distro=='d'?r.debian:distro=='u'?r.ubuntu:distro=='a'?r.arch:"";
    return "";
}
// cloud-init user-data for a ready image: the user, speech, Charon.
inline void ImageUserData(Text& y,bool speech,char distro) {
    y.Line("#cloud-config");
    y.Line("# Written by Charon from spoken answers.");
    y.Line("hostname: \"",ask::Get("hostname"),"\"");   // r409: quoted, as the user name below: no, on, null would be read as YAML truth values
    y.Line("locale: ",ask::Get("locale"),".UTF-8");
    y.Line("timezone: ",ask::Get("timezone"));
    // r405: Debian's image has no console-setup, so cloud-init's keyboard module fails there
    // (and marks the whole first start as failed): the layout file is written directly
    // and applied in runcmd once the package is in.
    if(distro=='d') {
        y.Line("write_files:");
        y.Line("  - path: /etc/default/keyboard");
        y.Line("    content: |");
        y.Line("      XKBMODEL=\"pc105\"");
        y.Line("      XKBLAYOUT=\"",ask::Get("keyboard"),"\"");
        y.Line("      XKBVARIANT=\"\"");
        y.Line("      XKBOPTIONS=\"\"");
        y.Line("      BACKSPACE=\"guess\"");
    } else y.Line("keyboard: {layout: ",ask::Get("keyboard"),"}");
    // r405: Debian's locale-gen takes no arguments, so cloud-init never generated a
    // language other than English there and the system (and its speech) stayed English.
    // The language is generated before cloud-init's own locale step.
    y.Line("bootcmd:");
    y.Line("  - [sh, -c, 'L=",ask::Get("locale"),".UTF-8; if [ -f /etc/locale.gen ] && ! grep -q \"^$L\" /etc/locale.gen; then echo \"$L UTF-8\" >> /etc/locale.gen; locale-gen >/dev/null 2>&1; fi; true']");
    y.Line("users:");
    y.Line("  - name: \"",ask::Get("user"),"\"");
    y.Line("    gecos: \"",ask::Get("fullname"),"\"");
    y.Line("    passwd: \"",ask::Get("hash"),"\"");
    y.Line("    lock_passwd: false");
    y.Line("    groups: [sudo, wheel, audio, adm]");
    y.Line("    sudo: \"ALL=(ALL) ALL\"");   // Arch and Fedora give the groups no sudo rule by themselves
    y.Line("    shell: /bin/bash");
    y.Line("ssh_pwauth: ",ask::Is("ssh","yes")?"true":"false");
    y.Line("growpart: {mode: auto, devices: ['/']}");
    // r406: no package list for cloud-init: it would install speech and a whole desktop in
    // one silent step, and one missing package would cancel all of them. Charon's own
    // first-start script does it in the order a blind person needs: progress tones at
    // once, speech first, the desktop last (src/linux/firstboot.sh).
    const bool desktops=ask::Any("desktop") && distro!='f';
    static char list[400];size_t n=0;list[0]=0;
    if(desktops)for(const auto& c:imageDesktops)if(ask::Has("desktop",c.value)) {
        for(const char* p=DesktopPackages(c.value,distro);*p && n+2<sizeof(list);++p) {if(*p==' ')continue;list[n++]=*p;}
        if(n && list[n-1]!=',' && n+2<sizeof(list))list[n++]=',';}
    while(n && list[n-1]==',')--n;
    list[n]=0;
    static char args[200];
    if(speech)ReaderArguments(args,sizeof(args));else {const char* none=ask::Is("login","console")?"none none none console":"none none none graphical";size_t i=0;for(;none[i];++i)args[i]=none[i];args[i]=0;}
    y.Line("runcmd:");
    // The EFI partition holds Charon and its scripts (CharonOnto); it may be mounted at /boot/efi or /efi, or not yet.
    y.Add("  - [sh, -c, 'e=/boot/efi; [ -d /efi/EFI/HAK ] && e=/efi; if [ ! -d $e/EFI/HAK ]; then mkdir -p /run/charon-esp; for d in $(blkid -t TYPE=vfat -o device); do mount $d /run/charon-esp 2>/dev/null || continue; [ -d /run/charon-esp/EFI/HAK ] && { e=/run/charon-esp; break; }; umount /run/charon-esp; done; fi; mkdir -p /var/lib/charon; "
          "for f in install-charon.sh speech.sh progress.sh firstboot.sh network.sh charon-tone voice.conf network.conf; do cp $e/EFI/HAK/$f /var/lib/charon/ 2>/dev/null; done; chmod 600 /var/lib/charon/network.conf 2>/dev/null; sed -i \"s|^esp=.*|esp=$e|\" /var/lib/charon/install-charon.sh; "
          "sh /var/lib/charon/firstboot.sh setup ");
    y.Add(args);y.Add(" ");y.Add(list[0]?list:"none");y.Add(ask::Is("dual","yes")?" yes":" no");y.Line("; true']");
    // r405: the images name the serial port as the last console, which makes it the main
    // one: on a computer the start-up messages and the emergency prompt then go to a port
    // nobody reads, and a console reader has nothing to read. The screen becomes the main
    // console; the serial port keeps getting the kernel's messages.
    y.Line("  - [sh, -c, 'c=0; for f in /etc/default/grub /etc/default/grub.d/*.cfg; do [ -f \"$f\" ] && grep -q \"console=tty[01] console=ttyS0\" \"$f\" && { sed -i \"s/console=tty\\([01]\\) console=ttyS0\\(,[0-9n]*\\)\\{0,1\\}/console=ttyS0\\2 console=tty\\1/\" \"$f\"; c=1; }; done; [ $c = 1 ] && { if command -v update-grub >/dev/null 2>&1; then update-grub; elif command -v grub2-mkconfig >/dev/null 2>&1; then grub2-mkconfig -o /boot/grub2/grub.cfg; else grub-mkconfig -o /boot/grub/grub.cfg; fi; } >/dev/null 2>&1; true']");   // r409: Fedora's command is grub2-mkconfig (docs.fedoraproject.org, quick-docs, grub2-bootloader)
    if(distro=='d')
        y.Line("  - [sh, -c, 'systemctl restart keyboard-setup.service console-setup.service >/dev/null 2>&1; true']");
    if(detailed::Carried())   // r410: the detailed answers; the file with them leaves the EFI partition
        y.Line("  - [sh, -c, 'for e in /boot/efi /efi /run/charon-esp; do [ -f $e/EFI/HAK/answers.env ] && break; done; if [ -f $e/EFI/HAK/answers.env ]; then cp $e/EFI/HAK/answers.env $e/EFI/HAK/personal.sh /var/lib/charon/; chmod 600 /var/lib/charon/answers.env; rm -f $e/EFI/HAK/answers.env; sh /var/lib/charon/personal.sh install now; fi; true']");
    y.Line("  - [sh, -c, 'sh /var/lib/charon/firstboot.sh done; true']");
}
// r405: a ready image brings no firmware boot entry: nothing in it ever ran
// grub-install on this computer. Without one the firmware started whatever
// else it had (the test machine: the UEFI Shell) once Charon's stick was
// taken out. Charon therefore writes the entries itself, right after the
// image: the system's own loader first (beside other systems: after Charon,
// which shows them all), the reader add-on as a driver, and the Charon boot
// manager after the system. The system's first start repeats the Charon part
// with efibootmgr (install-charon.sh), which is harmless.
inline const char16_t* ImageLoader(efi_handle esp) {
    static const char16_t* const loaders[]={u"\\EFI\\debian\\shimx64.efi",u"\\EFI\\ubuntu\\shimx64.efi",u"\\EFI\\fedora\\shimx64.efi",u"\\EFI\\debian\\grubx64.efi",
        u"\\EFI\\arch\\grubx64.efi",u"\\EFI\\GRUB\\grubx64.efi",u"\\EFI\\systemd\\systemd-bootx64.efi",u"\\EFI\\BOOT\\BOOTX64.EFI"};
    for(const auto* loader:loaders) {
        FileRef ref;ref.volume=esp;Set(ref.path,pathUnits,loader);efi_file_protocol* file=nullptr;
        if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS || !file)continue;
        file->Close(file);return loader;
    }
    return nullptr;
}
inline bool RegisterImage(efi_handle esp,const Entry& e,bool beside,bool charon) {
    if(!esp)return false;
    const char16_t* loader=ImageLoader(esp);
    if(!loader)return false;
    const char distro=DistroOf(e);
    const char16_t* label=distro=='d'?u"Debian":distro=='u'?u"Ubuntu":distro=='a'?u"Arch Linux":distro=='f'?u"Fedora":u"Linux";
    efi_status status=dossys::PutOption(false,label,esp,loader,true,nullptr,nullptr,true);
    charontrace::Value("CHARON image boot entry",uint64_t(status));
    if(status!=EFI_SUCCESS)return false;
    if(charon) {
        status=dossys::PutOption(true,u"Charon voice",esp,u"\\EFI\\HAK\\CHARONVOICE.EFI",true,nullptr,nullptr,true);
        charontrace::Value("CHARON image voice entry",uint64_t(status));
        // beside other systems Charon's manager comes first: it lists every system
        const efi_status manager=dossys::PutOption(false,u"Charon",esp,u"\\EFI\\HAK\\HAKBOOT.EFI",beside,beside?nullptr:label,nullptr,true);
        charontrace::Value("CHARON image manager entry",uint64_t(manager));
        if(status!=EFI_SUCCESS || manager!=EFI_SUCCESS)return false;
    }
    return true;
}
// r410 (ls): a ready image whose own EFI partition is too small for Charon (Fedora's: 100 MB, about 80 MB of Charon
// did not fit and the copy stopped part-way). Only on a whole disk, after SeedOnto (backup table at the end, CIDATA
// as the last partition): a new EFI partition of 256 MiB is made just before CIDATA (the root partition can still
// grow up to it), formatted FAT16 (as the image's own: Fedora's is FAT16 too) with the SAME volume serial number,
// so the image's fstab line "UUID=XXXX-XXXX /boot/efi" finds it; the image's \EFI is copied onto it; then the old
// EFI partition is taken out of the table (its 100 MB stay unused). Every step before the removal leaves the old one
// as it was: on any failure the new partition is removed again and null is returned (the caller then copies to the
// old partition, which fails and is said, as before). Returns the new volume.
inline efi_handle BiggerEsp(const disks::Disk& d,uint64_t wantBytes) {
    using namespace seeddisk;
    if(d.blockSize!=512 || !d.io)return nullptr;
    Disk disk{d.io,d.blockSize,d.blocks,images::SeedRead,images::SeedWrite};uint8_t* work=images::SeedWork();
    static Partition parts[16];Info info{};
    if(Inspect(disk,work,kWorkBytes,parts,16,info)!=Error::None || !info.backupAtEnd)return nullptr;
    const Partition* old=nullptr;const Partition* seed=nullptr;uint64_t below=0;
    for(size_t i=0;i<info.partitionCount;++i) {
        const auto& p=parts[i];
        if(detail::Same(p.type.bytes,kTypeEfiSystem.bytes,16) && !old)old=&p;
        if(p.name[0]==u'c' && p.name[1]==u'i' && p.name[2]==u'd' && p.name[3]==u'a' && p.name[4]==u't' && p.name[5]==u'a' && !p.name[6])seed=&p;
    }
    if(!old || !seed)return nullptr;
    if((old->lastLba-old->firstLba+1)*512>=wantBytes)return nullptr;   // already big enough
    for(size_t i=0;i<info.partitionCount;++i)if(&parts[i]!=seed && parts[i].lastLba>below)below=parts[i].lastLba;
    const uint64_t count=wantBytes/512,first=(seed->firstLba-count)/2048*2048;
    if(seed->firstLba<count+2048 || first<=below)return nullptr;
    static uint8_t boot[512];
    if(!images::SeedRead(d.io,old->firstLba,boot,1) || boot[510]!=0x55 || boot[511]!=0xAA)return nullptr;
    const bool fat32=boot[22]==0 && boot[23]==0;const uint8_t* sn=boot+(fat32?67:39);
    FatOptions options{};options.sectorSize=512;options.hiddenSectors=uint32_t(first);
    options.serial=uint32_t(sn[0])|uint32_t(sn[1])<<8|uint32_t(sn[2])<<16|uint32_t(sn[3])<<24;
    const Guid oldUnique=old->unique;const uint64_t oldFirst=old->firstLba;
    static char16_t name[37];for(int k=0;k<37;++k)name[k]=old->name[k];
    AddRequest request{};request.type=kTypeEfiSystem;images::RandomGuid(request.unique,3);request.name=name[0]?name:u"EFI System Partition";
    request.sizeBytes=count*512;request.alignmentBytes=0;request.placement=Placement::At;request.firstLba=first;request.attributes=0;
    AddResult added{};
    Error error=AddPartition(disk,work,kWorkBytes,request,added);
    charontrace::Value("CHARON bigger esp added",(uint64_t(error)<<56)|added.firstLba);
    if(error!=Error::None)return nullptr;
    const Guid unique=request.unique;
    auto undo=[&]() {const auto e2=RemovePartition(disk,work,kWorkBytes,unique,kTypeEfiSystem,first);charontrace::Value("CHARON bigger esp undone",uint64_t(e2));images::Reconnect(d.handle);return efi_handle(nullptr);};
    static Memory memory;memory.reserved=false;memory.extra=0;
    if(!Reserve(memory,size_t(count*512)))return undo();
    FatInfo fat{};
    error=BuildFat(memory.data,size_t(count*512),"EFI",nullptr,0,options,&fat);
    if(error==Error::None)error=WriteImage(disk,first,added.lbaCount,memory.data,size_t(1+2*fat.fatSectors+kFatRootEntries*32/512)*512);
    Free(memory);
    if(d.io->FlushBlocks)reinterpret_cast<images::BlockFlush>(d.io->FlushBlocks)(d.io);
    charontrace::Value("CHARON bigger esp fat",uint64_t(error));
    if(error!=Error::None)return undo();
    images::Reconnect(d.handle);
    efi_handle to=images::VolumeOfPartition(unique.bytes),from=images::VolumeOfPartition(oldUnique.bytes);
    if(!to || !from)return undo();
    FileRef a,b;a.volume=from;b.volume=to;Set(a.path,pathUnits,u"\\EFI");Set(b.path,pathUnits,u"\\EFI");size_t files=0;
    const efi_status copied=CopyTree(a,b,0,&files);
    charontrace::Value("CHARON bigger esp copied",(uint64_t(copied)<<32)|files);
    if(copied!=EFI_SUCCESS || !files)return undo();
    error=RemovePartition(disk,work,kWorkBytes,oldUnique,kTypeEfiSystem,oldFirst);
    charontrace::Value("CHARON bigger esp old removed",uint64_t(error));
    if(error!=Error::None)return undo();
    images::Reconnect(d.handle);
    return images::VolumeOfPartition(unique.bytes);
}
inline bool RunImage(const Entry& e) {
    if(!EnsureNetwork()) {HearOut(200);return false;}
    size_t mode=0;
    static const Choice imageModes[]={{u"tryb ekspresowy: jak najmniej pytań",u"express mode: as few questions as possible","quick"},{u"tryb szczegółowy: więcej pytań",u"detailed mode: more questions","detailed"}};
    const bool seeded=e.way!=Way::rawNoSeed;
    if(seeded) {
        if(!ask::Choose(T(u"Który tryb?",u"Which mode?"),CHARON_CHOICES(imageModes),mode))return false;
        if(Pl() && !ask::Get("locale")[0])ask::Put("locale","pl_PL");   // r405: Charon speaking Polish offers Polish first
        {const char distro=DistroOf(e);detailed::Begin('l',distro=='d'?'D':distro=='u'?'U':distro=='a'?'A':'F');}   // r410
        const bool fedora=DistroOf(e)=='f';
        const Question* own=fedora?imageQuestionsFedora:imageQuestions;
        const size_t owns=fedora?sizeof(imageQuestionsFedora)/sizeof(imageQuestionsFedora[0]):sizeof(imageQuestions)/sizeof(imageQuestions[0]);
        detailed::SetMain(commonQuestions,sizeof(commonQuestions)/sizeof(commonQuestions[0]),own,owns);
        if(mode==1 && !sets::Offer())return false;
        if(!ask::Run(commonQuestions,sizeof(commonQuestions)/sizeof(commonQuestions[0]),mode==0))return false;
        if(!ask::Run(own,owns,mode==0))return false;
        if(fedora) {ask::Put("desktop","");ask::Put("readers","");}   // r410: not asked (above)
        if(!ResolveReaders(ask::Any("desktop")))return false;
        DeriveDefaults();HashPassword();
        if(!ask::Get("hash")[0]) {Fail(T(u"Nie udało się przygotować hasła.",u"The password could not be prepared."));return false;}
        // r410 (ls): the Fedora image's own EFI partition (100 MB) cannot hold Charon's files; on a whole disk Charon
        // gives the system a bigger one (BiggerEsp), so the detailed questions and the software sets are asked for it
        // too. Beside other systems that is not done: said now, before the questions.
        if(mode==1 && fedora) {
            Say(T(u"Obraz Fedory ma partycję EFI o rozmiarze tylko 100 megabajtów, w której nie mieszczą się pliki Charona. Jeśli wymażesz cały dysk, Charon da Fedorze nową partycję EFI o rozmiarze 256 megabajtów i przeniesie na nią jej pliki rozruchowe; odpowiedzi szczegółowe i zestawy programów zadziałają wtedy jak w innych systemach. Obok innych systemów tego nie robię i wtedy system zainstaluje się z ustawieniami domyślnymi.",
                  u"The Fedora image has an EFI partition of only 100 megabytes, which cannot hold Charon's files. If you erase the whole disk, Charon gives Fedora a new EFI partition of 256 megabytes and moves its boot files onto it; the detailed answers and the software sets then work as in the other systems. Beside other systems I do not do that, and the system is then installed with its default settings."));
            HearOut(200);
        }
        if(mode==1 && !detailed::Run())return false;   // r410: the groups of the detailed mode
        // r409: before any disk is chosen or written: every answer was kept and the configuration fits its file
        // (it is written only after the image is on the disk, when it is too late to find out).
        if(!ask::Complete())return false;
        {static char probe[32000-256];Text y{probe,sizeof(probe),0,false};probe[0]=0;ImageUserData(y,ask::Is("speech","yes") && e.speech!=Speech::none,DistroOf(e));
         const bool fits=!y.overflow;for(auto& c:probe)c=0;
         if(!fits) {AnswersDoNotFit();return false;}}
    }
    size_t index=0;auto& remap=images::R();remap=images::Remap{};
    // r402: beside the systems already there (dual boot), or the whole disk.
    static const Choice places[]={{u"wymaż cały wybrany dysk",u"erase the whole disk you choose","whole"},
        {u"obok obecnych systemów: użyj tylko wolnego miejsca, niczego nie wymazuj",u"alongside the existing systems: use free space only, erase nothing","free"}};
    size_t place=0;
    if(seeded && !ask::Choose(T(u"Co zrobić z dyskiem?",u"What should be done with the disk?"),CHARON_CHOICES(places),place))return false;
    if(place==1 && e.wholeOnly) {
        Fail(T(u"Ten obraz ma w programie rozruchowym wpisany numer partycji, więc działa tylko na całym dysku; w testach nie uruchomił się obok innych systemów. Do instalacji obok innego systemu wybierz obraz Debiana, Ubuntu albo Fedory.",
            u"This image's boot program refers to its partition by number, so it works on a whole disk only; in tests it did not start alongside other systems. To install alongside another system choose the Debian, Ubuntu or Fedora image."));
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
            static char16_t text[500];Set(text,500,T(u"Żaden dysk z tablicą GPT nie ma dość wolnego miejsca poza partycjami; potrzeba około ",u"No disk with a GPT table has enough free space outside its partitions; needed: about "));
            systools::PutSize(text,500,e.bytes+(uint64_t(64)<<20),Pl());
            Cat(text,500,T(u". W Windows zwolnisz miejsce skryptem Charon MakeRoom z folderu Charona albo w Zarządzaniu dyskami; Charon sam nie zmniejsza partycji innych systemów.",
                u". In Windows you can free space with the Charon MakeRoom script from Charon's folder, or in Disk Management; Charon itself does not shrink other systems' partitions."));
            Fail(text);return false;}
        size_t chosen=0;
        if(!ask::Choose(T(u"Na którym dysku?",u"On which disk?"),list,count,chosen))return false;
        index=map[chosen];const auto& c=s.list[index];
        static char16_t text[900];static char16_t label[300];disks::DiskLabel(c,index,label,300);
        Set(text,900,T(u"Nic nie zostanie wymazane. System ",u"Nothing will be erased. The system "));Cat(text,900,Pl()?e.pl:e.en);
        Cat(text,900,T(u" zajmie wolne miejsce, ",u" will take the free space, "));systools::PutSize(text,900,c.freeBlocks*c.blockSize,Pl());Cat(text,900,T(u", na dysku: ",u", on the disk: "));Cat(text,900,label);
        Cat(text,900,T(u". Dostanie własną partycję rozruchową. Po instalacji pierwszy w kolejności rozruchu będzie menedżer rozruchu Charon, który pokazuje oba systemy. Uwaga: jeśli dysk z Windows jest zaszyfrowany funkcją BitLocker, po zmianie kolejności rozruchu Windows może zapytać o klucz odzyskiwania. Aby zacząć, wpisz słowo TAK i naciśnij Enter.",
            u". It gets its own boot partition. After the installation the Charon boot manager, which shows both systems, will be first in the boot order. Note: if the Windows disk is encrypted with BitLocker, Windows may ask for the recovery key after the boot order changes. To start, type the word YES and press Enter."));
        Say(text);
        static char16_t word[16];word[0]=0;
        if(!Prompt(T(u"Wpisz TAK, aby zacząć:",u"Type YES to start:"),word,15,nullptr,nullptr,false))return false;
        if(!SameFold(word,T(u"tak",u"yes"))) {Say(T(u"Nie wpisano potwierdzenia. Nic nie zostało zmienione.",u"No confirmation was typed. Nothing was changed."));return false;}
        remap.on=true;remap.disk=&c;remap.gapFirst=c.freeFirst*c.blockSize;remap.gapEnd=(c.freeFirst+c.freeBlocks)*c.blockSize;
        ask::Put("dual","yes");
    } else {
        if(!images::ChooseDisk(e.bytes+(seeded?uint64_t(1)<<30:0),false,index))return false;
        if(!images::ConfirmErase(disks::D().list[index],index,Pl()?e.pl:e.en))return false;
    }
    const auto& d=disks::D().list[index];
    if(seeded && U().host->ensureReader && !U().host->ensureReader(U().host->context))
        Say(T(u"Uwaga: nie udało się włączyć dodatku czytnika ekranu w firmware.",u"Note: the screen reader add-on could not be turned on in the firmware."));
    uint64_t written=0;const char16_t* what=T(u"Obraz na dysk",u"Image to the disk");
    const bool ownHome=d.hasCharon && !remap.on;   // r409: Charon's own disk is erased whole: Charon is carried back with its answer sets and plan state
    const bool wrote=e.way==Way::qcow?images::QcowToDisk(e.url,d,what,&written):images::UrlToDisk(e.url,d,what,&written);
    if(!wrote) {remap.on=false;return false;}
    bool configured=true,charon=false;
    if(seeded) {
        static char userData[32000];static char metaData[200];   // r409: was 9000; static, not on the stack
Text y{userData,sizeof(userData),0,false};userData[0]=0;
        const bool speech=ask::Is("speech","yes") && e.speech!=Speech::none;
        ImageUserData(y,speech,DistroOf(e));
        Text m{metaData,sizeof(metaData),0,false};metaData[0]=0;
        {uint32_t lo,hi;__asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));m.Add("instance-id: charon-");m.Number(lo);m.Add("\n");}
        m.Line("local-hostname: \"",ask::Get("hostname"),"\"");
        Say(T(u"Obraz jest już na dysku. Dodaję konfigurację: konto użytkownika, mowę i Charona.",u"The image is on the disk. Adding the configuration: the user account, speech and Charon."));
        configured=!y.overflow && images::SeedOnto(d,userData,y.size,metaData,m.size);
        for(auto& c:userData)c=0;
        if(!configured)Fail(T(u"Nie udało się zapisać konfiguracji na dysku. System się uruchomi, ale bez konta użytkownika i bez mowy.",
            u"The configuration could not be written to the disk. The system will start, but with no user account and no speech."));
    }
    images::Reconnect(d.handle);
    if(seeded) {
        // r410 (ls): Fedora on a whole disk gets a bigger EFI partition first (BiggerEsp); beside other systems its
        // 100 MB partition stays and Charon is not copied there at all (a copy that stops part-way fills it).
        const bool fedora=DistroOf(e)=='f';
        efi_handle esp=remap.on?(remap.hasEsp?images::VolumeOfPartition(remap.espUnique):nullptr):nullptr;
        if(!remap.on && fedora) {
            Say(T(u"Daję Fedorze większą partycję EFI, 256 megabajtów, i przenoszę na nią jej pliki rozruchowe.",u"Giving Fedora a bigger EFI partition, 256 megabytes, and moving its boot files onto it."));
            esp=BiggerEsp(d,uint64_t(256)<<20);
            if(!esp)Say(T(u"Uwaga: nie udało się zrobić większej partycji EFI; Fedora zostaje ze swoją partycją 100 megabajtów.",u"Note: the bigger EFI partition could not be made; Fedora keeps its 100-megabyte partition."));
        }
        if(!remap.on && !esp)esp=images::EspOn(d);
        charon=esp && !(fedora && remap.on) && images::CharonOnto(esp,ownHome) && WriteText(esp,u"\\EFI\\HAK\\speech.sh",speechScript,sizeof(speechScript)-1)
            && WriteText(esp,u"\\EFI\\HAK\\progress.sh",progressScript,sizeof(progressScript)-1)
            && WriteText(esp,u"\\EFI\\HAK\\firstboot.sh",firstbootScript,sizeof(firstbootScript)-1)
            && WriteText(esp,u"\\EFI\\HAK\\network.sh",networkScript,sizeof(networkScript)-1)   // r409: the connection chosen in Charon (network.conf, when Charon writes one) and the wait for the network
            && WriteText(esp,u"\\EFI\\HAK\\charon-tone",reinterpret_cast<const char*>(charonToneBinary),sizeof(charonToneBinary));
        // r409: the Wi-Fi connection joined in Charon, for this installation only (firstboot.sh applies it and removes
        // the file from the EFI partition; CharonPrivate.h keeps it out of every later copy of \EFI\HAK)
        if(charon) {static char conf[400];const size_t bytes=NetworkConfNow(conf,sizeof(conf));
            if(bytes && !WriteText(esp,u"\\EFI\\HAK\\network.conf",conf,bytes))
                Say(T(u"Uwaga: nie udało się przekazać nowemu systemowi ustawień sieci Wi-Fi; połączysz się w nim ręcznie albo kablem.",u"Note: the Wi-Fi settings could not be handed to the new system; connect there by hand or with a cable."));
            for(auto& c:conf)c=0;
            net::ForgetJoined();}   // r409: handed over (or said that it could not be); the passphrase does not stay in memory
        if(charon && detailed::Carried()) {static char env[16384];const size_t n=detailed::Env(env,sizeof(env));   // r410: runcmd moves both to /var/lib/charon
            if(!n || !WriteText(esp,u"\\EFI\\HAK\\answers.env",env,n) || !WriteText(esp,u"\\EFI\\HAK\\personal.sh",personalScript,sizeof(personalScript)-1))
                Say(T(u"Uwaga: nie udało się zapisać odpowiedzi szczegółowych; system zainstaluje się z ustawieniami domyślnymi.",u"Note: the detailed answers could not be written; the system will be installed with the default settings."));
            for(auto& c:env)c=0;}
        if(charon)WriteVoiceConf(esp,u"\\EFI\\HAK\\voice.conf");   // r409: runcmd copies it to /var/lib/charon for speech.sh
        if(!charon)Say(T(u"Uwaga: nie udało się skopiować Charona na partycję systemową EFI nowego systemu; czytnik ekranu i menedżer rozruchu Charona zostają tylko tam, skąd Charon został uruchomiony. Jeśli Charon działa teraz wyłącznie z pamięci, przed ponownym uruchomieniem komputera wybierz w środowisku Charona instalację Charona na tym komputerze.",
            u"Note: Charon could not be copied to the new system's EFI system partition; Charon's screen reader and boot manager stay only where Charon was started from. If Charon now runs from memory alone, choose to install Charon on this computer, in the Charon workspace, before the computer is restarted."));   // r409: no "USB stick" when Charon lives on the internal disk
        if(!RegisterImage(esp,e,remap.on,charon))
            Say(T(u"Uwaga: nie udało się zapisać wpisu rozruchowego w firmware. Jeśli komputer nie uruchomi nowego systemu sam, wybierz jego dysk w menu rozruchu firmware albo w menu urządzeń Charona.",
                u"Note: the boot entry could not be written to the firmware. If the computer does not start the new system by itself, choose its disk in the firmware's boot menu or in Charon's device menu."));
    } else SayCharonStays();
    ask::Clear();
    Play(Icon::done);
    static char16_t text[500];Set(text,500,T(u"Gotowe: ",u"Done: "));Cat(text,500,Pl()?e.pl:e.en);
    if(seeded && configured)Cat(text,500,T(u". Przy pierwszym uruchomieniu system sam się skonfiguruje i pobierze pakiety mowy; potrwa to kilka minut i wymaga sieci.",
        u". At its first start the system sets itself up and downloads the speech packages; that takes a few minutes and needs the network."));
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
        Fail(T(u"Nie znalazłem programu rozruchowego nowego systemu. Uruchom komputer ponownie i wybierz go w menu urządzeń Charona.",u"I did not find the new system's boot program. Restart the computer and choose it in Charon's device menu."));
        return false;
    }
    return StartDisk(d,Pl()?e.pl:e.en);
}
#endif
inline void Classic(size_t system);
// r409: several systems in one go. CharonWsPlanDialogue.h puts its entry question here when the
// workspace opens; it returns true when it took the dialogue over. Null, or false: one system, as always.
inline bool (*&PlanDialogue())() {static bool (*entry)()=nullptr;return entry;}
inline void Dialogue() {
    if(PlanDialogue() && PlanDialogue()())return;   // r409
    ask::Clear();
    Say(T(u"Instalacja systemu z sieci. Najpierw wybierz grupę, potem system. Przy każdym systemie usłyszysz, czy ma on mowę. Na dysku nic się nie zmieni, dopóki nie wpiszesz potwierdzenia. Escape cofa o jeden krok.",
        u"Installing a system from the network. First choose a group, then a system. For every system you will hear whether it speaks. Nothing on a disk changes until you type a confirmation. Escape goes back one step."));
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
            Cat(labels[i][0],200,s==Speech::installer?u"mówi w instalatorze":s==Speech::afterInstall?u"mówi po instalacji":s==Speech::charon?u"czyta go czytnik ekranu Charona":u"bez mowy");
            Cat(labels[i][1],200,s==Speech::installer?u"speaks in the installer":s==Speech::afterInstall?u"speaks after installation":s==Speech::charon?u"read by Charon's screen reader":u"no speech");
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
            Fail(T(u"Ta wersja Charona nie obsługuje HTTPS.",u"This version of Charon does not support HTTPS."));
#endif
            HearOut(200);continue;}
        if(e.way==Way::dosApps) {
#ifdef HAK_CHARON_HTTPS
            dosapps::Run();
#else
            Fail(T(u"Ta wersja Charona nie obsługuje HTTPS.",u"This version of Charon does not support HTTPS."));HearOut(200);
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
            if(!started) {Fail(T(u"Na żadnym dysku nie znalazłem pakietu CharonDOS; powinien być w folderze EFI, HAK, packages, CharonDOS.",u"I did not find the CharonDOS package on any disk; it should be in the folder EFI, HAK, packages, CharonDOS."));HearOut(200);continue;}
            return;}
        if(!ConfirmSilent(e))continue;
        if(!ConfirmUnofficial(e)) {HearOut(100);continue;}   // r409
        UnofficialChosen()=e.unofficial;
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
        Fail(T(u"Ta wersja Charona nie obsługuje HTTPS, więc nie pobierze tej pozycji.",u"This version of Charon does not support HTTPS, so it cannot download this item."));HearOut(200);
#endif
    }
}
}  // namespace install
}  // namespace ws
}  // namespace hakefi
