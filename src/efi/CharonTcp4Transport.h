#pragma once
// r401: firmware byte transport for Charon's HTTPS client (CharonHttps.h):
// TCP over EFI_TCP4_PROTOCOL, names over EFI_DNS4_PROTOCOL or, when firmware
// has no DNS driver, an own DNS query over EFI_UDP4_PROTOCOL to the server
// DHCP gave; the date from the firmware clock; entropy from EFI_RNG_PROTOCOL.
//
// *** UNTESTED IN FIRMWARE. *** This file is written to the UEFI
// specification 2.10 (sections 28.1 TCP4, 30.1 UDP4, 29.3 DNS4, 37.5 RNG) and
// only COMPILED inside the Charon tree (tools/check-charon-https-compile.py);
// it has not run under OVMF or on hardware yet. The HTTPS client, TLS and the
// Microsoft flow above it ARE tested, on the Windows host over Winsock
// (tests/CharonHttpsHostTest.cpp). Structure layouts are asserted below.
//
// The adapter must already have an address (net::Dhcp). Wait loops poll the
// protocol, keep speech playing (Pump) and end on Escape. One connection at a
// time. GPL-2.0-or-later.
#include "CharonWsNet.h"
#include "CharonHttps.h"
namespace hakefi {
namespace ws {
namespace net {
namespace tcp {
inline efi_guid tcp4BindingGuid={0x00720665,0x67EB,0x4a99,{0xBA,0xF7,0xD3,0xC3,0x3A,0x1C,0x7C,0xC9}};
inline efi_guid tcp4Guid={0x65530BC7,0xA359,0x410f,{0xB0,0x10,0x5A,0xAD,0xC7,0xEC,0x2B,0x62}};
inline efi_guid udp4BindingGuid={0x83f01464,0x99bd,0x45e5,{0xb3,0x83,0xaf,0x63,0x05,0xd8,0xe9,0xe6}};
inline efi_guid udp4Guid={0x3ad9df29,0x4501,0x478d,{0xb1,0xf8,0x7f,0x7f,0xe7,0x0e,0x50,0xf3}};
inline efi_guid dns4BindingGuid={0xb625b186,0xe063,0x44f7,{0x89,0x05,0x6a,0x74,0xdc,0x6f,0x52,0xb4}};
inline efi_guid dns4Guid={0xae3d28cc,0xe05b,0x4fa1,{0xa0,0x11,0x7e,0xb5,0x5a,0x3f,0x14,0x01}};
inline efi_guid rngGuid={0x3152bca5,0xeade,0x433d,{0x86,0x2e,0xc0,0x1c,0xdc,0x29,0x1f,0x44}};
constexpr efi_status connectionFin=(efi_status(1)<<63)|104;   // EFI_CONNECTION_FIN
// ---- TCP4 ----
struct Tcp4AccessPoint {bool UseDefaultAddress;uint8_t StationAddress[4];uint8_t SubnetMask[4];uint16_t StationPort;uint8_t RemoteAddress[4];uint16_t RemotePort;bool ActiveFlag;};
struct Tcp4Config {uint8_t TypeOfService;uint8_t TimeToLive;Tcp4AccessPoint AccessPoint;void* ControlOption;};
struct Fragment {uint32_t Length;void* Buffer;};
struct Tcp4Receive {bool UrgentFlag;uint32_t DataLength;uint32_t FragmentCount;Fragment Table[1];};
struct Tcp4Transmit {bool Push;bool Urgent;uint32_t DataLength;uint32_t FragmentCount;Fragment Table[1];};
struct Tcp4Token {efi_event Event;efi_status Status;void* Packet;};          // connection token: Event + Status only
struct Tcp4CloseToken {efi_event Event;efi_status Status;bool AbortOnClose;};
struct Tcp4 {
    void* GetModeData;
    efi_status (EFIAPI *Configure)(Tcp4*,Tcp4Config*);
    void* Routes;
    efi_status (EFIAPI *Connect)(Tcp4*,Tcp4Token*);
    void* Accept;
    efi_status (EFIAPI *Transmit)(Tcp4*,Tcp4Token*);
    efi_status (EFIAPI *Receive)(Tcp4*,Tcp4Token*);
    efi_status (EFIAPI *Close)(Tcp4*,Tcp4CloseToken*);
    efi_status (EFIAPI *Cancel)(Tcp4*,void* token);
    efi_status (EFIAPI *Poll)(void*);
};
static_assert(sizeof(Tcp4AccessPoint)==20 && sizeof(Tcp4Config)==32 && offsetof(Tcp4Config,ControlOption)==24,"TCP4 configuration layout");
static_assert(offsetof(Tcp4Receive,DataLength)==4 && offsetof(Tcp4Receive,Table)==16 && offsetof(Tcp4Transmit,Table)==16 && sizeof(Fragment)==16,"TCP4 data layout");
// ---- UDP4 (own DNS query) ----
struct Udp4Config {
    bool AcceptBroadcast,AcceptPromiscuous,AcceptAnyPort,AllowDuplicatePort;uint8_t TypeOfService,TimeToLive;bool DoNotFragment;
    uint32_t ReceiveTimeout,TransmitTimeout;bool UseDefaultAddress;uint8_t StationAddress[4];uint8_t SubnetMask[4];uint16_t StationPort;
    uint8_t RemoteAddress[4];uint16_t RemotePort;
};
struct Udp4Transmit {void* SessionData;void* GatewayAddress;uint32_t DataLength;uint32_t FragmentCount;Fragment Table[1];};
struct Udp4Receive {uint8_t TimeStamp[16];efi_event RecycleSignal;uint8_t Session[12];uint32_t DataLength;uint32_t FragmentCount;Fragment Table[1];};
struct Udp4Token {efi_event Event;efi_status Status;void* Packet;};
struct Udp4 {
    void* GetModeData;
    efi_status (EFIAPI *Configure)(Udp4*,Udp4Config*);
    void* Groups;void* Routes;
    efi_status (EFIAPI *Transmit)(Udp4*,Udp4Token*);
    efi_status (EFIAPI *Receive)(Udp4*,Udp4Token*);
    efi_status (EFIAPI *Cancel)(Udp4*,void* token);
    efi_status (EFIAPI *Poll)(void*);
};
static_assert(sizeof(Udp4Config)==36 && offsetof(Udp4Config,UseDefaultAddress)==16 && offsetof(Udp4Config,StationPort)==26 && offsetof(Udp4Config,RemotePort)==32,"UDP4 configuration layout");
static_assert(offsetof(Udp4Transmit,Table)==24 && offsetof(Udp4Receive,DataLength)==36 && offsetof(Udp4Receive,Table)==48,"UDP4 data layout");
// ---- DNS4 ----
struct Dns4Config {
    size_t DnsServerListCount;void* DnsServerList;bool UseDefaultSetting;bool EnableDnsCache;uint8_t Protocol;uint8_t StationIp[4];uint8_t SubnetMask[4];
    uint16_t LocalPort;uint32_t RetryCount;uint32_t RetryInterval;
};
struct Dns4HostToAddress {uint32_t IpCount;uint8_t* IpList;};   // IpList: IpCount * 4 bytes
struct Dns4Token {efi_event Event;efi_status Status;uint32_t RetryCount;uint32_t RetryInterval;Dns4HostToAddress* Answer;};
struct Dns4 {
    void* GetModeData;
    efi_status (EFIAPI *Configure)(Dns4*,Dns4Config*);
    efi_status (EFIAPI *HostNameToIp)(Dns4*,char16_t* name,Dns4Token*);
    void* IpToHostName;void* GeneralLookUp;void* UpdateDnsCache;
    efi_status (EFIAPI *Poll)(void*);
    efi_status (EFIAPI *Cancel)(Dns4*,Dns4Token*);
};
static_assert(sizeof(Dns4Config)==40 && offsetof(Dns4Config,LocalPort)==28,"DNS4 configuration layout");
struct Rng {void* GetInfo;efi_status (EFIAPI *GetRNG)(Rng*,efi_guid* algorithm,size_t size,uint8_t* out);};
// Transport error codes (negative), kept in https::Client::transportError.
constexpr int errNoTcp=-1,errNoAddress=-2,errNoDns=-3,errNameNotFound=-4,errConnect=-5,errTimeout=-6,errEscape=-7,errIo=-8,errMemory=-9;
inline const char16_t* ErrorText(int code) {
    switch(code) {
        case errNoTcp:return T(u"Firmware nie ma obsługi TCP.",u"The firmware has no TCP support.");
        case errNoAddress:return T(u"Sieć nie jest gotowa; najpierw połącz kartę.",u"The network is not ready; connect an adapter first.");
        case errNoDns:return T(u"Brak serwera nazw DNS.",u"There is no DNS name server.");
        case errNameNotFound:return T(u"Nie znaleziono serwera o tej nazwie.",u"No server with that name was found.");
        case errConnect:return T(u"Serwer nie przyjął połączenia.",u"The server did not accept the connection.");
        case errTimeout:return T(u"Serwer nie odpowiada.",u"The server does not answer.");
        case errEscape:return T(u"Przerwano",u"Stopped");
        case errMemory:return T(u"Brak pamięci.",u"Out of memory.");
    }
    return T(u"Połączenie przerwane.",u"The connection broke.");
}
constexpr size_t receiveUnits=65536;
struct State {
    efi_handle service;            // the handle with the TCP4 service binding and an address
    ServiceBinding* binding;efi_handle child;Tcp4* tcp;
    efi_event connectEvent,rxEvent,txEvent,closeEvent;
    volatile bool connectDone,rxDone,txDone,closeDone;
    bool connected,fin,escaped;
    IpInfo ip;
    Tcp4Receive rx;Tcp4Transmit tx;
    uint8_t buffer[receiveUnits];size_t at,end;
    unsigned receives;
    unsigned connectSeconds,ioSeconds;   // 0: defaults (20, 30)
};
inline State& S() {static State state;return state;}
inline void EFIAPI OnDone(efi_event,void* context) {*static_cast<volatile bool*>(context)=true;}
// Waits for `done` while polling the protocol; speech keeps playing; Escape ends the wait.
inline bool Await(volatile bool& done,unsigned milliseconds,efi_status (EFIAPI *poll)(void*),void* self) {
    auto* boot=U().host->boot;unsigned spins=0;
    for(uint64_t waited=0;;) {
        if(done)return true;
        if(poll)poll(self);
        if(done)return true;
        boot->Stall(500);waited+=500;++spins;
        if(spins%20==0)Pump();                                    // every 10 ms of waiting
        if(spins%200==0 && EscapePressed()) {S().escaped=true;return false;}
        if(waited>=uint64_t(milliseconds)*1000)return false;
    }
}
// The service binding handle of `guid` on an adapter that has an IPv4 address.
inline bool FindService(efi_guid& guid,efi_handle& service,IpInfo* info) {
    auto* boot=U().host->boot;efi_handle* handles=nullptr;size_t count=0;bool found=false;
    if(boot->LocateHandleBuffer(ByProtocol,&guid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return false;
    for(size_t i=0;i<count && !found;++i) {
        Adapter probe{};probe.handle=handles[i];IpInfo local;
        if(boot->HandleProtocol(handles[i],&ip4Config2Guid,(void**)&probe.ip)==EFI_SUCCESS && probe.ip && ReadIp(probe,local)) {
            service=handles[i];if(info)*info=local;found=true;}
    }
    boot->FreePool(handles);
    return found;
}
inline bool ParseIp(const char* text,uint8_t* out) {
    for(int part=0;part<4;++part) {
        unsigned value=0,digits=0;
        while(*text>='0' && *text<='9') {value=value*10+unsigned(*text++-'0');if(++digits>3)return false;}
        if(!digits || value>255)return false;
        out[part]=uint8_t(value);
        if(part<3) {if(*text!='.')return false;++text;}
    }
    return !*text;
}
// ---- names: firmware DNS4 ----
inline int ResolveFirmware(const char* host,uint8_t* address) {
    auto* boot=U().host->boot;efi_handle service=nullptr;
    if(!FindService(dns4BindingGuid,service,nullptr))return errNoDns;
    ServiceBinding* binding=nullptr;efi_handle child=nullptr;Dns4* dns=nullptr;
    if(boot->HandleProtocol(service,&dns4BindingGuid,(void**)&binding)!=EFI_SUCCESS || !binding || binding->CreateChild(binding,&child)!=EFI_SUCCESS || !child)return errNoDns;
    int result=errNoDns;efi_event event=nullptr;
    static volatile bool done;static char16_t name[https::hostUnits];
    do {
        if(boot->HandleProtocol(child,&dns4Guid,(void**)&dns)!=EFI_SUCCESS || !dns)break;
        Dns4Config config{};config.UseDefaultSetting=true;config.EnableDnsCache=true;config.Protocol=17;   // UDP, servers from DHCP
        if(dns->Configure(dns,&config)!=EFI_SUCCESS)break;
        size_t n=0;for(;host[n] && n+1<https::hostUnits;++n)name[n]=char16_t(uint8_t(host[n]));
        name[n]=0;
        Dns4Token token{};done=false;
        if(boot->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,OnDone,const_cast<bool*>(&done),&event)!=EFI_SUCCESS) {event=nullptr;result=errMemory;break;}
        token.Event=event;
        if(dns->HostNameToIp(dns,name,&token)!=EFI_SUCCESS) {result=errNameNotFound;break;}
        if(!Await(done,10000,dns->Poll,dns)) {dns->Cancel(dns,&token);result=S().escaped?errEscape:errTimeout;break;}
        if(token.Status!=EFI_SUCCESS || !token.Answer || !token.Answer->IpCount || !token.Answer->IpList) {result=errNameNotFound;}
        else {memcpy(address,token.Answer->IpList,4);result=0;}
        if(token.Answer) {if(token.Answer->IpList)boot->FreePool(token.Answer->IpList);boot->FreePool(token.Answer);}
    } while(false);
    if(event)boot->CloseEvent(event);
    if(dns)dns->Configure(dns,nullptr);
    binding->DestroyChild(binding,child);
    return result;
}
// ---- names: own query over UDP4 to the DHCP-provided server ----
inline size_t BuildQuery(const char* host,uint16_t id,uint8_t* out,size_t cap) {
    if(cap<18+https::hostUnits)return 0;
    size_t n=0;out[n++]=uint8_t(id>>8);out[n++]=uint8_t(id);out[n++]=1;out[n++]=0;   // recursion desired
    out[n++]=0;out[n++]=1;for(int i=0;i<6;++i)out[n++]=0;                              // one question
    for(const char* p=host;*p;) {
        size_t label=0;while(p[label] && p[label]!='.')++label;
        if(!label || label>63)return 0;
        out[n++]=uint8_t(label);for(size_t i=0;i<label;++i)out[n++]=uint8_t(p[i]);
        p+=label;if(*p=='.')++p;
    }
    out[n++]=0;out[n++]=0;out[n++]=1;out[n++]=0;out[n++]=1;   // type A, class IN
    return n;
}
// First A record of an answer to `id`. 0: found; errNameNotFound: a definite "no"; errIo: not an answer for us.
inline int ParseAnswer(const uint8_t* m,size_t size,uint16_t id,uint8_t* address) {
    if(size<12 || m[0]!=uint8_t(id>>8) || m[1]!=uint8_t(id) || !(m[2]&0x80))return errIo;
    if((m[3]&15)!=0)return errNameNotFound;
    const unsigned questions=unsigned(m[4])<<8|m[5],answers=unsigned(m[6])<<8|m[7];size_t at=12;
    auto skipName=[&]()->bool {
        for(unsigned guard=0;guard<128;++guard) {
            if(at>=size)return false;
            const uint8_t length=m[at];
            if((length&0xc0)==0xc0) {at+=2;return at<=size;}
            ++at;if(!length)return true;
            at+=length;
        }
        return false;
    };
    for(unsigned i=0;i<questions;++i) {if(!skipName())return errIo;at+=4;}
    for(unsigned i=0;i<answers;++i) {
        if(!skipName() || at+10>size)return errIo;
        const unsigned type=unsigned(m[at])<<8|m[at+1],klass=unsigned(m[at+2])<<8|m[at+3],length=unsigned(m[at+8])<<8|m[at+9];
        at+=10;if(at+length>size)return errIo;
        if(type==1 && klass==1 && length==4) {memcpy(address,m+at,4);return 0;}
        at+=length;   // CNAME and others: the A record follows
    }
    return errNameNotFound;
}
inline int ResolveOwn(const char* host,uint8_t* address) {
    auto* boot=U().host->boot;efi_handle service=nullptr;IpInfo info;
    if(!FindService(udp4BindingGuid,service,&info) || !info.hasDns)return errNoDns;
    ServiceBinding* binding=nullptr;efi_handle child=nullptr;Udp4* udp=nullptr;
    if(boot->HandleProtocol(service,&udp4BindingGuid,(void**)&binding)!=EFI_SUCCESS || !binding || binding->CreateChild(binding,&child)!=EFI_SUCCESS || !child)return errNoDns;
    int result=errNoDns;efi_event txEvent=nullptr,rxEvent=nullptr;
    static volatile bool txDone,rxDone;static uint8_t query[18+https::hostUnits+8];static Udp4Transmit tx;
    do {
        if(boot->HandleProtocol(child,&udp4Guid,(void**)&udp)!=EFI_SUCCESS || !udp)break;
        Udp4Config config{};config.TimeToLive=64;config.UseDefaultAddress=true;config.RemotePort=53;memcpy(config.RemoteAddress,info.dns,4);
        if(udp->Configure(udp,&config)!=EFI_SUCCESS) {result=errNoAddress;break;}
        if(boot->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,OnDone,const_cast<bool*>(&txDone),&txEvent)!=EFI_SUCCESS) {txEvent=nullptr;result=errMemory;break;}
        if(boot->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,OnDone,const_cast<bool*>(&rxDone),&rxEvent)!=EFI_SUCCESS) {rxEvent=nullptr;result=errMemory;break;}
        uint16_t id=uint16_t(__builtin_ia32_rdtsc());
        result=errTimeout;
        for(unsigned attempt=0;attempt<3 && result==errTimeout;++attempt) {
            ++id;const size_t size=BuildQuery(host,id,query,sizeof(query));
            if(!size) {result=errNameNotFound;break;}
            memset(&tx,0,sizeof(tx));tx.DataLength=uint32_t(size);tx.FragmentCount=1;tx.Table[0].Length=uint32_t(size);tx.Table[0].Buffer=query;
            Udp4Token send{};send.Event=txEvent;send.Packet=&tx;txDone=false;
            if(udp->Transmit(udp,&send)!=EFI_SUCCESS || !Await(txDone,3000,udp->Poll,udp) || send.Status!=EFI_SUCCESS) {
                if(!txDone)udp->Cancel(udp,&send);
                if(S().escaped) {result=errEscape;break;}
                continue;}
            // Answers that are not ours (late answers of an earlier attempt) are skipped until the time is up.
            for(unsigned packet=0;packet<4 && result==errTimeout;++packet) {
                Udp4Token receive{};receive.Event=rxEvent;rxDone=false;
                if(udp->Receive(udp,&receive)!=EFI_SUCCESS)break;
                if(!Await(rxDone,3000,udp->Poll,udp)) {udp->Cancel(udp,&receive);if(S().escaped)result=errEscape;break;}
                auto* data=static_cast<Udp4Receive*>(receive.Packet);
                if(receive.Status!=EFI_SUCCESS || !data)break;
                if(data->FragmentCount>=1 && data->Table[0].Buffer) {
                    const int parsed=ParseAnswer(static_cast<const uint8_t*>(data->Table[0].Buffer),data->Table[0].Length,id,address);
                    if(parsed!=errIo)result=parsed;
                }
                if(data->RecycleSignal)boot->SignalEvent(data->RecycleSignal);
            }
        }
    } while(false);
    if(txEvent)boot->CloseEvent(txEvent);
    if(rxEvent)boot->CloseEvent(rxEvent);
    if(udp)udp->Configure(udp,nullptr);
    binding->DestroyChild(binding,child);
    return result;
}
inline int ResolveName(const char* host,uint8_t* address) {
    if(ParseIp(host,address))return 0;
    const int firmware=ResolveFirmware(host,address);
    if(firmware==0 || firmware==errEscape)return firmware;
    const int own=ResolveOwn(host,address);
    return own==errNoDns && firmware!=errNoDns?firmware:own;
}
// ---- the transport ----
inline void Shut(void*) {
    auto& s=S();auto* boot=U().host->boot;
    if(s.tcp) {
        if(s.connected && s.closeEvent) {
            // Abortive close: the HTTP exchange is over (Connection: close); a
            // graceful FIN handshake would only wait on the peer.
            Tcp4CloseToken token{};token.Event=s.closeEvent;token.AbortOnClose=true;s.closeDone=false;
            if(s.tcp->Close(s.tcp,&token)==EFI_SUCCESS)Await(s.closeDone,2000,s.tcp->Poll,s.tcp);
        }
        s.tcp->Configure(s.tcp,nullptr);   // also cancels pending tokens
    }
    efi_event* events[4]={&s.connectEvent,&s.rxEvent,&s.txEvent,&s.closeEvent};
    for(efi_event* e:events)if(*e) {boot->CloseEvent(*e);*e=nullptr;}
    if(s.binding && s.child)s.binding->DestroyChild(s.binding,s.child);
    s.child=nullptr;s.tcp=nullptr;s.binding=nullptr;s.connected=false;s.at=s.end=0;s.fin=false;
}
inline int Connect(void*,const char* host,uint16_t port) {
    auto& s=S();auto* boot=U().host->boot;
    if(s.tcp || s.child)Shut(nullptr);
    s.escaped=false;s.at=s.end=0;s.fin=false;s.receives=0;
    if(!FindService(tcp4BindingGuid,s.service,&s.ip)) {
        efi_handle* handles=nullptr;size_t count=0;const bool any=boot->LocateHandleBuffer(ByProtocol,&tcp4BindingGuid,nullptr,&count,&handles)==EFI_SUCCESS && handles && count;
        if(handles)boot->FreePool(handles);
        return any?errNoAddress:errNoTcp;
    }
    uint8_t address[4];
    const int resolved=ResolveName(host,address);
    if(resolved!=0)return resolved;
    if(boot->HandleProtocol(s.service,&tcp4BindingGuid,(void**)&s.binding)!=EFI_SUCCESS || !s.binding || s.binding->CreateChild(s.binding,&s.child)!=EFI_SUCCESS || !s.child
        || boot->HandleProtocol(s.child,&tcp4Guid,(void**)&s.tcp)!=EFI_SUCCESS || !s.tcp) {Shut(nullptr);return errNoTcp;}
    Tcp4Config config{};config.TimeToLive=64;config.AccessPoint.UseDefaultAddress=true;config.AccessPoint.StationPort=0;   // any free local port
    memcpy(config.AccessPoint.RemoteAddress,address,4);config.AccessPoint.RemotePort=port;config.AccessPoint.ActiveFlag=true;
    if(s.tcp->Configure(s.tcp,&config)!=EFI_SUCCESS) {Shut(nullptr);return errNoAddress;}
    if(boot->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,OnDone,const_cast<bool*>(&s.connectDone),&s.connectEvent)!=EFI_SUCCESS) {s.connectEvent=nullptr;Shut(nullptr);return errMemory;}
    if(boot->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,OnDone,const_cast<bool*>(&s.rxDone),&s.rxEvent)!=EFI_SUCCESS) {s.rxEvent=nullptr;Shut(nullptr);return errMemory;}
    if(boot->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,OnDone,const_cast<bool*>(&s.txDone),&s.txEvent)!=EFI_SUCCESS) {s.txEvent=nullptr;Shut(nullptr);return errMemory;}
    if(boot->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,OnDone,const_cast<bool*>(&s.closeDone),&s.closeEvent)!=EFI_SUCCESS) {s.closeEvent=nullptr;Shut(nullptr);return errMemory;}
    Tcp4Token token{};token.Event=s.connectEvent;s.connectDone=false;
    if(s.tcp->Connect(s.tcp,&token)!=EFI_SUCCESS) {Shut(nullptr);return errConnect;}
    if(!Await(s.connectDone,(s.connectSeconds?s.connectSeconds:20)*1000,s.tcp->Poll,s.tcp)) {
        s.tcp->Cancel(s.tcp,nullptr);const int why=s.escaped?errEscape:errTimeout;Shut(nullptr);return why;}
    if(token.Status!=EFI_SUCCESS) {Shut(nullptr);return errConnect;}
    s.connected=true;
    return 0;
}
inline int Send(void*,const uint8_t* data,size_t size) {
    auto& s=S();if(!s.tcp || !s.connected)return errIo;
    if(size>32768)size=32768;
    memset(&s.tx,0,sizeof(s.tx));s.tx.Push=true;s.tx.DataLength=uint32_t(size);s.tx.FragmentCount=1;
    s.tx.Table[0].Length=uint32_t(size);s.tx.Table[0].Buffer=const_cast<uint8_t*>(data);
    Tcp4Token token{};token.Event=s.txEvent;token.Packet=&s.tx;s.txDone=false;
    if(s.tcp->Transmit(s.tcp,&token)!=EFI_SUCCESS)return errIo;
    if(!Await(s.txDone,(s.ioSeconds?s.ioSeconds:30)*1000,s.tcp->Poll,s.tcp)) {s.tcp->Cancel(s.tcp,nullptr);return s.escaped?errEscape:errTimeout;}
    return token.Status==EFI_SUCCESS?int(size):errIo;
}
inline int Receive(void*,uint8_t* out,size_t capacity) {
    auto& s=S();if(!s.tcp || !s.connected)return errIo;
    if(s.at>=s.end) {
        if(s.fin)return 0;
        memset(&s.rx,0,sizeof(s.rx));s.rx.DataLength=uint32_t(receiveUnits);s.rx.FragmentCount=1;
        s.rx.Table[0].Length=uint32_t(receiveUnits);s.rx.Table[0].Buffer=s.buffer;
        Tcp4Token token{};token.Event=s.rxEvent;token.Packet=&s.rx;s.rxDone=false;
        const efi_status started=s.tcp->Receive(s.tcp,&token);
        if(started==connectionFin) {s.fin=true;return 0;}
        if(started!=EFI_SUCCESS)return errIo;
        if(!Await(s.rxDone,(s.ioSeconds?s.ioSeconds:30)*1000,s.tcp->Poll,s.tcp)) {s.tcp->Cancel(s.tcp,nullptr);return s.escaped?errEscape:errTimeout;}
        if(token.Status==connectionFin) {s.fin=true;return 0;}
        if(token.Status!=EFI_SUCCESS)return errIo;
        s.at=0;s.end=s.rx.DataLength<=receiveUnits?s.rx.DataLength:receiveUnits;
        if(!s.end)return errIo;
        // Data that arrives without any waiting must not starve speech.
        if(++s.receives%8==0)Pump();
    }
    size_t n=s.end-s.at;if(n>capacity)n=capacity;
    memcpy(out,s.buffer+s.at,n);s.at+=n;
    return int(n);
}
// The firmware clock as UTC. A clock kept in local time without a stated zone
// (Windows' default) is taken as it is: at most half a day off, which
// certificate validity tolerates everywhere except on the first and last day.
inline bool Clock(void*,https::UtcTime* now) {
    auto* runtime=U().host->system->RuntimeServices;efi_time t{};
    if(!runtime || !runtime->GetTime || runtime->GetTime(&t,nullptr)!=EFI_SUCCESS || t.Year<2025 || t.Month<1 || t.Month>12 || t.Day<1 || t.Day>31)return false;
    int64_t seconds=int64_t(https::UnixSeconds(https::UtcTime{t.Year,t.Month,t.Day,t.Hour,t.Minute,t.Second}));
    if(t.TimeZone>=-1440 && t.TimeZone<=1440)seconds+=int64_t(t.TimeZone)*60;   // UTC = local time + TimeZone
    // Back to a calendar date.
    const int64_t days=seconds/86400;const unsigned rest=unsigned(seconds%86400);
    const int64_t z=days+719468;const int64_t era=z/146097;const unsigned doe=unsigned(z-era*146097);
    const unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;const unsigned doy=doe-(365*yoe+yoe/4-yoe/100);
    const unsigned mp=(5*doy+2)/153;const unsigned day=doy-(153*mp+2)/5+1;const unsigned month=mp<10?mp+3:mp-9;
    now->year=uint16_t(yoe+unsigned(era)*400+(month<=2?1:0));now->month=uint8_t(month);now->day=uint8_t(day);
    now->hour=uint8_t(rest/3600);now->minute=uint8_t(rest/60%60);now->second=uint8_t(rest%60);
    return true;
}
// Random bytes: the firmware generator when there is one. Otherwise timing
// jitter of the processor's cycle counter around short firmware delays,
// condensed with SHA-256 - a weaker source, used only so that TLS still has
// unpredictable input on firmware without EFI_RNG_PROTOCOL (BearSSL also
// mixes in RDRAND by itself when the processor has it).
inline bool Entropy(void*,uint8_t* out,size_t size) {
    auto* boot=U().host->boot;Rng* rng=nullptr;
    if(boot->LocateProtocol(&rngGuid,nullptr,(void**)&rng)==EFI_SUCCESS && rng && rng->GetRNG && rng->GetRNG(rng,nullptr,size,out)==EFI_SUCCESS)return true;
    static uint64_t counter;
    for(size_t done=0;done<size;) {
        br_sha256_context hash;br_sha256_init(&hash);
        for(unsigned i=0;i<256;++i) {const uint64_t tick=__builtin_ia32_rdtsc();br_sha256_update(&hash,&tick,sizeof(tick));boot->Stall(1);}
        efi_time t{};auto* runtime=U().host->system->RuntimeServices;if(runtime && runtime->GetTime)runtime->GetTime(&t,nullptr);
        br_sha256_update(&hash,&t,sizeof(t));++counter;br_sha256_update(&hash,&counter,sizeof(counter));
        uint8_t digest[32];br_sha256_out(&hash,digest);
        for(size_t i=0;i<32 && done<size;++i)out[done++]=digest[i];
    }
    return true;
}
// The transport for https::Client::transport. `progress` may be set by the caller afterwards.
inline https::Transport Transport() {
    https::Transport t{};t.context=nullptr;t.connect=Connect;t.send=Send;t.recv=Receive;t.close=Shut;t.clock=Clock;t.entropy=Entropy;t.progress=nullptr;
    return t;
}
}  // namespace tcp
}  // namespace net
}  // namespace ws
}  // namespace hakefi
