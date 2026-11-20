/* CharonPC: the operating-system side of the C library for Bochs.
 * GPL-2.0-or-later. Compiled against musl 1.2.5 internal headers (MIT); the
 * thread block, locale, locking and console-stream parts follow
 * src/synth/common/synth_posix.c. Nothing here performs a system call: file
 * descriptors are UEFI files of the boot volume (pc_efi.cpp), memory
 * images or the two sinks (log, Braille 'n Speak on COM1).
 */
#define _GNU_SOURCE 1
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>
#include <nl_types.h>
#include <pthread.h>
#include <utime.h>
#include "stdio_impl.h"
#include "libc.h"
#include "pthread_impl.h"
#include "locale_impl.h"
#include "synth_rt.h"
#include "pc_platform.h"

/* ---- thread block (src/synth/common/synth-musl-arch/pthread_arch.h) ---- */
unsigned char __synth_thread_block[sizeof(struct pthread)] __attribute__((aligned(64)));
int synth_rt_init_memory(const synth_platform* p, size_t limit);
static synth_platform platform;

int pc_rt_init(size_t limit)
{
    struct pthread* self = (struct pthread*)__synth_thread_block;
    memset(self, 0, sizeof *self);
    self->self = self;
    self->tid = 1;
    self->locale = &libc.global_locale;
    libc.global_locale.cat[LC_CTYPE] = &__c_dot_utf8;
    libc.page_size = 4096;
    platform.context = NULL;
    platform.allocate = pc_allocate;
    platform.release = pc_release;
    return synth_rt_init_memory(&platform, limit);
}

/* synth_coro.c reports through these (synth_rt.h). */
char synth_diagnostic[160];
void synth_diagnose(const char* text)
{
    size_t n = strlen(text);
    if (n >= sizeof synth_diagnostic) n = sizeof synth_diagnostic - 1;
    memcpy(synth_diagnostic, text, n);
    synth_diagnostic[n] = 0;
}

/* ---- locales: C and C.UTF-8 only ---- */
volatile int __locale_lock[1];
volatile int* const __locale_lockptr = __locale_lock;
const char* __lctrans_impl(const char* message, const struct __locale_map* map) { (void)map; return message; }
const struct __locale_map* __get_locale(int category, const char* name)
{
    if (!name || !*name || !strcmp(name, "C") || !strcmp(name, "POSIX")) return NULL;
    return category == LC_CTYPE ? &__c_dot_utf8 : NULL;
}
char* setlocale(int category, const char* name)
{
    static char current[] = "C.UTF-8";
    (void)category; (void)name;
    return current;
}

/* ---- locking: one thread ---- */
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

/* ---- file descriptors ---- */
enum { FD_FREE, FD_VOLUME, FD_MEMORY, FD_LOG, FD_BNS, FD_NULL };
#define FD_MAX 64
struct descriptor {
    int kind;
    int flags;               /* O_ACCMODE | O_APPEND */
    void* handle;            /* FD_VOLUME */
    const unsigned char* data;
    uint64_t size, position;
};
static struct descriptor fds[FD_MAX];

static int new_fd(int kind)
{
    for (int i = 3; i < FD_MAX; ++i)
        if (fds[i].kind == FD_FREE) {
            memset(&fds[i], 0, sizeof fds[i]);
            fds[i].kind = kind;
            return i;
        }
    errno = EMFILE;
    return -1;
}

static struct descriptor* get_fd(int fd)
{
    if (fd < 0 || fd >= FD_MAX || fds[fd].kind == FD_FREE) { errno = EBADF; return NULL; }
    return &fds[fd];
}

static void init_std_fds(void)
{
    if (fds[1].kind) return;
    fds[0].kind = FD_NULL; fds[0].flags = O_RDONLY;
    fds[1].kind = FD_LOG; fds[1].flags = O_WRONLY;
    fds[2].kind = FD_LOG; fds[2].flags = O_WRONLY;
}

