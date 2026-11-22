/*
 * Charon network drivers: the part of the OpenBSD kernel API that
 * sys/dev/pci/if_rge.c uses, provided on top of a few host calls
 * (rge_port.h) that the UEFI side implements.  The driver source is
 * compiled unmodified against this header.
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
#ifndef OBSD_COMPAT_H
#define OBSD_COMPAT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "rge_port.h"

#define _KERNEL		1
#define SMALL_KERNEL	1	/* no wake-on-LAN code */
#define NBPFILTER	0
#define NVLAN		0
#define NKSTAT		0

/* ---- types ---------------------------------------------------------- */
typedef unsigned char	u_char;
typedef unsigned short	u_short;
typedef unsigned int	u_int;
typedef unsigned long	u_long;
typedef uint8_t		u_int8_t;
typedef uint16_t	u_int16_t;
typedef uint32_t	u_int32_t;
typedef uint64_t	u_int64_t;
typedef char		*caddr_t;
typedef long		ssize_t;
typedef unsigned long	vaddr_t;
typedef unsigned long	paddr_t;
typedef int		pid_t;

_Static_assert(sizeof(long) == 8, "the OpenBSD code expects LP64");

#define __packed	__attribute__((__packed__))
#define __aligned(x)	__attribute__((__aligned__(x)))
#define __unused	__attribute__((__unused__))
#ifndef NULL
#define NULL		((void *)0)
#endif

#define nitems(a)	(sizeof((a)) / sizeof((a)[0]))
#define MIN(a, b)	(((a) < (b)) ? (a) : (b))
#define MAX(a, b)	(((a) > (b)) ? (a) : (b))
#define ISSET(t, f)	((t) & (f))
#define SET(t, f)	((t) |= (f))
#define CLR(t, f)	((t) &= ~(f))
#define READ_ONCE(x)	(x)

/* errno */
#define ENXIO		6
#define EIO		5
#define ENOMEM		12
#define EINVAL		22
#define EFBIG		27
#define ENETDOWN	50
#define ENETRESET	52
#define ENOBUFS		55
#define ETIMEDOUT	60
#define ENOTSUP		91
#define ENOENT		2

/* endian: x86-64 only */
#define htole16(x)	((uint16_t)(x))
#define htole32(x)	((uint32_t)(x))
#define htole64(x)	((uint64_t)(x))
#define letoh16(x)	((uint16_t)(x))
#define letoh32(x)	((uint32_t)(x))
#define letoh64(x)	((uint64_t)(x))
#define swap16(x)	__builtin_bswap16(x)
#define swap32(x)	__builtin_bswap32(x)
#define htons(x)	__builtin_bswap16(x)
#define ntohs(x)	__builtin_bswap16(x)
#define htonl(x)	__builtin_bswap32(x)
#define ntohl(x)	__builtin_bswap32(x)

/* ---- libkern -------------------------------------------------------- */
void	*memcpy(void *, const void *, size_t);
void	*memmove(void *, const void *, size_t);
void	*memset(void *, int, size_t);
int	 memcmp(const void *, const void *, size_t);
size_t	 strlen(const char *);
size_t	 strlcpy(char *, const char *, size_t);
int	 strcmp(const char *, const char *);
int	 printf(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
int	 snprintf(char *, size_t, const char *, ...)
	    __attribute__((__format__(__printf__, 3, 4)));
int	 vsnprintf(char *, size_t, const char *, va_list);
int	 fls(int);

#define KASSERT(x)	do { if (!(x)) obsd_panic(#x); } while (0)
void	 obsd_panic(const char *);
void	 panic(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));

/* ---- malloc --------------------------------------------------------- */
#define M_DEVBUF	2
#define M_TEMP		127
#define M_WAITOK	0x0001
#define M_NOWAIT	0x0002
#define M_CANFAIL	0x0004
#define M_ZERO		0x0008
void	*malloc(size_t, int, int);
void	*mallocarray(size_t, size_t, int, int);
void	 free(void *, int, size_t);

/* ---- time, locks, spl ----------------------------------------------- */
#define DELAY(us)	rge_host_stall((unsigned)(us))
#define delay(us)	rge_host_stall((unsigned)(us))
#define splnet()	0
#define splx(s)		((void)(s))
#define KERNEL_LOCK()	do { } while (0)
#define KERNEL_UNLOCK()	do { } while (0)
#define IPL_NET		0
#define IPL_MPSAFE	0
#define intr_barrier(ih)	((void)(ih))

