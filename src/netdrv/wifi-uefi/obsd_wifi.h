/*
 * Charon network drivers: additions to the OpenBSD kernel API shim
 * (obsd_compat.h, copied from src/netdrv/rge-uefi) that OpenBSD's net80211
 * and the Wi-Fi driver mwx(4) need.  EXPERIMENTAL: the image built with
 * this header has NEVER RUN on the hardware (no emulator has these cards).
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
#ifndef OBSD_WIFI_H
#define OBSD_WIFI_H

/* ---- types, attributes ---------------------------------------------- */
typedef long long	time_t;
typedef long		suseconds_t;
typedef long long	off_t;
typedef unsigned char	sa_family_t;
typedef unsigned int	uint;
typedef unsigned long	vsize_t;
typedef int		dev_t;
typedef unsigned long long u_quad_t;
#define __dead		__attribute__((__noreturn__))
#define __predict_true(x)	__builtin_expect(!!(x), 1)
#define __predict_false(x)	__builtin_expect(!!(x), 0)
#define __BEGIN_DECLS
#define __END_DECLS
#define __CONCAT(a, b)	a ## b
#ifndef __bounded__
#define __bounded__(...)
#endif
#define NBBY		8
#define howmany(x, y)	(((x) + ((y) - 1)) / (y))
#define roundup(x, y)	((((x) + ((y) - 1)) / (y)) * (y))
#define setbit(a, i)	(((uint8_t *)(a))[(i) / NBBY] |= 1 << ((i) % NBBY))
#define clrbit(a, i)	(((uint8_t *)(a))[(i) / NBBY] &= ~(1 << ((i) % NBBY)))
#define isset(a, i)	(((const uint8_t *)(a))[(i) / NBBY] & (1 << ((i) % NBBY)))
#define isclr(a, i)	((((const uint8_t *)(a))[(i) / NBBY] & (1 << ((i) % NBBY))) == 0)
static inline u_int min(u_int a, u_int b) { return a < b ? a : b; }
static inline u_int max(u_int a, u_int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int abs(int a) { return a < 0 ? -a : a; }

/* errno (the values of OpenBSD) */
#define EPERM		1
#define EINTR		4
#define E2BIG		7
#define EACCES		13
#define EFAULT		14
#define EEXIST		17
#define ENOTTY		25
#define ENOSPC		28
#define ERANGE		34
#define EAGAIN		35
#define EINPROGRESS	36
#define EALREADY	37
#define EMSGSIZE	40
#define EOPNOTSUPP	45
#define EAFNOSUPPORT	47
#define EADDRINUSE	48
#define EISCONN		56
#define ENOTCONN	57
#define EHOSTDOWN	64
#define ERESTART	(-1)

/* endian: x86-64 only */
#define le16toh(x)	((uint16_t)(x))
#define le32toh(x)	((uint32_t)(x))
#define le64toh(x)	((uint64_t)(x))
#define htobe16(x)	__builtin_bswap16(x)
#define htobe32(x)	__builtin_bswap32(x)
#define htobe64(x)	__builtin_bswap64(x)
#define betoh16(x)	__builtin_bswap16(x)
#define betoh32(x)	__builtin_bswap32(x)
#define betoh64(x)	__builtin_bswap64(x)
#define be16toh(x)	__builtin_bswap16(x)
#define be32toh(x)	__builtin_bswap32(x)
#define be64toh(x)	__builtin_bswap64(x)
#define swap64(x)	__builtin_bswap64(x)

#define LITTLE_ENDIAN	1234
#define BIG_ENDIAN	4321
#define BYTE_ORDER	LITTLE_ENDIAN
#define _LITTLE_ENDIAN	1234
#define _BIG_ENDIAN	4321
#define _BYTE_ORDER	_LITTLE_ENDIAN
#define CTASSERT(x)	_Static_assert(x, #x)
#define PAGE_SIZE	4096
#define PAGE_SHIFT	12
#define ALIGNED_POINTER(p, t)	1
#define MINCLSIZE	(MHLEN + MLEN + 1)
#define IFQ_MAXLEN	256
#define DLT_IEEE802_11	105
#define DLT_IEEE802_11_RADIO	127
#define IF_WIRELESS_DEFAULT_PRIORITY	4
#define BUS_DMA_READ	0x0200
#define BUS_DMA_WRITE	0x0400
struct if_ieee80211_data {
	uint8_t		ifie_channel;
	uint8_t		ifie_nwid_len;
	uint32_t	ifie_flags;
	uint32_t	ifie_xflags;
	uint8_t		ifie_nwid[32];
	uint8_t		ifie_addr[6];
};
struct ifnet;
void	rtm_80211info(struct ifnet *, struct if_ieee80211_data *);

/* ---- libkern -------------------------------------------------------- */
#define bcmp(a, b, n)	memcmp((a), (b), (n))
int	 strncmp(const char *, const char *, size_t);
size_t	 strlcat(char *, const char *, size_t);
int	 timingsafe_bcmp(const void *, const void *, size_t);
void	 explicit_bzero(void *, size_t);
uint32_t arc4random(void);
uint32_t arc4random_uniform(uint32_t);
void	 arc4random_buf(void *, size_t);
int	 ffs(int);
#define LOG_ERR		3
#define LOG_WARNING	4
#define LOG_NOTICE	5
#define LOG_INFO	6
#define LOG_DEBUG	7
void	 log(int, const char *, ...) __attribute__((__format__(__printf__, 2, 3)));
#define KASSERTMSG(x, ...)	KASSERT(x)
#define splassert(l)		do { } while (0)
#define splsoftnet()		0
#define NET_LOCK()		do { } while (0)
#define NET_UNLOCK()		do { } while (0)
#define NET_ASSERT_LOCKED()	do { } while (0)
#define KERNEL_ASSERT_LOCKED()	do { } while (0)
#define IPL_SOFTNET		0

/* ---- time ----------------------------------------------------------- */
struct timeval {
	time_t		tv_sec;
	suseconds_t	tv_usec;
};
struct timespec {
	time_t		tv_sec;
	long		tv_nsec;
};
#define timersub(a, b, r) do {						\
	(r)->tv_sec = (a)->tv_sec - (b)->tv_sec;			\
	(r)->tv_usec = (a)->tv_usec - (b)->tv_usec;			\
	if ((r)->tv_usec < 0) { (r)->tv_sec--; (r)->tv_usec += 1000000; } \
} while (0)
#define timercmp(a, b, cmp)						\
	(((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_usec cmp (b)->tv_usec) : \
	((a)->tv_sec cmp (b)->tv_sec))
#define timerclear(a)	((a)->tv_sec = (a)->tv_usec = 0)
#define timerisset(a)	((a)->tv_sec || (a)->tv_usec)
#define hz		100
extern int ticks;			/* advanced by the 10 ms poll */
void	getmicrouptime(struct timeval *);
void	getmicrotime(struct timeval *);
time_t	getuptime(void);
uint64_t getnsecuptime(void);
int	ratecheck(struct timeval *, const struct timeval *);
int	ppsratecheck(struct timeval *, int *, int);
#define SEC_TO_NSEC(s)		((uint64_t)(s) * 1000000000ULL)
#define USEC_TO_NSEC(us)	((uint64_t)(us) * 1000ULL)
#define INFSLP			0xffffffffffffffffULL
#define PCATCH			0x100
#define PRIBIO			16
#define PWAIT			32
int	timeout_add(struct timeout *, int);
int	timeout_add_usec(struct timeout *, int);
#define timeout_pending(to)	((to)->to_pending)
#define timeout_initialized(to)	((to)->to_func != NULL)
#define timeout_del_barrier(to)	timeout_del(to)
void	wakeup(const void *);
#define wakeup_one(i)		wakeup(i)
int	tsleep(const void *, int, const char *, int);

/* locks: one thread of control, nothing to lock */
struct rwlock { int dummy; };
struct mutex { int dummy; };
#define RW_WRITE	1
#define RW_READ		2
#define RW_INTR		0x10
#define RW_NOSLEEP	0x40
#define rw_init(l, n)		do { (void)(l); } while (0)
#define rw_enter(l, f)		((void)(l), 0)
#define rw_exit(l)		do { (void)(l); } while (0)
#define rw_enter_write(l)	do { (void)(l); } while (0)
#define rw_exit_write(l)	do { (void)(l); } while (0)
#define rw_assert_wrlock(l)	do { (void)(l); } while (0)
#define mtx_init(m, ipl)	do { (void)(m); } while (0)
#define mtx_enter(m)		do { (void)(m); } while (0)
#define mtx_leave(m)		do { (void)(m); } while (0)
#define MUTEX_INITIALIZER(ipl)	{ 0 }
#define atomic_inc_int(p)	((void)(++*(p)))
#define atomic_dec_int(p)	((void)(--*(p)))
#define atomic_setbits_int(p, v)	((void)(*(p) |= (v)))
#define atomic_clearbits_int(p, v)	((void)(*(p) &= ~(v)))
#define atomic_load_int(p)	(*(p))
#define atomic_store_int(p, v)	((void)(*(p) = (v)))
#define membar_sync()		do { } while (0)

/* ---- tasks: run from the poll loop, never nested --------------------- */
struct taskq { int dummy; };
extern struct taskq *const systq, *const systqmp;
int	task_add(struct taskq *, struct task *);
int	task_del(struct taskq *, struct task *);
struct taskq *taskq_create(const char *, unsigned int, int, unsigned int);
void	taskq_destroy(struct taskq *);
#define taskq_barrier(tq)	do { (void)(tq); } while (0)
#define TASKQ_MPSAFE		1

/* ---- mbuf additions --------------------------------------------------- */
#define MSIZE		256
#define MHLEN		MLEN
#define M_WAIT		M_WAITOK
#define M_BCAST		0x0100
#define M_MCAST		0x0200
#define M_LINK0		0x0040
#define MT_HEADER	2
#define MT_CONTROL	6
struct mbuf *m_clget(struct mbuf *, int, u_int);
struct mbuf *m_pullup(struct mbuf *, int);
struct mbuf *m_prepend(struct mbuf *, int, int);
struct mbuf *m_dup_pkt(struct mbuf *, u_int, int);
struct mbuf *m_split(struct mbuf *, int, int);
int	m_dup_pkthdr(struct mbuf *, struct mbuf *, int);
int	m_copyback(struct mbuf *, int, int, const void *, int);
void	m_align(struct mbuf *, int);
int	m_leadingspace(struct mbuf *);
int	m_trailingspace(struct mbuf *);
struct m_tag { int m_tag_id; };
struct m_tag *m_tag_find(struct mbuf *, int, struct m_tag *);
#define PACKET_TAG_DLT	0x0100
#define M_LEADINGSPACE(m)	m_leadingspace(m)
#define M_TRAILINGSPACE(m)	m_trailingspace(m)
#define M_PREPEND(m, plen, how)	((m) = m_prepend((m), (plen), (how)))
#define M_ALIGN(m, len)		m_align((m), (len))
#define MH_ALIGN(m, len)	m_align((m), (len))
struct mbuf_queue {
	struct mbuf_list mq_list;
	u_int		 mq_maxlen;
	u_int		 mq_drops;
};
void	mq_init(struct mbuf_queue *, u_int, int);
int	mq_enqueue(struct mbuf_queue *, struct mbuf *);
struct mbuf *mq_dequeue(struct mbuf_queue *);
int	mq_purge(struct mbuf_queue *);
#define mq_empty(mq)	((mq)->mq_list.ml_len == 0)
#define mq_len(mq)	((mq)->mq_list.ml_len)
void	ml_init(struct mbuf_list *);
int	ml_purge(struct mbuf_list *);
#define ml_empty(ml)	((ml)->ml_len == 0)
#define ml_len(ml)	((ml)->ml_len)

/* ---- ifnet additions -------------------------------------------------- */
#define IFF_DEBUG	0x0004
#define IFF_OACTIVE	0x0400
#define IFT_ETHER	0x06
#define IFT_IEEE80211	0x47
#define AF_UNSPEC	0
#define AF_INET		2
#define AF_LINK		18
#define AF_INET6	24
#define ETHERMTU	1500
#define ETHER_MAX_LEN	1518
#define ETHERTYPE_IP	0x0800
#define ETHERTYPE_ARP	0x0806
#define ETHERTYPE_AARP	0x80f3
#define ETHERTYPE_IPX	0x8137
#define ETHERTYPE_IPV6	0x86dd
#define ETHERTYPE_VLAN	0x8100
#define ETHERTYPE_EAPOL	0x888e
#define ETHERTYPE_PAE	0x888e
#define ETHER_IS_MULTICAST(a)	(*(a) & 0x01)
#define ETHER_IS_BROADCAST(a)	(((a)[0] & (a)[1] & (a)[2] & (a)[3] & (a)[4] & (a)[5]) == 0xff)
#define ETHER_IS_EQ(a, b)	(memcmp((a), (b), ETHER_ADDR_LEN) == 0)
extern const uint8_t etherbroadcastaddr[ETHER_ADDR_LEN];
extern const uint8_t etheranyaddr[ETHER_ADDR_LEN];
struct rtentry;
struct ifaddr { struct sockaddr *ifa_addr; };
int	if_enqueue(struct ifnet *, struct mbuf *);
void	if_start(struct ifnet *);
int	if_setlladdr(struct ifnet *, const uint8_t *);
void	if_deactivate(struct ifnet *);
int	if_addgroup(struct ifnet *, const char *);
int	ether_output(struct ifnet *, struct mbuf *, struct sockaddr *, struct rtentry *);
void	ether_input(struct ifnet *, struct mbuf *);
int	ether_addmulti(struct ifreq *, struct arpcom *);
int	ether_delmulti(struct ifreq *, struct arpcom *);
uint32_t ether_crc32_le_update(uint32_t, const uint8_t *, size_t);
uint32_t ether_crc32_le(const uint8_t *, size_t);
struct proc;
extern struct proc *const curproc;
int	suser(struct proc *);
int	copyin(const void *, void *, size_t);
int	copyout(const void *, void *, size_t);
#define SIOCADDMULTI	0x8010
#define SIOCDELMULTI	0x8011
#define SIOCSIFMTU	0x8012
#define SIOCGIFADDR	0x8013
#define SIOCSIFLLADDR	0x8014
#define SIOCAIFADDR	0x8015
/* BSD ioctl encoding for the SIOC[GS]80211* numbers of ieee80211_ioctl.h */
#define IOCPARM_MASK	0x1fff
#define IOC_VOID	0x20000000UL
#define IOC_OUT		0x40000000UL
#define IOC_IN		0x80000000UL
#define _IOC(inout, group, num, len) \
	((inout) | (((len) & IOCPARM_MASK) << 16) | ((group) << 8) | (num))
#define _IO(g, n)	_IOC(IOC_VOID, (g), (n), 0)
#define _IOR(g, n, t)	_IOC(IOC_OUT, (g), (n), sizeof(t))
#define _IOW(g, n, t)	_IOC(IOC_IN, (g), (n), sizeof(t))
#define _IOWR(g, n, t)	_IOC(IOC_IN | IOC_OUT, (g), (n), sizeof(t))

/* protocol headers the classifier looks at */
struct ip6_hdr {
	uint32_t	ip6_flow;
	uint16_t	ip6_plen;
	uint8_t		ip6_nxt;
	uint8_t		ip6_hlim;
	uint8_t		ip6_src[16];
	uint8_t		ip6_dst[16];
} __packed;
#define IPTOS_DSCP_CS0	0x00
#define IPTOS_DSCP_CS1	0x20
#define IPTOS_DSCP_CS2	0x40
#define IPTOS_DSCP_CS3	0x60
#define IPTOS_DSCP_CS4	0x80
#define IPTOS_DSCP_CS5	0xa0
#define IPTOS_DSCP_CS6	0xc0
#define IPTOS_DSCP_CS7	0xe0
#define IPTOS_DSCP_AF11	0x28
#define IPTOS_DSCP_AF12	0x30
#define IPTOS_DSCP_AF13	0x38
#define IPTOS_DSCP_AF21	0x48
#define IPTOS_DSCP_AF22	0x50
#define IPTOS_DSCP_AF23	0x58
#define IPTOS_DSCP_AF31	0x68
#define IPTOS_DSCP_AF32	0x70
#define IPTOS_DSCP_AF33	0x78
#define IPTOS_DSCP_AF41	0x88
#define IPTOS_DSCP_AF42	0x90
#define IPTOS_DSCP_AF43	0x98
#define IPTOS_DSCP_EF	0xb8
#define IPTOS_DSCP_VA	0xb0
#define IPTOS_DSCP_SHIFT 2
#define IPTOS_DSCP_MASK	0xfc

#endif /* OBSD_WIFI_H */
