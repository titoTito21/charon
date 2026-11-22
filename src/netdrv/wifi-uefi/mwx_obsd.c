/*
 * Charon network drivers: OpenBSD's mwx(4) (MediaTek MT7921/MT7922 Wi-Fi)
 * and net80211 behind the port interface of the UEFI side (rge_port.h for
 * the Simple Network Protocol, wifi_port.h for scanning and joining).
 * sys/dev/pci/if_mwx.c is compiled unmodified by including it here.
 *
 * EXPERIMENTAL: this driver has NEVER RUN on the hardware (no emulator has
 * these cards).  It compiles and links; nothing more is claimed.
 *
 * Copyright (c) 2026 Charon project.  ISC licence (see wifi_kern.c).
 */
#include "obsd_kern.h"
#include "wifi_port.h"

/* The driver itself (ISC licence, Claudio Jeker). */
#include <dev/pci/if_mwx.c>

#include <crypto/md5.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/hmac.h>

struct rge_port {
	struct obsd_env		*env;
	struct mwx_softc	*sc;
	struct pci_attach_args	 pa;
	int			 target_set;	/* a network was chosen */
	uint8_t			 scan_base;	/* ic_scan_count at scan start */
};

/* A network name no access point has: scanning without joining. */
static const uint8_t scan_only_essid[IEEE80211_NWID_LEN] = {
	0x01, 'C', 'h', 'a', 'r', 'o', 'n', 0x02, 's', 'c', 'a', 'n', 0x03,
	0x7f, 0x7f, 0x7f, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0e, 0x0f, 0x10,
	0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18
};

int
rge_port_match(uint16_t vendor, uint16_t product)
{
	struct pci_attach_args pa;

	memset(&pa, 0, sizeof(pa));
	pa.pa_id = (pcireg_t)vendor | ((pcireg_t)product << 16);
#ifdef NETDRV_TEST_ID
	/* TEST BUILDS ONLY (never shipped): also bind a device an emulator
	 * has, so that binding, the buffer pool, register mapping, net80211's
	 * attach, the firmware file reader and the error path can run up to
	 * the point where the chip does not answer. */
	if ((((uint32_t)vendor << 16) | product) == NETDRV_TEST_ID)
		return 1;
#endif
	return mwx_match(NULL, NULL, &pa);
}

unsigned
rge_port_ids(unsigned index, uint16_t *vendor, uint16_t *product)
{
	if (index >= nitems(mwx_devices))
		return 0;
	*vendor = mwx_devices[index].pm_vid;
	*product = mwx_devices[index].pm_pid;
	return 1;
}

static void
no_target(struct rge_port *port)
{
	struct ieee80211com *ic = &port->sc->sc_ic;

	ieee80211_disable_rsn(ic);
	memcpy(ic->ic_des_essid, scan_only_essid, IEEE80211_NWID_LEN);
	ic->ic_des_esslen = IEEE80211_NWID_LEN;
	ic->ic_flags &= ~IEEE80211_F_AUTO_JOIN;
	port->target_set = 0;
}

