/* HAK HDA controller compatibility for modern machines (Charon r406).
 * SPDX-License-Identifier: BSD-2-Clause
 * Added to the pinned OpenCore AudioDxe. See HakHdaCompat.h.
 *
 * NOT TESTED ON HARDWARE: the class 04/01 and 04/80 binding, the Skylake
 * clock gating and the ATI/AMD/NVIDIA snoop bits were written from the
 * Linux v6.12 sources and the HD Audio specification. QEMU (ich9-intel-hda,
 * class 04/03, vendor 8086) only proves that the class 04/03 path is
 * unchanged and that a non-HDA class 04/01 device (AC'97) is refused. */
#include "HdaController.h"
#include "HakHdaCompat.h"
#include <Library/OcHdaDevicesLib.h>
#include <Library/OcDeviceMiscLib.h>
#ifdef HAK_AUDIO_TRACE
#include <Library/IoLib.h>
static VOID HakCompatTraceByte(UINT8 value) {
  UINTN i;for(i=0;i<1024;i++)if(IoRead8(0x3fd)&0x20){IoWrite8(0x3f8,value);return;}
}
static VOID HakCompatTrace(CONST CHAR8 *name,UINT64 value) {
  UINTN i;INTN shift;for(i=0;name[i]&&i<64;i++)HakCompatTraceByte((UINT8)name[i]);
  HakCompatTraceByte('=');for(shift=60;shift>=0;shift-=4)HakCompatTraceByte((UINT8)"0123456789abcdef"[(value>>shift)&15]);
  HakCompatTraceByte('\r');HakCompatTraceByte('\n');
}
#else
#define HakCompatTrace(name,value)
#endif

/* HD Audio specification 1.0a, 3.3.4 and 3.3.5: read-only constants. */
#define HAK_HDA_REG_OUTPAY        0x04
#define HAK_HDA_REG_INPAY         0x06
#define HAK_HDA_OUTPAY_VALUE      0x003C
#define HAK_HDA_INPAY_VALUE       0x001D
/* Linux hda_intel.c */
#define HAK_INTEL_HDA_CGCTL       0x48
#define HAK_INTEL_HDA_CGCTL_MISCBDCGE BIT6
#define HAK_ATI_MISC_CNTR2        0x42
#define HAK_ATI_ENABLE_SNOOP      0x02
#define HAK_NVIDIA_TRANSREG       0x4e
#define HAK_NVIDIA_ISTRM_COH      0x4d
#define HAK_NVIDIA_OSTRM_COH      0x4c
#define HAK_VEN_AMD_CHIPSET       0x1022
#define HAK_VEN_ATI               0x1002

static BOOLEAN HakClassIsHda(CONST PCI_CLASSCODE *c) {
  return c->BaseCode==PCI_CLASS_MEDIA && c->SubClassCode==PCI_CLASS_MEDIA_MIXED_MODE;
}
/* Intel platforms with the audio DSP enabled report class 04/01 (Linux
 * intel-dsp-config.c: "class=04 subclass 01 prog-if 00: DSP is present");
 * 04/80 is accepted on the same terms. Linux still runs such a controller
 * as plain HD Audio when the codec on the link is an HD Audio codec. */
static BOOLEAN HakClassIsWidened(CONST PCI_CLASSCODE *c) {
  return c->BaseCode==PCI_CLASS_MEDIA && (c->SubClassCode==0x01 || c->SubClassCode==0x80);
}
/* BAR0 must be an assigned memory BAR. Intel AC'97 (also class 04/01) has
 * an I/O BAR here and is left to Charon's own AC'97 output. */
