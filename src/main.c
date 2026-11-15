#define _GNU_SOURCE
#include "pasillo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <math.h>

static PsApp *g_app = NULL;

void ps_hub_update(PsHub *hub, const uint8_t *jpeg, size_t len, int w, int h,
                   PsRisk risk, double risk_value, int objects, double fps,
                   double latency_ms, const char *state, const char *message,
                   const char *source) {
    pthread_mutex_lock(&hub->lock);
    free(hub->jpeg);
    hub->jpeg = NULL;
    hub->jpeg_len = 0;
    if (jpeg && len) {
        hub->jpeg = malloc(len);
        if (hub->jpeg) {
            memcpy(hub->jpeg, jpeg, len);
            hub->jpeg_len = len;
        }
    }
    hub->width = w;
    hub->height = h;
    hub->risk = risk;
    hub->risk_value = risk_value;
    hub->objects = objects;
    hub->fps = fps;
    hub->latency_ms = latency_ms;
    if (state) snprintf(hub->state, sizeof(hub->state), "%s", state);
    if (message) snprintf(hub->message, sizeof(hub->message), "%s", message);
    if (source) snprintf(hub->source, sizeof(hub->source), "%s", source);
    hub->seq++;
    pthread_cond_broadcast(&hub->cond);
    pthread_mutex_unlock(&hub->lock);
}

static void fake_rect(uint8_t *rgb, int w, int h, int x, int y, int rw, int rh,
                      uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0) { rw += x; x = 0; }
    if (y < 0) { rh += y; y = 0; }
    if (x + rw > w) rw = w - x;
    if (y + rh > h) rh = h - y;
    if (rw <= 0 || rh <= 0) return;
    for (int yy = y; yy < y + rh; yy++) {
        uint8_t *row = rgb + ((size_t)yy * w + x) * 3;
        for (int xx = 0; xx < rw; xx++) {
            row[0] = r; row[1] = g; row[2] = b;
            row += 3;
        }
    }
}

static void make_fake_frame(uint8_t *rgb, int w, int h, double t) {
    for (int y = 0; y < h; y++) {
        uint8_t *row = rgb + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            int v = 30 + (x * 40) / (w > 1 ? w : 1) + (y * 30) / (h > 1 ? h : 1);
            row[0] = (uint8_t)(v / 2);
            row[1] = (uint8_t)v;
            row[2] = (uint8_t)(v + 20);
            row += 3;
        }
    }
    double phase = fmod(t, 7.0) / 7.0;
    double scale = 0.25 + 0.75 * phase;
    int bw = (int)(120 * scale);
    int bh = (int)(230 * scale);
    int bx = w / 2 - bw / 2 + (int)(60 * sin(t * 0.8));
    int by = h - bh - 10;
    fake_rect(rgb, w, h, bx, by, bw, bh, 220, 80, 40);
    fake_rect(rgb, w, h, bx + bw / 3, by + bh / 8, bw / 3, bh / 4, 240, 190, 150);
    int bx2 = (int)((t * 90.0));
    bx2 = bx2 % (w + 120) - 60;
    fake_rect(rgb, w, h, bx2, h / 2, 30, 130, 80, 170, 230);
}