int open(const char* path, int flags, ...)
{
    init_std_fds();
    if (!path) { errno = EFAULT; return -1; }
    const int access = flags & O_ACCMODE;
    if (!strncmp(path, "rom:", 4)) {
        size_t size = 0;
        const unsigned char* data = pc_rom(path + 4, &size);
        if (!data) { errno = ENOENT; return -1; }
        if (access != O_RDONLY) { errno = EROFS; return -1; }
        int fd = new_fd(FD_MEMORY);
        if (fd < 0) return -1;
        fds[fd].data = data; fds[fd].size = size; fds[fd].flags = flags;
        return fd;
    }
    if (!strcmp(path, "charon:log") || !strcmp(path, "charon:bns") || !strcmp(path, "/dev/null")) {
        int fd = new_fd(!strcmp(path, "charon:log") ? FD_LOG : !strcmp(path, "charon:bns") ? FD_BNS : FD_NULL);
        if (fd >= 0) fds[fd].flags = flags;
        return fd;
    }
    void* handle = NULL;
    uint64_t size = 0;
    int error = pc_vol_open(path, access != O_RDONLY, (flags & O_CREAT) != 0, (flags & O_TRUNC) != 0, &handle, &size);
    if (error) { errno = error; return -1; }
    int fd = new_fd(FD_VOLUME);
    if (fd < 0) { pc_vol_close(handle); return -1; }
    fds[fd].handle = handle; fds[fd].size = size; fds[fd].flags = flags;
    return fd;
}
weak_alias(open, open64);

int creat(const char* path, mode_t mode) { (void)mode; return open(path, O_WRONLY | O_CREAT | O_TRUNC); }

int close(int fd)
{
    struct descriptor* d = get_fd(fd);
    if (!d) return -1;
    if (fd < 3) return 0;
    if (d->kind == FD_VOLUME) pc_vol_close(d->handle);
    d->kind = FD_FREE;
    return 0;
}

ssize_t read(int fd, void* buffer, size_t count)
{
    init_std_fds();
    struct descriptor* d = get_fd(fd);
    if (!d) return -1;
    if ((d->flags & O_ACCMODE) == O_WRONLY) { errno = EBADF; return -1; }
    switch (d->kind) {
    case FD_MEMORY: {
        uint64_t left = d->position < d->size ? d->size - d->position : 0;
        if (count > left) count = (size_t)left;
        memcpy(buffer, d->data + d->position, count);
        d->position += count;
        return (ssize_t)count;
    }
    case FD_VOLUME: {
        long n = pc_vol_read(d->handle, d->position, buffer, count);
        if (n < 0) { errno = EIO; return -1; }
        d->position += (uint64_t)n;
        return n;
    }
    default:
        return 0;
    }
}

ssize_t write(int fd, const void* buffer, size_t count)
{
    init_std_fds();
    struct descriptor* d = get_fd(fd);
    if (!d) return -1;
    if ((d->flags & O_ACCMODE) == O_RDONLY && fd > 2) { errno = EBADF; return -1; }
    switch (d->kind) {
    case FD_LOG: pc_log_write(buffer, count); return (ssize_t)count;
    case FD_BNS: pc_bns_write(buffer, count); return (ssize_t)count;
    case FD_NULL: return (ssize_t)count;
    case FD_VOLUME: {
        if (d->flags & O_APPEND) d->position = d->size;
        long n = pc_vol_write(d->handle, d->position, buffer, count);
        if (n < 0) { errno = EIO; return -1; }
        d->position += (uint64_t)n;
        if (d->position > d->size) d->size = d->position;
        return n;
    }
    default:
        errno = EBADF;
        return -1;
    }
}

off_t lseek(int fd, off_t offset, int whence)
{
    struct descriptor* d = get_fd(fd);
    if (!d) return -1;
    if (d->kind != FD_VOLUME && d->kind != FD_MEMORY) { errno = ESPIPE; return -1; }
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (int64_t)d->position
                 : whence == SEEK_END ? (int64_t)d->size : -1;
    if (base < 0 || base + offset < 0) { errno = EINVAL; return -1; }
    d->position = (uint64_t)(base + offset);
    return (off_t)d->position;
}
weak_alias(lseek, lseek64);
weak_alias(lseek, __lseek);

