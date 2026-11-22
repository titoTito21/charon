// Host harness for the OS companion programs (tools/charon-os). GPL-2.0-or-later.
//
// Runs Charon's OWN settings code on the host so that the PowerShell and Python
// programs can be checked against it byte for byte:
//   - CharSettings.h is included unchanged (EncodeSettings, DecodeSettings,
//     ChooseSettings, SettingsCRC, the CharSettings defaults);
//   - SettingsSlot / ReadSettingsSlot / LoadCharSettings / SaveCharSettings are the
//     text of src/efi/BootMenu.h, cut out by run-tests.py into
//     charon-menu-settings.inc (BootMenu.h as a whole needs firmware);
//   - Write() is the text of src/efi/HAKBootEFI.cpp (charon-write.inc);
//   - synths::KeyOf is the text of src/efi/CharonSynthHost.h (charon-keyof.inc);
//   - ContinueTarget.h / BootOption.h are included unchanged (ParseContinueTarget, Parse);
//   - efi_file_protocol is the tree's <efi/protocol/file.h>; here its functions
//     work on an ordinary folder that stands in for the EFI system partition.
//
// Commands (every output line is "name=value"):
//   load DIR                       LoadCharSettings
//   save DIR [member=value ...]    LoadCharSettings, change members, SaveCharSettings
//   encode sequence=N [member=value ...]   EncodeSettings, record as hex
//   decode HEX                     DecodeSettings of one record
//   keyof NAME                     synths::KeyOf of a manifest file name
//   continue FILE                  ParseContinueTarget of a continue.bin
#include <efi/protocol/file.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "CharSettings.h"
#include "ContinueTarget.h"

