#pragma once
// r410 (agent 3): several downloads at once. The user (2026-11-26): "packages in all the CharonDOSes, Alpines and
// others must download asynchronously, several at once, because it drags on terribly" - the Charon Hypervisor's 151
// Alpine packages took 25 minutes over ONE synchronous connection with a TLS handshake per file.
//
// How: a batch of files is fetched by up to Options::connections WORKERS. Each worker is one https::Client (its own
// TCP connection, its own BearSSL context, HTTP/1.1 keep-alive, so one handshake serves many files) running on its
// OWN STACK (cooperative fibers, switched by tvox_context_swap, the routine TVox already uses). The client code stays
// exactly as it is - redirects, chunked bodies, Range resumption, certificate checks - and a transport that would
// have to wait (a connect, a send, a receive that has not completed) YIELDS to the loop instead of stalling. The loop
// resumes every worker in turn, then calls Platform::idle once: there the firmware glue polls every TCP child, feeds
// the voice and looks for Escape, and stalls a little only when no worker moved. So one loop drives all connections
// and the voice; nothing waits inside a worker.
//
// Per file: an optional size probe (HEAD) when the size is not known, the caller's begin() (memory, a file), the body
// through write(), then - on the loop's own stack, while the other workers keep downloading - verify() (sizes,
// SHA-256, signed indexes: the caller's) and arrived() (the spoken name). A transfer that breaks is resumed by
// https::Download (Range); a file that still fails is tried again later with a growing pause (1, 2, 4 s); a file that
// fails for good never stops the others. Escape (Platform::idle returning false) stops all of them.
//
// Limits: Options::connections (at most kMaxWorkers), Options::perHost (be polite to one server), and
// Options::inFlightBytes (the sum of sizes being fetched at once; 0 = no cap). When the firmware refuses another
// TCP child (Platform::refused), the batch carries on with the connections it has.
//
// This file knows nothing about firmware: tests/CharonDownloadsHostTest.cpp runs it with a fake transport.
// GPL-2.0-or-later.
#include "CharonHttps.h"
#include "../tts/tvox_native/tvox_context.h"
namespace hakefi {
namespace https {
namespace many {
constexpr unsigned kMaxWorkers=8;
constexpr size_t kDefaultStack=size_t(256)<<10;   // BearSSL's RSA/EC work, the voice's timer callback and firmware
                                                   // interrupt handlers all run on the stack that is current
enum class State : uint8_t {waiting,running,complete,done,failed};
struct Job {
    // ---- given by the caller ----
    const char* url;          // http:// or https://, kept alive by the caller until Run returns
    uint64_t size;            // the expected size, or unknownLength (then a HEAD probe asks for it)
    void* user;               // the caller's own data for this file
    // ---- results ----
    State state;
    Result result;            // the last transfer's result
    int status;               // its HTTP status
    int transportError,tlsError;
    unsigned attempts;        // transfers started
    bool noMemory;            // begin() refused it
    bool verifyFailed;        // verify() refused it
    bool cancelled;           // stopped by Escape
    uint64_t total;           // the size the transfer used (size, or the probe's answer)
    uint64_t received;        // bytes given to write() in the current attempt
    // ---- internal ----
    uint64_t notBefore;       // a retry waits until then (ms of Platform::nowMs)
    char host[hostUnits];
};
// The caller's side. begin/write/reset run on a worker's stack (keep them small: no voice, no big locals);
// verify/arrived/progress run on the loop's stack.
struct Handler {
    bool (*begin)(void* context,Job& job,uint64_t total);                                   // false: no room (memory)
    bool (*write)(void* context,Job& job,uint64_t offset,const uint8_t* data,size_t size);   // false: writing failed
    void (*reset)(void* context,Job& job);          // an attempt failed: drop what it wrote (optional)
    bool (*verify)(void* context,Job& job);         // the whole body is there: check it (optional); false fails the file
    void (*arrived)(void* context,Job& job);        // verified (optional): say its name
    void (*progress)(void* context,uint64_t done,uint64_t total);   // ONE combined progress (optional)
};
// Plain aggregate (Charon runs no constructors for statics): take Defaults() and change what differs.
struct Options {
    unsigned connections;        // workers at once
    unsigned perHost;            // at most this many to one server
    unsigned attempts;           // transfers per file (each one already resumes a broken body)
    unsigned downloadRetries;    // reconnections inside one transfer (https::Download's maxRetries)
    uint64_t inFlightBytes;      // cap on the sum of the sizes being fetched (0: none)
    uint32_t backoffMs;          // first pause before a retry; doubles
    size_t stackBytes;
    const char* userAgent;
};
inline Options Defaults() {Options o{};o.connections=4;o.perHost=4;o.attempts=3;o.downloadRetries=2;o.inFlightBytes=0;o.backoffMs=1000;o.stackBytes=kDefaultStack;o.userAgent=nullptr;return o;}
struct Platform {
    void* context;
    Transport (*transport)(void* context,unsigned slot);   // a transport for worker `slot` (its own connection)
    void* (*allocate)(void* context,size_t bytes);
    void (*release)(void* context,void* block,size_t bytes);
    uint64_t (*nowMs)(void* context);
    bool (*idle)(void* context,bool moved);                 // once per round; false: stop everything (Escape)
    bool (*refused)(void* context,int transportError);      // optional: this error means "no more connections"
};
struct Summary {size_t ok,failed;bool cancelled;unsigned workersUsed,peakWorkers,refusals;bool stackOverflow;};

// ---------------------------------------------------------------------------------------------------------------
struct Batch;
struct Worker {
    TVoxContext context;
    Client client;
    Response probe,answer;
    Batch* batch;Job* job;unsigned slot;
    bool exited,idle;
    uint8_t* stack;size_t stackBytes;
    void* block;   // what Platform::allocate gave (the Worker itself is aligned to 16 inside it: TVoxContext, SSE stores)
};
struct Batch {   // trivially constructible: Run keeps it in a zero-initialized static
    Job* jobs;size_t count;const Handler* handler;void* context;Options options;const Platform* platform;
    Worker* workers[kMaxWorkers];unsigned limit,alive;
    bool cancelled;uint64_t moved;unsigned refusals,used,peak;bool overflow;
};
static_assert(__is_trivially_constructible(Batch),"many::Batch must need no constructor");
struct Shared {TVoxContext loop;Worker* current;Batch* batch;};
inline Shared& G() {static Shared shared;return shared;}   // zero at load time, no constructor
static_assert(__is_trivially_constructible(Shared),"many::Shared must need no constructor");
constexpr uint64_t kCanary=0x43686172306e5354ull;   // "Char0nST" at the bottom of every worker stack

// ---- what a transport calls ----
inline bool InWorker() {return G().current!=nullptr;}
inline bool Cancelled() {return G().batch && G().batch->cancelled;}
inline uint64_t NowMs() {auto* b=G().batch;return b && b->platform->nowMs?b->platform->nowMs(b->platform->context):0;}
// Back to the loop; returns when the loop resumes this worker. Outside a worker: nothing.
inline void Pass() {
    auto& g=G();Worker* w=g.current;if(!w)return;
    tvox_context_swap(&w->context,&g.loop);
}
inline void Moved() {if(G().batch)++G().batch->moved;}   // a transport got or sent bytes

// ---- helpers ----
inline void Zero(void* p,size_t n) {auto* b=static_cast<uint8_t*>(p);for(size_t i=0;i<n;++i)b[i]=0;}
inline bool SameText(const char* a,const char* b) {for(size_t i=0;;++i) {if(a[i]!=b[i])return false;if(!a[i])return true;}}
inline void HostOf(const char* url,char* host) {
    size_t at=0,n=0;
    if(StartsNoCase(url,"https://"))at=8;else if(StartsNoCase(url,"http://"))at=7;
    while(url[at] && url[at]!='/' && url[at]!='?' && url[at]!='#' && n+1<hostUnits)host[n++]=Lower(url[at++]);
    host[n]=0;
}
inline bool Retryable(const Job& j) {
    if(NetworkFailure(j.result))return true;
    return j.result==Result::httpStatus && (j.status>=500 || j.status==429 || j.status==408);
}
inline uint64_t Now(const Batch& b) {return b.platform->nowMs?b.platform->nowMs(b.platform->context):0;}

// ---- the worker ----
inline bool WorkerSink(void* context,const uint8_t* data,size_t size) {
    auto& w=*static_cast<Worker*>(context);Job& j=*w.job;auto& b=*w.batch;
    if(j.total!=unknownLength && j.received+size>j.total)return false;   // more than announced: refused (Result::stopped)
    if(!b.handler->write(b.context,j,j.received,data,size))return false;
    j.received+=size;++b.moved;
    return true;
}
inline bool WorkerProgress(void*,uint64_t,uint64_t) {return !Cancelled();}
inline unsigned Busy(const Batch& b,const char* host,const Worker* except) {
    unsigned n=0;
    for(unsigned i=0;i<kMaxWorkers;++i) {const Worker* w=b.workers[i];if(w && w!=except && !w->exited && w->job && SameText(w->job->host,host))++n;}
    return n;
}
inline uint64_t InFlight(const Batch& b) {   // being fetched, or fetched and not yet checked
    uint64_t n=0;
    for(size_t i=0;i<b.count;++i) {const Job& j=b.jobs[i];if((j.state==State::running || j.state==State::complete) && j.total!=unknownLength)n+=j.total;}
    return n;
}
inline bool Eligible(const Batch& b,const Job& j,const Worker& w,uint64_t now) {
    if(j.state!=State::waiting || j.notBefore>now)return false;
    if(b.options.perHost && Busy(b,j.host,&w)>=b.options.perHost)return false;
    if(b.options.inFlightBytes && j.size!=unknownLength) {
        const uint64_t busy=InFlight(b);
        if(busy && busy+j.size>b.options.inFlightBytes)return false;
    }
    return true;
}
// The next file for worker w; null: nothing left for it (it ends).
inline Job* Take(Worker& w) {
    auto& b=*w.batch;
    for(;;) {
        if(b.cancelled)return nullptr;
        const uint64_t now=Now(b);Job* pick=nullptr;size_t waiting=0;
        const bool kept=w.client.open && w.client.reusable;
        for(size_t i=0;i<b.count;++i) {
            Job& j=b.jobs[i];
            if(j.state==State::waiting)++waiting;
            if(!Eligible(b,j,w,now))continue;
            if(!pick)pick=&j;
            if(kept && SameText(j.host,w.client.openHost)) {pick=&j;break;}   // the open connection serves it
        }
        if(pick)return pick;
        // Files wait (a pause before a retry, or a cap): stay, unless enough other idle workers stay for them.
        unsigned others=0;
        for(unsigned i=0;i<kMaxWorkers;++i) {const Worker* o=b.workers[i];if(o && o!=&w && !o->exited && o->idle)++others;}
        if(!waiting || others>=waiting)return nullptr;
        w.idle=true;Pass();w.idle=false;
    }
}
inline void Fail(Job& j,Batch& b,bool retry) {
    if(b.handler->reset)b.handler->reset(b.context,j);
    j.received=0;
    if(retry && !b.cancelled && j.attempts<b.options.attempts) {
        uint64_t pause=b.options.backoffMs;for(unsigned k=1;k<j.attempts && pause<60000;++k)pause*=2;
        j.notBefore=Now(b)+pause;j.state=State::waiting;
    } else {j.cancelled=b.cancelled;j.state=State::failed;}
}
inline void RunJob(Worker& w,Job& j) {
    auto& b=*w.batch;auto& c=w.client;
    j.state=State::running;j.received=0;++j.attempts;j.noMemory=false;
    w.job=&j;
    uint64_t total=j.size;
    if(total==unknownLength) {
        const Result r=Probe(c,j.url,w.probe);
        j.result=r;j.status=w.probe.status;j.transportError=c.transportError;j.tlsError=c.tlsError;
        if(r!=Result::ok) {
            // the firmware has no more connections for us: give the file back and end this worker
            if(r==Result::connectFailed && b.alive>1 && b.platform->refused && b.platform->refused(b.platform->context,c.transportError)) {
                --j.attempts;j.state=State::waiting;w.job=nullptr;++b.refusals;b.limit=b.alive-1;w.exited=true;return;}
            Fail(j,b,Retryable(j));w.job=nullptr;return;
        }
        total=w.probe.totalLength!=unknownLength?w.probe.totalLength:w.probe.contentLength;
    }
    j.total=total;
    if(!b.handler->begin || !b.handler->begin(b.context,j,total)) {j.noMemory=true;j.result=Result::sinkTooSmall;Fail(j,b,false);w.job=nullptr;return;}
    uint64_t offset=0;
    const Result r=Download(c,j.url,offset,j.size==unknownLength && w.probe.validator[0]?w.probe.validator:nullptr,WorkerSink,&w,w.answer,b.options.downloadRetries);
    j.result=r;j.status=w.answer.status;j.transportError=c.transportError;j.tlsError=c.tlsError;
    if(r==Result::ok && (total==unknownLength || j.received==total)) {
        if(total==unknownLength)j.total=j.received;
        j.state=State::complete;   // the loop verifies it while this worker goes on
    } else if(r==Result::connectFailed && !j.received && b.alive>1 && b.platform->refused && b.platform->refused(b.platform->context,c.transportError)) {
        if(b.handler->reset)b.handler->reset(b.context,j);
        --j.attempts;j.state=State::waiting;++b.refusals;b.limit=b.alive-1;w.job=nullptr;w.exited=true;return;
    } else {
        const bool retry=r==Result::ok?true:Retryable(j);   // ok but short: the server's answer was cut, try again
        if(r==Result::ok)j.result=Result::truncated;
        Fail(j,b,retry && !(r==Result::stopped));
    }
    w.job=nullptr;
}
inline void WorkerMain(Worker& w) {
    while(!w.exited) {
        Job* j=Take(w);
        if(!j)break;
        RunJob(w,*j);
    }
    Close(w.client);
}
[[noreturn]] inline void WorkerEntry() {
    Worker* w=G().current;
    WorkerMain(*w);
    w->exited=true;
    for(;;)tvox_context_swap(&w->context,&G().loop);
}

// ---- the loop ----
inline bool Spawn(Batch& b,unsigned slot) {
    const Platform& p=*b.platform;
    void* block=p.allocate(p.context,sizeof(Worker)+16);   // firmware pools give 8-byte alignment only
    auto* w=reinterpret_cast<Worker*>((reinterpret_cast<uintptr_t>(block)+15)&~uintptr_t(15));
    if(!block)return false;
    Zero(w,sizeof(Worker));w->block=block;
    w->stackBytes=b.options.stackBytes<(size_t(64)<<10)?(size_t(64)<<10):b.options.stackBytes;
    w->stack=static_cast<uint8_t*>(p.allocate(p.context,w->stackBytes));
    if(!w->stack) {p.release(p.context,block,sizeof(Worker)+16);return false;}
    *reinterpret_cast<uint64_t*>(w->stack)=kCanary;
    w->batch=&b;w->slot=slot;
    w->client.transport=p.transport(p.context,slot);
    w->client.transport.progress=WorkerProgress;
    w->client.userAgent=b.options.userAgent;
    w->client.keepAlive=true;
    const uintptr_t top=(reinterpret_cast<uintptr_t>(w->stack)+w->stackBytes)&~uintptr_t(15);
    w->context.rsp=top-48;
    *reinterpret_cast<uintptr_t*>(top-48)=reinterpret_cast<uintptr_t>(&WorkerEntry);
    tvox_context_controls(&w->context);
    b.workers[slot]=w;++b.alive;++b.used;if(b.alive>b.peak)b.peak=b.alive;
    return true;
}
inline void Retire(Batch& b,unsigned slot) {
    Worker* w=b.workers[slot];const Platform& p=*b.platform;
    if(*reinterpret_cast<const uint64_t*>(w->stack)!=kCanary)b.overflow=true;
    p.release(p.context,w->stack,w->stackBytes);p.release(p.context,w->block,sizeof(Worker)+16);
    b.workers[slot]=nullptr;--b.alive;
}
inline void Report(Batch& b,uint64_t& lastAt,bool force) {
    if(!b.handler->progress)return;
    const uint64_t now=Now(b);
    if(!force && now<lastAt+250)return;
    lastAt=now;uint64_t done=0,total=0;
    for(size_t i=0;i<b.count;++i) {
        const Job& j=b.jobs[i];
        const uint64_t size=j.size!=unknownLength?j.size:j.total!=unknownLength?j.total:0;
        total+=size;
        if(j.state==State::done || j.state==State::complete)done+=size;else if(j.state==State::running)done+=j.received<size?j.received:size;
    }
    b.handler->progress(b.context,done,total);
}
// Fetches every job. Jobs are taken in their order. Returns when every job is done or failed (or after Escape).
inline Summary Run(Job* jobs,size_t count,const Handler& handler,void* context,const Options& options,const Platform& platform) {
    static Batch batch;   // zero-initialized; one batch at a time
    Summary s{};
    if(G().batch) {for(size_t i=0;i<count;++i) {jobs[i].state=State::failed;jobs[i].result=Result::noTransport;}s.failed=count;return s;}   // not reentrant
    Zero(&batch,sizeof(batch));
    Batch& b=batch;b.jobs=jobs;b.count=count;b.handler=&handler;b.context=context;b.options=options;b.platform=&platform;
    if(b.options.connections<1)b.options.connections=1;
    if(b.options.connections>kMaxWorkers)b.options.connections=kMaxWorkers;
    if(b.options.attempts<1)b.options.attempts=1;
    b.limit=b.options.connections;
    for(size_t i=0;i<count;++i) {
        Job& j=jobs[i];
        j.state=State::waiting;j.result=Result::ok;j.status=0;j.transportError=j.tlsError=0;j.attempts=0;
        j.noMemory=j.verifyFailed=j.cancelled=false;j.total=j.size;j.received=0;j.notBefore=0;
        HostOf(j.url?j.url:"",j.host);
        if(!j.url) {j.state=State::failed;j.result=Result::badUrl;}
    }
    G().batch=&b;
    uint64_t lastReport=0;
    for(;;) {
        // more workers while files wait for one
        const uint64_t now=Now(b);size_t ready=0,waiting=0;
        for(size_t i=0;i<count;++i)if(jobs[i].state==State::waiting) {++waiting;if(jobs[i].notBefore<=now)++ready;}
        unsigned idle=0;for(unsigned i=0;i<kMaxWorkers;++i)if(b.workers[i] && b.workers[i]->idle)++idle;
        while(!b.cancelled && b.alive<b.limit && ready>idle) {
            unsigned slot=kMaxWorkers;for(unsigned i=0;i<kMaxWorkers;++i)if(!b.workers[i]) {slot=i;break;}
            if(slot==kMaxWorkers || !Spawn(b,slot)) {
                if(!b.alive)   // not one worker could be made (no memory): what waits cannot be fetched
                    for(size_t i=0;i<count;++i)if(jobs[i].state==State::waiting) {jobs[i].state=State::failed;jobs[i].noMemory=true;jobs[i].result=Result::noTransport;}
                b.limit=b.alive?b.alive:1;break;}
            ++idle;   // it takes one file as soon as it runs
        }
        // one turn for every worker
        const uint64_t before=b.moved;
        for(unsigned i=0;i<kMaxWorkers;++i) {
            Worker* w=b.workers[i];if(!w)continue;
            G().current=w;
            tvox_context_swap(&G().loop,&w->context);
            G().current=nullptr;
            if(*reinterpret_cast<const uint64_t*>(w->stack)!=kCanary) {b.overflow=true;b.cancelled=true;}
            if(w->exited)Retire(b,i);
        }
        // finished bodies: checked here, while the other connections go on
        for(size_t i=0;i<count;++i) {
            Job& j=jobs[i];if(j.state!=State::complete)continue;
            if(handler.verify && !handler.verify(context,j)) {j.verifyFailed=true;j.state=State::failed;if(handler.reset)handler.reset(context,j);}
            else {j.state=State::done;if(handler.arrived)handler.arrived(context,j);}
        }
        Report(b,lastReport,false);
        waiting=0;for(size_t i=0;i<count;++i)if(jobs[i].state==State::waiting || jobs[i].state==State::running || jobs[i].state==State::complete)++waiting;
        if(!b.alive && (!waiting || b.cancelled))break;
        if(!platform.idle(platform.context,b.moved!=before))b.cancelled=true;
    }
    for(size_t i=0;i<count;++i) {
        Job& j=jobs[i];
        if(j.state==State::waiting || j.state==State::running) {j.state=State::failed;j.cancelled=b.cancelled;}
        if(j.state==State::done)++s.ok;else ++s.failed;
    }
    Report(b,lastReport,true);
    s.cancelled=b.cancelled;s.workersUsed=b.used;s.peakWorkers=b.peak;s.refusals=b.refusals;s.stackOverflow=b.overflow;
    G().batch=nullptr;
    return s;
}
}  // namespace many
}  // namespace https
}  // namespace hakefi