struct timeout {
	void	(*to_func)(void *);
	void	 *to_arg;
	uint64_t  to_deadline;		/* in ticks of 10 ms */
	int	  to_pending;
	struct timeout *to_next;
};
void	timeout_set(struct timeout *, void (*)(void *), void *);
int	timeout_add_sec(struct timeout *, int);
int	timeout_del(struct timeout *);

struct task {
	void	(*t_func)(void *);
	void	 *t_arg;
};
#define task_set(t, fn, arg)	do { (t)->t_func = (fn); (t)->t_arg = (arg); } while (0)

/* ---- autoconf ------------------------------------------------------- */
struct device {
	char	dv_xname[16];
	int	dv_unit;
#ifdef OBSD_COMPAT_MII
	int	dv_flags;
	struct device *dv_parent;
#endif
};
#define DVF_ACTIVE	0x0001
struct cfattach {
	size_t	  ca_devsize;
	int	(*ca_match)(struct device *, void *, void *);
	void	(*ca_attach)(struct device *, struct device *, void *);
	int	(*ca_detach)(struct device *, int);
	int	(*ca_activate)(struct device *, int);
};
struct cfdriver {
	void		**cd_devs;
	const char	 *cd_name;
	int		  cd_class;
};
#define DV_IFNET		1
#define DVACT_POWERDOWN		6
void	config_mountroot(struct device *, void (*)(struct device *));
int	loadfirmware(const char *, u_char **, size_t *);

/* ---- bus_space ------------------------------------------------------ */
typedef unsigned long	bus_addr_t;
typedef unsigned long	bus_size_t;
typedef int		bus_space_tag_t;	/* 0 memory, 1 I/O */
typedef struct obsd_busmap *bus_space_handle_t;
struct obsd_busmap {
	struct rge_host	*host;
	unsigned	 bar;		/* BAR number 0..5 */
	int		 io;
};
#define BUS_SPACE_BARRIER_READ	1
#define BUS_SPACE_BARRIER_WRITE	2
#define bus_space_barrier(t, h, o, l, f)	do { } while (0)

static inline uint8_t
bus_space_read_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	(void)t;
	return (uint8_t)rge_host_reg_read(h->host, h->bar, h->io, 1, o);
}
static inline uint16_t
bus_space_read_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	(void)t;
	return (uint16_t)rge_host_reg_read(h->host, h->bar, h->io, 2, o);
}
static inline uint32_t
bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	(void)t;
	return rge_host_reg_read(h->host, h->bar, h->io, 4, o);
}
static inline void
bus_space_write_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    uint8_t v)
{
	(void)t;
	rge_host_reg_write(h->host, h->bar, h->io, 1, o, v);
}
static inline void
bus_space_write_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    uint16_t v)
{
	(void)t;
	rge_host_reg_write(h->host, h->bar, h->io, 2, o, v);
}
static inline void
bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    uint32_t v)
{
	(void)t;
	rge_host_reg_write(h->host, h->bar, h->io, 4, o, v);
}

/* ---- bus_dma -------------------------------------------------------- */
typedef struct rge_host *bus_dma_tag_t;
typedef struct bus_dma_segment {
	bus_addr_t	ds_addr;	/* address the device uses */
	bus_size_t	ds_len;
	void		*_ds_va;	/* host address (bus_dmamem_alloc) */
	void		*_ds_cookie;
} bus_dma_segment_t;
struct bus_dmamap {
	bus_size_t	 dm_mapsize;
	int		 dm_nsegs;
	int		 _dm_maxsegs;
	bus_dma_segment_t dm_segs[1];	/* really _dm_maxsegs */
};
typedef struct bus_dmamap *bus_dmamap_t;
#define BUS_DMA_WAITOK		0x0000
#define BUS_DMA_NOWAIT		0x0001
#define BUS_DMA_ALLOCNOW	0x0002
#define BUS_DMA_COHERENT	0x0004
#define BUS_DMA_ZERO		0x1000
#define BUS_DMA_64BIT		0x2000
#define BUS_DMASYNC_PREREAD	0x01
#define BUS_DMASYNC_POSTREAD	0x02
#define BUS_DMASYNC_PREWRITE	0x04
#define BUS_DMASYNC_POSTWRITE	0x08
struct mbuf;
int	bus_dmamap_create(bus_dma_tag_t, bus_size_t, int, bus_size_t,
	    bus_size_t, int, bus_dmamap_t *);
