#include "pasillo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 5x7 bitmap font, one byte per column, bit 0 = top row. */
static const uint8_t FONT5X7[][5] = {
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x36,0x36,0x00,0x00},
    {0x23,0x13,0x08,0x64,0x62}, {0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02}, {0x08,0x08,0x08,0x08,0x08}
};

static int font_index(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + (c - '0');
    if (c == ' ') return 36;
    if (c == ':') return 37;
    if (c == '%') return 38;
    if (c == '.') return 39;
    if (c == '/') return 40;
    if (c == '-') return 41;
    return 36;
}

void ps_vision_draw_text(uint8_t *rgb, int w, int h, int x, int y,
                         const char *text, int scale,
                         uint8_t r, uint8_t g, uint8_t b) {
    if (!rgb || !text || scale < 1) return;
    int cx = x;
    for (const char *p = text; *p; p++) {
        int fi = font_index((unsigned char)*p);
        for (int col = 0; col < 5; col++) {
            uint8_t bits = FONT5X7[fi][col];
            for (int row = 0; row < 7; row++) {
                if (bits & (1u << row)) {
                    for (int dy = 0; dy < scale; dy++) {
                        int py = y + row * scale + dy;
                        if (py < 0 || py >= h) continue;
                        for (int dx = 0; dx < scale; dx++) {
                            int px = cx + col * scale + dx;
                            if (px < 0 || px >= w) continue;
                            uint8_t *q = rgb + ((size_t)py * (size_t)w + (size_t)px) * 3;
                            q[0] = r; q[1] = g; q[2] = b;
                        }
                    }
                }
            }
        }
        cx += 6 * scale;
    }
}

static void fill_rect(uint8_t *rgb, int w, int h, int x, int y, int rw, int rh,
                      uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0) { rw += x; x = 0; }
    if (y < 0) { rh += y; y = 0; }
    if (x + rw > w) rw = w - x;
    if (y + rh > h) rh = h - y;
    if (rw <= 0 || rh <= 0) return;
    for (int yy = y; yy < y + rh; yy++) {
        uint8_t *row = rgb + ((size_t)yy * (size_t)w + (size_t)x) * 3;
        for (int xx = 0; xx < rw; xx++) {
            row[xx * 3 + 0] = r;
            row[xx * 3 + 1] = g;
            row[xx * 3 + 2] = b;
        }
    }
}

static void draw_box(uint8_t *rgb, int w, int h, int x, int y, int bw, int bh,
                     int thick, uint8_t r, uint8_t g, uint8_t b) {
    fill_rect(rgb, w, h, x, y, bw, thick, r, g, b);
    fill_rect(rgb, w, h, x, y + bh - thick, bw, thick, r, g, b);
    fill_rect(rgb, w, h, x, y, thick, bh, r, g, b);
    fill_rect(rgb, w, h, x + bw - thick, y, thick, bh, r, g, b);
}

int ps_vision_init(PsVision *v, int w, int h) {
    if (!v || w <= 0 || h <= 0) return -1;
    memset(v, 0, sizeof(*v));
    v->w = w;
    v->h = h;
    size_t n = (size_t)w * (size_t)h;
    v->gray = calloc(n, 1);
    v->prev = calloc(n, 1);
    v->diff = calloc(n, 1);
    v->mask = calloc(n, 1);
    if (!v->gray || !v->prev || !v->diff || !v->mask) {
        ps_vision_free(v);
        return -1;
    }
    v->level = PS_RISK_GREEN;
    v->next_id = 1;
    return 0;
}

void ps_vision_free(PsVision *v) {
    if (!v) return;
    free(v->gray);
    free(v->prev);
    free(v->diff);
    free(v->mask);
    v->gray = v->prev = v->diff = v->mask = NULL;
}

