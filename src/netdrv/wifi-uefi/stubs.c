/*
 * Charon Wi-Fi driver (OpenBSD net80211 + mwx on UEFI): EVERY kernel
 * function that is NOT ported but stubbed or reduced lives in this file.
 * The list of unfinished kernel pieces is this file; nothing is stubbed
 * anywhere else.  (What is not *exercised* is a different, longer list: the
 * whole image has never run on the hardware.)
 *
 * Copyright (c) 2026 Charon project.  ISC licence (see wifi_kern.c).
 * ieee80211_disable_wep() and ieee80211_disable_rsn() are taken from
 * OpenBSD sys/net80211/ieee80211_ioctl.c (Copyright (c) 2001 Atsushi Onoe,
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting; BSD 2-clause).
 */
#include "obsd_kern.h"
#include <net/if_media.h>
#include <net80211/ieee80211_var.h>

/* STUB. Real: sys/net80211/ieee80211_ioctl.c, the SIOC[GS]80211* ioctls
 * through which ifconfig sets the network name, keys, channel and reads scan
 * results.  Here the UEFI face (wifi_port_* in mwx_obsd.c) sets the same
 * fields of struct ieee80211com directly.  The driver's ioctl routine falls
 * back to this for requests it does not know. */
int
ieee80211_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	(void)ifp; (void)cmd; (void)data;
	return ENOTTY;
}

/* REAL CODE, copied from ieee80211_ioctl.c (which is not compiled). */
void
ieee80211_disable_wep(struct ieee80211com *ic)
{
	struct ieee80211_key *k;
	int i;

	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		k = &ic->ic_nw_keys[i];
		if (k->k_cipher != IEEE80211_CIPHER_NONE)
			(*ic->ic_delete_key)(ic, NULL, k);
		explicit_bzero(k, sizeof(*k));
	}
	ic->ic_flags &= ~IEEE80211_F_WEPON;
}

/* REAL CODE, copied from ieee80211_ioctl.c (which is not compiled). */
void
ieee80211_disable_rsn(struct ieee80211com *ic)
{
	ic->ic_flags &= ~(IEEE80211_F_PSK | IEEE80211_F_RSNON);
	ic->ic_flags &= ~IEEE80211_F_MFPR;
	explicit_bzero(ic->ic_psk, sizeof(ic->ic_psk));
	ic->ic_rsnprotos = 0;
	ic->ic_rsnakms = 0;
	ic->ic_rsngroupcipher = 0;
	ic->ic_rsnciphers = 0;
}

/* STUB. Real: sys/net/rtsock.c, sends a routing socket message with the
 * network name, channel and flags to user programs (dhcpleased, ifconfig)
 * when the 802.11 state changes.  No such listener exists before boot. */
void
rtm_80211info(struct ifnet *ifp, struct if_ieee80211_data *ifie)
{
	(void)ifp; (void)ifie;
}

/* STUB. Real: sys/net/if_ethersubr.c, resolves the next hop (ARP/ND) and
 * builds the Ethernet header for a packet of the kernel's IP stack, then
 * queues it.  net80211's ieee80211_output() calls it for non-raw packets.
 * Here frames come complete from the firmware's network stack through the
 * Simple Network Protocol and are queued on if_snd directly, so this is
 * never reached; a packet that arrives is dropped. */
int
ether_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	(void)ifp; (void)dst; (void)rt;
	m_freem(m);
	return ENETDOWN;
}

/* STUB. Real: sys/net/if_ethersubr.c, hands one received frame to the
 * kernel's protocols.  Received frames go to the Simple Network Protocol
 * receive queue through if_input() instead. */
void
ether_input(struct ifnet *ifp, struct mbuf *m)
{
	(void)ifp;
	m_freem(m);
}

/* STUB. Real: sys/net/if_ethersubr.c, keep the list of multicast addresses
 * the interface listens to.  mwx has no multicast filter of its own; group
 * addressed frames are received through the access point anyway. */
int
ether_addmulti(struct ifreq *ifr, struct arpcom *ac)
{
	(void)ifr; (void)ac;
	return ENETRESET;
}

int
ether_delmulti(struct ifreq *ifr, struct arpcom *ac)
{
	(void)ifr; (void)ac;
	return ENETRESET;
}

/* STUB. Real: sys/net/if.c, puts the interface into a named group ("wlan")
 * for ifconfig and pf.  Nothing uses groups here. */
int
if_addgroup(struct ifnet *ifp, const char *group)
{
	(void)ifp; (void)group;
	return 0;
}

/* STUB. Real: sys/net/if.c, removes the addresses and routes of an
 * interface that goes away.  There are none. */
void
if_deactivate(struct ifnet *ifp)
{
	(void)ifp;
}

/* STUB. Real: sys/kern/uipc_mbuf2.c, finds a tag attached to a packet.
 * net80211 looks for the tag bpf(4) puts on injected raw 802.11 frames;
 * there is no bpf and mbufs carry no tags here. */
struct m_tag *
m_tag_find(struct mbuf *m, int type, struct m_tag *prev)
{
	(void)m; (void)type; (void)prev;
	return NULL;
}

/* STUB. Real: copy from/to user space and the super-user check, used by
 * ioctl paths only.  There is no user space. */
int
copyin(const void *uaddr, void *kaddr, size_t len)
{
	(void)uaddr; (void)kaddr; (void)len;
	return EFAULT;
}

int
copyout(const void *kaddr, void *uaddr, size_t len)
{
	(void)kaddr; (void)uaddr; (void)len;
	return EFAULT;
}

int
suser(struct proc *p)
{
	(void)p;
	return 0;
}
