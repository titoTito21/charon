/* Independent bounded console/editor observation for the owned EFI Shell.
 * GPL-2.0-or-later. No allocation, firmware calls, timers or speech caches. */
#ifndef HAK_SHELL_SPEECH_MODEL_H
#define HAK_SHELL_SPEECH_MODEL_H
typedef unsigned short HAK_CHAR16;
enum { HAK_SHELL_COLUMNS=256, HAK_SHELL_LINES=32, HAK_SHELL_UTF8=1024 };
typedef struct {
  HAK_CHAR16 line[HAK_SHELL_COLUMNS], history[HAK_SHELL_LINES][HAK_SHELL_COLUMNS];
  HAK_CHAR16 editor[HAK_SHELL_COLUMNS];
  unsigned column,row,length,head,count,review,editorLength,editorCursor;
  int pending,completedPending,editing,polish,truncated;
} HAK_SHELL_MODEL;
void HakModelOutput(HAK_SHELL_MODEL*,const HAK_CHAR16*);
void HakModelCursor(HAK_SHELL_MODEL*,unsigned,unsigned);
void HakModelClear(HAK_SHELL_MODEL*);
int HakModelFlush(HAK_SHELL_MODEL*,char*);
int HakModelReview(HAK_SHELL_MODEL*,int,char*);
int HakModelEditor(HAK_SHELL_MODEL*,const HAK_CHAR16*,unsigned,unsigned,char*);
int HakModelEditorLine(HAK_SHELL_MODEL*,char*);
unsigned HakModelUtf8(const HAK_CHAR16*,unsigned,char*,unsigned);
#endif
