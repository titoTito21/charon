/*
 * Charon Wi-Fi driver for MediaTek MT7921/MT7922 (OpenBSD mwx + net80211):
 * the UEFI side.  THIS FILE IS A COPY of src/netdrv/rge-uefi/rge_uefi.c (the
 * UEFI side of the wired rge driver: driver binding, PCI access, DMA memory,
 * Simple Network Protocol, log variable) made by tools/build-charon-netdrv-
 * wifi.py's recipe on 2026-11-22, with these changes: other names, the radio
 * is polled while it scans or joins even when the Simple Network Protocol is
 * not initialised, and wifi_face.inc (EFI_WIRELESS_MAC_CONNECTION_II,
 * EFI_SUPPLICANT, firmware file reader, random numbers) is included and its
 * two protocols are installed on the child handle beside the Simple Network
 * Protocol.  EXPERIMENTAL: NEVER RUN ON THE HARDWARE (no emulator has it).
 */
/*
 * Charon network drivers: the UEFI face of the rge(4) port.
 *
 * A UEFI driver model driver: EFI_DRIVER_BINDING_PROTOCOL binds to the
 * Realtek RTL8125/8126/8127 PCI function through EFI_PCI_IO_PROTOCOL, runs
 * OpenBSD's rge_attach() through the shim, and produces a child handle with
 * EFI_SIMPLE_NETWORK_PROTOCOL and a device path ending in a MAC node.  The
 * firmware's (or the driver pack's) MnpDxe/Ip4Dxe/Dhcp4Dxe stack on top.
 * No interrupts are used: the chip is polled from the SNP calls and from a
 * 10 ms timer event.  Every entry runs at TPL_CALLBACK.
 *
 * NEVER RUN ON THE HARDWARE (state of 2026-11-22): written against the UEFI
 * specification 2.10 (Simple Network, Driver Binding, PCI I/O) and load
 * tested in QEMU, where no such card exists.
 *
 * Copyright (c) 2026 Charon project
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <Uefi.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PciIo.h>
#include <Protocol/SimpleNetwork.h>

#include <Protocol/WiFi2.h>
#include <Protocol/Supplicant.h>
#include <Protocol/Rng.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

#include "rge_port.h"
#include "wifi_port.h"

#ifndef NETDRV_NAME
#define NETDRV_NAME	L"Charon mwx (OpenBSD) MediaTek MT7921/MT7922 Wi-Fi, EXPERIMENTAL"
#endif
#ifndef NETDRV_LOGVAR
#define NETDRV_LOGVAR	L"CharonMwxLog"
#endif
#ifndef NETDRV_STATVAR
#define NETDRV_STATVAR	L"CharonMwxStat"
#endif

#define TXDONE_MAX	64
#define FRAME_MAX	1514
#define MCAST_MAX	16

EFI_GUID gEfiPciIoProtocolGuid = EFI_PCI_IO_PROTOCOL_GUID;
EFI_GUID gEfiSimpleNetworkProtocolGuid = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;
EFI_GUID gEfiDevicePathProtocolGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
EFI_GUID gEfiDriverBindingProtocolGuid = EFI_DRIVER_BINDING_PROTOCOL_GUID;
EFI_GUID gEfiComponentName2ProtocolGuid = EFI_COMPONENT_NAME2_PROTOCOL_GUID;
EFI_GUID gEfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
/* vendor GUID of the diagnostic variable (volatile, boot services only) */
static EFI_GUID mLogGuid =
    { 0x6b3a2f61, 0x9d1c, 0x4e7a, { 0x8f, 0x52, 0x1c, 0x0e, 0x7d, 0x3a, 0x95, 0x21 } };

static EFI_BOOT_SERVICES	*gBS;
static EFI_RUNTIME_SERVICES	*gRT;
static BOOLEAN			 mExited;	/* after ExitBootServices */

struct rge_host {
	struct rge_host		*Next;
	EFI_HANDLE		 Controller;
	EFI_HANDLE		 Child;
	EFI_PCI_IO_PROTOCOL	*PciIo;
	UINT64			 SavedAttributes;
	BOOLEAN			 AttributesSaved;
	BOOLEAN			 IntxWasEnabled;
	struct rge_port		*Port;
	EFI_SIMPLE_NETWORK_PROTOCOL Snp;
	EFI_SIMPLE_NETWORK_MODE	 Mode;
	EFI_DEVICE_PATH_PROTOCOL *DevicePath;
	EFI_EVENT		 Timer;
	EFI_EVENT		 ExitBoot;
	BOOLEAN			 Busy;
	UINT32			 Ticks;
	UINT64			 TxFrames, TxBusy, TxNoLink, RxFrames;
	UINT64			 StatSum;	/* what was published last */
	VOID			*TxDone[TXDONE_MAX];
	UINTN			 TxHead, TxCount;
	UINT8			 Frame[FRAME_MAX + 2];
};
typedef struct rge_host RGE_DEV;

static RGE_DEV	*mDevices;

/* ---- driver messages ------------------------------------------------ */
static CHAR8	mLog[4096];
static UINTN	mLogLen;
static UINT64	mLogCount, mLogPublished;	/* characters logged so far */

void
rge_host_log(const char *text)
{
	for (; *text; text++) {
		if (mLogLen + 1 >= sizeof(mLog)) {
			/* keep the newest half */
			UINTN half = sizeof(mLog) / 2, i;

			for (i = 0; i < mLogLen - half; i++)
				mLog[i] = mLog[i + half];
			mLogLen -= half;
		}
		mLog[mLogLen++] = (CHAR8)*text;
		mLogCount++;
	}
}

/* The messages of the driver ("rge0: RTL8125B, address ...", errors) are
 * kept in a volatile variable that a UEFI shell shows with
 * "dmpstore -guid 6B3A2F61-9D1C-4E7A-8F52-1C0E7D3A9521". */
static VOID
PublishLog(VOID)
{
	if (!mExited && mLogLen != 0)
		gRT->SetVariable(NETDRV_LOGVAR, &mLogGuid,
		    EFI_VARIABLE_BOOTSERVICE_ACCESS, mLogLen, mLog);
	mLogPublished = mLogCount;
}

