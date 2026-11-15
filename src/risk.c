#include "pasillo.h"

#include <math.h>

static double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

double ps_track_risk(const PsTrack *t, int w, int h, const PsConfig *cfg) {
    (void)cfg;
    if (!t || !t->active || w <= 0 || h <= 0) return 0.0;

    double cx = (double)t->x + (double)t->w * 0.5;
    double bottom = (double)t->y + (double)t->h;
    double in_corridor = 0.0;
    if (cx > 0.18 * w && cx < 0.82 * w && bottom > 0.40 * h) {
        in_corridor = 1.0;
    }
    double proximity = clamp01((bottom - 0.40 * h) / (0.60 * h));

    double ttc_score = 0.0;
    if (t->ttc > 0.0 && t->ttc < 2.0) {
        ttc_score = clamp01(1.0 - t->ttc / 2.0);
    } else if (t->ttc <= 0.0) {
        ttc_score = 1.0;
    }
    double growth_score = clamp01(t->growth / 0.9);
    double lateral_score = clamp01(fabs(t->vx) / (0.10 * (double)w));
    double area_score = clamp01(((double)t->w * (double)t->h) / (0.08 * (double)w * (double)h));

    double risk = 0.28 * in_corridor +
                  0.22 * proximity +
                  0.24 * ttc_score +
                  0.10 * growth_score +
                  0.08 * lateral_score +
                  0.08 * area_score;

    if (cx < 0.08 * w || cx > 0.92 * w) risk *= 0.60;
    if ((double)t->w * (double)t->h < 0.0015 * (double)w * (double)h) risk *= 0.50;
    return clamp01(risk);
}

PsRisk ps_risk_update(PsVision *v, double risk, const PsConfig *cfg) {
    if (!v || !cfg) return PS_RISK_GREEN;
    if (risk < 0.0) risk = 0.0;
    if (risk > 1.0) risk = 1.0;
    v->risk = risk;

    if (risk >= cfg->risk_red) {
        v->red_count++;
        v->green_count = 0;
        if (v->red_count >= cfg->red_frames) v->level = PS_RISK_RED;
    } else if (risk <= cfg->risk_amber) {
        v->green_count++;
        v->red_count = 0;
        if (v->green_count >= cfg->green_frames) v->level = PS_RISK_GREEN;
    } else {
        v->red_count = 0;
        v->green_count = 0;
        if (v->level != PS_RISK_RED) v->level = PS_RISK_AMBER;
    }
    return v->level;
}
