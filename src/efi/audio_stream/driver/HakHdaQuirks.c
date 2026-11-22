/* HAK HDA codec families and machine quirks (Charon r408).
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Ported from the Linux kernel, tag v7.3 (commit c03c1a14507d), files under
 * GPL-2.0-or-later: sound/hda/common/auto_parser.c (fixup pick and chains,
 * speaker/headphone pins without an association), sound/hda/common/codec.c
 * (snd_hda_codec_set_gpio), sound/hda/codecs/realtek/realtek.c
 * (alc_fill_eapd_coef, alc_auto_init_amp, alc_write_gpio, the GPIO fixups),
 * realtek/alc269.c and alc882.c (the function fixups named in
 * tools/gen-charon-hda-quirks.py), sound/hda/codecs/sigmatel.c
 * (stac_gpio_set, the sub-family amplifier GPIO), sound/hda/codecs/analog.c
 * (inverted EAPD of the AD1986A), sound/hda/codecs/cirrus/cs420x.c
 * (amplifier GPIOs). Copyright of that logic: the Linux kernel contributors.
 * The machine table HakHdaQuirkTable.h is generated from the same tree.
 *
 * SAFETY
 *  - Nothing in this file runs for the virtual codecs of QEMU (vendor 1af4)
 *    and VMware (vendor 15ad), nor on any machine whose CPU reports a
 *    hypervisor (CPUID.1:ECX[31]). There the driver behaves exactly as r407.
 *  - A table is used only for the codec IDs (and revisions) the kernel
 *    driver it came from binds to. Vendor verbs (coefficients, the IDT
 *    0x7E7 verb) are sent only to a codec whose ID selects them.
 *  - Pin configuration overrides are kept in the driver's memory; the
 *    codec's own pin defaults are never written, so the operating system
 *    started afterwards sees what the firmware left.
 *  - A codec that matches no table gets the generic bring-up only.
 *  - Off-switch: a file \EFI\HAK\audio-noquirks on any file system the
 *    firmware has connected when the first codec is bound, or a build with
 *    HAK_HDA_NO_QUIRKS defined. Then this file does nothing at all.
 *  - No function here returns a status; errors end the step they occur in.
 *
 * NOT TESTED ON HARDWARE. */
#include "../HdaCodec/HdaCodec.h"
#include "HakHdaCompat.h"
#include "HakHdaQuirks.h"
#include <Library/BaseLib.h>
#include <Protocol/SimpleFileSystem.h>
#include "HakHdaQuirkTable.h"
#include "HakHdaQuirkMatch.h"

#ifdef HAK_AUDIO_TRACE
#include <Library/IoLib.h>
static VOID HakQuirkTraceByte(UINT8 value) {
  UINTN i;for(i=0;i<1024;i++)if(IoRead8(0x3fd)&0x20){IoWrite8(0x3f8,value);return;}
}
static VOID HakQuirkTrace(CONST CHAR8 *name,UINT64 value) {
  UINTN i;INTN shift;for(i=0;name[i]&&i<80;i++)HakQuirkTraceByte((UINT8)name[i]);
  HakQuirkTraceByte('=');for(shift=60;shift>=0;shift-=4)HakQuirkTraceByte((UINT8)"0123456789abcdef"[(value>>shift)&15]);
  HakQuirkTraceByte('\r');HakQuirkTraceByte('\n');
}
#else
#define HakQuirkTrace(name,value)
#endif

#define HAK_NID_REALTEK_COEF  0x20   /* Realtek vendor widget (alc_*_coef_idx) */

