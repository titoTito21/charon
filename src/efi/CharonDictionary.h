// Charon's built-in firmware pronunciation dictionary (r393). GPL-2.0-or-later.
//
// Like an NVDA speech dictionary (whole word, case sensitive): firmware and
// boot-manager abbreviations are spoken the way people say them, in the
// language the text is read in. Applied to screen text only (never to the
// reader's own words), before NVDA's symbol processing.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hakefi::dictionary {
struct Entry {const char16_t* word;const char16_t* pl;const char16_t* en;};
inline constexpr Entry entries[]={
    {u"UEFI",u"u e f i",u"U E F I"},{u"EFI",u"e f i",u"E F I"},{u"BIOS",u"bajos",u"bye-oss"},
    {u"CSM",u"ce es em",u"C S M"},{u"TPM",u"te pe em",u"T P M"},{u"fTPM",u"ef te pe em",u"firmware T P M"},{u"PTT",u"pe te te",u"P T T"},
    {u"XHCI",u"iks ha ce i",u"X H C I"},{u"EHCI",u"e ha ce i",u"E H C I"},{u"OHCI",u"o ha ce i",u"O H C I"},{u"UHCI",u"u ha ce i",u"U H C I"},
    {u"AHCI",u"a ha ce i",u"A H C I"},{u"SATA",u"sata",u"say-ta"},{u"NVMe",u"en wu em e",u"N V M E"},{u"RAID",u"rajd",u"raid"},
    {u"IDE",u"i de e",u"I D E"},{u"SSD",u"es es de",u"S S D"},{u"HDD",u"ha de de",u"H D D"},{u"ODD",u"o de de",u"O D D"},
    {u"PXE",u"piksi",u"pixie"},{u"iPXE",u"aj piksi",u"eye pixie"},{u"TFTP",u"te ef te pe",u"T F T P"},{u"DHCP",u"de ha ce pe",u"D H C P"},
    {u"IPv4",u"i pe w cztery",u"I P version 4"},{u"IPv6",u"i pe w sześć",u"I P version 6"},{u"HTTP",u"ha te te pe",u"H T T P"},{u"HTTPS",u"ha te te pe es",u"H T T P S"},
    {u"LAN",u"lan",u"lan"},{u"WLAN",u"wu lan",u"W lan"},{u"MAC",u"mak",u"mac"},{u"VLAN",u"wu lan",u"V lan"},
    {u"VT-x",u"wu te iks",u"V T X"},{u"VT-d",u"wu te de",u"V T D"},{u"AMD-V",u"a em de wu",u"A M D V"},{u"SVM",u"es wu em",u"S V M"},
    {u"IOMMU",u"i o em em u",u"I O M M U"},{u"SR-IOV",u"es er i o wu",u"S R I O V"},{u"SMT",u"es em te",u"S M T"},{u"HT",u"ha te",u"H T"},
    {u"CPU",u"ce pe u",u"C P U"},{u"GPU",u"gie pe u",u"G P U"},{u"iGPU",u"i gie pe u",u"integrated G P U"},{u"APU",u"a pe u",u"A P U"},
    {u"RAM",u"ram",u"ram"},{u"DRAM",u"de ram",u"D ram"},{u"DDR4",u"de de er cztery",u"D D R 4"},{u"DDR5",u"de de er pięć",u"D D R 5"},
    {u"XMP",u"iks em pe",u"X M P"},{u"EXPO",u"ekspo",u"expo"},{u"ECC",u"e ce ce",u"E C C"},{u"SPD",u"es pe de",u"S P D"},
    {u"PCIe",u"pe ce i e",u"P C I E"},{u"PCI",u"pe ce i",u"P C I"},{u"USB",u"u es be",u"U S B"},{u"ACPI",u"a ce pe i",u"A C P I"},
    {u"SMBIOS",u"es em bajos",u"S M bye-oss"},{u"NVRAM",u"en wu ram",u"N V ram"},{u"CMOS",u"si mos",u"see-moss"},{u"POST",u"post",u"post"},
    {u"GPT",u"gie pe te",u"G P T"},{u"MBR",u"em be er",u"M B R"},{u"ESP",u"e es pe",u"E S P"},{u"FAT32",u"fat trzydzieści dwa",u"fat 32"},
    {u"NTFS",u"en te ef es",u"N T F S"},{u"BCD",u"be ce de",u"B C D"},{u"GRUB",u"grab",u"grub"},{u"WinPE",u"win pe e",u"win P E"},
    {u"MOK",u"mok",u"mok"},{u"PK",u"pe ka",u"P K"},{u"KEK",u"kek",u"K E K"},{u"DBX",u"de be iks",u"D B X"},
    {u"OEM",u"o e em",u"O E M"},{u"HII",u"ha i i",u"H I I"},{u"GOP",u"gop",u"G O P"},{u"SMM",u"es em em",u"S M M"},
    {u"ME",u"em e",u"M E"},{u"CSME",u"ce es em e",u"C S M E"},{u"PSP",u"pe es pe",u"P S P"},{u"BMC",u"be em ce",u"B M C"},
    {u"IPMI",u"i pe em i",u"I P M I"},{u"WOL",u"wol",u"wake on LAN"},{u"LED",u"led",u"L E D"},{u"RGB",u"er gie be",u"R G B"},
    {u"HDMI",u"ha de em i",u"H D M I"},{u"DP",u"de pe",u"D P"},{u"VGA",u"wu gie a",u"V G A"},{u"HDA",u"ha de a",u"H D A"},
    {u"C-State",u"stan ce",u"C state"},{u"C-States",u"stany ce",u"C states"},{u"P-State",u"stan pe",u"P state"},{u"ASPM",u"a es pe em",u"A S P M"},
    {u"ReBAR",u"ri bar",u"re-bar"},{u"SAM",u"sam",u"S A M"},{u"4G",u"cztery gie",u"4 G"},{u"AES",u"a e es",u"A E S"},{u"SHA256",u"sza dwieście pięćdziesiąt sześć",u"shah 256"},
    {u"F1",u"ef jeden",u"F 1"},{u"F2",u"ef dwa",u"F 2"},{u"F7",u"ef siedem",u"F 7"},{u"F8",u"ef osiem",u"F 8"},{u"F9",u"ef dziewięć",u"F 9"},
    {u"F10",u"ef dziesięć",u"F 10"},{u"F11",u"ef jedenaście",u"F 11"},{u"F12",u"ef dwanaście",u"F 12"},{u"ESC",u"escape",u"escape"},{u"Esc",u"escape",u"escape"},
};
inline constexpr size_t count=sizeof(entries)/sizeof(entries[0]);
inline bool WordChar(char16_t c) {return (c>=u'a' && c<=u'z') || (c>=u'A' && c<=u'Z') || (c>=u'0' && c<=u'9') || c==u'_' || c>=0xc0;}
// Writes s[0..n) with dictionary words replaced into out; returns the length.
inline size_t Apply(const char16_t* s,size_t n,bool pl,char16_t* out,size_t cap) {
    size_t at=0;
    for(size_t i=0;i<n;) {
        if(WordChar(s[i]) && (!i || !WordChar(s[i-1]))) {
            // the word, with '-' inside it ("VT-x", "C-State")
            size_t e=i;while(e<n && (WordChar(s[e]) || (s[e]==u'-' && e+1<n && WordChar(s[e+1]) && e>i)))++e;
            const Entry* hit=nullptr;
            for(size_t k=0;k<count && !hit;++k) {
                const char16_t* w=entries[k].word;size_t j=0;
                while(i+j<e && w[j] && w[j]==s[i+j])++j;
                if(!w[j] && i+j==e)hit=&entries[k];
            }
            if(hit) {for(const char16_t* p=pl?hit->pl:hit->en;*p && at+1<cap;++p)out[at++]=*p;i=e;continue;}
            // r397: i always reaches e: a full output buffer (a long help page)
            // left it in place and the loop never ended (firmware hang).
            for(;i<e;++i)if(at+1<cap)out[at++]=s[i];
            continue;
        }
        if(at+1<cap)out[at++]=s[i];
        ++i;
    }
    if(cap)out[at<cap?at:cap-1]=0;
    return at;
}
}  // namespace hakefi::dictionary