void	bus_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
int	bus_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *, bus_size_t,
	    void *, int);
int	bus_dmamap_load_mbuf(bus_dma_tag_t, bus_dmamap_t, struct mbuf *, int);
void	bus_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
/* common-buffer mappings on x86-64: nothing to synchronise */
#define bus_dmamap_sync(t, m, o, l, ops)				\
	do { (void)(t); (void)(m); (void)(o); (void)(l); (void)(ops); } while (0)
int	bus_dmamem_alloc(bus_dma_tag_t, bus_size_t, bus_size_t, bus_size_t,
	    bus_dma_segment_t *, int, int *, int);
void	bus_dmamem_free(bus_dma_tag_t, bus_dma_segment_t *, int);
int	bus_dmamem_map(bus_dma_tag_t, bus_dma_segment_t *, int, size_t,
	    caddr_t *, int);
void	bus_dmamem_unmap(bus_dma_tag_t, caddr_t, size_t);

/* ---- PCI ------------------------------------------------------------ */
typedef uint32_t		pcireg_t;
typedef struct rge_host		*pci_chipset_tag_t;
typedef int			pcitag_t;
typedef struct obsd_intr	*pci_intr_handle_t;
struct obsd_intr {
	int	(*ih_func)(void *);
	void	 *ih_arg;
};
struct pci_attach_args {
	pci_chipset_tag_t	pa_pc;
	pcitag_t		pa_tag;
	bus_dma_tag_t		pa_dmat;
	pcireg_t		pa_id;
	pcireg_t		pa_class;
};
struct pci_matchid {
	uint16_t	pm_vid;
	uint16_t	pm_pid;
};
struct intrmap;
int	pci_matchbyid(struct pci_attach_args *, const struct pci_matchid *,
	    int);
pcireg_t pci_conf_read(pci_chipset_tag_t, pcitag_t, int);
void	pci_conf_write(pci_chipset_tag_t, pcitag_t, int, pcireg_t);
int	pci_get_capability(pci_chipset_tag_t, pcitag_t, int, int *,
	    pcireg_t *);
int	pci_set_powerstate(pci_chipset_tag_t, pcitag_t, int);
int	pci_mapreg_map(struct pci_attach_args *, int, pcireg_t, int,
	    bus_space_tag_t *, bus_space_handle_t *, bus_addr_t *,
	    bus_size_t *, bus_size_t);
int	pci_intr_msix_count(struct pci_attach_args *);
int	pci_intr_map_msix(struct pci_attach_args *, int, pci_intr_handle_t *);
int	pci_intr_map_msi(struct pci_attach_args *, pci_intr_handle_t *);
int	pci_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *pci_intr_string(pci_chipset_tag_t, pci_intr_handle_t);
void	*pci_intr_establish(pci_chipset_tag_t, pci_intr_handle_t, int,
	    int (*)(void *), void *, const char *);
void	*pci_intr_establish_cpu(pci_chipset_tag_t, pci_intr_handle_t, int,
	    void *, int (*)(void *), void *, const char *);
void	pci_intr_disestablish(pci_chipset_tag_t, void *);
/* multi-vector interrupts are never used here (no MSI-X) */
#define INTRMAP_POWEROF2	1
#define IF_MAX_VECTORS		8
struct intrmap *intrmap_create(const struct device *, unsigned int,
	    unsigned int, unsigned int);
void	intrmap_destroy(struct intrmap *);
unsigned int intrmap_count(const struct intrmap *);
void	*intrmap_cpu(const struct intrmap *, unsigned int);
void	stoeplitz_to_key(void *, size_t);

