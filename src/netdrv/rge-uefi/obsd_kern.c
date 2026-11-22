/*
 * Charon network drivers: implementation of the OpenBSD kernel services an
 * Ethernet driver uses (memory, mbufs, bus_dma, PCI helpers, interface
 * queues, timeouts, ifmedia, printf), on top of the host calls of
 * rge_port.h.  Everything runs single-threaded: the UEFI side serialises
 * all entries at TPL_CALLBACK.  No interrupts: the handler a driver
 * establishes is only recorded and later called from the poll path.
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

#include <sys/queue.h>
#include <net/if_media.h>
#include <dev/pci/pcireg.h>

struct obsd_env *obsd_cur;

/* ---- libkern -------------------------------------------------------- */
void *
memcpy(void *dst, const void *src, size_t n)
{
	volatile unsigned char *d = dst;
	const volatile unsigned char *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

void *
memmove(void *dst, const void *src, size_t n)
{
	volatile unsigned char *d = dst;
	const volatile unsigned char *s = src;

	if (d <= s || d >= s + n) {
		while (n--)
			*d++ = *s++;
	} else {
		while (n--)
			d[n] = s[n];
	}
	return dst;
}

void *
memset(void *dst, int c, size_t n)
{
	volatile unsigned char *d = dst;

	while (n--)
		*d++ = (unsigned char)c;
	return dst;
}

int
memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *x = a, *y = b;

	for (; n; n--, x++, y++)
		if (*x != *y)
			return (*x - *y);
	return 0;
}

size_t
strlen(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return n;
}

size_t
strlcpy(char *dst, const char *src, size_t size)
{
	size_t n = strlen(src), i;

	if (size) {
		for (i = 0; i + 1 < size && src[i]; i++)
			dst[i] = src[i];
		dst[i] = 0;
	}
	return n;
}

int
strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return ((unsigned char)*a - (unsigned char)*b);
}

int
fls(int mask)
{
	int bit;

	if (mask == 0)
		return 0;
	for (bit = 1; mask != 1; bit++)
		mask = (unsigned int)mask >> 1;
	return bit;
}

static void
put(char *buf, size_t size, size_t *at, char c)
{
	if (*at + 1 < size)
		buf[*at] = c;
	(*at)++;
}

int
vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	size_t at = 0;
	char tmp[24];

	for (; *fmt; fmt++) {
		int lng = 0, zero = 0, width = 0, base = 10, neg = 0, n = 0;
		unsigned long long v;
		const char *s;

		if (*fmt != '%') {
			put(buf, size, &at, *fmt);
			continue;
		}
		fmt++;
		if (*fmt == '0')
			zero = 1;
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		while (*fmt == 'l' || *fmt == 'z' || *fmt == 'j' ||
		    *fmt == 'h') {
			if (*fmt != 'h')
				lng++;
			fmt++;
		}
		switch (*fmt) {
		case 's':
			s = va_arg(ap, const char *);
			if (s == NULL)
				s = "(null)";
			while (*s)
				put(buf, size, &at, *s++);
			continue;
		case 'c':
			put(buf, size, &at, (char)va_arg(ap, int));
			continue;
		case '%':
			put(buf, size, &at, '%');
			continue;
		case 'd':
		case 'i':
			if (lng) {
				long long sv = va_arg(ap, long);
				neg = sv < 0;
				v = neg ? -(unsigned long long)sv : (unsigned long long)sv;
			} else {
				int sv = va_arg(ap, int);
				neg = sv < 0;
				v = neg ? -(unsigned long long)sv : (unsigned long long)sv;
			}
			break;
		case 'p':
			lng = 1;
			/* FALLTHROUGH */
		case 'x':
		case 'X':
			base = 16;
			/* FALLTHROUGH */
		case 'u':
			v = lng ? va_arg(ap, unsigned long) :
			    va_arg(ap, unsigned int);
			break;
		case 0:
			fmt--;
			continue;
		default:
			put(buf, size, &at, '?');
			continue;
		}
		do {
			tmp[n++] = "0123456789abcdef"[v % base];
			v /= base;
		} while (v && n < (int)sizeof(tmp));
		if (neg)
			put(buf, size, &at, '-');
		for (width -= n + neg; width > 0; width--)
			put(buf, size, &at, zero ? '0' : ' ');
		while (n)
			put(buf, size, &at, tmp[--n]);
	}
	if (size)
		buf[at < size ? at : size - 1] = 0;
	return (int)at;
}

int
snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}

int
printf(const char *fmt, ...)
{
	char line[200];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	rge_host_log(line);
	return n;
}

/* A kernel would stop here.  A boot manager must keep running: the message
 * is recorded and the caller continues. */
void
panic(const char *fmt, ...)
{
	char line[200];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	rge_host_log("panic: ");
	rge_host_log(line);
	rge_host_log("\n");
}

void
obsd_panic(const char *what)
{
	rge_host_log("assertion failed: ");
	rge_host_log(what);
	rge_host_log("\n");
}

