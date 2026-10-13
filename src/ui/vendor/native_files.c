/* Native O32 implementation of ctl_platform file callbacks. See native_files.h. */
#include "native_files.h"

#include <asm/errno.h>
#include <asm/stat.h>
#include <asm/unistd.h>
#include <linux/fcntl.h>
#include <linux/stat.h>

#if !defined(__mips__) || _MIPS_SIM != _MIPS_SIM_ABI32 || !defined(__MIPSEL__)
#error "native_files.c targets Linux MIPS O32 little-endian only"
#endif
#if !defined(__mips_nan2008)
#error "build with -mnan=2008 (XM5 userland is nan2008)"
#endif

/* struct stat64, O32 (arch/mips/include/uapi/asm/stat.h). Offsets also match qemu's target_stat64 for
 * TARGET_ABI_MIPSO32; any header drift breaks the build here. */
_Static_assert(sizeof(struct stat64) == 104, "stat64 size");
_Static_assert(__builtin_offsetof(struct stat64, st_ino) == 16, "st_ino");
_Static_assert(__builtin_offsetof(struct stat64, st_mode) == 24, "st_mode");
_Static_assert(__builtin_offsetof(struct stat64, st_rdev) == 40, "st_rdev");
_Static_assert(__builtin_offsetof(struct stat64, st_size) == 56, "st_size");
_Static_assert(__builtin_offsetof(struct stat64, st_blocks) == 96, "st_blocks");
_Static_assert(sizeof(((struct stat64 *)0)->st_size) == 8, "st_size width");

/* struct linux_dirent64 is not exported by UAPI. Kernel include/linux/dirent.h (v6.8):
 *   u64 d_ino; s64 d_off; unsigned short d_reclen; unsigned char d_type; char d_name[];
 * Records are parsed with byte loads at these offsets, so a corrupt reclen cannot cause unaligned access. */
#define NF_DIRENT_RECLEN_OFF 16
#define NF_DIRENT_TYPE_OFF 18
#define NF_DIRENT_NAME_OFF 19
/* d_type values, kernel include/linux/fs_types.h (FT_* >> 12 == S_IF* >> 12); not exported by UAPI. */
#define NF_DT_UNKNOWN 0
#define NF_DT_REG (S_IFREG >> 12)
_Static_assert(NF_DT_REG == 8, "DT_REG");

#define NF_OPEN_DIR (O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_LARGEFILE)
#define NF_OPEN_READ (O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC | O_LARGEFILE)
#define NF_OPEN_TMP (O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_NOCTTY | O_CLOEXEC | O_LARGEFILE)

/* O32: number in v0, args a0..a3; on return a3 != 0 means v0 holds a positive errno.
 * The kernel preserves a0..a2; at, v1, t0..t9, hi, lo may be clobbered. */
long nf_raw_syscall(long nr, long a0, long a1, long a2, long a3) {
    register long v0 __asm__("$2") = nr;
    register long r4 __asm__("$4") = a0;
    register long r5 __asm__("$5") = a1;
    register long r6 __asm__("$6") = a2;
    register long r7 __asm__("$7") = a3;
    __asm__ volatile("syscall"
                     : "+r"(v0), "+r"(r7)
                     : "r"(r4), "r"(r5), "r"(r6)
                     : "$1", "$3", "$8", "$9", "$10", "$11", "$12", "$13", "$14", "$15", "$24", "$25", "hi", "lo", "memory");
    return r7 ? -v0 : v0;
}

static long sc(nf_backend *b, long nr, long a0, long a1, long a2, long a3) {
    return b->sys ? b->sys(b->sys_arg, nr, a0, a1, a2, a3) : nf_raw_syscall(nr, a0, a1, a2, a3);
}

/* For calls that transfer nothing when interrupted (openat, fsync, getdents64). */
static long sc_retry(nf_backend *b, long nr, long a0, long a1, long a2, long a3) {
    long r;
    int i = 0;
    do r = sc(b, nr, a0, a1, a2, a3);
    while (r == -EINTR && ++i < NF_EINTR_RETRIES);
    return r;
}

static void sc_close(nf_backend *b, long fd) { (void)sc(b, __NR_close, fd, 0, 0, 0); } /* never retried */

static int is_dot(const char *n) { return n[0] == '.' && (!n[1] || (n[1] == '.' && !n[2])); }

/* One path component / directory entry name: 1..NF_NAME_MAX bytes, no '/', not "." or "..". */
static int name_ok(const char *n) {
    unsigned i;
    if (!n || !n[0] || is_dot(n)) return 0;
    for (i = 0; n[i]; ++i)
        if (n[i] == '/' || i >= NF_NAME_MAX) return 0;
    return 1;
}

