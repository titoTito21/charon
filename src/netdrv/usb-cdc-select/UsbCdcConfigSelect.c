/** @file
  UsbCdcConfigSelect - makes USB Ethernet adapters whose DEFAULT configuration is maker-specific usable through
  their standard CDC configuration (Realtek RTL8152/8153/8156/8157, Aquantia AQC111U/AQC112U and similar).

  Firmware selects the first configuration of a USB device. For these adapters that configuration carries only a
  vendor-specific interface (class 0xff), which needs the maker's driver; a second or third configuration is a
  standard CDC-ECM (02/06) or CDC-NCM (02/0d) function that class drivers can use. EFI_USB_IO_PROTOCOL has no
  "set configuration" call, but edk2's USB bus driver (MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBus.c,
  UsbIoControlTransfer) watches control transfers: after a successful standard SET_CONFIGURATION request it removes
  the interface handles of the old configuration and creates and connects those of the new one.

  What this driver does:
    Supported()  the handle carries a vendor-specific interface that NO driver manages, the device has 2..8
                 configurations and is not a hub.
    Start()      only notes the handle and arms a 20 ms timer; it returns an error, so the driver never manages
                 anything. (The switch destroys the handle Start() was called for; doing it inside Start() would
                 pull the handle away under the firmware's ConnectController(). From a timer notification at
                 TPL_CALLBACK it happens in the same context in which the USB bus driver itself removes and adds
                 devices on hot-plug.)
    timer        for every noted handle that still exists and is still unmanaged: reads all configuration
                 descriptors with GET_DESCRIPTOR, and only if the ACTIVE configuration consists of vendor-specific
                 interfaces alone and another configuration has a CDC-ECM or CDC-NCM interface, sends
                 SET_CONFIGURATION for that one (ECM preferred: one Ethernet frame per transfer, nothing to
                 negotiate; NCM when there is no ECM; a configuration whose bConfigurationValue equals its
                 descriptor index + 1 goes first, because the edk2 class drivers read descriptor "value - 1").
                 Afterwards it does not touch the old interface again. When
                 the firmware's USB stack keeps the old handle (it did not follow the change) the original
                 configuration is restored.
  The driver binding version is 1, so every real driver for the vendor-specific interface is asked first; hubs,
  HID, storage, audio and devices with a single configuration are never touched; one device type is switched at most
  three times per boot. The result is written to the volatile variable CharonUsbCdcSelect (GUID below) as text.

  Built with -DCHARON_USBCFG_TEST the image additionally accepts QEMU's usb-net device (0525:a4a2: RNDIS first,
  CDC-ECM second) and prints its log on the console; that variant is for the emulator test only and is not shipped.

  NOT RUN ON ANY PHYSICAL ADAPTER. Run in QEMU/OVMF with the emulated usb-net device only.

  Copyright (c) 2026, Charon project. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Protocol/UsbIo.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/LoadedImage.h>
#include <IndustryStandard/Usb.h>

EFI_GUID  gEfiUsbIoProtocolGuid          = EFI_USB_IO_PROTOCOL_GUID;
EFI_GUID  gEfiDriverBindingProtocolGuid  = EFI_DRIVER_BINDING_PROTOCOL_GUID;
EFI_GUID  gEfiComponentName2ProtocolGuid = EFI_COMPONENT_NAME2_PROTOCOL_GUID;
EFI_GUID  gEfiLoadedImageProtocolGuid    = EFI_LOADED_IMAGE_PROTOCOL_GUID;

#define CHARON_USB_CDC_SELECT_GUID \
  { 0x6b1f4d2a, 0x3c5e, 0x4f7b, { 0x9a, 0x1d, 0x52, 0xc8, 0x0e, 0x7f, 0x31, 0xa6 } }
STATIC EFI_GUID  mLogGuid = CHARON_USB_CDC_SELECT_GUID;

#define MAX_RECORDS    16
#define MAX_SWITCHED   16
#define MAX_SWITCHES   3
#define CONFIG_BUFFER  2048
#define LOG_SIZE       1536
#define TIMER_DELAY    200000             // 20 ms in 100 ns units
#define USB_TIMEOUT    3000               // ms

#define USB_CLASS_COMM_         0x02
#define USB_CLASS_HUB_          0x09
#define USB_CLASS_VENDOR_SPEC_  0xff
#define CDC_SUBCLASS_ECM        0x06
#define CDC_SUBCLASS_NCM        0x0d

typedef enum {
  RecordFree = 0,
  RecordPending,   // noted by Start(), waits for the timer
  RecordClosed     // looked at; nothing (more) to do for this handle
} RECORD_STATE;

typedef struct {
  EFI_HANDLE    Handle;
  UINT16        Vid;
  UINT16        Pid;
  UINT8         State;
} RECORD;

typedef struct {
  UINT16    Vid;
  UINT16    Pid;
  UINT8     Count;
} SWITCHED;

STATIC EFI_BOOT_SERVICES     *mBS;
STATIC EFI_RUNTIME_SERVICES  *mRT;
STATIC EFI_HANDLE            mImage;
STATIC EFI_EVENT             mTimer;
STATIC RECORD                mRecords[MAX_RECORDS];
STATIC SWITCHED              mSwitched[MAX_SWITCHED];
STATIC UINT8                 mConfig[CONFIG_BUFFER];
STATIC CHAR8                 mLog[LOG_SIZE];
STATIC UINTN                 mLogLength;
STATIC BOOLEAN               mBusy;
#ifdef CHARON_USBCFG_TEST
STATIC EFI_SYSTEM_TABLE  *mST;
STATIC UINTN             mPrinted;
#endif

// The compiler may emit calls to these for structure copies and initialisers.
void *
memset (
  void             *Destination,
  int              Value,
  __SIZE_TYPE__    Count
  )
{
  volatile unsigned char  *Out = (volatile unsigned char *)Destination;

  while (Count-- != 0) {
    *Out++ = (unsigned char)Value;
  }

  return Destination;
}

void *
memcpy (
  void             *Destination,
  const void       *Source,
  __SIZE_TYPE__    Count
  )
{
  volatile unsigned char  *Out = (volatile unsigned char *)Destination;
  const unsigned char     *In  = (const unsigned char *)Source;

  while (Count-- != 0) {
    *Out++ = *In++;
  }

  return Destination;
}

// ---- log -----------------------------------------------------------------------------------------------------
STATIC
VOID
Log (
  IN CONST CHAR8  *Text
  )
{
  while ((*Text != 0) && (mLogLength + 1 < LOG_SIZE)) {
    mLog[mLogLength++] = *Text++;
  }
}

STATIC
VOID
LogHex (
  IN UINT32  Value,
  IN UINTN   Digits
  )
{
  CHAR8  Text[9];
  UINTN  Index;

  for (Index = 0; Index < Digits; Index++) {
    Text[Index] = "0123456789abcdef"[(Value >> ((Digits - 1 - Index) * 4)) & 0xf];
  }

  Text[Digits] = 0;
  Log (Text);
}

STATIC
VOID
LogDevice (
  IN UINT16  Vid,
  IN UINT16  Pid
  )
{
  LogHex (Vid, 4);
  Log (":");
  LogHex (Pid, 4);
}

STATIC
VOID
FlushLog (
  VOID
  )
{
  if (mLogLength == 0) {
    return;
  }

  mRT->SetVariable (L"CharonUsbCdcSelect", &mLogGuid, EFI_VARIABLE_BOOTSERVICE_ACCESS, mLogLength, mLog);
 #ifdef CHARON_USBCFG_TEST
  if ((mST != NULL) && (mST->ConOut != NULL)) {
    CHAR16  Wide[2];

    Wide[1] = 0;
    for ( ; mPrinted < mLogLength; mPrinted++) {
      if (mLog[mPrinted] == '\n') {
        mST->ConOut->OutputString (mST->ConOut, L"\r\n");
      } else {
        Wide[0] = (CHAR16)(UINT8)mLog[mPrinted];
        mST->ConOut->OutputString (mST->ConOut, Wide);
      }
    }
  }

 #endif
}

// ---- helpers -------------------------------------------------------------------------------------------------
STATIC
RECORD *
FindRecord (
  IN EFI_HANDLE  Handle
  )
{
  UINTN  Index;

  for (Index = 0; Index < MAX_RECORDS; Index++) {
    if ((mRecords[Index].State != RecordFree) && (mRecords[Index].Handle == Handle)) {
      return &mRecords[Index];
    }
  }

  return NULL;
}

STATIC
RECORD *
AddRecord (
  IN EFI_HANDLE  Handle,
  IN UINT16      Vid,
  IN UINT16      Pid
  )
{
  UINTN  Index;

  for (Index = 0; Index < MAX_RECORDS; Index++) {
    if (mRecords[Index].State == RecordFree) {
      mRecords[Index].Handle = Handle;
      mRecords[Index].Vid    = Vid;
      mRecords[Index].Pid    = Pid;
      mRecords[Index].State  = RecordPending;
      return &mRecords[Index];
    }
  }

  return NULL;
}

/** TRUE when the handle still exists and carries USB I/O (never dereferences a handle that may be gone). **/
STATIC
BOOLEAN
HandleAlive (
  IN EFI_HANDLE  Handle
  )
{
  EFI_HANDLE  *Handles;
  UINTN       Count;
  UINTN       Index;
  BOOLEAN     Alive;

  Handles = NULL;
  Count   = 0;
  Alive   = FALSE;
  if (EFI_ERROR (mBS->LocateHandleBuffer (ByProtocol, &gEfiUsbIoProtocolGuid, NULL, &Count, &Handles)) || (Handles == NULL)) {
    return FALSE;
  }

  for (Index = 0; Index < Count; Index++) {
    if (Handles[Index] == Handle) {
      Alive = TRUE;
      break;
    }
  }

  mBS->FreePool (Handles);
  return Alive;
}

