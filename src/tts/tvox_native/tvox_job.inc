/* MIT-0. Preserve whole frontend activation on a private stack.
 * No hosted coroutine runtime, utterance segmentation, or cached speech.
 */
#include "tvox_context.h"
static constexpr size_t PREPARATION_STACK_BYTES=1024*1024;
struct Preparation {
    TVoxContext caller{},worker{};
    void* stack=nullptr;
    const char* text=nullptr;
    size_t bytes=0,budget=0,charged=0;
    int (*yield)(void*)=nullptr;
    void* yieldContext=nullptr;
    double rate=1,pitch=1;
    bool english=false;
    int status=0;
    const double* frames=nullptr;
    size_t count=0,leading=0,trailing=0;
};
static Preparation preparation;
static_assert(std::is_trivially_destructible<Preparation>::value,"cancel must not skip cleanup");
static void preparationCheckpoint(){
    while(!preparation.budget || (preparation.yield && preparation.yield(preparation.yieldContext)))
        tvox_context_swap(&preparation.worker,&preparation.caller);
    --preparation.budget;++preparation.charged;
    // Fixed large stack, no recursive frontend. Detect approach to guard area
    // at checkpoints; this is not hardware guard-page overflow protection.
    unsigned char position;
    if((uintptr_t)&position<(uintptr_t)preparation.stack+16384)preparationFail(-2);
}
[[noreturn]] static void preparationFail(int error){
    preparation.status=error;
    tvox_context_swap(&preparation.worker,&preparation.caller);
    // Completed/error activations are never resumed by Pump.
    for(;;)tvox_context_swap(&preparation.worker,&preparation.caller);
}
[[noreturn]] static void preparationEntry(){
    auto phrases=acousticPhrases(decode(preparation.text,preparation.bytes),preparation.english);
    auto seg=segments(phrases,preparation.rate*ADAM_RATE,preparation.english);
    size_t total=0;for(auto& s:seg){TVOX_CHECKPOINT();total+=s.n;if(total>500000)tvox_value_fail(-1);}
    auto track=tracks(seg);auto f0=pitchTrack(seg,phrases,preparation.pitch,preparation.english);
    auto frames=buildFrames(seg,track,f0,preparation.english);
    preparation.frames=frames.empty()?nullptr:frames[0].begin();
    preparation.count=frames.size();preparation.leading=seg.front().n;
    preparation.trailing=seg.size()>1?seg.back().n:0;
    preparation.status=1;
    tvox_context_swap(&preparation.worker,&preparation.caller);
    for(;;)tvox_context_swap(&preparation.worker,&preparation.caller);
}
static int beginPreparation(const char* text,size_t bytes,double rate,double pitch,bool english){
    if(!arena||busy)return -4;
    if(!text||bytes>65536||!tvx::isfinite(rate)||!tvx::isfinite(pitch)||rate<.1||rate>10||pitch<=0||pitch>10)return -1;
    void* stack=charon_pool_malloc(arena,PREPARATION_STACK_BYTES);
    if(!stack)return -2;
    preparation=Preparation{};preparation.stack=stack;preparation.text=text;preparation.bytes=bytes;
    preparation.rate=rate;preparation.pitch=pitch;preparation.english=english;
    uintptr_t top=((uintptr_t)stack+PREPARATION_STACK_BYTES)&~uintptr_t(15);
    // ret enters with SP == 8 mod16; reserve Windows x64 shadow space too.
    preparation.worker.rsp=top-48;
    *(uintptr_t*)(top-48)=(uintptr_t)&preparationEntry;
    tvox_context_controls(&preparation.worker);busy=true;return 0;
}
extern "C" int tvox_frontend_begin_pl(const char* text,size_t bytes,double rate,double pitch){return beginPreparation(text,bytes,rate,pitch,false);}
extern "C" int tvox_frontend_begin_en(const char* text,size_t bytes,double rate,double pitch){return beginPreparation(text,bytes,rate,pitch,true);}
extern "C" int tvox_frontend_pump_until(size_t budget,size_t* consumed,int (*yield)(void*),void* context){
    if(!busy||!preparation.stack||preparationRunning)return -4;
    if(!budget||budget>1000000||!consumed)return -1;
    if(preparation.status){*consumed=0;return preparation.status;}
    preparation.budget=budget;preparation.charged=0;preparationRunning=true;
    preparation.yield=yield;preparation.yieldContext=context;
    tvox_context_swap(&preparation.caller,&preparation.worker);
    preparationRunning=false;*consumed=preparation.charged;return preparation.status;
}
extern "C" int tvox_frontend_pump(size_t budget,size_t* consumed){
    return tvox_frontend_pump_until(budget,consumed,nullptr,nullptr);
}
extern "C" int tvox_frontend_frames(const double** data,size_t* count,size_t* leading,size_t* trailing){
    if(preparationRunning||!busy||!preparation.stack)return -4;
    if(!data||!count||!leading||!trailing)return -1;
    if(preparation.status!=1)return preparation.status<0?preparation.status:0;
    *data=preparation.frames;*count=preparation.count;*leading=preparation.leading;*trailing=preparation.trailing;return 1;
}
extern "C" int tvox_frontend_cancel(){
    if(preparationRunning)return -4;
    if(!preparation.stack)return busy?-4:0;
    clearArena();charon_pool_free(arena,preparation.stack);preparation=Preparation{};busy=false;return 0;
}
