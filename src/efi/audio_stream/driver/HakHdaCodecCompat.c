/* HAK HDA codec output preparation for laptops (Charon r406).
 * SPDX-License-Identifier: BSD-2-Clause
 * Added to the pinned OpenCore AudioDxe. See HakHdaCompat.h.
 *
 * What AudioDxe already did for every selected output pin: pin widget
 * control OUT, EAPD on that pin, output amplifiers unmuted along the path
 * (pin, mixer/selector, DAC), the path's input amplifier unmuted and the
 * other mixer inputs muted, connection select, D0 once at probe time.
 * Added here:
 *  - D0 is checked again before playback and waited for (firmware or an
 *    earlier loader may have left the function group in D3);
 *  - EAPD is switched on for EVERY pin of the audio function group that
 *    has the capability, not only the pins being played to. The amplifier
 *    of the built-in speakers is often wired to the EAPD line of another
 *    pin (Linux: Realtek alc_auto_setup_eapd() on NIDs 0x0f/0x10/0x14/
 *    0x15/0x17; Conexant cx_auto_parse_eapd() collects every pin that
 *    has the EAPD capability and switches them together);
 *  - headphone pins get the headphone amplifier bit (HP_EN) with OUT_EN;
 *  - Realtek: the amplifier GPIO that the board designer encoded in the
 *    codec subsystem ID (or in the pin default of NID 0x1d) is enabled,
 *    made an output and driven high (Linux alc_subsystem_id()).
 *
 * What cannot be done generically (Linux needs a per-model quirk table,
 * keyed by PCI/codec subsystem ID, that is far too large and too risky to
 * replay blind): GPIOs of Realtek machines whose quirk sets them by hand
 * (many HP/Dell/Lenovo/ASUS models: alc_fixup_gpio*, mute-LED and
 * amplifier GPIOs with either polarity), Apple Cirrus CS420x GPIOs,
 * IDT/SigmaTel GPIO-switched EAPD, Conexant GPIO amplifiers, vendor
 * coefficient writes (ALC2xx class-D enable, ALC1220 dual codecs),
 * inverted EAPD boards, and every amplifier behind I2C/SPI (CS35L41,
 * TAS2781) or SoundWire. For such machines OpenCore's load options
 * --gpio-setup / --gpio-pins are still honoured by this driver.
 *
 * NOT TESTED ON HARDWARE. QEMU's codec has no EAPD, GPIO or power
 * control, so the virtual machine only proves that nothing regressed. */
#include "../HdaCodec/HdaCodec.h"
#include "HakHdaCompat.h"
#include "HakHdaQuirks.h" /* r408: inverted EAPD, SKU ignore (GPL-2.0-or-later part) */
#include <Library/OcHdaDevicesLib.h>
#ifdef HAK_AUDIO_TRACE
#include <Library/IoLib.h>
static VOID HakCodecTraceByte(UINT8 value) {
  UINTN i;for(i=0;i<1024;i++)if(IoRead8(0x3fd)&0x20){IoWrite8(0x3f8,value);return;}
}
static VOID HakCodecTrace(CONST CHAR8 *name,UINT64 value) {
  UINTN i;INTN shift;for(i=0;name[i]&&i<64;i++)HakCodecTraceByte((UINT8)name[i]);
  HakCodecTraceByte('=');for(shift=60;shift>=0;shift-=4)HakCodecTraceByte((UINT8)"0123456789abcdef"[(value>>shift)&15]);
  HakCodecTraceByte('\r');HakCodecTraceByte('\n');
}
#else
#define HakCodecTrace(name,value)
#endif

#define HAK_POWER_ACTUAL(r)  (((r)>>4)&0xF)   /* PS-Act of Get Power State */

VOID
HakHdaCodecWaitPower (
  IN EFI_HDA_IO_PROTOCOL  *HdaIo,
  IN UINT8                NodeId
  )
{
  UINT32 response=0;UINTN tries;
  /* Linux snd_hda_sync_power_state() polls the same field (up to 500 ms). */
  for(tries=0;tries<50;tries++) {
    if(EFI_ERROR(HdaIo->SendCommand(HdaIo,NodeId,HDA_CODEC_VERB(HDA_VERB_GET_POWER_STATE,0),&response)))return;
    if(HAK_POWER_ACTUAL(response)==0)return;
    gBS->Stall(2000);
  }
}

BOOLEAN
HakHdaCodecPinIsHeadphone (
  IN HDA_WIDGET_DEV  *HdaWidget
  )
{
  /* Linux hda_generic: PIN_HP (OUT_EN|HP_EN) for headphone pins; HP_EN is
   * dropped when the pin has no headphone drive capability. */
  return HdaWidget!=NULL && HdaWidget->Type==HDA_WIDGET_TYPE_PIN_COMPLEX
    && HDA_VERB_GET_CONFIGURATION_DEFAULT_DEVICE(HdaWidget->DefaultConfiguration)==HDA_CONFIG_DEFAULT_DEVICE_HEADPHONE_OUT
    && (HdaWidget->PinCapabilities&HDA_PARAMETER_PIN_CAPS_HEADPHONE)!=0;
}

