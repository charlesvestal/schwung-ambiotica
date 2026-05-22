/* Sub-octave pitch shifter — 2-head crossfading tape-style. */
#include "pshift.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TWO_PI (2.0f * (float)M_PI)

#define P_BUF_SAMPLES 4096    /* ~93 ms @ 44.1 kHz — power of two for fast wraparound */
#define P_RATIO       0.5f    /* −1 octave read rate */
#define P_MASK        (P_BUF_SAMPLES - 1)

struct pshift_s {
    float *buf_L;
    float *buf_R;
    int    write_pos;
    float  read_a;   /* fractional sample position */
    float  read_b;
};

pshift_t* pshift_create(void) {
    pshift_t *p = (pshift_t*)calloc(1, sizeof(pshift_t));
    if (!p) return NULL;
    p->buf_L = (float*)calloc(P_BUF_SAMPLES, sizeof(float));
    p->buf_R = (float*)calloc(P_BUF_SAMPLES, sizeof(float));
    if (!p->buf_L || !p->buf_R) { pshift_destroy(p); return NULL; }
    /* Offset the two heads by half the buffer so they're 180° out of phase
     * in their proximity to the write head — one is far when the other is
     * close, perfect for a constant-power crossfade window. */
    p->read_a = 0.0f;
    p->read_b = (float)(P_BUF_SAMPLES / 2);
    return p;
}

void pshift_destroy(pshift_t *p) {
    if (!p) return;
    free(p->buf_L);
    free(p->buf_R);
    free(p);
}

static inline float read_interp(const float *buf, float pos) {
    int i = (int)pos;
    float frac = pos - (float)i;
    int i2 = (i + 1) & P_MASK;
    return buf[i & P_MASK] * (1.0f - frac) + buf[i2] * frac;
}

void pshift_process(pshift_t *p,
                    const float *in_l, const float *in_r,
                    float *out_l, float *out_r,
                    int frames) {
    if (!p || frames <= 0) return;
    int   wpos = p->write_pos;
    float read_a = p->read_a;
    float read_b = p->read_b;
    const float inv_buf = 1.0f / (float)P_BUF_SAMPLES;

    for (int n = 0; n < frames; n++) {
        /* Write input. */
        p->buf_L[wpos] = in_l[n];
        p->buf_R[wpos] = in_r[n];
        wpos = (wpos + 1) & P_MASK;

        /* Read both heads. */
        float a_L = read_interp(p->buf_L, read_a);
        float a_R = read_interp(p->buf_R, read_a);
        float b_L = read_interp(p->buf_L, read_b);
        float b_R = read_interp(p->buf_R, read_b);

        /* Crossfade by distance from write_pos. Cosine window peaks at
         * buf/2 (head is "safely far" from write) and zeros at 0 and buf
         * (head is about to be overwritten by the writer). */
        float dist_a = (float)((wpos - (int)read_a + P_BUF_SAMPLES) & P_MASK);
        float dist_b = (float)((wpos - (int)read_b + P_BUF_SAMPLES) & P_MASK);
        float gain_a = 0.5f * (1.0f - cosf(TWO_PI * dist_a * inv_buf));
        float gain_b = 0.5f * (1.0f - cosf(TWO_PI * dist_b * inv_buf));

        out_l[n] = gain_a * a_L + gain_b * b_L;
        out_r[n] = gain_a * a_R + gain_b * b_R;

        /* Advance reads at the pitch ratio. */
        read_a += P_RATIO;
        if (read_a >= (float)P_BUF_SAMPLES) read_a -= (float)P_BUF_SAMPLES;
        read_b += P_RATIO;
        if (read_b >= (float)P_BUF_SAMPLES) read_b -= (float)P_BUF_SAMPLES;
    }

    p->write_pos = wpos;
    p->read_a = read_a;
    p->read_b = read_b;
}