ssize_t pread(int fd, void* buffer, size_t count, off_t offset)
{
    struct descriptor* d = get_fd(fd);
    if (!d) return -1;
    uint64_t keep = d->position;
    d->position = (uint64_t)offset;
    ssize_t n = read(fd, buffer, count);
    d->position = keep;
    return n;
}
ssize_t pwrite(int fd, const void* buffer, size_t count, off_t offset)
{
    struct descriptor* d = get_fd(fd);
    if (!d) return -1;
    uint64_t keep = d->position;
    d->position = (uint64_t)offset;
    ssize_t n = write(fd, buffer, count);
    d->position = keep;
    return n;
}
weak_alias(pread, pread64);
weak_alias(pwrite, pwrite64);

int ftruncate(int fd, off_t size)
{
    struct descriptor* d = get_fd(fd);
    if (!d) return -1;
    if (d->kind != FD_VOLUME || size < 0) { errno = EINVAL; return -1; }
    int error = pc_vol_set_size(d->handle, (uint64_t)size);
    if (error) { errno = error; return -1; }
    d->size = (uint64_t)size;
    return 0;
}
weak_alias(ftruncate, ftruncate64);

int fsync(int fd)
{
    struct descriptor* d = get_fd(fd);
    if (!d) return -1;
    if (d->kind == FD_VOLUME && pc_vol_flush(d->handle)) { errno = EIO; return -1; }
    return 0;
}
int fdatasync(int fd) { return fsync(fd); }

static void fill_stat(struct stat* st, uint64_t size, int kind)
{
    memset(st, 0, sizeof *st);
    st->st_mode = kind == 2 ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    st->st_size = (off_t)size;
    st->st_nlink = 1;
    st->st_blksize = 512;
    st->st_blocks = (blkcnt_t)((size + 511) / 512);
}

int fstat(int fd, struct stat* st)
{
    init_std_fds();
    struct descriptor* d = get_fd(fd);
    if (!d) return -1;
    if (d->kind == FD_VOLUME || d->kind == FD_MEMORY) fill_stat(st, d->size, 1);
    else { fill_stat(st, 0, 1); st->st_mode = S_IFCHR | 0666; }
    return 0;
}
weak_alias(fstat, fstat64);

int stat(const char* restrict path, struct stat* restrict st)
{
    if (!strncmp(path, "rom:", 4)) {
        size_t size = 0;
        if (!pc_rom(path + 4, &size)) { errno = ENOENT; return -1; }
        fill_stat(st, size, 1);
        return 0;
    }
    uint64_t size = 0;
    int kind = 0;
    int error = pc_vol_stat(path, &size, &kind);
    if (error) { errno = error; return -1; }
    fill_stat(st, size, kind);
    return 0;
}
weak_alias(stat, stat64);
int lstat(const char* restrict path, struct stat* restrict st) { return stat(path, st); }
weak_alias(lstat, lstat64);
int access(const char* path, int mode) { struct stat st; (void)mode; return stat(path, &st); }
int unlink(const char* path) { int e = pc_vol_delete(path); if (e) { errno = e; return -1; } return 0; }
int remove(const char* path) { return unlink(path); }
int rename(const char* a, const char* b) { (void)a; (void)b; errno = EROFS; return -1; }
int mkdir(const char* path, mode_t mode) { (void)path; (void)mode; errno = EROFS; return -1; }
int rmdir(const char* path) { (void)path; errno = EROFS; return -1; }
int isatty(int fd) { (void)fd; errno = ENOTTY; return 0; }
int fcntl(int fd, int command, ...) { (void)command; return get_fd(fd) ? 0 : -1; }
int ioctl(int fd, int request, ...) { (void)fd; (void)request; errno = ENOTTY; return -1; }
int dup(int fd) { (void)fd; errno = EMFILE; return -1; }
int dup2(int fd, int to) { (void)fd; (void)to; errno = EMFILE; return -1; }
char* getcwd(char* buffer, size_t size)
{
    if (!buffer || size < 2) { errno = ERANGE; return NULL; }
    buffer[0] = '/'; buffer[1] = 0;
    return buffer;
}
int chdir(const char* path) { (void)path; return 0; }
pid_t getpid(void) { return 1; }
uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
long sysconf(int name) { return name == _SC_PAGESIZE ? 4096 : -1; }
int mkstemp(char* t) { (void)t; errno = EROFS; return -1; }
char* realpath(const char* restrict path, char* restrict resolved)
{
    size_t n = strlen(path);
    if (!resolved) resolved = malloc(n + 1);
    if (!resolved) return NULL;
    memcpy(resolved, path, n + 1);
    return resolved;
}

