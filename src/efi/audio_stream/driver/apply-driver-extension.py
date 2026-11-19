"""Apply a reviewed additive patch to a build copy, not the pinned source tree."""
from pathlib import Path
import sys,hashlib,json
here=Path(__file__).resolve().parent
original=Path(sys.argv[1]).resolve()/'Staging/AudioDxe'
target=Path(sys.argv[2]).resolve()/'OpenCorePkg/Staging/AudioDxe'
assert original!=target
manifest={}
def edit(name,changes):
    p=original/name;text=p.read_text();manifest[name]=hashlib.sha256(p.read_bytes()).hexdigest()
    for old,new,count in changes:
        assert text.count(old)==count,(name,old[:100],text.count(old),count)
        text=text.replace(old,new)
    (target/name).write_text(text,newline='\n')
edit('HdaController/HdaController.h',[
 ('#define HDA_BDL_SIZE         (sizeof(HDA_BDL_ENTRY) * HDA_BDL_ENTRY_COUNT)',
  '#define HDA_BDL_SIZE         (sizeof(HDA_BDL_ENTRY) * 4) /* HAK four-descriptor mapping; legacy uses first two. */',1),
 ('#include "AudioDxe.h"','#include "AudioDxe.h"\n#include "HAKAudioStream.h"',1),
 ('  BOOLEAN                       BufferActive;','  BOOLEAN                       BufferActive;\n  VOID                          *HakContext;',1),
 ('  EFI_HDA_IO_PROTOCOL    HdaIo;','  EFI_HDA_IO_PROTOCOL    HdaIo;\n  HAK_AUDIO_STREAM       HakProtocol;\n  VOID                   *HakContext;',1),
 ('#endif','extern EFI_GUID gHakAudioStreamGuid;\nVOID HakAudioInitialize(HDA_IO_PRIVATE_DATA *Owner);\nVOID HakAudioPoll(HDA_STREAM *Stream);\nEFI_STATUS HakAudioStopStream(HDA_STREAM *Stream);\nEFI_STATUS HakAudioRelease(HDA_IO_PRIVATE_DATA *Owner);\n#endif',1)])
edit('HdaController/HdaController.c',[
 ('  HdaStream    = (HDA_STREAM *)Context;','  HdaStream    = (HDA_STREAM *)Context;\n  if (HdaStream->HakContext != NULL) { HakAudioPoll(HdaStream); return; }',1),
 ('        HdaIoPrivateData->HdaIo.StopStream  = HdaControllerHdaIoStopStream;',
  '        HdaIoPrivateData->HdaIo.StopStream  = HdaControllerHdaIoStopStream;\n        HakAudioInitialize(HdaIoPrivateData);',1),
 ('                                                               &HdaControllerDev->HdaIoChildren[Index].PrivateData->HdaIo,\n                                                               NULL',
  '                                                               &HdaControllerDev->HdaIoChildren[Index].PrivateData->HdaIo,\n                                                               &gHakAudioStreamGuid,\n                                                               &HdaControllerDev->HdaIoChildren[Index].PrivateData->HakProtocol,\n                                                               NULL',1),
 ('      // Free private data.\n      FreePool (HdaControllerDev->HdaIoChildren[i].PrivateData);',
  '      gBS->UninstallProtocolInterface(HdaControllerDev->HdaIoChildren[i].Handle, &gHakAudioStreamGuid, &HdaControllerDev->HdaIoChildren[i].PrivateData->HakProtocol);\n      HakAudioRelease(HdaControllerDev->HdaIoChildren[i].PrivateData);\n      // Free private data.\n      FreePool (HdaControllerDev->HdaIoChildren[i].PrivateData);',1),
 ('    // Cleanup controller.\n    //\n    HdaControllerCleanup (HdaControllerDev);',
  '    // Cleanup controller only after all continuous DMA owners quiesce.\n    //\n    { UINTN HakIndex; for (HakIndex=0; HakIndex<HDA_MAX_CODECS; ++HakIndex) {\n      HDA_IO_PRIVATE_DATA *HakOwner=HdaControllerDev->HdaIoChildren[HakIndex].PrivateData;\n      if(HakOwner) { Status=HakOwner->HakProtocol.Stop(&HakOwner->HakProtocol); if(EFI_ERROR(Status)) return Status; }\n    }}\n    HdaControllerCleanup (HdaControllerDev);',1)])
p=original/'HdaController/HdaControllerHdaIo.c';text=p.read_text();manifest[str(p.relative_to(original))]=hashlib.sha256(p.read_bytes()).hexdigest()
for name,action in [('HdaControllerHdaIoSetupStream','return EFI_ACCESS_DENIED;'),
                    ('HdaControllerHdaIoStartStream','return EFI_ACCESS_DENIED;'),
                    ('HdaControllerHdaIoStopStream','return HakAudioStopStream(HdaStream);')]:
    start=text.index('\n'+name+' (');end=text.find('\nEFI_STATUS',start+10)
    if end<0:end=len(text)
    part=text[start:end];needle='  // Get current stream ID.'
    assert needle in part
    part=part.replace(needle,'  if (HdaStream != NULL && HdaStream->HakContext != NULL) { '+action+' }\n\n'+needle,1)
    text=text[:start]+part+text[end:]
(target/'HdaController/HdaControllerHdaIo.c').write_text(text,newline='\n')
edit('AudioDxe.inf', [('  HdaController/HdaController.c','  HdaController/HdaController.c\n  HdaController/HakAudioStream.c',1),
 ('  SynchronizationLib','  SynchronizationLib\n  TimerLib\n  IoLib',1)])
for name in ['HakAudioStream.c','HAKAudioStream.h','HakAudioRing.h']:
    (target/'HdaController'/name).write_bytes((here/name).read_bytes())
(here/'PATCH-BASE-SHA256.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Applied additive stream extension to build copy; pinned originals retained')