/** TRUE when some driver has the interface's USB I/O open BY_DRIVER. **/
STATIC
BOOLEAN
HandleManaged (
  IN EFI_HANDLE  Handle
  )
{
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY  *Entries;
  UINTN                                Count;
  UINTN                                Index;
  BOOLEAN                              Managed;

  Entries = NULL;
  Count   = 0;
  Managed = FALSE;
  if (EFI_ERROR (mBS->OpenProtocolInformation (Handle, &gEfiUsbIoProtocolGuid, &Entries, &Count)) || (Entries == NULL)) {
    return FALSE;
  }

  for (Index = 0; Index < Count; Index++) {
    if ((Entries[Index].Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) != 0) {
      Managed = TRUE;
      break;
    }
  }

  mBS->FreePool (Entries);
  return Managed;
}

STATIC
BOOLEAN
IsTestDevice (
  IN UINT16  Vid,
  IN UINT16  Pid
  )
{
 #ifdef CHARON_USBCFG_TEST
  return (BOOLEAN)((Vid == 0x0525) && (Pid == 0xa4a2));   // QEMU usb-net
 #else
  (VOID)Vid;
  (VOID)Pid;
  return FALSE;
 #endif
}

/** The cheap test (descriptors the bus driver has cached; no transfer): is this an interface worth looking at? **/
STATIC
BOOLEAN
Qualifies (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo,
  OUT UINT16               *Vid,
  OUT UINT16               *Pid
  )
{
  EFI_USB_INTERFACE_DESCRIPTOR  Interface;
  EFI_USB_DEVICE_DESCRIPTOR     Device;

  if (EFI_ERROR (UsbIo->UsbGetInterfaceDescriptor (UsbIo, &Interface)) ||
      EFI_ERROR (UsbIo->UsbGetDeviceDescriptor (UsbIo, &Device)))
  {
    return FALSE;
  }

  *Vid = Device.IdVendor;
  *Pid = Device.IdProduct;
  if ((Device.NumConfigurations < 2) || (Device.NumConfigurations > 8) || (Device.DeviceClass == USB_CLASS_HUB_)) {
    return FALSE;
  }

  if (Interface.InterfaceClass == USB_CLASS_VENDOR_SPEC_) {
    return TRUE;
  }

  // Emulator test only: the RNDIS control interface of QEMU's usb-net (02/02/ff).
  return (BOOLEAN)(IsTestDevice (Device.IdVendor, Device.IdProduct) && (Interface.InterfaceClass == USB_CLASS_COMM_) &&
                   (Interface.InterfaceSubClass == 0x02) && (Interface.InterfaceProtocol == 0xff));
}

