/*
 * Charon network drivers: per-adapter state of the OpenBSD kernel shim.
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
#ifndef OBSD_KERN_H
#define OBSD_KERN_H

#include "obsd_compat.h"

#define OBSD_NMBUF	1024	/* mbuf headers in the pool */
#define OBSD_NCLUSTER	768	/* 2048 byte clusters in the pool */
#define OBSD_RXRING_MAX	256	/* receive buffers given to the chip */
#define OBSD_RXQ_MAX	128	/* received frames waiting for Receive() */
#define OBSD_TX_MAX	64	/* frames handed to the driver, not yet sent */
#define OBSD_IFQ_MAX	64
#define M_OBSD_TX	0x4000	/* m_flags: counted in env->txinflight */

struct obsd_alloc {
	struct obsd_alloc *next, *prev;
	size_t size;
};
struct obsd_region {
	struct obsd_region *next;
	char	*va;
	uint64_t dev;
	size_t	 len;
	void	*cookie;
};
struct obsd_env {
	struct rge_host	*host;
	struct obsd_alloc *allocs;
	struct obsd_region *regions;
	/* mbuf pool: one DMA region */
	struct mbuf	*mfree;
	char		*clfree;	/* free clusters, linked through word 0 */
	unsigned	 nmfree, nclfree;
	unsigned	 txinflight;
	/* time */
	uint64_t	 ticks;		/* 10 ms */
	struct timeout	*timeouts;
	/* the interrupt handler the driver established */
	struct obsd_intr intr;
	int		 intr_set;
	/* register mappings */
	struct obsd_busmap busmap[4];
	int		 nbusmap;
	/* the interface */
	struct ifnet	*ifp;
	struct mbuf_list rxq;		/* frames for SNP Receive() */
	uint64_t	 rxdropped;
	void		(*mountroot_hook)(struct device *);
	struct device	*mountroot_dev;
};

extern struct obsd_env *obsd_cur;	/* adapter the current call is for */

struct obsd_env *obsd_env_create(struct rge_host *);
void	obsd_env_destroy(struct obsd_env *);	/* frees everything left */
void	obsd_tick(struct obsd_env *);
int	obsd_dma_translate(struct obsd_env *, const void *, size_t,
	    uint64_t *);

#endif /* OBSD_KERN_H */