static void *capture_thread(void *argp) {
    PsApp *app = argp;
    PsCamera cam;
    memset(&cam, 0, sizeof(cam));
    cam.fd = -1;
    double last_frame_ts = 0.0;
    double fps_ema = 0.0;
    int fake_frame = 0;
    int jpeg_errors = 0;

    while (!app->stop) {
        if (!app->running) {
            if (cam.fd >= 0) ps_camera_close(&cam);
            ps_sleep_ms(120);
            continue;
        }
        if (app->cfg.fake) {
            int fw = 640, fh = 480;
            uint8_t *rgb = malloc((size_t)fw * fh * 3);
            if (!rgb) {
                ps_sleep_ms(500);
                continue;
            }
            make_fake_frame(rgb, fw, fh, ps_now());
            ps_vision_process(&app->vision, rgb, fw, fh, &app->cfg);
            uint8_t *jpeg = NULL;
            size_t jlen = 0;
            char jerr[256];
            if (ps_jpeg_encode(rgb, fw, fh, app->cfg.jpeg_quality,
                               &jpeg, &jlen, jerr, sizeof(jerr)) == 0) {
                double now = ps_now();
                double fps = 0.0;
                if (last_frame_ts > 0) {
                    double dt = now - last_frame_ts;
                    if (dt > 0.001) fps_ema = 0.85 * fps_ema + 0.15 * (1.0 / dt);
                    fps = fps_ema;
                }
                last_frame_ts = now;
                ps_hub_update(&app->hub, jpeg, jlen, fw, fh,
                              app->vision.level, app->vision.risk,
                              app->vision.objects, fps, 0.0,
                              "running", "fuente sintetica", "fake");
                free(jpeg);
            }
            free(rgb);
            fake_frame++;
            int delay = 1000 / (app->cfg.stream_fps > 0 ? app->cfg.stream_fps : 12);
            ps_sleep_ms(delay);
            continue;
        }

        char url[PS_MAX_URL] = {0};
        pthread_mutex_lock(&app->state_lock);
        snprintf(url, sizeof(url), "%s", app->cfg.camera_url);
        pthread_mutex_unlock(&app->state_lock);
        if (!url[0]) {
            ps_sleep_ms(300);
            continue;
        }

        if (cam.fd < 0) {
            char err[256];
            if (ps_camera_open(&cam, url, err, sizeof(err)) != 0) {
                char msg[512];
                snprintf(msg, sizeof(msg), "no se pudo abrir la camara: %s", err);
                ps_hub_update(&app->hub, NULL, 0, 0, 0,
                              PS_RISK_GREEN, 0.0, 0, 0.0, 0.0,
                              "error", msg, url);
                ps_log("%s", msg);
                ps_sleep_ms(1500);
                continue;
            }
            ps_hub_update(&app->hub, NULL, 0, 0, 0,
                          PS_RISK_GREEN, 0.0, 0, 0.0, 0.0,
                          "running", "conectado", url);
        }

        PsJpegFrame frame;
        char err[256];
        int rc = ps_camera_read(&cam, &frame, 800, err, sizeof(err));
        if (rc < 0) {
            ps_log("camara: %s", err);
            ps_camera_close(&cam);
            char msg[512];
            snprintf(msg, sizeof(msg), "camara desconectada: %s", err);
            ps_hub_update(&app->hub, NULL, 0, 0, 0,
                          PS_RISK_GREEN, 0.0, 0, 0.0, 0.0,
                          "error", msg, url);
            ps_sleep_ms(1200);
            continue;
        }
        if (rc == 0) continue;

        uint8_t *rgb = NULL;
        int iw = 0, ih = 0;
        char jerr[256];
        if (ps_jpeg_decode(frame.data, frame.len, &rgb, &iw, &ih, jerr, sizeof(jerr)) != 0) {
            jpeg_errors++;
            if (jpeg_errors <= 3 || (jpeg_errors % 100) == 0) {
                ps_log("JPEG: %s (errores=%d)", jerr, jpeg_errors);
            }
            free(frame.data);
            continue;
        }
        free(frame.data);

        if (app->vision.frame_no == 0) {
            int pw = app->cfg.proc_w;
            int ph = app->cfg.proc_h;
            if (ih > iw * 1.15) { pw = 108; ph = 192; }
            else if (iw > ih * 1.15) { pw = 192; ph = 144; }
            else { pw = 160; ph = 160; }
            if (pw != app->vision.w || ph != app->vision.h) {
                ps_vision_free(&app->vision);
                if (ps_vision_init(&app->vision, pw, ph) != 0) {
                    ps_log("no se pudo adaptar la vision a %dx%d", pw, ph);
                    free(rgb);
                    continue;
                }
            }
        }

        ps_vision_process(&app->vision, rgb, iw, ih, &app->cfg);
        uint8_t *jpeg = NULL;
        size_t jlen = 0;
        if (ps_jpeg_encode(rgb, iw, ih, app->cfg.jpeg_quality,
                           &jpeg, &jlen, jerr, sizeof(jerr)) == 0) {
            double now = ps_now();
            double fps = 0.0;
            if (last_frame_ts > 0) {
                double dt = now - last_frame_ts;
                if (dt > 0.001) fps_ema = 0.85 * fps_ema + 0.15 * (1.0 / dt);
                fps = fps_ema;
            }
            last_frame_ts = now;
            double latency = (now - frame.ts) * 1000.0;
            if (latency < 0) latency = 0;
            ps_hub_update(&app->hub, jpeg, jlen, iw, ih,
                          app->vision.level, app->vision.risk,
                          app->vision.objects, fps, latency,
                          "running", "conectado", url);
            free(jpeg);
        }
        free(rgb);
    }
    if (cam.fd >= 0) ps_camera_close(&cam);
    return NULL;
}

