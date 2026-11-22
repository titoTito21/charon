/*
 * Charon network drivers: OpenBSD's rge(4) (sys/dev/pci/if_rge.c, compiled
 * here UNMODIFIED by inclusion) and the port functions of rge_port.h that
 * drive it: attach, bring up, poll instead of interrupts, transmit, receive.
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

/* The driver itself (ISC licence, Kevin Lo). */
#include <dev/pci/if_rge.c>

struct rge_port {
	struct obsd_env		*env;
	struct rge_softc	*sc;
	struct pci_attach_args	 pa;
};

int
rge_port_match(uint16_t vendor, uint16_t product)
{
	struct pci_attach_args pa;

	memset(&pa, 0, sizeof(pa));
	pa.pa_id = (pcireg_t)vendor | ((pcireg_t)product << 16);
#ifdef NETDRV_TEST_PRODUCT
	/* TEST BUILDS ONLY (never shipped): also bind another Realtek product
	 * an emulator has, so that binding, the buffer pool, register mapping
	 * and the error path can run up to the point where rge_attach() reads
	 * the chip revision and gives up. */
	if (vendor == 0x10ec && product == NETDRV_TEST_PRODUCT)
		return 1;
#endif
	return rge_match(NULL, NULL, &pa);
}

unsigned
rge_port_ids(unsigned index, uint16_t *vendor, uint16_t *product)
{
	if (index >= nitems(rge_devices))
		return 0;
	*vendor = rge_devices[index].pm_vid;
	*product = rge_devices[index].pm_pid;
	return 1;
}

