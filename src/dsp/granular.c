/* Granular stage — 8-grain scheduler over a 2s capture ring. */
#include "granular.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TWO_PI (2.0f * (float)M_PI)

#define G_SAMPLE_RATE       44100
#define G_BUF_SAMPLES       (2 * G_SAMPLE_RATE)   /* 2 s stereo capture */
#define G_MAX_GRAINS        8
#define G_GRAIN_MIN_SAMPLES 441                   /* 10 ms */
#define G_GRAIN_MAX_SAMPLES 22050                 /* 500 ms */

/* Pitch quantization set — universally consonant intervals.
 * Octaves and perfect fifths are tonally neutral (work in major, minor,
 * modal, drone). Avoiding thirds/sixths keeps Ambiotica from accidentally
 * imposing a key on the source material. */
static const float G_PITCH_STEPS[5] = {
    1.0f,                  /* unison */
    2.0f,                  /* +1 octave */
    0.5f,                  /* −1 octave */
    1.4983070768766815f,   /* +P5  (2^(+7/12)) */
    0.6674199270738774f    /* −P5  (2^(−7/12)) */
};

typedef struct {
    int   active;
    float read_pos;       /* fractional position in buffer (L & R share) */
    float read_step;      /* per-sample increment = pitch ratio */
    int   age;            /* samples since spawn */
    int   length;         /* total samples in this grain */
} grain_t;

struct granular_s {
    float *buf_L;
    float *buf_R;
    int    buf_len;
    int    write_pos;

    grain_t grains[G_MAX_GRAINS];
    int     samples_to_next;     /* countdown to next grain spawn */

    /* Params */
    float grain_size_0_1;
    float scatter_0_1;
    int   glitchy;

    /* LCG RNG */
    uint32_t rng;
};

static inline uint32_t lcg_next(uint32_t *s) {
    *s = (*s) * 1103515245u + 12345u;
    return *s;
}
/* Uniform float in [-1, +1]. */
static inline float lcg_bipolar(uint32_t *s) {
    uint32_t v = lcg_next(s);
    return (float)v * (2.0f / 4294967295.0f) - 1.0f;
}
/* Uniform float in [0, 1). */
static inline float lcg_unipolar(uint32_t *s) {
    uint32_t v = lcg_next(s);
    return (float)v * (1.0f / 4294967295.0f);
}

static int current_grain_length(const granular_t *g) {
    int span = G_GRAIN_MAX_SAMPLES - G_GRAIN_MIN_SAMPLES;
    return G_GRAIN_MIN_SAMPLES + (int)((float)span * g->grain_size_0_1);
}

static void spawn_grain(granular_t *g) {
    /* Find a free slot — drop the spawn if none free. */
    grain_t *gr = NULL;
    for (int i = 0; i < G_MAX_GRAINS; i++) {
        if (!g->grains[i].active) { gr = &g->grains[i]; break; }
    }
    if (!gr) return;

    int len = current_grain_length(g);
    gr->length = len;
    gr->age = 0;

    /* Pitch — quantize to unison / ±octave / ±fifth (G_PITCH_STEPS).
     * Scatter controls probability of leaving unison; when it does, the
     * non-unison interval is picked uniformly from the four others.
     *   scatter=0   → 100% unison
     *   scatter=0.5 → 60% unison, 10% each non-unison
     *   scatter=1   → 20% each (all 5 intervals equally likely) */
    float pick = lcg_unipolar(&g->rng);
    float p_unity = 1.0f - g->scatter_0_1 * 0.8f;
    int   idx;
    if (pick < p_unity) {
        idx = 0;  /* unison */
    } else {
        float pick2 = lcg_unipolar(&g->rng);
        int j = (int)(pick2 * 4.0f);
        if (j >= 4) j = 3;
        idx = j + 1;  /* 1..4 → ±oct, ±P5 */
    }
    gr->read_step = G_PITCH_STEPS[idx];

    /* Start position — read from `len` samples behind write_pos so the
     * grain stays in valid past audio for its full duration (even at +1 oct
     * pitch the read just catches up to write at grain end). Scatter then
     * offsets the start by ±buf_len/2 within the ring. */
    int latency = len;
    int max_scatter = g->buf_len / 2;
    int scatter_off = (int)((float)max_scatter * g->scatter_0_1 * lcg_bipolar(&g->rng));
    int start = g->write_pos - latency - scatter_off;
    /* Bring into [0, buf_len). */
    while (start < 0)            start += g->buf_len;
    while (start >= g->buf_len)  start -= g->buf_len;
    gr->read_pos = (float)start;

    gr->active = 1;
}