static BOOLEAN HakBarIsMemory(EFI_PCI_IO_PROTOCOL *pci) {
  UINT32 bar[2]={0,0};
  if(EFI_ERROR(pci->Pci.Read(pci,EfiPciIoWidthUint32,PCI_BASE_ADDRESSREG_OFFSET,2,bar)))return FALSE;
  if(bar[0]&BIT0)return FALSE;                         /* I/O space */
  if((bar[0]&~0xFU)==0 && ((bar[0]&0x6)!=0x4 || bar[1]==0))return FALSE; /* not assigned */
  return bar[0]!=0xFFFFFFFFU;
}
/* GCAP/VMAJ/VMIN/OUTPAY/INPAY of an HD Audio 1.0 controller. Reads only. */
static BOOLEAN HakRegistersLookLikeHda(EFI_PCI_IO_PROTOCOL *pci) {
  UINT16 gcap=0,outpay=0,inpay=0;UINT8 vmaj=0,vmin=0xff;
  if(EFI_ERROR(pci->Mem.Read(pci,EfiPciIoWidthUint16,PCI_HDA_BAR,HDA_REG_GCAP,1,&gcap)))return FALSE;
  if(EFI_ERROR(pci->Mem.Read(pci,EfiPciIoWidthUint8,PCI_HDA_BAR,HDA_REG_VMIN,1,&vmin)))return FALSE;
  if(EFI_ERROR(pci->Mem.Read(pci,EfiPciIoWidthUint8,PCI_HDA_BAR,HDA_REG_VMAJ,1,&vmaj)))return FALSE;
  if(EFI_ERROR(pci->Mem.Read(pci,EfiPciIoWidthUint16,PCI_HDA_BAR,HAK_HDA_REG_OUTPAY,1,&outpay)))return FALSE;
  if(EFI_ERROR(pci->Mem.Read(pci,EfiPciIoWidthUint16,PCI_HDA_BAR,HAK_HDA_REG_INPAY,1,&inpay)))return FALSE;
  HakCompatTrace("HAK HDA r406 gcap",gcap|((UINT64)vmaj<<16)|((UINT64)vmin<<24)|((UINT64)outpay<<32)|((UINT64)inpay<<48));
  if(gcap==0 || gcap==0xFFFF || vmaj!=1 || vmin!=0)return FALSE;
  if(outpay!=HAK_HDA_OUTPAY_VALUE || inpay!=HAK_HDA_INPAY_VALUE)return FALSE;
  return HDA_REG_GCAP_OSS(gcap)+HDA_REG_GCAP_BSS(gcap)>=1; /* something to play through */
}
static EFI_STATUS HakReadIdentity(EFI_PCI_IO_PROTOCOL *pci,PCI_CLASSCODE *c,UINT16 *vendor,UINT16 *command) {
  EFI_STATUS status=pci->Pci.Read(pci,EfiPciIoWidthUint8,PCI_CLASSCODE_OFFSET,sizeof(PCI_CLASSCODE),c);
  if(EFI_ERROR(status))return status;
  status=pci->Pci.Read(pci,EfiPciIoWidthUint16,PCI_VENDOR_ID_OFFSET,1,vendor);
  if(EFI_ERROR(status))return status;
  return pci->Pci.Read(pci,EfiPciIoWidthUint16,PCI_COMMAND_OFFSET,1,command);
}

EFI_STATUS
HakHdaControllerSupported (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN CONST UINT8          *ClassCode
  )
{
  PCI_CLASSCODE c;UINT16 vendor=0,command=0;
  CopyMem(&c,ClassCode,sizeof(c));
  if(HakClassIsHda(&c))return EFI_SUCCESS;               /* unchanged path, any vendor */
  if(!HakClassIsWidened(&c))return EFI_UNSUPPORTED;
  if(EFI_ERROR(HakReadIdentity(PciIo,&c,&vendor,&command)) || vendor!=VEN_INTEL_ID)return EFI_UNSUPPORTED;
  if(!HakBarIsMemory(PciIo))return EFI_UNSUPPORTED;
  /* Supported() may not change the device. With decoding off the register
   * check is left to Start (HakHdaControllerStartCheck). */
  if((command&EFI_PCI_COMMAND_MEMORY_SPACE) && !HakRegistersLookLikeHda(PciIo))return EFI_UNSUPPORTED;
  return EFI_SUCCESS;
}

