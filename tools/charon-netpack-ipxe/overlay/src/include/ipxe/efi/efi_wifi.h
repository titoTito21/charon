#ifndef _IPXE_EFI_WIFI_H
#define _IPXE_EFI_WIFI_H

/** @file
 *
 * UEFI Wi-Fi face for 802.11 devices (Charon driver pack)
 *
 * Hooks between efi_snp.c and efi_wifi.c.  efi_snp.c never refers to
 * efi_wifi.c, so images without an 802.11 driver do not link it.
 */

FILE_LICENCE ( GPL2_OR_LATER );

#include <ipxe/efi/efi.h>

struct net_device;
struct io_buffer;

extern int ( * efi_snp_rx_hook ) ( struct net_device *netdev,
				   struct io_buffer *iobuf );
extern EFI_HANDLE efi_snp_netdev_handle ( struct net_device *netdev );
extern void efi_snp_netdev_poll ( struct net_device *netdev,
				  unsigned int max_rx );

#endif /* _IPXE_EFI_WIFI_H */
