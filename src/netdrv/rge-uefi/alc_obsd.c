/*
 * Charon network drivers: OpenBSD's alc(4) (sys/dev/pci/if_alc.c, compiled
 * here UNMODIFIED by inclusion; Atheros/Qualcomm AR813x/AR815x/AR816x/AR817x
 * and Killer E2200/E2400/E2500) and the port functions of rge_port.h that
 * drive it.  The UEFI face (rge_uefi.c) and the kernel shim are the ones of
 * the rge port; alc needs the MII layer (sys/dev/mii: mii.c, mii_physubr.c,
 * atphy.c, ukphy.c, compiled unmodified as well) and the small autoconf of
 * obsd_kern.c that attaches the PHY driver.
 *
 * NEVER RUN ON THE HARDWARE.
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

/* The driver itself (BSD-2-Clause, Pyun YongHyeon). */
#include <dev/pci/if_alc.c>

/* PHY drivers the miniature autoconf may attach: the Atheros F1/F2 PHYs of
 * these chips, and the generic one as the fallback OpenBSD also has. */
extern const struct cfattach atphy_ca, ukphy_ca;
extern struct cfdriver atphy_cd, ukphy_cd;
struct cfdata obsd_children[] = {
	{ &atphy_ca, &atphy_cd, { -1, 0 } },
	{ &ukphy_ca, &ukphy_cd, { -1, 0 } },
	{ NULL, NULL, { 0, 0 } }
};

#if defined(NETDRV_TEST_VENDOR) && defined(NETDRV_TEST_FORCE_LINK)
/* TEST BUILDS ONLY: pretend the PHY reported a link, so that the firmware's
 * network stack really transmits into the (foreign, deaf) device and the
 * transmit path, the buffer recycling and the watchdog are exercised. */
#define TEST_FORCE_LINK(sc)	((sc)->alc_flags |= ALC_FLAG_LINK)
#else
#define TEST_FORCE_LINK(sc)	do { } while (0)
#endif

struct rge_port {
	struct obsd_env		*env;
	struct alc_softc	*sc;
	struct pci_attach_args	 pa;
};

int
rge_port_match(uint16_t vendor, uint16_t product)
{
	struct pci_attach_args pa;

	memset(&pa, 0, sizeof(pa));
	pa.pa_id = (pcireg_t)vendor | ((pcireg_t)product << 16);
#ifdef NETDRV_TEST_VENDOR
	/* TEST BUILDS ONLY (never shipped): also bind one foreign device of
	 * an emulator.  alc_attach() does not identify the chip, so the whole
	 * attach, the MII probe, the Simple Network child and the firmware's
	 * network stack on top run against registers that answer nonsense:
	 * a test of the shim, of the UEFI face and of every bounded wait. */
	if (vendor == NETDRV_TEST_VENDOR && product == NETDRV_TEST_PRODUCT)
		return 1;
#endif
	return alc_match(NULL, NULL, &pa);
}

unsigned
rge_port_ids(unsigned index, uint16_t *vendor, uint16_t *product)
{
	if (index >= nitems(alc_devices))
		return 0;
	*vendor = alc_devices[index].pm_vid;
	*product = alc_devices[index].pm_pid;
	return 1;
}

