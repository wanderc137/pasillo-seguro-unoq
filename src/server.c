#define _GNU_SOURCE
#include "pasillo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

typedef struct {
    PsApp *app;
    int fd;
    struct sockaddr_in peer;
} ClientArg;

static int send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, p + off, len - off, MSG_NOSIGNAL);
        if (n > 0) {
            off += (size_t)n;
        } else if (n < 0 && (errno == EINTR)) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd = { .fd = fd, .events = POLLOUT };
            if (poll(&pfd, 1, 3000) <= 0) return -1;
        } else {
            return -1;
        }
    }
    return 0;
}

static void send_headers(int fd, int code, const char *reason,
                         const char *ctype, size_t clen, const char *extra) {
    char hdr[1024];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        code, reason, ctype, clen, extra ? extra : "");
    if (n > 0) send_all(fd, hdr, (size_t)n);
}

static void send_json(int fd, int code, const char *reason, const char *json) {
    size_t len = strlen(json);
    send_headers(fd, code, reason, "application/json; charset=utf-8", len, NULL);
    send_all(fd, json, len);
}

static void send_error_json(int fd, int code, const char *reason, const char *msg) {
    char esc[1024];
    ps_json_escape(msg ? msg : "", esc, sizeof(esc));
    char json[1200];
    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", esc);
    send_json(fd, code, reason, json);
}

static int is_loopback(const struct sockaddr_in *peer) {
    return peer && peer->sin_addr.s_addr == htonl(INADDR_LOOPBACK);
}

static void handle_stream(PsApp *app, int fd) {
    const char *hdr =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n";
    if (send_all(fd, hdr, strlen(hdr)) != 0) return;

    uint64_t last = 0;
    int fps = app->cfg.stream_fps > 0 ? app->cfg.stream_fps : 12;
    long frame_ms = 1000 / fps;
    if (frame_ms < 30) frame_ms = 30;

    while (!app->stop) {
        uint8_t *copy = NULL;
        size_t clen = 0;
        pthread_mutex_lock(&app->hub.lock);
        if (app->hub.seq == last) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            pthread_cond_timedwait(&app->hub.cond, &app->hub.lock, &ts);
        }
        if (app->hub.seq != last && app->hub.jpeg && app->hub.jpeg_len) {
            clen = app->hub.jpeg_len;
            copy = malloc(clen);
            if (copy) memcpy(copy, app->hub.jpeg, clen);
            last = app->hub.seq;
        }
        pthread_mutex_unlock(&app->hub.lock);
        if (!copy) {
            if (app->stop) break;
            continue;
        }
        long long t0 = ps_now_ms();
        char part[256];
        int pl = snprintf(part, sizeof(part),
            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", clen);
        int rc = 0;
        if (pl > 0) rc |= send_all(fd, part, (size_t)pl);
        rc |= send_all(fd, copy, clen);
        rc |= send_all(fd, "\r\n", 2);
        free(copy);
        if (rc != 0) break;
        long long spent = ps_now_ms() - t0;
        if (spent < frame_ms) ps_sleep_ms((int)(frame_ms - spent));
    }
}

static void handle_snapshot(PsApp *app, int fd) {
    pthread_mutex_lock(&app->hub.lock);
    uint8_t *copy = NULL;
    size_t len = 0;
    if (app->hub.jpeg && app->hub.jpeg_len) {
        len = app->hub.jpeg_len;
        copy = malloc(len);
        if (copy) memcpy(copy, app->hub.jpeg, len);
    }
    pthread_mutex_unlock(&app->hub.lock);
    if (!copy) {
        send_error_json(fd, 503, "Service Unavailable", "sin frame disponible");
        return;
    }
    send_headers(fd, 200, "OK", "image/jpeg", len, NULL);
    send_all(fd, copy, len);
    free(copy);
}