/** Reads configuration descriptor number Index completely into mConfig. **/
STATIC
EFI_STATUS
ReadConfiguration (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo,
  IN  UINT8                Index,
  OUT UINTN                *Length
  )
{
  EFI_USB_DEVICE_REQUEST  Request;
  EFI_STATUS              Status;
  UINT32                  UsbStatus;
  UINTN                   Total;

  Request.RequestType = 0x80;                       // device to host, standard, device
  Request.Request     = USB_REQ_GET_DESCRIPTOR;
  Request.Value       = (UINT16)((USB_DESC_TYPE_CONFIG << 8) | Index);
  Request.Index       = 0;
  Request.Length      = 9;
  UsbStatus           = 0;
  Status              = UsbIo->UsbControlTransfer (UsbIo, &Request, EfiUsbDataIn, USB_TIMEOUT, mConfig, 9, &UsbStatus);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Total = (UINTN)mConfig[2] | ((UINTN)mConfig[3] << 8);
  if ((mConfig[1] != USB_DESC_TYPE_CONFIG) || (Total < 9) || (Total > CONFIG_BUFFER)) {
    return EFI_DEVICE_ERROR;
  }

  Request.Length = (UINT16)Total;
  UsbStatus      = 0;
  Status         = UsbIo->UsbControlTransfer (UsbIo, &Request, EfiUsbDataIn, USB_TIMEOUT, mConfig, Total, &UsbStatus);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Length = Total;
  return EFI_SUCCESS;
}

