/* COPY of src/netdrv/rge-uefi/rge_port.h (2026-11-22) for the EXPERIMENTAL Wi-Fi driver (never run on the
 * hardware); changes: OBSD_COMPAT_MII/WIFI always on, larger buffer pool, struct task/mbuf/ifnet/ifreq/ip
 * fields net80211 needs, obsd_wifi.h included, tsleep_nsec left to wifi_kern.c. */
/*
 * Charon network drivers: the narrow interface between the OpenBSD side
 * (if_rge.c + obsd_kern.c, OpenBSD types) and the UEFI side (rge_uefi.c,
 * edk2 types).  Plain C types only, so that neither side sees the other's
 * headers.
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
#ifndef RGE_PORT_H
#define RGE_PORT_H

#include <stddef.h>
#include <stdint.h>

struct rge_host;			/* one adapter, owned by the UEFI side */

/* ---- implemented by the UEFI side (rge_uefi.c) ---------------------- */
/* PCI configuration space, 32 bit; a failed read gives 0xffffffff. */
uint32_t rge_host_cfg_read(struct rge_host *, uint32_t off);
void	 rge_host_cfg_write(struct rge_host *, uint32_t off, uint32_t val);
/* Register access through BAR `bar` (0..5); width 1, 2 or 4 bytes. */
uint32_t rge_host_reg_read(struct rge_host *, unsigned bar, int io,
	    unsigned width, uint64_t off);
void	 rge_host_reg_write(struct rge_host *, unsigned bar, int io,
	    unsigned width, uint64_t off, uint32_t val);
/* Memory the device may read and write: host address returned, the address
 * the device must use in *dev.  Zeroed.  NULL when there is none. */
void	*rge_host_dma_alloc(struct rge_host *, size_t bytes, uint64_t *dev,
	    void **cookie);
void	 rge_host_dma_free(struct rge_host *, void *va, size_t bytes,
	    void *cookie);
void	*rge_host_alloc(size_t bytes);		/* pool memory, not zeroed */
void	 rge_host_free(void *);
void	 rge_host_stall(unsigned microseconds);
void	 rge_host_log(const char *text);	/* driver messages */

/* ---- implemented by the OpenBSD side (rge_obsd.c) ------------------- */
struct rge_port;			/* softc + bookkeeping */

int	 rge_port_match(uint16_t vendor, uint16_t product);
unsigned rge_port_ids(unsigned index, uint16_t *vendor, uint16_t *product);
/* Runs rge_attach(); NULL when the chip is not usable. */
struct rge_port *rge_port_attach(struct rge_host *, uint16_t vendor,
	    uint16_t product, uint8_t mac[6]);
void	 rge_port_detach(struct rge_port *);	/* stops the chip, frees all */
int	 rge_port_up(struct rge_port *);	/* rge_init(); 0 when running */
void	 rge_port_down(struct rge_port *);	/* rge_stop() */
int	 rge_port_running(struct rge_port *);
void	 rge_port_poll(struct rge_port *);	/* interrupt handler + rings */
void	 rge_port_tick(struct rge_port *);	/* every 10 ms: timeouts */
int	 rge_port_link(struct rge_port *);	/* 1 when the link is up */
/* 0 queued, 1 no room now, 2 dropped (no link). The frame is copied. */
int	 rge_port_tx(struct rge_port *, const void *frame, size_t len);
size_t	 rge_port_rx_peek(struct rge_port *);	/* length of next frame or 0 */
size_t	 rge_port_rx_take(struct rge_port *, void *buf, size_t cap);
void	 rge_port_filter(struct rge_port *, int promiscuous,
	    const uint8_t *mcast, unsigned count);	/* count * 6 bytes */
void	 rge_port_set_mac(struct rge_port *, const uint8_t mac[6]);
void	 rge_port_quiesce(struct rge_port *);	/* ExitBootServices: no free */
const char *rge_port_chip(struct rge_port *);

#endif /* RGE_PORT_H */