static void handle_static(PsApp *app, int fd, const char *path) {
    const char *name = NULL;
    const char *ctype = "application/octet-stream";
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        name = "index.html"; ctype = "text/html; charset=utf-8";
    } else if (strcmp(path, "/app.js") == 0) {
        name = "app.js"; ctype = "application/javascript; charset=utf-8";
    } else if (strcmp(path, "/style.css") == 0) {
        name = "style.css"; ctype = "text/css; charset=utf-8";
    } else if (strcmp(path, "/favicon.svg") == 0) {
        name = "favicon.svg"; ctype = "image/svg+xml";
    } else if (strcmp(path, "/favicon.ico") == 0) {
        send_headers(fd, 204, "No Content", "text/plain", 0, NULL);
        return;
    } else {
        send_error_json(fd, 404, "Not Found", "no encontrado");
        return;
    }
    char full[PS_MAX_PATH * 2];
    snprintf(full, sizeof(full), "%s/%s", app->cfg.web_dir, name);
    uint8_t *data = NULL;
    size_t len = 0;
    char err[256];
    if (ps_read_file(full, &data, &len, err, sizeof(err)) != 0) {
        send_error_json(fd, 404, "Not Found", err);
        return;
    }
    send_headers(fd, 200, "OK", ctype, len, NULL);
    send_all(fd, data, len);
    free(data);
}

static void handle_status(PsApp *app, int fd) {
    char source[PS_MAX_URL], state[32], message[256], adb_state[32], adb_detail[2048];
    PsRisk risk;
    double risk_value, fps, latency;
    int objects;
    uint64_t seq;

    pthread_mutex_lock(&app->hub.lock);
    snprintf(source, sizeof(source), "%s", app->hub.source);
    snprintf(state, sizeof(state), "%s", app->hub.state);
    snprintf(message, sizeof(message), "%s", app->hub.message);
    risk = app->hub.risk;
    risk_value = app->hub.risk_value;
    objects = app->hub.objects;
    fps = app->hub.fps;
    latency = app->hub.latency_ms;
    seq = app->hub.seq;
    pthread_mutex_unlock(&app->hub.lock);

    pthread_mutex_lock(&app->state_lock);
    snprintf(adb_state, sizeof(adb_state), "%s", app->adb_state);
    snprintf(adb_detail, sizeof(adb_detail), "%s", app->adb_detail);
    pthread_mutex_unlock(&app->state_lock);

    if (source[0] == '\0') {
        pthread_mutex_lock(&app->state_lock);
        snprintf(source, sizeof(source), "%s", app->cfg.camera_url);
        pthread_mutex_unlock(&app->state_lock);
    }

    char esrc[PS_MAX_URL * 2], estate[128], emsg[1024], eadb[4096], edetail[4096];
    ps_json_escape(source, esrc, sizeof(esrc));
    ps_json_escape(state, estate, sizeof(estate));
    ps_json_escape(message, emsg, sizeof(emsg));
    ps_json_escape(adb_state, eadb, sizeof(eadb));
    ps_json_escape(adb_detail, edetail, sizeof(edetail));

    char json[PS_MAX_JSON * 2];
    snprintf(json, sizeof(json),
        "{\"ok\":true,\"version\":\"%s\",\"state\":\"%s\",\"message\":\"%s\","
        "\"source\":\"%s\",\"transport\":\"%s\",\"risk\":\"%s\",\"risk_value\":%.3f,"
        "\"objects\":%d,\"fps\":%.1f,\"latency_ms\":%.1f,\"frames\":%llu,"
        "\"thresholds\":{\"red\":%.2f,\"amber\":%.2f},"
        "\"adb\":{\"state\":\"%s\",\"detail\":\"%s\"}}",
        PS_VERSION, estate, emsg, esrc,
        app->cfg.transport == PS_TRANSPORT_USB ? "usb" : "lan",
        ps_risk_name(risk), risk_value, objects, fps, latency,
        (unsigned long long)seq, app->cfg.risk_red, app->cfg.risk_amber,
        eadb, edetail);
    send_json(fd, 200, "OK", json);
}

