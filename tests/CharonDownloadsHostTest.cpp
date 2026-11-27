// r410 (agent 3): host test of Charon's download scheduler (src/efi/CharonDownloads.h).
//
// Part 1 (always): a FAKE transport - simulated HTTP/1.1 servers inside this program, no network - checks ordering,
// retries, a slow connection, servers that close keep-alive, a broken body resumed by Range, 503 then success, 404,
// verification failure, the in-flight memory cap, the per-host cap, a firmware that refuses more connections, Escape.
// Part 2 (--real [N]): the same scheduler over non-blocking Winsock against the official Alpine mirror (HTTPS, TLS by
// BearSSL): the first N (default all) packages of Alpine 3.24 main, sequential one-request-per-connection as before
// versus 6 kept-alive connections at once; prints both times.
//
// Build (Windows host, from the repository root):
//   clang++ -std=c++17 -O2 -Wall -Wextra -Werror -mno-stack-arg-probe -Isrc/efi tests/CharonDownloadsHostTest.cpp
//       BUILD\bearssl-host\charon-bearssl.lib -lws2_32 -lbcrypt -o BUILD\CharonDownloadsHostTest.exe
// (-mno-stack-arg-probe: the workers run on heap stacks, which the MSVC stack probe would take for an overflow.)
// Exit code 0 only when every check passed.
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "CharonDownloads.h"
using namespace hakefi;
using https::many::Job;

// The context switch of src/tts/tvox_native/tvox_context_x64.S for the Microsoft x64 convention (rcx, rdx).
__asm__(
    ".text\n.globl tvox_context_swap\ntvox_context_swap:\n"
    " movq %rsp,0(%rcx)\n movq %rbx,8(%rcx)\n movq %rbp,16(%rcx)\n movq %r12,24(%rcx)\n movq %r13,32(%rcx)\n"
    " movq %r14,40(%rcx)\n movq %r15,48(%rcx)\n movq %rdi,56(%rcx)\n movq %rsi,64(%rcx)\n"
    " movdqu %xmm6,80(%rcx)\n movdqu %xmm7,96(%rcx)\n movdqu %xmm8,112(%rcx)\n movdqu %xmm9,128(%rcx)\n movdqu %xmm10,144(%rcx)\n"
    " movdqu %xmm11,160(%rcx)\n movdqu %xmm12,176(%rcx)\n movdqu %xmm13,192(%rcx)\n movdqu %xmm14,208(%rcx)\n movdqu %xmm15,224(%rcx)\n"
    " stmxcsr 240(%rcx)\n fnstcw 244(%rcx)\n"
    " movq 0(%rdx),%rsp\n movq 8(%rdx),%rbx\n movq 16(%rdx),%rbp\n movq 24(%rdx),%r12\n movq 32(%rdx),%r13\n"
    " movq 40(%rdx),%r14\n movq 48(%rdx),%r15\n movq 56(%rdx),%rdi\n movq 64(%rdx),%rsi\n"
    " movdqu 80(%rdx),%xmm6\n movdqu 96(%rdx),%xmm7\n movdqu 112(%rdx),%xmm8\n movdqu 128(%rdx),%xmm9\n movdqu 144(%rdx),%xmm10\n"
    " movdqu 160(%rdx),%xmm11\n movdqu 176(%rdx),%xmm12\n movdqu 192(%rdx),%xmm13\n movdqu 208(%rdx),%xmm14\n movdqu 224(%rdx),%xmm15\n"
    " ldmxcsr 240(%rdx)\n fldcw 244(%rdx)\n ret\n"
    ".globl tvox_context_controls\ntvox_context_controls:\n stmxcsr 240(%rcx)\n fnstcw 244(%rcx)\n ret\n");

static int failures=0,checks=0;
static void Check(bool ok,const char* what) {++checks;if(!ok)++failures;printf("  %s %s\n",ok?"PASS":"FAIL",what);}

