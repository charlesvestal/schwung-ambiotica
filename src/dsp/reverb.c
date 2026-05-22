/* 4-delay Hadamard FDN reverb + 2-stage allpass pre-diffuser. */
#include "reverb.h"

#include <stdlib.h>
#include <string.h>

#define R_N 4
#define R_AP 2

/* Delay lengths in samples at 44.1 kHz, chosen near-prime / mutually
 * incommensurate for good modal density. Range ~36 ms – 66 ms. */
static const int R_D_LEN[R_N]   = { 1607, 2053, 2477, 2917 };
/* Allpass lengths ~7.9 ms / 9.5 ms. Standard ratios. */
static const int R_AP_LEN[R_AP] = { 347, 421 };

struct reverb_s {
    /* Delay lines + circular write positions. */
    float *d_buf[R_N];
    int    d_pos[R_N];

    /* 1-pole damping LPF state, one per delay's feedback. */
    float  damp_state[R_N];

    /* Allpass pre-diffuser state. */
    float *ap_buf[R_AP];
    int    ap_pos[R_AP];

    /* Tunables. */
    float fb;       /* feedback gain, set by reverb_set_decay() */
    float damp_a;   /* damping LPF coefficient (0..1, higher = darker) */
    float ap_g;     /* allpass diffusion gain */
};

reverb_t* reverb_create(void) {
    reverb_t *r = (reverb_t*)calloc(1, sizeof(reverb_t));
    if (!r) return NULL;
    for (int i = 0; i < R_N; i++) {
        r->d_buf[i] = (float*)calloc((size_t)R_D_LEN[i], sizeof(float));
        if (!r->d_buf[i]) { reverb_destroy(r); return NULL; }
    }
    for (int i = 0; i < R_AP; i++) {
        r->ap_buf[i] = (float*)calloc((size_t)R_AP_LEN[i], sizeof(float));
        if (!r->ap_buf[i]) { reverb_destroy(r); return NULL; }
    }
    /* Defaults: ~moderate tail, ~5 kHz damping, classic 0.7 allpass. */
    r->fb = 0.70f;
    r->damp_a = 0.49f;   /* fc ~5 kHz at 44.1 kHz */
    r->ap_g = 0.70f;
    return r;
}

void reverb_destroy(reverb_t *r) {
    if (!r) return;
    for (int i = 0; i < R_N; i++)  free(r->d_buf[i]);
    for (int i = 0; i < R_AP; i++) free(r->ap_buf[i]);
    free(r);
}

void reverb_set_decay(reverb_t *r, float decay_0_1) {
    if (!r) return;
    if (decay_0_1 < 0.0f) decay_0_1 = 0.0f;
    if (decay_0_1 > 1.0f) decay_0_1 = 1.0f;
    /* Exponential feel so the lush end of the range has more travel.
     * 0.00 -> 0.500 (short),  0.50 -> 0.621,  1.00 -> 0.985 (very lush). */
    float d2 = decay_0_1 * decay_0_1;
    r->fb = 0.500f + 0.485f * d2;
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
    const float ap_g   = r->ap_g;

    for (int n = 0; n < frames; n++) {
        /* Mono sum at half-gain into pre-diffuser. */
        float x = 0.5f * (in_l[n] + in_r[n]);
        x = ap_tick(r->ap_buf[0], R_AP_LEN[0], &r->ap_pos[0], ap_g, x);
        x = ap_tick(r->ap_buf[1], R_AP_LEN[1], &r->ap_pos[1], ap_g, x);

        /* Read current tap from each delay. */
        float t0 = r->d_buf[0][r->d_pos[0]];
        float t1 = r->d_buf[1][r->d_pos[1]];
        float t2 = r->d_buf[2][r->d_pos[2]];
        float t3 = r->d_buf[3][r->d_pos[3]];

        /* 1-pole damping LPF on each feedback path. */
        float ld0 = (1.0f - damp_a) * t0 + damp_a * r->damp_state[0]; r->damp_state[0] = ld0;
        float ld1 = (1.0f - damp_a) * t1 + damp_a * r->damp_state[1]; r->damp_state[1] = ld1;
        float ld2 = (1.0f - damp_a) * t2 + damp_a * r->damp_state[2]; r->damp_state[2] = ld2;
        float ld3 = (1.0f - damp_a) * t3 + damp_a * r->damp_state[3]; r->damp_state[3] = ld3;

        /* Hadamard 4x4 mix (normalized by 1/2). */
        float m0 = 0.5f * ( ld0 + ld1 + ld2 + ld3);
        float m1 = 0.5f * ( ld0 - ld1 + ld2 - ld3);
        float m2 = 0.5f * ( ld0 + ld1 - ld2 - ld3);
        float m3 = 0.5f * ( ld0 - ld1 - ld2 + ld3);

        /* Inject input + scaled feedback. */
        r->d_buf[0][r->d_pos[0]] = x + fb * m0;
        r->d_buf[1][r->d_pos[1]] = x + fb * m1;
        r->d_buf[2][r->d_pos[2]] = x + fb * m2;
        r->d_buf[3][r->d_pos[3]] = x + fb * m3;

        /* Advance write positions. */
        if (++r->d_pos[0] >= R_D_LEN[0]) r->d_pos[0] = 0;
        if (++r->d_pos[1] >= R_D_LEN[1]) r->d_pos[1] = 0;
        if (++r->d_pos[2] >= R_D_LEN[2]) r->d_pos[2] = 0;
        if (++r->d_pos[3] >= R_D_LEN[3]) r->d_pos[3] = 0;

        /* Decorrelated stereo: (d0 - d2) vs (d1 - d3). */
        out_l[n] = t0 - t2;
        out_r[n] = t1 - t3;
    }
}