struct rge_port *
rge_port_attach(struct rge_host *host, uint16_t vendor, uint16_t product,
    uint8_t mac[6])
{
	struct obsd_env *env;
	struct rge_port *port;
	struct mwx_softc *sc;
	void (*hook)(struct device *);

	env = obsd_env_create(host);
	if (env == NULL) {
		rge_host_log("mwx: no memory for the buffer pool\n");
		return NULL;
	}
	obsd_cur = env;
	wifi_forget_tasks();
	port = malloc(sizeof(*port), M_DEVBUF, M_NOWAIT | M_ZERO);
	sc = malloc(sizeof(*sc), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (port == NULL || sc == NULL)
		goto fail;
	port->env = env;
	port->sc = sc;
	port->pa.pa_pc = host;
	port->pa.pa_dmat = host;
	port->pa.pa_id = (pcireg_t)vendor | ((pcireg_t)product << 16);
#ifdef NETDRV_TEST_ID
	if ((((uint32_t)vendor << 16) | product) == NETDRV_TEST_ID)
		port->pa.pa_id = PCI_VENDOR_MEDIATEK |
		    ((pcireg_t)PCI_PRODUCT_MEDIATEK_MT7921 << 16);
#endif
	strlcpy(sc->sc_dev.dv_xname, "mwx0", sizeof(sc->sc_dev.dv_xname));

	printf("mwx0");
	mwx_attach(NULL, &sc->sc_dev, &port->pa);
	hook = env->mountroot_hook;
	if (hook == NULL)		/* attach gave up and said why */
		goto fail;
	env->mountroot_hook = NULL;
	/* OpenBSD's if_attach() gives the send queue its length; the shim's
	 * does not (an empty limit would refuse every frame, the EAPOL key
	 * frames of the handshake included). */
	ifq_init_maxlen(&sc->sc_ic.ic_if.if_snd, OBSD_IFQ_MAX);
	/* mwx_attach_hook: loads the two firmware files, starts the chip's
	 * processor, reads the address and the channel list. */
	hook(&sc->sc_dev);
	if (memcmp(sc->sc_ic.ic_myaddr, etheranyaddr, ETHER_ADDR_LEN) == 0) {
		rge_host_log("mwx0: no hardware address: the firmware did not "
		    "start (are both files in \\EFI\\HAK\\firmware?)\n");
		goto fail;
	}
	memcpy(mac, sc->sc_ic.ic_myaddr, ETHER_ADDR_LEN);
	no_target(port);
	return port;
fail:
	wifi_forget_tasks();
	obsd_env_destroy(env);		/* frees port and sc as well */
	return NULL;
}

void
rge_port_detach(struct rge_port *port)
{
	struct obsd_env *env = port->env;
	struct ifnet *ifp = &port->sc->sc_ic.ic_if;
	struct mbuf *m;

	obsd_cur = env;
	if (ifp->if_flags & IFF_RUNNING)
		mwx_stop(ifp);
	mwx_dma_disable(port->sc, 1);
	wifi_forget_tasks();
	while ((m = ml_dequeue(&env->rxq)) != NULL)
		m_freem(m);
	explicit_bzero(port->sc->sc_ic.ic_psk, sizeof(port->sc->sc_ic.ic_psk));
	obsd_env_destroy(env);
}

int
rge_port_up(struct rge_port *port)
{
	struct ifnet *ifp = &port->sc->sc_ic.ic_if;

	obsd_cur = port->env;
	if (ifp->if_flags & IFF_RUNNING)
		return 0;		/* scanning or joined already */
	ifp->if_flags |= IFF_UP;
	mwx_init(ifp);			/* ends in the SCAN state */
	return (ifp->if_flags & IFF_RUNNING) ? 0 : 1;
}

void
rge_port_down(struct rge_port *port)
{
	struct ifnet *ifp = &port->sc->sc_ic.ic_if;
	struct mbuf *m;

	obsd_cur = port->env;
	ifp->if_flags &= ~IFF_UP;
	if (ifp->if_flags & IFF_RUNNING)
		mwx_stop(ifp);
	ifq_purge(&ifp->if_snd);
	while ((m = ml_dequeue(&port->env->rxq)) != NULL)
		m_freem(m);
}

int
rge_port_running(struct rge_port *port)
{
	return (port->sc->sc_ic.ic_if.if_flags & IFF_RUNNING) != 0;
}

void
rge_port_poll(struct rge_port *port)
{
	struct obsd_env *env = port->env;

	obsd_cur = env;
	/* The handler the driver established (mwx_intr), unchanged; the
	 * chip's interrupt line is never enabled. */
	if (env->intr_set)
		env->intr.ih_func(env->intr.ih_arg);
}

void
rge_port_tick(struct rge_port *port)
{
	obsd_cur = port->env;
	obsd_tick(port->env);		/* timeouts, interface watchdog */
	ticks = (int)port->env->ticks;
	wifi_run_tasks();		/* state changes wait for the chip */
}

int
wifi_port_state(struct rge_port *port)
{
	struct ieee80211com *ic = &port->sc->sc_ic;

	if (!port->target_set || !(ic->ic_if.if_flags & IFF_RUNNING) ||
	    ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == NULL)
		return 0;
	if ((ic->ic_flags & IEEE80211_F_RSNON) && !ic->ic_bss->ni_port_valid)
		return 1;
	return 2;
}

int
rge_port_link(struct rge_port *port)
{
	return wifi_port_state(port) == 2;
}

int
rge_port_tx(struct rge_port *port, const void *frame, size_t len)
{
	struct ifnet *ifp = &port->sc->sc_ic.ic_if;
	struct mbuf *m;

	obsd_cur = port->env;
	if (len > MCLBYTES - 128 || len < ETHER_HDR_LEN)
		return 2;
	if (wifi_port_state(port) != 2)
		return 2;		/* not connected: dropped */
	if (ifq_is_oactive(&ifp->if_snd) ||
	    ifp->if_snd.ifq_ml.ml_len >= OBSD_IFQ_MAX)
		return 1;
	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (m == NULL)
		return 1;
	m->m_data += 96;		/* room for the 802.11 and LLC headers */
	memcpy(m->m_data, frame, len);
	m->m_len = m->m_pkthdr.len = (int)len;
	if (ifq_enqueue(&ifp->if_snd, m) != 0)
		return 1;
	mwx_start(ifp);			/* ieee80211_encap, CCMP, the ring */
	return 0;
}

size_t
rge_port_rx_peek(struct rge_port *port)
{
	struct mbuf *m = port->env->rxq.ml_head;

	return (m != NULL) ? (size_t)m->m_pkthdr.len : 0;
}

/* net80211 has turned the frame into an Ethernet frame (ieee80211_decap)
 * and kept the EAPOL key frames for itself. */
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

void
rge_port_filter(struct rge_port *port, int promiscuous, const uint8_t *mcast,
    unsigned count)
{
	/* mwx has no receive filter to program: a station receives what the
	 * access point sends to it and to group addresses. */
	(void)port; (void)promiscuous; (void)mcast; (void)count;
}

void
rge_port_set_mac(struct rge_port *port, const uint8_t mac[6])
{
	/* Not supported: the address is the one the chip's firmware reports. */
	(void)port; (void)mac;
}

/* ExitBootServices: no DMA after the system takes the memory.  No memory is
 * allocated or freed. */
void
rge_port_quiesce(struct rge_port *port)
{
	obsd_cur = port->env;
	mwx_write(port->sc, MT_WFDMA0_HOST_INT_ENA, 0);
	mwx_dma_disable(port->sc, 1);
	port->sc->sc_ic.ic_if.if_flags &= ~IFF_RUNNING;
}

const char *
rge_port_chip(struct rge_port *port)
{
	switch (PCI_PRODUCT(port->pa.pa_id)) {
	case PCI_PRODUCT_MEDIATEK_MT7920:	return "MT7920";
	case PCI_PRODUCT_MEDIATEK_MT7921:	return "MT7921";
	case PCI_PRODUCT_MEDIATEK_MT7921K:	return "MT7921K";
	case PCI_PRODUCT_MEDIATEK_MT7922:
	case PCI_PRODUCT_MEDIATEK_RZ616:	return "MT7922";
	case PCI_PRODUCT_MEDIATEK_MT7925:
	case PCI_PRODUCT_MEDIATEK_RZ717:	return "MT7925";
	default:				return "unknown";
	}
}

/* ---- scanning and joining -------------------------------------------- */
static void
restart(struct rge_port *port)
{
	struct ifnet *ifp = &port->sc->sc_ic.ic_if;

	/* What mwx_ioctl() does on ENETRESET. */
	if (ifp->if_flags & IFF_RUNNING)
		mwx_stop(ifp);
	ifp->if_flags |= IFF_UP;
	mwx_init(ifp);
}

int
wifi_port_scan_start(struct rge_port *port)
{
	struct ieee80211com *ic = &port->sc->sc_ic;

	obsd_cur = port->env;
	port->scan_base = ic->ic_scan_count;
	if (port->target_set && (ic->ic_if.if_flags & IFF_RUNNING))
		return 0;	/* joined or joining: the kept list is used */
	no_target(port);
	if (!(ic->ic_if.if_flags & IFF_RUNNING))
		restart(port);
	return (ic->ic_if.if_flags & IFF_RUNNING) ? 0 : -1;
}

int
wifi_port_scan_done(struct rge_port *port)
{
	struct ieee80211com *ic = &port->sc->sc_ic;

	if (port->target_set)
		return 1;
	return ic->ic_scan_count != port->scan_base;
}

unsigned
wifi_port_scan_results(struct rge_port *port, struct wifi_bss *out,
    unsigned max)
{
	struct ieee80211com *ic = &port->sc->sc_ic;
	struct ieee80211_node *ni;
	unsigned n = 0;
	int dbm, quality;

	obsd_cur = port->env;
	RBT_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
		struct wifi_bss *b;

		if (n >= max)
			break;
		if (ni->ni_esslen == 0 || ni->ni_esslen > IEEE80211_NWID_LEN ||
		    ni == ic->ic_bss)
			continue;
		b = &out[n++];
		memset(b, 0, sizeof(*b));
		memcpy(b->ssid, ni->ni_essid, ni->ni_esslen);
		b->ssid_len = ni->ni_esslen;
		/* ic_max_rssi is 0 for mwx: ni_rssi is a dBm value. */
		dbm = (int8_t)ni->ni_rssi;
		quality = 2 * (dbm + 100);
		b->quality = (uint8_t)(quality < 0 ? 0 : quality > 100 ? 100 :
		    quality);
		if (ni->ni_rsnprotos != IEEE80211_PROTO_NONE) {
			if (ni->ni_rsnakms & IEEE80211_AKM_PSK)
				b->akms |= WIFI_AKM_PSK;
			if (ni->ni_rsnakms & IEEE80211_AKM_SHA256_PSK)
				b->akms |= WIFI_AKM_PSK_SHA256;
			if (ni->ni_rsnakms & (IEEE80211_AKM_8021X |
			    IEEE80211_AKM_SHA256_8021X))
				b->akms |= WIFI_AKM_8021X;
			if (ni->ni_rsnakms & IEEE80211_AKM_SAE)
				b->akms |= WIFI_AKM_SAE;
			if (b->akms == 0)
				b->akms = WIFI_AKM_OTHER;
			if (ni->ni_rsnciphers & IEEE80211_CIPHER_CCMP)
				b->ciphers |= WIFI_CIPHER_CCMP;
			if (ni->ni_rsnciphers & IEEE80211_CIPHER_TKIP)
				b->ciphers |= WIFI_CIPHER_TKIP;
		} else if (ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY)
			b->akms = WIFI_AKM_OTHER;	/* WEP: not offered */
	}
	return n;
}

