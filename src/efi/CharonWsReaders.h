#pragma once
// r403: Charon workspace - desktops and screen readers for the Linux systems,
// as check boxes: anyone may want several desktops or several readers. Two
// readers that would speak over each other (Speakup and Fenrir in the
// console, Orca and Cthulhu on the desktop) are both installed; one question
// says which of the two starts, the other stays installed and switched off.
// The packages come from each system's own repositories; Cthulhu (a fork of
// Orca, written in Go) has no Debian or Ubuntu package, so it is built from
// its official source at the first start with the network (on Arch: from the
// Stormux repository first), and Orca speaks until it is there.
// GPL-2.0-or-later.
#include "CharonWsAsk.h"
#include "CharonLinuxScripts.h"
#include "CharonToneBinary.h"
namespace hakefi {
namespace ws {
namespace install {
using ask::Choice;
inline const Choice linuxReaders[]={
    {u"Orca, czytnik ekranu dla pulpitu",u"Orca, the screen reader for the desktop","orca"},
    // r409: on Arch from the Stormux package repository; elsewhere built from its source, while Orca speaks
    {u"Cthulhu, czytnik ekranu dla pulpitu wywodzący się z Orki; w Archu z repozytorium Stormux, w pozostałych systemach budowany ze źródeł przy pierwszym uruchomieniu z dostępem do sieci, a do tego czasu mówi Orca",
     u"Cthulhu, a screen reader for the desktop derived from Orca; on Arch from the Stormux repository, elsewhere built from source at the first start with network access, with Orca speaking until then","cthulhu"},
    // r409: Linda (https://github.com/Mudb0y/linda, GPL 2 or later): NVDA for the Linux desktop. Only through Nix, built on
    // the computer (about eleven minutes and 7.5 gigabytes in the tests on Arch and Fedora); X11 sessions; Orca until then.
    {u"Linda, czytnik ekranu NVDA dla pulpitu Linuksa; instalowany przez Nix przy pierwszym uruchomieniu z siecią, co trwa kilkanaście minut i zajmuje 8 gigabajtów; działa w sesjach X11, a do tego czasu i w sesjach Wayland mówi Orca",
     u"Linda, the NVDA screen reader for the Linux desktop; installed through Nix at the first start with network access, which takes ten minutes or more and 8 gigabytes; works in X11 sessions, and until then and in Wayland sessions Orca speaks","linda"},
    {u"Speakup z espeakup, mowa w konsoli od uruchomienia systemu",u"Speakup with espeakup, console speech from system start-up","speakup"},
    {u"Fenrir, czytnik ekranu dla konsoli",u"Fenrir, a screen reader for the console","fenrir"},
    {u"Emacspeak, pulpit dźwiękowy w Emacsie; gdy to on ma mówić, konsola 1 loguje się sama i uruchamia Emacspeak",
     u"Emacspeak, the audio desktop in Emacs; when it is the one that speaks, console 1 signs in by itself and starts Emacspeak","emacspeak"}};
// r408: i3 with I38, Storm Dragon's accessible set-up of the i3 window manager
// (https://git.stormux.org/storm/I38, GPL 3). Its value is the package of i3 in Debian,
// Ubuntu and Arch alike; it is no tasksel task (CharonWsInstall.h leaves it out of
// tasksel/first). speech.sh installs what I38 needs and writes its configuration for
// every user with "i38.sh -D" (no questions); it is told so by the word i38 in its list
// of readers (ReaderArguments below). The reader inside it is Cthulhu when installed,
// otherwise Orca. Not offered for Fedora, whose image gets no desktop at all.
#define CHARON_I38_VALUE "i3-wm"
#define CHARON_I38_DESKTOP {u"i3 z I38 (klawiaturowy pulpit dla niewidomych, Stormux)",u"i3 with I38 (keyboard desktop for blind users, Stormux)",CHARON_I38_VALUE}
inline const Choice debianDesktops[]={
    {u"GNOME",u"GNOME","gnome-desktop"},{u"MATE",u"MATE","mate-desktop"},{u"KDE Plasma",u"KDE Plasma","kde-desktop"},
    {u"Xfce",u"Xfce","xfce-desktop"},{u"Cinnamon",u"Cinnamon","cinnamon-desktop"},{u"LXQt",u"LXQt","lxqt-desktop"},{u"LXDE",u"LXDE","lxde-desktop"},
    CHARON_I38_DESKTOP};
inline const Choice ubuntuDesktops[]={
    {u"pulpit Ubuntu, GNOME",u"Ubuntu desktop, GNOME","ubuntu-desktop-minimal"},{u"Kubuntu, KDE Plasma",u"Kubuntu, KDE Plasma","kubuntu-desktop"},
    {u"Xubuntu, Xfce",u"Xubuntu, Xfce","xubuntu-desktop"},{u"Ubuntu MATE",u"Ubuntu MATE","ubuntu-mate-desktop"},
    {u"Lubuntu, LXQt",u"Lubuntu, LXQt","lubuntu-desktop"},{u"Ubuntu Cinnamon",u"Ubuntu Cinnamon","ubuntucinnamon-desktop"},
    CHARON_I38_DESKTOP};
// Ready images: the same desktops by their package names in each system.
inline const Choice imageDesktops[]={
    {u"GNOME",u"GNOME","gnome"},{u"KDE Plasma",u"KDE Plasma","kde"},{u"Xfce",u"Xfce","xfce"},{u"MATE",u"MATE","mate"},{u"Cinnamon",u"Cinnamon","cinnamon"},{u"LXQt",u"LXQt","lxqt"},
    CHARON_I38_DESKTOP};
#define CHARON_READER_HELP_PL u"Możesz zaznaczyć kilka. Jeśli dwa mówiłyby naraz, zapytam, który ma się uruchamiać."
#define CHARON_READER_HELP_EN u"You can check several. If two would speak at once, I will ask which one should start."
// After the questions: which console reader and which desktop reader start,
// and whether there is any speech at all. False: the user left (Escape).
inline bool ResolveReaders(bool desktops) {
    const bool orca=ask::Has("readers","orca"),cthulhu=ask::Has("readers","cthulhu"),speakup=ask::Has("readers","speakup"),fenrir=ask::Has("readers","fenrir");
    const bool emacspeak=ask::Has("readers","emacspeak"),linda=ask::Has("readers","linda");
    // The console has room for one speaker: asked only when two or three were checked.
    static const Choice consoleAll[]={{u"Speakup z espeakup",u"Speakup with espeakup","speakup"},{u"Fenrir",u"Fenrir","fenrir"},{u"Emacspeak",u"Emacspeak","emacspeak"}};
    static Choice console[3];size_t consoles=0;
    if(speakup)console[consoles++]=consoleAll[0];
    if(fenrir)console[consoles++]=consoleAll[1];
    if(emacspeak)console[consoles++]=consoleAll[2];
    if(consoles>1) {size_t i=0;
        if(!ask::Choose(T(u"Który czytnik ekranu ma mówić w konsoli? Pozostałe zostaną zainstalowane, ale wyłączone.",u"Which screen reader should speak in the console? The others are installed but switched off."),console,consoles,i))return false;
        ask::Put("console",console[i].value);
    } else ask::Put("console",consoles?console[0].value:"none");
    // r409: the desktop has room for one speaker too: asked only when two or three were checked (Orca, Cthulhu, Linda).
    static const Choice desktopAll[]={{u"Orca",u"Orca","orca"},{u"Cthulhu",u"Cthulhu","cthulhu"},{u"Linda",u"Linda","linda"}};
    static Choice desktopReaders[3];size_t desktopCount=0;
    if(orca)desktopReaders[desktopCount++]=desktopAll[0];
    if(cthulhu)desktopReaders[desktopCount++]=desktopAll[1];
    if(linda)desktopReaders[desktopCount++]=desktopAll[2];
    if(desktops && desktopCount>1) {size_t i=0;
        if(!ask::Choose(T(u"Który czytnik ekranu ma się uruchamiać na pulpicie? Pozostałe zostaną zainstalowane, ale wyłączone.",u"Which screen reader should start on the desktop? The others are installed but switched off."),desktopReaders,desktopCount,i))return false;
        ask::Put("gui",desktopReaders[i].value);
    } else ask::Put("gui",(!desktops || !desktopCount)?"none":desktopReaders[0].value);
    if(!desktops && desktopCount)
        Say(T(u"Bez pulpitu czytniki Orca, Cthulhu i Linda nie mają gdzie działać, więc ich nie instaluję.",u"Without a desktop Orca, Cthulhu and Linda have nowhere to run, so I am not installing them."));
    ask::Put("speech",(speakup || fenrir || emacspeak || (desktops && desktopCount))?"yes":"no");
    // r406 (user): with a desktop, how to sign in - on a graphical login screen, or in the
    // console with the desktop started by the command startx. Asked for every Linux.
    if(desktops) {
        static const Choice logins[]={{u"w trybie graficznym: mówiący ekran logowania, potem pulpit",u"graphically: a speaking sign-in screen, then the desktop","graphical"},
            {u"w konsoli: logowanie tekstowe, a pulpit uruchamiasz poleceniem startx",u"in the console: a text sign-in, and you start the desktop with the command startx","console"}};
        size_t i=0;
        if(!ask::Choose(T(u"Jak chcesz się logować?",u"How do you want to sign in?"),logins,2,i))return false;
        ask::Put("login",logins[i].value);
    } else ask::Put("login","graphical");
    return true;
}
// The readers' packages for a distribution's package list (", a, b").
inline void ReaderPackages(char* out,size_t capacity,bool desktops) {
    size_t n=0;out[0]=0;
    auto add=[&](const char* s) {while(*s && n+1<capacity)out[n++]=*s++;out[n]=0;};
    const bool any=ask::Is("speech","yes");
    if(any)add(", espeak-ng, alsa-utils");
    if(ask::Has("readers","speakup"))add(", espeakup");
    if(ask::Has("readers","fenrir"))add(", fenrir, speech-dispatcher");
    if(desktops && (ask::Has("readers","orca") || ask::Has("readers","cthulhu") || ask::Has("readers","linda")))add(", orca");   // Orca also speaks until Cthulhu or Linda is built
}
// The arguments of speechScript: CONSOLE GUI LIST.
inline void ReaderArguments(char* out,size_t capacity) {
    size_t n=0;out[0]=0;
    auto add=[&](const char* s) {while(*s && n+1<capacity)out[n++]=*s++;out[n]=0;};
    add(ask::Get("console")[0]?ask::Get("console"):"none");add(" ");add(ask::Get("gui")[0]?ask::Get("gui"):"none");add(" ");
    add(ask::Get("readers")[0]?ask::Get("readers"):"none");
    if(ask::Has("desktop",CHARON_I38_VALUE))add(",i38");   // r408: speech.sh sets up I38 for the desktop "i3 with I38"
    add(ask::Is("login","console")?" console":" graphical");   // r406: the fourth argument, LOGIN
}
// r406: speechScript (speech.sh) and Charon's other Linux scripts are kept as files in
// src/linux and compiled in through CharonLinuxScripts.h (tools/gen-charon-linux-scripts.py);
// the tone program charon-tone through CharonToneBinary.h (tools/build-charon-tone.py).
}  // namespace install
}  // namespace ws
}  // namespace hakefi