/* ---- malloc --------------------------------------------------------- */
void *
malloc(size_t size, int type, int flags)
{
	struct obsd_env *env = obsd_cur;
	struct obsd_alloc *a;

	(void)type;
	if (env == NULL || size > (64UL << 20))
		return NULL;
	a = rge_host_alloc(sizeof(*a) + size);
	if (a == NULL)
		return NULL;
	a->size = size;
	a->prev = NULL;
	a->next = env->allocs;
	if (a->next)
		a->next->prev = a;
	env->allocs = a;
	if (flags & M_ZERO)
		memset(a + 1, 0, size);
	return a + 1;
}

void *
mallocarray(size_t nmemb, size_t size, int type, int flags)
{
	if (size && nmemb > (64UL << 20) / size)
		return NULL;
	return malloc(nmemb * size, type, flags);
}

void
free(void *p, int type, size_t size)
{
	struct obsd_env *env = obsd_cur;
	struct obsd_alloc *a;

	(void)type;
	(void)size;
	if (p == NULL || env == NULL)
		return;
	a = (struct obsd_alloc *)p - 1;
	if (a->prev)
		a->prev->next = a->next;
	else
		env->allocs = a->next;
	if (a->next)
		a->next->prev = a->prev;
	rge_host_free(a);
}

/* ---- timeouts ------------------------------------------------------- */
void
timeout_set(struct timeout *to, void (*fn)(void *), void *arg)
{
	to->to_func = fn;
	to->to_arg = arg;
	to->to_pending = 0;
	to->to_next = NULL;
	to->to_deadline = 0;
}

int
timeout_add_sec(struct timeout *to, int secs)
{
	struct obsd_env *env = obsd_cur;
	struct timeout *t;
	int was = to->to_pending;

	to->to_deadline = env->ticks + (uint64_t)secs * 100;
	to->to_pending = 1;
	for (t = env->timeouts; t != NULL; t = t->to_next)
		if (t == to)
			return !was;
	to->to_next = env->timeouts;
	env->timeouts = to;
	return !was;
}

int
timeout_del(struct timeout *to)
{
	int was = to->to_pending;

	to->to_pending = 0;
	return was;
}

void
obsd_tick(struct obsd_env *env)
{
	struct timeout *t;
	struct ifnet *ifp = env->ifp;

	env->ticks++;
	for (t = env->timeouts; t != NULL; t = t->to_next) {
		if (t->to_pending && t->to_deadline <= env->ticks) {
			t->to_pending = 0;
			t->to_func(t->to_arg);
		}
	}
	/* the interface watchdog counts seconds */
	if (env->ticks % 100 == 0 && ifp != NULL && ifp->if_timer > 0 &&
	    --ifp->if_timer == 0 && ifp->if_watchdog != NULL)
		ifp->if_watchdog(ifp);
}

/* ---- autoconf ------------------------------------------------------- */
void
config_mountroot(struct device *dev, void (*hook)(struct device *))
{
	/* run by the port right after attach returns */
	obsd_cur->mountroot_hook = hook;
	obsd_cur->mountroot_dev = dev;
}

/* ---- PCI ------------------------------------------------------------ */
int
pci_matchbyid(struct pci_attach_args *pa, const struct pci_matchid *ids,
    int nent)
{
	int i;

	for (i = 0; i < nent; i++)
		if (PCI_VENDOR(pa->pa_id) == ids[i].pm_vid &&
		    PCI_PRODUCT(pa->pa_id) == ids[i].pm_pid)
			return 1;
	return 0;
}

pcireg_t
pci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	(void)tag;
	return rge_host_cfg_read(pc, (uint32_t)reg);
}

void
pci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, pcireg_t val)
{
	(void)tag;
	rge_host_cfg_write(pc, (uint32_t)reg, val);
}

int
pci_get_capability(pci_chipset_tag_t pc, pcitag_t tag, int capid, int *offset,
    pcireg_t *value)
{
	pcireg_t reg;
	unsigned ptr;
	int guard;

	reg = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
	if (reg == 0xffffffff || !(reg & PCI_STATUS_CAPLIST_SUPPORT))
		return 0;
	ptr = pci_conf_read(pc, tag, PCI_CAPLISTPTR_REG) & 0xfc;
	for (guard = 0; ptr >= 0x40 && guard < 48; guard++) {
		reg = pci_conf_read(pc, tag, (int)ptr);
		if (reg == 0xffffffff)
			return 0;
		if ((reg & 0xff) == (pcireg_t)capid) {
			if (offset)
				*offset = (int)ptr;
			if (value)
				*value = reg;
			return 1;
		}
		ptr = (reg >> 8) & 0xfc;
	}
	return 0;
}

int
pci_set_powerstate(pci_chipset_tag_t pc, pcitag_t tag, int state)
{
	pcireg_t reg;
	int offset;

	if (!pci_get_capability(pc, tag, PCI_CAP_PWRMGMT, &offset, NULL))
		return 0;
	reg = pci_conf_read(pc, tag, offset + PCI_PMCSR);
	if (reg == 0xffffffff || (reg & PCI_PMCSR_STATE_MASK) == (pcireg_t)state)
		return 0;
	pci_conf_write(pc, tag, offset + PCI_PMCSR,
	    (reg & ~PCI_PMCSR_STATE_MASK) | (pcireg_t)state);
	DELAY(10000);		/* D3hot -> D0 needs 10 ms */
	return 0;
}