static void downscale_gray(const uint8_t *rgb, int w, int h,
                           uint8_t *gray, int pw, int ph) {
    for (int y = 0; y < ph; y++) {
        int y0 = y * h / ph;
        int y1 = (y + 1) * h / ph;
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > h) y1 = h;
        for (int x = 0; x < pw; x++) {
            int x0 = x * w / pw;
            int x1 = (x + 1) * w / pw;
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > w) x1 = w;
            uint32_t sr = 0, sg = 0, sb = 0;
            uint32_t cnt = 0;
            for (int yy = y0; yy < y1; yy++) {
                const uint8_t *row = rgb + ((size_t)yy * (size_t)w + (size_t)x0) * 3;
                for (int xx = x0; xx < x1; xx++) {
                    sr += row[0]; sg += row[1]; sb += row[2];
                    row += 3;
                    cnt++;
                }
            }
            if (!cnt) cnt = 1;
            uint32_t r = sr / cnt, g = sg / cnt, b = sb / cnt;
            gray[(size_t)y * (size_t)pw + (size_t)x] =
                (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
        }
    }
}

static void blur3(const uint8_t *in, uint8_t *out, uint8_t *tmp, int w, int h) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sum = 0, cnt = 0;
            for (int dx = -1; dx <= 1; dx++) {
                int xx = x + dx;
                if (xx < 0 || xx >= w) continue;
                sum += in[(size_t)y * (size_t)w + (size_t)xx];
                cnt++;
            }
            tmp[(size_t)y * (size_t)w + (size_t)x] = (uint8_t)(sum / (cnt ? cnt : 1));
        }
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sum = 0, cnt = 0;
            for (int dy = -1; dy <= 1; dy++) {
                int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                sum += tmp[(size_t)yy * (size_t)w + (size_t)x];
                cnt++;
            }
            out[(size_t)y * (size_t)w + (size_t)x] = (uint8_t)(sum / (cnt ? cnt : 1));
        }
    }
}

typedef struct {
    int x, y, w, h, area;
    double cx, cy;
} PsDet;

static int find_components(const uint8_t *mask, int w, int h,
                           PsDet *dets, int max_dets) {
    uint8_t *seen = calloc((size_t)w * (size_t)h, 1);
    int *stack = malloc((size_t)w * (size_t)h * sizeof(int));
    if (!seen || !stack) {
        free(seen);
        free(stack);
        return 0;
    }
    int nd = 0;
    int min_area = (w * h) / 500;
    if (min_area < 18) min_area = 18;
    for (int y = 0; y < h && nd < max_dets; y++) {
        for (int x = 0; x < w && nd < max_dets; x++) {
            size_t idx = (size_t)y * (size_t)w + (size_t)x;
            if (!mask[idx] || seen[idx]) continue;
            int sp = 0;
            stack[sp++] = (int)idx;
            seen[idx] = 1;
            int minx = x, maxx = x, miny = y, maxy = y;
            long sumx = 0, sumy = 0, area = 0;
            while (sp > 0) {
                int cur = stack[--sp];
                int cy = cur / w;
                int cx = cur % w;
                area++;
                sumx += cx; sumy += cy;
                if (cx < minx) minx = cx;
                if (cx > maxx) maxx = cx;
                if (cy < miny) miny = cy;
                if (cy > maxy) maxy = cy;
                const int dx[4] = {-1, 1, 0, 0};
                const int dy[4] = {0, 0, -1, 1};
                for (int k = 0; k < 4; k++) {
                    int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    size_t ni = (size_t)ny * (size_t)w + (size_t)nx;
                    if (mask[ni] && !seen[ni]) {
                        seen[ni] = 1;
                        stack[sp++] = (int)ni;
                    }
                }
            }
            int bw = maxx - minx + 1;
            int bh = maxy - miny + 1;
            if (area < min_area) continue;
            if (bw < 6 || bh < 6) continue;
            if (bw > w * 3 / 4 || bh > h * 3 / 4) continue;
            if (bw > bh * 6 || bh > bw * 6) continue;
            dets[nd].x = minx;
            dets[nd].y = miny;
            dets[nd].w = bw;
            dets[nd].h = bh;
            dets[nd].area = (int)area;
            dets[nd].cx = (double)sumx / (double)area;
            dets[nd].cy = (double)sumy / (double)area;
            nd++;
        }
    }
    free(seen);
    free(stack);
    return nd;
}

