/*
 * Charon Wi-Fi driver, UEFI side: EFI_WIRELESS_MAC_CONNECTION_II_PROTOCOL
 * and EFI_SUPPLICANT_PROTOCOL as Charon's Network view uses them
 * (src/efi/CharonWsNet.h WifiScan / WifiJoin), the firmware file reader and
 * the random number source.  Included by wifi_uefi.c (a copy of
 * src/netdrv/rge-uefi/rge_uefi.c) after its Simple Network Protocol code.
 *
 * EXPERIMENTAL: WRITTEN TO THE UEFI SPECIFICATION AND NEVER EXERCISED - no
 * emulator has the card, so none of these functions has ever been called.
 * One adapter at a time (the state below is global).
 *
 * Copyright (c) 2026 Charon project.  ISC licence (see wifi_kern.c).
 */
static EFI_GUID mWifi2Guid = EFI_WIRELESS_MAC_CONNECTION_II_PROTOCOL_GUID;
static EFI_GUID mSupplicantGuid = EFI_SUPPLICANT_PROTOCOL_GUID;
static EFI_GUID mRngGuid = EFI_RNG_PROTOCOL_GUID;
static EFI_GUID mSfsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID mFileInfoGuid = EFI_FILE_INFO_ID;
static EFI_HANDLE mFaceImage;

#define FACE_MAX_NETWORKS	32
#define FACE_SCAN_MIN_TICKS	150	/* 1.5 s: let a scan cover the bands */
#define FACE_SCAN_MAX_TICKS	1000	/* 10 s */

static struct {
	RGE_DEV				*Dev;
	EFI_80211_GET_NETWORKS_TOKEN	*ScanToken;
	UINT32				 ScanTicks;
	EFI_80211_CONNECT_NETWORK_TOKEN	*ConnToken;
	UINT32				 ConnTicks, ConnLimit;
	BOOLEAN				 SawAssociation;
	CHAR8				 Password[64];
	UINTN				 PasswordLen;
	struct wifi_bss			 Bss[FACE_MAX_NETWORKS];
} mFace;

/* ---- firmware files: \EFI\HAK\firmware\<name> ------------------------- */
static EFI_STATUS
ReadFileFrom(EFI_HANDLE Volume, CHAR16 *Path, VOID **Buffer, UINTN *Length)
{
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Sfs = NULL;
	EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
	UINT8 InfoBuffer[sizeof(EFI_FILE_INFO) + 256];
	EFI_FILE_INFO *Info = (EFI_FILE_INFO *)InfoBuffer;
	UINTN Size = sizeof(InfoBuffer);
	VOID *Data = NULL;
	EFI_STATUS Status;

	Status = gBS->HandleProtocol(Volume, &mSfsGuid, (VOID **)&Sfs);
	if (EFI_ERROR(Status) || Sfs == NULL)
		return EFI_NOT_FOUND;
	Status = Sfs->OpenVolume(Sfs, &Root);
	if (EFI_ERROR(Status) || Root == NULL)
		return EFI_NOT_FOUND;
	Status = Root->Open(Root, &File, Path, EFI_FILE_MODE_READ, 0);
	Root->Close(Root);
	if (EFI_ERROR(Status) || File == NULL)
		return EFI_NOT_FOUND;
	Status = File->GetInfo(File, &mFileInfoGuid, &Size, Info);
	if (!EFI_ERROR(Status) && (Info->FileSize == 0 ||
	    Info->FileSize > 16 * 1024 * 1024))
		Status = EFI_BAD_BUFFER_SIZE;
	if (!EFI_ERROR(Status)) {
		Size = (UINTN)Info->FileSize;
		Status = gBS->AllocatePool(EfiBootServicesData, Size, &Data);
	}
	if (!EFI_ERROR(Status)) {
		*Length = Size;
		Status = File->Read(File, &Size, Data);
		if (!EFI_ERROR(Status) && Size != *Length)
			Status = EFI_DEVICE_ERROR;
	}
	File->Close(File);
	if (EFI_ERROR(Status)) {
		if (Data != NULL)
			gBS->FreePool(Data);
		return Status;
	}
	*Buffer = Data;
	return EFI_SUCCESS;
}