static void handle_health(PsApp *app, int fd) {
    char json[512];
    snprintf(json, sizeof(json),
        "{\"ok\":true,\"version\":\"%s\",\"state\":\"%s\",\"running\":%s}",
        PS_VERSION, app->running ? "running" : "idle",
        app->running ? "true" : "false");
    send_json(fd, 200, "OK", json);
}

static void handle_probe(PsApp *app, int fd, const char *query) {
    (void)app;
    char raw[PS_MAX_URL], video[PS_MAX_URL], photo[PS_MAX_URL], err[256];
    if (ps_parse_query(query, "url", raw, sizeof(raw)) != 0 || !raw[0]) {
        send_error_json(fd, 400, "Bad Request", "falta url");
        return;
    }
    if (ps_normalize_url(raw, video, sizeof(video), photo, sizeof(photo),
                         err, sizeof(err)) != 0) {
        send_error_json(fd, 400, "Bad Request", err);
        return;
    }
    uint8_t *data = NULL;
    size_t len = 0;
    if (ps_http_get(photo, &data, &len, 5000, err, sizeof(err)) != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "no se pudo leer /photo.jpg: %s", err);
        send_error_json(fd, 502, "Bad Gateway", msg);
        return;
    }
    uint8_t *rgb = NULL;
    int iw = 0, ih = 0;
    char jerr[256];
    if (ps_jpeg_decode(data, len, &rgb, &iw, &ih, jerr, sizeof(jerr)) != 0) {
        free(data);
        send_error_json(fd, 502, "Bad Gateway", jerr);
        return;
    }
    free(rgb);
    free(data);
    char evideo[PS_MAX_URL * 2], ephoto[PS_MAX_URL * 2];
    ps_json_escape(video, evideo, sizeof(evideo));
    ps_json_escape(photo, ephoto, sizeof(ephoto));
    char json[4096];
    snprintf(json, sizeof(json),
        "{\"ok\":true,\"url\":\"%s\",\"photo_url\":\"%s\",\"resolution\":[%d,%d]}",
        evideo, ephoto, ih, iw);
    send_json(fd, 200, "OK", json);
}

static void handle_start(PsApp *app, int fd, const char *query) {
    char raw[PS_MAX_URL], video[PS_MAX_URL], photo[PS_MAX_URL], err[256];
    char sens[32];
    if (ps_parse_query(query, "url", raw, sizeof(raw)) == 0 && raw[0]) {
        if (strcmp(raw, "fake") == 0 || strcmp(raw, "demo") == 0) {
            app->cfg.fake = 1;
            app->running = 0;
            ps_sleep_ms(250);
            pthread_mutex_lock(&app->hub.lock);
            snprintf(app->hub.state, sizeof(app->hub.state), "starting");
            snprintf(app->hub.message, sizeof(app->hub.message), "demo sintetica");
            snprintf(app->hub.source, sizeof(app->hub.source), "fake");
            pthread_mutex_unlock(&app->hub.lock);
            app->running = 1;
            send_json(fd, 200, "OK", "{\"ok\":true,\"url\":\"fake\"}");
            return;
        }
        app->cfg.fake = 0;
        if (ps_normalize_url(raw, video, sizeof(video), photo, sizeof(photo),
                             err, sizeof(err)) != 0) {
            send_error_json(fd, 400, "Bad Request", err);
            return;
        }
        pthread_mutex_lock(&app->state_lock);
        snprintf(app->cfg.camera_url, sizeof(app->cfg.camera_url), "%s", video);
        pthread_mutex_unlock(&app->state_lock);
    } else {
        pthread_mutex_lock(&app->state_lock);
        if (!app->cfg.camera_url[0]) {
            pthread_mutex_unlock(&app->state_lock);
            send_error_json(fd, 400, "Bad Request", "falta url de camara");
            return;
        }
        snprintf(video, sizeof(video), "%s", app->cfg.camera_url);
        pthread_mutex_unlock(&app->state_lock);
    }
    if (ps_parse_query(query, "sensitivity", sens, sizeof(sens)) == 0) {
        if (strcmp(sens, "low") == 0) { app->cfg.risk_red = 0.82; app->cfg.risk_amber = 0.50; }
        else if (strcmp(sens, "high") == 0) { app->cfg.risk_red = 0.60; app->cfg.risk_amber = 0.28; }
        else { app->cfg.risk_red = 0.72; app->cfg.risk_amber = 0.38; }
    }
    app->running = 0;
    ps_sleep_ms(250);
    pthread_mutex_lock(&app->hub.lock);
    snprintf(app->hub.state, sizeof(app->hub.state), "starting");
    snprintf(app->hub.message, sizeof(app->hub.message), "conectando a la camara");
    snprintf(app->hub.source, sizeof(app->hub.source), "%s", video);
    pthread_mutex_unlock(&app->hub.lock);
    app->running = 1;
    char evideo[PS_MAX_URL * 2];
    ps_json_escape(video, evideo, sizeof(evideo));
    char json[PS_MAX_URL * 2 + 128];
    snprintf(json, sizeof(json), "{\"ok\":true,\"url\":\"%s\"}", evideo);
    send_json(fd, 200, "OK", json);
}