int
pci_mapreg_map(struct pci_attach_args *pa, int reg, pcireg_t type, int flags,
    bus_space_tag_t *tagp, bus_space_handle_t *handlep, bus_addr_t *basep,
    bus_size_t *sizep, bus_size_t maxsize)
{
	struct obsd_env *env = obsd_cur;
	struct obsd_busmap *map;
	pcireg_t val, high = 0;
	int io;

	(void)flags;
	(void)maxsize;
	if (reg < PCI_MAPREG_START || reg >= PCI_MAPREG_END || (reg & 3) ||
	    env->nbusmap >= (int)nitems(env->busmap))
		return 1;
	val = rge_host_cfg_read(pa->pa_pc, (uint32_t)reg);
	if (val == 0xffffffff)
		return 1;
	io = (val & PCI_MAPREG_TYPE_MASK) == PCI_MAPREG_TYPE_IO;
	if (PCI_MAPREG_TYPE(type) == PCI_MAPREG_TYPE_IO) {
		if (!io || (val & ~0x3U) == 0)
			return 1;
	} else {
		if (io)
			return 1;
		if (PCI_MAPREG_MEM_TYPE(val) != PCI_MAPREG_MEM_TYPE(type))
			return 1;
		if (PCI_MAPREG_MEM_TYPE(val) == PCI_MAPREG_MEM_TYPE_64BIT) {
			high = rge_host_cfg_read(pa->pa_pc, (uint32_t)reg + 4);
			if (high == 0xffffffff)
				return 1;
		}
		if ((val & ~0xfU) == 0 && high == 0)
			return 1;	/* the firmware assigned no address */
	}
	map = &env->busmap[env->nbusmap++];
	map->host = pa->pa_pc;
	map->bar = (unsigned)(reg - PCI_MAPREG_START) / 4;
	map->io = io;
	if (tagp)
		*tagp = io;
	if (handlep)
		*handlep = map;
	if (basep)
		*basep = 0;
	if (sizep)
		*sizep = 0;
	return 0;
}

/* No MSI-X, no MSI: the driver takes its single-vector INTx path, and the
 * handler it establishes is called by the poll path. */
int
pci_intr_msix_count(struct pci_attach_args *pa)
{
	(void)pa;
	return 0;
}

int
pci_intr_map_msix(struct pci_attach_args *pa, int vec, pci_intr_handle_t *ihp)
{
	(void)pa; (void)vec; (void)ihp;
	return 1;
}

int
pci_intr_map_msi(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	(void)pa; (void)ihp;
	return 1;
}

int
pci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	(void)pa;
	*ihp = &obsd_cur->intr;
	return 0;
}

const char *
pci_intr_string(pci_chipset_tag_t pc, pci_intr_handle_t ih)
{
	(void)pc; (void)ih;
	return "polled";
}

void *
pci_intr_establish(pci_chipset_tag_t pc, pci_intr_handle_t ih, int level,
    int (*func)(void *), void *arg, const char *name)
{
	struct obsd_env *env = obsd_cur;

	(void)pc; (void)ih; (void)level; (void)name;
	env->intr.ih_func = func;
	env->intr.ih_arg = arg;
	env->intr_set = 1;
	return &env->intr;
}

void *
pci_intr_establish_cpu(pci_chipset_tag_t pc, pci_intr_handle_t ih, int level,
    void *ci, int (*func)(void *), void *arg, const char *name)
{
	(void)pc; (void)ih; (void)level; (void)ci; (void)func; (void)arg;
	(void)name;
	return NULL;		/* multi-vector operation is not offered */
}

void
pci_intr_disestablish(pci_chipset_tag_t pc, void *cookie)
{
	(void)pc; (void)cookie;
	if (obsd_cur)
		obsd_cur->intr_set = 0;
}

struct intrmap *
intrmap_create(const struct device *dv, unsigned int nintr,
    unsigned int maxintr, unsigned int flags)
{
	(void)dv; (void)nintr; (void)maxintr; (void)flags;
	return NULL;
}

void
intrmap_destroy(struct intrmap *im)
{
	(void)im;
}

unsigned int
intrmap_count(const struct intrmap *im)
{
	(void)im;
	return 1;
}

void *
intrmap_cpu(const struct intrmap *im, unsigned int i)
{
	(void)im; (void)i;
	return NULL;
}

void
stoeplitz_to_key(void *key, size_t len)
{
	memset(key, 0x6d, len);	/* only used with several queues: never */
}

/* ---- bus_dma -------------------------------------------------------- */
int
obsd_dma_translate(struct obsd_env *env, const void *va, size_t len,
    uint64_t *dev)
{
	const char *p = va;
	struct obsd_region *r;

	for (r = env->regions; r != NULL; r = r->next) {
		if (p >= r->va && len <= r->len &&
		    (size_t)(p - r->va) <= r->len - len) {
			*dev = r->dev + (uint64_t)(p - r->va);
			return 0;
		}
	}
	return EINVAL;
}

