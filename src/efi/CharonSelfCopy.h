#pragma once
// r407: Charon's own files, copied into memory soon after Charon starts.
//
// Charon started from a stick or a disc keeps a copy of everything it would
// install - the whole folder \EFI\HAK of its own volume (the screen reader
// CHARONVOICE.EFI, the boot manager, the audio driver, cues, synthesizer
// add-ons, extra drivers, packages) - in memory, so that "Install Charon on
// this computer" (CharonWsSelfInstall.h) works from that copy: the medium may
// be slow, read-only or taken out by then.
//
// The copy is made in small steps from the menu's own loop (Step: a quarter
// of a megabyte per call), a few seconds after the start, so the welcome and
// the first key presses are not held up. Files that belong to the medium and
// not to an installed copy are left out (the remembered Continue target, the
// reader-only marker, a test address for a Windows image).
// When Charon runs from \EFI\BOOT\BOOTX64.EFI and its folder has no
// HAKBOOT.EFI, the running program is stored under that name.
//
// No dependency on the workspace: boot services and the volume's root only.
// GPL-2.0-or-later.
#include <stddef.h>
#include <stdint.h>
#include <efi/boot-services.h>
#include <efi/protocol/file.h>
namespace hakefi {
namespace selfcopy {
constexpr size_t maxItems=600,pathUnits=200,maxFolders=64,stepBytes=256*1024;
constexpr unsigned startAfterCalls=400;      // menu loop turns before the first read
struct Item {
    char16_t path[pathUnits];       // where the file goes on the target volume
    char16_t source[pathUnits];     // where it is on Charon's volume
    uint8_t* data;uint64_t size;
};
struct State {
    Item* items;size_t count,current;
    uint64_t total,done,offset;
    efi_file_protocol* open;
    unsigned calls;
    bool listed,complete,failed;
};
inline State& S() {static State s;return s;}
inline size_t Length(const char16_t* s) {size_t n=0;while(s[n])++n;return n;}
inline bool Put(char16_t* out,const char16_t* a,const char16_t* b) {
    const size_t x=Length(a),y=Length(b);
    if(x+1+y+1>pathUnits)return false;
    for(size_t i=0;i<x;++i)out[i]=a[i];
    out[x]=u'\\';
    for(size_t i=0;i<=y;++i)out[x+1+i]=b[i];
    return true;
}
inline char16_t Fold(char16_t c) {return c>=u'a' && c<=u'z'?char16_t(c-32):c;}
inline bool Same(const char16_t* a,const char16_t* b) {
    for(;;++a,++b) {if(Fold(*a)!=Fold(*b))return false;if(!*a)return true;}
}
// Files of the medium that an installed copy must not inherit.
inline bool Left(const char16_t* name) {
    static const char16_t* const names[]={u"continue.bin",u"continue-set",u"installed-by.txt",u"reader-only",u"windows-iso-url.txt"};
    for(const auto* n:names)if(Same(name,n))return true;
    return false;
}
// Lists \EFI\HAK (folders breadth-first, bounded). False: nothing usable.
inline bool List(efi_boot_services* boot,efi_file_protocol* root) {
    auto& s=S();
    void* memory=nullptr;
    if(boot->AllocatePool(EfiLoaderData,sizeof(Item)*maxItems,&memory)!=EFI_SUCCESS || !memory)return false;
    s.items=static_cast<Item*>(memory);s.count=0;s.total=0;
    static char16_t folders[maxFolders][pathUnits];size_t folderCount=1,folderAt=0;
    {const char16_t* top=u"\\EFI\\HAK";size_t i=0;for(;top[i];++i)folders[0][i]=top[i];folders[0][i]=0;}
    alignas(8) static uint8_t info[1200];
    bool manager=false;
    for(;folderAt<folderCount;++folderAt) {
        efi_file_protocol* dir=nullptr;
        if(root->Open(root,&dir,folders[folderAt],EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !dir) {if(!folderAt)return false;continue;}
        for(unsigned guard=0;guard<5000;++guard) {
            size_t size=sizeof(info);
            if(dir->Read(dir,&size,info)!=EFI_SUCCESS || !size)break;
            const auto* entry=reinterpret_cast<const efi_file_info*>(info);const char16_t* name=entry->FileName;
            if(name[0]==u'.' && (!name[1] || (name[1]==u'.' && !name[2])))continue;
            if(entry->Attribute&EFI_FILE_DIRECTORY) {
                if(folderCount<maxFolders && Put(folders[folderCount],folders[folderAt],name))++folderCount;
                continue;
            }
            if(Left(name) || s.count>=maxItems)continue;
            Item& item=s.items[s.count];
            if(!Put(item.path,folders[folderAt],name))continue;
            for(size_t i=0;i<pathUnits;++i) {item.source[i]=item.path[i];if(!item.path[i])break;}
            item.data=nullptr;item.size=entry->FileSize;
            if(!folderAt && Same(name,u"HAKBOOT.EFI"))manager=true;
            s.total+=item.size;++s.count;
        }
        dir->Close(dir);
    }
    if(!manager && s.count<maxItems) {
        // Charon run from a boot disc: the running program becomes the installed boot manager
        efi_file_protocol* self=nullptr;
        if(root->Open(root,&self,(char16_t*)u"\\EFI\\BOOT\\BOOTX64.EFI",EFI_FILE_MODE_READ,0)==EFI_SUCCESS && self) {
            alignas(8) static uint8_t own[1200];size_t size=sizeof(own);
            static efi_guid infoGuid=EFI_FILE_INFO_GUID;
            if(self->GetInfo(self,&infoGuid,&size,own)==EFI_SUCCESS) {
                Item& item=s.items[s.count];
                const char16_t* to=u"\\EFI\\HAK\\HAKBOOT.EFI";const char16_t* from=u"\\EFI\\BOOT\\BOOTX64.EFI";
                size_t i=0;for(;to[i];++i)item.path[i]=to[i];item.path[i]=0;
                i=0;for(;from[i];++i)item.source[i]=from[i];item.source[i]=0;
                item.data=nullptr;item.size=reinterpret_cast<const efi_file_info*>(own)->FileSize;
                s.total+=item.size;++s.count;
            }
            self->Close(self);
        }
    }
    return s.count!=0;
}
inline bool Ready() {return S().complete;}
inline bool Failed() {return S().failed;}
inline uint64_t Total() {return S().total;}
inline uint64_t Done() {return S().done;}
// One step of the copy. `now`: do not wait for the quiet start (the installer asks for the copy).
// Returns true while there is more to do.
inline bool Step(efi_boot_services* boot,efi_file_protocol* root,bool now=false) {
    auto& s=S();
    if(s.complete || s.failed || !boot || !root)return false;
    if(!now && ++s.calls<startAfterCalls)return true;
    if(!s.listed) {
        s.listed=true;
        if(!List(boot,root)) {s.failed=true;return false;}
        return true;
    }
    if(s.current>=s.count) {s.complete=true;return false;}
    Item& item=s.items[s.current];
    if(!s.open) {
        if(!item.size) {++s.current;return true;}
        void* memory=nullptr;
        if(item.size>0x7fffffffu || boot->AllocatePool(EfiLoaderData,size_t(item.size),&memory)!=EFI_SUCCESS || !memory) {s.failed=true;return false;}
        item.data=static_cast<uint8_t*>(memory);
        if(root->Open(root,&s.open,item.source,EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !s.open) {s.open=nullptr;s.failed=true;return false;}
        s.offset=0;
    }
    size_t part=item.size-s.offset>stepBytes?stepBytes:size_t(item.size-s.offset);
    const size_t asked=part;
    if(s.open->Read(s.open,&part,item.data+s.offset)!=EFI_SUCCESS || part!=asked) {s.open->Close(s.open);s.open=nullptr;s.failed=true;return false;}
    s.offset+=part;s.done+=part;
    if(s.offset>=item.size) {s.open->Close(s.open);s.open=nullptr;++s.current;}
    return true;
}
}  // namespace selfcopy
}  // namespace hakefi
