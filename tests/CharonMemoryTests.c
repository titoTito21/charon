/* Hosted differential test. Compile CharonMemory.c separately with
 * -ffreestanding -fno-builtin and rename ONLY that object's public symbols:
 * memcpy/memmove/memset/memcmp/strlen -> charon_test_<name> (objcopy --redefine-sym
 * or per-object -D flags). Link this test normally against independent libc.
 * No asserts/debug dialogs. No GUI, allocation or runtime source changes. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void* charon_test_memcpy(void*,const void*,size_t);
extern void* charon_test_memmove(void*,const void*,size_t);
extern void* charon_test_memset(void*,int,size_t);
extern int charon_test_memcmp(const void*,const void*,size_t);
extern size_t charon_test_strlen(const char*);
static unsigned long checks;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1; } } while(0)
static int sign(int v) {return (v>0)-(v<0);}
static void pattern(unsigned char* p,size_t n,unsigned seed) {
    size_t i;for(i=0;i<n;i++) p[i]=(unsigned char)((i*73+seed*137)^(i>>2));
}
int main(void) {
    unsigned char a[640],b[640],source[640],left[96],right[96];
    size_t src,dst,n,i,len,off;
    static const size_t lengths[]={0,1,2,3,7,8,15,16,17,31,32,33,63,64,65,127,128,129,255};
    static const int values[]={0,1,127,128,255,256,-1,-128,0x12345};
    /* Every short relative overlap, both directions, identity, adjacency,
     * nonoverlap, zero length and unaligned pointers. Entire-array comparison
     * verifies untouched bytes before/after the destination as well. */
    for(src=0;src<40;src++) for(dst=0;dst<40;dst++) for(n=0;n<=80;n++) {
        pattern(a,sizeof(a),(unsigned)(src+dst+n));memcpy(b,a,sizeof(a));
        CHECK(charon_test_memmove(a+dst,a+src,n)==a+dst);
        CHECK(memmove(b+dst,b+src,n)==b+dst);
        CHECK(memcmp(a,b,sizeof(a))==0);
    }
    pattern(source,sizeof(source),91);
    for(src=0;src<17;src++) for(dst=0;dst<17;dst++)
        for(i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++) {
            n=lengths[i];pattern(a,sizeof(a),4);memcpy(b,a,sizeof(a));
            CHECK(charon_test_memcpy(a+dst,source+src,n)==a+dst);
            memcpy(b+dst,source+src,n);
            CHECK(memcmp(a,b,sizeof(a))==0);
        }
    /* Nonoverlapping ranges inside the same allocation and touching endpoints. */
    for(n=0;n<=256;n++) {
        pattern(a,sizeof(a),7);memcpy(b,a,sizeof(a));
        CHECK(charon_test_memmove(a+320,a+1,n)==a+320);
        memmove(b+320,b+1,n);CHECK(memcmp(a,b,sizeof(a))==0);
        CHECK(charon_test_memmove(a+1,a+320,n)==a+1);
        memmove(b+1,b+320,n);CHECK(memcmp(a,b,sizeof(a))==0);
    }
    for(dst=0;dst<17;dst++) for(i=0;i<sizeof(values)/sizeof(values[0]);i++)
        for(n=0;n<=257;n++) {
            pattern(a,sizeof(a),5);memcpy(b,a,sizeof(a));
            CHECK(charon_test_memset(a+dst,values[i],n)==a+dst);
            memset(b+dst,values[i],n);CHECK(memcmp(a,b,sizeof(a))==0);
        }
    /* memcmp specifies sign, not the exact magnitude returned by libc.
     * Include all unsigned byte pairs and unequal bytes beyond count. */
    for(src=0;src<256;src++) for(dst=0;dst<256;dst++) {
        memset(left,0xa5,sizeof(left));memcpy(right,left,sizeof(left));
        left[35]=(unsigned char)src;right[35]=(unsigned char)dst;
        right[50]^=0xff;
        CHECK(charon_test_memcmp(left+1,right+1,34)==0);
        CHECK(sign(charon_test_memcmp(left+1,right+1,35))==sign(memcmp(left+1,right+1,35)));
        CHECK(sign(charon_test_memcmp(left+1,right+1,60))==sign(memcmp(left+1,right+1,60)));
        CHECK(charon_test_memcmp(left+1,right+1,0)==0);
    }
    for(off=0;off<17;off++) for(len=0;len<=511;len++) {
        for(i=0;i<sizeof(a);i++) a[i]=(unsigned char)(1+(i*83)%255);
        a[off+len]=0;
        CHECK(charon_test_strlen((const char*)a+off)==strlen((const char*)a+off));
        CHECK(charon_test_strlen((const char*)a+off)==len);
    }
    printf("PASS %lu CharonMemory differential/guard/return-value checks\n",checks);
    return 0;
}
