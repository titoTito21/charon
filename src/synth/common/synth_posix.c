/* Charon synthesizer modules: the operating-system side of the C library.
 * GPL-2.0-or-later. Compiled against musl 1.2.5 internal headers (MIT): the
 * memory FILE follows musl's fmemopen.c, the console streams musl's
 * stdout.c, but nothing here performs a system call.
 *
 * - fopen() serves read-only files from the virtual file system;
 * - stdout/stderr exist (engines log to them) and discard their output, a
 *   module never prints to the firmware console;
 * - opendir()/readdir()/stat() describe the virtual file system;
 * - abort()/exit() fail the running coroutine instead of returning.
 */
#define _GNU_SOURCE 1
#include "stdio_impl.h"
#include "libc.h"
#include "pthread_impl.h"
#include "locale_impl.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <nl_types.h>
#include "synth_rt.h"

/* ---- thread block (see synth-musl-arch/pthread_arch.h) ---------------- */
unsigned char __synth_thread_block[sizeof(struct pthread)] __attribute__((aligned(64)));
static const synth_platform* active_platform;
int synth_rt_init_memory(const synth_platform* p, size_t limit);

int synth_rt_init(const synth_platform* platform, size_t limit)
{
    struct pthread* self = (struct pthread*)__synth_thread_block;
    memset(self, 0, sizeof *self);
    self->self = self;
    self->tid = 1;
    self->locale = &libc.global_locale;
    /* C.UTF-8 for multibyte conversions (musl's built-in UTF-8 map). */
    libc.global_locale.cat[LC_CTYPE] = &__c_dot_utf8;
    libc.page_size = 4096;
    active_platform = platform;
    return synth_rt_init_memory(platform, limit);
}

const synth_platform* synth_rt_platform(void) { return active_platform; }

/* ---- locales: only C and C.UTF-8 (musl c_locale.c) exist; replaces musl
 * locale_map.c, which loads locale files. */
volatile int __locale_lock[1];
volatile int* const __locale_lockptr = __locale_lock;

const char* __lctrans_impl(const char* message, const struct __locale_map* map) { (void)map; return message; }

const struct __locale_map* __get_locale(int category, const char* name)
{
    if (!name || !*name || !strcmp(name, "C") || !strcmp(name, "POSIX")) return NULL;
    return category == LC_CTYPE ? &__c_dot_utf8 : NULL;
}

/* Every request is answered with the one locale that exists. */
char* setlocale(int category, const char* name)
{
    static char current[] = "C.UTF-8";
    (void)category; (void)name;
    return current;
}

/* ---- console streams that discard -------------------------------------- */
/* The last bytes written to stderr (engine warnings, the C++ runtime's
 * "terminating due to ..." message) stay readable for test loaders. */
char synth_stderr_tail[512];
static size_t stderr_used;
static void keep_tail(const unsigned char* data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (stderr_used == sizeof synth_stderr_tail - 1) {
            memmove(synth_stderr_tail, synth_stderr_tail + 128, stderr_used - 128);
            stderr_used -= 128;
        }
        synth_stderr_tail[stderr_used++] = data[i] >= ' ' ? (char)data[i] : ' ';
        synth_stderr_tail[stderr_used] = 0;
    }
}

static size_t sink_write(FILE* f, const unsigned char* buffer, size_t length)
{
    if (f->fd == 2) { keep_tail(f->wbase, (size_t)(f->wpos - f->wbase)); keep_tail(buffer, length); }
    f->wend = f->buf + f->buf_size;
    f->wpos = f->wbase = f->buf;
    return length;
}
static off_t no_seek(FILE* f, off_t offset, int whence) { (void)f; (void)offset; (void)whence; errno = ESPIPE; return -1; }
static int no_close(FILE* f) { (void)f; return 0; }
static size_t no_read(FILE* f, unsigned char* buffer, size_t length) { (void)buffer; (void)length; f->flags |= F_EOF; return 0; }