int
wifi_port_connect(struct rge_port *port, const uint8_t *ssid,
    unsigned ssid_len, const uint8_t psk[32])
{
	struct ieee80211com *ic = &port->sc->sc_ic;

	obsd_cur = port->env;
	if (ssid_len == 0 || ssid_len > IEEE80211_NWID_LEN)
		return -1;
	ieee80211_disable_rsn(ic);
	ieee80211_disable_wep(ic);
	memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
	memcpy(ic->ic_des_essid, ssid, ssid_len);
	ic->ic_des_esslen = (int)ssid_len;
	if (psk != NULL) {
		/* What "ifconfig mwx0 nwid NAME wpakey PASSWORD" sets: WPA2
		 * only, pre-shared key, CCMP. */
		memcpy(ic->ic_psk, psk, IEEE80211_PMK_LEN);
		ic->ic_flags |= IEEE80211_F_PSK | IEEE80211_F_RSNON;
		ic->ic_rsnprotos = IEEE80211_PROTO_RSN;
		ic->ic_rsnakms = IEEE80211_AKM_PSK | IEEE80211_AKM_SHA256_PSK;
		ic->ic_rsngroupcipher = IEEE80211_CIPHER_CCMP;
		ic->ic_rsnciphers = IEEE80211_CIPHER_CCMP;
	}
	port->target_set = 1;
	restart(port);			/* scan, authenticate, associate */
	return (ic->ic_if.if_flags & IFF_RUNNING) ? 0 : -1;
}