static CHAR8 *
PutNumber(CHAR8 *At, const CHAR8 *Label, UINT64 Value)
{
	CHAR8 Digits[20];
	UINTN Count = 0;

	while (*Label)
		*At++ = *Label++;
	do {
		Digits[Count++] = (CHAR8)('0' + (Value % 10));
		Value /= 10;
	} while (Value != 0 && Count < sizeof(Digits));
	while (Count != 0)
		*At++ = Digits[--Count];
	return At;
}

/* Once a second from the timer: new driver messages go to the log variable,
 * and the frame counters to a second one (NETDRV_STATVAR, plain text), so
 * that a test on real hardware can tell "nothing was sent" from "frames go
 * out and nothing comes back" without any other tool than dmpstore. */
static VOID
PublishState(struct rge_host *Dev);

/* ---- host services for the OpenBSD side ----------------------------- */
uint32_t
rge_host_cfg_read(struct rge_host *Dev, uint32_t Offset)
{
	UINT32 Value = 0xffffffff;

	if (EFI_ERROR(Dev->PciIo->Pci.Read(Dev->PciIo, EfiPciIoWidthUint32,
	    Offset, 1, &Value)))
		return 0xffffffff;
	return Value;
}

void
rge_host_cfg_write(struct rge_host *Dev, uint32_t Offset, uint32_t Value)
{
	UINT32 Data = Value;

	Dev->PciIo->Pci.Write(Dev->PciIo, EfiPciIoWidthUint32, Offset, 1, &Data);
}

static EFI_PCI_IO_PROTOCOL_WIDTH
WidthOf(unsigned Width)
{
	return (Width == 1) ? EfiPciIoWidthUint8 :
	    (Width == 2) ? EfiPciIoWidthUint16 : EfiPciIoWidthUint32;
}

uint32_t
rge_host_reg_read(struct rge_host *Dev, unsigned Bar, int Io, unsigned Width,
    uint64_t Offset)
{
	UINT32 Value = 0;
	EFI_STATUS Status;

	if (Io)
		Status = Dev->PciIo->Io.Read(Dev->PciIo, WidthOf(Width),
		    (UINT8)Bar, Offset, 1, &Value);
	else
		Status = Dev->PciIo->Mem.Read(Dev->PciIo, WidthOf(Width),
		    (UINT8)Bar, Offset, 1, &Value);
	if (EFI_ERROR(Status))
		return 0xffffffff;
	return (Width == 1) ? (Value & 0xff) : (Width == 2) ? (Value & 0xffff) :
	    Value;
}

void
rge_host_reg_write(struct rge_host *Dev, unsigned Bar, int Io, unsigned Width,
    uint64_t Offset, uint32_t Value)
{
	UINT32 Data = Value;

	if (Io)
		Dev->PciIo->Io.Write(Dev->PciIo, WidthOf(Width), (UINT8)Bar,
		    Offset, 1, &Data);
	else
		Dev->PciIo->Mem.Write(Dev->PciIo, WidthOf(Width), (UINT8)Bar,
		    Offset, 1, &Data);
}

void *
rge_host_dma_alloc(struct rge_host *Dev, size_t Bytes, uint64_t *DeviceAddress,
    void **Cookie)
{
	UINTN Pages = EFI_SIZE_TO_PAGES(Bytes), Mapped = Bytes, i;
	VOID *Host = NULL, *Mapping = NULL;
	EFI_PHYSICAL_ADDRESS Address = 0;

	if (mExited || Bytes == 0)
		return NULL;
	if (EFI_ERROR(Dev->PciIo->AllocateBuffer(Dev->PciIo, AllocateAnyPages,
	    EfiBootServicesData, Pages, &Host, 0)) || Host == NULL)
		return NULL;
	if (EFI_ERROR(Dev->PciIo->Map(Dev->PciIo,
	    EfiPciIoOperationBusMasterCommonBuffer, Host, &Mapped, &Address,
	    &Mapping)) || Mapped < Bytes) {
		if (Mapping != NULL)
			Dev->PciIo->Unmap(Dev->PciIo, Mapping);
		Dev->PciIo->FreeBuffer(Dev->PciIo, Pages, Host);
		return NULL;
	}
	for (i = 0; i < Bytes; i++)
		((volatile UINT8 *)Host)[i] = 0;
	*DeviceAddress = Address;
	*Cookie = Mapping;
	return Host;
}

void
rge_host_dma_free(struct rge_host *Dev, void *Host, size_t Bytes, void *Cookie)
{
	if (mExited)
		return;
	if (Cookie != NULL)
		Dev->PciIo->Unmap(Dev->PciIo, Cookie);
	Dev->PciIo->FreeBuffer(Dev->PciIo, EFI_SIZE_TO_PAGES(Bytes), Host);
}

void *
rge_host_alloc(size_t Bytes)
{
	VOID *Memory = NULL;

	if (mExited || EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, Bytes,
	    &Memory)))
		return NULL;
	return Memory;
}

void
rge_host_free(void *Memory)
{
	if (!mExited && Memory != NULL)
		gBS->FreePool(Memory);
}

void
rge_host_stall(unsigned Microseconds)
{
	gBS->Stall(Microseconds ? Microseconds : 1);
}

static VOID
PublishState(struct rge_host *Dev)
{
	CHAR8 Text[160], *At = Text;
	UINT64 Sum;

	if (mExited)
		return;
	if (mLogCount != mLogPublished)
		PublishLog();
	Sum = Dev->TxFrames + Dev->TxBusy + Dev->TxNoLink + Dev->RxFrames +
	    ((UINT64)Dev->Mode.State << 60) +
	    (Dev->Mode.MediaPresent ? ((UINT64)1 << 59) : 0);
	if (Sum == Dev->StatSum)
		return;
	Dev->StatSum = Sum;
	At = PutNumber(At, (const CHAR8 *)"state=", Dev->Mode.State);
	At = PutNumber(At, (const CHAR8 *)" link=", Dev->Mode.MediaPresent ? 1 : 0);
	At = PutNumber(At, (const CHAR8 *)" sent=", Dev->TxFrames);
	At = PutNumber(At, (const CHAR8 *)" refused-busy=", Dev->TxBusy);
	At = PutNumber(At, (const CHAR8 *)" dropped-no-link=", Dev->TxNoLink);
	At = PutNumber(At, (const CHAR8 *)" received=", Dev->RxFrames);
	*At++ = '\n';
	gRT->SetVariable(NETDRV_STATVAR, &mLogGuid,
	    EFI_VARIABLE_BOOTSERVICE_ACCESS, (UINTN)(At - Text), Text);
}

