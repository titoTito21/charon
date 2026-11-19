/* Runs the actual freestanding objects with a hosted backing allocator.
 * This is a native synthesis test, not evidence of an EFI audio device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/tts/tvox_native/tvox_freestanding.h"
#include "../src/tts/tvox_native/tvox_pl.h"
#include "../src/tts/tvox_native/tvox_dsp_stream.h"
static void* allocate(void* context,size_t n) {(void)context;return malloc(n);}
static void release(void* context,void* p) {(void)context;free(p);}
static int cancel(void* context) {size_t* remaining=(size_t*)context;return !(*remaining)--;}
static int fail(const char* reason) {fprintf(stderr,"FAIL %s\n",reason);return 1;}
int main(void) {
    CharonPool pool;
    const char* texts[]={"Witaj w Charon.","Dysk USB, partycja 2.","Ustawienia. Dźwięki włączone.",
        "Firmware odmówiło uruchomienia. Naruszenie zasad bezpieczeństwa.",
        "Brak prawidłowych wpisów rozruchowych. Odśwież listę urządzeń."};
    if(!charon_pool_init(&pool,0,allocate,release,32u*1024*1024)
        || tvox_frontend_configure(&pool,0,0)!=0)return fail("configure");
    /* Exercise GCC's real nonlocal error boundary, then reuse the instance. */
    size_t needed=987,remaining=10;
    if(tvox_frontend_configure(&pool,cancel,&remaining)!=0)return fail("cancel configure");
    if(tvox_pl_frames(texts[0],strlen(texts[0]),1,1,0,0,&needed)!=-3
        || needed!=987 || pool.used || pool.allocations)return fail("cancel cleanup/atomicity");
    if(tvox_frontend_configure(&pool,0,0)!=0)return fail("reset configure");
    pool.limit=16;
    if(tvox_pl_frames(texts[0],strlen(texts[0]),1,1,0,0,&needed)!=-2
        || needed!=987 || pool.used || pool.allocations)return fail("OOM cleanup/atomicity");
    pool.limit=32u*1024*1024;
    for(size_t t=0;t<sizeof(texts)/sizeof(texts[0]);++t) {
        size_t frames=0,leading=0,trailing=0,total=0,nonzero=0;
        if(tvox_pl_frames_ex(texts[t],strlen(texts[t]),1,1,0,0,&frames,&leading,&trailing)!=1
            || !frames || pool.used || pool.allocations)return fail("frame query/arena cleanup");
        double* data=(double*)malloc(frames*28*sizeof(double));
        void* stream=malloc(tvox_dsp_stream_size());
        if(!data||!stream)return fail("host allocation");
        if(tvox_pl_frames_ex(texts[t],strlen(texts[t]),1,1,data,frames,&frames,&leading,&trailing)!=0
            || pool.used || pool.allocations)return fail("frontend/arena cleanup");
        if(tvox_dsp_stream_begin(stream,data,frames,leading,trailing,1,1)!=0)return fail("DSP begin");
        int done=0;size_t calls=0;
        while(!done && calls++<frames*4+100) {
            short block[257];size_t written=0;
            done=tvox_dsp_stream_render(stream,block,257,&written);
            if(done<0||written>257)return fail("bounded DSP render");
            total+=written;for(size_t i=0;i<written;++i)nonzero+=block[i]!=0;
        }
        if(done!=1||!total||!nonzero)return fail("PCM completion");
        if(tvox_dsp_stream_begin(stream,data,frames,leading,trailing,1,1)!=0)return fail("restart");
        tvox_dsp_stream_cancel(stream);
        short out=123;size_t written=99;
        if(tvox_dsp_stream_render(stream,&out,1,&written)!=1||written||out!=123)return fail("cancel");
        printf("PASS dynamic PL text %zu: %zu frames, %zu PCM samples, %zu nonzero\n",t,frames,total,nonzero);
        free(stream);free(data);
    }
    return 0;
}