EFI_STATUS
HakHdaControllerStartCheck (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  PCI_CLASSCODE c;UINT16 vendor=0,command=0;UINT64 original=0;EFI_STATUS status;BOOLEAN good;
  status=HakReadIdentity(PciIo,&c,&vendor,&command);
  if(EFI_ERROR(status))return status;
  /* Class 04/03 and a controller forced by device path go the old way. */
  if(HakClassIsHda(&c) || gForcedControllerDevicePath!=NULL)return EFI_SUCCESS;
  if(!HakClassIsWidened(&c) || vendor!=VEN_INTEL_ID || !HakBarIsMemory(PciIo))return EFI_UNSUPPORTED;
  status=PciIo->Attributes(PciIo,EfiPciIoAttributeOperationGet,0,&original);
  if(EFI_ERROR(status))return status;
  status=PciIo->Attributes(PciIo,EfiPciIoAttributeOperationEnable,EFI_PCI_IO_ATTRIBUTE_MEMORY,NULL);
  if(EFI_ERROR(status))return status;
  good=HakRegistersLookLikeHda(PciIo);
  HakCompatTrace("HAK HDA r406 widened class accepted",good);
  if(good)return EFI_SUCCESS;                            /* InitPciHw saves and enables again */
  PciIo->Attributes(PciIo,EfiPciIoAttributeOperationSet,original,NULL);
  return EFI_UNSUPPORTED;
}

static VOID HakUpdatePciByte(EFI_PCI_IO_PROTOCOL *pci,UINT32 offset,UINT8 mask,UINT8 value) {
  UINT8 data=0,wanted;
  if(EFI_ERROR(pci->Pci.Read(pci,EfiPciIoWidthUint8,offset,1,&data)))return;
  wanted=(UINT8)((data&~mask)|(value&mask));
  if(wanted!=data)pci->Pci.Write(pci,EfiPciIoWidthUint8,offset,1,&wanted);
}

VOID
HakHdaControllerPciSetup (
  IN HDA_CONTROLLER_DEV  *HdaControllerDev
  )
{
  EFI_PCI_IO_PROTOCOL *pci=HdaControllerDev->PciIo;
  UINT16 vendor=(UINT16)GET_PCI_VENDOR_ID(HdaControllerDev->VendorId);
  UINT16 device=(UINT16)GET_PCI_DEVICE_ID(HdaControllerDev->VendorId);
  /* ATI SB450/600/700/800/900, AMD Hudson, X370, X570, Stoney, Raven
   * (Ryzen laptops): snoop on, so DMA sees what the processor wrote
   * (Linux AZX_SNOOP_TYPE_ATI; the same device list as hda_intel.c). */
  if((vendor==HAK_VEN_ATI && (device==0x437b || device==0x4383))
     || (vendor==HAK_VEN_AMD_CHIPSET && (device==0x780d || device==0x1457 || device==0x1487 || device==0x157a || device==0x15e3))) {
    HakUpdatePciByte(pci,HAK_ATI_MISC_CNTR2,0x07,HAK_ATI_ENABLE_SNOOP);
    HakCompatTrace("HAK HDA r406 ATI snoop",device);
  }
  /* Every NVIDIA HD Audio function (Linux AZX_SNOOP_TYPE_NVIDIA). */
  if(vendor==VEN_NVIDIA_ID) {
    HakUpdatePciByte(pci,HAK_NVIDIA_TRANSREG,0x0f,0x0f);
    HakUpdatePciByte(pci,HAK_NVIDIA_ISTRM_COH,0x01,0x01);
    HakUpdatePciByte(pci,HAK_NVIDIA_OSTRM_COH,0x01,0x01);
    HakCompatTrace("HAK HDA r406 NVIDIA snoop",device);
  }
}

/* Linux hda_intel.c: every PCI ID with driver type AZX_DRIVER_SKL (v6.12). */
static CONST UINT16 mHakSkylakeIds[]={
  0xa170,0x9d70,0xa171,0x9d71,0xa2f0,0xa348,0x9dc8,0x02c8,0x06c8,0xf1c8,0xa3f0,0xf0c8,
  0x34c8,0x3dc8,0x38c8,0x4dc8,0xa0c8,0x43c8,0x490d,0x4f90,0x4f91,0x4f92,0x7ad0,0x51c8,
  0x51c9,0x51cd,0x51cc,0x54c8,0x4b55,0x4b58,0x7a50,0x51ca,0x51cb,0x51ce,0x51cf,0x7e28,
  0xe2f7,0xa828,0x7f50,0x7728,0xe428,0x5a98,0x3198};