typedef struct {
  HDA_CODEC_DEV           *Dev;
  CONST HAK_HDA_FAMILY    *Family;
  BOOLEAN                 Off;          /* virtual codec or switched off: do nothing */
  BOOLEAN                 SkuIgnore;
  BOOLEAN                 EapdNormal;
  BOOLEAN                 GpioReplaced; /* a fixup replaced the family's default GPIO */
  UINT8                   GpioMask;
  UINT8                   GpioDir;
  UINT8                   GpioData;
  UINT8                   PulseMask;
  UINT32                  CodecSsid;
  UINT16                  Order[HAK_HDA_CHAIN_MAX];
  UINTN                   Count;
} HAK_QUIRK_STATE;

/* One entry per bound codec; an entry is rewritten when its codec is bound again. */
static HAK_QUIRK_STATE  mHakState[8];
static UINTN            mHakStateNext;
static UINT8            mHakSwitch;   /* 0 not looked yet, 1 quirks allowed, 2 switched off */

static HAK_QUIRK_STATE *HakStateFind(HDA_CODEC_DEV *dev) {
  UINTN i;
  for(i=0;i<sizeof(mHakState)/sizeof(mHakState[0]);i++)if(mHakState[i].Dev==dev)return &mHakState[i];
  return NULL;
}

static HAK_QUIRK_STATE *HakStateNew(HDA_CODEC_DEV *dev) {
  HAK_QUIRK_STATE *st=HakStateFind(dev);
  if(st==NULL) {
    st=&mHakState[mHakStateNext];
    mHakStateNext=(mHakStateNext+1)%(sizeof(mHakState)/sizeof(mHakState[0]));
  }
  ZeroMem(st,sizeof(*st));
  st->Dev=dev;
  return st;
}

static BOOLEAN HakCodecIsVirtual(UINT32 vendorId) {
  UINT16 vendor=(UINT16)(vendorId>>16);
  if(vendor==0x1af4 || vendor==0x15ad)return TRUE;   /* QEMU hda-codec, VMware HD Audio */
#if defined(MDE_CPU_X64) || defined(MDE_CPU_IA32)
  { UINT32 ecx=0;AsmCpuid(1,NULL,NULL,&ecx,NULL);if((ecx&BIT31)!=0)return TRUE; }   /* any hypervisor */
#endif
  return FALSE;
}

static BOOLEAN HakQuirksSwitchedOff(VOID) {
#ifdef HAK_HDA_NO_QUIRKS
  return TRUE;
#else
  EFI_HANDLE *handles=NULL;UINTN count=0,i;
  if(mHakSwitch!=0)return (BOOLEAN)(mHakSwitch==2);
  mHakSwitch=1;
  if(EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&count,&handles)) || handles==NULL)return FALSE;
  for(i=0;i<count && mHakSwitch==1;i++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;EFI_FILE_PROTOCOL *root=NULL,*file=NULL;
    if(EFI_ERROR(gBS->HandleProtocol(handles[i],&gEfiSimpleFileSystemProtocolGuid,(VOID **)&fs)) || fs==NULL)continue;
    if(EFI_ERROR(fs->OpenVolume(fs,&root)) || root==NULL)continue;
    if(!EFI_ERROR(root->Open(root,&file,L"\\EFI\\HAK\\audio-noquirks",EFI_FILE_MODE_READ,0)) && file!=NULL) {
      file->Close(file);
      mHakSwitch=2;
    }
    root->Close(root);
  }
  FreePool(handles);
  return (BOOLEAN)(mHakSwitch==2);
#endif
}

static HDA_WIDGET_DEV *HakWidget(HDA_FUNC_GROUP *group,UINT8 nid) {
  UINTN w;
  for(w=0;w<group->WidgetsCount;w++)if(group->Widgets[w].NodeId==nid)return &group->Widgets[w];
  return NULL;
}

/* --- Realtek processing coefficients (realtek.c alc_read/write/update_coefex_idx) --- */

