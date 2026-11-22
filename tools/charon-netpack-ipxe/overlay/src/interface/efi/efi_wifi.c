/*
 * Copyright (C) 2026 Charon project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

FILE_LICENCE ( GPL2_OR_LATER );

/** @file
 *
 * UEFI Wi-Fi face for iPXE's 802.11 devices (Charon driver pack)
 *
 * NEVER RUN ON HARDWARE: no emulator offers a card that iPXE's 802.11
 * drivers support.  Written against the UEFI specification's EFI
 * Wireless MAC Connection II and EFI Supplicant protocols as edk2
 * declares them (Protocol/WiFi2.h, Protocol/Supplicant.h) and against
 * how Charon calls them (CharonWsNet.h).
 *
 * When an iPXE UEFI driver image contains an 802.11 driver, this file
 * publishes, on the handle that carries the Simple Network Protocol:
 *
 *  - EFI_WIRELESS_MAC_CONNECTION_II_PROTOCOL: GetNetworks (passive
 *    scan through net80211_probe_*), ConnectNetwork (sets iPXE's
 *    "ssid" and "key" settings of the device and starts iPXE's
 *    association with WPA/WPA2-PSK handshake), DisconnectNetwork;
 *  - EFI_SUPPLICANT_PROTOCOL: SetData for the network name and the
 *    PSK pass phrase only (the handshake itself is iPXE's, wpa.c).
 *
 * The device never joins a network by itself (NET80211_NO_ASSOC):
 * without this iPXE would associate with the strongest open network
 * as soon as the firmware's IPv4 stack initialises the adapter.
 *
 * In a driver image nothing runs iPXE's scheduler, so a periodic
 * timer (20 ms) calls step() - scan, association and the handshake
 * are iPXE processes - and moves received frames to the SNP queue;
 * EAPOL frames are handed to iPXE's own EAPOL handler instead
 * (efi_snp_rx_hook), because the firmware's network stack would
 * otherwise swallow the handshake.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <byteswap.h>
#include <ipxe/netdevice.h>
#include <ipxe/iobuf.h>
#include <ipxe/if_ether.h>
#include <ipxe/list.h>
#include <ipxe/process.h>
#include <ipxe/timer.h>
#include <ipxe/settings.h>
#include <ipxe/ieee80211.h>
#include <ipxe/net80211.h>
#include <ipxe/efi/efi.h>
#include <ipxe/efi/Protocol/WiFi2.h>
#include <ipxe/efi/Protocol/Supplicant.h>
#include <ipxe/efi/efi_wifi.h>

/** Timer period (in 100ns units) */
#define EFI_WIFI_PERIOD 200000

/** Maximum number of received frames kept for the SNP consumer */
#define EFI_WIFI_MAX_RX 128

/** Maximum number of networks reported by a scan */
#define EFI_WIFI_MAX_NETWORKS 64

/** Default connection timeout (in seconds) */
#define EFI_WIFI_TIMEOUT 20

/** Failed association attempts before a connection is given up */
#define EFI_WIFI_MAX_FAILURES 2

/** SSID setting (defined in net80211.c) */
extern const struct setting
net80211_ssid_setting __setting ( SETTING_NETDEV_EXTRA, ssid );

/** A Wi-Fi device */
struct efi_wifi {
	/** Network device */
	struct net_device *netdev;
	/** 802.11 device */
	struct net80211_device *dev;
	/** Handle the protocols are installed on (the SNP handle) */
	EFI_HANDLE handle;
	/** Wireless MAC connection protocol */
	EFI_WIRELESS_MAC_CONNECTION_II_PROTOCOL wifi2;
	/** Supplicant protocol */
	EFI_SUPPLICANT_PROTOCOL supplicant;
	/** Periodic timer */
	EFI_EVENT timer;
	/** Scan in progress */
	struct net80211_probe_ctx *scan;
	/** Token of the scan in progress */
	EFI_80211_GET_NETWORKS_TOKEN *scan_token;
	/** Channel to return to after a scan while associated */
	int scan_channel;
	/** Token of the connection attempt in progress */
	EFI_80211_CONNECT_NETWORK_TOKEN *connect_token;
	/** Connection deadline (in ticks) */
	unsigned long connect_deadline;
	/** Association attempt has passed the probe stage */
	int connect_probed;
	/** Failed association attempts */
	unsigned int connect_failures;
	/** Network name set through the supplicant protocol */
	char ssid[ IEEE80211_MAX_SSID_LEN + 1 ];
	/** Pass phrase set through the supplicant protocol */
	char key[65];
};



