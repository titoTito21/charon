/* Host contract tests execute the actual C consumer with deterministic EFI. */
#include "CharonShellSpeech.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
EFI_BOOT_SERVICES *gBS;
EFI_RUNTIME_SERVICES *gRT;
VOID *EFIAPI ZeroMem(VOID *Buffer,UINTN Length) {return memset(Buffer,0,Length);}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned Pumps,Waits,Checks,Created,Closed,Spoken,Stops;
static int Available=1,PreparingWork=3;
static EFI_STATUS StopError=EFI_SUCCESS;
static CHAR8 Said[1024],SaidLanguage[3];
static EFI_STATUS EFIAPI TestSpeak(HAK_MENU_VOICE *Self,CONST CHAR8 *Language,CONST CHAR8 *Text) {
  (void)Self;strcpy(Said,Text);strcpy(SaidLanguage,Language);++Spoken;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestPump(HAK_MENU_VOICE *Self,UINT32 Frames) {(void)Self;CHECK(Frames==480);++Pumps;if(PreparingWork)--PreparingWork;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI TestStop(HAK_MENU_VOICE *Self) {(void)Self;++Stops;return StopError;}
static BOOLEAN EFIAPI TestPreparing(HAK_MENU_VOICE *Self) {(void)Self;return PreparingWork!=0;}
static HAK_MENU_VOICE_V2 Service={{2,sizeof(HAK_MENU_VOICE_V2),TestSpeak,TestPump,TestStop},TestPreparing};
static EFI_STATUS EFIAPI Locate(EFI_GUID *Guid,VOID *Registration,VOID **Out) {(void)Guid;(void)Registration;if(!Available)return EFI_NOT_FOUND;*Out=&Service;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Create(UINT32 Type,EFI_TPL Tpl,EFI_EVENT_NOTIFY Callback,VOID *Context,EFI_EVENT *Out) {
  CHECK(Type==EVT_TIMER && Tpl==TPL_APPLICATION && !Callback && !Context);++Created;*Out=(EFI_EVENT)2;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI SetTimer(EFI_EVENT Event,EFI_TIMER_DELAY Type,UINT64 Time) {CHECK(Event==(EFI_EVENT)2 && Type==TimerPeriodic && Time==100000);return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Close(EFI_EVENT Event) {CHECK(Event==(EFI_EVENT)2);++Closed;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Wait(UINTN Count,EFI_EVENT *Events,UINTN *Index) {(void)Events;++Waits;*Index=Count==1 || Pumps>=3?0:1;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Check(EFI_EVENT Event) {CHECK(Event==(EFI_EVENT)1);++Checks;return Pumps>=2?EFI_SUCCESS:EFI_NOT_READY;}
static EFI_STATUS EFIAPI GetVariable(CHAR16 *Name,EFI_GUID *Guid,UINT32 *Attr,UINTN *Size,VOID *Data) {(void)Name;(void)Guid;(void)Attr;CHECK(*Size>=6);memcpy(Data,"pl-PL",6);*Size=6;return EFI_SUCCESS;}
int main(void) {
  EFI_BOOT_SERVICES Boot={0};EFI_RUNTIME_SERVICES Runtime={0};UINTN Index=9;EFI_INPUT_KEY Key={0};
  Boot.LocateProtocol=Locate;Boot.CreateEvent=Create;Boot.SetTimer=SetTimer;Boot.CloseEvent=Close;Boot.WaitForEvent=Wait;Boot.CheckEvent=Check;
  Runtime.GetVariable=GetVariable;gBS=&Boot;gRT=&Runtime;
  Available=0;CHECK(ShellSpeechWaitKey((EFI_EVENT)1,&Index)==EFI_SUCCESS && Index==0 && Waits==1 && !Created);
  Available=1;Waits=0;ShellSpeechOutput(L"wynik\r\nFS0:> ");
  CHECK(ShellSpeechWaitKey((EFI_EVENT)1,&Index)==EFI_SUCCESS && Index==0);
  CHECK(Pumps==2 && Checks==3 && Waits==0 && Created==1 && Spoken==1 && !strcmp(SaidLanguage,"pl"));
  CHECK(!strcmp(Said,"wynik. FS0:> "));
  ShellSpeechEditorBegin();ShellSpeechOutput(L"redraw");CHECK(!Model.pending);
  ShellSpeechEditorUpdate(L"a",1,1);CHECK(!strcmp(Said,"a"));
  Key.ScanCode=SCAN_F11;CHECK(ShellSpeechReviewKey(&Key) && !strcmp(Said,"a"));
  Key.ScanCode=SCAN_F7;CHECK(!ShellSpeechReviewKey(&Key));
  ShellSpeechEditorEnd();StopError=EFI_DEVICE_ERROR;
  CHECK(ShellSpeechBeforeChild()==EFI_DEVICE_ERROR && !Suspended);
  StopError=EFI_SUCCESS;CHECK(ShellSpeechBeforeChild()==EFI_SUCCESS && Suspended);
  ShellSpeechOutput(L"child output must not be captured");CHECK(!Model.pending);
  ShellSpeechAfterChild();CHECK(!Suspended);
  StopError=EFI_DEVICE_ERROR;CHECK(ShellSpeechClose()==EFI_DEVICE_ERROR && !Closed && Voice);
  StopError=EFI_SUCCESS;CHECK(ShellSpeechClose()==EFI_SUCCESS && Closed==1 && !Voice && !Timer);
  Service.Base.Version=1;Service.Base.Size=sizeof(HAK_MENU_VOICE);Pumps=Waits=0;PreparingWork=3;
  CHECK(ShellSpeechWaitKey((EFI_EVENT)1,&Index)==EFI_SUCCESS && Pumps==3 && Waits==4);
  CHECK(ShellSpeechClose()==EFI_SUCCESS);
  Service.Base.Version=2;Service.Base.Size=sizeof(HAK_MENU_VOICE);
  CHECK(Connect()==EFI_UNSUPPORTED && !Voice);
  puts("PASS actual Shell C ABI consumer: v1/v2, application-TPL timer, direct consumed-key dispatch, PL text, editor/review, child Stop and teardown failure");
  return 0;
}
