/* net.c — POSIX TCP trampolines for `std/net.yuga` (`yuga_net_*`).
 *
 * No HTTP here. Wasm: connect/read/write are stubs; `fetch_rpc` is JS.
 */
#include "net_rt.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __wasm32__

__attribute__((import_module("zeus"), import_name("fetch_rpc")))
int32_t zeus_js_fetch_rpc(const char *path, int32_t path_len, const char *body, int32_t body_len,
                         char *out, int32_t out_cap);

int64_t yuga_net_tcp_connect(yuga_str host, int64_t port) {
    (void)host;
    (void)port;
    return -1;
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

yuga_str yuga_net_fetch_rpc(yuga_str path, yuga_str body) {
    (void)path;
    (void)body;
    return (yuga_str){ .ptr = "", .len = 0 };
}

#endif