/**
 * Hand EAPOL frames to iPXE's own handler
 *
 * @v netdev		Network device
 * @v iobuf		Received frame (802.11 data frame)
 * @ret consumed	Frame was consumed
 */
static int efi_wifi_rx ( struct net_device *netdev, struct io_buffer *iobuf ) {
	struct ieee80211_llc_snap_header *lhdr;
	const void *ll_dest;
	const void *ll_source;
	uint16_t net_proto;
	unsigned int flags;

	/* Ignore anything other than 802.11 devices */
	if ( ! net80211_get ( netdev ) )
		return 0;

	/* Ignore anything other than EAPOL */
	if ( iob_len ( iobuf ) < ( IEEE80211_TYP_FRAME_HEADER_LEN +
				   IEEE80211_LLC_HEADER_LEN ) )
		return 0;
	lhdr = ( iobuf->data + IEEE80211_TYP_FRAME_HEADER_LEN );
	if ( lhdr->ethertype != htons ( ETH_P_EAPOL ) )
		return 0;

	/* Hand to iPXE's network stack */
	if ( netdev->ll_protocol->pull ( netdev, iobuf, &ll_dest, &ll_source,
					 &net_proto, &flags ) != 0 ) {
		free_iob ( iobuf );
		return 1;
	}
	net_rx ( iobuf, netdev, net_proto, ll_dest, ll_source, flags );
	return 1;
}

/**
 * Open network device if necessary
 *
 * @v wifi		Wi-Fi device
 * @ret rc		Return status code
 */
static int efi_wifi_open ( struct efi_wifi *wifi ) {

	/* Never join a network unasked */
	wifi->dev->state |= NET80211_NO_ASSOC;
	if ( netdev_is_open ( wifi->netdev ) )
		return 0;
	return netdev_open ( wifi->netdev );
}

/**
 * Abandon any association
 *
 * @v wifi		Wi-Fi device
 */
static void efi_wifi_idle ( struct efi_wifi *wifi ) {
	struct settings *settings = netdev_settings ( wifi->netdev );

	/* Forget the network; closing stops iPXE's association process */
	delete_setting ( settings, &net80211_key_setting );
	delete_setting ( settings, &net80211_ssid_setting );
	wifi->dev->state |= NET80211_NO_ASSOC;
	if ( netdev_is_open ( wifi->netdev ) ) {
		netdev_close ( wifi->netdev );
		netdev_open ( wifi->netdev );
	}
}

/**
 * Complete a scan
 *
 * @v wifi		Wi-Fi device
 */
