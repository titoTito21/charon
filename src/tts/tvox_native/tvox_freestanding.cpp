/* MIT-0. All algorithm locals have trivial destructors in this TU.
 * GCC builtins save/restore the stack only at the wrapper error boundary.
 * Hosted MSVC test builds use the C jump runtime; firmware GCC has no CRT jump.
 */
#define TVOX_FREESTANDING 1
#include "tvox_freestanding.h"
#include <stdint.h>
#ifdef _MSC_VER
#include <setjmp.h>
static jmp_buf jump;
#define TVOX_SETJMP() setjmp(jump)
#define TVOX_LONGJMP() longjmp(jump,1)
#else
static intptr_t jump[5];
// GCC accepts the integer buffer; Clang requires its documented void* type.
#define TVOX_SETJMP() __builtin_setjmp(reinterpret_cast<void**>(jump))
#define TVOX_LONGJMP() __builtin_longjmp(reinterpret_cast<void**>(jump),1)
#endif
struct alignas(16) Allocation {Allocation* next;size_t used,capacity;};
static CharonPool* arena;
static Allocation* blocks;
static int (*cancelled)(void*);
static void* cancelContext;
static int errorCode;
static bool busy;
static bool preparationRunning;
static void preparationCheckpoint();
[[noreturn]] static void preparationFail(int);
extern "C" int tvox_frontend_configure(CharonPool* pool,int (*callback)(void*),void* context){if(busy)return -4;if(!pool||!pool->allocate||!pool->release)return -1;arena=pool;cancelled=callback;cancelContext=context;return 0;}
/* GCC's builtin longjmp must execute in a different function from setjmp,
 * including optimized/LTO builds. Keep this boundary explicitly uninlined. */
[[noreturn]]
#ifdef _MSC_VER
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void tvox_value_fail(int error){if(preparationRunning)preparationFail(error);errorCode=error;TVOX_LONGJMP();}
void tvox_value_checkpoint(){if(preparationRunning)preparationCheckpoint();if(cancelled&&cancelled(cancelContext))tvox_value_fail(-3);}
void* tvox_value_allocate(size_t bytes){
    tvox_value_checkpoint();
    if(bytes>SIZE_MAX-sizeof(Allocation)-15)tvox_value_fail(-2);
    bytes=(bytes+15)&~size_t(15);
    if(!blocks||bytes>blocks->capacity-blocks->used){
        size_t capacity=bytes>65536?bytes:65536;
        Allocation* block=(Allocation*)charon_pool_malloc(arena,sizeof(Allocation)+capacity);
        if(!block)tvox_value_fail(-2);block->next=blocks;block->used=0;block->capacity=capacity;blocks=block;
    }
    void* result=(unsigned char*)(blocks+1)+blocks->used;blocks->used+=bytes;return result;
}
static void clearArena(){while(blocks){Allocation* next=blocks->next;charon_pool_free(arena,blocks);blocks=next;}}
#define tvox_pl_analyze tvox_pl_analyze_impl
#define tvox_normalise tvox_normalise_impl
#define tvox_pl_text tvox_pl_text_impl
#define tvox_pl_frames tvox_pl_frames_impl
#define tvox_pl_frames_ex tvox_pl_frames_ex_impl
#define tvox_en_analyze tvox_en_analyze_impl
#define tvox_en_frames_ex tvox_en_frames_ex_impl
#include "tvox_pl.cpp"
#undef tvox_pl_analyze
#undef tvox_normalise
#undef tvox_pl_text
#undef tvox_pl_frames
#undef tvox_pl_frames_ex
#undef tvox_en_analyze
#undef tvox_en_frames_ex
static_assert(std::is_trivially_destructible<Segment>::value,"jump must not skip destructors");
static_assert(std::is_trivially_destructible<APhrase>::value,"jump must not skip destructors");
static_assert(std::is_trivially_destructible<tvx::vector<Track>>::value,"jump must not skip destructors");
/* Only trivial scalar locals surround setjmp. Error code is static because a
 * changed automatic local is indeterminate after a longjmp. */
#define ENTER() if(!arena||busy)return -4;busy=true;errorCode=0;if(TVOX_SETJMP()){clearArena();busy=false;return errorCode;}
#define LEAVE(value) int result=(value);clearArena();busy=false;return result
extern "C" int tvox_pl_analyze(const char* p,size_t n,int mode,char* out,size_t cap,size_t* need){ENTER();LEAVE(tvox_pl_analyze_impl(p,n,mode,out,cap,need));}
extern "C" int tvox_normalise(const char* p,size_t n,int lang,char* out,size_t cap,size_t* need){ENTER();LEAVE(tvox_normalise_impl(p,n,lang,out,cap,need));}
extern "C" int tvox_pl_text(const char* p,size_t n,char* out,size_t cap,size_t* need){ENTER();LEAVE(tvox_pl_text_impl(p,n,out,cap,need));}
extern "C" int tvox_en_analyze(const char* p,size_t n,char* out,size_t cap,size_t* need){ENTER();LEAVE(tvox_en_analyze_impl(p,n,out,cap,need));}
extern "C" int tvox_pl_frames_ex(const char* p,size_t n,double rate,double pitch,double* out,size_t cap,size_t* need,size_t* lead,size_t* tail){ENTER();LEAVE(tvox_pl_frames_ex_impl(p,n,rate,pitch,out,cap,need,lead,tail));}
extern "C" int tvox_en_frames_ex(const char* p,size_t n,double rate,double pitch,double* out,size_t cap,size_t* need,size_t* lead,size_t* tail){ENTER();LEAVE(tvox_en_frames_ex_impl(p,n,rate,pitch,out,cap,need,lead,tail));}
extern "C" int tvox_pl_frames(const char* p,size_t n,double rate,double pitch,double* out,size_t cap,size_t* need){return tvox_pl_frames_ex(p,n,rate,pitch,out,cap,need,nullptr,nullptr);}
#include "tvox_job.inc"
