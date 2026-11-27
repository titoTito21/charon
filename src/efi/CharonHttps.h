#pragma once
// r401: Charon's own HTTPS client. Firmware rarely ships TLS, and Microsoft,
// Debian and Ubuntu serve their images over HTTPS, so Charon brings TLS 1.2
// with it (BearSSL 0.6, MIT, src/efi/third_party/bearssl) and its own list of
// root certificates (CharonTrustAnchors.h). The client speaks HTTP/1.1 over an
// ABSTRACT byte transport (connect/send/recv/close + clock + entropy +
// progress), so the same code runs on the Windows host test (Winsock) and in
// firmware (CharonTcp4Transport.h, EFI_TCP4).
//
// What it does: http:// and https:// GET and HEAD, SNI, certificate chain and
// host name validation against the embedded roots at the time given by the
// clock callback, custom request headers, redirects, Content-Length, chunked
// and until-close bodies, Range requests and resuming downloads, streaming of
// the body to a sink (nothing is buffered whole).
// What it does not do: TLS 1.3, HTTP/2, proxies, compression (it asks for
// identity), cookies (response headers are readable with Header()), IPv6
// literals, client certificates, certificate revocation checks.
//
// No heap. One Client is about 90 KiB: keep it static, never on the stack.
// Not reentrant: one request at a time per Client. GPL-2.0-or-later.
#include <stddef.h>
#include <stdint.h>
#include "third_party/bearssl/inc/bearssl.h"
#include "CharonTrustAnchors.h"
namespace hakefi {
namespace https {
struct UtcTime {uint16_t year;uint8_t month,day,hour,minute,second;};
// The byte transport. All callbacks get `context`. A transport handles one
// connection at a time and its own timeouts; in firmware its wait loops keep
// speech playing.
struct Transport {
    void* context;
    int (*connect)(void*,const char* host,uint16_t port);    // 0, or a negative transport error (kept in Client::transportError)
    int (*send)(void*,const uint8_t* data,size_t size);      // > 0: bytes accepted; <= 0: error
    int (*recv)(void*,uint8_t* data,size_t capacity);        // > 0: bytes; 0: the peer closed; < 0: error, timeout or cancel
    void (*close)(void*);
    bool (*clock)(void*,UtcTime* now);                       // current UTC; required for https
    bool (*entropy)(void*,uint8_t* out,size_t size);         // random bytes; required for https unless the CPU has RDRAND
    bool (*progress)(void*,uint64_t received,uint64_t total);// optional; total is UINT64_MAX when unknown; false cancels
};
enum class Result : uint8_t {
    ok,
    stopped,               // the sink returned false (wanted early stop)
    cancelled,             // the progress callback returned false
    badUrl,                // not http:// or https://, or too long
    noTransport,
    connectFailed,         // name lookup or TCP connect; Client::transportError has the transport's code
    noClock,               // https needs the date; the clock callback is missing or gave nonsense
    noEntropy,             // no random source for TLS
    tlsFailed,             // handshake or record failure; Client::tlsError has the BearSSL code
    certificateUntrusted,  // the chain does not end at an embedded root
    certificateExpired,    // expired or not yet valid AT THE CLOCK'S DATE (a wrong firmware clock ends here)
    certificateWrongHost,  // valid certificate, but for another name
    certificateBad,        // malformed, bad signature, unsupported algorithm ...
    sendFailed,
    receiveFailed,         // the connection broke
    truncated,             // closed before the announced length
    badAnswer,             // not HTTP, or a malformed header or chunk
    headerTooLarge,
    tooManyRedirects,
    insecureRedirect,      // https redirected to http and the request did not allow it
    httpStatus,            // the server answered with an error status (Response::status); the body was not delivered
    rangeIgnored,          // a Range was asked from an offset above 0 and the server sent the whole file instead
    sinkTooSmall,          // (helpers) a bounded text buffer overflowed
};
inline const char* ResultText(Result r) {
    switch(r) {
        case Result::ok:return "ok";case Result::stopped:return "stopped by the caller";case Result::cancelled:return "cancelled";
        case Result::badUrl:return "bad address";case Result::noTransport:return "no network transport";
        case Result::connectFailed:return "could not connect";case Result::noClock:return "the date is unknown, certificates cannot be checked";
        case Result::noEntropy:return "no random number source";case Result::tlsFailed:return "TLS failure";
        case Result::certificateUntrusted:return "certificate not trusted";case Result::certificateExpired:return "certificate expired or not yet valid (check the clock)";
        case Result::certificateWrongHost:return "certificate is for another host";case Result::certificateBad:return "certificate invalid";
        case Result::sendFailed:return "sending failed";case Result::receiveFailed:return "connection broke";
        case Result::truncated:return "answer cut short";case Result::badAnswer:return "malformed answer";
        case Result::headerTooLarge:return "answer header too large";case Result::tooManyRedirects:return "too many redirects";
        case Result::insecureRedirect:return "redirect from https to http refused";case Result::httpStatus:return "server error status";
        case Result::rangeIgnored:return "server does not support resuming";case Result::sinkTooSmall:return "answer larger than the buffer";
    }
    return "?";
}
// Receives body bytes in order; false stops the transfer (Result::stopped).
typedef bool (*Sink)(void* context,const uint8_t* data,size_t size);
constexpr uint64_t unknownLength=UINT64_MAX;
constexpr size_t urlUnits=2048,hostUnits=256,headUnits=16384,bodyUnits=16384;
struct Request {
    const char* url=nullptr;
    const char* headers=nullptr;     // extra header lines, each ending "\r\n" (e.g. "Referer: https://x/\r\n"); sent to the first host only
    bool head=false;                 // HEAD instead of GET
    bool range=false;                // send Range: bytes=rangeStart-[rangeEnd]
    uint64_t rangeStart=0,rangeEnd=unknownLength;   // rangeEnd inclusive; unknownLength = to the end
    const char* ifRange=nullptr;     // validator (ETag or Last-Modified) for If-Range
    unsigned maxRedirects=8;
    bool allowInsecureRedirect=false;// follow https -> http
    bool deliverErrorBody=false;     // true: bodies of 4xx/5xx answers go to the sink too and the result is ok
};
struct Response {
    int status=0;
    uint64_t contentLength=unknownLength;   // bytes of this answer's body, when announced
    uint64_t totalLength=unknownLength;     // size of the whole resource (Content-Range total, or contentLength of a 200)
    uint64_t rangeStart=0;                  // first byte offset of this body within the resource
    uint64_t received=0;                    // body bytes delivered to the sink
    bool chunked=false,partial=false;
    unsigned redirects=0;
    char contentType[96]={};
    char validator[160]={};                 // ETag, else Last-Modified: pass as Request::ifRange when resuming
};
// A plain aggregate without initializers on purpose: `static Client c;` is
// zero-initialized at load time and needs no constructor (Charon runs none).
struct Client {
    Transport transport;
    const char* userAgent;       // null: "Charon"
    // r410 (agent 2): opt-in HTTP/1.1 keep-alive. false (the zero default): every request opens and closes its own
    // connection, exactly as before. true: the connection (and its TLS session) stays open after a complete answer
    // and the next request to the same host, port and scheme reuses it; the caller ends with Close(c).
    bool keepAlive;
    // ---- results of the last request ----
    int transportError;          // transport's code when connect/send/recv failed
    int tlsError;                // BearSSL error code (BR_ERR_*) of the last TLS failure
    char url[urlUnits];          // the address finally used (after redirects)
    // ---- internal ----
    bool open,tls,anchorsReady;
    bool reusable;char openHost[hostUnits];uint16_t openPort;   // keep-alive: the open connection may take another request
    char host[hostUnits];uint16_t port;const char* path;
    char next[urlUnits];
    char keptUrl[urlUnits];char kept[160];   // r410 (agent 3): Download's address and validator, per client (several clients run at once: CharonDownloads.h)
    char head[headUnits];size_t headSize,rawAt,rawEnd;   // response header text, then already received body bytes
    uint8_t body[bodyUnits];
    br_ssl_client_context ssl;br_x509_minimal_context x509;br_sslio_context io;
    br_x509_trust_anchor anchors[anchors::rootCount];
    unsigned char record[BR_SSL_BUFSIZE_BIDI];
};
static_assert(__is_trivially_constructible(Client),"https::Client must need no constructor");
// ---- small text helpers (no C library) ----
inline size_t Length(const char* s) {size_t n=0;while(s && s[n])++n;return n;}
inline char Lower(char c) {return c>='A' && c<='Z'?char(c+32):c;}
inline bool StartsNoCase(const char* text,const char* prefix) {for(size_t i=0;prefix[i];++i)if(Lower(text[i])!=Lower(prefix[i]))return false;return true;}
inline bool Copy(char* out,size_t cap,const char* text,size_t n) {
    if(!cap)return false;
    const bool fits=n<cap;if(!fits)n=cap-1;
    for(size_t i=0;i<n;++i)out[i]=text[i];
    out[n]=0;return fits;
}
inline bool Copy(char* out,size_t cap,const char* text) {return Copy(out,cap,text,Length(text));}
struct Appender {
    char* out;size_t cap;size_t n=0;bool overflow=false;
    Appender(char* buffer,size_t capacity):out(buffer),cap(capacity) {if(cap)out[0]=0;}
    void Add(const char* text,size_t count) {for(size_t i=0;i<count;++i) {if(n+1>=cap) {overflow=true;return;}out[n++]=text[i];}if(cap)out[n]=0;}
    void Add(const char* text) {Add(text,Length(text));}
    void Number(uint64_t v) {char d[21];size_t k=0;do {d[k++]=char('0'+v%10);v/=10;} while(v);while(k) {const char c=d[--k];Add(&c,1);}}
};
// Days since 1 January 0 AD (proleptic Gregorian), the count BearSSL wants.
inline uint32_t DaysFromCivil(unsigned year,unsigned month,unsigned day) {
    const unsigned y=month<=2?year-1:year;const unsigned era=y/400,yoe=y-era*400;
    const unsigned doy=(153*(month+(month>2?unsigned(-3):9))+2)/5+day-1;
    const unsigned doe=yoe*365+yoe/4-yoe/100+doy;
    return era*146097+doe+60;   // 1970-01-01 -> 719528
}
// Seconds since 1970-01-01 UTC.
inline uint64_t UnixSeconds(const UtcTime& t) {
    return (uint64_t(DaysFromCivil(t.year,t.month,t.day))-719528)*86400+uint64_t(t.hour)*3600+uint64_t(t.minute)*60+t.second;
}
// ---- transport and TLS plumbing ----
inline int LowRead(void* context,unsigned char* data,size_t size) {
    auto& c=*static_cast<Client*>(context);
    const int got=c.transport.recv(c.transport.context,data,size>0x4000?0x4000:size);
    if(got<0)c.transportError=got;
    return got>0?got:-1;
}
inline int LowWrite(void* context,const unsigned char* data,size_t size) {
    auto& c=*static_cast<Client*>(context);
    const int sent=c.transport.send(c.transport.context,data,size>0x4000?0x4000:size);
    if(sent<=0)c.transportError=sent;
    return sent>0?sent:-1;
}
inline void Close(Client& c) {
    if(!c.open)return;
    // No close_notify exchange: the answer is already complete or abandoned,
    // and waiting for the peer's alert only costs a timeout.
    if(c.transport.close)c.transport.close(c.transport.context);
    c.open=false;c.reusable=false;
}
inline void PrepareAnchors(Client& c) {
    if(c.anchorsReady)return;
    for(size_t i=0;i<anchors::rootCount;++i) {
        const auto& r=anchors::roots[i];auto& a=c.anchors[i];
        a.dn.data=const_cast<unsigned char*>(r.dn);a.dn.len=r.dnSize;a.flags=BR_X509_TA_CA;
        if(r.kind==1) {
            a.pkey.key_type=BR_KEYTYPE_RSA;a.pkey.key.rsa.n=const_cast<unsigned char*>(r.a);a.pkey.key.rsa.nlen=r.aSize;
            a.pkey.key.rsa.e=const_cast<unsigned char*>(r.b);a.pkey.key.rsa.elen=r.bSize;
        } else {
            a.pkey.key_type=BR_KEYTYPE_EC;a.pkey.key.ec.curve=r.curve;a.pkey.key.ec.q=const_cast<unsigned char*>(r.a);a.pkey.key.ec.qlen=r.aSize;
        }
    }
    c.anchorsReady=true;
}
inline Result TlsResult(Client& c) {
    const int e=br_ssl_engine_last_error(&c.ssl.eng);c.tlsError=e;
    switch(e) {
        case BR_ERR_NO_RANDOM:return Result::noEntropy;
        case BR_ERR_X509_NOT_TRUSTED:return Result::certificateUntrusted;
        case BR_ERR_X509_EXPIRED:return Result::certificateExpired;
        case BR_ERR_X509_TIME_UNKNOWN:return Result::noClock;
        case BR_ERR_X509_BAD_SERVER_NAME:return Result::certificateWrongHost;
        case BR_ERR_IO:return Result::receiveFailed;
    }
    if(e>=BR_ERR_X509_INVALID_VALUE && e<=BR_ERR_X509_NOT_TRUSTED)return Result::certificateBad;
    return Result::tlsFailed;
}
// Connects and, for https, completes the handshake with a fully checked certificate.
inline Result Open(Client& c,const char* host,uint16_t port,bool tls) {
    auto& t=c.transport;c.transportError=0;c.tlsError=0;
    if(!t.connect || !t.send || !t.recv)return Result::noTransport;
    UtcTime now{};
    if(tls && (!t.clock || !t.clock(t.context,&now) || now.year<2025 || now.year>2200 || now.month<1 || now.month>12 || now.day<1 || now.day>31))return Result::noClock;
    const int status=t.connect(t.context,host,port);
    if(status<0) {c.transportError=status;return Result::connectFailed;}
    c.open=true;c.tls=tls;
    if(!tls)return Result::ok;
    PrepareAnchors(c);
    // TLS 1.2 only, forward secret AEAD suites only.
    static const uint16_t suites[]={
        BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384};
    br_ssl_client_zero(&c.ssl);
    br_ssl_engine_set_versions(&c.ssl.eng,BR_TLS12,BR_TLS12);
    br_ssl_engine_set_suites(&c.ssl.eng,suites,sizeof(suites)/sizeof(suites[0]));
    br_ssl_engine_set_default_rsavrfy(&c.ssl.eng);
    br_ssl_engine_set_default_ecdsa(&c.ssl.eng);
    br_ssl_engine_set_default_ec(&c.ssl.eng);
    br_ssl_engine_set_hash(&c.ssl.eng,br_sha256_ID,&br_sha256_vtable);
    br_ssl_engine_set_hash(&c.ssl.eng,br_sha384_ID,&br_sha384_vtable);
    br_ssl_engine_set_hash(&c.ssl.eng,br_sha512_ID,&br_sha512_vtable);
    br_ssl_engine_set_prf_sha256(&c.ssl.eng,&br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(&c.ssl.eng,&br_tls12_sha384_prf);
    br_ssl_engine_set_default_aes_gcm(&c.ssl.eng);
    br_ssl_engine_set_default_chapol(&c.ssl.eng);
    br_x509_minimal_init(&c.x509,&br_sha256_vtable,c.anchors,anchors::rootCount);
    br_x509_minimal_set_hash(&c.x509,br_sha256_ID,&br_sha256_vtable);
    br_x509_minimal_set_hash(&c.x509,br_sha384_ID,&br_sha384_vtable);
    br_x509_minimal_set_hash(&c.x509,br_sha512_ID,&br_sha512_vtable);
    br_x509_minimal_set_rsa(&c.x509,br_rsa_pkcs1_vrfy_get_default());
    br_x509_minimal_set_ecdsa(&c.x509,br_ec_get_default(),br_ecdsa_vrfy_asn1_get_default());
    br_x509_minimal_set_time(&c.x509,DaysFromCivil(now.year,now.month,now.day),uint32_t(now.hour)*3600+uint32_t(now.minute)*60+now.second);
    br_ssl_engine_set_x509(&c.ssl.eng,&c.x509.vtable);
    br_ssl_engine_set_buffer(&c.ssl.eng,c.record,sizeof(c.record),1);
    uint8_t seed[32];
    if(t.entropy && t.entropy(t.context,seed,sizeof(seed)))br_ssl_engine_inject_entropy(&c.ssl.eng,seed,sizeof(seed));
    for(auto& b:seed)b=0;
    if(!br_ssl_client_reset(&c.ssl,host,0)) {const Result r=TlsResult(c);Close(c);return r;}
    br_sslio_init(&c.io,&c.ssl.eng,LowRead,&c,LowWrite,&c);
    if(br_sslio_flush(&c.io)<0) {   // runs the whole handshake
        Result r=TlsResult(c);
        if(r==Result::receiveFailed && !c.transportError)r=Result::tlsFailed;   // closed on us during the handshake
        Close(c);return r;}
    return Result::ok;
}
inline Result WriteAll(Client& c,const char* data,size_t size) {
    if(c.tls) {
        if(br_sslio_write_all(&c.io,data,size)<0 || br_sslio_flush(&c.io)<0) {c.tlsError=br_ssl_engine_last_error(&c.ssl.eng);return Result::sendFailed;}
        return Result::ok;
    }
    while(size) {
        const int sent=c.transport.send(c.transport.context,reinterpret_cast<const uint8_t*>(data),size>0x4000?0x4000:size);
        if(sent<=0) {c.transportError=sent;return Result::sendFailed;}
        data+=sent;size-=size_t(sent);
    }
    return Result::ok;
}
// > 0: bytes; 0: closed cleanly; < 0: failure.
inline int ReadSome(Client& c,uint8_t* out,size_t cap) {
    if(c.tls) {
        const int got=br_sslio_read(&c.io,out,cap);
        if(got>0)return got;
        const int e=br_ssl_engine_last_error(&c.ssl.eng);c.tlsError=e;
        return e==BR_ERR_OK?0:-1;   // BR_ERR_OK: close_notify received
    }
    const int got=c.transport.recv(c.transport.context,out,cap);
    if(got<0)c.transportError=got;
    return got;
}
// ---- URL ----
// Splits c.url in place into c.host, c.port, c.path (path points into c.url).
inline bool ParseUrl(Client& c,bool& tls) {
    const char* u=c.url;size_t at;
    if(StartsNoCase(u,"https://")) {tls=true;at=8;c.port=443;}
    else if(StartsNoCase(u,"http://")) {tls=false;at=7;c.port=80;}
    else return false;
    size_t h=0;
    while(u[at] && u[at]!='/' && u[at]!='?' && u[at]!='#' && u[at]!=':') {
        if(u[at]=='@' || u[at]=='[' || uint8_t(u[at])<=32 || h+1>=hostUnits)return false;
        c.host[h++]=Lower(u[at++]);}
    c.host[h]=0;if(!h)return false;
    if(u[at]==':') {
        ++at;uint32_t port=0;size_t digits=0;
        while(u[at]>='0' && u[at]<='9') {port=port*10+uint32_t(u[at++]-'0');if(++digits>5)return false;}
        if(!digits || !port || port>65535)return false;
        c.port=uint16_t(port);
    }
    if(u[at] && u[at]!='/' && u[at]!='?' && u[at]!='#')return false;
    // Cut the fragment; it is never sent.
    for(size_t i=at;c.url[i];++i)if(c.url[i]=='#') {c.url[i]=0;break;}
    c.path=c.url+at;
    return true;
}
// Resolves a Location value against the current c.url into c.next.
inline bool Resolve(Client& c,const char* location,size_t n,bool tls) {
    Appender a(c.next,urlUnits);
    if(StartsNoCase(location,"http://") || StartsNoCase(location,"https://")) {a.Add(location,n);return !a.overflow;}
    a.Add(tls?"https:":"http:");
    if(n>=2 && location[0]=='/' && location[1]=='/') {a.Add(location,n);return !a.overflow;}
    a.Add("//");a.Add(c.host);
    if(c.port!=(tls?443:80)) {a.Add(":");a.Number(c.port);}
    if(n && location[0]=='/') {a.Add(location,n);return !a.overflow;}
    // Relative to the directory of the current path (without its query).
    size_t end=0,slash=0;
    while(c.path[end] && c.path[end]!='?') {if(c.path[end]=='/')slash=end+1;++end;}
    if(!slash)a.Add("/");else a.Add(c.path,slash);
    a.Add(location,n);return !a.overflow;
}
// ---- response header ----
// Value of a header of the last answer (first occurrence; `index` picks later ones, e.g. Set-Cookie).
inline bool Header(const Client& c,const char* name,char* out,size_t cap,size_t index=0) {
    const size_t nameSize=Length(name);size_t at=0;
    while(at<c.headSize && c.head[at]!='\n')++at;   // status line
    ++at;
    while(at<c.headSize) {
        size_t end=at;while(end<c.headSize && c.head[end]!='\n')++end;
        if(end-at>nameSize && c.head[at+nameSize]==':' && StartsNoCase(c.head+at,name)) {
            bool same=true;for(size_t i=0;i<nameSize;++i)if(Lower(c.head[at+i])!=Lower(name[i]))same=false;
            if(same && index--==0) {
                size_t from=at+nameSize+1,to=end;
                while(from<to && (c.head[from]==' ' || c.head[from]=='\t'))++from;
                while(to>from && (c.head[to-1]=='\r' || c.head[to-1]==' ' || c.head[to-1]=='\t'))--to;
                Copy(out,cap,c.head+from,to-from);return true;
            }
        }
        at=end+1;
    }
    if(cap)out[0]=0;
    return false;
}
inline bool ParseNumber(const char* text,uint64_t& value) {
    value=0;size_t i=0;
    for(;text[i]>='0' && text[i]<='9';++i) {if(value>(UINT64_MAX-9)/10)return false;value=value*10+uint64_t(text[i]-'0');}
    return i>0;
}
// Reads until the blank line; leaves extra bytes (start of the body) at rawAt..rawEnd.
inline Result ReadHead(Client& c) {
    size_t size=0,scanned=0;c.headSize=0;c.rawAt=c.rawEnd=0;
    for(;;) {
        if(size>=headUnits-1)return Result::headerTooLarge;
        const int got=ReadSome(c,reinterpret_cast<uint8_t*>(c.head)+size,headUnits-1-size);
        if(got<=0)return size?Result::truncated:Result::receiveFailed;
        size+=size_t(got);
        for(size_t i=scanned;i+1<size;++i) {
            if(c.head[i]!='\n')continue;
            size_t end=0;
            if(c.head[i+1]=='\n')end=i+2;
            else if(i+2<size && c.head[i+1]=='\r' && c.head[i+2]=='\n')end=i+3;
            if(end) {c.headSize=end;c.rawAt=end;c.rawEnd=size;return Result::ok;}
        }
        scanned=size>2?size-2:0;
    }
}
// Next piece of raw body bytes: first what came with the header, then the wire.
inline int NextRaw(Client& c,const uint8_t*& data) {
    if(c.rawAt<c.rawEnd) {data=reinterpret_cast<const uint8_t*>(c.head)+c.rawAt;const size_t n=c.rawEnd-c.rawAt;c.rawAt=c.rawEnd;return int(n);}
    data=c.body;return ReadSome(c,c.body,bodyUnits);
}
inline Result Deliver(Client& c,Response& rs,Sink sink,void* sinkContext,const uint8_t* data,size_t size,uint64_t total) {
    if(!size)return Result::ok;
    rs.received+=size;
    if(sink && !sink(sinkContext,data,size))return Result::stopped;
    if(c.transport.progress && !c.transport.progress(c.transport.context,rs.rangeStart+rs.received,total))return Result::cancelled;
    return Result::ok;
}
inline Result ReadBody(Client& c,Response& rs,Sink sink,void* sinkContext) {
    const uint8_t* data=nullptr;
    if(rs.chunked) {
        // size line -> data -> CRLF -> ... -> 0 -> trailers -> blank line
        enum {sizeLine,extension,sizeEnd,chunk,chunkEnd,trailerStart,trailer} state=sizeLine;
        uint64_t left=0;bool digits=false;
        for(;;) {
            const int got=NextRaw(c,data);
            if(got<0)return Result::receiveFailed;
            if(got==0)return Result::truncated;
            size_t at=0;const size_t n=size_t(got);
            while(at<n) {
                if(state==chunk) {
                    const size_t take=left<n-at?size_t(left):n-at;
                    const Result r=Deliver(c,rs,sink,sinkContext,data+at,take,rs.totalLength);
                    if(r!=Result::ok)return r;
                    at+=take;left-=take;if(!left)state=chunkEnd;
                    continue;
                }
                const char ch=char(data[at++]);
                switch(state) {
                    case sizeLine: {
                        int v=-1;
                        if(ch>='0' && ch<='9')v=ch-'0';else if(Lower(ch)>='a' && Lower(ch)<='f')v=Lower(ch)-'a'+10;
                        if(v>=0) {if(left>>59)return Result::badAnswer;left=left*16+unsigned(v);digits=true;}
                        else if(ch=='\r' || ch==' ' || ch=='\t' || ch==';')state=ch=='\r'?sizeEnd:extension;
                        else if(ch=='\n') {if(!digits)return Result::badAnswer;state=left?chunk:trailerStart;digits=false;}
                        else return Result::badAnswer;
                        break;}
                    case extension:if(ch=='\n') {if(!digits)return Result::badAnswer;state=left?chunk:trailerStart;digits=false;}break;
                    case sizeEnd:if(ch!='\n' || !digits)return Result::badAnswer;state=left?chunk:trailerStart;digits=false;break;
                    case chunkEnd:if(ch=='\n')state=sizeLine;else if(ch!='\r')return Result::badAnswer;break;
                    case trailerStart:if(ch=='\n')return Result::ok;if(ch!='\r')state=trailer;break;
                    case trailer:if(ch=='\n')state=trailerStart;break;
                    case chunk:break;
                }
            }
        }
    }
    uint64_t left=rs.contentLength;
    while(left) {
        const int got=NextRaw(c,data);
        if(got<0)return Result::receiveFailed;
        if(got==0)return rs.contentLength==unknownLength?Result::ok:Result::truncated;
        size_t n=size_t(got);if(left!=unknownLength && n>left)n=size_t(left);
        const Result r=Deliver(c,rs,sink,sinkContext,data,n,rs.totalLength);
        if(r!=Result::ok)return r;
        if(left!=unknownLength)left-=n;
    }
    return Result::ok;
}
// One HTTP request with redirects. The body goes to `sink` (may be null to
// discard). On return c.url is the address finally used and the header of the
// last answer stays readable with Header().
inline Result Get(Client& c,const Request& rq,Response& rs,Sink sink,void* sinkContext) {
    rs=Response{};
    if(!rq.url || !Copy(c.url,urlUnits,rq.url))return Result::badUrl;
    bool firstHost=true;char first[hostUnits];first[0]=0;bool wasTls=false;
    for(unsigned hop=0;;++hop) {
        bool tls=false;
        if(!ParseUrl(c,tls))return Result::badUrl;
        if(hop && wasTls && !tls && !rq.allowInsecureRedirect)return Result::insecureRedirect;
        wasTls=tls;
        if(!hop)Copy(first,hostUnits,c.host);
        else {firstHost=true;for(size_t i=0;;++i) {if(first[i]!=c.host[i]) {firstHost=false;break;}if(!first[i])break;}}
        // r410 keep-alive: reuse the open connection only for the same host, port and scheme; anything else closes it.
        bool reused=false;
        if(c.open) {
            bool same=c.keepAlive && c.reusable && c.tls==tls && c.openPort==c.port;
            for(size_t i=0;same;++i) {if(c.openHost[i]!=c.host[i]) {same=false;break;}if(!c.host[i])break;}
            if(same)reused=true;else Close(c);
        }
        c.reusable=false;
        Result r=Result::ok;
        if(!reused) {r=Open(c,c.host,c.port,tls);if(r!=Result::ok)return r;}
        for(int fresh=0;;++fresh) {   // a reused connection the server has closed meanwhile: once more on a new one
        {   // The request is built in the header buffer, which is free until the answer arrives.
            Appender a(c.head,headUnits);
            a.Add(rq.head?"HEAD ":"GET ");a.Add(c.path[0]=='/'?"":"/");a.Add(c.path);a.Add(" HTTP/1.1\r\nHost: ");a.Add(c.host);
            if(c.port!=(tls?443:80)) {a.Add(":");a.Number(c.port);}
            a.Add("\r\nUser-Agent: ");a.Add(c.userAgent?c.userAgent:"Charon");
            a.Add(c.keepAlive?"\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: keep-alive\r\n":"\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n");
            if(rq.range) {
                a.Add("Range: bytes=");a.Number(rq.rangeStart);a.Add("-");if(rq.rangeEnd!=unknownLength)a.Number(rq.rangeEnd);
                a.Add("\r\n");
                if(rq.ifRange && rq.ifRange[0]) {a.Add("If-Range: ");a.Add(rq.ifRange);a.Add("\r\n");}
            }
            if(rq.headers && firstHost)a.Add(rq.headers);
            a.Add("\r\n");
            if(a.overflow) {Close(c);return Result::badUrl;}
            r=WriteAll(c,c.head,a.n);
            if(r!=Result::ok && reused && !fresh) {Close(c);r=Open(c,c.host,c.port,tls);if(r!=Result::ok)return r;continue;}
            if(r!=Result::ok) {Close(c);return r;}
        }
        r=ReadHead(c);
        if(r!=Result::ok && reused && !fresh) {Close(c);r=Open(c,c.host,c.port,tls);if(r!=Result::ok)return r;continue;}
        if(r!=Result::ok) {Close(c);return r;}
        break;
        }
        // "HTTP/1.x NNN"
        if(c.headSize<12 || !StartsNoCase(c.head,"HTTP/1.") || c.head[8]!=' ') {Close(c);return Result::badAnswer;}
        int status=0;for(size_t i=9;i<12;++i) {if(c.head[i]<'0' || c.head[i]>'9') {Close(c);return Result::badAnswer;}status=status*10+(c.head[i]-'0');}
        rs.status=status;rs.redirects=hop;
        char value[urlUnits];
        if((status==301 || status==302 || status==303 || status==307 || status==308) && Header(c,"Location",value,sizeof(value))) {
            Close(c);
            if(hop>=rq.maxRedirects)return Result::tooManyRedirects;
            if(!Resolve(c,value,Length(value),tls))return Result::badUrl;
            Copy(c.url,urlUnits,c.next);
            continue;
        }
        rs.chunked=false;rs.contentLength=unknownLength;
        if(Header(c,"Transfer-Encoding",value,sizeof(value))) {
            // "chunked" is always the last coding.
            const size_t n=Length(value);rs.chunked=n>=7 && StartsNoCase(value+n-7,"chunked");
        }
        if(!rs.chunked && Header(c,"Content-Length",value,sizeof(value)) && !ParseNumber(value,rs.contentLength)) {Close(c);return Result::badAnswer;}
        Header(c,"Content-Type",rs.contentType,sizeof(rs.contentType));
        if(!Header(c,"ETag",rs.validator,sizeof(rs.validator)) || (rs.validator[0]=='W' && rs.validator[1]=='/'))   // weak tags cannot be used in If-Range
            Header(c,"Last-Modified",rs.validator,sizeof(rs.validator));
        rs.partial=status==206;rs.totalLength=rs.contentLength;rs.rangeStart=0;
        if(rs.partial) {
            // "bytes START-END/TOTAL"
            if(!Header(c,"Content-Range",value,sizeof(value)) || !StartsNoCase(value,"bytes ")) {Close(c);return Result::badAnswer;}
            const char* p=value+6;uint64_t start=0,end=0;
            if(!ParseNumber(p,start)) {Close(c);return Result::badAnswer;}
            while(*p>='0' && *p<='9')++p;
            if(*p!='-' || !ParseNumber(++p,end) || end<start) {Close(c);return Result::badAnswer;}
            while(*p>='0' && *p<='9')++p;
            rs.rangeStart=start;rs.totalLength=unknownLength;
            if(*p=='/' && p[1]!='*' && !ParseNumber(p+1,rs.totalLength)) {Close(c);return Result::badAnswer;}
            if(rq.range && start!=rq.rangeStart) {Close(c);return Result::badAnswer;}   // never splice bytes from another offset
            if(rs.contentLength==unknownLength && !rs.chunked)rs.contentLength=end-start+1;
        } else if(status==416 && Header(c,"Content-Range",value,sizeof(value)) && StartsNoCase(value,"bytes */")) {
            ParseNumber(value+8,rs.totalLength);
        }
        if(rq.range && rq.rangeStart>0 && status==200) {Close(c);return Result::rangeIgnored;}
        const bool error=status>=400 || (status>=300 && status!=304);
        if(error && !rq.deliverErrorBody) {Close(c);return Result::httpStatus;}
        if(rq.head || status==204 || status==304 || (status>=100 && status<200)) {
            // r410 (agent 3): with keep-alive a HEAD answer (no body) leaves the connection usable, so a size probe
            // does not cost a second connection and handshake.
            char connection[32];
            if(c.keepAlive && status>=200 && !(Header(c,"Connection",connection,sizeof(connection)) && StartsNoCase(connection,"close")) && c.rawAt>=c.rawEnd) {
                c.reusable=true;c.openPort=c.port;Copy(c.openHost,hostUnits,c.host);
            } else Close(c);
            return Result::ok;}
        r=ReadBody(c,rs,sink,sinkContext);
        {   // keep-alive: keep the connection when the answer ended by its own length and the server did not close it
            char connection[32];
            const bool closing=Header(c,"Connection",connection,sizeof(connection)) && StartsNoCase(connection,"close");
            if(c.keepAlive && r==Result::ok && !closing && (rs.chunked || rs.contentLength!=unknownLength) && c.rawAt>=c.rawEnd) {
                c.reusable=true;c.openPort=c.port;Copy(c.openHost,hostUnits,c.host);
            } else Close(c);
        }
        return r;
    }
}
// ---- helpers ----
// A bounded text sink for small answers (JSON, small pages): always NUL terminated.
struct TextBuffer {char* data;size_t capacity;size_t size;bool overflow;};
inline bool TextSink(void* context,const uint8_t* data,size_t size) {
    auto& t=*static_cast<TextBuffer*>(context);
    for(size_t i=0;i<size;++i) {if(t.size+1>=t.capacity) {t.overflow=true;return false;}t.data[t.size++]=char(data[i]);}
    t.data[t.size]=0;return true;
}
// GET into a caller buffer. Result::sinkTooSmall when it does not fit (the buffer holds the beginning).
inline Result GetText(Client& c,const Request& rq,Response& rs,char* out,size_t cap) {
    TextBuffer t{out,cap,0,false};if(cap)out[0]=0;
    const Result r=Get(c,rq,rs,TextSink,&t);
    return r==Result::stopped && t.overflow?Result::sinkTooSmall:r;
}
// Size and resume validator of a resource without downloading it: HEAD, and
// when the server refuses HEAD, a one byte Range request.
// r410 (known issue 9, seen in VMware: one connect time-out on github.com left NVDA off the Windows media): a
// failure of the network itself is tried again, three attempts in all, as Download retries its transfers.
inline unsigned& ProbeRetries() {static unsigned count=0;return count;}
inline bool NetworkFailure(Result r) {
    return r==Result::receiveFailed || r==Result::truncated || r==Result::connectFailed || r==Result::sendFailed || r==Result::tlsFailed;
}
inline Result Probe(Client& c,const char* url,Response& rs) {
    Request rq;rq.url=url;rq.head=true;
    Result r=Get(c,rq,rs,nullptr,nullptr);
    for(unsigned again=0;again<2 && NetworkFailure(r);++again) {
        ++ProbeRetries();   // r410 (helper pw): counted, so that a trace can show a retry (CharonWsPlanDialogue.h)
        rs=Response{};r=Get(c,rq,rs,nullptr,nullptr);}
    if(r==Result::ok && rs.totalLength!=unknownLength)return r;
    Request one;one.url=url;one.range=true;one.rangeStart=0;one.rangeEnd=0;
    Response second;const Result r2=Get(c,one,second,nullptr,nullptr);
    if(r2==Result::ok) {rs=second;return r2;}
    return r==Result::ok?r:r2;
}
// A whole download that survives broken connections: when the transfer breaks
// after the start, it reconnects and continues with a Range request from the
// byte after the last one delivered (If-Range guards against the file having
// changed). `offset` is where to start (0, or the size already on disk when
// resuming an earlier attempt; then pass the validator saved from that attempt
// or null). On return `offset` is the number of bytes of the resource that the
// sink has now been given in total.
struct DownloadCount {Sink sink;void* context;uint64_t bytes;};
inline bool CountingSink(void* context,const uint8_t* data,size_t size) {
    auto& d=*static_cast<DownloadCount*>(context);d.bytes+=size;return !d.sink || d.sink(d.context,data,size);
}
inline Result Download(Client& c,const char* url,uint64_t& offset,const char* validator,Sink sink,void* sinkContext,Response& rs,unsigned maxRetries=6) {
    char* keptUrl=c.keptUrl;char* kept=c.kept;
    if(url!=keptUrl && !Copy(keptUrl,urlUnits,url))return Result::badUrl;
    if(validator!=kept)Copy(kept,sizeof(c.kept),validator?validator:"");
    unsigned failures=0;
    for(;;) {
        Request rq;rq.url=keptUrl;rq.range=offset>0;rq.rangeStart=offset;rq.ifRange=kept[0]?kept:nullptr;
        DownloadCount count{sink,sinkContext,0};
        const Result r=Get(c,rq,rs,CountingSink,&count);
        offset+=count.bytes;
        if(rs.validator[0] && (rs.status==200 || rs.status==206))Copy(kept,sizeof(c.kept),rs.validator);
        if(r==Result::httpStatus && rs.status==416 && rs.totalLength==offset)return Result::ok;   // already complete
        const bool network=r==Result::receiveFailed || r==Result::truncated || r==Result::connectFailed || r==Result::sendFailed || r==Result::tlsFailed;
        if(!network)return r;
        if(count.bytes)failures=0;
        if(++failures>maxRetries)return r;
    }
}
}  // namespace https
}  // namespace hakefi
