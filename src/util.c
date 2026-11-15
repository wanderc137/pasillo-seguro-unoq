#define _GNU_SOURCE
#include "pasillo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

double ps_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

long long ps_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

void ps_sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

void ps_log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[pasillo %lld] %s\n", ps_now_ms(), buf);
    fflush(stderr);
}

void ps_json_escape(const char *in, char *out, size_t outlen) {
    size_t j = 0;
    if (outlen == 0) return;
    if (!in) in = "";
    for (size_t i = 0; in[i] && j + 2 < outlen; i++) {
        unsigned char c = (unsigned char)in[i];
        switch (c) {
            case '"': out[j++] = '\\'; out[j++] = '"'; break;
            case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
            case '\n': out[j++] = '\\'; out[j++] = 'n'; break;
            case '\r': out[j++] = '\\'; out[j++] = 'r'; break;
            case '\t': out[j++] = '\\'; out[j++] = 't'; break;
            default:
                if (c < 0x20) {
                    if (j + 6 < outlen) {
                        snprintf(out + j, outlen - j, "\\u%04x", c);
                        j += 6;
                    }
                } else {
                    out[j++] = (char)c;
                }
        }
    }
    out[j] = '\0';
}

const char *ps_risk_name(PsRisk r) {
    switch (r) {
        case PS_RISK_RED: return "red";
        case PS_RISK_AMBER: return "amber";
        default: return "green";
    }
}

static int valid_host(const char *h) {
    if (!h || !*h) return 0;
    size_t n = strlen(h);
    if (n > 200) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)h[i];
        if (!(isalnum(c) || c == '.' || c == '-' || c == '_')) return 0;
    }
    return 1;
}

int ps_normalize_url(const char *in, char *video, size_t vlen,
                     char *photo, size_t plen, char *err, size_t elen) {
    char tmp[PS_MAX_URL];
    char host[256];
    char path[256];
    int port = 8080;
    const char *p;
    size_t n;

    if (err && elen) err[0] = '\0';
    if (!in) {
        snprintf(err, elen, "URL vacia");
        return -1;
    }
    snprintf(tmp, sizeof(tmp), "%s", in);
    /* trim */
    char *s = tmp;
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    if (!*s) {
        snprintf(err, elen, "URL vacia");
        return -1;
    }
    if (strncasecmp(s, "https://", 8) == 0) {
        snprintf(err, elen, "IP Webcam usa http, no https");
        return -1;
    }
    if (strncasecmp(s, "http://", 7) == 0) s += 7;
    if (strstr(s, "://")) {
        snprintf(err, elen, "esquema no soportado; usa http o una IP");
        return -1;
    }

    p = strchr(s, '/');
    if (p) {
        n = (size_t)(p - s);
        if (n >= sizeof(host)) n = sizeof(host) - 1;
        memcpy(host, s, n);
        host[n] = '\0';
        snprintf(path, sizeof(path), "%s", p);
    } else {
        snprintf(host, sizeof(host), "%.255s", s);
        path[0] = '\0';
    }

    char *colon = strrchr(host, ':');
    if (colon) {
        if (strchr(host, ':' ) != colon) {
            snprintf(err, elen, "IPv6 no soportado; usa IPv4");
            return -1;
        }
        *colon = '\0';
        char *endp = NULL;
        long v = strtol(colon + 1, &endp, 10);
        if (!endp || *endp || v < 1 || v > 65535) {
            snprintf(err, elen, "puerto invalido");
            return -1;
        }
        port = (int)v;
    }
    if (!valid_host(host)) {
        snprintf(err, elen, "host invalido");
        return -1;
    }
    /* strip query/fragment from path */
    char *q = strpbrk(path, "?#");
    if (q) *q = '\0';
    if (!path[0] || strcmp(path, "/") == 0) {
        snprintf(path, sizeof(path), "/video");
    } else if (strncmp(path, "/video", 6) == 0) {
        snprintf(path, sizeof(path), "/video");
    } else if (strncmp(path, "/photo", 6) == 0) {
        snprintf(path, sizeof(path), "/video");
    } else {
        snprintf(path, sizeof(path), "/video");
    }

    if (snprintf(video, vlen, "http://%s:%d%s", host, port, path) >= (int)vlen) {
        snprintf(err, elen, "URL demasiado larga");
        return -1;
    }
    if (snprintf(photo, plen, "http://%s:%d/photo.jpg", host, port) >= (int)plen) {
        snprintf(err, elen, "URL demasiado larga");
        return -1;
    }
    return 0;
}

static int parse_http_url(const char *url, char *host, size_t hlen,
                          int *port, char *path, size_t plen) {
    const char *s = url;
    if (strncasecmp(s, "http://", 7) == 0) s += 7;
    else if (strstr(s, "://")) return -1;
    const char *slash = strchr(s, '/');
    const char *host_end = slash ? slash : s + strlen(s);
    const char *colon = NULL;
    for (const char *p = s; p < host_end; p++) {
        if (*p == ':') colon = p;
    }
    size_t n = (size_t)((colon ? colon : host_end) - s);
    if (n == 0 || n >= hlen) return -1;
    memcpy(host, s, n);
    host[n] = '\0';
    *port = 80;
    if (colon) {
        char *endp = NULL;
        long v = strtol(colon + 1, &endp, 10);
        if (!endp || endp != host_end || v < 1 || v > 65535) return -1;
        *port = (int)v;
    }
    snprintf(path, plen, "%s", slash ? slash : "/");
    return 0;
}

