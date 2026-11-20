// r401: host proof of Charon's HTTPS client (src/efi/CharonHttps.h) and of the
// Microsoft Windows 11 ISO link flow (src/efi/CharonMsIso.h) over a Winsock
// transport. Real network, real servers; nothing is mocked.
//
// Build (Windows host, from the repository root):
//   python tools/build-charon-bearssl.py BUILD\bearssl-host --host
//   clang++ -std=c++17 -O2 -Wall -Wextra -Werror -Isrc/efi tests/CharonHttpsHostTest.cpp
//       BUILD\bearssl-host\charon-bearssl.lib -lws2_32 -lbcrypt -o BUILD\CharonHttpsHostTest.exe
// Run: CharonHttpsHostTest.exe [--language "Polish"] [--megabytes 64] [--no-microsoft] [--refusal] [--save FILE]
//   --refusal  additionally asks Microsoft for a link WITHOUT whitelisting the
//              session, to show how the anti-bot refusal is reported
//   --save     writes the downloaded beginning of the ISO to FILE
//   --time-url URL  only downloads the first --megabytes of URL and prints the rate
// Exit code 0 only when every check passed.
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "CharonHttps.h"
#include "CharonMsIso.h"
using namespace hakefi;
namespace {
struct Wire {SOCKET socket=INVALID_SOCKET;int yearShift=0;uint64_t lastShown=0;bool quiet=true;};
int WireConnect(void* context,const char* host,uint16_t port) {
    auto& w=*static_cast<Wire*>(context);
    addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;addrinfo* list=nullptr;
    char service[8];snprintf(service,sizeof(service),"%u",unsigned(port));
    if(getaddrinfo(host,service,&hints,&list)!=0 || !list)return -1;
    int result=-2;
    for(addrinfo* a=list;a;a=a->ai_next) {
        SOCKET s=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(s==INVALID_SOCKET)continue;
        DWORD timeout=30000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
        setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
        if(connect(s,a->ai_addr,int(a->ai_addrlen))==0) {w.socket=s;result=0;break;}
        closesocket(s);
    }
    freeaddrinfo(list);return result;
}
int WireSend(void* context,const uint8_t* data,size_t size) {
    const int sent=send(static_cast<Wire*>(context)->socket,reinterpret_cast<const char*>(data),int(size),0);
    return sent>0?sent:-3;
}
int WireRecv(void* context,uint8_t* data,size_t capacity) {
    const int got=recv(static_cast<Wire*>(context)->socket,reinterpret_cast<char*>(data),int(capacity),0);
    return got>=0?got:-4;
}
void WireClose(void* context) {auto& w=*static_cast<Wire*>(context);if(w.socket!=INVALID_SOCKET)closesocket(w.socket);w.socket=INVALID_SOCKET;}
bool WireClock(void* context,https::UtcTime* now) {
    SYSTEMTIME t;GetSystemTime(&t);
    now->year=uint16_t(t.wYear+static_cast<Wire*>(context)->yearShift);now->month=uint8_t(t.wMonth);now->day=uint8_t(t.wDay);
    now->hour=uint8_t(t.wHour);now->minute=uint8_t(t.wMinute);now->second=uint8_t(t.wSecond);return true;
}
bool WireEntropy(void*,uint8_t* out,size_t size) {return BCryptGenRandom(nullptr,out,ULONG(size),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0;}
bool WireProgress(void* context,uint64_t received,uint64_t total) {
    auto& w=*static_cast<Wire*>(context);
    if(!w.quiet && received-w.lastShown>=(16u<<20)) {w.lastShown=received;printf("    ... %llu MiB of %llu MiB\n",received>>20,total==https::unknownLength?0ull:total>>20);fflush(stdout);}
    return true;
}
Wire wire;
https::Client client;   // static: about 90 KiB
int failures=0;
void Check(bool ok,const char* what) {printf("%s %s\n",ok?"PASS":"FAIL",what);fflush(stdout);if(!ok)++failures;}
void Describe(const char* what,https::Result r,const https::Response& rs) {
    printf("  %s: result=%s, HTTP %d, received %llu, tlsError=%d, transportError=%d\n",what,https::ResultText(r),rs.status,
        static_cast<unsigned long long>(rs.received),client.tlsError,client.transportError);
}
// Prints an address without the secret values of its query.
void PrintUrl(const char* label,const char* url) {
    printf("  %s: ",label);
    bool value=false;size_t shown=0;
    for(size_t i=0;url[i];++i) {
        const char ch=url[i];
        if(ch=='?' || ch=='&') {value=false;putchar(ch);continue;}
        if(value) {if(++shown<=6)putchar(ch);else if(shown==7)fputs("...",stdout);continue;}
        putchar(ch);
        if(ch=='=') {bool query=false;for(size_t k=0;k<i;++k)if(url[k]=='?')query=true;if(query) {value=true;shown=0;}}
    }
    putchar('\n');
}
struct Capture {
    uint8_t* data;size_t capacity;uint64_t limit;uint64_t offset;   // offset: position of the next byte within the resource
    uint64_t base;br_sha256_context hash;FILE* file;
};
bool CaptureSink(void* context,const uint8_t* data,size_t size) {
    auto& c=*static_cast<Capture*>(context);
    bool more=true;
    if(c.offset+size>=c.limit) {size=size_t(c.limit-c.offset);more=false;}
    const uint64_t at=c.offset-c.base;
    if(at<c.capacity)memcpy(c.data+at,data,size_t(at+size<=c.capacity?size:c.capacity-at));
    br_sha256_update(&c.hash,data,size);
    if(c.file)fwrite(data,1,size,c.file);
    c.offset+=size;return more;
}
void HashText(br_sha256_context& hash,char* out) {
    uint8_t digest[32];br_sha256_out(&hash,digest);
    for(int i=0;i<32;++i)snprintf(out+i*2,3,"%02x",digest[i]);
}
}  // namespace
int main(int argc,char** argv) {
    const char* language="Polish";unsigned megabytes=64;bool microsoft=true,refusal=false;const char* save=nullptr;const char* timeUrl=nullptr;
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--language") && i+1<argc)language=argv[++i];
        else if(!strcmp(argv[i],"--megabytes") && i+1<argc)megabytes=unsigned(atoi(argv[++i]));
        else if(!strcmp(argv[i],"--no-microsoft"))microsoft=false;
        else if(!strcmp(argv[i],"--refusal"))refusal=true;
        else if(!strcmp(argv[i],"--save") && i+1<argc)save=argv[++i];
        else if(!strcmp(argv[i],"--time-url") && i+1<argc)timeUrl=argv[++i];
        else {fprintf(stderr,"unknown argument %s\n",argv[i]);return 2;}
    }
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa)!=0) {fprintf(stderr,"Winsock unavailable\n");return 2;}
    client.transport={&wire,WireConnect,WireSend,WireRecv,WireClose,WireClock,WireEntropy,WireProgress};
    client.userAgent="Charon";
    printf("sizeof(https::Client)=%zu bytes, trust anchors=%zu\n",sizeof(client),https::anchors::rootCount);
    static char text[65536];static char other[65536];
    https::Response rs;
    if(timeUrl) {
        // Only a timed partial download of a given address (no other checks).
        static uint8_t none[1];Capture capture{none,0,uint64_t(megabytes)<<20,0,0,{},nullptr};br_sha256_init(&capture.hash);
        wire.quiet=false;uint64_t offset=0;const ULONGLONG started=GetTickCount64();
        const https::Result r=https::Download(client,timeUrl,offset,nullptr,CaptureSink,&capture,rs);
        const double seconds=double(GetTickCount64()-started)/1000.0;char digest[65];HashText(capture.hash,digest);
        Describe("timed download",r,rs);
        printf("  %llu bytes in %.1f s (%.1f MiB/s), SHA-256 %s\n",static_cast<unsigned long long>(capture.offset),seconds,seconds>0?double(capture.offset)/1048576.0/seconds:0.0,digest);
        return r==https::Result::stopped || r==https::Result::ok?0:1;
    }

    // ---- (a) Debian ----
    printf("== (a) https://deb.debian.org/debian/dists/stable/Release\n");
    {
        https::Request rq;rq.url="https://deb.debian.org/debian/dists/stable/Release";rq.range=true;rq.rangeStart=0;rq.rangeEnd=4095;
        https::Result r=https::GetText(client,rq,rs,text,sizeof(text));Describe("first 4096 bytes (Range)",r,rs);
        Check(r==https::Result::ok && rs.status==206 && rs.received==4096 && rs.totalLength>100000,"Range 0-4095 answered 206 with 4096 bytes and a total size");
        Check(!strncmp(text,"Origin: Debian",14) && strstr(text,"Suite: stable"),"the Release file starts with Origin: Debian and names Suite: stable");
        char line[200];size_t n=0;while(text[n] && n<199) {line[n]=text[n]=='\n'?' ':text[n];if(++n>=150)break;}
        line[n]=0;printf("  head: %s\n",line);
        const uint64_t total=rs.totalLength;
        // Whole file with a stop in the sink is not needed; compare a middle range over https and plain http.
        https::Request mid;mid.url="https://deb.debian.org/debian/dists/stable/Release";mid.range=true;mid.rangeStart=50000;mid.rangeEnd=50999;
        r=https::GetText(client,mid,rs,text,sizeof(text));Describe("bytes 50000-50999 over https",r,rs);
        https::Response plain;mid.url="http://deb.debian.org/debian/dists/stable/Release";
        const https::Result r2=https::GetText(client,mid,plain,other,sizeof(other));Describe("bytes 50000-50999 over http",r2,plain);
        Check(r==https::Result::ok && r2==https::Result::ok && rs.received==1000 && plain.received==1000 && !memcmp(text,other,1000) && plain.totalLength==total,
            "the same range over plain http is byte-identical (plain transport path works)");
        // Redirect: the directory without the slash.
        https::Request dir;dir.url="https://deb.debian.org/debian/dists/stable";
        r=https::GetText(client,dir,rs,text,sizeof(text));Describe("directory without slash",r,rs);
        printf("  redirect target: %s\n",client.url);
        Check(r==https::Result::insecureRedirect && rs.received==0,"Debian's redirect from https to plain http is refused by default");
        dir.allowInsecureRedirect=true;
        r=https::GetText(client,dir,rs,text,sizeof(text));Describe("the same, insecure redirect allowed",r,rs);
        printf("  final address: %s (redirects: %u, chunked: %d)\n",client.url,rs.redirects,int(rs.chunked));
        Check((r==https::Result::ok || r==https::Result::sinkTooSmall) && rs.redirects>=1 && rs.status==200 && strstr(text,"Release"),"redirect followed to the directory listing");
        // Chunked transfer coding: the W3C's test resource (72200 bytes in chunks).
        https::Request chunked;chunked.url="https://jigsaw.w3.org/HTTP/ChunkedScript";
        Capture whole{nullptr,0,UINT64_MAX,0,0,{},nullptr};br_sha256_init(&whole.hash);
        r=https::Get(client,chunked,rs,CaptureSink,&whole);Describe("chunked body",r,rs);
        Check(r==https::Result::ok && rs.chunked && rs.received==72200,"a chunked body is decoded to its 72200 bytes");
        https::Request ubuntu;ubuntu.url="https://releases.ubuntu.com/";
        r=https::GetText(client,ubuntu,rs,text,sizeof(text));Describe("releases.ubuntu.com",r,rs);
        Check((r==https::Result::ok || r==https::Result::sinkTooSmall) && rs.status==200 && strstr(text,"Ubuntu"),"https://releases.ubuntu.com/ validated and read");
    }

    // ---- (d) bad certificates must be refused ----
    printf("== (d) certificates that must be refused\n");
    {
        struct Bad {const char* url;const char* why;} bad[]={
            {"https://wrong.host.badssl.com/","wrong host name"},{"https://expired.badssl.com/","expired"},
            {"https://self-signed.badssl.com/","self-signed"},{"https://untrusted-root.badssl.com/","untrusted root"}};
        for(const auto& b:bad) {
            https::Request rq;rq.url=b.url;text[0]=0;
            const https::Result r=https::GetText(client,rq,rs,text,sizeof(text));Describe(b.url,r,rs);
            const bool refusedByCertificate=r==https::Result::certificateUntrusted || r==https::Result::certificateExpired || r==https::Result::certificateWrongHost || r==https::Result::certificateBad;
            char what[200];snprintf(what,sizeof(what),"%s (%s) refused at the certificate, no HTTP exchange, no body",b.url,b.why);
            Check(refusedByCertificate && rs.status==0 && rs.received==0 && !text[0],what);
        }
        // Control: the same site with a good certificate is accepted only if its root is embedded; report, do not judge.
        https::Request good;good.url="https://badssl.com/";
        const https::Result r=https::GetText(client,good,rs,text,sizeof(text));Describe("control https://badssl.com/ (informative)",r,rs);
        // A wrong clock must refuse a good certificate.
        wire.yearShift=20;
        https::Request rq;rq.url="https://deb.debian.org/debian/dists/stable/Release";rq.head=true;
        const https::Result late=https::Get(client,rq,rs,nullptr,nullptr);Describe("deb.debian.org with the clock 20 years ahead",late,rs);
        wire.yearShift=0;
        Check(late==https::Result::certificateExpired,"a good certificate is refused as expired when the clock is 20 years ahead");
        client.transport.clock=nullptr;
        const https::Result none=https::Get(client,rq,rs,nullptr,nullptr);
        client.transport.clock=WireClock;
        Check(none==https::Result::noClock,"https without a clock is refused (no unchecked validity)");
        https::Request down;down.url="https://tls-v1-0.badssl.com:1010/";
        const https::Result old=https::Get(client,down,rs,nullptr,nullptr);Describe("TLS 1.0 only server",old,rs);
        Check(old!=https::Result::ok,"a TLS 1.0 only server is refused");
    }

    // ---- (b) Microsoft ----
    if(microsoft) {
        printf("== (b) Microsoft Windows 11 x64 ISO link, language \"%s\"\n",language);
        static msiso::Link link;
        const msiso::Result m=msiso::Windows11Link(client,language,link);
        printf("  result=%d stage=%s transport=%s HTTP %d tlsError=%d\n",int(m),link.stage?link.stage:"",https::ResultText(link.transport),link.httpStatus,client.tlsError);
        printf("  session %s, edition id %s, SKU %s, language %s, product %s\n",link.sessionId,link.editionId,link.skuId,link.language,link.product);
        if(m==msiso::Result::refused)printf("  MICROSOFT REFUSED: key=%s type=%d text=%s\n",link.errorKey,link.errorType,link.error);
        else if(m!=msiso::Result::ok)printf("  error: key=%s type=%d text=%.500s\n",link.errorKey,link.errorType,link.error);
        Check(m==msiso::Result::ok,"Microsoft returned a direct ISO link");
        if(m==msiso::Result::ok) {
            PrintUrl("ISO address",link.url);
            bool tls=false;https::Copy(client.url,https::urlUnits,link.url);https::ParseUrl(client,tls);
            printf("  ISO host: %s\n  file: %s\n  size: %llu bytes (%.2f GiB)%s\n  SHA-256 (Microsoft's page): %s\n  link expires: %s\n  validator: %s\n",client.host,link.fileName,
                static_cast<unsigned long long>(link.size),double(link.size)/double(1u<<30),link.hasSize?"":" [unknown]",link.hasSha256?link.sha256:"[not available]",link.expires,link.validator);
            Check(link.hasSize && link.size>(3ull<<30) && link.size<(12ull<<30),"the ISO size is plausible (3-12 GiB)");
            Check(strstr(link.fileName,".iso")!=nullptr,"the file name ends in .iso");
            // ---- (c) the beginning of the ISO ----
            printf("== (c) first %u MiB of the ISO\n",megabytes);
            static uint8_t first[1u<<20];
            Capture capture{first,sizeof(first),uint64_t(megabytes)<<20,0,0,{},save?fopen(save,"wb"):nullptr};br_sha256_init(&capture.hash);
            wire.quiet=false;wire.lastShown=0;
            uint64_t offset=0;const ULONGLONG started=GetTickCount64();
            https::Result r=https::Download(client,link.url,offset,nullptr,CaptureSink,&capture,rs);
            const double seconds=double(GetTickCount64()-started)/1000.0;wire.quiet=true;
            if(capture.file)fclose(capture.file);
            Describe("download with early stop",r,rs);
            char digest[65];HashText(capture.hash,digest);
            printf("  %llu bytes in %.1f s (%.1f MiB/s), total announced %llu, SHA-256 of these bytes %s\n",static_cast<unsigned long long>(capture.offset),seconds,
                seconds>0?double(capture.offset)/1048576.0/seconds:0.0,static_cast<unsigned long long>(rs.totalLength),digest);
            Check(r==https::Result::stopped && capture.offset==uint64_t(megabytes)<<20,"exactly the requested number of bytes arrived, then the sink stopped the transfer");
            Check(rs.totalLength==link.size,"the size announced by the download equals the probed size");
            Check(!memcmp(first+0x8001,"CD001",5),"ISO 9660 signature CD001 at offset 0x8001");
            bool udf=false;for(unsigned sector=16;sector<64 && !udf;++sector)udf=!memcmp(first+sector*2048+1,"NSR0",4);
            Check(udf,"UDF volume recognition (NSR0x) present");
            printf("  volume id: %.32s\n",reinterpret_cast<const char*>(first+0x8028));
            // Resume: continue from where the first transfer stopped, one more MiB.
            static uint8_t tail[1u<<20];
            Capture next{tail,sizeof(tail),offset+(1u<<20),offset,offset,{},nullptr};br_sha256_init(&next.hash);
            uint64_t resumed=offset;
            r=https::Download(client,link.url,resumed,link.validator,CaptureSink,&next,rs);Describe("resume with Range + If-Range",r,rs);
            Check(r==https::Result::stopped && rs.status==206 && rs.rangeStart==offset && next.offset==offset+(1u<<20),"resume from the stop offset answered 206 at exactly that offset");
            // And the same megabyte again by an explicit bounded Range: must be identical bytes.
            https::Request again;again.url=link.url;again.range=true;again.rangeStart=offset;again.rangeEnd=offset+(1u<<20)-1;
            static uint8_t twice[1u<<20];Capture third{twice,sizeof(twice),offset+(2u<<20),offset,offset,{},nullptr};br_sha256_init(&third.hash);
            r=https::Get(client,again,rs,CaptureSink,&third);Describe("bounded Range of the same megabyte",r,rs);
            Check(r==https::Result::ok && rs.received==(1u<<20) && !memcmp(tail,twice,sizeof(tail)),"a bounded Range returns the same megabyte, complete (Content-Length path)");
        }
        if(refusal) {
            printf("== (e) Microsoft's refusal (link asked without whitelisting the session)\n");
            static msiso::Link refused;msiso::Options options;options.whitelist=false;options.readPage=false;
            const msiso::Result e=msiso::Windows11Link(client,language,refused,options);
            printf("  result=%d stage=%s HTTP %d key=%s type=%d\n  text=%.500s\n  session %s\n",int(e),refused.stage?refused.stage:"",refused.httpStatus,refused.errorKey,refused.errorType,refused.error,refused.sessionId);
            Check(e==msiso::Result::refused,"a non-whitelisted session is reported as Microsoft's refusal, with its text");
        }
    }
    printf("== %s (%d failed)\n",failures?"FAILED":"ALL PASSED",failures);
    WSACleanup();
    return failures?1:0;
}
