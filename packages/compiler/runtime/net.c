/* net.c — POSIX TCP trampolines for `std/net.yuga` (`yuga_net_*`).
 *
 * Blocking ops (connect/read/write/…), non-blocking ops for the async
 * transport (`tcp_nb_connect` / `tcp_poll` / `tcp_send` / `tcp_so_error`),
 * and — wasm only — async fetch slots (`fetch_issue` / `fetch_ready` /
 * `fetch_take`) whose XHR completes in JS and lands per frame.
 */
#include "net_rt.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __wasm32__

__attribute__((import_module("zeus"), import_name("fetch_rpc")))
int32_t zeus_js_fetch_rpc(const char *path, int32_t path_len, const char *body, int32_t body_len,
                         char *out, int32_t out_cap);

/* Async fetch: JS starts an XHR and later calls the `zeus_fetch_done` export
   below with this handle. `out`/`out_cap` is the slot buffer JS writes into. */
__attribute__((import_module("zeus"), import_name("fetch_rpc_async")))
int32_t zeus_js_fetch_rpc_async(const char *path, int32_t path_len, const char *body,
                               int32_t body_len, char *out, int32_t out_cap, int32_t handle);

#define ASYNC_FETCH_SLOTS 4
#define ASYNC_FETCH_CAP 65536

typedef struct {
    int used;
    int32_t handle;
    char *buf;
    int32_t len;
} AsyncFetch;

static AsyncFetch g_fetches[ASYNC_FETCH_SLOTS];
static int32_t g_fetch_handle;

__attribute__((export_name("zeus_fetch_done")))
void zeus_fetch_done(int32_t handle, int32_t n) {
    int i;
    for (i = 0; i < ASYNC_FETCH_SLOTS; i++) {
        if (g_fetches[i].used && g_fetches[i].handle == handle) {
            g_fetches[i].len = n;
            return;
        }
    }
}

int64_t yuga_net_fetch_issue(yuga_str path, yuga_str body) {
    int i;
    if (path.len <= 0 || !path.ptr) return -1;
    for (i = 0; i < ASYNC_FETCH_SLOTS; i++) {
        if (g_fetches[i].used) continue;
        g_fetches[i].buf = (char *)malloc(ASYNC_FETCH_CAP);
        if (!g_fetches[i].buf) return -1;
        g_fetches[i].used = 1;
        g_fetches[i].len = -1;
        g_fetches[i].handle = ++g_fetch_handle;
        if (zeus_js_fetch_rpc_async(path.ptr, (int32_t)path.len,
                                    body.ptr ? body.ptr : "",
                                    (int32_t)(body.len > 0 ? body.len : 0),
                                    g_fetches[i].buf, ASYNC_FETCH_CAP - 1,
                                    g_fetches[i].handle) < 0) {
            free(g_fetches[i].buf);
            g_fetches[i].buf = NULL;
            g_fetches[i].used = 0;
            return -1;
        }
        return (int64_t)i;
    }
    return -1;
}

int64_t yuga_net_fetch_ready(void) {
    int i, n = 0;
    for (i = 0; i < ASYNC_FETCH_SLOTS; i++)
        if (g_fetches[i].used && g_fetches[i].len >= 0) n++;
    return (int64_t)n;
}

/* Hands one completed response to Yuga (ownership of the slot buffer moves;
   Yuga frees it when the string drops). Errors come back empty. */
yuga_str yuga_net_fetch_take(void) {
    int i;
    for (i = 0; i < ASYNC_FETCH_SLOTS; i++) {
        if (!g_fetches[i].used || g_fetches[i].len < 0) continue;
        g_fetches[i].used = 0;
        if (g_fetches[i].len > 0) {
            g_fetches[i].buf[g_fetches[i].len] = 0;
            return (yuga_str){ .ptr = g_fetches[i].buf, .len = g_fetches[i].len };
        }
        free(g_fetches[i].buf);
        g_fetches[i].buf = NULL;
        return (yuga_str){ .ptr = "", .len = 0 };
    }
    return (yuga_str){ .ptr = "", .len = 0 };
}

int64_t yuga_net_tcp_connect(yuga_str host, int64_t port) {
    (void)host;
    (void)port;
    return -1;
}

int64_t yuga_net_tcp_nb_connect(yuga_str host, int64_t port) {
    (void)host;
    (void)port;
    return -1;
}

int64_t yuga_net_tcp_poll(int64_t fd, int64_t want, int64_t ms) {
    (void)fd;
    (void)want;
    (void)ms;
    return -1;
}

int64_t yuga_net_tcp_send(int64_t fd, yuga_str data, int64_t off) {
    (void)fd;
    (void)data;
    (void)off;
    return -1;
}

int64_t yuga_net_tcp_so_error(int64_t fd) {
    (void)fd;
    return 1;
}