int ps_http_get(const char *url, uint8_t **out, size_t *outlen,
                int timeout_ms, char *err, size_t elen) {
    char host[256], path[512];
    int port = 80;
    if (err && elen) err[0] = '\0';
    *out = NULL;
    *outlen = 0;
    if (!url || parse_http_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        snprintf(err, elen, "URL http invalida");
        return -1;
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    int gai = getaddrinfo(host, portstr, &hints, &res);
    if (gai != 0 || !res) {
        snprintf(err, elen, "no se pudo resolver %s: %s", host, gai_strerror(gai));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc == 0) {
            fcntl(fd, F_SETFL, flags);
            break;
        }
        if (errno == EINPROGRESS) {
            struct pollfd pfd = { .fd = fd, .events = POLLOUT };
            int pr = poll(&pfd, 1, timeout_ms);
            if (pr > 0) {
                int soerr = 0;
                socklen_t sl = sizeof(soerr);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
                if (soerr == 0) {
                    fcntl(fd, F_SETFL, flags);
                    break;
                }
            }
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        snprintf(err, elen, "no se pudo conectar a %s:%d", host, port);
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    char req[1024];
    int rl = snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %s:%d\r\nUser-Agent: pasillo-seguro/%s\r\n"
        "Connection: close\r\nAccept: image/jpeg,*/*\r\n\r\n",
        path, host, port, PS_VERSION);
    if (rl <= 0 || send(fd, req, (size_t)rl, 0) != rl) {
        close(fd);
        snprintf(err, elen, "fallo al enviar la peticion");
        return -1;
    }

    size_t cap = 256 * 1024;
    uint8_t *buf = malloc(cap);
    if (!buf) {
        close(fd);
        snprintf(err, elen, "sin memoria");
        return -1;
    }
    size_t len = 0;
    while (len < 4 * 1024 * 1024) {
        if (len + 65536 > cap) {
            size_t ncap = cap * 2;
            uint8_t *nb = realloc(buf, ncap);
            if (!nb) break;
            buf = nb;
            cap = ncap;
        }
        ssize_t r = recv(fd, buf + len, cap - len, 0);
        if (r > 0) {
            len += (size_t)r;
        } else if (r == 0) {
            break;
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }
    close(fd);
    if (len == 0) {
        free(buf);
        snprintf(err, elen, "respuesta vacia");
        return -1;
    }
    uint8_t *hdr_end = NULL;
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
            hdr_end = buf + i + 4;
            break;
        }
    }
    if (!hdr_end) {
        free(buf);
        snprintf(err, elen, "respuesta HTTP incompleta");
        return -1;
    }
    int status = 0;
    if (sscanf((char *)buf, "HTTP/%*s %d", &status) != 1 || status < 200 || status >= 300) {
        free(buf);
        snprintf(err, elen, "HTTP %d", status);
        return -1;
    }
    size_t blen = len - (size_t)(hdr_end - buf);
    uint8_t *body = malloc(blen ? blen : 1);
    if (!body) {
        free(buf);
        snprintf(err, elen, "sin memoria");
        return -1;
    }
    memcpy(body, hdr_end, blen);
    free(buf);
    *out = body;
    *outlen = blen;
    return 0;
}

int ps_read_file(const char *path, uint8_t **out, size_t *outlen,
                 char *err, size_t elen) {
    *out = NULL;
    *outlen = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, elen, "no se pudo abrir %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        snprintf(err, elen, "no se pudo leer %s", path);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > 16 * 1024 * 1024) {
        fclose(f);
        snprintf(err, elen, "archivo demasiado grande");
        return -1;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        snprintf(err, elen, "sin memoria");
        return -1;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        free(buf);
        snprintf(err, elen, "lectura incompleta");
        return -1;
    }
    buf[n] = 0;
    *out = buf;
    *outlen = n;
    return 0;
}

int ps_write_file(const char *path, const void *data, size_t len) {
    char tmp[PS_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    size_t n = fwrite(data, 1, len, f);
    if (fclose(f) != 0 || n != len) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int ps_parse_query(const char *query, const char *key, char *out, size_t outlen) {
    if (!query || !key || !out || outlen == 0) return -1;
    size_t klen = strlen(key);
    const char *p = query;
    while (*p) {
        const char *amp = strchr(p, '&');
        const char *end = amp ? amp : p + strlen(p);
        if ((size_t)(end - p) > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            size_t j = 0;
            while (v < end && j + 1 < outlen) {
                if (*v == '+') {
                    out[j++] = ' ';
                    v++;
                } else if (*v == '%' && v + 2 < end) {
                    int a = hexval(v[1]);
                    int b = hexval(v[2]);
                    if (a >= 0 && b >= 0) {
                        out[j++] = (char)((a << 4) | b);
                        v += 3;
                    } else {
                        out[j++] = *v++;
                    }
                } else {
                    out[j++] = *v++;
                }
            }
            out[j] = '\0';
            return 0;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return -1;
}