static void *
dma_region_alloc(struct obsd_env *env, size_t len, uint64_t *dev,
    void **cookiep)
{
	struct obsd_region *r;
	void *va, *cookie = NULL;

	r = rge_host_alloc(sizeof(*r));
	if (r == NULL)
		return NULL;
	va = rge_host_dma_alloc(env->host, len, dev, &cookie);
	if (va == NULL) {
		rge_host_free(r);
		return NULL;
	}
	r->va = va;
	r->dev = *dev;
	r->len = len;
	r->cookie = cookie;
	r->next = env->regions;
	env->regions = r;
	if (cookiep)
		*cookiep = cookie;
	return va;
}

static void
dma_region_free(struct obsd_env *env, void *va)
{
	struct obsd_region **rp, *r;

	for (rp = &env->regions; (r = *rp) != NULL; rp = &r->next) {
		if (r->va == (char *)va) {
			*rp = r->next;
			rge_host_dma_free(env->host, r->va, r->len, r->cookie);
			rge_host_free(r);
			return;
		}
	}
}

int
bus_dmamap_create(bus_dma_tag_t tag, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags, bus_dmamap_t *mapp)
{
	bus_dmamap_t map;

	(void)tag; (void)size; (void)maxsegsz; (void)boundary; (void)flags;
	if (nsegments < 1)
		return EINVAL;
	map = malloc(sizeof(*map) +
	    (size_t)(nsegments - 1) * sizeof(bus_dma_segment_t), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (map == NULL)
		return ENOMEM;
	map->_dm_maxsegs = nsegments;
	*mapp = map;
	return 0;
}

void
bus_dmamap_destroy(bus_dma_tag_t tag, bus_dmamap_t map)
{
	(void)tag;
	free(map, M_DEVBUF, 0);
}

int
bus_dmamap_load(bus_dma_tag_t tag, bus_dmamap_t map, void *buf,
    bus_size_t buflen, void *p, int flags)
{
	uint64_t dev;

	(void)tag; (void)p; (void)flags;
	if (obsd_dma_translate(obsd_cur, buf, buflen, &dev))
		return EINVAL;
	map->dm_segs[0].ds_addr = dev;
	map->dm_segs[0].ds_len = buflen;
	map->dm_nsegs = 1;
	map->dm_mapsize = buflen;
	return 0;
}

int
bus_dmamap_load_mbuf(bus_dma_tag_t tag, bus_dmamap_t map, struct mbuf *m0,
    int flags)
{
	struct mbuf *m;
	uint64_t dev;
	int n = 0;
	bus_size_t total = 0;

	(void)tag; (void)flags;
	for (m = m0; m != NULL; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		if (n >= map->_dm_maxsegs)
			return EFBIG;
		if (obsd_dma_translate(obsd_cur, m->m_data, m->m_len, &dev))
			return EINVAL;
		map->dm_segs[n].ds_addr = dev;
		map->dm_segs[n].ds_len = m->m_len;
		total += m->m_len;
		n++;
	}
	if (n == 0)
		return EINVAL;
	map->dm_nsegs = n;
	map->dm_mapsize = total;
	return 0;
}

void
bus_dmamap_unload(bus_dma_tag_t tag, bus_dmamap_t map)
{
	(void)tag;
	map->dm_nsegs = 0;
	map->dm_mapsize = 0;
}

int
bus_dmamem_alloc(bus_dma_tag_t tag, bus_size_t size, bus_size_t alignment,
    bus_size_t boundary, bus_dma_segment_t *segs, int nsegs, int *rsegs,
    int flags)
{
	uint64_t dev = 0;
	void *va, *cookie = NULL;

	(void)tag; (void)boundary; (void)flags;
	if (nsegs < 1 || alignment > 4096)
		return EINVAL;
	va = dma_region_alloc(obsd_cur, size, &dev, &cookie);	/* zeroed */
	if (va == NULL)
		return ENOMEM;
	segs[0].ds_addr = dev;
	segs[0].ds_len = size;
	segs[0]._ds_va = va;
	segs[0]._ds_cookie = cookie;
	*rsegs = 1;
	return 0;
}

void
bus_dmamem_free(bus_dma_tag_t tag, bus_dma_segment_t *segs, int nsegs)
{
	(void)tag;
	if (nsegs >= 1 && segs[0]._ds_va != NULL) {
		dma_region_free(obsd_cur, segs[0]._ds_va);
		segs[0]._ds_va = NULL;
	}
}

int
bus_dmamem_map(bus_dma_tag_t tag, bus_dma_segment_t *segs, int nsegs,
    size_t size, caddr_t *kvap, int flags)
{
	(void)tag; (void)size; (void)flags;
	if (nsegs < 1 || segs[0]._ds_va == NULL)
		return EINVAL;
	*kvap = segs[0]._ds_va;
	return 0;
}

void
bus_dmamem_unmap(bus_dma_tag_t tag, caddr_t kva, size_t size)
{
	(void)tag; (void)kva; (void)size;
}

/* ---- mbufs ---------------------------------------------------------- */
static char *
cluster_get(struct obsd_env *env)
{
	char *cl = env->clfree;

	if (cl != NULL) {
		env->clfree = *(char **)cl;
		env->nclfree--;
	}
	return cl;
}

static void
cluster_put(struct obsd_env *env, char *cl)
{
	*(char **)cl = env->clfree;
	env->clfree = cl;
	env->nclfree++;
}

static struct mbuf *
mbuf_get(int pkthdr)
{
	struct obsd_env *env = obsd_cur;
	struct mbuf *m = env->mfree;

	if (m == NULL)
		return NULL;
	env->mfree = m->m_next;
	env->nmfree--;
	m->m_next = NULL;
	m->m_nextpkt = NULL;
	m->m_data = m->m_dat;
	m->m_len = 0;
	m->m_flags = pkthdr ? M_PKTHDR : 0;
	memset(&m->m_pkthdr, 0, sizeof(m->m_pkthdr));
	m->m_ext_buf = NULL;
	m->m_ext_size = 0;
	return m;
}

struct mbuf *
m_gethdr(int how, int type)
{
	(void)how; (void)type;
	return mbuf_get(1);
}

struct mbuf *
m_get(int how, int type)
{
	(void)how; (void)type;
	return mbuf_get(0);
}

struct mbuf *
MCLGETL(struct mbuf *m, int how, u_int len)
{
	struct obsd_env *env = obsd_cur;
	struct mbuf *m0 = m;
	char *cl;

	(void)how;
	if (len > MCLBYTES)
		return NULL;
	if (m == NULL && (m = mbuf_get(1)) == NULL)
		return NULL;
	cl = cluster_get(env);
	if (cl == NULL) {
		if (m0 == NULL)
			m_free(m);
		return NULL;
	}
	m->m_ext_buf = cl;
	m->m_ext_size = MCLBYTES;
	m->m_data = cl;
	m->m_flags |= M_EXT;
	return m;
}

struct mbuf *
m_free(struct mbuf *m)
{
	struct obsd_env *env = obsd_cur;
	struct mbuf *n;

	if (m == NULL)
		return NULL;
	n = m->m_next;
	if ((m->m_flags & M_EXT) && m->m_ext_buf != NULL)
		cluster_put(env, m->m_ext_buf);
	if ((m->m_flags & M_OBSD_TX) && env->txinflight > 0)
		env->txinflight--;
	m->m_flags = 0;
	m->m_ext_buf = NULL;
	m->m_next = env->mfree;
	env->mfree = m;
	env->nmfree++;
	return n;
}

void
m_freem(struct mbuf *m)
{
	while (m != NULL)
		m = m_free(m);
}

void
m_adj(struct mbuf *mp, int req)
{
	struct mbuf *m = mp;
	int len = req, count;

	if (mp == NULL)
		return;
	if (len >= 0) {
		while (m != NULL && len > 0) {
			if ((int)m->m_len <= len) {
				len -= m->m_len;
				m->m_len = 0;
				m = m->m_next;
			} else {
				m->m_len -= len;
				m->m_data += len;
				len = 0;
			}
		}
		if (mp->m_flags & M_PKTHDR)
			mp->m_pkthdr.len -= (req - len);
		return;
	}
	len = -len;
	count = 0;
	for (m = mp; m != NULL; m = m->m_next)
		count += m->m_len;
	if (len > count)
		len = count;
	count -= len;
	if (mp->m_flags & M_PKTHDR)
		mp->m_pkthdr.len = count;
	for (m = mp; m != NULL; m = m->m_next) {
		if ((int)m->m_len >= count) {
			m->m_len = count;
			count = 0;
		} else
			count -= m->m_len;
	}
}

void
m_cat(struct mbuf *m, struct mbuf *n)
{
	while (m->m_next != NULL)
		m = m->m_next;
	m->m_next = n;
}

void
m_copydata(struct mbuf *m, int off, int len, void *vp)
{
	char *cp = vp;
	int n;

	while (m != NULL && off >= (int)m->m_len) {
		off -= m->m_len;
		m = m->m_next;
	}
	while (m != NULL && len > 0) {
		n = MIN((int)m->m_len - off, len);
		memcpy(cp, m->m_data + off, (size_t)n);
		cp += n;
		len -= n;
		off = 0;
		m = m->m_next;
	}
}

struct mbuf *
m_getptr(struct mbuf *m, int loc, int *off)
{
	while (m != NULL) {
		if ((int)m->m_len > loc) {
			*off = loc;
			return m;
		}
		loc -= m->m_len;
		if (m->m_next == NULL) {
			if (loc == 0) {
				*off = m->m_len;
				return m;
			}
			return NULL;
		}
		m = m->m_next;
	}
	return NULL;
}

/* Collapse a chain into one cluster (frames of up to 2048 bytes). */
int
m_defrag(struct mbuf *m, int how)
{
	struct obsd_env *env = obsd_cur;
	char *cl;
	int total = 0;
	struct mbuf *n;

	(void)how;
	if (m->m_next == NULL)
		return 0;
	for (n = m; n != NULL; n = n->m_next)
		total += n->m_len;
	if (total > MCLBYTES || (cl = cluster_get(env)) == NULL)
		return ENOBUFS;
	m_copydata(m, 0, total, cl);
	m_freem(m->m_next);
	m->m_next = NULL;
	if ((m->m_flags & M_EXT) && m->m_ext_buf != NULL)
		cluster_put(env, m->m_ext_buf);
	m->m_ext_buf = cl;
	m->m_ext_size = MCLBYTES;
	m->m_flags |= M_EXT;
	m->m_data = cl;
	m->m_len = (u_int)total;
	return 0;
}

void
ml_enqueue(struct mbuf_list *ml, struct mbuf *m)
{
	m->m_nextpkt = NULL;
	if (ml->ml_tail == NULL)
		ml->ml_head = ml->ml_tail = m;
	else {
		ml->ml_tail->m_nextpkt = m;
		ml->ml_tail = m;
	}
	ml->ml_len++;
}

struct mbuf *
ml_dequeue(struct mbuf_list *ml)
{
	struct mbuf *m = ml->ml_head;

	if (m != NULL) {
		ml->ml_head = m->m_nextpkt;
		if (ml->ml_head == NULL)
			ml->ml_tail = NULL;
		m->m_nextpkt = NULL;
		ml->ml_len--;
	}
	return m;
}

/* ---- interface ------------------------------------------------------ */
void
if_attach(struct ifnet *ifp)
{
	ifp->if_snd.ifq_if = ifp;
	ifp->_if_ifq0 = &ifp->if_snd;
	ifp->if_ifqs = &ifp->_if_ifq0;
	ifp->_if_iq0.ifiq_if = ifp;
	ifp->_if_iq0p = &ifp->_if_iq0;
	ifp->if_iqs = &ifp->_if_iq0p;
	ifp->if_link_state = LINK_STATE_DOWN;
	obsd_cur->ifp = ifp;
}

void
ether_ifattach(struct ifnet *ifp)
{
#ifdef OBSD_COMPAT_MII
	/* struct arpcom begins with its ifnet */
	memcpy(ifp->_if_sadl0.sdl_data, ((struct arpcom *)ifp)->ac_enaddr,
	    ETHER_ADDR_LEN);
	ifp->if_sadl = &ifp->_if_sadl0;
#endif
	ifp->_if_attached = 1;
}

void
if_attach_queues(struct ifnet *ifp, unsigned int n)
{
	KASSERT(n == 1);
	(void)ifp;
}

void
if_attach_iqueues(struct ifnet *ifp, unsigned int n)
{
	KASSERT(n == 1);
	(void)ifp;
}

void
if_link_state_change(struct ifnet *ifp)
{
	(void)ifp;		/* the port reads if_link_state */
}

int
ether_ioctl(struct ifnet *ifp, struct arpcom *ac, u_long cmd, caddr_t data)
{
	(void)ifp; (void)ac; (void)cmd; (void)data;
	return ENOTSUP;		/* there is no ioctl path in this port */
}

uint32_t
ether_crc32_be(const uint8_t *buf, size_t len)
{
	uint32_t crc = 0xffffffff, carry;
	size_t i;
	int bit;
	uint8_t data;

	for (i = 0; i < len; i++) {
		for (data = buf[i], bit = 0; bit < 8; bit++, data >>= 1) {
			carry = ((crc & 0x80000000U) ? 1 : 0) ^ (data & 0x01);
			crc <<= 1;
			if (carry)
				crc = (crc ^ 0x04c11db6U) | carry;
		}
	}
	return crc;
}

char *
ether_sprintf(u_char *ap)
{
	static char buf[18];

	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
	    ap[0], ap[1], ap[2], ap[3], ap[4], ap[5]);
	return buf;
}

