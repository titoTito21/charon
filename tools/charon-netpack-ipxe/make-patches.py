"""Regenerates the *.patch files of this folder from the pristine iPXE source tree.

  make-patches.py IPXE_SRC_DIR      (the "src" folder of the pinned iPXE commit)

The patches are what tools/make-charon-netpack.py sends to the build machine; this
script only documents how they were made (each edit must match exactly once).
"""
import difflib,sys
from pathlib import Path
src=Path(sys.argv[1]);out=Path(__file__).resolve().parent
def patch(rel,edits,name):
    old=(src/rel).read_text(encoding='utf-8');new=old
    for a,b in edits:
        if new.count(a)!=1:raise SystemExit(f'{rel}: edit does not match once: {a[:60]!r} ({new.count(a)})')
        new=new.replace(a,b)
    d=''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),'a/src/'+rel,'b/src/'+rel))
    (out/name).write_text(d,encoding='utf-8',newline='\n');print(name,len(d))

# 1. Images with an 802.11 driver link the UEFI Wi-Fi face.
patch('config/config_net80211.c',[("#ifdef CRYPTO_80211_WEP\n",
 "/*\n * Drag in the UEFI Wi-Fi face (Charon driver pack)\n *\n */\n#ifdef CHARON_EFI_WIFI\nREQUIRE_OBJECT ( efi_wifi );\n#endif\n\n#ifdef CRYPTO_80211_WEP\n")],'10-config-net80211.patch')

# 1b. Error file identifier of the new file interface/efi/efi_wifi.c.
patch('include/ipxe/errfile.h',[("#define ERRFILE_tlsfmt\t\t      ( ERRFILE_OTHER | 0x00700000 )\n",
 "#define ERRFILE_tlsfmt\t\t      ( ERRFILE_OTHER | 0x00700000 )\n#define ERRFILE_efi_wifi\t      ( ERRFILE_OTHER | 0x00710000 )\n")],'15-errfile-efi-wifi.patch')

# 2. Intel: remaining I225/I226 variants and Killer E3100 (IDs from Linux igc_hw.h).
patch('drivers/net/intel.c',[('\tPCI_ROM ( 0x8086, 0x5502, "i225lmvp", "I225-LMvP", INTEL_PBSIZE_RST ),\n',
 '\tPCI_ROM ( 0x8086, 0x5502, "i225lmvp", "I225-LMvP", INTEL_PBSIZE_RST ),\n'
 '\tPCI_ROM ( 0x8086, 0x5503, "i226lmvp", "I226-LMvP", INTEL_PBSIZE_RST ),\n'
 '\tPCI_ROM ( 0x8086, 0x3100, "i225k", "I225-K (Killer E3100G)", INTEL_PBSIZE_RST ),\n'
 '\tPCI_ROM ( 0x8086, 0x3101, "i225k2", "I225-K2 (Killer E3100G)", INTEL_PBSIZE_RST ),\n'
 '\tPCI_ROM ( 0x8086, 0x3102, "i226k", "I226-K (Killer E3100G)", INTEL_PBSIZE_RST ),\n'
 '\tPCI_ROM ( 0x8086, 0x15f8, "i225i", "I225-I", INTEL_PBSIZE_RST ),\n'
 '\tPCI_ROM ( 0x8086, 0x15f7, "i220v", "I220-V", INTEL_PBSIZE_RST ),\n'
 '\tPCI_ROM ( 0x8086, 0x125e, "i221v", "I221-V", INTEL_PBSIZE_RST ),\n')],'20-intel-igc-ids.patch')

