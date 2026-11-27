#pragma once
// r409: host stand-in for src/efi/CharonWsCore.h, used only by tests/charon-install-answers-host.py.
//
// The real CharonWsCore.h needs firmware (console, speech, block devices). The dialogue engine
// (CharonWsAsk.h), the reader tables (CharonWsReaders.h) and the answer-file generators only need a
// small part of it: text helpers, Say/Echo, keys, Prompt/Confirm, files. This header gives them
//   - a scripted keyboard (WaitKey takes keys from a queue; an empty queue ends the run with an error),
//   - a transcript of everything said,
//   - folders of the host as volumes behind the tree's own efi_file_protocol,
// and includes charon-core-cuts.inc: Fold, SameFold, Prompt, Confirm, JoinPath, CopyTree, CopyCharonTree,
// Utf8ToUtf16 and Utf16ToUtf8, cut out of the real CharonWsCore.h by the test runner, unchanged.
// GPL-2.0-or-later.
#include <stddef.h>
#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>
#include <efi/types.h>
#include <efi/boot-services.h>
#include <efi/protocol/file.h>
#include "CharonPrivate.h"

namespace hakefi {
namespace speechtext {
inline char16_t LowerCase(char16_t c) {
    if(c>=u'A' && c<=u'Z')return char16_t(c+32);
    switch(c) {case u'Ą':return u'ą';case u'Ć':return u'ć';case u'Ę':return u'ę';case u'Ł':return u'ł';case u'Ń':return u'ń';
        case u'Ó':return u'ó';case u'Ś':return u'ś';case u'Ź':return u'ź';case u'Ż':return u'ż';}
    return c;
}
}  // namespace speechtext
enum class MenuCue {none,welcome,focus,edge,wrap,submenuOpen,submenuClose,popupOpen,popupClose,dialogOpen,dialogClose,select,continueBoot,systemLaunch,bootManagerLoaded};
namespace ws {
struct Key {uint16_t scan;char16_t ch;bool ctrl,alt,shift;};
constexpr uint16_t scUp=1,scDown=2,scRight=3,scLeft=4,scHome=5,scEnd=6,scInsert=7,scDelete=8,scPageUp=9,scPageDown=10,
    scF1=11,scF2=12,scF3=13,scF4=14,scF5=15,scF6=16,scF7=17,scF8=18,scF9=19,scF10=20,scEsc=23;
inline size_t Len(const char16_t* s) {size_t n=0;while(s && s[n])++n;return n;}
inline void Cat(char16_t* out,size_t cap,const char16_t* text) {size_t n=Len(out);for(;text && *text && n+1<cap;++text)out[n++]=*text;if(cap)out[n]=0;}
inline void Set(char16_t* out,size_t cap,const char16_t* text) {if(cap) {out[0]=0;Cat(out,cap,text);}}
inline void CatC(char16_t* out,size_t cap,char16_t c) {char16_t t[2]={c,0};Cat(out,cap,t);}
inline void CatN(char16_t* out,size_t cap,uint64_t value) {char16_t d[24];size_t n=0;do {d[n++]=char16_t(u'0'+value%10);value/=10;}while(value);while(n)CatC(out,cap,d[--n]);}
inline void CatAscii(char16_t* out,size_t cap,const char* text) {for(;text && *text;++text)CatC(out,cap,(char16_t)(unsigned char)*text);}

// ---- the host's state ---------------------------------------------------------
struct Effects {bool (*SpeechIdle)(void*);void* context;};
struct HostServices {efi_boot_services* boot;Effects* effects;efi_handle selfDevice;bool pl;};
struct Universe {HostServices* host;size_t rows;};
struct HostState {
    std::deque<Key> keys;
    std::vector<std::u16string> said;     // everything spoken, in order
    std::vector<std::u16string> failed;   // what Fail() said
    size_t keysTaken=0;
};
inline HostState& H() {static HostState state;return state;}
inline efi_status EFIAPI HostStall(size_t) {return EFI_SUCCESS;}
inline efi_status EFIAPI HostAllocatePool(efi_memory_type,size_t size,void** out) {*out=calloc(1,size?size:1);return *out?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;}
inline efi_status EFIAPI HostFreePool(void* p) {free(p);return EFI_SUCCESS;}
inline Universe& U() {
    static efi_boot_services boot;static HostServices host;static Universe universe;
    if(!universe.host) {memset(&boot,0,sizeof(boot));boot.Stall=HostStall;boot.AllocatePool=HostAllocatePool;boot.FreePool=HostFreePool;
        host.boot=&boot;host.effects=nullptr;host.selfDevice=nullptr;host.pl=false;universe.host=&host;universe.rows=25;}
    return universe;
}
inline bool Pl() {return U().host->pl;}
inline const char16_t* T(const char16_t* pl,const char16_t* en) {return U().host->pl?pl:en;}
inline void Pump() {}
void HostSaid(const char16_t* text);   // the harness: which question this is
inline void Say(const char16_t* text) {if(text && text[0]) {H().said.emplace_back(text);HostSaid(text);}}
inline void SayEcho(const char16_t*) {}
inline void SayChar(char16_t) {}
inline void Echo(const char16_t*) {}
inline void Cursor(bool,size_t=0,size_t=0) {}
inline void Tone(unsigned,unsigned) {}
inline void Cue(MenuCue) {}
enum class Icon {edge,open,close,mark,save,deleted,error,yank,search,done,task};
inline void Play(Icon) {}
inline void PositionVoice(char16_t*,size_t,size_t) {}
inline void Fail(const char16_t* text,efi_status=EFI_SUCCESS) {H().failed.emplace_back(text);std::u16string line=u"[FAIL] ";line+=text;H().said.push_back(line);}
inline Key WaitKey() {
    auto& h=H();
    if(h.keys.empty()) {printf("error=the key script ran out after %zu keys\n",h.keysTaken);
        // r410: the last things said, so that a vector can be corrected without guessing
        for(size_t i=h.said.size()>4?h.said.size()-4:0;i<h.said.size();++i) {printf("said=");for(char16_t c:h.said[i])putchar(c<128?char(c):'?');printf("\n");}
        exit(3);}
    const Key key=h.keys.front();h.keys.pop_front();++h.keysTaken;return key;
}
inline bool Printable(const Key& key) {return !key.scan && key.ch>=32 && key.ch!=127 && !key.ctrl && !key.alt;}
inline bool IsCancel(const Key& key) {return key.scan==scEsc || (key.ctrl && key.ch==u'g');}

// ---- folders of the host as volumes -------------------------------------------
struct HostVolume {std::filesystem::path root;};
struct HostFile {
    efi_file_protocol protocol;   // first member: the protocol pointer is the object
    std::filesystem::path path;FILE* stream=nullptr;
    std::vector<std::filesystem::directory_entry> children;size_t next=0;bool directory=false;
};
inline HostFile* AsHost(efi_file_protocol* self) {return reinterpret_cast<HostFile*>(self);}
inline HostFile* NewHostFile(const std::filesystem::path& path,FILE* stream);
inline std::filesystem::path HostJoin(std::filesystem::path path,const char16_t* name) {
    std::u16string part;
    for(const char16_t* c=name;;++c) {
        if(*c==u'\\' || !*c) {if(!part.empty())path/=std::filesystem::path(part);part.clear();if(!*c)break;}
        else part+=*c;
    }
    return path;
}
inline efi_status EFIAPI HostOpen(efi_file_protocol* self,efi_file_protocol** result,const char16_t* name,uint64_t mode,uint64_t attributes) {
    const std::filesystem::path path=HostJoin(AsHost(self)->path,name);
    std::error_code error;*result=nullptr;
    if(std::filesystem::is_directory(path,error)) {*result=&NewHostFile(path,nullptr)->protocol;return EFI_SUCCESS;}
    if((mode&EFI_FILE_MODE_CREATE) && (attributes&EFI_FILE_DIRECTORY)) {
        if(!std::filesystem::create_directory(path,error) && !std::filesystem::is_directory(path,error))return EFI_DEVICE_ERROR;
        *result=&NewHostFile(path,nullptr)->protocol;return EFI_SUCCESS;}
    FILE* stream=fopen(path.string().c_str(),(mode&EFI_FILE_MODE_WRITE)?"r+b":"rb");
    if(!stream && (mode&EFI_FILE_MODE_CREATE))stream=fopen(path.string().c_str(),"w+b");
    if(!stream)return EFI_NOT_FOUND;
    *result=&NewHostFile(path,stream)->protocol;return EFI_SUCCESS;
}
inline efi_status EFIAPI HostClose(efi_file_protocol* self) {HostFile* f=AsHost(self);if(f->stream)fclose(f->stream);delete f;return EFI_SUCCESS;}
inline efi_status EFIAPI HostDelete(efi_file_protocol* self) {
    HostFile* f=AsHost(self);if(f->stream)fclose(f->stream);std::error_code error;const bool ok=std::filesystem::remove(f->path,error);delete f;
    return ok?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
inline size_t HostInfo(const std::filesystem::path& path,void* buffer,size_t capacity) {
    const std::u16string name=path.filename().u16string();const size_t need=sizeof(efi_file_info)+(name.size()+1)*sizeof(char16_t);
    if(capacity<need)return need;
    auto* info=static_cast<efi_file_info*>(buffer);memset(info,0,need);std::error_code error;
    const bool directory=std::filesystem::is_directory(path,error);
    info->Size=need;info->Attribute=directory?EFI_FILE_DIRECTORY:0;
    info->FileSize=directory?0:uint64_t(std::filesystem::file_size(path,error));info->PhysicalSize=info->FileSize;
    memcpy(info->FileName,name.c_str(),(name.size()+1)*sizeof(char16_t));
    return need;
}
inline efi_status EFIAPI HostRead(efi_file_protocol* self,size_t* length,void* buffer) {
    HostFile* f=AsHost(self);
    if(!f->directory) {if(!f->stream)return EFI_DEVICE_ERROR;*length=fread(buffer,1,*length,f->stream);return ferror(f->stream)?EFI_DEVICE_ERROR:EFI_SUCCESS;}
    if(f->next>=f->children.size()) {*length=0;return EFI_SUCCESS;}
    const size_t need=HostInfo(f->children[f->next].path(),buffer,*length);
    if(*length<need) {*length=need;return EFI_BUFFER_TOO_SMALL;}
    *length=need;++f->next;return EFI_SUCCESS;
}
inline efi_status EFIAPI HostWrite(efi_file_protocol* self,size_t* length,void* buffer) {
    HostFile* f=AsHost(self);if(!f->stream)return EFI_DEVICE_ERROR;*length=fwrite(buffer,1,*length,f->stream);return ferror(f->stream)?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
inline efi_status EFIAPI HostFlush(efi_file_protocol* self) {HostFile* f=AsHost(self);return !f->stream || fflush(f->stream)==0?EFI_SUCCESS:EFI_DEVICE_ERROR;}
inline efi_status EFIAPI HostSetPosition(efi_file_protocol* self,uint64_t position) {
    HostFile* f=AsHost(self);if(f->directory) {f->next=0;return EFI_SUCCESS;}
    if(!f->stream)return EFI_DEVICE_ERROR;
    return (position==~uint64_t(0)?fseek(f->stream,0,SEEK_END):fseek(f->stream,long(position),SEEK_SET))==0?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
inline efi_status EFIAPI HostGetPosition(efi_file_protocol* self,uint64_t* position) {HostFile* f=AsHost(self);if(!f->stream)return EFI_UNSUPPORTED;*position=uint64_t(ftell(f->stream));return EFI_SUCCESS;}
inline efi_status EFIAPI HostGetInfo(efi_file_protocol* self,efi_guid*,size_t* size,void* buffer) {
    const size_t need=HostInfo(AsHost(self)->path,buffer,*size);
    if(*size<need) {*size=need;return EFI_BUFFER_TOO_SMALL;}
    *size=need;return EFI_SUCCESS;
}
inline HostFile* NewHostFile(const std::filesystem::path& path,FILE* stream) {
    HostFile* f=new HostFile;memset(&f->protocol,0,sizeof(f->protocol));
    f->protocol.Revision=EFI_FILE_PROTOCOL_REVISION;f->protocol.Open=HostOpen;f->protocol.Close=HostClose;f->protocol.Delete=HostDelete;
    f->protocol.Read=HostRead;f->protocol.Write=HostWrite;f->protocol.Flush=HostFlush;f->protocol.SetPosition=HostSetPosition;
    f->protocol.GetPosition=HostGetPosition;f->protocol.GetInfo=HostGetInfo;
    f->path=path;f->stream=stream;f->directory=!stream;
    if(f->directory) {std::error_code error;for(const auto& e:std::filesystem::directory_iterator(path,error))f->children.push_back(e);}
    return f;
}
constexpr size_t pathUnits=512;
struct FileRef {efi_handle volume;char16_t path[pathUnits];};   // path starts with a backslash
inline efi_file_protocol* OpenRoot(efi_handle volume) {
    if(!volume)return nullptr;
    return &NewHostFile(static_cast<HostVolume*>(volume)->root,nullptr)->protocol;
}
inline efi_status OpenFile(const FileRef& ref,uint64_t mode,uint64_t attributes,efi_file_protocol** file) {
    *file=nullptr;efi_file_protocol* root=OpenRoot(ref.volume);if(!root)return EFI_NOT_FOUND;
    if(ref.path[0]==u'\\' && !ref.path[1]) {*file=root;return EFI_SUCCESS;}
    efi_status status=root->Open(root,file,ref.path,mode,attributes);root->Close(root);
    if(status==EFI_SUCCESS && !*file)status=EFI_NOT_FOUND;
    return status;
}
inline efi_status MakeFolders(efi_handle volume,const char16_t* path) {
    const std::filesystem::path full=HostJoin(static_cast<HostVolume*>(volume)->root,path);
    std::error_code error;std::filesystem::create_directories(full,error);
    return std::filesystem::is_directory(full,error)?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
inline efi_status CopyFile(const FileRef& from,const FileRef& to) {
    efi_file_protocol* in=nullptr;efi_file_protocol* out=nullptr;
    if(OpenFile(from,EFI_FILE_MODE_READ,0,&in)!=EFI_SUCCESS)return EFI_NOT_FOUND;
    if(OpenFile(to,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0,&out)!=EFI_SUCCESS) {in->Close(in);return EFI_DEVICE_ERROR;}
    static uint8_t block[65536];efi_status status=EFI_SUCCESS;
    for(;;) {size_t part=sizeof(block);if(in->Read(in,&part,block)!=EFI_SUCCESS) {status=EFI_DEVICE_ERROR;break;}if(!part)break;
        size_t wrote=part;if(out->Write(out,&wrote,block)!=EFI_SUCCESS || wrote!=part) {status=EFI_DEVICE_ERROR;break;}}
    in->Close(in);out->Close(out);return status;
}
inline void* Allocate(size_t bytes) {return calloc(1,bytes?bytes:1);}
inline void Release(void* p) {free(p);}
constexpr size_t maxEntries=512,nameUnits=128;
struct Entry {char16_t name[nameUnits];bool directory,marked;uint64_t size;};
struct Listing {Entry entries[maxEntries];size_t count;bool truncated;};
inline efi_status ReadFolder(const FileRef& folder,Listing& list) {
    list.count=0;list.truncated=false;efi_file_protocol* dir=nullptr;
    auto status=OpenFile(folder,EFI_FILE_MODE_READ,0,&dir);if(status!=EFI_SUCCESS)return status;
    alignas(8) static uint8_t buffer[2048];
    for(;;) {
        size_t size=sizeof(buffer);status=dir->Read(dir,&size,buffer);
        if(status!=EFI_SUCCESS || !size)break;
        const auto* info=reinterpret_cast<const efi_file_info*>(buffer);
        if(list.count>=maxEntries || Len(info->FileName)>=nameUnits) {list.truncated=true;continue;}
        auto& e=list.entries[list.count++];Set(e.name,nameUnits,info->FileName);
        e.directory=(info->Attribute&EFI_FILE_DIRECTORY)!=0;e.marked=false;e.size=info->FileSize;
    }
    dir->Close(dir);return EFI_SUCCESS;
}
#include "charon-core-cuts.inc"
}  // namespace ws
}  // namespace hakefi