void
wifi_port_disconnect(struct rge_port *port)
{
	obsd_cur = port->env;
	no_target(port);
	if (port->sc->sc_ic.ic_if.if_flags & IFF_RUNNING)
		restart(port);
}

/* PBKDF2-HMAC-SHA1, 4096 rounds, 32 bytes (IEEE 802.11-2020 J.4), with the
 * kernel's own HMAC (sys/crypto/hmac.c). */
void
wifi_pbkdf2_sha1(const char *pass, size_t passlen, const uint8_t *ssid,
    size_t ssidlen, uint8_t out[32])
{
	HMAC_SHA1_CTX ctx;
	uint8_t u[SHA1_DIGEST_LENGTH], t[SHA1_DIGEST_LENGTH], be[4];
	unsigned block, round, i;

	for (block = 1; block <= 2; block++) {
		be[0] = be[1] = be[2] = 0;
		be[3] = (uint8_t)block;
		HMAC_SHA1_Init(&ctx, (const uint8_t *)pass, (u_int)passlen);
		HMAC_SHA1_Update(&ctx, ssid, (u_int)ssidlen);
		HMAC_SHA1_Update(&ctx, be, 4);
		HMAC_SHA1_Final(u, &ctx);
		memcpy(t, u, sizeof(t));
		for (round = 1; round < 4096; round++) {
			HMAC_SHA1_Init(&ctx, (const uint8_t *)pass,
			    (u_int)passlen);
			HMAC_SHA1_Update(&ctx, u, sizeof(u));
			HMAC_SHA1_Final(u, &ctx);
			for (i = 0; i < sizeof(t); i++)
				t[i] ^= u[i];
		}
		memcpy(out + (block - 1) * 20, t, block == 1 ? 20 : 12);
	}
	explicit_bzero(&ctx, sizeof(ctx));
	explicit_bzero(u, sizeof(u));
	explicit_bzero(t, sizeof(t));
}