struct rge_port *
rge_port_attach(struct rge_host *host, uint16_t vendor, uint16_t product,
    uint8_t mac[6])
{
	struct obsd_env *env;
	struct rge_port *port;
	struct rge_softc *sc;
	void (*hook)(struct device *);

	env = obsd_env_create(host);
	if (env == NULL) {
		rge_host_log("rge: no memory for the buffer pool\n");
		return NULL;
	}
	obsd_cur = env;
	port = malloc(sizeof(*port), M_DEVBUF, M_NOWAIT | M_ZERO);
	sc = malloc(sizeof(*sc), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (port == NULL || sc == NULL)
		goto fail;
	port->env = env;
	port->sc = sc;
	port->pa.pa_pc = host;
	port->pa.pa_dmat = host;
	port->pa.pa_id = (pcireg_t)vendor | ((pcireg_t)product << 16);
	strlcpy(sc->sc_dev.dv_xname, "rge0", sizeof(sc->sc_dev.dv_xname));

	printf("rge0");
	rge_attach(NULL, &sc->sc_dev, &port->pa);
	hook = env->mountroot_hook;
	if (hook == NULL)		/* attach gave up and said why */
		goto fail;
	env->mountroot_hook = NULL;
	hook(&sc->sc_dev);		/* rge_attachhook: firmware, rings */
	if (!sc->sc_arpcom.ac_if._if_attached) {
		rge_host_log("rge0: attach did not complete\n");
		goto fail;
	}
	memcpy(mac, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	return port;
fail:
	obsd_env_destroy(env);		/* frees port and sc as well */
	return NULL;
}

void
rge_port_detach(struct rge_port *port)
{
	struct obsd_env *env = port->env;
	struct ifnet *ifp = &port->sc->sc_arpcom.ac_if;
	struct mbuf *m;

	obsd_cur = env;
	rge_stop(ifp);			/* chip reset, rings emptied */
	while ((m = ml_dequeue(&env->rxq)) != NULL)
		m_freem(m);
	obsd_env_destroy(env);
}

int
rge_port_up(struct rge_port *port)
{
	struct ifnet *ifp = &port->sc->sc_arpcom.ac_if;

	obsd_cur = port->env;
	ifp->if_flags |= IFF_UP;
	rge_init(ifp);
	return (ifp->if_flags & IFF_RUNNING) ? 0 : 1;
}

void
rge_port_down(struct rge_port *port)
{
	struct ifnet *ifp = &port->sc->sc_arpcom.ac_if;
	struct mbuf *m;

	obsd_cur = port->env;
	ifp->if_flags &= ~IFF_UP;
	rge_stop(ifp);
	ifq_purge(ifp->if_ifqs[0]);
	while ((m = ml_dequeue(&port->env->rxq)) != NULL)
		m_freem(m);
	ifp->if_link_state = LINK_STATE_DOWN;
}

int
rge_port_running(struct rge_port *port)
{
	return (port->sc->sc_arpcom.ac_if.if_flags & IFF_RUNNING) != 0;
}

void
rge_port_poll(struct rge_port *port)
{
	struct obsd_env *env = port->env;
	struct rge_softc *sc = port->sc;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	obsd_cur = env;
	if (!(ifp->if_flags & IFF_RUNNING))
		return;
	/* The handler the driver established (rge_intr), unchanged. */
	if (env->intr_set)
		env->intr.ih_func(env->intr.ih_arg);
	/* And the rings directly, so that reception does not depend on the
	 * interrupt status register latching while interrupts are masked. */
	if (ifp->if_flags & IFF_RUNNING) {
		rge_rxeof(&sc->sc_queues[0]);
		rge_txeof(&sc->sc_queues[0]);
	}
}

void
rge_port_tick(struct rge_port *port)
{
	obsd_cur = port->env;
	obsd_tick(port->env);		/* rge_tick every second, watchdog */
}

int
rge_port_link(struct rge_port *port)
{
	struct rge_softc *sc = port->sc;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	obsd_cur = port->env;
	if (!(ifp->if_flags & IFF_RUNNING))
		return 0;
	rge_link_state(sc);
	return LINK_STATE_IS_UP(ifp->if_link_state) ? 1 : 0;
}

int
rge_port_tx(struct rge_port *port, const void *frame, size_t len)
{
	struct obsd_env *env = port->env;
	struct rge_softc *sc = port->sc;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct ifqueue *ifq;
	struct mbuf *m;

	obsd_cur = env;
	if (!(ifp->if_flags & IFF_RUNNING) || len > MCLBYTES || len < 14)
		return 2;
	rge_link_state(sc);
	if (!LINK_STATE_IS_UP(ifp->if_link_state))
		return 2;		/* rge_start() would purge it anyway */
	ifq = ifp->if_ifqs[0];
	if (env->txinflight >= OBSD_TX_MAX)
		rge_txeof(&sc->sc_queues[0]);
	if (env->txinflight >= OBSD_TX_MAX)
		return 1;
	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (m == NULL)
		return 1;
	memcpy(m->m_data, frame, len);
	m->m_len = m->m_pkthdr.len = (int)len;
	m->m_flags |= M_OBSD_TX;
	env->txinflight++;
	if (ifq_enqueue(ifq, m) != 0) {
		m_freem(m);
		return 1;
	}
	if (!ifq_is_oactive(ifq))
		ifp->if_qstart(ifq);	/* rge_start */
	return 0;
}

size_t
rge_port_rx_peek(struct rge_port *port)
{
	struct mbuf *m = port->env->rxq.ml_head;

	return (m != NULL) ? (size_t)m->m_pkthdr.len : 0;
}

size_t
rge_port_rx_take(struct rge_port *port, void *buf, size_t cap)
{
	struct mbuf *m;
	size_t len;

	obsd_cur = port->env;
	m = ml_dequeue(&port->env->rxq);
	if (m == NULL)
		return 0;
	len = (size_t)m->m_pkthdr.len;
	if (len > cap || m->m_pkthdr.len <= 0)
		len = 0;
	else
		m_copydata(m, 0, (int)len, buf);
	m_freem(m);
	return len;
}

/* promiscuous: bit 0 everything, bit 1 all multicast */
void
rge_port_filter(struct rge_port *port, int promiscuous, const uint8_t *mcast,
    unsigned count)
{
	struct rge_softc *sc = port->sc;
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &ac->ac_if;
	unsigned i;

	obsd_cur = port->env;
	if (promiscuous & 1)
		ifp->if_flags |= IFF_PROMISC;
	else
		ifp->if_flags &= ~IFF_PROMISC;
	ac->ac_multirangecnt = ((promiscuous & 2) || count > OBSD_MAXMULTI) ?
	    1 : 0;
	ac->ac_multicnt = (int)MIN(count, OBSD_MAXMULTI);
	for (i = 0; i < (unsigned)ac->ac_multicnt; i++) {
		memcpy(ac->ac_multi[i].enm_addrlo, mcast + i * 6, 6);
		memcpy(ac->ac_multi[i].enm_addrhi, mcast + i * 6, 6);
	}
	if (ifp->if_flags & IFF_RUNNING)
		rge_iff(sc);
}

void
rge_port_set_mac(struct rge_port *port, const uint8_t mac[6])
{
	struct rge_softc *sc = port->sc;

	obsd_cur = port->env;
	memcpy(sc->sc_arpcom.ac_enaddr, mac, ETHER_ADDR_LEN);
	rge_set_macaddr(sc, sc->sc_arpcom.ac_enaddr);
}

/* ExitBootServices: silence the chip (no DMA after the system takes the
 * memory).  No allocation, no freeing, one bounded wait of at most 10 ms. */
void
rge_port_quiesce(struct rge_port *port)
{
	struct rge_softc *sc = port->sc;
	int i;

	if (!(sc->sc_arpcom.ac_if.if_flags & IFF_RUNNING))
		return;
	RGE_WRITE_4(sc, RGE_IMR, 0);
	RGE_WRITE_1(sc, RGE_CMD, RGE_CMD_RESET);
	for (i = 0; i < RGE_TIMEOUT; i++) {
		DELAY(100);
		if (!(RGE_READ_1(sc, RGE_CMD) & RGE_CMD_RESET))
			break;
	}
	sc->sc_arpcom.ac_if.if_flags &= ~IFF_RUNNING;
}

const char *
rge_port_chip(struct rge_port *port)
{
	switch (port->sc->rge_type) {
	case MAC_R25:		return "RTL8125";
	case MAC_R25B:		return "RTL8125B";
	case MAC_R25D_1:
	case MAC_R25D_2:	return "RTL8125D";
	case MAC_R26_1:
	case MAC_R26_2:		return "RTL8126";
	case MAC_R27:		return "RTL8127";
	case MAC_R27ATF:	return "RTL8127ATF";
	default:		return "unknown";
	}
}
