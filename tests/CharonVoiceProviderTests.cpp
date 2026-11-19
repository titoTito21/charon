#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4200)
#endif
#include "../src/efi/CharonVoiceProvider.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(false)
using namespace charonaudioefi;
static efi_boot_services boot{};
static bool protocolPresent=true,hardwareDone=true;static uint64_t sent=0;static Stream stream{};static hakaudioefi::Protocol audio{};
static efi_handle handles[2];static hakaudioefi::Port ports[1];static State observed{};
static size_t accepted=0,written=0;static int calls=0,setups=0,begins=0,stops=0,frees=0;
static bool sameHandle=true,badAccepted=false;static efi_status setupStatus=0,beginStatus=0,writeStatus=0,stateStatus=0,stopStatus=0;
static efi_status EFIAPI Locate(efi_locate_search_type type,efi_guid* guid,void*,size_t* count,efi_handle** out) {
    if(type==ByProtocol) {CHECK(!memcmp(guid,&streamGuid,sizeof(*guid)));if(!protocolPresent)return EFI_NOT_FOUND;}
    *count=1;*out=handles;return EFI_SUCCESS;
}
static efi_status EFIAPI Protocol(efi_handle handle,efi_guid* guid,void** out) {
    CHECK(handle==handles[0]);
    if(!memcmp(guid,&streamGuid,sizeof(*guid))) {*out=&stream;return EFI_SUCCESS;}
    CHECK(!memcmp(guid,&hakaudioefi::guid,sizeof(*guid)));
    if(!sameHandle) {return EFI_NOT_FOUND;}
    *out=&audio;return EFI_SUCCESS;
}
static size_t allocations=0;
static efi_status EFIAPI Free(void* value) {if(value!=handles && value!=ports) {CHECK(allocations);--allocations;free(value);}++frees;return EFI_SUCCESS;}
static efi_status EFIAPI Outputs(hakaudioefi::Protocol*,hakaudioefi::Port** out,size_t* count) {*out=ports;*count=1;return EFI_SUCCESS;}
static efi_status EFIAPI Setup(hakaudioefi::Protocol*,uint64_t mask,int8_t gain,uint32_t frequency,uint32_t bits,uint8_t channels,size_t delay) {
    ++setups;CHECK(mask==1 && gain==-12 && frequency==64 && bits==2 && channels==2 && delay==0);return setupStatus;
}
static efi_status EFIAPI Begin(Stream*) {CHECK(setups>=1);++begins;sent=0;return beginStatus;}
static efi_status EFIAPI Write(Stream*,const int16_t*,size_t frames,size_t* taken) {
    ++calls;CHECK(frames<=128);*taken=badAccepted?frames+1:(accepted<frames?accepted:frames);written+=*taken;sent+=*taken;return writeStatus;
}
static efi_status EFIAPI GetState(Stream*,State* state) {CHECK(state->size==sizeof(State));*state=observed;state->submitted=sent;state->consumed=hardwareDone?sent:0;state->hardware=hardwareDone?0:uint32_t(sent);state->flags=hardwareDone?3:1;return stateStatus;}
static efi_status EFIAPI Stop(Stream*) {++stops;return stopStatus;}
void ResetAudio() {
    boot={};boot.LocateHandleBuffer=Locate;boot.HandleProtocol=Protocol;boot.FreePool=Free;
    stream={1,sizeof(Stream),Begin,Write,GetState,Stop};audio={};audio.revision=4;audio.outputs=Outputs;audio.setup=Setup;
    handles[0]=&audio;ports[0]={0,2,64,0,0,0};observed={};observed.size=sizeof(State);observed.flags=3;
    accepted=written=0;calls=setups=begins=stops=frees=0;sameHandle=true;badAccepted=false;
    setupStatus=beginStatus=writeStatus=stateStatus=stopStatus=0;
}