// ================================================================== fake servers ==================================
struct FakeFile {std::string path,body;int breakFirst=0;int unavailableFirst=0;bool missing=false;};
struct FakeServer {
    std::string host;std::vector<FakeFile> files;
    int keepAliveMax=1000;     // requests per connection, then "Connection: close"
    int latency=1;             // rounds before each receive is answered
    size_t chunk=4096;         // bytes per receive
    int slowSlot=-1;           // this worker slot gets 1 byte per receive
    bool noHead=false;         // answers HEAD with 405
    int connections=0,maxOpen=0,open=0,requests=0;
};
struct Fake {
    std::vector<FakeServer> servers;
    int refuseFromSlot=99;     // a connect on this slot or higher is refused like a firmware without more TCP children
    uint64_t clock=0;int rounds=0;int cancelAtRound=-1;
    uint64_t inFlightPeak=0;
} fake;
constexpr int kRefused=-101;
struct Conn {FakeServer* server=nullptr;std::string in,out;size_t at=0;bool closing=false,open=false;unsigned slot=0;int requests=0;};
static Conn conns[https::many::kMaxWorkers];
static FakeFile* Find(FakeServer& s,const std::string& path) {for(auto& f:s.files)if(f.path==path)return &f;return nullptr;}
static void Wait(int rounds) {for(int i=0;i<rounds;++i)https::many::Pass();}
static int FakeConnect(void* context,const char* host,uint16_t) {
    Conn& c=*static_cast<Conn*>(context);
    if(int(c.slot)>=fake.refuseFromSlot)return kRefused;
    for(auto& s:fake.servers)if(s.host==host) {
        Wait(s.latency*2);   // the three-way handshake
        c.server=&s;c.in.clear();c.out.clear();c.at=0;c.closing=false;c.open=true;c.requests=0;
        ++s.connections;++s.open;s.maxOpen=std::max(s.maxOpen,s.open);return 0;}
    return -4;
}
static void Answer(Conn& c,const std::string& request) {
    FakeServer& s=*c.server;++s.requests;
    const bool head=request.compare(0,5,"HEAD ")==0;
    const size_t p0=request.find(' ')+1,p1=request.find(' ',p0);const std::string path=request.substr(p0,p1-p0);
    uint64_t from=0;bool range=false;
    const size_t r=request.find("Range: bytes=");
    if(r!=std::string::npos) {range=true;from=strtoull(request.c_str()+r+13,nullptr,10);}
    const bool closeAsked=request.find("Connection: close")!=std::string::npos;
    const bool last=closeAsked || ++c.requests>=s.keepAliveMax;
    if(last)c.closing=true;
    const char* connection=last?"Connection: close\r\n":"";
    FakeFile* f=Find(s,path);
    char head_[512];
    if(!f || f->missing) {snprintf(head_,sizeof head_,"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n%s\r\n",connection);c.out+=head_;return;}
    if(head && s.noHead) {snprintf(head_,sizeof head_,"HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n%s\r\n",connection);c.out+=head_;return;}
    if(!head && f->unavailableFirst>0) {--f->unavailableFirst;snprintf(head_,sizeof head_,"HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n%s\r\n",connection);c.out+=head_;return;}
    const uint64_t n=f->body.size();
    if(range && from>=n) {snprintf(head_,sizeof head_,"HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */%llu\r\nContent-Length: 0\r\n%s\r\n",(unsigned long long)n,connection);c.out+=head_;return;}
    if(range) snprintf(head_,sizeof head_,"HTTP/1.1 206 Partial\r\nContent-Length: %llu\r\nContent-Range: bytes %llu-%llu/%llu\r\nETag: \"v1\"\r\n%s\r\n",
        (unsigned long long)(n-from),(unsigned long long)from,(unsigned long long)(n-1),(unsigned long long)n,connection);
    else snprintf(head_,sizeof head_,"HTTP/1.1 200 OK\r\nContent-Length: %llu\r\nETag: \"v1\"\r\n%s\r\n",(unsigned long long)n,connection);
    c.out+=head_;
    if(head)return;
    std::string body=f->body.substr(size_t(from));
    if(f->breakFirst>0) {--f->breakFirst;body.resize(body.size()/2);c.out+=body;c.closing=true;return;}   // the connection breaks in the middle
    c.out+=body;
}
static int FakeSend(void* context,const uint8_t* data,size_t size) {
    Conn& c=*static_cast<Conn*>(context);if(!c.open)return -8;
    Wait(c.server->latency);
    c.in.append(reinterpret_cast<const char*>(data),size);
    for(size_t end;(end=c.in.find("\r\n\r\n"))!=std::string::npos;) {const std::string request=c.in.substr(0,end+4);c.in.erase(0,end+4);if(!c.closing)Answer(c,request);}
    return int(size);
}
static int FakeRecv(void* context,uint8_t* data,size_t capacity) {
    Conn& c=*static_cast<Conn*>(context);if(!c.open)return -8;
    for(int waited=0;c.at>=c.out.size();++waited) {
        if(c.closing)return 0;
        if(waited>3000)return -6;   // the idle keep-alive connection: timeout
        https::many::Pass();
    }
    Wait(c.server->latency);
    size_t n=std::min(capacity,c.out.size()-c.at);
    n=std::min(n,int(c.slot)==c.server->slowSlot?size_t(1):c.server->chunk);
    memcpy(data,c.out.data()+c.at,n);c.at+=n;
    https::many::Moved();
    if(c.at==c.out.size()) {c.out.clear();c.at=0;}
    return int(n);
}
static void FakeClose(void* context) {Conn& c=*static_cast<Conn*>(context);if(c.open && c.server)--c.server->open;c.open=false;c.out.clear();c.at=0;c.in.clear();}
static https::Transport FakeTransport(void*,unsigned slot) {
    conns[slot]=Conn{};conns[slot].slot=slot;
    https::Transport t{};t.context=&conns[slot];t.connect=FakeConnect;t.send=FakeSend;t.recv=FakeRecv;t.close=FakeClose;return t;
}
static void* Allocate(void*,size_t bytes) {return malloc(bytes);}
static void Release(void*,void* block,size_t) {free(block);}
static uint64_t FakeNow(void*) {return fake.clock;}
static bool FakeIdle(void*,bool) {fake.clock+=10;++fake.rounds;return fake.cancelAtRound<0 || fake.rounds<fake.cancelAtRound;}
static bool FakeRefused(void*,int error) {return error==kRefused;}
static https::many::Platform FakePlatform() {https::many::Platform p{};p.transport=FakeTransport;p.allocate=Allocate;p.release=Release;p.nowMs=FakeNow;p.idle=FakeIdle;p.refused=FakeRefused;return p;}

