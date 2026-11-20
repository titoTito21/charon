// Boot-manager profiles (r393). GPL-2.0-or-later.
//
// What Charon knows about each boot manager it recognizes:
//  - its own keys, said first in the reader's key list (Insert+F1) while
//    that manager runs (from each project's documentation);
//  - how it hides its menu and which key brings it back (hidden menus and
//    countdowns, settings V10 byte 41);
//  - its configuration on the disk it starts from, read only: entries,
//    default, timeout, hidden menu (CharonManagerConfig below). Charon never
//    writes these files.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "CharonKeyHelp.h"
#include "CharonBcd.h"
#include <efi/protocol/file.h>
namespace hakefi::managers {
struct Profile {
    const char16_t* name;              // as CharonBootManagers.h names it
    const keyhelp::Entry* keys;size_t keyCount;
    uint16_t revealScan;char16_t revealChar;   // key that shows a hidden menu (0/0: none Charon can send)
    const keyhelp::Entry* hiddenHint;  // how a person shows it
};
inline constexpr keyhelp::Entry grubKeys[]={
    {u"GRUB: strzałki w górę i w dół wybierają pozycję, Enter uruchamia, Home i End pierwsza i ostatnia",u"GRUB: Up and Down choose an entry, Enter starts it, Home and End first and last"},
    {u"GRUB: e edytuje wybraną pozycję; w edytorze Control X albo F10 uruchamia, Escape wraca bez zmian, Tab uzupełnia",u"GRUB: e edits the entry; in the editor Control X or F10 boots it, Escape goes back unchanged, Tab completes"},
    {u"GRUB: c otwiera wiersz poleceń; help wypisuje polecenia, Escape wraca do menu",u"GRUB: c opens the command line; help lists commands, Escape returns to the menu"},
    {u"GRUB: dowolny klawisz zatrzymuje odliczanie; przy ukrytym menu Escape, F4 albo przytrzymany Shift je pokazuje",u"GRUB: any key stops the countdown; with a hidden menu Escape, F4 or holding Shift shows it"},
};
inline constexpr keyhelp::Entry sdbootKeys[]={
    {u"systemd-boot: strzałki wybierają, Enter uruchamia, d ustawia wybraną pozycję jako domyślną",u"systemd-boot: arrows choose, Enter starts, d makes the entry the default"},
    {u"systemd-boot: e edytuje parametry jądra, plus i minus albo t i T zmieniają limit czasu",u"systemd-boot: e edits kernel options, plus and minus or t and T change the timeout"},
    {u"systemd-boot: p pokazuje stan, h albo znak zapytania pomoc, f ustawienia firmware, r rozdzielczość, Q wyjście",u"systemd-boot: p shows status, h or question mark help, f firmware setup, r resolution, Q quits"},
    {u"systemd-boot: przy ukrytym menu przytrzymaj spację w czasie startu; litera skacze do pozycji",u"systemd-boot: with a hidden menu hold Space during start; a letter jumps to an entry"},
};
inline constexpr keyhelp::Entry refindKeys[]={
    {u"rEFInd: strzałki wybierają ikonę, Enter albo spacja uruchamia, Home i End pierwsza i ostatnia",u"rEFInd: arrows choose an icon, Enter or Space starts it, Home and End first and last"},
    {u"rEFInd: Insert, F2 albo Tab otwiera opcje wybranej pozycji; plus i minus też",u"rEFInd: Insert, F2 or Tab opens the entry's options; plus and minus too"},
    {u"rEFInd: Escape odświeża listę, Delete albo minus ukrywa pozycję, F10 zrzut ekranu, cyfry 1 do 9 uruchamiają pozycję",u"rEFInd: Escape rescans, Delete or minus hides an entry, F10 screenshot, digits 1 to 9 start an entry"},
};
inline constexpr keyhelp::Entry windowsKeys[]={
    {u"Menedżer rozruchu Windows: strzałki wybierają system, Enter uruchamia, Tab przechodzi do narzędzi",u"Windows Boot Manager: arrows choose a system, Enter starts it, Tab moves to the tools"},
    {u"Menedżer rozruchu Windows: F8 zaawansowane opcje rozruchu, F10 opcje edycji, Escape wychodzi z menu",u"Windows Boot Manager: F8 advanced boot options, F10 edit options, Escape leaves the menu"},
    {u"Menedżer rozruchu Windows: dowolny klawisz zatrzymuje odliczanie",u"Windows Boot Manager: any key stops the countdown"},
};
inline constexpr keyhelp::Entry openCoreKeys[]={
    {u"OpenCore: strzałki wybierają, Enter uruchamia, Control Enter ustawia domyślną pozycję",u"OpenCore: arrows choose, Enter starts, Control Enter sets the default"},
    {u"OpenCore: spacja pokazuje pozycje pomocnicze, cyfry 1 do 9 wybierają pozycję, Escape ponownie wczytuje listę",u"OpenCore: Space shows auxiliary entries, digits 1 to 9 pick an entry, Escape reloads the list"},
    {u"OpenCore: przy ukrytym wyborze przytrzymaj Option albo Escape podczas startu",u"OpenCore: with the picker hidden hold Option or Escape during start"},
};
inline constexpr keyhelp::Entry cloverKeys[]={
    {u"Clover: strzałki wybierają, Enter uruchamia, spacja opcje wybranej pozycji",u"Clover: arrows choose, Enter starts, Space shows the entry's options"},
    {u"Clover: F1 pomoc, F2 zapis dziennika, F4 zrzut ACPI, F10 zrzut ekranu, F12 wysuń płytę",u"Clover: F1 help, F2 saves the log, F4 dumps ACPI, F10 screenshot, F12 ejects a disc"},
};
inline constexpr keyhelp::Entry limineKeys[]={
    {u"Limine: strzałki wybierają, Enter uruchamia, strzałka w prawo rozwija katalog",u"Limine: arrows choose, Enter starts, Right expands a folder"},
    {u"Limine: e edytuje pozycję, w edytorze F10 uruchamia, Escape wraca; c wiersz poleceń",u"Limine: e edits the entry, in the editor F10 boots, Escape goes back; c command line"},
};
inline constexpr keyhelp::Entry syslinuxKeys[]={
    {u"Syslinux: strzałki wybierają, Enter uruchamia, Tab edytuje wiersz poleceń jądra, Escape wiersz boot",u"Syslinux: arrows choose, Enter starts, Tab edits the kernel command line, Escape the boot prompt"},
};
inline constexpr keyhelp::Entry ventoyKeys[]={
    {u"Ventoy: strzałki wybierają obraz, Enter uruchamia, F1 pomoc, F2 przeglądanie, F3 widok drzewa, F4 lokalne systemy, F5 narzędzia, F6 menu własne",u"Ventoy: arrows choose an image, Enter boots it, F1 help, F2 browse, F3 tree view, F4 local systems, F5 tools, F6 own menu"},
};
inline constexpr keyhelp::Entry ipxeKeys[]={
    {u"iPXE: Control B otwiera wiersz poleceń w czasie startu; w menu strzałki i Enter",u"iPXE: Control B opens the command line during start; in menus arrows and Enter"},
};
inline constexpr keyhelp::Entry freebsdKeys[]={
    {u"Program rozruchowy FreeBSD: cyfry i litery w nawiasach wybierają opcję, spacja zatrzymuje odliczanie, Escape wiersz poleceń",u"FreeBSD loader: digits and letters in brackets choose an option, Space pauses the countdown, Escape the prompt"},
};
inline constexpr keyhelp::Entry haikuKeys[]={
    {u"Program rozruchowy Haiku: przytrzymaj spację albo Shift podczas startu, aby otworzyć menu; strzałki i Enter",u"Haiku loader: hold Space or Shift during start to open the menu; arrows and Enter"},
};
inline constexpr keyhelp::Entry grubHidden={u"Menu GRUB jest ukryte.",u"The GRUB menu is hidden."};
inline constexpr keyhelp::Entry sdbootHidden={u"Menu systemd-boot jest ukryte.",u"The systemd-boot menu is hidden."};
inline constexpr keyhelp::Entry refindHidden={u"Menu rEFInd jest ukryte.",u"The rEFInd menu is hidden."};
inline constexpr keyhelp::Entry openCoreHidden={u"Wybór OpenCore jest ukryty. Przytrzymaj Escape podczas startu, aby go pokazać.",u"The OpenCore picker is hidden. Hold Escape during start to show it."};
inline constexpr keyhelp::Entry windowsHidden={u"Menu rozruchu Windows ma limit czasu zero i się nie pokaże.",u"The Windows boot menu has a zero timeout and will not show."};
inline constexpr keyhelp::Entry limineHidden={u"Menu Limine ma limit czasu zero.",u"The Limine menu has a zero timeout."};
#define CHARON_KEYS(a) a,sizeof(a)/sizeof(a[0])
inline constexpr Profile profiles[]={
    {u"GRUB",CHARON_KEYS(grubKeys),0x17,0,&grubHidden},
    {u"Ventoy",CHARON_KEYS(ventoyKeys),0x17,0,&grubHidden},
    {u"systemd-boot",CHARON_KEYS(sdbootKeys),0,u' ',&sdbootHidden},
    {u"rEFInd",CHARON_KEYS(refindKeys),1,0,&refindHidden},
    {u"Windows Boot Manager",CHARON_KEYS(windowsKeys),0,0,&windowsHidden},
    {u"OpenCore",CHARON_KEYS(openCoreKeys),0,0,&openCoreHidden},
    {u"Clover",CHARON_KEYS(cloverKeys),0,0,nullptr},
    {u"Limine",CHARON_KEYS(limineKeys),0,0,&limineHidden},
    {u"Syslinux",CHARON_KEYS(syslinuxKeys),0,0,nullptr},
    {u"iPXE",CHARON_KEYS(ipxeKeys),0,0,nullptr},
    {u"FreeBSD loader",CHARON_KEYS(freebsdKeys),0,0,nullptr},
    {u"Haiku loader",CHARON_KEYS(haikuKeys),0,u' ',nullptr},
};
#undef CHARON_KEYS
inline bool SameName(const char16_t* a,const char16_t* b) {if(!a || !b)return false;size_t i=0;for(;a[i] && a[i]==b[i];++i) {}return a[i]==b[i];}
// ---- r396: profiles from files ------------------------------------------
// \EFI\HAK\profiles\*.txt on Charon's disk (UTF-8, one "field=value" per
// line, '#' starts a comment) describe a boot manager Charon has no profile
// for, without a new Charon build:
//   name=MyLoader                  how the reader names it
//   signature=MyLoader version     text in its program image (ASCII or UTF-16, 6+ characters)
//   key=strzałki wybierają | arrows choose    (Polish | English; up to 8)
//   reveal=esc|space|enter|f2|f4|f8|f10|f12|up|down|none   key that shows a hidden menu
//   hidden=Przytrzymaj Shift | Hold Shift     how a person shows a hidden menu
//   screenkey=Edit Options :: F10 zapisuje | F10 saves   (r397) a key of the
//            screen that shows "Edit Options"; said first in the key list
//            while that text is on the screen (up to 8)
// A built-in signature always wins; the files only add managers.
struct UserProfile {
    char signature[64]={};size_t signatureLength=0;char16_t name[48]={};
    char16_t keyPl[8][160]={},keyEn[8][160]={};keyhelp::Entry keys[8]={};size_t keyCount=0;
    char16_t hintPl[160]={},hintEn[160]={};keyhelp::Entry hint{};
    Profile profile{};
    // r397: screen keys (screenkey=MARKER :: pl | en)
    char16_t screenMarker[8][48]={},screenPl[8][160]={},screenEn[8][160]={};keyhelp::Entry screenKeys[8]={};size_t screenKeyCount=0;
};
inline constexpr size_t maxUserProfiles=16;
inline UserProfile userProfiles[maxUserProfiles];inline size_t userProfileCount=0;inline bool userProfilesLoaded=false;
// UTF-8 to UCS-2 (characters outside the BMP become '?').
inline size_t Utf8To16(const char* s,size_t n,char16_t* out,size_t capacity) {
    size_t o=0;
    for(size_t i=0;i<n && o+1<capacity;) {
        const uint8_t c=uint8_t(s[i]);uint32_t v=c;size_t extra=0;
        if(c>=0xf0) {v=c&7;extra=3;} else if(c>=0xe0) {v=c&15;extra=2;} else if(c>=0xc0) {v=c&31;extra=1;}
        ++i;for(size_t k=0;k<extra && i<n;++k,++i)v=(v<<6)|(uint8_t(s[i])&63);
        out[o++]=v<0x10000?char16_t(v):u'?';
    }
    out[o]=0;return o;
}
inline bool ProfileSpace(char c) {return c==' ' || c=='\t' || c=='\r';}
inline void Trim(const char*& s,size_t& n) {while(n && ProfileSpace(*s)) {++s;--n;}while(n && ProfileSpace(s[n-1]))--n;}
inline void SplitLanguages(const char* v,size_t n,char16_t* pl,char16_t* en,size_t capacity) {
    size_t bar=0;while(bar<n && v[bar]!='|')++bar;
    const char* a=v;size_t an=bar;Trim(a,an);
    Utf8To16(a,an,pl,capacity);
    if(bar<n) {const char* b=v+bar+1;size_t bn=n-bar-1;Trim(b,bn);Utf8To16(b,bn,en,capacity);}
    else Utf8To16(a,an,en,capacity);
}
inline bool KeyWord(const char* v,size_t n,const char* w) {size_t i=0;for(;i<n && w[i];++i) {char c=v[i];if(c>='A' && c<='Z')c=char(c+32);if(c!=w[i])return false;}return i==n && !w[i];}
inline bool ParseUserProfile(const char* text,size_t length,UserProfile& u) {
    u=UserProfile{};bool hidden=false;
    for(size_t at=0;at<length;) {
        size_t end=at;while(end<length && text[end]!='\n')++end;
        const char* line=text+at;size_t n=end-at;at=end+1;
        if(n>=3 && uint8_t(line[0])==0xef && uint8_t(line[1])==0xbb && uint8_t(line[2])==0xbf) {line+=3;n-=3;}   // BOM
        Trim(line,n);if(!n || line[0]=='#')continue;
        size_t eq=0;while(eq<n && line[eq]!='=')++eq;if(eq==n)continue;
        const char* key=line;size_t kn=eq;Trim(key,kn);
        const char* v=line+eq+1;size_t vn=n-eq-1;Trim(v,vn);
        if(KeyWord(key,kn,"name"))Utf8To16(v,vn,u.name,48);
        else if(KeyWord(key,kn,"signature")) {if(vn>=6 && vn<sizeof(u.signature)) {for(size_t i=0;i<vn;++i)u.signature[i]=v[i];u.signature[vn]=0;u.signatureLength=vn;}}
        else if(KeyWord(key,kn,"key") && u.keyCount<8) {SplitLanguages(v,vn,u.keyPl[u.keyCount],u.keyEn[u.keyCount],160);u.keys[u.keyCount]={u.keyPl[u.keyCount],u.keyEn[u.keyCount]};++u.keyCount;}
        else if(KeyWord(key,kn,"screenkey") && u.screenKeyCount<8) {
            size_t sep=0;while(sep+1<vn && !(v[sep]==':' && v[sep+1]==':'))++sep;
            if(sep+1<vn && sep) {
                const char* m=v;size_t mn=sep;Trim(m,mn);
                const char* t=v+sep+2;size_t tn=vn-sep-2;Trim(t,tn);
                const size_t k=u.screenKeyCount;
                if(mn && tn) {Utf8To16(m,mn,u.screenMarker[k],48);SplitLanguages(t,tn,u.screenPl[k],u.screenEn[k],160);u.screenKeys[k]={u.screenPl[k],u.screenEn[k]};++u.screenKeyCount;}
            }
        }
        else if(KeyWord(key,kn,"hidden")) {SplitLanguages(v,vn,u.hintPl,u.hintEn,160);u.hint={u.hintPl,u.hintEn};hidden=true;}
        else if(KeyWord(key,kn,"reveal")) {
            static const struct {const char* word;uint16_t scan;char16_t ch;} keys[]={{"esc",0x17,0},{"escape",0x17,0},{"space",0,u' '},{"enter",0,u'\r'},
                {"f2",0x0c,0},{"f4",0x0e,0},{"f8",0x12,0},{"f10",0x14,0},{"f12",0x16,0},{"up",1,0},{"down",2,0},{"none",0,0}};
            for(const auto& k:keys)if(KeyWord(v,vn,k.word)) {u.profile.revealScan=k.scan;u.profile.revealChar=k.ch;}
        }
    }
    if(!u.name[0] || !u.signatureLength)return false;
    u.profile.name=u.name;u.profile.keys=u.keys;u.profile.keyCount=u.keyCount;u.profile.hiddenHint=hidden?&u.hint:nullptr;
    return true;
}
// Reads the profile files once (Charon's own disk; read only).
inline size_t LoadUserProfiles(efi_file_protocol* root) {
    if(userProfilesLoaded || !root)return userProfileCount;
    userProfilesLoaded=true;userProfileCount=0;
    efi_file_protocol* dir=nullptr;
    if(root->Open(root,&dir,(char16_t*)u"\\EFI\\HAK\\profiles",EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !dir)return 0;
    alignas(8) uint8_t entry[600];
    for(unsigned guard=0;guard<128 && userProfileCount<maxUserProfiles;++guard) {
        size_t bytes=sizeof(entry);
        if(dir->Read(dir,&bytes,entry)!=EFI_SUCCESS || !bytes)break;
        if(*reinterpret_cast<const uint64_t*>(entry+72)&0x10)continue;
        const char16_t* name=reinterpret_cast<const char16_t*>(entry+80);size_t n=0;while(name[n] && n<60)++n;
        if(n<=4)continue;
        bool txt=true;static const char16_t ext[]=u".txt";for(size_t i=0;i<4 && txt;++i) {char16_t c=name[n-4+i];if(c>=u'A' && c<=u'Z')c=char16_t(c+32);txt=c==ext[i];}
        if(!txt)continue;
        char16_t file[64];for(size_t i=0;i<=n;++i)file[i]=name[i];
        efi_file_protocol* f=nullptr;
        if(dir->Open(dir,&f,file,EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !f)continue;
        static char text[8192];size_t size=sizeof(text)-1;
        const bool read=f->Read(f,&size,text)==EFI_SUCCESS;f->Close(f);
        if(!read)continue;text[size]=0;
        if(ParseUserProfile(text,size,userProfiles[userProfileCount]))++userProfileCount;
    }
    dir->Close(dir);return userProfileCount;
}
// The name of a profile file's manager whose signature is in the image.
template<class Idle> const char16_t* DetectUserManager(const uint8_t* image,size_t size,Idle idle) {
    if(!image || !userProfileCount)return nullptr;
    for(size_t p=0;p<userProfileCount;++p) {
        const UserProfile& u=userProfiles[p];const size_t n=u.signatureLength;
        for(size_t i=0;i+n<=size;++i) {
            if(!(i&0x3ffff) && i)idle();
            if(image[i]!=uint8_t(u.signature[0]))continue;
            bool ascii=true,wide=i+2*n<=size;
            for(size_t j=0;j<n && (ascii || wide);++j) {
                if(ascii && image[i+j]!=uint8_t(u.signature[j]))ascii=false;
                if(wide && (image[i+2*j]!=uint8_t(u.signature[j]) || image[i+2*j+1]))wide=false;
            }
            if(ascii || wide)return u.name;
        }
    }
    return nullptr;
}
// ---- r397: keys of one screen ------------------------------------------
// A screen of a boot program (an editor, a command line, an options page)
// is recognized by text it always shows; the key list (Insert+F1) then
// names it and says its keys first, before the manager's and the reader's.
// One screen per group: the first match wins (an options page before the
// page it opens from).
struct ScreenKeys {
    const char16_t* marker;            // text on the screen (case matters)
    uint8_t group;
    keyhelp::Entry name;
    const keyhelp::Entry* keys;size_t keyCount;
};
inline constexpr keyhelp::Entry grubCommandKeys[]={
    {u"help wypisuje polecenia, help i nazwa opisuje polecenie, ls pokazuje dyski i pliki, set zmienne, boot uruchamia, normal wraca do menu",u"help lists commands, help and a name describes one, ls shows disks and files, set variables, boot starts, normal returns to the menu"},
    {u"Tab uzupełnia polecenie, dysk albo plik, dwa razy Tab wypisuje możliwości; strzałki w górę i w dół historia poleceń",u"Tab completes a command, disk or file, Tab twice lists the choices; Up and Down walk the command history"},
    {u"Control A i Control E początek i koniec wiersza, Control K usuwa do końca, Control U do początku, Escape wraca do menu",u"Control A and Control E start and end of the line, Control K deletes to the end, Control U to the start, Escape returns to the menu"},
};
inline constexpr keyhelp::Entry grubEditorKeys[]={
    {u"Control X albo F10 uruchamia zmienioną pozycję, Escape wraca do menu bez zmian, Control C albo F2 otwiera wiersz poleceń",u"Control X or F10 boots the changed entry, Escape returns to the menu unchanged, Control C or F2 opens the command line"},
    {u"Strzałki przechodzą po wierszach i znakach, Tab uzupełnia, Control A i Control E początek i koniec wiersza, Control K usuwa do końca wiersza, Control O wstawia wiersz",u"Arrows move over lines and characters, Tab completes, Control A and Control E start and end of the line, Control K deletes to the end of the line, Control O inserts a line"},
};
inline constexpr keyhelp::Entry limineEditorKeys[]={
    {u"F10 uruchamia zmienioną pozycję, Escape porzuca zmiany i wraca do menu",u"F10 boots the changed entry, Escape discards the changes and returns to the menu"},
    {u"Strzałki przechodzą po wierszach i znakach, Home i End początek i koniec wiersza; pod edytorem Limine mówi, czy konfiguracja jest poprawna",u"Arrows move over lines and characters, Home and End start and end of the line; below the editor Limine says whether the configuration is valid"},
};
inline constexpr keyhelp::Entry windowsEditKeys[]={
    {u"Wpisz albo popraw opcje rozruchu w wierszu, Enter uruchamia z nimi, Escape wraca do menu",u"Type or correct the boot options on the line, Enter starts with them, Escape returns to the menu"},
};
inline constexpr keyhelp::Entry windowsAdvancedKeys[]={
    {u"Strzałki wybierają opcję, na przykład tryb awaryjny, Enter uruchamia z nią, Escape wraca do menu",u"Arrows choose an option such as Safe Mode, Enter starts with it, Escape returns to the menu"},
};
inline constexpr keyhelp::Entry memdiagOptionsKeys[]={
    {u"Tab przechodzi między ustawieniami: zestaw testów, pamięć podręczna, liczba przebiegów; strzałki wybierają wartość, cyfry wpisują liczbę przebiegów",u"Tab moves between the settings: test mix, cache, pass count; arrows choose a value, digits type the pass count"},
    {u"F10 zapisuje i zaczyna test, Escape anuluje",u"F10 applies and starts the test, Escape cancels"},
};
inline constexpr keyhelp::Entry memdiagKeys[]={
    {u"F1 otwiera opcje testu, Escape kończy test i uruchamia komputer ponownie",u"F1 opens the test options, Escape ends the test and restarts the computer"},
};
inline constexpr keyhelp::Entry shellKeys[]={
    {u"help -b wypisuje polecenia stronami, help i nazwa opisuje polecenie; spacja następna strona, q kończy",u"help -b lists commands page by page, help and a name describes one; Space next page, q stops"},
    {u"Tab uzupełnia nazwę pliku, strzałki w górę i w dół historia, Insert tryb wstawiania; map -r dyski, fs0: przechodzi na dysk, ls pliki, edit plik edytor",u"Tab completes a file name, Up and Down the history, Insert toggles insert mode; map -r disks, fs0: changes to a disk, ls files, edit file the editor"},
};
inline constexpr keyhelp::Entry shellEditKeys[]={
    {u"Control E pomoc edytora, Control W zamyka pomoc; Control S zapis, Control Q wyjście, Control F szukaj, Control R zamień, Control G idź do wiersza; zamiast Control także F1 do F9",u"Control E editor help, Control W closes the help; Control S save, Control Q exit, Control F find, Control R replace, Control G go to line; F1 to F9 instead of Control too"},
    {u"Control K wycina wiersz, Control U wkleja, Control O otwiera plik, Control T typ pliku, Insert tryb wstawiania",u"Control K cuts a line, Control U pastes, Control O opens a file, Control T file type, Insert toggles insert mode"},
};
inline constexpr keyhelp::Entry formKeys[]={
    {u"Strzałki przechodzą po pozycjach, Enter otwiera albo wybiera, Escape wraca, plus i minus albo spacja zmieniają wartość",u"Arrows move over items, Enter opens or chooses, Escape goes back, plus and minus or Space change a value"},
    {u"F9 przywraca domyślne, F10 zapisuje; opis wybranej pozycji jest czytany po klawiszu",u"F9 resets the defaults, F10 saves; the selected item's help is read after a key"},
};
inline constexpr keyhelp::Entry ipxeConfigKeys[]={
    {u"Strzałki wybierają ustawienie, Enter edytuje, Control D usuwa ustawienie, Control X kończy",u"Arrows choose a setting, Enter edits it, Control D deletes a setting, Control X exits"},
};
inline constexpr keyhelp::Entry ipxeShellKeys[]={
    {u"help wypisuje polecenia; config ustawienia, dhcp sieć, autoboot rozruch z sieci, exit wraca do firmware",u"help lists commands; config settings, dhcp network, autoboot boots from the network, exit returns to the firmware"},
};
inline constexpr keyhelp::Entry freebsdPromptKeys[]={
    {u"znak zapytania wypisuje polecenia, help opisuje je, menu wraca do menu, boot uruchamia, lsdev urządzenia",u"question mark lists commands, help describes them, menu returns to the menu, boot starts, lsdev lists devices"},
};
inline constexpr keyhelp::Entry freebsdOptionsKeys[]={
    {u"Cyfry przełączają opcje, Backspace wraca do menu głównego",u"Digits toggle the options, Backspace returns to the main menu"},
};
inline constexpr keyhelp::Entry bsdPromptKeys[]={
    {u"help wypisuje polecenia, ls pliki, boot uruchamia, machine informacje o komputerze",u"help lists commands, ls files, boot starts, machine shows the computer"},
};
inline constexpr keyhelp::Entry syslinuxPromptKeys[]={
    {u"Wpisz nazwę pozycji i opcje, Enter uruchamia, Tab wypisuje pozycje, klawisze F1 do F10 ekrany pomocy",u"Type an entry name and options, Enter starts it, Tab lists the entries, F1 to F10 show help screens"},
};
inline constexpr keyhelp::Entry haikuOptionsKeys[]={
    {u"Strzałki wybierają, Enter przełącza pole wyboru, Escape albo pozycja Return to main menu wraca",u"Arrows choose, Enter toggles a check box, Escape or the Return to main menu entry goes back"},
};
#define CHARON_KEYS(a) a,sizeof(a)/sizeof(a[0])
inline constexpr ScreenKeys screens[]={
    {u"Minimal BASH-like line editing",1,{u"Wiersz poleceń GRUB",u"GRUB command line"},CHARON_KEYS(grubCommandKeys)},
    {u"Minimum Emacs-like screen editing",1,{u"Edytor pozycji GRUB",u"GRUB entry editor"},CHARON_KEYS(grubEditorKeys)},
    {u"Discard and Exit",1,{u"Edytor pozycji Limine",u"Limine entry editor"},CHARON_KEYS(limineEditorKeys)},
    {u"Edit Boot Options",1,{u"Edycja opcji rozruchu Windows",u"Windows edit boot options"},CHARON_KEYS(windowsEditKeys)},
    {u"Advanced Boot Options",1,{u"Zaawansowane opcje rozruchu Windows",u"Windows advanced boot options"},CHARON_KEYS(windowsAdvancedKeys)},
    {u"Pass Count",1,{u"Opcje diagnostyki pamięci Windows",u"Windows Memory Diagnostic options"},CHARON_KEYS(memdiagOptionsKeys)},
    {u"Windows Memory Diagnostic",1,{u"Diagnostyka pamięci Windows",u"Windows Memory Diagnostic"},CHARON_KEYS(memdiagKeys)},
    {u"UEFI EDIT",1,{u"Edytor powłoki UEFI",u"UEFI Shell editor"},CHARON_KEYS(shellEditKeys)},
    {u"Shell>",1,{u"Powłoka UEFI",u"UEFI Shell"},CHARON_KEYS(shellKeys)},
    {u":\\>",1,{u"Powłoka UEFI",u"UEFI Shell"},CHARON_KEYS(shellKeys)},
    {u"Move Highlight",1,{u"Formularz ustawień firmware",u"Firmware settings form"},CHARON_KEYS(formKeys)},
    {u"exit configuration utility",1,{u"Ustawienia iPXE",u"iPXE settings"},CHARON_KEYS(ipxeConfigKeys)},
    {u"iPXE>",1,{u"Wiersz poleceń iPXE",u"iPXE command line"},CHARON_KEYS(ipxeShellKeys)},
    {u"Back to main menu",1,{u"Opcje rozruchu FreeBSD",u"FreeBSD boot options"},CHARON_KEYS(freebsdOptionsKeys)},
    {u"for a list of commands",1,{u"Wiersz poleceń programu rozruchowego FreeBSD",u"FreeBSD loader prompt"},CHARON_KEYS(freebsdPromptKeys)},
    {u"boot>",1,{u"Wiersz boot",u"boot prompt"},CHARON_KEYS(bsdPromptKeys)},
    {u"boot:",1,{u"Wiersz boot Syslinux",u"Syslinux boot prompt"},CHARON_KEYS(syslinuxPromptKeys)},
    {u"Safe Mode Options",1,{u"Opcje trybu awaryjnego Haiku",u"Haiku safe mode options"},CHARON_KEYS(haikuOptionsKeys)},
    {u"Debug Options",1,{u"Opcje debugowania Haiku",u"Haiku debug options"},CHARON_KEYS(haikuOptionsKeys)},
};
#undef CHARON_KEYS
inline const Profile* Find(const char16_t* name) {
    for(const auto& p:profiles)if(SameName(p.name,name))return &p;
    for(size_t i=0;i<userProfileCount;++i)if(SameName(userProfiles[i].name,name))return &userProfiles[i].profile;   // r396
    return nullptr;
}
// ---- configuration on the manager's disk (read only) --------------------
// The summary is the BCD one (CharonBcd.h): entries, default, timeout,
// hidden, configuration elsewhere (a GRUB stub that loads another file).
namespace config {
inline bool Space(char c) {return c==' ' || c=='\t';}
inline bool StartsWith(const char* s,const char* p) {while(*p) {char a=*s,b=*p;if(a>='A' && a<='Z')a=char(a+32);if(b>='A' && b<='Z')b=char(b+32);if(a!=b)return false;++s;++p;}return true;}
inline long Number(const char* s,bool* ok=nullptr) {
    while(Space(*s) || *s=='=' || *s==':' || *s=='"' || *s=='\'')++s;
    bool negative=false;if(*s=='-') {negative=true;++s;}
    long v=0;bool any=false;while(*s>='0' && *s<='9') {v=v*10+(*s-'0');++s;any=true;}
    if(ok)*ok=any;return negative?-v:v;
}
inline void AddName(BcdSummary& s,const char* from,const char* end) {
    if(s.count>=BcdSummary::maxEntries)return;
    while(from<end && Space(*from))++from;
    while(end>from && (Space(end[-1]) || end[-1]=='\r'))--end;
    char16_t* out=s.name[s.count];size_t k=0;
    // UTF-8 to UTF-16 (BMP)
    for(const unsigned char* p=(const unsigned char*)from;p<(const unsigned char*)end && k+1<64;) {
        uint32_t c=*p++;
        if(c>=0xc0 && c<0xe0 && p<(const unsigned char*)end) {c=((c&31)<<6)|(*p++&63);}
        else if(c>=0xe0 && c<0xf0 && p+1<(const unsigned char*)end) {c=((c&15)<<12)|((p[0]&63)<<6)|(p[1]&63);p+=2;}
        else if(c>=0x80)c='?';
        if(c<32)c=' ';
        out[k++]=char16_t(c);
    }
    out[k]=0;if(k)++s.count;
}
// Text between quotes after `at` ('…' or "…"), or the rest of the line.
inline void QuotedName(BcdSummary& s,const char* at,const char* lineEnd) {
    while(at<lineEnd && Space(*at))++at;
    if(at<lineEnd && (*at=='\'' || *at=='"')) {const char q=*at++;const char* e=at;while(e<lineEnd && *e!=q)++e;AddName(s,at,e);}
    else {const char* e=at;while(e<lineEnd && *e!='{')++e;AddName(s,at,e);}
}
template<class Line> void Lines(const char* text,size_t size,Line line) {
    const char* p=text;const char* end=text+size;
    while(p<end) {
        const char* e=p;while(e<end && *e!='\n')++e;
        const char* s=p;while(s<e && Space(*s))++s;
        if(s<e && *s!='#')line(s,e);
        p=e<end?e+1:e;
    }
}
inline void Grub(const char* text,size_t size,BcdSummary& s) {
    bool hidden=false,configfile=false;long defaultIndex=-1;int depth=0;
    Lines(text,size,[&](const char* l,const char* e) {
        // only the top level is the menu (a submenu's entries are inside it)
        const int at=depth;for(const char* p=l;p<e;++p) {if(*p=='{')++depth;else if(*p=='}' && depth)--depth;}
        if(at)return;
        if(StartsWith(l,"menuentry ") || StartsWith(l,"menuentry\t"))QuotedName(s,l+10,e);
        else if(StartsWith(l,"submenu "))QuotedName(s,l+8,e);
        else if(StartsWith(l,"set timeout_style=")) {hidden=StartsWith(l+18,"hidden") || StartsWith(l+18,"\"hidden");}
        else if(StartsWith(l,"set timeout=")) {bool ok;const long t=Number(l+12,&ok);if(ok)s.timeout=t;}
        else if(StartsWith(l,"set default=")) {bool ok;const long d=Number(l+12,&ok);if(ok)defaultIndex=d;}
        else if(StartsWith(l,"configfile ") || StartsWith(l,"source "))configfile=true;
    });
    if(defaultIndex>=0 && size_t(defaultIndex)<s.count)s.defaultIndex=size_t(defaultIndex);
    else if(s.count && defaultIndex<0)s.defaultIndex=0;
    s.hidden=hidden || s.timeout==0;
    s.elsewhere=configfile && !s.count;
}
inline void SystemdLoader(const char* text,size_t size,BcdSummary& s) {
    Lines(text,size,[&](const char* l,const char* e) {
        if(StartsWith(l,"timeout")) {
            const char* v=l+7;while(v<e && Space(*v))++v;
            if(StartsWith(v,"menu-hidden"))s.hidden=true;
            else if(StartsWith(v,"menu-force"))s.timeout=-1;
            else {bool ok;const long t=Number(v,&ok);if(ok) {s.timeout=t;if(!t)s.hidden=true;}}
        }
    });
}
inline void SystemdEntry(const char* text,size_t size,BcdSummary& s) {
    bool named=false;
    Lines(text,size,[&](const char* l,const char* e) {if(!named && StartsWith(l,"title")) {named=true;AddName(s,l+5,e);}});
}
inline void Refind(const char* text,size_t size,BcdSummary& s) {
    Lines(text,size,[&](const char* l,const char* e) {
        if(StartsWith(l,"menuentry"))QuotedName(s,l+9,e);
        else if(StartsWith(l,"timeout")) {bool ok;const long t=Number(l+7,&ok);if(ok) {s.timeout=t;if(t<0)s.hidden=true;}}
    });
    s.automatic=true;   // rEFInd adds what it finds on the disks
}
inline void Limine(const char* text,size_t size,BcdSummary& s) {
    Lines(text,size,[&](const char* l,const char* e) {
        if(*l=='/' && !(l+1<e && l[1]=='/'))AddName(s,l+1,e);                      // limine.conf: "/Title"
        else if(*l==':' && !(l+1<e && l[1]==':'))AddName(s,l+1,e);                 // limine.cfg (old): ":Title"
        else if(StartsWith(l,"timeout:") || StartsWith(l,"TIMEOUT=")) {
            const char* v=l+8;while(v<e && Space(*v))++v;
            if(StartsWith(v,"no"))s.timeout=-1;else {bool ok;const long t=Number(v,&ok);if(ok) {s.timeout=t;if(!t)s.hidden=true;}}
        }
        else if(StartsWith(l,"default_entry:") || StartsWith(l,"DEFAULT_ENTRY=")) {bool ok;const long d=Number(l+14,&ok);if(ok && d>=1)s.defaultIndex=size_t(d-1);}
    });
    if(s.defaultIndex==SIZE_MAX && s.count)s.defaultIndex=0;
}
inline void Syslinux(const char* text,size_t size,BcdSummary& s) {
    bool menu=false,promptOff=false,lastAdded=false;   // lastAdded: the last LABEL made an entry (not dropped as the 9th)
    Lines(text,size,[&](const char* l,const char* e) {
        if(StartsWith(l,"menu label ")) {const char* a=l+11;const char* b=e;while(a<b && Space(*a))++a;while(b>a && (Space(b[-1]) || b[-1]=='\r'))--b;
            if(lastAdded && s.count && b>a) {--s.count;AddName(s,l+11,e);}}   // replaces the LABEL just added
        else if(StartsWith(l,"label ")) {const size_t had=s.count;AddName(s,l+6,e);lastAdded=s.count>had;}
        else if(StartsWith(l,"timeout ")) {bool ok;const long t=Number(l+8,&ok);if(ok)s.timeout=(t+9)/10;}
        else if(StartsWith(l,"ui "))menu=true;
        else if(StartsWith(l,"prompt 0"))promptOff=true;
    });
    s.hidden=!menu && promptOff;
}
// OpenCore / Clover config.plist: Timeout, ShowPicker and entry names.
inline const char* Find(const char* from,const char* end,const char* needle) {
    for(const char* p=from;p<end;++p) {const char* a=p;const char* b=needle;while(a<end && *b && *a==*b) {++a;++b;}if(!*b)return p;}
    return nullptr;
}
inline void Plist(const char* text,size_t size,BcdSummary& s,bool openCore) {
    const char* end=text+size;
    if(const char* t=Find(text,end,"<key>Timeout</key>")) {
        if(const char* v=Find(t,end,"<integer>"))s.timeout=Number(v+9);
    }
    if(openCore) {
        if(const char* p=Find(text,end,"<key>ShowPicker</key>")) {
            const char* f=Find(p,end,"<false/>");const char* t=Find(p,end,"<true/>");
            if(f && (!t || f<t))s.hidden=true;
        }
        if(const char* entries=Find(text,end,"<key>Entries</key>")) {
            const char* stop=Find(entries,end,"</array>");if(!stop)stop=end;
            for(const char* p=entries;p && p<stop;) {
                p=Find(p,stop,"<key>Name</key>");if(!p)break;
                const char* v=Find(p,stop,"<string>");if(!v)break;v+=8;
                const char* e=Find(v,stop,"</string>");if(!e)break;
                AddName(s,v,e);p=e;
            }
        }
        s.automatic=true;
    }
}
}  // namespace config
// Reads a file of at most `limit` bytes; the caller frees it with FreePool.
inline char* ReadFile(efi_boot_services* boot,efi_file_protocol* root,const char16_t* path,size_t& size,size_t limit=256*1024) {
    efi_file_protocol* file=nullptr;size=0;
    if(!root || root->Open(root,&file,(char16_t*)path,EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !file)return nullptr;
    uint64_t end=0;char* data=nullptr;
    if(file->SetPosition(file,UINT64_MAX)==EFI_SUCCESS && file->GetPosition(file,&end)==EFI_SUCCESS && end && end<=limit && file->SetPosition(file,0)==EFI_SUCCESS
        && boot->AllocatePool(EfiBootServicesData,size_t(end)+1,(void**)&data)==EFI_SUCCESS && data) {
        size_t got=size_t(end);
        if(file->Read(file,&got,data)==EFI_SUCCESS) {size=got;data[got]=0;}
        else {boot->FreePool(data);data=nullptr;}
    }
    file->Close(file);return data;
}
template<class Parse> bool ParseFile(efi_boot_services* boot,efi_file_protocol* root,const char16_t* path,BcdSummary& s,Parse parse) {
    size_t size=0;char* data=ReadFile(boot,root,path,size);
    if(!data)return false;
    parse(data,size,s);boot->FreePool(data);return true;
}
// systemd-boot's entries: every *.conf in \loader\entries.
inline void SystemdEntries(efi_boot_services* boot,efi_file_protocol* root,BcdSummary& s) {
    efi_file_protocol* dir=nullptr;
    if(root->Open(root,&dir,(char16_t*)u"\\loader\\entries",EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !dir)return;
    alignas(8) uint8_t buffer[512];
    for(unsigned guard=0;guard<64 && s.count<BcdSummary::maxEntries;++guard) {
        size_t bytes=sizeof(buffer);
        if(dir->Read(dir,&bytes,buffer)!=EFI_SUCCESS || !bytes)break;
        const char16_t* name=reinterpret_cast<const char16_t*>(buffer+80);   // EFI_FILE_INFO.FileName
        const uint64_t attribute=*reinterpret_cast<const uint64_t*>(buffer+72);
        if(attribute&0x10)continue;   // a directory
        size_t n=0;while(name[n] && n<200)++n;
        if(n<6 || name[n-5]!=u'.' || (name[n-4]|0x20)!=u'c' || (name[n-3]|0x20)!=u'o' || (name[n-2]|0x20)!=u'n' || (name[n-1]|0x20)!=u'f')continue;
        char16_t path[260];size_t at=0;
        for(const char16_t* p=u"\\loader\\entries\\";*p;++p)path[at++]=*p;
        for(size_t i=0;i<n && at+1<260;++i)path[at++]=name[i];
        path[at]=0;
        ParseFile(boot,root,path,s,config::SystemdEntry);
    }
    dir->Close(dir);
}
// The configuration of the boot manager `name` started from the volume
// `root`; `imageDir` is the folder of its image (u"\\EFI\\debian"), may be
// empty. False when no configuration file was found.
inline bool ReadConfig(efi_boot_services* boot,efi_file_protocol* root,const char16_t* name,const char16_t* imageDir,BcdSummary& s) {
    using namespace config;
    s=BcdSummary{};
    if(!boot || !root || !name)return false;
    auto inDir=[&](const char16_t* file,char16_t* out) {
        size_t at=0;for(const char16_t* p=imageDir?imageDir:u"";*p && at<200;++p)out[at++]=*p;
        if(!at || out[at-1]!=u'\\')out[at++]=u'\\';
        for(const char16_t* p=file;*p && at<258;++p)out[at++]=*p;out[at]=0;
    };
    char16_t path[260];
    if(SameName(name,u"GRUB") || SameName(name,u"Ventoy")) {
        inDir(u"grub.cfg",path);
        const char16_t* tries[]={path,u"\\boot\\grub\\grub.cfg",u"\\grub\\grub.cfg",u"\\EFI\\BOOT\\grub.cfg",u"\\EFI\\debian\\grub.cfg",u"\\EFI\\ubuntu\\grub.cfg"};
        for(auto* t:tries)if(ParseFile(boot,root,t,s,Grub))return true;
        return false;
    }
    if(SameName(name,u"systemd-boot")) {
        const bool loader=ParseFile(boot,root,u"\\loader\\loader.conf",s,SystemdLoader);
        SystemdEntries(boot,root,s);
        s.automatic=true;   // it adds Windows, the firmware setup and others by itself
        if(s.count && s.defaultIndex==SIZE_MAX)s.defaultIndex=0;
        return loader || s.count;
    }
    if(SameName(name,u"rEFInd")) {
        inDir(u"refind.conf",path);
        const char16_t* tries[]={path,u"\\EFI\\refind\\refind.conf",u"\\EFI\\BOOT\\refind.conf"};
        for(auto* t:tries)if(ParseFile(boot,root,t,s,Refind))return true;
        return false;
    }
    if(SameName(name,u"Limine")) {
        inDir(u"limine.conf",path);
        const char16_t* tries[]={path,u"\\limine.conf",u"\\boot\\limine\\limine.conf",u"\\limine\\limine.conf",u"\\boot\\limine.conf",u"\\EFI\\BOOT\\limine.conf",u"\\limine.cfg",u"\\boot\\limine.cfg"};
        for(auto* t:tries)if(ParseFile(boot,root,t,s,Limine))return true;
        return false;
    }
    if(SameName(name,u"Syslinux")) {
        inDir(u"syslinux.cfg",path);
        const char16_t* tries[]={path,u"\\syslinux.cfg",u"\\syslinux\\syslinux.cfg",u"\\EFI\\BOOT\\syslinux.cfg",u"\\boot\\syslinux\\syslinux.cfg"};
        for(auto* t:tries)if(ParseFile(boot,root,t,s,Syslinux))return true;
        return false;
    }
    if(SameName(name,u"OpenCore"))return ParseFile(boot,root,u"\\EFI\\OC\\config.plist",s,[](const char* t,size_t n,BcdSummary& x) {config::Plist(t,n,x,true);});
    if(SameName(name,u"Clover"))return ParseFile(boot,root,u"\\EFI\\CLOVER\\config.plist",s,[](const char* t,size_t n,BcdSummary& x) {config::Plist(t,n,x,false);});
    return false;
}
}  // namespace hakefi::managers
