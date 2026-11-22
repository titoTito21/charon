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
# r406: binding and output preparation for modern machines (HakHdaCompat.h).
def edit2(name,changes):
    """Further changes to a file that edit() already wrote to the target."""
    p=target/name;text=p.read_text()
    for old,new,count in changes:
        assert text.count(old)==count,(name,old[:100],text.count(old),count)
        text=text.replace(old,new)
    p.write_text(text,newline='\n')
edit2('HdaController/HdaController.c',[
 ('#include "HdaControllerComponentName.h"','#include "HdaControllerComponentName.h"\n#include "HakHdaCompat.h" /* r406 */',1),
 # Supported(): class 04/03 as before, plus verified Intel 04/01 and 04/80.
 ('  if (  !EFI_ERROR (Status)\n     && (  (HdaClassReg.BaseCode != PCI_CLASS_MEDIA)\n        || (HdaClassReg.SubClassCode != PCI_CLASS_MEDIA_MIXED_MODE)))\n  {\n    return EFI_UNSUPPORTED;\n  }\n',
  '  if (!EFI_ERROR (Status)) {\n    Status = HakHdaControllerSupported (PciIo, (CONST UINT8 *)&HdaClassReg); /* r406 */\n  }\n',1),
 # Start(): nothing is written to a device outside class 04/03 before its register block was checked.
 ('  //\n  // Allocate controller device.\n  //\n',
  '  Status = HakHdaControllerStartCheck (PciIo); /* r406 */\n  if (EFI_ERROR (Status)) {\n    goto CLOSE_PCIIO;\n  }\n\n  //\n  // Allocate controller device.\n  //\n',1),
 ('  if (GET_PCI_VENDOR_ID (HdaControllerDev->VendorId) == VEN_INTEL_ID) {\n    //\n    // Set TC0 in TCSEL register for Intel controllers.',
  '  HakHdaControllerPciSetup (HdaControllerDev); /* r406: ATI/AMD/NVIDIA snoop */\n\n  if (GET_PCI_VENDOR_ID (HdaControllerDev->VendorId) == VEN_INTEL_ID) {\n    //\n    // Set TC0 in TCSEL register for Intel controllers.',1),
 # Reset: at least 100 us in reset for the codec PLLs (Linux snd_hdac_bus_reset_link waits 500-1000 us).
 ('  //\n  // Set CRST bit to begin the process of coming out of reset.\n  //\n',
  '  gBS->Stall (1000); /* r406: stay in reset >= 100 us */\n\n  //\n  // Set CRST bit to begin the process of coming out of reset.\n  //\n',1),
 ('  UINT32                    OpenMode;\n\n  //\n  // Identify device by the path',
  '  UINT32                    OpenMode;\n  UINT32                    HakGate; /* r406 */\n\n  //\n  // Identify device by the path',1),
 ('  Status = HdaControllerReset (HdaControllerDev, TRUE);\n',
  '  HakGate = HakHdaControllerResetPrepare (HdaControllerDev); /* r406: Skylake+ clock gating */\n  Status  = HdaControllerReset (HdaControllerDev, TRUE);\n  HakHdaControllerResetDone (HdaControllerDev, HakGate);\n',1),
 # r407: never share a controller with another driver (HakHdaControllerMayShare, HakHdaCompat.h).
 ('      if (  PcdGetBool (PcdAudioControllerTryProtocolGetMode)\n',
  '      if (  HakHdaControllerMayShare () /* r407 */\n         && PcdGetBool (PcdAudioControllerTryProtocolGetMode)\n',1),
 ('  //\n  // Init streams after prescan, uses codec vendor id.\n  //\n',
  '  Status = HakHdaControllerCodecsAnswer (HdaControllerDev); /* r406 */\n  if (EFI_ERROR (Status)) {\n    DEBUG ((DEBUG_WARN, "HDA: No codec on a DSP class controller - %r\\n", Status));\n    goto FREE_CONTROLLER;\n  }\n\n  //\n  // Init streams after prescan, uses codec vendor id.\n  //\n',1)])