static efi_file_protocol root{},file{};static std::map<std::u16string,std::vector<uint8_t>> files;
static std::vector<uint8_t>* content=nullptr;static size_t position=0,claim=SIZE_MAX;
static bool failAllocation=false;static int stalls=0,loadedImages=0,unloadedImages=0,startedImages=0;
static efi_status loadStatus=0,driverStartStatus=0;static char driverToken;
static char deadlineToken;static bool deadlineOpen=false;
static unsigned deadlineChecks=0,deadlineLimit=32;
static efi_status createDeadlineStatus=0,armDeadlineStatus=0,checkDeadlineStatus=0;
static efi_status EFIAPI CreateDeadline(uint32_t type,efi_tpl tpl,efi_event_notify notify,void* context,efi_event* out) {
    CHECK(type==EVT_TIMER&&tpl==TPL_APPLICATION&&!notify&&!context&&!deadlineOpen);
    if(createDeadlineStatus)return createDeadlineStatus;
    deadlineOpen=true;*out=&deadlineToken;return EFI_SUCCESS;
}
static efi_status EFIAPI ArmDeadline(efi_event event,efi_timer_delay type,uint64_t time) {
    CHECK(event==&deadlineToken&&deadlineOpen&&type==TimerRelative&&time==50000);
    deadlineChecks=0;return armDeadlineStatus;
}
static efi_status EFIAPI CheckDeadline(efi_event event) {
    CHECK(event==&deadlineToken&&deadlineOpen);if(checkDeadlineStatus)return checkDeadlineStatus;
    return ++deadlineChecks>deadlineLimit?EFI_SUCCESS:EFI_NOT_READY;
}
static efi_status EFIAPI CloseDeadline(efi_event event) {CHECK(event==&deadlineToken&&deadlineOpen);deadlineOpen=false;return EFI_SUCCESS;}
static efi_status EFIAPI Allocate(efi_memory_type,size_t bytes,void** out) {
    if(failAllocation) {return EFI_OUT_OF_RESOURCES;}
    *out=malloc(bytes);if(!*out)return EFI_OUT_OF_RESOURCES;++allocations;return EFI_SUCCESS;
}
static efi_status EFIAPI Stall(size_t) {++stalls;return EFI_SUCCESS;}
static efi_status EFIAPI OpenFile(efi_file_protocol*,efi_file_protocol** result,const char16_t* path,uint64_t,uint64_t) {
    const auto found=files.find(path);if(found==files.end())return EFI_NOT_FOUND;
    content=&found->second;position=0;*result=&file;return EFI_SUCCESS;
}
static efi_status EFIAPI Info(efi_file_protocol*,efi_guid*,size_t* length,void* output) {
    auto* info=static_cast<efi_file_info*>(output);memset(info,0,sizeof(*info));info->Size=sizeof(*info);
    info->FileSize=claim==SIZE_MAX?content->size():claim;*length=sizeof(*info);return EFI_SUCCESS;
}
static efi_status EFIAPI Read(efi_file_protocol*,size_t* length,void* output) {
    size_t count=*length;if(count>content->size()-position)count=content->size()-position;
    if(count>3) {count=3;}
    if(count)memcpy(output,content->data()+position,count);
    position+=count;*length=count;return EFI_SUCCESS;
}
static efi_status EFIAPI Close(efi_file_protocol*) {return EFI_SUCCESS;}
static efi_status EFIAPI Load(bool policy,efi_handle,efi_device_path_protocol* path,void* bytes,size_t length,efi_handle* out) {
    CHECK(!policy && !path && bytes && length==8);++loadedImages;*out=&driverToken;return loadStatus;
}
static efi_status EFIAPI Start(efi_handle image,size_t*,char16_t**) {CHECK(image==&driverToken);++startedImages;protocolPresent=true;return driverStartStatus;}
static efi_status EFIAPI Unload(efi_handle image) {CHECK(image==&driverToken);++unloadedImages;return EFI_SUCCESS;}
static efi_status EFIAPI Connect(efi_handle,efi_handle*,efi_device_path_protocol*,bool) {return EFI_SUCCESS;}
void Reset(bool pack=false) {
    CHECK(!deadlineOpen);createDeadlineStatus=armDeadlineStatus=checkDeadlineStatus=0;deadlineLimit=32;
    CHECK(allocations==0);ResetAudio();protocolPresent=hardwareDone=true;sent=0;failAllocation=false;stalls=loadedImages=unloadedImages=startedImages=0;
    loadStatus=driverStartStatus=0;claim=SIZE_MAX;accepted=128;
    boot.AllocatePool=Allocate;boot.Stall=Stall;boot.LoadImage=Load;boot.StartImage=Start;boot.UnloadImage=Unload;boot.ConnectController=Connect;
    boot.CreateEvent=CreateDeadline;boot.SetTimer=ArmDeadline;boot.CheckEvent=CheckDeadline;boot.CloseEvent=CloseDeadline;
    root={};file={};root.Open=OpenFile;file.GetInfo=Info;file.Read=Read;file.Close=Close;files.clear();
    if(pack) {
        for(unsigned i=1;i<=12;++i) {std::string name=hakefi::MenuCueAsset(hakefi::MenuCue(i));name.replace(name.size()-3,3,"pcm");
            files[std::u16string(u"\\EFI\\HAK\\audio\\")+std::u16string(name.begin(),name.end())]={100,0,200,0};}
        for(unsigned i=1;i<=4;++i)for(const auto* prefix:{u"start-",u"background-"})
            files[std::u16string(u"\\EFI\\HAK\\audio\\")+prefix+char16_t(u'0'+i)+u".pcm"]={100,0,200,0};
    }
}
int main() {
    Reset();VoiceProvider voiceOnly;CHECK(voiceOnly.Initialize(&root,&boot,&root)==0);
    CHECK(voiceOnly.Voice() && !voiceOnly.Effects() && voiceOnly.AssetsStatus()==EFI_NOT_FOUND);
    CHECK(voiceOnly.Initialize(&root,&boot,&root)==EFI_ALREADY_STARTED);
    auto* api=voiceOnly.Voice();
    CHECK(api->version==2&&api->size==sizeof(hakefi::MenuVoiceV2));
    auto* extended=reinterpret_cast<hakefi::MenuVoiceV2*>(api);
    CHECK(extended->Preparing&&!extended->Preparing(api));
    // Already expired firmware timers must permit4096 useful checkpoints before
    // yielding. Only the private event is polled, per1024 work checkpoints.
    CHECK(api->SpeakUtf8(api,"pl","test")==0);deadlineLimit=0;
    CHECK(extended->Preparing(api));
    for(unsigned i=0;i<3;++i)CHECK(api->Pump(api,480)==0&&written==0&&deadlineChecks==1);
    for(unsigned i=0;!written;++i){CHECK(i<128);CHECK(api->Pump(api,480)==0);CHECK(deadlineChecks<=1);}
    CHECK(!extended->Preparing(api));
    CHECK(api->SpeakUtf8(api,"en","Boot devices, submenu")==0);deadlineLimit=1000000;
    CHECK(extended->Preparing(api));
    CHECK(api->Pump(api,480)==0&&deadlineChecks==28); //32768 cap,4096 minimum,1024 poll quantum
    deadlineLimit=32;
    for(const auto* language:{"pl","en"}) {
        const auto before=written;CHECK(api->SpeakUtf8(api,language,"test") ==0);
        for(unsigned i=0;i<30000 && written==before;++i)CHECK(api->Pump(api,480)==0);
        CHECK(written>before);
    }
    CHECK(api->SpeakUtf8(api,"xx","test")==EFI_UNSUPPORTED);
    CHECK(api->Pump(api,0)==EFI_INVALID_PARAMETER);CHECK(api->Pump(api,480)==0);
    CHECK(api->Stop(api)==0);const auto priorBegins=begins;
    CHECK(!extended->Preparing(api));
    CHECK(api->SpeakUtf8(api,"pl","nowy test")==0 && begins==priorBegins+1);
    CHECK(voiceOnly.Shutdown()==0 && !voiceOnly.Voice() && !allocations);
    Reset(true);VoiceProvider full;CHECK(full.Initialize(&root,&boot,&root)==0 && full.Effects());
    auto* effects=full.Effects();effects->SetMelodies(effects->context,1,1);effects->QueueCue(effects->context,hakefi::MenuCue::continueBoot);
    hardwareDone=false;CHECK(full.Stop()==EFI_TIMEOUT && stops==0 && stalls==5000 && allocations>0);
    stopStatus=EFI_DEVICE_ERROR;hardwareDone=true;CHECK(full.Stop()==EFI_DEVICE_ERROR && allocations>0);
    CHECK(full.Shutdown()==EFI_DEVICE_ERROR && full.Voice() && allocations>0);
    stopStatus=0;CHECK(full.Shutdown()==0 && !allocations);
    Reset();failAllocation=true;VoiceProvider oom;CHECK(oom.Initialize(&root,&boot,&root)==EFI_OUT_OF_RESOURCES);
    failAllocation=false;CHECK(oom.Shutdown()==0 && !allocations);
    Reset();setupStatus=EFI_DEVICE_ERROR;VoiceProvider partial;CHECK(partial.Initialize(&root,&boot,&root)==EFI_DEVICE_ERROR);
    stopStatus=EFI_DEVICE_ERROR;CHECK(partial.Shutdown()==EFI_DEVICE_ERROR && allocations>0);
    stopStatus=0;CHECK(partial.Shutdown()==0 && !allocations);
    Reset();protocolPresent=false;files[u"\\EFI\\HAK\\AudioDxe.efi"]={1,2,3,4,5,6,7,8};loadStatus=EFI_SECURITY_VIOLATION;
    VoiceProvider denied;CHECK(denied.Initialize(&root,&boot,&root)==EFI_SECURITY_VIOLATION);
    CHECK(loadedImages==1 && unloadedImages==1 && startedImages==0);CHECK(denied.Shutdown()==0 && !allocations);
    Reset();protocolPresent=false;files[u"\\EFI\\HAK\\AudioDxe.efi"]={1,2,3,4,5,6,7,8};claim=4;
    VoiceProvider changed;CHECK(changed.Initialize(&root,&boot,&root)==EFI_MEDIA_CHANGED && loadedImages==0);CHECK(changed.Shutdown()==0 && !allocations);
    Reset();protocolPresent=false;files[u"\\EFI\\HAK\\AudioDxe.efi"]={1,2,3,4,5,6,7,8};
    VoiceProvider driver;CHECK(driver.Initialize(&root,&boot,&root)==0 && loadedImages==1 && startedImages==1 && unloadedImages==0);
    CHECK(driver.Shutdown()==0 && !allocations && unloadedImages==0);
    Reset();createDeadlineStatus=EFI_OUT_OF_RESOURCES;VoiceProvider noTimer;
    CHECK(noTimer.Initialize(&root,&boot,&root)==EFI_OUT_OF_RESOURCES);
    CHECK(noTimer.Shutdown()==0&&!allocations&&!deadlineOpen);
    Reset();VoiceProvider timerFailure;CHECK(timerFailure.Initialize(&root,&boot,&root)==0);
    api=timerFailure.Voice();CHECK(api->SpeakUtf8(api,"pl","test")==0);
    armDeadlineStatus=EFI_DEVICE_ERROR;CHECK(api->Pump(api,480)==EFI_DEVICE_ERROR);
    armDeadlineStatus=0;checkDeadlineStatus=EFI_DEVICE_ERROR;CHECK(api->Pump(api,480)==EFI_DEVICE_ERROR);
    CHECK(timerFailure.Shutdown()==0&&!allocations&&!deadlineOpen);
    Reset();VoiceProvider transient;CHECK(transient.Initialize(&root,&boot,&root)==0);
    api=transient.Voice();CHECK(api->SpeakUtf8(api,"pl","test")==0);
    const int beginsBefore=begins,stopsBefore=stops;writeStatus=EFI_TIMEOUT;
    for(unsigned i=0;stops==stopsBefore;++i){CHECK(i<30000);CHECK(api->Pump(api,480)==0);} //real Stop, fresh Begin.
    writeStatus=0;for(unsigned i=0;i<64;++i)CHECK(api->Pump(api,480)==0);
    CHECK(begins==beginsBefore+1&&stops==stopsBefore+1&&transient.PlaybackStatus()==0&&transient.DeviceStatus()==0);
    stopStatus=0;CHECK(api->Stop(api)==0&&transient.Shutdown()==0&&!allocations&&!deadlineOpen);
    Reset();VoiceProvider playbackFailure;CHECK(playbackFailure.Initialize(&root,&boot,&root)==0);
    api=playbackFailure.Voice();CHECK(api->SpeakUtf8(api,"en","test")==0);writeStatus=EFI_TIMEOUT;
    efi_status failed=EFI_SUCCESS;
    for(unsigned i=0;failed==EFI_SUCCESS;++i){CHECK(i<30000);failed=api->Pump(api,480);}
    CHECK(failed==EFI_TIMEOUT&&playbackFailure.PlaybackStatus()==EFI_TIMEOUT);
    stopStatus=EFI_DEVICE_ERROR;CHECK(api->Stop(api)==EFI_DEVICE_ERROR);
    CHECK(playbackFailure.PlaybackStatus()==EFI_TIMEOUT); //Stop failure cannot maskfirstcause.
    stopStatus=0;CHECK(api->Stop(api)==0); //quiescent DMA permits handoff despitehistoricfault.
    CHECK(playbackFailure.DeviceStatus()==EFI_TIMEOUT&&playbackFailure.PlaybackStatus()==EFI_TIMEOUT);
    CHECK(playbackFailure.Shutdown()==0&&!allocations&&!deadlineOpen);
    puts("PASS actual local provider/voice/device/assets, lifecycle cleanup and authenticated driver loading");
}