void
ifq_init_maxlen(struct ifqueue *ifq, unsigned int maxlen)
{
	ifq->ifq_maxlen = MIN(maxlen, OBSD_IFQ_MAX);
}

int
ifq_enqueue(struct ifqueue *ifq, struct mbuf *m)
{
	if (ifq->ifq_ml.ml_len >= ifq->ifq_maxlen)
		return ENOBUFS;
	ml_enqueue(&ifq->ifq_ml, m);
	return 0;
}

struct mbuf *
ifq_dequeue(struct ifqueue *ifq)
{
	return ml_dequeue(&ifq->ifq_ml);
}

void
ifq_purge(struct ifqueue *ifq)
{
	struct mbuf *m;

	while ((m = ml_dequeue(&ifq->ifq_ml)) != NULL)
		m_freem(m);
}

void
ifq_restart(struct ifqueue *ifq)
{
	ifq_clr_oactive(ifq);
	if (ifq->ifq_if != NULL && ifq->ifq_if->if_qstart != NULL)
		ifq->ifq_if->if_qstart(ifq);
}

void
ifq_serialize(struct ifqueue *ifq, struct task *t)
{
	(void)ifq;
	t->t_func(t->t_arg);
}

/* Received frames: kept for SNP Receive(), the oldest dropped never, the
 * newest dropped when the consumer does not keep up. */