// ---- the caller's side: memory per job, an FNV hash as "verification" ----
struct Target {std::string data;uint64_t expectHash=0;bool begun=false;int resets=0;};
struct Run_ {std::vector<Target> targets;std::vector<size_t> arrivals;uint64_t lastDone=0,lastTotal=0;bool progressBackwards=false;uint64_t inFlight=0,inFlightPeak=0;};
static uint64_t Fnv(const std::string& s) {uint64_t h=1469598103934665603ull;for(unsigned char ch:s) {h^=ch;h*=1099511628211ull;}return h;}
static bool Begin(void* context,Job& j,uint64_t total) {
    auto& r=*static_cast<Run_*>(context);auto& t=r.targets[size_t(reinterpret_cast<uintptr_t>(j.user))];
    t.data.clear();if(total!=https::unknownLength)t.data.reserve(size_t(total));t.begun=true;
    r.inFlight+=total!=https::unknownLength?total:0;r.inFlightPeak=std::max(r.inFlightPeak,r.inFlight);return true;
}
static bool Write(void* context,Job& j,uint64_t offset,const uint8_t* data,size_t size) {
    auto& t=static_cast<Run_*>(context)->targets[size_t(reinterpret_cast<uintptr_t>(j.user))];
    if(offset!=t.data.size())return false;t.data.append(reinterpret_cast<const char*>(data),size);return true;
}
static void Reset(void* context,Job& j) {
    auto& r=*static_cast<Run_*>(context);auto& t=r.targets[size_t(reinterpret_cast<uintptr_t>(j.user))];
    t.data.clear();++t.resets;if(t.begun && j.total!=https::unknownLength)r.inFlight-=j.total;t.begun=false;
}
static bool Verify(void* context,Job& j) {
    auto& r=*static_cast<Run_*>(context);auto& t=r.targets[size_t(reinterpret_cast<uintptr_t>(j.user))];
    if(t.begun && j.total!=https::unknownLength)r.inFlight-=j.total;t.begun=false;
    return !t.expectHash || Fnv(t.data)==t.expectHash;
}
static void Arrived(void* context,Job& j) {static_cast<Run_*>(context)->arrivals.push_back(size_t(reinterpret_cast<uintptr_t>(j.user)));}
static void Progress(void* context,uint64_t done,uint64_t total) {
    auto& r=*static_cast<Run_*>(context);if(done<r.lastDone)r.progressBackwards=true;r.lastDone=done;r.lastTotal=total;
}
static std::string Body(size_t n,unsigned seed) {std::string s(n,'\0');uint32_t x=seed*2654435761u+1;for(auto& ch:s) {x^=x<<13;x^=x>>17;x^=x<<5;ch=char(x);}return s;}

