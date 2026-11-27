#pragma once
// r400: Charon workspace - the views (start page, UEFI program launcher,
// Dired-style file manager, packages, buffer list, calculator, key help),
// the command palette (M-x) and the main loop. See CharonWsCore.h.
// GPL-2.0-or-later.
#include "CharonWsEditor.h"
#include "CharonSynthHost.h"
#include "CharonWsNet.h"
#include "CharonDriverPack.h"   // r408: load storage/filesystem/input/USB-network drivers on demand (only for undriven controllers)
#include "CharonWsDisks.h"
#include "CharonWsAsk.h"
namespace hakefi {
namespace ws {
// ---- UEFI programs --------------------------------------------------------
constexpr size_t maxApps=200,maxRecords=96,appPath=260;
constexpr uint8_t appFavourite=1,appDriver=2,appForeign=4,appPackage=8;
struct App {efi_handle volume;char16_t path[appPath];char16_t title[80];uint8_t flags;size_t record;};
// What the user chose for a program; kept in \EFI\HAK\programs.txt.
struct Record {char16_t device[240];char16_t path[appPath];char16_t args[128];bool favourite;uint32_t recent;};
struct Programs {
    App apps[maxApps];size_t count;bool scanned,truncated;
    Record records[maxRecords];size_t recordCount;uint32_t clock;bool loaded;
    ListView view;
};
inline Programs& P() {static Programs programs;return programs;}
inline const uint8_t* DevicePathOf(efi_handle volume,size_t& bytes) {
    uint8_t* own=nullptr;bytes=0;
    if(U().host->boot->HandleProtocol(volume,&media::pathGuid,(void**)&own)!=EFI_SUCCESS || !own)return nullptr;
    bytes=media::PathLength(own);if(bytes==SIZE_MAX || bytes>1500) {bytes=0;return nullptr;}
    return own;
}
inline void DeviceText(efi_handle volume,char16_t* out,size_t cap) {
    out[0]=0;size_t bytes=0;const uint8_t* path=DevicePathOf(volume,bytes);
    static const char16_t hex[]=u"0123456789abcdef";size_t at=0;
    for(size_t i=0;path && i<bytes && at+3<cap;++i) {out[at++]=hex[path[i]>>4];out[at++]=hex[path[i]&15];}
    out[at]=0;
}
inline size_t FindRecord(const App& app,bool create) {
    auto& p=P();static char16_t device[240];DeviceText(app.volume,device,240);
    for(size_t i=0;i<p.recordCount;++i)if(SameFold(p.records[i].path,app.path) && SameFold(p.records[i].device,device))return i;
    if(!create || p.recordCount>=maxRecords)return SIZE_MAX;
    auto& r=p.records[p.recordCount];memset(&r,0,sizeof(r));Set(r.device,240,device);Set(r.path,appPath,app.path);
    return p.recordCount++;
}
inline void LoadRecords() {
    auto& p=P();if(p.loaded)return;
    p.loaded=true;p.recordCount=0;p.clock=0;
    FileRef ref{U().host->selfDevice,u"\\EFI\\HAK\\programs.txt"};uint8_t* data=nullptr;size_t size=0;
    if(ReadWhole(ref,256*1024,&data,&size)!=EFI_SUCCESS)return;
    char16_t* text=static_cast<char16_t*>(Allocate(size*2+4));
    if(text) {
        const size_t n=Utf8ToUtf16(data,size,text,size+2);
        for(size_t at=0;at<n && p.recordCount<maxRecords;) {
            size_t end=at;while(end<n && text[end]!=u'\n')++end;
            // favourite TAB recent TAB device TAB path TAB arguments
            char16_t* field[5]={};size_t fields=0;field[fields++]=text+at;
            for(size_t i=at;i<end;++i) {if(text[i]==u'\r')text[i]=0;if(text[i]==u'\t') {text[i]=0;if(fields<5)field[fields++]=text+i+1;}}
            if(end<n)text[end]=0;
            if(fields==5 && field[3][0]==u'\\') {
                auto& r=p.records[p.recordCount++];memset(&r,0,sizeof(r));
                r.favourite=field[0][0]==u'1';for(const char16_t* d=field[1];*d>=u'0' && *d<=u'9';++d)r.recent=r.recent*10+(*d-u'0');
                Set(r.device,240,field[2]);Set(r.path,appPath,field[3]);Set(r.args,128,field[4]);
                if(r.recent>p.clock)p.clock=r.recent;
            }
            at=end+1;
        }
        Release(text);
    }
    Release(data);
}
inline efi_status SaveRecords() {
    auto& p=P();const size_t cap=p.recordCount*2200+16;
    char16_t* text=static_cast<char16_t*>(Allocate(cap*2));uint8_t* bytes=static_cast<uint8_t*>(Allocate(cap*3));
    if(!text || !bytes) {Release(text);Release(bytes);return EFI_OUT_OF_RESOURCES;}
    text[0]=0;
    for(size_t i=0;i<p.recordCount;++i) {
        const auto& r=p.records[i];
        if(!r.favourite && !r.recent && !r.args[0])continue;
        Cat(text,cap,r.favourite?u"1\t":u"0\t");CatN(text,cap,r.recent);Cat(text,cap,u"\t");Cat(text,cap,r.device);Cat(text,cap,u"\t");
        Cat(text,cap,r.path);Cat(text,cap,u"\t");Cat(text,cap,r.args);Cat(text,cap,u"\n");
    }
    const size_t n=Utf16ToUtf8(text,Len(text),bytes,cap*3);
    MakeFolders(U().host->selfDevice,u"\\EFI\\HAK");
    FileRef ref{U().host->selfDevice,u"\\EFI\\HAK\\programs.txt"};
    const auto status=WriteWhole(ref,bytes,n);Release(text);Release(bytes);return status;
}
// A readable name for well-known programs; false = use the file name.
inline bool KnownTitle(const char16_t* base,char16_t* out,size_t cap) {
    struct Known {const char16_t* prefix;const char16_t* name;};
    static const Known known[]={{u"bootmgfw",u"Windows Boot Manager"},{u"bootmgr",u"Windows Boot Manager"},{u"memtest",u"Memtest86+"},{u"mt86",u"Memtest86+"},
        {u"shell",u"UEFI Shell"},{u"grub",u"GRUB"},{u"shim",u"shim"},{u"mm",u"MokManager"},{u"fb",u"shim fallback"},{u"systemd-boot",u"systemd-boot"},
        {u"refind",u"rEFInd"},{u"limine",u"Limine"},{u"opencore",u"OpenCore"},{u"clover",u"Clover"},{u"ipxe",u"iPXE"},{u"snponly",u"iPXE"},
        {u"charonvoice",u"Charon voice driver"},{u"charon",u"Charon"},{u"keytool",u"KeyTool"},{u"hashtool",u"HashTool"},{u"fwupd",u"fwupd"},
        {u"ventoy",u"Ventoy"},{u"netboot.xyz",u"netboot.xyz"},{u"gdisk",u"GPT fdisk"},{u"memdiag",u"Windows Memory Diagnostic"},{u"loader",u"FreeBSD loader"},
        {u"haiku_loader",u"Haiku loader"},{u"winload",u"Windows loader"},{u"syslinux",u"Syslinux"},{u"uefi-shell",u"UEFI Shell"},{u"edk2-shell",u"UEFI Shell"}};
    for(const auto& k:known)if(StartsFold(base,k.prefix)) {
        // "mm" and "fb" only as mmx64/fbx64 and the like.
        const size_t n=Len(k.prefix);
        if(n<=2 && !(Fold(base[n])==u'x' || Fold(base[n])==u'a' || Fold(base[n])==u'i'))continue;
        Set(out,cap,k.name);return true;
    }
    return false;
}
inline void TitleFor(App& app) {
    const char16_t* base=BaseName(app.path);static char16_t parent[appPath];ParentOf(app.path,parent,appPath);
    const char16_t* folder=BaseName(parent);
    app.title[0]=0;
    if(SameFold(base,u"bootx64.efi") || SameFold(base,u"bootia32.efi") || SameFold(base,u"bootaa64.efi")) {
        // The default start file: name what it really is.
        size_t bytes=0;const uint8_t* device=DevicePathOf(app.volume,bytes);
        static uint8_t path[2400];const size_t units=Len(app.path)+1,node=4+units*2;
        const char16_t* name=nullptr;
        if(device && bytes+node+4<sizeof(path)) {
            memcpy(path,device,bytes);uint8_t* f=path+bytes;f[0]=4;f[1]=4;f[2]=uint8_t(node);f[3]=uint8_t(node>>8);
            for(size_t i=0;i<units;++i) {f[4+i*2]=uint8_t(app.path[i]);f[5+i*2]=uint8_t(app.path[i]>>8);}
            uint8_t* end=f+node;end[0]=0x7f;end[1]=0xff;end[2]=4;end[3]=0;
            name=ProbeOptionManager(U().host->boot,path,bytes+node+4);
        }
        Set(app.title,80,name && name[0]?name:lastImageWasCharon?u"Charon":T(u"Domyślny program startowy",u"Default start program"));
        Cat(app.title,80,T(u" (plik startowy)",u" (start file)"));
        return;
    }
    if(!KnownTitle(base,app.title,80)) {
        Set(app.title,80,base);size_t n=Len(app.title);if(n>4)app.title[n-4]=0;
    }
    if(folder[0] && !SameFold(folder,u"boot") && !SameFold(folder,u"efi")) {Cat(app.title,80,u" (");Cat(app.title,80,folder);Cat(app.title,80,u")");}
}
// x64 UEFI application? Sets the driver / foreign flags from the PE header.
inline void Classify(App& app) {
    FileRef ref;ref.volume=app.volume;Set(ref.path,pathUnits,app.path);efi_file_protocol* file=nullptr;
    if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)!=EFI_SUCCESS)return;
    alignas(8) static uint8_t head[1024];size_t size=sizeof(head);
    const auto status=file->Read(file,&size,head);file->Close(file);
    if(status!=EFI_SUCCESS || size<0x60 || head[0]!='M' || head[1]!='Z') {app.flags|=appForeign;return;}
    const uint64_t pe=systools::Le32(head+0x3c);
    if(pe+0x5e>size || head[pe]!='P' || head[pe+1]!='E') {if(pe+0x5e<=size)app.flags|=appForeign;return;}
    const uint16_t machine=systools::Le16(head+pe+4),subsystem=systools::Le16(head+pe+24+68);
    if(machine!=0x8664)app.flags|=appForeign;
    if(subsystem==11 || subsystem==12)app.flags|=appDriver;
}
inline bool SkippedFolder(const char16_t* name) {
    static const char16_t* const skipped[]={u"System Volume Information",u"$RECYCLE.BIN",u"Windows",u"Program Files",u"Program Files (x86)",u"ProgramData",
        u"Users",u"Recovery",u"fonts",u"locale",u"themes",u"x86_64-efi",u"i386-efi",u"Resources",u"synths",u"audio"};
    for(const auto* s:skipped)if(SameFold(name,s))return true;
    return name[0]==u'.';
}
// Breadth-first walk of every volume for *.efi files.
inline void ScanPrograms() {
    auto& p=P();p.count=0;p.truncated=false;p.scanned=true;LoadRecords();
    constexpr size_t maxQueue=300;
    struct Walk {char16_t path[maxQueue][200];uint8_t depth[maxQueue];};
    Walk* walk=static_cast<Walk*>(Allocate(sizeof(Walk)));if(!walk)return;
    efi_handle volumes[32];const size_t volumeCount=Volumes(volumes,32);
    alignas(8) static uint8_t buffer[2048];unsigned ticks=0;bool stopped=false;
    for(size_t v=0;v<volumeCount && !stopped;++v) {
        if(hakefi::selfram::IsVolume(volumes[v]) && hakefi::selfram::OriginalDevice())continue;   // r408: the start medium is listed; its copy in memory would list every program twice
        size_t head=0,tail=0;Set(walk->path[tail],200,u"\\");walk->depth[tail++]=0;
        while(head<tail && !stopped) {
            FileRef folder;folder.volume=volumes[v];Set(folder.path,pathUnits,walk->path[head]);const uint8_t depth=walk->depth[head];++head;
            efi_file_protocol* dir=nullptr;if(OpenFile(folder,EFI_FILE_MODE_READ,0,&dir)!=EFI_SUCCESS)continue;
            for(unsigned guard=0;guard<5000;++guard) {
                size_t size=sizeof(buffer);
                if(dir->Read(dir,&size,buffer)!=EFI_SUCCESS || !size)break;
                const auto* info=reinterpret_cast<const efi_file_info*>(buffer);const char16_t* name=info->FileName;
                if(name[0]==u'.' && (!name[1] || (name[1]==u'.' && !name[2])))continue;
                if(info->Attribute&EFI_FILE_DIRECTORY) {
                    if(depth<5 && tail<maxQueue && !SkippedFolder(name) && JoinPath(walk->path[tail],200,folder.path,name))walk->depth[tail++]=uint8_t(depth+1);
                    continue;
                }
                if(!EndsFold(name,u".efi"))continue;
                if(p.count>=maxApps) {p.truncated=true;break;}
                auto& app=p.apps[p.count];memset(&app,0,sizeof(app));app.volume=volumes[v];
                if(!JoinPath(app.path,appPath,folder.path,name))continue;
                // r408: the driver pack's own files are loaded by rules (CharonDriverPack.h), not picked as programs; drivers\manual is.
                if(StartsFold(app.path,u"\\EFI\\HAK\\drivers\\") && !StartsFold(app.path,u"\\EFI\\HAK\\drivers\\manual\\"))continue;
                if(ContainsFold(app.path,u"\\EFI\\HAK\\packages\\") || StartsFold(app.path,u"\\charon-packages\\"))app.flags|=appPackage;
                ++p.count;
            }
            dir->Close(dir);
            if(++ticks%8==0) {Tone(500+ticks%64*10,15);if(EscapePressed())stopped=true;}
            Pump();
        }
    }
    Release(walk);
    for(size_t i=0;i<p.count;++i) {
        auto& app=p.apps[i];Classify(app);TitleFor(app);app.record=FindRecord(app,false);
        if(app.record!=SIZE_MAX && p.records[app.record].favourite)app.flags|=appFavourite;
        Pump();
    }
    // Favourites, then most recently started, then by name.
    auto recent=[&](const App& a) {return a.record!=SIZE_MAX?p.records[a.record].recent:0u;};
    auto before=[&](const App& a,const App& b) {
        const bool fa=(a.flags&appFavourite)!=0,fb=(b.flags&appFavourite)!=0;if(fa!=fb)return fa;
        if(recent(a)!=recent(b))return recent(a)>recent(b);
        const bool da=(a.flags&(appDriver|appForeign))!=0,db=(b.flags&(appDriver|appForeign))!=0;if(da!=db)return db;
        return CompareFold(a.title,b.title)<0;
    };
    for(size_t i=1;i<p.count;++i) {
        static App moving;moving=p.apps[i];size_t j=i;
        while(j && before(moving,p.apps[j-1])) {p.apps[j]=p.apps[j-1];--j;}
        p.apps[j]=moving;
    }
    p.view.count=p.count;if(p.view.index>=p.count)p.view.index=0;
    p.view.top=0;
}
inline void ProgramLabel(size_t row,char16_t* out,size_t cap,bool spoken,void*) {
    auto& p=P();if(row>=p.count)return;
    const auto& app=p.apps[row];out[0]=0;
    if(spoken) {
        // Voice lock: favourites firm, drivers and foreign programs dimmed.
        if(app.flags&appFavourite)Mark(out,cap,FaceVoice(Face::key),formatting::Event::none);
        else if(app.flags&(appDriver|appForeign))Mark(out,cap,FaceVoice(Face::comment));
        Cat(out,cap,app.title);
        Mark(out,cap,formatting::Voice{});
        if(app.flags&appFavourite)Cat(out,cap,T(u", ulubiony",u", favourite"));
        if(app.flags&appDriver)Cat(out,cap,T(u", sterownik",u", driver"));
        if(app.flags&appForeign)Cat(out,cap,T(u", nie dla tego komputera",u", not for this computer"));
        if(app.flags&appPackage)Cat(out,cap,T(u", pakiet",u", package"));
        static char16_t label[64];VolumeLabel(app.volume,label,64);Cat(out,cap,u", ");Cat(out,cap,label);
    } else {
        Cat(out,cap,app.flags&appFavourite?u"★ ":u"  ");Cat(out,cap,app.title);Cat(out,cap,u"   ");Cat(out,cap,app.path);
    }
}
inline uint8_t* BuildOption(efi_handle volume,const char16_t* file,const char16_t* title,const char16_t* args,size_t& size) {
    size=0;size_t device=0;const uint8_t* own=DevicePathOf(volume,device);if(!own)return nullptr;
    const size_t titleUnits=Len(title)+1,fileUnits=Len(file)+1,node=4+fileUnits*2,argBytes=args && args[0]?(Len(args)+1)*2:0;
    const size_t pathBytes=device+node+4;
    size=6+titleUnits*2+pathBytes+argBytes;
    uint8_t* data=static_cast<uint8_t*>(Allocate(size));if(!data) {size=0;return nullptr;}
    data[0]=1;data[1]=data[2]=data[3]=0;data[4]=uint8_t(pathBytes);data[5]=uint8_t(pathBytes>>8);
    uint8_t* at=data+6;
    for(size_t i=0;i<titleUnits;++i) {*at++=uint8_t(title[i]);*at++=uint8_t(title[i]>>8);}
    memcpy(at,own,device);at+=device;
    at[0]=4;at[1]=4;at[2]=uint8_t(node);at[3]=uint8_t(node>>8);
    for(size_t i=0;i<fileUnits;++i) {at[4+i*2]=uint8_t(file[i]);at[5+i*2]=uint8_t(file[i]>>8);}
    at+=node;at[0]=0x7f;at[1]=0xff;at[2]=4;at[3]=0;at+=4;
    for(size_t i=0;i*2<argBytes;++i) {*at++=uint8_t(args[i]);*at++=uint8_t(args[i]>>8);}
    return data;
}
// Starts a program and reports how it ended; the screen is ours again after.
inline void StartProgram(efi_handle volume,const char16_t* path,const char16_t* title,const char16_t* args) {
    size_t size=0;uint8_t* option=BuildOption(volume,path,title,args,size);
    if(!option) {Fail(T(u"Nie zbudowano ścieżki programu.",u"The program path could not be built."));return;}
    static char16_t report[700];report[0]=0;
    Cursor(false);
    ReviewMode(false);ScriptEnd();U().host->launch(U().host->context,title,option,size,report,700);ReviewMode(true);
    Release(option);Reshape();
    Play(Icon::done);Say(report[0]?report:T(u"Powrót do środowiska Charona.",u"Back in the Charon workspace."));
}
}  // namespace ws
}  // namespace hakefi
#include "CharonWsInstall2.h"
#include "CharonWsSelfInstall.h"   // r407
#include "CharonWsBrowser.h"
#ifdef HAK_CHARON_HTTPS            // the installers, and with them the plan, exist only in the build with the network
#include "CharonWsPlan.h"          // r409: a waiting multi-system plan (planrun::AtStart)
#include "CharonWsPlanDialogue.h"  // r409: the dialogue that builds a plan, and the installers' steps
#endif
namespace hakefi {
namespace ws {
inline void StartApp(size_t index) {
    auto& p=P();if(index>=p.count)return;
    App app=p.apps[index];
    if(app.flags&appForeign) {Fail(T(u"Ten program nie jest dla tego komputera; firmware go nie uruchomi.",u"This program is not for this computer; firmware will not start it."));return;}
    const size_t record=FindRecord(app,true);
    static char16_t args[128];args[0]=0;
    if(record!=SIZE_MAX) {Set(args,128,p.records[record].args);p.records[record].recent=++p.clock;SaveRecords();}
    StartProgram(app.volume,app.path,app.title,args);
}
inline void ProgramDetail(size_t index) {
    auto& p=P();if(index>=p.count)return;
    const auto& app=p.apps[index];static char16_t text[900];static char16_t label[64];
    Set(text,900,app.title);Cat(text,900,T(u". Plik: ",u". File: "));Cat(text,900,app.path);
    VolumeLabel(app.volume,label,64);Cat(text,900,T(u". Wolumin: ",u". Volume: "));Cat(text,900,label);
    Cat(text,900,app.flags&appDriver?T(u". Sterownik UEFI: zostanie w pamięci i od razu odda sterowanie.",u". UEFI driver: it stays in memory and returns at once.")
        :app.flags&appForeign?T(u". Nie jest programem UEFI dla tego komputera.",u". Not a UEFI program for this computer."):T(u". Aplikacja UEFI x64.",u". x64 UEFI application."));
    if(app.record!=SIZE_MAX && p.records[app.record].args[0]) {Cat(text,900,T(u" Argumenty: ",u" Arguments: "));Cat(text,900,p.records[app.record].args);Cat(text,900,u".");}
    Say(text);
}
// ---- files (Dired) ----------------------------------------------------------
constexpr size_t maxClip=32;
struct Files {
    bool atVolumes;efi_handle volumes[32];size_t volumeCount;
    FileRef folder;Listing list;ListView view;
    FileRef clip[maxClip];bool clipFolder[maxClip];size_t clipCount;
};
inline Files& F() {static Files files;return files;}
inline void FilesLabel(size_t row,char16_t* out,size_t cap,bool spoken,void*) {
    auto& f=F();out[0]=0;
    if(f.atVolumes) {
        if(row>=f.volumeCount)return;
        static char16_t label[64];VolumeLabel(f.volumes[row],label,64);
        if(spoken)Mark(out,cap,FaceVoice(Face::heading));
        Cat(out,cap,label);
        if(spoken)Mark(out,cap,formatting::Voice{});
        if(f.volumes[row]==U().host->selfDevice && !ContainsFold(label,u"Charon"))Cat(out,cap,T(u", dysk Charona",u", Charon's disk"));
        if(VolumeReadOnly(f.volumes[row]))Cat(out,cap,T(u", tylko do odczytu",u", read only"));
        Cat(out,cap,spoken?T(u", wolumin",u", volume"):u"  ◄►");
        return;
    }
    if(row>=f.list.count)return;
    const auto& e=f.list.entries[row];
    if(spoken) {
        if(e.marked)Cat(out,cap,T(u"zaznaczony, ",u"marked, "));
        // Folders in the heading voice, programs lively, other files plain.
        if(e.directory)Mark(out,cap,FaceVoice(Face::heading));
        else if(EndsFold(e.name,u".efi"))Mark(out,cap,FaceVoice(Face::keyword));
        Cat(out,cap,e.name);
        if(e.directory || EndsFold(e.name,u".efi"))Mark(out,cap,formatting::Voice{});
        if(e.directory)Cat(out,cap,u", folder");
        else {Cat(out,cap,u", ");systools::PutSize(out,cap,e.size,Pl());}
    } else {
        Cat(out,cap,e.marked?u"* ":u"  ");Cat(out,cap,e.name);
        if(e.directory)Cat(out,cap,u" ►");
        else {Cat(out,cap,u"   ");systools::PutSize(out,cap,e.size,Pl());}
    }
}
inline void LoadFiles(const char16_t* focus=nullptr) {
    auto& f=F();
    if(f.atVolumes) {f.volumeCount=Volumes(f.volumes,32);f.view.count=f.volumeCount;}
    else {
        const auto status=ReadFolder(f.folder,f.list);
        if(status!=EFI_SUCCESS && !f.list.count) {f.atVolumes=true;f.volumeCount=Volumes(f.volumes,32);f.view.count=f.volumeCount;f.view.index=0;
            Fail(T(u"Nie odczytano folderu.",u"The folder could not be read."),status);return;}
        f.view.count=f.list.count;
    }
    if(f.view.index>=f.view.count)f.view.index=f.view.count?f.view.count-1:0;
    if(focus && !f.atVolumes)for(size_t i=0;i<f.list.count;++i)if(SameFold(f.list.entries[i].name,focus)) {f.view.index=i;break;}
    f.view.top=0;
}
inline void FilesUp() {
    auto& f=F();
    if(f.atVolumes) {Play(Icon::edge);return;}
    static char16_t left[nameUnits];
    if(Len(f.folder.path)<=1) {
        const efi_handle volume=f.folder.volume;f.atVolumes=true;f.view.index=0;LoadFiles();
        for(size_t i=0;i<f.volumeCount;++i)if(f.volumes[i]==volume)f.view.index=i;
    } else {
        Set(left,nameUnits,BaseName(f.folder.path));static char16_t parent[pathUnits];ParentOf(f.folder.path,parent,pathUnits);
        Set(f.folder.path,pathUnits,parent);f.view.index=0;LoadFiles(left);
    }
    Cue(MenuCue::submenuClose);SayRow(f.view);
}
inline bool CurrentFile(FileRef& ref,Entry** entry=nullptr) {
    auto& f=F();if(f.atVolumes || f.view.index>=f.list.count)return false;
    ref.volume=f.folder.volume;if(!JoinPath(ref.path,pathUnits,f.folder.path,f.list.entries[f.view.index].name))return false;
    if(entry)*entry=&f.list.entries[f.view.index];
    return true;
}
inline bool TextName(const char16_t* name) {
    static const char16_t* const ext[]={u".txt",u".cfg",u".conf",u".nsh",u".ini",u".lst",u".md",u".json",u".plist",u".xml",u".manifest",u".log",u".sh",u".ps1",u".inf",u".csv",u".list"};
    for(const auto* e:ext)if(EndsFold(name,e))return true;
    return false;
}
size_t OpenInEditor(const FileRef& ref,bool hex);   // below (switches the view)
inline void FilesOpen(bool forceHex=false,bool forceEdit=false) {
    auto& f=F();
    if(f.atVolumes) {
        if(f.view.index>=f.volumeCount)return;
        f.atVolumes=false;f.folder.volume=f.volumes[f.view.index];Set(f.folder.path,pathUnits,u"\\");f.view.index=0;LoadFiles();
        Cue(MenuCue::submenuOpen);SayRow(f.view);return;
    }
    FileRef ref;Entry* e=nullptr;if(!CurrentFile(ref,&e))return;
    if(e->directory) {
        Set(f.folder.path,pathUnits,ref.path);f.view.index=0;LoadFiles();Cue(MenuCue::submenuOpen);SayRow(f.view);return;}
    if(!forceHex && !forceEdit && EndsFold(e->name,u".efi")) {StartProgram(ref.volume,ref.path,e->name,nullptr);LoadFiles(e->name);return;}
    if(!forceHex && !forceEdit && (EndsFold(e->name,u".iso") || EndsFold(e->name,u".img"))) {ramdisk::BootFile(ref,e->name,e->size);Forget();LoadFiles(e->name);return;}   // r400: a disc image runs from memory
    if(e->size>maxFileBytes) {Fail(T(u"Plik jest za duży: najwyżej 4 megabajty.",u"The file is too large: 4 megabytes at most."));return;}
    OpenInEditor(ref,forceHex || (!forceEdit && !TextName(e->name) && e->size>256*1024));
}
inline void FilesDetail() {
    auto& f=F();static char16_t text[700];
    if(f.atVolumes) {
        if(f.view.index>=f.volumeCount)return;
        FilesLabel(f.view.index,text,500,true,nullptr);
        efi_file_protocol* root=OpenRoot(f.volumes[f.view.index]);
        if(root) {alignas(8) uint8_t info[512]={};size_t size=sizeof(info);
            if(root->GetInfo(root,&media::fsInfoGuid,&size,info)==EFI_SUCCESS && size>36) {
                Cat(text,700,T(u". Rozmiar ",u". Size "));systools::PutSize(text,700,systools::Le64(info+16),Pl());
                Cat(text,700,T(u", wolne ",u", free "));systools::PutSize(text,700,systools::Le64(info+24),Pl());}
            root->Close(root);}
        Say(text);return;
    }
    FileRef ref;Entry* e=nullptr;if(!CurrentFile(ref,&e)) {Say(T(u"Pusty folder",u"Empty folder"));return;}
    Set(text,700,e->name);Cat(text,700,e->directory?u". Folder. ":T(u". Plik, ",u". File, "));
    if(!e->directory) {systools::PutSize(text,700,e->size,Pl());Cat(text,700,u". ");}
    Cat(text,700,T(u"Ścieżka: ",u"Path: "));Cat(text,700,ref.path);
    if(e->modified.Year) {Cat(text,700,T(u". Zmieniono: ",u". Modified: "));systools::PutNumber(text,700,e->modified.Year,4);Cat(text,700,u"-");
        systools::PutNumber(text,700,e->modified.Month,2);Cat(text,700,u"-");systools::PutNumber(text,700,e->modified.Day,2);Cat(text,700,u" ");
        systools::PutNumber(text,700,e->modified.Hour,2);Cat(text,700,u":");systools::PutNumber(text,700,e->modified.Minute,2);}
    Say(text);
}
inline void FilesCopy() {
    auto& f=F();if(f.atVolumes) {Play(Icon::edge);return;}
    f.clipCount=0;bool any=false;
    for(size_t i=0;i<f.list.count;++i)if(f.list.entries[i].marked)any=true;
    for(size_t i=0;i<f.list.count && f.clipCount<maxClip;++i) {
        if(any?!f.list.entries[i].marked:i!=f.view.index)continue;
        auto& ref=f.clip[f.clipCount];ref.volume=f.folder.volume;
        if(!JoinPath(ref.path,pathUnits,f.folder.path,f.list.entries[i].name))continue;
        f.clipFolder[f.clipCount++]=f.list.entries[i].directory;
    }
    static char16_t text[160];Set(text,160,T(u"Skopiowano do schowka: ",u"Copied to the clipboard: "));CatN(text,160,f.clipCount);
    Cat(text,160,T(u". Przejdź do folderu docelowego i naciśnij P.",u". Go to the target folder and press P."));Play(Icon::mark);Say(text);
}
inline void FilesPaste() {
    auto& f=F();
    if(f.atVolumes || !f.clipCount) {Fail(f.clipCount?T(u"Najpierw otwórz folder docelowy.",u"Open the target folder first."):T(u"Schowek plików jest pusty.",u"The file clipboard is empty."));return;}
    size_t done=0;efi_status status=EFI_SUCCESS;static FileRef to;
    Say(T(u"Kopiuję.",u"Copying."));
    for(size_t i=0;i<f.clipCount && status==EFI_SUCCESS;++i) {
        to.volume=f.folder.volume;
        if(!JoinPath(to.path,pathUnits,f.folder.path,BaseName(f.clip[i].path))) {status=EFI_BAD_BUFFER_SIZE;break;}
        if(to.volume==f.clip[i].volume && SameFold(to.path,f.clip[i].path)) {
            if(Len(to.path)+6>=pathUnits) {status=EFI_BAD_BUFFER_SIZE;break;}
            Cat(to.path,pathUnits,u".copy");}
        if(to.volume==f.clip[i].volume && f.clipFolder[i] && StartsFold(to.path,f.clip[i].path) && to.path[Len(f.clip[i].path)]==u'\\') {status=EFI_INVALID_PARAMETER;break;}
        if(f.clipFolder[i])status=CopyTree(f.clip[i],to,0,&done);
        else {status=CopyFile(f.clip[i],to);if(status==EFI_SUCCESS)++done;}
    }
    LoadFiles();
    if(status!=EFI_SUCCESS) {Fail(status==EFI_VOLUME_FULL?T(u"Brak miejsca na dysku.",u"The disk is full."):status==EFI_WRITE_PROTECTED?T(u"Nośnik tylko do odczytu.",u"The medium is read only.")
        :T(u"Kopiowanie nie powiodło się.",u"Copying failed."),status);return;}
    static char16_t text[120];Set(text,120,T(u"Skopiowano plików: ",u"Files copied: "));CatN(text,120,done);Play(Icon::done);Say(text);
}
inline void FilesDelete() {
    FileRef ref;Entry* e=nullptr;if(!CurrentFile(ref,&e)) {Play(Icon::edge);return;}
    static char16_t question[400];Set(question,400,e->directory?T(u"Usunąć folder z całą zawartością: ",u"Delete the folder and everything in it: "):T(u"Usunąć plik: ",u"Delete the file: "));
    Cat(question,400,e->name);Cat(question,400,u"?");
    if(!Confirm(question))return;
    const auto status=DeleteTree(ref,0);LoadFiles();
    if(status!=EFI_SUCCESS) {Fail(T(u"Nie usunięto.",u"Not deleted."),status);return;}
    Play(Icon::deleted);Say(T(u"Usunięto.",u"Deleted."));
}
inline void FilesRename() {
    FileRef ref;Entry* e=nullptr;if(!CurrentFile(ref,&e)) {Play(Icon::edge);return;}
    static char16_t name[nameUnits];Set(name,nameUnits,e->name);
    if(!Prompt(T(u"Nowa nazwa:",u"New name:"),name,nameUnits-1) || !name[0] || SameFold(name,e->name))return;
    for(size_t i=0;name[i];++i)if(name[i]==u'\\' || name[i]==u'/') {Fail(T(u"Nazwa nie może zawierać ukośnika.",u"A name cannot contain a slash."));return;}
    efi_file_protocol* file=nullptr;auto status=OpenFile(ref,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&file);
    if(status==EFI_SUCCESS) {
        static efi_guid infoGuid=EFI_FILE_INFO_GUID;alignas(8) static uint8_t buffer[1400];size_t size=sizeof(buffer)-nameUnits*2;
        status=file->GetInfo(file,&infoGuid,&size,buffer);
        if(status==EFI_SUCCESS) {
            auto* info=reinterpret_cast<efi_file_info*>(buffer);const size_t units=Len(name)+1;
            for(size_t i=0;i<units;++i)info->FileName[i]=name[i];
            info->Size=offsetof(efi_file_info,FileName)+units*2;
            status=file->SetInfo(file,&infoGuid,size_t(info->Size),buffer);
        }
        file->Close(file);
    }
    LoadFiles(name);
    if(status!=EFI_SUCCESS) {Fail(T(u"Nie zmieniono nazwy.",u"Not renamed."),status);return;}
    Play(Icon::done);Say(T(u"Zmieniono nazwę.",u"Renamed."));
}
inline void FilesNew(bool folder) {
    auto& f=F();if(f.atVolumes) {Play(Icon::edge);return;}
    static char16_t name[nameUnits];name[0]=0;
    if(!Prompt(folder?T(u"Nazwa nowego folderu:",u"New folder name:"):T(u"Nazwa nowego pliku:",u"New file name:"),name,nameUnits-1) || !name[0])return;
    FileRef ref;ref.volume=f.folder.volume;if(!JoinPath(ref.path,pathUnits,f.folder.path,name))return;
    if(folder) {
        const auto status=MakeFolders(ref.volume,ref.path);LoadFiles(name);
        if(status!=EFI_SUCCESS) {Fail(T(u"Nie utworzono folderu.",u"The folder was not created."),status);return;}
        Play(Icon::done);Say(T(u"Utworzono folder.",u"Folder created."));
    } else OpenInEditor(ref,false);
}
// ---- packages -----------------------------------------------------------------
// A package is a folder \EFI\HAK\packages\<name> or \charon-packages\<name> on
// ANY volume (so big ones need not live on the small EFI partition), with an
// optional package.txt (name=, name.pl=, description=, description.pl=,
// program=, arguments=). Synthesizer add-ons (\EFI\HAK\synths\*.manifest on any
// volume) are listed too. Packages run in place, and can be copied to another
// volume or removed.
constexpr size_t maxPackages=64;
struct Package {
    efi_handle volume;char16_t folder[appPath];char16_t name[72];char16_t description[200];char16_t program[96];char16_t args[96];
    bool synth;char16_t module[96];char16_t data[96];   // synth: manifest path in `folder`
};
struct Packages {Package items[maxPackages];size_t count;ListView view;};
inline Packages& K() {static Packages packages;return packages;}
inline void ParsePackageText(Package& pk,const uint8_t* data,size_t size) {
    static char16_t text[4096];const size_t n=Utf8ToUtf16(data,size>6000?6000:size,text,4096);
    for(size_t at=0;at<n;) {
        size_t end=at;while(end<n && text[end]!=u'\n')++end;
        size_t stop=end;while(stop>at && (text[stop-1]==u'\r' || text[stop-1]==u' '))--stop;
        const char16_t saved=text[stop];text[stop]=0;const char16_t* line=text+at;
        auto take=[&](const char16_t* key,char16_t* out,size_t cap,bool localized) {
            const size_t k=Len(key);
            if(!StartsFold(line,key))return;
            if(line[k]==u'=') {if(!out[0] || !localized)Set(out,cap,line+k+1);}
            else if(localized && line[k]==u'.' && Fold(line[k+1])==(Pl()?u'p':u'e') && line[k+3]==u'=')Set(out,cap,line+k+4);
        };
        if(line[0]!=u'#') {
            take(u"name",pk.name,72,true);take(u"description",pk.description,200,true);
            take(u"program",pk.program,96,false);take(u"arguments",pk.args,96,false);
        }
        text[stop]=saved;at=end+1;
    }
}
inline void AddPackagesIn(efi_handle volume,const char16_t* base) {
    auto& k=K();static Listing list;FileRef folder;folder.volume=volume;Set(folder.path,pathUnits,base);
    if(ReadFolder(folder,list)!=EFI_SUCCESS)return;
    for(size_t i=0;i<list.count && k.count<maxPackages;++i) {
        if(!list.entries[i].directory)continue;
        auto& pk=k.items[k.count];memset(&pk,0,sizeof(pk));pk.volume=volume;
        if(!JoinPath(pk.folder,appPath,base,list.entries[i].name))continue;
        FileRef manifest;manifest.volume=volume;uint8_t* data=nullptr;size_t size=0;
        if(JoinPath(manifest.path,pathUnits,pk.folder,u"package.txt") && ReadWhole(manifest,64*1024,&data,&size)==EFI_SUCCESS) {ParsePackageText(pk,data,size);Release(data);}
        if(!pk.name[0])Set(pk.name,72,list.entries[i].name);
        if(!pk.program[0]) {
            // No manifest: the first program in the folder.
            static Listing inner;FileRef in;in.volume=volume;Set(in.path,pathUnits,pk.folder);
            if(ReadFolder(in,inner)==EFI_SUCCESS)for(size_t j=0;j<inner.count;++j)if(!inner.entries[j].directory && EndsFold(inner.entries[j].name,u".efi")) {
                Set(pk.program,96,inner.entries[j].name);break;}
        }
        ++k.count;Pump();
    }
}
inline void AddSynthsOn(efi_handle volume) {
    auto& k=K();efi_file_protocol* root=OpenRoot(volume);if(!root)return;
    static synths::Info found[synths::maxSynths];
    const size_t n=synths::Scan(U().host->boot,root,found,synths::maxSynths);root->Close(root);
    for(size_t i=0;i<n && k.count<maxPackages;++i) {
        auto& pk=k.items[k.count++];memset(&pk,0,sizeof(pk));pk.volume=volume;pk.synth=true;
        Set(pk.folder,appPath,u"\\EFI\\HAK\\synths\\");Cat(pk.folder,appPath,found[i].manifest);
        size_t at=0;for(;found[i].name[at] && at<70;++at)pk.name[at]=char16_t(uint8_t(found[i].name[at]));
        pk.name[at]=0;if(!at)Set(pk.name,72,found[i].manifest);
        Set(pk.module,96,found[i].module);Set(pk.data,96,found[i].data);
        Set(pk.description,200,T(u"Syntezator mowy, głosów: ",u"Speech synthesizer, voices: "));CatN(pk.description,200,found[i].voices);
    }
}
inline void ScanPackages() {
    auto& k=K();k.count=0;efi_handle volumes[32];const size_t n=Volumes(volumes,32);
    for(size_t v=0;v<n;++v) {AddPackagesIn(volumes[v],u"\\EFI\\HAK\\packages");AddPackagesIn(volumes[v],u"\\charon-packages");AddSynthsOn(volumes[v]);}
    k.view.count=k.count;if(k.view.index>=k.count)k.view.index=0;
    k.view.top=0;
}
inline void PackageLabel(size_t row,char16_t* out,size_t cap,bool spoken,void*) {
    auto& k=K();if(row>=k.count)return;
    const auto& pk=k.items[row];static char16_t label[64];VolumeLabel(pk.volume,label,64);out[0]=0;
    if(spoken && pk.synth)Mark(out,cap,FaceVoice(Face::string));
    Cat(out,cap,pk.name);
    if(spoken && pk.synth)Mark(out,cap,formatting::Voice{});
    if(spoken) {
        Cat(out,cap,pk.synth?T(u", syntezator",u", synthesizer"):pk.program[0]?T(u", program",u", program"):T(u", dane",u", data"));
        Cat(out,cap,u", ");Cat(out,cap,label);
        if(pk.volume==U().host->selfDevice)Cat(out,cap,T(u", na dysku Charona",u", on Charon's disk"));
    } else {Cat(out,cap,pk.synth?u"  ♪  ":u"     ");Cat(out,cap,label);Cat(out,cap,u"  ");Cat(out,cap,pk.folder);}
}
inline void PackageDetail(size_t index) {
    auto& k=K();if(index>=k.count)return;
    const auto& pk=k.items[index];static char16_t text[700];
    Set(text,700,pk.name);Cat(text,700,u". ");
    if(pk.description[0]) {Cat(text,700,pk.description);Cat(text,700,u". ");}
    if(pk.program[0]) {Cat(text,700,T(u"Program: ",u"Program: "));Cat(text,700,pk.program);Cat(text,700,u". ");}
    Cat(text,700,T(u"Miejsce: ",u"Location: "));Cat(text,700,pk.folder);
    Say(text);
}
inline void PackageRun(size_t index) {
    auto& k=K();if(index>=k.count)return;
    const Package pk=k.items[index];
    if(pk.synth) {Say(T(u"To syntezator: wybierz go w ustawieniach głosu Charona. Działa z każdego dysku.",u"This is a synthesizer: choose it in Charon's voice settings. It works from any disk."));return;}
    if(!pk.program[0]) {Fail(T(u"Ten pakiet nie ma programu do uruchomienia.",u"This package has no program to start."));return;}
    static char16_t path[pathUnits];if(!JoinPath(path,pathUnits,pk.folder,pk.program))return;
    StartProgram(pk.volume,path,pk.name,pk.args);
}
// Pick a writable volume (other than `except`) from the echo line.
inline bool ChooseVolume(efi_handle except,efi_handle& chosen) {
    efi_handle volumes[32];size_t n=Volumes(volumes,32),count=0;static efi_handle list[32];
    for(size_t i=0;i<n;++i)if(volumes[i]!=except && !VolumeReadOnly(volumes[i]))list[count++]=volumes[i];
    if(!count) {Fail(T(u"Nie ma innego woluminu, na którym można zapisywać.",u"There is no other volume that can be written."));return false;}
    size_t index=0;static char16_t label[64];static char16_t line[300];
    auto say=[&]() {VolumeLabel(list[index],label,64);Set(line,300,T(u"Na który wolumin? ",u"To which volume? "));Cat(line,300,label);
        if(list[index]==U().host->selfDevice)Cat(line,300,T(u", dysk Charona",u", Charon's disk"));
        Cat(line,300,u", ");CatN(line,300,index+1);Cat(line,300,T(u" z ",u" of "));CatN(line,300,count);Echo(line);Say(line);};
    say();
    for(;;) {
        const Key key=WaitKey();
        if(IsCancel(key)) {Echo(u"");Say(T(u"Anulowano",u"Cancelled"));return false;}
        if(!key.scan && key.ch==13) {Echo(u"");chosen=list[index];return true;}
        if(key.scan==scDown || key.scan==scRight) {index=(index+1)%count;say();}
        else if(key.scan==scUp || key.scan==scLeft) {index=(index+count-1)%count;say();}
    }
}
inline void PackageCopy(size_t index) {
    auto& k=K();if(index>=k.count)return;
    const Package pk=k.items[index];efi_handle target=nullptr;
    if(!ChooseVolume(pk.volume,target))return;
    Say(T(u"Kopiuję pakiet.",u"Copying the package."));
    size_t files=0;efi_status status=EFI_SUCCESS;static FileRef from,to;from.volume=pk.volume;to.volume=target;
    if(pk.synth) {
        MakeFolders(target,u"\\EFI\\HAK\\synths");
        const char16_t* parts[3]={pk.folder,pk.module,pk.data};
        for(int i=0;i<3 && status==EFI_SUCCESS;++i) {
            if(!parts[i][0])continue;
            Set(from.path,pathUnits,parts[i]);Set(to.path,pathUnits,parts[i]);
            if(i==2)status=CopyTree(from,to,0,&files);
            else {status=CopyFile(from,to);if(status==EFI_SUCCESS)++files;}
        }
    } else {
        Set(from.path,pathUnits,pk.folder);Set(to.path,pathUnits,pk.folder);
        static char16_t parent[pathUnits];ParentOf(to.path,parent,pathUnits);MakeFolders(target,parent);
        status=CopyTree(from,to,0,&files);
    }
    ScanPackages();
    if(status!=EFI_SUCCESS) {Fail(status==EFI_VOLUME_FULL?T(u"Brak miejsca na docelowym woluminie.",u"The target volume is full."):T(u"Nie skopiowano pakietu.",u"The package was not copied."),status);return;}
    if(pk.synth && U().host->synthsChanged)U().host->synthsChanged(U().host->context);
    static char16_t text[160];Set(text,160,T(u"Pakiet skopiowany, plików: ",u"Package copied, files: "));CatN(text,160,files);Play(Icon::done);Say(text);
}
inline void PackageRemove(size_t index) {
    auto& k=K();if(index>=k.count)return;
    const Package pk=k.items[index];
    static char16_t question[300];Set(question,300,T(u"Usunąć pakiet z tego dysku: ",u"Remove the package from this disk: "));Cat(question,300,pk.name);Cat(question,300,u"?");
    if(!Confirm(question))return;
    efi_status status=EFI_SUCCESS;static FileRef ref;ref.volume=pk.volume;
    if(pk.synth) {
        const char16_t* parts[3]={pk.data,pk.module,pk.folder};
        for(int i=0;i<3;++i) {if(!parts[i][0])continue;Set(ref.path,pathUnits,parts[i]);const auto one=DeleteTree(ref,0);if(one!=EFI_SUCCESS && one!=EFI_NOT_FOUND)status=one;}
    } else {Set(ref.path,pathUnits,pk.folder);status=DeleteTree(ref,0);}
    ScanPackages();
    if(status!=EFI_SUCCESS) {Fail(T(u"Nie usunięto pakietu.",u"The package was not removed."),status);return;}
    if(pk.synth && U().host->synthsChanged)U().host->synthsChanged(U().host->context);
    Play(Icon::deleted);Say(T(u"Pakiet usunięty.",u"Package removed."));
}
// ---- the workspace ------------------------------------------------------------
enum class View : uint8_t {home,programs,files,packages,buffers,editor,calc,help,network,wifi};
enum class Command : uint8_t {programs,files,packages,network,download,install,selfInstall,browser,newFile,findFile,notes,calculator,buffers,save,saveAs,search,replace,gotoLine,killBuffer,
    readBuffer,undo,voiceLock,clock,rescan,help,home,menu,devices,order,settings,firmware,shell,tools,continueBoot,restart,shutdown,count};
struct CommandName {Command id;const char16_t* pl;const char16_t* en;};
inline const CommandName commandNames[]={
    {Command::programs,u"programy uefi",u"uefi programs"},{Command::files,u"pliki",u"files"},{Command::packages,u"pakiety i dodatki",u"packages and add-ons"},
    {Command::network,u"sieć ethernet i wi-fi",u"network ethernet and wi-fi"},{Command::download,u"pobierz plik z sieci",u"download a file"},{Command::install,u"instalacja systemu z sieci",u"install a system from the network"},{Command::selfInstall,u"zainstaluj charona na tym komputerze",u"install charon on this computer"},{Command::browser,u"przeglądarka www",u"web browser"},
    {Command::newFile,u"nowy plik tekstowy",u"new text file"},{Command::findFile,u"otwórz plik",u"find file"},{Command::notes,u"notatki",u"notes"},
    {Command::calculator,u"kalkulator",u"calculator"},{Command::buffers,u"bufory",u"buffers"},{Command::save,u"zapisz bufor",u"save buffer"},
    {Command::saveAs,u"zapisz jako",u"write file"},{Command::search,u"szukaj",u"search"},{Command::replace,u"zamień wszystko",u"replace all"},
    {Command::gotoLine,u"idź do wiersza",u"go to line"},{Command::killBuffer,u"zamknij bufor",u"kill buffer"},{Command::readBuffer,u"czytaj bufor",u"read buffer"},
    {Command::undo,u"cofnij",u"undo"},{Command::voiceLock,u"głosy formatowania",u"voice lock"},{Command::clock,u"zegar",u"clock"},
    {Command::rescan,u"odśwież listę",u"refresh list"},{Command::help,u"pomoc klawisze",u"help keys"},{Command::home,u"strona startowa",u"start page"},
    {Command::menu,u"wróć do menu charona",u"back to charon menu"},{Command::devices,u"urządzenia startowe",u"boot devices"},{Command::order,u"kolejność startu",u"boot order"},
    {Command::settings,u"ustawienia charona",u"charon settings"},{Command::firmware,u"ustawienia uefi",u"uefi settings"},{Command::shell,u"powłoka uefi",u"uefi shell"},
    {Command::tools,u"narzędzia systemowe",u"system tools"},{Command::continueBoot,u"kontynuuj rozruch",u"continue boot"},{Command::restart,u"uruchom ponownie komputer",u"restart computer"},
    {Command::shutdown,u"wyłącz komputer",u"shut down computer"}};
constexpr size_t commandCount=sizeof(commandNames)/sizeof(commandNames[0]);
struct Space {
    View view;View stack[10];size_t depth;
    ListView home,buffers,help,calc;
    char16_t history[24][160];size_t historyCount;
    size_t bufferRows[maxBuffers];
    size_t matches[commandCount];size_t matchCount,matchIndex;
};
inline Space& S() {static Space space;return space;}
inline const char16_t* const homeRows[][2]={{u"Programy UEFI",u"UEFI programs"},{u"Pliki",u"Files"},{u"Pakiety i dodatki",u"Packages and add-ons"},{u"Sieć: Ethernet i Wi-Fi",u"Network: Ethernet and Wi-Fi"},{u"Instalacja systemu z sieci",u"Install a system from the network"},{u"Zainstaluj Charona na tym komputerze",u"Install Charon on this computer"},{u"Przeglądarka WWW, tekstowa",u"Web browser, text"},
    {u"Nowy plik tekstowy",u"New text file"},{u"Notatki",u"Notes"},{u"Kalkulator",u"Calculator"},{u"Bufory",u"Buffers"},
    {u"Polecenia (Alt+X lub F10)",u"Commands (Alt+X or F10)"},{u"Zegar",u"Clock"},{u"Pomoc: klawisze",u"Help: keys"},{u"Wróć do menu Charona",u"Back to Charon's menu"}};
constexpr size_t homeCount=sizeof(homeRows)/sizeof(homeRows[0]);
inline void HomeLabel(size_t row,char16_t* out,size_t cap,bool,void*) {if(row<homeCount)Set(out,cap,homeRows[row][Pl()?0:1]);}
inline const char16_t* const helpRows[][2]={
    {u"Wszędzie: Alt+X lub F10 polecenia po nazwie; F1 pomoc; Escape wstecz; Control+X B bufory; Control+X Control+C wyjście.",
     u"Everywhere: Alt+X or F10 commands by name; F1 help; Escape back; Control+X B buffers; Control+X Control+C exit."},
    {u"Listy: strzałki, Home, End, Page Up, Page Down albo Control+N, Control+P; Enter otwiera; Spacja szczegóły; litera skacze do nazwy.",
     u"Lists: arrows, Home, End, Page Up, Page Down or Control+N, Control+P; Enter opens; Space details; a letter jumps to a name."},
    {u"Programy: Enter uruchamia; F2 argumenty; F3 lub plus ulubiony; F5 szukaj programów od nowa.",
     u"Programs: Enter starts; F2 arguments; F3 or plus favourite; F5 scan again."},
    {u"Pliki: Enter otwiera folder, program albo plik; Backspace w górę; M zaznacz; C kopiuj; P wklej; R zmień nazwę; D lub Delete usuń; plus nowy folder; N nowy plik; E edytuj; H szesnastkowo; G odśwież.",
     u"Files: Enter opens a folder, program or file; Backspace up; M mark; C copy; P paste; R rename; D or Delete delete; plus new folder; N new file; E edit; H hex; G refresh."},
    {u"Pakiety: Enter uruchamia; C kopiuj na inny wolumin; D usuń; Spacja opis; G odśwież. Pakiety działają z każdego dysku.",
     u"Packages: Enter starts; C copy to another volume; D remove; Space description; G refresh. Packages work from any disk."},
    {u"Sieć: Enter łączy kartę przez DHCP albo pokazuje sieci Wi-Fi; Spacja opis; D pobiera plik z adresu http do folderu otwartego w Plikach; L wczytuje dodatkowe sterowniki z folderu EFI, HAK, drivers na dowolnym dysku.",
     u"Network: Enter connects an adapter with DHCP or lists Wi-Fi networks; Space details; D downloads a file from an http address into the folder open in Files; L loads extra drivers from the folder EFI, HAK, drivers on any disk."},
    {u"Edytor, ruch: strzałki lub Control+F B N P; Control+strzałki lub Alt+F Alt+B słowa; Home End; Control+Home Control+End; Page Up Page Down; F4 idź do wiersza.",
     u"Editor, moving: arrows or Control+F B N P; Control+arrows or Alt+F Alt+B words; Home End; Control+Home Control+End; Page Up Page Down; F4 go to line."},
    {u"Edytor, zmiany: Control+K wytnij wiersz; F8 znacznik; Control+W wytnij; Alt+W kopiuj; Control+Y wklej; F9 lub Control+Z cofnij; Alt+procent zamień wszystko.",
     u"Editor, changes: Control+K kill line; F8 mark; Control+W kill; Alt+W copy; Control+Y yank; F9 or Control+Z undo; Alt+percent replace all."},
    {u"Edytor, pliki: F2 lub Control+X Control+S zapisz; Control+X Control+W zapisz jako; Control+X Control+F otwórz; Control+X K zamknij; Control+S szukaj; F3 szukaj dalej.",
     u"Editor, files: F2 or Control+X Control+S save; Control+X Control+W write as; Control+X Control+F find file; Control+X K kill; Control+S search; F3 search again."},
    {u"Edytor, mowa: Control+E potem L wiersz, W słowo, C znak, B bufor, N reszta, R zaznaczenie, M stan, I wcięcie, V głosy formatowania, S cisza, E koniec wiersza; F6 stan.",
     u"Editor, speech: Control+E then L line, W word, C character, B buffer, N rest, R region, M mode line, I indent, V voice lock, S silence, E end of line; F6 mode line."},
    {u"Głosy: komentarze cicho i lekko, nagłówki nisko i wolno, klucze mocno, napisy gładko, liczby płasko, słowa kluczowe żywo, WIELKIE litery wyżej. Ton przed wierszem to wcięcie.",
     u"Voices: comments soft and light, headings low and slow, keys firm, strings smooth, numbers flat, keywords lively, CAPITALS higher. The tone before a line is its indentation."},
    {u"Kalkulator: wpisz wyrażenie, na przykład 2+2*3, 0x1F, 4g/512, 1 w lewo 20 jako dwa znaki mniejszości; Enter liczy.",
     u"Calculator: type an expression, for example 2+2*3, 0x1F, 4g/512, 1 shifted left 20 as two less-than signs; Enter computes."}};
constexpr size_t helpCount=sizeof(helpRows)/sizeof(helpRows[0]);
inline void HelpLabel(size_t row,char16_t* out,size_t cap,bool,void*) {if(row<helpCount)Set(out,cap,helpRows[row][Pl()?0:1]);}
inline void CalcLabel(size_t row,char16_t* out,size_t cap,bool,void*) {if(row<S().historyCount)Set(out,cap,S().history[row]);}
inline void BufferLabel(size_t row,char16_t* out,size_t cap,bool spoken,void*) {
    auto& s=S();if(row>=s.buffers.count)return;
    const auto& b=E().buffers[s.bufferRows[row]];Set(out,cap,b.name);
    if(spoken) {
        if(b.modified)Cat(out,cap,T(u", zmieniony",u", modified"));
        Cat(out,cap,b.kind==Kind::hex?T(u", szesnastkowy",u", hex"):T(u", tekst",u", text"));
    } else {Cat(out,cap,b.modified?u"  **  ":u"      ");if(b.hasFile)Cat(out,cap,b.file.path);}
}
inline void LoadBufferRows() {
    auto& s=S();s.buffers.count=0;
    for(size_t i=0;i<maxBuffers;++i)if(E().buffers[i].kind!=Kind::none)s.bufferRows[s.buffers.count++]=i;
    if(s.buffers.index>=s.buffers.count)s.buffers.index=0;
}
inline ListView* CurrentList() {
    auto& s=S();
    switch(s.view) {case View::home:return &s.home;case View::programs:return &P().view;case View::files:return &F().view;case View::packages:return &K().view;
        case View::buffers:return &s.buffers;case View::help:return &s.help;case View::calc:return &s.calc;
        case View::network:return &net::N().view;case View::wifi:return &net::N().wifiView;default:return nullptr;}
}
inline const char16_t* ViewName(View view) {
    switch(view) {
        case View::home:return T(u"Środowisko Charona",u"Charon workspace");case View::programs:return T(u"Programy UEFI",u"UEFI programs");
        case View::files:return T(u"Pliki",u"Files");case View::packages:return T(u"Pakiety i dodatki",u"Packages and add-ons");
        case View::buffers:return T(u"Bufory",u"Buffers");case View::calc:return T(u"Kalkulator",u"Calculator");
        case View::help:return T(u"Pomoc: klawisze",u"Help: keys");case View::network:return T(u"Sieć",u"Network");
        case View::wifi:return T(u"Sieci Wi-Fi",u"Wi-Fi networks");default:return T(u"Edytor",u"Editor");
    }
}
inline void Announce() {
    auto& s=S();static char16_t text[900];
    if(s.view==View::editor) {
        Buffer* b=Current();if(!b)return;
        Set(text,900,b->name);Cat(text,900,b->kind==Kind::hex?T(u", podgląd szesnastkowy. ",u", hex view. "):T(u", edytor. ",u", editor. "));
        Say(text);return;
    }
    ListView* v=CurrentList();Set(text,900,ViewName(s.view));
    if(s.view==View::files && !F().atVolumes) {Cat(text,900,u", ");Cat(text,900,Len(F().folder.path)>1?F().folder.path:T(u"folder główny",u"top folder"));}
    const size_t items=v?v->count:0;
    Cat(text,900,u", ");CatN(text,900,items);
    Cat(text,900,!Pl()?(items==1?u" item. ":u" items. "):items==1?u" pozycja. ":(items%10>=2 && items%10<=4 && !(items%100>=12 && items%100<=14))?u" pozycje. ":u" pozycji. ");
    if(v && v->count) {static char16_t row[640];row[0]=0;v->label(v->index,row,640,true,v->context);Cat(text,900,row);}
    Say(text);
}
inline void Show(View view,bool push=true) {
    auto& s=S();
    if(push && s.view!=view && s.depth<10)s.stack[s.depth++]=s.view;
    s.view=view;Forget();Cursor(false);Cue(MenuCue::submenuOpen);
    if(view==View::programs && !P().scanned) {Say(T(u"Szukam programów UEFI na wszystkich dyskach. Escape przerywa.",u"Looking for UEFI programs on every disk. Escape stops."));drivers::LoadStorage(false);ScanPrograms();}
    if(view==View::packages)ScanPackages();
    if(view==View::network) {
        net::LoadAdapters();
        // No adapter, or adapters without IPv4: try Charon's own drivers once.
        static bool tried=false;
        if(!tried && (!net::N().count || !net::HasProtocol(net::ip4Config2Guid))) {tried=true;if(net::LoadDrivers(false))net::LoadAdapters();}
        if(drivers::LoadUsbNetwork(false))net::LoadAdapters();   // r408: a USB network adapter the firmware does not drive
    }
    if(view==View::files) {drivers::LoadStorage(false);LoadFiles();}
    if(view==View::buffers)LoadBufferRows();
    Announce();
}
inline void Back() {
    auto& s=S();
    if(!s.depth) {if(s.view!=View::home) {s.view=View::home;Forget();Announce();}return;}
    s.view=s.stack[--s.depth];
    if(s.view==View::editor && !Current())s.view=View::home;
    Forget();Cursor(false);Cue(MenuCue::submenuClose);
    if(s.view==View::buffers)LoadBufferRows();
    if(s.view==View::files) {   // a saved or deleted file shows at once
        static char16_t focus[nameUnits];focus[0]=0;auto& f=F();
        if(!f.atVolumes && f.view.index<f.list.count)Set(focus,nameUnits,f.list.entries[f.view.index].name);
        LoadFiles(focus[0]?focus:nullptr);}
    Announce();
}
inline size_t OpenInEditor(const FileRef& ref,bool hex) {
    efi_status error=EFI_SUCCESS;const size_t index=OpenBuffer(ref,hex,&error);
    if(index==SIZE_MAX) {
        Fail(error==EFI_BAD_BUFFER_SIZE?T(u"Plik jest za duży: najwyżej 4 megabajty.",u"The file is too large: 4 megabytes at most.")
            :error==EFI_OUT_OF_RESOURCES?T(u"Za dużo otwartych buforów albo brak pamięci.",u"Too many open buffers or out of memory."):T(u"Nie otwarto pliku.",u"The file was not opened."),error);
        return SIZE_MAX;
    }
    E().current=index;Show(View::editor);
    Buffer* b=Current();
    if(b && b->kind==Kind::text && b->length)SayLineAt(*b,b->point);
    return index;
}
inline bool Unsaved() {for(const auto& b:E().buffers)if(b.kind==Kind::text && b.modified)return true;return false;}
inline void Leaving(Leave leave) {
    if(Unsaved() && !Confirm(T(u"Są niezapisane bufory. Wyjść bez zapisywania?",u"There are unsaved buffers. Leave without saving?")))return;
    U().leave=leave;U().quit=true;
}
inline void SayClock() {
    auto* runtime=U().host->system->RuntimeServices;efi_time t{};static char16_t text[160];
    if(!runtime || !runtime->GetTime || runtime->GetTime(&t,nullptr)!=EFI_SUCCESS) {Fail(T(u"Zegar firmware jest niedostępny.",u"The firmware clock is unavailable."));return;}
    text[0]=0;systools::PutNumber(text,160,t.Hour,2);Cat(text,160,u":");systools::PutNumber(text,160,t.Minute,2);Cat(text,160,u", ");
    systools::PutNumber(text,160,t.Year,4);Cat(text,160,u"-");systools::PutNumber(text,160,t.Month,2);Cat(text,160,u"-");systools::PutNumber(text,160,t.Day,2);
    Say(text);
}
inline void KillCurrent() {
    Buffer* b=Current();if(!b || S().view!=View::editor) {Play(Icon::edge);return;}
    if(b->modified && !Confirm(T(u"Bufor ma niezapisane zmiany. Zamknąć?",u"The buffer has unsaved changes. Kill it?")))return;
    KillBuffer(E().current);Play(Icon::close);Say(T(u"Bufor zamknięty.",u"Buffer killed."));Back();
}
inline void Execute(Command command) {
    Buffer* b=S().view==View::editor?Current():nullptr;
    auto needText=[&]() {if(b && b->kind==Kind::text)return true;Fail(T(u"To polecenie działa w edytorze tekstu.",u"This command works in the text editor."));return false;};
    switch(command) {
        case Command::programs:Show(View::programs);break;
        case Command::files:Show(View::files);break;
        case Command::packages:Show(View::packages);break;
        case Command::network:Show(View::network);break;
        case Command::install:install::Run();Forget();Announce();break;
        case Command::selfInstall:selfinstall::Run();Forget();Announce();break;
        case Command::browser:
#ifdef HAK_CHARON_HTTPS
            browser::Run();Reshape();Forget();Announce();
#else
            Fail(T(u"Ta wersja Charona nie ma klienta HTTPS.",u"This build of Charon has no HTTPS client."));
#endif
            break;
        case Command::download: {
            auto& n=net::N();if(!n.count)net::LoadAdapters();
            if(!n.url[0])Set(n.url,300,u"http://");
            if(!Prompt(T(u"Adres pliku:",u"File address:"),n.url,299) || Len(n.url)<10)break;
            // Into the folder open in Files, else Charon's disk.
            static FileRef target;const char16_t* base=BaseName(n.url);
            for(const char16_t* p=n.url;*p;++p)if(*p==u'/')base=p+1;
            static char16_t name[nameUnits];size_t k=0;for(;base[k] && base[k]!=u'?' && k+1<nameUnits;++k)name[k]=base[k];
            name[k]=0;if(!k)Set(name,nameUnits,u"download.bin");
            if(!F().atVolumes && F().folder.volume && !VolumeReadOnly(F().folder.volume)) {target.volume=F().folder.volume;if(!JoinPath(target.path,pathUnits,F().folder.path,name))break;}
            else {target.volume=U().host->selfDevice;MakeFolders(target.volume,u"\\EFI\\HAK\\downloads");Set(target.path,pathUnits,u"\\EFI\\HAK\\downloads\\");Cat(target.path,pathUnits,name);}
            Say(T(u"Pobieram. Escape przerywa.",u"Downloading. Escape stops."));
            uint64_t received=0;
            if(EndsFold(n.url,u".iso") && ramdisk::Find() && Confirm(T(u"To obraz płyty. Pobrać do pamięci i uruchomić zamiast zapisywać?",u"This is a disc image. Download into memory and start it instead of saving?"))) {
                ramdisk::BootUrl(n.url,u"ISO");Forget();
            } else if(net::DownloadAny(n.url,target,&received)) {
                static char16_t text[500];Set(text,500,T(u"Pobrano ",u"Downloaded "));systools::PutSize(text,500,received,Pl());
                Cat(text,500,T(u" do pliku ",u" into "));Cat(text,500,target.path);Play(Icon::done);Say(text);
                if(S().view==View::files)LoadFiles(name);
            }
            break;}
        case Command::calculator:Show(View::calc);break;
        case Command::buffers:Show(View::buffers);break;
        case Command::help:Show(View::help);break;
        case Command::home:S().depth=0;Show(View::home,false);break;
        case Command::newFile: {
            static unsigned serial=0;static char16_t name[40];Set(name,40,T(u"nowy",u"new"));CatN(name,40,++serial);Cat(name,40,u".txt");
            const size_t index=NewBuffer(Kind::text,name);
            if(index==SIZE_MAX) {Fail(T(u"Za dużo otwartych buforów.",u"Too many open buffers."));break;}
            E().current=index;Show(View::editor);break;}
        case Command::findFile: {FileRef ref;if(AskFile(Current(),T(u"Otwórz plik:",u"Find file:"),ref))OpenInEditor(ref,false);break;}
        case Command::notes: {
            MakeFolders(U().host->selfDevice,u"\\EFI\\HAK");
            FileRef ref{U().host->selfDevice,u"\\EFI\\HAK\\notes.txt"};OpenInEditor(ref,false);break;}
        case Command::save:if(needText())Save(*b,false);break;
        case Command::saveAs:if(needText())Save(*b,true);break;
        case Command::search:if(needText())Search(*b,true);break;
        case Command::replace:if(needText())ReplaceAll(*b);break;
        case Command::gotoLine:if(b)GotoLine(*b);else needText();break;
        case Command::killBuffer:KillCurrent();break;
        case Command::readBuffer:if(needText())SayRange(*b,0,b->length);break;
        case Command::undo:if(needText())Undo(*b);break;
        case Command::voiceLock:if(needText()) {b->voices=!b->voices;Say(b->voices?T(u"Głosy formatowania włączone",u"Voice lock on"):T(u"Głosy formatowania wyłączone",u"Voice lock off"));}break;
        case Command::clock:SayClock();break;
        case Command::rescan:
            if(S().view==View::programs) {Say(T(u"Szukam programów.",u"Scanning."));ScanPrograms();Announce();}
            else if(S().view==View::packages) {ScanPackages();Announce();}
            else if(S().view==View::files) {LoadFiles();Announce();}
            else Play(Icon::edge);
            break;
        case Command::menu:Leaving(Leave::menu);break;
        case Command::devices:Leaving(Leave::devices);break;
        case Command::order:Leaving(Leave::order);break;
        case Command::settings:Leaving(Leave::settings);break;
        case Command::firmware:Leaving(Leave::firmware);break;
        case Command::shell:Leaving(Leave::shell);break;
        case Command::tools:Leaving(Leave::tools);break;
        case Command::continueBoot:Leaving(Leave::continueBoot);break;
        case Command::restart:Leaving(Leave::restart);break;
        case Command::shutdown:Leaving(Leave::shutdown);break;
        default:break;
    }
}
// M-x: every change of the typed text speaks the best match and the count.
inline void PaletteHook(const char16_t* text,int move,void*) {
    auto& s=S();
    if(!move) {
        s.matchCount=0;s.matchIndex=0;
        // Names that start with the text first, then names that contain it.
        for(int pass=0;pass<2;++pass)for(size_t i=0;i<commandCount;++i) {
            const char16_t* name=Pl()?commandNames[i].pl:commandNames[i].en;
            const bool starts=StartsFold(name,text);
            if(pass==0?starts:(!starts && (ContainsFold(name,text) || ContainsFold(Pl()?commandNames[i].en:commandNames[i].pl,text))))s.matches[s.matchCount++]=i;
        }
    } else if(s.matchCount)s.matchIndex=(s.matchIndex+s.matchCount+(move>0?1:-1))%s.matchCount;
    static char16_t line[200];
    if(!s.matchCount) {Play(Icon::edge);Say(T(u"brak pasujących poleceń",u"no matching command"));return;}
    const auto& name=commandNames[s.matches[s.matchIndex]];Set(line,200,Pl()?name.pl:name.en);
    Cat(line,200,u", ");CatN(line,200,s.matchIndex+1);Cat(line,200,T(u" z ",u" of "));CatN(line,200,s.matchCount);
    Row(U().rows-2,line,0x70);Forget();Say(line);
}
inline void Palette() {
    auto& s=S();static char16_t text[64];text[0]=0;
    Play(Icon::open);Say(T(u"Polecenie: wpisz nazwę; strzałki wybierają; Enter wykonuje.",u"Command: type a name; arrows choose; Enter runs."));
    s.matchCount=0;s.matchIndex=0;for(size_t i=0;i<commandCount;++i)s.matches[s.matchCount++]=i;
    if(!Prompt(u"M-x",text,64,PaletteHook,nullptr,false) || !s.matchCount)return;
    Execute(commandNames[s.matches[s.matchIndex]].id);
}
// Keys every view shares. True when used.
inline bool GlobalKey(const Key& key) {
    auto& e=E();
    if(e.prefixX) {
        e.prefixX=false;
        if(Printable(key) && Fold(key.ch)==u'b') {Show(View::buffers);return true;}
        if(Printable(key) && Fold(key.ch)==u'k') {KillCurrent();return true;}
        if(key.ctrl && key.ch==u'f') {Execute(Command::findFile);return true;}
        if(key.ctrl && key.ch==u'c') {Leaving(Leave::menu);return true;}
        if(key.ctrl && key.ch==u's') {Execute(Command::save);return true;}
        Play(Icon::edge);Say(T(u"Control+X: B bufory, K zamknij bufor, Control+F otwórz plik, Control+S zapisz, Control+C wyjście",
            u"Control+X: B buffers, K kill buffer, Control+F find file, Control+S save, Control+C exit"));
        return true;
    }
    if(key.ctrl && key.ch==u'x') {e.prefixX=true;return true;}
    if(key.scan==scF10 || (key.alt && key.ch==u'x')) {Palette();return true;}
    if(key.scan==scF1) {if(S().view==View::help)Back();else Show(View::help);return true;}
    return false;
}
// Typing a letter in a list jumps to the next row that starts with it.
inline bool JumpTo(ListView& v,char16_t c) {
    static char16_t text[300];
    for(size_t step=1;step<=v.count;++step) {
        const size_t i=(v.index+step)%v.count;text[0]=0;v.label(i,text,300,false,v.context);
        const char16_t* name=text;while(*name==u' ' || *name==u'*' || *name==u'★')++name;
        if(Fold(*name)==Fold(c)) {v.index=i;Cue(MenuCue::focus);SayRow(v);return true;}
    }
    Play(Icon::edge);return false;
}
inline void ViewKey(const Key& key) {
    auto& s=S();ListView* v=CurrentList();if(!v)return;
    if(MoveList(*v,key))return;
    const bool enter=!key.scan && key.ch==13;const bool space=Printable(key) && key.ch==u' ';
    const char16_t letter=Printable(key)?Fold(key.ch):0;
    if(key.scan==scEsc) {if(s.view==View::home && !s.depth)Leaving(Leave::menu);else Back();return;}
    switch(s.view) {
        case View::home:
            if(enter || key.scan==scRight) {
                static const Command rows[homeCount]={Command::programs,Command::files,Command::packages,Command::network,Command::install,Command::selfInstall,Command::browser,Command::newFile,Command::notes,Command::calculator,
                    Command::buffers,Command::count,Command::clock,Command::help,Command::menu};
                if(rows[v->index]==Command::count)Palette();else Execute(rows[v->index]);
            } else if(letter)JumpTo(*v,letter);
            break;
        case View::programs: {
            auto& p=P();
            if(enter) {StartApp(v->index);break;}
            if(space) {ProgramDetail(v->index);break;}
            if(key.scan==scF5) {Execute(Command::rescan);break;}
            if(key.scan==scLeft || (!key.scan && key.ch==8)) {Back();break;}
            if(v->index>=p.count)break;
            if(key.scan==scF3 || letter==u'+') {
                const size_t record=FindRecord(p.apps[v->index],true);
                if(record==SIZE_MAX) {Fail(T(u"Lista zapamiętanych programów jest pełna.",u"The list of remembered programs is full."));break;}
                p.records[record].favourite=!p.records[record].favourite;p.apps[v->index].record=record;
                p.apps[v->index].flags=uint8_t((p.apps[v->index].flags&~appFavourite)|(p.records[record].favourite?appFavourite:0));
                const auto status=SaveRecords();Play(Icon::mark);
                Say(p.records[record].favourite?T(u"Dodano do ulubionych",u"Added to favourites"):T(u"Usunięto z ulubionych",u"Removed from favourites"));
                if(status!=EFI_SUCCESS)Fail(T(u"Nie zapisano listy na dysku Charona.",u"The list was not saved on Charon's disk."),status);
                break;
            }
            if(key.scan==scF2) {
                const size_t record=FindRecord(p.apps[v->index],true);if(record==SIZE_MAX)break;
                static char16_t args[128];Set(args,128,p.records[record].args);
                if(Prompt(T(u"Argumenty programu:",u"Program arguments:"),args,127)) {
                    Set(p.records[record].args,128,args);p.apps[v->index].record=record;SaveRecords();
                    Say(args[0]?T(u"Argumenty zapamiętane",u"Arguments remembered"):T(u"Argumenty usunięte",u"Arguments cleared"));}
                break;
            }
            if(letter)JumpTo(*v,letter);
            break;}
        case View::files:
            if(enter || key.scan==scRight) {FilesOpen();break;}
            if(key.scan==scLeft || (!key.scan && key.ch==8)) {FilesUp();break;}
            if(space) {FilesDetail();break;}
            if(key.scan==scDelete || letter==u'd') {FilesDelete();break;}
            if(key.scan==scF2 || letter==u'r') {FilesRename();break;}
            if(key.scan==scF5 || letter==u'c') {FilesCopy();break;}
            if(key.scan==scF6 || letter==u'p') {FilesPaste();break;}
            if(letter==u'+') {FilesNew(true);break;}
            if(letter==u'n') {FilesNew(false);break;}
            if(letter==u'e') {FilesOpen(false,true);break;}
            if(letter==u'h') {FilesOpen(true);break;}
            if(letter==u'g') {LoadFiles();Announce();break;}
            if(letter==u'm') {
                auto& f=F();if(f.atVolumes || v->index>=f.list.count) {Play(Icon::edge);break;}
                f.list.entries[v->index].marked=!f.list.entries[v->index].marked;Play(Icon::mark);
                Say(f.list.entries[v->index].marked?T(u"zaznaczony",u"marked"):T(u"odznaczony",u"unmarked"));
                if(v->index+1<v->count)++v->index;
                break;}
            if(letter)JumpTo(*v,letter);
            break;
        case View::packages:
            if(enter) {PackageRun(v->index);break;}
            if(space) {PackageDetail(v->index);break;}
            if(key.scan==scLeft || (!key.scan && key.ch==8)) {Back();break;}
            if(letter==u'c' || key.scan==scF5) {PackageCopy(v->index);break;}
            if(letter==u'd' || key.scan==scDelete) {PackageRemove(v->index);break;}
            if(letter==u'g') {ScanPackages();Announce();break;}
            if(letter)JumpTo(*v,letter);
            break;
        case View::buffers:
            if(enter && v->index<v->count) {E().current=s.bufferRows[v->index];s.view=View::editor;Forget();Announce();
                Buffer* b=Current();if(b && b->kind==Kind::text && b->length)SayLineAt(*b,b->point);break;}
            if((letter==u'k' || key.scan==scDelete) && v->index<v->count) {
                auto& b=E().buffers[s.bufferRows[v->index]];
                if(b.modified && !Confirm(T(u"Bufor ma niezapisane zmiany. Zamknąć?",u"The buffer has unsaved changes. Kill it?")))break;
                KillBuffer(s.bufferRows[v->index]);LoadBufferRows();Play(Icon::close);SayRow(*v);break;}
            if(key.scan==scLeft || (!key.scan && key.ch==8))Back();
            break;
        case View::network: {
            auto& n=net::N();
            if(key.scan==scLeft || (!key.scan && key.ch==8)) {Back();break;}
            if(space) {net::AdapterDetail(v->index);break;}
            if(letter==u'g' || key.scan==scF5) {net::LoadAdapters();Announce();break;}
            if(letter==u'd') {Execute(Command::download);break;}
            if(letter==u'l') {net::LoadDrivers(true);drivers::LoadUsbNetwork(true,true);net::LoadAdapters();Announce();break;}
            if(enter && v->index<n.count) {
                if(n.adapters[v->index].wifi) {
                    net::IpInfo info;
                    if(net::ReadIp(n.adapters[v->index],info)) {net::AdapterDetail(v->index);break;}
                    if(net::WifiScan(v->index))Show(View::wifi);
                } else net::Dhcp(n.adapters[v->index]);
            } else if(enter)Say(T(u"Firmware nie obsługuje żadnej karty sieciowej tego komputera.",u"The firmware drives no network adapter of this computer."));
            break;}
        case View::wifi:
            if(key.scan==scLeft || (!key.scan && key.ch==8)) {Back();break;}
            if(letter==u'g' || key.scan==scF5) {if(net::WifiScan(net::N().wifiAdapter))Announce();break;}
            if(enter && net::WifiJoin(v->index))Back();
            else if(space)SayRow(*v);
            break;
        case View::help:
            if(enter || space)SayRow(*v);
            else if(key.scan==scLeft || (!key.scan && key.ch==8))Back();
            break;
        case View::calc:
            if(key.scan==scLeft || (!key.scan && key.ch==8)) {Back();break;}
            if(enter || Printable(key)) {
                static char16_t expression[100];expression[0]=0;
                if(Printable(key) && !space) {expression[0]=key.ch;expression[1]=0;}
                if(!Prompt(T(u"Wyrażenie:",u"Expression:"),expression,99) || !expression[0])break;
                static char16_t result[100];static char16_t line[160];
                if(!Calculate(expression,result,100)) {Fail(T(u"Błąd w wyrażeniu.",u"Error in the expression."));break;}
                Set(line,160,expression);Cat(line,160,u" = ");Cat(line,160,result);
                if(s.historyCount==24) {for(size_t i=1;i<24;++i)Set(s.history[i-1],160,s.history[i]);--s.historyCount;}
                Set(s.history[s.historyCount++],160,line);s.calc.count=s.historyCount;s.calc.index=s.historyCount-1;
                Play(Icon::done);Say(result);
            }
            break;
        default:break;
    }
}
inline void Draw() {
    auto& s=S();static char16_t line[300];
    if(s.view==View::editor) {Buffer* b=Current();if(b) {DrawEditor(*b);return;}s.view=View::home;}
    ListView* v=CurrentList();if(!v)return;
    Cursor(false);
    Set(line,300,T(u"Środowisko Charona: ",u"Charon workspace: "));Cat(line,300,ViewName(s.view));
    if(s.view==View::files && !F().atVolumes) {static char16_t label[64];VolumeLabel(F().folder.volume,label,64);Cat(line,300,u" - ");Cat(line,300,label);Cat(line,300,u" ");Cat(line,300,F().folder.path);}
    Title(line);DrawList(*v);
    line[0]=0;if(v->count) {CatN(line,300,v->index+1);Cat(line,300,u" / ");CatN(line,300,v->count);}
    Cat(line,300,u"   ");
    Cat(line,300,s.view==View::programs?T(u"Enter: start  F2: argumenty  F3: ulubiony  F5: szukaj  Spacja: opis",u"Enter: start  F2: arguments  F3: favourite  F5: scan  Space: details")
        :s.view==View::files?T(u"Enter  Backspace  M C P R D + N E H  Spacja: opis",u"Enter  Backspace  M C P R D + N E H  Space: details")
        :s.view==View::packages?T(u"Enter: start  C: kopiuj  D: usuń  Spacja: opis",u"Enter: start  C: copy  D: remove  Space: details")
        :s.view==View::network?T(u"Enter: połącz  Spacja: opis  D: pobierz plik  L: sterowniki  G: odśwież",u"Enter: connect  Space: details  D: download a file  L: drivers  G: refresh")
        :s.view==View::wifi?T(u"Enter: połącz  G: szukaj ponownie",u"Enter: join  G: scan again")
        :s.view==View::calc?T(u"Enter albo cyfra: nowe wyrażenie",u"Enter or a digit: new expression"):T(u"F1: pomoc  Alt+X / F10: polecenia  Esc: wstecz",u"F1: help  Alt+X / F10: commands  Esc: back"));
    ModeLine(line);
}
// The workspace's main loop. Returns where the boot menu should go.
inline Leave Run(Host& host) {
    auto& u=U();auto& s=S();auto* boot=host.boot;
    memset(&u,0,sizeof(u));u.host=&host;u.out=host.system->ConOut;u.leave=Leave::menu;
    u.audioFailed=!ValidMenuVoice(host.voice);
    ReviewMode(true);   // r401: the reader's review keys work in the whole workspace
    efi_guid exGuid={0xdd9e7534,0x7762,0x4698,{0x8c,0x14,0xf5,0x85,0x17,0xa6,0x25,0xaa}};
    if(host.system->ConsoleInHandle && boot->HandleProtocol(host.system->ConsoleInHandle,&exGuid,(void**)&u.ex)!=EFI_SUCCESS)u.ex=nullptr;
    if(boot->CreateEvent(EVT_TIMER,TPL_APPLICATION,nullptr,nullptr,&u.timer)!=EFI_SUCCESS || boot->SetTimer(u.timer,TimerPeriodic,100000)!=EFI_SUCCESS) {
        if(u.timer)boot->CloseEvent(u.timer);
        u.timer=nullptr;}
    // Buffers, records and the kill ring live across visits; views start fresh.
    s.view=View::home;s.depth=0;
    s.home={homeCount,s.home.index<homeCount?s.home.index:0,0,HomeLabel,nullptr};
    s.help={helpCount,0,0,HelpLabel,nullptr};s.calc={s.historyCount,s.historyCount?s.historyCount-1:0,0,CalcLabel,nullptr};
    s.buffers={0,0,0,BufferLabel,nullptr};
    P().view.label=ProgramLabel;F().view.label=FilesLabel;K().view.label=PackageLabel;
    net::N().view.label=net::AdapterLabel;net::N().wifiView.label=net::WifiLabel;
    if(!F().folder.volume)F().atVolumes=true;
    E().prefixX=E().prefixE=false;
    if(E().current>=maxBuffers || E().buffers[E().current].kind==Kind::none) {E().current=SIZE_MAX;for(size_t i=0;i<maxBuffers;++i)if(E().buffers[i].kind!=Kind::none) {E().current=i;break;}}
    // r409: the boot menu opened the workspace because a multi-system plan is waiting: the plan
    // takes the turn first (pass-through to an installer does not come back here).
#ifdef HAK_CHARON_HTTPS
    planrun::Register();
    if(host.planResume) {host.planResume=false;Reshape();planrun::AtStart(planrun::MachineNow());}
#ifdef HAK_CHARON_TRACE
    net::many::SelfTest(install::EnsureNetwork);   // r410 (agent 3): test tooling, only with EFI\HAK\net-test.txt
    {   // r410 (agent 3): test tooling, only with EFI\HAK\dos-test.txt: CharonDOS's "all tested programs" onto its package
        FileRef ref;ref.volume=host.selfDevice;Set(ref.path,pathUnits,u"\\EFI\\HAK\\dos-test.txt");efi_file_protocol* file=nullptr;
        if(OpenFile(ref,EFI_FILE_MODE_READ,0,&file)==EFI_SUCCESS && file) {
            file->Close(file);efi_handle volume=nullptr;static char16_t package[pathUnits];
            if(dosapps::Package(volume,package,pathUnits) && install::EnsureNetwork()) {
                charontrace::Text("CHARON dostest: begins");dosapps::Fetch(volume,package,0);charontrace::Text("CHARON dostest: done");}
            else charontrace::Text("CHARON dostest: no writable CharonDOS package or no network");
        }
    }
#endif
#endif
    Reshape();Play(Icon::open);
    {static char16_t text[500];Set(text,500,T(u"Środowisko Charona. F1 pomoc, Alt+X polecenia. ",u"Charon workspace. F1 help, Alt+X commands. "));
        static char16_t row[100];HomeLabel(s.home.index,row,100,true,nullptr);Cat(text,500,row);Say(text);}
    drivers::LoadInput(false);   // r408: a USB keyboard/mouse the firmware does not drive
    while(!u.quit) {
        Draw();
        const Key key=WaitKey();
        if(s.view==View::editor) {
            Buffer* b=Current();
            if(!b) {s.view=View::home;continue;}
            const auto result=EditKey(*b,key);
            if(result==EditResult::close)Back();
            else if(result==EditResult::global && !GlobalKey(key))Play(Icon::edge);
            continue;
        }
        if(!GlobalKey(key))ViewKey(key);
    }
    Cursor(false);
    if(u.timer)boot->CloseEvent(u.timer);
    u.timer=nullptr;
    if(u.out) {u.out->SetAttribute(u.out,7);u.out->ClearScreen(u.out);}
    Play(Icon::close);
    return u.leave;
}
}  // namespace ws
}  // namespace hakefi
