#pragma once
// r387: start any EFI program from a file (System tools -> "Start an EFI
// program from a file"). Level 0 lists every file system the firmware has
// mounted (label, kind of device, size); then folders and *.efi files of the
// chosen volume. Enter opens a folder or starts a program, Space reads its
// details, Left/Escape goes one level up. A program is started through an
// in-memory load option (volume device path + file path); nothing is written
// to NVRAM or to the volume. GPL-2.0-or-later.
#include <stddef.h>
#include <stdint.h>
#include "CharonFirmwareBrowser.h"
#include "CharonMedia.h"
namespace hakefi {
namespace files {
constexpr size_t maxEntries=systools::listRows,nameUnits=128,pathUnits=1024,maxVolumes=64;
struct Entry {char16_t name[nameUnits];bool directory;uint64_t size;efi_time modified;};
struct Browser {
    // Plain data (no initializers: a guarded static would need the C++ runtime);
    // LoadVolumes sets every field before use.
    efi_handle volumes[maxVolumes];size_t volumeCount;
    size_t volume;                 // index into volumes; SIZE_MAX = the volume list
    char16_t path[pathUnits];      // current folder, "\\" at the root
    Entry entries[maxEntries];size_t count;bool truncated;
};
inline Browser& Shared() {static Browser browser;return browser;}
inline size_t Length(const char16_t* s) {size_t n=0;while(s[n])++n;return n;}
inline char16_t Lower(char16_t c) {return c>=u'A' && c<=u'Z'?char16_t(c+32):c;}
inline bool EndsEfi(const char16_t* name) {
    const size_t n=Length(name);
    return n>4 && name[n-4]==u'.' && Lower(name[n-3])==u'e' && Lower(name[n-2])==u'f' && Lower(name[n-1])==u'i';
}
inline int Compare(const Entry& a,const Entry& b) {
    if(a.directory!=b.directory)return a.directory?-1:1;
    for(size_t i=0;;++i) {
        const char16_t x=Lower(a.name[i]),y=Lower(b.name[i]);
        if(x!=y || !x)return int(x)-int(y);
    }
}
// Kind of device a volume lives on, from its device path.
inline void PutVolumeKind(char16_t* out,size_t capacity,const uint8_t* path,bool pl) {
    const char16_t* kind=pl?u"urządzenie UEFI":u"UEFI device";uint32_t partition=0;bool memory=false;
    for(size_t at=0;at<4096;) {
        const uint8_t type=path[at],sub=path[at+1];const size_t n=systools::Le16(path+at+2);
        if(type==0x7f || n<4)break;
        if(type==3 && (sub==5 || sub==15 || sub==16))kind=u"USB";
        else if(type==3 && (sub==1 || sub==2 || sub==0x12 || sub==0x17 || sub==0x1d || sub==0x19) && kind[0]!=u'U')kind=pl?u"dysk":u"disk";
        else if(type==4 && sub==2)kind=u"CD/DVD";
        else if(type==4 && sub==1 && n>=8)partition=systools::Le32(path+at+4);
        else if(type==1 && sub==3)memory=true;
        else if(type==4 && sub==6)kind=pl?u"firmware":u"firmware";
        at+=n;
    }
    if(memory && kind[0]==u'u')kind=pl?u"dysk w pamięci":u"RAM disk";
    systools::Put(out,capacity,kind);
    if(partition) {systools::Put(out,capacity,pl?u", partycja ":u", partition ");systools::PutNumber(out,capacity,partition);}
}
// Volume list: every SimpleFileSystem handle with a device path.
inline efi_status LoadVolumes(efi_boot_services* boot,efi_handle selfDevice,bool pl,systools::InfoList& list,Browser& browser) {
    list.kind=systools::ListKind::files;list.count=0;list.truncated=false;
    browser.volume=SIZE_MAX;browser.volumeCount=0;browser.count=0;browser.path[0]=0;
    efi_handle* handles=nullptr;size_t n=0;
    auto status=boot->LocateHandleBuffer(ByProtocol,&media::fsGuid,nullptr,&n,&handles);
    if(status==EFI_NOT_FOUND)return EFI_SUCCESS;
    if(status!=EFI_SUCCESS || !handles)return status;
    for(size_t i=0;i<n;++i) {
        if(browser.volumeCount>=maxVolumes) {list.truncated=true;break;}
        uint8_t* own=nullptr;efi_simple_file_system_protocol* fs=nullptr;efi_file_protocol* root=nullptr;
        if(boot->HandleProtocol(handles[i],&media::pathGuid,(void**)&own)!=EFI_SUCCESS || !own)continue;
        if(media::PathLength(own)==SIZE_MAX)continue;
        if(boot->HandleProtocol(handles[i],&media::fsGuid,(void**)&fs)!=EFI_SUCCESS || !fs)continue;
        auto& row=list.rows[list.count];row[0]=0;
        char16_t label[64]={};uint64_t volumeSize=0;bool readOnly=false,opened=false;
        if(fs->OpenVolume(fs,&root)==EFI_SUCCESS && root) {
            opened=true;
            alignas(8) uint8_t info[512]={};size_t infoSize=sizeof(info);
            if(root->GetInfo(root,&media::fsInfoGuid,&infoSize,info)==EFI_SUCCESS && infoSize>36) {
                readOnly=info[8]!=0;volumeSize=systools::Le64(info+16);
                for(size_t k=0;k<63 && 36+k*2+1<infoSize;++k) {const char16_t c=char16_t(systools::Le16(info+36+k*2));if(!c)break;label[k]=c<32?u' ':c;}
            }
            root->Close(root);
        }
        if(!label[0])media::IsoVolumeName(boot,own,media::PathLength(own),label,64);
        systools::Put(row,systools::listRowText,label[0]?label:(pl?u"bez etykiety":u"no label"));
        systools::Put(row,systools::listRowText,u", ");PutVolumeKind(row,systools::listRowText,own,pl);
        if(volumeSize) {systools::Put(row,systools::listRowText,u", ");systools::PutSize(row,systools::listRowText,volumeSize,pl);}
        if(readOnly)systools::Put(row,systools::listRowText,pl?u", tylko do odczytu":u", read only");
        if(handles[i]==selfDevice)systools::Put(row,systools::listRowText,pl?u", dysk Charona":u", Charon's disk");
        if(!opened)systools::Put(row,systools::listRowText,pl?u", nie da się otworzyć":u", cannot be opened");
        systools::Put(row,systools::listRowText,pl?u", wolumin":u", volume");
        list.handles[list.count]=handles[i];browser.volumes[browser.volumeCount++]=handles[i];++list.count;
    }
    boot->FreePool(handles);
    return EFI_SUCCESS;
}
// One folder of the chosen volume, read again from the disk each time.
inline efi_status LoadFolder(efi_boot_services* boot,bool pl,systools::InfoList& list,Browser& browser) {
    list.kind=systools::ListKind::files;list.count=0;list.truncated=false;browser.count=0;browser.truncated=false;
    if(browser.volume>=browser.volumeCount)return EFI_INVALID_PARAMETER;
    efi_simple_file_system_protocol* fs=nullptr;efi_file_protocol* root=nullptr;efi_file_protocol* folder=nullptr;
    auto status=boot->HandleProtocol(browser.volumes[browser.volume],&media::fsGuid,(void**)&fs);
    if(status!=EFI_SUCCESS || !fs)return status==EFI_SUCCESS?EFI_NOT_FOUND:status;
    status=fs->OpenVolume(fs,&root);if(status!=EFI_SUCCESS || !root)return status==EFI_SUCCESS?EFI_DEVICE_ERROR:status;
    if(browser.path[0]==u'\\' && !browser.path[1]) {folder=root;root=nullptr;}
    else {status=root->Open(root,&folder,browser.path,EFI_FILE_MODE_READ,0);root->Close(root);root=nullptr;
        if(status!=EFI_SUCCESS || !folder)return status==EFI_SUCCESS?EFI_NOT_FOUND:status;}
    alignas(8) static uint8_t buffer[4096];
    for(unsigned guard=0;guard<100000;++guard) {
        size_t size=sizeof(buffer);
        status=folder->Read(folder,&size,buffer);
        if(status==EFI_BUFFER_TOO_SMALL) {browser.truncated=true;continue;}   // name longer than 2 KB: skip it
        if(status!=EFI_SUCCESS || !size)break;
        const auto* info=reinterpret_cast<const efi_file_info*>(buffer);
        const char16_t* name=info->FileName;
        if(name[0]==u'.' && (!name[1] || (name[1]==u'.' && !name[2])))continue;
        const bool directory=(info->Attribute&EFI_FILE_DIRECTORY)!=0;
        if(!directory && !EndsEfi(name))continue;
        if(Length(name)>=nameUnits) {browser.truncated=true;continue;}
        if(browser.count>=maxEntries) {browser.truncated=true;break;}
        auto& entry=browser.entries[browser.count++];
        size_t k=0;for(;name[k] && k+1<nameUnits;++k)entry.name[k]=name[k];entry.name[k]=0;
        entry.directory=directory;entry.size=info->FileSize;entry.modified=info->ModificationTime;
    }
    folder->Close(folder);
    // Insertion sort: folders first, then names without regard to case.
    for(size_t i=1;i<browser.count;++i) {
        static Entry moving;moving=browser.entries[i];size_t j=i;
        while(j && Compare(moving,browser.entries[j-1])<0) {browser.entries[j]=browser.entries[j-1];--j;}
        browser.entries[j]=moving;
    }
    for(size_t i=0;i<browser.count;++i) {
        auto& row=list.rows[i];row[0]=0;const auto& entry=browser.entries[i];
        systools::Put(row,systools::listRowText,entry.name);
        if(entry.directory)systools::Put(row,systools::listRowText,pl?u", folder":u", folder");
        else {systools::Put(row,systools::listRowText,pl?u", program EFI, ":u", EFI program, ");systools::PutSize(row,systools::listRowText,entry.size,pl);}
    }
    list.count=browser.count;list.truncated=browser.truncated;
    return status==EFI_SUCCESS || status==EFI_BUFFER_TOO_SMALL?EFI_SUCCESS:(browser.count?EFI_SUCCESS:status);
}
// Enter a folder (true) or leave one; the name left is returned so the
// caller can keep the focus on it.
inline bool Descend(Browser& browser,const char16_t* name) {
    size_t at=Length(browser.path);const size_t n=Length(name);
    if(at+n+2>=pathUnits)return false;
    if(at>1)browser.path[at++]=u'\\';
    for(size_t i=0;i<n;++i)browser.path[at++]=name[i];
    browser.path[at]=0;return true;
}
inline void Ascend(Browser& browser,char16_t* left,size_t capacity) {
    left[0]=0;size_t at=Length(browser.path);
    if(at<=1) {browser.volume=SIZE_MAX;browser.path[0]=0;return;}
    size_t cut=at;while(cut && browser.path[cut-1]!=u'\\')--cut;
    size_t k=0;for(size_t i=cut;i<at && k+1<capacity;++i)left[k++]=browser.path[i];left[k]=0;
    browser.path[cut>1?cut-1:1]=0;
}
// Load option for one program: ACTIVE, the file name as its description,
// the volume's device path and one file path node. FreePool the result.
inline uint8_t* BuildFileOption(efi_boot_services* boot,const Browser& browser,const char16_t* name,size_t& size) {
    size=0;if(browser.volume>=browser.volumeCount)return nullptr;
    uint8_t* own=nullptr;
    if(boot->HandleProtocol(browser.volumes[browser.volume],&media::pathGuid,(void**)&own)!=EFI_SUCCESS || !own)return nullptr;
    const size_t device=media::PathLength(own);if(device==SIZE_MAX || device>2048)return nullptr;
    static char16_t full[pathUnits+nameUnits+2];size_t at=0;
    for(size_t i=0;browser.path[i];++i)full[at++]=browser.path[i];
    if(at>1)full[at++]=u'\\';
    for(size_t i=0;name[i];++i)full[at++]=name[i];
    full[at]=0;
    const size_t node=4+(at+1)*2;
    static uint8_t path[2048+4+(pathUnits+nameUnits+2)*2];
    for(size_t i=0;i<device;++i)path[i]=own[i];
    uint8_t* file=path+device;file[0]=4;file[1]=4;file[2]=uint8_t(node);file[3]=uint8_t(node>>8);
    for(size_t i=0;i<=at;++i) {file[4+i*2]=uint8_t(full[i]);file[5+i*2]=uint8_t(full[i]>>8);}
    return media::BuildOption(boot,name,path,device+node,size);
}
inline void PutDate(char16_t* out,size_t capacity,const efi_time& t) {
    systools::PutNumber(out,capacity,t.Year,4);systools::Put(out,capacity,u"-");systools::PutNumber(out,capacity,t.Month,2);
    systools::Put(out,capacity,u"-");systools::PutNumber(out,capacity,t.Day,2);systools::Put(out,capacity,u" ");
    systools::PutNumber(out,capacity,t.Hour,2);systools::Put(out,capacity,u":");systools::PutNumber(out,capacity,t.Minute,2);
}
// Space on a row: what it is, where it is, and (programs) whether it is an
// x64 UEFI image at all, read from its PE header.
inline void Detail(efi_boot_services* boot,const Browser& browser,const systools::InfoList& list,size_t index,bool pl,char16_t* out,size_t capacity) {
    out[0]=0;
    if(browser.volume==SIZE_MAX) {if(index<list.count)systools::Put(out,capacity,list.rows[index]);return;}
    if(index>=browser.count)return;
    const auto& entry=browser.entries[index];
    systools::Put(out,capacity,entry.name);systools::Put(out,capacity,entry.directory?(pl?u". Folder. ":u". Folder. "):(pl?u". Program EFI, ":u". EFI program, "));
    if(!entry.directory) {systools::PutSize(out,capacity,entry.size,pl);systools::Put(out,capacity,u". ");}
    systools::Put(out,capacity,pl?u"Ścieżka: ":u"Path: ");systools::Put(out,capacity,browser.path);
    if(Length(browser.path)>1)systools::Put(out,capacity,u"\\");
    systools::Put(out,capacity,entry.name);systools::Put(out,capacity,u". ");
    if(entry.modified.Year) {systools::Put(out,capacity,pl?u"Zmieniono: ":u"Modified: ");PutDate(out,capacity,entry.modified);systools::Put(out,capacity,u". ");}
    if(entry.directory)return;
    // PE machine and subsystem.
    efi_simple_file_system_protocol* fs=nullptr;efi_file_protocol* root=nullptr;efi_file_protocol* file=nullptr;
    if(boot->HandleProtocol(browser.volumes[browser.volume],&media::fsGuid,(void**)&fs)!=EFI_SUCCESS || !fs || fs->OpenVolume(fs,&root)!=EFI_SUCCESS || !root)return;
    static char16_t full[pathUnits+nameUnits+2];size_t at=0;
    for(size_t i=0;browser.path[i];++i)full[at++]=browser.path[i];
    if(at>1)full[at++]=u'\\';
    for(size_t i=0;entry.name[i];++i)full[at++]=entry.name[i];
    full[at]=0;
    const auto opened=root->Open(root,&file,full,EFI_FILE_MODE_READ,0);root->Close(root);
    if(opened!=EFI_SUCCESS || !file)return;
    alignas(8) static uint8_t head[1024];size_t size=sizeof(head);
    const auto read=file->Read(file,&size,head);file->Close(file);
    if(read!=EFI_SUCCESS)return;
    const uint32_t pe=size>=64?systools::Le32(head+0x3c):0;
    if(size<64 || head[0]!='M' || head[1]!='Z' || pe+0x5e>size || head[pe]!='P' || head[pe+1]!='E') {
        systools::Put(out,capacity,pl?u"To nie jest obraz PE; firmware go nie uruchomi.":u"Not a PE image; firmware will not start it.");return;}
    const uint16_t machine=systools::Le16(head+pe+4),subsystem=systools::Le16(head+pe+24+68);
    const char16_t* arch=machine==0x8664?u"x64":machine==0x14c?u"IA32":machine==0xaa64?u"AArch64":machine==0x5064?u"RISC-V 64":nullptr;
    systools::Put(out,capacity,pl?u"Architektura: ":u"Architecture: ");systools::Put(out,capacity,arch?arch:(pl?u"inna":u"other"));
    systools::Put(out,capacity,u", ");
    systools::Put(out,capacity,subsystem==10?(pl?u"aplikacja UEFI":u"UEFI application"):subsystem==11?(pl?u"sterownik usług rozruchu":u"boot service driver")
        :subsystem==12?(pl?u"sterownik czasu działania":u"runtime driver"):(pl?u"nie program UEFI":u"not a UEFI program"));
    systools::Put(out,capacity,u".");
    if(machine!=0x8664)systools::Put(out,capacity,pl?u" Ten komputer go nie uruchomi.":u" This computer will not start it.");
    else if(subsystem==11 || subsystem==12)systools::Put(out,capacity,pl?u" Uruchomiony zostanie w pamięci jak sterownik i od razu odda sterowanie.":u" It will be loaded as a driver and return at once.");
}
}  // namespace files
}  // namespace hakefi
