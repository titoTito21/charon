/*
 * Charon network drivers: the kernel services OpenBSD's net80211 and mwx(4)
 * need beyond what obsd_kern.c (copied from src/netdrv/rge-uefi) has: sleeping
 * as polling, tasks, more mbuf operations, queues, time, random numbers.
 *
 * EXPERIMENTAL: this code has NEVER RUN on the hardware (no emulator has a
 * MediaTek MT7921/MT7922); it is compiled and linked only.
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
#include "wifi_port.h"

int ticks;
const uint8_t etherbroadcastaddr[ETHER_ADDR_LEN] =
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
const uint8_t etheranyaddr[ETHER_ADDR_LEN] = { 0, 0, 0, 0, 0, 0 };
/* the miniature autoconf of obsd_kern.c: this driver has no children */
struct cfdata obsd_children[] = { { NULL, NULL, { 0, 0 } } };
struct proc *const curproc = NULL;

/* ---- libkern -------------------------------------------------------- */
int
strncmp(const char *a, const char *b, size_t n)
{
	for (; n > 0; a++, b++, n--) {
		if (*a != *b)
			return (unsigned char)*a - (unsigned char)*b;
		if (*a == '\0')
			break;
	}
	return 0;
}

size_t
strlcat(char *dst, const char *src, size_t size)
{
	size_t used = strlen(dst);

	if (used >= size)
		return size + strlen(src);
	return used + strlcpy(dst + used, src, size - used);
}

int
timingsafe_bcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *p = a, *q = b;
	unsigned char diff = 0;

	while (n-- > 0)
		diff |= *p++ ^ *q++;
	return diff != 0;
}

void
explicit_bzero(void *p, size_t n)
{
	volatile unsigned char *v = p;

	while (n-- > 0)
		*v++ = 0;
}

int
ffs(int mask)
{
	return mask == 0 ? 0 : __builtin_ctz((unsigned)mask) + 1;
}

uint32_t
arc4random(void)
{
	uint32_t v;

	wifi_host_random(&v, sizeof(v));
	return v;
}

void
arc4random_buf(void *buf, size_t n)
{
	wifi_host_random(buf, n);
}

uint32_t
arc4random_uniform(uint32_t bound)
{
	return bound < 2 ? 0 : arc4random() % bound;
}

void
log(int level, const char *fmt, ...)
{
	char text[240];
	va_list ap;

	(void)level;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);
	rge_host_log(text);
}

