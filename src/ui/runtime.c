/* Freestanding C runtime for the U5 MIPS payload.
 *
 * The payload is not linked against a C library. Every libc/libm symbol referenced by the vendored
 * components (P1 peq.c, U2 controller.c/view.c, U3 peq_awtk_view.c, U4 native_files.c) is resolved
 * here, in one of two ways:
 *   - mem/str/fabs/isfinite/strtod: implemented in this file (trivial, deterministic, host-tested).
 *   - snprintf/cos/sin/pow/sqrt: forwarded to the demo's OWN glibc/libm through its .plt stubs, via an
 *     indirect call so gcc sets t9 (the stub and the resolver are position-dependent; this preserves
 *     lazy resolution). The demo does not import strtod, so strtod is implemented here and proven
 *     equal to libc strtod over the P1 corpus (tests/test_strtod.c).
 *
 * Build it with -DU5_HOST to expose only the host-testable helpers (u5_strtod_impl, u5_memops) and
 * NOT the public libc names, so the host validator can link against the real libc. */
#include <stddef.h>
#include <stdint.h>
#ifdef U5_HOST
#include <stdlib.h>          /* host atof for the -DU5_HOST adapter build */
#else
#include "thunks.h"          /* t9-setting thunks to the demo's atof/libm/vsnprintf .plt stubs */
#endif

#ifndef U5_HOST
int strncmp(const char *a,const char *b,size_t n) {
    for (size_t i=0;i<n;++i) {
        unsigned char x=(unsigned char)a[i], y=(unsigned char)b[i];
        if (x!=y) return (int)x-(int)y;
        if (!x) break;
    }
    return 0;
}
#endif

/* -------------------------------------------------- P1 numeric adapter (strtod seam) -- */
/* This is NOT a general-purpose strtod. P1's number_token has ALREADY validated `s` as a clean decimal
 * token (optional sign, decimal digits, at most one '.', optional [eE][+-]digits; no hex/NaN/inf/garbage)
 * and then requires *endptr == s + token_length and isfinite(value). We delegate the numeric conversion
 * to libc atof -- identical to libc strtod's value for such a token, and the very conversion the player's
 * own code would use -- and set endptr by a lexical scan of the same grammar. atof returns +/-HUGE_VAL on
 * overflow, which P1 then rejects via PEQ_ISFINITE; underflow yields a finite tiny value, as with strtod. */
double u5_peq_strtod(const char *s, char **endptr) {
    double v =
#ifdef U5_HOST
        atof(s);
#else
        s_atof(s);
#endif
    if (endptr) {
        const char *p = s;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f') p++;
        if (*p == '+' || *p == '-') p++;
        while (*p >= '0' && *p <= '9') p++;
        if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
        if (*p == 'e' || *p == 'E') {
            const char *q = p + 1;
            if (*q == '+' || *q == '-') q++;
            if (*q >= '0' && *q <= '9') { while (*q >= '0' && *q <= '9') q++; p = q; }
        }
        *endptr = (char *)p;
    }
    return v;
}

#ifndef U5_HOST
/* --------------------------------------------------------- public libc names -- */
void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *dd = d; const unsigned char *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    unsigned char *dd = d; const unsigned char *ss = s;
    if (dd < ss) while (n--) *dd++ = *ss++;
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}
void *memset(void *d, int c, size_t n) {
    unsigned char *dd = d;
    while (n--) *dd++ = (unsigned char)c;
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return (int)*x - (int)*y;
    return 0;
}
void *memchr(const void *s, int c, size_t n) {
    const unsigned char *x = s;
    for (; n; n--, x++) if (*x == (unsigned char)c) return (void *)x;
    return NULL;
}
size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
char *strstr(const char *h, const char *n) {
    if (!*n) return (char *)h;
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char *)h;
    }
    return NULL;
}

/* fabs/isfinite without libm. */
double fabs(double x) {
    union { double d; uint64_t u; } v; v.d = x; v.u &= 0x7fffffffffffffffULL; return v.d;
}
int isfinite(double x) { return x - x == 0.0; } /* inf-inf=nan!=0, nan!=0, finite-finite=0 */

/* ----------------------------------------- forwarders to the demo's glibc/libm -- */
#include <stdarg.h>

int snprintf(char *str, size_t size, const char *fmt, ...) {
    /* glibc vsnprintf through the demo .plt via the t9-setting thunk. */
    va_list ap; va_start(ap, fmt);
    int r = s_vsnprintf(str, size, fmt, (void *)ap);
    va_end(ap);
    return r;
}
double cos(double x)  { return s_cos(x); }
double sin(double x)  { return s_sin(x); }
double sqrt(double x) { return s_sqrt(x); }
double pow(double a, double b) { return s_pow(a, b); }
#endif /* !U5_HOST */