int64_t yuga_net_tcp_write(int64_t fd, yuga_str data) {
    (void)fd;
    (void)data;
    return -1;
}

yuga_str yuga_net_tcp_read(int64_t fd, int64_t max) {
    (void)fd;
    (void)max;
    return (yuga_str){ .ptr = "", .len = 0 };
}

void yuga_net_tcp_close(int64_t fd) { (void)fd; }

int64_t yuga_net_tcp_listen(int64_t port) {
    (void)port;
    return -1;
}

int64_t yuga_net_tcp_accept(int64_t fd) {
    (void)fd;
    return -1;
}

int64_t yuga_net_tcp_bound_port(int64_t fd) {
    (void)fd;
    return 0;
}

yuga_str yuga_net_tcp_peek(int64_t fd, int64_t max) {
    (void)fd;
    (void)max;
    return (yuga_str){ .ptr = "", .len = 0 };
}

yuga_str yuga_net_fetch_rpc(yuga_str path, yuga_str body) {
    char *buf;
    int32_t n;
    yuga_str out;
    if (path.len <= 0 || !path.ptr) return (yuga_str){ .ptr = "", .len = 0 };
    buf = (char *)malloc(65536);
    if (!buf) return (yuga_str){ .ptr = "", .len = 0 };
    n = zeus_js_fetch_rpc(path.ptr, (int32_t)path.len, body.ptr ? body.ptr : "",
                          (int32_t)(body.len > 0 ? body.len : 0), buf, 65535);
    if (n < 0) n = 0;
    out.ptr = buf;
    out.len = (int64_t)n;
    buf[n] = 0;
    return out;
}

#else

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int64_t yuga_net_tcp_connect(yuga_str host, int64_t port) {
    char name[256];
    int fd;
    struct sockaddr_in sa;
    if (port <= 0 || port > 65535 || host.len <= 0 || host.len >= 256 || !host.ptr)
        return -1;
    memcpy(name, host.ptr, (size_t)host.len);
    name[host.len] = '\0';
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, name, &sa.sin_addr) != 1) {
        if (strcmp(name, "localhost") == 0)
            sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        else {
            close(fd);
            return -1;
        }
    }
    int yes = 1;
#ifdef SO_NOSIGPIPE
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#else
    (void)yes;
#endif
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
        close(fd);
        return -1;
    }
    return (int64_t)fd;
}

int64_t yuga_net_tcp_write(int64_t fd, yuga_str data) {
    size_t len;
    size_t off;
    if (fd < 0) return -1;
    len = data.len > 0 ? (size_t)data.len : 0;
    if (len == 0) return 0;
    if (!data.ptr) return -1;
    off = 0;
    while (off < len) {
        ssize_t n;
#if defined(MSG_NOSIGNAL)
        n = send((int)fd, data.ptr + off, len - off, MSG_NOSIGNAL);
#else
        n = write((int)fd, data.ptr + off, len - off);
#endif
        if (n <= 0) return -1;
        off += (size_t)n;
    }
    return (int64_t)off;
}

yuga_str yuga_net_tcp_read(int64_t fd, int64_t max) {
    char *p;
    ssize_t n;
    if (fd < 0 || max <= 0) return (yuga_str){ .ptr = "", .len = 0 };
    if (max > 65536) max = 65536;
    p = (char *)malloc((size_t)max + 1);
    if (!p) return (yuga_str){ .ptr = "", .len = 0 };
    n = read((int)fd, p, (size_t)max);
    if (n <= 0) {
        free(p);
        return (yuga_str){ .ptr = "", .len = 0 };
    }
    p[n] = 0;
    return (yuga_str){ .ptr = p, .len = n };
}

void yuga_net_tcp_close(int64_t fd) {
    if (fd >= 0) close((int)fd);
}

int64_t yuga_net_tcp_listen(int64_t port) {
    int fd;
    struct sockaddr_in sa;
    int one = 1;
    uint16_t p = 0;
    if (port > 0 && port < 65536) p = (uint16_t)port;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons(p);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 16) != 0) {
        close(fd);
        return -1;
    }
    return (int64_t)fd;
}

int64_t yuga_net_tcp_accept(int64_t fd) {
    int c;
    struct timeval tv;
    if (fd < 0) return -1;
    c = accept((int)fd, NULL, NULL);
    if (c < 0) return -1;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    (void)setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#ifdef SO_NOSIGPIPE
    {
        int yes = 1;
        (void)setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
    }
#endif
    return (int64_t)c;
}

int64_t yuga_net_tcp_bound_port(int64_t fd) {
    struct sockaddr_in sa;
    socklen_t n = sizeof sa;
    if (fd < 0) return 0;
    memset(&sa, 0, sizeof sa);
    if (getsockname((int)fd, (struct sockaddr *)&sa, &n) != 0) return 0;
    return (int64_t)ntohs(sa.sin_port);
}

