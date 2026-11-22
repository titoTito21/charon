/* HAK HDA compatibility layer for modern machines (Charon r406).
 * SPDX-License-Identifier: BSD-2-Clause
 * Added to the pinned OpenCore AudioDxe; existing protocols are unchanged.
 * Behaviour follows Linux v6.12 (sound/pci/hda/hda_intel.c, sound/hda/hdac_controller.c,
 * sound/hda/intel-dsp-config.c, sound/pci/hda/patch_realtek.c, patch_conexant.c). */
#ifndef HAK_HDA_COMPAT_H
#define HAK_HDA_COMPAT_H

#include <Uefi.h>
#include <Protocol/PciIo.h>
#include <Protocol/HdaIo.h>
#include <IndustryStandard/Pci.h>

struct _HDA_CONTROLLER_DEV;
struct _HDA_CODEC_DEV;
struct _HDA_WIDGET_DEV;

/* Driver binding Supported(): class 04/03 as before; additionally Intel
 * 04/01 and 04/80 functions whose BAR0 is a memory BAR and (when memory
 * decoding is already on) holds an HD Audio register block. */
EFI_STATUS
HakHdaControllerSupported (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN CONST UINT8          *ClassCode   /* 3 bytes at PCI offset 9: prog-if, subclass, class */
  );

/* Driver binding Start(), before anything is written to the device: for a
 * function that is not class 04/03, turn memory decoding on, check the
 * register block and put the attributes back on refusal. Class 04/03 is
 * accepted untouched. */
EFI_STATUS
HakHdaControllerStartCheck (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  );

/* After the PCI device is enabled: snoop/coherence bits of ATI/AMD and
 * NVIDIA controllers as Linux azx_init_pci() sets them. */
VOID
HakHdaControllerPciSetup (
  IN struct _HDA_CONTROLLER_DEV  *HdaControllerDev
  );

/* Around the controller reset: Intel Skylake and newer need the
 * Miscellaneous Backbone Dynamic Clock Gating off while the link resets,
 * or codecs are not detected (Linux hda_intel_init_chip()). Returns what
 * Done needs to restore. */
UINT32
HakHdaControllerResetPrepare (
  IN struct _HDA_CONTROLLER_DEV  *HdaControllerDev
  );

VOID
HakHdaControllerResetDone (
  IN struct _HDA_CONTROLLER_DEV  *HdaControllerDev,
  IN UINT32                      Gate
  );

/* After the codec prescan: a controller that was accepted outside class
 * 04/03 must have at least one codec that answers; otherwise the machine
 * is DSP/SoundWire only and the driver lets go of it. */
EFI_STATUS
HakHdaControllerCodecsAnswer (
  IN struct _HDA_CONTROLLER_DEV  *HdaControllerDev
  );

/* Wait until a node reports power state D0 (at most 100 ms). */
VOID
HakHdaCodecWaitPower (
  IN EFI_HDA_IO_PROTOCOL  *HdaIo,
  IN UINT8                NodeId
  );

/* TRUE for a headphone jack whose pin has a headphone amplifier. */
BOOLEAN
HakHdaCodecPinIsHeadphone (
  IN struct _HDA_WIDGET_DEV  *HdaWidget
  );

/* Before the output paths are set up: function group and widgets in D0,
 * EAPD on for every pin that has it, Realtek amplifier GPIO. Never fails
 * playback setup. */
VOID
HakHdaCodecPrepareOutputs (
  IN struct _HDA_CODEC_DEV  *HdaCodecDev
  );

/* r407: Charon never shares a controller with another driver. With the firmware's own
 * HD Audio driver still bound (VMware), the shared start left this driver half working
 * once Charon disconnected the other one: the codec driver could not start and the
 * stream had no output. Without sharing, the controller is started only after the
 * other driver is gone, exclusively. */
BOOLEAN
HakHdaControllerMayShare (
  VOID
  );

#endif
