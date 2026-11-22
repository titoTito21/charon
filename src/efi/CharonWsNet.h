#pragma once
// r400: Charon workspace - network. Lists the network adapters the firmware
// drives (Ethernet and Wi-Fi), brings one up with DHCP (IP4Config2), scans and
// joins Wi-Fi networks (Wireless MAC Connection II + Supplicant, WPA2-PSK) and
// downloads a file over HTTP (firmware HTTP protocol) into a folder, so
// packages can be fetched from a network. Everything goes through firmware
// protocols; a computer whose firmware has no driver for its adapter shows no
// adapter here. Wi-Fi could not be exercised in QEMU (no Wi-Fi model) - it is
// written to the UEFI specification 2.10 section 11.3 and marked untested.
// GPL-2.0-or-later.
#include "CharonWsCore.h"
#include "CharonTrace.h"   // r408
namespace hakefi {
namespace ws {
namespace net {
inline efi_guid snpGuid={0xA19832B9,0xAC25,0x11D3,{0x9A,0x2D,0x00,0x90,0x27,0x3F,0xC1,0x4D}};
inline efi_guid ip4Config2Guid={0x5b446ed1,0xe30b,0x4faa,{0x87,0x1a,0x36,0x54,0xec,0xa3,0x60,0x80}};
inline efi_guid httpBindingGuid={0xbdc8e6af,0xd9bc,0x4379,{0xa7,0x2a,0xe0,0xc4,0xe7,0x5d,0xae,0x1c}};
inline efi_guid httpGuid={0x7a59b29b,0x910b,0x4171,{0x82,0x42,0xa8,0x5a,0x0d,0xf2,0x5b,0x5b}};
inline efi_guid wifi2Guid={0x1b0fb9bf,0x699d,0x4fdd,{0xa7,0xc3,0x25,0x46,0x68,0x1b,0xf6,0x3b}};
inline efi_guid supplicantGuid={0x54fcc43e,0xaa89,0x4333,{0x9a,0x85,0xcd,0xea,0x24,0x05,0x1e,0x9e}};
inline efi_guid nii31Guid={0x1ACED566,0x76ED,0x4218,{0xBC,0x81,0x76,0x7F,0x1F,0x97,0x7A,0x89}};   // Network Interface Identifier (UNDI 3.1)
inline efi_guid rngGuid={0x3152bca5,0xeade,0x433d,{0x86,0x2e,0xc0,0x1c,0xdc,0x29,0x1f,0x44}};     // EFI_RNG_PROTOCOL
inline efi_guid dpcGuid={0x480f8ae9,0x0c46,0x4aa9,{0xbc,0x89,0xdb,0x9f,0xba,0x61,0x98,0x06}};     // edk2 DPC protocol (DpcDxe)
struct SnpMode;   // read by offset (CurrentAddress 552, MediaPresentSupported 651, MediaPresent 652)
struct Snp {
    uint64_t Revision;void* Start;void* Stop;void* Initialize;void* Reset;void* Shutdown;void* ReceiveFilters;void* StationAddress;void* Statistics;
    void* MCastIpToMac;void* NvData;
    efi_status (EFIAPI *GetStatus)(Snp*,uint32_t* interruptStatus,void** txBuffer);
    void* Transmit;void* Receive;efi_event WaitForPacket;uint8_t* Mode;
};
struct Ip4Config2 {
    efi_status (EFIAPI *SetData)(Ip4Config2*,uint32_t type,size_t size,void* data);
    efi_status (EFIAPI *GetData)(Ip4Config2*,uint32_t type,size_t* size,void* data);
    void* RegisterDataNotify;void* UnregisterDataNotify;
};
struct ServiceBinding {
    efi_status (EFIAPI *CreateChild)(ServiceBinding*,efi_handle* child);
    efi_status (EFIAPI *DestroyChild)(ServiceBinding*,efi_handle child);
};
struct HttpAccessPoint4 {bool UseDefaultAddress;uint8_t LocalAddress[4];uint8_t LocalSubnet[4];uint16_t LocalPort;};
struct HttpConfig {uint32_t HttpVersion;uint32_t TimeOutMillisec;bool LocalAddressIsIPv6;HttpAccessPoint4* IPv4Node;};
struct HttpRequest {uint32_t Method;char16_t* Url;};
struct HttpResponse {uint32_t StatusCode;};
struct HttpHeader {char* FieldName;char* FieldValue;};
struct HttpMessage {void* Data;size_t HeaderCount;HttpHeader* Headers;size_t BodyLength;void* Body;};
struct HttpToken {efi_event Event;efi_status Status;HttpMessage* Message;};
struct Http {
    void* GetModeData;
    efi_status (EFIAPI *Configure)(Http*,HttpConfig*);
    efi_status (EFIAPI *Request)(Http*,HttpToken*);
    efi_status (EFIAPI *Cancel)(Http*,HttpToken*);
    efi_status (EFIAPI *Response)(Http*,HttpToken*);
    efi_status (EFIAPI *Poll)(Http*);
};
// ---- Wi-Fi (UEFI 2.10, 11.3 EFI Wireless MAC Connection II) ----
struct Ssid {uint8_t length;uint8_t name[32];};
struct SuiteSelector {uint8_t oui[3];uint8_t type;};
struct SuiteList {uint16_t count;SuiteSelector list[1];};
struct Network80211 {uint32_t bssType;Ssid ssid;SuiteList* akm;SuiteList* cipher;};
struct NetworkDescription {Network80211 network;uint8_t quality;};
struct NetworksResult {uint8_t count;NetworkDescription networks[1];};
struct NetworksData {uint32_t ssidCount;Ssid ssids[1];};
// EFI_80211_GET_NETWORKS_TOKEN (edk2 MdePkg Protocol/WiFi2.h) has NO result code; only the connect token
// has one. With a ResultCode member here the firmware's Result pointer landed in it and every scan "failed" (r407).
struct NetworksToken {efi_event Event;efi_status Status;NetworksData* Data;NetworksResult* Result;};
static_assert(sizeof(NetworksToken)==32,"EFI_80211_GET_NETWORKS_TOKEN layout");
struct ConnectData {Network80211* Network;uint32_t FailureTimeout;};
struct ConnectToken {efi_event Event;efi_status Status;ConnectData* Data;uint32_t ResultCode;};
struct Wifi2 {
    efi_status (EFIAPI *GetNetworks)(Wifi2*,NetworksToken*);
    efi_status (EFIAPI *ConnectNetwork)(Wifi2*,ConnectToken*);
    efi_status (EFIAPI *DisconnectNetwork)(Wifi2*,void*);
};
struct Supplicant {
    void* BuildResponsePacket;void* ProcessPacket;
    efi_status (EFIAPI *SetData)(Supplicant*,uint32_t type,void* data,size_t size);
    void* GetData;
};
static_assert(sizeof(Network80211)==56 && sizeof(NetworkDescription)==64,"802.11 structure layout");
constexpr uint32_t supplicantAkm=0,supplicantPairwise=2,supplicantPassword=3,supplicantSsid=4;
// ---- adapters ----
constexpr size_t maxAdapters=16,maxNetworks=32;
struct Adapter {efi_handle handle;Snp* snp;Ip4Config2* ip;Wifi2* wifi;uint8_t mac[6];};
struct WifiNetwork {Ssid ssid;uint8_t quality;bool psk,open,enterprise;SuiteSelector akm,cipher;bool hasCipher;uint32_t bssType;};
struct State {
    Adapter adapters[maxAdapters];size_t count;ListView view;
    WifiNetwork networks[maxNetworks];size_t networkCount;ListView wifiView;size_t wifiAdapter;bool inWifi;
    char16_t url[300];
};
inline bool SameBytes(const uint8_t* a,const uint8_t* b,size_t n) {for(size_t i=0;i<n;++i)if(a[i]!=b[i])return false;return true;}
inline State& N() {static State state;return state;}
inline volatile bool& Signalled() {static volatile bool flag;return flag;}
inline void EFIAPI OnSignal(efi_event,void*) {Signalled()=true;}
inline void LoadAdapters() {
    auto& n=N();auto* boot=U().host->boot;n.count=0;efi_handle* handles=nullptr;size_t count=0;
    if(boot->LocateHandleBuffer(ByProtocol,&snpGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles) {n.view.count=0;return;}
    for(size_t i=0;i<count && n.count<maxAdapters;++i) {
        Snp* snp=nullptr;if(boot->HandleProtocol(handles[i],&snpGuid,(void**)&snp)!=EFI_SUCCESS || !snp || !snp->Mode)continue;
        // The same adapter appears on child handles (IPv4, IPv6 ...): keep the one that can be configured.
        Ip4Config2* ip=nullptr;boot->HandleProtocol(handles[i],&ip4Config2Guid,(void**)&ip);
        bool known=false;
        for(size_t k=0;k<n.count;++k)if(SameBytes(n.adapters[k].mac,snp->Mode+552,6)) {known=true;if(ip && !n.adapters[k].ip) {n.adapters[k].ip=ip;n.adapters[k].handle=handles[i];n.adapters[k].snp=snp;}}
        if(known)continue;
        auto& a=n.adapters[n.count++];a.handle=handles[i];a.snp=snp;a.ip=ip;a.wifi=nullptr;memcpy(a.mac,snp->Mode+552,6);
        boot->HandleProtocol(handles[i],&wifi2Guid,(void**)&a.wifi);
    }
    boot->FreePool(handles);n.view.count=n.count;if(n.view.index>=n.count)n.view.index=0;
}
inline bool LinkUp(Adapter& a,bool& known) {
    known=a.snp->Mode[651]!=0;
    if(a.snp->GetStatus && *reinterpret_cast<uint32_t*>(a.snp->Mode)==2) {uint32_t irq=0;a.snp->GetStatus(a.snp,&irq,nullptr);}   // refreshes MediaPresent when started
    return a.snp->Mode[652]!=0;
}
struct IpInfo {uint8_t address[4],mask[4],gateway[4],dns[4];bool dhcp,hasGateway,hasDns;};
inline bool ReadIp(Adapter& a,IpInfo& info) {
    memset(&info,0,sizeof(info));if(!a.ip)return false;
    alignas(8) static uint8_t buffer[2048];size_t size=sizeof(buffer);
    if(a.ip->GetData(a.ip,0,&size,buffer)!=EFI_SUCCESS || size<112)return false;
    memcpy(info.address,buffer+104,4);memcpy(info.mask,buffer+108,4);
    uint32_t policy=0;size=4;if(a.ip->GetData(a.ip,1,&size,&policy)==EFI_SUCCESS)info.dhcp=policy==1;
    size=sizeof(buffer);if(a.ip->GetData(a.ip,3,&size,buffer)==EFI_SUCCESS && size>=4) {memcpy(info.gateway,buffer,4);info.hasGateway=true;}
    size=sizeof(buffer);if(a.ip->GetData(a.ip,4,&size,buffer)==EFI_SUCCESS && size>=4) {memcpy(info.dns,buffer,4);info.hasDns=true;}
    return info.address[0]!=0;
}
inline void CatIp(char16_t* out,size_t cap,const uint8_t* ip) {for(int i=0;i<4;++i) {if(i)Cat(out,cap,u".");CatN(out,cap,ip[i]);}}
inline void AdapterLabel(size_t row,char16_t* out,size_t cap,bool spoken,void*) {
    auto& n=N();if(row>=n.count)return;
    auto& a=n.adapters[row];out[0]=0;bool known=false;const bool up=LinkUp(a,known);IpInfo info;const bool address=ReadIp(a,info);
    Cat(out,cap,a.wifi?u"Wi-Fi":u"Ethernet");
    if(n.count>1) {Cat(out,cap,u" ");CatN(out,cap,row+1);}
    if(address) {Cat(out,cap,T(u", połączony, adres ",u", connected, address "));CatIp(out,cap,info.address);}
    else if(a.wifi)Cat(out,cap,T(u", niepołączony",u", not connected"));
    else if(known)Cat(out,cap,up?T(u", kabel podłączony, bez adresu",u", cable connected, no address"):T(u", kabel odłączony",u", cable unplugged"));
    else Cat(out,cap,T(u", bez adresu",u", no address"));
    if(!spoken) {
        static const char16_t hex[]=u"0123456789ABCDEF";Cat(out,cap,u"   ");
        for(int i=0;i<6;++i) {if(i)CatC(out,cap,u':');CatC(out,cap,hex[a.mac[i]>>4]);CatC(out,cap,hex[a.mac[i]&15]);}
    }
}
inline void AdapterDetail(size_t index) {
    auto& n=N();if(index>=n.count)return;
    auto& a=n.adapters[index];static char16_t text[700];AdapterLabel(index,text,400,true,nullptr);IpInfo info;
    if(ReadIp(a,info)) {
        Cat(text,700,T(u". Maska ",u". Mask "));CatIp(text,700,info.mask);
        if(info.hasGateway) {Cat(text,700,T(u". Brama ",u". Gateway "));CatIp(text,700,info.gateway);}
        if(info.hasDns) {Cat(text,700,u". DNS ");CatIp(text,700,info.dns);}
        Cat(text,700,info.dhcp?T(u". Adres z DHCP",u". Address from DHCP"):T(u". Adres stały",u". Static address"));
    }
    Cat(text,700,T(u". Adres sprzętowy ",u". Hardware address "));
    static const char16_t hex[]=u"0123456789ABCDEF";
    for(int i=0;i<6;++i) {CatC(text,700,hex[a.mac[i]>>4]);CatC(text,700,hex[a.mac[i]&15]);Cat(text,700,u" ");}
    if(!a.ip)Cat(text,700,T(u". Firmware nie ma dla niej obsługi IPv4.",u". The firmware has no IPv4 support for it."));
    Say(text);
}
// DHCP on one adapter: sets the policy and waits for an address (rising ticks).
inline bool Dhcp(Adapter& a) {
    if(!a.ip) {Fail(T(u"Firmware nie ma obsługi IPv4 dla tej karty.",u"The firmware has no IPv4 support for this adapter."));return false;}
    IpInfo info;
    if(ReadIp(a,info)) {static char16_t text[200];Set(text,200,T(u"Już połączony, adres ",u"Already connected, address "));CatIp(text,200,info.address);Say(text);return true;}
    uint32_t policy=1;const auto status=a.ip->SetData(a.ip,1,4,&policy);
    if(status!=EFI_SUCCESS && status!=EFI_ALREADY_STARTED && status!=EFI_NOT_READY) {
        // Already DHCP but stuck: back to static and again, as firmware setup does.
        policy=0;a.ip->SetData(a.ip,1,4,&policy);policy=1;
        const auto again=a.ip->SetData(a.ip,1,4,&policy);
        if(again!=EFI_SUCCESS && again!=EFI_NOT_READY) {Fail(T(u"Nie uruchomiono DHCP.",u"DHCP could not be started."),again);return false;}
    }
    Say(T(u"Pobieram adres z DHCP. Escape przerywa.",u"Getting an address from DHCP. Escape stops."));
    for(unsigned tick=0;tick<300;++tick) {   // 30 s
        U().host->boot->Stall(100000);
        if(tick%10==0)Tone(500+tick*2,25);
        if(EscapePressed()) {Say(T(u"Przerwano",u"Stopped"));return false;}
        if(ReadIp(a,info)) {
            static char16_t text[300];Set(text,300,T(u"Połączono. Adres ",u"Connected. Address "));CatIp(text,300,info.address);
            if(info.hasGateway) {Cat(text,300,T(u", brama ",u", gateway "));CatIp(text,300,info.gateway);}
            Play(Icon::done);Say(text);return true;}
    }
    Fail(T(u"Serwer DHCP nie odpowiedział w 30 sekund.",u"No DHCP answer in 30 seconds."));return false;
}
// Waits for an event-driven operation; false on Escape or timeout.
inline bool WaitSignal(unsigned seconds,Http* http=nullptr) {
    for(unsigned tick=0;tick<seconds*100;++tick) {
        if(Signalled())return true;
        if(http)http->Poll(http);
        U().host->boot->Stall(10000);
        if(tick%100==50)Tone(600,20);
        if(tick%10==0 && EscapePressed())return false;
    }
    return Signalled();
}
// ---- extra drivers ----
// Firmware of many computers (and laptops in particular) drives no network
// adapter outside PXE, or has no HTTP. Charon can load UEFI drivers kept in
// \EFI\HAK\drivers on ANY volume: "nic-*.efi" and "wifi-*.efi" (adapter
// drivers) when no adapter is driven, "net-*.efi" (the IPv4 stack, in name
// order) when adapters have no IPv4 service. Nothing is loaded when the
// firmware already provides the layer, so firmware drivers are never doubled.
inline bool HasProtocol(efi_guid& guid) {
    efi_handle* handles=nullptr;size_t count=0;
    const bool any=U().host->boot->LocateHandleBuffer(ByProtocol,&guid,nullptr,&count,&handles)==EFI_SUCCESS && handles && count;
    if(handles)U().host->boot->FreePool(handles);
    return any;
}
inline void ConnectEverything() {
    auto* boot=U().host->boot;efi_handle* handles=nullptr;size_t count=0;
    if(boot->LocateHandleBuffer(AllHandles,nullptr,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return;
    for(size_t i=0;i<count;++i) {boot->ConnectController(handles[i],nullptr,nullptr,true);if(i%32==0)Pump();}
    boot->FreePool(handles);
}
// r408: a driver file name is started once per run, whichever volume it came from (Charon's disk and its RAM
// copy both hold the folder) and however often the user asks; the image stays resident either way.
inline bool DriverStarted(const char16_t* name,bool remember) {
    static char16_t names[128][64];static size_t count=0;
    for(size_t i=0;i<count;++i)if(SameFold(names[i],name))return true;
    if(remember && count<128 && Len(name)<64)Set(names[count++],64,name);
    return false;
}
inline size_t LoadDriverFiles(const char16_t* prefix,const char16_t* prefix2,size_t* failed,const char16_t* skip=nullptr) {
    auto* boot=U().host->boot;size_t loaded=0;efi_handle volumes[32];const size_t n=Volumes(volumes,32);static Listing list;
    for(size_t v=0;v<n;++v) {
        FileRef folder;folder.volume=volumes[v];Set(folder.path,pathUnits,u"\\EFI\\HAK\\drivers");
        if(ReadFolder(folder,list)!=EFI_SUCCESS)continue;
        for(size_t i=0;i<list.count;++i) {   // sorted by name
            const auto& e=list.entries[i];
            if(e.directory || !EndsFold(e.name,u".efi") || !(StartsFold(e.name,prefix) || (prefix2 && StartsFold(e.name,prefix2))))continue;
            if(skip && StartsFold(e.name,skip))continue;
            if(DriverStarted(e.name,false))continue;   // r408
            static char16_t path[pathUnits];if(!JoinPath(path,pathUnits,folder.path,e.name))continue;
            size_t device=0;uint8_t* own=nullptr;
            if(boot->HandleProtocol(volumes[v],&media::pathGuid,(void**)&own)!=EFI_SUCCESS || !own || (device=media::PathLength(own))==SIZE_MAX || device>1500)continue;
            const size_t units=Len(path)+1,node=4+units*2;
            uint8_t* full=static_cast<uint8_t*>(Allocate(device+node+4));if(!full)continue;
            memcpy(full,own,device);uint8_t* f=full+device;f[0]=4;f[1]=4;f[2]=uint8_t(node);f[3]=uint8_t(node>>8);
            for(size_t k=0;k<units;++k) {f[4+k*2]=uint8_t(path[k]);f[5+k*2]=uint8_t(path[k]>>8);}
            f[node]=0x7f;f[node+1]=0xff;f[node+2]=4;f[node+3]=0;
            efi_handle image=nullptr;
            auto status=boot->LoadImage(false,U().host->image,reinterpret_cast<efi_device_path_protocol*>(full),nullptr,0,&image);
            Release(full);
            if(status==EFI_SUCCESS && image) {size_t exitSize=0;char16_t* exitData=nullptr;status=boot->StartImage(image,&exitSize,&exitData);}
            DriverStarted(e.name,true);   // r408: also a file that failed is not tried again
            charontrace::Text(status==EFI_SUCCESS?"CHARON net driver file started":"CHARON net driver file failed");
            if(status==EFI_SUCCESS)++loaded;else if(failed)++*failed;
            Tone(700+unsigned(loaded)*40,25);Pump();
        }
    }
    return loaded;
}
// Loads what is missing. Returns the number of drivers started.
inline size_t LoadDrivers(bool announce) {
    size_t loaded=0,failed=0;
    // Driver images stay resident, so without being asked (announce) each kind is
    // loaded once per session, not again on every visit while its hardware is absent.
    static bool nicTried=false,wifiTried=false;
    const bool noAdapter=!HasProtocol(snpGuid);
    if((noAdapter && (announce || !nicTried)) || (!noAdapter && !HasProtocol(wifi2Guid) && (announce || !wifiTried))) {
        const size_t got=LoadDriverFiles(noAdapter?u"nic-":u"wifi-",u"wifi-",&failed);
        if(noAdapter)nicTried=true;
        wifiTried=true;
        if(got) {
            loaded+=got;ConnectEverything();
            // A USB adapter that nic-usb-29-cdc-select.efi moves to its CDC configuration gets its
            // new interface handles from that driver's timer, a moment after the connect pass.
            if(noAdapter && !HasProtocol(snpGuid) && !HasProtocol(nii31Guid)) {
                for(unsigned i=0;i<8;++i) {U().host->boot->Stall(100000);Pump();}
                ConnectEverything();
            }
        }
    }
    // An UNDI driver (Intel's, Realtek's) gives only the Network Interface Identifier
    // protocol; the Simple Network Protocol above it comes from SnpDxe, a net-* file.
    bool noRandom=false;static bool noRandomSaid=false;
    if((HasProtocol(snpGuid) || HasProtocol(nii31Guid)) && !HasProtocol(ip4Config2Guid)) {
        // The pack's stack modules are a debug build of edk2 and stop the machine in an assertion:
        // Ip4Dxe when the firmware has no random number protocol (DxeNetLib), DpcDxe when the firmware
        // already has the DPC protocol (seen in QEMU/OVMF, r407). So: no RNG, no stack; DPC there, no net-10.
        if(!HasProtocol(rngGuid))noRandom=true;
        else {
            const size_t got=LoadDriverFiles(u"net-",nullptr,&failed,HasProtocol(dpcGuid)?u"net-10-":nullptr);
            if(got) {loaded+=got;ConnectEverything();}
        }
    }
    const bool tellRandom=noRandom && (announce || !noRandomSaid);
    if(announce || loaded || failed || tellRandom) {
        static char16_t text[300];
        if(!loaded && !failed && tellRandom)text[0]=0;
        else if(!loaded && !failed)Set(text,300,HasProtocol(snpGuid) && HasProtocol(ip4Config2Guid)?T(u"Firmware ma już sterowniki sieci; nic nie wczytano.",u"The firmware already has network drivers; nothing was loaded.")
            :T(u"Brak pasujących sterowników w folderze EFI, HAK, drivers.",u"No matching drivers in the folder EFI, HAK, drivers."));
        else {Set(text,300,T(u"Wczytano sterowników: ",u"Drivers loaded: "));CatN(text,300,loaded);
            if(failed) {Cat(text,300,T(u", nieudanych: ",u", failed: "));CatN(text,300,failed);}
            if(tellRandom)Cat(text,300,u". ");}
        if(tellRandom) {noRandomSaid=true;Cat(text,300,T(u"Firmware nie ma źródła liczb losowych, którego wymagają moduły IPv4; nie wczytano ich.",u"The firmware has no random number source, which the IPv4 modules need; they were not loaded."));}
        Say(text);
    }
    return loaded;
}
// ---- Wi-Fi ----
inline void SsidText(const Ssid& ssid,char16_t* out,size_t cap) {
    static uint8_t bytes[33];const size_t n=ssid.length<=32?ssid.length:32;memcpy(bytes,ssid.name,n);
    Utf8ToUtf16(bytes,n,out,cap);if(!out[0])Set(out,cap,T(u"sieć ukryta",u"hidden network"));
}
inline void WifiLabel(size_t row,char16_t* out,size_t cap,bool spoken,void*) {
    auto& n=N();if(row>=n.networkCount)return;
    const auto& w=n.networks[row];SsidText(w.ssid,out,cap);
    Cat(out,cap,spoken?T(u", sygnał ",u", signal "):u"   ");CatN(out,cap,w.quality>100?100:w.quality);Cat(out,cap,spoken?T(u" procent",u" percent"):u"%");
    Cat(out,cap,w.open?T(u", otwarta",u", open"):w.psk?T(u", z hasłem",u", password"):w.enterprise?T(u", firmowa, nieobsługiwana",u", enterprise, not supported"):T(u", zabezpieczona",u", secured"));
}
inline bool WifiScan(size_t adapter) {
    auto& n=N();auto* boot=U().host->boot;n.networkCount=0;n.wifiAdapter=adapter;
    Wifi2* wifi=n.adapters[adapter].wifi;if(!wifi)return false;
    static NetworksData data;memset(&data,0,sizeof(data));
    NetworksToken token{};token.Data=&data;Signalled()=false;
    if(boot->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,OnSignal,nullptr,&token.Event)!=EFI_SUCCESS)return false;
    Say(T(u"Szukam sieci Wi-Fi.",u"Scanning for Wi-Fi networks."));
    auto status=wifi->GetNetworks(wifi,&token);
    const bool done=status==EFI_SUCCESS && WaitSignal(30);
    boot->CloseEvent(token.Event);
    if(!done || token.Status!=EFI_SUCCESS || !token.Result) {Fail(T(u"Nie udało się wyszukać sieci.",u"The scan failed."),status!=EFI_SUCCESS?status:token.Status);return false;}
    for(size_t i=0;i<token.Result->count && n.networkCount<maxNetworks;++i) {
        const auto& d=token.Result->networks[i];auto& w=n.networks[n.networkCount];memset(&w,0,sizeof(w));
        w.ssid=d.network.ssid;w.quality=d.quality;w.bssType=d.network.bssType;
        w.open=!d.network.akm || !d.network.akm->count;
        for(size_t k=0;d.network.akm && k<d.network.akm->count && k<8;++k) {
            const auto& s=d.network.akm->list[k];
            if(s.type==2 || s.type==6) {if(!w.psk)w.akm=s;w.psk=true;}   // PSK, PSK-SHA256
            else if(s.type==1 || s.type==5)w.enterprise=true;
        }
        if(d.network.cipher && d.network.cipher->count) {
            w.hasCipher=true;w.cipher=d.network.cipher->list[0];
            for(size_t k=0;k<d.network.cipher->count && k<8;++k)if(d.network.cipher->list[k].type==4)w.cipher=d.network.cipher->list[k];   // prefer CCMP
        }
        // The same name once (the strongest access point).
        bool merged=false;
        for(size_t k=0;k<n.networkCount;++k)if(n.networks[k].ssid.length==w.ssid.length && SameBytes(n.networks[k].ssid.name,w.ssid.name,w.ssid.length)) {
            if(w.quality>n.networks[k].quality)n.networks[k]=w;
            merged=true;break;}
        if(!merged)++n.networkCount;
    }
    boot->FreePool(token.Result);
    // Strongest first.
    for(size_t i=1;i<n.networkCount;++i) {WifiNetwork moving=n.networks[i];size_t j=i;while(j && n.networks[j-1].quality<moving.quality) {n.networks[j]=n.networks[j-1];--j;}n.networks[j]=moving;}
    n.wifiView.count=n.networkCount;n.wifiView.index=0;n.wifiView.top=0;
    return true;
}
inline bool WifiJoin(size_t index) {
    auto& n=N();auto* boot=U().host->boot;if(index>=n.networkCount)return false;
    auto& w=n.networks[index];auto& a=n.adapters[n.wifiAdapter];
    if(w.enterprise && !w.psk) {Fail(T(u"Sieci firmowe z logowaniem 802.1X nie są obsługiwane.",u"Enterprise networks with 802.1X sign-in are not supported."));return false;}
    Supplicant* supplicant=nullptr;boot->HandleProtocol(a.handle,&supplicantGuid,(void**)&supplicant);
    static char16_t password[70];static char ascii[70];password[0]=0;
    if(!w.open) {
        if(!supplicant || !supplicant->SetData) {Fail(T(u"Firmware nie ma modułu haseł Wi-Fi dla tej karty.",u"The firmware has no Wi-Fi password module for this adapter."));return false;}
        if(!Prompt(T(u"Hasło sieci:",u"Network password:"),password,64) || Len(password)<8) {
            if(password[0])Fail(T(u"Hasło ma co najmniej 8 znaków.",u"A password has at least 8 characters."));
            return false;}
        size_t k=0;for(;password[k] && k<64;++k)ascii[k]=char(password[k]);
        ascii[k]=0;
        static Ssid target;target=w.ssid;
        static SuiteList akm,cipher;akm.count=1;akm.list[0]=w.akm;cipher.count=1;cipher.list[0]=w.cipher;
        supplicant->SetData(supplicant,supplicantSsid,&target,sizeof(target));
        supplicant->SetData(supplicant,supplicantAkm,&akm,sizeof(akm));
        if(w.hasCipher)supplicant->SetData(supplicant,supplicantPairwise,&cipher,sizeof(cipher));
        const auto set=supplicant->SetData(supplicant,supplicantPassword,ascii,k+1);
        memset(password,0,sizeof(password));
        if(set!=EFI_SUCCESS) {memset(ascii,0,sizeof(ascii));Fail(T(u"Firmware nie przyjęło hasła.",u"The firmware did not accept the password."),set);return false;}
    }
    static Network80211 network;static SuiteList akmList,cipherList;memset(&network,0,sizeof(network));
    network.bssType=w.bssType;network.ssid=w.ssid;
    if(!w.open) {akmList.count=1;akmList.list[0]=w.akm;network.akm=&akmList;if(w.hasCipher) {cipherList.count=1;cipherList.list[0]=w.cipher;network.cipher=&cipherList;}}
    static ConnectData data;data.Network=&network;data.FailureTimeout=20;
    ConnectToken token{};token.Data=&data;Signalled()=false;
    if(boot->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,OnSignal,nullptr,&token.Event)!=EFI_SUCCESS)return false;
    Say(T(u"Łączę się z siecią.",u"Connecting to the network."));
    const auto status=a.wifi->ConnectNetwork(a.wifi,&token);
    const bool done=status==EFI_SUCCESS && WaitSignal(30);
    boot->CloseEvent(token.Event);memset(ascii,0,sizeof(ascii));
    if(!done || token.Status!=EFI_SUCCESS || token.ResultCode!=0) {
        Fail((token.ResultCode==1 || token.ResultCode==2)?T(u"Sieć odrzuciła połączenie; sprawdź hasło.",u"The network refused; check the password."):T(u"Nie połączono z siecią.",u"Not connected."),status!=EFI_SUCCESS?status:token.Status);
        return false;}
    return Dhcp(a);
}
// ---- HTTP download ----
// GET `url` into the file `target`. Plain HTTP; HTTPS works only when the
// firmware has TLS and certificates set up (the error says so otherwise).
// With `memory` the body is kept in pages instead (memory->extra more bytes are
// reserved after it) and `target` is not used: nothing touches a disk.
inline bool Download(const char16_t* url,const FileRef& target,uint64_t* received,Memory* memory=nullptr,const char16_t* what=nullptr) {
    auto& n=N();auto* boot=U().host->boot;*received=0;
    efi_handle* handles=nullptr;size_t count=0;
    if(boot->LocateHandleBuffer(ByProtocol,&httpBindingGuid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles || !count) {
        Fail(T(u"Firmware nie obsługuje HTTP.",u"The firmware does not support HTTP."));return false;}
    // The adapter that has an address.
    efi_handle chosen=handles[0];
    for(size_t i=0;i<count;++i)for(size_t k=0;k<n.count;++k) {IpInfo info;if(handles[i]==n.adapters[k].handle && ReadIp(n.adapters[k],info))chosen=handles[i];}
    boot->FreePool(handles);
    ServiceBinding* binding=nullptr;efi_handle child=nullptr;Http* http=nullptr;
    if(boot->HandleProtocol(chosen,&httpBindingGuid,(void**)&binding)!=EFI_SUCCESS || !binding || binding->CreateChild(binding,&child)!=EFI_SUCCESS || !child
        || boot->HandleProtocol(child,&httpGuid,(void**)&http)!=EFI_SUCCESS || !http) {
        if(binding && child)binding->DestroyChild(binding,child);
        Fail(T(u"Nie udało się uruchomić HTTP.",u"HTTP could not be started."));return false;}
    static HttpAccessPoint4 point;memset(&point,0,sizeof(point));point.UseDefaultAddress=true;
    HttpConfig config{};config.HttpVersion=1;config.TimeOutMillisec=15000;config.IPv4Node=&point;
    bool ok=false;const char16_t* why=nullptr;efi_status status=http->Configure(http,&config);
    efi_file_protocol* file=nullptr;efi_event event=nullptr;
    static char host[200];static uint8_t block[32768];
    do {
        if(status!=EFI_SUCCESS) {why=T(u"Sieć nie jest gotowa; najpierw połącz kartę sieciową.",u"The network is not ready; connect a network adapter first.");break;}
        // Host header from the URL.
        size_t at=0;while(url[at] && !(url[at]==u'/' && url[at+1]==u'/'))++at;
        if(!url[at]) {why=T(u"Adres musi zaczynać się od http://",u"The address must start with http://");break;}
        at+=2;size_t h=0;while(url[at] && url[at]!=u'/' && h<198)host[h++]=char(url[at++]);
        host[h]=0;
        static char nameHost[]="Host",nameAgent[]="User-Agent",agent[]="Charon",nameAccept[]="Accept",accept[]="*/*",nameEncoding[]="Accept-Encoding",identity[]="identity";
        HttpHeader headers[4]={{nameHost,host},{nameAgent,agent},{nameAccept,accept},{nameEncoding,identity}};
        HttpRequest request{0,const_cast<char16_t*>(url)};HttpMessage message{};message.Data=&request;message.HeaderCount=4;message.Headers=headers;
        HttpToken token{};token.Message=&message;
        if(boot->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,OnSignal,nullptr,&event)!=EFI_SUCCESS) {why=T(u"Za mało pamięci RAM.",u"Not enough RAM.");break;}
        token.Event=event;Signalled()=false;
        status=http->Request(http,&token);
        if(status!=EFI_SUCCESS || !WaitSignal(30,http) || token.Status!=EFI_SUCCESS) {
            if(status==EFI_SUCCESS)status=token.Status;
            http->Cancel(http,nullptr);
            why=(url[4]==u's' || url[4]==u'S')?T(u"Nie udało się połączyć. HTTPS wymaga certyfikatów w firmware; spróbuj adresu http.",u"Could not connect. HTTPS needs certificates in the firmware; try an http address.")
                :T(u"Serwer nie odpowiada.",u"The server does not answer.");break;}
        // Response: headers with the first part of the body, then the rest.
        HttpResponse response{};HttpMessage answer{};answer.Data=&response;answer.BodyLength=sizeof(block);answer.Body=block;
        HttpToken rx{};rx.Event=event;rx.Message=&answer;Signalled()=false;
        status=http->Response(http,&rx);
        if(status!=EFI_SUCCESS || !WaitSignal(30,http) || rx.Status!=EFI_SUCCESS) {if(status==EFI_SUCCESS)status=rx.Status;http->Cancel(http,nullptr);why=T(u"Brak odpowiedzi serwera.",u"No answer from the server.");break;}
        if(response.StatusCode!=3) {   // HTTP_STATUS_200_OK
            static char16_t text[160];Set(text,160,response.StatusCode==21?T(u"Na serwerze nie ma takiego pliku.",u"There is no such file on the server.")
                :T(u"Serwer odmówił; kod stanu UEFI ",u"The server refused; UEFI status code "));
            if(response.StatusCode!=21)CatN(text,160,response.StatusCode);
            why=text;status=EFI_SUCCESS;break;}
        uint64_t length=UINT64_MAX;bool chunked=false;
        for(size_t i=0;i<answer.HeaderCount && answer.Headers;++i) {
            const char* name=answer.Headers[i].FieldName;const char* value=answer.Headers[i].FieldValue;if(!name || !value)continue;
            auto same=[&](const char* want) {size_t k=0;for(;want[k];++k) {char c=name[k];if(c>='A' && c<='Z')c=char(c+32);if(c!=want[k])return false;}return !name[k];};
            if(same("content-length")) {length=0;for(const char* d=value;*d>='0' && *d<='9';++d)length=length*10+uint64_t(*d-'0');}
            if(same("transfer-encoding") && (value[0]=='c' || value[0]=='C'))chunked=true;
        }
        if(answer.Headers)boot->FreePool(answer.Headers);
        if(chunked || length==UINT64_MAX) {why=T(u"Serwer nie podał rozmiaru pliku; takie pobieranie nie jest obsługiwane.",u"The server did not give the file size; such a download is not supported.");status=EFI_SUCCESS;break;}
        if(memory) {
            if(!Reserve(*memory,size_t(length)+memory->extra+4096)) {why=T(u"Za mało pamięci RAM na ten plik.",u"Not enough RAM for this file.");status=EFI_SUCCESS;break;}
        } else {
        if(OpenFile(target,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&file)==EFI_SUCCESS) {if(file->Delete(file)!=EFI_SUCCESS) {file=nullptr;why=T(u"Nośnik jest tylko do odczytu.",u"The media is read-only.");break;}file=nullptr;}
        status=OpenFile(target,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0,&file);
        if(status!=EFI_SUCCESS) {why=T(u"Nie udało się utworzyć pliku.",u"The file could not be created.");break;}
        }
        const char16_t* label=what?what:T(u"Pobieranie",u"Downloading");
        uint64_t done=0;size_t part=answer.BodyLength;unsigned rounds=0;bool failed=false;
        for(;;) {
            if(part) {
                size_t wrote=part;
                if(memory) {if(done+part>length) {why=T(u"Serwer przysłał więcej, niż zapowiedział.",u"The server sent more than it announced.");failed=true;break;}memcpy(memory->data+done,block,part);status=EFI_SUCCESS;}
                else status=file->Write(file,&wrote,block);
                if(status!=EFI_SUCCESS || wrote!=part) {why=T(u"Zapis nie powiódł się; może brakuje miejsca.",u"Writing failed; the disk may be full.");failed=true;break;}
                done+=part;
            }
            if(done>=length)break;
            HttpMessage more{};more.BodyLength=sizeof(block);more.Body=block;
            HttpToken next{};next.Event=event;next.Message=&more;Signalled()=false;
            status=http->Response(http,&next);
            if(status!=EFI_SUCCESS || !WaitSignal(30,http) || next.Status!=EFI_SUCCESS) {if(status==EFI_SUCCESS)status=next.Status;http->Cancel(http,nullptr);why=T(u"Połączenie zostało przerwane.",u"The connection was lost.");failed=true;break;}
            part=more.BodyLength;
            if(!part) {why=T(u"Połączenie zostało przerwane.",u"The connection was lost.");failed=true;break;}
            if(++rounds%8==0) {
                Progress(label,done,length);
            }
            Pump();
        }
        *received=done;
        if(failed) {if(memory)Free(*memory);else {file->Delete(file);file=nullptr;}break;}
        if(memory)memory->size=size_t(done);
        else if(file->Flush)file->Flush(file);
        Progress(label,done,length);
        ok=true;
    } while(false);
    if(file)file->Close(file);
    if(event)boot->CloseEvent(event);
    http->Configure(http,nullptr);binding->DestroyChild(binding,child);
    if(!ok)Fail(why?why:T(u"Pobieranie nie powiodło się.",u"The download failed."),status);
    return ok;
}
}  // namespace net
}  // namespace ws
}  // namespace hakefi