static BOOLEAN HakIsSkylake(HDA_CONTROLLER_DEV *dev) {
  UINTN i;UINT16 device=(UINT16)GET_PCI_DEVICE_ID(dev->VendorId);
  if(GET_PCI_VENDOR_ID(dev->VendorId)!=VEN_INTEL_ID)return FALSE;
  for(i=0;i<ARRAY_SIZE(mHakSkylakeIds);i++)if(mHakSkylakeIds[i]==device)return TRUE;
  return FALSE;
}

UINT32
HakHdaControllerResetPrepare (
  IN HDA_CONTROLLER_DEV  *HdaControllerDev
  )
{
  EFI_PCI_IO_PROTOCOL *pci=HdaControllerDev->PciIo;UINT32 value=0,cleared;
  if(!HakIsSkylake(HdaControllerDev))return 0;
  if(EFI_ERROR(pci->Pci.Read(pci,EfiPciIoWidthUint32,HAK_INTEL_HDA_CGCTL,1,&value)))return 0;
  if(!(value&HAK_INTEL_HDA_CGCTL_MISCBDCGE))return 0;
  cleared=value&~(UINT32)HAK_INTEL_HDA_CGCTL_MISCBDCGE;
  if(EFI_ERROR(pci->Pci.Write(pci,EfiPciIoWidthUint32,HAK_INTEL_HDA_CGCTL,1,&cleared)))return 0;
  HakCompatTrace("HAK HDA r406 clock gating off for reset",value);
  return HAK_INTEL_HDA_CGCTL_MISCBDCGE;
}

VOID
HakHdaControllerResetDone (
  IN HDA_CONTROLLER_DEV  *HdaControllerDev,
  IN UINT32              Gate
  )
{
  EFI_PCI_IO_PROTOCOL *pci=HdaControllerDev->PciIo;UINT32 value=0;
  if(!Gate)return;   /* only what Prepare cleared is set again */
  if(EFI_ERROR(pci->Pci.Read(pci,EfiPciIoWidthUint32,HAK_INTEL_HDA_CGCTL,1,&value)))return;
  value|=Gate;
  pci->Pci.Write(pci,EfiPciIoWidthUint32,HAK_INTEL_HDA_CGCTL,1,&value);
}

EFI_STATUS
HakHdaControllerCodecsAnswer (
  IN HDA_CONTROLLER_DEV  *HdaControllerDev
  )
{
  EFI_PCI_IO_PROTOCOL *pci=HdaControllerDev->PciIo;
  PCI_CLASSCODE c;UINT16 vendor=0,command=0,statests=0;UINT32 index,verb,response;
  EFI_HDA_IO_VERB_LIST list;EFI_STATUS status;
  status=HakReadIdentity(pci,&c,&vendor,&command);
  if(EFI_ERROR(status))return status;
  if(HakClassIsHda(&c) || gForcedControllerDevicePath!=NULL)return EFI_SUCCESS; /* old behaviour: bind even without codecs */
  status=pci->Mem.Read(pci,EfiPciIoWidthUint16,PCI_HDA_BAR,HDA_REG_STATESTS,1,&statests);
  if(EFI_ERROR(status))return status;
  verb=HDA_CODEC_VERB(HDA_VERB_GET_PARAMETER,HDA_PARAMETER_VENDOR_ID);
  ZeroMem(&list,sizeof(list));list.Count=1;list.Verbs=&verb;list.Responses=&response;
  for(index=0;index<HDA_MAX_CODECS;index++) {
    if(!(statests&(1U<<index)))continue;
    response=0;
    status=HdaControllerSendCommands(HdaControllerDev,(UINT8)index,HDA_NID_ROOT,&list);
    if(!EFI_ERROR(status) && response!=0 && response!=0xFFFFFFFFU) {
      HakCompatTrace("HAK HDA r406 widened class codec",response);
      return EFI_SUCCESS;
    }
  }
  /* DSP-only link (SoundWire/SOF, digital microphones only): nothing an
   * HD Audio driver can play through. */
  HakCompatTrace("HAK HDA r406 widened class without codecs",statests);
  return EFI_UNSUPPORTED;
}

BOOLEAN
HakHdaControllerMayShare (
  VOID
  )
{
  return FALSE;
}