static void efi_wifi_scan_done ( struct efi_wifi *wifi ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	EFI_80211_GET_NETWORKS_TOKEN *token = wifi->scan_token;
	EFI_80211_GET_NETWORKS_RESULT *result = NULL;
	EFI_80211_NETWORK_DESCRIPTION *desc;
	EFI_80211_AKM_SUITE_SELECTOR *akm;
	EFI_80211_CIPHER_SUITE_SELECTOR *cipher;
	struct net80211_device *dev = wifi->dev;
	struct list_head *networks;
	struct net80211_wlan *wlan;
	unsigned int count = 0;
	unsigned int i = 0;
	size_t suites_offset;
	size_t len;
	void *data;
	int quality;
	EFI_STATUS efirc;

	/* Collect networks */
	networks = net80211_probe_finish_all ( wifi->scan );
	wifi->scan = NULL;
	wifi->scan_token = NULL;
	if ( dev->state & NET80211_ASSOCIATED )
		net80211_change_channel ( dev, wifi->scan_channel );

	/* Count named networks */
	list_for_each_entry ( wlan, networks, list ) {
		if ( wlan->essid[0] && ( count < EFI_WIFI_MAX_NETWORKS ) )
			count++;
	}

	/* Allocate result: header, descriptions, then two suite
	 * lists (8 bytes each) per network, all in one pool block
	 * (the caller frees only the result pointer).
	 */
	suites_offset = ( offsetof ( typeof ( *result ), NetworkDesc ) +
			  ( ( count ? count : 1 ) * sizeof ( *desc ) ) );
	len = ( suites_offset + ( count * 16 ) );
	if ( ( efirc = bs->AllocatePool ( EfiBootServicesData, len,
					  &data ) ) != 0 ) {
		net80211_free_wlanlist ( networks );
		token->Result = NULL;
		token->Status = efirc;
		bs->SignalEvent ( token->Event );
		return;
	}
	memset ( data, 0, len );
	result = data;

	/* Describe networks */
	list_for_each_entry ( wlan, networks, list ) {
		if ( ( ! wlan->essid[0] ) || ( i >= count ) )
			continue;
		desc = &result->NetworkDesc[i];
		akm = ( data + suites_offset + ( i * 16 ) );
		cipher = ( data + suites_offset + ( i * 16 ) + 8 );
		desc->Network.BSSType = IeeeInfrastructureBSS;
		desc->Network.SSId.SSIdLen = strlen ( wlan->essid );
		memcpy ( desc->Network.SSId.SSId, wlan->essid,
			 desc->Network.SSId.SSIdLen );
		quality = ( ( wlan->signal < 0 ) ?
			    ( 100 + wlan->signal ) : wlan->signal );
		if ( quality < 0 )
			quality = 0;
		if ( quality > 100 )
			quality = 100;
		desc->NetworkQuality = quality;

		/* Authentication and key management (00-0F-AC:n):
		 * 1 for 802.1X, 2 for a pre-shared key; none for an
		 * open network.  WEP has no AKM suite: report the
		 * reserved type 0 so that the network is not taken
		 * for an open one.
		 */
		if ( ( wlan->handshaking != NET80211_SECPROT_NONE ) ||
		     ( wlan->crypto != NET80211_CRYPT_NONE ) ) {
			akm->AKMSuiteCount = 1;
			akm->AKMSuiteList[0].Oui[0] = 0x00;
			akm->AKMSuiteList[0].Oui[1] = 0x0f;
			akm->AKMSuiteList[0].Oui[2] = 0xac;
			akm->AKMSuiteList[0].SuiteType =
				( ( wlan->handshaking == NET80211_SECPROT_PSK )
				  ? 2 : ( ( wlan->handshaking ==
					    NET80211_SECPROT_EAP ) ? 1 : 0 ) );
			desc->Network.AKMSuite = akm;
		}

		/* Cipher (00-0F-AC:n): 1 WEP-40, 2 TKIP, 4 CCMP */
		if ( wlan->crypto != NET80211_CRYPT_NONE ) {
			cipher->CipherSuiteCount = 1;
			cipher->CipherSuiteList[0].Oui[0] = 0x00;
			cipher->CipherSuiteList[0].Oui[1] = 0x0f;
			cipher->CipherSuiteList[0].Oui[2] = 0xac;
			cipher->CipherSuiteList[0].SuiteType =
				( ( wlan->crypto == NET80211_CRYPT_CCMP ) ? 4 :
				  ( ( wlan->crypto == NET80211_CRYPT_TKIP ) ? 2 :
				    ( ( wlan->crypto == NET80211_CRYPT_WEP )
				      ? 1 : 0 ) ) );
			desc->Network.CipherSuite = cipher;
		}
		i++;
	}
	result->NumOfNetworkDesc = i;
	net80211_free_wlanlist ( networks );

	/* Complete token */
	token->Result = result;
	token->Status = 0;
	bs->SignalEvent ( token->Event );
}

/**
 * Complete a connection attempt
 *
 * @v wifi		Wi-Fi device
 * @v code		Result code
 */
static void efi_wifi_connect_done ( struct efi_wifi *wifi,
				    EFI_80211_CONNECT_NETWORK_RESULT_CODE code){
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	EFI_80211_CONNECT_NETWORK_TOKEN *token = wifi->connect_token;

	wifi->connect_token = NULL;
	if ( code != ConnectSuccess )
		efi_wifi_idle ( wifi );
	token->ResultCode = code;
	token->Status = 0;
	bs->SignalEvent ( token->Event );
}

