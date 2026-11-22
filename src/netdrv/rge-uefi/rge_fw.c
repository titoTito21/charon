/*
 * Charon network drivers: loadfirmware() for rge(4).
 *
 * OpenBSD builds the files /etc/firmware/rge-rtl81xx from the tables of
 * sys/dev/microcode/rge/rgemcode.h with sys/dev/microcode/rge/build.c: a
 * header of two little-endian lengths followed by the MAC table and the PHY
 * table (pairs of 16 bit register and value).  Here the same image is put
 * together in memory from the same tables, so the driver needs no file.
 * The tables are Realtek's ("Permission is hereby granted for the
 * distribution of this firmware data in hexadecimal or equivalent format,
 * provided this copyright notice is accompanying it" - see rge-license).
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
#include "obsd_kern.h"

#include <dev/microcode/rge/rgemcode.h>

struct fw_header {		/* struct rge_fw_hdr of if_rgereg.h */
	uint32_t	maclen;
	uint32_t	phylen;
} __packed;

static const struct {
	const char	*name;
	const void	*mac;
	uint32_t	 maclen;
	const void	*phy;
	uint32_t	 phylen;
} images[] = {
	{ "rge-rtl8125", rtl8125_mac_bps, sizeof(rtl8125_mac_bps),
	    mac_r25_mcu, sizeof(mac_r25_mcu) },
	{ "rge-rtl8125b", rtl8125b_mac_bps, sizeof(rtl8125b_mac_bps),
	    mac_r25b_mcu, sizeof(mac_r25b_mcu) },
	{ "rge-rtl8125d-1", NULL, 0,
	    mac_r25d_1_mcu, sizeof(mac_r25d_1_mcu) },
	{ "rge-rtl8125d-2", rtl8125d_2_mac_bps, sizeof(rtl8125d_2_mac_bps),
	    mac_r25d_2_mcu, sizeof(mac_r25d_2_mcu) },
	{ "rge-rtl8126-1", rtl8126_1_mac_bps, sizeof(rtl8126_1_mac_bps),
	    mac_r26_1_mcu, sizeof(mac_r26_1_mcu) },
	{ "rge-rtl8126-2", rtl8126_2_mac_bps, sizeof(rtl8126_2_mac_bps),
	    mac_r26_2_mcu, sizeof(mac_r26_2_mcu) },
	{ "rge-rtl8127", rtl8127_mac_bps, sizeof(rtl8127_mac_bps),
	    mac_r27_mcu, sizeof(mac_r27_mcu) },
};

int
loadfirmware(const char *name, u_char **bufp, size_t *buflen)
{
	struct fw_header *hdr;
	u_char *buf;
	size_t len;
	unsigned i;

	for (i = 0; i < nitems(images); i++) {
		if (strcmp(name, images[i].name) != 0)
			continue;
		len = sizeof(*hdr) + images[i].maclen + images[i].phylen;
		buf = malloc(len, M_DEVBUF, M_NOWAIT);
		if (buf == NULL)
			return ENOMEM;
		hdr = (struct fw_header *)buf;
		hdr->maclen = htole32(images[i].maclen);
		hdr->phylen = htole32(images[i].phylen);
		if (images[i].maclen)
			memcpy(buf + sizeof(*hdr), images[i].mac,
			    images[i].maclen);
		memcpy(buf + sizeof(*hdr) + images[i].maclen, images[i].phy,
		    images[i].phylen);
		*bufp = buf;
		*buflen = len;
		return 0;
	}
	return ENOENT;
}