static BOOLEAN HakCoefRead(EFI_HDA_IO_PROTOCOL *io,UINT8 nid,UINT16 index,UINT32 *value) {
  UINT32 response=0;
  if(EFI_ERROR(io->SendCommand(io,nid,HDA_CODEC_VERB(HDA_VERB_SET_COEFFICIENT_INDEX,index),&response)))return FALSE;
  if(EFI_ERROR(io->SendCommand(io,nid,HDA_CODEC_VERB(HDA_VERB_GET_PROCESSING_COEFFICIENT,0),&response)))return FALSE;
  if(response==0xffffffff)return FALSE;
  *value=response;
  return TRUE;
}

static VOID HakCoefWrite(EFI_HDA_IO_PROTOCOL *io,UINT8 nid,UINT16 index,UINT16 value) {
  UINT32 response=0;
  if(EFI_ERROR(io->SendCommand(io,nid,HDA_CODEC_VERB(HDA_VERB_SET_COEFFICIENT_INDEX,index),&response)))return;
  io->SendCommand(io,nid,HDA_CODEC_VERB(HDA_VERB_SET_PROCESSING_COEFFICIENT,value),&response);
}

static VOID HakCoefUpdate(EFI_HDA_IO_PROTOCOL *io,UINT8 nid,UINT16 index,UINT16 mask,UINT16 bits) {
  UINT32 value=0;
  if(!HakCoefRead(io,nid,index,&value))return;
  HakCoefWrite(io,nid,index,(UINT16)((value&~(UINT32)mask)|bits));
}

#define HakAlcUpdate(io,index,mask,bits)  HakCoefUpdate(io,HAK_NID_REALTEK_COEF,index,(UINT16)(mask),(UINT16)(bits))
#define HakAlcWrite(io,index,value)       HakCoefWrite(io,HAK_NID_REALTEK_COEF,index,(UINT16)(value))

/* realtek.c alc_fill_eapd_coef(): "Change EAPD to verb control". Without it
 * the EAPD line of these codecs follows the codec's own power logic and
 * not the EAPD verb. Keyed by the exact codec ID. */
static VOID HakRealtekFillEapdCoef(HDA_CODEC_DEV *dev) {
  EFI_HDA_IO_PROTOCOL *io=dev->HdaIo;UINT32 coef=0;
  switch(dev->VendorId) {
    case 0x10ec0269: case 0x10ec0662: case 0x10ec0888:
      if(!HakCoefRead(io,HAK_NID_REALTEK_COEF,0,&coef))return;   /* alc_get_coef0() */
      break;
    default: break;
  }
  switch(dev->VendorId) {
    case 0x10ec0262: HakAlcUpdate(io,0x7,0,1<<5);break;
    case 0x10ec0267: case 0x10ec0268: HakAlcUpdate(io,0x7,0,1<<13);break;
    case 0x10ec0269:
      if((coef&0x00f0)==0x0010)HakAlcUpdate(io,0xd,0,1<<14);
      if((coef&0x00f0)==0x0020)HakAlcUpdate(io,0x4,1<<15,0);
      if((coef&0x00f0)==0x0030)HakAlcUpdate(io,0x10,1<<9,0);
      break;
    case 0x10ec0280: case 0x10ec0284: case 0x10ec0290: case 0x10ec0292:
      HakAlcUpdate(io,0x4,1<<15,0);break;
    case 0x10ec0225: case 0x10ec0295: case 0x10ec0299:
      HakAlcUpdate(io,0x67,0xf000,0x3000);
      HakAlcUpdate(io,0x36,1<<13,0);
      HakAlcUpdate(io,0x10,1<<9,0);
      break;
    case 0x10ec0215: case 0x10ec0236: case 0x10ec0245: case 0x10ec0256: case 0x10ec0257: case 0x10ec0285: case 0x10ec0289:
      HakAlcUpdate(io,0x36,1<<13,0);
      HakAlcUpdate(io,0x10,1<<9,0);
      break;
    case 0x10ec0230: case 0x10ec0233: case 0x10ec0235: case 0x10ec0255: case 0x19e58326: case 0x10ec0282:
    case 0x10ec0283: case 0x10ec0286: case 0x10ec0288: case 0x10ec0298: case 0x10ec0300:
      HakAlcUpdate(io,0x10,1<<9,0);break;
    case 0x10ec0275: HakAlcUpdate(io,0xe,0,1<<0);break;
    case 0x10ec0287: HakAlcUpdate(io,0x10,1<<9,0);HakAlcWrite(io,0x8,0x4ab7);break;
    case 0x10ec0293: HakAlcUpdate(io,0xa,1<<13,0);break;
    case 0x10ec0234: case 0x10ec0274:
      HakAlcWrite(io,0x6e,0x0c25);
      HakAlcUpdate(io,0x10,1<<15,0);
      break;
    case 0x10ec0294: case 0x10ec0700: case 0x10ec0701: case 0x10ec0703: case 0x10ec0711:
      HakAlcUpdate(io,0x10,1<<15,0);break;
    case 0x10ec0662: if((coef&0x00f0)==0x0030)HakAlcUpdate(io,0x4,1<<10,0);break;
    case 0x10ec0272: case 0x10ec0273: case 0x10ec0663: case 0x10ec0665: case 0x10ec0670: case 0x10ec0671: case 0x10ec0672:
      HakAlcUpdate(io,0xd,0,1<<14);break;
    case 0x10ec0222: case 0x10ec0623: HakAlcUpdate(io,0x19,1<<13,0);break;
    case 0x10ec0668: HakAlcUpdate(io,0x7,3<<13,0);break;
    case 0x10ec0867: HakAlcUpdate(io,0x4,1<<10,0);break;
    case 0x10ec0888: if((coef&0x00f0)==0x0020 || (coef&0x00f0)==0x0030)HakAlcUpdate(io,0x7,1<<5,0);break;
    case 0x10ec0892: case 0x10ec0897: HakAlcUpdate(io,0x7,1<<5,0);break;
    case 0x10ec0899: case 0x10ec0900: case 0x10ec0b00: case 0x10ec1168: case 0x10ec1220:
      HakAlcUpdate(io,0x7,1<<1,0);break;
    default: break;
  }
}

