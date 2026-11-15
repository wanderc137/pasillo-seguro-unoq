#include "pasillo.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_normalize(void) {
    char v[PS_MAX_URL], p[PS_MAX_URL], e[256];
    assert(ps_normalize_url("192.168.1.50", v, sizeof(v), p, sizeof(p), e, sizeof(e)) == 0);
    assert(strcmp(v, "http://192.168.1.50:8080/video") == 0);
    assert(strcmp(p, "http://192.168.1.50:8080/photo.jpg") == 0);

    assert(ps_normalize_url("192.168.1.50:8080", v, sizeof(v), p, sizeof(p), e, sizeof(e)) == 0);
    assert(strcmp(v, "http://192.168.1.50:8080/video") == 0);

    assert(ps_normalize_url("http://10.0.0.2:9090/video", v, sizeof(v), p, sizeof(p), e, sizeof(e)) == 0);
    assert(strcmp(v, "http://10.0.0.2:9090/video") == 0);

    assert(ps_normalize_url("10.0.0.2/video", v, sizeof(v), p, sizeof(p), e, sizeof(e)) == 0);
    assert(strcmp(v, "http://10.0.0.2:8080/video") == 0);

    assert(ps_normalize_url("10.0.0.2/photo.jpg", v, sizeof(v), p, sizeof(p), e, sizeof(e)) == 0);
    assert(strcmp(v, "http://10.0.0.2:8080/video") == 0);

    assert(ps_normalize_url("  http://10.0.0.2:9090/video  ", v, sizeof(v), p, sizeof(p), e, sizeof(e)) == 0);
    assert(strcmp(v, "http://10.0.0.2:9090/video") == 0);

    assert(ps_normalize_url("", v, sizeof(v), p, sizeof(p), e, sizeof(e)) != 0);
    assert(ps_normalize_url("ftp://x", v, sizeof(v), p, sizeof(p), e, sizeof(e)) != 0);
    assert(ps_normalize_url("10.0.0.2:99999", v, sizeof(v), p, sizeof(p), e, sizeof(e)) != 0);
    assert(ps_normalize_url("10.0.0.2:abc", v, sizeof(v), p, sizeof(p), e, sizeof(e)) != 0);
    assert(ps_normalize_url("https://10.0.0.2", v, sizeof(v), p, sizeof(p), e, sizeof(e)) != 0);
    printf("normalize: OK\n");
}

static void test_risk(void) {
    PsConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.risk_red = 0.72;
    cfg.risk_amber = 0.38;
    cfg.red_frames = 2;
    cfg.green_frames = 3;

    PsTrack high;
    memset(&high, 0, sizeof(high));
    high.active = 1;
    high.x = 20; high.y = 28; high.w = 20; high.h = 18;
    high.growth = 1.2; high.ttc = 0.4; high.vx = 8.0;
    double rh = ps_track_risk(&high, 64, 48, &cfg);
    assert(rh > 0.55);

    PsTrack low;
    memset(&low, 0, sizeof(low));
    low.active = 1;
    low.x = 0; low.y = 0; low.w = 5; low.h = 5;
    low.growth = 0; low.ttc = 1e9; low.vx = 0;
    double rl = ps_track_risk(&low, 64, 48, &cfg);
    assert(rl < 0.30);

    PsVision v;
    memset(&v, 0, sizeof(v));
    v.level = PS_RISK_GREEN;
    assert(ps_risk_update(&v, 0.90, &cfg) == PS_RISK_GREEN);
    assert(ps_risk_update(&v, 0.90, &cfg) == PS_RISK_RED);
    assert(ps_risk_update(&v, 0.10, &cfg) == PS_RISK_RED);
    assert(ps_risk_update(&v, 0.10, &cfg) == PS_RISK_RED);
    assert(ps_risk_update(&v, 0.10, &cfg) == PS_RISK_GREEN);
    printf("risk: OK (alto=%.2f bajo=%.2f)\n", rh, rl);
}

static void put_rect(uint8_t *rgb, int w, int h, int x, int y, int rw, int rh,
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

static void test_vision(void) {
    PsVision v;
    assert(ps_vision_init(&v, 64, 48) == 0);
    PsConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.risk_red = 0.72;
    cfg.risk_amber = 0.38;
    cfg.red_frames = 2;
    cfg.green_frames = 3;

    int w = 128, h = 96;
    uint8_t *rgb = malloc((size_t)w * h * 3);
    assert(rgb);
    double max_risk = 0.0;
    int max_objects = 0;
    for (int f = 0; f < 14; f++) {
        for (int i = 0; i < w * h; i++) {
            rgb[i * 3 + 0] = 18;
            rgb[i * 3 + 1] = 24;
            rgb[i * 3 + 2] = 30;
        }
        int rw = 18 + f * 2;
        int rh = 28 + f * 3;
        int x = 55 - rw / 2;
        int y = 88 - rh;
        put_rect(rgb, w, h, x, y, rw, rh, 230, 90, 40);
        put_rect(rgb, w, h, x + rw / 3, y + 6, rw / 3, rh / 4, 250, 210, 170);
        ps_vision_process(&v, rgb, w, h, &cfg);
        if (v.risk > max_risk) max_risk = v.risk;
        if (v.objects > max_objects) max_objects = v.objects;
    }
    printf("vision: objects=%d risk=%.2f level=%s\n",
           max_objects, max_risk, ps_risk_name(v.level));
    assert(max_objects > 0);
    assert(max_risk > 0.15);
    free(rgb);
    ps_vision_free(&v);
}

int main(void) {
    test_normalize();
    test_risk();
    test_vision();
    printf("test_core: TODO OK\n");
    return 0;
}