int
wifi_host_loadfile(const char *name, void **buf, size_t *len)
{
	static const CHAR16 Folder[] = L"\\EFI\\HAK\\firmware\\";
	EFI_LOADED_IMAGE_PROTOCOL *Loaded = NULL;
	EFI_HANDLE *Handles = NULL;
	CHAR16 Path[96];
	UINTN At = 0, Count = 0, Length = 0, i;
	VOID *Data = NULL;

	while (Folder[At] != 0) {
		Path[At] = Folder[At];
		At++;
	}
	for (i = 0; name[i] != '\0' && At + 1 < sizeof(Path) / sizeof(Path[0]); i++)
		Path[At++] = (CHAR16)(UINT8)name[i];
	Path[At] = 0;
	/* the volume this driver was loaded from first */
	if (mFaceImage != NULL && !EFI_ERROR(gBS->HandleProtocol(mFaceImage,
	    &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded)) &&
	    Loaded != NULL && Loaded->DeviceHandle != NULL &&
	    !EFI_ERROR(ReadFileFrom(Loaded->DeviceHandle, Path, &Data, &Length)))
		goto found;
	if (EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, &mSfsGuid, NULL,
	    &Count, &Handles)) || Handles == NULL)
		return -1;
	for (i = 0; i < Count; i++)
		if (!EFI_ERROR(ReadFileFrom(Handles[i], Path, &Data, &Length)))
			break;
	gBS->FreePool(Handles);
	if (i == Count)
		return -1;
found:
	*buf = Data;
	*len = (size_t)Length;
	return 0;
}

void
wifi_host_freefile(void *buf)
{
	if (buf != NULL)
		gBS->FreePool(buf);
}

/* ---- random numbers ---------------------------------------------------- */
void
wifi_host_random(void *buf, size_t len)
{
	static BOOLEAN Warned;
	static UINT64 State;
	EFI_RNG_PROTOCOL *Rng = NULL;
	UINT8 *Out = buf;
	size_t i;

	if (!mExited && !EFI_ERROR(gBS->LocateProtocol(&mRngGuid, NULL,
	    (VOID **)&Rng)) && Rng != NULL &&
	    !EFI_ERROR(Rng->GetRNG(Rng, NULL, (UINTN)len, Out)))
		return;
	/* NOT cryptographic: the handshake nonce deserves better. */
	if (!Warned) {
		Warned = TRUE;
		rge_host_log("mwx0: WARNING: the firmware has no random number "
		    "protocol; weak numbers are used for the WPA2 handshake\n");
	}
	for (i = 0; i < len; i++) {
		State ^= __builtin_ia32_rdtsc() + 0x9e3779b97f4a7c15ULL;
		State ^= State << 13;
		State ^= State >> 7;
		State ^= State << 17;
		Out[i] = (UINT8)(State >> 32);
	}
}

/* ---- EFI_WIRELESS_MAC_CONNECTION_II_PROTOCOL --------------------------- */
static VOID
PutSuite(EFI_80211_SUITE_SELECTOR *Suite, UINT8 Type)
{
	Suite->Oui[0] = 0x00;		/* 00-0F-AC: IEEE 802.11 */
	Suite->Oui[1] = 0x0f;
	Suite->Oui[2] = 0xac;
	Suite->SuiteType = Type;
}

/* The whole answer in ONE pool block (the caller frees Token->Result only):
 * the descriptions, then per network an AKM list and a cipher list. */