int
ifiq_input(struct ifiqueue *ifiq, struct mbuf_list *ml)
{
	struct obsd_env *env = obsd_cur;
	struct mbuf *m;

	(void)ifiq;
	while ((m = ml_dequeue(ml)) != NULL) {
		if (env->rxq.ml_len >= OBSD_RXQ_MAX) {
			env->rxdropped++;
			m_freem(m);
		} else
			ml_enqueue(&env->rxq, m);
	}
	return 0;
}

void
if_rxr_init(struct if_rxring *rxr, u_int lwm, u_int hwm)
{
	rxr->rxr_alive = 0;
	rxr->rxr_lwm = lwm;
	rxr->rxr_hwm = MIN(hwm, OBSD_RXRING_MAX);
}

u_int
if_rxr_get(struct if_rxring *rxr, u_int max)
{
	u_int diff;

	if (rxr->rxr_alive >= rxr->rxr_hwm)
		return 0;
	diff = MIN(rxr->rxr_hwm - rxr->rxr_alive, max);
	rxr->rxr_alive += diff;
	return diff;
}

int
if_rxr_info_ioctl(struct if_rxrinfo *ifri, u_int n, struct if_rxring_info *e)
{
	(void)ifri; (void)n; (void)e;
	return ENOTSUP;
}

