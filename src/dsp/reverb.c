/* Ambiotica reverb — Freeverb-scaled topology with modulated comb reads.
 *
 * Phase 3 addition: each comb's read position is modulated by a slow sine
 * LFO with per-comb phase offset, giving the tail a "breathing" pitch
 * wobble. This is what gives the static plate the lush, evolving character
 * of Slö-style ambient reverb. mod_depth = 0 collapses to the static plate.
 */
#include "reverb.h"
#include "lfo.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TWO_PI (2.0f * (float)M_PI)

#define R_COMB           8
#define R_AP             4
#define R_STEREO_SPREAD  37
#define R_MOD_HEADROOM   256   /* extra samples per comb buffer for mod range */
#define R_SAMPLE_RATE    44100

/* Comb lengths (samples @ 44.1 kHz) span ~50–74 ms — scaled up from
 * Freeverb's small-room values for ambient-pad modal density. */
static const int R_COMB_BASE[R_COMB] = {
    2237, 2381, 2557, 2719, 2861, 2999, 3137, 3271
};
static const int R_AP_BASE[R_AP] = {
    347, 421, 511, 619
};

/* Initial LFO phase per comb — irregular placement so the 8 mods start
 * decorrelated rather than aligned. Phases drift apart further at runtime
 * because each comb runs at its own rate (see R_COMB_RATE_MULT). */
static const float R_COMB_PHASE[R_COMB] = {
    0.00f, 0.83f, 1.71f, 2.42f, 3.27f, 4.15f, 5.02f, 5.74f
};

/* Per-comb LFO rate multipliers — each comb runs at base_rate * mult[i].
 * Spread around 1.0 with irrational-ish ratios so the 8 LFOs never re-sync.
 * This is the "asynchronous LFO" character: tail evolves continuously
 * instead of pulsing in unison (= detune sound). */
static const float R_COMB_RATE_MULT[R_COMB] = {
    1.000f, 1.093f, 0.872f, 1.207f, 0.954f, 1.131f, 0.827f, 1.049f
};

struct reverb_s {
    /* L channel — combs */
    float *comb_buf_L[R_COMB];
    int    comb_buf_len_L[R_COMB];
    int    comb_write_L[R_COMB];
    float  comb_damp_L[R_COMB];

    float *ap_buf_L[R_AP];
    int    ap_buf_len_L[R_AP];
    int    ap_pos_L[R_AP];

    /* R channel — combs (length includes stereo spread offset) */
    float *comb_buf_R[R_COMB];
    int    comb_buf_len_R[R_COMB];
    int    comb_write_R[R_COMB];
    float  comb_damp_R[R_COMB];

    float *ap_buf_R[R_AP];
    int    ap_buf_len_R[R_AP];
    int    ap_pos_R[R_AP];

    /* Tunables */
    float fb_target,           fb_current;
    float damp_a;
    float ap_g;
    float input_gain;
    float wet_gain;

    /* Modulation — one LFO per comb, each at its own rate. */
    lfo_t lfo[R_COMB];
    float base_rate_hz;       /* user-set rate before per-comb multiplication */
    float mod_depth_target,    mod_depth_current;  /* 0..R_MOD_HEADROOM/2 */
    int   mod_shape;           /* 0=sine, 1=warp, 2=sink */

    /* Input highpass — stops sub-80-Hz energy from piling up in the comb
     * feedback (standard practice for algorithmic reverbs). One-pole. */
    float hp_state_L, hp_state_R;
};

/* a = exp(-2π × 80 / 44100) ≈ 0.9887. One-pole HPF: y = x - lowpass(x). */
#define R_HPF_LP_COEF  0.9887f

#define R_SMOOTH_COEF 0.9989f  /* ~20 ms time constant @ 44.1 kHz */