# 3. efi_snp.c: Ethernet framing for 802.11 devices, receive hook, two exports.
E=[]
E.append(('#include <ipxe/efi/efi_snp.h>\n','#include <ipxe/efi/efi_snp.h>\n#include <ipxe/efi/efi_wifi.h>\n#include <ipxe/if_ether.h>\n#include <ipxe/if_arp.h>\n'))
E.append(('static LIST_HEAD ( efi_snp_devices );\n','''static LIST_HEAD ( efi_snp_devices );

/** Receive hook: frames the driver itself must consume (Charon, efi_wifi.c) */
int ( * efi_snp_rx_hook ) ( struct net_device *netdev,
			    struct io_buffer *iobuf );

/**
 * Check for an 802.11 device, presented with Ethernet framing (Charon)
 *
 * @v netdev		Network device
 * @ret is_80211	Device has 802.11 link-layer headers
 *
 * The firmware's network stack (MNP) parses Ethernet headers itself,
 * so an 802.11 device must look like an Ethernet device through SNP.
 */
static int efi_snp_eth_emul ( struct net_device *netdev ) {
	struct ll_protocol *ll_protocol = netdev->ll_protocol;

	return ( ( ll_protocol->ll_proto == htons ( ARPHRD_ETHER ) ) &&
		 ( ll_protocol->ll_addr_len == ETH_ALEN ) &&
		 ( ll_protocol->ll_header_len > ETH_HLEN ) );
}

/**
 * Get link-layer header length as seen through SNP
 *
 * @v netdev		Network device
 * @ret len		Header length
 */
static size_t efi_snp_ll_header_len ( struct net_device *netdev ) {

	return ( efi_snp_eth_emul ( netdev ) ?
		 ( ( size_t ) ETH_HLEN ) : netdev->ll_protocol->ll_header_len );
}
'''))
E.append(('\tmode->MediaHeaderSize = ll_protocol->ll_header_len;\n','\tmode->MediaHeaderSize = efi_snp_ll_header_len ( netdev );\n'))
E.append(('''	while ( ( iobuf = netdev_rx_dequeue ( snpdev->netdev ) ) ) {
		list_add_tail ( &iobuf->list, &snpdev->rx );''','''	while ( ( iobuf = netdev_rx_dequeue ( snpdev->netdev ) ) ) {
		if ( efi_snp_rx_hook &&
		     efi_snp_rx_hook ( snpdev->netdev, iobuf ) )
			continue;
		list_add_tail ( &iobuf->list, &snpdev->rx );'''))
E.append(('\t\tif ( ll_header_len != ll_protocol->ll_header_len ) {\n','\t\tif ( ll_header_len !=\n\t\t     efi_snp_ll_header_len ( snpdev->netdev ) ) {\n'))
E.append(('''	/* Allocate buffer */
	payload_len = ( len - ll_protocol->ll_header_len );
''','''	/* 802.11 device presented with Ethernet framing (Charon):
	 * the caller's frame has, or expects, an Ethernet header.
	 */
	if ( efi_snp_eth_emul ( snpdev->netdev ) ) {
		const uint8_t *eth = data;
		const void *eth_dest = eth;
		const void *eth_src = ( eth + ETH_ALEN );
		uint16_t eth_proto;

		if ( len < ETH_HLEN ) {
			rc = -EINVAL;
			goto err_sanity;
		}
		memcpy ( &eth_proto, ( eth + ( 2 * ETH_ALEN ) ),
			 sizeof ( eth_proto ) );
		if ( ll_header_len ) {
			eth_dest = ll_dest;
			eth_src = ll_src;
			eth_proto = htons ( *net_proto );
		}
		payload_len = ( len - ETH_HLEN );
		iobuf = alloc_iob ( MAX_LL_HEADER_LEN +
				    ( ( payload_len > IOB_ZLEN ) ?
				      payload_len : IOB_ZLEN ) );
		if ( ! iobuf ) {
			rc = -ENOMEM;
			goto err_alloc_iob;
		}
		iob_reserve ( iobuf, MAX_LL_HEADER_LEN );
		memcpy ( iob_put ( iobuf, payload_len ), ( eth + ETH_HLEN ),
			 payload_len );
		if ( ( rc = ll_protocol->push ( snpdev->netdev, iobuf,
						eth_dest, eth_src,
						eth_proto ) ) != 0 )
			goto err_ll_push;
		goto ll_done;
	}

	/* Allocate buffer */
	payload_len = ( len - ll_protocol->ll_header_len );
'''))
E.append(('''	/* Transmit packet */
	if ( ( rc = netdev_tx ( snpdev->netdev, iob_disown ( iobuf ) ) ) != 0){''',''' ll_done:
	/* Transmit packet */
	if ( ( rc = netdev_tx ( snpdev->netdev, iob_disown ( iobuf ) ) ) != 0){'''))