static VOID HakPowerUp(HDA_CODEC_DEV *dev,HDA_FUNC_GROUP *group) {
  EFI_HDA_IO_PROTOCOL *io=dev->HdaIo;UINT32 response=0;UINTN w;BOOLEAN changed=FALSE;
  if(!EFI_ERROR(io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_GET_POWER_STATE,0),&response))
     && HAK_POWER_ACTUAL(response)!=0) {
    HakCodecTrace("HAK HDA r406 function group was in power state",HAK_POWER_ACTUAL(response));
    io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_SET_POWER_STATE,0),&response);
    HakHdaCodecWaitPower(io,group->NodeId);
    changed=TRUE;
  }
  for(w=0;w<group->WidgetsCount;w++) {
    HDA_WIDGET_DEV *widget=group->Widgets+w;
    if(!(widget->Capabilities&HDA_PARAMETER_WIDGET_CAPS_POWER_CNTRL))continue;
    if(EFI_ERROR(io->SendCommand(io,widget->NodeId,HDA_CODEC_VERB(HDA_VERB_GET_POWER_STATE,0),&response)))continue;
    if(HAK_POWER_ACTUAL(response)==0)continue;
    io->SendCommand(io,widget->NodeId,HDA_CODEC_VERB(HDA_VERB_SET_POWER_STATE,0),&response);
    changed=TRUE;
  }
  if(changed)gBS->Stall(10000);   /* let the nodes settle after leaving D3 */
}

static VOID HakEapdOn(HDA_CODEC_DEV *dev,HDA_FUNC_GROUP *group) {
  EFI_HDA_IO_PROTOCOL *io=dev->HdaIo;UINT32 response=0;UINTN w;
  for(w=0;w<group->WidgetsCount;w++) {
    HDA_WIDGET_DEV *widget=group->Widgets+w;
    if(widget->Type!=HDA_WIDGET_TYPE_PIN_COMPLEX || !(widget->PinCapabilities&HDA_PARAMETER_PIN_CAPS_EAPD))continue;
    if(EFI_ERROR(io->SendCommand(io,widget->NodeId,HDA_CODEC_VERB(HDA_VERB_GET_EAPD_BTL_ENABLE,0),&response)))continue;
    if(response&HDA_EAPD_BTL_ENABLE_EAPD)continue;
    /* BTL and left/right swap stay as the firmware set them. */
    response=(response&(HDA_EAPD_BTL_ENABLE_BTL|HDA_EAPD_BTL_ENABLE_L_R_SWAP))|HDA_EAPD_BTL_ENABLE_EAPD;
    HakCodecTrace("HAK HDA r406 EAPD on at node",widget->NodeId);
    io->SendCommand(io,widget->NodeId,HDA_CODEC_VERB(HDA_VERB_SET_EAPD_BTL_ENABLE,(UINT8)response),&response);
  }
}

/* PCI subsystem device ID of the controller this codec hangs on. */
static BOOLEAN HakControllerSubsystem(HDA_CODEC_DEV *dev,UINT16 *subsystem) {
  EFI_DEVICE_PATH_PROTOCOL *path=dev->DevicePath;EFI_HANDLE handle=NULL;EFI_PCI_IO_PROTOCOL *pci=NULL;
  if(path==NULL || EFI_ERROR(gBS->LocateDevicePath(&gEfiPciIoProtocolGuid,&path,&handle)))return FALSE;
  if(EFI_ERROR(gBS->HandleProtocol(handle,&gEfiPciIoProtocolGuid,(VOID **)&pci)) || pci==NULL)return FALSE;
  return !EFI_ERROR(pci->Pci.Read(pci,EfiPciIoWidthUint16,PCI_SUBSYSTEM_ID_OFFSET,1,subsystem));
}

/* Linux patch_realtek.c alc_subsystem_id(): bits 3-5 of the Realtek
 * "assembly ID" name the external amplifier control: 1 = GPIO0, 3 = GPIO1,
 * 7 = GPIO2, anything else = EAPD only. Returns the GPIO mask or 0. */
