#pragma once
// r400: Charon workspace - installing an operating system from the network
// ("cloud install"). Charon asks questions by voice (quick: the few that
// matter; detailed: everything the installer lets one set), downloads the
// OFFICIAL network installer over HTTP, writes the installer's own answer
// file from the answers and starts the installer. Charon never writes a
// partition table itself: erasing a disk, replacing a partition or using free
// space is done by the operating system's installer, exactly as chosen and
// confirmed here by typing a word. The answer file also carries Charon's
// screen reader add-on and the script that registers it, so the boot manager
// the new system installs (GRUB, Windows Boot Manager) speaks from the first
// start; the boot order is left to the installed system.
// GPL-2.0-or-later.
#include "CharonWsAsk.h"
#include "CharonWsDisks.h"
#include "CharonWsFetch.h"
#include "CharonWsRamDisk.h"
#include "CharonWsReaders.h"
namespace hakefi {
namespace ws {
void StartProgram(efi_handle volume,const char16_t* path,const char16_t* title,const char16_t* args);   // CharonWorkspace.h
namespace install {
using ask::Choice;using ask::Kind;using ask::Question;
// A growing ASCII/UTF-8 text.
struct Text {
    char* data;size_t capacity,size;bool overflow;
    void Add(const char* s) {while(*s) {if(size+1>=capacity) {overflow=true;return;}data[size++]=*s++;}data[size]=0;}
    void Number(uint64_t v) {char d[24];size_t n=0;do {d[n++]=char('0'+v%10);v/=10;}while(v);while(n) {char c[2]={d[--n],0};Add(c);}}
    void Line(const char* a,const char* b=nullptr,const char* c=nullptr,const char* d=nullptr) {Add(a);if(b)Add(b);if(c)Add(c);if(d)Add(d);Add("\n");}
};
inline const Choice systems[]={{u"Debian (stabilny)",u"Debian (stable)","debian"},{u"Ubuntu (LTS)",u"Ubuntu (LTS)","ubuntu"},{u"Windows 11",u"Windows 11","windows"}};
inline const Choice modes[]={
    {u"automatycznie, szybkie pytania",u"automatic, quick questions","quick"},
    {u"automatycznie, dokładne pytania",u"automatic, detailed questions","detailed"},
    {u"z przewodnikiem: sam prowadzę instalator",u"guided: I drive the installer myself","guided"}};
inline const Choice locales[]={
    {u"polski",u"Polish","pl_PL"},{u"angielski, Stany Zjednoczone",u"English, United States","en_US"},{u"angielski, Wielka Brytania",u"English, United Kingdom","en_GB"},
    {u"niemiecki",u"German","de_DE"},{u"francuski",u"French","fr_FR"},{u"hiszpański",u"Spanish","es_ES"},{u"włoski",u"Italian","it_IT"},
    {u"ukraiński",u"Ukrainian","uk_UA"},{u"czeski",u"Czech","cs_CZ"}};
inline const Choice keyboards[]={
    {u"polska",u"Polish","pl"},{u"amerykańska",u"US","us"},{u"brytyjska",u"UK","gb"},{u"niemiecka",u"German","de"},{u"francuska",u"French","fr"},
    {u"hiszpańska",u"Spanish","es"},{u"włoska",u"Italian","it"},{u"ukraińska",u"Ukrainian","ua"},{u"czeska",u"Czech","cz"}};
inline const Choice zones[]={
    {u"Warszawa",u"Warsaw","Europe/Warsaw"},{u"Londyn",u"London","Europe/London"},{u"Berlin",u"Berlin","Europe/Berlin"},{u"Paryż",u"Paris","Europe/Paris"},
    {u"Madryt",u"Madrid","Europe/Madrid"},{u"Rzym",u"Rome","Europe/Rome"},{u"Kijów",u"Kyiv","Europe/Kyiv"},{u"Praga",u"Prague","Europe/Prague"},
    {u"Nowy Jork",u"New York","America/New_York"},{u"Chicago",u"Chicago","America/Chicago"},{u"Denver",u"Denver","America/Denver"},
    {u"Los Angeles",u"Los Angeles","America/Los_Angeles"},{u"czas uniwersalny UTC",u"universal time UTC","UTC"}};
inline const Choice diskModes[]={
    {u"wymaż cały wybrany dysk",u"erase a whole chosen disk","whole"},
    {u"zastąp jedną wybraną partycję",u"replace one chosen partition","partition"},
    {u"użyj tylko wolnego miejsca, nic nie kasuj",u"use free space only, erase nothing","free"}};
inline const Choice mirrors[]={{u"automatyczny (deb.debian.org)",u"automatic (deb.debian.org)","deb.debian.org"},{u"Polska",u"Poland","ftp.pl.debian.org"},
    {u"Niemcy",u"Germany","ftp.de.debian.org"},{u"Wielka Brytania",u"United Kingdom","ftp.uk.debian.org"},{u"Stany Zjednoczone",u"United States","ftp.us.debian.org"}};
#define CHARON_CHOICES(list) list,sizeof(list)/sizeof(list[0])
// What every system asks. quick = asked in the quick mode too.
inline const Question commonQuestions[]={
    {"locale",true,Kind::choice,u"Język systemu:",u"System language:","en_US",CHARON_CHOICES(locales),nullptr,nullptr},
    {"keyboard",false,Kind::choice,u"Układ klawiatury:",u"Keyboard layout:","",CHARON_CHOICES(keyboards),u"W trybie szybkim klawiatura wynika z języka.",u"In the quick mode the keyboard follows the language."},
    {"timezone",false,Kind::choice,u"Strefa czasowa:",u"Time zone:","",CHARON_CHOICES(zones),nullptr,nullptr},
    {"hostname",false,Kind::name,u"Nazwa komputera:",u"Computer name:","charon-pc",nullptr,0,u"Małe litery, cyfry i myślnik.",u"Lower-case letters, digits and a hyphen."},
    {"fullname",false,Kind::text,u"Imię i nazwisko użytkownika:",u"User's full name:","",nullptr,0,nullptr,nullptr},
    {"user",true,Kind::name,u"Nazwa użytkownika:",u"User name:","user",nullptr,0,nullptr,nullptr},
    {"password",true,Kind::secret,u"Hasło użytkownika:",u"User's password:","",nullptr,0,nullptr,nullptr},
};
inline const Question debianQuestions[]={
    {"desktop",true,Kind::multi,u"Pulpity:",u"Desktops:","gnome-desktop",CHARON_CHOICES(debianDesktops),u"Można zaznaczyć kilka pulpitów albo żadnego: wtedy sama konsola z mową.",u"Several desktops may be checked, or none: then the console with speech only."},
    {"readers",true,Kind::multi,u"Czytniki ekranu:",u"Screen readers:","orca,speakup",CHARON_CHOICES(linuxReaders),CHARON_READER_HELP_PL,CHARON_READER_HELP_EN},
    {"mirror",false,Kind::choice,u"Serwer z pakietami:",u"Package mirror:","deb.debian.org",CHARON_CHOICES(mirrors),nullptr,nullptr},
    {"root",false,Kind::yesNo,u"Osobne konto root z hasłem? Nie: użytkownik dostaje sudo.",u"A separate root account with a password? No: the user gets sudo.","no",nullptr,0,nullptr,nullptr},
    {"ssh",false,Kind::yesNo,u"Zainstalować serwer SSH?",u"Install the SSH server?","no",nullptr,0,nullptr,nullptr},
    {"nonfree",false,Kind::yesNo,u"Dodać niewolne oprogramowanie sprzętowe, firmware kart?",u"Add non-free hardware firmware?","yes",nullptr,0,nullptr,nullptr},
};
// Quick mode: the keyboard and the time zone follow the language.
inline void DeriveDefaults() {
    struct Row {const char* locale;const char* keyboard;const char* zone;};
    static const Row rows[]={{"pl_PL","pl","Europe/Warsaw"},{"en_US","us","America/New_York"},{"en_GB","gb","Europe/London"},{"de_DE","de","Europe/Berlin"},
        {"fr_FR","fr","Europe/Paris"},{"es_ES","es","Europe/Madrid"},{"it_IT","it","Europe/Rome"},{"uk_UA","ua","Europe/Kyiv"},{"cs_CZ","cz","Europe/Prague"}};
    for(const auto& r:rows)if(ask::Is("locale",r.locale)) {
        if(!ask::Get("keyboard")[0])ask::Put("keyboard",r.keyboard);
        if(!ask::Get("timezone")[0])ask::Put("timezone",r.zone);
    }
    if(!ask::Get("keyboard")[0])ask::Put("keyboard","us");
    if(!ask::Get("timezone")[0])ask::Put("timezone","UTC");
    if(!ask::Get("fullname")[0])ask::Put("fullname",ask::Get("user"));
}
// ---- the target ---------------------------------------------------------------
struct Target {size_t disk;size_t part;};   // indices into disks::D()
inline Target& Chosen() {static Target target;return target;}
// Picks the disk (and partition) for the chosen mode by what is spoken.
inline bool ChooseTarget() {
    auto& s=disks::D();disks::Load();auto& t=Chosen();t.disk=SIZE_MAX;t.part=SIZE_MAX;
    static char16_t labels[disks::maxDisks][300];static Choice list[disks::maxParts>disks::maxDisks?disks::maxParts:disks::maxDisks];static size_t map[disks::maxDisks];
    size_t count=0;const bool whole=ask::Is("diskmode","whole"),freeOnly=ask::Is("diskmode","free");
    for(size_t i=0;i<s.count;++i) {
        const auto& d=s.list[i];
        if(d.readOnly)continue;
        if(freeOnly && !d.blank && (!d.gpt || d.freeBlocks*d.blockSize<(uint64_t(12)<<30)))continue;   // 12 GB at least; an empty disk is all free
        if(!whole && !freeOnly && (!d.gpt || !d.partCount))continue;
        if(disks::Bytes(d)<(uint64_t(12)<<30))continue;
        disks::DiskLabel(d,i,labels[count],300);list[count]={labels[count],labels[count],""};map[count]=i;++count;
    }
    if(!count) {
        Fail(freeOnly?T(u"Na żadnym dysku nie ma co najmniej 12 gigabajtów wolnego, niepodzielonego miejsca.",u"No disk has at least 12 gigabytes of free, unpartitioned space.")
            :T(u"Nie znaleziono dysku, na którym można instalować.",u"No disk to install on was found."));return false;}
    // Two disks of the same size cannot be told apart by the installer.
    size_t index=0;
    if(!ask::Choose(T(u"Na którym dysku?",u"On which disk?"),list,count,index))return false;
    t.disk=map[index];
    for(size_t i=0;i<s.count;++i)if(i!=t.disk && s.list[i].blocks*s.list[i].blockSize==disks::Bytes(s.list[t.disk])) {
        Fail(T(u"Dwa dyski mają dokładnie ten sam rozmiar. Instalator nie odróżni ich na pewno, więc tryb automatyczny odmawia. Odłącz drugi dysk albo użyj trybu z przewodnikiem.",
            u"Two disks have exactly the same size. The installer could not tell them apart for certain, so the automatic mode refuses. Unplug the other disk or use the guided mode."));
        return false;}
    // An empty disk has no free-space gap to describe to an installer: it is simply used whole.
    if(freeOnly && s.list[t.disk].blank)ask::Put("diskmode","whole");
    if(ask::Is("diskmode","partition")) {
        const auto& d=s.list[t.disk];static char16_t names[disks::maxParts][200];static size_t partMap[disks::maxParts];size_t n=0;
        for(size_t i=0;i<d.partCount;++i) {
            if(d.parts[i].kind==1 || d.parts[i].kind==5 || d.parts[i].kind==6)continue;   // never the EFI, reserved or recovery partition
            if((d.parts[i].last-d.parts[i].first+1)*d.blockSize<(uint64_t(12)<<30))continue;
            disks::PartLabel(d,d.parts[i],names[n],200);list[n]={names[n],names[n],""};partMap[n]=i;++n;
        }
        if(!n) {Fail(T(u"Ten dysk nie ma partycji, którą można zastąpić: potrzeba co najmniej 12 gigabajtów.",u"This disk has no partition that can be replaced: 12 gigabytes at least."));return false;}
        size_t chosen=0;
        if(!ask::Choose(T(u"Którą partycję zastąpić?",u"Which partition to replace?"),list,n,chosen))return false;
        t.part=partMap[chosen];
    }
    return true;
}
// The summary that is read before anything starts, and the typed word.
inline bool ConfirmPlan(const char16_t* systemName) {
    auto& s=disks::D();const auto& t=Chosen();const auto& d=s.list[t.disk];
    static char16_t text[1600];static char16_t label[300];static char16_t wide[200];
    Set(text,1600,T(u"Plan instalacji. System: ",u"Installation plan. System: "));Cat(text,1600,systemName);Cat(text,1600,u". ");
    ask::Wide(ask::Get("user"),wide,200);Cat(text,1600,T(u"Użytkownik: ",u"User: "));Cat(text,1600,wide);Cat(text,1600,u". ");
    disks::DiskLabel(d,t.disk,label,300);
    if(ask::Is("diskmode","whole")) {
        Cat(text,1600,d.blank?T(u"Ten pusty dysk zostanie użyty w całości: ",u"This empty disk will be used whole: ")
            :T(u"CAŁY ten dysk zostanie wymazany, ze wszystkimi systemami i danymi: ",u"This WHOLE disk will be erased, with every system and all data on it: "));Cat(text,1600,label);Cat(text,1600,u". ");
        if(d.hasCharon)Cat(text,1600,T(u"Na tym dysku jest też Charon; zostanie wczytany do pamięci i po instalacji wróci na nowy dysk. ",
            u"Charon is on this disk too; it is loaded into memory and returns to the new disk after the installation. "));
    } else if(ask::Is("diskmode","partition")) {
        static char16_t part[200];disks::PartLabel(d,d.parts[t.part],part,200);
        Cat(text,1600,T(u"Ta partycja zostanie wymazana i zastąpiona: ",u"This partition will be erased and replaced: "));Cat(text,1600,part);
        Cat(text,1600,T(u". Reszta dysku zostaje: ",u". The rest of the disk stays: "));Cat(text,1600,label);Cat(text,1600,u". ");
    } else {
        Cat(text,1600,T(u"Nic nie zostanie skasowane. Instalacja zajmie wolne miejsce, ",u"Nothing will be erased. The installation takes the free space, "));
        systools::PutSize(text,1600,d.freeBlocks*d.blockSize,Pl());Cat(text,1600,T(u", na dysku: ",u", on the disk: "));Cat(text,1600,label);Cat(text,1600,u". ");
    }
    Cat(text,1600,T(u"Po instalacji Charon zostaje w UEFI: pliki na partycji EFI, czytnik ekranu ładowany przez firmware przy każdym starcie i wpis menedżera Charon po menedżerze nowego systemu. ",
        u"After the installation Charon stays in UEFI: its files on the EFI partition, the screen reader loaded by firmware at every start, and the Charon boot manager entry after the new system's own. "));
    Cat(text,1600,T(u"Aby zacząć, wpisz słowo TAK i naciśnij Enter. Escape rezygnuje.",u"To start, type the word YES and press Enter. Escape gives up."));
    Say(text);
    static char16_t word[16];word[0]=0;
    if(!Prompt(T(u"Wpisz TAK, aby zacząć:",u"Type YES to start:"),word,15,nullptr,nullptr,false))return false;
    if(!SameFold(word,T(u"tak",u"yes"))) {Say(T(u"Nie wpisano potwierdzenia. Nic nie zrobiono.",u"The confirmation was not typed. Nothing was done."));return false;}
    return true;
}
// A writable volume with room for the installer's files (not a read-only one).
inline bool WorkVolume(uint64_t need,efi_handle& volume) {
    efi_handle volumes[32];const size_t n=Volumes(volumes,32);uint64_t best=0;volume=nullptr;
    for(size_t i=0;i<n;++i) {
        efi_file_protocol* root=OpenRoot(volumes[i]);if(!root)continue;
        alignas(8) uint8_t info[512]={};size_t size=sizeof(info);uint64_t freeBytes=0;bool readOnly=true;
        if(root->GetInfo(root,&media::fsInfoGuid,&size,info)==EFI_SUCCESS && size>36) {readOnly=info[8]!=0;freeBytes=systools::Le64(info+24);}
        root->Close(root);
        if(readOnly || freeBytes<need)continue;
        // Charon's own volume first; otherwise the roomiest.
        if(volumes[i]==U().host->selfDevice) {volume=volumes[i];return true;}
        if(freeBytes>best) {best=freeBytes;volume=volumes[i];}
    }
    if(!volume) {static char16_t text[300];Set(text,300,T(u"Żaden wolumin FAT nie ma dość wolnego miejsca na pliki instalatora; potrzeba ",u"No FAT volume has room for the installer's files; needed: "));
        systools::PutSize(text,300,need,Pl());Fail(text);}
    return volume!=nullptr;
}
// An adapter with an address, connecting the first cabled one when needed.
inline bool EnsureNetwork() {
    auto& n=net::N();net::LoadAdapters();
    if(!n.count || !net::HasProtocol(net::ip4Config2Guid)) {net::LoadDrivers(false);net::LoadAdapters();}
    net::IpInfo info;
    for(size_t i=0;i<n.count;++i)if(net::ReadIp(n.adapters[i],info))return true;
    Say(T(u"Łączę z siecią.",u"Connecting to the network."));
    for(size_t i=0;i<n.count;++i)if(!n.adapters[i].wifi && net::Dhcp(n.adapters[i]))return true;
    Fail(T(u"Brak połączenia z siecią. Otwórz Sieć w środowisku Charona, połącz kartę i wróć tutaj.",u"No network connection. Open Network in the workspace, connect an adapter and come back."));
    return false;
}
// Waits until what was said has been heard (at most `tenths` tenths of a second).
inline void HearOut(unsigned tenths) {
    for(unsigned i=0;i<tenths;++i) {U().host->boot->Stall(100000);Pump();auto* fx=U().host->effects;if(i>5 && fx && fx->SpeechIdle && fx->SpeechIdle(fx->context))break;}
}
inline bool Fetch(const char16_t* url,efi_handle volume,const char16_t* path,const char16_t* what) {
    static FileRef target;target.volume=volume;Set(target.path,pathUnits,path);
    static char16_t text[200];Set(text,200,T(u"Pobieram: ",u"Downloading: "));Cat(text,200,what);Say(text);HearOut(60);
    uint64_t received=0;return net::DownloadAny(url,target,&received,nullptr,T(u"Pobieranie",u"Downloading"));
}
// The same, into memory: nothing is written to any disk.
inline bool FetchToMemory(const char16_t* url,Memory& memory,size_t extra,const char16_t* what) {
    static FileRef none;none.volume=nullptr;none.path[0]=0;
    static char16_t text[200];Set(text,200,T(u"Pobieram do pamięci: ",u"Downloading into memory: "));Cat(text,200,what);Say(text);HearOut(60);
    memory.data=nullptr;memory.capacity=0;memory.size=0;memory.extra=extra;memory.reserved=false;
    uint64_t received=0;return net::DownloadAny(url,none,&received,&memory,T(u"Pobieranie",u"Downloading"));
}
// ---- Charon kept in memory ---------------------------------------------------------
// An installation may erase the very disk Charon runs from. So everything
// Charon consists of (\EFI\HAK on its own disk: the boot manager, the screen
// reader add-on, the audio driver, sounds, speech synthesizers, tools,
// settings) is read into memory as a cpio archive before the installer
// starts. The installer's kernel and its initrd are downloaded into memory
// as well, and firmware is given both from memory (LoadFile protocols), so
// the installation needs no file on any disk. At its end the installer
// unpacks Charon onto the new EFI system partition and registers the screen
// reader add-on (Driver####) and the Charon boot manager (a Boot#### entry
// after the new system's own), so Charon is still in UEFI afterwards.
inline bool SkipInArchive(const char16_t* name) {
    // Per-installation records that would be wrong on a new disk, and bulk.
    static const char16_t* const skipped[]={u"continue.bin",u"continue-set",u"installed-by.txt",u"reader-only",u"downloads",u"charon-install"};
    for(const auto* s:skipped)if(SameFold(name,s))return true;
    return false;
}
inline void ArchiveName(const char16_t* path,char* out,size_t cap) {
    static const char prefix[]="charon";size_t n=0;for(;prefix[n];++n)out[n]=prefix[n];
    static uint8_t utf8[pathUnits*3];const size_t bytes=Utf16ToUtf8(path,Len(path),utf8,sizeof(utf8)-1);
    for(size_t i=0;i<bytes && n+1<cap;++i)out[n++]=utf8[i]=='\\'?'/':char(utf8[i]);
    out[n]=0;
}
inline bool ReadInto(const FileRef& ref,uint8_t* at,uint64_t length) {
    efi_file_protocol* file=nullptr;if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS)return false;
    uint64_t done=0;efi_status status=EFI_SUCCESS;
    while(done<length) {
        size_t part=size_t(length-done);if(part>262144)part=262144;
        status=file->Read(file,&part,at+done);
        if(status!=EFI_SUCCESS || !part)break;
        done+=part;Pump();
    }
    file->Close(file);return done==length;
}
// Walks \EFI\HAK on Charon's disk. Without an archive it only measures.
inline bool HakTree(crypt::Cpio* cpio,uint64_t& bytes,size_t& files) {
    constexpr size_t maxDepth=24;static char16_t stack[maxDepth][pathUnits];static Listing list;static char name[pathUnits*3+16];
    size_t depth=0;bytes=0;files=0;
    Set(stack[depth++],pathUnits,u"\\EFI\\HAK");
    if(cpio) {cpio->Folder("charon");cpio->Folder("charon/EFI");cpio->Folder("charon/EFI/HAK");}
    while(depth) {
        static FileRef folder;folder.volume=U().host->selfDevice;Set(folder.path,pathUnits,stack[--depth]);
        if(ReadFolder(folder,list)!=EFI_SUCCESS)continue;
        for(size_t i=0;i<list.count;++i) {
            const auto& e=list.entries[i];
            if(SkipInArchive(e.name))continue;
            static FileRef child;child.volume=folder.volume;if(!JoinPath(child.path,pathUnits,folder.path,e.name))continue;
            ArchiveName(child.path,name,sizeof(name));
            if(e.directory) {
                if(depth<maxDepth) {Set(stack[depth++],pathUnits,child.path);if(cpio)cpio->Folder(name);}
                bytes+=1024;continue;}
            if(e.size>(uint64_t(128)<<20))continue;   // nothing of Charon's is that large
            bytes+=e.size+1024;++files;
            if(cpio) {
                uint8_t* at=cpio->Begin(name,0100644,size_t(e.size));
                if(!at || !ReadInto(child,at,e.size))return false;
                if(files%4==0)Tone(500+unsigned(files%16)*40,15);
            }
        }
    }
    return true;
}
// Runs in the installed system: the reader add-on and the Charon boot entry.
// A copy stays as a one-time service for the first start, in case the
// installer's environment could not reach the firmware variables.
inline const char charonScript[]=
    "#!/bin/sh\n"
    "# Written by Charon. Registers, on the EFI system partition of this system:\n"
    "#  - the screen reader add-on as a UEFI driver option (Driver####, \"Charon voice\")\n"
    "#  - the Charon boot manager as a boot entry (\"Charon\"), after the system's own.\n"
    "esp=/boot/efi\n"
    "state=/var/lib/charon\n"
    "[ -f $esp/EFI/HAK/CHARONVOICE.EFI ] || exit 0\n"
    "mkdir -p $state; : > $state/pending\n"
    "# Charon's own installers stay in the system, in case it must be set up again:\n"
    "# sudo sh /var/lib/charon/installer/zainstaluj-charona.sh\n"
    "if [ -d $esp/EFI/HAK/addon ] && [ ! -d $state/installer ]; then\n"
    "  mkdir -p $state/installer/EFI && cp $esp/EFI/HAK/addon/* $state/installer/ 2>/dev/null\n"
    "  ln -s $esp/EFI/HAK $state/installer/EFI/HAK\n"
    "fi\n"
    "if [ ! -f /etc/systemd/system/charon-register.service ] && [ -d /etc/systemd/system ]; then\n"
    "  cp \"$0\" /usr/local/sbin/charon-register 2>/dev/null && chmod 755 /usr/local/sbin/charon-register\n"
    "  printf '[Unit]\\nDescription=Register Charon in UEFI (once)\\nConditionPathExists=/var/lib/charon/pending\\nAfter=local-fs.target\\n\\n[Service]\\nType=oneshot\\nExecStart=/usr/local/sbin/charon-register\\n\\n[Install]\\nWantedBy=multi-user.target\\n' > /etc/systemd/system/charon-register.service\n"
    "  mkdir -p /etc/systemd/system/multi-user.target.wants\n"
    "  ln -sf /etc/systemd/system/charon-register.service /etc/systemd/system/multi-user.target.wants/charon-register.service\n"
    "fi\n"
    "# Firmware code cannot run while the system shuts down, so the farewell\n"
    "# (falling tones, the reverse of Charon's start sound, and a sentence)\n"
    "# is played by the system itself, just before sound is stopped.\n"
    "if [ -f $state/goodbye.wav ] && [ -d /etc/systemd/system ] && [ ! -f /etc/systemd/system/charon-goodbye.service ]; then\n"
    "  printf '#!/bin/sh\\ncommand -v aplay >/dev/null && aplay -q /var/lib/charon/goodbye.wav 2>/dev/null\\nl=$(. /etc/default/locale 2>/dev/null; echo $LANG)\\nif command -v espeak-ng >/dev/null; then case \"$l\" in pl*) espeak-ng -v pl \"Wy\\305\\202\\304\\205czanie Charona\" 2>/dev/null;; *) espeak-ng \"Charon is turning off\" 2>/dev/null;; esac; fi\\nexit 0\\n' > /usr/local/sbin/charon-goodbye\n"
    "  chmod 755 /usr/local/sbin/charon-goodbye\n"
    "  printf '[Unit]\\nDescription=Charon farewell sound at shutdown\\nAfter=sound.target alsa-restore.service espeakup.service\\n\\n[Service]\\nType=oneshot\\nRemainAfterExit=yes\\nExecStart=/bin/true\\nExecStop=/usr/local/sbin/charon-goodbye\\nTimeoutStopSec=10\\n\\n[Install]\\nWantedBy=multi-user.target\\n' > /etc/systemd/system/charon-goodbye.service\n"
    "  ln -sf /etc/systemd/system/charon-goodbye.service /etc/systemd/system/multi-user.target.wants/charon-goodbye.service\n"
    "fi\n"
    "[ -d /sys/firmware/efi ] || exit 1\n"
    "ls /sys/firmware/efi/efivars/Boot* >/dev/null 2>&1 || mount -t efivarfs efivarfs /sys/firmware/efi/efivars 2>/dev/null\n"
    "command -v efibootmgr >/dev/null || exit 1\n"
    "# r402: an EFI partition mounted on demand (Arch: systemd automount) shows as \"systemd-1\" until it is used.\n"
    "ls $esp >/dev/null 2>&1\n"
    "dev=$(findmnt -n -t vfat -o SOURCE $esp | tail -n1); [ -n \"$dev\" ] || exit 1\n"
    "part=$(cat /sys/class/block/${dev#/dev/}/partition) || exit 1\n"
    "disk=/dev/$(basename $(readlink -f /sys/class/block/${dev#/dev/}/..))\n"
    "for n in $(efibootmgr --driver 2>/dev/null | sed -n 's/^Driver\\([0-9A-Fa-f]\\{4\\}\\)\\*\\{0,1\\} Charon voice.*/\\1/p'); do efibootmgr --driver --bootnum $n --delete-bootnum >/dev/null 2>&1; done\n"
    "efibootmgr --driver --create --disk \"$disk\" --part \"$part\" --loader '\\EFI\\HAK\\CHARONVOICE.EFI' --label 'Charon voice' >/dev/null || exit 1\n"
    "if [ -f $esp/EFI/HAK/HAKBOOT.EFI ]; then\n"
    "  for n in $(efibootmgr 2>/dev/null | sed -n 's/^Boot\\([0-9A-Fa-f]\\{4\\}\\)\\*\\{0,1\\} Charon$/\\1/p;s/^Boot\\([0-9A-Fa-f]\\{4\\}\\)\\*\\{0,1\\} Charon[[:space:]].*HAKBOOT.*/\\1/p'); do efibootmgr --bootnum $n --delete-bootnum >/dev/null 2>&1; done\n"
    "  order=$(efibootmgr | sed -n 's/^BootOrder: //p')\n"
    "  efibootmgr --create --disk \"$disk\" --part \"$part\" --loader '\\EFI\\HAK\\HAKBOOT.EFI' --label 'Charon' >/dev/null || exit 1\n"
    "  new=$(efibootmgr | sed -n 's/^Boot\\([0-9A-Fa-f]\\{4\\}\\)\\*\\{0,1\\} Charon$/\\1/p;s/^Boot\\([0-9A-Fa-f]\\{4\\}\\)\\*\\{0,1\\} Charon[[:space:]].*HAKBOOT.*/\\1/p' | head -n1)\n"
    "  # The system's own boot manager stays first; Charon follows it.\n"
    "  [ -n \"$order\" ] && [ -n \"$new\" ] && efibootmgr --bootorder \"$order,$new\" >/dev/null\n"
    "fi\n"
    "rm -f $state/pending\n"
    "exit 0\n";
// Charon's files as a cpio in pages at `memory` (appended at memory.size,
// 4-byte aligned, so it can follow an initrd already there). `extraFile`:
// one more text file for the archive root (the installer's answers).
// The farewell sound for the installed system: the start chime reversed
// (1175 Hz, then 784 Hz), 16-bit mono 22050 Hz WAV. Returns its size.
inline size_t GoodbyeWav(uint8_t* out,size_t cap) {
    constexpr uint32_t rate=22050,frames=rate*320/1000,bytes=frames*2;
    if(cap<44+bytes)return 0;
    auto le32=[&](size_t at,uint32_t v) {for(int i=0;i<4;++i)out[at+i]=uint8_t(v>>(8*i));};
    memcpy(out,"RIFF",4);le32(4,36+bytes);memcpy(out+8,"WAVEfmt ",8);le32(16,16);out[20]=1;out[21]=0;out[22]=1;out[23]=0;
    le32(24,rate);le32(28,rate*2);out[32]=2;out[33]=0;out[34]=16;out[35]=0;memcpy(out+36,"data",4);le32(40,bytes);
    for(uint32_t i=0;i<bytes;++i)out[44+i]=0;
    static const uint32_t tones[2][3]={{1175,0,110},{784,140,170}};
    for(const auto& tone:tones) {
        const uint32_t start=rate*tone[1]/1000,length=rate*tone[2]/1000,attack=rate/500;
        for(uint32_t i=0;i<length;++i) {
            const uint32_t phase=uint32_t((uint64_t(i)*tone[0]*65536/rate)&65535);
            const int64_t u=phase<32768?phase:phase-32768;
            int64_t v=9000*4*u*(32768-u)/(int64_t(32768)*32768);if(phase>=32768)v=-v;
            v=v*(i<attack?int64_t(i)*1024/attack:int64_t(length-i)*1024/int64_t(length-attack))/1024;
            const int16_t sample=int16_t(v);out[44+(start+i)*2]=uint8_t(sample);out[45+(start+i)*2]=uint8_t(uint16_t(sample)>>8);
        }
    }
    return 44+bytes;
}
struct ArchivePlan {uint64_t bytes;size_t files;bool manager,reader;};
inline ArchivePlan MeasureCharon() {
    ArchivePlan plan{};HakTree(nullptr,plan.bytes,plan.files);
    auto has=[](const char16_t* path) {FileRef ref{U().host->selfDevice,{}};Set(ref.path,pathUnits,path);efi_file_protocol* f=nullptr;
        if(OpenFile(ref,EFI_FILE_MODE_READ,0,&f)!=EFI_SUCCESS)return false;f->Close(f);return true;};
    plan.manager=has(u"\\EFI\\HAK\\HAKBOOT.EFI");plan.reader=has(u"\\EFI\\HAK\\CHARONVOICE.EFI");
    plan.bytes+=uint64_t(64)<<10;   // scripts, answers, trailer
    return plan;
}
inline bool AppendCharon(Memory& memory,const char* answersName,const char* answers,size_t answersSize) {
    while(memory.size%4 && memory.size<memory.capacity)memory.data[memory.size++]=0;
    crypt::Cpio cpio{memory.data+memory.size,memory.capacity-memory.size,0,0,false};
    if(answers)cpio.File(answersName,answers,answersSize);
    uint64_t bytes=0;size_t files=0;
    Say(T(u"Wczytuję Charona do pamięci.",u"Loading Charon into memory."));
    const bool ok=HakTree(&cpio,bytes,files);
    cpio.Program("charon/install-charon.sh",charonScript,sizeof(charonScript)-1);
    cpio.Program("charon/speech.sh",speechScript,sizeof(speechScript)-1);
    {static uint8_t wav[16384];const size_t n=GoodbyeWav(wav,sizeof(wav));if(n)cpio.File("charon/goodbye.wav",wav,n);}
    cpio.End();
    if(!ok || cpio.overflow)return false;
    memory.size+=cpio.size;return true;
}
// ---- files served to firmware from memory -----------------------------------------
struct LoadFileProtocol {efi_status (EFIAPI *LoadFile)(LoadFileProtocol*,void* path,bool bootPolicy,size_t* size,void* buffer);};
struct Served {LoadFileProtocol protocol;const uint8_t* data;size_t size;efi_handle handle;bool both;uint8_t path[24];};
inline efi_guid loadFileGuid={0x56ec3091,0x954c,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
inline efi_guid loadFile2Guid={0x4006c0c1,0xfcb3,0x403e,{0x99,0x6d,0x4a,0x6c,0x87,0x24,0xe0,0x6d}};
// Linux looks for its initrd under this vendor media path (LINUX_EFI_INITRD_MEDIA_GUID).
inline const uint8_t initrdMediaGuid[16]={0x27,0xe4,0x68,0x55,0xfc,0x68,0x3d,0x4f,0xac,0x74,0xca,0x55,0x52,0x31,0xcc,0x68};
// Charon's own: the installer kernel held in memory.
inline const uint8_t kernelMediaGuid[16]={0x1c,0x9e,0x41,0x6b,0x52,0x0d,0x7a,0x4e,0x93,0x15,0x43,0x48,0x41,0x52,0x4f,0x4e};
inline efi_status EFIAPI ServeFile(LoadFileProtocol* self,void*,bool,size_t* size,void* buffer) {
    auto* served=reinterpret_cast<Served*>(self);
    if(!size)return EFI_INVALID_PARAMETER;
    if(!buffer || *size<served->size) {*size=served->size;return EFI_BUFFER_TOO_SMALL;}
    memcpy(buffer,served->data,served->size);*size=served->size;return EFI_SUCCESS;
}
inline void Withdraw(Served& s) {
    auto* boot=U().host->boot;if(!s.handle)return;
    boot->UninstallProtocolInterface(s.handle,&loadFile2Guid,&s.protocol);
    if(s.both)boot->UninstallProtocolInterface(s.handle,&loadFileGuid,&s.protocol);
    boot->UninstallProtocolInterface(s.handle,&media::pathGuid,s.path);
    s.handle=nullptr;
}
inline bool Publish(Served& s,const uint8_t* guid,const Memory& memory,bool both) {
    auto* boot=U().host->boot;s.protocol.LoadFile=ServeFile;s.data=memory.data;s.size=memory.size;s.handle=nullptr;s.both=both;
    s.path[0]=4;s.path[1]=3;s.path[2]=20;s.path[3]=0;for(int i=0;i<16;++i)s.path[4+i]=guid[i];
    s.path[20]=0x7f;s.path[21]=0xff;s.path[22]=4;s.path[23]=0;
    // Another program's leftover under the same path would be found first.
    {auto* remaining=reinterpret_cast<efi_device_path_protocol*>(s.path);efi_handle old=nullptr;
     if(boot->LocateDevicePath(&loadFile2Guid,&remaining,&old)==EFI_SUCCESS && old && reinterpret_cast<uint8_t*>(remaining)[0]==0x7f)return false;}
    if(boot->InstallProtocolInterface(&s.handle,&media::pathGuid,EFI_NATIVE_INTERFACE,s.path)!=EFI_SUCCESS) {s.handle=nullptr;return false;}
    bool ok=boot->InstallProtocolInterface(&s.handle,&loadFile2Guid,EFI_NATIVE_INTERFACE,&s.protocol)==EFI_SUCCESS;
    if(ok && both)ok=boot->InstallProtocolInterface(&s.handle,&loadFileGuid,EFI_NATIVE_INTERFACE,&s.protocol)==EFI_SUCCESS;
    if(!ok) {Withdraw(s);return false;}
    return true;
}
// Starts the kernel held in memory; its initrd is served from memory too.
// Returns only when the kernel did not take over.
inline bool StartFromMemory(Memory& kernel,Memory& initrd,const char16_t* title,const char16_t* args) {
    static Served servedKernel,servedInitrd;
    const bool withInitrd=initrd.data && initrd.size;   // r402: a plain UEFI program (netboot.xyz) has none
    if(withInitrd && !Publish(servedInitrd,initrdMediaGuid,initrd,false)) {Fail(T(u"Nie udało się udostępnić instalatora z pamięci.",u"The installer could not be offered from memory."));return false;}
    if(!Publish(servedKernel,kernelMediaGuid,kernel,true)) {if(withInitrd)Withdraw(servedInitrd);Fail(T(u"Nie udało się udostępnić jądra z pamięci.",u"The kernel could not be offered from memory."));return false;}
    const size_t titleUnits=Len(title)+1,argBytes=args && args[0]?(Len(args)+1)*2:0,size=6+titleUnits*2+24+argBytes;
    uint8_t* option=static_cast<uint8_t*>(Allocate(size));
    if(option) {
        option[0]=1;option[1]=option[2]=option[3]=0;option[4]=24;option[5]=0;uint8_t* at=option+6;
        for(size_t i=0;i<titleUnits;++i) {*at++=uint8_t(title[i]);*at++=uint8_t(title[i]>>8);}
        memcpy(at,servedKernel.path,24);at+=24;
        for(size_t i=0;i*2<argBytes;++i) {*at++=uint8_t(args[i]);*at++=uint8_t(args[i]>>8);}
        static char16_t report[700];report[0]=0;Cursor(false);
        ReviewMode(false);ScriptEnd();U().host->launch(U().host->context,title,option,size,report,700);ReviewMode(true);
        Release(option);Reshape();
        Play(Icon::error);Say(report[0]?report:T(u"Instalator nie przejął komputera.",u"The installer did not take over."));
    }
    Withdraw(servedKernel);if(withInitrd)Withdraw(servedInitrd);
    return false;
}
// ---- Debian ---------------------------------------------------------------------
// What runs at the end of every Debian installation, guided or automatic:
// Charon goes from the installer's memory onto the new EFI system partition.
inline void DebianLateCommand(Text& p,bool speech) {
    p.Add("d-i preseed/late_command string mkdir -p /target/boot/efi/EFI/HAK /target/var/lib/charon; "
          "cp -r /charon/EFI/HAK/. /target/boot/efi/EFI/HAK/; cp /charon/install-charon.sh /charon/goodbye.wav /target/var/lib/charon/; "
          "in-target sh /var/lib/charon/install-charon.sh || true");
    if(speech) {static char args[200];ReaderArguments(args,sizeof(args));
        p.Add("; cp /charon/speech.sh /target/var/lib/charon/; in-target sh /var/lib/charon/speech.sh ");p.Add(args);p.Add(" > /target/var/lib/charon/speech.log 2>&1 || true");}
    p.Add("\n");
}
// Filled from the answers: /preseed.cfg for debian-installer.
inline void DebianPreseed(Text& p) {
    const auto& d=disks::D().list[Chosen().disk];
    p.Line("# Written by Charon from spoken answers.");
    p.Line("d-i debian-installer/locale string ",ask::Get("locale"),".UTF-8");
    p.Line("d-i keyboard-configuration/xkb-keymap select ",ask::Get("keyboard"));
    p.Line("d-i netcfg/choose_interface select auto");
    p.Line("d-i netcfg/get_hostname string ",ask::Get("hostname"));
    p.Line("d-i netcfg/get_domain string");
    p.Line("d-i netcfg/hostname string ",ask::Get("hostname"));
    p.Line("d-i mirror/country string manual");
    p.Line("d-i mirror/http/hostname string ",ask::Get("mirror"));
    p.Line("d-i mirror/http/directory string /debian");
    p.Line("d-i mirror/http/proxy string");
    if(ask::Is("root","yes")) {p.Line("d-i passwd/root-login boolean true");p.Line("d-i passwd/root-password-crypted password ",ask::Get("hash"));}
    else p.Line("d-i passwd/root-login boolean false");
    p.Line("d-i passwd/user-fullname string ",ask::Get("fullname"));
    p.Line("d-i passwd/username string ",ask::Get("user"));
    p.Line("d-i passwd/user-password-crypted password ",ask::Get("hash"));
    p.Line("d-i clock-setup/utc boolean true");
    p.Line("d-i time/zone string ",ask::Get("timezone"));
    p.Line("d-i clock-setup/ntp boolean true");
    // The disk is found by its exact size in 512-byte sectors; with no match
    // the installer stops and asks instead of guessing.
    p.Add("d-i partman/early_command string D=; for d in $(list-devices disk); do n=${d#/dev/}; if [ \"$(cat /sys/block/$n/size)\" = \"");
    p.Number(d.blocks*(d.blockSize/512));
    p.Line("\" ]; then D=$d; fi; done; if [ -n \"$D\" ]; then debconf-set partman-auto/disk \"$D\"; debconf-set grub-installer/bootdev \"$D\"; fi");
    if(ask::Is("diskmode","free"))p.Line("d-i partman-auto/init_automatically_partition select biggest_free");   // honoured only without partman-auto/method
    else p.Line("d-i partman-auto/method string regular");
    p.Line("d-i partman-auto/choose_recipe select atomic");
    p.Line("d-i partman-partitioning/choose_label select gpt");
    p.Line("d-i partman-partitioning/default_label string gpt");
    p.Line("d-i partman-lvm/device_remove_lvm boolean true");
    p.Line("d-i partman-md/device_remove_md boolean true");
    p.Line("d-i partman-partitioning/confirm_write_new_label boolean true");
    p.Line("d-i partman/choose_partition select finish");
    p.Line("d-i partman/confirm boolean true");
    p.Line("d-i partman/confirm_nooverwrite boolean true");
    p.Line("d-i partman-efi/non_efi_system boolean true");
    p.Line("d-i apt-setup/non-free-firmware boolean ",ask::Is("nonfree","yes")?"true":"false");
    p.Line("d-i apt-setup/cdrom/set-first boolean false");
    p.Add("tasksel tasksel/first multiselect standard");
    for(const auto& c:debianDesktops)if(ask::Has("desktop",c.value)) {p.Add(", ");p.Add(c.value);}
    if(ask::Is("ssh","yes"))p.Add(", ssh-server");
    p.Add("\n");
    p.Add("d-i pkgsel/include string efibootmgr");
    {static char packages[200];ReaderPackages(packages,sizeof(packages),ask::Any("desktop"));   // ", a, b" -> " a b"
     for(const char* c=packages;*c;++c)if(*c!=',') {char one[2]={*c,0};p.Add(one);}}
    p.Add("\n");
    p.Line("popularity-contest popularity-contest/participate boolean false");
    p.Line("d-i grub-installer/only_debian boolean false");
    p.Line("d-i grub-installer/with_other_os boolean true");
    p.Line("d-i finish-install/reboot_in_progress note");
    DebianLateCommand(p,ask::Is("speech","yes"));
}
inline bool RunDebian(bool guided) {
    static char16_t url[400];static Memory kernel,initrd;
    const ArchivePlan plan=MeasureCharon();
    if(!plan.reader)Say(T(u"Uwaga: na dysku Charona nie ma plików czytnika; nowy system dostanie Charona dopiero po jego osobnej instalacji.",
        u"Note: Charon's disk has no reader files; the new system gets Charon only when it is installed there separately."));
    Set(url,400,u"http://deb.debian.org/debian/dists/stable/main/installer-amd64/current/images/netboot/debian-installer/amd64/linux");
    if(!FetchToMemory(url,kernel,0,T(u"jądro instalatora Debiana",u"the Debian installer's kernel")))return false;
    // The graphical netboot initrd is the one that carries espeakup and the
    // sound drivers, so the installer itself can speak (speakup.synth=soft).
    const bool speech=guided || ask::Is("speech","yes");
    Set(url,400,u"http://deb.debian.org/debian/dists/stable/main/installer-amd64/current/images/netboot/");
    Cat(url,400,speech?u"gtk/debian-installer/amd64/initrd.gz":u"debian-installer/amd64/initrd.gz");
    if(!FetchToMemory(url,initrd,size_t(plan.bytes)+(64u<<10),speech?T(u"instalator Debiana z mową",u"the Debian installer with speech")
        :T(u"instalator Debiana",u"the Debian installer"))) {Free(kernel);return false;}
    // After the installer's own initrd: the answers and Charon itself.
    static char preseed[12000];Text text{preseed,sizeof(preseed),0,false};preseed[0]=0;
    if(guided) {text.Line("# Written by Charon: the guided installation only brings Charon along.");DebianLateCommand(text,false);}
    else DebianPreseed(text);
    const bool packed=!text.overflow && AppendCharon(initrd,"preseed.cfg",preseed,text.size);
    for(auto& c:preseed)c=0;
    if(!packed) {Free(kernel);Free(initrd);Fail(T(u"Nie udało się wczytać Charona i odpowiedzi do pamięci.",u"Charon and the answers could not be loaded into memory."));return false;}
    static char16_t arguments[300];arguments[0]=0;
    if(guided)Cat(arguments,300,u"speakup.synth=soft vga=788 --- quiet");   // the installer's own speech
    else {Cat(arguments,300,u"auto=true priority=critical ");if(speech)Cat(arguments,300,u"speakup.synth=soft ");Cat(arguments,300,u"vga=788 --- quiet");}
    Say(guided?T(u"Wszystko jest w pamięci. Uruchamiam instalator Debiana z mową. Dalej prowadzi go jego własny głos. Na końcu instalator sam wgra Charona na nowy dysk.",
                 u"Everything is in memory. Starting the Debian installer with speech. Its own voice leads from here. At the end the installer puts Charon on the new disk by itself.")
        :speech?T(u"Wszystko jest w pamięci. Uruchamiam instalator Debiana. Postęp czyta głos instalatora. Instalacja potrwa od kilkunastu minut do godziny i na końcu sama uruchomi komputer ponownie, z Charonem i czytnikiem ekranu.",
                  u"Everything is in memory. Starting the Debian installer. Its own voice reads the progress. The installation takes from a quarter of an hour to an hour and restarts the computer by itself at the end, with Charon and the screen reader.")
        :T(u"Wszystko jest w pamięci. Uruchamiam instalator Debiana bez mowy. Instalacja potrwa od kilkunastu minut do godziny i na końcu sama uruchomi komputer ponownie.",
           u"Everything is in memory. Starting the Debian installer without speech. The installation takes from a quarter of an hour to an hour and restarts the computer by itself at the end."));
    HearOut(200);
    StartFromMemory(kernel,initrd,u"Debian installer",arguments);
    for(size_t i=0;i<initrd.size;++i)initrd.data[i]=0;   // the answers were in there
    Free(kernel);Free(initrd);
    return false;
}
}  // namespace install
}  // namespace ws
}  // namespace hakefi