/* No memory-mapped files: Bochs's sparse images then use plain reads. */
void* mmap(void* a, size_t n, int p, int f, int fd, off_t o) { (void)a; (void)n; (void)p; (void)f; (void)fd; (void)o; errno = ENODEV; return MAP_FAILED; }
weak_alias(mmap, mmap64);
int munmap(void* a, size_t n) { (void)a; (void)n; return 0; }
int msync(void* a, size_t n, int f) { (void)a; (void)n; (void)f; return 0; }

/* No directories are listed (Bochs's vvfat mode is not supported). */
DIR* opendir(const char* path) { (void)path; errno = ENOTSUP; return NULL; }
struct dirent* readdir(DIR* d) { (void)d; return NULL; }
int closedir(DIR* d) { (void)d; return 0; }

/* ---- stdio over descriptors (after musl __fdopen.c / __stdio_*.c) ---- */
static size_t fd_stream_read(FILE* f, unsigned char* buffer, size_t length)
{
    size_t direct = length - !!f->buf_size;
    ssize_t n = direct ? read(f->fd, buffer, direct) : 0;
    if (n < 0) { f->flags |= F_ERR; return 0; }
    if ((size_t)n < direct) { if (!n) f->flags |= F_EOF; return (size_t)n; }
    if (!f->buf_size) return (size_t)n;
    ssize_t m = read(f->fd, f->buf, f->buf_size);
    if (m <= 0) { if (!m && !n) f->flags |= F_EOF; if (m < 0) f->flags |= F_ERR; return (size_t)n; }
    f->rpos = f->buf;
    f->rend = f->buf + m;
    buffer[length - 1] = *f->rpos++;
    return length;
}

static size_t fd_stream_write(FILE* f, const unsigned char* buffer, size_t length)
{
    size_t pending = (size_t)(f->wpos - f->wbase);
    if (pending && write(f->fd, f->wbase, pending) != (ssize_t)pending) goto fail;
    if (length && write(f->fd, buffer, length) != (ssize_t)length) goto fail;
    f->wend = f->buf + f->buf_size;
    f->wpos = f->wbase = f->buf;
    return length;
fail:
    f->wpos = f->wbase = f->wend = 0;
    f->flags |= F_ERR;
    return 0;
}

static off_t fd_stream_seek(FILE* f, off_t offset, int whence) { return lseek(f->fd, offset, whence); }
static int fd_stream_close(FILE* f) { return close(f->fd); }

FILE* fdopen(int fd, const char* mode)
{
    if (!mode || !strchr("rwa", *mode)) { errno = EINVAL; return NULL; }
    FILE* f = malloc(sizeof *f + UNGET + BUFSIZ);
    if (!f) return NULL;
    memset(f, 0, sizeof *f);
    if (!strchr(mode, '+')) f->flags = (*mode == 'r') ? F_NOWR : F_NORD;
    if (*mode == 'a') { f->flags |= F_APP; fds[fd].flags |= O_APPEND; }
    f->fd = fd;
    f->buf = (unsigned char*)f + sizeof *f + UNGET;
    f->buf_size = BUFSIZ;
    f->lbf = (fds[fd].kind == FD_LOG || fds[fd].kind == FD_BNS) ? '\n' : EOF;
    f->read = fd_stream_read;
    f->write = fd_stream_write;
    f->seek = fd_stream_seek;
    f->close = fd_stream_close;
    f->lock = -1;
    return __ofl_add(f);
}

FILE* fopen(const char* restrict path, const char* restrict mode)
{
    if (!mode || !strchr("rwa", *mode)) { errno = EINVAL; return NULL; }
    int flags = strchr(mode, '+') ? O_RDWR : *mode == 'r' ? O_RDONLY : O_WRONLY;
    if (*mode != 'r') flags |= O_CREAT;
    if (*mode == 'w') flags |= O_TRUNC;
    if (*mode == 'a') flags |= O_APPEND;
    int fd = open(path, flags);
    if (fd < 0) return NULL;
    FILE* f = fdopen(fd, mode);
    if (!f) close(fd);
    return f;
}
weak_alias(fopen, fopen64);

