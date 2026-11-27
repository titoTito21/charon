/* HAK continuous HDA output. SPDX-License-Identifier: BSD-2-Clause
 * Added to the pinned OpenCore AudioDxe; existing AudioIo ABI is unchanged. */
#include "HdaController.h"
#include <Library/TimerLib.h>
#include "HakAudioRing.h"
#ifdef HAK_AUDIO_TRACE
#include <Library/IoLib.h>
#endif
EFI_GUID gHakAudioStreamGuid=HAK_AUDIO_STREAM_GUID;
typedef struct {
  HDA_IO_PRIVATE_DATA *Owner;
  HDA_STREAM *Stream;
  HAK_AUDIO_RING Ring;
  UINT32 Position,LastWallClock;
  BOOLEAN WallClockValid;
  UINT64 LastTick,LastProgressTick;
  EFI_STATUS Error;
  BOOLEAN Begun,Running,SmallDma;
#ifdef HAK_AUDIO_TRACE
  UINT64 TraceStartNs,TraceGapNs[3],TraceReadNs[3];UINT32 TracePolls,TraceStartCursor;   /* r409 measurement: printed after the third poll or at a fault */
#endif
} HAK_STREAM_CONTEXT;
#define HAK_FROM_PROTOCOL(p) CR(p,HDA_IO_PRIVATE_DATA,HakProtocol,HDA_CONTROLLER_PRIVATE_DATA_SIGNATURE)
static EFI_STATUS HakSetLength(HAK_STREAM_CONTEXT *c,BOOLEAN small) {
  HDA_STREAM *s=c->Stream;EFI_PCI_IO_PROTOCOL *pci=s->HdaDev->PciIo;
  UINT32 length=small?HAK_RING_DMA_FRAMES*4:HDA_STREAM_BUF_SIZE;
  UINT32 count=small?HAK_RING_BLOCK_COUNT:HDA_BDL_ENTRY_COUNT;
  UINT32 block=length/count,i;UINT16 last=(UINT16)(count-1);EFI_STATUS status;
  for(i=0;i<count;i++) {
    EFI_PHYSICAL_ADDRESS address=s->BufferDataPhysAddr+i*block;
    s->BufferList[i].Address=(UINT32)address;
    s->BufferList[i].AddressHigh=(UINT32)(address>>32);
    s->BufferList[i].Length=block;s->BufferList[i].InterruptOnCompletion=TRUE;
  }
  status=pci->Mem.Write(pci,EfiPciIoWidthUint16,PCI_HDA_BAR,HDA_REG_SDNLVI(s->Index),1,&last);
  if(EFI_ERROR(status))return status;
  return pci->Mem.Write(pci,EfiPciIoWidthUint32,PCI_HDA_BAR,HDA_REG_SDNCBL(s->Index),1,&length);
}
/* r400 Linger memory: one BDL page, the loop (256 KiB) and 1 MiB of silence,
 * reserved once per boot below 4 GiB and never freed. */
