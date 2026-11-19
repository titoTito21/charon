/* GPL-2.0-or-later. Genuine public MenuVoice consumer; no substitute TTS. */
#include "CharonShellSpeech.h"
#include "ShellSpeechModel.h"
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Guid/GlobalVariable.h>
typedef struct HAK_MENU_VOICE HAK_MENU_VOICE;
struct HAK_MENU_VOICE {
  UINT32 Version,Size;
  EFI_STATUS (EFIAPI *Speak)(HAK_MENU_VOICE*,CONST CHAR8*,CONST CHAR8*);
  EFI_STATUS (EFIAPI *Pump)(HAK_MENU_VOICE*,UINT32);
  EFI_STATUS (EFIAPI *Stop)(HAK_MENU_VOICE*);
};
typedef struct {HAK_MENU_VOICE Base;BOOLEAN (EFIAPI *Preparing)(HAK_MENU_VOICE*);} HAK_MENU_VOICE_V2;
STATIC_ASSERT(sizeof(HAK_MENU_VOICE)==32,"x64 MenuVoice v1 ABI");
STATIC_ASSERT(sizeof(HAK_MENU_VOICE_V2)==40,"x64 MenuVoice v2 ABI");
static EFI_GUID VoiceGuid={0x99e5faac,0x6c86,0x45db,{0xb0,0x9d,0x3c,0xba,0x80,0x4d,0x0e,0x51}};
static EFI_GUID GlobalGuid=EFI_GLOBAL_VARIABLE;
static HAK_MENU_VOICE *Voice;
static EFI_EVENT Timer;
static HAK_SHELL_MODEL Model;
static BOOLEAN Suspended,LocaleRead;
static CHAR8 Language[3]={'e','n',0};
static EFI_STATUS LastError=EFI_SUCCESS;
static EFI_STATUS Connect(VOID) {
  EFI_STATUS Status;UINTN Size;CHAR8 Locale[64];
  if(Suspended)return EFI_NOT_READY;
  if(Voice)return LastError;
  Status=gBS->LocateProtocol(&VoiceGuid,NULL,(VOID**)&Voice);
  if(EFI_ERROR(Status)){Voice=NULL;return Status;}
  if(!Voice || !Voice->Speak || !Voice->Pump || !Voice->Stop ||
    !((Voice->Version==1 && Voice->Size==sizeof(*Voice)) ||
    (Voice->Version==2 && Voice->Size>=sizeof(HAK_MENU_VOICE_V2) && ((HAK_MENU_VOICE_V2*)Voice)->Preparing))) {
    Voice=NULL;return EFI_UNSUPPORTED;
  }
  if(!LocaleRead) {
    ZeroMem(Locale,sizeof(Locale));Size=sizeof(Locale)-1;
    Status=gRT->GetVariable(L"PlatformLang",&GlobalGuid,NULL,&Size,Locale);
    if(!EFI_ERROR(Status) && Size>=2 && Locale[0]=='p' && Locale[1]=='l') {Language[0]='p';Language[1]='l';Model.polish=1;}
    LocaleRead=TRUE;
  }
  Status=gBS->CreateEvent(EVT_TIMER,TPL_APPLICATION,NULL,NULL,&Timer);
  if(!EFI_ERROR(Status))Status=gBS->SetTimer(Timer,TimerPeriodic,100000);
  if(EFI_ERROR(Status)){if(Timer)gBS->CloseEvent(Timer);Timer=NULL;Voice=NULL;}
  LastError=Status;return Status;
}
static VOID Speak(CONST CHAR8 *Text) {
  if(!Text[0] || EFI_ERROR(Connect()))return;
  LastError=Voice->Speak(Voice,Language,Text);
}
EFI_STATUS ShellSpeechWaitKey(EFI_EVENT KeyEvent,UINTN *Index) {
  EFI_EVENT Events[2];EFI_STATUS Status;CHAR8 Text[HAK_SHELL_UTF8];UINTN Selected;
  Status=Connect();
  if(Status==EFI_NOT_FOUND || Status==EFI_UNSUPPORTED || Status==EFI_NOT_READY)
    return gBS->WaitForEvent(1,&KeyEvent,Index);
  if(EFI_ERROR(Status))return Status;
  if(HakModelFlush(&Model,Text))Speak(Text);
  Events[0]=KeyEvent;Events[1]=Timer;
  for(;;) {
    if(EFI_ERROR(LastError))return LastError;
    if(Voice->Version==2 && ((HAK_MENU_VOICE_V2*)Voice)->Preparing(Voice)) {
      Status=gBS->CheckEvent(KeyEvent);
      if(Status==EFI_SUCCESS){*Index=0;return EFI_SUCCESS;}
      if(Status!=EFI_NOT_READY)return Status;
      Selected=1;
    } else {
      Status=gBS->WaitForEvent(2,Events,&Selected);
      if(EFI_ERROR(Status))return Status;
      if(Selected==0){*Index=0;return EFI_SUCCESS;}
    }
    LastError=Voice->Pump(Voice,480);
  }
}
VOID ShellSpeechOutput(CONST CHAR16 *Text) {if(!Suspended)HakModelOutput(&Model,(CONST HAK_CHAR16*)Text);}
VOID ShellSpeechCursor(UINTN Column,UINTN Row) {if(!Suspended)HakModelCursor(&Model,(unsigned)Column,(unsigned)Row);}
VOID ShellSpeechClear(VOID) {if(!Suspended)HakModelClear(&Model);}
VOID ShellSpeechEditorBegin(VOID) {Model.editing=1;Model.editorLength=Model.editorCursor=0;Model.editor[0]=0;}
VOID ShellSpeechEditorUpdate(CONST CHAR16 *Text,UINTN Length,UINTN Cursor) {
  CHAR8 Out[HAK_SHELL_UTF8];if(Suspended)return;
  if(Length>HAK_SHELL_COLUMNS-1)Length=HAK_SHELL_COLUMNS-1;
  if(Cursor>Length)Cursor=Length;
  if(HakModelEditor(&Model,(CONST HAK_CHAR16*)Text,(unsigned)Length,(unsigned)Cursor,Out))Speak(Out);
}
VOID ShellSpeechEditorEnd(VOID) {Model.editing=0;HakModelClear(&Model);}
BOOLEAN ShellSpeechReviewKey(CONST EFI_INPUT_KEY *Key) {
  CHAR8 Out[HAK_SHELL_UTF8];
  if(Key->UnicodeChar || Key->ScanCode<SCAN_F9 || Key->ScanCode>SCAN_F12)return FALSE;
  if(Key->ScanCode==SCAN_F12) {if(Voice)LastError=Voice->Stop(Voice);return TRUE;}
  if(Key->ScanCode==SCAN_F11)HakModelEditorLine(&Model,Out);
  else HakModelReview(&Model,Key->ScanCode==SCAN_F9?-1:1,Out);
  Speak(Out);return TRUE;
}
EFI_STATUS ShellSpeechBeforeChild(VOID) {
  EFI_STATUS Status=Voice?Voice->Stop(Voice):EFI_SUCCESS;
  if(!EFI_ERROR(Status))Suspended=TRUE;
  return Status;
}
VOID ShellSpeechAfterChild(VOID) {Suspended=FALSE;}
EFI_STATUS ShellSpeechClose(VOID) {
  EFI_STATUS Status=Voice?Voice->Stop(Voice):EFI_SUCCESS;
  if(EFI_ERROR(Status))return Status;
  if(Timer){Status=gBS->CloseEvent(Timer);if(EFI_ERROR(Status))return Status;}
  Timer=NULL;Voice=NULL;Suspended=FALSE;return EFI_SUCCESS;
}
