// Reuse genuine parser fixture construction without running its separate suite.
#define main HiiModelFixtureTestMain
#include "../src/efi/hii/HiiModelTests.cpp"
#undef main
#undef CHECK
#include "../src/efi/HiiMenu.h"
#include "../src/efi/hii/HiiConfig.h"
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
static efi_status EFIAPI List(const DatabaseProtocol*,uint8_t type,const efi_guid* guid,size_t* size,void** out){CHECK(type==2 && !guid);*size=sizeof(void*);if(!out)return EFI_BUFFER_TOO_SMALL;*out=&hii;return 0;}
static efi_status EFIAPI Export(const DatabaseProtocol*,void* value,size_t* size,void* out){CHECK(value==&hii);*size=exported.size();if(!out)return EFI_OUT_OF_RESOURCES;memcpy(out,exported.data(),exported.size());return 0;}
static efi_status EFIAPI Register(const DatabaseProtocol*,uint8_t,const efi_guid*,PackageNotify cb,size_t,efi_handle* out){callback=cb;*out=reinterpret_cast<void*>(++notifications);return 0;}
static unsigned closeFailures=0,closeAttempts=0;
static efi_status EFIAPI Unregister(const DatabaseProtocol*,efi_handle){++closeAttempts;if(closeFailures){--closeFailures;return EFI_DEVICE_ERROR;}return 0;}
static efi_status EFIAPI Owner(const DatabaseProtocol*,void*,efi_handle* out){*out=&driver;return 0;}
static efi_status EFIAPI Extract(const ConfigAccessProtocol*,const char16_t* request,char16_t** progress,char16_t** result){
    ++extracts;const std::u16string text=request;
    auto number=[&](const char16_t* field){const auto start=text.find(field);CHECK(start!=text.npos);size_t n=0;for(size_t i=start+7;i<text.size() && text[i]!=u'&';++i){auto c=text[i];n=n*16+(c<=u'9'?c-u'0':c-u'a'+10);}return n;};
    const auto offset=number(u"OFFSET="),width=number(u"&WIDTH=");CHECK(offset+width<=sizeof(storage));
    std::u16string response=text+u"&VALUE=";for(size_t i=width;i;--i){auto value=storage[offset+i-1];response+=u"0123456789abcdef"[value>>4];response+=u"0123456789abcdef"[value&15];}
    void* memory=nullptr;CHECK(Alloc(EfiLoaderData,(response.size()+1)*2,&memory)==0);*result=static_cast<char16_t*>(memory);
    memcpy(*result,response.c_str(),(response.size()+1)*2);*progress=const_cast<char16_t*>(request+text.size());return 0;
}
static efi_status EFIAPI Route(const ConfigRoutingProtocol*,const char16_t* request,char16_t** progress){
    ++routes;std::u16string text=request;auto digit=[](char16_t c){return c<=u'9'?c-u'0':c-u'a'+10;};
    auto number=[&](const char16_t* field){auto at=text.find(field);CHECK(at!=text.npos);size_t value=0;for(at+=7;at<text.size() && text[at]!=u'&';++at)value=value*16+digit(text[at]);return value;};
    const auto offset=number(u"OFFSET="),width=number(u"&WIDTH=");CHECK(offset+width<=sizeof(storage));
    auto at=text.find(u"&VALUE=");CHECK(at!=text.npos);at+=7;
    for(size_t i=0;i<width;++i){const auto index=at+(width-1-i)*2;storage[offset+i]=uint8_t(digit(text[index])*16+digit(text[index+1]));}
    *progress=const_cast<char16_t*>(request+text.size());return 0;
}
using namespace hakefi;
static std::vector<efi_input_key> keys;static size_t keyAt=0,pumps=0,frames=0;
static size_t invalidateAt=SIZE_MAX;
static std::vector<std::string> utterances;static std::vector<MenuCue> cues;
static std::u16string painted;static bool preparation=false;
static efi_simple_text_input_protocol input{};static efi_simple_text_output_protocol console{};
static simple_text_output_mode mode{};
static efi_status EFIAPI ReadKey(efi_simple_text_input_protocol*,efi_input_key* key){CHECK(keyAt<keys.size());if(keyAt==invalidateAt){callback(2,nullptr,nullptr,&hii,2);invalidateAt=SIZE_MAX;}*key=keys[keyAt++];return 0;}
static efi_status EFIAPI Wait(size_t,efi_event*,size_t* selected){CHECK(keyAt<keys.size());*selected=0;return 0;}
static efi_status EFIAPI Check(efi_event){return preparation?EFI_NOT_READY:EFI_SUCCESS;}
static efi_status EFIAPI Create(uint32_t,efi_tpl,efi_event_notify,void*,efi_event* event){*event=&mode;return 0;}
static efi_status EFIAPI Timer(efi_event,efi_timer_delay,uint64_t){return 0;}
static efi_status EFIAPI Close(efi_event){return 0;}
static efi_status EFIAPI Stall(size_t){return 0;}
static efi_status EFIAPI Query(efi_simple_text_output_protocol*,size_t,size_t* cols,size_t* rows){*cols=100;*rows=25;return 0;}
static efi_status EFIAPI Attribute(efi_simple_text_output_protocol*,size_t){return 0;}
static efi_status EFIAPI Clear(efi_simple_text_output_protocol*){++frames;return 0;}
static efi_status EFIAPI Cursor(efi_simple_text_output_protocol*,size_t,size_t){return 0;}
static efi_status EFIAPI Output(efi_simple_text_output_protocol*,char16_t* text){painted+=text;return 0;}
static efi_status EFIAPI Speak(MenuVoice*,const char*,const char* text){utterances.emplace_back(text);preparation=true;return 0;}
static efi_status EFIAPI Pump(MenuVoice*,uint32_t){++pumps;preparation=false;return 0;}
static efi_status EFIAPI Stop(MenuVoice*){return 0;}
static bool EFIAPI Preparing(MenuVoice*){return preparation;}
static void Cue(void*,MenuCue cue){cues.push_back(cue);}
static void Enabled(void*,bool){}
static void Run(std::initializer_list<efi_input_key> sequence){
    keys=sequence;keyAt=0;preparation=false;
    efi_system_table system{};system.BootServices=&boot;system.ConIn=&input;system.ConOut=&console;
    MenuVoiceV2 voice{{2,sizeof(MenuVoiceV2),Speak,Pump,Stop},Preparing};AudioEffects effects{nullptr,Cue,Enabled,nullptr};
    CHECK(RunHiiMenu(&system,&voice.base,&effects,false,true,false)==0);
    CHECK(keyAt==keys.size() && allocations==0);
}
int main(){
    boot.LocateProtocol=Locate;boot.HandleProtocol=Handle;boot.AllocatePool=Alloc;boot.FreePool=Free;
    boot.CreateEvent=Create;boot.SetTimer=Timer;boot.WaitForEvent=Wait;boot.CheckEvent=Check;boot.CloseEvent=Close;boot.Stall=Stall;
    input.ReadKeyStroke=ReadKey;input.WaitForKey=&input;
    console.Mode=&mode;console.QueryMode=Query;console.SetAttribute=Attribute;console.ClearScreen=Clear;console.SetCursorPosition=Cursor;console.OutputString=Output;
    dbApi.list=List;dbApi.exportList=Export;dbApi.driver=Owner;dbApi.registerNotify=Register;dbApi.unregisterNotify=Unregister;
    access.extract=Extract;routing.route=Route;exported=fixture(false);storage[0]=1;storage[1]=1;storage[4]=10;storage[8]='A';
    const efi_input_key enter{0,13},space{0,32},down{2,0},back{23,0};
    // Genuine EFI keys -> real model -> Extract -> default Cancel -> no Route.
    Run({enter,enter,enter,space,enter,back,back,back,back});CHECK(routes==0 && storage[0]==1);
    // Explicit Yes -> Route/readback once. Dismissing result cannot write again.
    closeFailures=1;const auto closesBefore=closeAttempts;
    Run({enter,enter,enter,space,down,enter,enter,back,back,back,back});
    CHECK(routes==1 && storage[0]==0 && closeAttempts>=closesBefore+7);
    CHECK(pumps>0 && frames>10 && painted.find(u"Change saved and read back.")!=painted.npos);
    bool proposed=false,checked=false;for(const auto& speech:utterances){if(speech.find("proposed value")!=speech.npos)proposed=true;if(speech.find("checked")!=speech.npos)checked=true;}
    CHECK(proposed && checked);bool dialog=false;for(auto cue:cues)if(cue==MenuCue::dialogOpen)dialog=true;CHECK(dialog);
    // Typed text editor, no commit on Escape; F1 popup and F5 actual refresh.
    Run({enter,enter,enter,down,down,down,enter,{0,'Z'},back,{11,0},enter,{15,0},back});
    CHECK(routes==1 && storage[8]=='A');
    // A notification retires the edited object. Dismiss then F5 must work,
    // without a popup recreation loop or replaying the interrupted operation.
    invalidateAt=3;Run({enter,enter,enter,space,enter,{15,0},back});CHECK(routes==1);
    // Numeric text outside the genuine IFR bounds is never submitted.
    Run({enter,enter,enter,down,down,enter,{0,'9'},{0,'9'},{0,'9'},enter,enter,back,back,back,back,back});
    CHECK(routes==1 && storage[4]==10);
    Run({enter,enter,enter,down,enter,down,enter,down,enter,enter,back,back,back,back});
    CHECK(routes==2 && storage[1]==2);
    Run({enter,enter,enter,down,down,enter,{0,'2'},{0,'0'},enter,down,enter,enter,back,back,back,back});
    CHECK(routes==3 && storage[4]==20);
    Run({enter,enter,enter,down,down,down,enter,{0,'Z'},enter,down,enter,enter,back,back,back,back});
    CHECK(routes==4 && storage[8]=='A' && storage[10]=='Z' && storage[12]==0);
    // A firmware string can exist yet be empty: retain role/ID and provide a
    // genuine unnamed fallback instead of speech beginning with a comma.
    Bytes named(20),strings(42);const char* locale="en-US";strings.insert(strings.end(),locale,locale+6);
    set(strings,0,strings.size()+4,4);set(strings,4,strings.size()+4,4);text(strings,"Settings");text(strings,"");text(strings,"Choice");strings.push_back(0);package(named,4,strings);
    const size_t previousStrings=size_t(exported[20])|(size_t(exported[21])<<8)|(size_t(exported[22])<<16);
    named.insert(named.end(),exported.begin()+20+previousStrings,exported.end());set(named,16,named.size(),4);exported=named;
    Run({enter,enter,enter,back,back,back,back});CHECK(painted.find(u"Unnamed item #2, checkbox")!=painted.npos && routes==4);
    // Defensive export with no Forms after the filtered enumeration must stay
    // empty, speak an explanation, and accept Back; never invent a question.
    const size_t stringBytes=size_t(exported[20])|(size_t(exported[21])<<8)|(size_t(exported[22])<<16);
    Bytes empty(20);empty.insert(empty.end(),exported.begin()+20,exported.begin()+20+stringBytes);package(empty,0xdf,{});set(empty,16,empty.size(),4);exported=empty;
    const auto readsBefore=extracts;Run({enter,enter,back,back});
    CHECK(extracts==readsBefore && routes==4 && painted.find(u"No items in this view. Escape: back; F5: refresh.")!=painted.npos);
    bool emptySpoken=false;for(const auto& speech:utterances)if(speech.find("No items in this view.")!=speech.npos)emptySpoken=true;CHECK(emptySpoken);
    puts("PASS actual EFI HII menu, Forms-only enumeration, truthful empty-view speech/Back, all editable controls, and lifetime guards");
}