static unsigned char out_buffer[BUFSIZ + UNGET], err_buffer[UNGET + 8], in_buffer[BUFSIZ + UNGET];
hidden FILE __stdout_FILE = { .buf = out_buffer + UNGET, .buf_size = sizeof out_buffer - UNGET, .fd = 1,
    .flags = F_PERM | F_NORD, .lbf = '\n', .write = sink_write, .seek = no_seek, .close = no_close, .lock = -1 };
hidden FILE __stderr_FILE = { .buf = err_buffer + UNGET, .buf_size = 0, .fd = 2,
    .flags = F_PERM | F_NORD, .lbf = -1, .write = sink_write, .seek = no_seek, .close = no_close, .lock = -1 };
hidden FILE __stdin_FILE = { .buf = in_buffer + UNGET, .buf_size = sizeof in_buffer - UNGET, .fd = 0,
    .flags = F_PERM | F_NOWR, .read = no_read, .seek = no_seek, .close = no_close, .lock = -1 };
#undef stdout
#undef stderr
#undef stdin
FILE* const stdout = &__stdout_FILE;
FILE* const stderr = &__stderr_FILE;
FILE* const stdin = &__stdin_FILE;
FILE* volatile __stdout_used = &__stdout_FILE;
FILE* volatile __stderr_used = &__stderr_FILE;
FILE* volatile __stdin_used = &__stdin_FILE;

/* ---- locking: one thread, no futexes (replaces musl __lock.c,
 * __lockfile.c, flockfile.c and the locking getc/putc variants) ---------- */
void __lock(volatile int* lock) { (void)lock; }
void __unlock(volatile int* lock) { (void)lock; }
int __lockfile(FILE* f) { (void)f; return 0; }
void __unlockfile(FILE* f) { (void)f; }
void flockfile(FILE* f) { (void)f; }
int ftrylockfile(FILE* f) { (void)f; return 0; }
void funlockfile(FILE* f) { (void)f; }
int fgetc(FILE* f) { return getc_unlocked(f); }
int getc(FILE* f) { return getc_unlocked(f); }
weak_alias(getc, _IO_getc);
int getchar(void) { return getc_unlocked(stdin); }
int fputc(int c, FILE* f) { return putc_unlocked(c, f); }
int putc(int c, FILE* f) { return putc_unlocked(c, f); }
weak_alias(putc, _IO_putc);
int putchar(int c) { return putc_unlocked(c, stdout); }

/* ---- pthread synchronisation for single-threaded engines (RHVoice guards
 * shared voice data with mutexes). One caller at a time by contract, so a
 * mutex is always free; waiting on a condition would never end and fails. */
int pthread_mutex_init(pthread_mutex_t* restrict m, const pthread_mutexattr_t* restrict a) { (void)a; memset(m, 0, sizeof *m); return 0; }
int pthread_mutex_destroy(pthread_mutex_t* m) { (void)m; return 0; }
int pthread_mutex_lock(pthread_mutex_t* m) { (void)m; return 0; }
int pthread_mutex_trylock(pthread_mutex_t* m) { (void)m; return 0; }
int pthread_mutex_unlock(pthread_mutex_t* m) { (void)m; return 0; }
int pthread_cond_init(pthread_cond_t* restrict c, const pthread_condattr_t* restrict a) { (void)a; memset(c, 0, sizeof *c); return 0; }
int pthread_cond_destroy(pthread_cond_t* c) { (void)c; return 0; }
int pthread_cond_signal(pthread_cond_t* c) { (void)c; return 0; }
int pthread_cond_broadcast(pthread_cond_t* c) { (void)c; return 0; }
int pthread_cond_wait(pthread_cond_t* restrict c, pthread_mutex_t* restrict m) { (void)c; (void)m; return EDEADLK; }