struct Case {
    std::vector<Job> jobs;std::vector<std::string> urls;Run_ run;https::many::Summary summary{};
    void Add(const std::string& url,uint64_t size,uint64_t hash) {urls.push_back(url);run.targets.push_back(Target{});run.targets.back().expectHash=hash;sizes.push_back(size);}
    std::vector<uint64_t> sizes;
    void Go(const https::many::Options& o) {
        jobs.assign(urls.size(),Job{});
        for(size_t i=0;i<urls.size();++i) {jobs[i].url=urls[i].c_str();jobs[i].size=sizes[i];jobs[i].user=reinterpret_cast<void*>(uintptr_t(i));}
        https::many::Handler h{Begin,Write,Reset,Verify,Arrived,Progress};
        const auto p=FakePlatform();fake.rounds=0;
        summary=https::many::Run(jobs.data(),jobs.size(),h,&run,o,p);
    }
    bool Exact(size_t i,const std::string& body) const {return jobs[i].state==https::many::State::done && run.targets[i].data==body;}
};
static FakeServer& Server(const char* host) {fake.servers.push_back(FakeServer{});fake.servers.back().host=host;return fake.servers.back();}
static void Reset() {fake=Fake{};fake.servers.reserve(8);for(auto& c:conns)c=Conn{};}

