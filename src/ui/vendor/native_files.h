/* Native Linux/MIPS O32 (little-endian, nan2008) file backend for the U2 ctl_platform.
 * Freestanding: raw O32 syscalls only, no libc, no external symbols. Constants and the stat64 layout
 * come from the MIPS Linux UAPI headers (linux-libc-dev-mipsel-cross 6.8.0-25.25cross2).
 * Paths are supplied by the caller; NF_TARGET_* are the intended device paths, never used by tests.
 * Single-threaded, no allocation; call from the UI thread only. */
#ifndef NATIVE_FILES_H
#define NATIVE_FILES_H

#include "controller.h"

#ifndef NF_TARGET_IMPORT_DIR
#define NF_TARGET_IMPORT_DIR "/mnt/mmc/EQ"
#endif
#ifndef NF_TARGET_ACTIVE_DIR
#define NF_TARGET_ACTIVE_DIR "/mnt/data/peq"
#endif
#define NF_ACTIVE_NAME "active.txt"
#define NF_TMP_NAME "active.txt.tmp"   /* same directory as active.txt so renameat is atomic */

#define NF_PATH_MAX 256        /* bytes including NUL; absolute, no "." / ".." / empty components */
#define NF_NAME_MAX 255        /* Linux NAME_MAX */
#define NF_MAX_DIRENTS 4096    /* directory records scanned per list_dir; more is reported as CTL_IO_ERROR */
#define NF_DIRBUF 4096         /* getdents64 buffer */
#define NF_EINTR_RETRIES 16    /* EINTR retries per operation, then the operation fails */

/* Syscall seam: returns result or -errno. NULL selects nf_raw_syscall. Tests use it to inject faults. */
typedef long (*nf_sys_fn)(void *arg, long nr, long a0, long a1, long a2, long a3);

/* On/off and navigation are owned by the UI/player integration and supplied as function pointers. */
typedef struct {
    void *ctx;
    int (*get_enabled)(void *ctx);
    void (*set_enabled)(void *ctx, int on);
    void (*request_reload)(void *ctx);
    void (*navigate_back)(void *ctx);
} nf_owner;

typedef struct {
    char import_dir[NF_PATH_MAX];
    char active_dir[NF_PATH_MAX];
    nf_owner owner;
    const char *active_name, *tmp_name; /* fixed trusted names, separate layer-state transaction */
    nf_sys_fn sys;
    void *sys_arg;
    int tmp_fd;      /* open temp file, -1 none */
    int dir_fd;      /* active directory held from tmp_open until rename/discard, -1 none */
    long last_err;   /* last failing syscall result (-errno), diagnostics only */
    unsigned long long dirbuf[NF_DIRBUF / 8];
} nf_backend;

/* Returns 0, or -1 for an unsafe/oversized path or a missing owner callback. Fills `out` bound to `b`. */
int nf_init(nf_backend *b, ctl_platform *out, const char *import_dir, const char *active_dir, const nf_owner *owner);

/* Raw O32 syscall: result or -errno. */
long nf_raw_syscall(long nr, long a0, long a1, long a2, long a3);

/* Walk getdents64 records in buf[0..len): calls rec(arg, name, d_type) for each; rec returns nonzero to stop.
 * Returns the record count, or -1 for a malformed record (bad reclen, truncated header, unterminated
 * or empty name) or when rec stops. */
int nf_dirent_parse(const unsigned char *buf, long len, int (*rec)(void *arg, const char *name, unsigned type), void *arg);

#endif
