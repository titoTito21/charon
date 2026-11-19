"""Create a separate, reproducible owned Shell from pinned AUDK; never patch firmware."""
from pathlib import Path
import hashlib,json,shutil,sys
PIN='dfd5516702b4911f1fc1919a429d637b456df913'
HASHES={
'Shell.c':'6bacc12822054dda3d892b73eda8871b126a913a29ec7462eeab107967930840',
'Shell.h':'b89cf27ea1fa054f7b6aa0361f86735390e89d5e3513453baa8f8d824613b6be',
'Shell.inf':'56f32c6d80bce40879f1cb24589789ecd3d75a4fcf9e4bbff1824476b7c5ac8b',
'ShellProtocol.c':'48056f071fe90bb764913a9e60684c5637bbbd0437421cb3a6bc9c3b2fea75d6',
'ConsoleLogger.c':'9e33772e9c1cb0fb40db99a4c1a88601d8ac878322685e2d2b202337870085cb',
'FileHandleWrappers.c':'aeaaa80efcead914afef9e10c7185e5e9b14b6a38801481ada978bdf22e11c82'}
LIB_HASHES={'UefiShellLib.c':'d372888b4116ae54eac9f6c89a2d8989d7a216455473df6e91c21c0af41b0018',
'UefiShellLib.inf':'e785456895702dcc3f88692a7f6a0535bb14f1913ea8f28ad60e8685770f6704'}
def prepare(workspace):
    original=workspace/'ShellPkg/Application/Shell';target=workspace/'ShellPkg/Application/CharonShell'
    dsc=workspace/'ShellPkg/CharonShell.dsc'
    assert hashlib.sha256((workspace/'ShellPkg/ShellPkg.dsc').read_bytes()).hexdigest()=='02fb54321a265ad6032a4b3cf4cc395a1dda261bb6502b71b3af6904b3d6031e','Unpinned Shell DSC'
    assert not target.exists() and not dsc.exists(),'Use a fresh owned output directory'
    originalLib=workspace/'ShellPkg/Library/UefiShellLib';ownedLib=workspace/'ShellPkg/Library/CharonShellLib'
    assert not ownedLib.exists()
    for name,digest in LIB_HASHES.items():assert hashlib.sha256((originalLib/name).read_bytes()).hexdigest()==digest,('Unpinned library',name)
    for name,digest in HASHES.items():
        assert hashlib.sha256((original/name).read_bytes()).hexdigest()==digest,('Unpinned input',name)
    files={p.name:p.read_text(encoding='utf-8') for p in original.iterdir() if p.is_file() and p.suffix in ('.c','.h','.inf')}
    def replace(name,old,new):
        assert files[name].count(old)==1,(name,old[:80],files[name].count(old))
        files[name]=files[name].replace(old,new)
    replace('Shell.h','#include <Uefi.h>','#include <Uefi.h>\n#include "CharonShellSpeech.h"')
    replace('Shell.inf','  Shell.c\n','  Shell.c\n  CharonShellSpeech.c\n  CharonShellSpeech.h\n  ShellSpeechModel.c\n  ShellSpeechModel.h\n')
    replace('Shell.inf','BASE_NAME                      = Shell','BASE_NAME                      = CharonShell')
    replace('Shell.c','gImageHandle, ShellStrings, NULL','gImageHandle, CharonShellStrings, NULL')
    replace('Shell.inf','7C04A583-9E3E-4f1c-AD65-E05268D0B4D1','58B2BC58-CA5B-44EA-A3EE-702925135F37')
    files['Shell.inf']+='\n[BuildOptions]\n  MSFT:*_*_X64_CC_FLAGS = /utf-8\n'
    replace('FileHandleWrappers.c','    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &EventIndex);\n    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);',
        '    Status = ShellSpeechWaitKey (gST->ConIn->WaitForKey, &EventIndex);\n    if (!EFI_ERROR (Status)) Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);')
    replace('FileHandleWrappers.c','  ZeroMem (CurrentString, MaxStr * sizeof (CHAR16));\n  do {',
        '  ZeroMem (CurrentString, MaxStr * sizeof (CHAR16));\n  ShellSpeechEditorBegin ();\n  do {')
    replace('FileHandleWrappers.c','    // Press PageUp or PageDown to scroll the history screen up or down.',
        '    // Charon review consumes only F9-F12; upstream F7 history is unchanged.\n    //\n    if (ShellSpeechReviewKey (&Key)) continue;\n    //\n    // Press PageUp or PageDown to scroll the history screen up or down.')
    replace('FileHandleWrappers.c','    gST->ConOut->SetCursorPosition (gST->ConOut, Column, Row);\n  } while (!Done);',
        '    ShellSpeechEditorUpdate (CurrentString, StringLen, StringCurPos);\n    gST->ConOut->SetCursorPosition (gST->ConOut, Column, Row);\n  } while (!Done);\n  ShellSpeechEditorEnd ();')
    replace('FileHandleWrappers.c','  return (gST->StdErr->OutputString (gST->StdErr, Buffer));',
        '  EFI_STATUS Status = gST->StdErr->OutputString (gST->StdErr, Buffer);\n  if (!EFI_ERROR (Status) && gST->StdErr != gST->ConOut) ShellSpeechOutput (Buffer);\n  return Status;')
    replace('ConsoleLogger.c','      Status = gBS->WaitForEvent (1, &TxtInEx->WaitForKeyEx, &EventIndex);',
        '      Status = ShellSpeechWaitKey (TxtInEx->WaitForKeyEx, &EventIndex);')
    replace('ConsoleLogger.c','  return (AppendStringToHistory (String, ConsoleInfo));',
        '  ShellSpeechOutput (String);\n  return (AppendStringToHistory (String, ConsoleInfo));')
    replace('ConsoleLogger.c','    ConsoleInfo->HistoryMode.CursorColumn = (INT32)Column;\n    ConsoleInfo->HistoryMode.CursorRow    = (INT32)(ConsoleInfo->OriginalStartRow + Row);',
        '    ShellSpeechCursor (Column, Row);\n    ConsoleInfo->HistoryMode.CursorColumn = (INT32)Column;\n    ConsoleInfo->HistoryMode.CursorRow    = (INT32)(ConsoleInfo->OriginalStartRow + Row);')
    replace('ConsoleLogger.c','  Status = ConsoleInfo->OldConOut->ClearScreen (ConsoleInfo->OldConOut);',
        '  Status = ConsoleInfo->OldConOut->ClearScreen (ConsoleInfo->OldConOut);\n  if (!EFI_ERROR (Status)) ShellSpeechClear ();')
    replace('ShellProtocol.c','      StartStatus = gBS->StartImage (\n                           NewHandle,\n                           0,\n                           NULL\n                           );',
        '      StartStatus = ShellSpeechBeforeChild ();\n      if (!EFI_ERROR (StartStatus)) {\n        StartStatus = gBS->StartImage (NewHandle, 0, NULL);\n        ShellSpeechAfterChild ();\n      } else {\n        gBS->UnloadImage (NewHandle);\n      }')
    replace('Shell.c','  if (ShellCommandGetExit ()) {\n    return ((EFI_STATUS)ShellCommandGetExitCode ());',
        '  {\n    EFI_STATUS SpeechStatus = ShellSpeechClose ();\n    if (EFI_ERROR (SpeechStatus)) return SpeechStatus;\n  }\n  if (ShellCommandGetExit ()) {\n    return ((EFI_STATUS)ShellCommandGetExitCode ());')
    sourceDsc=(workspace/'ShellPkg/ShellPkg.dsc').read_text(encoding='utf-8')
    assert sourceDsc.count('ShellPkg/Application/Shell/Shell.inf')==2
    first=sourceDsc.index('  ShellPkg/Application/Shell/Shell.inf {')
    end=sourceDsc.index('\n  }',first)+4
    options=sourceDsc[sourceDsc.index('[BuildOptions]'):]
    sourceDsc=sourceDsc[:sourceDsc.index('[Components]')]+'[Components]\n'+sourceDsc[first:end]+'\n\n'+options
    sourceDsc=sourceDsc.replace('ShellPkg/Application/Shell/Shell.inf','ShellPkg/Application/CharonShell/Shell.inf').replace('= Build/Shell','= Build/CharonShell').replace('= Shell\n','= CharonShell\n')
    sourceDsc=sourceDsc.replace('[LibraryClasses.common]','[LibraryClasses.common]\n  IntrinsicLib|MdePkg/Library/IntrinsicLib/IntrinsicLib.inf')
    sourceDsc=sourceDsc.replace('ShellLib|ShellPkg/Library/UefiShellLib/UefiShellLib.inf','ShellLib|ShellPkg/Library/CharonShellLib/UefiShellLib.inf')
    library=(originalLib/'UefiShellLib.c').read_text(encoding='utf-8')
    library='#include "../../Application/CharonShell/CharonShellSpeech.h"\n'+library
    wait='gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &EventIndex);'
    assert library.count(wait)==6
    library=library.replace(wait,'Status = ShellSpeechWaitKey (gST->ConIn->WaitForKey, &EventIndex);\n        if (EFI_ERROR (Status)) break;')
    shutil.copytree(originalLib,ownedLib)
    (ownedLib/'UefiShellLib.c').write_bytes(library.encode('utf-8'))
    shutil.copytree(original,target)
    for name,data in files.items():(target/name).write_bytes(data.encode('utf-8'))
    for name in ('CharonShellSpeech.c','CharonShellSpeech.h','ShellSpeechModel.c','ShellSpeechModel.h'):
        shutil.copyfile(Path(__file__).parent/name,target/name)
    dsc.write_bytes(sourceDsc.encode('utf-8'))
    (target/'charon-provenance.json').write_text(json.dumps({'audk_commit':PIN,'original_inputs':HASHES,'original_library':LIB_HASHES,'patched_library_sha256':hashlib.sha256((ownedLib/'UefiShellLib.c').read_bytes()).hexdigest(),'patched_outputs':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in target.iterdir() if p.is_file()}},indent=2))
    print(target)
if __name__=='__main__':prepare(Path(sys.argv[1]).resolve())