FILE* freopen(const char* restrict path, const char* restrict mode, FILE* restrict f)
{
    (void)path; (void)mode; (void)f;
    errno = ENOTSUP;
    return NULL;
}
FILE* tmpfile(void) { errno = EROFS; return NULL; }
weak_alias(tmpfile, tmpfile64);
char* tmpnam(char* s) { (void)s; return NULL; }
FILE* popen(const char* c, const char* m) { (void)c; (void)m; errno = ENOSYS; return NULL; }
int pclose(FILE* f) { (void)f; return -1; }

/* Console streams: stdin is empty, stdout and stderr go to the Bochs log. */
static unsigned char out_buffer[BUFSIZ + UNGET], err_buffer[UNGET + 8], in_buffer[BUFSIZ + UNGET];
hidden FILE __stdout_FILE = { .buf = out_buffer + UNGET, .buf_size = sizeof out_buffer - UNGET, .fd = 1,
    .flags = F_PERM | F_NORD, .lbf = '\n', .write = fd_stream_write, .seek = fd_stream_seek, .close = fd_stream_close, .lock = -1 };
hidden FILE __stderr_FILE = { .buf = err_buffer + UNGET, .buf_size = 0, .fd = 2,
    .flags = F_PERM | F_NORD, .lbf = -1, .write = fd_stream_write, .seek = fd_stream_seek, .close = fd_stream_close, .lock = -1 };
hidden FILE __stdin_FILE = { .buf = in_buffer + UNGET, .buf_size = sizeof in_buffer - UNGET, .fd = 0,
    .flags = F_PERM | F_NOWR, .read = fd_stream_read, .seek = fd_stream_seek, .close = fd_stream_close, .lock = -1 };
#undef stdout
#undef stderr
#undef stdin
FILE* const stdout = &__stdout_FILE;
FILE* const stderr = &__stderr_FILE;
FILE* const stdin = &__stdin_FILE;
FILE* volatile __stdout_used = &__stdout_FILE;
FILE* volatile __stderr_used = &__stderr_FILE;
FILE* volatile __stdin_used = &__stdin_FILE;

/* Flushes every stream and closes every file Bochs left open, so data
 * written to disk images reaches the volume before CHARONPC returns. */
void pc_rt_close_all(void)
{
    fflush(NULL);
    for (int i = 3; i < FD_MAX; ++i)
        if (fds[i].kind == FD_VOLUME) { pc_vol_close(fds[i].handle); fds[i].kind = FD_FREE; }
}

/* ---- process ---- */
_Noreturn void abort(void) { synth_diagnose("abort()"); synth_coro_fail(); }
_Noreturn void exit(int status) { (void)status; fflush(NULL); synth_diagnose("exit()"); synth_coro_fail(); }
_Noreturn void _Exit(int status) { (void)status; synth_coro_fail(); }
_Noreturn void _exit(int status) { (void)status; synth_coro_fail(); }
int atexit(void (*function)(void)) { (void)function; return 0; }
int __cxa_atexit(void (*function)(void*), void* argument, void* dso) { (void)function; (void)argument; (void)dso; return 0; }
void* __dso_handle = 0;
void (*signal(int number, void (*handler)(int)))(int) { (void)number; (void)handler; return SIG_DFL; }
int raise(int number) { (void)number; return 0; }
unsigned alarm(unsigned seconds) { (void)seconds; return 0; }
int system(const char* command) { (void)command; return -1; }

/* ---- time ---- */
const char __utc[] = "UTC";
const char* __tm_to_tzname(const struct tm* tm) { (void)tm; return __utc; }

