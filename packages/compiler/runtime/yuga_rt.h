/* yuga_rt.h — panics, overflow, bounds. Included at the top of generated C. */
#ifndef YUGA_RT_H
#define YUGA_RT_H

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/uio.h>

typedef struct {
    const char *ptr;
    int64_t len;
} yuga_str;

typedef struct {
    void *fn;
    void *env;
    /* Byte size of `env` when it is a closure heap/stack record. intern_fn
       memcpy's this so the handler outlives the value that was interned. */
    size_t env_size;
} yuga_fn;

typedef struct {
    void *ptr;
    int64_t len;
    int64_t cap;
} yuga_vec;

/* Length-based stdout. No malloc, no printf, no NUL requirement. */
static inline void yuga_write_bytes(const char *p, size_t n) {
    while (n) {
        ssize_t w = write(STDOUT_FILENO, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (w == 0) return;
        p += (size_t)w;
        n -= (size_t)w;
    }
}

static inline void yuga_writev_all(struct iovec *iov, int n) {
    while (n > 0) {
        if (iov->iov_len == 0) {
            iov++;
            n--;
            continue;
        }
        ssize_t w = writev(STDOUT_FILENO, iov, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (w == 0) return;
        size_t left = (size_t)w;
        while (n > 0 && left >= iov->iov_len) {
            left -= iov->iov_len;
            iov++;
            n--;
        }
        if (n > 0 && left) {
            iov->iov_base = (char *)iov->iov_base + left;
            iov->iov_len -= left;
        }
    }
}

static inline void yuga_fmt_write(yuga_str s) {
    if (!s.ptr || s.len <= 0) return;
    yuga_write_bytes(s.ptr, (size_t)s.len);
}

static inline yuga_str yuga_fmt_itoa(char buf[24], int64_t v) {
    char *end = buf + 24;
    char *p = end;
    uint64_t u;
    if (v < 0)
        u = (uint64_t)(-(v + 1)) + 1;
    else
        u = (uint64_t)v;
    if (u == 0) {
        *--p = '0';
    } else {
        while (u) {
            *--p = (char)('0' + (u % 10));
            u /= 10;
        }
        if (v < 0) *--p = '-';
    }
    yuga_str s;
    s.ptr = p;
    s.len = (int64_t)(end - p);
    return s;
}

static inline void yuga_fmt_write_int(int64_t v) {
    char buf[24];
    yuga_str s = yuga_fmt_itoa(buf, v);
    yuga_write_bytes(s.ptr, (size_t)s.len);
}

static inline void yuga_fmt_write_bool(bool v) {
    if (v) yuga_write_bytes("true", 4);
    else yuga_write_bytes("false", 5);
}

static inline yuga_str yuga_fmt_ftoa(char buf[64], double v) {
    int n = snprintf(buf, 64, "%.15g", v);
    if (n < 0) n = 0;
    if (n > 63) n = 63;
    yuga_str s;
    s.ptr = buf;
    s.len = (int64_t)n;
    return s;
}

static inline void yuga_fmt_write_float(double v) {
    char buf[64];
    yuga_str s = yuga_fmt_ftoa(buf, v);
    yuga_write_bytes(s.ptr, (size_t)s.len);
}

static inline void yuga_fmt_writeln(void) { yuga_write_bytes("\n", 1); }

static inline bool yuga_fmt_eq(yuga_str a, yuga_str b) {
    if (a.len != b.len) return false;
    if (a.len <= 0) return true;
    if (!a.ptr || !b.ptr) return a.ptr == b.ptr;
    return memcmp(a.ptr, b.ptr, (size_t)a.len) == 0;
}

static inline void yuga_panic(const char *file, int line, const char *msg) {
    fflush(stdout);
    fprintf(stderr, "%s:%d: panic: %s\n", file, line, msg);
    abort();
}

static inline int64_t yuga_add_i64(int64_t a, int64_t b, const char *f, int l) {
    int64_t r;
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_add_overflow(a, b, &r)) yuga_panic(f, l, "integer overflow");
    return r;
#else
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        yuga_panic(f, l, "integer overflow");
    return a + b;
#endif
}

static inline int64_t yuga_sub_i64(int64_t a, int64_t b, const char *f, int l) {
    int64_t r;
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_sub_overflow(a, b, &r)) yuga_panic(f, l, "integer overflow");
    return r;
#else
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
        yuga_panic(f, l, "integer overflow");
    return a - b;
#endif
}