static void track_update(PsVision *v, PsDet *dets, int nd, double now) {
    int used_track[PS_MAX_TRACKS];
    int used_det[64];
    memset(used_track, 0, sizeof(used_track));
    memset(used_det, 0, sizeof(used_det));
    if (nd > 64) nd = 64;
    double dt = now - v->last_ts;
    if (dt < 1.0 / 60.0) dt = 1.0 / 60.0;
    if (dt > 1.0) dt = 1.0;

    for (int i = 0; i < nd; i++) {
        int best = -1;
        double bestd = 1e18;
        for (int j = 0; j < v->ntracks; j++) {
            PsTrack *t = &v->tracks[j];
            if (!t->active || used_track[j]) continue;
            double tcx = (double)t->x + (double)t->w * 0.5;
            double tcy = (double)t->y + (double)t->h * 0.5;
            double dx = tcx - dets[i].cx;
            double dy = tcy - dets[i].cy;
            double d = dx * dx + dy * dy;
            double reach = 0.35 * (double)(v->w > v->h ? v->w : v->h);
            if (d < reach * reach && d < bestd) {
                bestd = d;
                best = j;
            }
        }
        if (best >= 0) {
            PsTrack *t = &v->tracks[best];
            used_track[best] = 1;
            used_det[i] = 1;
            double oldcx = (double)t->x + (double)t->w * 0.5;
            double oldcy = (double)t->y + (double)t->h * 0.5;
            double oldarea = (double)t->w * (double)t->h;
            t->px = t->x; t->py = t->y; t->pw = t->w; t->ph = t->h;
            t->x = dets[i].x; t->y = dets[i].y;
            t->w = dets[i].w; t->h = dets[i].h;
            t->vx = (dets[i].cx - oldcx) / dt;
            t->vy = (dets[i].cy - oldcy) / dt;
            double newarea = (double)t->w * (double)t->h;
            if (oldarea > 1.0) {
                t->growth = (newarea / oldarea - 1.0) / dt;
                if (t->growth > 5.0) t->growth = 5.0;
                if (t->growth < -5.0) t->growth = -5.0;
            } else {
                t->growth = 0.0;
            }
            t->ttc = (t->growth > 0.05) ? (1.0 / t->growth) : 1e9;
            if (t->ttc > 10.0) t->ttc = 1e9;
            t->age++;
            t->missed = 0;
            t->active = true;
        } else if (!used_det[i]) {
            if (v->ntracks < PS_MAX_TRACKS) {
                PsTrack *t = &v->tracks[v->ntracks];
                memset(t, 0, sizeof(*t));
                t->id = v->next_id++;
                t->x = dets[i].x; t->y = dets[i].y;
                t->w = dets[i].w; t->h = dets[i].h;
                t->px = t->x; t->py = t->y; t->pw = t->w; t->ph = t->h;
                t->growth = 0.0;
                t->ttc = 1e9;
                t->age = 1;
                t->missed = 0;
                t->active = true;
                v->ntracks++;
                used_det[i] = 1;
            }
        }
    }
    for (int j = 0; j < v->ntracks; j++) {
        PsTrack *t = &v->tracks[j];
        if (!t->active || used_track[j]) continue;
        t->missed++;
        if (t->missed > 6) t->active = false;
    }
    int w = 0;
    for (int j = 0; j < v->ntracks; j++) {
        if (v->tracks[j].active) v->tracks[w++] = v->tracks[j];
    }
    v->ntracks = w;
}