int gettimeofday(struct timeval* restrict tv, void* restrict tz)
{
    (void)tz;
    if (tv) {
        uint64_t us = pc_usec();
        tv->tv_sec = (time_t)(pc_epoch_start() + (int64_t)(us / 1000000));
        tv->tv_usec = (suseconds_t)(us % 1000000);
    }
    return 0;
}
time_t time(time_t* t)
{
    time_t now = (time_t)(pc_epoch_start() + (int64_t)(pc_usec() / 1000000));
    if (t) *t = now;
    return now;
}
int clock_gettime(clockid_t id, struct timespec* ts)
{
    uint64_t us = pc_usec();
    ts->tv_sec = (time_t)(us / 1000000);
    ts->tv_nsec = (long)(us % 1000000) * 1000;
    if (id == CLOCK_REALTIME) ts->tv_sec += (time_t)pc_epoch_start();
    return 0;
}
clock_t clock(void) { return (clock_t)pc_usec(); }
/* The firmware clock is kept in local time: local time is "UTC" here. */
struct tm* localtime_r(const time_t* restrict t, struct tm* restrict tm) { return gmtime_r(t, tm); }
struct tm* localtime(const time_t* t) { static struct tm tm; return gmtime_r(t, &tm); }
time_t mktime(struct tm* tm) { return timegm(tm); }
void tzset(void) {}
int usleep(unsigned microseconds) { pc_stall(microseconds); return 0; }
unsigned sleep(unsigned seconds) { while (seconds--) pc_stall(1000000); return 0; }
int nanosleep(const struct timespec* req, struct timespec* rem)
{
    (void)rem;
    pc_stall((uint32_t)(req->tv_sec * 1000000 + req->tv_nsec / 1000));
    return 0;
}

/* ---- message catalogues (libc++ std::messages) ---- */
nl_catd catopen(const char* name, int flags) { (void)name; (void)flags; errno = ENOENT; return (nl_catd)-1; }
char* catgets(nl_catd c, int set, int message, const char* fallback) { (void)c; (void)set; (void)message; return (char*)fallback; }
int catclose(nl_catd c) { (void)c; return 0; }

/* ---- one thread: a mutex is always free ---- */
int pthread_mutex_init(pthread_mutex_t* restrict m, const pthread_mutexattr_t* restrict a) { (void)a; memset(m, 0, sizeof *m); return 0; }
int pthread_mutex_destroy(pthread_mutex_t* m) { (void)m; return 0; }
int pthread_mutex_lock(pthread_mutex_t* m) { (void)m; return 0; }
int pthread_mutex_trylock(pthread_mutex_t* m) { (void)m; return 0; }
int pthread_mutex_unlock(pthread_mutex_t* m) { (void)m; return 0; }
int getpagesize(void) { return 4096; }
char* mktemp(char* t) { if (t && *t) *t = 0; return t; }
int utime(const char* path, const struct utimbuf* times) { (void)path; (void)times; return 0; }

/* ---- networking: none in stage 1 (Bochs compiles socket modes of COM ports) ---- */
int socket(int domain, int type, int protocol) { (void)domain; (void)type; (void)protocol; errno = EAFNOSUPPORT; return -1; }
int bind(int s, const struct sockaddr* a, socklen_t l) { (void)s; (void)a; (void)l; errno = EBADF; return -1; }
int listen(int s, int b) { (void)s; (void)b; errno = EBADF; return -1; }
int accept(int s, struct sockaddr* restrict a, socklen_t* restrict l) { (void)s; (void)a; (void)l; errno = EBADF; return -1; }
int connect(int s, const struct sockaddr* a, socklen_t l) { (void)s; (void)a; (void)l; errno = EBADF; return -1; }
ssize_t send(int s, const void* b, size_t n, int f) { (void)s; (void)b; (void)n; (void)f; errno = EBADF; return -1; }
ssize_t recv(int s, void* b, size_t n, int f) { (void)s; (void)b; (void)n; (void)f; errno = EBADF; return -1; }
int setsockopt(int s, int l, int n, const void* v, socklen_t c) { (void)s; (void)l; (void)n; (void)v; (void)c; errno = EBADF; return -1; }
struct hostent* gethostbyname(const char* name) { (void)name; return NULL; }
int select(int n, fd_set* restrict r, fd_set* restrict w, fd_set* restrict e, struct timeval* restrict t)
{
    (void)n; (void)r; (void)w; (void)e; (void)t;
    return 0;
}