/* realtek.c alc_auto_init_amp(), type ALC_INIT_DEFAULT ("Turn EAPD to High"):
 * the amplifier type the kernel falls back to when the assembly ID names
 * no amplifier GPIO. */
static VOID HakRealtekDefaultAmp(HDA_CODEC_DEV *dev) {
  EFI_HDA_IO_PROTOCOL *io=dev->HdaIo;UINT32 coef=0;
  switch(dev->VendorId) {
    case 0x10ec0260: HakCoefUpdate(io,0x1a,7,0,0x2010);break;
    case 0x10ec0880: case 0x10ec0882: case 0x10ec0883: case 0x10ec0885: HakAlcUpdate(io,7,0,0x2030);break;
    case 0x10ec0888:
      if(HakCoefRead(io,HAK_NID_REALTEK_COEF,0,&coef) && ((coef&0x00f0)==0x00 || (coef&0x00f0)==0x10))HakAlcUpdate(io,7,0,0x2030);
      break;
    default: break;
  }
}

/* --- GPIO (codec.c snd_hda_codec_set_gpio with the 1 ms delay; sigmatel.c stac_gpio_set) --- */

static VOID HakGpioWrite(HDA_CODEC_DEV *dev,UINT8 mask,UINT8 dir,UINT8 data,BOOLEAN idtCmos) {
  EFI_HDA_IO_PROTOCOL *io=dev->HdaIo;HDA_FUNC_GROUP *group=dev->AudioFuncGroup;
  UINT32 enable=0,direction=0,state=0,response=0;UINT8 count;
  count=HDA_PARAMETER_GPIO_COUNT_NUM_GPIOS(group->GpioCapabilities);
  if(count==0 || count>8)return;
  mask=(UINT8)(mask&((1U<<count)-1));
  if(mask==0)return;
  if(EFI_ERROR(io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_GET_GPIO_ENABLE_MASK,0),&enable))
     || EFI_ERROR(io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_GET_GPIO_DIRECTION,0),&direction))
     || EFI_ERROR(io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_GET_GPIO_DATA,0),&state)))return;
  /* The GPIOs outside the mask keep what the firmware gave them. */
  enable=(enable&0xff)|mask;
  direction=((direction&0xff)&~(UINT32)mask)|(dir&mask);
  state=((state&0xff)&~(UINT32)mask)|(data&mask);
  HakQuirkTrace("HAK HDA r408 GPIO enable/direction/data",((UINT64)enable<<16)|((UINT64)direction<<8)|state);
  if(idtCmos)io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(0x7e7,0),&response);   /* IDT: "Configure GPIOx as CMOS" */
  if(EFI_ERROR(io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_SET_GPIO_ENABLE_MASK,(UINT8)enable),&response)))return;
  if(EFI_ERROR(io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_SET_GPIO_DIRECTION,(UINT8)direction),&response)))return;
  gBS->Stall(1000);
  io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_SET_GPIO_DATA,(UINT8)state),&response);
}