granular_t* granular_create(void) {
    granular_t *g = (granular_t*)calloc(1, sizeof(granular_t));
    if (!g) return NULL;
    g->buf_len = G_BUF_SAMPLES;
    g->buf_L = (float*)calloc((size_t)g->buf_len, sizeof(float));
    g->buf_R = (float*)calloc((size_t)g->buf_len, sizeof(float));
    if (!g->buf_L || !g->buf_R) { granular_destroy(g); return NULL; }
    g->grain_size_0_1 = 0.5f;
    g->scatter_0_1 = 0.0f;
    g->glitchy = 0;
    g->rng = 0x12345678u;
    g->samples_to_next = current_grain_length(g) / 2;
    return g;
}

void granular_destroy(granular_t *g) {
    if (!g) return;
    free(g->buf_L);
    free(g->buf_R);
    free(g);
}

void granular_set_grain_size(granular_t *g, float size_0_1) {
    if (!g) return;
    if (size_0_1 < 0.0f) size_0_1 = 0.0f;
    if (size_0_1 > 1.0f) size_0_1 = 1.0f;
    g->grain_size_0_1 = size_0_1;
}

void granular_set_scatter(granular_t *g, float scatter_0_1) {
    if (!g) return;
    if (scatter_0_1 < 0.0f) scatter_0_1 = 0.0f;
    if (scatter_0_1 > 1.0f) scatter_0_1 = 1.0f;
    g->scatter_0_1 = scatter_0_1;
}

void granular_set_glitchy(granular_t *g, int glitchy) {
    if (!g) return;
    g->glitchy = glitchy ? 1 : 0;
}

void granular_process(granular_t *g,
                      const float *in_l, const float *in_r,
                      float *out_l, float *out_r,
                      int frames) {
    if (!g || frames <= 0) return;

    const int buf_len = g->buf_len;
    const int glitchy = g->glitchy;

    for (int n = 0; n < frames; n++) {
        /* 1. Write current input to capture buffer. */
        g->buf_L[g->write_pos] = in_l[n];
        g->buf_R[g->write_pos] = in_r[n];
        g->write_pos++;
        if (g->write_pos >= buf_len) g->write_pos = 0;

        /* 2. Grain scheduler — fire every length/2 samples (50% overlap). */
        g->samples_to_next--;
        if (g->samples_to_next <= 0) {
            spawn_grain(g);
            g->samples_to_next = current_grain_length(g) / 2;
            if (g->samples_to_next < 1) g->samples_to_next = 1;
        }

        /* 3. Render active grains. */
        float sum_l = 0.0f, sum_r = 0.0f;
        for (int i = 0; i < G_MAX_GRAINS; i++) {
            grain_t *gr = &g->grains[i];
            if (!gr->active) continue;

            /* Linear-interp read at fractional position. */
            int pi = (int)gr->read_pos;
            float pf = gr->read_pos - (float)pi;
            int pi2 = pi + 1; if (pi2 >= buf_len) pi2 = 0;
            float yl = g->buf_L[pi] * (1.0f - pf) + g->buf_L[pi2] * pf;
            float yr = g->buf_R[pi] * (1.0f - pf) + g->buf_R[pi2] * pf;

            /* Envelope: Hann (smooth, overlap-adds to ~1.0) or rectangular. */
            float env;
            if (glitchy) {
                env = 1.0f;
            } else {
                float phase = (float)gr->age / (float)gr->length;
                env = 0.5f * (1.0f - cosf(TWO_PI * phase));
            }

            sum_l += yl * env;
            sum_r += yr * env;

            /* Advance grain. */
            gr->read_pos += gr->read_step;
            if (gr->read_pos >= (float)buf_len) gr->read_pos -= (float)buf_len;
            else if (gr->read_pos < 0.0f) gr->read_pos += (float)buf_len;
            gr->age++;
            if (gr->age >= gr->length) gr->active = 0;
        }

        out_l[n] = sum_l;
        out_r[n] = sum_r;
    }
}
