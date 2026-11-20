// Recognizes other boot managers by the program image they start (r361).
// GPL-2.0-or-later.
//
// The resident reader looks at every image passed to StartImage (by Charon,
// firmware boot options or a chain loader) and names it when its code and data
// carry a boot manager's own name: rEFInd, Windows Boot Manager, GRUB,
// systemd-boot, OpenCore, Clover, Limine. Signatures are stored with every
// byte raised by one, so Charon's own image never matches itself.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hakefi {
struct BootManagerSignature {const char* raised;const char16_t* name;};
// Charon's own images carry this marker (kept by ResidentReader::Install)
// and other boot managers' names in their texts: an image with the marker is
// never announced (empty name).
inline constexpr char charonImageMarker[]="HAK Charon image";
inline constexpr BootManagerSignature bootManagerSignatures[]={
    {"IBL!Dibspo!jnbhf",u""},
    // Tools a boot manager starts (r366): named, without the boot manager cue.
    {"Nfnuftu97,",u"Memtest86+"},
    {"QbttNbsl!NfnUftu97",u"MemTest86"},  // r400 (not exercised: no image here): "PassMark MemTest86"
    {"HQU!gejtl!)hejtl*",u"GPT fdisk"},  // r400 (not exercised: no image here): "GPT fdisk (gdisk)"
    {"Qfsgpsn!NPL!nbobhfnfou",u"MokManager"},  // r400 (not exercised: no image here): "Perform MOK management"
    // Clover carries OpenCore's libraries and other boot managers' names.
    {"Dmpwfs!sfwjtjpo",u"Clover"},  // "Clover revision"
    {"sFGJoe",u"rEFInd"},
    // systemd-boot names its automatic Windows entry, so it comes first.
    {"MpbefsJogp;!tztufne.cppu",u"systemd-boot"},  // "LoaderInfo: systemd-boot" (not the systemd-stub of kernels)
    {"Xjoepxt!Cppu!Nbobhfs",u"Windows Boot Manager"},
    {"PqfoDpsf!Cppu!Nfov",u"OpenCore"},  // "OpenCore Boot Menu" (OpenCore.efi)
    {"FGJ]PD]PqfoDpsf/fgj",u"OpenCore"},  // "EFI\OC\OpenCore.efi" (its Bootstrap BOOTx64.efi)
    // r386: shim, the signed first stage of Debian, Ubuntu, Fedora, SUSE
    // ("UEFI SHIM"): named in the device list; at start-up the loader it
    // starts (GRUB, systemd-boot) is announced instead.
    {"VFGJ!TIJN",u"shim"},
    // r386: Syslinux EFI ("Syslinux/6.04") and iPXE ("iPXE command line").
    {"Tztmjovy0",u"Syslinux"},
    {"PqfoCTE0bne75!&t",u"OpenBSD boot"},  // r386: ">> OpenBSD/amd64 %s %s"
    {"OfuCTE0y97!FGJ!Cppu",u"NetBSD boot"},  // r386
    {"GsffCTE0bne75!FGJ!mpbefs",u"FreeBSD loader"},  // r386: "FreeBSD/amd64 EFI loader"
    {"WUPZFGJ!qbsujujpo",u"Ventoy"},  // r386: Ventoy's GRUB ("VTOYEFI partition"), before GRUB
    {"ofucppu/yz{",u"netboot.xyz"},  // r400 (not exercised): "netboot.xyz"
    {"HSVC5EPT",u"GRUB4DOS"},  // r400 (not exercised): "GRUB4DOS"
    {"jQYF!dpnnboe!mjof",u"iPXE"},
    {"HOV!HSVC",u"GRUB"},
    {"Mjnjof",u"Limine"},
    {"sFGJu",u"rEFIt"},  // r400 (not exercised): "rEFIt", after rEFInd
};
inline bool FirstStage(const char16_t* name) {
    return name && name[0]==u's' && name[1]==u'h' && name[2]==u'i' && name[3]==u'm' && !name[4];
}
inline bool BootTool(const char16_t* name) {
    if(!name)return false;
    static const char16_t* const tools[]={u"Memtest86+",u"MemTest86",u"GPT fdisk",u"MokManager"};
    for(const auto* tool:tools) {size_t i=0;for(;tool[i] && name[i]==tool[i];++i) {}if(!tool[i] && !name[i])return true;}
    return false;
}
// Name of the first signature (in table order) found as ASCII or UTF-16LE
// text, or nullptr. One pass over the image; `idle` runs every 256 KiB.
inline bool lastImageWasCharon=false;   // r396: the last image DetectBootManager read carries Charon's marker
template<class Idle> const char16_t* DetectBootManager(const uint8_t* image,size_t size,Idle idle) {
    lastImageWasCharon=false;
    constexpr size_t count=sizeof(bootManagerSignatures)/sizeof(bootManagerSignatures[0]);
    size_t found=count;
    if(!image)return nullptr;
    for(size_t i=0;i<size;++i) {
        if(!(i&0x3ffff) && i)idle();
        const uint8_t first=uint8_t(image[i]+1);
        for(size_t k=0;k<found;++k) {
            const char* raised=bootManagerSignatures[k].raised;
            if(uint8_t(raised[0])!=first)continue;
            size_t n=0;while(raised[n])++n;
            bool ascii=i+n<=size,wide=i+2*n<=size;
            for(size_t j=0;j<n && (ascii || wide);++j) {
                const uint8_t want=uint8_t(raised[j]-1);
                if(ascii && image[i+j]!=want)ascii=false;
                if(wide && (image[i+2*j]!=want || image[i+2*j+1]))wide=false;
            }
            if(ascii || wide) {found=k;break;}  // later entries cannot win any more
        }
        if(found==0)break;
    }
    lastImageWasCharon=found==0;   // r396
    return found<count && bootManagerSignatures[found].name[0]?bootManagerSignatures[found].name:nullptr;
}
}  // namespace hakefi