/* --- identification --- */

static VOID HakReadSsids(HDA_CODEC_DEV *dev,HDA_FUNC_GROUP *group,UINT16 *pci,UINT32 *codec) {
  EFI_HDA_IO_PROTOCOL *io=dev->HdaIo;EFI_DEVICE_PATH_PROTOCOL *path=dev->DevicePath;
  EFI_HANDLE handle=NULL;EFI_PCI_IO_PROTOCOL *pciIo=NULL;UINT32 response=0;
  pci[0]=0;pci[1]=0;*codec=0;
  if(path!=NULL && !EFI_ERROR(gBS->LocateDevicePath(&gEfiPciIoProtocolGuid,&path,&handle))
     && !EFI_ERROR(gBS->HandleProtocol(handle,&gEfiPciIoProtocolGuid,(VOID **)&pciIo)) && pciIo!=NULL) {
    if(EFI_ERROR(pciIo->Pci.Read(pciIo,EfiPciIoWidthUint16,PCI_SUBSYSTEM_VENDOR_ID_OFFSET,2,pci))) { pci[0]=0;pci[1]=0; }
    if(pci[0]==0xffff) { pci[0]=0;pci[1]=0; }
  }
  /* Linux snd_hdac_device_init(): root parameter 1, else the function group's verb F20. */
  if(!EFI_ERROR(io->SendCommand(io,HDA_NID_ROOT,HDA_CODEC_VERB(HDA_VERB_GET_PARAMETER,0x01),&response))
     && response!=0 && response!=0xffffffff) {
    *codec=response;
  } else if(!EFI_ERROR(io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_GET_IMPLEMENTATION_ID,0),&response))
            && response!=0xffffffff) {
    *codec=response;
  }
}

/* auto_parser.c snd_hda_parse_pin_defcfg(): a fixed or jack pin whose device
 * is "speaker" or "headphone out" is an output even when its association
 * is 0 (only line-outs need one). AudioDxe ignores every pin with
 * association 0, so such pins get association 15 in the driver's copy. */
static VOID HakAcceptUnassociatedOutputs(HDA_FUNC_GROUP *group) {
  UINTN w;
  for(w=0;w<group->WidgetsCount;w++) {
    HDA_WIDGET_DEV *widget=&group->Widgets[w];UINT32 config=widget->DefaultConfiguration;UINT32 device=(config>>20)&0xf;
    if(widget->Type!=HDA_WIDGET_TYPE_PIN_COMPLEX || !(widget->PinCapabilities&HDA_PARAMETER_PIN_CAPS_OUTPUT))continue;
    if((config>>30)==1 || (config&0xf0)!=0)continue;       /* no physical connection / already associated */
    if(device!=0x1 && device!=0x2)continue;                 /* speaker, headphone out */
    HakQuirkTrace("HAK HDA r408 output pin without association accepted",widget->NodeId);
    widget->DefaultConfiguration=config|0xf0;
  }
}