reverb_t* reverb_create(void) {
    reverb_t *r = (reverb_t*)calloc(1, sizeof(reverb_t));
    if (!r) return NULL;
    for (int i = 0; i < R_COMB; i++) {
        int Llen = R_COMB_BASE[i] + R_MOD_HEADROOM;
        int Rlen = R_COMB_BASE[i] + R_STEREO_SPREAD + R_MOD_HEADROOM;
        r->comb_buf_len_L[i] = Llen;
        r->comb_buf_len_R[i] = Rlen;
        r->comb_buf_L[i] = (float*)calloc((size_t)Llen, sizeof(float));
        r->comb_buf_R[i] = (float*)calloc((size_t)Rlen, sizeof(float));
        if (!r->comb_buf_L[i] || !r->comb_buf_R[i]) { reverb_destroy(r); return NULL; }
    }
    for (int i = 0; i < R_AP; i++) {
        int Llen = R_AP_BASE[i];
        int Rlen = R_AP_BASE[i] + R_STEREO_SPREAD;
        r->ap_buf_len_L[i] = Llen;
        r->ap_buf_len_R[i] = Rlen;
        r->ap_buf_L[i] = (float*)calloc((size_t)Llen, sizeof(float));
        r->ap_buf_R[i] = (float*)calloc((size_t)Rlen, sizeof(float));
        if (!r->ap_buf_L[i] || !r->ap_buf_R[i]) { reverb_destroy(r); return NULL; }
    }
    r->fb_target = 0.78f;
    r->fb_current = 0.78f;
    r->damp_a = 0.55f;
    r->ap_g = 0.70f;
    r->input_gain = 0.40f;
    r->wet_gain = 0.18f;
    r->base_rate_hz = 0.3f;
    r->mod_depth_target = 0.0f;
    r->mod_depth_current = 0.0f;
    r->mod_shape = 0;
    for (int i = 0; i < R_COMB; i++) {
        lfo_init(&r->lfo[i], R_SAMPLE_RATE);
        lfo_set_phase(&r->lfo[i], R_COMB_PHASE[i]);
        lfo_set_rate_hz(&r->lfo[i], r->base_rate_hz * R_COMB_RATE_MULT[i]);
    }
    return r;
}

void reverb_destroy(reverb_t *r) {
    if (!r) return;
    for (int i = 0; i < R_COMB; i++) { free(r->comb_buf_L[i]); free(r->comb_buf_R[i]); }
    for (int i = 0; i < R_AP; i++)   { free(r->ap_buf_L[i]);   free(r->ap_buf_R[i]);   }
    free(r);
}

void reverb_set_decay(reverb_t *r, float decay_0_1) {
    if (!r) return;
    if (decay_0_1 < 0.0f) decay_0_1 = 0.0f;
    if (decay_0_1 > 1.0f) decay_0_1 = 1.0f;
    float curve = powf(decay_0_1, 0.4f);
    r->fb_target = 0.50f + 0.49f * curve;
}

void reverb_set_mod_depth(reverb_t *r, float depth_0_1) {
    if (!r) return;
    if (depth_0_1 < 0.0f) depth_0_1 = 0.0f;
    if (depth_0_1 > 1.0f) depth_0_1 = 1.0f;
    /* Knob -> 0..45 samples (~±1 ms) with depth^0.7 curve.
     * Past ~50 samples per comb each delay becomes audibly retuned (the
     * "broken piano" failure mode). 45 samples × 44.1 kHz = ~1 ms ≈ a few
     * cents of detune per comb — perceived as movement, not pitch.
     * Concave curve puts more useful travel in the low/mid knob range. */
    float curve = powf(depth_0_1, 0.7f);
    r->mod_depth_target = curve * 45.0f;
}

void reverb_set_mod_rate(reverb_t *r, float rate_0_1) {
    if (!r) return;
    if (rate_0_1 < 0.0f) rate_0_1 = 0.0f;
    if (rate_0_1 > 1.0f) rate_0_1 = 1.0f;
    /* Log map: 0 -> 0.05 Hz, 0.5 -> ~0.63 Hz, 1 -> 8 Hz. */
    r->base_rate_hz = 0.05f * expf(rate_0_1 * 5.075f);
    for (int i = 0; i < R_COMB; i++) {
        lfo_set_rate_hz(&r->lfo[i], r->base_rate_hz * R_COMB_RATE_MULT[i]);
    }
}

void reverb_set_mod_rate_hz(reverb_t *r, float hz) {
    if (!r) return;
    if (hz < 0.0f) hz = 0.0f;
    r->base_rate_hz = hz;
    for (int i = 0; i < R_COMB; i++) {
        lfo_set_rate_hz(&r->lfo[i], r->base_rate_hz * R_COMB_RATE_MULT[i]);
    }
}

void reverb_set_mod_shape(reverb_t *r, int shape) {
    if (!r) return;
    if (shape < 0) shape = 0;
    if (shape > 2) shape = 2;
    r->mod_shape = shape;
}

static inline float ap_tick(float *buf, int len, int *pos, float g, float x) {
    int p = *pos;
    float s = buf[p];
    float v = x - g * s;
    float y = s + g * v;
    buf[p] = v;
    p++; if (p >= len) p = 0;
    *pos = p;
    return y;
}

