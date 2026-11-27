#pragma once
// r410 (agent 3): Charon workspace - several files at once (the firmware side of CharonDownloads.h).
//
// net::many::Download(items, n, options) fetches every item over up to options.connections TCP connections at once
// (http:// and https://, keep-alive), into memory, into a file, or into the caller's own target (hooks). One loop
// drives the connections and the voice: Platform::idle below polls every TCP child, feeds the voice every 10 ms,
// looks for Escape every 100 ms and stalls 0.25 ms only when no connection moved; the background voice timer
// (BackgroundPump) is on for the batch, so even a long TLS handshake does not break the speech up.
// Each item's check() runs on the loop as soon as its body is complete (the others keep downloading); its name is
// said as it arrives - the newest name, with "and N more" when names come faster than speech.
// GPL-2.0-or-later.
#ifdef HAK_CHARON_HTTPS
namespace hakefi {
namespace ws {
namespace net {
namespace many {
using https::many::Job;
struct Item;
typedef bool (*ItemCheck)(Item& item);                                  // on the loop: false refuses the file
typedef bool (*ItemBegin)(Item& item,uint64_t total);                   // own target (worker): room for `total`
typedef bool (*ItemPut)(Item& item,uint64_t offset,const uint8_t* data,size_t size);   // own target (worker)
typedef void (*ItemDrop)(Item& item);                                   // own target: an attempt failed
struct Item {
    const char* url;           // http:// or https:// (ASCII), alive until Download returns
    uint64_t size;             // https::unknownLength: asked from the server first (HEAD)
    // the target: memory, a file, or the caller's own hooks
    Memory* memory;            // memory: reserved here (size + extra + 4096) unless it already holds `size` bytes
    size_t extra;
    const FileRef* file;       // a file, replaced
    ItemBegin begin;ItemPut put;ItemDrop drop;
    ItemCheck check;           // optional verification (sizes, SHA-256, signatures)
    const char16_t* name;      // said as it arrives (null: nothing said)
    void* user;
    // ---- results ----
    bool ok;
    https::Result result;int status,transportError;
    bool noMemory,writeFailed,checkFailed,cancelled;
    uint64_t received;
    // ---- internal ----
    bool reservedHere;efi_file_protocol* open;
};
struct Options {
    unsigned connections,perHost;   // 0: 4 and 4
    uint64_t inFlightBytes;         // 0: no cap
    const char16_t* label;          // the progress line ("Downloading")
    void (*progress)(void* context,uint64_t done,uint64_t total);   // instead of Progress(label, ...)
    void* progressContext;
    const char16_t* prefix;         // before a spoken name ("Downloaded: "), or null
    bool period;                    // a full stop after it
    bool sayRest;                   // at the end: "Also downloaded: N." for names that were never said
};
struct Outcome {size_t ok,failed;bool cancelled;unsigned peakConnections;uint64_t milliseconds;Item* firstFailure;};

// ---- the platform ----
struct Glue {tcp::State* states[https::many::kMaxWorkers];uint64_t tscPerMs,lastPump,lastKey;unsigned spoken,unsaid;};
inline Glue& L() {static Glue glue;return glue;}
inline uint64_t Tsc() {return __builtin_ia32_rdtsc();}
inline void Calibrate() {
    auto& g=L();if(g.tscPerMs)return;
    const uint64_t start=Tsc();U().host->boot->Stall(5000);
    g.tscPerMs=(Tsc()-start)/5;if(!g.tscPerMs)g.tscPerMs=1;
}
inline uint64_t Milliseconds() {return L().tscPerMs?Tsc()/L().tscPerMs:0;}
inline uint64_t PlatformNow(void*) {return Milliseconds();}
inline https::Transport PlatformTransport(void*,unsigned slot) {
    auto& g=L();
    if(!g.states[slot]) {
        g.states[slot]=static_cast<tcp::State*>(Allocate(sizeof(tcp::State)));
        if(g.states[slot])https::many::Zero(g.states[slot],sizeof(tcp::State));
    }
    // no memory for its state: a transport that refuses to connect (the scheduler then goes on with fewer)
    if(!g.states[slot]) {https::Transport t{};t.connect=[](void*,const char*,uint16_t)->int {return tcp::errMemory;};
        t.send=[](void*,const uint8_t*,size_t)->int {return tcp::errIo;};t.recv=[](void*,uint8_t*,size_t)->int {return tcp::errIo;};return t;}
    return tcp::Transport(g.states[slot]);
}
inline void* PlatformAllocate(void*,size_t bytes) {return Allocate(bytes);}
inline void PlatformRelease(void*,void* block,size_t) {Release(block);}
inline bool PlatformRefused(void*,int error) {return error==tcp::errNoTcp || error==tcp::errMemory;}
inline bool PlatformIdle(void*,bool moved) {
    auto& g=L();
    for(auto* s:g.states)if(s && s->tcp && s->tcp->Poll)s->tcp->Poll(s->tcp);
    const uint64_t now=Milliseconds();
    if(now-g.lastPump>=10) {g.lastPump=now;Pump();}
    if(now-g.lastKey>=100) {g.lastKey=now;if(EscapePressed())return false;}
    if(!moved)U().host->boot->Stall(250);
    return true;
}
inline https::many::Platform MakePlatform() {
    https::many::Platform p{};p.transport=PlatformTransport;p.allocate=PlatformAllocate;p.release=PlatformRelease;
    p.nowMs=PlatformNow;p.idle=PlatformIdle;p.refused=PlatformRefused;return p;
}

// ---- the targets ----
struct Run_ {Item* items;const Options* options;uint64_t lastDone;};
inline Item& ItemOf(Job& j) {return *static_cast<Item*>(j.user);}
inline bool Begin(void*,Job& j,uint64_t total) {
    Item& it=ItemOf(j);it.received=0;
    if(it.begin)return it.begin(it,total);
    if(it.memory) {
        Memory& m=*it.memory;
        if(m.data && total!=https::unknownLength && m.capacity>=total) {m.size=0;return true;}   // the caller's memory
        if(total==https::unknownLength)return false;                                          // memory needs a size
        m.data=nullptr;m.capacity=0;m.size=0;
        if(!Reserve(m,size_t(total)+m.extra+4096)) {it.noMemory=true;return false;}
        it.reservedHere=true;return true;
    }
    if(it.file) {
        if(OpenFile(*it.file,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&it.open)==EFI_SUCCESS && it.open) {
            if(it.open->Delete(it.open)!=EFI_SUCCESS) {it.open=nullptr;it.writeFailed=true;return false;}
            it.open=nullptr;}
        if(OpenFile(*it.file,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0,&it.open)!=EFI_SUCCESS || !it.open) {it.open=nullptr;it.writeFailed=true;return false;}
        return true;
    }
    return false;
}
inline bool Put(void*,Job& j,uint64_t offset,const uint8_t* data,size_t size) {
    Item& it=ItemOf(j);
    if(it.put) {if(!it.put(it,offset,data,size)) {it.writeFailed=true;return false;}}
    else if(it.memory) {
        Memory& m=*it.memory;
        if(offset+size>m.capacity) {it.noMemory=true;return false;}
        memcpy(m.data+offset,data,size);m.size=size_t(offset+size);
    } else if(it.open) {
        size_t wrote=size;
        if(it.open->Write(it.open,&wrote,const_cast<uint8_t*>(data))!=EFI_SUCCESS || wrote!=size) {it.writeFailed=true;return false;}
    } else return false;
    it.received=offset+size;
    return true;
}
inline void Drop(void*,Job& j) {
    Item& it=ItemOf(j);it.received=0;
    if(it.drop) {it.drop(it);return;}
    if(it.memory) {if(it.reservedHere) {Free(*it.memory);it.reservedHere=false;}else it.memory->size=0;}
    if(it.open) {it.open->Delete(it.open);it.open=nullptr;}
}
inline bool Verify(void*,Job& j) {
    Item& it=ItemOf(j);
    if(it.open) {if(it.open->Flush)it.open->Flush(it.open);it.open->Close(it.open);it.open=nullptr;}
    if(it.check && !it.check(it)) {it.checkFailed=true;return false;}
    return true;
}
// The newest name as it arrives; when the voice is still busy, it is counted and said with the next one.
inline void Arrived(void* context,Job& j) {
    auto& r=*static_cast<Run_*>(context);Item& it=ItemOf(j);it.ok=true;
    if(!it.name)return;
    auto* fx=U().host->effects;auto& g=L();
    const bool idle=!fx || !fx->SpeechIdle || fx->SpeechIdle(fx->context);
    if(!idle) {++g.unsaid;return;}
    static char16_t said[300];said[0]=0;
    if(r.options->prefix)Set(said,300,r.options->prefix);
    Cat(said,300,it.name);
    if(g.unsaid) {Cat(said,300,T(u", i jeszcze ",u", and "));CatN(said,300,g.unsaid);Cat(said,300,T(u"",u" more"));}
    if(r.options->period)Cat(said,300,u".");
    Say(said);g.unsaid=0;++g.spoken;
}
inline void Progressed(void* context,uint64_t done,uint64_t total) {
    auto& r=*static_cast<Run_*>(context);
    if(r.options->progress)r.options->progress(r.options->progressContext,done,total);
    else Progress(r.options->label?r.options->label:T(u"Pobieranie",u"Downloading"),done,total);
}

// ---- the one call ----
inline Outcome Download(Item* items,size_t count,const Options& options) {
    Outcome out{};if(!count)return out;
    Calibrate();
    auto& g=L();g.unsaid=0;g.spoken=0;g.lastPump=g.lastKey=Milliseconds();
    for(size_t i=0;i<count;++i) {
        Item& it=items[i];it.ok=false;it.result=https::Result::ok;it.status=0;it.transportError=0;
        it.noMemory=it.writeFailed=it.checkFailed=it.cancelled=false;it.received=0;it.reservedHere=false;it.open=nullptr;
        if(it.memory) {const size_t extra=it.extra;if(!it.memory->data || it.size==https::unknownLength || it.memory->capacity<it.size) {it.memory->data=nullptr;it.memory->capacity=0;}
            it.memory->size=0;it.memory->extra=extra;}
    }
    Job* jobs=static_cast<Job*>(Allocate(sizeof(Job)*count));
    if(!jobs) {for(size_t i=0;i<count;++i)items[i].noMemory=true;out.failed=count;out.firstFailure=items;return out;}
    https::many::Zero(jobs,sizeof(Job)*count);
    for(size_t i=0;i<count;++i) {jobs[i].url=items[i].url;jobs[i].size=items[i].size;jobs[i].user=&items[i];}
    Run_ run{items,&options,0};
    const https::many::Handler handler{Begin,Put,Drop,Verify,Arrived,Progressed};
    auto o=https::many::Defaults();
    o.connections=options.connections?options.connections:4;o.perHost=options.perHost?options.perHost:o.connections;
    o.inFlightBytes=options.inFlightBytes;o.userAgent="Charon";
    const auto platform=MakePlatform();
    const bool background=U().backPump!=nullptr;
    if(!background)BackgroundPump(true);
    tcp::ForgetNames();
    const uint64_t started=Milliseconds();
    const auto summary=https::many::Run(jobs,count,handler,&run,o,platform);
    out.milliseconds=Milliseconds()-started;
    tcp::ForgetNames();
    for(auto*& s:g.states)if(s) {if(s->tcp || s->child)tcp::Shut(s);Release(s);s=nullptr;}
    if(!background)BackgroundPump(false);
    for(size_t i=0;i<count;++i) {
        Item& it=items[i];const Job& j=jobs[i];
        it.result=j.result;it.status=j.status;it.transportError=j.transportError;it.cancelled=j.cancelled;
        it.ok=j.state==https::many::State::done;
        if(!it.ok) {
            if(it.open) {it.open->Delete(it.open);it.open=nullptr;}
            if(it.memory && it.reservedHere) {Free(*it.memory);it.reservedHere=false;}
            if(!out.firstFailure)out.firstFailure=&it;
        }
    }
    Release(jobs);
    out.ok=summary.ok;out.failed=summary.failed;out.cancelled=summary.cancelled;out.peakConnections=summary.peakWorkers;
    if(options.sayRest && g.unsaid && !out.cancelled) {
        static char16_t text[120];Set(text,120,T(u"Pobrano jeszcze: ",u"Also downloaded: "));CatN(text,120,g.unsaid);Cat(text,120,u".");Say(text);g.unsaid=0;}
    charontrace::Value("CHARON net: batch files",uint64_t(count));
    charontrace::Value("CHARON net: batch files failed",uint64_t(out.failed));
    charontrace::Value("CHARON net: batch connections at most",uint64_t(out.peakConnections));
    charontrace::Value("CHARON net: batch milliseconds",out.milliseconds);
    if(summary.stackOverflow)charontrace::Text("CHARON net: a worker stack overflowed");
    return out;
}
// Says why an item failed (the network's reason as net::HttpsFail says it, memory, writing, Escape).
inline void Explain(const Item& it) {
    if(it.cancelled) {Fail(T(u"Przerwano.",u"Stopped."));return;}
    if(it.noMemory) {Fail(T(u"Za mało pamięci RAM na ten plik.",u"Not enough RAM for this file."));return;}
    if(it.writeFailed) {Fail(T(u"Zapis nie powiódł się; może brakuje miejsca.",u"Writing failed; the disk may be full."));return;}
    if(it.checkFailed)return;   // the caller's check said it
    HttpsFail(it.transportError,it.result);
}
// http(s):// in a wide string -> ASCII (false: characters an address may not hold).
inline bool Ascii(const char16_t* url,char* out,size_t cap) {
    size_t n=0;for(;url[n] && n+1<cap;++n) {if(url[n]>126 || url[n]<33)return false;out[n]=char(url[n]);}
    out[n]=0;return !url[n];
}
#ifdef HAK_CHARON_TRACE
// A TRACE build only (test tooling, never in a release): when EFI\HAK\net-test.txt is on Charon's medium, the
// workspace runs the downloads it lists at start and writes the times to the serial port. Lines: "run N" (N
// connections; 0 = the single-file path net::DownloadAny, as before r410), then "URL SIZE" lines (SIZE 0: unknown).
inline bool RunLine(const uint8_t* text,size_t at,size_t size) {return at+4<=size && text[at]=='r' && text[at+1]=='u' && text[at+2]=='n' && text[at+3]==' ';}
inline void SelfTest(bool (*network)()) {
    FileRef ref;ref.volume=U().host->selfDevice;Set(ref.path,pathUnits,u"\\EFI\\HAK\\net-test.txt");
    uint8_t* text=nullptr;size_t size=0;
    if(ReadWhole(ref,1<<20,&text,&size)!=EFI_SUCCESS || !text)return;
    charontrace::Text("CHARON nettest: begins");
    if(!network || !network()) {charontrace::Text("CHARON nettest: no network");Release(text);return;}
    constexpr size_t most=400;
    static Item items[most];static Memory memory[most];static char urls[most][300];static char16_t names[most][64];
    size_t at=0;
    while(at<size) {
        // one run: "run N" then its lines
        while(at<size && (text[at]=='\r' || text[at]=='\n' || text[at]==' '))++at;
        if(!RunLine(text,at,size))break;
        at+=4;unsigned connections=0;while(at<size && text[at]>='0' && text[at]<='9')connections=connections*10+unsigned(text[at++]-'0');
        size_t n=0;
        for(;;) {
            while(at<size && (text[at]=='\r' || text[at]=='\n'))++at;
            if(at>=size || RunLine(text,at,size))break;
            size_t k=0;while(at<size && text[at]!=' ' && text[at]!='\r' && text[at]!='\n') {if(k+1<300 && n<most)urls[n][k++]=char(text[at]);++at;}
            uint64_t bytes=0;while(at<size && text[at]==' ')++at;
            while(at<size && text[at]>='0' && text[at]<='9')bytes=bytes*10+uint64_t(text[at++]-'0');
            while(at<size && text[at]!='\n')++at;
            if(n<most && k) {urls[n][k]=0;Item& it=items[n];https::many::Zero(&it,sizeof(it));memory[n]=Memory{};it.url=urls[n];it.size=bytes?bytes:https::unknownLength;it.memory=&memory[n];
                size_t b=0;for(size_t q=0;q<k;++q)if(urls[n][q]=='/')b=q+1;size_t c=0;for(;urls[n][b+c] && urls[n][b+c]!='-' && urls[n][b+c]!='.' && c+1<64;++c)names[n][c]=char16_t(uint8_t(urls[n][b+c]));names[n][c]=0;it.name=names[n];   // spoken as it arrives
                ++n;}
        }
        charontrace::Value("CHARON nettest: run, connections (0 = single-file path)",connections);
        charontrace::Value("CHARON nettest: files",uint64_t(n));
        Calibrate();const uint64_t started=Milliseconds();size_t ok=0;uint64_t bytes=0;
        if(!connections) {
            static char16_t wide[300];static FileRef none;none.volume=nullptr;none.path[0]=0;
            for(size_t i=0;i<n;++i) {
                size_t k=0;for(;urls[i][k] && k+1<300;++k)wide[k]=char16_t(uint8_t(urls[i][k]));wide[k]=0;
                uint64_t received=0;memory[i]=Memory{};
                const uint64_t one=Milliseconds();
                if(DownloadAny(wide,none,&received,&memory[i],u"net test")) {++ok;bytes+=memory[i].size;}
                charontrace::Value("CHARON nettest: file ms",Milliseconds()-one);
                Free(memory[i]);
            }
        } else {
            Options o{};o.connections=connections;o.label=u"net test";
            const Outcome r=Download(items,n,o);ok=r.ok;
            for(size_t i=0;i<n;++i) {if(items[i].ok)bytes+=memory[i].size;Free(memory[i]);}
            if(r.firstFailure)charontrace::Value("CHARON nettest: first failure, result",uint64_t(r.firstFailure->result));
        }
        charontrace::Value("CHARON nettest: files ok",uint64_t(ok));
        charontrace::Value("CHARON nettest: bytes",bytes);
        charontrace::Value("CHARON nettest: milliseconds",Milliseconds()-started);
    }
    charontrace::Text("CHARON nettest: done");
    Release(text);
}
#endif
}  // namespace many
}  // namespace net
}  // namespace ws
}  // namespace hakefi
#endif