static inline int64_t yuga_mul_i64(int64_t a, int64_t b, const char *f, int l) {
    int64_t r;
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_mul_overflow(a, b, &r)) yuga_panic(f, l, "integer overflow");
    return r;
#else
    if (a != 0 && b != 0) {
        if (a > 0 && b > 0 && a > INT64_MAX / b) yuga_panic(f, l, "integer overflow");
        if (a > 0 && b < 0 && b < INT64_MIN / a) yuga_panic(f, l, "integer overflow");
        if (a < 0 && b > 0 && a < INT64_MIN / b) yuga_panic(f, l, "integer overflow");
        if (a < 0 && b < 0 && a < INT64_MAX / b) yuga_panic(f, l, "integer overflow");
    }
    return a * b;
#endif
}

static inline int64_t yuga_div_i64(int64_t a, int64_t b, const char *f, int l) {
    if (b == 0) yuga_panic(f, l, "division by zero");
    if (a == INT64_MIN && b == -1) yuga_panic(f, l, "integer overflow");
    return a / b;
}

static inline int64_t yuga_mod_i64(int64_t a, int64_t b, const char *f, int l) {
    if (b == 0) yuga_panic(f, l, "division by zero");
    if (a == INT64_MIN && b == -1) return 0;
    return a % b;
}

static inline int64_t yuga_wrapping_add(int64_t a, int64_t b) {
    return (int64_t)((uint64_t)a + (uint64_t)b);
}

static inline int64_t yuga_wrapping_shr(int64_t a, int64_t n) {
    if (n < 0 || n > 63) return 0;
    return (int64_t)((uint64_t)a >> (unsigned)n);
}

static inline int64_t yuga_wrapping_shl(int64_t a, int64_t n) {
    if (n < 0 || n > 63) return 0;
    return (int64_t)((uint64_t)a << (unsigned)n);
}

static inline int64_t yuga_wrapping_or(int64_t a, int64_t b) {
    return (int64_t)((uint64_t)a | (uint64_t)b);
}

static inline int64_t yuga_wrapping_and(int64_t a, int64_t b) {
    return (int64_t)((uint64_t)a & (uint64_t)b);
}

static inline int64_t yuga_saturating_add(int64_t a, int64_t b) {
    int64_t r;
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_add_overflow(a, b, &r)) return b > 0 ? INT64_MAX : INT64_MIN;
    return r;
#else
    if (b > 0 && a > INT64_MAX - b) return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b) return INT64_MIN;
    return a + b;
#endif
}

static inline int64_t yuga_idx(int64_t i, int64_t n, const char *f, int l) {
    if (i < 0 || i >= n) yuga_panic(f, l, "index out of bounds");
    return i;
}

static inline void *yuga_new(size_t sz, const char *f, int l) {
    void *p = malloc(sz ? sz : 1);
    if (!p) yuga_panic(f, l, "out of memory");
    return p;
}

static inline void yuga_drop(void **p) {
    if (p && *p) {
        free(*p);
        *p = NULL;
    }
}

static inline void *yuga_move_ptr(void **src) {
    void *p = src ? *src : NULL;
    if (src) *src = NULL;
    return p;
}

static inline yuga_vec yuga_vec_new(void) {
    yuga_vec v;
    v.ptr = NULL;
    v.len = 0;
    v.cap = 0;
    return v;
}

static inline yuga_vec yuga_vec_move(yuga_vec *src) {
    yuga_vec v;
    v.ptr = NULL;
    v.len = 0;
    v.cap = 0;
    if (src) {
        v = *src;
        src->ptr = NULL;
        src->len = 0;
        src->cap = 0;
    }
    return v;
}

