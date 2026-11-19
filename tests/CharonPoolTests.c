#include "../src/tts/runtime/CharonPool.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct Backing {size_t live;int fail;} Backing;
static void* allocate(void* context,size_t size) {
    Backing* backing=(Backing*)context;
    if(backing->fail)return NULL;
    void* p=malloc(size);if(p)backing->live++;return p;
}
static void release(void* context,void* pointer) {((Backing*)context)->live--;free(pointer);}
int main(void) {
    Backing backing={0};CharonPool a,b;
    CHECK(charon_pool_init(&a,&backing,allocate,release,4096));
    CHECK(charon_pool_init(&b,&backing,allocate,release,4096));
    CHECK(!charon_pool_malloc(&a,SIZE_MAX));CHECK(!charon_pool_calloc(&a,SIZE_MAX,2));
    unsigned char* p=charon_pool_calloc(&a,32,1);CHECK(p && !((uintptr_t)p%16));
    for(size_t i=0;i<32;i++)CHECK(!p[i]);
    memset(p,0xa5,32);size_t used=a.used;
    charon_pool_free(&b,p);CHECK(a.used==used && b.used==0 && backing.live==1);
    CHECK(!charon_pool_realloc(&b,p,80));
    backing.fail=1;CHECK(!charon_pool_realloc(&a,p,64));CHECK(a.used==used);
    for(size_t i=0;i<32;i++)CHECK(p[i]==0xa5);
    backing.fail=0;
    CHECK(!charon_pool_realloc(&a,p,4096));CHECK(a.used==used);
    unsigned char* grown=charon_pool_realloc(&a,p,64);CHECK(grown && backing.live==1);
    for(size_t i=0;i<32;i++)CHECK(grown[i]==0xa5);
    p=charon_pool_realloc(&a,grown,4);CHECK(p && backing.live==1);
    for(size_t i=0;i<4;i++)CHECK(p[i]==0xa5);
    CHECK(charon_pool_realloc(&a,p,4)==p);
    CHECK(!charon_pool_realloc(&a,p,0));CHECK(!a.used && !a.allocations && !backing.live);
    p=charon_pool_malloc(&a,0);CHECK(p);charon_pool_free(&a,p);
    CHECK(!a.used && !a.allocations && !backing.live);
    puts("PASS synthesis pool alignment, overflow, accounting, calloc, realloc failure preservation and instance ownership");return 0;
}
