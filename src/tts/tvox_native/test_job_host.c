/* Host test allocator only. Never link this CRT shim into firmware. */
#include <stdlib.h>
#include "tvox_freestanding.h"
static CharonPool pool;
static size_t live,calls,failAt,cancelAt;
static void* allocate(void* unused,size_t bytes){(void)unused;if(failAt&&live+1>=failAt)return NULL;void* p=malloc(bytes);if(p)++live;return p;}
static void release(void* unused,void* p){(void)unused;--live;free(p);}
static int cancelled(void* unused){(void)unused;++calls;return cancelAt&&calls>=cancelAt;}
int tvox_test_setup(size_t fail,size_t cancel){if(live)return -1;failAt=fail;cancelAt=cancel;calls=0;if(!charon_pool_init(&pool,NULL,allocate,release,256*1024*1024))return -1;return tvox_frontend_configure(&pool,cancelled,NULL);}
size_t tvox_test_live(void){return live;}
size_t tvox_test_calls(void){return calls;}