static inline void yuga_vec_reserve(yuga_vec *v, int64_t n, size_t esz, const char *f, int l) {
    if (!v || n <= v->cap) return;
    int64_t cap = v->cap ? v->cap : 8;
    while (cap < n) {
        if (cap > INT64_MAX / 2) yuga_panic(f, l, "out of memory");
        cap *= 2;
    }
    void *p = realloc(v->ptr, (esz ? (size_t)cap * esz : 1));
    if (!p) yuga_panic(f, l, "out of memory");
    v->ptr = p;
    v->cap = cap;
}

static inline void yuga_vec_push(yuga_vec *v, const void *elem, size_t esz, const char *f, int l) {
    if (!v) yuga_panic(f, l, "push on null array");
    yuga_vec_reserve(v, v->len + 1, esz, f, l);
    if (esz && elem) memcpy((char *)v->ptr + (size_t)v->len * esz, elem, esz);
    v->len++;
}

static inline void yuga_vec_pop(yuga_vec *v, void *out, size_t esz, const char *f, int l) {
    if (!v || v->len <= 0) yuga_panic(f, l, "pop from empty array");
    v->len--;
    if (esz && out) memcpy(out, (char *)v->ptr + (size_t)v->len * esz, esz);
}

static inline void yuga_vec_drop(yuga_vec *v) {
    if (!v) return;
    free(v->ptr);
    v->ptr = NULL;
    v->len = 0;
    v->cap = 0;
}

static inline yuga_fn yuga_fn_move(yuga_fn *src) {
    yuga_fn v;
    v.fn = NULL;
    v.env = NULL;
    v.env_size = 0;
    if (src) {
        v = *src;
        src->fn = NULL;
        src->env = NULL;
        src->env_size = 0;
    }
    return v;
}

static inline void yuga_fn_drop(yuga_fn *f) {
    if (!f) return;
    if (f->env) free(f->env);
    f->env = NULL;
    f->fn = NULL;
}

/* Takes ownership of `b` (IR_CALL steals []int). Trailing NUL for C hosts. */
static inline yuga_str yuga_string_from_bytes(yuga_vec b) {
    int64_t n = b.len > 0 ? b.len : 0;
    char *p = (char *)yuga_new((size_t)n + 1, "string_from_bytes", 0);
    int64_t *el = (int64_t *)b.ptr;
    int64_t i;
    for (i = 0; i < n; i++)
        p[i] = (char)(el ? (el[i] & 255) : 0);
    p[n] = 0;
    yuga_vec_drop(&b);
    return (yuga_str){ .ptr = p, .len = n };
}

#ifdef __wasm32__
static inline int64_t yuga_sys_env_set(yuga_str name) {
    (void)name;
    return 0;
}
static inline void yuga_sys_exit(int64_t code) {
    (void)code;
    abort();
}
static inline yuga_str yuga_sys_env(yuga_str name) {
    (void)name;
    return (yuga_str){"", 0};
}
static inline int64_t yuga_sys_write_file(yuga_str path, yuga_str body) {
    (void)path;
    (void)body;
    return 1;
}
static inline yuga_str yuga_sys_exec(yuga_str cmd) {
    (void)cmd;
    return (yuga_str){"", 0};
}
static inline int64_t yuga_sys_exec_status(void) { return 1; }
static inline yuga_str yuga_sys_read_file(yuga_str path) {
    (void)path;
    return (yuga_str){"", 0};
}
#else
#include <sys/wait.h>

static int yuga_sys_last_status = 1;

static inline int64_t yuga_sys_env_set(yuga_str name) {
    char buf[256];
    const char *v;
    if (!name.ptr || name.len <= 0 || name.len >= 256) return 0;
    memcpy(buf, name.ptr, (size_t)name.len);
    buf[name.len] = 0;
    v = getenv(buf);
    if (!v || !v[0]) return 0;
    return 1;
}
static inline void yuga_sys_exit(int64_t code) { exit((int)code); }