struct rge_port *
rge_port_attach(struct rge_host *host, uint16_t vendor, uint16_t product,
    uint8_t mac[6])
{
	struct obsd_env *env;
	struct rge_port *port;
	struct alc_softc *sc;

	env = obsd_env_create(host);
	if (env == NULL) {
		rge_host_log("alc: no memory for the buffer pool\n");
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
	port->pa.pa_class = rge_host_cfg_read(host, 0x08);	/* revision */
	strlcpy(sc->sc_dev.dv_xname, "alc0", sizeof(sc->sc_dev.dv_xname));

	printf("alc0");
	alc_attach(NULL, &sc->sc_dev, &port->pa);
	if (!sc->sc_arpcom.ac_if._if_attached) {
		rge_host_log("alc0: attach did not complete\n");
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
	struct mbuf *m;

	obsd_cur = env;
	alc_stop(port->sc);		/* MAC and DMA stopped, rings emptied */
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
	if (alc_init(ifp) != 0)
		return 1;
	return (ifp->if_flags & IFF_RUNNING) ? 0 : 1;
}

void
rge_port_down(struct rge_port *port)
{
	struct ifnet *ifp = &port->sc->sc_arpcom.ac_if;
	struct mbuf *m;

	obsd_cur = port->env;
	ifp->if_flags &= ~IFF_UP;
	alc_stop(port->sc);
	ifq_purge(&ifp->if_snd);
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
	struct alc_softc *sc = port->sc;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	obsd_cur = env;
	if (!(ifp->if_flags & IFF_RUNNING))
		return;
	/* The handler the driver established (alc_intr), unchanged. */
	if (env->intr_set)
		env->intr.ih_func(env->intr.ih_arg);
	/* And the rings directly, so that reception does not depend on the
	 * interrupt status register latching while interrupts are off. */
	if (ifp->if_flags & IFF_RUNNING) {
		if (alc_rxintr(sc) != 0)
			alc_init(ifp);	/* as alc_intr() does on that error */
		else {
			alc_txeof(sc);
			alc_start(ifp);
		}
	}
}

void
rge_port_tick(struct rge_port *port)
{
	obsd_cur = port->env;
	obsd_tick(port->env);		/* alc_tick -> mii_tick every second */
}

int
rge_port_link(struct rge_port *port)
{
	struct alc_softc *sc = port->sc;

	if (!(sc->sc_arpcom.ac_if.if_flags & IFF_RUNNING))
		return 0;
	TEST_FORCE_LINK(sc);
	return (sc->alc_flags & ALC_FLAG_LINK) ? 1 : 0;	/* set from the PHY */
}

int
rge_port_tx(struct rge_port *port, const void *frame, size_t len)
{
	struct obsd_env *env = port->env;
	struct alc_softc *sc = port->sc;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf *m;

	obsd_cur = env;
	if (!(ifp->if_flags & IFF_RUNNING) || len > MCLBYTES || len < 14)
		return 2;
	TEST_FORCE_LINK(sc);
	if (!(sc->alc_flags & ALC_FLAG_LINK))
		return 2;		/* alc_start() sends nothing without */
	if (env->txinflight >= OBSD_TX_MAX)
		alc_txeof(sc);
	if (env->txinflight >= OBSD_TX_MAX)
		return 1;
	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (m == NULL)
		return 1;
	memcpy(m->m_data, frame, len);
	m->m_len = m->m_pkthdr.len = (int)len;
	m->m_flags |= M_OBSD_TX;
	env->txinflight++;
	if (ifq_enqueue(&ifp->if_snd, m) != 0) {
		m_freem(m);
		return 1;
	}
	alc_start(ifp);
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
	struct alc_softc *sc = port->sc;
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
		alc_iff(sc);
}

void
rge_port_set_mac(struct rge_port *port, const uint8_t mac[6])
{
	struct alc_softc *sc = port->sc;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	obsd_cur = port->env;
	memcpy(sc->sc_arpcom.ac_enaddr, mac, ETHER_ADDR_LEN);
	memcpy(LLADDR(ifp->if_sadl), mac, ETHER_ADDR_LEN);
	if (ifp->if_flags & IFF_RUNNING)
		alc_init(ifp);		/* alc programmes the address there */
}

/* ExitBootServices: interrupts off, MAC and its DMA stopped.  No memory is
 * freed; alc_stop_mac() waits at most ALC_TIMEOUT * 10 us. */
void
rge_port_quiesce(struct rge_port *port)
{
	struct alc_softc *sc = port->sc;

	if (!(sc->sc_arpcom.ac_if.if_flags & IFF_RUNNING))
		return;
	CSR_WRITE_4(sc, ALC_INTR_MASK, 0);
	CSR_WRITE_4(sc, ALC_INTR_STATUS, 0xFFFFFFFF);
	alc_stop_mac(sc);
	sc->sc_arpcom.ac_if.if_flags &= ~IFF_RUNNING;
}

const char *
rge_port_chip(struct rge_port *port)
{
	(void)port;
	return "Atheros";
}
