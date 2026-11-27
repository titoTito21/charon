// What the selected entry of a boot manager starts (r410). GPL-2.0-or-later.
//
// The reader says, on demand, what the row selected in another boot manager's
// menu would start. The answer comes from that manager's CONFIGURATION ON
// DISK, never from the entry's name: kernel or EFI program, initrd, options,
// root partition (as written: UUID=..., /dev/..., LABEL=...), the volume the
// manager reads the file from, the version (the config's own `version`, or the
// digits in the kernel file name: vmlinuz-6.8.0-45-generic -> 6.8.0-45),
// whether the entry opens a submenu, is the default, and a risk class proven
// by config words only (recovery / single-user, firmware setup, memory test,
// installer). Unknown fields stay empty and are not spoken.
//
// Read only. No firmware calls: the caller reads the files and passes their
// bytes (text need not be NUL-terminated; nothing is read outside
// [text,text+size)). No heap, no STL, no exceptions. The same code runs in
// tests/CharonEntryInfoTests.cpp.
//
// Reused from CharonManagerProfiles.h (namespace hakefi::managers): the line
// splitter config::Lines, config::Space, config::Find and Utf8To16. Its
// StartsWith / Number are NOT used here because they read up to the length of
// the keyword without an end pointer (they rely on the NUL that ReadFile
// appends); this header has bounded versions (Key, Num).
//
// Syntax facts and where each was checked (2026-11-24; the list with notes is
// in work/r410-evidence/web/entryinfo-sources.txt):
//  GRUB    https://cgit.git.savannah.gnu.org/cgit/grub.git/plain/docs/grub.texi
//          menuentry title [--class=class ...] [--users=users] [--unrestricted]
//          [--hotkey=key] [--id=id] [arg ...] { command; ... }; submenu title
//          ... { menu entries ... }; quoting (single quotes literal, double
//          quotes keep $ and \, backslash-newline continues a line); `default`
//          is a number counted from 0 at each level, a title or an id, levels
//          joined with '>'; saved_entry; fwsetup [--is-supported]; search
//          [--file|--label|--fs-uuid] [--set [var]] name (default var root);
//          chainloader [--force] file [args]; linux file ... (rest = kernel
//          command line); linux16, initrd, initrd16, configfile, source.
//          linuxefi / initrdefi are NOT in the upstream manual (distribution
//          patches); they are read like linux / initrd.
//  BLS     https://uapi-group.org/specifications/specs/boot_loader_specification/
//          Type #1 keys title, version, machine-id, sort-key, linux, initrd
//          (repeatable), efi, options (repeatable), uki, devicetree,
//          architecture; key, spaces, value; '#' comments; loader/entries/*.conf.
//  loader.conf https://www.freedesktop.org/software/systemd/man/latest/loader.conf.html
//          default (glob on the entry id, "@saved"), timeout; automatic entry
//          titles (auto-reboot-to-firmware-setup = "Reboot Into Firmware
//          Interface", auto-windows = "Windows Boot Manager", ...).
//  Limine  https://raw.githubusercontent.com/limine-bootloader/limine/v12.x/CONFIG.md
//          /Title, //Sub (more slashes = deeper), '+' after the slashes,
//          option: value (names case-insensitive), '#' comment lines, timeout,
//          default_entry (1-based index or path, 1 when unset),
//          remember_last_entry, protocol (linux, limine, multiboot,
//          multiboot1, multiboot2, efi, uefi, efi_chainload, efi_boot_entry,
//          bios, bios_chainload), path / kernel_path / image_path, cmdline /
//          kernel_cmdline, module_path (repeatable), resource(arg):/path.
//          CONFIG.md does not describe the old limine.cfg (":Title",
//          KEY=VALUE); such a file is reported as unsupported.
//  rEFInd  https://www.rodsbooks.com/refind/configfile.html (menuentry, volume,
//          loader, initrd, firmware_bootnum, ostype MacOS|Linux|ELILO|Windows|
//          XOM, options, disabled, submenuentry {loader, initrd, graphics,
//          options, add_options, disabled}; a bare `initrd` / `options` in a
//          submenu removes the inherited value; timeout, default_selection,
//          include) and https://www.rodsbooks.com/refind/linux.html
//          (refind_linux.conf: "title" "options", first line = default).
//  Syslinux https://raw.githubusercontent.com/geneC/syslinux/master/doc/syslinux.txt
//          (LABEL, KERNEL, LINUX, APPEND, INITRD, DEFAULT, INCLUDE, TIMEOUT in
//          1/10 s, file types by extension) and .../doc/menu.txt (MENU LABEL
//          with ^ hotkey, MENU DEFAULT, MENU HIDE, MENU DISABLE, MENU BEGIN /
//          MENU END, MENU GOTO, MENU INCLUDE).
//  OpenCore https://raw.githubusercontent.com/acidanthera/OpenCorePkg/master/Docs/Configuration.tex
//          Misc -> Entries and Misc -> Tools, entry properties Arguments,
//          Auxiliary, Comment, Enabled, Flavour, Name, Path (Tools: relative
//          to OC/Tools).
//  Recovery words: systemd(1), https://raw.githubusercontent.com/systemd/systemd/main/man/systemd.xml
//          (emergency, rd.emergency = emergency.target; rescue, rd.rescue,
//          single, 1 = rescue.target; systemd.unit=) and sysvinit init(8),
//          https://raw.githubusercontent.com/slicer69/sysvinit/main/man/init.8
//          ("-s, S, single: single user mode boot"; "-b, emergency").
//  NOT checked on the web: Ubuntu's kernel word `recovery`, the installer
//  words (inst.stage2= / inst.repo= / inst.ks= of Anaconda, `autoinstall`),
//  GRUB's behaviour when `default` is unset (no entry is marked then).
//  Clover's config.plist custom entries are not read.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "CharonManagerProfiles.h"
namespace hakefi::entryinfo {
namespace cfg=hakefi::managers::config;
constexpr uint16_t noParent=0xffff,anyParent=0xfffe;
constexpr size_t noMatch=SIZE_MAX;
// What the entry does.
enum Kind : uint8_t {kindUnknown=0,kindLinux,kindEfi,kindSubmenu,kindFirmware,kindConfig,kindCommands,kindKernel,
    kindBootSector,kindBootOption,kindPowerOff,kindReboot,kindLocalBoot,kindModule};
// Risk class, from config words only.
enum Risk : uint8_t {riskNone=0,riskRecovery,riskFirmwareSetup,riskMemoryTest,riskInstaller};
struct Entry {
    char16_t title[96]={};
    char16_t program[128]={};   // kernel, EFI program, config file, or the commands (kindCommands)
    char16_t initrd[128]={};
    char16_t options[256]={};
    char16_t root[96]={};       // the kernel's root= as written
    char16_t volume[96]={};     // where the manager reads the file: UUID=..., LABEL=..., (hd0,gpt1), boot(), a rEFInd volume
    char16_t version[32]={};
    char16_t system[48]={};     // as written: GRUB --class, BLS sort-key, rEFInd ostype, OpenCore Flavour
    char16_t id[96]={};         // GRUB --id, entry file name without .conf, Syslinux LABEL
    bool submenu=false,isDefault=false,disabled=false,tool=false;
    uint8_t depth=0,risk=riskNone,kind=kindUnknown;
    uint16_t parent=noParent,children=0;
};
struct Info {
    long timeout=-1;             // seconds; -1 unknown
    bool truncated=false;        // more entries than the buffer holds
    bool defaultSaved=false;     // the default comes from saved state (grubenv variable, @saved, remember_last_entry)
    bool unsupported=false;      // a syntax this header does not read (old limine.cfg)
    bool usesBls=false;          // GRUB `blscfg`: entries are in loader/entries/*.conf
    char16_t defaultText[96]={}; // the default as written
    static constexpr size_t maxIncludes=8;
    char16_t include[maxIncludes][128]={};size_t includeCount=0;   // files the caller may load and parse too
};
// ---- text helpers ---------------------------------------------------------
inline size_t Len(const char16_t* s) {size_t n=0;while(s[n])++n;return n;}
inline char16_t Lower(char16_t c) {
    if(c>=u'A' && c<=u'Z')return char16_t(c+32);
    if(c>=0xc0 && c<=0xde && c!=0xd7)return char16_t(c+32);
    if((c>=0x100 && c<=0x12f) || (c>=0x14a && c<=0x177))return char16_t(c|1);
    if((c>=0x139 && c<=0x148) || (c>=0x179 && c<=0x17e))return (c&1)?char16_t(c+1):c;
    return c;
}
inline bool Alnum(char16_t c) {
    if(c<0x80)return (c>=u'0' && c<=u'9') || (c>=u'a' && c<=u'z') || (c>=u'A' && c<=u'Z');
    return (c>=0xc0 && c<0x2000 && c!=0xd7 && c!=0xf7) || c>=0x3040;
}
template<size_t N> void Copy(char16_t (&dst)[N],const char16_t* src) {size_t i=0;for(;src[i] && i+1<N;++i)dst[i]=src[i];dst[i]=0;}
template<size_t N> void Set(char16_t (&dst)[N],const char* a,const char* b) {
    const size_t n=b>a?managers::Utf8To16(a,size_t(b-a),dst,N):0;dst[n]=0;
    for(size_t i=0;i<n;++i)if(dst[i]<32)dst[i]=u' ';
}
template<size_t N> void Add(char16_t (&dst)[N],const char16_t* separator,const char* a,const char* b) {
    size_t n=Len(dst);
    if(n)for(;*separator && n+1<N;++separator)dst[n++]=*separator;
    dst[n]=0;if(n+1>=N || b<=a)return;
    const size_t m=managers::Utf8To16(a,size_t(b-a),dst+n,N-n);
    for(size_t i=n;i<n+m;++i)if(dst[i]<32)dst[i]=u' ';
}
inline void Trim(const char*& a,const char*& b) {
    while(a<b && (cfg::Space(*a) || *a=='\r'))++a;
    while(b>a && (cfg::Space(b[-1]) || b[-1]=='\r'))--b;
}
inline char LowerA(char c) {return c>='A' && c<='Z'?char(c+32):c;}
// A keyword at l (case-insensitive) followed by the end or white space; on a
// match l moves past it and the spaces after it.
inline bool Key(const char*& l,const char* e,const char* word) {
    const char* p=l;
    for(;*word;++word,++p)if(p>=e || LowerA(*p)!=LowerA(*word))return false;
    if(p<e && !cfg::Space(*p) && *p!='\r')return false;
    while(p<e && cfg::Space(*p))++p;
    l=p;return true;
}
inline bool Same(const char* a,const char* b,const char* word) {
    for(;*word;++word,++a)if(a>=b || LowerA(*a)!=LowerA(*word))return false;
    return a==b;
}
inline bool Prefix(const char* a,const char* b,const char* word) {
    for(;*word;++word,++a)if(a>=b || LowerA(*a)!=LowerA(*word))return false;
    return true;
}
inline bool Num(const char* a,const char* b,long& value) {
    while(a<b && (cfg::Space(*a) || *a=='"' || *a=='\''))++a;
    bool negative=false;if(a<b && *a=='-') {negative=true;++a;}
    long v=0;bool any=false;
    for(;a<b && *a>='0' && *a<='9' && v<100000000;++a) {v=v*10+(*a-'0');any=true;}
    if(any)value=negative?-v:v;
    return any;
}
inline bool AllDigits(const char* a,const char* b) {if(a>=b)return false;for(;a<b;++a)if(*a<'0' || *a>'9')return false;return true;}
// ASCII needle in a UTF-16 text, case-insensitive.
inline bool Contains(const char16_t* text,const char* needle) {
    for(;*text;++text) {
        size_t i=0;for(;needle[i] && text[i] && Lower(text[i])==char16_t(LowerA(needle[i]));++i) {}
        if(!needle[i])return true;
    }
    return false;
}
inline bool SameText(const char16_t* a,const char16_t* b,bool fold) {
    for(;*a && *b;++a,++b)if(fold?Lower(*a)!=Lower(*b):*a!=*b)return false;
    return !*a && !*b;
}
inline bool SameAscii(const char16_t* a,const char* b) {
    for(;*a && *b;++a,++b)if(Lower(*a)!=char16_t(LowerA(*b)))return false;
    return !*a && !*b;
}
// A white-space separated word of `options` equal to `word` (case-sensitive:
// S is single-user mode, s is not documented).
inline bool HasWord(const char16_t* options,const char* word) {
    for(const char16_t* p=options;*p;) {
        while(*p==u' ')++p;
        const char16_t* w=p;while(*p && *p!=u' ')++p;
        size_t i=0;for(;word[i] && w+i<p && w[i]==char16_t(uint8_t(word[i]));++i) {}
        if(!word[i] && w+i==p && p>w)return true;
    }
    return false;
}
// The value of the first word "prefix..." of `options` (root=UUID=x -> UUID=x).
template<size_t N> bool WordValue(const char16_t* options,const char* prefix,char16_t (&out)[N]) {
    for(const char16_t* p=options;*p;) {
        while(*p==u' ')++p;
        const char16_t* w=p;while(*p && *p!=u' ')++p;
        size_t i=0;for(;prefix[i] && w+i<p && w[i]==char16_t(uint8_t(prefix[i]));++i) {}
        if(!prefix[i]) {size_t n=0;for(const char16_t* v=w+i;v<p && n+1<N;++v)out[n++]=*v;out[n]=0;return n>0;}
    }
    return false;
}
// vmlinuz-6.8.0-45-generic -> 6.8.0-45 (digits, dots and "-digits" only).
inline void KernelVersion(const char16_t* program,char16_t (&version)[32]) {
    const char16_t* name=program;
    for(const char16_t* p=program;*p;++p)if(*p==u'/' || *p==u'\\')name=p+1;
    static const char* const prefixes[]={"vmlinuz-","vmlinux-","bzimage-","kernel-","linux-"};
    for(const char* prefix:prefixes) {
        size_t i=0;for(;prefix[i] && name[i] && Lower(name[i])==char16_t(prefix[i]);++i) {}
        if(prefix[i])continue;
        const char16_t* p=name+i;size_t n=0;
        auto digit=[](char16_t c) {return c>=u'0' && c<=u'9';};
        if(!digit(*p))return;
        while(n+1<32) {
            if(digit(*p))version[n++]=*p++;
            else if((*p==u'.' || *p==u'-') && digit(p[1]))version[n++]=*p++;
            else break;
        }
        version[n]=0;return;
    }
}
// Fills what follows from the fields already set: root, version, risk.
inline void Classify(Entry& e) {
    if(e.kind==kindLinux) {
        if(!e.root[0])WordValue(e.options,"root=",e.root);
        if(!e.version[0])KernelVersion(e.program,e.version);
    }
    if(e.kind==kindEfi && !e.system[0] && Contains(e.program,"bootmgfw.efi"))Copy(e.system,u"Windows Boot Manager");
    if(e.kind==kindFirmware) {e.risk=riskFirmwareSetup;return;}
    if(e.kind==kindSubmenu || e.kind==kindCommands || e.kind==kindConfig)return;
    if(Contains(e.program,"memtest")) {e.risk=riskMemoryTest;return;}
    if(e.kind!=kindLinux)return;
    const char16_t* o=e.options;
    char16_t unit[48]={};
    const bool hasUnit=WordValue(o,"systemd.unit=",unit) || WordValue(o,"rd.systemd.unit=",unit);
    if(HasWord(o,"single") || HasWord(o,"S") || HasWord(o,"-s") || HasWord(o,"1") || HasWord(o,"recovery") || HasWord(o,"rescue") || HasWord(o,"rd.rescue")
        || HasWord(o,"emergency") || HasWord(o,"rd.emergency") || HasWord(o,"-b")
        || (hasUnit && (SameAscii(unit,"rescue.target") || SameAscii(unit,"emergency.target")))) {e.risk=riskRecovery;return;}
    char16_t value[8];
    if(WordValue(o,"inst.stage2=",value) || WordValue(o,"inst.repo=",value) || WordValue(o,"inst.ks=",value) || HasWord(o,"autoinstall"))e.risk=riskInstaller;
}
// Classifies the entries [from,count) and counts the children of their parents.
inline void Finish(Entry* out,size_t from,size_t count) {
    for(size_t i=from;i<count;++i) {
        Classify(out[i]);
        if(out[i].parent<count)++out[out[i].parent].children;
    }
}
inline void AddInclude(Info& info,const char* a,const char* b) {
    if(info.includeCount>=Info::maxIncludes || b<=a) {if(b>a)info.truncated=true;return;}
    Set(info.include[info.includeCount++],a,b);
}
// ---- GRUB -----------------------------------------------------------------
namespace grub {
struct Cursor {
    const char* p;const char* end;
    bool AtEnd() const {return p>=end;}
    // The next word of the current command, unquoted as GRUB does ($variables
    // stay as written). 0: the command ended (its newline or ';' is consumed),
    // 1: a word, 2: a lone '{', 3: a lone '}'.
    int Next(char* out,size_t capacity,size_t& n) {
        n=0;out[0]=0;
        for(;;) {
            while(p<end && (*p==' ' || *p=='\t' || *p=='\r'))++p;
            if(p+1<end && *p=='\\' && p[1]=='\n') {p+=2;continue;}
            if(p+2<end && *p=='\\' && p[1]=='\r' && p[2]=='\n') {p+=3;continue;}
            break;
        }
        if(p>=end)return 0;
        if(*p=='\n' || *p==';') {++p;return 0;}
        if(*p=='#') {while(p<end && *p!='\n')++p;if(p<end)++p;return 0;}
        if((*p=='{' || *p=='}') && (p+1>=end || p[1]==' ' || p[1]=='\t' || p[1]=='\r' || p[1]=='\n' || p[1]==';')) {return *p++=='{'?2:3;}
        auto put=[&](char c) {if(n+1<capacity)out[n++]=c;};
        while(p<end) {
            const char c=*p;
            if(c==' ' || c=='\t' || c=='\r' || c=='\n' || c==';')break;
            if(c=='\'') {++p;while(p<end && *p!='\'')put(*p++);if(p<end)++p;continue;}
            if(c=='"') {
                ++p;
                while(p<end && *p!='"') {
                    if(*p=='\\' && p+1<end) {
                        const char d=p[1];
                        if(d=='$' || d=='"' || d=='\\') {put(d);p+=2;continue;}
                        if(d=='\n') {p+=2;continue;}
                    }
                    put(*p++);
                }
                if(p<end)++p;continue;
            }
            if(c=='\\' && p+1<end) {if(p[1]!='\n')put(p[1]);p+=2;continue;}
            put(c);++p;
        }
        out[n]=0;return 1;
    }
};
inline bool Eq(const char* w,const char* word) {for(;*w && *word;++w,++word)if(*w!=*word)return false;return !*w && !*word;}
inline bool Pre(const char* w,const char* word) {for(;*word;++w,++word)if(*w!=*word)return false;return true;}
inline bool Quiet(const char* w) {
    static const char* const words[]={"insmod","rmmod","load_video","recordfail","savedefault","gfxmode","terminal_output","terminal_input","loadfont","export",
        "true","false","if","then","else","elif","fi","[","test","set","unset","save_env","load_env","background_image","font"};
    for(const char* q:words)if(Eq(w,q))return true;
    for(const char* p=w;*p;++p)if(*p=='=')return true;
    return Pre(w,"search");
}
// The default named by `value` ("2", "Title", "id", "Advanced>1") among the
// entries [from,count).
inline void ResolveDefault(Entry* out,size_t from,size_t count,const char16_t* value) {
    uint16_t parent=noParent;const char16_t* p=value;
    for(;;) {
        char16_t part[96];size_t n=0;
        while(*p && *p!=u'>') {if(n+1<96)part[n++]=*p;++p;}
        part[n]=0;if(!n)return;
        bool number=true;size_t index=0;
        for(size_t i=0;i<n;++i) {if(part[i]<u'0' || part[i]>u'9' || i>5)number=false;else index=index*10+size_t(part[i]-u'0');}
        size_t found=noMatch,seen=0;
        for(size_t i=from;i<count && found==noMatch;++i) {
            if(out[i].parent!=parent)continue;
            if(number?seen==index:(SameText(out[i].title,part,false) || (out[i].id[0] && SameText(out[i].id,part,false))))found=i;
            ++seen;
        }
        if(found==noMatch)return;
        if(!*p) {out[found].isDefault=true;return;}
        if(!out[found].submenu)return;
        parent=uint16_t(found);++p;
    }
}
}  // namespace grub
// grub.cfg. Entries are appended at out[have]; the new total is returned.
// `configfile` / `source` targets are listed in info->include (not followed).
inline size_t ParseGrub(const char* text,size_t size,Entry* out,size_t max,Info* info=nullptr,size_t have=0) {
    using namespace grub;
    Info local;Info& I=info?*info:local;
    Cursor c{text,text+size};
    uint16_t blockEntry[16];uint8_t blockType[16];size_t depth=0,over=0;   // type 0 other, 1 menuentry, 2 submenu
    size_t count=have,n=0;
    char w[512];char16_t commands[128]={};char16_t defaultText[96]={};bool haveDefault=false,ended=false;
    auto push=[&](uint8_t type,uint16_t index) {if(depth<16) {blockType[depth]=type;blockEntry[depth]=index;++depth;} else ++over;};
    auto pop=[&]() {
        if(over) {--over;return;}
        if(!depth)return;
        --depth;
        if(blockType[depth]==1 && blockEntry[depth]!=noParent) {
            Entry& e=out[blockEntry[depth]];
            if(e.kind==kindUnknown && commands[0]) {e.kind=kindCommands;Copy(e.program,commands);}
        }
    };
    auto word=[&]()->bool {
        if(ended)return false;
        for(;;) {const int t=c.Next(w,sizeof(w),n);if(t==1)return true;if(t==0) {ended=true;return false;}if(t==2)push(0,noParent);else pop();}
    };
    auto inEntry=[&]() {for(size_t i=depth;i--;)if(blockType[i]==1)return true;return false;};
    auto current=[&]()->Entry* {for(size_t i=depth;i--;)if(blockType[i]==1)return blockEntry[i]==noParent?nullptr:&out[blockEntry[i]];return nullptr;};
    auto generic=[&](const char* k) {return Eq(k,"gnu-linux") || Eq(k,"gnu") || Eq(k,"os");};
    while(!c.AtEnd()) {
        const int t=c.Next(w,sizeof(w),n);
        if(t==0)continue;
        if(t==2) {push(0,noParent);continue;}
        if(t==3) {pop();continue;}
        ended=false;
        const bool isMenu=Eq(w,"menuentry"),isSub=Eq(w,"submenu");
        if(isMenu || isSub) {
            uint16_t index=noParent;Entry* e=nullptr;
            if(inEntry()) {}   // not valid GRUB: an entry inside an entry is skipped
            else if(count<max) {
                index=uint16_t(count);e=&out[count++];*e=Entry{};
                for(size_t i=depth;i--;)if(blockType[i]==2) {if(e->parent==noParent)e->parent=blockEntry[i];++e->depth;}
                e->submenu=isSub;if(isSub)e->kind=kindSubmenu;
            } else I.truncated=true;
            bool titled=false,opened=false,nextId=false,nextClass=false,skip=false;
            for(;;) {
                const int h=c.Next(w,sizeof(w),n);
                if(h==0)break;
                if(h==2) {opened=true;break;}
                if(h==3) {pop();continue;}
                if(!e)continue;
                if(nextId) {Set(e->id,w,w+n);nextId=false;continue;}
                if(nextClass) {if(!e->system[0] && !generic(w))Set(e->system,w,w+n);nextClass=false;continue;}
                if(skip) {skip=false;continue;}
                if(Eq(w,"--id") || Eq(w,"$menuentry_id_option") || Eq(w,"${menuentry_id_option}")) {nextId=true;continue;}   // grub-mkconfig writes the variable; it expands to --id
                if(Pre(w,"--id=")) {Set(e->id,w+5,w+n);continue;}
                if(Eq(w,"--class")) {nextClass=true;continue;}
                if(Pre(w,"--class=")) {if(!e->system[0] && !generic(w+8))Set(e->system,w+8,w+n);continue;}
                if(Eq(w,"--users") || Eq(w,"--hotkey")) {skip=true;continue;}
                if(Pre(w,"--"))continue;
                if(!titled) {Set(e->title,w,w+n);titled=true;}
            }
            if(opened)push(isSub?2:1,index);
            if(isMenu)commands[0]=0;
            continue;
        }
        char command[40];size_t k=0;for(;w[k] && k+1<sizeof(command);++k)command[k]=w[k];command[k]=0;
        Entry* e=current();
        if(e) {
            if(Eq(command,"linux") || Eq(command,"linuxefi") || Eq(command,"linux16")) {
                if(word() && !e->program[0]) {
                    Set(e->program,w,w+n);e->kind=kindLinux;e->options[0]=0;
                    while(word())Add(e->options,u" ",w,w+n);
                }
            } else if(Eq(command,"initrd") || Eq(command,"initrdefi") || Eq(command,"initrd16")) {
                const bool take=!e->initrd[0];
                while(word())if(take)Add(e->initrd,u" ",w,w+n);
            } else if(Eq(command,"chainloader")) {
                bool got=false,take=false;
                while(word()) {
                    if(!got && Pre(w,"--"))continue;
                    if(!got) {
                        got=true;
                        if(!e->program[0]) {
                            take=true;Set(e->program,w,w+n);e->options[0]=0;
                            e->kind=(n>=2 && w[n-2]=='+' && w[n-1]>='0' && w[n-1]<='9')?kindBootSector:kindEfi;   // block list "+1": a boot sector
                        }
                    } else if(take)Add(e->options,u" ",w,w+n);
                }
            } else if(Eq(command,"search") || Eq(command,"search.fs_uuid") || Eq(command,"search.fs_label")) {
                int mode=Eq(command,"search.fs_uuid")?1:Eq(command,"search.fs_label")?2:0;
                const bool dotted=mode!=0;
                bool set=false,bare=false,rootVar=true;char first[160]={};char second[160]={};char lastWord[160]={};size_t positional=0;
                auto keep=[&](char* to) {size_t i=0;for(;w[i] && i+1<160;++i)to[i]=w[i];to[i]=0;};
                while(word()) {
                    if(!dotted && w[0]=='-') {
                        if(Eq(w,"--fs-uuid") || Eq(w,"-u"))mode=1;
                        else if(Eq(w,"--label") || Eq(w,"-l"))mode=2;
                        else if(Eq(w,"--file") || Eq(w,"-f"))mode=3;
                        else if(Eq(w,"--set") || Eq(w,"-s")) {set=true;bare=true;}
                        else if(Pre(w,"--set=")) {set=true;rootVar=Eq(w+6,"root");}
                        continue;
                    }
                    keep(lastWord);if(positional==0)keep(first);else if(positional==1)keep(second);
                    ++positional;
                }
                const char* name=lastWord;
                if(dotted) {name=first;set=positional>=2;rootVar=Eq(second,"root");}   // search.fs_uuid NAME VAR [hints]
                else if(bare && positional>=2)rootVar=Eq(first,"root");               // --set VAR NAME
                if(set && rootVar && name[0] && !e->volume[0] && (mode==1 || mode==2)) {
                    Copy(e->volume,mode==1?u"UUID=":u"LABEL=");
                    size_t m=0;while(name[m])++m;
                    Add(e->volume,u"",name,name+m);
                }
            } else if(Eq(command,"fwsetup")) {
                bool query=false;while(word())if(Eq(w,"--is-supported"))query=true;
                if(!query && e->kind==kindUnknown)e->kind=kindFirmware;
            } else if(Eq(command,"configfile") || Eq(command,"source") || Eq(command,".") || Eq(command,"extract_entries_configfile") || Eq(command,"extract_entries_source")) {
                if(word()) {AddInclude(I,w,w+n);if(e->kind==kindUnknown) {e->kind=kindConfig;Set(e->program,w,w+n);}}
            } else if(Eq(command,"multiboot") || Eq(command,"multiboot2") || Eq(command,"kfreebsd") || Eq(command,"knetbsd") || Eq(command,"kopenbsd") || Eq(command,"xnu_kernel") || Eq(command,"xnu_kernel64")) {
                if(word() && !e->program[0]) {Set(e->program,w,w+n);e->kind=kindKernel;e->options[0]=0;while(word())Add(e->options,u" ",w,w+n);}
            } else if(Eq(command,"halt")) {if(e->kind==kindUnknown)e->kind=kindPowerOff;}
            else if(Eq(command,"reboot")) {if(e->kind==kindUnknown)e->kind=kindReboot;}
            else if(Eq(command,"set") || Pre(command,"root=")) {
                const char* v=nullptr;
                if(Eq(command,"set")) {if(word() && Pre(w,"root="))v=w+5;}
                else {size_t i=0;for(;command[i];++i)w[i]=command[i];w[i]=0;n=i;v=w+5;}
                if(v && *v && !e->volume[0])Set(e->volume,v,w+n);
            } else if(!Quiet(command)) {
                size_t m=0;while(command[m])++m;
                Add(commands,u"; ",command,command+m);
                while(word()) {size_t at=Len(commands);if(at+1<128) {commands[at++]=u' ';commands[at]=0;managers::Utf8To16(w,n,commands+at,128-at);}}
            }
        } else if(!inEntry()) {
            const char* name=nullptr;
            if(Eq(command,"set")) {if(word())name=w;}
            else {size_t i=0;for(;command[i];++i)w[i]=command[i];w[i]=0;n=i;name=w;}
            if(name && Pre(name,"default=")) {
                const char* v=name+8;bool variable=false;for(const char* q=v;*q;++q)if(*q=='$')variable=true;
                if(variable)I.defaultSaved=true;
                else {Set(defaultText,v,w+n);haveDefault=true;}
            } else if(name && Pre(name,"timeout=")) {long seconds=0;if(Num(name+8,w+n,seconds))I.timeout=seconds;}
            else if(Eq(command,"configfile") || Eq(command,"source") || Eq(command,".") || Eq(command,"extract_entries_configfile") || Eq(command,"extract_entries_source")) {if(word())AddInclude(I,w,w+n);}
            else if(Eq(command,"blscfg"))I.usesBls=true;
        }
        while(word()) {}
    }
    while(depth || over)pop();
    if(haveDefault) {Copy(I.defaultText,defaultText);ResolveDefault(out,have,count,defaultText);}
    Finish(out,have,count);
    return count;
}
// ---- Boot Loader Specification / systemd-boot ----------------------------
// One loader/entries/*.conf file; `fileName` (may be nullptr) gives the entry
// id. Appends at most one entry; returns the new total.
inline size_t ParseSystemdBoot(const char* text,size_t size,const char16_t* fileName,Entry* out,size_t max,Info* info=nullptr,size_t have=0) {
    Info local;Info& I=info?*info:local;
    if(have>=max) {I.truncated=true;return have;}
    Entry e;bool any=false;
    cfg::Lines(text,size,[&](const char* l,const char* end) {
        const char* v=l;
        auto value=[&](const char* key)->bool {const char* p=l;if(!Key(p,end,key))return false;v=p;const char* b=end;Trim(v,b);end=b;return true;};
        if(value("title")) {Set(e.title,v,end);any=true;}
        else if(value("version"))Set(e.version,v,end);
        else if(value("sort-key"))Set(e.system,v,end);
        else if(value("linux")) {Set(e.program,v,end);e.kind=kindLinux;any=true;}
        else if(value("uki")) {Set(e.program,v,end);e.kind=kindLinux;any=true;}
        else if(value("efi")) {if(e.kind==kindUnknown) {Set(e.program,v,end);e.kind=kindEfi;}any=true;}
        else if(value("initrd"))Add(e.initrd,u" ",v,end);
        else if(value("options"))Add(e.options,u" ",v,end);
    });
    if(!any)return have;
    if(fileName) {
        const char16_t* name=fileName;for(const char16_t* p=fileName;*p;++p)if(*p==u'/' || *p==u'\\')name=p+1;
        Copy(e.id,name);
        size_t n=Len(e.id);
        if(n>5 && e.id[n-5]==u'.' && Lower(e.id[n-4])==u'c' && Lower(e.id[n-3])==u'o' && Lower(e.id[n-2])==u'n' && Lower(e.id[n-1])==u'f')e.id[n-5]=0;
    }
    if(!e.title[0])Copy(e.title,e.id);
    out[have]=e;Finish(out,have,have+1);
    return have+1;
}
inline bool Glob(const char16_t* pattern,const char16_t* text,int depth=0) {
    if(depth>8)return false;
    for(;*pattern;++pattern,++text) {
        if(*pattern==u'*') {
            while(*pattern==u'*')++pattern;
            if(!*pattern)return true;
            for(;*text;++text)if(Glob(pattern,text,depth+1))return true;
            return false;
        }
        if(!*text)return false;
        if(*pattern==u'?')continue;
        if(*pattern==u'[') {
            const char16_t* q=pattern+1;bool hit=false,closed=false;
            for(;*q;++q) {
                if(*q==u']' && q>pattern+1) {closed=true;break;}
                if(q[1]==u'-' && q[2] && q[2]!=u']') {if(Lower(*text)>=Lower(*q) && Lower(*text)<=Lower(q[2]))hit=true;q+=2;}
                else if(Lower(*q)==Lower(*text))hit=true;
            }
            if(!closed || !hit)return false;
            pattern=q;continue;
        }
        if(Lower(*pattern)!=Lower(*text))return false;
    }
    return !*text;
}
// loader/loader.conf: timeout and default. The default is marked when its
// pattern matches exactly one of the entries (systemd-boot's own sorting, and
// its automatic entries, are not known here).
inline void ApplyLoaderConf(const char* text,size_t size,Entry* out,size_t count,Info* info=nullptr) {
    Info local;Info& I=info?*info:local;
    cfg::Lines(text,size,[&](const char* l,const char* end) {
        const char* v=l;
        if(Key(v,end,"default")) {const char* b=end;Trim(v,b);Set(I.defaultText,v,b);}
        else if(Key(v,end,"timeout")) {
            const char* b=end;Trim(v,b);long seconds=0;
            if(Same(v,b,"menu-force"))I.timeout=-1;
            else if(Same(v,b,"menu-hidden") || Same(v,b,"menu-disabled"))I.timeout=0;
            else if(Num(v,b,seconds))I.timeout=seconds;
        }
    });
    if(!I.defaultText[0])return;
    if(SameAscii(I.defaultText,"@saved")) {I.defaultSaved=true;return;}
    char16_t pattern[96];Copy(pattern,I.defaultText);
    size_t n=Len(pattern);
    if(n>5 && pattern[n-5]==u'.' && SameAscii(pattern+n-4,"conf"))pattern[n-5]=0;
    size_t found=noMatch,hits=0;
    for(size_t i=0;i<count;++i)if(out[i].id[0] && Glob(pattern,out[i].id)) {found=i;++hits;}
    if(hits==1)out[found].isDefault=true;
}
// The entries systemd-boot adds by itself (loader.conf(5) names their titles).
// True when `screenLabel` is one of them; `e` then says what it does.
inline bool SystemdAutoEntry(const char16_t* screenLabel,Entry& e) {
    static const struct {const char16_t* title;const char16_t* id;uint8_t kind;} rows[]={
        {u"Reboot Into Firmware Interface",u"auto-reboot-to-firmware-setup",kindFirmware},
        {u"Windows Boot Manager",u"auto-windows",kindEfi},
        {u"EFI Shell",u"auto-efi-shell",kindEfi},
        {u"EFI Default Loader",u"auto-efi-default",kindEfi},
        {u"macOS",u"auto-osx",kindEfi},
        {u"Power Off The System",u"auto-poweroff",kindPowerOff},
        {u"Reboot The System",u"auto-reboot",kindReboot}};
    while(*screenLabel==u' ')++screenLabel;
    char16_t label[96];Copy(label,screenLabel);
    for(size_t n=Len(label);n && label[n-1]==u' ';)label[--n]=0;
    for(const auto& row:rows) {
        if(!SameText(label,row.title,true))continue;
        e=Entry{};Copy(e.title,row.title);Copy(e.id,row.id);e.kind=row.kind;
        if(row.kind==kindFirmware)e.risk=riskFirmwareSetup;
        return true;
    }
    return false;
}
// ---- Limine ---------------------------------------------------------------
inline size_t ParseLimine(const char* text,size_t size,Entry* out,size_t max,Info* info=nullptr,size_t have=0) {
    Info local;Info& I=info?*info:local;
    size_t count=have;Entry* e=nullptr;bool skipping=false,oldSyntax=false,directories=false,remember=false;
    uint16_t last[8];for(uint16_t& x:last)x=noParent;
    char defaultEntry[128]={};size_t defaultLength=0;
    cfg::Lines(text,size,[&](const char* l,const char* end) {
        const char* b=end;Trim(l,b);if(l>=b)return;
        if(*l=='/') {
            size_t slashes=0;while(l<b && *l=='/') {++slashes;++l;}
            if(l<b && *l=='+')++l;
            Trim(l,b);
            e=nullptr;skipping=true;
            if(count>=max) {I.truncated=true;return;}
            skipping=false;
            e=&out[count];*e=Entry{};
            const size_t level=slashes-1;
            e->depth=uint8_t(level>7?7:level);
            if(e->depth) {e->parent=last[e->depth-1];if(e->parent!=noParent) {out[e->parent].submenu=true;out[e->parent].kind=kindSubmenu;directories=true;}}
            last[e->depth]=uint16_t(count);for(size_t i=e->depth+1u;i<8;++i)last[i]=noParent;
            Set(e->title,l,b);++count;
            return;
        }
        if(*l==':' && count==have) {oldSyntax=true;return;}   // limine.cfg: ":Title"
        const char* colon=l;while(colon<b && *colon!=':' && *colon!='=')++colon;
        if(colon>=b)return;
        if(*colon=='=') {if(*l!='$' && count==have)oldSyntax=true;return;}   // ${MACRO}=text, or limine.cfg KEY=VALUE
        const char* ke=colon;const char* v=colon+1;const char* ks=l;Trim(ks,ke);Trim(v,b);
        if(!e) {
            if(skipping)return;
            long number=0;
            if(Same(ks,ke,"timeout")) {if(Same(v,b,"no"))I.timeout=-1;else if(Num(v,b,number))I.timeout=number;}
            else if(Same(ks,ke,"default_entry")) {for(defaultLength=0;v+defaultLength<b && defaultLength+1<sizeof(defaultEntry);++defaultLength)defaultEntry[defaultLength]=v[defaultLength];}
            else if(Same(ks,ke,"remember_last_entry"))remember=Same(v,b,"yes");
            return;
        }
        if(Same(ks,ke,"protocol")) {
            if(Same(v,b,"linux"))e->kind=kindLinux;
            else if(Same(v,b,"efi") || Same(v,b,"uefi") || Same(v,b,"efi_chainload"))e->kind=kindEfi;
            else if(Same(v,b,"bios") || Same(v,b,"bios_chainload"))e->kind=kindBootSector;
            else if(Same(v,b,"efi_boot_entry"))e->kind=kindBootOption;
            else e->kind=kindKernel;
        } else if(Same(ks,ke,"path") || Same(ks,ke,"kernel_path") || Same(ks,ke,"image_path")) {
            const char* cut=v;while(cut+1<b && !(cut[0]==')' && cut[1]==':'))++cut;
            if(cut+1<b && v<cut && *v!='/') {Set(e->volume,v,cut+1);Set(e->program,cut+2,b);}
            else Set(e->program,v,b);
        } else if(Same(ks,ke,"cmdline") || Same(ks,ke,"kernel_cmdline"))Set(e->options,v,b);
        else if(Same(ks,ke,"module_path")) {
            const char* cut=v;while(cut+1<b && !(cut[0]==')' && cut[1]==':'))++cut;
            if(cut+1<b && v<cut && *v!='/')Add(e->initrd,u" ",cut+2,b);else Add(e->initrd,u" ",v,b);
        }
    });
    if(oldSyntax && count==have) {I.unsupported=true;return have;}
    if(remember)I.defaultSaved=true;
    Set(I.defaultText,defaultEntry,defaultEntry+defaultLength);
    // default_entry: a 1-based index (only when the menu has no directories:
    // how directories count is not documented) or a path of titles.
    if(count>have) {
        if(!defaultLength) {if(!out[have].submenu)out[have].isDefault=true;}   // "If unspecified, it is 1"
        else if(AllDigits(defaultEntry,defaultEntry+defaultLength)) {
            long index=0;Num(defaultEntry,defaultEntry+defaultLength,index);
            if(!directories && index>=1 && have+size_t(index)<=count)out[have+size_t(index)-1].isDefault=true;
        } else {
            uint16_t parent=noParent;const char* p=defaultEntry;const char* stop=defaultEntry+defaultLength;
            for(;;) {
                char part[128];size_t n=0;bool numbered=false;
                while(p<stop && *p!='/') {if(*p=='\\' && p+1<stop)++p;else if(*p=='#')numbered=true;if(n+1<sizeof(part))part[n++]=*p;++p;}
                if(numbered || !n)break;
                char16_t title[96];Set(title,part,part+n);
                size_t found=noMatch,hits=0;
                for(size_t i=have;i<count;++i)if(out[i].parent==parent && SameText(out[i].title,title,false)) {if(found==noMatch)found=i;++hits;}
                if(hits!=1)break;
                if(p>=stop) {if(!out[found].submenu)out[found].isDefault=true;break;}
                parent=uint16_t(found);++p;
            }
        }
    }
    Finish(out,have,count);
    return count;
}
// ---- rEFInd ---------------------------------------------------------------
namespace refind {
// The next token of a line: white-space separated, "quoted text" ("" inside
// quotes is one quote character).
inline bool Token(const char*& p,const char* e,char* out,size_t capacity,size_t& n) {
    n=0;out[0]=0;
    while(p<e && (cfg::Space(*p) || *p=='\r'))++p;
    if(p>=e || *p=='#')return false;
    if(*p=='"') {
        ++p;
        while(p<e) {
            if(*p=='"') {if(p+1<e && p[1]=='"') {if(n+1<capacity)out[n++]='"';p+=2;continue;}++p;break;}
            if(n+1<capacity)out[n++]=*p;
            ++p;
        }
    } else while(p<e && !cfg::Space(*p) && *p!='\r') {if(n+1<capacity)out[n++]=*p;++p;}
    out[n]=0;return true;
}
}  // namespace refind
// refind.conf: the manual stanzas (rEFInd also shows what it finds itself).
inline size_t ParseRefind(const char* text,size_t size,Entry* out,size_t max,Info* info=nullptr,size_t have=0) {
    Info local;Info& I=info?*info:local;
    size_t count=have;Entry* main=nullptr;Entry* sub=nullptr;uint16_t mainIndex=noParent;bool inMain=false,inSub=false;
    char selection[96]={};size_t selectionLength=0;size_t selectionWords=0;
    char t[300];size_t n=0;
    cfg::Lines(text,size,[&](const char* l,const char* end) {
        const char* p=l;
        if(!refind::Token(p,end,t,sizeof(t),n))return;
        char key[24];size_t k=0;for(;t[k] && k+1<sizeof(key);++k)key[k]=LowerA(t[k]);key[k]=0;
        auto is=[&](const char* word) {return grub::Eq(key,word);};
        const bool has=refind::Token(p,end,t,sizeof(t),n);
        if(is("}")) {if(inSub) {inSub=false;sub=nullptr;} else {inMain=false;main=nullptr;mainIndex=noParent;}return;}
        if(is("menuentry")) {
            inMain=true;inSub=false;main=nullptr;sub=nullptr;mainIndex=noParent;
            if(count>=max) {I.truncated=true;return;}
            mainIndex=uint16_t(count);main=&out[count++];*main=Entry{};
            if(has && !grub::Eq(t,"{"))Set(main->title,t,t+n);
            return;
        }
        if(is("submenuentry")) {
            inSub=true;sub=nullptr;
            if(!main)return;
            if(count>=max) {I.truncated=true;return;}
            sub=&out[count++];*sub=*main;sub->children=0;sub->depth=1;sub->parent=mainIndex;sub->isDefault=false;sub->title[0]=0;
            main=&out[mainIndex];
            if(has && !grub::Eq(t,"{"))Set(sub->title,t,t+n);
            return;
        }
        if(!inMain) {
            long seconds=0;
            if(is("timeout")) {if(has && Num(t,t+n,seconds))I.timeout=seconds;}
            else if(is("default_selection")) {
                if(has) {for(selectionLength=0;selectionLength<n && selectionLength+1<sizeof(selection);++selectionLength)selection[selectionLength]=t[selectionLength];selectionWords=1;
                    while(refind::Token(p,end,t,sizeof(t),n))++selectionWords;}
            } else if(is("include")) {if(has)AddInclude(I,t,t+n);}
            return;
        }
        Entry* e=inSub?sub:main;
        if(!e)return;
        if(is("loader")) {if(has)Set(e->program,t,t+n);}
        else if(is("initrd"))Set(e->initrd,t,t+(has?n:0));
        else if(is("options"))Set(e->options,t,t+(has?n:0));
        else if(is("add_options")) {if(has)Add(e->options,u" ",t,t+n);}
        else if(is("volume")) {if(has)Set(e->volume,t,t+n);}
        else if(is("ostype")) {if(has)Set(e->system,t,t+n);}
        else if(is("disabled"))e->disabled=true;
        else if(is("firmware_bootnum")) {if(has) {Set(e->program,t,t+n);e->kind=kindBootOption;}}
    });
    for(size_t i=have;i<count;++i) {
        Entry& e=out[i];
        if(e.kind==kindBootOption || !e.program[0])continue;
        const bool linux_=e.initrd[0] || SameAscii(e.system,"Linux") || Contains(e.program,"vmlinu") || Contains(e.program,"bzimage");
        e.kind=linux_?kindLinux:kindEfi;
    }
    Set(I.defaultText,selection,selection+selectionLength);
    // default_selection: a substring of a title. Marked only for one plain
    // identifier that fits exactly one manual stanza (numbers, '+', lists and
    // time ranges depend on what rEFInd finds at run time).
    bool plain=selectionLength>0 && selectionWords==1 && !AllDigits(selection,selection+selectionLength);
    for(size_t i=0;i<selectionLength;++i)if(selection[i]==',' || selection[i]=='+')plain=false;
    if(plain) {
        size_t found=noMatch,hits=0;
        for(size_t i=have;i<count;++i)if(!out[i].depth && !out[i].disabled && Contains(out[i].title,selection)) {found=i;++hits;}
        if(hits==1)out[found].isDefault=true;
    }
    Finish(out,have,count);
    return count;
}
// refind_linux.conf beside a kernel: "title" "options" per line; the first
// line is the default. `kernelPath` (may be nullptr) is the kernel the file
// belongs to, known to the caller.
inline size_t ParseRefindLinux(const char* text,size_t size,const char16_t* kernelPath,Entry* out,size_t max,Info* info=nullptr,size_t have=0) {
    Info local;Info& I=info?*info:local;
    size_t count=have;char t[300];size_t n=0;
    cfg::Lines(text,size,[&](const char* l,const char* end) {
        const char* p=l;
        if(!refind::Token(p,end,t,sizeof(t),n) || !n)return;
        if(count>=max) {I.truncated=true;return;}
        Entry& e=out[count];e=Entry{};
        Set(e.title,t,t+n);
        if(refind::Token(p,end,t,sizeof(t),n))Set(e.options,t,t+n);
        for(size_t k=Len(e.options);k && e.options[k-1]==u' ';)e.options[--k]=0;
        e.kind=kindLinux;e.isDefault=count==have;
        if(kernelPath)Copy(e.program,kernelPath);
        ++count;
    });
    Finish(out,have,count);
    return count;
}
// ---- Syslinux -------------------------------------------------------------
inline size_t ParseSyslinux(const char* text,size_t size,Entry* out,size_t max,Info* info=nullptr,size_t have=0) {
    Info local;Info& I=info?*info:local;
    size_t count=have;Entry* e=nullptr;
    uint16_t stack[8];size_t sp=0,lost=0;uint16_t pending=noParent;bool pendingLabelled=false,menuDefault=false,skipping=false;
    static constexpr size_t maxTracked=256;
    bool appended[maxTracked]={};bool linuxWord[maxTracked]={};bool moduleWord[maxTracked]={};
    char globalAppend[256]={};size_t globalAppendLength=0;char defaultLabel[64]={};size_t defaultLength=0;
    auto add=[&]()->Entry* {
        if(count>=max) {I.truncated=true;return nullptr;}
        Entry* x=&out[count];*x=Entry{};x->depth=uint8_t(sp);x->parent=sp?stack[sp-1]:noParent;++count;return x;
    };
    auto caretless=[&](char16_t (&dst)[96],const char* a,const char* b) {
        char plain[200];size_t n=0;for(;a<b && n+1<sizeof(plain);++a)if(*a!='^')plain[n++]=*a;
        Set(dst,plain,plain+n);
    };
    cfg::Lines(text,size,[&](const char* l,const char* end) {
        const char* b=end;Trim(l,b);const char* v=l;
        const size_t index=e?size_t(e-out)-have:maxTracked;
        if(Key(v,b,"menu")) {
            const char* m=v;
            if(Key(m,b,"begin")) {
                e=nullptr;skipping=false;pending=noParent;
                Entry* x=add();
                if(!x) {++lost;return;}
                x->submenu=true;x->kind=kindSubmenu;Set(x->title,m,b);Set(x->id,m,b);
                pending=uint16_t(x-out);pendingLabelled=false;
                if(sp<8)stack[sp++]=pending;else ++lost;
            } else if(Key(m,b,"end")) {e=nullptr;pending=noParent;if(lost)--lost;else if(sp)--sp;}
            else if(Key(m,b,"label")) {
                if(e)caretless(e->title,m,b);
                else if(pending!=noParent && !skipping) {caretless(out[pending].title,m,b);pendingLabelled=true;}
            } else if(Key(m,b,"title")) {if(!e && pending!=noParent && !pendingLabelled && !skipping)caretless(out[pending].title,m,b);}
            else if(Key(m,b,"default")) {if(e) {e->isDefault=true;menuDefault=true;}}
            else if(Key(m,b,"hide") || Key(m,b,"disable")) {if(e)e->disabled=true;}
            else if(Key(m,b,"goto")) {if(e) {e->kind=kindSubmenu;e->submenu=true;Set(e->program,m,b);}}
            else if(Key(m,b,"include")) {const char* f=m;while(f<b && !cfg::Space(*f))++f;AddInclude(I,m,f);}
            return;
        }
        if(Key(v,b,"label")) {
            pending=noParent;skipping=false;
            e=add();if(!e) {skipping=true;return;}
            Set(e->title,v,b);Set(e->id,v,b);
            return;
        }
        if(skipping)return;
        const bool kernel=Key(v,b,"kernel"),linux_=!kernel && Key(v,b,"linux"),com32=!kernel && !linux_ && Key(v,b,"com32");
        if(kernel || linux_ || com32) {
            if(!e)return;
            const char* f=v;while(f<b && !cfg::Space(*f))++f;
            Set(e->program,v,f);
            if(index<maxTracked) {linuxWord[index]=linux_;moduleWord[index]=com32;}
            const char* rest=f;Trim(rest,b);
            if(rest<b) {Set(e->options,rest,b);if(index<maxTracked)appended[index]=true;}
            return;
        }
        if(Key(v,b,"append")) {
            if(!e) {for(globalAppendLength=0;v+globalAppendLength<b && globalAppendLength+1<sizeof(globalAppend);++globalAppendLength)globalAppend[globalAppendLength]=v[globalAppendLength];return;}
            if(index<maxTracked)appended[index]=true;
            if(b-v==1 && *v=='-')e->options[0]=0;else Set(e->options,v,b);
            return;
        }
        if(Key(v,b,"initrd")) {if(e)Set(e->initrd,v,b);return;}
        if(Key(v,b,"localboot")) {if(e) {e->kind=kindLocalBoot;}return;}
        if(Key(v,b,"config")) {if(e) {const char* f=v;while(f<b && !cfg::Space(*f))++f;e->kind=kindConfig;Set(e->program,v,f);AddInclude(I,v,f);}return;}
        if(Key(v,b,"boot") || Key(v,b,"bss")) {if(e) {e->kind=kindBootSector;Set(e->program,v,b);}return;}
        if(Key(v,b,"fdimage") || Key(v,b,"pxe")) {if(e) {e->kind=kindKernel;Set(e->program,v,b);}return;}
        if(Key(v,b,"default")) {for(defaultLength=0;v+defaultLength<b && !cfg::Space(v[defaultLength]) && defaultLength+1<sizeof(defaultLabel);++defaultLength)defaultLabel[defaultLength]=v[defaultLength];return;}
        if(Key(v,b,"timeout")) {long tenths=0;if(Num(v,b,tenths))I.timeout=(tenths+9)/10;return;}
        if(Key(v,b,"include")) {const char* f=v;while(f<b && !cfg::Space(*f))++f;AddInclude(I,v,f);return;}
    });
    for(size_t i=have;i<count;++i) {
        Entry& x=out[i];const size_t index=i-have;
        if(x.kind==kindUnknown && x.program[0]) {
            const size_t n=Len(x.program);
            auto ext=[&](const char* suffix) {size_t m=0;while(suffix[m])++m;return n>=m && SameAscii(x.program+n-m,suffix);};
            if(index<maxTracked && linuxWord[index])x.kind=kindLinux;
            else if((index<maxTracked && moduleWord[index]) || ext(".c32"))x.kind=kindModule;
            else if(ext(".bin") || ext(".bs") || ext(".bss"))x.kind=kindBootSector;
            else if(ext(".0") || ext(".img"))x.kind=kindKernel;
            else if(ext(".efi"))x.kind=kindEfi;
            else x.kind=kindLinux;   // "none or other: Linux kernel image"
        }
        if(x.kind==kindLinux) {
            if(globalAppendLength && !x.options[0] && !(index<maxTracked && appended[index]))Set(x.options,globalAppend,globalAppend+globalAppendLength);
            if(!x.initrd[0])WordValue(x.options,"initrd=",x.initrd);
        }
    }
    Set(I.defaultText,defaultLabel,defaultLabel+defaultLength);
    if(!menuDefault && defaultLength) {
        size_t found=noMatch,hits=0;
        for(size_t i=have;i<count;++i)if(!out[i].submenu && SameText(out[i].id,I.defaultText,true)) {found=i;++hits;}
        if(hits==1)out[found].isDefault=true;
    }
    Finish(out,have,count);
    return count;
}
// ---- OpenCore -------------------------------------------------------------
namespace plist {
// <tag>text</tag> after `from`: the text bounds, entities left in place.
inline bool Text(const char* from,const char* end,const char*& a,const char*& b) {
    const char* open=cfg::Find(from,end,"<string>");
    const char* empty=cfg::Find(from,end,"<string/>");
    const char* next=cfg::Find(from,end,"<key>");
    if(empty && (!open || empty<open) && (!next || empty<next)) {a=b=empty;return true;}
    if(!open || (next && next<open))return false;
    a=open+8;b=cfg::Find(a,end,"</string>");
    return b!=nullptr;
}
template<size_t N> void SetXml(char16_t (&dst)[N],const char* a,const char* b) {
    char plain[400];size_t n=0;
    for(;a<b && n+1<sizeof(plain);) {
        if(*a=='&') {
            static const struct {const char* name;char c;} entities[]={{"&amp;",'&'},{"&lt;",'<'},{"&gt;",'>'},{"&quot;",'"'},{"&apos;",'\''}};
            bool done=false;
            for(const auto& entity:entities)if(Prefix(a,b,entity.name)) {plain[n++]=entity.c;size_t m=0;while(entity.name[m])++m;a+=m;done=true;break;}
            if(done)continue;
        }
        plain[n++]=*a++;
    }
    Set(dst,plain,plain+n);
}
// The first <true/> or <false/> after `from` and before the next <key>.
inline int Bool(const char* from,const char* end) {
    const char* next=cfg::Find(from,end,"<key>");if(!next)next=end;
    if(cfg::Find(from,next,"<true/>"))return 1;
    if(cfg::Find(from,next,"<false/>"))return 0;
    return -1;
}
}  // namespace plist
// OpenCore config.plist: Misc -> Entries and Misc -> Tools (OpenCore also
// shows the systems it finds itself; its default is kept in NVRAM, not here).
inline size_t ParseOpenCore(const char* text,size_t size,Entry* out,size_t max,Info* info=nullptr,size_t have=0) {
    Info local;Info& I=info?*info:local;
    const char* end=text+size;size_t count=have;
    const char* misc=cfg::Find(text,end,"<key>Misc</key>");
    if(!misc)return have;
    if(const char* t=cfg::Find(misc,end,"<key>Timeout</key>")) {
        const char* v=cfg::Find(t,end,"<integer>");const char* next=cfg::Find(t+18,end,"<key>");
        long seconds=0;if(v && (!next || v<next) && Num(v+9,end,seconds))I.timeout=seconds;
    }
    for(int pass=0;pass<2;++pass) {
        const char* key=cfg::Find(misc,end,pass?"<key>Tools</key>":"<key>Entries</key>");
        if(!key)continue;
        const char* p=key+(pass?16:18);
        while(p<end && (*p==' ' || *p=='\t' || *p=='\r' || *p=='\n'))++p;
        if(!Prefix(p,end,"<array>"))continue;   // <array/>: none
        const char* stop=cfg::Find(p,end,"</array>");if(!stop)continue;
        for(;;) {
            const char* dict=cfg::Find(p,stop,"<dict>");if(!dict)break;
            const char* close=cfg::Find(dict,stop,"</dict>");if(!close)break;
            p=close+7;
            if(count>=max) {I.truncated=true;break;}
            Entry& e=out[count];e=Entry{};e.tool=pass==1;e.kind=kindEfi;
            const char* a=nullptr;const char* b=nullptr;
            if(const char* k=cfg::Find(dict,close,"<key>Name</key>"))if(plist::Text(k+15,close,a,b))plist::SetXml(e.title,a,b);
            if(const char* k=cfg::Find(dict,close,"<key>Path</key>"))if(plist::Text(k+15,close,a,b))plist::SetXml(e.program,a,b);
            if(const char* k=cfg::Find(dict,close,"<key>Arguments</key>"))if(plist::Text(k+20,close,a,b))plist::SetXml(e.options,a,b);
            if(const char* k=cfg::Find(dict,close,"<key>Flavour</key>"))if(plist::Text(k+18,close,a,b)) {plist::SetXml(e.system,a,b);if(SameAscii(e.system,"Auto"))e.system[0]=0;}
            if(const char* k=cfg::Find(dict,close,"<key>Enabled</key>"))e.disabled=plist::Bool(k+18,close)!=1;
            else e.disabled=true;   // failsafe: false
            if(e.title[0] || e.program[0])++count;
        }
    }
    Finish(out,have,count);
    return count;
}
// ---- finding the entry of the row on screen ------------------------------
// Exact title, then case-insensitive, then the title inside the label with
// only marks around it ("> Title", "Title  >", "*Title"), then the label as a
// cut-off beginning of one title ("Title, with Li..."). Each step must give
// exactly one entry; two or more is no match (noMatch), never a guess.
// `parent`: only entries of that submenu (noParent = top level), or anyParent.
inline size_t Match(const Entry* entries,size_t count,const char16_t* screenLabel,uint16_t parent=anyParent) {
    if(!entries || !screenLabel)return noMatch;
    char16_t label[160];size_t n=0;
    while(*screenLabel==u' ' || *screenLabel==0xa0)++screenLabel;
    for(;screenLabel[n] && n+1<160;++n)label[n]=screenLabel[n];
    while(n && (label[n-1]==u' ' || label[n-1]==0xa0))--n;
    label[n]=0;if(!n)return noMatch;
    auto usable=[&](const Entry& e) {return e.title[0] && !e.disabled && (parent==anyParent || e.parent==parent);};
    for(int step=0;step<4;++step) {
        size_t found=noMatch,hits=0;
        // the label without marks at both ends, for step 3
        size_t first=0,last=n;
        while(first<last && !Alnum(label[first]))++first;
        while(last>first && !Alnum(label[last-1]) && label[last-1]!=u')' && label[last-1]!=u']')--last;
        if(last>=first+3 && label[last-1]==u'.' && label[last-2]==u'.' && label[last-3]==u'.')last-=3;
        while(last>first && label[last-1]==u' ')--last;
        for(size_t i=0;i<count;++i) {
            const Entry& e=entries[i];if(!usable(e))continue;
            bool hit=false;
            if(step<2)hit=SameText(e.title,label,step==1);
            else if(step==2) {
                const size_t m=Len(e.title);
                for(size_t at=0;at+m<=n && !hit;++at) {
                    bool same=true;
                    for(size_t k=0;k<m && same;++k)same=Lower(label[at+k])==Lower(e.title[k]);
                    if(!same)continue;
                    bool marks=true;
                    for(size_t k=0;k<at && marks;++k)marks=!Alnum(label[k]);
                    for(size_t k=at+m;k<n && marks;++k)marks=!Alnum(label[k]);
                    hit=marks;
                }
            } else {
                const size_t m=Len(e.title),cut=last-first;
                if(cut>=4 && cut<m) {hit=true;for(size_t k=0;k<cut && hit;++k)hit=Lower(label[first+k])==Lower(e.title[k]);}
            }
            if(hit) {if(found==noMatch)found=i;++hits;}
        }
        if(hits==1)return found;
        if(hits>1)return noMatch;
    }
    return noMatch;
}
// ---- the sentence ---------------------------------------------------------
namespace say {
struct Out {
    char16_t* text;size_t capacity,n=0;
    void Put(const char16_t* s) {if(!capacity)return;while(*s && n+1<capacity)text[n++]=*s++;text[n]=0;}
    void Number(unsigned v) {char16_t d[8];size_t k=0;do {d[k++]=char16_t(u'0'+v%10);v/=10;} while(v && k<8);while(k) {char16_t one[2]={d[--k],0};Put(one);}}
};
// The options without root= (said as the root partition) and initrd=.
inline void Options(Out& o,const char16_t* options) {
    bool any=false;
    for(const char16_t* p=options;*p;) {
        while(*p==u' ')++p;
        const char16_t* w=p;while(*p && *p!=u' ')++p;
        if(p==w)break;
        auto starts=[&](const char* prefix) {size_t i=0;for(;prefix[i] && w+i<p && w[i]==char16_t(prefix[i]);++i) {}return !prefix[i];};
        if(starts("root=") || starts("initrd="))continue;
        if(any)o.Put(u" ");
        for(const char16_t* c=w;c<p;++c) {char16_t one[2]={*c,0};o.Put(one);}
        any=true;
    }
}
inline bool HasOptions(const char16_t* options) {char16_t buffer[8];Out o{buffer,8};Options(o,options);return o.n>0;}
// UUID=1234 -> "UUID 1234" (also LABEL=, PARTUUID=, PARTLABEL=).
inline void Root(Out& o,const char16_t* root) {
    static const char* const tags[]={"UUID=","LABEL=","PARTUUID=","PARTLABEL="};
    for(const char* tag:tags) {
        size_t i=0;for(;tag[i] && root[i]==char16_t(tag[i]);++i) {}
        if(tag[i])continue;
        for(size_t k=0;k+1<i;++k) {char16_t one[2]={root[k],0};o.Put(one);}
        o.Put(u" ");o.Put(root+i);return;
    }
    o.Put(root);
}
}  // namespace say
// One or more short sentences, English or Polish. Only what the configuration
// says; empty fields are left out. Returns the length.
inline size_t Describe(const Entry& e,bool polish,char16_t* out,size_t capacity) {
    if(!out || !capacity)return 0;
    out[0]=0;say::Out o{out,capacity};
    const bool pl=polish;
    auto options=[&]() {if(say::HasOptions(e.options)) {o.Put(pl?u", opcje: ":u", options: ");say::Options(o,e.options);}};
    auto volume=[&]() {if(e.volume[0] && !SameAscii(e.volume,"boot()")) {o.Put(pl?u", wolumin ":u", volume ");say::Root(o,e.volume);}};
    auto file=[&](const char16_t* en,const char16_t* po) {if(e.program[0]) {o.Put(pl?po:en);o.Put(e.program);}};
    const bool memoryTest=e.risk==riskMemoryTest && (e.kind==kindLinux || e.kind==kindEfi || e.kind==kindKernel || e.kind==kindModule);
    switch(memoryTest?uint8_t(0xff):e.kind) {
    case 0xff:o.Put(pl?u"Uruchamia test pamięci":u"Starts a memory test");file(u", file ",u", plik ");volume();options();break;
    case kindSubmenu:
        if(!e.children)o.Put(pl?u"Otwiera podmenu":u"Opens a submenu");
        else if(pl) {o.Put(u"Otwiera podmenu z ");o.Number(e.children);o.Put(e.children==1?u" pozycją":u" pozycjami");}
        else {o.Put(u"Opens a submenu with ");o.Number(e.children);o.Put(e.children==1?u" entry":u" entries");}
        break;
    case kindLinux:
        o.Put(pl?u"Uruchamia jądro Linux":u"Starts Linux kernel");
        if(e.version[0]) {o.Put(u" ");o.Put(e.version);}
        if(e.system[0]) {o.Put(pl?u", system ":u", system ");o.Put(e.system);}
        file(u", file ",u", plik ");
        if(e.initrd[0]) {o.Put(u", initrd ");o.Put(e.initrd);}
        if(e.root[0]) {o.Put(pl?u", partycja główna ":u", root partition ");say::Root(o,e.root);}
        else volume();
        options();
        break;
    case kindEfi:
        if(e.system[0]) {o.Put(pl?u"Uruchamia ":u"Starts ");o.Put(e.system);file(u", EFI program ",u", program EFI ");}
        else if(e.program[0]) {o.Put(pl?u"Uruchamia program EFI ":u"Starts the EFI program ");o.Put(e.program);}
        else o.Put(pl?u"Uruchamia program EFI":u"Starts an EFI program");
        volume();options();
        break;
    case kindFirmware:o.Put(pl?u"Otwiera ustawienia oprogramowania układowego":u"Opens the firmware setup");break;
    case kindConfig:o.Put(pl?u"Wczytuje plik menu":u"Loads the menu file");file(u" ",u" ");volume();break;
    case kindCommands:o.Put(pl?u"Wykonuje polecenia: ":u"Runs the commands: ");o.Put(e.program);break;
    case kindKernel:o.Put(pl?u"Uruchamia jądro lub obraz":u"Starts the kernel or image");file(u" ",u" ");if(e.initrd[0]) {o.Put(pl?u", moduły ":u", modules ");o.Put(e.initrd);}volume();options();break;
    case kindBootSector:o.Put(pl?u"Uruchamia sektor rozruchowy":u"Starts a boot sector");file(u", file ",u", plik ");volume();break;
    case kindBootOption:o.Put(pl?u"Uruchamia pozycję rozruchową oprogramowania układowego":u"Starts the firmware boot option");file(u" ",u" ");break;
    case kindPowerOff:o.Put(pl?u"Wyłącza komputer":u"Turns the computer off");break;
    case kindReboot:o.Put(pl?u"Uruchamia komputer ponownie":u"Restarts the computer");break;
    case kindLocalBoot:o.Put(pl?u"Uruchamia system z dysku lokalnego":u"Boots from the local disk");break;
    case kindModule:o.Put(pl?u"Uruchamia moduł Syslinux":u"Starts the Syslinux module");file(u" ",u" ");options();break;
    default:o.Put(pl?u"Konfiguracja nie podaje, co uruchamia ta pozycja":u"The configuration does not say what this entry starts");break;
    }
    o.Put(u".");
    if(e.risk==riskRecovery)o.Put(pl?u" Tryb odzyskiwania lub jednego użytkownika.":u" Recovery or single-user mode.");
    else if(e.risk==riskMemoryTest && !memoryTest)o.Put(pl?u" To jest test pamięci.":u" This is a memory test.");
    else if(e.risk==riskInstaller)o.Put(pl?u" To jest instalator.":u" This is an installer.");
    else if(e.risk==riskFirmwareSetup && e.kind!=kindFirmware)o.Put(pl?u" Otwiera ustawienia oprogramowania układowego.":u" Opens the firmware setup.");
    if(e.tool)o.Put(pl?u" Narzędzie.":u" Tool.");
    if(e.isDefault)o.Put(pl?u" Pozycja domyślna.":u" Default entry.");
    if(e.disabled)o.Put(pl?u" Wyłączona w konfiguracji.":u" Disabled in the configuration.");
    return o.n;
}
}  // namespace hakefi::entryinfo
