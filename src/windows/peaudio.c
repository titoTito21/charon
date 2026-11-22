/* peaudio.dll: makes NVDA audible in Windows Setup's WinPE (r407).
   Measured on the Windows 11 boot image 10.0.26100.1742 from outside the machine: WinPE plays only
   audio streams of the categories "alerts" (4) and "sound effects" (5); every other category,
   among them the default one NVDA's streams have, is accepted and consumed but comes out as
   silence. NVDA never sets a category, so this library, loaded into NVDA by the synthesizer driver
   charonpe.py before the first stream is made, puts itself in front of IAudioClient::Initialize
   (one shared method table for all streams of the process) and sets the category "alerts" first.
   CharonPeAudio returns 0 when the method is in place, otherwise the failing HRESULT.
   kernel32 and ole32 only, no C runtime. GPL-2.0-or-later. */
typedef unsigned int u32; typedef unsigned short u16; typedef long hr_t;
#define IMP __declspec(dllimport)
IMP int __stdcall VirtualProtect(void* address,unsigned long long size,u32 protect,u32* old);
IMP hr_t __stdcall CoInitializeEx(void* reserved,u32 model);
IMP hr_t __stdcall CoCreateInstance(const void* clsid,void* outer,u32 context,const void* iid,void** out);
typedef struct {u32 a;u16 b,c;unsigned char d[8];} guid;
static const guid classEnumerator={0xBCDE0395,0xE52F,0x467C,{0x8E,0x3D,0xC4,0x57,0x92,0x91,0x69,0x2E}};
static const guid idEnumerator={0xA95664D2,0x9614,0x4F35,{0xA7,0x46,0xDE,0x8D,0xB6,0x36,0x17,0xE6}};
static const guid idClient={0x1CB9AD4C,0xDBFA,0x4C32,{0xB1,0x78,0xC2,0xF5,0x68,0xA7,0x03,0xB2}};
static const guid idClient2={0x726778CD,0xF60A,0x4EDA,{0x82,0xDE,0xE4,0x76,0x10,0xCD,0x78,0xAA}};
#define METHOD(object,index) ((*(void***)(object))[index])
typedef hr_t (__stdcall *initialize_t)(void* self,u32 mode,u32 flags,long long duration,long long period,const void* format,const void* session);
static initialize_t original;
static hr_t __stdcall Initialize(void* self,u32 mode,u32 flags,long long duration,long long period,const void* format,const void* session) {
    void* client2=0;
    if(((hr_t(__stdcall*)(void*,const void*,void**))METHOD(self,0))(self,&idClient2,&client2)>=0 && client2) {
        const u32 properties[4]={16,0,4,0};   /* AudioClientProperties: size, not offloaded, AudioCategory_Alerts, no options */
        ((hr_t(__stdcall*)(void*,const void*))METHOD(client2,16))(client2,properties);   /* IAudioClient2::SetClientProperties */
        ((u32(__stdcall*)(void*))METHOD(client2,2))(client2);
    }
    return original(self,mode,flags,duration,period,format,session);
}
__declspec(dllexport) long CharonPeAudio(void) {
    if(original)return 0;
    void* enumerator=0;void* device=0;void* client=0;
    CoInitializeEx(0,0);   /* already initialized on NVDA's main thread: either answer is fine */
    hr_t r=CoCreateInstance(&classEnumerator,0,23,&idEnumerator,&enumerator);
    if(r>=0)r=((hr_t(__stdcall*)(void*,int,int,void**))METHOD(enumerator,4))(enumerator,0,0,&device);   /* default render endpoint, console */
    if(r>=0)r=((hr_t(__stdcall*)(void*,const void*,u32,void*,void**))METHOD(device,3))(device,&idClient,23,0,&client);
    if(r>=0) {
        void** slot=&METHOD(client,3);u32 old=0;   /* IAudioClient::Initialize */
        if(VirtualProtect(slot,sizeof(void*),4,&old)) {   /* PAGE_READWRITE */
            original=(initialize_t)*slot;*slot=(void*)Initialize;
            VirtualProtect(slot,sizeof(void*),old,&old);
        } else r=(hr_t)0x80070005;   /* E_ACCESSDENIED */
    }
    if(client)((u32(__stdcall*)(void*))METHOD(client,2))(client);
    if(device)((u32(__stdcall*)(void*))METHOD(device,2))(device);
    if(enumerator)((u32(__stdcall*)(void*))METHOD(enumerator,2))(enumerator);
    return r>=0?0:r;
}
int _DllMainCRTStartup(void* instance,unsigned long reason,void* reserved) {(void)instance;(void)reason;(void)reserved;return 1;}
