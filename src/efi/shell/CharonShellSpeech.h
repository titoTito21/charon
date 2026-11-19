/* GPL-2.0-or-later. Owned Shell integration; not a firmware-wide console hook. */
#ifndef CHARON_SHELL_SPEECH_H
#define CHARON_SHELL_SPEECH_H
#include <Uefi.h>
EFI_STATUS ShellSpeechWaitKey(EFI_EVENT KeyEvent,UINTN *Index);
VOID ShellSpeechOutput(CONST CHAR16 *Text);
VOID ShellSpeechCursor(UINTN Column,UINTN Row);
VOID ShellSpeechClear(VOID);
VOID ShellSpeechEditorBegin(VOID);
VOID ShellSpeechEditorUpdate(CONST CHAR16 *Text,UINTN Length,UINTN Cursor);
VOID ShellSpeechEditorEnd(VOID);
BOOLEAN ShellSpeechReviewKey(CONST EFI_INPUT_KEY *Key);
EFI_STATUS ShellSpeechBeforeChild(VOID);
VOID ShellSpeechAfterChild(VOID);
EFI_STATUS ShellSpeechClose(VOID);
#endif
