#include <efi/protocol/simple-file-system.h>
#include "../src/efi/BootSpeech.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
static efi_file_protocol root={},file={};
static hakaudioefi::Protocol audio={};
static int stopped=0,opened=0,played=0,closed=0;
static size_t sampleSize=8;
static size_t expectedLength=0;
static efi_status stopStatus=EFI_SUCCESS,readStatus=EFI_SUCCESS,playStatus=EFI_SUCCESS;
static int configured=0,failSetup=0;
static hakaudioefi::Callback lastCallback=nullptr;
static void* lastContext=nullptr;
static efi_status EFIAPI Setup(hakaudioefi::Protocol*,uint64_t mask,int8_t gain,uint32_t frequency,uint32_t bits,uint8_t channels,size_t delay) {
    ++configured;assert(mask==1 && gain==-12 && bits==2 && channels==2 && !delay);
    assert(frequency==(configured%2 ? 32U : 64U));
    return configured==failSetup ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static efi_status EFIAPI Stop(hakaudioefi::Protocol*) {++stopped;return stopStatus;}
static efi_status EFIAPI Open(efi_file_protocol*,efi_file_protocol** out,const char16_t*,uint64_t,uint64_t) {
    assert(stopped);++opened;*out=&file;return EFI_SUCCESS;
}
static efi_status EFIAPI Read(efi_file_protocol*,size_t* size,void* data) {
    assert(sampleSize<=*size);*size=sampleSize;memset(data,42,sampleSize);return readStatus;
}
static efi_status EFIAPI Close(efi_file_protocol*) {++closed;return EFI_SUCCESS;}
static efi_status EFIAPI Play(hakaudioefi::Protocol*,void*,size_t length,size_t pos,hakaudioefi::Callback callback,void* context) {
    assert(closed==opened && length==(expectedLength ? expectedLength : sampleSize) && !pos && bool(callback)==bool(context));
    lastCallback=callback;lastContext=context;++played;return playStatus;
}
int main() {
    char16_t path[64]={};
    assert(hakaudioefi::SpeechPath("pl","Menedżer rozruchu Haiku Accessibility Kit.",path));
    const char16_t expected[]=u"\\EFI\\HAK\\SPEECH\\0a0f1984fa661905.pcm";
    assert(sizeof(expected)<=sizeof(path) && !memcmp(path,expected,sizeof(expected)));
    assert(!hakaudioefi::SpeechPath("pl","",path));
    (void)hakaudioefi::guid;
    audio.revision=4;audio.stop=Stop;audio.playAsync=Play;
    root.Open=Open;file.Read=Read;file.Close=Close;
    uint8_t buffer[32]={};hakaudioefi::Speech speech(&audio,&root,buffer,sizeof(buffer));
    assert(speech.SayFile(u"test")==EFI_SUCCESS && played==1);
    stopStatus=EFI_DEVICE_ERROR;memset(buffer,7,sizeof(buffer));
    assert(speech.SayFile(u"next")==EFI_DEVICE_ERROR && opened==1 && buffer[0]==7);
    stopStatus=EFI_SUCCESS;
    for(size_t size:{size_t(0),size_t(3),sizeof(buffer)}) {
        sampleSize=size;assert(speech.SayFile(u"bad")==EFI_BAD_BUFFER_SIZE && played==1);
    }
    sampleSize=8;readStatus=EFI_DEVICE_ERROR;
    assert(speech.SayFile(u"badread")==EFI_DEVICE_ERROR && closed==opened && played==1);
    readStatus=EFI_SUCCESS;playStatus=EFI_DEVICE_ERROR;
    int before=stopped;assert(speech.SayFile(u"badplay")==EFI_DEVICE_ERROR && stopped==before+2);
    audio.revision=3;assert(speech.SayFile(u"wrongABI")==EFI_INVALID_PARAMETER);
    audio.revision=4;audio.setup=Setup;playStatus=EFI_SUCCESS;
    hakaudioefi::Speech resetSpeech(&audio,&root,buffer,sizeof(buffer),1);
    assert(resetSpeech.SayFile(u"reset")==EFI_SUCCESS && configured==2);
    int beforePlay=played;failSetup=4;
    assert(resetSpeech.SayFile(u"setupfailure")==EFI_DEVICE_ERROR && played==beforePlay);
    stopStatus=EFI_DEVICE_ERROR;
    assert(speech.Handoff()==EFI_DEVICE_ERROR);
    int beforeOpen=opened;beforePlay=played;
    assert(speech.SayText("en","HAK boot manager.")==EFI_ACCESS_DENIED);
    assert(opened==beforeOpen && played==beforePlay);
    stopStatus=EFI_SUCCESS;assert(speech.Handoff()==EFI_SUCCESS);
    before=stopped;audio.revision=0;
    assert(speech.Handoff()==EFI_SUCCESS && speech.Stop()==EFI_SUCCESS && stopped==before);
    assert(speech.SayFile(u"late")==EFI_ACCESS_DENIED && played==beforePlay);
    audio.revision=4;hakaudioefi::Completion completion;
    hakaudioefi::Speech tracked(&audio,&root,buffer,sizeof(buffer),0,&completion);
    assert(tracked.SayFile(u"tracked")==EFI_SUCCESS && !completion.done);
    assert(lastCallback && lastContext==&completion);lastCallback(&audio,lastContext);
    assert(completion.done);
    assert(tracked.SayFile(u"again")==EFI_SUCCESS && !completion.done);
    stopStatus=EFI_DEVICE_ERROR;assert(tracked.Stop()==EFI_DEVICE_ERROR && !completion.done);
    stopStatus=EFI_SUCCESS;assert(tracked.Handoff()==EFI_SUCCESS && completion.done);
    hakaudioefi::Speech padded(&audio,&root,buffer,sizeof(buffer),0,nullptr,1,1);
    expectedLength=16;assert(padded.SayFile(u"padded")==EFI_SUCCESS);
    for(unsigned i=0;i<16;++i)assert(buffer[i]==(i<4 || i>=12 ? 0 : 42));
    hakaudioefi::Speech tooLarge(&audio,&root,buffer,sizeof(buffer),0,nullptr,100,100);
    beforePlay=played;assert(tooLarge.SayFile(u"oversize")==EFI_BAD_BUFFER_SIZE && played==beforePlay);
    puts("PASS boot speech stop-before-overwrite, bounded PCM, close/error and failed-start cleanup");
}