/**
 * Watch a connection attempt
 *
 * @v wifi		Wi-Fi device
 */
static void efi_wifi_connect_step ( struct efi_wifi *wifi ) {
	struct net80211_device *dev = wifi->dev;

	/* Associated, keys in place, association process finished */
	if ( ( dev->state & NET80211_ASSOCIATED ) &&
	     ( dev->state & NET80211_CRYPTO_SYNCED ) &&
	     ( ! ( dev->state & NET80211_WORKING ) ) &&
	     netdev_link_ok ( wifi->netdev ) ) {
		efi_wifi_connect_done ( wifi, ConnectSuccess );
		return;
	}

	/* iPXE retries a failed association for ever and resets its
	 * error on each retry.  A failure after the network was found
	 * (refused, or the handshake failed: wrong pass phrase) shows
	 * as the "probed" state being lost again.
	 */
	if ( dev->state & NET80211_PROBED ) {
		wifi->connect_probed = 1;
	} else if ( wifi->connect_probed ) {
		wifi->connect_probed = 0;
		if ( ++wifi->connect_failures >= EFI_WIFI_MAX_FAILURES ) {
			efi_wifi_connect_done ( wifi, ConnectFailed );
			return;
		}
	}

	/* Give up at the caller's deadline */
	if ( ( ( long ) ( currticks() - wifi->connect_deadline ) ) > 0 )
		efi_wifi_connect_done ( wifi, ConnectFailureTimeout );
}

/**
 * Install protocols on the SNP handle, once it exists
 *
 * @v wifi		Wi-Fi device
 */
static void efi_wifi_install ( struct efi_wifi *wifi ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	EFI_HANDLE handle = efi_snp_netdev_handle ( wifi->netdev );
	EFI_STATUS efirc;

	if ( ! handle )
		return;
	if ( ( efirc = bs->InstallMultipleProtocolInterfaces (
			&handle,
			&efi_wifi2_protocol_guid, &wifi->wifi2,
			&efi_supplicant_protocol_guid, &wifi->supplicant,
			NULL ) ) != 0 ) {
		DBGC ( wifi, "WIFI %s could not install protocols: %s\n",
		       wifi->netdev->name, strerror ( -EEFI ( efirc ) ) );
		return;
	}
	wifi->handle = handle;
}

/**
 * Periodic timer
 *
 * @v event		Timer event
 * @v context		Wi-Fi device
 */
static VOID EFIAPI efi_wifi_tick ( EFI_EVENT event __unused, VOID *context ) {
	struct efi_wifi *wifi = context;
	struct efi_saved_tpl tpl;

	efi_raise_tpl ( &tpl );

	/* Publish protocols as soon as the SNP handle exists */
	if ( ! wifi->handle )
		efi_wifi_install ( wifi );

	if ( netdev_is_open ( wifi->netdev ) ) {

		/* Run iPXE's processes (association, handshake, timers) */
		step();

		/* Sort received frames: EAPOL to iPXE, the rest to SNP */
		efi_snp_netdev_poll ( wifi->netdev, EFI_WIFI_MAX_RX );

		/* Progress scan */
		if ( wifi->scan && ( net80211_probe_step ( wifi->scan ) != 0 ) )
			efi_wifi_scan_done ( wifi );

		/* Progress connection attempt */
		if ( wifi->connect_token )
			efi_wifi_connect_step ( wifi );
	}

	efi_restore_tpl ( &tpl );
}

/**
 * Scan for networks
 *
 * @v wifi2		Wireless MAC connection protocol
 * @v token		Token
 * @ret efirc		EFI status code
 */