static UINT8 HakRealtekAmplifierGpio(HDA_CODEC_DEV *dev,HDA_FUNC_GROUP *group) {
  EFI_HDA_IO_PROTOCOL *io=dev->HdaIo;UINT32 response=0,ass,sum,i;UINT16 pciSubsystem=0;UINT8 node;UINTN w;
  BOOLEAN valid=FALSE;
  if(!EFI_ERROR(io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_GET_IMPLEMENTATION_ID,0),&response))) {
    ass=response&0xffff;
    if(HakControllerSubsystem(dev,&pciSubsystem) && ass!=pciSubsystem && (ass&1))valid=TRUE;
  } else ass=0;
  if(!valid) {
    /* No valid SSID: the pin default of the special node carries it. */
    node=(dev->VendorId==0x10ec0260)?0x17:0x1d;
    for(w=0;w<group->WidgetsCount;w++)if(group->Widgets[w].NodeId==node)break;
    if(w==group->WidgetsCount || group->Widgets[w].Type!=HDA_WIDGET_TYPE_PIN_COMPLEX)return 0;
    ass=group->Widgets[w].DefaultConfiguration;
    if(!(ass&1) || (ass>>30)!=1)return 0;
    for(sum=0,i=1;i<16;i++)if((ass>>i)&1)sum++;
    if(((ass>>16)&0xf)!=sum)return 0;
  }
  HakCodecTrace("HAK HDA r406 Realtek assembly ID",ass);
  switch((ass&0x38)>>3) {
    case 1: return 0x01;
    case 3: return 0x02;
    case 7: return 0x04;
    default: return 0;
  }
}

static VOID HakRealtekGpio(HDA_CODEC_DEV *dev,HDA_FUNC_GROUP *group) {
  EFI_HDA_IO_PROTOCOL *io=dev->HdaIo;UINT32 enable=0,direction=0,data=0,response=0;UINT8 mask,count;
  if(GET_CODEC_VENDOR_ID(dev->VendorId)!=VEN_REALTEK_ID)return;
  count=HDA_PARAMETER_GPIO_COUNT_NUM_GPIOS(group->GpioCapabilities);
  mask=HakRealtekAmplifierGpio(dev,group);
  if(mask==0 || count>8 || mask>=(1U<<count))return;
  if(EFI_ERROR(io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_GET_GPIO_ENABLE_MASK,0),&enable))
     || EFI_ERROR(io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_GET_GPIO_DIRECTION,0),&direction))
     || EFI_ERROR(io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_GET_GPIO_DATA,0),&data)))return;
  HakCodecTrace("HAK HDA r406 Realtek amplifier GPIO mask",mask|((UINT64)(enable&0xff)<<8)|((UINT64)(direction&0xff)<<16)|((UINT64)(data&0xff)<<24));
  if((enable&mask) && (direction&mask) && (data&mask))return;   /* firmware did it */
  /* Other GPIOs keep the state the firmware gave them (mute LEDs etc.). */
  io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_SET_GPIO_ENABLE_MASK,(UINT8)(enable|mask)),&response);
  io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_SET_GPIO_DIRECTION,(UINT8)(direction|mask)),&response);
  gBS->Stall(1000);
  io->SendCommand(io,group->NodeId,HDA_CODEC_VERB(HDA_VERB_SET_GPIO_DATA,(UINT8)(data|mask)),&response);
}

/* r408: the amplifier GPIO the assembly ID names, 0 when it names none (HakHdaQuirks.c
 * then applies the default amplifier coefficient, as Linux ALC_INIT_DEFAULT does). */
UINT8
HakHdaCodecRealtekSkuGpio (
  IN HDA_CODEC_DEV  *HdaCodecDev
  )
{
  if(HdaCodecDev==NULL || HdaCodecDev->HdaIo==NULL || HdaCodecDev->AudioFuncGroup==NULL
     || HdaCodecDev->AudioFuncGroup->Widgets==NULL || GET_CODEC_VENDOR_ID(HdaCodecDev->VendorId)!=VEN_REALTEK_ID)return 0;
  return HakRealtekAmplifierGpio(HdaCodecDev,HdaCodecDev->AudioFuncGroup);
}

VOID
HakHdaCodecPrepareOutputs (
  IN HDA_CODEC_DEV  *HdaCodecDev
  )
{
  HDA_FUNC_GROUP *group;
  if(HdaCodecDev==NULL || HdaCodecDev->HdaIo==NULL)return;
  group=HdaCodecDev->AudioFuncGroup;
  if(group==NULL || group->Widgets==NULL)return;
  HakPowerUp(HdaCodecDev,group);
  /* r408: AD1986A has an inverted EAPD (bit clear = amplifier on); HakHdaQuirkFinish handles it. */
  if(!HakHdaQuirkEapdInverted(HdaCodecDev))HakEapdOn(HdaCodecDev,group);
  /* r408: Linux alc_fixup_sku_ignore: on some machines the assembly ID is not to be trusted. */
  if(!HakHdaQuirkSkuIgnored(HdaCodecDev))HakRealtekGpio(HdaCodecDev,group);
}