static inline float comb_read_interp(const float *buf, int buf_len,
                                     int write_pos, float read_delay) {
    int d_int = (int)read_delay;
    float d_frac = read_delay - (float)d_int;
    int ridx = write_pos - d_int;
    if (ridx < 0) ridx += buf_len;
    int ridx_next = ridx - 1;
    if (ridx_next < 0) ridx_next += buf_len;
    return buf[ridx] * (1.0f - d_frac) + buf[ridx_next] * d_frac;
}

void reverb_process(reverb_t *r,
                    const float *in_l, const float *in_r,
                    float *out_l, float *out_r,
                    int frames) {
    if (!r || frames <= 0) return;

    const float damp_a = r->damp_a;
    const float damp_b = 1.0f - damp_a;
    const float ap_g   = r->ap_g;
    const float in_g   = r->input_gain;
    const float out_g  = r->wet_gain;

    /* Smoothed feedback + mod depth — both feed delay buffers, so abrupt
     * changes would echo forever in the reverb tail. */
    float fb_curr    = r->fb_current;
    float mod_curr   = r->mod_depth_current;
    const float fb_t   = r->fb_target;
    const float mod_t  = r->mod_depth_target;
    const float c      = R_SMOOTH_COEF;
    const float ic     = 1.0f - c;

    const float hp_a   = R_HPF_LP_COEF;
    const float hp_1ma = 1.0f - hp_a;
    float hp_L = r->hp_state_L;
    float hp_R = r->hp_state_R;

    for (int n = 0; n < frames; n++) {
        fb_curr  = c * fb_curr  + ic * fb_t;
        mod_curr = c * mod_curr + ic * mod_t;

        float xL = in_l[n] * in_g;
        float xR = in_r[n] * in_g;
        /* Input HPF — strip sub-~80 Hz before the comb network. */
        hp_L = hp_a * hp_L + hp_1ma * xL;
        hp_R = hp_a * hp_R + hp_1ma * xR;
        xL = xL - hp_L;
        xR = xR - hp_R;

        float sumL = 0.0f, sumR = 0.0f;
        const int shape = r->mod_shape;
        for (int i = 0; i < R_COMB; i++) {
            /* Each comb advances its own LFO at its own rate. */
            float raw_sin = lfo_tick_sine(&r->lfo[i]);
            float mod = lfo_shape_apply(shape, raw_sin) * mod_curr;

            /* L comb */
            float dL = (float)R_COMB_BASE[i] + mod;
            float yL = comb_read_interp(r->comb_buf_L[i],
                                        r->comb_buf_len_L[i],
                                        r->comb_write_L[i], dL);
            float fL = damp_b * yL + damp_a * r->comb_damp_L[i];
            r->comb_damp_L[i] = fL;
            r->comb_buf_L[i][r->comb_write_L[i]] = xL + fb_curr * fL;
            r->comb_write_L[i]++;
            if (r->comb_write_L[i] >= r->comb_buf_len_L[i]) r->comb_write_L[i] = 0;
            sumL += yL;

            /* R comb — same mod offset, longer base delay via stereo spread. */
            float dR = (float)(R_COMB_BASE[i] + R_STEREO_SPREAD) + mod;
            float yR = comb_read_interp(r->comb_buf_R[i],
                                        r->comb_buf_len_R[i],
                                        r->comb_write_R[i], dR);
            float fR = damp_b * yR + damp_a * r->comb_damp_R[i];
            r->comb_damp_R[i] = fR;
            r->comb_buf_R[i][r->comb_write_R[i]] = xR + fb_curr * fR;
            r->comb_write_R[i]++;
            if (r->comb_write_R[i] >= r->comb_buf_len_R[i]) r->comb_write_R[i] = 0;
            sumR += yR;
        }

        float oL = sumL, oR = sumR;
        for (int i = 0; i < R_AP; i++) {
            oL = ap_tick(r->ap_buf_L[i], r->ap_buf_len_L[i],
                         &r->ap_pos_L[i], ap_g, oL);
            oR = ap_tick(r->ap_buf_R[i], r->ap_buf_len_R[i],
                         &r->ap_pos_R[i], ap_g, oR);
        }

        out_l[n] = oL * out_g;
        out_r[n] = oR * out_g;
    }

    r->fb_current = fb_curr;
    r->mod_depth_current = mod_curr;
    r->hp_state_L = hp_L;
    r->hp_state_R = hp_R;
}