static EFI_STATUS EFIAPI
efi_wifi_get_networks ( EFI_WIRELESS_MAC_CONNECTION_II_PROTOCOL *wifi2,
			EFI_80211_GET_NETWORKS_TOKEN *token ) {
	struct efi_wifi *wifi = container_of ( wifi2, struct efi_wifi, wifi2 );
	struct net80211_device *dev = wifi->dev;
	struct efi_saved_tpl tpl;
	int rc;

	/* Sanity checks */
	if ( ( ! token ) || ( ! token->Event ) )
		return EFI_INVALID_PARAMETER;
	if ( wifi->scan || wifi->connect_token )
		return EFI_ALREADY_STARTED;

	efi_raise_tpl ( &tpl );

	/* Open device */
	if ( ( rc = efi_wifi_open ( wifi ) ) != 0 )
		goto done;

	/* Refuse while an association is being made */
	if ( dev->state & NET80211_WORKING ) {
		rc = -EBUSY;
		goto done;
	}

	/* Start passive scan on all bands */
	if ( ! ( dev->state & NET80211_ASSOCIATED ) ) {
		if ( ( rc = net80211_prepare_probe ( dev, dev->hw->bands,
						     0 ) ) != 0 )
			goto done;
	}
	wifi->scan_channel = dev->channels[dev->channel].channel_nr;
	wifi->scan = net80211_probe_start ( dev, "", 0 );
	if ( ! wifi->scan ) {
		rc = -ENOMEM;
		goto done;
	}
	wifi->scan_token = token;
	rc = 0;

 done:
	efi_restore_tpl ( &tpl );
	return EFIRC ( rc );
}

/**
 * Connect to a network
 *
 * @v wifi2		Wireless MAC connection protocol
 * @v token		Token
 * @ret efirc		EFI status code
 */
static EFI_STATUS EFIAPI
efi_wifi_connect ( EFI_WIRELESS_MAC_CONNECTION_II_PROTOCOL *wifi2,
		   EFI_80211_CONNECT_NETWORK_TOKEN *token ) {
	struct efi_wifi *wifi = container_of ( wifi2, struct efi_wifi, wifi2 );
	struct settings *settings = netdev_settings ( wifi->netdev );
	EFI_80211_NETWORK *network;
	char ssid[ IEEE80211_MAX_SSID_LEN + 1 ];
	struct efi_saved_tpl tpl;
	unsigned int timeout;
	size_t ssid_len;
	int secured;
	int rc;

	/* Sanity checks */
	if ( ( ! token ) || ( ! token->Event ) || ( ! token->Data ) ||
	     ( ! token->Data->Network ) )
		return EFI_INVALID_PARAMETER;
	if ( wifi->scan || wifi->connect_token )
		return EFI_ALREADY_STARTED;
	network = token->Data->Network;
	ssid_len = network->SSId.SSIdLen;
	if ( ( ssid_len == 0 ) || ( ssid_len > IEEE80211_MAX_SSID_LEN ) )
		return EFI_INVALID_PARAMETER;
	memcpy ( ssid, network->SSId.SSId, ssid_len );
	ssid[ssid_len] = '\0';
	if ( strlen ( ssid ) != ssid_len )
		return EFI_UNSUPPORTED; /* iPXE keeps the name as a string */
	secured = ( network->AKMSuite &&
		    ( network->AKMSuite->AKMSuiteCount != 0 ) );
	if ( secured && ( ! wifi->key[0] ) )
		return EFI_NOT_READY; /* no pass phrase was set */

	efi_raise_tpl ( &tpl );

	/* Open device */
	if ( ( rc = efi_wifi_open ( wifi ) ) != 0 )
		goto done;

	/* Give iPXE the pass phrase and the name, then associate */
	if ( secured ) {
		if ( ( rc = store_setting ( settings, &net80211_key_setting,
					    wifi->key,
					    strlen ( wifi->key ) ) ) != 0 )
			goto done;
	} else {
		delete_setting ( settings, &net80211_key_setting );
	}
	if ( ( rc = store_setting ( settings, &net80211_ssid_setting, ssid,
				    ssid_len ) ) != 0 )
		goto done;
	net80211_autoassociate ( wifi->dev );

	/* Watch the attempt from the timer */
	timeout = token->Data->FailureTimeout;
	if ( ! timeout )
		timeout = EFI_WIFI_TIMEOUT;
	wifi->connect_deadline = ( currticks() + ( timeout * TICKS_PER_SEC ) );
	wifi->connect_probed = 0;
	wifi->connect_failures = 0;
	wifi->connect_token = token;
	rc = 0;

 done:
	/* The pass phrase is kept by iPXE's setting only */
	memset ( wifi->key, 0, sizeof ( wifi->key ) );
	efi_restore_tpl ( &tpl );
	return EFIRC ( rc );
}

