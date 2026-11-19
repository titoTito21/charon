/* Native-host validation of actual SysV switch and original synchronous frames.
 * gcc compile; link production frontend/context/pool/math and test_job_host.c.
 * This executable is a host test, never an EFI dependency. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "tvox_freestanding.h"
#include "tvox_pl.h"
int tvox_test_setup(size_t,size_t);
size_t tvox_test_live(void);
#define CHECK(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint32_t mxcsr(void){uint32_t v;__asm__ volatile("stmxcsr %0":"=m"(v));return v;}
static void setmx(uint32_t v){__asm__ volatile("ldmxcsr %0"::"m"(v));}
static uint16_t x87(void){uint16_t v;__asm__ volatile("fnstcw %0":"=m"(v));return v;}
static void setx87(uint16_t v){__asm__ volatile("fldcw %0"::"m"(v));}
int main(int argc,char** argv){
 int english=argc>1&&!strcmp(argv[1],"en");
 int (*beginJob)(const char*,size_t,double,double)=english?tvox_frontend_begin_en:tvox_frontend_begin_pl;
 int (*prepareFrames)(const char*,size_t,double,double,double*,size_t,size_t*,size_t*,size_t*)=english?tvox_en_frames_ex:tvox_pl_frames_ex;
 const char* texts[]={"Menedżer rozruchu Haiku.","Dysk USB, partycja 2, Linux.","Czy chcesz kontynuować?",""};
 const char* englishTexts[]={"Haiku boot manager.","USB device, partition 2, Linux.","Do you want to continue?",""};
 if(english)for(size_t i=0;i<4;++i)texts[i]=englishTexts[i];
 const size_t budgets[]={1,7,64,4096};
 uint32_t originalMx=mxcsr();uint16_t original87=x87();
 for(size_t t=0;t<4;++t){
  CHECK(tvox_test_setup(0,0)==0);size_t count=0,lead=0,tail=0;
  CHECK(prepareFrames(texts[t],strlen(texts[t]),1,1,NULL,0,&count,&lead,&tail)==1);
  double* expected=(double*)malloc(count*28*sizeof(double));CHECK(expected);
  CHECK(prepareFrames(texts[t],strlen(texts[t]),1,1,expected,count,&count,&lead,&tail)==0);
  CHECK(tvox_test_live()==0);
  for(size_t b=0;b<4;++b){
   CHECK(beginJob(texts[t],strlen(texts[t]),1,1)==0);
   size_t used=0,steps=0;int status;
   do{
    // Worker keeps initial rounding; caller may choose another between pumps.
    uint32_t callerMx=(originalMx&~0x6000u)|((steps%4)<<13);
    uint16_t caller87=(original87&~0xc00u)|((steps%4)<<10);
    setmx(callerMx);setx87(caller87);
    status=tvox_frontend_pump(budgets[b],&used);
    CHECK(mxcsr()==callerMx && x87()==caller87);
    CHECK(used<=budgets[b]);CHECK(status!=0||used==budgets[b]);CHECK(++steps<2000000);
   }while(status==0);
   setmx(originalMx);setx87(original87);CHECK(status==1);
   const double* frames=NULL;size_t n=0,l=0,r=0;
   CHECK(tvox_frontend_frames(&frames,&n,&l,&r)==1);
   CHECK(n==count&&l==lead&&r==tail);CHECK(memcmp(frames,expected,n*28*sizeof(double))==0);
   CHECK(tvox_frontend_cancel()==0 && tvox_test_live()==0);
  }
  free(expected);
 }
 for(size_t step=0;step<1000;step=step?step*10:1){
  CHECK(beginJob(texts[0],strlen(texts[0]),1,1)==0);size_t used;
  for(size_t i=0;i<step;++i)CHECK(tvox_frontend_pump(1,&used)==0);
  CHECK(tvox_frontend_cancel()==0&&tvox_test_live()==0);
 }
 for(size_t scenario=0;scenario<4;++scenario){
  CHECK(tvox_test_setup(scenario==0?2:0,scenario==1?1:scenario==2?1000:0)==0);
  const char* text=scenario==3?"\xff":texts[0];CHECK(beginJob(text,strlen(text),1,1)==0);
  int status;size_t used;do{status=tvox_frontend_pump(7,&used);}while(!status);
  CHECK(status==(scenario==0?-2:scenario==3?-1:-3));
  CHECK(tvox_frontend_cancel()==0&&tvox_test_live()==0);
 }
 CHECK(tvox_test_setup(0,0)==0);
 CHECK(beginJob(texts[0],strlen(texts[0]),1,1)==0);
 int status;size_t used;do{status=tvox_frontend_pump(64,&used);}while(!status);
 CHECK(status==1);CHECK(tvox_frontend_cancel()==0&&tvox_test_live()==0);
 puts(english?"PASS16 EN SysV resumable frame parity + FP/budgets/cancel/OOM/restart":"PASS16 PL SysV resumable frame parity + caller/worker MXCSR/x87 preservation, budgets, cancellation/OOM/invalid/restart");
 return 0;
}