yuga_str yuga_net_tcp_peek(int64_t fd, int64_t max) {
    char *p;
    ssize_t n;
    if (fd < 0 || max <= 0) return (yuga_str){ .ptr = "", .len = 0 };
    if (max > 65536) max = 65536;
    p = (char *)malloc((size_t)max + 1);
    if (!p) return (yuga_str){ .ptr = "", .len = 0 };
    n = recv((int)fd, p, (size_t)max, MSG_PEEK);
    if (n <= 0) {
        free(p);
        return (yuga_str){ .ptr = "", .len = 0 };
    }
    p[n] = 0;
    return (yuga_str){ .ptr = p, .len = n };
}

/* --- Async transport (UI never blocks): non-blocking connect + poll. --- */

/* Non-blocking connect. Returns a non-blocking fd that is either connected
   already or connecting in the background, or -1 on immediate failure. After
   `tcp_poll(fd, 2, ...)` reports ready, `tcp_so_error(fd)` says whether the
   connect actually succeeded. */
int64_t yuga_net_tcp_nb_connect(yuga_str host, int64_t port) {
    char name[256];
    int fd, rc, flags;
    struct sockaddr_in sa;
    if (port <= 0 || port > 65535 || host.len <= 0 || host.len >= 256 || !host.ptr)
        return -1;
    memcpy(name, host.ptr, (size_t)host.len);
    name[host.len] = '\0';
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        close(fd);
        return -1;
    }
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, name, &sa.sin_addr) != 1) {
        if (strcmp(name, "localhost") == 0)
            sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        else {
            close(fd);
            return -1;
        }
    }
    {
        int yes = 1;
#ifdef SO_NOSIGPIPE
        (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#else
        (void)yes;
#endif
    }
    rc = connect(fd, (struct sockaddr *)&sa, sizeof sa);
    if (rc != 0 && errno != EINPROGRESS && errno != EINTR && errno != EALREADY) {
        close(fd);
        return -1;
    }
    return (int64_t)fd;
}

/* Single-fd poll. `want`: 1 = readable, 2 = writable. `ms`: timeout
   (0 = immediate). Returns 1 when ready (including HUP/ERR so the caller can
   observe EOF or a failed connect), 0 on timeout, -1 on error. */
int64_t yuga_net_tcp_poll(int64_t fd, int64_t want, int64_t ms) {
    struct pollfd p;
    int ev, rc;
    if (fd < 0) return -1;
    if (ms < 0) ms = 0;
    if (ms > 60000) ms = 60000;
    p.fd = (int)fd;
    p.events = (want == 2) ? POLLOUT : POLLIN;
    p.revents = 0;
    for (;;) {
        rc = poll(&p, 1, (int)ms);
        if (rc >= 0) break;
        if (errno != EINTR) return -1;
    }
    if (rc == 0) return 0;
    if (p.revents & (POLLERR | POLLNVAL)) return 1;
    if (p.revents & POLLHUP) return 1;
    if (p.revents & (POLLIN | POLLOUT)) return 1;
    return 0;
}

/* One non-blocking send of `data[off..]`. Returns bytes written (> 0),
   0 when the socket would block (poll for writable, then resume), -1 on
   error. Offsets past the end are an error; an empty remainder writes 0. */
int64_t yuga_net_tcp_send(int64_t fd, yuga_str data, int64_t off) {
    ssize_t n;
    size_t left;
    if (fd < 0) return -1;
    if (off < 0 || !data.ptr || off > data.len) return -1;
    left = (size_t)(data.len - off);
    if (left == 0) return 0;
    for (;;) {
#if defined(MSG_NOSIGNAL)
        n = send((int)fd, data.ptr + off, left, MSG_NOSIGNAL);
#else
        n = send((int)fd, data.ptr + off, left, 0);
#endif
        if (n >= 0) return (int64_t)n;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
}

/* Socket error after a non-blocking connect polled writable: 0 = connected,
   otherwise the errno the connect failed with. */
int64_t yuga_net_tcp_so_error(int64_t fd) {
    int err = 0;
    socklen_t n = sizeof err;
    if (fd < 0) return 1;
    if (getsockopt((int)fd, SOL_SOCKET, SO_ERROR, &err, &n) != 0) return 1;
    return (int64_t)err;
}

/* Async fetch is wasm-only (browser XHR). Native: issue fails, nothing is
   ever ready, take returns empty. */
int64_t yuga_net_fetch_issue(yuga_str path, yuga_str body) {
    (void)path;
    (void)body;
    return -1;
}

int64_t yuga_net_fetch_ready(void) { return 0; }

yuga_str yuga_net_fetch_take(void) {
    return (yuga_str){ .ptr = "", .len = 0 };
}

yuga_str yuga_net_fetch_rpc(yuga_str path, yuga_str body) {
    (void)path;
    (void)body;
    return (yuga_str){ .ptr = "", .len = 0 };
}

#endif