/** Walks the interface descriptors of the configuration in mConfig. **/
STATIC
VOID
Classify (
  IN  UINTN    Length,
  OUT BOOLEAN  *AllVendor,
  OUT BOOLEAN  *HasEcm,
  OUT BOOLEAN  *HasNcm
  )
{
  UINTN  Offset;
  UINTN  Interfaces;
  UINTN  Vendor;
  UINT8  Size;

  *HasEcm    = FALSE;
  *HasNcm    = FALSE;
  Interfaces = 0;
  Vendor     = 0;
  for (Offset = 0; Offset + 2 <= Length; Offset += Size) {
    Size = mConfig[Offset];
    if (Size < 2) {
      break;
    }

    if ((mConfig[Offset + 1] == USB_DESC_TYPE_INTERFACE) && (Size >= 9) && (Offset + 9 <= Length)) {
      Interfaces++;
      if (mConfig[Offset + 5] == USB_CLASS_VENDOR_SPEC_) {
        Vendor++;
      } else if (mConfig[Offset + 5] == USB_CLASS_COMM_) {
        if (mConfig[Offset + 6] == CDC_SUBCLASS_ECM) {
          *HasEcm = TRUE;
        } else if (mConfig[Offset + 6] == CDC_SUBCLASS_NCM) {
          *HasNcm = TRUE;
        }
      }
    }
  }

  *AllVendor = (BOOLEAN)((Interfaces != 0) && (Vendor == Interfaces));
}

STATIC
EFI_STATUS
SetConfiguration (
  IN EFI_USB_IO_PROTOCOL  *UsbIo,
  IN UINT8                Value
  )
{
  EFI_USB_DEVICE_REQUEST  Request;
  UINT32                  UsbStatus;

  Request.RequestType = 0x00;                       // host to device, standard, device
  Request.Request     = USB_REQ_SET_CONFIG;
  Request.Value       = Value;
  Request.Index       = 0;
  Request.Length      = 0;
  UsbStatus           = 0;
  return UsbIo->UsbControlTransfer (UsbIo, &Request, EfiUsbNoData, USB_TIMEOUT, NULL, 0, &UsbStatus);
}

STATIC
BOOLEAN
MaySwitch (
  IN UINT16  Vid,
  IN UINT16  Pid
  )
{
  UINTN  Index;

  for (Index = 0; Index < MAX_SWITCHED; Index++) {
    if ((mSwitched[Index].Count != 0) && (mSwitched[Index].Vid == Vid) && (mSwitched[Index].Pid == Pid)) {
      if (mSwitched[Index].Count >= MAX_SWITCHES) {
        return FALSE;
      }

      mSwitched[Index].Count++;
      return TRUE;
    }
  }

  for (Index = 0; Index < MAX_SWITCHED; Index++) {
    if (mSwitched[Index].Count == 0) {
      mSwitched[Index].Vid   = Vid;
      mSwitched[Index].Pid   = Pid;
      mSwitched[Index].Count = 1;
      return TRUE;
    }
  }

  return FALSE;
}