/**
 * Disconnect from the network
 *
 * @v wifi2		Wireless MAC connection protocol
 * @v token		Token
 * @ret efirc		EFI status code
 */
static EFI_STATUS EFIAPI
efi_wifi_disconnect ( EFI_WIRELESS_MAC_CONNECTION_II_PROTOCOL *wifi2,
		      EFI_80211_DISCONNECT_NETWORK_TOKEN *token ) {
	struct efi_wifi *wifi = container_of ( wifi2, struct efi_wifi, wifi2 );
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	struct efi_saved_tpl tpl;

	/* Sanity checks */
	if ( ! token )
		return EFI_INVALID_PARAMETER;
	if ( wifi->scan || wifi->connect_token )
		return EFI_ACCESS_DENIED;

	efi_raise_tpl ( &tpl );
	efi_wifi_idle ( wifi );
	efi_restore_tpl ( &tpl );

	token->Status = 0;
	if ( token->Event )
		bs->SignalEvent ( token->Event );
	return 0;
}

/**
 * Build response packet (not provided: the handshake is iPXE's own)
 */
static EFI_STATUS EFIAPI
efi_wifi_build_response ( EFI_SUPPLICANT_PROTOCOL *supplicant __unused,
			  UINT8 *request __unused, UINTN request_len __unused,
			  UINT8 *buffer __unused, UINTN *len __unused ) {
	return EFI_UNSUPPORTED;
}

/**
 * Process packet (not provided: encryption is iPXE's own)
 */
static EFI_STATUS EFIAPI
efi_wifi_process_packet ( EFI_SUPPLICANT_PROTOCOL *supplicant __unused,
			  EFI_SUPPLICANT_FRAGMENT_DATA **fragments __unused,
			  UINT32 *count __unused,
			  EFI_SUPPLICANT_CRYPT_MODE mode __unused ) {
	return EFI_UNSUPPORTED;
}

/**
 * Set supplicant data
 *
 * @v supplicant	Supplicant protocol
 * @v type		Data type
 * @v data		Data
 * @v len		Length of data
 * @ret efirc		EFI status code
 */
static EFI_STATUS EFIAPI
efi_wifi_set_data ( EFI_SUPPLICANT_PROTOCOL *supplicant,
		    EFI_SUPPLICANT_DATA_TYPE type, VOID *data, UINTN len ) {
	struct efi_wifi *wifi =
		container_of ( supplicant, struct efi_wifi, supplicant );
	EFI_80211_SSID *ssid = data;
	const char *key = data;
	size_t key_len;

	if ( ( ! data ) || ( ! len ) )
		return EFI_INVALID_PARAMETER;
	switch ( type ) {
	case EfiSupplicant80211TargetSSIDName:
		if ( ( len < sizeof ( *ssid ) ) ||
		     ( ssid->SSIdLen > IEEE80211_MAX_SSID_LEN ) )
			return EFI_INVALID_PARAMETER;
		memset ( wifi->ssid, 0, sizeof ( wifi->ssid ) );
		memcpy ( wifi->ssid, ssid->SSId, ssid->SSIdLen );
		return 0;
	case EfiSupplicant80211PskPassword:
		/* ASCII pass phrase of 8..63 characters (or 64 hex
		 * digits), with or without the terminating NUL.
		 */
		key_len = strnlen ( key, len );
		if ( ( key_len < 8 ) || ( key_len > 64 ) )
			return EFI_INVALID_PARAMETER;
		memset ( wifi->key, 0, sizeof ( wifi->key ) );
		memcpy ( wifi->key, key, key_len );
		return 0;
	case EfiSupplicant80211AKMSuite:
	case EfiSupplicant80211PairwiseCipherSuite:
	case EfiSupplicant80211GroupDataCipherSuite:
		/* iPXE takes these from the access point's beacon */
		return 0;
	default:
		return EFI_UNSUPPORTED;
	}
}