static void FakeTests() {
    printf("== 1. twenty files, one server, 4 connections, sizes known\n");
    {
        Reset();auto& s=Server("mirror.test");std::vector<std::string> bodies;
        for(int i=0;i<20;++i) {bodies.push_back(Body(1000+size_t(i)*7919,unsigned(i)));s.files.push_back(FakeFile{"/p/"+std::to_string(i),bodies.back()});}
        Case c;for(int i=0;i<20;++i)c.Add("http://mirror.test/p/"+std::to_string(i),bodies[size_t(i)].size(),Fnv(bodies[size_t(i)]));
        auto o=https::many::Defaults();o.connections=4;c.Go(o);
        bool all=true;for(int i=0;i<20;++i)all=all && c.Exact(size_t(i),bodies[size_t(i)]);
        Check(c.summary.ok==20 && c.summary.failed==0 && all,"all 20 files arrive byte for byte");
        Check(s.maxOpen==4 && s.connections==4,"exactly 4 connections, each kept alive for several files (keep-alive)");
        Check(c.run.arrivals.size()==20,"each file announced once");
        Check(c.run.lastDone==c.run.lastTotal && !c.run.progressBackwards,"one combined progress, never going backwards, ends at 100%");
        bool ordered=true;for(size_t i=1;i<c.run.arrivals.size();++i)if(c.run.arrivals[i]+4<c.run.arrivals[i-1])ordered=false;
        Check(ordered,"files are taken in their order (arrival order within the connection count)");
    }
    printf("== 2. sizes unknown: a HEAD probe first, on the same kept-alive connection\n");
    {
        Reset();auto& s=Server("mirror.test");std::string a=Body(5000,1),b=Body(7000,2),d=Body(300,3);
        s.files={{"/a",a},{"/b",b},{"/d",d}};
        Case c;c.Add("http://mirror.test/a",https::unknownLength,Fnv(a));c.Add("http://mirror.test/b",https::unknownLength,Fnv(b));c.Add("http://mirror.test/d",https::unknownLength,Fnv(d));
        auto o=https::many::Defaults();o.connections=1;c.Go(o);
        Check(c.Exact(0,a) && c.Exact(1,b) && c.Exact(2,d),"three files of unknown size arrive");
        Check(s.connections==1 && s.requests==6,"one connection for 3 probes + 3 bodies (HEAD keeps the connection)");
    }
    printf("== 3. a broken body is resumed with Range; 503 twice then success; 404 fails alone\n");
    {
        Reset();auto& s=Server("mirror.test");std::string a=Body(40000,4),b=Body(9000,5),e=Body(100,6);
        s.files={{"/a",a,2},{"/b",b,0,2},{"/e",e}};s.files.push_back(FakeFile{"/gone","",0,0,true});
        Case c;c.Add("http://mirror.test/a",a.size(),Fnv(a));c.Add("http://mirror.test/b",b.size(),Fnv(b));c.Add("http://mirror.test/gone",10,0);c.Add("http://mirror.test/e",e.size(),Fnv(e));
        auto o=https::many::Defaults();o.connections=2;c.Go(o);
        Check(c.Exact(0,a) && c.jobs[0].attempts==1,"broken twice in the middle: resumed by Range inside ONE attempt, bytes exact");
        Check(c.Exact(1,b) && c.jobs[1].attempts==3,"503 twice: tried again after a pause (1 s, 2 s), third attempt arrives");
        Check(c.jobs[2].state==https::many::State::failed && c.jobs[2].result==https::Result::httpStatus && c.jobs[2].status==404 && c.jobs[2].attempts==1,"404: fails at once, not retried");
        Check(c.Exact(3,e) && c.summary.ok==3 && c.summary.failed==1,"the failed file does not stop the others");
        Check(fake.clock>=3000,"the retries waited (simulated clock " "advanced past the back-off)");
    }
    printf("== 4. the server closes keep-alive after every 2nd request; one slow connection\n");
    {
        Reset();auto& s=Server("mirror.test");s.keepAliveMax=2;s.slowSlot=0;s.chunk=1500;std::vector<std::string> bodies;
        for(int i=0;i<12;++i) {bodies.push_back(Body(3000,unsigned(100+i)));s.files.push_back(FakeFile{"/f"+std::to_string(i),bodies.back()});}
        Case c;for(int i=0;i<12;++i)c.Add("http://mirror.test/f"+std::to_string(i),3000,Fnv(bodies[size_t(i)]));
        auto o=https::many::Defaults();o.connections=3;c.Go(o);
        bool all=true;for(int i=0;i<12;++i)all=all && c.Exact(size_t(i),bodies[size_t(i)]);
        Check(all && c.summary.ok==12,"all 12 arrive although the server ends each connection after 2 answers");
        Check(s.connections>=6,"a new connection after each close (connections made: " "at least 6)");
        size_t before=0;while(before<c.run.arrivals.size() && c.run.arrivals[before]!=0)++before;
        printf("    files that arrived before the slow connection's first file: %zu\n",before);
        Check(c.run.arrivals.size()==12 && before>=8,"the slow connection (1 byte per receive) does not hold up the others: 8 or more files arrive meanwhile");
    }
    printf("== 5. verification failure\n");
    {
        Reset();auto& s=Server("mirror.test");std::string a=Body(2000,7),b=Body(2000,8);s.files={{"/a",a},{"/b",b}};
        Case c;c.Add("http://mirror.test/a",a.size(),Fnv(a)^1);c.Add("http://mirror.test/b",b.size(),Fnv(b));
        auto o=https::many::Defaults();c.Go(o);
        Check(c.jobs[0].state==https::many::State::failed && c.jobs[0].verifyFailed && c.run.targets[0].resets>=1,"a file that fails its check is failed (and its memory dropped), not announced");
        Check(c.Exact(1,b) && c.run.arrivals.size()==1,"the other file arrives");
    }
    printf("== 6. memory cap: never more than 50000 bytes in flight\n");
    {
        Reset();auto& s=Server("mirror.test");std::vector<std::string> bodies;
        for(int i=0;i<10;++i) {bodies.push_back(Body(20000,unsigned(200+i)));s.files.push_back(FakeFile{"/m"+std::to_string(i),bodies.back()});}
        Case c;for(int i=0;i<10;++i)c.Add("http://mirror.test/m"+std::to_string(i),20000,Fnv(bodies[size_t(i)]));
        auto o=https::many::Defaults();o.connections=6;o.inFlightBytes=50000;c.Go(o);
        Check(c.summary.ok==10,"all 10 arrive");
        printf("    peak in flight: %llu bytes\n",(unsigned long long)c.run.inFlightPeak);
        Check(c.run.inFlightPeak<=50000 && c.run.inFlightPeak>=40000,"peak in flight 40000..50000 bytes (two at once, not six)");
    }
    printf("== 7. two servers, per-host cap 2, 6 connections\n");
    {
        Reset();auto& s1=Server("one.test");auto& s2=Server("two.test");std::vector<std::string> bodies;
        Case c;
        for(int i=0;i<16;++i) {bodies.push_back(Body(4000,unsigned(300+i)));auto& s=(i%2)?s2:s1;s.files.push_back(FakeFile{"/x"+std::to_string(i),bodies.back()});
            c.Add(std::string("http://")+((i%2)?"two":"one")+".test/x"+std::to_string(i),4000,Fnv(bodies.back()));}
        auto o=https::many::Defaults();o.connections=6;o.perHost=2;c.Go(o);
        Check(c.summary.ok==16,"all 16 arrive from both servers");
        Check(s1.maxOpen<=2 && s2.maxOpen<=2,"never more than 2 connections to one server");
    }
    printf("== 8. the firmware refuses a 3rd connection\n");
    {
        Reset();auto& s=Server("mirror.test");fake.refuseFromSlot=2;std::vector<std::string> bodies;
        for(int i=0;i<9;++i) {bodies.push_back(Body(2500,unsigned(400+i)));s.files.push_back(FakeFile{"/r"+std::to_string(i),bodies.back()});}
        Case c;for(int i=0;i<9;++i)c.Add("http://mirror.test/r"+std::to_string(i),2500,Fnv(bodies[size_t(i)]));
        auto o=https::many::Defaults();o.connections=5;c.Go(o);
        Check(c.summary.ok==9 && c.summary.failed==0,"all 9 arrive over the 2 connections the firmware gives");
        Check(c.summary.refusals>=1 && s.maxOpen==2,"the refusal is counted and the batch goes on with 2");
        bool attempts=true;for(auto& j:c.jobs)attempts=attempts && j.attempts==1;
        Check(attempts,"a refused file is given back without using up an attempt");
    }
    printf("== 9. Escape in the middle\n");
    {
        Reset();auto& s=Server("mirror.test");s.latency=5;std::vector<std::string> bodies;
        for(int i=0;i<30;++i) {bodies.push_back(Body(30000,unsigned(500+i)));s.files.push_back(FakeFile{"/c"+std::to_string(i),bodies.back()});}
        Case c;for(int i=0;i<30;++i)c.Add("http://mirror.test/c"+std::to_string(i),30000,Fnv(bodies[size_t(i)]));
        fake.cancelAtRound=200;auto o=https::many::Defaults();o.connections=4;c.Go(o);
        Check(c.summary.cancelled && c.summary.ok<30 && c.summary.ok+c.summary.failed==30,"stopped: the batch ends, the rest is failed as cancelled");
        bool marked=true;for(auto& j:c.jobs)if(j.state!=https::many::State::done)marked=marked && j.cancelled;
        Check(marked && s.open==0,"every unfinished file says cancelled; every connection is closed");
    }
    printf("== 10. no stack overflow, workers retired\n");
    {
        Reset();auto& s=Server("mirror.test");std::string a=Body(100,9);s.files={{"/a",a}};
        Case c;c.Add("http://mirror.test/a",a.size(),Fnv(a));auto o=https::many::Defaults();o.stackBytes=64<<10;c.Go(o);
        Check(c.Exact(0,a) && !c.summary.stackOverflow && c.summary.workersUsed==1,"one file, one worker, 64 KiB stack is enough for plain HTTP");
    }
}