static EFI_80211_GET_NETWORKS_RESULT *
BuildNetworks(unsigned Count)
{
	const UINTN ListSize = sizeof(UINT16) + 6 +
	    4 * sizeof(EFI_80211_SUITE_SELECTOR);	/* padded, 4 suites */
	EFI_80211_GET_NETWORKS_RESULT *Result = NULL;
	UINTN Head = sizeof(*Result) +
	    Count * sizeof(EFI_80211_NETWORK_DESCRIPTION);
	UINT8 *Lists;
	unsigned i;

	Head = (Head + 7) & ~(UINTN)7;
	if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData,
	    Head + Count * 2 * ListSize + 8, (VOID **)&Result)) || Result == NULL)
		return NULL;
	gBS->SetMem(Result, Head + Count * 2 * ListSize + 8, 0);
	Lists = (UINT8 *)Result + Head;
	Result->NumOfNetworkDesc = (UINT8)Count;
	for (i = 0; i < Count; i++) {
		EFI_80211_NETWORK_DESCRIPTION *Desc = &Result->NetworkDesc[i];
		struct wifi_bss *Bss = &mFace.Bss[i];
		EFI_80211_AKM_SUITE_SELECTOR *Akm =
		    (VOID *)(Lists + i * 2 * ListSize);
		EFI_80211_CIPHER_SUITE_SELECTOR *Cipher =
		    (VOID *)(Lists + i * 2 * ListSize + ListSize);

		Desc->Network.BSSType = IeeeInfrastructureBSS;
		Desc->Network.SSId.SSIdLen = Bss->ssid_len;
		gBS->CopyMem(Desc->Network.SSId.SSId, Bss->ssid, Bss->ssid_len);
		Desc->NetworkQuality = Bss->quality;
		if (Bss->akms == 0)
			continue;		/* open: no lists */
		if (Bss->akms & WIFI_AKM_PSK)
			PutSuite(&Akm->AKMSuiteList[Akm->AKMSuiteCount++], 2);
		if (Bss->akms & WIFI_AKM_PSK_SHA256)
			PutSuite(&Akm->AKMSuiteList[Akm->AKMSuiteCount++], 6);
		if (Bss->akms & WIFI_AKM_8021X)
			PutSuite(&Akm->AKMSuiteList[Akm->AKMSuiteCount++], 1);
		if (Bss->akms & WIFI_AKM_SAE)
			PutSuite(&Akm->AKMSuiteList[Akm->AKMSuiteCount++], 8);
		if (Akm->AKMSuiteCount == 0)	/* WEP, unknown: "secured" */
			PutSuite(&Akm->AKMSuiteList[Akm->AKMSuiteCount++], 0);
		Desc->Network.AKMSuite = Akm;
		if (Bss->ciphers & WIFI_CIPHER_CCMP)
			PutSuite(&Cipher->CipherSuiteList[Cipher->CipherSuiteCount++], 4);
		if (Bss->ciphers & WIFI_CIPHER_TKIP)
			PutSuite(&Cipher->CipherSuiteList[Cipher->CipherSuiteCount++], 2);
		if (Cipher->CipherSuiteCount != 0)
			Desc->Network.CipherSuite = Cipher;
	}
	return Result;
}

static EFI_STATUS EFIAPI
FaceGetNetworks(EFI_WIRELESS_MAC_CONNECTION_II_PROTOCOL *This,
    EFI_80211_GET_NETWORKS_TOKEN *Token)
{
	RGE_DEV *Dev = mFace.Dev;
	EFI_STATUS Status = EFI_SUCCESS;
	EFI_TPL OldTpl;

	if (This == NULL || Token == NULL || Token->Event == NULL)
		return EFI_INVALID_PARAMETER;
	if (Dev == NULL || Dev->Port == NULL || mExited)
		return EFI_NOT_READY;
	if (!Enter(Dev, &OldTpl))
		return EFI_ACCESS_DENIED;
	if (mFace.ScanToken != NULL)
		Status = EFI_ALREADY_STARTED;
	else if (wifi_port_scan_start(Dev->Port) != 0) {
		rge_host_log("mwx0: the scan could not be started\n");
		Status = EFI_DEVICE_ERROR;
	} else {
		Token->Status = EFI_NOT_READY;
		Token->Result = NULL;
		mFace.ScanToken = Token;
		mFace.ScanTicks = 0;
		rge_host_log("mwx0: scan started\n");
	}
	Leave(Dev, OldTpl);
	return Status;
}