static void handle_stop(PsApp *app, int fd) {
    app->running = 0;
    pthread_mutex_lock(&app->hub.lock);
    snprintf(app->hub.state, sizeof(app->hub.state), "idle");
    snprintf(app->hub.message, sizeof(app->hub.message), "detenido");
    pthread_mutex_unlock(&app->hub.lock);
    send_json(fd, 200, "OK", "{\"ok\":true}");
}

static void handle_usb(PsApp *app, int fd, const char *path, int loopback) {
    if (!loopback) {
        send_error_json(fd, 403, "Forbidden", "solo loopback");
        return;
    }
    char adb[PS_MAX_PATH], msg[1024];
    if (ps_adb_find(adb, sizeof(adb)) != 0) {
        send_error_json(fd, 503, "Service Unavailable", "adb no encontrado");
        return;
    }
    if (strcmp(path, "/api/usb/status") == 0) {
        char state[32], detail[2048];
        ps_adb_status(adb, state, sizeof(state), detail, sizeof(detail));
        pthread_mutex_lock(&app->state_lock);
        snprintf(app->adb_state, sizeof(app->adb_state), "%s", state);
        snprintf(app->adb_detail, sizeof(app->adb_detail), "%s", detail);
        pthread_mutex_unlock(&app->state_lock);
        char es[128], ed[4096];
        ps_json_escape(state, es, sizeof(es));
        ps_json_escape(detail, ed, sizeof(ed));
        char json[8192];
        snprintf(json, sizeof(json), "{\"ok\":true,\"state\":\"%s\",\"detail\":\"%s\"}", es, ed);
        send_json(fd, 200, "OK", json);
        return;
    }
    if (strcmp(path, "/api/usb/setup") == 0) {
        int rc = ps_adb_setup(adb, app->cfg.usb_camera_port, app->cfg.usb_viewer_port,
                              msg, sizeof(msg));
        pthread_mutex_lock(&app->state_lock);
        snprintf(app->adb_state, sizeof(app->adb_state), "%s", rc == 0 ? "ready" : "error");
        snprintf(app->adb_detail, sizeof(app->adb_detail), "%s", msg);
        pthread_mutex_unlock(&app->state_lock);
        char em[2048];
        ps_json_escape(msg, em, sizeof(em));
        char json[4096];
        snprintf(json, sizeof(json), "{\"ok\":%s,\"message\":\"%s\"}",
                 rc == 0 ? "true" : "false", em);
        send_json(fd, rc == 0 ? 200 : 409, rc == 0 ? "OK" : "Conflict", json);
        return;
    }
    if (strcmp(path, "/api/usb/teardown") == 0) {
        ps_adb_teardown(adb, msg, sizeof(msg));
        pthread_mutex_lock(&app->state_lock);
        snprintf(app->adb_state, sizeof(app->adb_state), "none");
        snprintf(app->adb_detail, sizeof(app->adb_detail), "%s", msg);
        pthread_mutex_unlock(&app->state_lock);
        send_json(fd, 200, "OK", "{\"ok\":true}");
        return;
    }
    send_error_json(fd, 404, "Not Found", "endpoint USB desconocido");
}

