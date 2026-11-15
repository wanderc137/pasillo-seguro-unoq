#define _GNU_SOURCE
#include "pasillo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

static int cam_parse_url(const char *url, char *host, size_t hlen,
                         int *port, char *path, size_t plen) {
    const char *s = url;
    if (strncasecmp(s, "http://", 7) == 0) s += 7;
    else if (strstr(s, "://")) return -1;
    const char *slash = strchr(s, '/');
    const char *hend = slash ? slash : s + strlen(s);
    const char *colon = NULL;
    for (const char *p = s; p < hend; p++) {
        if (*p == ':') colon = p;
    }
    size_t n = (size_t)((colon ? colon : hend) - s);
    if (n == 0 || n >= hlen) return -1;
    memcpy(host, s, n);
    host[n] = '\0';
    *port = 8080;
    if (colon) {
        char *endp = NULL;
        long v = strtol(colon + 1, &endp, 10);
        if (!endp || endp != hend || v < 1 || v > 65535) return -1;
        *port = (int)v;
    }
    snprintf(path, plen, "%s", slash ? slash : "/video");
    return 0;
}

static int connect_timeout(const char *host, int port, int timeout_ms, char *err, size_t elen) {
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
    if (fd < 0) snprintf(err, elen, "no se pudo conectar a %s:%d", host, port);
    return fd;
}

int ps_camera_open(PsCamera *cam, const char *url, char *err, size_t elen) {
    memset(cam, 0, sizeof(*cam));
    cam->fd = -1;
    if (err && elen) err[0] = '\0';
    if (!url || !*url) {
        snprintf(err, elen, "URL de camara vacia");
        return -1;
    }
    if (cam_parse_url(url, cam->host, sizeof(cam->host), &cam->port,
                      cam->path, sizeof(cam->path)) != 0) {
        snprintf(err, elen, "URL de camara invalida");
        return -1;
    }
    cam->fd = connect_timeout(cam->host, cam->port, 5000, err, elen);
    if (cam->fd < 0) return -1;

    int flags = fcntl(cam->fd, F_GETFL, 0);
    fcntl(cam->fd, F_SETFL, flags | O_NONBLOCK);

    char req[1024];
    int rl = snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %s:%d\r\nUser-Agent: pasillo-seguro/%s\r\n"
        "Connection: close\r\nAccept: multipart/x-mixed-replace,image/jpeg,*/*\r\n\r\n",
        cam->path, cam->host, cam->port, PS_VERSION);
    if (rl <= 0) {
        ps_camera_close(cam);
        snprintf(err, elen, "peticion demasiado larga");
        return -1;
    }
    size_t sent = 0;
    while (sent < (size_t)rl) {
        ssize_t n = send(cam->fd, req + sent, (size_t)rl - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            struct pollfd pfd = { .fd = cam->fd, .events = POLLOUT };
            if (poll(&pfd, 1, 3000) <= 0) {
                ps_camera_close(cam);
                snprintf(err, elen, "timeout enviando peticion");
                return -1;
            }
        } else {
            ps_camera_close(cam);
            snprintf(err, elen, "fallo enviando peticion");
            return -1;
        }
    }

    cam->cap = 512 * 1024;
    cam->buf = malloc(cam->cap);
    if (!cam->buf) {
        ps_camera_close(cam);
        snprintf(err, elen, "sin memoria");
        return -1;
    }
    cam->len = 0;
    cam->header_done = 0;
    cam->last_rx = ps_now();

    /* read HTTP headers */
    double deadline = ps_now() + 5.0;
    while (!cam->header_done) {
        if (ps_now() > deadline) {
            ps_camera_close(cam);
            snprintf(err, elen, "timeout esperando cabeceras");
            return -1;
        }
        if (cam->len + 65536 > cam->cap) {
            size_t ncap = cam->cap * 2;
            uint8_t *nb = realloc(cam->buf, ncap);
            if (!nb) {
                ps_camera_close(cam);
                snprintf(err, elen, "sin memoria");
                return -1;
            }
            cam->buf = nb;
            cam->cap = ncap;
        }
        ssize_t n = recv(cam->fd, cam->buf + cam->len, cam->cap - cam->len, 0);
        if (n > 0) {
            cam->len += (size_t)n;
            cam->last_rx = ps_now();
        } else if (n == 0) {
            ps_camera_close(cam);
            snprintf(err, elen, "camara cerro la conexion");
            return -1;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            struct pollfd pfd = { .fd = cam->fd, .events = POLLIN };
            poll(&pfd, 1, 200);
            continue;
        } else {
            ps_camera_close(cam);
            snprintf(err, elen, "error leyendo cabeceras");
            return -1;
        }
        for (size_t i = 0; i + 3 < cam->len; i++) {
            if (cam->buf[i] == '\r' && cam->buf[i+1] == '\n' &&
                cam->buf[i+2] == '\r' && cam->buf[i+3] == '\n') {
                size_t hlen = i + 4;
                int status = 0;
                if (sscanf((char *)cam->buf, "HTTP/%*s %d", &status) != 1 ||
                    status < 200 || status >= 300) {
                    ps_camera_close(cam);
                    snprintf(err, elen, "HTTP %d de la camara", status);
                    return -1;
                }
                size_t rem = cam->len - hlen;
                if (rem) memmove(cam->buf, cam->buf + hlen, rem);
                cam->len = rem;
                cam->header_done = 1;
                break;
            }
        }
        if (cam->len > 65536) {
            ps_camera_close(cam);
            snprintf(err, elen, "cabeceras demasiado grandes");
            return -1;
        }
    }
    return 0;
}