static EFI_STATUS EFIAPI
FaceConnectNetwork(EFI_WIRELESS_MAC_CONNECTION_II_PROTOCOL *This,
    EFI_80211_CONNECT_NETWORK_TOKEN *Token)
{
	RGE_DEV *Dev = mFace.Dev;
	EFI_80211_NETWORK *Network;
	EFI_STATUS Status = EFI_SUCCESS;
	EFI_TPL OldTpl;
	UINT8 Psk[32];
	BOOLEAN Secure;

	if (This == NULL || Token == NULL || Token->Event == NULL ||
	    Token->Data == NULL || Token->Data->Network == NULL)
		return EFI_INVALID_PARAMETER;
	Network = Token->Data->Network;
	if (Network->SSId.SSIdLen == 0 || Network->SSId.SSIdLen > EFI_MAX_SSID_LEN)
		return EFI_INVALID_PARAMETER;
	if (Dev == NULL || Dev->Port == NULL || mExited)
		return EFI_NOT_READY;
	Secure = Network->AKMSuite != NULL && Network->AKMSuite->AKMSuiteCount != 0;
	if (Secure && mFace.PasswordLen < 8)
		return EFI_NOT_FOUND;	/* no password given to the supplicant */
	if (!Enter(Dev, &OldTpl))
		return EFI_ACCESS_DENIED;
	if (mFace.ConnToken != NULL)
		Status = EFI_ALREADY_STARTED;
	else {
		if (Secure)
			wifi_pbkdf2_sha1(mFace.Password, mFace.PasswordLen,
			    Network->SSId.SSId, Network->SSId.SSIdLen, Psk);
		if (wifi_port_connect(Dev->Port, Network->SSId.SSId,
		    Network->SSId.SSIdLen, Secure ? Psk : NULL) != 0) {
			rge_host_log("mwx0: joining could not be started\n");
			Status = EFI_DEVICE_ERROR;
		} else {
			Token->Status = EFI_NOT_READY;
			mFace.ConnToken = Token;
			mFace.ConnTicks = 0;
			mFace.SawAssociation = FALSE;
			mFace.ConnLimit = Token->Data->FailureTimeout;
			if (mFace.ConnLimit < 10)
				mFace.ConnLimit = 10;
			if (mFace.ConnLimit > 60)
				mFace.ConnLimit = 60;
			mFace.ConnLimit *= 100;
			rge_host_log("mwx0: joining started\n");
		}
		gBS->SetMem(Psk, sizeof(Psk), 0);
		gBS->SetMem(mFace.Password, sizeof(mFace.Password), 0);
		mFace.PasswordLen = 0;
	}
	Leave(Dev, OldTpl);
	return Status;
}

static EFI_STATUS EFIAPI
FaceDisconnectNetwork(EFI_WIRELESS_MAC_CONNECTION_II_PROTOCOL *This,
    EFI_80211_DISCONNECT_NETWORK_TOKEN *Token)
{
	RGE_DEV *Dev = mFace.Dev;
	EFI_TPL OldTpl;

	if (This == NULL || Token == NULL)
		return EFI_INVALID_PARAMETER;
	if (Dev == NULL || Dev->Port == NULL || mExited)
		return EFI_NOT_READY;
	if (!Enter(Dev, &OldTpl))
		return EFI_ACCESS_DENIED;
	wifi_port_disconnect(Dev->Port);
	Token->Status = EFI_SUCCESS;
	Leave(Dev, OldTpl);
	if (Token->Event != NULL)
		gBS->SignalEvent(Token->Event);
	return EFI_SUCCESS;
}

static EFI_WIRELESS_MAC_CONNECTION_II_PROTOCOL mWifi2 = {
	FaceGetNetworks, FaceConnectNetwork, FaceDisconnectNetwork
};

/* Every 10 ms from the timer event, with the device marked busy. */
static VOID
WifiFaceTick(RGE_DEV *Dev)
{
	mFace.Dev = Dev;
	if (mFace.ScanToken != NULL) {
		EFI_80211_GET_NETWORKS_TOKEN *Token = mFace.ScanToken;

		mFace.ScanTicks++;
		if ((mFace.ScanTicks >= FACE_SCAN_MIN_TICKS &&
		    wifi_port_scan_done(Dev->Port)) ||
		    mFace.ScanTicks >= FACE_SCAN_MAX_TICKS) {
			unsigned Count = wifi_port_scan_results(Dev->Port,
			    mFace.Bss, FACE_MAX_NETWORKS);

			mFace.ScanToken = NULL;
			Token->Result = BuildNetworks(Count);
			Token->Status = Token->Result != NULL ? EFI_SUCCESS :
			    EFI_OUT_OF_RESOURCES;
			rge_host_log(Count != 0 ? "mwx0: scan finished\n" :
			    "mwx0: scan finished, no network heard\n");
			gBS->SignalEvent(Token->Event);
		}
	}
	if (mFace.ConnToken != NULL) {
		EFI_80211_CONNECT_NETWORK_TOKEN *Token = mFace.ConnToken;
		int State = wifi_port_state(Dev->Port);

		mFace.ConnTicks++;
		if (State >= 1)
			mFace.SawAssociation = TRUE;
		if (State == 2) {
			mFace.ConnToken = NULL;
			Token->ResultCode = ConnectSuccess;
			Token->Status = EFI_SUCCESS;
			Dev->Mode.MediaPresent = TRUE;
			rge_host_log("mwx0: joined, keys installed\n");
			gBS->SignalEvent(Token->Event);
		} else if (mFace.ConnTicks >= mFace.ConnLimit) {
			mFace.ConnToken = NULL;
			/* associated but no keys: the 4-way handshake failed,
			 * which is what a wrong password looks like */
			Token->ResultCode = mFace.SawAssociation ?
			    ConnectFailed : ConnectFailureTimeout;
			Token->Status = EFI_SUCCESS;
			rge_host_log(mFace.SawAssociation ?
			    "mwx0: associated but the key handshake failed "
			    "(wrong password?)\n" :
			    "mwx0: the network did not answer in time\n");
			gBS->SignalEvent(Token->Event);
		}
	}
}