uint32_t
ether_crc32_le_update(uint32_t crc, const uint8_t *buf, size_t len)
{
	size_t i;
	int bit;

	for (i = 0; i < len; i++) {
		crc ^= buf[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
	}
	return crc;
}

/* ---- time ----------------------------------------------------------- */
void
getmicrouptime(struct timeval *tv)
{
	uint64_t t = obsd_cur != NULL ? obsd_cur->ticks : 0;	/* 10 ms */

	tv->tv_sec = (time_t)(t / 100);
	tv->tv_usec = (suseconds_t)(t % 100) * 10000;
}

void
getmicrotime(struct timeval *tv)
{
	getmicrouptime(tv);		/* no wall clock is needed */
}

time_t
getuptime(void)
{
	return obsd_cur != NULL ? (time_t)(obsd_cur->ticks / 100) : 0;
}

uint64_t
getnsecuptime(void)
{
	return obsd_cur != NULL ? obsd_cur->ticks * 10000000ULL : 0;
}

int
ratecheck(struct timeval *last, const struct timeval *min)
{
	struct timeval now, delta;

	getmicrouptime(&now);
	timersub(&now, last, &delta);
	if (!timerisset(last) || timercmp(&delta, min, >=)) {
		*last = now;
		return 1;
	}
	return 0;
}

int
ppsratecheck(struct timeval *last, int *count, int maxpps)
{
	struct timeval now;

	getmicrouptime(&now);
	if (now.tv_sec != last->tv_sec) {
		*last = now;
		*count = 0;
	}
	return maxpps < 0 || (*count)++ < maxpps;
}

int
timeout_add(struct timeout *to, int nticks)
{
	return timeout_add_msec(to, nticks * 10);
}

int
timeout_add_usec(struct timeout *to, int usecs)
{
	return timeout_add_msec(to, (usecs + 999) / 1000);
}

/* ---- sleeping is polling --------------------------------------------- */
/* A thread that sleeps in the kernel waits for the interrupt handler to
 * wake it.  Here nothing else runs, so the sleeper polls the handler the
 * driver established until wakeup(ident) is called or the time is over.
 * Every wait is bounded (5 s at most, also for INFSLP). */
#define SLEEP_NEST	8
static const void *sleep_ident[SLEEP_NEST];
static int sleep_woken[SLEEP_NEST];
static int sleep_depth;

int
tsleep_nsec(const void *ident, int priority, const char *wmesg, uint64_t nsecs)
{
	struct obsd_env *env = obsd_cur;
	uint64_t waited = 0;
	int slot, woken;

	(void)priority; (void)wmesg;
	if (nsecs > 5000000000ULL)
		nsecs = 5000000000ULL;
	if (env == NULL || sleep_depth >= SLEEP_NEST) {
		DELAY(1000);
		return EWOULDBLOCK;
	}
	slot = sleep_depth++;
	sleep_ident[slot] = ident;
	sleep_woken[slot] = 0;
	while (!sleep_woken[slot] && waited < nsecs) {
		if (env->intr_set)
			env->intr.ih_func(env->intr.ih_arg);
		if (sleep_woken[slot])
			break;
		DELAY(500);
		waited += 500000;
		if (waited % 10000000 == 0) {	/* 10 ms: timeouts still run */
			obsd_tick(env);
			ticks = (int)env->ticks;
		}
	}
	woken = sleep_woken[slot];
	sleep_depth--;
	return woken ? 0 : EWOULDBLOCK;
}

int
tsleep(const void *ident, int priority, const char *wmesg, int timo)
{
	return tsleep_nsec(ident, priority, wmesg,
	    timo <= 0 ? INFSLP : (uint64_t)timo * 10000000ULL);
}

void
wakeup(const void *ident)
{
	int i;

	for (i = 0; i < sleep_depth; i++)
		if (sleep_ident[i] == ident)
			sleep_woken[i] = 1;
}

/* ---- tasks ------------------------------------------------------------ */
/* One queue; tasks run from the 10 ms poll (wifi_run_tasks), one after the
 * other, never from inside another task or a sleep. */
static struct taskq the_taskq;
struct taskq *const systq = &the_taskq, *const systqmp = &the_taskq;
static struct task *task_head;
static int tasks_running;

int
task_add(struct taskq *tq, struct task *t)
{
	struct task **pp;

	(void)tq;
	if (t->t_pending)
		return 0;
	t->t_pending = 1;
	t->t_next = NULL;
	for (pp = &task_head; *pp != NULL; pp = &(*pp)->t_next)
		;
	*pp = t;
	return 1;
}

int
task_del(struct taskq *tq, struct task *t)
{
	struct task **pp;

	(void)tq;
	if (!t->t_pending)
		return 0;
	for (pp = &task_head; *pp != NULL; pp = &(*pp)->t_next) {
		if (*pp == t) {
			*pp = t->t_next;
			break;
		}
	}
	t->t_pending = 0;
	t->t_next = NULL;
	return 1;
}

struct taskq *
taskq_create(const char *name, unsigned int nthreads, int ipl,
    unsigned int flags)
{
	(void)name; (void)nthreads; (void)ipl; (void)flags;
	return &the_taskq;
}

void
taskq_destroy(struct taskq *tq)
{
	(void)tq;
}

void
wifi_run_tasks(void)
{
	struct task *t;
	int budget = 32;

	if (tasks_running)
		return;
	tasks_running = 1;
	while ((t = task_head) != NULL && budget-- > 0) {
		task_head = t->t_next;
		t->t_next = NULL;
		t->t_pending = 0;
		t->t_func(t->t_arg);
	}
	tasks_running = 0;
}

void
wifi_forget_tasks(void)
{
	struct task *t;

	while ((t = task_head) != NULL) {
		task_head = t->t_next;
		t->t_pending = 0;
		t->t_next = NULL;
	}
	sleep_depth = 0;
}

/* ---- mbufs ------------------------------------------------------------ */
static char *
m_bufstart(struct mbuf *m, u_int *size)
{
	if ((m->m_flags & M_EXT) && m->m_ext.ext_buf != NULL) {
		*size = m->m_ext.ext_size;
		return m->m_ext.ext_buf;
	}
	*size = MLEN;
	return m->m_dat;
}

int
m_leadingspace(struct mbuf *m)
{
	u_int size;

	return (int)(m->m_data - m_bufstart(m, &size));
}

int
m_trailingspace(struct mbuf *m)
{
	u_int size;
	char *start = m_bufstart(m, &size);

	return (int)((start + size) - (m->m_data + m->m_len));
}

void
m_align(struct mbuf *m, int len)
{
	u_int size;
	char *start = m_bufstart(m, &size);

	if (len < 0 || (u_int)len > size)
		return;
	m->m_data = start + ((size - (u_int)len) & ~7U);
}

struct mbuf *
m_clget(struct mbuf *m, int how, u_int len)
{
	if (len > MCLBYTES)
		return NULL;		/* the pool has 2048 byte clusters */
	return MCLGETL(m, how, MCLBYTES);
}

int
m_dup_pkthdr(struct mbuf *to, struct mbuf *from, int how)
{
	(void)how;
	to->m_pkthdr = from->m_pkthdr;
	to->m_flags |= M_PKTHDR | (from->m_flags & (M_BCAST | M_MCAST |
	    M_VLANTAG | M_LINK0));
	return 0;
}

struct mbuf *
m_prepend(struct mbuf *m, int len, int how)
{
	struct mbuf *n;

	if (len <= m_leadingspace(m)) {
		m->m_data -= len;
		m->m_len += len;
	} else {
		if (len > MLEN) {
			m_freem(m);
			return NULL;
		}
		n = (m->m_flags & M_PKTHDR) ? m_gethdr(how, m->m_type) :
		    m_get(how, m->m_type);
		if (n == NULL) {
			m_freem(m);
			return NULL;
		}
		if (m->m_flags & M_PKTHDR) {
			m_dup_pkthdr(n, m, how);
			m->m_flags &= ~M_PKTHDR;
		}
		n->m_next = m;
		m = n;
		m_align(m, len);
		m->m_len = len;
	}
	if (m->m_flags & M_PKTHDR)
		m->m_pkthdr.len += len;
	return m;
}

/* The first `len` bytes contiguous: a chain that is not is copied into one
 * cluster (frames here are below 2048 bytes). */
struct mbuf *
m_pullup(struct mbuf *m, int len)
{
	struct mbuf *n, *t;
	int total = 0;

	if ((int)m->m_len >= len)
		return m;
	for (t = m; t != NULL; t = t->m_next)
		total += t->m_len;
	if (total < len || total > MCLBYTES - 64) {
		m_freem(m);
		return NULL;
	}
	n = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (n == NULL) {
		m_freem(m);
		return NULL;
	}
	n->m_data += 64;		/* room for headers put in front */
	m_copydata(m, 0, total, n->m_data);
	n->m_len = total;
	if (m->m_flags & M_PKTHDR)
		m_dup_pkthdr(n, m, M_DONTWAIT);
	n->m_pkthdr.len = total;
	m_freem(m);
	return n;
}

/* The packet from byte len0 on becomes a packet of its own. */
struct mbuf *
m_split(struct mbuf *m0, int len0, int how)
{
	struct mbuf *n, *t;
	int total = 0, rest;

	for (t = m0; t != NULL; t = t->m_next)
		total += t->m_len;
	rest = total - len0;
	if (len0 < 0 || rest < 0 || rest > MCLBYTES)
		return NULL;
	n = MCLGETL(NULL, how, MCLBYTES);
	if (n == NULL)
		return NULL;
	m_copydata(m0, len0, rest, n->m_data);
	n->m_len = n->m_pkthdr.len = rest;
	m_adj(m0, -rest);
	return n;
}

void
ml_init(struct mbuf_list *ml)
{
	ml->ml_head = ml->ml_tail = NULL;
	ml->ml_len = 0;
}

int
ml_purge(struct mbuf_list *ml)
{
	struct mbuf *m;
	int n = 0;

	while ((m = ml_dequeue(ml)) != NULL) {
		m_freem(m);
		n++;
	}
	return n;
}

void
mq_init(struct mbuf_queue *mq, u_int maxlen, int ipl)
{
	(void)ipl;
	ml_init(&mq->mq_list);
	mq->mq_maxlen = maxlen;
	mq->mq_drops = 0;
}

int
mq_enqueue(struct mbuf_queue *mq, struct mbuf *m)
{
	if (mq->mq_list.ml_len >= mq->mq_maxlen) {
		mq->mq_drops++;
		m_freem(m);
		return ENOBUFS;
	}
	ml_enqueue(&mq->mq_list, m);
	return 0;
}

struct mbuf *
mq_dequeue(struct mbuf_queue *mq)
{
	return ml_dequeue(&mq->mq_list);
}

int
mq_purge(struct mbuf_queue *mq)
{
	return ml_purge(&mq->mq_list);
}

/* ---- interface -------------------------------------------------------- */
void
if_start(struct ifnet *ifp)
{
	if (ifp->if_start != NULL)
		ifp->if_start(ifp);
}

int
if_enqueue(struct ifnet *ifp, struct mbuf *m)
{
	int error = ifq_enqueue(&ifp->if_snd, m);

	if (error == 0)
		if_start(ifp);
	return error;
}

int
if_setlladdr(struct ifnet *ifp, const uint8_t *lladdr)
{
	memcpy(((struct arpcom *)ifp)->ac_enaddr, lladdr, ETHER_ADDR_LEN);
	return 0;
}

/* ---- firmware --------------------------------------------------------- */
/* \EFI\HAK\firmware\<name>, read by the UEFI side; handed to the driver in
 * memory of this allocator so that free(buf, M_DEVBUF, len) is right. */
int
loadfirmware(const char *name, u_char **bufp, size_t *lenp)
{
	void *file = NULL;
	size_t len = 0;
	u_char *buf;

	if (wifi_host_loadfile(name, &file, &len) != 0 || file == NULL ||
	    len == 0) {
		printf("firmware file %s not found in \\EFI\\HAK\\firmware\n",
		    name);
		return ENOENT;
	}
	buf = malloc(len, M_DEVBUF, M_NOWAIT);
	if (buf == NULL) {
		wifi_host_freefile(file);
		return ENOMEM;
	}
	memcpy(buf, file, len);
	wifi_host_freefile(file);
	*bufp = buf;
	*lenp = len;
	return 0;
}