static void *adb_thread(void *argp) {
    PsApp *app = argp;
    while (!app->stop) {
        if (app->cfg.transport == PS_TRANSPORT_USB && app->cfg.adb_path[0]) {
            char state[32], detail[2048];
            ps_adb_status(app->cfg.adb_path, state, sizeof(state), detail, sizeof(detail));
            pthread_mutex_lock(&app->state_lock);
            snprintf(app->adb_state, sizeof(app->adb_state), "%s", state);
            snprintf(app->adb_detail, sizeof(app->adb_detail), "%s", detail);
            if (strcmp(state, "device") == 0 && !app->adb_setup_done) {
                char msg[1024];
                if (ps_adb_setup(app->cfg.adb_path, app->cfg.usb_camera_port,
                                 app->cfg.usb_viewer_port, msg, sizeof(msg)) == 0) {
                    app->adb_setup_done = 1;
                    snprintf(app->adb_state, sizeof(app->adb_state), "ready");
                    snprintf(app->adb_detail, sizeof(app->adb_detail), "%s", msg);
                } else {
                    snprintf(app->adb_state, sizeof(app->adb_state), "error");
                    snprintf(app->adb_detail, sizeof(app->adb_detail), "%s", msg);
                }
            } else if (strcmp(state, "device") != 0) {
                app->adb_setup_done = 0;
            }
            pthread_mutex_unlock(&app->state_lock);
        }
        ps_sleep_ms(5000);
    }
    return NULL;
}

static void on_signal(int sig) {
    (void)sig;
    if (g_app) g_app->stop = 1;
}

