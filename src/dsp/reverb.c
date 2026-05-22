/* Ambiotica reverb — Freeverb-scaled topology.
 *
 * 8 parallel damped combs -> 4 serial allpasses, per channel. True stereo
 * via +37-sample spread on the R channel. Decay knob maps to comb feedback
 * via a perceptual ~T60 curve (knob^0.4) so the low/mid knob range covers
 * "short room" -> "long hall" rather than compressing everything into the top.
 *
 * Phase 2: static plate. Phase 3 will add LFO modulation on the comb delay
 * read positions — that's where Slö's "breathing" character actually lives.
 */
#include "reverb.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define R_COMB 8
#define R_AP 4
#define R_STEREO_SPREAD 37

/* Comb lengths (samples @ 44.1 kHz) span ~50–74 ms. Chosen odd, spread for
 * mutually incommensurate modes, scaled up from Freeverb's small-room values
 * for ambient-pad density. */
static const int R_COMB_BASE[R_COMB] = {
    2237, 2381, 2557, 2719, 2861, 2999, 3137, 3271
};
/* Allpass lengths ~8–14 ms. Four stages of serial diffusion smear the comb
 * sum into a continuous tail. */
static const int R_AP_BASE[R_AP] = {
    347, 421, 511, 619
};

struct reverb_s {
    /* L channel state. */
    float *comb_buf_L[R_COMB];
    int    comb_pos_L[R_COMB];
    float  comb_damp_L[R_COMB];   /* 1-pole LPF state inside FB */

    float *ap_buf_L[R_AP];
    int    ap_pos_L[R_AP];

    /* R channel — delays are R_COMB_BASE[i] + R_STEREO_SPREAD, APs too. */
    float *comb_buf_R[R_COMB];
    int    comb_pos_R[R_COMB];
    float  comb_damp_R[R_COMB];

    float *ap_buf_R[R_AP];
    int    ap_pos_R[R_AP];

    /* Tunables. */
    float fb;          /* comb feedback gain — set by reverb_set_decay */
    float damp_a;      /* damping LPF coef (0..1, higher = darker) */
    float ap_g;        /* allpass diffusion gain */
    float input_gain;  /* attenuates input before injection into 8 combs */
    float wet_gain;    /* final scaling of summed-then-diffused output */
};

reverb_t* reverb_create(void) {
    reverb_t *r = (reverb_t*)calloc(1, sizeof(reverb_t));
    if (!r) return NULL;
    for (int i = 0; i < R_COMB; i++) {
        r->comb_buf_L[i] = (float*)calloc((size_t)R_COMB_BASE[i], sizeof(float));
        r->comb_buf_R[i] = (float*)calloc((size_t)(R_COMB_BASE[i] + R_STEREO_SPREAD), sizeof(float));
        if (!r->comb_buf_L[i] || !r->comb_buf_R[i]) { reverb_destroy(r); return NULL; }
    }
    for (int i = 0; i < R_AP; i++) {
        r->ap_buf_L[i] = (float*)calloc((size_t)R_AP_BASE[i], sizeof(float));
        r->ap_buf_R[i] = (float*)calloc((size_t)(R_AP_BASE[i] + R_STEREO_SPREAD), sizeof(float));
        if (!r->ap_buf_L[i] || !r->ap_buf_R[i]) { reverb_destroy(r); return NULL; }
    }
    r->fb = 0.78f;            /* matches decay=0.25 with the new curve */
    r->damp_a = 0.55f;        /* fc ~4 kHz @ 44.1 kHz (darker than Freeverb default) */
    r->ap_g = 0.70f;
    r->input_gain = 0.40f;    /* attenuate so 8 parallel combs don't pile hot */
    r->wet_gain = 0.18f;      /* final scaling — tune by ear */
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
    /* Perceptual ~T60 curve. knob^0.4 expands the low-mid range so a 50%
     * knob lands in "long hall" rather than "short room". Endpoints:
     *   0.00 -> fb 0.50 (~250 ms tail)
     *   0.25 -> fb 0.78 (~1 s)
     *   0.50 -> fb 0.87 (~3 s)
     *   0.75 -> fb 0.93 (~6 s)
     *   1.00 -> fb 0.99 (15 s+, near-infinite) */
    float curve = powf(decay_0_1, 0.4f);
    r->fb = 0.50f + 0.49f * curve;
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

void reverb_process(reverb_t *r,
                    const float *in_l, const float *in_r,
                    float *out_l, float *out_r,
                    int frames) {
    if (!r || frames <= 0) return;

    const float fb     = r->fb;
    const float damp_a = r->damp_a;
    const float damp_b = 1.0f - damp_a;
    const float ap_g   = r->ap_g;
    const float in_g   = r->input_gain;
    const float out_g  = r->wet_gain;

    for (int n = 0; n < frames; n++) {
        float xL = in_l[n] * in_g;
        float xR = in_r[n] * in_g;

        /* Sum parallel damped combs. */
        float sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < R_COMB; i++) {
            int pL = r->comb_pos_L[i];
            float yL = r->comb_buf_L[i][pL];
            float fL = damp_b * yL + damp_a * r->comb_damp_L[i];
            r->comb_damp_L[i] = fL;
            r->comb_buf_L[i][pL] = xL + fb * fL;
            pL++; if (pL >= R_COMB_BASE[i]) pL = 0;
            r->comb_pos_L[i] = pL;
            sumL += yL;

            int pR = r->comb_pos_R[i];
            float yR = r->comb_buf_R[i][pR];
            float fR = damp_b * yR + damp_a * r->comb_damp_R[i];
            r->comb_damp_R[i] = fR;
            r->comb_buf_R[i][pR] = xR + fb * fR;
            pR++; if (pR >= R_COMB_BASE[i] + R_STEREO_SPREAD) pR = 0;
            r->comb_pos_R[i] = pR;
            sumR += yR;
        }

        /* Serial allpass diffusion. */
        float oL = sumL, oR = sumR;
        for (int i = 0; i < R_AP; i++) {
            oL = ap_tick(r->ap_buf_L[i], R_AP_BASE[i],
                         &r->ap_pos_L[i], ap_g, oL);
            oR = ap_tick(r->ap_buf_R[i], R_AP_BASE[i] + R_STEREO_SPREAD,
                         &r->ap_pos_R[i], ap_g, oR);
        }

        out_l[n] = oL * out_g;
        out_r[n] = oR * out_g;
    }
}