#define HAK_LINGER_LOOP_BYTES (65536u*4u)
#define HAK_LINGER_SILENCE_BYTES (1u<<20)
#define HAK_LINGER_ENTRIES 256u
static EFI_PHYSICAL_ADDRESS mHakLinger;
static VOID HakLingerReserve(VOID) {
  EFI_PHYSICAL_ADDRESS address=0xffffffffULL;
  if(mHakLinger)return;
  if(!EFI_ERROR(gBS->AllocatePages(AllocateMaxAddress,EfiReservedMemoryType,
      EFI_SIZE_TO_PAGES(4096+HAK_LINGER_LOOP_BYTES+HAK_LINGER_SILENCE_BYTES),&address))) {
    ZeroMem((VOID*)(UINTN)address,4096+HAK_LINGER_LOOP_BYTES+HAK_LINGER_SILENCE_BYTES);mHakLinger=address;
  }
}
static EFI_STATUS HakStopContext(HAK_STREAM_CONTEXT *c) {
  EFI_STATUS status;
  if(!c || !c->Begun)return EFI_SUCCESS;
  status=gBS->SetTimer(c->Stream->PollTimer,TimerCancel,0);
  if(EFI_ERROR(status))return status;
  if(!HdaControllerSetStreamState(c->Stream,FALSE))return EFI_DEVICE_ERROR;
  if(c->SmallDma) {
    status=HakSetLength(c,FALSE);
    if(EFI_ERROR(status))return status;
  }
  c->Running=FALSE;c->Begun=FALSE;c->SmallDma=FALSE;
  c->Stream->HakContext=NULL;c->Stream->BufferActive=FALSE;
  ZeroMem(&c->Ring,sizeof(c->Ring));
  ZeroMem(c->Stream->BufferData,HDA_STREAM_BUF_SIZE);
  return EFI_SUCCESS;
}
#ifdef HAK_AUDIO_TRACE
static VOID HakTraceByte(UINT8 value) {
  UINTN i;for(i=0;i<1024;i++)if(IoRead8(0x3fd)&0x20){IoWrite8(0x3f8,value);return;}
}
static VOID HakTraceValue(CONST CHAR8 *name,UINT64 value) {
  UINTN i;INTN shift;for(i=0;name[i]&&i<64;i++)HakTraceByte((UINT8)name[i]);
  HakTraceByte('=');for(shift=60;shift>=0;shift-=4)HakTraceByte((UINT8)"0123456789abcdef"[(value>>shift)&15]);
  HakTraceByte('\r');HakTraceByte('\n');
}
#endif
static VOID HakFault(HAK_STREAM_CONTEXT *c,EFI_STATUS error,UINT32 reason,UINT64 detail) {
  EFI_STATUS stopStatus;
#ifdef HAK_AUDIO_TRACE
  EFI_STATUS clockStatus,cursorStatus;UINT32 wall=0,cursor=0;
  EFI_PCI_IO_PROTOCOL *pci=c->Stream->HdaDev->PciIo;
#endif
  if(EFI_ERROR(c->Error))return; /* Keep the first cause, including failed stop. */
  c->Error=error;
#ifdef HAK_AUDIO_TRACE
  clockStatus=pci->Mem.Read(pci,EfiPciIoWidthUint32,PCI_HDA_BAR,HDA_REG_WALLCLOCK,1,&wall);
  cursorStatus=pci->Mem.Read(pci,EfiPciIoWidthUint32,PCI_HDA_BAR,HDA_REG_SDNLPIB(c->Stream->Index),1,&cursor);
#endif
  /* Stop failure retains context/image ownership and FAULT; never fake drain. */
  stopStatus=HakStopContext(c);
  if(!EFI_ERROR(stopStatus))c->Running=FALSE;
#ifdef HAK_AUDIO_TRACE
  HakTraceValue("HAK start run ns",c->TraceStartNs);HakTraceValue("HAK start cursor",c->TraceStartCursor);
  {UINT32 i;for(i=0;i<c->TracePolls&&i<3;i++){HakTraceValue("HAK poll gap ns",c->TraceGapNs[i]);HakTraceValue("HAK poll read ns",c->TraceReadNs[i]);}}
  HakTraceValue("HAK DMA fault status",error);HakTraceValue("HAK DMA fault reason",reason);
  HakTraceValue("HAK DMA detail",detail);HakTraceValue("HAK DMA stop status",stopStatus);
  HakTraceValue("HAK DMA prior position",c->Position);HakTraceValue("HAK DMA actual bytes",cursor);
  HakTraceValue("HAK DMA cursor read status",cursorStatus);HakTraceValue("HAK DMA clock read status",clockStatus);
  if(c->WallClockValid&&!EFI_ERROR(clockStatus))HakTraceValue("HAK DMA wallclock delta24MHz",(UINT32)(wall-c->LastWallClock));
#else
  (VOID)reason;(VOID)detail;
#endif
}