// ---- an ordinary folder behind efi_file_protocol ---------------------------
struct HostFile {
    efi_file_protocol protocol;   // first member: the protocol pointer is the object
    std::string path;
    FILE* stream=nullptr;
};
static HostFile* Host(efi_file_protocol* self) {return reinterpret_cast<HostFile*>(self);}
static efi_status EFIAPI HostOpen(efi_file_protocol* self,efi_file_protocol** result,const char16_t* name,uint64_t mode,uint64_t);
static efi_status EFIAPI HostClose(efi_file_protocol* self) {
    HostFile* file=Host(self);const bool ok=!file->stream || fclose(file->stream)==0;delete file;
    return ok?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
static efi_status EFIAPI HostDelete(efi_file_protocol* self) {
    HostFile* file=Host(self);if(file->stream)fclose(file->stream);
    const bool ok=remove(file->path.c_str())==0;delete file;
    return ok?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
static efi_status EFIAPI HostRead(efi_file_protocol* self,size_t* length,void* buffer) {
    HostFile* file=Host(self);if(!file->stream)return EFI_DEVICE_ERROR;
    *length=fread(buffer,1,*length,file->stream);
    return ferror(file->stream)?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static efi_status EFIAPI HostWrite(efi_file_protocol* self,size_t* length,void* buffer) {
    HostFile* file=Host(self);if(!file->stream)return EFI_DEVICE_ERROR;
    *length=fwrite(buffer,1,*length,file->stream);
    return ferror(file->stream)?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static efi_status EFIAPI HostFlush(efi_file_protocol* self) {
    HostFile* file=Host(self);return file->stream && fflush(file->stream)==0?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
static HostFile* NewHost(const std::string& path,FILE* stream) {
    HostFile* file=new HostFile;memset(&file->protocol,0,sizeof(file->protocol));
    file->protocol.Revision=EFI_FILE_PROTOCOL_REVISION;
    file->protocol.Open=HostOpen;file->protocol.Close=HostClose;file->protocol.Delete=HostDelete;
    file->protocol.Read=HostRead;file->protocol.Write=HostWrite;file->protocol.Flush=HostFlush;
    file->path=path;file->stream=stream;return file;
}
static efi_status EFIAPI HostOpen(efi_file_protocol* self,efi_file_protocol** result,const char16_t* name,uint64_t mode,uint64_t) {
    std::string path=Host(self)->path;
    for(;*name;++name) {
        if(*name>127)return EFI_INVALID_PARAMETER;
        path+=*name==u'\\'?'/':char(*name);
    }
    FILE* stream=fopen(path.c_str(),(mode&EFI_FILE_MODE_WRITE)?"r+b":"rb");
    if(!stream && (mode&EFI_FILE_MODE_CREATE))stream=fopen(path.c_str(),"w+b");
    if(!stream)return EFI_NOT_FOUND;
    *result=&NewHost(path,stream)->protocol;return EFI_SUCCESS;
}

// ---- Charon's own functions --------------------------------------------------
namespace hakefi {
#include "charon-write.inc"
#include "charon-menu-settings.inc"
namespace synths {
#include "charon-keyof.inc"
}
}
using namespace hakefi;

struct Member {const char* name;int (*get)(const CharSettings&);void (*set)(CharSettings&,int);};
#define BYTE_MEMBER(m) {#m,[](const CharSettings& s) {return int(s.m);},[](CharSettings& s,int v) {s.m=uint8_t(v);}}
#define BOOL_MEMBER(m) {#m,[](const CharSettings& s) {return int(s.m?1:0);},[](CharSettings& s,int v) {s.m=v!=0;}}
static const Member members[]={
    BYTE_MEMBER(language),BOOL_MEMBER(wrap),BOOL_MEMBER(sounds),BOOL_MEMBER(explicitMessages),BYTE_MEMBER(startMelody),
    BYTE_MEMBER(backgroundMelody),BYTE_MEMBER(rate),BYTE_MEMBER(speechVolume),BYTE_MEMBER(effectsVolume),BYTE_MEMBER(verbosity),
    BYTE_MEMBER(progress),BOOL_MEMBER(rememberLast),BYTE_MEMBER(autoSeconds),BYTE_MEMBER(welcome),BYTE_MEMBER(pitch),
    BYTE_MEMBER(readerRate),BYTE_MEMBER(readerPitch),BYTE_MEMBER(readerVolume),BYTE_MEMBER(detail),BYTE_MEMBER(timers),
    BYTE_MEMBER(messageRate),BYTE_MEMBER(messagePitch),BYTE_MEMBER(punctuation),BYTE_MEMBER(capitals),BYTE_MEMBER(screenLanguage),
    BYTE_MEMBER(readerFlags),BYTE_MEMBER(formatFlags),BYTE_MEMBER(readerExtra),BYTE_MEMBER(synthKey),BYTE_MEMBER(synthVoice)};

static void Print(const CharSettings& value) {
    printf("sequence=%llu\n",(unsigned long long)value.sequence);
    for(const auto& member:members)printf("%s=%d\n",member.name,member.get(value));
}
static bool Assign(CharSettings& value,const char* argument) {
    const char* equals=strchr(argument,'=');if(!equals)return false;
    const std::string name(argument,equals-argument);
    if(name=="sequence") {value.sequence=strtoull(equals+1,nullptr,10);return true;}
    for(const auto& member:members)if(name==member.name) {member.set(value,atoi(equals+1));return true;}
    return false;
}
static const char* StatusName(efi_status status) {
    return status==EFI_SUCCESS?"ok":status==EFI_NOT_FOUND?"notfound":status==EFI_INVALID_PARAMETER?"invalid":status==EFI_DEVICE_ERROR?"deviceerror":"error";
}
static bool Hex(const char* text,std::vector<uint8_t>& bytes) {
    auto digit=[](char c) {return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;};
    for(;text[0] && text[1];text+=2) {const int a=digit(text[0]),b=digit(text[1]);if(a<0 || b<0)return false;bytes.push_back(uint8_t(a<<4|b));}
    return !text[0];
}
static char SlotFormat(efi_file_protocol* root,unsigned slot,uint64_t& sequence) {
    uint8_t bytes[charSettingsBytes+1];size_t length=0;sequence=0;
    if(ReadSettingsSlot(root,slot,bytes,length)!=EFI_SUCCESS)return 0;
    for(unsigned i=0;i<8;++i)sequence|=uint64_t(bytes[8+i])<<(i*8);
    return char(bytes[7]);
}
static void PrintSlots(efi_file_protocol* root,const CharSettings* chosen) {
    char format=0;
    for(unsigned slot=0;slot<2;++slot) {
        uint8_t bytes[charSettingsBytes+1];size_t length=0;
        const auto status=ReadSettingsSlot(root,slot,bytes,length);
        printf("slot%c=%s\n",slot?'b':'a',StatusName(status));
        uint64_t sequence;const char found=SlotFormat(root,slot,sequence);
        if(found && chosen && sequence==chosen->sequence)format=found;
    }
    if(format)printf("format=%c\n",format);else printf("format=none\n");
}

int main(int argc,char** argv) {
    if(argc<2) {fprintf(stderr,"usage: load DIR | save DIR [member=value ...] | encode sequence=N [...] | decode HEX | keyof NAME | continue FILE\n");return 2;}
    const std::string command=argv[1];
    if(command=="load" || command=="save") {
        if(argc<3)return 2;
        HostFile* root=NewHost(std::string(argv[2])+"/",nullptr);
        CharSettings settings;
        auto status=LoadCharSettings(&root->protocol,settings);
        if(command=="load") {
            printf("status=%s\n",StatusName(status));
            PrintSlots(&root->protocol,status==EFI_SUCCESS?&settings:nullptr);
            if(status==EFI_INVALID_PARAMETER) {delete root;return 4;}
            if(status==EFI_NOT_FOUND)settings=CharSettings{};
            Print(settings);delete root;return 0;
        }
        if(status==EFI_INVALID_PARAMETER) {printf("status=invalid\n");delete root;return 4;}
        if(status==EFI_NOT_FOUND)settings=CharSettings{};
        for(int i=3;i<argc;++i)if(!Assign(settings,argv[i])) {fprintf(stderr,"unknown member: %s\n",argv[i]);return 2;}
        status=SaveCharSettings(&root->protocol,settings);
        printf("status=%s\n",StatusName(status));
        if(status!=EFI_SUCCESS) {delete root;return 5;}
        PrintSlots(&root->protocol,&settings);Print(settings);delete root;return 0;
    }
    if(command=="encode") {
        CharSettings settings;
        for(int i=2;i<argc;++i)if(!Assign(settings,argv[i])) {fprintf(stderr,"unknown member: %s\n",argv[i]);return 2;}
        uint8_t bytes[charSettingsBytes];
        if(!EncodeSettings(settings,bytes,sizeof(bytes))) {printf("encode=rejected\n");return 5;}
        printf("record=");for(uint8_t byte:bytes)printf("%02x",byte);printf("\n");return 0;
    }
    if(command=="decode") {
        if(argc<3)return 2;
        std::vector<uint8_t> bytes;if(!Hex(argv[2],bytes)) {fprintf(stderr,"bad hex\n");return 2;}
        CharSettings settings;
        if(!DecodeSettings(bytes.data(),bytes.size(),settings)) {printf("decode=rejected\n");return 4;}
        printf("decode=ok\nformat=%c\n",char(bytes[7]));Print(settings);return 0;
    }
    if(command=="keyof") {
        if(argc<3)return 2;
        std::u16string name;for(const unsigned char* p=(const unsigned char*)argv[2];*p;++p)name+=char16_t(*p);   // ASCII file names
        printf("key=%u\n",unsigned(synths::KeyOf(name.c_str())));return 0;
    }
    if(command=="continue") {
        if(argc<3)return 2;
        FILE* file=fopen(argv[2],"rb");if(!file) {printf("continue=notfound\n");return 3;}
        static uint8_t bytes[continueCapacity];const size_t length=fread(bytes,1,sizeof(bytes),file);fclose(file);
        uint16_t id=0;size_t size=0;
        if(!ParseContinueTarget(bytes,length,id,size)) {printf("continue=invalid\n");return 4;}
        printf("continue=ok\nid=%04X\nsize=%u\n",unsigned(id),unsigned(size));
        BootOption parsed;Parse(bytes+14,size,parsed);
        printf("descriptionBytes=%u\npathBytes=%u\n",unsigned(parsed.descriptionBytes),unsigned(parsed.pathBytes));return 0;
    }
    fprintf(stderr,"unknown command\n");return 2;
}