edit('HdaCodec/HdaCodec.c',[
 ('#include "HdaCodecComponentName.h"','#include "HdaCodecComponentName.h"\n#include "../HdaController/HakHdaCompat.h" /* r406 */\n#include "../HdaController/HakHdaQuirks.h" /* r408 */',1),
 # r408: codec family and machine quirks (HakHdaQuirks.h): pin configuration overrides before the ports are parsed.
 ('  // Get ports.\n  Status = HdaCodecParsePorts (HdaCodecDev);\n',
  '  HakHdaQuirkProbe (HdaCodecDev); /* r408 */\n\n  // Get ports.\n  Status = HdaCodecParsePorts (HdaCodecDev);\n',1),
 ('  Status = HdaIo->SendCommand (HdaIo, FuncGroup->NodeId, HDA_CODEC_VERB (HDA_VERB_SET_POWER_STATE, 0), &Response);\n  ASSERT_EFI_ERROR (Status);\n',
  '  Status = HdaIo->SendCommand (HdaIo, FuncGroup->NodeId, HDA_CODEC_VERB (HDA_VERB_SET_POWER_STATE, 0), &Response);\n  ASSERT_EFI_ERROR (Status);\n  HakHdaCodecWaitPower (HdaIo, FuncGroup->NodeId); /* r406 */\n',1),
 ('HDA_VERB_SET_PIN_WIDGET_CONTROL_PAYLOAD (VrefCtrl, FALSE, TRUE, FALSE)',
  'HDA_VERB_SET_PIN_WIDGET_CONTROL_PAYLOAD (VrefCtrl, FALSE, TRUE, HakHdaCodecPinIsHeadphone (HdaWidget)) /* r406: HP_EN */',1)])
edit('HdaCodec/HdaCodecAudioIo.c',[
 ('#include "HdaCodec.h"','#include "HdaCodec.h"\n#include "../HdaController/HakHdaCompat.h" /* r406 */\n#include "../HdaController/HakHdaQuirks.h" /* r408 */',1),
 # r408: after the output paths are enabled: amplifier GPIOs, family initialisation, the machine's verb lists.
 ('  //\n  // Enable GPIO pins. This is similar to how Linux drivers enable audio on e.g. Cirrus Logic and\n',
  '  HakHdaQuirkFinish (HdaCodecDev); /* r408 */\n\n  //\n  // Enable GPIO pins. This is similar to how Linux drivers enable audio on e.g. Cirrus Logic and\n',1),
 ('  // Setup widget path for desired outputs.\n',
  '  HakHdaCodecPrepareOutputs (HdaCodecDev); /* r406: D0, EAPD on every pin, Realtek amplifier GPIO */\n\n  // Setup widget path for desired outputs.\n',1)])
edit('AudioDxe.inf', [('  HdaController/HdaController.c','  HdaController/HdaController.c\n  HdaController/HakAudioStream.c\n  HdaController/HakHdaCompat.h\n  HdaController/HakHdaCompat.c\n  HdaController/HakHdaCodecCompat.c\n  HdaController/HakHdaQuirks.h\n  HdaController/HakHdaQuirkMatch.h\n  HdaController/HakHdaQuirkTable.h\n  HdaController/HakHdaQuirks.c',1),
 ('  SynchronizationLib','  SynchronizationLib\n  TimerLib\n  IoLib\n  BaseLib',1),
 # r408: the off-switch file /EFI/HAK/audio-noquirks is looked for through this protocol.
 ('  gEfiPciIoProtocolGuid               # CONSUMES','  gEfiPciIoProtocolGuid               # CONSUMES\n  gEfiSimpleFileSystemProtocolGuid    # SOMETIMES_CONSUMES (r408)',1)])
# r408: HakHdaQuirks*.{c,h} are GPL-2.0-or-later (logic and data from the Linux kernel); HakHdaQuirkTable.h is
# generated by tools/gen-charon-hda-quirks.py. A driver built with them is GPL-2.0-or-later as a whole.
for name in ['HakAudioStream.c','HAKAudioStream.h','HakAudioRing.h','HakHdaCompat.h','HakHdaCompat.c','HakHdaCodecCompat.c',
             'HakHdaQuirks.h','HakHdaQuirkMatch.h','HakHdaQuirkTable.h','HakHdaQuirks.c']:
    (target/'HdaController'/name).write_bytes((here/name).read_bytes())
(here/'PATCH-BASE-SHA256.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Applied additive stream extension to build copy; pinned originals retained')