// ================================================================== real network (--real) ========================
struct Sock {SOCKET s=INVALID_SOCKET;};
static Sock socks[https::many::kMaxWorkers+1];
static void YieldOrSleep() {if(https::many::InWorker())https::many::Pass();else Sleep(1);}
static int RealConnect(void* context,const char* host,uint16_t port) {
    auto& k=*static_cast<Sock*>(context);
    addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;addrinfo* list=nullptr;char service[8];snprintf(service,sizeof service,"%u",unsigned(port));
    if(getaddrinfo(host,service,&hints,&list)!=0 || !list)return -4;
    SOCKET s=socket(list->ai_family,list->ai_socktype,list->ai_protocol);u_long on=1;ioctlsocket(s,FIONBIO,&on);
    connect(s,list->ai_addr,int(list->ai_addrlen));freeaddrinfo(list);
    const ULONGLONG start=GetTickCount64();
    for(;;) {
        fd_set w,e;FD_ZERO(&w);FD_ZERO(&e);FD_SET(s,&w);FD_SET(s,&e);timeval tv{0,0};
        if(select(0,nullptr,&w,&e,&tv)>0) {if(FD_ISSET(s,&e)) {closesocket(s);return -5;}break;}
        if(GetTickCount64()-start>20000) {closesocket(s);return -6;}
        YieldOrSleep();
    }
    k.s=s;return 0;
}
static int RealSend(void* context,const uint8_t* data,size_t size) {
    auto& k=*static_cast<Sock*>(context);
    for(const ULONGLONG start=GetTickCount64();;) {
        const int n=send(k.s,reinterpret_cast<const char*>(data),int(size),0);
        if(n>0) {https::many::Moved();return n;}
        if(WSAGetLastError()!=WSAEWOULDBLOCK || GetTickCount64()-start>30000)return -8;
        YieldOrSleep();
    }
}
static int RealRecv(void* context,uint8_t* data,size_t capacity) {
    auto& k=*static_cast<Sock*>(context);
    for(const ULONGLONG start=GetTickCount64();;) {
        const int n=recv(k.s,reinterpret_cast<char*>(data),int(capacity),0);
        if(n>0) {https::many::Moved();return n;}
        if(n==0)return 0;
        if(WSAGetLastError()!=WSAEWOULDBLOCK || GetTickCount64()-start>30000)return -6;
        YieldOrSleep();
    }
}
static void RealClose(void* context) {auto& k=*static_cast<Sock*>(context);if(k.s!=INVALID_SOCKET)closesocket(k.s);k.s=INVALID_SOCKET;}
static bool RealClock(void*,https::UtcTime* now) {SYSTEMTIME t;GetSystemTime(&t);*now={uint16_t(t.wYear),uint8_t(t.wMonth),uint8_t(t.wDay),uint8_t(t.wHour),uint8_t(t.wMinute),uint8_t(t.wSecond)};return true;}
static bool RealEntropy(void*,uint8_t* out,size_t size) {return BCryptGenRandom(nullptr,out,ULONG(size),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0;}
static https::Transport RealTransport(void*,unsigned slot) {
    https::Transport t{};t.context=&socks[slot];t.connect=RealConnect;t.send=RealSend;t.recv=RealRecv;t.close=RealClose;t.clock=RealClock;t.entropy=RealEntropy;return t;
}
static uint64_t RealNow(void*) {return GetTickCount64();}
static bool RealIdle(void*,bool moved) {if(!moved)Sleep(0);return true;}

static int RealTest(size_t limit) {
    printf("== real: Alpine 3.24 main packages over HTTPS (official mirror dl-cdn.alpinelinux.org)\n");
    static https::Client one;one.transport=RealTransport(nullptr,https::many::kMaxWorkers);
    static char index[4<<20];https::Response rs;
    https::Request rq;rq.url="https://dl-cdn.alpinelinux.org/alpine/v3.24/main/x86_64/APKINDEX.tar.gz";
    // The index is gzip+tar; the host test takes names and sizes from a plain list made with curl+tar instead:
    // tests read work list file given by --list (lines "name size").
    (void)index;(void)rs;(void)rq;
    FILE* f=fopen("alpine-list.txt","rb");
    if(!f) {printf("  (alpine-list.txt missing: make it from APKINDEX with tools - skipped)\n");return 0;}
    std::vector<std::string> urls;std::vector<uint64_t> sizes;char name[300];unsigned long long size;
    while(fscanf(f,"%299s %llu",name,&size)==2 && urls.size()<limit) {urls.push_back(std::string("https://dl-cdn.alpinelinux.org/alpine/v3.24/")+name);sizes.push_back(size);}
    fclose(f);
    uint64_t bytes=0;for(auto s:sizes)bytes+=s;
    printf("  %zu packages, %.1f MB\n",urls.size(),double(bytes)/1e6);
    // before: what DownloadAny did - a probe and a GET, each on its own connection
    ULONGLONG start=GetTickCount64();size_t okBefore=0;
    {
        for(size_t i=0;i<urls.size();++i) {
            https::Response probe,answer;uint64_t offset=0;
            if(https::Probe(one,urls[i].c_str(),probe)!=https::Result::ok)continue;
            if(https::Download(one,urls[i].c_str(),offset,nullptr,nullptr,nullptr,answer)==https::Result::ok && offset==sizes[i])++okBefore;
        }
    }
    const double before=double(GetTickCount64()-start)/1000;
    printf("  BEFORE (sequential, probe + GET, a new TLS connection each): %zu/%zu in %.1f s\n",okBefore,urls.size(),before);
    for(unsigned connections:{1u,6u}) {
        std::vector<Job> jobs(urls.size());
        for(size_t i=0;i<urls.size();++i) {jobs[i]=Job{};jobs[i].url=urls[i].c_str();jobs[i].size=sizes[i];}
        https::many::Handler h{[](void*,Job&,uint64_t){return true;},[](void*,Job&,uint64_t,const uint8_t*,size_t){return true;},nullptr,nullptr,nullptr,nullptr};
        https::many::Platform p{};p.transport=RealTransport;p.allocate=Allocate;p.release=Release;p.nowMs=RealNow;p.idle=RealIdle;
        auto o=https::many::Defaults();o.connections=connections;o.perHost=connections;
        start=GetTickCount64();
        const auto s=https::many::Run(jobs.data(),jobs.size(),h,nullptr,o,p);
        const double t=double(GetTickCount64()-start)/1000;
        printf("  AFTER  (%u kept-alive connection%s at once): %zu/%zu in %.1f s (%.1fx), peak workers %u\n",connections,connections>1?"s":"",s.ok,urls.size(),t,t>0?before/t:0,s.peakWorkers);
        Check(s.ok==urls.size(),"every package arrived with its signed size");
    }
    return 0;
}

int main(int argc,char** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    FakeTests();
    for(int i=1;i<argc;++i)if(!strcmp(argv[i],"--real")) {
        WSADATA wsa;WSAStartup(MAKEWORD(2,2),&wsa);
        RealTest(i+1<argc?size_t(atoi(argv[i+1])):100000);
    }
    printf("%d checks, %d failed\n",checks,failures);
    return failures?1:0;
}