/* ---- read-only memory files (after musl fmemopen.c) ------------------- */
struct memory_file {
    FILE f;
    unsigned char* data;
    size_t size, position;
    unsigned char buffer[BUFSIZ + UNGET];
};

static size_t memory_read(FILE* f, unsigned char* buffer, size_t length)
{
    struct memory_file* m = f->cookie;
    size_t left = m->position < m->size ? m->size - m->position : 0;
    if (length > left) { length = left; f->flags |= F_EOF; }
    memcpy(buffer, m->data + m->position, length);
    m->position += length;
    left -= length;
    if (left > f->buf_size) left = f->buf_size;
    f->rpos = f->buf;
    f->rend = f->buf + left;
    memcpy(f->rpos, m->data + m->position, left);
    m->position += left;
    return length;
}

static off_t memory_seek(FILE* f, off_t offset, int whence)
{
    struct memory_file* m = f->cookie;
    if ((unsigned)whence > 2u) { errno = EINVAL; return -1; }
    off_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (off_t)m->position : (off_t)m->size;
    if (offset < -base || offset > (off_t)m->size - base) { errno = EINVAL; return -1; }
    m->position = (size_t)(base + offset);
    return (off_t)m->position;
}

static int memory_close(FILE* f)
{
    struct memory_file* m = f->cookie;
    free(m->data);
    m->data = NULL;
    return 0;
}

static int read_only_mode(const char* mode)
{
    return mode && mode[0] == 'r' && !strchr(mode, '+');
}

FILE* fopen(const char* restrict path, const char* restrict mode)
{
    if (!read_only_mode(mode)) { errno = EROFS; return NULL; }
    void* data;
    size_t size;
    if (synth_vfs_load(path, &data, &size)) { errno = ENOENT; return NULL; }
    struct memory_file* m = calloc(1, sizeof *m);
    if (!m) { free(data); errno = ENOMEM; return NULL; }
    m->data = data;
    m->size = size;
    m->f.cookie = m;
    m->f.fd = -1;
    m->f.flags = F_NOWR;
    m->f.buf = m->buffer + UNGET;
    m->f.buf_size = BUFSIZ;
    m->f.lbf = EOF;
    m->f.read = memory_read;
    m->f.seek = memory_seek;
    m->f.close = memory_close;
    m->f.lock = -1;
    return __ofl_add(&m->f);
}
weak_alias(fopen, fopen64);

FILE* freopen(const char* restrict path, const char* restrict mode, FILE* restrict f)
{
    (void)path; (void)mode; (void)f;
    errno = EROFS;
    return NULL;
}

int remove(const char* path) { (void)path; errno = EROFS; return -1; }
int rename(const char* a, const char* b) { (void)a; (void)b; errno = EROFS; return -1; }
char* tmpnam(char* s) { (void)s; return NULL; }
FILE* tmpfile(void) { errno = EROFS; return NULL; }

/* ---- directories and stat ---------------------------------------------- */
struct __dirstream {
    char path[512];
    size_t index;
    struct dirent entry;
};

DIR* opendir(const char* path)
{
    if (synth_vfs_kind(path, NULL) != 2) { errno = ENOENT; return NULL; }
    DIR* d = calloc(1, sizeof *d);
    if (!d) { errno = ENOMEM; return NULL; }
    size_t n = strlen(path);
    if (n >= sizeof d->path) { free(d); errno = ENAMETOOLONG; return NULL; }
    memcpy(d->path, path, n + 1);
    return d;
}

struct dirent* readdir(DIR* d)
{
    int kind = 0;
    const char* name = synth_vfs_child(d->path, d->index, &kind);
    if (!name) return NULL;
    d->index++;
    memset(&d->entry, 0, sizeof d->entry);
    d->entry.d_ino = d->index;
    d->entry.d_type = kind == 2 ? DT_DIR : DT_REG;
    size_t n = strlen(name);
    if (n >= sizeof d->entry.d_name) n = sizeof d->entry.d_name - 1;
    memcpy(d->entry.d_name, name, n);
    return &d->entry;
}