/* ---- Simple Network Protocol ---------------------------------------- */
#define DEV_FROM_SNP(a)	\
	((RGE_DEV *)((UINT8 *)(a) - OFFSET_OF(RGE_DEV, Snp)))

static VOID
Poll(RGE_DEV *Dev)
{
	if (Dev->Port == NULL || (Dev->Mode.State != EfiSimpleNetworkInitialized &&
	    !rge_port_running(Dev->Port)))
		return;
	rge_port_poll(Dev->Port);
	Dev->Mode.MediaPresent = rge_port_link(Dev->Port) ? TRUE : FALSE;
}

/* Serialises with the timer event; a nested call (a notification that calls
 * back into the protocol while it is busy) is refused. */
static BOOLEAN
Enter(RGE_DEV *Dev, EFI_TPL *OldTpl)
{
	*OldTpl = gBS->RaiseTPL(TPL_CALLBACK);
	if (Dev->Busy) {
		gBS->RestoreTPL(*OldTpl);
		return FALSE;
	}
	Dev->Busy = TRUE;
	return TRUE;
}

static VOID
Leave(RGE_DEV *Dev, EFI_TPL OldTpl)
{
	Dev->Busy = FALSE;
	gBS->RestoreTPL(OldTpl);
}

static EFI_STATUS
StateError(RGE_DEV *Dev)
{
	return (Dev->Mode.State == EfiSimpleNetworkStopped) ? EFI_NOT_STARTED :
	    EFI_DEVICE_ERROR;
}

static EFI_STATUS EFIAPI
SnpStart(EFI_SIMPLE_NETWORK_PROTOCOL *This)
{
	RGE_DEV *Dev;
	EFI_TPL Tpl;
	EFI_STATUS Status = EFI_SUCCESS;

	if (This == NULL)
		return EFI_INVALID_PARAMETER;
	Dev = DEV_FROM_SNP(This);
	if (!Enter(Dev, &Tpl))
		return EFI_DEVICE_ERROR;
	if (Dev->Mode.State == EfiSimpleNetworkStopped)
		Dev->Mode.State = EfiSimpleNetworkStarted;
	else
		Status = EFI_ALREADY_STARTED;
	Leave(Dev, Tpl);
	return Status;
}

static EFI_STATUS EFIAPI
SnpStop(EFI_SIMPLE_NETWORK_PROTOCOL *This)
{
	RGE_DEV *Dev;
	EFI_TPL Tpl;
	EFI_STATUS Status = EFI_SUCCESS;

	if (This == NULL)
		return EFI_INVALID_PARAMETER;
	Dev = DEV_FROM_SNP(This);
	if (!Enter(Dev, &Tpl))
		return EFI_DEVICE_ERROR;
	if (Dev->Mode.State == EfiSimpleNetworkStarted)
		Dev->Mode.State = EfiSimpleNetworkStopped;
	else
		Status = StateError(Dev);
	Leave(Dev, Tpl);
	return Status;
}

