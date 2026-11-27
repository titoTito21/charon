#pragma once
// r409: what in \EFI\HAK belongs to one person or one computer and is never GIVEN away.
//
// Two different operations move \EFI\HAK, and they treat the private folders differently:
//   GIVE   Charon goes into another system or onto another medium: the cpio archive a Debian
//          installation takes along (CharonWsInstall.h HakTree), Windows installation media, the EFI
//          system partition of a ready-made image or of CharonDOS (CopyCharonTree in CharonWsCore.h),
//          the shell copy in the Ubuntu installer (CharonWsInstall2.h UbuntuUserData), "Install
//          Charon on this computer" (CharonWsSelfInstall.h Wanted). The private folders are left out.
//   CARRY  Charon's own home is kept or re-made on the SAME computer: the copy in memory
//          (CharonSelfCopy.h holds the private files too, marked privateItem; the volume in memory
//          shows them, so answer sets and a plan can be read while Charon runs from memory), and
//          CarryCharonTree in CharonWsCore.h, used when the disk Charon itself lives on has just been
//          erased whole and Charon is written back to it (images::CharonOnto with ownHome).
// Every one of them asks THIS list, so a private folder cannot be forgotten at one of them:
//   answers   saved answer sets (\EFI\HAK\answers\<name>.ini); an administrator's file may hold
//             passwords in clear text;
//   plan      the state of a multi-system plan (\EFI\HAK\plan\): plan.txt, the log and the answer
//             files of the steps, among them the Windows answer file with the account password.
//   network.conf   the Wi-Fi connection handed to ONE installation (it holds the network's password).
//             Charon writes it only onto the target of that installation (NetworkConfNow in
//             CharonWsInstall.h); should one ever lie in Charon's own folder, it is not passed on.
// The names are compared without regard to case and only directly inside \EFI\HAK: a folder called
// "plan" deeper down (inside a package, say) is ordinary content. A file with one of these names is
// left out as well.
//
// Consequence for the code that will read and write these folders: the volume in memory holds them
// as they were when Charon was copied into memory (a few seconds after its start). It is memory:
// what is written there is gone at the next restart, so a change must also be written to the medium
// Charon was started from (selfram::OriginalDevice(), as BootMenu.h MirrorSettingsToMedium does for
// the settings record) or to the plan's own EFI system partition.
//
// No dependency on the workspace or on firmware. GPL-2.0-or-later.
#include <stddef.h>
namespace hakefi {
namespace hakprivate {
inline constexpr const char* const names[]={"answers","plan","network.conf"};   // lower case, ASCII
constexpr size_t count=sizeof(names)/sizeof(names[0]);
// True: `name` (one path component directly inside \EFI\HAK) is private and must not be copied.
inline bool IsPrivate(const char16_t* name) {
    if(!name)return false;
    for(const char* candidate:names) {
        size_t i=0;
        for(;candidate[i];++i) {
            char16_t c=name[i];if(c>=u'A' && c<=u'Z')c=char16_t(c+32);
            if(c!=char16_t(static_cast<unsigned char>(candidate[i])))break;
        }
        if(!candidate[i] && !name[i])return true;
    }
    return false;
}
// True: `path` ("\EFI\HAK\answers\x.ini", any case) lies in a private folder of \EFI\HAK or is one.
inline bool IsPrivatePath(const char16_t* path) {
    static const char16_t prefix[]=u"\\efi\\hak\\";size_t i=0;
    if(!path)return false;
    for(;prefix[i];++i) {char16_t c=path[i];if(c>=u'A' && c<=u'Z')c=char16_t(c+32);if(c!=prefix[i])return false;}
    char16_t part[16];size_t n=0;
    for(;path[i+n] && path[i+n]!=u'\\';++n) {if(n+1>=sizeof(part)/sizeof(part[0]))return false;part[n]=path[i+n];}
    part[n]=0;return IsPrivate(part);
}
// The same list for a POSIX shell: a command that removes the private entries from a copy of
// \EFI\HAK that a shell made at `folder` (no trailing slash), whatever the case of their names.
// add(const char*) receives the pieces; nothing is added when the list is empty.
template<class Add> inline void ShellRemove(const char* folder,Add add) {
    if(!count)return;
    add("for d in ");add(folder);add("/*; do case \"$(basename \"$d\" | tr A-Z a-z)\" in ");
    for(size_t i=0;i<count;++i) {if(i)add("|");add(names[i]);}
    add(") rm -rf \"$d\";; esac; done");
}
}  // namespace hakprivate
}  // namespace hakefi