static void *client_thread(void *argp) {
    ClientArg *arg = argp;
    PsApp *app = arg->app;
    int fd = arg->fd;
    int loopback = is_loopback(&arg->peer);

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    char req[16384];
    size_t len = 0;
    while (len < sizeof(req) - 1) {
        ssize_t n = recv(fd, req + len, sizeof(req) - 1 - len, 0);
        if (n > 0) {
            len += (size_t)n;
            req[len] = '\0';
            if (strstr(req, "\r\n\r\n")) break;
        } else if (n == 0) {
            close(fd);
            free(arg);
            return NULL;
        } else if (errno == EINTR) {
            continue;
        } else {
            close(fd);
            free(arg);
            return NULL;
        }
    }

    char method[16] = {0}, target[2048] = {0};
    if (sscanf(req, "%15s %2047s", method, target) != 2) {
        send_error_json(fd, 400, "Bad Request", "peticion invalida");
        close(fd);
        free(arg);
        return NULL;
    }
    char *query = strchr(target, '?');
    if (query) *query++ = '\0';
    char *path = target;

    if (strcmp(method, "OPTIONS") == 0) {
        send_headers(fd, 204, "No Content", "text/plain", 0,
                     "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
                     "Access-Control-Allow-Headers: Content-Type\r\n");
    } else if (strncmp(path, "/api/", 5) == 0) {
        if (strcmp(path, "/api/health") == 0) handle_health(app, fd);
        else if (strcmp(path, "/api/status") == 0) handle_status(app, fd);
        else if (strcmp(path, "/api/probe") == 0) handle_probe(app, fd, query);
        else if (strcmp(path, "/api/start") == 0) handle_start(app, fd, query);
        else if (strcmp(path, "/api/stop") == 0) handle_stop(app, fd);
        else if (strncmp(path, "/api/usb/", 9) == 0) handle_usb(app, fd, path, loopback);
        else send_error_json(fd, 404, "Not Found", "endpoint desconocido");
    } else if (strcmp(path, "/stream.mjpg") == 0) {
        handle_stream(app, fd);
    } else if (strcmp(path, "/snapshot.jpg") == 0) {
        handle_snapshot(app, fd);
    } else {
        handle_static(app, fd, path);
    }

    close(fd);
    free(arg);
    return NULL;
}

int ps_server_run(PsApp *app) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        ps_log("no se pudo crear socket: %s", strerror(errno));
        return -1;
    }
    int one = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)app->cfg.port);
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ps_log("bind %d fallo: %s", app->cfg.port, strerror(errno));
        close(sfd);
        return -1;
    }
    if (listen(sfd, 16) != 0) {
        ps_log("listen fallo: %s", strerror(errno));
        close(sfd);
        return -1;
    }
    ps_log("servidor HTTP en 0.0.0.0:%d", app->cfg.port);

    while (!app->stop) {
        struct pollfd pfd = { .fd = sfd, .events = POLLIN };
        int pr = poll(&pfd, 1, 500);
        if (pr <= 0) continue;
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int cfd = accept(sfd, (struct sockaddr *)&peer, &plen);
        if (cfd < 0) continue;
        ClientArg *arg = malloc(sizeof(*arg));
        if (!arg) { close(cfd); continue; }
        arg->app = app;
        arg->fd = cfd;
        arg->peer = peer;
        pthread_t th;
        if (pthread_create(&th, NULL, client_thread, arg) != 0) {
            close(cfd);
            free(arg);
            continue;
        }
        pthread_detach(th);
    }
    close(sfd);
    return 0;
}