/* ---- mbuf ----------------------------------------------------------- */
#define MCLBYTES	2048
#define MAXMCLBYTES	(64 * 1024)
#define MLEN		160
#define ETHER_ALIGN	2
struct pkthdr {
	int		len;
	uint16_t	csum_flags;
	uint16_t	ether_vtag;
	uint32_t	ph_flowid;
	uint16_t	ph_mss;
};
struct mbuf {
	struct mbuf	*m_next;
	struct mbuf	*m_nextpkt;
	caddr_t		 m_data;
	u_int		 m_len;
	uint16_t	 m_flags;
	struct pkthdr	 m_pkthdr;
	char		*m_ext_buf;	/* cluster, or NULL */
	u_int		 m_ext_size;
	char		 m_dat[MLEN];
};
#define M_EXT		0x0001
#define M_PKTHDR	0x0002
#define M_VLANTAG	0x0020
#define M_DONTWAIT	M_NOWAIT
#define MT_DATA		1
#define M_IPV4_CSUM_OUT		0x0001
#define M_TCP_CSUM_OUT		0x0002
#define M_UDP_CSUM_OUT		0x0004
#define M_IPV4_CSUM_IN_OK	0x0008
#define M_TCP_CSUM_IN_OK	0x0020
#define M_UDP_CSUM_IN_OK	0x0080
#define M_TCP_TSO		0x8000
#define M_FLOWID		0x4000
#define mtod(m, t)	((t)((m)->m_data))
struct mbuf *m_gethdr(int, int);
struct mbuf *m_get(int, int);
struct mbuf *MCLGETL(struct mbuf *, int, u_int);
#define MGET(m, how, type)	((m) = m_get((how), (type)))
struct mbuf *m_free(struct mbuf *);
void	m_freem(struct mbuf *);
void	m_adj(struct mbuf *, int);
void	m_cat(struct mbuf *, struct mbuf *);
int	m_defrag(struct mbuf *, int);
void	m_copydata(struct mbuf *, int, int, void *);
struct mbuf *m_getptr(struct mbuf *, int, int *);

struct mbuf_list {
	struct mbuf	*ml_head;
	struct mbuf	*ml_tail;
	u_int		 ml_len;
};
#define MBUF_LIST_INITIALIZER()	{ NULL, NULL, 0 }
void	ml_enqueue(struct mbuf_list *, struct mbuf *);
struct mbuf *ml_dequeue(struct mbuf_list *);

/* ---- ifnet ---------------------------------------------------------- */
#define IFNAMSIZ	16
#define ETHER_ADDR_LEN	6
#define ETHER_HDR_LEN	14
#define ETHER_CRC_LEN	4
#define ETHER_MIN_LEN	64
#define ETHER_VLAN_ENCAP_LEN	4
#define IFF_UP		0x0001
#define IFF_BROADCAST	0x0002
#define IFF_RUNNING	0x0040
#define IFF_PROMISC	0x0100
#define IFF_ALLMULTI	0x0200
#define IFF_SIMPLEX	0x0800
#define IFF_MULTICAST	0x8000
#define IFXF_MPSAFE	0x1
#define IFXF_MBUF_64BIT	0x2
#define IFCAP_CSUM_IPv4		0x00000001
#define IFCAP_CSUM_TCPv4	0x00000002
#define IFCAP_CSUM_UDPv4	0x00000004
#define IFCAP_VLAN_MTU		0x00000010
#define IFCAP_VLAN_HWTAGGING	0x00000020
#define IFCAP_CSUM_TCPv6	0x00000080
#define IFCAP_CSUM_UDPv6	0x00000100
#define IFCAP_TSOv4		0x00001000
#define IFCAP_TSOv6		0x00002000
#define IFCAP_WOL		0x00008000
#define LINK_STATE_UNKNOWN	0
#define LINK_STATE_DOWN		2
#define LINK_STATE_UP		4
#define LINK_STATE_HALF_DUPLEX	5
#define LINK_STATE_FULL_DUPLEX	6
#define LINK_STATE_IS_UP(s)	((s) >= LINK_STATE_UP)
#define IF_Mbps(x)	((uint64_t)(x) * 1000000ULL)
#define IF_Gbps(x)	(IF_Mbps((x) * 1000ULL))
#define SIOCSIFADDR	0x8001
#define SIOCSIFFLAGS	0x8002
#define SIOCGIFMEDIA	0x8003
#define SIOCSIFMEDIA	0x8004
#define SIOCGIFRXR	0x8005
#define SIOCGIFSFFPAGE	0x8006
#define IFSFF_ADDR_EEPROM	0xa0