int closedir(DIR* d) { free(d); return 0; }
void rewinddir(DIR* d) { d->index = 0; }

int stat(const char* restrict path, struct stat* restrict st)
{
    size_t size = 0;
    int kind = synth_vfs_kind(path, &size);
    if (!kind) { errno = ENOENT; return -1; }
    memset(st, 0, sizeof *st);
    st->st_mode = kind == 2 ? (S_IFDIR | 0555) : (S_IFREG | 0444);
    st->st_size = (off_t)size;
    st->st_nlink = 1;
    return 0;
}
int lstat(const char* restrict path, struct stat* restrict st) { return stat(path, st); }
int access(const char* path, int mode) { (void)mode; if (synth_vfs_kind(path, NULL)) return 0; errno = ENOENT; return -1; }
int mkdir(const char* path, mode_t mode) { (void)path; (void)mode; errno = EROFS; return -1; }

/* ---- process ----------------------------------------------------------- */

_Noreturn void abort(void) { synth_coro_fail(); }
_Noreturn void exit(int status) { (void)status; synth_coro_fail(); }
_Noreturn void _Exit(int status) { (void)status; synth_coro_fail(); }
int atexit(void (*function)(void)) { (void)function; return 0; }
int __cxa_atexit(void (*function)(void*), void* argument, void* dso) { (void)function; (void)argument; (void)dso; return 0; }
void* __dso_handle = 0;

/* No wall clock is needed by the engines' synthesis: a fixed epoch keeps
 * runs reproducible. clock() counts calls so loops that poll it advance. */
time_t time(time_t* t) { if (t) *t = 0; return 0; }
clock_t clock(void) { static clock_t ticks; return ticks += 1000; }
int clock_gettime(clockid_t id, struct timespec* ts) { (void)id; ts->tv_sec = 0; ts->tv_nsec = 0; return 0; }

/* No message catalogues (libc++ std::messages): every lookup yields the
 * caller's default string. No time formatting (no wall clock in a module). */
nl_catd catopen(const char* name, int flags) { (void)name; (void)flags; errno = ENOENT; return (nl_catd)-1; }
char* catgets(nl_catd catalogue, int set, int message, const char* fallback) { (void)catalogue; (void)set; (void)message; return (char*)fallback; }
int catclose(nl_catd catalogue) { (void)catalogue; return 0; }
size_t strftime_l(char* restrict s, size_t n, const char* restrict format, const struct tm* restrict tm, locale_t l)
{
    (void)format; (void)tm; (void)l;
    if (n) s[0] = 0;
    return 0;
}
size_t strftime(char* restrict s, size_t n, const char* restrict format, const struct tm* restrict tm)
{
    return strftime_l(s, n, format, tm, 0);
}

char synth_diagnostic[160];
void synth_diagnose(const char* text)
{
    size_t n = strlen(text);
    if (n >= sizeof synth_diagnostic) n = sizeof synth_diagnostic - 1;
    memcpy(synth_diagnostic, text, n);
    synth_diagnostic[n] = 0;
}

/* ---- helpers ----------------------------------------------------------- */
uint32_t synth_crc32(const void* data, size_t bytes)
{
    const unsigned char* p = data;
    uint32_t c = 0xffffffffu;
    while (bytes--) {
        c ^= *p++;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

void synth_path_from_utf16(const uint16_t* text, size_t bytes, char* out, size_t capacity)
{
    size_t n = 0;
    for (size_t i = 0; text && i < bytes / 2 && text[i] && n + 1 < capacity; ++i) {
        uint16_t c = text[i];
        out[n++] = c == '\\' ? '/' : (c < 0x80 ? (char)c : '_');
    }
    while (n > 1 && out[n - 1] == '/') --n;
    out[n] = 0;
}