/* Only the Ethernet and IPv4 headers are located: that is all the driver
 * reads when no checksum or segmentation offload is requested, and this
 * port never requests one. */
void
ether_extract_headers(struct mbuf *m, struct ether_extracted *ext)
{
	struct ether_header *eh;
	struct ip *ip;
	u_int hlen;

	memset(ext, 0, sizeof(*ext));
	if (m->m_len < sizeof(*eh))
		return;
	eh = mtod(m, struct ether_header *);
	ext->eh = eh;
	if (eh->ether_type != htons(0x0800) ||
	    m->m_len < sizeof(*eh) + sizeof(*ip))
		return;
	ip = (struct ip *)(eh + 1);
	hlen = (u_int)(ip->ip_vhl & 0x0f) << 2;
	if ((ip->ip_vhl >> 4) != 4 || hlen < sizeof(*ip) ||
	    m->m_len < sizeof(*eh) + hlen)
		return;
	ext->ip4 = ip;
	ext->iphlen = hlen;
	ext->iplen = ntohs(ip->ip_len);
	if (ip->ip_p == IPPROTO_UDP &&
	    m->m_len >= sizeof(*eh) + hlen + sizeof(struct udphdr))
		ext->udp = (struct udphdr *)((char *)ip + hlen);
}

/* Software checksums are only asked for when the stack requested checksum
 * offload on a padded frame; this port never sets those flags. */
uint16_t
in_cksum(struct mbuf *m, int len)
{
	(void)m; (void)len;
	return 0;
}

void
in_delayed_cksum(struct mbuf *m)
{
	(void)m;
}

/* ---- ifmedia -------------------------------------------------------- */
void
ifmedia_init(struct ifmedia *ifm, uint64_t dontcare_mask,
    ifm_change_cb_t change_callback, ifm_stat_cb_t status_callback)
{
	TAILQ_INIT(&ifm->ifm_list);
	ifm->ifm_nwords = 0;
	ifm->ifm_cur = NULL;
	ifm->ifm_media = 0;
	ifm->ifm_mask = dontcare_mask;
	ifm->ifm_change_cb = change_callback;
	ifm->ifm_status_cb = status_callback;
}

void
ifmedia_add(struct ifmedia *ifm, uint64_t mword, int data, void *aux)
{
	struct ifmedia_entry *entry;

	entry = malloc(sizeof(*entry), M_DEVBUF, M_NOWAIT);
	if (entry == NULL)
		return;
	entry->ifm_media = mword;
	entry->ifm_data = (u_int)data;
	entry->ifm_aux = aux;
	TAILQ_INSERT_TAIL(&ifm->ifm_list, entry, ifm_list);
	ifm->ifm_nwords++;
}

void
ifmedia_set(struct ifmedia *ifm, uint64_t target)
{
	struct ifmedia_entry *entry;

	TAILQ_FOREACH(entry, &ifm->ifm_list, ifm_list) {
		if (entry->ifm_media == target) {
			ifm->ifm_cur = entry;
			ifm->ifm_media = target;
			return;
		}
	}
	/* as the kernel does: fall back to the first entry */
	entry = TAILQ_FIRST(&ifm->ifm_list);
	if (entry != NULL) {
		ifm->ifm_cur = entry;
		ifm->ifm_media = entry->ifm_media;
	}
}

int
ifmedia_ioctl(struct ifnet *ifp, struct ifreq *ifr, struct ifmedia *ifm,
    u_long cmd)
{
	(void)ifp; (void)ifr; (void)ifm; (void)cmd;
	return ENOTSUP;
}

#ifdef OBSD_COMPAT_MII
/* ---- additions for drivers with the old start routine and an MII bus -- */
void
if_input(struct ifnet *ifp, struct mbuf_list *ml)
{
	(void)ifp;
	ifiq_input(NULL, ml);
}

