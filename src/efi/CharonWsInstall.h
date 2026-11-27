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
#include "CharonWsDetailed.h"   // r410: the detailed mode's groups of questions and what carries their answers
#include "CharonWpaPsk.h"   // r409
#include "CharonVoiceRest.h"   // r409
#include "CharSettings.h"
#include "CharonSynthHost.h"
namespace hakefi {
efi_status LoadCharSettings(efi_file_protocol* root,CharSettings& settings);   // BootMenu.h (r409: the voice hand-over below)
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
// r409: an answer file that outgrew its buffer is never written cut: said, and the installation does not start.
inline void AnswersDoNotFit() {
    Fail(T(u"Odpowiedzi nie mieszczą się w pliku odpowiedzi instalatora, więc instalacja nie zostanie uruchomiona. Na dysku docelowym nic się nie zmieniło. Skróć najdłuższe odpowiedzi, na przykład listy programów.",
           u"The answers do not fit in the installer's answer file, so the installation will not be started. Nothing changed on the target disk. Shorten the longest answers, for example lists of programs."));
}
// r409: the voice Charon itself speaks with goes to the installed system: /var/lib/charon/voice.conf,
// NAME=value lines read by src/linux/speech.sh (CHARON_SYNTH, CHARON_VOICE, CHARON_RATE, CHARON_PITCH,
// CHARON_PUNCTUATION - the names and scales are speech.sh's). synth: the active synthesizer's manifest
// name without ".manifest" (rhvoice, espeak; tvox for the built-in voice; "" unknown: line left out);
// voice: the voice's id in that manifest without the "<synth>-" prefix (natan, magda, slt);
// rate and pitch: the screen reader's, 5..20 tenths; punctuation: 0 none, 1 some, 2 most, 3 all.
inline void VoiceConf(Text& t,const char* synth,const char* voice,unsigned rate,unsigned pitch,unsigned punctuation) {
    static const char* const levels[]={"none","some","most","all"};   // r409, read in the code: speechtext::Level levelNone=0, levelSome=1, levelMost=2, levelAll=3 (CharonSpeechText.h); the setting is checked to be 0..3 (CharSettings.h); speech.sh takes exactly these four words
    t.Line("# Written by Charon: the voice and the reader settings Charon itself uses (read by speech.sh).");
    if(synth[0])t.Line("CHARON_SYNTH=",synth);
    if(voice[0])t.Line("CHARON_VOICE=",voice);
    t.Add("CHARON_RATE=");t.Number(rate);t.Add("\n");
    t.Add("CHARON_PITCH=");t.Number(pitch);t.Add("\n");
    t.Line("CHARON_PUNCTUATION=",levels[punctuation&3]);
}
// The active synthesizer and voice by those names, from Charon's settings record and the manifests.
inline void CurrentVoice(char* synth,size_t synthCap,char* voice,size_t voiceCap,CharSettings& settings) {
    synth[0]=0;voice[0]=0;settings=CharSettings{};
    LoadCharSettings(U().host->root,settings);
    auto put=[](char* out,size_t cap,const char* in) {size_t i=0;for(;in[i] && i+1<cap;++i)out[i]=in[i];out[i]=0;};
    if(!settings.synthKey) {put(synth,synthCap,"tvox");return;}
    static synths::Info found[synths::maxSynths];
    const size_t n=synths::ScanAll(U().host->boot,U().host->selfDevice,U().host->root,found,synths::maxSynths);
    const synths::Info* info=synths::Find(found,n,settings.synthKey);
    if(!info)return;
    size_t k=0;
    for(;info->manifest[k] && info->manifest[k]!=u'.' && k+1<synthCap;++k) {char16_t c=info->manifest[k];if(c>=u'A' && c<=u'Z')c=char16_t(c+32);synth[k]=c<128?char(c):'_';}
    synth[k]=0;
    if(settings.synthVoice>=info->voices)return;
    const char* id=info->id[settings.synthVoice];size_t skip=0;
    while(synth[skip] && id[skip]==synth[skip])++skip;
    put(voice,voiceCap,(!synth[skip] && id[skip]=='-')?id+skip+1:id);
}
// r409: the Wi-Fi connection Charon itself uses goes to the system installed now: network.conf, NAME=value
// lines read by src/linux/network.sh (the names are its own). Only Wi-Fi is written: Charon knows no static
// address and no proxy, and a system on a cable needs no file (DHCP is every system's default). The password
// is the passphrase as typed (network.sh takes a passphrase or a 64-digit key; Charon has no PBKDF2-SHA1 to
// compute the key). The text holds a secret: the caller writes it only to the target of this installation
// and wipes its buffer; it is never spoken, shown or logged.
inline void NetworkConf(Text& t,const char* mac,const char* ssid,bool open,const char* key) {
    t.Line("# Written by Charon: the Wi-Fi connection chosen in Charon (read by network.sh).");
    t.Line("CHARON_NET_TYPE=wifi");
    if(mac[0])t.Line("CHARON_NET_MAC=",mac);
    t.Line("CHARON_NET_METHOD=dhcp");
    t.Line("CHARON_WIFI_SSID=",ssid);
    t.Line("CHARON_WIFI_SECURITY=",open?"open":"wpa-psk");
    if(!open)t.Line("CHARON_WIFI_PSK=",key);
}
// network.conf for the network joined now. Returns its size; 0: no Wi-Fi was joined in Charon, or its name
// cannot stand in a line of text (control characters), or it did not fit.
inline size_t NetworkConfNow(char* out,size_t capacity) {
    const auto& j=net::Joined();out[0]=0;
    if(!j.valid || !j.ssid.length || j.ssid.length>32)return 0;
    static char ssid[33];
    for(size_t i=0;i<j.ssid.length;++i) {const uint8_t c=j.ssid.name[i];if(c<32 || c==127)return 0;ssid[i]=char(c);}
    ssid[j.ssid.length]=0;
    static char mac[18];mac[0]=0;auto& n=net::N();
    if(n.wifiAdapter<n.count && n.adapters[n.wifiAdapter].wifi) {
        static const char hex[]="0123456789abcdef";size_t k=0;
        for(int i=0;i<6;++i) {if(i)mac[k++]=':';mac[k++]=hex[n.adapters[n.wifiAdapter].mac[i]>>4];mac[k++]=hex[n.adapters[n.wifiAdapter].mac[i]&15];}
        mac[k]=0;
    }
    // r409: the computed key, never the passphrase (CharonWpaPsk.h; network.sh and Charon-Network.ps1 both take a key
    // of 64 hexadecimal digits). A key that is already 64 digits is passed on; a passphrase that cannot be a WPA2
    // passphrase (not 8 to 63 bytes) gives no file at all rather than a file with the passphrase in it.
    static char key[65];key[0]=0;
    if(!j.open) {
        size_t n=0;while(n<64 && j.key[n])++n;
        bool hexKey=n==64;for(size_t i=0;i<n && hexKey;++i) {const char c=j.key[i];hexKey=(c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F');}
        if(hexKey) {for(size_t i=0;i<=64;++i)key[i]=i<64?j.key[i]:char(0);}
        else if(!wpapsk::KeyHex(j.key,n,j.ssid.name,j.ssid.length,key))return 0;
    }
    Text t{out,capacity,0,false};
    NetworkConf(t,mac,ssid,j.open,key);
    for(auto& c:key)c=0;
    if(t.overflow) {for(size_t i=0;i<capacity;++i)out[i]=0;return 0;}
    return t.size;
}
// True: Charon's only connection is Wi-Fi (no adapter on a cable has an address).
inline bool OnlyWifi() {
    if(!net::Joined().valid)return false;
    auto& n=net::N();net::IpInfo info;
    for(size_t i=0;i<n.count;++i)if(!n.adapters[i].wifi && net::ReadIp(n.adapters[i],info))return false;
    return true;
}
// voice.conf as it is now. Returns its size (0: it did not fit).
inline size_t VoiceConfNow(char* out,size_t capacity) {
    static char synth[64],voice[48];CharSettings settings;CurrentVoice(synth,sizeof(synth),voice,sizeof(voice),settings);
    Text t{out,capacity,0,false};out[0]=0;
    VoiceConf(t,synth,voice,settings.readerRate,settings.readerPitch,settings.punctuation);
    return t.overflow?0:t.size;
}
inline const Choice systems[]={{u"Debian (stabilny)",u"Debian (stable)","debian"},{u"Ubuntu (LTS)",u"Ubuntu (LTS)","ubuntu"},{u"Windows 11",u"Windows 11","windows"}};
inline const Choice modes[]={
    {u"tryb ekspresowy: jak najmniej pytań",u"express mode: as few questions as possible","quick"},
    {u"tryb szczegółowy: więcej pytań",u"detailed mode: more questions","detailed"},
    {u"zainstaluję ten system sam",u"I will install this system myself","guided"}};
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
    {u"wymaż cały wybrany dysk",u"erase the whole disk you choose","whole"},
    {u"zastąp jedną wybraną partycję",u"replace one partition you choose","partition"},
    {u"użyj tylko wolnego miejsca, niczego nie wymazuj",u"use free space only, erase nothing","free"}};
inline const Choice mirrors[]={{u"automatyczny (deb.debian.org)",u"automatic (deb.debian.org)","deb.debian.org"},{u"Polska",u"Poland","ftp.pl.debian.org"},
    {u"Niemcy",u"Germany","ftp.de.debian.org"},{u"Wielka Brytania",u"United Kingdom","ftp.uk.debian.org"},{u"Stany Zjednoczone",u"United States","ftp.us.debian.org"}};
#define CHARON_CHOICES(list) list,sizeof(list)/sizeof(list[0])
// What every system asks. quick = asked in the quick mode too.
inline const Question commonQuestions[]={
    {"locale",true,Kind::choice,u"Język systemu:",u"System language:","en_US",CHARON_CHOICES(locales),nullptr,nullptr},
    {"keyboard",false,Kind::choice,u"Układ klawiatury:",u"Keyboard layout:","",CHARON_CHOICES(keyboards),u"Bez twojego wyboru układ klawiatury wynika z języka.",u"Without your choice the keyboard layout follows the language."},
    {"timezone",false,Kind::choice,u"Strefa czasowa:",u"Time zone:","",CHARON_CHOICES(zones),nullptr,nullptr},
    {"hostname",false,Kind::name,u"Nazwa komputera:",u"Computer name:","charon-pc",nullptr,0,u"Małe litery, cyfry i myślnik.",u"Lower-case letters, digits and a hyphen."},
    {"fullname",false,Kind::text,u"Twoje imię i nazwisko:",u"Your full name:","",nullptr,0,nullptr,nullptr},
    {"user",true,Kind::name,u"Nazwa użytkownika:",u"User name:","user",nullptr,0,nullptr,nullptr},
    {"password",true,Kind::secret,u"Hasło użytkownika:",u"User password:","",nullptr,0,nullptr,nullptr},
};
inline const Question debianQuestions[]={
    {"desktop",true,Kind::multi,u"Pulpity:",u"Desktops:","gnome-desktop",CHARON_CHOICES(debianDesktops),u"Możesz zaznaczyć kilka pulpitów albo żaden; bez pulpitu zostaje sama konsola z mową.",u"You can check several desktops or none; with none you get just the console with speech."},
    {"readers",true,Kind::multi,u"Czytniki ekranu:",u"Screen readers:","orca,speakup",CHARON_CHOICES(linuxReaders),CHARON_READER_HELP_PL,CHARON_READER_HELP_EN},
    {"mirror",false,Kind::choice,u"Serwer z pakietami:",u"Package server:","deb.debian.org",CHARON_CHOICES(mirrors),nullptr,nullptr},
    {"root",false,Kind::yesNo,u"Osobne konto root z hasłem? Nie: twoje konto dostaje sudo.",u"A separate root account with a password? No: your account gets sudo.","no",nullptr,0,nullptr,nullptr},
    {"ssh",false,Kind::yesNo,u"Zainstalować serwer SSH?",u"Install an SSH server?","no",nullptr,0,nullptr,nullptr},
    {"nonfree",false,Kind::yesNo,u"Dodać niewolne firmware, potrzebne niektórym kartom sieciowym i dźwiękowym?",u"Add non-free firmware, which some network and sound cards need?","yes",nullptr,0,nullptr,nullptr},
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
        Fail(freeOnly?T(u"Żaden dysk nie ma co najmniej 12 gigabajtów wolnego miejsca poza partycjami.",u"No disk has at least 12 gigabytes of free space outside its partitions.")
            :T(u"Nie znaleziono dysku, na którym można zainstalować system.",u"No disk was found to install the system on."));return false;}
    // Two disks of the same size cannot be told apart by the installer.
    size_t index=0;
    if(!ask::Choose(T(u"Na którym dysku?",u"On which disk?"),list,count,index))return false;
    t.disk=map[index];
    for(size_t i=0;i<s.count;++i)if(i!=t.disk && s.list[i].blocks*s.list[i].blockSize==disks::Bytes(s.list[t.disk])) {
        Fail(T(u"Dwa dyski mają dokładnie ten sam rozmiar. Instalator mógłby je pomylić, więc w trybie ekspresowym i szczegółowym instalacja się nie zacznie. Odłącz jeden z tych dysków albo wybierz opcję „zainstaluję ten system sam”.",
            u"Two disks have exactly the same size. The installer could mix them up, so the express and detailed modes will not go ahead. Unplug one of these disks or choose the option \"I will install this system myself\"."));
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
        if(!n) {Fail(T(u"Ten dysk nie ma partycji, którą można zastąpić: musi mieć co najmniej 12 gigabajtów.",u"This disk has no partition that can be replaced: it must have at least 12 gigabytes."));return false;}
        size_t chosen=0;
        if(!ask::Choose(T(u"Którą partycję zastąpić?",u"Which partition should be replaced?"),list,n,chosen))return false;
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
        Cat(text,1600,d.blank?T(u"Ten pusty dysk zostanie użyty w całości: ",u"The whole of this empty disk will be used: ")
            :T(u"CAŁY ten dysk zostanie wymazany, ze wszystkimi systemami i danymi: ",u"This WHOLE disk will be erased, with every system and all data on it: "));Cat(text,1600,label);Cat(text,1600,u". ");
        if(d.hasCharon)Cat(text,1600,T(u"Na tym dysku jest też Charon; zostanie wczytany do pamięci i po instalacji wróci na ten dysk. ",
            u"Charon is on this disk too; it will be loaded into memory and put back on this disk after the installation. "));
    } else if(ask::Is("diskmode","partition")) {
        static char16_t part[200];disks::PartLabel(d,d.parts[t.part],part,200);
        Cat(text,1600,T(u"Ta partycja zostanie wymazana i zastąpiona: ",u"This partition will be erased and replaced: "));Cat(text,1600,part);
        Cat(text,1600,T(u". Reszta dysku zostaje bez zmian: ",u". The rest of the disk is kept: "));Cat(text,1600,label);Cat(text,1600,u". ");
    } else {
        Cat(text,1600,T(u"Nic nie zostanie wymazane. System zajmie wolne miejsce, ",u"Nothing will be erased. The system will take the free space, "));
        systools::PutSize(text,1600,d.freeBlocks*d.blockSize,Pl());Cat(text,1600,T(u", na dysku: ",u", on the disk: "));Cat(text,1600,label);Cat(text,1600,u". ");
    }
    Cat(text,1600,T(u"Po instalacji Charon zostaje na komputerze: jego pliki na partycji systemowej EFI, czytnik ekranu wczytywany przez firmware przy każdym uruchomieniu i wpis rozruchowy menedżera rozruchu Charon, zaraz po menedżerze nowego systemu. ",
        u"After the installation Charon stays on the computer: its files on the EFI system partition, the screen reader, which the firmware loads at every start, and a boot entry for the Charon boot manager, right after the new system's own. "));
    Cat(text,1600,T(u"Aby zacząć, wpisz słowo TAK i naciśnij Enter. Escape anuluje.",u"To start, type the word YES and press Enter. Escape cancels."));
    Say(text);
    static char16_t word[16];word[0]=0;
    if(!Prompt(T(u"Wpisz TAK, aby zacząć:",u"Type YES to start:"),word,15,nullptr,nullptr,false))return false;
    if(!SameFold(word,T(u"tak",u"yes"))) {Say(T(u"Nie wpisano potwierdzenia. Nic nie zostało zmienione.",u"No confirmation was typed. Nothing was changed."));return false;}
    return true;
}
// A writable volume with room for the installer's files (not a read-only one).
inline bool WorkVolume(uint64_t need,efi_handle& volume,bool tell=true) {   // r409: tell=false: the caller has another place to offer
    efi_handle volumes[32];const size_t n=Volumes(volumes,32);uint64_t best=0;volume=nullptr;
    for(size_t i=0;i<n;++i) {
        if(hakefi::selfram::IsVolume(volumes[i]))continue;   // r408: never Charon's copy in memory - files put there are gone after a restart
        efi_file_protocol* root=OpenRoot(volumes[i]);if(!root)continue;
        alignas(8) uint8_t info[512]={};size_t size=sizeof(info);uint64_t freeBytes=0;bool readOnly=true;
        if(root->GetInfo(root,&media::fsInfoGuid,&size,info)==EFI_SUCCESS && size>36) {readOnly=info[8]!=0;freeBytes=systools::Le64(info+24);}
        root->Close(root);
        if(readOnly || freeBytes<need)continue;
        // Charon's own volume first; otherwise the roomiest.
        if(volumes[i]==U().host->selfDevice) {volume=volumes[i];return true;}
        if(freeBytes>best) {best=freeBytes;volume=volumes[i];}
    }
    if(!volume && tell) {static char16_t text[300];Set(text,300,T(u"Żadna partycja FAT nie ma dość wolnego miejsca na pliki instalatora; potrzeba ",u"No FAT partition has enough free space for the installer's files; needed: "));
        systools::PutSize(text,300,need,Pl());Fail(text);}
    return volume!=nullptr;
}
// An adapter with an address, connecting the first cabled one when needed.
inline bool EnsureNetwork() {
    auto& n=net::N();net::LoadAdapters();
    if(!n.count || !net::HasProtocol(net::ip4Config2Guid)) {net::LoadDrivers(false);net::LoadAdapters();}
    net::IpInfo info;
    for(size_t i=0;i<n.count;++i)if(net::ReadIp(n.adapters[i],info))return true;
    Say(T(u"Łączę się z siecią.",u"Connecting to the network."));
    for(size_t i=0;i<n.count;++i)if(!n.adapters[i].wifi && net::Dhcp(n.adapters[i]))return true;
    Fail(T(u"Brak połączenia z siecią. Otwórz Sieć w środowisku Charona, połącz kartę sieciową i wróć tutaj.",u"No network connection. Open Network in the Charon workspace, connect a network adapter and come back here."));
    return false;
}
// Waits until what was said has been heard (at most `tenths` tenths of a second).
// r409: the wait also ends when a key is waiting - the key is NOT read here, so the next question gets it - and
// when the voice rests after a stall of the sound card (nothing would be heard; the dialogue must not look hung).
// The timing without a key and without a stall is unchanged.
inline void HearOut(unsigned tenths) {
    auto* input=U().host->system?U().host->system->ConIn:nullptr;
    for(unsigned i=0;i<tenths;++i) {
        U().host->boot->Stall(100000);Pump();auto* fx=U().host->effects;
        if(i>5 && fx && fx->SpeechIdle && fx->SpeechIdle(fx->context))break;
        if(input && input->WaitForKey && U().host->boot->CheckEvent(input->WaitForKey)==EFI_SUCCESS)break;
        if(i>5 && hakefi::VoiceResting())break;
    }
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
// r410 (agent 3): an installer's two files (kernel and initrd) at once - net::many::Download, two kept-alive
// connections. False: said why; neither file is kept.
inline bool FetchPair(const char16_t* url1,Memory* memory1,size_t extra1,const FileRef* file1,const char16_t* what1,
                      const char16_t* url2,Memory* memory2,size_t extra2,const FileRef* file2,const char16_t* what2) {
#ifndef HAK_CHARON_HTTPS   // a build without Charon's own client: one after the other, as before r410
    auto one=[](const char16_t* url,Memory* m,size_t extra,const FileRef* f,const char16_t* what)->bool {
        return m?FetchToMemory(url,*m,extra,what):Fetch(url,f->volume,f->path,what);};
    if(!one(url1,memory1,extra1,file1,what1))return false;
    if(one(url2,memory2,extra2,file2,what2))return true;
    if(memory1)Free(*memory1);
    return false;
#else
    static char ascii[2][400];static net::many::Item items[2];
    if(!net::many::Ascii(url1,ascii[0],400) || !net::many::Ascii(url2,ascii[1],400)) {Fail(T(u"Adres zawiera niedozwolone znaki.",u"The address contains characters that are not allowed."));return false;}
    static char16_t text[300];Set(text,300,memory1?T(u"Pobieram do pamięci: ",u"Downloading into memory: "):T(u"Pobieram: ",u"Downloading: "));
    Cat(text,300,what1);Cat(text,300,T(u" i ",u" and "));Cat(text,300,what2);Say(text);HearOut(60);
    Memory* memory[2]={memory1,memory2};const size_t extra[2]={extra1,extra2};const FileRef* file[2]={file1,file2};
    for(int i=0;i<2;++i) {
        memset(&items[i],0,sizeof(items[i]));items[i].url=ascii[i];items[i].size=https::unknownLength;
        if(memory[i]) {Memory& m=*memory[i];m.data=nullptr;m.capacity=0;m.size=0;m.extra=extra[i];m.reserved=false;items[i].memory=&m;items[i].extra=extra[i];}
        else items[i].file=file[i];
    }
    net::many::Options o{};o.connections=2;o.label=T(u"Pobieranie",u"Downloading");
    const net::many::Outcome r=net::many::Download(items,2,o);
    if(!r.failed)return true;
    for(int i=0;i<2;++i)if(memory[i])Free(*memory[i]);
    net::many::Explain(*r.firstFailure);
    return false;
#endif
}
inline bool FetchPairToMemory(const char16_t* url1,Memory& m1,size_t extra1,const char16_t* what1,const char16_t* url2,Memory& m2,size_t extra2,const char16_t* what2) {
    return FetchPair(url1,&m1,extra1,nullptr,what1,url2,&m2,extra2,nullptr,what2);
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
// r409: true while the archive is made for an installation that erases the disk Charon itself lives on: Charon's
// saved answer sets and plan state are then CARRIED in the archive (the installer's late_command puts the whole of
// /charon/EFI/HAK on the new EFI partition), because nothing else brings them over the wipe. network.conf never goes.
inline bool& CarryHome() {static bool carry=false;return carry;}
inline bool SkipInArchive(const char16_t* name,bool top=false) {
    // Per-installation records that would be wrong on a new disk, and bulk.
    static const char16_t* const skipped[]={u"continue.bin",u"continue-set",u"installed-by.txt",u"reader-only",u"downloads",u"charon-install"};
    for(const auto* s:skipped)if(SameFold(name,s))return true;
    // r409: directly inside \EFI\HAK (`top`): saved answer sets and plan state stay on Charon's own medium (CharonPrivate.h)
    if(top && CarryHome() && !SameFold(name,u"network.conf"))return false;
    return top && hakprivate::IsPrivate(name);
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
        const bool top=SameFold(folder.path,u"\\EFI\\HAK");
        for(size_t i=0;i<list.count;++i) {
            const auto& e=list.entries[i];
            if(SkipInArchive(e.name,top))continue;
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
// r406: charonScript is src/linux/install-charon.sh (CharonLinuxScripts.h).
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
    if(!plan.manager)plan.bytes+=uint64_t(40)<<20;   // r406: the running program goes along as HAKBOOT.EFI
    return plan;
}
inline bool AppendCharon(Memory& memory,const char* answersName,const char* answers,size_t answersSize) {
    while(memory.size%4 && memory.size<memory.capacity)memory.data[memory.size++]=0;
    crypt::Cpio cpio{memory.data+memory.size,memory.capacity-memory.size,0,0,false};
    if(answers)cpio.File(answersName,answers,answersSize);
    uint64_t bytes=0;size_t files=0;
    Say(T(u"Wczytuję Charona do pamięci.",u"Loading Charon into memory."));
    bool ok=HakTree(&cpio,bytes,files);
    // r406: from the boot disc Charon is \EFI\BOOT\BOOTX64.EFI: it goes along as HAKBOOT.EFI.
    if(ok && !MeasureCharon().manager) {
        static FileRef self;self.volume=U().host->selfDevice;Set(self.path,pathUnits,u"\\EFI\\BOOT\\BOOTX64.EFI");
        efi_file_protocol* file=nullptr;uint64_t size=0;
        if(OpenFile(self,EFI_FILE_MODE_READ,0,&file)==EFI_SUCCESS && file) {
            file->SetPosition(file,~uint64_t(0));file->GetPosition(file,&size);file->Close(file);
            if(size && size<(uint64_t(64)<<20)) {uint8_t* at=cpio.Begin("charon/EFI/HAK/HAKBOOT.EFI",0100644,size_t(size));ok=at && ReadInto(self,at,size);}
        }
    }
    // r410: the answers of the detailed mode that differ from the defaults, and the script that applies them in the
    // installed system (late_command takes both along). With every answer at its default nothing is added.
    if(detailed::Carried()) {static char env[16384];const size_t n=detailed::Env(env,sizeof(env));
        if(!n)ok=false;else {cpio.File("charon/answers.env",env,n);cpio.File("charon/personal.sh",personalScript,sizeof(personalScript)-1);}
        for(auto& c:env)c=0;}
    {static char conf[512];const size_t n=VoiceConfNow(conf,sizeof(conf));if(n)cpio.File("charon/voice.conf",conf,n);}   // r409: late_command copies it for speech.sh
    // r409: the Wi-Fi network Charon is on, for the installed system (the installer itself works over the cable):
    // network.conf with the computed key and the script that applies it; late_command takes both along.
    {static char conf[400];const size_t n=NetworkConfNow(conf,sizeof(conf));
     if(n) {cpio.File("charon/network.conf",conf,n);cpio.Program("charon/network.sh",networkScript,sizeof(networkScript)-1);}
     for(auto& c:conf)c=0;}
    cpio.Program("charon/install-charon.sh",charonScript,sizeof(charonScript)-1);
    cpio.Program("charon/speech.sh",speechScript,sizeof(speechScript)-1);
    // r406: progress tones in the installer and in the installed system's set-up
    cpio.Program("charon/progress.sh",progressScript,sizeof(progressScript)-1);
    cpio.Program("charon/charon-tone",charonToneBinary,sizeof(charonToneBinary));
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
    if(withInitrd && !Publish(servedInitrd,initrdMediaGuid,initrd,false)) {Fail(T(u"Nie udało się przygotować instalatora w pamięci.",u"The installer could not be made ready in memory."));return false;}
    if(!Publish(servedKernel,kernelMediaGuid,kernel,true)) {if(withInitrd)Withdraw(servedInitrd);Fail(T(u"Nie udało się przygotować jądra systemu w pamięci.",u"The system kernel could not be made ready in memory."));return false;}
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
        Play(Icon::error);Say(report[0]?report:T(u"Instalator się nie uruchomił.",u"The installer did not start."));
    }
    Withdraw(servedKernel);if(withInitrd)Withdraw(servedInitrd);
    return false;
}
// ---- Debian ---------------------------------------------------------------------
// What runs at the end of every Debian installation, guided or automatic:
// Charon goes from the installer's memory onto the new EFI system partition.
inline void DebianLateCommand(Text& p,bool speech) {
    // r406: tones of progress from the installer's first minute (progress.sh watch: the
    // tone rises as the new disk fills), stopped when the installer's last step begins.
    p.Add("d-i preseed/early_command string (sh /charon/progress.sh watch /target </dev/null >/dev/null 2>&1 3>&- 4>&- 5>&- 6>&- &)\n");   // debconf's descriptors closed: the installer waits for them
    p.Add("d-i preseed/late_command string : > /tmp/charon-watch-stop; mkdir -p /target/boot/efi/EFI/HAK /target/var/lib/charon; "
          "cp -r /charon/EFI/HAK/. /target/boot/efi/EFI/HAK/; cp /charon/install-charon.sh /charon/goodbye.wav /charon/progress.sh /charon/charon-tone /target/var/lib/charon/; "
          "chmod 755 /target/var/lib/charon/charon-tone; cp /charon/voice.conf /target/var/lib/charon/ 2>/dev/null; in-target sh /var/lib/charon/install-charon.sh || true");   // r409: voice.conf (Charon's synthesizer, rate, pitch, punctuation for speech.sh) is copied when the add-on archive holds one
    if(speech || ask::Any("desktop")) {static char args[200];ReaderArguments(args,sizeof(args));   // r406: a desktop alone too (sign-in mode)
        // r409: in-target sends what its command prints to the installer's syslog (debian-installer-utils, in-target:
        // log-output -t in-target chroot /target ...), so a redirection after it wrote an empty speech.log; the
        // redirection now happens inside the new system. CHARON_CHROOT=1: speech.sh starts and builds nothing here.
        p.Add("; cp /charon/speech.sh /target/var/lib/charon/; in-target sh -c \"CHARON_CHROOT=1 sh /var/lib/charon/speech.sh ");p.Add(args);p.Add(" > /var/lib/charon/speech.log 2>&1\" || true");}
    // r409: a step of a multi-system plan (CharonWsPlanDialogue.h sets the answer "plan"): the last act of the
    // installation tells Charon that this step is done (EFI/HAK/plan/stepN.done on the EFI partition).
    // r409: the Wi-Fi connection chosen in Charon (charon/network.conf is in the archive only when there is one).
    p.Add("; if [ -f /charon/network.conf ]; then cp /charon/network.sh /charon/network.conf /target/var/lib/charon/; chmod 600 /target/var/lib/charon/network.conf; in-target sh /var/lib/charon/network.sh apply || true; fi");
    if(detailed::Carried())   // r410
        p.Add("; if [ -f /charon/answers.env ]; then cp /charon/personal.sh /charon/answers.env /target/var/lib/charon/; chmod 600 /target/var/lib/charon/answers.env; in-target sh /var/lib/charon/personal.sh install || true; fi");
    if(ask::Any("plan"))p.Add("; in-target sh /var/lib/charon/install-charon.sh plan-done || true");
    p.Add("; /charon/charon-tone -p 100 784 110 0 30 1175 170 || true\n");
}
// Filled from the answers: /preseed.cfg for debian-installer.
inline void DebianPreseed(Text& p) {
    const auto& d=disks::D().list[Chosen().disk];
    p.Line("# Written by Charon from spoken answers.");
    p.Line("d-i debian-installer/locale string ",ask::Get("locale"),".UTF-8");
    p.Line("d-i keyboard-configuration/xkb-keymap select ",ask::Get("keyboard"));
    // r409 (seen in VMware: the installation stopped at "Configuring keyboard-configuration ... Keyboard layout:" during
    // "Select and install software" and waited for Enter). The line above answers the installer's own question only.
    // When the package keyboard-configuration is configured inside the new system, its config script asks
    // keyboard-configuration/variant (and layout) at priority critical unless they are answered there (console-setup,
    // debian/keyboard-configuration.config: ask_debconf ... "$layout_priority"). Answers owned by the package are put
    // into the new system's debconf database by the installer and marked as seen: the layout Charon chose, no variant.
    p.Line("keyboard-configuration keyboard-configuration/xkb-keymap select ",ask::Get("keyboard"));
    p.Line("keyboard-configuration keyboard-configuration/layoutcode string ",ask::Get("keyboard"));
    p.Line("keyboard-configuration keyboard-configuration/variantcode string");
    p.Line("keyboard-configuration keyboard-configuration/modelcode string pc105");
    p.Line("keyboard-configuration keyboard-configuration/layout select");
    p.Line("keyboard-configuration keyboard-configuration/variant select");
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
    for(const auto& c:debianDesktops)if(ask::Has("desktop",c.value) && !ask::Same(c.value,CHARON_I38_VALUE)) {p.Add(", ");p.Add(c.value);}   // r408: i3 with I38 is no task; speech.sh installs it
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
    // r409: the whole of Charon's own disk is erased by this installation: answer sets and plan state travel in the archive.
    CarryHome()=!guided && ask::Is("diskmode","whole") && Chosen().disk<disks::D().count && disks::D().list[Chosen().disk].hasCharon;
    const ArchivePlan plan=MeasureCharon();
    if(!plan.reader)Say(T(u"Uwaga: na dysku Charona nie ma plików czytnika ekranu; w nowym systemie Charona trzeba będzie zainstalować osobno.",
        u"Note: Charon's disk has no screen reader files; Charon will have to be installed in the new system separately."));
    static char16_t url2[400];   // r410 (agent 3): kernel and initrd at once (FetchPairToMemory)
    Set(url2,400,u"http://deb.debian.org/debian/dists/stable/main/installer-amd64/current/images/netboot/debian-installer/amd64/linux");
    // The graphical netboot initrd is the one that carries espeakup and the
    // sound drivers, so the installer itself can speak (speakup.synth=soft).
    const bool speech=guided || ask::Is("speech","yes");
    Set(url,400,u"http://deb.debian.org/debian/dists/stable/main/installer-amd64/current/images/netboot/");
    Cat(url,400,speech?u"gtk/debian-installer/amd64/initrd.gz":u"debian-installer/amd64/initrd.gz");
    if(!FetchPairToMemory(url2,kernel,0,T(u"jądro instalatora Debiana",u"the Debian installer's kernel"),url,initrd,size_t(plan.bytes)+(64u<<10),
        speech?T(u"instalator Debiana z mową",u"the Debian installer with speech"):T(u"instalator Debiana",u"the Debian installer")))return false;
    // After the installer's own initrd: the answers and Charon itself.
    static char preseed[48000];Text text{preseed,sizeof(preseed),0,false};preseed[0]=0;   // r409: was 12000; static, not on the stack
    if(guided) {text.Line("# Written by Charon: the guided installation only brings Charon along.");DebianLateCommand(text,false);}
    else DebianPreseed(text);
    if(text.overflow) {for(auto& c:preseed)c=0;Free(kernel);Free(initrd);AnswersDoNotFit();return false;}
    const bool packed=!text.overflow && AppendCharon(initrd,"preseed.cfg",preseed,text.size);
    for(auto& c:preseed)c=0;
    if(!packed) {Free(kernel);Free(initrd);Fail(T(u"Nie udało się wczytać Charona i twoich odpowiedzi do pamięci.",u"Charon and your answers could not be loaded into memory."));return false;}
    static char16_t arguments[300];arguments[0]=0;
    if(guided)Cat(arguments,300,u"speakup.synth=soft vga=788 --- quiet");   // the installer's own speech
    else {Cat(arguments,300,u"auto=true priority=critical ");if(speech)Cat(arguments,300,u"speakup.synth=soft ");Cat(arguments,300,u"vga=788 --- quiet");}
    Say(guided?T(u"Wszystko jest w pamięci. Uruchamiam instalator Debiana z mową. Od teraz mówi sam instalator, własnym głosem. Na końcu sam skopiuje Charona na dysk nowego systemu.",
                 u"Everything is in memory. Starting the Debian installer with speech. From here on the installer speaks with its own voice. At the end it copies Charon to the new system's disk by itself.")
        :speech?T(u"Wszystko jest w pamięci. Uruchamiam instalator Debiana. Postęp odczytuje głos instalatora. Instalacja potrwa od kilkunastu minut do godziny. Na końcu komputer sam uruchomi się ponownie, z Charonem i czytnikiem ekranu.",
                  u"Everything is in memory. Starting the Debian installer. The installer's own voice reads the progress. The installation takes from a quarter of an hour to an hour. At the end the computer restarts by itself, with Charon and the screen reader.")
        :T(u"Wszystko jest w pamięci. Uruchamiam instalator Debiana bez mowy. Instalacja potrwa od kilkunastu minut do godziny. Na końcu komputer sam uruchomi się ponownie.",
           u"Everything is in memory. Starting the Debian installer without speech. The installation takes from a quarter of an hour to an hour. At the end the computer restarts by itself."));
    HearOut(200);
    if(!guided)net::ForgetJoined();   // r409: the install files are written; the Wi-Fi passphrase does not stay in memory
    StartFromMemory(kernel,initrd,u"Debian installer",arguments);
    for(size_t i=0;i<initrd.size;++i)initrd.data[i]=0;   // the answers were in there
    Free(kernel);Free(initrd);
    return false;
}
}  // namespace install
}  // namespace ws
}  // namespace hakefi