E.append(('''	/* Dequeue packet */
	list_del ( &iobuf->list );

	/* Return packet to caller, truncating to buffer length */''','''	/* Dequeue packet */
	list_del ( &iobuf->list );

	/* 802.11 device presented with Ethernet framing (Charon):
	 * replace the 802.11 and LLC headers with an Ethernet header.
	 */
	if ( efi_snp_eth_emul ( snpdev->netdev ) ) {
		uint8_t eth[ETH_HLEN];
		size_t frame_len;

		if ( ( rc = ll_protocol->pull ( snpdev->netdev, iobuf,
						&iob_ll_dest, &iob_ll_src,
						&iob_net_proto,
						&iob_flags ) ) != 0 )
			goto out_bad_ll_header;
		memcpy ( eth, iob_ll_dest, ETH_ALEN );
		memcpy ( ( eth + ETH_ALEN ), iob_ll_src, ETH_ALEN );
		memcpy ( ( eth + ( 2 * ETH_ALEN ) ), &iob_net_proto,
			 sizeof ( iob_net_proto ) );
		frame_len = ( ETH_HLEN + iob_len ( iobuf ) );
		copy_len = frame_len;
		if ( copy_len > *len )
			copy_len = *len;
		if ( copy_len >= ETH_HLEN ) {
			memcpy ( data, eth, ETH_HLEN );
			memcpy ( ( data + ETH_HLEN ), iobuf->data,
				 ( copy_len - ETH_HLEN ) );
		} else {
			memcpy ( data, eth, copy_len );
		}
		*len = frame_len;
		if ( ll_header_len )
			*ll_header_len = ETH_HLEN;
		if ( ll_src )
			memcpy ( ll_src, iob_ll_src, ETH_ALEN );
		if ( ll_dest )
			memcpy ( ll_dest, iob_ll_dest, ETH_ALEN );
		if ( net_proto )
			*net_proto = ntohs ( iob_net_proto );
		rc = ( ( copy_len == frame_len ) ? 0 : -ERANGE );
		goto out_bad_ll_header;
	}

	/* Return packet to caller, truncating to buffer length */'''))
TAIL='''
/**
 * Get SNP handle of a network device (Charon, efi_wifi.c)
 *
 * @v netdev		Network device
 * @ret handle		SNP handle, or NULL
 */
EFI_HANDLE efi_snp_netdev_handle ( struct net_device *netdev ) {
	struct efi_snp_device *snpdev = efi_snp_demux ( netdev );

	return ( snpdev ? snpdev->handle : NULL );
}

/**
 * Poll a network device on behalf of its SNP consumer (Charon, efi_wifi.c)
 *
 * @v netdev		Network device
 * @v max_rx		Maximum number of received frames to keep
 *
 * Used where nothing may be polling SNP (a Wi-Fi handshake must go on
 * regardless); the oldest frames are dropped beyond max_rx.
 */
void efi_snp_netdev_poll ( struct net_device *netdev, unsigned int max_rx ) {
	struct efi_snp_device *snpdev = efi_snp_demux ( netdev );
	struct io_buffer *iobuf;
	unsigned int count = 0;

	if ( ( ! snpdev ) || efi_snp_claimed )
		return;
	efi_snp_poll ( snpdev );
	list_for_each_entry ( iobuf, &snpdev->rx, list )
		count++;
	while ( count > max_rx ) {
		iobuf = list_first_entry ( &snpdev->rx, struct io_buffer,
					   list );
		list_del ( &iobuf->list );
		free_iob ( iobuf );
		count--;
	}
}
'''
old=(src/'interface/efi/efi_snp.c').read_text(encoding='utf-8')
E.append((old[-200:],old[-200:]+TAIL))
patch('interface/efi/efi_snp.c',E,'30-efi-snp-80211.patch')