static inline yuga_str yuga_sys_env(yuga_str name) {
    char buf[256];
    const char *v;
    size_t n;
    char *p;
    if (!name.ptr || name.len <= 0 || name.len >= 256) return (yuga_str){"", 0};
    memcpy(buf, name.ptr, (size_t)name.len);
    buf[name.len] = 0;
    v = getenv(buf);
    if (!v) return (yuga_str){"", 0};
    n = strlen(v);
    p = (char *)yuga_new(n + 1, "sys_env", 0);
    memcpy(p, v, n + 1);
    return (yuga_str){p, (int64_t)n};
}

static inline int64_t yuga_sys_write_file(yuga_str path, yuga_str body) {
    char pbuf[4096];
    FILE *f;
    if (!path.ptr || path.len <= 0 || path.len >= 4095) return 1;
    memcpy(pbuf, path.ptr, (size_t)path.len);
    pbuf[path.len] = 0;
    f = fopen(pbuf, "wb");
    if (!f) return 1;
    if (body.ptr && body.len > 0) {
        if (fwrite(body.ptr, 1, (size_t)body.len, f) != (size_t)body.len) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static inline yuga_str yuga_sys_exec(yuga_str cmd) {
    char cbuf[8192];
    FILE *p;
    size_t cap = 4096;
    size_t n = 0;
    char *out;
    int st;
    const size_t max_out = 262144;
    if (!cmd.ptr || cmd.len <= 0 || cmd.len >= 8191) {
        yuga_sys_last_status = 1;
        return (yuga_str){"", 0};
    }
    memcpy(cbuf, cmd.ptr, (size_t)cmd.len);
    cbuf[cmd.len] = 0;
    p = popen(cbuf, "r");
    if (!p) {
        yuga_sys_last_status = 1;
        return (yuga_str){"", 0};
    }
    out = (char *)yuga_new(cap, "sys_exec", 0);
    for (;;) {
        size_t r;
        if (n + 512 >= cap) {
            size_t ncap = cap * 2;
            char *nbuf;
            if (ncap > max_out + 512) ncap = max_out + 512;
            nbuf = (char *)yuga_new(ncap, "sys_exec", 0);
            memcpy(nbuf, out, n);
            free(out);
            out = nbuf;
            cap = ncap;
        }
        if (n >= max_out) break;
        r = fread(out + n, 1, 512, p);
        n += r;
        if (r < 512) break;
    }
    st = pclose(p);
    if (st == -1) yuga_sys_last_status = 1;
    else if (WIFEXITED(st)) yuga_sys_last_status = WEXITSTATUS(st);
    else yuga_sys_last_status = 1;
    out[n] = 0;
    return (yuga_str){out, (int64_t)n};
}

static inline int64_t yuga_sys_exec_status(void) { return yuga_sys_last_status; }

static inline yuga_str yuga_sys_read_file(yuga_str path) {
    char pbuf[4096];
    FILE *f;
    long sz;
    char *p;
    size_t n;
    const size_t max_n = 16u * 1024u * 1024u;
    if (!path.ptr || path.len <= 0 || path.len >= 4095) return (yuga_str){"", 0};
    memcpy(pbuf, path.ptr, (size_t)path.len);
    pbuf[path.len] = 0;
    f = fopen(pbuf, "rb");
    if (!f) return (yuga_str){"", 0};
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return (yuga_str){"", 0};
    }
    sz = ftell(f);
    if (sz < 0 || (size_t)sz > max_n) {
        fclose(f);
        return (yuga_str){"", 0};
    }
    rewind(f);
    p = (char *)yuga_new((size_t)sz + 1, "sys_read_file", 0);
    n = fread(p, 1, (size_t)sz, f);
    fclose(f);
    p[n] = 0;
    return (yuga_str){p, (int64_t)n};
}
#endif

#endif /* YUGA_RT_H */