static VOID
WifiFaceRelease(RGE_DEV *Dev)
{
	if (mFace.Dev != Dev)
		return;
	/* pending requests end as aborted; their events are not signalled
	 * from here (the adapter is going away under the caller) */
	if (mFace.ScanToken != NULL)
		mFace.ScanToken->Status = EFI_ABORTED;
	if (mFace.ConnToken != NULL)
		mFace.ConnToken->Status = EFI_ABORTED;
	gBS->SetMem(&mFace, sizeof(mFace), 0);
}

/* ---- EFI_SUPPLICANT_PROTOCOL: only what carries the password ---------- */
static EFI_STATUS EFIAPI
SupBuildResponsePacket(EFI_SUPPLICANT_PROTOCOL *This, UINT8 *Request,
    UINTN RequestSize, UINT8 *Buffer, UINTN *BufferSize)
{
	/* The 4-way handshake runs inside net80211 (ieee80211_pae_*.c). */
	(VOID)This; (VOID)Request; (VOID)RequestSize; (VOID)Buffer;
	(VOID)BufferSize;
	return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI
SupProcessPacket(EFI_SUPPLICANT_PROTOCOL *This,
    EFI_SUPPLICANT_FRAGMENT_DATA **FragmentTable, UINT32 *FragmentCount,
    EFI_SUPPLICANT_CRYPT_MODE CryptMode)
{
	/* Frames are encrypted and decrypted inside net80211 (CCMP). */
	(VOID)This; (VOID)FragmentTable; (VOID)FragmentCount; (VOID)CryptMode;
	return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI
SupSetData(EFI_SUPPLICANT_PROTOCOL *This, EFI_SUPPLICANT_DATA_TYPE DataType,
    VOID *Data, UINTN DataSize)
{
	const CHAR8 *Text = Data;
	UINTN Length;

	if (This == NULL)
		return EFI_INVALID_PARAMETER;
	switch (DataType) {
	case EfiSupplicant80211PskPassword:
		if (Data == NULL || DataSize == 0) {
			gBS->SetMem(mFace.Password, sizeof(mFace.Password), 0);
			mFace.PasswordLen = 0;
			return EFI_SUCCESS;
		}
		for (Length = 0; Length < DataSize && Text[Length] != '\0'; Length++)
			;
		if (Length < 8 || Length > 63)
			return EFI_INVALID_PARAMETER;
		gBS->SetMem(mFace.Password, sizeof(mFace.Password), 0);
		gBS->CopyMem(mFace.Password, Data, Length);
		mFace.PasswordLen = Length;
		return EFI_SUCCESS;
	case EfiSupplicant80211AKMSuite:
	case EfiSupplicant80211GroupDataCipherSuite:
	case EfiSupplicant80211PairwiseCipherSuite:
	case EfiSupplicant80211TargetSSIDName:
		/* accepted; the network of ConnectNetwork() decides, and the
		 * stack negotiates WPA2-PSK with CCMP only */
		return EFI_SUCCESS;
	default:
		return EFI_UNSUPPORTED;
	}
}

static EFI_STATUS EFIAPI
SupGetData(EFI_SUPPLICANT_PROTOCOL *This, EFI_SUPPLICANT_DATA_TYPE DataType,
    UINT8 *Data, UINTN *DataSize)
{
	(VOID)This; (VOID)DataType; (VOID)Data; (VOID)DataSize;
	return EFI_UNSUPPORTED;
}

static EFI_SUPPLICANT_PROTOCOL mSupplicant = {
	SupBuildResponsePacket, SupProcessPacket, SupSetData, SupGetData
};