static VOID HakPinOverride(HDA_FUNC_GROUP *group,UINT8 nid,UINT32 config) {
  HDA_WIDGET_DEV *widget=HakWidget(group,nid);
  if(widget==NULL || widget->Type!=HDA_WIDGET_TYPE_PIN_COMPLEX)return;
  HakQuirkTrace("HAK HDA r408 pin configuration override",((UINT64)nid<<32)|config);
  widget->DefaultConfiguration=config;
}

/* The part of a ported function fixup the kernel runs at HDA_FIXUP_ACT_PRE_PROBE. */
static VOID HakFunctionProbe(HAK_QUIRK_STATE *st,HDA_FUNC_GROUP *group,CONST HAK_HDA_FIXUP *fix) {
  UINT8 mask=(UINT8)fix->Arg;
  switch(fix->Data) {
    case HAK_HDA_FN_GPIO_HIGH:
      st->GpioMask|=mask;st->GpioDir|=mask;st->GpioData|=mask;break;
    case HAK_HDA_FN_GPIO_SET:
      st->GpioMask=(UINT8)(fix->Arg>>16);st->GpioDir=(UINT8)(fix->Arg>>8);st->GpioData=(UINT8)fix->Arg;st->GpioReplaced=TRUE;break;
    case HAK_HDA_FN_GPIO_PULSE:
      st->PulseMask|=mask;break;
    case HAK_HDA_FN_SKU_IGNORE:
      st->SkuIgnore=TRUE;break;
    case HAK_HDA_FN_ALC_COEF_SET_BITS:
      if((st->Dev->VendorId>>16)==0x10ec)HakAlcUpdate(st->Dev->HdaIo,(UINT16)(fix->Arg>>16),0,fix->Arg);
      break;
    case HAK_HDA_FN_PIN17_BASS_SPEAKER:
      HakPinOverride(group,0x17,0x90170121);break;
    case HAK_HDA_FN_EAPD_NOT_INVERTED:
      st->EapdNormal=TRUE;break;
    case HAK_HDA_FN_IDT_DELL_DMIC_GPIO2:
      if(st->CodecSsid!=0x1028022f) { st->GpioMask=0x04;st->GpioDir=0x04;st->GpioData=0x04;st->GpioReplaced=TRUE; }
      break;
    default: break;
  }
}

VOID
HakHdaQuirkProbe (
  IN HDA_CODEC_DEV  *HdaCodecDev
  )
{
  HAK_QUIRK_STATE *st;HDA_FUNC_GROUP *group;CONST HAK_HDA_QUIRK *row;UINT16 pci[2];UINTN i,index;
  if(HdaCodecDev==NULL || HdaCodecDev->HdaIo==NULL)return;
  st=HakStateNew(HdaCodecDev);
  group=HdaCodecDev->AudioFuncGroup;
  if(group==NULL || group->Widgets==NULL || HakCodecIsVirtual(HdaCodecDev->VendorId) || HakQuirksSwitchedOff()) {
    st->Off=TRUE;
    HakQuirkTrace("HAK HDA r408 quirks not used for codec",HdaCodecDev->VendorId);
    return;
  }
  HakAcceptUnassociatedOutputs(group);
  HakReadSsids(HdaCodecDev,group,pci,&st->CodecSsid);
  HakRealtekFillEapdCoef(HdaCodecDev);
  st->Family=HakQuirkFamily(HdaCodecDev->VendorId,HdaCodecDev->RevisionId);
  HakQuirkTrace("HAK HDA r408 codec",((UINT64)HdaCodecDev->VendorId<<32)|st->CodecSsid);
  HakQuirkTrace("HAK HDA r408 PCI subsystem",((UINT64)pci[0]<<16)|pci[1]);
  if(st->Family==NULL || st->Family->Quirks==NULL)return;
  row=HakQuirkPick(st->Family,pci[0],pci[1],(UINT16)(st->CodecSsid>>16),(UINT16)st->CodecSsid);
  if(row==NULL)return;
  HakQuirkTrace("HAK HDA r408 machine row",(UINT64)(row-st->Family->Quirks));
  index=row->Fixup&HAK_HDA_QUIRK_NO_ACTION;
  if(index==HAK_HDA_QUIRK_NO_ACTION || st->Family->Fixups==NULL)return;
  HakQuirkChainWalk(st->Family,index,0,st->Order,&st->Count);
  for(i=0;i<st->Count;i++) {
    CONST HAK_HDA_FIXUP *fix=&st->Family->Fixups[st->Order[i]];
    if(fix->Type==HAK_HDA_FIX_PINS && st->Family->Pins!=NULL) {
      CONST HAK_HDA_PINCFG *pin;
      for(pin=&st->Family->Pins[fix->Data];pin->Nid!=0;pin++)HakPinOverride(group,pin->Nid,pin->Config);
    } else if(fix->Type==HAK_HDA_FIX_FUNC) {
      HakFunctionProbe(st,group,fix);
    }
  }
}

