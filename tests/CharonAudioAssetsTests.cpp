#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4200) // Existing EFI variable-length file-info ABI.
#endif
#include "../src/efi/CharonAudioAssets.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(false)
using namespace charonaudioefi;
static efi_file_protocol root{},file{};static CharonPool pool{};
static std::map<std::u16string,std::vector<uint8_t>> files;
static std::vector<std::u16string> opened;
static std::vector<uint8_t>* content=nullptr;
static size_t offset=0,chunk=3,claimed=SIZE_MAX,live=0;static int closes=0;
static bool directory=false,oversizedRead=false,badInfo=false,allocationFail=false;
static efi_status closeStatus=0,infoStatus=0,readStatus=0;
static void* Allocate(void*,size_t bytes) {if(allocationFail)return nullptr;auto* data=malloc(bytes);if(data)++live;return data;}
static void Release(void*,void* data) {CHECK(data && live);--live;free(data);}
static efi_status EFIAPI Open(efi_file_protocol*,efi_file_protocol** result,const char16_t* path,uint64_t mode,uint64_t attributes) {
    CHECK(mode==EFI_FILE_MODE_READ && attributes==0);opened.emplace_back(path);
    auto found=files.find(path);if(found==files.end())return EFI_NOT_FOUND;
    content=&found->second;offset=0;*result=&file;return EFI_SUCCESS;
}
static efi_status EFIAPI Info(efi_file_protocol*,efi_guid* guid,size_t* size,void* output) {
    efi_guid expected=EFI_FILE_INFO_GUID;CHECK(!memcmp(guid,&expected,sizeof(expected)) && *size>=sizeof(efi_file_info));
    auto* info=static_cast<efi_file_info*>(output);memset(info,0,sizeof(*info));info->Size=sizeof(*info);
    info->FileSize=claimed==SIZE_MAX?content->size():claimed;info->Attribute=directory?EFI_FILE_DIRECTORY:0;
    *size=badInfo?sizeof(*info)-1:sizeof(*info);return infoStatus;
}
static efi_status EFIAPI Read(efi_file_protocol*,size_t* size,void* output) {
    if(readStatus!=EFI_SUCCESS)return readStatus;
    if(oversizedRead) {++*size;return EFI_SUCCESS;}
    const auto count=(std::min)({*size,chunk,content->size()-offset});
    if(count)memcpy(output,content->data()+offset,count);
    offset+=count;*size=count;return EFI_SUCCESS;
}
static efi_status EFIAPI Close(efi_file_protocol*) {++closes;return closeStatus;}
void Reset() {
    CHECK(!live);CHECK(charon_pool_init(&pool,nullptr,Allocate,Release,32*1024*1024));
    root={};file={};root.Open=Open;file.GetInfo=Info;file.Read=Read;file.Close=Close;
    files.clear();opened.clear();content=nullptr;offset=0;chunk=3;claimed=SIZE_MAX;closes=0;
    directory=oversizedRead=badInfo=allocationFail=false;closeStatus=infoStatus=readStatus=0;
    files[u"test"]={0,1,2,3,4,5,6,7};
}
void Failure(efi_status expected,size_t limit=1024) {
    auto* sentinel=reinterpret_cast<void*>(uintptr_t(0x1234));void* data=sentinel;size_t bytes=777;
    CHECK(ReadAudioFile(&root,&pool,u"test",limit,data,bytes)==expected);
    CHECK(data==sentinel && bytes==777 && closes==1 && live==0 && pool.used==0 && pool.allocations==0);
}
void Pack() {
    const char16_t* names[]={u"welcome.pcm",u"focus.pcm",u"edge.pcm",u"wrap.pcm",u"submenu_open.pcm",u"submenu_close.pcm",u"popUp.pcm",u"popup_close.pcm",u"dialog_open.pcm",u"dialog_close.pcm",u"select.pcm",u"continue.pcm",
        u"start-1.pcm",u"background-1.pcm",u"start-2.pcm",u"background-2.pcm",u"start-3.pcm",u"background-3.pcm",u"start-4.pcm",u"background-4.pcm"};
    for(const auto* name:names)files[std::u16string(u"\\EFI\\HAK\\audio\\")+name]={1,0,2,0,3,0,4,0};
}
int main() {
    Reset();void* data=nullptr;size_t bytes=0;CHECK(ReadAudioFile(&root,&pool,u"test",1024,data,bytes)==0);
    CHECK(bytes==8 && closes==1 && live==1 && !memcmp(data,files[u"test"].data(),8));charon_pool_free(&pool,data);CHECK(!live && !pool.used);
    Reset();claimed=12;Failure(EFI_DEVICE_ERROR); // File shrank after GetInfo.
    Reset();claimed=4;Failure(EFI_MEDIA_CHANGED); // File grew after GetInfo.
    Reset();Failure(EFI_BAD_BUFFER_SIZE,4);
    Reset();directory=true;Failure(EFI_BAD_BUFFER_SIZE);
    Reset();closeStatus=EFI_ACCESS_DENIED;Failure(EFI_ACCESS_DENIED);
    Reset();readStatus=EFI_DEVICE_ERROR;closeStatus=EFI_ACCESS_DENIED;Failure(EFI_DEVICE_ERROR);
    Reset();files[u"test"].clear();Failure(EFI_BAD_BUFFER_SIZE);
    Reset();badInfo=true;Failure(EFI_BAD_BUFFER_SIZE);
    Reset();infoStatus=EFI_BUFFER_TOO_SMALL;Failure(EFI_BUFFER_TOO_SMALL);
    Reset();oversizedRead=true;Failure(EFI_DEVICE_ERROR);
    Reset();allocationFail=true;Failure(EFI_OUT_OF_RESOURCES);
    Reset();Pack();Assets assets;CHECK(assets.Load(&root,&pool)==0 && assets.Ready() && opened.size()==20 && live==20);
    CHECK(assets.Load(&root,&pool)==EFI_ALREADY_STARTED);
    CHECK(!assets.Cue(hakefi::MenuCue::none).samples && !assets.Cue(hakefi::MenuCue(999)).samples);
    CHECK(!assets.Start(0).samples && !assets.Background(0).samples && !assets.Start(5).samples && !assets.Background(5).samples);
    for(unsigned i=1;i<=12;++i)CHECK(assets.Cue(hakefi::MenuCue(i)).frames==2);
    for(unsigned i=1;i<=4;++i)CHECK(assets.Start(i).frames==2 && assets.Background(i).frames==2);
    CHECK(opened[6]==u"\\EFI\\HAK\\audio\\popUp.pcm");
    for(const auto& path:opened)CHECK(files.count(path)==1);
    assets.Clear();CHECK(!assets.Ready() && !live && pool.used==0);assets.Clear();
    for(const auto* missing:{u"welcome.pcm",u"popUp.pcm",u"background-4.pcm"}) {
        Reset();Pack();files.erase(std::u16string(u"\\EFI\\HAK\\audio\\")+missing);
        CHECK(assets.Load(&root,&pool)==EFI_NOT_FOUND && !assets.Ready() && !live && !pool.used);
        for(unsigned i=1;i<=12;++i)CHECK(!assets.Cue(hakefi::MenuCue(i)).samples);
        for(unsigned i=1;i<=4;++i)CHECK(!assets.Start(i).samples && !assets.Background(i).samples);
    }
    Reset();Pack();files[u"\\EFI\\HAK\\audio\\dialog_close.pcm"].push_back(0);
    CHECK(assets.Load(&root,&pool)==EFI_BAD_BUFFER_SIZE && !assets.Ready() && !live && !pool.used);
    Reset();Pack();pool.limit=1;CHECK(assets.Load(&root,&pool)==EFI_OUT_OF_RESOURCES && !assets.Ready() && !live);
    puts("PASS actual ESP audio file exact/chunk/EOF/close validation, atomic20-asset pack, case-sensitive names and pool cleanup");
}