struct ifnet;
#ifdef OBSD_COMPAT_MII
struct sockaddr_dl {
	uint8_t	sdl_len;
	uint8_t	sdl_family;
	uint8_t	sdl_data[30];		/* the link-level address */
};
#define LLADDR(s)	((caddr_t)((s)->sdl_data))
#endif
struct ifqueue {
	struct ifnet	*ifq_if;
	void		*ifq_softc;
	struct mbuf_list ifq_ml;
	u_int		 ifq_maxlen;
	int		 ifq_oactive;
};
struct ifiqueue {
	struct ifnet	*ifiq_if;
	void		*ifiq_softc;
};
struct if_rxring {
	u_int	rxr_alive;
	u_int	rxr_lwm;
	u_int	rxr_hwm;
};
struct if_rxring_info {
	u_int		ifr_size;
	struct if_rxring ifr_info;
};
struct if_rxrinfo {
	u_int	ifri_total;
};
struct if_sffpage {
	uint8_t	sff_addr;
	uint8_t	sff_page;
	uint8_t	sff_data[256];
};
struct ifreq {
	char	ifr_name[IFNAMSIZ];
	caddr_t	ifr_data;
};
struct ifmediareq {
	char		ifm_name[IFNAMSIZ];
	uint64_t	ifm_current;
	uint64_t	ifm_mask;
	uint64_t	ifm_status;
	uint64_t	ifm_active;
};
struct ifnet {
	void		*if_softc;
	char		 if_xname[IFNAMSIZ];
	int		 if_flags;
	int		 if_xflags;
	int		(*if_ioctl)(struct ifnet *, u_long, caddr_t);
	void		(*if_qstart)(struct ifqueue *);
	void		(*if_watchdog)(struct ifnet *);
	int		(*if_wol)(struct ifnet *, int);
	struct ifqueue	 if_snd;
	struct ifqueue	**if_ifqs;
	struct ifiqueue	**if_iqs;
	struct ifqueue	*_if_ifq0;
	struct ifiqueue	 _if_iq0;
	struct ifiqueue	*_if_iq0p;
	u_int		 if_hardmtu;
	u_int		 if_capabilities;
	int		 if_timer;
	int		 if_link_state;
	uint64_t	 if_baudrate;
	uint64_t	 if_ierrors;
	uint64_t	 if_oerrors;
	uint64_t	 if_collisions;
	caddr_t		 if_bpf;
	int		 _if_attached;
#ifdef OBSD_COMPAT_MII
	void		(*if_start)(struct ifnet *);	/* old-style start */
	uint64_t	 if_iqdrops;
	struct sockaddr_dl *if_sadl;
	struct sockaddr_dl _if_sadl0;
#endif
};
#define OBSD_MAXMULTI	32
struct ether_multi {
	uint8_t	enm_addrlo[ETHER_ADDR_LEN];
	uint8_t	enm_addrhi[ETHER_ADDR_LEN];
};
struct ether_multistep {
	int	e_index;
};
struct arpcom {
	struct ifnet	ac_if;
	uint8_t		ac_enaddr[ETHER_ADDR_LEN];
	int		ac_multirangecnt;
	int		ac_multicnt;
	struct ether_multi ac_multi[OBSD_MAXMULTI];
};
#define ETHER_FIRST_MULTI(step, ac, enm)				\
do {									\
	(step).e_index = 0;						\
	ETHER_NEXT_MULTI((step), (enm));				\
	(void)(ac);							\
} while (0)
/* the walk needs the arpcom: rge_iff() calls it "ac" */
#define ETHER_NEXT_MULTI(step, enm)					\
do {									\
	if ((step).e_index < ac->ac_multicnt)				\
		(enm) = &ac->ac_multi[(step).e_index++];		\
	else								\
		(enm) = NULL;						\
} while (0)

void	if_attach(struct ifnet *);
void	ether_ifattach(struct ifnet *);
void	if_attach_queues(struct ifnet *, unsigned int);
void	if_attach_iqueues(struct ifnet *, unsigned int);
void	if_link_state_change(struct ifnet *);
int	ether_ioctl(struct ifnet *, struct arpcom *, u_long, caddr_t);
uint32_t ether_crc32_be(const uint8_t *, size_t);
char	*ether_sprintf(u_char *);

void	ifq_init_maxlen(struct ifqueue *, unsigned int);
struct mbuf *ifq_dequeue(struct ifqueue *);
int	ifq_enqueue(struct ifqueue *, struct mbuf *);
void	ifq_purge(struct ifqueue *);
void	ifq_restart(struct ifqueue *);
void	ifq_serialize(struct ifqueue *, struct task *);
#define ifq_barrier(ifq)	((void)(ifq))
#define ifq_set_oactive(ifq)	((ifq)->ifq_oactive = 1)
#define ifq_clr_oactive(ifq)	((ifq)->ifq_oactive = 0)
#define ifq_is_oactive(ifq)	((ifq)->ifq_oactive)
int	ifiq_input(struct ifiqueue *, struct mbuf_list *);