BOOLEAN
HakHdaQuirkEapdInverted (
  IN HDA_CODEC_DEV  *HdaCodecDev
  )
{
  HAK_QUIRK_STATE *st=HakStateFind(HdaCodecDev);
  return (BOOLEAN)(st!=NULL && !st->Off && st->Family!=NULL
                   && (st->Family->Flags&HAK_HDA_FAM_EAPD_INVERTED)!=0 && !st->EapdNormal);
}

BOOLEAN
HakHdaQuirkSkuIgnored (
  IN HDA_CODEC_DEV  *HdaCodecDev
  )
{
  HAK_QUIRK_STATE *st=HakStateFind(HdaCodecDev);
  return (BOOLEAN)(st!=NULL && !st->Off && st->SkuIgnore);
}

static BOOLEAN HakIsOutputPort(HDA_CODEC_DEV *dev,UINT8 nid) {
  UINTN i;
  for(i=0;i<dev->OutputPortsCount;i++)if(dev->OutputPorts[i]!=NULL && dev->OutputPorts[i]->NodeId==nid)return TRUE;
  return FALSE;
}

VOID
HakHdaQuirkFinish (
  IN HDA_CODEC_DEV  *HdaCodecDev
  )
{
  HAK_QUIRK_STATE *st;HDA_FUNC_GROUP *group;EFI_HDA_IO_PROTOCOL *io;UINT32 response=0;UINTN i,w;
  UINT8 mask,dir,data;BOOLEAN idt;
  if(HdaCodecDev==NULL || HdaCodecDev->HdaIo==NULL)return;
  st=HakStateFind(HdaCodecDev);
  group=HdaCodecDev->AudioFuncGroup;
  if(st==NULL || st->Off || group==NULL || group->Widgets==NULL)return;
  io=HdaCodecDev->HdaIo;

  /* AD1986A: AudioDxe has just set EAPD on the output pins; here that means "amplifier off". */
  if(HakHdaQuirkEapdInverted(HdaCodecDev)) {
    for(w=0;w<group->WidgetsCount;w++) {
      HDA_WIDGET_DEV *widget=&group->Widgets[w];
      if(widget->Type!=HDA_WIDGET_TYPE_PIN_COMPLEX || !(widget->PinCapabilities&HDA_PARAMETER_PIN_CAPS_EAPD))continue;
      if(EFI_ERROR(io->SendCommand(io,widget->NodeId,HDA_CODEC_VERB(HDA_VERB_GET_EAPD_BTL_ENABLE,0),&response)))continue;
      if(!(response&HDA_EAPD_BTL_ENABLE_EAPD))continue;
      response&=(HDA_EAPD_BTL_ENABLE_BTL|HDA_EAPD_BTL_ENABLE_L_R_SWAP);
      io->SendCommand(io,widget->NodeId,HDA_CODEC_VERB(HDA_VERB_SET_EAPD_BTL_ENABLE,(UINT8)response),&response);
    }
  }

  /* Amplifier GPIOs: the machine's fixups, else the sub-family default. */
  mask=st->GpioMask;dir=st->GpioDir;data=st->GpioData;
  idt=(BOOLEAN)(st->Family!=NULL && (st->Family->Flags&HAK_HDA_FAM_IDT_GPIO0_EAPD)!=0);
  if(idt && !st->GpioReplaced) { mask|=0x01;dir|=0x01;data|=0x01; }
  mask|=st->PulseMask;dir|=st->PulseMask;
  HakGpioWrite(HdaCodecDev,mask,dir,data,idt);

  /* Realtek without an amplifier GPIO in its assembly ID: the default amplifier coefficient. */
  if((HdaCodecDev->VendorId>>16)==0x10ec && (st->SkuIgnore || HakHdaCodecRealtekSkuGpio(HdaCodecDev)==0))HakRealtekDefaultAmp(HdaCodecDev);

  if(st->Family==NULL || st->Family->Fixups==NULL)return;

  /* Verb lists and pin controls of the machine, in the kernel's chain order. */
  for(i=0;i<st->Count;i++) {
    CONST HAK_HDA_FIXUP *fix=&st->Family->Fixups[st->Order[i]];
    if(fix->Type==HAK_HDA_FIX_VERBS && st->Family->Verbs!=NULL) {
      CONST UINT32 *verb;
      for(verb=&st->Family->Verbs[fix->Data];*verb!=0;verb++) {
        UINT8 nid=(UINT8)(*verb>>24);
        if(nid!=group->NodeId && HakWidget(group,nid)==NULL)continue;   /* node this codec does not have */
        if(EFI_ERROR(io->SendCommand(io,nid,*verb&0xfffff,&response)))break;
      }
    } else if(fix->Type==HAK_HDA_FIX_PINCTLS && st->Family->Pins!=NULL) {
      CONST HAK_HDA_PINCFG *pin;
      for(pin=&st->Family->Pins[fix->Data];pin->Nid!=0;pin++) {
        HDA_WIDGET_DEV *widget=HakWidget(group,pin->Nid);
        /* Output pins keep the control the path set-up gave them, as in the kernel. */
        if(widget==NULL || widget->Type!=HDA_WIDGET_TYPE_PIN_COMPLEX || HakIsOutputPort(HdaCodecDev,pin->Nid))continue;
        io->SendCommand(io,pin->Nid,HDA_CODEC_VERB(HDA_VERB_SET_PIN_WIDGET_CONTROL,(UINT8)pin->Config),&response);
      }
    }
  }

  /* The part of the ported function fixups the kernel runs at HDA_FIXUP_ACT_INIT. */
  for(i=0;i<st->Count;i++) {
    CONST HAK_HDA_FIXUP *fix=&st->Family->Fixups[st->Order[i]];
    if(fix->Type!=HAK_HDA_FIX_FUNC)continue;
    if(fix->Data==HAK_HDA_FN_GPIO_PULSE) {
      UINT8 pulse=(UINT8)fix->Arg;
      HakGpioWrite(HdaCodecDev,pulse,pulse,pulse,FALSE);
      gBS->Stall(100000);
      HakGpioWrite(HdaCodecDev,pulse,pulse,0,FALSE);
    } else if(fix->Data==HAK_HDA_FN_ALC_COEF_WRITE_100MS && (HdaCodecDev->VendorId>>16)==0x10ec) {
      gBS->Stall(100000);
      HakAlcWrite(io,(UINT16)(fix->Arg>>16),fix->Arg);
    }
  }
}