static EFI_STATUS
BringUp(RGE_DEV *Dev)
{
	Dev->TxHead = Dev->TxCount = 0;
	if (rge_port_up(Dev->Port) != 0) {
		rge_port_down(Dev->Port);
		PublishLog();
		return EFI_DEVICE_ERROR;
	}
	Dev->Mode.State = EfiSimpleNetworkInitialized;
	Dev->Mode.MediaPresent = FALSE;
	PublishLog();
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
SnpInitialize(EFI_SIMPLE_NETWORK_PROTOCOL *This, UINTN ExtraRx, UINTN ExtraTx)
{
	RGE_DEV *Dev;
	EFI_TPL Tpl;
	EFI_STATUS Status;

	(VOID)ExtraRx;
	(VOID)ExtraTx;
	if (This == NULL)
		return EFI_INVALID_PARAMETER;
	Dev = DEV_FROM_SNP(This);
	if (!Enter(Dev, &Tpl))
		return EFI_DEVICE_ERROR;
	if (Dev->Mode.State != EfiSimpleNetworkStarted)
		Status = StateError(Dev);
	else {
		Dev->Mode.ReceiveFilterSetting = 0;
		Dev->Mode.MCastFilterCount = 0;
		Status = BringUp(Dev);
	}
	Leave(Dev, Tpl);
	return Status;
}

static EFI_STATUS EFIAPI
SnpReset(EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN Extended)
{
	RGE_DEV *Dev;
	EFI_TPL Tpl;
	EFI_STATUS Status;

	(VOID)Extended;
	if (This == NULL)
		return EFI_INVALID_PARAMETER;
	Dev = DEV_FROM_SNP(This);
	if (!Enter(Dev, &Tpl))
		return EFI_DEVICE_ERROR;
	if (Dev->Mode.State != EfiSimpleNetworkInitialized)
		Status = StateError(Dev);
	else {
		rge_port_down(Dev->Port);
		Dev->Mode.State = EfiSimpleNetworkStarted;
		Status = BringUp(Dev);
	}
	Leave(Dev, Tpl);
	return Status;
}

static EFI_STATUS EFIAPI
SnpShutdown(EFI_SIMPLE_NETWORK_PROTOCOL *This)
{
	RGE_DEV *Dev;
	EFI_TPL Tpl;
	EFI_STATUS Status = EFI_SUCCESS;

	if (This == NULL)
		return EFI_INVALID_PARAMETER;
	Dev = DEV_FROM_SNP(This);
	if (!Enter(Dev, &Tpl))
		return EFI_DEVICE_ERROR;
	if (Dev->Mode.State != EfiSimpleNetworkInitialized)
		Status = StateError(Dev);
	else {
		rge_port_down(Dev->Port);
		Dev->Mode.State = EfiSimpleNetworkStarted;
		Dev->Mode.MediaPresent = FALSE;
		Dev->TxHead = Dev->TxCount = 0;
	}
	Leave(Dev, Tpl);
	return Status;
}

static VOID
ApplyFilter(RGE_DEV *Dev)
{
	UINT8 List[MCAST_MAX * 6];
	UINTN i, k;
	int Mode = 0;
	UINT32 Setting = Dev->Mode.ReceiveFilterSetting;

	if (Setting & EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS)
		Mode |= 1;
	if (Setting & EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST)
		Mode |= 2;
	for (i = 0; i < Dev->Mode.MCastFilterCount && i < MCAST_MAX; i++)
		for (k = 0; k < 6; k++)
			List[i * 6 + k] = Dev->Mode.MCastFilter[i].Addr[k];
	if (!(Setting & EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST))
		i = 0;
	rge_port_filter(Dev->Port, Mode, List, (unsigned)i);
}

static EFI_STATUS EFIAPI
SnpReceiveFilters(EFI_SIMPLE_NETWORK_PROTOCOL *This, UINT32 Enable,
    UINT32 Disable, BOOLEAN ResetMCast, UINTN MCastCount,
    EFI_MAC_ADDRESS *MCastFilter)
{
	RGE_DEV *Dev;
	EFI_TPL Tpl;
	EFI_STATUS Status = EFI_SUCCESS;
	UINTN i;

	if (This == NULL)
		return EFI_INVALID_PARAMETER;
	Dev = DEV_FROM_SNP(This);
	if (!Enter(Dev, &Tpl))
		return EFI_DEVICE_ERROR;
	if (Dev->Mode.State != EfiSimpleNetworkInitialized)
		Status = StateError(Dev);
	else if (((Enable | Disable) & ~Dev->Mode.ReceiveFilterMask) != 0 ||
	    (!ResetMCast && (Enable & EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST) &&
	    MCastCount != 0 && (MCastCount > MCAST_MAX || MCastFilter == NULL)))
		Status = EFI_INVALID_PARAMETER;
	else {
		if (ResetMCast)
			Dev->Mode.MCastFilterCount = 0;
		else if (MCastCount != 0 && MCastFilter != NULL) {
			Dev->Mode.MCastFilterCount = (UINT32)MCastCount;
			for (i = 0; i < MCastCount; i++)
				Dev->Mode.MCastFilter[i] = MCastFilter[i];
		}
		Dev->Mode.ReceiveFilterSetting |= Enable;
		Dev->Mode.ReceiveFilterSetting &= ~Disable;
		ApplyFilter(Dev);
	}
	Leave(Dev, Tpl);
	return Status;
}

static EFI_STATUS EFIAPI
SnpStationAddress(EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN Reset,
    EFI_MAC_ADDRESS *New)
{
	RGE_DEV *Dev;
	EFI_TPL Tpl;
	EFI_STATUS Status = EFI_SUCCESS;

	if (This == NULL || (!Reset && New == NULL))
		return EFI_INVALID_PARAMETER;
	Dev = DEV_FROM_SNP(This);
	if (!Enter(Dev, &Tpl))
		return EFI_DEVICE_ERROR;
	if (Dev->Mode.State != EfiSimpleNetworkInitialized)
		Status = StateError(Dev);
	else {
		Dev->Mode.CurrentAddress = Reset ? Dev->Mode.PermanentAddress :
		    *New;
		rge_port_set_mac(Dev->Port, Dev->Mode.CurrentAddress.Addr);
	}
	Leave(Dev, Tpl);
	return Status;
}

static EFI_STATUS EFIAPI
SnpStatistics(EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN Reset, UINTN *Size,
    EFI_NETWORK_STATISTICS *Table)
{
	(VOID)This; (VOID)Reset; (VOID)Size; (VOID)Table;
	return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI
SnpMCastIpToMac(EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN IPv6,
    EFI_IP_ADDRESS *Ip, EFI_MAC_ADDRESS *Mac)
{
	RGE_DEV *Dev;
	UINTN i;

	if (This == NULL || Ip == NULL || Mac == NULL)
		return EFI_INVALID_PARAMETER;
	Dev = DEV_FROM_SNP(This);
	if (Dev->Mode.State == EfiSimpleNetworkStopped)
		return EFI_NOT_STARTED;
	for (i = 0; i < sizeof(Mac->Addr); i++)
		Mac->Addr[i] = 0;
	if (IPv6) {
		if (Ip->v6.Addr[0] != 0xff)
			return EFI_INVALID_PARAMETER;
		Mac->Addr[0] = 0x33;
		Mac->Addr[1] = 0x33;
		for (i = 0; i < 4; i++)
			Mac->Addr[2 + i] = Ip->v6.Addr[12 + i];
	} else {
		if ((Ip->v4.Addr[0] & 0xf0) != 0xe0)
			return EFI_INVALID_PARAMETER;
		Mac->Addr[0] = 0x01;
		Mac->Addr[1] = 0x00;
		Mac->Addr[2] = 0x5e;
		Mac->Addr[3] = Ip->v4.Addr[1] & 0x7f;
		Mac->Addr[4] = Ip->v4.Addr[2];
		Mac->Addr[5] = Ip->v4.Addr[3];
	}
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
SnpNvData(EFI_SIMPLE_NETWORK_PROTOCOL *This, BOOLEAN Read, UINTN Offset,
    UINTN Size, VOID *Buffer)
{
	(VOID)This; (VOID)Read; (VOID)Offset; (VOID)Size; (VOID)Buffer;
	return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI
SnpGetStatus(EFI_SIMPLE_NETWORK_PROTOCOL *This, UINT32 *Interrupt, VOID **TxBuf)
{
	RGE_DEV *Dev;
	EFI_TPL Tpl;
	EFI_STATUS Status = EFI_SUCCESS;

	if (This == NULL || (Interrupt == NULL && TxBuf == NULL))
		return EFI_INVALID_PARAMETER;
	Dev = DEV_FROM_SNP(This);
	if (!Enter(Dev, &Tpl))
		return EFI_DEVICE_ERROR;
	if (Dev->Mode.State != EfiSimpleNetworkInitialized)
		Status = StateError(Dev);
	else {
		Poll(Dev);
		if (Interrupt != NULL) {
			*Interrupt = 0;
			if (rge_port_rx_peek(Dev->Port) != 0)
				*Interrupt |=
				    EFI_SIMPLE_NETWORK_RECEIVE_INTERRUPT;
			if (Dev->TxCount != 0)
				*Interrupt |=
				    EFI_SIMPLE_NETWORK_TRANSMIT_INTERRUPT;
		}
		if (TxBuf != NULL) {
			/* every transmitted buffer is handed back once */
			if (Dev->TxCount != 0) {
				*TxBuf = Dev->TxDone[Dev->TxHead];
				Dev->TxHead = (Dev->TxHead + 1) % TXDONE_MAX;
				Dev->TxCount--;
			} else
				*TxBuf = NULL;
		}
	}
	Leave(Dev, Tpl);
	return Status;
}

static EFI_STATUS EFIAPI
SnpTransmit(EFI_SIMPLE_NETWORK_PROTOCOL *This, UINTN HeaderSize, UINTN Size,
    VOID *Buffer, EFI_MAC_ADDRESS *Src, EFI_MAC_ADDRESS *Dest, UINT16 *Protocol)
{
	RGE_DEV *Dev;
	EFI_TPL Tpl;
	EFI_STATUS Status = EFI_SUCCESS;
	UINTN i, Length;
	int Result;

	if (This == NULL || Buffer == NULL)
		return EFI_INVALID_PARAMETER;
	Dev = DEV_FROM_SNP(This);
	if (Size < Dev->Mode.MediaHeaderSize)
		return EFI_BUFFER_TOO_SMALL;
	if (Size > FRAME_MAX || (HeaderSize != 0 &&
	    (HeaderSize != Dev->Mode.MediaHeaderSize || Dest == NULL ||
	    Protocol == NULL)))
		return EFI_INVALID_PARAMETER;
	if (!Enter(Dev, &Tpl))
		return EFI_NOT_READY;
	if (Dev->Mode.State != EfiSimpleNetworkInitialized)
		Status = StateError(Dev);
	else if (Dev->TxCount >= TXDONE_MAX)
		Status = EFI_NOT_READY;	/* the caller must collect buffers */
	else {
		for (i = 0; i < Size; i++)
			Dev->Frame[i] = ((UINT8 *)Buffer)[i];
		if (HeaderSize != 0) {
			if (Src == NULL)
				Src = &Dev->Mode.CurrentAddress;
			for (i = 0; i < 6; i++) {
				Dev->Frame[i] = Dest->Addr[i];
				Dev->Frame[6 + i] = Src->Addr[i];
			}
			Dev->Frame[12] = (UINT8)(*Protocol >> 8);
			Dev->Frame[13] = (UINT8)*Protocol;
		}
		for (Length = Size; Length < 60; Length++)
			Dev->Frame[Length] = 0;	/* minimum frame, zero padded */
		Poll(Dev);
		Result = rge_port_tx(Dev->Port, Dev->Frame, Length);
		if (Result == 1) {
			Dev->TxBusy++;
			Status = EFI_NOT_READY;
		} else {
			if (Result == 0)
				Dev->TxFrames++;
			else
				Dev->TxNoLink++;
			/* queued, or dropped without a link as a cable-less
			 * card drops it: the buffer is free either way */
			Dev->TxDone[(Dev->TxHead + Dev->TxCount) % TXDONE_MAX] =
			    Buffer;
			Dev->TxCount++;
		}
	}
	Leave(Dev, Tpl);
	return Status;
}

static EFI_STATUS EFIAPI
SnpReceive(EFI_SIMPLE_NETWORK_PROTOCOL *This, UINTN *HeaderSize, UINTN *Size,
    VOID *Buffer, EFI_MAC_ADDRESS *Src, EFI_MAC_ADDRESS *Dest, UINT16 *Protocol)
{
	RGE_DEV *Dev;
	EFI_TPL Tpl;
	EFI_STATUS Status = EFI_SUCCESS;
	UINTN Length, i;
	UINT8 *Frame = Buffer;

	if (This == NULL || Size == NULL || Buffer == NULL)
		return EFI_INVALID_PARAMETER;
	Dev = DEV_FROM_SNP(This);
	if (!Enter(Dev, &Tpl))
		return EFI_NOT_READY;
	if (Dev->Mode.State != EfiSimpleNetworkInitialized)
		Status = StateError(Dev);
	else {
		Poll(Dev);
		/* The chip accepts jumbo frames; a frame no caller buffer of
		 * this protocol can take (MaxPacketSize 1500 + header + VLAN
		 * tag + FCS) is dropped here, or it would stay at the head of
		 * the queue for ever. */
		while ((Length = rge_port_rx_peek(Dev->Port)) > FRAME_MAX + 8)
			rge_port_rx_take(Dev->Port, Buffer, 0);
		if (Length == 0)
			Status = EFI_NOT_READY;
		else if (Length > *Size) {
			*Size = Length;		/* the frame stays queued */
			Status = EFI_BUFFER_TOO_SMALL;
		} else {
			Length = rge_port_rx_take(Dev->Port, Buffer, *Size);
			if (Length < 14)
				Status = EFI_NOT_READY;	/* runt: dropped */
			else {
				Dev->RxFrames++;
				*Size = Length;
				if (HeaderSize != NULL)
					*HeaderSize = Dev->Mode.MediaHeaderSize;
				for (i = 0; i < 6; i++) {
					if (Dest != NULL)
						Dest->Addr[i] = Frame[i];
					if (Src != NULL)
						Src->Addr[i] = Frame[6 + i];
				}
				for (i = 6; i < sizeof(Dest->Addr); i++) {
					if (Dest != NULL)
						Dest->Addr[i] = 0;
					if (Src != NULL)
						Src->Addr[i] = 0;
				}
				if (Protocol != NULL)
					*Protocol = (UINT16)((Frame[12] << 8) |
					    Frame[13]);
			}
		}
	}
	Leave(Dev, Tpl);
	return Status;
}

#include "wifi_face.inc"

static VOID EFIAPI
WaitForPacketNotify(EFI_EVENT Event, VOID *Context)
{
	RGE_DEV *Dev = Context;

	if (mExited || Dev->Busy || Dev->Port == NULL)
		return;
	Dev->Busy = TRUE;
	Poll(Dev);
	if (Dev->Mode.State == EfiSimpleNetworkInitialized &&
	    rge_port_rx_peek(Dev->Port) != 0)
		gBS->SignalEvent(Event);
	Dev->Busy = FALSE;
}

static VOID EFIAPI
TimerNotify(EFI_EVENT Event, VOID *Context)
{
	RGE_DEV *Dev = Context;

	(VOID)Event;
	if (mExited || Dev->Busy || Dev->Port == NULL)
		return;
	Dev->Busy = TRUE;
	rge_port_tick(Dev->Port);
	Poll(Dev);
	WifiFaceTick(Dev);
	if (++Dev->Ticks % 100 == 0)
		PublishState(Dev);
	Dev->Busy = FALSE;
}

static VOID EFIAPI
ExitBootNotify(EFI_EVENT Event, VOID *Context)
{
	RGE_DEV *Dev = Context;

	(VOID)Event;
	mExited = TRUE;
	if (Dev->Port != NULL)
		rge_port_quiesce(Dev->Port);
}

/* ---- driver binding ------------------------------------------------- */
static EFI_STATUS EFIAPI BindingSupported(EFI_DRIVER_BINDING_PROTOCOL *,
    EFI_HANDLE, EFI_DEVICE_PATH_PROTOCOL *);
static EFI_STATUS EFIAPI BindingStart(EFI_DRIVER_BINDING_PROTOCOL *,
    EFI_HANDLE, EFI_DEVICE_PATH_PROTOCOL *);
static EFI_STATUS EFIAPI BindingStop(EFI_DRIVER_BINDING_PROTOCOL *,
    EFI_HANDLE, UINTN, EFI_HANDLE *);

static EFI_DRIVER_BINDING_PROTOCOL mBinding = {
	BindingSupported, BindingStart, BindingStop, 0x10, NULL, NULL
};

static EFI_STATUS EFIAPI
BindingSupported(EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller,
    EFI_DEVICE_PATH_PROTOCOL *Remaining)
{
	EFI_PCI_IO_PROTOCOL *PciIo;
	EFI_STATUS Status;
	UINT16 Ids[2] = { 0xffff, 0xffff };

	(VOID)Remaining;
	Status = gBS->OpenProtocol(Controller, &gEfiPciIoProtocolGuid,
	    (VOID **)&PciIo, This->DriverBindingHandle, Controller,
	    EFI_OPEN_PROTOCOL_BY_DRIVER);
	if (EFI_ERROR(Status))
		return Status;
	Status = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint16, 0, 2, Ids);
	gBS->CloseProtocol(Controller, &gEfiPciIoProtocolGuid,
	    This->DriverBindingHandle, Controller);
	if (EFI_ERROR(Status))
		return EFI_UNSUPPORTED;
	return rge_port_match(Ids[0], Ids[1]) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

static UINTN
DevicePathSize(EFI_DEVICE_PATH_PROTOCOL *Path)
{
	UINT8 *Node = (UINT8 *)Path;
	UINTN Size = 0, Length, Guard;

	if (Path == NULL)
		return 0;
	for (Guard = 0; Guard < 64; Guard++) {
		Length = (UINTN)Node[2] | ((UINTN)Node[3] << 8);
		if (Length < 4)
			return 0;
		if (Node[0] == 0x7f && Node[1] == 0xff)
			return Size;	/* without the end node */
		Size += Length;
		Node += Length;
	}
	return 0;
}

static EFI_DEVICE_PATH_PROTOCOL *
MakeChildPath(EFI_DEVICE_PATH_PROTOCOL *Parent, UINT8 *Mac)
{
	UINTN Base = DevicePathSize(Parent), i;
	UINT8 *Path = NULL, *Node;

	if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, Base + 37 + 4,
	    (VOID **)&Path)))
		return NULL;
	for (i = 0; i < Base; i++)
		Path[i] = ((UINT8 *)Parent)[i];
	Node = Path + Base;		/* MAC address node, UEFI 2.10 10.3.4.10 */
	for (i = 0; i < 37; i++)
		Node[i] = 0;
	Node[0] = 0x03;
	Node[1] = 0x0b;
	Node[2] = 37;
	for (i = 0; i < 6; i++)
		Node[4 + i] = Mac[i];
	Node[36] = 0x01;		/* IfType: Ethernet */
	Node += 37;
	Node[0] = 0x7f;
	Node[1] = 0xff;
	Node[2] = 4;
	Node[3] = 0;
	return (EFI_DEVICE_PATH_PROTOCOL *)Path;
}

static VOID
ReleaseHardware(RGE_DEV *Dev)
{
	UINT16 Command;

	WifiFaceRelease(Dev);
	if (Dev->Timer != NULL) {
		gBS->CloseEvent(Dev->Timer);
		Dev->Timer = NULL;
	}
	if (Dev->ExitBoot != NULL) {
		gBS->CloseEvent(Dev->ExitBoot);
		Dev->ExitBoot = NULL;
	}
	if (Dev->Snp.WaitForPacket != NULL) {
		gBS->CloseEvent(Dev->Snp.WaitForPacket);
		Dev->Snp.WaitForPacket = NULL;
	}
	if (Dev->Port != NULL) {
		rge_port_detach(Dev->Port);	/* chip stopped, memory freed */
		Dev->Port = NULL;
	}
	if (Dev->IntxWasEnabled && !EFI_ERROR(Dev->PciIo->Pci.Read(Dev->PciIo,
	    EfiPciIoWidthUint16, 4, 1, &Command))) {
		Command &= (UINT16)~0x0400;
		Dev->PciIo->Pci.Write(Dev->PciIo, EfiPciIoWidthUint16, 4, 1,
		    &Command);
		Dev->IntxWasEnabled = FALSE;
	}
	if (Dev->AttributesSaved) {
		Dev->PciIo->Attributes(Dev->PciIo, EfiPciIoAttributeOperationSet,
		    Dev->SavedAttributes, NULL);
		Dev->AttributesSaved = FALSE;
	}
	if (Dev->DevicePath != NULL) {
		gBS->FreePool(Dev->DevicePath);
		Dev->DevicePath = NULL;
	}
	Dev->Mode.State = EfiSimpleNetworkStopped;
}

static VOID
Forget(RGE_DEV *Dev)
{
	RGE_DEV **Link;

	for (Link = &mDevices; *Link != NULL; Link = &(*Link)->Next) {
		if (*Link == Dev) {
			*Link = Dev->Next;
			break;
		}
	}
	gBS->FreePool(Dev);
}

static EFI_STATUS EFIAPI
BindingStart(EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller,
    EFI_DEVICE_PATH_PROTOCOL *Remaining)
{
	EFI_PCI_IO_PROTOCOL *PciIo, *ChildPciIo;
	EFI_DEVICE_PATH_PROTOCOL *ParentPath = NULL;
	EFI_STATUS Status;
	EFI_TPL Tpl;
	RGE_DEV *Dev = NULL;
	UINT16 Ids[2], Command;
	UINT64 Supported = 0;
	UINT8 Mac[6];
	UINTN i;

	(VOID)Remaining;
	Status = gBS->OpenProtocol(Controller, &gEfiPciIoProtocolGuid,
	    (VOID **)&PciIo, This->DriverBindingHandle, Controller,
	    EFI_OPEN_PROTOCOL_BY_DRIVER);
	if (EFI_ERROR(Status))
		return Status;
	Status = gBS->AllocatePool(EfiBootServicesData, sizeof(*Dev),
	    (VOID **)&Dev);
	if (EFI_ERROR(Status))
		goto close;
	for (i = 0; i < sizeof(*Dev); i++)
		((UINT8 *)Dev)[i] = 0;
	Dev->Controller = Controller;
	Dev->PciIo = PciIo;
	Dev->Next = mDevices;
	mDevices = Dev;

	Status = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint16, 0, 2, Ids);
	if (EFI_ERROR(Status) || !rge_port_match(Ids[0], Ids[1])) {
		Status = EFI_UNSUPPORTED;
		goto forget;
	}
	/* Decode memory and I/O, allow DMA; remember what was set before. */
	Status = PciIo->Attributes(PciIo, EfiPciIoAttributeOperationGet, 0,
	    &Dev->SavedAttributes);
	if (EFI_ERROR(Status))
		goto forget;
	Dev->AttributesSaved = TRUE;
	PciIo->Attributes(PciIo, EfiPciIoAttributeOperationSupported, 0,
	    &Supported);
	Status = PciIo->Attributes(PciIo, EfiPciIoAttributeOperationEnable,
	    Supported & EFI_PCI_DEVICE_ENABLE, NULL);
	if (EFI_ERROR(Status))
		goto release;
	/* The chip is polled: its INTx line stays switched off at the PCI
	 * level whatever the driver writes to the interrupt mask register. */
	if (!EFI_ERROR(PciIo->Pci.Read(PciIo, EfiPciIoWidthUint16, 4, 1,
	    &Command)) && !(Command & 0x0400)) {
		Command |= 0x0400;
		PciIo->Pci.Write(PciIo, EfiPciIoWidthUint16, 4, 1, &Command);
		Dev->IntxWasEnabled = TRUE;
	}

	Tpl = gBS->RaiseTPL(TPL_CALLBACK);
	Dev->Port = rge_port_attach(Dev, Ids[0], Ids[1], Mac);
	gBS->RestoreTPL(Tpl);
	PublishLog();
	if (Dev->Port == NULL) {
		Status = EFI_DEVICE_ERROR;
		goto release;
	}

	Dev->Snp.Revision = EFI_SIMPLE_NETWORK_PROTOCOL_REVISION;
	Dev->Snp.Start = SnpStart;
	Dev->Snp.Stop = SnpStop;
	Dev->Snp.Initialize = SnpInitialize;
	Dev->Snp.Reset = SnpReset;
	Dev->Snp.Shutdown = SnpShutdown;
	Dev->Snp.ReceiveFilters = SnpReceiveFilters;
	Dev->Snp.StationAddress = SnpStationAddress;
	Dev->Snp.Statistics = SnpStatistics;
	Dev->Snp.MCastIpToMac = SnpMCastIpToMac;
	Dev->Snp.NvData = SnpNvData;
	Dev->Snp.GetStatus = SnpGetStatus;
	Dev->Snp.Transmit = SnpTransmit;
	Dev->Snp.Receive = SnpReceive;
	Dev->Snp.Mode = &Dev->Mode;
	Dev->Mode.State = EfiSimpleNetworkStopped;
	Dev->Mode.HwAddressSize = 6;
	Dev->Mode.MediaHeaderSize = 14;
	Dev->Mode.MaxPacketSize = 1500;
	Dev->Mode.ReceiveFilterMask = EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
	    EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST |
	    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST |
	    EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS |
	    EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST;
	Dev->Mode.MaxMCastFilterCount = MCAST_MAX;
	Dev->Mode.IfType = 1;
	Dev->Mode.MacAddressChangeable = TRUE;
	Dev->Mode.MultipleTxSupported = FALSE;
	Dev->Mode.MediaPresentSupported = TRUE;
	Dev->Mode.MediaPresent = FALSE;
	for (i = 0; i < 6; i++) {
		Dev->Mode.CurrentAddress.Addr[i] = Mac[i];
		Dev->Mode.PermanentAddress.Addr[i] = Mac[i];
		Dev->Mode.BroadcastAddress.Addr[i] = 0xff;
	}

	Status = gBS->CreateEvent(EVT_NOTIFY_WAIT, TPL_CALLBACK,
	    WaitForPacketNotify, Dev, &Dev->Snp.WaitForPacket);
	if (EFI_ERROR(Status))
		goto release;
	Status = gBS->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
	    TimerNotify, Dev, &Dev->Timer);
	if (EFI_ERROR(Status))
		goto release;
	Status = gBS->SetTimer(Dev->Timer, TimerPeriodic, 100000);  /* 10 ms */
	if (EFI_ERROR(Status))
		goto release;
	Status = gBS->CreateEvent(EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY,
	    ExitBootNotify, Dev, &Dev->ExitBoot);
	if (EFI_ERROR(Status))
		goto release;

	gBS->OpenProtocol(Controller, &gEfiDevicePathProtocolGuid,
	    (VOID **)&ParentPath, This->DriverBindingHandle, Controller,
	    EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	Dev->DevicePath = MakeChildPath(ParentPath, Mac);
	if (Dev->DevicePath == NULL) {
		Status = EFI_OUT_OF_RESOURCES;
		goto release;
	}
	Status = gBS->InstallMultipleProtocolInterfaces(&Dev->Child,
	    &gEfiSimpleNetworkProtocolGuid, &Dev->Snp,
	    &gEfiDevicePathProtocolGuid, Dev->DevicePath,
	    &mWifi2Guid, &mWifi2, &mSupplicantGuid, &mSupplicant, NULL);
	if (EFI_ERROR(Status))
		goto release;
	Status = gBS->OpenProtocol(Controller, &gEfiPciIoProtocolGuid,
	    (VOID **)&ChildPciIo, This->DriverBindingHandle, Dev->Child,
	    EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
	if (EFI_ERROR(Status)) {
		gBS->UninstallMultipleProtocolInterfaces(Dev->Child,
		    &gEfiSimpleNetworkProtocolGuid, &Dev->Snp,
		    &gEfiDevicePathProtocolGuid, Dev->DevicePath,
	    &mWifi2Guid, &mWifi2, &mSupplicantGuid, &mSupplicant, NULL);
		Dev->Child = NULL;
		goto release;
	}
	return EFI_SUCCESS;

release:
	ReleaseHardware(Dev);
forget:
	Forget(Dev);
close:
	gBS->CloseProtocol(Controller, &gEfiPciIoProtocolGuid,
	    This->DriverBindingHandle, Controller);
	return Status;
}

static EFI_STATUS
RemoveChild(EFI_DRIVER_BINDING_PROTOCOL *This, RGE_DEV *Dev)
{
	EFI_PCI_IO_PROTOCOL *PciIo;
	EFI_STATUS Status;
	EFI_TPL Tpl;

	if (Dev->Child == NULL)
		return EFI_SUCCESS;
	gBS->CloseProtocol(Dev->Controller, &gEfiPciIoProtocolGuid,
	    This->DriverBindingHandle, Dev->Child);
	Status = gBS->UninstallMultipleProtocolInterfaces(Dev->Child,
	    &gEfiSimpleNetworkProtocolGuid, &Dev->Snp,
	    &gEfiDevicePathProtocolGuid, Dev->DevicePath,
	    &mWifi2Guid, &mWifi2, &mSupplicantGuid, &mSupplicant, NULL);
	if (EFI_ERROR(Status)) {
		/* still in use: keep the child as it was */
		gBS->OpenProtocol(Dev->Controller, &gEfiPciIoProtocolGuid,
		    (VOID **)&PciIo, This->DriverBindingHandle, Dev->Child,
		    EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
		return EFI_DEVICE_ERROR;
	}
	Dev->Child = NULL;
	Tpl = gBS->RaiseTPL(TPL_CALLBACK);
	Dev->Busy = TRUE;		/* no notification runs in between */
	gBS->RestoreTPL(Tpl);
	ReleaseHardware(Dev);
	Dev->Busy = FALSE;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
BindingStop(EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller,
    UINTN ChildCount, EFI_HANDLE *Children)
{
	RGE_DEV *Dev;
	EFI_STATUS Status;

	(VOID)ChildCount;
	(VOID)Children;
	for (Dev = mDevices; Dev != NULL; Dev = Dev->Next)
		if (Dev->Controller == Controller)
			break;
	if (Dev == NULL)
		return EFI_SUCCESS;
	/* One child per controller: whether the firmware names it or asks
	 * for the controller itself, the child goes first. */
	Status = RemoveChild(This, Dev);
	if (EFI_ERROR(Status))
		return Status;
	if (ChildCount != 0)
		return EFI_SUCCESS;	/* the call for the controller follows */
	gBS->CloseProtocol(Controller, &gEfiPciIoProtocolGuid,
	    This->DriverBindingHandle, Controller);
	Forget(Dev);
	return EFI_SUCCESS;
}

/* ---- component name ------------------------------------------------- */
static EFI_STATUS EFIAPI
NameDriver(EFI_COMPONENT_NAME2_PROTOCOL *This, CHAR8 *Language, CHAR16 **Name)
{
	(VOID)This;
	if (Language == NULL || Name == NULL)
		return EFI_INVALID_PARAMETER;
	if (Language[0] != 'e' || Language[1] != 'n')
		return EFI_UNSUPPORTED;
	*Name = (CHAR16 *)NETDRV_NAME;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
NameController(EFI_COMPONENT_NAME2_PROTOCOL *This, EFI_HANDLE Controller,
    EFI_HANDLE Child, CHAR8 *Language, CHAR16 **Name)
{
	(VOID)This; (VOID)Controller; (VOID)Child; (VOID)Language; (VOID)Name;
	return EFI_UNSUPPORTED;
}

static EFI_COMPONENT_NAME2_PROTOCOL mName = {
	NameDriver, NameController, (CHAR8 *)"en"
};

/* ---- image ---------------------------------------------------------- */
static EFI_STATUS EFIAPI
Unload(EFI_HANDLE Image)
{
	RGE_DEV *Dev, *Next;

	for (Dev = mDevices; Dev != NULL; Dev = Next) {
		Next = Dev->Next;
		gBS->DisconnectController(Dev->Controller, Image, NULL);
	}
	if (mDevices != NULL)
		return EFI_ACCESS_DENIED;	/* an adapter is still in use */
	return gBS->UninstallMultipleProtocolInterfaces(Image,
	    &gEfiDriverBindingProtocolGuid, &mBinding,
	    &gEfiComponentName2ProtocolGuid, &mName, NULL);
}

EFI_STATUS EFIAPI
efi_main(EFI_HANDLE Image, EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_LOADED_IMAGE_PROTOCOL *Loaded = NULL;
	EFI_STATUS Status;

	gBS = SystemTable->BootServices;
	gRT = SystemTable->RuntimeServices;
	mBinding.ImageHandle = Image;
	mFaceImage = Image;
	mBinding.DriverBindingHandle = Image;
	Status = gBS->InstallMultipleProtocolInterfaces(&mBinding.DriverBindingHandle,
	    &gEfiDriverBindingProtocolGuid, &mBinding,
	    &gEfiComponentName2ProtocolGuid, &mName, NULL);
	if (EFI_ERROR(Status))
		return Status;
	if (!EFI_ERROR(gBS->HandleProtocol(Image, &gEfiLoadedImageProtocolGuid,
	    (VOID **)&Loaded)) && Loaded != NULL)
		Loaded->Unload = Unload;
	return EFI_SUCCESS;
}