int ps_load_config(PsConfig *cfg, int argc, char **argv) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->port = 8020;
    cfg->web_dir[0] = '\0';
    cfg->data_dir[0] = '\0';
    cfg->proc_w = PS_PROC_W;
    cfg->proc_h = PS_PROC_H;
    cfg->stream_fps = 12;
    cfg->jpeg_quality = 75;
    cfg->red_frames = 2;
    cfg->green_frames = 8;
    cfg->risk_red = 0.72;
    cfg->risk_amber = 0.38;
    cfg->usb_camera_port = 8080;
    cfg->usb_viewer_port = 8020;
    cfg->transport = PS_TRANSPORT_LAN;

    const char *e;
    if ((e = getenv("PS_PORT"))) cfg->port = atoi(e);
    if ((e = getenv("PS_CAMERA_URL"))) snprintf(cfg->camera_url, sizeof(cfg->camera_url), "%s", e);
    if ((e = getenv("PS_WEB_DIR"))) snprintf(cfg->web_dir, sizeof(cfg->web_dir), "%s", e);
    if ((e = getenv("PS_DATA_DIR"))) snprintf(cfg->data_dir, sizeof(cfg->data_dir), "%s", e);
    if ((e = getenv("PS_ADB_PATH"))) snprintf(cfg->adb_path, sizeof(cfg->adb_path), "%s", e);
    if ((e = getenv("PS_TRANSPORT"))) cfg->transport = strcmp(e, "usb") == 0 ? PS_TRANSPORT_USB : PS_TRANSPORT_LAN;
    if ((e = getenv("PS_FAKE"))) cfg->fake = atoi(e) != 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            cfg->port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--camera") == 0 && i + 1 < argc) {
            snprintf(cfg->camera_url, sizeof(cfg->camera_url), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--web") == 0 && i + 1 < argc) {
            snprintf(cfg->web_dir, sizeof(cfg->web_dir), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            snprintf(cfg->data_dir, sizeof(cfg->data_dir), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--adb") == 0 && i + 1 < argc) {
            snprintf(cfg->adb_path, sizeof(cfg->adb_path), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--transport") == 0 && i + 1 < argc) {
            cfg->transport = strcmp(argv[++i], "usb") == 0 ? PS_TRANSPORT_USB : PS_TRANSPORT_LAN;
        } else if (strcmp(argv[i], "--fake") == 0) {
            cfg->fake = 1;
        } else if (strcmp(argv[i], "--proc") == 0 && i + 1 < argc) {
            int w = 0, h = 0;
            if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w >= 64 && h >= 48 && w <= 640 && h <= 480) {
                cfg->proc_w = w; cfg->proc_h = h;
            }
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            cfg->stream_fps = atoi(argv[++i]);
            if (cfg->stream_fps < 1) cfg->stream_fps = 1;
            if (cfg->stream_fps > 60) cfg->stream_fps = 60;
        } else if (strcmp(argv[i], "--quality") == 0 && i + 1 < argc) {
            cfg->jpeg_quality = atoi(argv[++i]);
            if (cfg->jpeg_quality < 30) cfg->jpeg_quality = 30;
            if (cfg->jpeg_quality > 95) cfg->jpeg_quality = 95;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("pasillo-seguro %s\n", PS_VERSION);
            printf("  --port N        puerto HTTP (default 8020)\n");
            printf("  --camera URL    IP/URL de IP Webcam\n");
            printf("  --web DIR       directorio web (default web)\n");
            printf("  --data DIR      directorio de datos (default data)\n");
            printf("  --transport usb|lan\n");
            printf("  --adb PATH      binario adb\n");
            printf("  --fake          fuente sintetica para pruebas\n");
            printf("  --proc WxH      resolucion de proceso (default 192x144)\n");
            printf("  --fps N         fps de salida (default 12)\n");
            printf("  --quality N     calidad JPEG (default 75)\n");
            exit(0);
        }
    }
    if (cfg->port == 8000 || cfg->port == 8010) {
        fprintf(stderr, "puerto %d reservado por otro servicio; usa 8020\n", cfg->port);
        return -1;
    }
    if (cfg->port < 1 || cfg->port > 65535) {
        fprintf(stderr, "puerto invalido\n");
        return -1;
    }
    if (!cfg->web_dir[0]) snprintf(cfg->web_dir, sizeof(cfg->web_dir), "web");
    if (!cfg->data_dir[0]) snprintf(cfg->data_dir, sizeof(cfg->data_dir), "data");
    if (!cfg->adb_path[0]) {
        if (ps_adb_find(cfg->adb_path, sizeof(cfg->adb_path)) != 0) {
            cfg->adb_path[0] = '\0';
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    PsApp app;
    memset(&app, 0, sizeof(app));
    if (ps_load_config(&app.cfg, argc, argv) != 0) return 2;

    pthread_mutex_init(&app.hub.lock, NULL);
    pthread_cond_init(&app.hub.cond, NULL);
    pthread_mutex_init(&app.state_lock, NULL);
    snprintf(app.hub.state, sizeof(app.hub.state), "idle");
    snprintf(app.hub.message, sizeof(app.hub.message), "esperando camara");
    snprintf(app.adb_state, sizeof(app.adb_state), "unknown");
    if (app.cfg.adb_path[0]) {
        snprintf(app.adb_detail, sizeof(app.adb_detail), "%s", app.cfg.adb_path);
    }

    if (ps_vision_init(&app.vision, app.cfg.proc_w, app.cfg.proc_h) != 0) {
        fprintf(stderr, "no se pudo inicializar vision\n");
        return 3;
    }

    g_app = &app;
    ps_log("pasillo-seguro %s puerto=%d web=%s data=%s adb=%s transport=%s fake=%d",
           PS_VERSION, app.cfg.port, app.cfg.web_dir, app.cfg.data_dir,
           app.cfg.adb_path[0] ? app.cfg.adb_path : "(none)",
           app.cfg.transport == PS_TRANSPORT_USB ? "usb" : "lan", app.cfg.fake);

    pthread_create(&app.capture_thread, NULL, capture_thread, &app);
    pthread_create(&app.adb_thread, NULL, adb_thread, &app);

    int rc = ps_server_run(&app);
    app.stop = 1;
    pthread_join(app.capture_thread, NULL);
    pthread_join(app.adb_thread, NULL);
    ps_vision_free(&app.vision);
    free(app.hub.jpeg);
    pthread_mutex_destroy(&app.hub.lock);
    pthread_cond_destroy(&app.hub.cond);
    pthread_mutex_destroy(&app.state_lock);
    ps_log("fin (rc=%d)", rc);
    return rc == 0 ? 0 : 4;
}
