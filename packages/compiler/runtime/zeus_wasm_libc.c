/* Freestanding libc for Zeus WASM (Canvas2D host, no WASI). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/uio.h>

__attribute__((import_module("zeus"), import_name("write")))
void zeus_js_write(const char *p, int32_t n);

__attribute__((import_module("zeus"), import_name("panic")))
void zeus_js_panic(const char *p, int32_t n);

int errno;
static FILE file_out = {1};
static FILE file_err = {2};
FILE *stdout = &file_out;
FILE *stderr = &file_err;

#define HEAP_SIZE (16 * 1024 * 1024)
#define HDR 8u
#define FREE_BIT ((size_t)1)

_Alignas(8) static unsigned char heap[HEAP_SIZE];
static size_t heap_off = HDR;

static size_t align8(size_t n) { return (n + 7u) & ~7u; }
static size_t payload_sz(size_t bits) { return bits & ~FREE_BIT; }
static int is_free(size_t bits) { return (int)(bits & FREE_BIT); }

static size_t *hdr_at(size_t off) { return (size_t *)(heap + off); }

static void *payload(size_t *h) { return (void *)((unsigned char *)h + HDR); }
static size_t *hdr_of(void *p) { return (size_t *)((unsigned char *)p - HDR); }

void *malloc(size_t n) {
    size_t off, bits, pay, next, leftover;
    size_t *h, *split;
    if (!n) n = 1;
    n = align8(n);

    off = HDR;
    while (off < heap_off) {
        h = hdr_at(off);
        bits = *h;
        pay = payload_sz(bits);
        next = off + HDR + pay;
        if (is_free(bits)) {
            while (next < heap_off && is_free(*hdr_at(next))) {
                pay += HDR + payload_sz(*hdr_at(next));
                next = off + HDR + pay;
            }
            *h = pay | FREE_BIT;
            if (pay >= n) {
                leftover = pay - n;
                if (leftover >= HDR + 8) {
                    *h = n;
                    split = hdr_at(off + HDR + n);
                    *split = (leftover - HDR) | FREE_BIT;
                } else {
                    *h = pay;
                }
                return payload(h);
            }
        }
        off = next;
    }

    if (heap_off + HDR + n > HEAP_SIZE) return NULL;
    h = hdr_at(heap_off);
    *h = n;
    heap_off += HDR + n;
    return payload(h);
}

void *realloc(void *p, size_t n) {
    void *q;
    size_t old;
    if (!p) return malloc(n);
    old = payload_sz(*hdr_of(p));
    if (n <= old) return p;
    q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, old);
    free(p);
    return q;
}

void *calloc(size_t n, size_t sz) {
    size_t t = n * sz;
    void *q = malloc(t);
    if (q) memset(q, 0, t);
    return q;
}

void free(void *p) {
    size_t *h;
    if (!p) return;
    h = hdr_of(p);
    *h = payload_sz(*h) | FREE_BIT;
}

void abort(void) {
    zeus_js_panic("abort", 5);
    for (;;) {
    }
}

void exit(int c) {
    (void)c;
    abort();
}

char *getenv(const char *name) {
    (void)name;
    return NULL;
}

int atoi(const char *s) {
    int v = 0, sign = 1;
    if (!s) return 0;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v * sign;
}

void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    size_t i;
    for (i = 0; i < n; i++) dd[i] = ss[i];
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    size_t i;
    if (dd < ss) {
        for (i = 0; i < n; i++) dd[i] = ss[i];
    } else {
        for (i = n; i > 0; i--) dd[i - 1] = ss[i - 1];
    }
    return d;
}

void *memset(void *d, int c, size_t n) {
    unsigned char *dd = (unsigned char *)d;
    size_t i;
    for (i = 0; i < n; i++) dd[i] = (unsigned char)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *aa = (const unsigned char *)a;
    const unsigned char *bb = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; i++) {
        if (aa[i] != bb[i]) return (int)aa[i] - (int)bb[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    size_t i;
    if (!a) a = "";
    if (!b) b = "";
    for (i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

char *strcpy(char *d, const char *s) {
    char *o = d;
    while ((*d++ = *s++)) {
    }
    return o;
}

char *strncpy(char *d, const char *s, size_t n) {
    size_t i;
    for (i = 0; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strchr(const char *s, int c) {
    unsigned char ch = (unsigned char)c;
    if (!s) return NULL;
    for (; *s; s++)
        if ((unsigned char)*s == ch) return (char *)s;
    if (ch == 0) return (char *)s;
    return NULL;
}

char *strrchr(const char *s, int c) {
    unsigned char ch = (unsigned char)c;
    const char *last = NULL;
    if (!s) return NULL;
    for (; *s; s++)
        if ((unsigned char)*s == ch) last = s;
    if (ch == 0) return (char *)s;
    return (char *)last;
}

ssize_t write(int fd, const void *p, size_t n) {
    (void)fd;
    if (p && n) zeus_js_write((const char *)p, (int32_t)n);
    return (ssize_t)n;
}

ssize_t writev(int fd, const struct iovec *iov, int n) {
    int i;
    ssize_t t = 0;
    for (i = 0; i < n; i++) {
        write(fd, iov[i].iov_base, iov[i].iov_len);
        t += (ssize_t)iov[i].iov_len;
    }
    return t;
}

static void js_print(const char *s) {
    if (s) zeus_js_write(s, (int32_t)strlen(s));
}

int fflush(FILE *f) {
    (void)f;
    return 0;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    char buf[256];
    (void)f;
    if (fmt && fmt[0] == '%' && fmt[1] == 's' && fmt[2] == ':' && fmt[3] == '%') {
        const char *file;
        int line;
        const char *msg;
        va_start(ap, fmt);
        file = va_arg(ap, const char *);
        line = va_arg(ap, int);
        msg = va_arg(ap, const char *);
        va_end(ap);
        snprintf(buf, sizeof buf, "%s:%d: panic: %s\n", file ? file : "?", line,
                 msg ? msg : "");
        js_print(buf);
        return 0;
    }
    va_start(ap, fmt);
    (void)ap;
    va_end(ap);
    js_print(fmt);
    return 0;
}

int printf(const char *fmt, ...) {
    js_print(fmt);
    return 0;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    size_t o = 0;
    const char *p;
    if (!buf || !n) return 0;
    va_start(ap, fmt);
    for (p = fmt; p && *p && o + 1 < n; p++) {
        if (*p != '%') {
            buf[o++] = *p;
            continue;
        }
        p++;
        if (*p == '%') {
            buf[o++] = '%';
            continue;
        }
        if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "";
            while (*s && o + 1 < n) buf[o++] = *s++;
        } else if (*p == 'd' || *p == 'i') {
            int v = va_arg(ap, int);
            char tmp[16];
            int ti = 0, neg = 0;
            unsigned u;
            if (v < 0) {
                neg = 1;
                u = (unsigned)(-(v + 1)) + 1;
            } else
                u = (unsigned)v;
            if (!u) tmp[ti++] = '0';
            while (u && ti < 15) {
                tmp[ti++] = (char)('0' + (u % 10));
                u /= 10;
            }
            if (neg && o + 1 < n) buf[o++] = '-';
            while (ti && o + 1 < n) buf[o++] = tmp[--ti];
        } else if (*p == 'l') {
            p++;
            if (*p == 'l') p++;
            if (*p == 'd' || *p == 'i' || *p == 'u') {
                long long v = va_arg(ap, long long);
                char tmp[24];
                int ti = 0, neg = 0;
                unsigned long long u;
                if (v < 0) {
                    neg = 1;
                    u = (unsigned long long)(-(v + 1)) + 1;
                } else
                    u = (unsigned long long)v;
                if (!u) tmp[ti++] = '0';
                while (u && ti < 23) {
                    tmp[ti++] = (char)('0' + (u % 10));
                    u /= 10;
                }
                if (neg && o + 1 < n) buf[o++] = '-';
                while (ti && o + 1 < n) buf[o++] = tmp[--ti];
            }
        } else if (*p == '.' && p[1] == '*' && p[2] == 's') {
            int ln = va_arg(ap, int);
            const char *s = va_arg(ap, const char *);
            int i;
            p += 2;
            if (!s) s = "";
            for (i = 0; i < ln && s[i] && o + 1 < n; i++) buf[o++] = s[i];
        }
    }
    va_end(ap);
    buf[o] = 0;
    return (int)o;
}

/* compiler-rt i128 helpers. wasm32 clang emits these as `env.*` imports
   unless we define them. Do not multiply `__int128` here — that recurses. */

static void mulu64(uint64_t a, uint64_t b, uint64_t *lo, uint64_t *hi) {
    uint64_t a0 = (uint32_t)a, a1 = a >> 32;
    uint64_t b0 = (uint32_t)b, b1 = b >> 32;
    uint64_t p0 = a0 * b0;
    uint64_t p1 = a0 * b1;
    uint64_t p2 = a1 * b0;
    uint64_t p3 = a1 * b1;
    uint64_t mid = (p0 >> 32) + (uint32_t)p1 + (uint32_t)p2;
    *lo = (p0 & 0xffffffffu) | (mid << 32);
    *hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
}

void __multi3(uint64_t out[2], uint64_t a_lo, uint64_t a_hi, uint64_t b_lo, uint64_t b_hi) {
    uint64_t lo, hi, t, t_hi;
    mulu64(a_lo, b_lo, &lo, &hi);
    mulu64(a_lo, b_hi, &t, &t_hi);
    hi += t;
    mulu64(a_hi, b_lo, &t, &t_hi);
    hi += t;
    out[0] = lo;
    out[1] = hi;
}