VOID HakAudioPoll(HDA_STREAM *stream) {
  HAK_STREAM_CONTEXT *c=stream->HakContext;EFI_PCI_IO_PROTOCOL *pci;
  UINT32 raw,position,remaining;UINT8 flags;UINT64 now;EFI_STATUS status;
  if(!c || !c->Running || EFI_ERROR(c->Error))return;
  pci=stream->HdaDev->PciIo;now=GetPerformanceCounter();
#ifdef HAK_AUDIO_TRACE
  if(c->TracePolls<3 && now>=c->LastTick)c->TraceGapNs[c->TracePolls]=GetTimeInNanoSecond(now-c->LastTick);
#endif
  /* A delayed poll can hide an entire ring wrap. Refuse ambiguous accounting;
   * wall time detects a fault, it never supplies a consumed-frame estimate. */
  if(now<c->LastTick || GetTimeInNanoSecond(now-c->LastTick)>HAK_RING_MAX_POLL_NS) {
    HakFault(c,EFI_TIMEOUT,now<c->LastTick?2:1,now<c->LastTick?0:GetTimeInNanoSecond(now-c->LastTick));return;
  }
  status=pci->Mem.Read(pci,EfiPciIoWidthUint8,PCI_HDA_BAR,HDA_REG_SDNSTS(stream->Index),1,&flags);
  if(EFI_ERROR(status) || (flags&(HDA_REG_SDNSTS_FIFOE|HDA_REG_SDNSTS_DESE))) {
    HakFault(c,EFI_ERROR(status)?status:EFI_DEVICE_ERROR,EFI_ERROR(status)?3:4,EFI_ERROR(status)?0:flags);return;
  }
  /* HDA LPIB is the hardware cursor, independent of the optional DMA position
   * buffer's known startup/non-Intel update problems. */
  status=pci->Mem.Read(pci,EfiPciIoWidthUint32,PCI_HDA_BAR,HDA_REG_SDNLPIB(stream->Index),1,&raw);
  /* r404: LPIB may read exactly CBL just before it wraps (VMware does): that is position 0. */
  if(!EFI_ERROR(status) && raw==HAK_RING_DMA_FRAMES*4)raw=0;
  if(EFI_ERROR(status) || raw>=HAK_RING_DMA_FRAMES*4) {HakFault(c,EFI_ERROR(status)?status:EFI_DEVICE_ERROR,EFI_ERROR(status)?5:6,EFI_ERROR(status)?0:raw);return;}
#ifdef HAK_AUDIO_TRACE
  if(c->TracePolls<3) {
    c->TraceReadNs[c->TracePolls]=GetTimeInNanoSecond(GetPerformanceCounter()-now);
    if(++c->TracePolls==3) {UINT32 i;HakTraceValue("HAK start run ns",c->TraceStartNs);HakTraceValue("HAK start cursor",c->TraceStartCursor);
      for(i=0;i<3;i++){HakTraceValue("HAK poll gap ns",c->TraceGapNs[i]);HakTraceValue("HAK poll read ns",c->TraceReadNs[i]);}}
  }
#endif
  position=raw/4;remaining=(position+HAK_RING_DMA_FRAMES-c->Position)%HAK_RING_DMA_FRAMES;
  if(remaining>HAK_RING_MAX_ADVANCE) {HakFault(c,EFI_TIMEOUT,7,remaining);return;}
  if(remaining)c->LastProgressTick=now;
  else if(GetTimeInNanoSecond(now-c->LastProgressTick)>200000000ULL) {
    HakFault(c,EFI_TIMEOUT,8,GetTimeInNanoSecond(now-c->LastProgressTick));return;
  }
  while(remaining) {
    UINT32 block=c->Position/HAK_RING_BLOCK_FRAMES,from=c->Position%HAK_RING_BLOCK_FRAMES;
    UINT32 step=HAK_RING_BLOCK_FRAMES-from;
    if(step>remaining)step=remaining;
    HakRingConsume(&c->Ring,block,from,from+step);
    c->Position=(c->Position+step)%HAK_RING_DMA_FRAMES;remaining-=step;
    if(from+step==HAK_RING_BLOCK_FRAMES)
      HakRingFill(&c->Ring,block,(INT16*)(stream->BufferData+block*HAK_RING_BLOCK_FRAMES*4));
  }
  c->LastTick=now;
#ifdef HAK_AUDIO_TRACE
  c->WallClockValid=!EFI_ERROR(pci->Mem.Read(pci,EfiPciIoWidthUint32,PCI_HDA_BAR,HDA_REG_WALLCLOCK,1,&c->LastWallClock));
#endif
  if(flags&HDA_REG_SDNSTS_BCIS) {
    flags=HDA_REG_SDNSTS_BCIS;
    status=pci->Mem.Write(pci,EfiPciIoWidthUint8,PCI_HDA_BAR,HDA_REG_SDNSTS(stream->Index),1,&flags);
    if(EFI_ERROR(status))HakFault(c,status,9,status);
  }
}
static EFI_STATUS EFIAPI HakBegin(HAK_AUDIO_STREAM *protocol) {
  HDA_IO_PRIVATE_DATA *owner;HDA_STREAM *stream;HAK_STREAM_CONTEXT *c;
  EFI_PCI_IO_PROTOCOL *pci;EFI_STATUS status;EFI_TPL tpl;UINT16 format;BOOLEAN running;UINT8 id;
  if(!protocol)return EFI_INVALID_PARAMETER;
  owner=HAK_FROM_PROTOCOL(protocol);stream=owner->HdaOutputStream;
  if(!stream)return EFI_NOT_READY;
  tpl=gBS->RaiseTPL(TPL_NOTIFY);
  c=owner->HakContext;
  if(c && (c->Begun || c->Running)) {status=EFI_ALREADY_STARTED;goto done;}
  if(!HdaControllerGetStreamState(stream,&running) || running || stream->BufferActive) {status=EFI_ACCESS_DENIED;goto done;}
  if(!HdaControllerGetStreamId(stream,&id) || !id) {status=EFI_NOT_READY;goto done;}
  pci=stream->HdaDev->PciIo;
  status=pci->Mem.Read(pci,EfiPciIoWidthUint16,PCI_HDA_BAR,HDA_REG_SDNFMT(stream->Index),1,&format);
  if(EFI_ERROR(status))goto done;
  if(format!=0x11) {status=EFI_UNSUPPORTED;goto done;}
  HakLingerReserve();
  if(!c) {c=AllocateZeroPool(sizeof(*c));if(!c){status=EFI_OUT_OF_RESOURCES;goto done;}owner->HakContext=c;}
  ZeroMem(c,sizeof(*c));c->Owner=owner;c->Stream=stream;
  /* Reset cursor only while stopped, then restore verified format/stream ID. */
  if(!HdaControllerResetStream(stream) || !HdaControllerSetStreamId(stream,id)) {status=EFI_DEVICE_ERROR;goto done;}
  status=pci->Mem.Write(pci,EfiPciIoWidthUint16,PCI_HDA_BAR,HDA_REG_SDNFMT(stream->Index),1,&format);
  if(EFI_ERROR(status))goto done;
  c->Begun=TRUE;stream->HakContext=c;
  status=HakSetLength(c,TRUE);
  c->SmallDma=TRUE; /* even a partial failed reconfiguration must be restored */
  if(EFI_ERROR(status)){HakFault(c,status,12,status);goto done;}
  ZeroMem(stream->BufferData,HAK_RING_DMA_FRAMES*4);
done:
  gBS->RestoreTPL(tpl);return status;
}
static EFI_STATUS EFIAPI HakWrite(HAK_AUDIO_STREAM *protocol,CONST INT16 *samples,UINTN frames,UINTN *accepted) {
  HAK_STREAM_CONTEXT *c;EFI_TPL tpl;EFI_STATUS status=EFI_SUCCESS;UINT32 n,block;
  if(!protocol || !accepted || (!samples && frames))return EFI_INVALID_PARAMETER;
  *accepted=0;tpl=gBS->RaiseTPL(TPL_NOTIFY);c=HAK_FROM_PROTOCOL(protocol)->HakContext;
  if(!c){status=EFI_NOT_READY;goto done;}
  if(EFI_ERROR(c->Error)){status=c->Error;goto done;}
  if(!c->Begun){status=EFI_NOT_READY;goto done;}
  if(c->Running)HakAudioPoll(c->Stream);
  if(EFI_ERROR(c->Error)){status=c->Error;goto done;}
  n=frames>HAK_RING_QUEUE_FRAMES?HAK_RING_QUEUE_FRAMES:(UINT32)frames;
  if(c->Ring.Submitted>MAX_UINT64-n){HakFault(c,EFI_VOLUME_FULL,10,c->Ring.Submitted);status=EFI_VOLUME_FULL;goto done;}
  *accepted=HakRingWrite(&c->Ring,samples,n);
  if(c->Running && *accepted) /* r389: silent blocks ahead take the frames now */
    HakRingEarlyFill(&c->Ring,c->Position/HAK_RING_BLOCK_FRAMES,c->Position%HAK_RING_BLOCK_FRAMES,(INT16*)c->Stream->BufferData);
  if(!c->Running && *accepted) {
    for(block=0;block<HAK_RING_BLOCK_COUNT;block++)
      HakRingFill(&c->Ring,block,(INT16*)(c->Stream->BufferData+block*HAK_RING_BLOCK_FRAMES*4));
    c->Position=0;c->LastTick=GetPerformanceCounter();c->LastProgressTick=c->LastTick;c->Running=TRUE;
#ifdef HAK_AUDIO_STARTDELAY_MS
    gBS->Stall(HAK_AUDIO_STARTDELAY_MS*1000);   /* test builds only: a host that needs this long to create its stream, before the stream runs */
#endif
    if(!HdaControllerSetStreamState(c->Stream,TRUE))status=EFI_DEVICE_ERROR;
    else {
      /* r409: a hypervisor creates its host sound stream inside the write of the RUN bit, on the
       * processor's own thread (VMware: 23..48 ms on an idle host, measured; more on a busy one).
       * That time was counted as a late poll: beyond HAK_RING_MAX_POLL_NS the stream was declared
       * stalled before it had played, and the restart that followed met the same wait. A cursor that still reads 0 when the call returns has not moved during the
       * call, so nothing can have been played unseen: the poll clock starts here. A cursor that
       * has moved keeps the clock taken before the call, as before; every later poll is judged
       * by the unchanged limits. */
      EFI_PCI_IO_PROTOCOL *startPci=c->Stream->HdaDev->PciIo;UINT32 startCursor=MAX_UINT32;
#ifdef HAK_AUDIO_TRACE
      c->TraceStartNs=GetTimeInNanoSecond(GetPerformanceCounter()-c->LastTick);c->TracePolls=0;
#endif
      if(!EFI_ERROR(startPci->Mem.Read(startPci,EfiPciIoWidthUint32,PCI_HDA_BAR,HDA_REG_SDNLPIB(c->Stream->Index),1,&startCursor)) && startCursor==0) {
#ifndef HAK_AUDIO_OLDSTART   /* test builds only: the clock as up to r408, to compare */
        c->LastTick=GetPerformanceCounter();c->LastProgressTick=c->LastTick;
#endif
      }
#ifdef HAK_AUDIO_TRACE
      c->TraceStartCursor=startCursor;
#endif
      status=gBS->SetTimer(c->Stream->PollTimer,TimerPeriodic,EFI_TIMER_PERIOD_MILLISECONDS(1));
    }
    if(EFI_ERROR(status))HakFault(c,status,11,status);
  }
done:gBS->RestoreTPL(tpl);return status;
}
static EFI_STATUS EFIAPI HakState(HAK_AUDIO_STREAM *protocol,HAK_AUDIO_STREAM_STATE *state) {
  HAK_AUDIO_STREAM_STATE result;HAK_STREAM_CONTEXT *c;EFI_TPL tpl;
  if(!protocol || !state)return EFI_INVALID_PARAMETER;
  tpl=gBS->RaiseTPL(TPL_NOTIFY);c=HAK_FROM_PROTOCOL(protocol)->HakContext;
  ZeroMem(&result,sizeof(result));result.Size=sizeof(result);
  if(c) {
    if(c->Running)HakAudioPoll(c->Stream);
    result.SubmittedFrames=c->Ring.Submitted;result.ConsumedFrames=c->Ring.Consumed;
    result.QueuedFrames=c->Ring.Count;result.HardwareFrames=HakRingHardwareFrames(&c->Ring);
    result.UnderrunFrames=c->Ring.Underrun;result.LastError=c->Error;
    if(c->Running)result.Flags|=HAK_AUDIO_STREAM_RUNNING;
    if(EFI_ERROR(c->Error))result.Flags|=HAK_AUDIO_STREAM_FAULT;
    else if(HakRingDrained(&c->Ring))result.Flags|=HAK_AUDIO_STREAM_DRAINED;
  } else result.Flags=HAK_AUDIO_STREAM_DRAINED;
  *state=result;gBS->RestoreTPL(tpl);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI HakStop(HAK_AUDIO_STREAM *protocol) {
  EFI_STATUS status;EFI_TPL tpl;
  if(!protocol)return EFI_INVALID_PARAMETER;
  tpl=gBS->RaiseTPL(TPL_NOTIFY);status=HakStopContext(HAK_FROM_PROTOCOL(protocol)->HakContext);
  gBS->RestoreTPL(tpl);return status;
}
static EFI_STATUS EFIAPI HakDiscard(HAK_AUDIO_STREAM *protocol,UINT64 *discarded) {
  HAK_STREAM_CONTEXT *c;EFI_TPL tpl;EFI_STATUS status=EFI_SUCCESS;
  if(!protocol || !discarded)return EFI_INVALID_PARAMETER;
  *discarded=0;tpl=gBS->RaiseTPL(TPL_NOTIFY);c=HAK_FROM_PROTOCOL(protocol)->HakContext;
  if(!c || !c->Begun){status=EFI_NOT_READY;goto done;}
  if(EFI_ERROR(c->Error)){status=c->Error;goto done;}
  if(c->Running) {
    HakAudioPoll(c->Stream);
    if(EFI_ERROR(c->Error)){status=c->Error;goto done;}
    *discarded=HakRingDiscard(&c->Ring,c->Position/HAK_RING_BLOCK_FRAMES,c->Position%HAK_RING_BLOCK_FRAMES,(INT16*)c->Stream->BufferData);
  } else {*discarded=c->Ring.Count;c->Ring.Submitted-=c->Ring.Count;c->Ring.Count=0;}
done:gBS->RestoreTPL(tpl);return status;
}
static EFI_STATUS EFIAPI HakLinger(HAK_AUDIO_STREAM *protocol,CONST INT16 *loop,UINT32 frames,UINT32 repeats) {
  HDA_IO_PRIVATE_DATA *owner;HDA_STREAM *stream;HAK_STREAM_CONTEXT *c;EFI_PCI_IO_PROTOCOL *pci;
  EFI_STATUS status;EFI_TPL tpl;UINT16 format,last;BOOLEAN running;UINT8 id;UINT32 i,total,low,high;
  HDA_BDL_ENTRY *list;EFI_PHYSICAL_ADDRESS loopAt,silenceAt;
  if(!protocol || !loop || !frames || frames>HAK_LINGER_LOOP_BYTES/4 || !repeats || repeats>64)return EFI_INVALID_PARAMETER;
  if(!mHakLinger)return EFI_NOT_READY;
  owner=HAK_FROM_PROTOCOL(protocol);stream=owner->HdaOutputStream;
  if(!stream)return EFI_NOT_READY;
  tpl=gBS->RaiseTPL(TPL_NOTIFY);c=owner->HakContext;
  if(c && (c->Begun || c->Running)) {status=EFI_ACCESS_DENIED;goto done;}   /* Stop first */
  if(!HdaControllerGetStreamState(stream,&running) || running) {status=EFI_ACCESS_DENIED;goto done;}
  if(!HdaControllerGetStreamId(stream,&id) || !id) {status=EFI_NOT_READY;goto done;}
  pci=stream->HdaDev->PciIo;
  status=pci->Mem.Read(pci,EfiPciIoWidthUint16,PCI_HDA_BAR,HDA_REG_SDNFMT(stream->Index),1,&format);
  if(EFI_ERROR(status))goto done;
  if(format!=0x11) {status=EFI_UNSUPPORTED;goto done;}
  gBS->SetTimer(stream->PollTimer,TimerCancel,0);
  if(!HdaControllerResetStream(stream) || !HdaControllerSetStreamId(stream,id)) {status=EFI_DEVICE_ERROR;goto done;}
  status=pci->Mem.Write(pci,EfiPciIoWidthUint16,PCI_HDA_BAR,HDA_REG_SDNFMT(stream->Index),1,&format);
  if(EFI_ERROR(status))goto done;
  list=(HDA_BDL_ENTRY*)(UINTN)mHakLinger;loopAt=mHakLinger+4096;silenceAt=loopAt+HAK_LINGER_LOOP_BYTES;
  CopyMem((VOID*)(UINTN)loopAt,loop,(UINTN)frames*4);
  total=0;
  for(i=0;i<HAK_LINGER_ENTRIES;i++) {
    EFI_PHYSICAL_ADDRESS address=i<repeats?loopAt:silenceAt;UINT32 length=i<repeats?frames*4:HAK_LINGER_SILENCE_BYTES;
    list[i].Address=(UINT32)address;list[i].AddressHigh=(UINT32)(address>>32);
    list[i].Length=length;list[i].InterruptOnCompletion=FALSE;total+=length;
  }
  low=(UINT32)mHakLinger;high=(UINT32)(mHakLinger>>32);last=(UINT16)(HAK_LINGER_ENTRIES-1);
  status=pci->Mem.Write(pci,EfiPciIoWidthUint32,PCI_HDA_BAR,HDA_REG_SDNBDPL(stream->Index),1,&low);
  if(!EFI_ERROR(status))status=pci->Mem.Write(pci,EfiPciIoWidthUint32,PCI_HDA_BAR,HDA_REG_SDNBDPU(stream->Index),1,&high);
  if(!EFI_ERROR(status))status=pci->Mem.Write(pci,EfiPciIoWidthUint16,PCI_HDA_BAR,HDA_REG_SDNLVI(stream->Index),1,&last);
  if(!EFI_ERROR(status))status=pci->Mem.Write(pci,EfiPciIoWidthUint32,PCI_HDA_BAR,HDA_REG_SDNCBL(stream->Index),1,&total);
  if(EFI_ERROR(status))goto done;
  stream->BufferActive=TRUE;   /* the legacy AudioIo path must not take the stream back */
  if(!HdaControllerSetStreamState(stream,TRUE))status=EFI_DEVICE_ERROR;
done:
  gBS->RestoreTPL(tpl);return status;
}
VOID HakAudioInitialize(HDA_IO_PRIVATE_DATA *owner) {
  owner->HakProtocol.Revision=HAK_AUDIO_STREAM_REVISION;owner->HakProtocol.Size=sizeof(HAK_AUDIO_STREAM);
  owner->HakProtocol.Begin=HakBegin;owner->HakProtocol.Write=HakWrite;
  owner->HakProtocol.GetState=HakState;owner->HakProtocol.Stop=HakStop;owner->HakProtocol.Discard=HakDiscard;owner->HakProtocol.Linger=HakLinger;
}
EFI_STATUS HakAudioRelease(HDA_IO_PRIVATE_DATA *owner) {
  EFI_STATUS status=HakStop(&owner->HakProtocol);
  if(!EFI_ERROR(status) && owner->HakContext) {FreePool(owner->HakContext);owner->HakContext=NULL;}
  return status;
}
EFI_STATUS HakAudioStopStream(HDA_STREAM *stream) {
  HAK_STREAM_CONTEXT *c=stream->HakContext;
  return c?HakStop(&c->Owner->HakProtocol):EFI_SUCCESS;
}