// ---- the switch (timer notification, TPL_CALLBACK) -------------------------------------------------------------
STATIC
VOID
Process (
  IN RECORD  *Record
  )
{
  EFI_USB_IO_PROTOCOL        *UsbIo;
  EFI_USB_DEVICE_DESCRIPTOR  Device;
  EFI_USB_CONFIG_DESCRIPTOR  Active;
  EFI_HANDLE                 Handle;
  EFI_STATUS                 Status;
  UINTN                      Length;
  UINT8                      Index;
  UINT8                      Ecm;
  UINT8                      Ncm;
  UINT8                      EcmOrdered;
  UINT8                      NcmOrdered;
  UINT8                      Target;
  BOOLEAN                    IsEcm;
  BOOLEAN                    Ordered;
  BOOLEAN                    ActiveSeen;
  BOOLEAN                    ActiveVendorOnly;
  BOOLEAN                    AllVendor;
  BOOLEAN                    HasEcm;
  BOOLEAN                    HasNcm;

  Handle = Record->Handle;
  if (!HandleAlive (Handle)) {
    Record->State = RecordFree;                     // unplugged meanwhile
    return;
  }

  Record->State = RecordClosed;                     // whatever happens below, this handle is not looked at again
  UsbIo         = NULL;
  if (HandleManaged (Handle) ||
      EFI_ERROR (mBS->OpenProtocol (Handle, &gEfiUsbIoProtocolGuid, (VOID **)&UsbIo, mImage, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) ||
      (UsbIo == NULL))
  {
    return;                                         // a real driver took the interface: leave it alone
  }

  if (EFI_ERROR (UsbIo->UsbGetDeviceDescriptor (UsbIo, &Device)) || (Device.IdVendor != Record->Vid) ||
      (Device.IdProduct != Record->Pid) || EFI_ERROR (UsbIo->UsbGetConfigDescriptor (UsbIo, &Active)))
  {
    return;
  }

  Ecm              = 0;
  Ncm              = 0;
  EcmOrdered       = 0;
  NcmOrdered       = 0;
  ActiveSeen       = FALSE;
  ActiveVendorOnly = FALSE;
  for (Index = 0; Index < Device.NumConfigurations; Index++) {
    Status = ReadConfiguration (UsbIo, Index, &Length);
    if (EFI_ERROR (Status)) {
      LogDevice (Record->Vid, Record->Pid);
      Log (": configuration descriptor ");
      LogHex (Index, 1);
      Log (" could not be read; left alone\n");
      return;
    }

    Classify (Length, &AllVendor, &HasEcm, &HasNcm);
    if (mConfig[5] == Active.ConfigurationValue) {
      ActiveSeen       = TRUE;
      ActiveVendorOnly = AllVendor;
    } else if (mConfig[5] != 0) {
      if (HasEcm && (Ecm == 0)) {
        Ecm = mConfig[5];
      }

      if (HasNcm && (Ncm == 0)) {
        Ncm = mConfig[5];
      }

      //
      // "Ordered": bConfigurationValue == descriptor index + 1. The edk2 USB network class drivers
      // (MdeModulePkg/Bus/Usb/UsbNetwork, LoadAllDescriptor) read descriptor number "value - 1", so only
      // such a configuration works with them. Preferred when there is a choice.
      //
      if (mConfig[5] == Index + 1) {
        if (HasEcm && (EcmOrdered == 0)) {
          EcmOrdered = mConfig[5];
        }

        if (HasNcm && (NcmOrdered == 0)) {
          NcmOrdered = mConfig[5];
        }
      }
    }
  }

  LogDevice (Record->Vid, Record->Pid);
  if (!ActiveSeen || !(ActiveVendorOnly || IsTestDevice (Record->Vid, Record->Pid))) {
    Log (": the active configuration is not vendor-specific only; left alone\n");
    return;
  }

  if (EcmOrdered != 0) {
    Target = EcmOrdered;
  } else if (NcmOrdered != 0) {
    Target = NcmOrdered;
  } else {
    Target = (Ecm != 0) ? Ecm : Ncm;
  }

  if (Target == 0) {
    Log (": no CDC-ECM or CDC-NCM configuration; left alone\n");
    return;
  }

  IsEcm   = (BOOLEAN)((Target == EcmOrdered) || ((EcmOrdered == 0) && (NcmOrdered == 0) && (Target == Ecm)));
  Ordered = (BOOLEAN)((Target == EcmOrdered) || (Target == NcmOrdered));

  if (!MaySwitch (Record->Vid, Record->Pid)) {
    Log (": switched too often already; left alone\n");
    return;
  }

  Log (": configuration ");
  LogHex (Active.ConfigurationValue, 2);
  Log (" -> ");
  LogHex (Target, 2);
  Log (IsEcm ? " (CDC-ECM" : " (CDC-NCM");
  Log (Ordered ? ")" : ", descriptor index is not value-1)");

  //
  // The USB bus driver removes this interface handle (and UsbIo with it) inside the call when the device accepted
  // the request. Nothing below uses UsbIo or the handle without asking the handle database first.
  //
  Status = SetConfiguration (UsbIo, Target);
  UsbIo  = NULL;
  if (!HandleAlive (Handle)) {
    Record->State = RecordFree;                     // the handle is gone: its address may be reused
    Log (EFI_ERROR (Status) ? ": interfaces replaced, request status reported an error\n" : ": switched, new interfaces created\n");
    return;
  }

  if (EFI_ERROR (Status)) {
    Log (": the device refused the request; unchanged\n");
    return;
  }

  //
  // The device changed its configuration but the firmware's USB stack kept the old interface: restore the
  // configuration the firmware believes in.
  //
  Log (": firmware USB stack did not follow the change");
  if (!EFI_ERROR (mBS->OpenProtocol (Handle, &gEfiUsbIoProtocolGuid, (VOID **)&UsbIo, mImage, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) &&
      (UsbIo != NULL))
  {
    Status = SetConfiguration (UsbIo, Active.ConfigurationValue);
    Log (EFI_ERROR (Status) ? "; restoring the old configuration FAILED\n" : "; old configuration restored\n");
  } else {
    Log ("\n");
  }
}

STATIC
VOID
EFIAPI
OnTimer (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN  Index;

  (VOID)Event;
  (VOID)Context;
  if (mBusy) {
    return;
  }

  mBusy = TRUE;
  for (Index = 0; Index < MAX_RECORDS; Index++) {
    if (mRecords[Index].State == RecordPending) {
      Process (&mRecords[Index]);
    } else if ((mRecords[Index].State == RecordClosed) && !HandleAlive (mRecords[Index].Handle)) {
      mRecords[Index].State = RecordFree;           // forget handles that no longer exist
    }
  }

  FlushLog ();
  mBusy = FALSE;
}

// ---- driver binding --------------------------------------------------------------------------------------------
STATIC
EFI_STATUS
EFIAPI
Supported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  EFI_USB_IO_PROTOCOL  *UsbIo;
  EFI_STATUS           Status;
  RECORD               *Record;
  UINT16               Vid;
  UINT16               Pid;

  (VOID)RemainingDevicePath;
  UsbIo  = NULL;
  Status = mBS->OpenProtocol (Controller, &gEfiUsbIoProtocolGuid, (VOID **)&UsbIo, This->DriverBindingHandle, Controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR (Status)) {
    return Status;                                  // also: another driver manages the interface
  }

  Status = EFI_UNSUPPORTED;
  if ((UsbIo != NULL) && Qualifies (UsbIo, &Vid, &Pid)) {
    Record = FindRecord (Controller);
    if ((Record != NULL) && (Record->State == RecordClosed) && ((Record->Vid != Vid) || (Record->Pid != Pid))) {
      Record->State = RecordFree;                   // an old handle's address reused by another device
      Record        = NULL;
    }

    if (Record == NULL) {
      Status = EFI_SUCCESS;
    }
  }

  mBS->CloseProtocol (Controller, &gEfiUsbIoProtocolGuid, This->DriverBindingHandle, Controller);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
Start (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  EFI_USB_IO_PROTOCOL  *UsbIo;
  EFI_STATUS           Status;
  UINT16               Vid;
  UINT16               Pid;
  BOOLEAN              Noted;

  (VOID)RemainingDevicePath;
  UsbIo  = NULL;
  Status = mBS->OpenProtocol (Controller, &gEfiUsbIoProtocolGuid, (VOID **)&UsbIo, This->DriverBindingHandle, Controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Noted = FALSE;
  if ((UsbIo != NULL) && Qualifies (UsbIo, &Vid, &Pid) && (FindRecord (Controller) == NULL)) {
    Noted = (BOOLEAN)(AddRecord (Controller, Vid, Pid) != NULL);
  }

  mBS->CloseProtocol (Controller, &gEfiUsbIoProtocolGuid, This->DriverBindingHandle, Controller);
  if (Noted) {
    mBS->SetTimer (mTimer, TimerRelative, TIMER_DELAY);
  }

  //
  // Deliberately an error: this driver manages nothing. The work is done by the timer notification.
  //
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
Stop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer OPTIONAL
  )
{
  (VOID)This;
  (VOID)Controller;
  (VOID)NumberOfChildren;
  (VOID)ChildHandleBuffer;
  return EFI_SUCCESS;
}

STATIC EFI_DRIVER_BINDING_PROTOCOL  mDriverBinding = {
  Supported,
  Start,
  Stop,
  0x01,   // lowest priority: every real driver for the interface is asked before this one
  NULL,
  NULL
};

// ---- component name --------------------------------------------------------------------------------------------
STATIC
EFI_STATUS
EFIAPI
GetDriverName (
  IN  EFI_COMPONENT_NAME2_PROTOCOL  *This,
  IN  CHAR8                         *Language,
  OUT CHAR16                        **DriverName
  )
{
  (VOID)This;
  if ((Language == NULL) || (DriverName == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Language[0] != 'e') || (Language[1] != 'n')) {
    return EFI_UNSUPPORTED;
  }

 #ifdef CHARON_USBCFG_TEST
  *DriverName = L"Charon USB CDC configuration selector (TEST variant)";
 #else
  *DriverName = L"Charon USB CDC configuration selector";
 #endif
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GetControllerName (
  IN  EFI_COMPONENT_NAME2_PROTOCOL  *This,
  IN  EFI_HANDLE                    ControllerHandle,
  IN  EFI_HANDLE                    ChildHandle OPTIONAL,
  IN  CHAR8                         *Language,
  OUT CHAR16                        **ControllerName
  )
{
  (VOID)This;
  (VOID)ControllerHandle;
  (VOID)ChildHandle;
  (VOID)Language;
  (VOID)ControllerName;
  return EFI_UNSUPPORTED;
}

STATIC EFI_COMPONENT_NAME2_PROTOCOL  mComponentName2 = {
  GetDriverName,
  GetControllerName,
  "en"
};

// ---- image -----------------------------------------------------------------------------------------------------
STATIC
EFI_STATUS
EFIAPI
Unload (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS  Status;

  Status = mBS->UninstallMultipleProtocolInterfaces (
                  ImageHandle,
                  &gEfiDriverBindingProtocolGuid,
                  &mDriverBinding,
                  &gEfiComponentName2ProtocolGuid,
                  &mComponentName2,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mBS->SetTimer (mTimer, TimerCancel, 0);
  mBS->CloseEvent (mTimer);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UsbCdcConfigSelectEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *LoadedImage;
  EFI_STATUS                 Status;

  mBS    = SystemTable->BootServices;
  mRT    = SystemTable->RuntimeServices;
  mImage = ImageHandle;
 #ifdef CHARON_USBCFG_TEST
  mST = SystemTable;
 #endif
  Status = mBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, OnTimer, NULL, &mTimer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mDriverBinding.ImageHandle         = ImageHandle;
  mDriverBinding.DriverBindingHandle = ImageHandle;
  Status                             = mBS->InstallMultipleProtocolInterfaces (
                                              &ImageHandle,
                                              &gEfiDriverBindingProtocolGuid,
                                              &mDriverBinding,
                                              &gEfiComponentName2ProtocolGuid,
                                              &mComponentName2,
                                              NULL
                                              );
  if (EFI_ERROR (Status)) {
    mBS->CloseEvent (mTimer);
    return Status;
  }

  LoadedImage = NULL;
  if (!EFI_ERROR (mBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage)) && (LoadedImage != NULL)) {
    LoadedImage->Unload = Unload;
  }

  return EFI_SUCCESS;
}