void ps_vision_process(PsVision *v, uint8_t *rgb, int w, int h,
                       const PsConfig *cfg) {
    if (!v || !rgb || !cfg || w <= 0 || h <= 0) return;
    int pw = v->w, ph = v->h;
    downscale_gray(rgb, w, h, v->gray, pw, ph);
    double now = ps_now();
    v->frame_no++;
    if (v->frame_no == 1) {
        memcpy(v->prev, v->gray, (size_t)pw * (size_t)ph);
        v->last_ts = now;
        v->objects = 0;
        v->risk = 0.0;
        v->level = PS_RISK_GREEN;
        /* still draw the frame */
        goto draw;
    }

    /* per-pixel difference, blurred */
    size_t n = (size_t)pw * (size_t)ph;
    for (size_t i = 0; i < n; i++) {
        int d = (int)v->gray[i] - (int)v->prev[i];
        v->diff[i] = (uint8_t)(d < 0 ? -d : d);
    }
    blur3(v->diff, v->diff, v->mask, pw, ph);

    /* block motion + global compensation */
    memset(v->mask, 0, n);
    const int BS = 8;
    int bw = pw / BS, bh = ph / BS;
    PsDet dets[64];
    int nd = 0;
    if (bw >= 2 && bh >= 2) {
        int nb = bw * bh;
        int *bdx = calloc((size_t)nb, sizeof(int));
        int *bdy = calloc((size_t)nb, sizeof(int));
        int *bsad = calloc((size_t)nb, sizeof(int));
        if (bdx && bdy && bsad) {
            for (int by = 0; by < bh; by++) {
                for (int bx = 0; bx < bw; bx++) {
                    int best = 1 << 30, bd = 0, be = 0;
                    for (int dy = -3; dy <= 3; dy++) {
                        for (int dx = -3; dx <= 3; dx++) {
                            int sx = bx * BS + dx, sy = by * BS + dy;
                            if (sx < 0 || sy < 0 || sx + BS > pw || sy + BS > ph) continue;
                            int sad = 0;
                            for (int y = 0; y < BS; y++) {
                                const uint8_t *a = v->gray + (size_t)(by * BS + y) * pw + bx * BS;
                                const uint8_t *b = v->prev + (size_t)(sy + y) * pw + sx;
                                for (int x = 0; x < BS; x++) {
                                    int d = (int)a[x] - (int)b[x];
                                    sad += d < 0 ? -d : d;
                                }
                            }
                            if (sad < best) { best = sad; bd = dx; be = dy; }
                        }
                    }
                    bdx[by * bw + bx] = bd;
                    bdy[by * bw + bx] = be;
                    bsad[by * bw + bx] = best;
                }
            }
            int hist[7][7];
            memset(hist, 0, sizeof(hist));
            for (int i = 0; i < nb; i++) {
                if (bsad[i] < BS * BS * 10) {
                    int hx = bdx[i] + 3, hy = bdy[i] + 3;
                    if (hx >= 0 && hx < 7 && hy >= 0 && hy < 7) hist[hy][hx]++;
                }
            }
            int gdx = 0, gdy = 0, bestc = 0;
            for (int hy = 0; hy < 7; hy++) {
                for (int hx = 0; hx < 7; hx++) {
                    if (hist[hy][hx] > bestc) {
                        bestc = hist[hy][hx];
                        gdx = hx - 3; gdy = hy - 3;
                    }
                }
            }
            if (bestc < nb / 6) { gdx = 0; gdy = 0; }
            long sum = 0;
            int cnt = 0;
            for (int i = 0; i < nb; i++) { sum += bsad[i]; cnt++; }
            double mean = cnt ? (double)sum / cnt : 0.0;
            double var = 0.0;
            for (int i = 0; i < nb; i++) {
                double d = bsad[i] - mean;
                var += d * d;
            }
            double std = cnt ? sqrt(var / cnt) : 0.0;
            double thr = mean + 1.6 * std;
            if (thr < BS * BS * 4) thr = BS * BS * 4;
            if (thr > BS * BS * 28) thr = BS * BS * 28;
            for (int by = 0; by < bh; by++) {
                for (int bx = 0; bx < bw; bx++) {
                    int sx = bx * BS + gdx, sy = by * BS + gdy;
                    int sad = 0;
                    if (sx >= 0 && sy >= 0 && sx + BS <= pw && sy + BS <= ph) {
                        for (int y = 0; y < BS; y++) {
                            const uint8_t *a = v->gray + (size_t)(by * BS + y) * pw + bx * BS;
                            const uint8_t *b = v->prev + (size_t)(sy + y) * pw + sx;
                            for (int x = 0; x < BS; x++) {
                                int d = (int)a[x] - (int)b[x];
                                sad += d < 0 ? -d : d;
                            }
                        }
                    } else {
                        sad = (int)thr + 1;
                    }
                    if (sad > thr) {
                        for (int y = 0; y < BS; y++) {
                            uint8_t *m = v->mask + (size_t)(by * BS + y) * pw + bx * BS;
                            memset(m, 255, BS);
                        }
                    }
                }
            }
        }
        free(bdx); free(bdy); free(bsad);
    } else {
        memset(v->mask, 0, n);
    }

    /* clean mask: erode then dilate, use diff as temp */
    for (int y = 1; y < ph - 1; y++) {
        for (int x = 1; x < pw - 1; x++) {
            size_t i = (size_t)y * pw + x;
            int c = v->mask[i] != 0;
            c += v->mask[i - 1] != 0;
            c += v->mask[i + 1] != 0;
            c += v->mask[i - pw] != 0;
            c += v->mask[i + pw] != 0;
            v->diff[i] = (c >= 4) ? 255 : 0;
        }
    }
    for (int y = 1; y < ph - 1; y++) {
        for (int x = 1; x < pw - 1; x++) {
            size_t i = (size_t)y * pw + x;
            int c = v->diff[i] != 0;
            c += v->diff[i - 1] != 0;
            c += v->diff[i + 1] != 0;
            c += v->diff[i - pw] != 0;
            c += v->diff[i + pw] != 0;
            v->mask[i] = c ? 255 : 0;
        }
    }

    nd = find_components(v->mask, pw, ph, dets, 64);
    track_update(v, dets, nd, now);
    v->objects = 0;
    double maxrisk = 0.0;
    for (int i = 0; i < v->ntracks; i++) {
        PsTrack *t = &v->tracks[i];
        if (!t->active) continue;
        t->risk = ps_track_risk(t, pw, ph, cfg);
        if (t->risk > maxrisk) maxrisk = t->risk;
        v->objects++;
    }
    ps_risk_update(v, maxrisk, cfg);
    memcpy(v->prev, v->gray, n);
    v->last_ts = now;
    (void)nd;

draw:
    {
        int thick = h / 28;
        if (thick < 8) thick = 8;
        if (thick > 40) thick = 40;
        uint8_t br = 0, bg = 200, bb = 80;
        if (v->level == PS_RISK_RED) { br = 235; bg = 30; bb = 30; }
        else if (v->level == PS_RISK_AMBER) { br = 255; bg = 170; bb = 0; }
        fill_rect(rgb, w, h, 0, 0, w, thick, br, bg, bb);
        fill_rect(rgb, w, h, 0, h - thick, w, thick, br, bg, bb);
        fill_rect(rgb, w, h, 0, 0, thick, h, br, bg, bb);
        fill_rect(rgb, w, h, w - thick, 0, thick, h, br, bg, bb);

        int barh = h / 14;
        if (barh < 26) barh = 26;
        fill_rect(rgb, w, h, thick, thick, w - 2 * thick, barh, 10, 12, 16);
        const char *msg = (v->level == PS_RISK_RED) ? "STOP" :
                          (v->level == PS_RISK_AMBER) ? "PRECAUCION" : "AVANZA";
        int scale = h / 240;
        if (scale < 2) scale = 2;
        if (scale > 4) scale = 4;
        ps_vision_draw_text(rgb, w, h, thick + 8, thick + 5, msg, scale, br, bg, bb);
        char info[64];
        snprintf(info, sizeof(info), "OBJETOS:%d RIESGO:%d%%", v->objects, (int)(v->risk * 100));
        int tw = (int)strlen(info) * 6 * 2;
        int tx = w - thick - 8 - tw;
        if (tx > thick + 8) {
            ps_vision_draw_text(rgb, w, h, tx, thick + 7, info, 2, 230, 230, 230);
        }

        double sx = (double)w / (double)pw;
        double sy = (double)h / (double)ph;
        for (int i = 0; i < v->ntracks; i++) {
            PsTrack *t = &v->tracks[i];
            if (!t->active) continue;
            uint8_t tr = 0, tg = 200, tb = 80;
            if (t->risk >= 0.70) { tr = 235; tg = 30; tb = 30; }
            else if (t->risk >= 0.40) { tr = 255; tg = 170; tb = 0; }
            int bx = (int)(t->x * sx);
            int by = (int)(t->y * sy);
            int bwd = (int)(t->w * sx);
            int bhd = (int)(t->h * sy);
            draw_box(rgb, w, h, bx, by, bwd, bhd, 2, tr, tg, tb);
            int cx = bx + bwd / 2;
            int cy = by + bhd;
            fill_rect(rgb, w, h, cx - 10, cy, 20, 3, tr, tg, tb);
        }
    }
}