void
if_detach(struct ifnet *ifp)
{
	(void)ifp;
}

void
ether_ifdetach(struct ifnet *ifp)
{
	ifp->_if_attached = 0;
}

pcireg_t
pci_mapreg_type(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	pcireg_t val = pci_conf_read(pc, tag, reg);

	if ((val & PCI_MAPREG_TYPE_MASK) == PCI_MAPREG_TYPE_IO)
		return PCI_MAPREG_TYPE_IO;
	return PCI_MAPREG_TYPE_MEM | (val & PCI_MAPREG_MEM_TYPE_MASK);
}

int
timeout_add_msec(struct timeout *to, int msecs)
{
	struct obsd_env *env = obsd_cur;
	struct timeout *t;
	int was = to->to_pending;

	to->to_deadline = env->ticks + (uint64_t)(msecs + 9) / 10;
	to->to_pending = 1;
	for (t = env->timeouts; t != NULL; t = t->to_next)
		if (t == to)
			return !was;
	to->to_next = env->timeouts;
	env->timeouts = to;
	return !was;
}

/* Nothing else runs meanwhile: sleeping is waiting. */
int
tsleep_nsec(const void *ident, int priority, const char *wmesg, uint64_t nsecs)
{
	(void)ident; (void)priority; (void)wmesg;
	if (nsecs > 1000000000ULL)
		nsecs = 1000000000ULL;		/* never more than a second */
	DELAY((unsigned)(nsecs / 1000));
	return EWOULDBLOCK;
}

uint64_t
ifmedia_baudrate(uint64_t mword)
{
	(void)mword;
	return 0;			/* only reported, never used */
}

void
ifmedia_delete_instance(struct ifmedia *ifm, uint64_t inst)
{
	(void)ifm; (void)inst;		/* entries go with the adapter */
}

struct device *
config_found_sm(struct device *parent, void *aux, cfprint_t print,
    cfmatch_t submatch)
{
	struct cfdata *cf, *best = NULL;
	struct device *dev;
	int pri, bestpri = 0;

	for (cf = obsd_children; cf->cf_attach != NULL; cf++) {
		pri = (submatch != NULL) ? submatch(parent, cf, aux) :
		    cf->cf_attach->ca_match(parent, cf, aux);
		if (pri > bestpri) {
			bestpri = pri;
			best = cf;
		}
	}
	if (best == NULL || best->cf_attach->ca_devsize < sizeof(*dev))
		return NULL;
	dev = malloc(best->cf_attach->ca_devsize, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (dev == NULL)
		return NULL;
	snprintf(dev->dv_xname, sizeof(dev->dv_xname), "%s0",
	    best->cf_driver->cd_name);
	dev->dv_parent = parent;
	dev->dv_flags = DVF_ACTIVE;
	printf("%s at %s", dev->dv_xname, parent ? parent->dv_xname : "?");
	if (print != NULL)
		print(aux, NULL);
	best->cf_attach->ca_attach(parent, dev, aux);
	return dev;
}

int
config_detach(struct device *dev, int flags)
{
	(void)dev; (void)flags;		/* memory goes with the adapter */
	return 0;
}

int
config_activate_children(struct device *dev, int act)
{
	(void)dev; (void)act;
	return 0;
}
#endif /* OBSD_COMPAT_MII */

/* ---- per-adapter environment ---------------------------------------- */
struct obsd_env *
obsd_env_create(struct rge_host *host)
{
	struct obsd_env *env;
	struct mbuf *m;
	char *pool, *cl;
	uint64_t dev;
	size_t mbytes, total;
	unsigned i;

	env = rge_host_alloc(sizeof(*env));
	if (env == NULL)
		return NULL;
	memset(env, 0, sizeof(*env));
	env->host = host;

	mbytes = (sizeof(struct mbuf) * OBSD_NMBUF + 4095) & ~(size_t)4095;
	total = mbytes + (size_t)OBSD_NCLUSTER * MCLBYTES;
	pool = dma_region_alloc(env, total, &dev, NULL);
	if (pool == NULL) {
		rge_host_free(env);
		return NULL;
	}
	m = (struct mbuf *)pool;
	for (i = 0; i < OBSD_NMBUF; i++, m++) {
		m->m_next = env->mfree;
		env->mfree = m;
	}
	env->nmfree = OBSD_NMBUF;
	cl = pool + mbytes;
	for (i = 0; i < OBSD_NCLUSTER; i++, cl += MCLBYTES)
		cluster_put(env, cl);
	return env;
}

void
obsd_env_destroy(struct obsd_env *env)
{
	struct obsd_alloc *a;
	struct obsd_region *r;

	while ((a = env->allocs) != NULL) {
		env->allocs = a->next;
		rge_host_free(a);
	}
	while ((r = env->regions) != NULL) {
		env->regions = r->next;
		rge_host_dma_free(env->host, r->va, r->len, r->cookie);
		rge_host_free(r);
	}
	if (obsd_cur == env)
		obsd_cur = NULL;
	rge_host_free(env);
}