/* Absolute directory path made of name_ok components separated by single '/', no trailing '/'. */
static int path_copy(char *dst, const char *src) {
    char comp[NF_NAME_MAX + 1];
    unsigned i = 0, n = 0;
    if (!src || src[0] != '/' || !src[1]) return -1;
    for (;;) {
        char ch = src[i];
        if (i >= NF_PATH_MAX - 1) return -1;
        dst[i] = ch;
        if (i > 0 && (ch == '/' || !ch)) {
            comp[n] = 0;
            if (!name_ok(comp)) return -1;
            n = 0;
            if (!ch) return 0;
        } else if (i > 0) {
            if (n >= NF_NAME_MAX) return -1;
            comp[n++] = ch;
        }
        ++i;
    }
}

/* Open `path` from "/" one component at a time with O_NOFOLLOW|O_DIRECTORY, so no component may be a
 * symlink. create_last: mkdirat (0755) the final component when it is missing. Returns fd or -errno. */
static long walk_dir(nf_backend *b, const char *path, int create_last) {
    char comp[NF_NAME_MAX + 1];
    const char *p = path + 1;
    long fd = sc_retry(b, __NR_openat, AT_FDCWD, (long)"/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_LARGEFILE, 0);
    while (fd >= 0 && *p) {
        unsigned n = 0;
        long nfd;
        while (p[n] && p[n] != '/') { comp[n] = p[n]; ++n; } /* n <= NF_NAME_MAX: checked by path_copy */
        comp[n] = 0;
        p += n;
        if (*p == '/') ++p;
        nfd = sc_retry(b, __NR_openat, fd, (long)comp, NF_OPEN_DIR, 0);
        if (nfd == -ENOENT && create_last && !*p) {
            long m = sc(b, __NR_mkdirat, fd, (long)comp, 0755, 0);
            nfd = (m == 0 || m == -EEXIST) ? sc_retry(b, __NR_openat, fd, (long)comp, NF_OPEN_DIR, 0) : m;
        }
        sc_close(b, fd);
        fd = nfd;
    }
    if (fd < 0) b->last_err = fd;
    return fd;
}

int nf_dirent_parse(const unsigned char *buf, long len, int (*rec)(void *arg, const char *name, unsigned type), void *arg) {
    long off = 0;
    int count = 0;
    if (len < 0) return -1;
    while (off < len) {
        const unsigned char *r = buf + off;
        unsigned long left = (unsigned long)(len - off);
        unsigned reclen, max, n = 0;
        if (left < NF_DIRENT_NAME_OFF + 2) return -1;
        reclen = r[NF_DIRENT_RECLEN_OFF] | (unsigned)r[NF_DIRENT_RECLEN_OFF + 1] << 8; /* little-endian */
        if (reclen < NF_DIRENT_NAME_OFF + 2 || reclen > left) return -1;
        max = reclen - NF_DIRENT_NAME_OFF;
        while (n < max && r[NF_DIRENT_NAME_OFF + n]) ++n;
        if (n == 0 || n == max) return -1; /* empty, or no NUL inside the record */
        if (rec(arg, (const char *)r + NF_DIRENT_NAME_OFF, r[NF_DIRENT_TYPE_OFF])) return -1;
        ++count;
        off += reclen;
    }
    return count;
}

typedef struct {
    nf_backend *b;
    long dfd;
    void (*emit)(void *arg, const char *name);
    void *arg;
    unsigned seen;
} list_state;

static int list_rec(void *arg, const char *name, unsigned type) {
    list_state *s = arg;
    struct stat64 st;
    if (++s->seen > NF_MAX_DIRENTS) return 1;
    if (is_dot(name) || (type != NF_DT_REG && type != NF_DT_UNKNOWN)) return 0;
    /* d_type is a hint only: confirm with lstat semantics (the entry itself, never a link target). */
    if (sc(s->b, __NR_fstatat64, s->dfd, (long)name, (long)&st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(st.st_mode))
        s->emit(s->arg, name);
    return 0;
}

static int list_dir(void *ctx, void (*emit)(void *arg, const char *name), void *arg) {
    nf_backend *b = ctx;
    list_state s;
    int r = CTL_IO_OK, calls;
    long dfd = walk_dir(b, b->import_dir, 0);
    if (dfd < 0) return dfd == -ENOENT ? CTL_IO_MISSING : CTL_IO_ERROR;
    s.b = b; s.dfd = dfd; s.emit = emit; s.arg = arg; s.seen = 0;
    for (calls = 0;; ++calls) {
        long n;
        if (calls > NF_MAX_DIRENTS) { r = CTL_IO_ERROR; break; } /* every non-empty batch holds >= 1 record */
        n = sc_retry(b, __NR_getdents64, dfd, (long)b->dirbuf, sizeof b->dirbuf, 0);
        if (n == 0) break;
        if (n < 0 || (unsigned long)n > sizeof b->dirbuf) { b->last_err = n < 0 ? n : -EIO; r = CTL_IO_ERROR; break; }
        if (nf_dirent_parse((const unsigned char *)b->dirbuf, n, list_rec, &s) < 0) { b->last_err = -EIO; r = CTL_IO_ERROR; break; }
    }
    sc_close(b, dfd);
    return r;
}

/* Read regular, non-symlink file `name` in directory dfd. TOO_LARGE when it holds more than cap bytes. */
static int read_at(nf_backend *b, long dfd, const char *name, char *buf, unsigned cap, unsigned *len) {
    struct stat64 pre, st;
    unsigned n = 0;
    int r = CTL_IO_OK, intr = 0;
    long fd, e;
    *len = 0;
    if (!name_ok(name)) return CTL_IO_ERROR;
    /* lstat first: symlinks, FIFOs and devices are refused before any open side effect. */
    e = sc(b, __NR_fstatat64, dfd, (long)name, (long)&pre, AT_SYMLINK_NOFOLLOW);
    if (e) { b->last_err = e; return e == -ENOENT || e == -ENOTDIR ? CTL_IO_MISSING : CTL_IO_ERROR; }
    if (!S_ISREG(pre.st_mode)) return CTL_IO_NOT_REGULAR;
    if (pre.st_size > (long long)cap) return CTL_IO_TOO_LARGE;
    fd = sc_retry(b, __NR_openat, dfd, (long)name, NF_OPEN_READ, 0);
    if (fd < 0) {
        b->last_err = fd;
        if (fd == -ENOENT || fd == -ENOTDIR) return CTL_IO_MISSING;
        return fd == -ELOOP ? CTL_IO_NOT_REGULAR : CTL_IO_ERROR;
    }
    /* The opened object must be the regular file that was checked (no swap between lstat and open). */
    e = sc(b, __NR_fstat64, fd, (long)&st, 0, 0);
    if (e || !S_ISREG(st.st_mode) || st.st_ino != pre.st_ino || st.st_dev != pre.st_dev) {
        sc_close(b, fd);
        return e ? CTL_IO_ERROR : CTL_IO_NOT_REGULAR;
    }
    for (;;) {
        char extra;
        long got = n < cap ? sc(b, __NR_read, fd, (long)(buf + n), (long)(cap - n), 0) : sc(b, __NR_read, fd, (long)&extra, 1, 0);
        if (got == -EINTR && ++intr < NF_EINTR_RETRIES) continue;
        if (got < 0) { b->last_err = got; r = CTL_IO_ERROR; break; }
        if (got == 0) break;
        if (n >= cap) { r = CTL_IO_TOO_LARGE; break; } /* file grew past cap after the size check */
        if ((unsigned long)got > cap - n) { r = CTL_IO_ERROR; break; }
        n += (unsigned)got;
    }
    sc_close(b, fd);
    *len = n;
    return r;
}

static int read_import(void *ctx, const char *name, char *buf, unsigned cap, unsigned *len) {
    nf_backend *b = ctx;
    int r;
    long dfd = walk_dir(b, b->import_dir, 0);
    *len = 0;
    if (dfd < 0) return dfd == -ENOENT ? CTL_IO_MISSING : CTL_IO_ERROR;
    r = read_at(b, dfd, name, buf, cap, len);
    sc_close(b, dfd);
    return r;
}

static int read_active(void *ctx, char *buf, unsigned cap, unsigned *len) {
    nf_backend *b = ctx;
    int r;
    long dfd = walk_dir(b, b->active_dir, 0);
    *len = 0;
    if (dfd < 0) return dfd == -ENOENT ? CTL_IO_MISSING : CTL_IO_ERROR;
    r = read_at(b, dfd, b->active_name, buf, cap, len);
    sc_close(b, dfd);
    return r;
}

static void tmp_discard(void *ctx) {
    nf_backend *b = ctx;
    if (b->tmp_fd >= 0) { sc_close(b, b->tmp_fd); b->tmp_fd = -1; }
    if (b->dir_fd >= 0) {
        (void)sc(b, __NR_unlinkat, b->dir_fd, (long)b->tmp_name, 0, 0); /* discard: remove own temp */
        sc_close(b, b->dir_fd);
        b->dir_fd = -1;
    }
}

static int tmp_open(void *ctx) {
    nf_backend *b = ctx;
    long dfd, fd;
    tmp_discard(b); /* a previous commit that was never discarded */
    dfd = walk_dir(b, b->active_dir, 1);
    if (dfd < 0) return -1;
    fd = sc_retry(b, __NR_openat, dfd, (long)b->tmp_name, NF_OPEN_TMP, 0644);
    if (fd == -EEXIST) {
        /* Stale temp from an interrupted commit. Only this fixed name inside the held directory is removed;
         * unlinkat removes a symlink itself, never its target, and refuses directories. */
        struct stat64 st;
        if (sc(b, __NR_fstatat64, dfd, (long)b->tmp_name, (long)&st, AT_SYMLINK_NOFOLLOW) == 0 && !S_ISDIR(st.st_mode) &&
            sc(b, __NR_unlinkat, dfd, (long)b->tmp_name, 0, 0) == 0)
            fd = sc_retry(b, __NR_openat, dfd, (long)b->tmp_name, NF_OPEN_TMP, 0644);
    }
    if (fd < 0) { b->last_err = fd; sc_close(b, dfd); return -1; }
    b->dir_fd = (int)dfd;
    b->tmp_fd = (int)fd;
    return 0;
}

static int tmp_write(void *ctx, const char *data, unsigned len) {
    nf_backend *b = ctx;
    unsigned done = 0;
    int intr = 0;
    if (b->tmp_fd < 0) return -1;
    while (done < len) {
        long w = sc(b, __NR_write, b->tmp_fd, (long)(data + done), (long)(len - done), 0);
        if (w == -EINTR && ++intr < NF_EINTR_RETRIES) continue;
        if (w <= 0 || (unsigned long)w > len - done) { b->last_err = w < 0 ? w : -EIO; return -1; }
        done += (unsigned)w;
    }
    return 0;
}

/* No user-space buffering: write() already handed every byte to the kernel. */
static int tmp_flush(void *ctx) { return ((nf_backend *)ctx)->tmp_fd >= 0 ? 0 : -1; }

static int tmp_fsync(void *ctx) {
    nf_backend *b = ctx;
    long r;
    if (b->tmp_fd < 0) return -1;
    r = sc_retry(b, __NR_fsync, b->tmp_fd, 0, 0, 0);
    if (r) b->last_err = r;
    return r ? -1 : 0;
}

static int tmp_close(void *ctx) {
    nf_backend *b = ctx;
    long r;
    if (b->tmp_fd < 0) return -1;
    r = sc(b, __NR_close, b->tmp_fd, 0, 0, 0);
    b->tmp_fd = -1; /* Linux releases the fd even when close reports an error */
    if (r) b->last_err = r;
    return r ? -1 : 0;
}

static int tmp_rename(void *ctx) {
    nf_backend *b = ctx;
    long r;
    if (b->dir_fd < 0 || b->tmp_fd >= 0) return -1;
    /* Same directory fd for both names: atomic replace, no path re-resolution. */
    r = sc(b, __NR_renameat, b->dir_fd, (long)b->tmp_name, b->dir_fd, (long)b->active_name);
    if (r) { b->last_err = r; return -1; }
    /* Best effort: make the rename durable. The new file is already in place (same policy as U2 host). */
    (void)sc_retry(b, __NR_fsync, b->dir_fd, 0, 0, 0);
    sc_close(b, b->dir_fd);
    b->dir_fd = -1;
    return 0;
}

static int get_enabled(void *ctx) { nf_backend *b = ctx; return b->owner.get_enabled(b->owner.ctx); }
static void set_enabled(void *ctx, int on) { nf_backend *b = ctx; b->owner.set_enabled(b->owner.ctx, on); }
static void request_reload(void *ctx) { nf_backend *b = ctx; b->owner.request_reload(b->owner.ctx); }
static void navigate_back(void *ctx) { nf_backend *b = ctx; b->owner.navigate_back(b->owner.ctx); }

int nf_init(nf_backend *b, ctl_platform *out, const char *import_dir, const char *active_dir, const nf_owner *owner) {
    if (!b || !out || !owner || !owner->get_enabled || !owner->set_enabled || !owner->request_reload || !owner->navigate_back)
        return -1;
    if (path_copy(b->import_dir, import_dir) || path_copy(b->active_dir, active_dir)) return -1;
    b->owner = *owner; b->active_name = NF_ACTIVE_NAME; b->tmp_name = NF_TMP_NAME;
    b->sys = 0;
    b->sys_arg = 0;
    b->tmp_fd = -1;
    b->dir_fd = -1;
    b->last_err = 0;
    out->ctx = b;
    out->list_dir = list_dir;
    out->read_import = read_import;
    out->read_active = read_active;
    out->tmp_open = tmp_open;
    out->tmp_write = tmp_write;
    out->tmp_flush = tmp_flush;
    out->tmp_fsync = tmp_fsync;
    out->tmp_close = tmp_close;
    out->tmp_rename = tmp_rename;
    out->tmp_discard = tmp_discard;
    out->get_enabled = get_enabled;
    out->set_enabled = set_enabled;
    out->request_reload = request_reload;
    out->navigate_back = navigate_back;
    return 0;
}
