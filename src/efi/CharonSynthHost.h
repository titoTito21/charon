// Charon's side of the synthesizer add-ons (r393). GPL-2.0-or-later.
//
// Lists the manifests in \EFI\HAK\synths (CharonSynthModule.h), checks a
// module's SHA-256 against its manifest and loads it (a boot service
// driver: LoadImage + StartImage with its data folder as LoadOptions),
// then finds the CharonSynthModule it installed on a new handle. The add-ons
// are optional: without them, or on any error, Charon speaks with TVox.
// Only at TPL_APPLICATION (LoadImage/StartImage).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <efi/system-table.h>
#include <efi/protocol/simple-file-system.h>
#include <efi/protocol/file.h>
#include <efi/protocol/loaded-image.h>
#include <efi/protocol/device-path.h>
#include "CharonSynthModule.h"
#include "CharonSha256.h"
namespace hakefi::synths {
constexpr size_t maxSynths=8,maxVoices=16;
struct Info {
    char16_t manifest[64]={};char name[48]={};char version[24]={};char16_t module[96]={};char16_t data[96]={};
    uint8_t sha[32]={};bool hasSha=false;uint32_t abi=0;uint8_t key=0;
    efi_handle volume=nullptr;   // r400: the volume the manifest was found on (null: Charon's own)
    size_t voices=0;char id[maxVoices][32]={};char16_t voiceName[maxVoices][48]={};char language[maxVoices][8]={};
};
// 1..255, stable for a manifest file name (settings V11 byte 42; 0 = TVox).
inline uint8_t KeyOf(const char16_t* name) {
    uint32_t h=2166136261u;for(;*name;++name) {char16_t c=*name;if(c>=u'A' && c<=u'Z')c=char16_t(c+32);h=(h^c)*16777619u;}
    const uint8_t k=uint8_t(h^(h>>8)^(h>>16)^(h>>24));return k?k:1;
}
inline int Hex(char c) {return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;}
inline void Wide(const char* s,size_t n,char16_t* out,size_t cap) {   // UTF-8 (BMP) to UTF-16
    size_t k=0;const unsigned char* p=(const unsigned char*)s;const unsigned char* e=p+n;
    while(p<e && k+1<cap) {uint32_t c=*p++;
        if(c>=0xc0 && c<0xe0 && p<e)c=((c&31)<<6)|(*p++&63);
        else if(c>=0xe0 && c<0xf0 && p+1<e) {c=((c&15)<<12)|((p[0]&63)<<6)|(p[1]&63);p+=2;}
        else if(c>=0x80)c='?';
        out[k++]=char16_t(c);}
    out[k]=0;
}
inline void Narrow(const char* s,size_t n,char* out,size_t cap) {size_t k=0;for(size_t i=0;i<n && k+1<cap;++i)out[k++]=s[i];out[k]=0;}
inline void Trim(const char*& s,const char*& e) {while(s<e && (*s==' ' || *s=='\t'))++s;while(e>s && (e[-1]==' ' || e[-1]=='\t' || e[-1]=='\r'))--e;}
inline bool ParseManifest(const char* text,size_t size,Info& m) {
    const char* p=text;const char* end=text+size;
    while(p<end) {
        const char* e=p;while(e<end && *e!='\n')++e;
        const char* s=p;const char* le=e;Trim(s,le);p=e<end?e+1:e;
        if(s>=le || *s=='#')continue;
        const char* eq=s;while(eq<le && *eq!='=')++eq;if(eq==le)continue;
        const char* ks=s;const char* ke=eq;Trim(ks,ke);const char* vs=eq+1;const char* ve=le;Trim(vs,ve);
        auto key=[&](const char* k) {size_t n=0;while(k[n])++n;if(size_t(ke-ks)!=n)return false;for(size_t i=0;i<n;++i)if(ks[i]!=k[i])return false;return true;};
        if(key("abi")) {uint32_t v=0;for(const char* q=vs;q<ve && *q>='0' && *q<='9';++q)v=v*10+uint32_t(*q-'0');m.abi=v;}
        else if(key("name"))Narrow(vs,size_t(ve-vs),m.name,sizeof(m.name));
        else if(key("version"))Narrow(vs,size_t(ve-vs),m.version,sizeof(m.version));
        else if(key("module")) {char16_t f[64];Wide(vs,size_t(ve-vs),f,64);size_t k=0;for(const char16_t* q=u"\\EFI\\HAK\\synths\\";*q;++q)m.module[k++]=*q;for(size_t i=0;f[i] && k+1<96;++i)m.module[k++]=f[i]==u'/'?u'\\':f[i];m.module[k]=0;}
        else if(key("data")) {char16_t f[64];Wide(vs,size_t(ve-vs),f,64);size_t k=0;for(const char16_t* q=u"\\EFI\\HAK\\synths\\";*q;++q)m.data[k++]=*q;for(size_t i=0;f[i] && k+1<96;++i)m.data[k++]=f[i]==u'/'?u'\\':f[i];m.data[k]=0;}
        else if(key("sha256") && ve-vs==64) {bool ok=true;for(int i=0;i<32 && ok;++i) {const int a=Hex(vs[i*2]),b=Hex(vs[i*2+1]);if(a<0 || b<0)ok=false;else m.sha[i]=uint8_t(a<<4|b);}m.hasSha=ok;}
        else if(key("voice") && m.voices<maxVoices) {
            // id | spoken name | language
            const char* a=vs;const char* bar1=a;while(bar1<ve && *bar1!='|')++bar1;
            const char* bar2=bar1<ve?bar1+1:ve;while(bar2<ve && *bar2!='|')++bar2;
            const char* i0=a;const char* i1=bar1;Trim(i0,i1);
            const char* n0=bar1<ve?bar1+1:ve;const char* n1=bar2;Trim(n0,n1);
            const char* l0=bar2<ve?bar2+1:ve;const char* l1=ve;Trim(l0,l1);
            if(i1>i0) {const size_t v=m.voices++;Narrow(i0,size_t(i1-i0),m.id[v],32);Wide(n0,size_t(n1-n0),m.voiceName[v],48);Narrow(l0,size_t(l1-l0),m.language[v],8);}
        }
    }
    return m.module[0] && m.voices;
}
// The manifests on Charon's volume, at most `maximum`.
inline size_t Scan(efi_boot_services* boot,efi_file_protocol* root,Info* out,size_t maximum) {
    if(!boot || !root)return 0;
    efi_file_protocol* dir=nullptr;
    if(root->Open(root,&dir,(char16_t*)u"\\EFI\\HAK\\synths",EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !dir)return 0;
    size_t count=0;alignas(8) uint8_t entry[600];
    for(unsigned guard=0;guard<128 && count<maximum;++guard) {
        size_t bytes=sizeof(entry);
        if(dir->Read(dir,&bytes,entry)!=EFI_SUCCESS || !bytes)break;
        if(*reinterpret_cast<const uint64_t*>(entry+72)&0x10)continue;
        const char16_t* name=reinterpret_cast<const char16_t*>(entry+80);size_t n=0;while(name[n] && n<60)++n;
        static const char16_t ext[]=u".manifest";const size_t e=9;
        if(n<=e)continue;bool match=true;for(size_t i=0;i<e && match;++i) {char16_t c=name[n-e+i];if(c>=u'A' && c<=u'Z')c=char16_t(c+32);match=c==ext[i];}
        if(!match)continue;
        Info& m=out[count];m=Info{};for(size_t i=0;i<n;++i)m.manifest[i]=name[i];m.manifest[n]=0;
        efi_file_protocol* file=nullptr;
        if(dir->Open(dir,&file,(char16_t*)m.manifest,EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !file)continue;
        char text[4096];size_t size=sizeof(text)-1;
        const bool read=file->Read(file,&size,text)==EFI_SUCCESS;file->Close(file);
        if(!read)continue;text[size]=0;
        if(ParseManifest(text,size,m)) {m.key=KeyOf(m.manifest);++count;}
    }
    dir->Close(dir);return count;
}
// r400: add-ons may live on ANY volume (\EFI\HAK\synths there), so large
// voices need not fill the EFI system partition. Charon's own volume wins
// when the same manifest name exists twice.
inline size_t ScanAll(efi_boot_services* boot,efi_handle ownVolume,efi_file_protocol* ownRoot,Info* out,size_t maximum) {
    size_t count=Scan(boot,ownRoot,out,maximum);
    for(size_t i=0;i<count;++i)out[i].volume=ownVolume;
    efi_guid fsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;efi_handle* handles=nullptr;size_t n=0;
    if(!boot || boot->LocateHandleBuffer(ByProtocol,&fsGuid,nullptr,&n,&handles)!=EFI_SUCCESS || !handles)return count;
    static Info other[maxSynths];
    for(size_t h=0;h<n && count<maximum;++h) {
        if(handles[h]==ownVolume)continue;
        efi_simple_file_system_protocol* fs=nullptr;efi_file_protocol* root=nullptr;
        if(boot->HandleProtocol(handles[h],&fsGuid,(void**)&fs)!=EFI_SUCCESS || !fs || fs->OpenVolume(fs,&root)!=EFI_SUCCESS || !root)continue;
        const size_t found=Scan(boot,root,other,maxSynths);root->Close(root);
        for(size_t i=0;i<found && count<maximum;++i) {
            bool known=false;for(size_t k=0;k<count;++k)if(out[k].key==other[i].key)known=true;
            if(known)continue;
            out[count]=other[i];out[count++].volume=handles[h];
        }
    }
    boot->FreePool(handles);return count;
}
inline const Info* Find(const Info* list,size_t count,uint8_t key) {for(size_t i=0;i<count;++i)if(list[i].key==key)return &list[i];return nullptr;}
// Why a module could not be used (spoken; the file name is added).
enum class Failure {none,missing,checksum,abi,load,start,protocol};
// Loads the module of `m` from the volume of `device`; the module stays
// resident. *out receives its protocol.
inline Failure Load(efi_boot_services* boot,efi_handle parent,efi_handle device,efi_file_protocol* root,const Info& m,CharonSynthModule** out) {
    *out=nullptr;
    if(m.abi<charonSynthAbiOldest || m.abi>charonSynthAbi)return Failure::abi;
    efi_file_protocol* file=nullptr;
    if(root->Open(root,&file,(char16_t*)m.module,EFI_FILE_MODE_READ,0)!=EFI_SUCCESS || !file)return Failure::missing;
    uint64_t end=0;uint8_t* data=nullptr;size_t size=0;Failure failure=Failure::none;
    if(file->SetPosition(file,UINT64_MAX)!=EFI_SUCCESS || file->GetPosition(file,&end)!=EFI_SUCCESS || !end || end>(64u<<20)
        || file->SetPosition(file,0)!=EFI_SUCCESS || boot->AllocatePool(EfiBootServicesData,size_t(end),(void**)&data)!=EFI_SUCCESS || !data)failure=Failure::missing;
    else {size=size_t(end);if(file->Read(file,&size,data)!=EFI_SUCCESS || size!=end)failure=Failure::missing;}
    file->Close(file);
    if(failure!=Failure::none) {if(data)boot->FreePool(data);return failure;}
    if(m.hasSha) {Sha256 h;h.Update(data,size);uint8_t got[32];h.Final(got);for(int i=0;i<32;++i)if(got[i]!=m.sha[i])failure=Failure::checksum;}
    else failure=Failure::checksum;   // a manifest must name the file's hash
    if(failure!=Failure::none) {boot->FreePool(data);return failure;}
    // Device path of the file: the volume's path + a file node, so the
    // module's LoadedImage->DeviceHandle is Charon's volume.
    efi_guid pathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;const uint8_t* volume=nullptr;uint8_t* path=nullptr;
    size_t prefix=0;
    if(boot->HandleProtocol(device,&pathGuid,(void**)&volume)==EFI_SUCCESS && volume)
        while(prefix<4096) {const size_t n=size_t(volume[prefix+2]|(volume[prefix+3]<<8));if(volume[prefix]==0x7f || n<4)break;prefix+=n;}
    size_t nameBytes=0;while(m.module[nameBytes/2])nameBytes+=2;nameBytes+=2;
    const size_t total=prefix+4+nameBytes+4;
    if(boot->AllocatePool(EfiBootServicesData,total,(void**)&path)!=EFI_SUCCESS || !path) {boot->FreePool(data);return Failure::load;}
    for(size_t i=0;i<prefix;++i)path[i]=volume[i];
    uint8_t* node=path+prefix;node[0]=4;node[1]=4;node[2]=uint8_t((4+nameBytes)&255);node[3]=uint8_t((4+nameBytes)>>8);
    for(size_t i=0;i<nameBytes;i+=2) {const char16_t c=m.module[i/2];node[4+i]=uint8_t(c);node[5+i]=uint8_t(c>>8);}
    uint8_t* tail=node+4+nameBytes;tail[0]=0x7f;tail[1]=0xff;tail[2]=4;tail[3]=0;
    efi_handle image=nullptr;
    const efi_status loaded=boot->LoadImage(false,parent,(efi_device_path_protocol*)path,data,size,&image);
    boot->FreePool(path);boot->FreePool(data);
    if(loaded!=EFI_SUCCESS || !image)return Failure::load;
    // The handles that carry the protocol before the start: the new one is the module's.
    efi_guid guid=CHARON_SYNTH_MODULE_GUID;
    efi_handle* before=nullptr;size_t beforeCount=0;
    if(boot->LocateHandleBuffer(ByProtocol,&guid,nullptr,&beforeCount,&before)!=EFI_SUCCESS) {before=nullptr;beforeCount=0;}
    efi_guid loadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;efi_loaded_image_protocol* li=nullptr;
    static char16_t options[maxSynths*2][96];static size_t optionSlot=0;   // stays valid while the module runs
    char16_t* option=options[optionSlot++%(maxSynths*2)];size_t k=0;for(;m.data[k] && k+1<96;++k)option[k]=m.data[k];option[k]=0;
    if(boot->HandleProtocol(image,&loadedGuid,(void**)&li)==EFI_SUCCESS && li) {li->LoadOptions=option;li->LoadOptionsSize=uint32_t((k+1)*2);}
    size_t exitSize=0;char16_t* exitData=nullptr;
    if(boot->StartImage(image,&exitSize,&exitData)!=EFI_SUCCESS) {if(before)boot->FreePool(before);return Failure::start;}
    efi_handle* after=nullptr;size_t afterCount=0;CharonSynthModule* found=nullptr;
    if(boot->LocateHandleBuffer(ByProtocol,&guid,nullptr,&afterCount,&after)==EFI_SUCCESS && after) {
        for(size_t i=0;i<afterCount && !found;++i) {
            bool old=false;for(size_t j=0;j<beforeCount;++j)if(before[j]==after[i])old=true;
            if(old)continue;
            CharonSynthModule* module=nullptr;
            if(boot->HandleProtocol(after[i],&guid,(void**)&module)==EFI_SUCCESS && module)found=module;
        }
        boot->FreePool(after);
    }
    if(before)boot->FreePool(before);
    if(!found)return Failure::protocol;
    if(found->abi<charonSynthAbiOldest || found->abi>charonSynthAbi || (found->abi>=3 && !found->SetPersona) || !found->voices || !found->voiceCount || found->voiceCount>maxVoices || !found->Prepare || !found->Begin || !found->Render || !found->Cancel)return Failure::abi;
    *out=found;return Failure::none;
}
// The module's voice with `id`, else its first voice of `language`.
inline uint32_t VoiceIndex(const CharonSynthModule* m,const char* id,const char* language) {
    for(uint32_t i=0;i<m->voiceCount;++i) {size_t k=0;while(id && id[k] && m->voices[i].id[k]==id[k])++k;if(id && id[k]==0 && m->voices[i].id[k]==0)return i;}
    for(uint32_t i=0;i<m->voiceCount;++i)if(language && m->voices[i].language[0]==language[0] && m->voices[i].language[1]==language[1])return i;
    return UINT32_MAX;
}
// r400: loads `m` from the volume it was found on (see ScanAll).
inline Failure LoadAny(efi_boot_services* boot,efi_handle parent,efi_handle ownVolume,efi_file_protocol* ownRoot,const Info& m,CharonSynthModule** out) {
    if(!m.volume || m.volume==ownVolume)return Load(boot,parent,ownVolume,ownRoot,m,out);
    *out=nullptr;efi_guid fsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    efi_simple_file_system_protocol* fs=nullptr;efi_file_protocol* root=nullptr;
    if(boot->HandleProtocol(m.volume,&fsGuid,(void**)&fs)!=EFI_SUCCESS || !fs || fs->OpenVolume(fs,&root)!=EFI_SUCCESS || !root)return Failure::missing;
    const Failure f=Load(boot,parent,m.volume,root,m,out);root->Close(root);return f;
}
}  // namespace hakefi::synths