void	if_rxr_init(struct if_rxring *, u_int, u_int);
u_int	if_rxr_get(struct if_rxring *, u_int);
#define if_rxr_put(rxr, n)	((rxr)->rxr_alive -= (n))
#define if_rxr_inuse(rxr)	((rxr)->rxr_alive)
#define if_rxr_livelocked(rxr)	((void)(rxr))
int	if_rxr_info_ioctl(struct if_rxrinfo *, u_int, struct if_rxring_info *);

/* ---- protocol headers (only what rge_tx_offload() looks at) --------- */
struct ether_header {
	uint8_t		ether_dhost[ETHER_ADDR_LEN];
	uint8_t		ether_shost[ETHER_ADDR_LEN];
	uint16_t	ether_type;
} __packed;
struct ip {
	uint8_t		ip_vhl;
	uint8_t		ip_tos;
	uint16_t	ip_len;
	uint16_t	ip_id;
	uint16_t	ip_off;
	uint8_t		ip_ttl;
	uint8_t		ip_p;
	uint16_t	ip_sum;
	uint32_t	ip_src;
	uint32_t	ip_dst;
} __packed;
struct udphdr {
	uint16_t	uh_sport;
	uint16_t	uh_dport;
	uint16_t	uh_ulen;
	uint16_t	uh_sum;
} __packed;
struct tcphdr;
struct ip6_hdr;
#define IPPROTO_TCP	6
#define IPPROTO_UDP	17
struct ether_extracted {
	struct ether_header	*eh;
	struct ip		*ip4;
	struct ip6_hdr		*ip6;
	struct tcphdr		*tcp;
	struct udphdr		*udp;
	u_int			 iplen;
	u_int			 iphlen;
	u_int			 tcphlen;
	u_int			 paylen;
};
void	ether_extract_headers(struct mbuf *, struct ether_extracted *);
uint16_t in_cksum(struct mbuf *, int);
#define tcpstat_add(c, v)	do { } while (0)
#define tcpstat_inc(c)		do { } while (0)

#ifdef OBSD_COMPAT_MII
/* ---- additions for drivers with the old start routine and an MII bus -- */
#define MGETHDR(m, how, type)	((m) = m_gethdr((how), (type)))
#define MCLGET(m, how)		((void)MCLGETL((m), (how), MCLBYTES))
#define bcopy(s, d, n)		memmove((d), (s), (n))
#define bzero(d, n)		memset((d), 0, (n))
#define ifq_empty(ifq)		((ifq)->ifq_ml.ml_len == 0)
#define EWOULDBLOCK		35
#define EBUSY			16
#define ENODEV			19
#define PZERO			22
#define MSEC_TO_NSEC(ms)	((uint64_t)(ms) * 1000000ULL)
#define DV_DULL			0
#define UNCONF			1
#define QUIET			0
#define DETACH_FORCE		0x01
#define DVACT_QUIESCE		2
#define DVACT_SUSPEND		3
#define DVACT_RESUME		4
#define DVACT_WAKEUP		5
#define EJUSTRETURN		(-2)
#define bus_space_unmap(t, h, s)	do { (void)(h); } while (0)
struct ether_vlan_header {
	uint8_t		evl_dhost[ETHER_ADDR_LEN];
	uint8_t		evl_shost[ETHER_ADDR_LEN];
	uint16_t	evl_encap_proto;
	uint16_t	evl_tag;
	uint16_t	evl_proto;
} __packed;
void	if_input(struct ifnet *, struct mbuf_list *);
void	if_detach(struct ifnet *);
void	ether_ifdetach(struct ifnet *);
pcireg_t pci_mapreg_type(pci_chipset_tag_t, pcitag_t, int);
int	timeout_add_msec(struct timeout *, int);
int	tsleep_nsec(const void *, int, const char *, uint64_t);

/* a miniature autoconf: children (PHYs) are found in obsd_children[] */
struct cfdata {
	const struct cfattach	*cf_attach;
	struct cfdriver		*cf_driver;
	int			 cf_loc[2];
};
typedef int (*cfprint_t)(void *, const char *);
typedef int (*cfmatch_t)(struct device *, void *, void *);
extern struct cfdata obsd_children[];	/* ends with cf_attach == NULL */
struct device *config_found_sm(struct device *, void *, cfprint_t, cfmatch_t);
int	config_detach(struct device *, int);
int	config_activate_children(struct device *, int);
#endif /* OBSD_COMPAT_MII */

#endif /* OBSD_COMPAT_H */