int ps_camera_read(PsCamera *cam, PsJpegFrame *frame, int timeout_ms,
                   char *err, size_t elen) {
    if (err && elen) err[0] = '\0';
    memset(frame, 0, sizeof(*frame));
    if (!cam || cam->fd < 0 || !cam->buf) {
        snprintf(err, elen, "camara no abierta");
        return -1;
    }
    double deadline = ps_now() + (double)timeout_ms / 1000.0;

    for (;;) {
        /* Find an EOI and take the closest SOI before it. This resyncs
           cleanly if a previous frame was truncated and the next one starts. */
        size_t soi = SIZE_MAX, eoi = SIZE_MAX;
        for (size_t i = 0; i + 1 < cam->len; i++) {
            if (cam->buf[i] == 0xFF && cam->buf[i+1] == 0xD9) {
                eoi = i;
                soi = SIZE_MAX;
                for (size_t j = i; j > 0; j--) {
                    if (cam->buf[j-1] == 0xFF && cam->buf[j] == 0xD8) {
                        soi = j - 1;
                        break;
                    }
                }
                if (soi != SIZE_MAX) break;
            }
        }
        if (soi != SIZE_MAX && eoi != SIZE_MAX) {
            size_t jlen = eoi + 2 - soi;
            uint8_t *j = malloc(jlen);
            if (!j) {
                snprintf(err, elen, "sin memoria para frame");
                return -1;
            }
            memcpy(j, cam->buf + soi, jlen);
            size_t consumed = eoi + 2;
            if (consumed < cam->len) {
                memmove(cam->buf, cam->buf + consumed, cam->len - consumed);
                cam->len -= consumed;
            } else {
                cam->len = 0;
            }
            frame->data = j;
            frame->len = jlen;
            frame->ts = ps_now();
            return 1;
        }

        if (ps_now() >= deadline) return 0;
        if (cam->len + 65536 > cam->cap) {
            size_t ncap = cam->cap * 2;
            if (ncap > PS_MAX_JPEG * 2) {
                /* keep the tail, drop ancient bytes */
                size_t keep = cam->len / 2;
                memmove(cam->buf, cam->buf + cam->len - keep, keep);
                cam->len = keep;
                ncap = cam->cap;
            }
            uint8_t *nb = realloc(cam->buf, ncap);
            if (!nb) {
                snprintf(err, elen, "sin memoria");
                return -1;
            }
            cam->buf = nb;
            cam->cap = ncap;
        }
        ssize_t n = recv(cam->fd, cam->buf + cam->len, cam->cap - cam->len, 0);
        if (n > 0) {
            cam->len += (size_t)n;
            cam->last_rx = ps_now();
        } else if (n == 0) {
            snprintf(err, elen, "camara cerro la conexion");
            return -1;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            struct pollfd pfd = { .fd = cam->fd, .events = POLLIN };
            int wait = (int)((deadline - ps_now()) * 1000.0);
            if (wait < 0) wait = 0;
            if (wait > 200) wait = 200;
            poll(&pfd, 1, wait);
        } else {
            snprintf(err, elen, "error leyendo camara");
            return -1;
        }
    }
}

void ps_camera_close(PsCamera *cam) {
    if (!cam) return;
    if (cam->fd >= 0) {
        close(cam->fd);
        cam->fd = -1;
    }
    free(cam->buf);
    cam->buf = NULL;
    cam->len = cam->cap = 0;
    cam->header_done = 0;
}
