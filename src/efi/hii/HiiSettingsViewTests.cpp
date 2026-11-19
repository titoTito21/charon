// Reuse genuine parser fixture construction without running its separate suite.
#define main HiiModelFixtureTestMain
#include "HiiModelTests.cpp"
#undef main
#undef CHECK
#include "HiiSettingsView.h"
#include <string>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL view %d: %s\n",__LINE__,#x);exit(1);}}while(false)
static efi_boot_services boot{};static DatabaseProtocol dbApi{};static ConfigAccessProtocol access{};static ConfigRoutingProtocol routing{};
static Bytes exported;static uint8_t storage[64];static char hii,driver;static uint8_t devicePath[]={0x7f,0xff,4,0};
static PackageNotify callback=nullptr;static size_t allocations=0,extracts=0,routes=0,notifications=0;
static efi_status EFIAPI Alloc(efi_memory_type,size_t n,void** out){*out=malloc(n);if(!*out)return EFI_OUT_OF_RESOURCES;++allocations;return 0;}
static efi_status EFIAPI Free(void* p){CHECK(allocations);--allocations;free(p);return 0;}
static efi_status EFIAPI Locate(efi_guid* guid,void*,void** out){*out=guid->data1==0xef9fc172?static_cast<void*>(&dbApi):static_cast<void*>(&routing);return 0;}
static efi_status EFIAPI Handle(efi_handle value,efi_guid* guid,void** out){CHECK(value==&driver);*out=guid->data1==0x330d4706?static_cast<void*>(&access):static_cast<void*>(devicePath);return 0;}
static efi_status EFIAPI List(const DatabaseProtocol*,uint8_t,const efi_guid*,size_t* size,void** out){*size=sizeof(void*);if(!out)return EFI_BUFFER_TOO_SMALL;*out=&hii;return 0;}
static efi_status EFIAPI Export(const DatabaseProtocol*,void* value,size_t* size,void* out){CHECK(value==&hii);*size=exported.size();if(!out)return EFI_OUT_OF_RESOURCES;memcpy(out,exported.data(),exported.size());return 0;}
static efi_status EFIAPI Register(const DatabaseProtocol*,uint8_t,const efi_guid*,PackageNotify cb,size_t,efi_handle* out){callback=cb;*out=reinterpret_cast<void*>(++notifications);return 0;}
static efi_status EFIAPI Unregister(const DatabaseProtocol*,efi_handle){return 0;}
static efi_status EFIAPI Owner(const DatabaseProtocol*,void*,efi_handle* out){*out=&driver;return 0;}
static efi_status EFIAPI Extract(const ConfigAccessProtocol*,const char16_t* request,char16_t** progress,char16_t** result){
    ++extracts;const std::u16string text=request;
    auto number=[&](const char16_t* field){const auto start=text.find(field);CHECK(start!=text.npos);size_t n=0;for(size_t i=start+7;i<text.size() && text[i]!=u'&';++i){auto c=text[i];n=n*16+(c<=u'9'?c-u'0':c-u'a'+10);}return n;};
    const auto offset=number(u"OFFSET="),width=number(u"&WIDTH=");CHECK(offset+width<=sizeof(storage));
    std::u16string response=text+u"&VALUE=";for(size_t i=width;i;--i){auto value=storage[offset+i-1];response+=u"0123456789abcdef"[value>>4];response+=u"0123456789abcdef"[value&15];}
    void* memory=nullptr;CHECK(Alloc(EfiLoaderData,(response.size()+1)*2,&memory)==0);*result=static_cast<char16_t*>(memory);
    memcpy(*result,response.c_str(),(response.size()+1)*2);*progress=const_cast<char16_t*>(request+text.size());return 0;
}
static efi_status EFIAPI Route(const ConfigRoutingProtocol*,const char16_t*,char16_t**){++routes;return EFI_ACCESS_DENIED;}
int main(){
    boot.LocateProtocol=Locate;boot.HandleProtocol=Handle;boot.AllocatePool=Alloc;boot.FreePool=Free;
    dbApi.list=List;dbApi.exportList=Export;dbApi.driver=Owner;dbApi.registerNotify=Register;dbApi.unregisterNotify=Unregister;
    access.extract=Extract;routing.route=Route;exported=fixture(false);storage[0]=1;storage[1]=1;storage[4]=10;storage[8]='A';
    Database database;CHECK(database.Open(&boot)==0 && database.Refresh()==0);const char* langs[]={"pl-PL","en-US"};CHECK(database.Read(0,langs,2)==0);
    Config config;SettingsView view;CHECK(view.Bind(&boot,database,config,database.Current(),false));
    CHECK(!strcmp(view.ResolvedLanguage(),"en-US") && view.Count()==1);char16_t label[1024];CHECK(view.RowLabel(0,label,1024) && std::u16string(label)==u"Settings");
    CHECK(view.Key(ViewKey::Enter)==ViewResult::Opened && view.Page()==ViewPage::Forms);
    CHECK(view.Key(ViewKey::Right)==ViewResult::Opened && view.Page()==ViewPage::Questions && view.Count()==7);
    CHECK(view.FocusAnnouncement(label,1024) && std::u16string(label).find(u"checked")!=std::u16string::npos);
    CHECK(view.Key(ViewKey::Right)==ViewResult::None);CHECK(view.Key(ViewKey::Space)==ViewResult::Opened && view.Page()==ViewPage::Confirm && view.Selected()==0);
    CHECK(!view.ConfirmedProposal() && routes==0 && storage[0]==1);
    CHECK(view.FocusAnnouncement(label,1024) && std::u16string(label).find(u"not checked")!=std::u16string::npos);
    CHECK(view.Key(ViewKey::Enter)==ViewResult::Closed && view.Page()==ViewPage::Questions);
    CHECK(view.Key(ViewKey::Space)==ViewResult::Opened);CHECK(view.Key(ViewKey::Down)==ViewResult::Focus);
    CHECK(view.Key(ViewKey::Enter)==ViewResult::ProposalReady && view.ConfirmedProposal()->value.number==0 && routes==0);
    CHECK(view.Key(ViewKey::Back)==ViewResult::Closed);CHECK(view.Key(ViewKey::Down)==ViewResult::Focus);
    CHECK(view.Key(ViewKey::Enter)==ViewResult::Opened && view.Page()==ViewPage::EnumChoices && view.Count()==2);
    CHECK(view.Key(ViewKey::Down)==ViewResult::Focus);CHECK(view.Key(ViewKey::Enter)==ViewResult::Opened);CHECK(view.Key(ViewKey::Down)==ViewResult::Focus);
    CHECK(view.Key(ViewKey::Enter)==ViewResult::ProposalReady && view.ConfirmedProposal()->value.number==2);view.Key(ViewKey::Back);
    view.Key(ViewKey::Down);CHECK(view.Key(ViewKey::Enter)==ViewResult::Opened && view.Page()==ViewPage::Number);
    CHECK(view.Key(ViewKey::Left)==ViewResult::Edge);CHECK(view.Key(ViewKey::Right)==ViewResult::Focus);
    storage[4]=20;CHECK(view.Key(ViewKey::Enter)==ViewResult::Unavailable && view.ValueStatus()==EFI_MEDIA_CHANGED && !view.ConfirmedProposal());storage[4]=10;
    CHECK(view.Key(ViewKey::Enter)==ViewResult::Opened);view.Key(ViewKey::Down);CHECK(view.Key(ViewKey::Enter)==ViewResult::ProposalReady && view.ConfirmedProposal()->value.number==15);view.Key(ViewKey::Back);
    view.Key(ViewKey::Down);CHECK(view.Key(ViewKey::Enter)==ViewResult::Opened && view.Page()==ViewPage::String);
    CHECK(view.SetEditText(u"new",3));const char16_t bad[]={0xd800};CHECK(!view.SetEditText(bad,1));
    CHECK(view.Key(ViewKey::Erase)==ViewResult::Focus);CHECK(view.Key(ViewKey::Enter)==ViewResult::Opened);view.Key(ViewKey::Down);
    CHECK(view.Key(ViewKey::Enter)==ViewResult::ProposalReady && std::u16string(view.ConfirmedProposal()->value.text)==u"ne");view.Key(ViewKey::Back);
    const auto before=extracts;view.Key(ViewKey::Down);CHECK(view.Key(ViewKey::Space)==ViewResult::Unavailable && extracts==before);
    CHECK(view.FocusAnnouncement(label,1024) && std::u16string(label).find(u"protected password")!=std::u16string::npos);
    view.Key(ViewKey::Down);view.Key(ViewKey::Down);view.Key(ViewKey::Down);CHECK(view.Key(ViewKey::Enter)==ViewResult::Unavailable);
    CHECK(view.RowLabel(view.Selected(),label,1024) && std::u16string(label).find(u"unresolved")!=std::u16string::npos);
    callback(2,nullptr,nullptr,&hii,2);CHECK(!view.ConfirmedProposal() && view.Key(ViewKey::Enter)==ViewResult::Stale && view.Page()==ViewPage::Stale);
    CHECK(routes==0 && storage[0]==1 && storage[4]==10 && storage[8]=='A');
    exported=fixture(true);CHECK(database.Refresh()==0 && database.Read(0,langs,2)==0);
    CHECK(view.Bind(&boot,database,config,database.Current(),true));view.Key(ViewKey::Enter);view.Key(ViewKey::Enter);
    CHECK(view.Key(ViewKey::Space)==ViewResult::Unavailable && view.RowLabel(0,label,1024) && std::u16string(label).find(u"nierozwiązane")!=std::u16string::npos);
    CHECK(!view.RowLabel(0,label,1));CHECK(database.Close()==0 && allocations==0);
    puts("PASS actual HII view/model/database/config navigation, current values, protected denial, proposal-only edits and stale invalidation");
}