/**
 * Get supplicant data (not provided)
 */
static EFI_STATUS EFIAPI
efi_wifi_get_data ( EFI_SUPPLICANT_PROTOCOL *supplicant __unused,
		    EFI_SUPPLICANT_DATA_TYPE type __unused,
		    UINT8 *data __unused, UINTN *len __unused ) {
	return EFI_UNSUPPORTED;
}

/**
 * Create Wi-Fi device
 *
 * @v netdev		Network device
 * @v priv		Private data
 * @ret rc		Return status code
 */
static int efi_wifi_probe ( struct net_device *netdev, void *priv ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	struct net80211_device *dev = net80211_get ( netdev );
	struct efi_wifi *wifi = priv;
	EFI_STATUS efirc;

	/* Ignore anything other than 802.11 devices */
	if ( ! dev )
		return 0;

	/* Initialise structure */
	wifi->netdev = netdev;
	wifi->dev = dev;
	wifi->wifi2.GetNetworks = efi_wifi_get_networks;
	wifi->wifi2.ConnectNetwork = efi_wifi_connect;
	wifi->wifi2.DisconnectNetwork = efi_wifi_disconnect;
	wifi->supplicant.BuildResponsePacket = efi_wifi_build_response;
	wifi->supplicant.ProcessPacket = efi_wifi_process_packet;
	wifi->supplicant.SetData = efi_wifi_set_data;
	wifi->supplicant.GetData = efi_wifi_get_data;

	/* Never join a network unasked; keep received frames for SNP */
	dev->state |= NET80211_NO_ASSOC;
	netdev_rx_freeze ( netdev );
	efi_snp_rx_hook = efi_wifi_rx;

	/* Start timer */
	if ( ( efirc = bs->CreateEvent ( ( EVT_TIMER | EVT_NOTIFY_SIGNAL ),
					 TPL_CALLBACK, efi_wifi_tick, wifi,
					 &wifi->timer ) ) != 0 )
		return -EEFI ( efirc );
	if ( ( efirc = bs->SetTimer ( wifi->timer, TimerPeriodic,
				      EFI_WIFI_PERIOD ) ) != 0 ) {
		bs->CloseEvent ( wifi->timer );
		wifi->timer = NULL;
		return -EEFI ( efirc );
	}

	return 0;
}

/**
 * Destroy Wi-Fi device
 *
 * @v netdev		Network device
 * @v priv		Private data
 */
static void efi_wifi_remove ( struct net_device *netdev __unused, void *priv ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	struct efi_wifi *wifi = priv;

	/* Ignore anything other than 802.11 devices */
	if ( ! wifi->dev )
		return;

	/* Stop timer */
	if ( wifi->timer ) {
		bs->SetTimer ( wifi->timer, TimerCancel, 0 );
		bs->CloseEvent ( wifi->timer );
		wifi->timer = NULL;
	}

	/* Fail anything still pending */
	if ( wifi->scan ) {
		net80211_free_wlanlist (
			net80211_probe_finish_all ( wifi->scan ) );
		wifi->scan = NULL;
		wifi->scan_token->Status = EFI_ABORTED;
		bs->SignalEvent ( wifi->scan_token->Event );
	}
	if ( wifi->connect_token ) {
		wifi->connect_token->Status = EFI_ABORTED;
		bs->SignalEvent ( wifi->connect_token->Event );
		wifi->connect_token = NULL;
	}

	/* Uninstall protocols */
	if ( wifi->handle ) {
		bs->UninstallMultipleProtocolInterfaces (
			wifi->handle,
			&efi_wifi2_protocol_guid, &wifi->wifi2,
			&efi_supplicant_protocol_guid, &wifi->supplicant,
			NULL );
		wifi->handle = NULL;
	}
	memset ( wifi->key, 0, sizeof ( wifi->key ) );
}

/** Wi-Fi driver */
struct net_driver efi_wifi_driver __net_driver = {
	.name = "WIFI",
	.priv_len = sizeof ( struct efi_wifi ),
	.probe = efi_wifi_probe,
	.remove = efi_wifi_remove,
};
