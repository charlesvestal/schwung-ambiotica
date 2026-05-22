/* Micro-loop — short feedback delay that locks into freeze near max hold. */
#include "microloop.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define M_SAMPLE_RATE      44100
#define M_MIN_LEN_SAMPLES  4410     /* 100 ms @ 44.1 kHz — above phasing range */
#define M_MAX_LEN_SAMPLES  176400   /* 4 s   @ 44.1 kHz */
#define M_AUTO_FREEZE      0.95f    /* knob threshold for auto-engaged freeze */
#define M_SMOOTH_COEF      0.9989f  /* ~20 ms time constant @ 44.1 kHz */

struct microloop_s {
    float *buf_L;
    float *buf_R;
    int    buf_capacity;
    int    write_pos;
    int    loop_len;
    float  hold;       /* 0..1 — both loop length and blend amount */
    int    freeze;     /* external freeze flag (alt-state) */

    /* Smoothed targets — process() ramps the live state per sample so knob
     * changes don't inject clicks into the feedback buffer. */
    float  fb_target,       fb_current;
    float  out_gain_target, out_gain_current;
};

microloop_t* microloop_create(void) {
    microloop_t *m = (microloop_t*)calloc(1, sizeof(microloop_t));
    if (!m) return NULL;
    m->buf_capacity = M_MAX_LEN_SAMPLES;
    m->buf_L = (float*)calloc((size_t)m->buf_capacity, sizeof(float));
    m->buf_R = (float*)calloc((size_t)m->buf_capacity, sizeof(float));
    if (!m->buf_L || !m->buf_R) { microloop_destroy(m); return NULL; }
    m->loop_len = M_MIN_LEN_SAMPLES;
    m->hold = 0.0f;
    m->freeze = 0;
    return m;
}

void microloop_destroy(microloop_t *m) {
    if (!m) return;
    free(m->buf_L);
    free(m->buf_R);
    free(m);
}

void microloop_set_hold(microloop_t *m, float hold_0_1) {
    if (!m) return;
    if (hold_0_1 < 0.0f) hold_0_1 = 0.0f;
    if (hold_0_1 > 1.0f) hold_0_1 = 1.0f;
    m->hold = hold_0_1;

    /* Log scale 100 ms .. 4 s. Floor at 100 ms keeps the low knob out of
     * the comb-filter / phasing range and into musically distinct echoes. */
    float log_min = logf((float)M_MIN_LEN_SAMPLES);
    float log_max = logf((float)M_MAX_LEN_SAMPLES);
    float exponent = log_min + (log_max - log_min) * hold_0_1;
    int len = (int)expf(exponent);
    if (len < M_MIN_LEN_SAMPLES) len = M_MIN_LEN_SAMPLES;
    if (len > M_MAX_LEN_SAMPLES) len = M_MAX_LEN_SAMPLES;
    m->loop_len = len;

    /* Update smoothing targets. process() ramps current → target per sample. */
    m->out_gain_target = sqrtf(hold_0_1);
    float fb_curve = hold_0_1 * 5.0f;
    if (fb_curve > 1.0f) fb_curve = 1.0f;
    m->fb_target = fb_curve * 0.95f;
}

void microloop_set_freeze(microloop_t *m, int freeze) {
    if (!m) return;
    m->freeze = freeze ? 1 : 0;
}

void microloop_process(microloop_t *m,
                      const float *in_l, const float *in_r,
                      float *out_l, float *out_r,
                      int frames) {
    if (!m || frames <= 0) return;
    /* Auto-engage freeze when knob approaches max, OR if alt-state demands it. */
    const int frozen = m->freeze || (m->hold >= M_AUTO_FREEZE);
    const int loop_len = m->loop_len;
    const int buf_capacity = m->buf_capacity;
    int write_pos = m->write_pos;

    /* Smoothed values — ramp per sample toward target. */
    float fb_curr  = m->fb_current;
    float out_curr = m->out_gain_current;
    const float fb_t  = m->fb_target;
    const float out_t = m->out_gain_target;
    const float c     = M_SMOOTH_COEF;
    const float ic    = 1.0f - c;

    for (int n = 0; n < frames; n++) {
        fb_curr  = c * fb_curr  + ic * fb_t;
        out_curr = c * out_curr + ic * out_t;

        int read_pos = write_pos - loop_len;
        if (read_pos < 0) read_pos += buf_capacity;
        float read_L = m->buf_L[read_pos];
        float read_R = m->buf_R[read_pos];

        /* Output: ONLY the loop content. Caller mixes in dry passthrough. */
        out_l[n] = out_curr * read_L;
        out_r[n] = out_curr * read_R;

        /* Write input + feedback into buffer unless frozen. */
        if (!frozen) {
            float new_L = in_l[n] + fb_curr * read_L;
            float new_R = in_r[n] + fb_curr * read_R;
            if (new_L >  1.0f) new_L =  1.0f; else if (new_L < -1.0f) new_L = -1.0f;
            if (new_R >  1.0f) new_R =  1.0f; else if (new_R < -1.0f) new_R = -1.0f;
            m->buf_L[write_pos] = new_L;
            m->buf_R[write_pos] = new_R;
        }

        write_pos++;
        if (write_pos >= buf_capacity) write_pos = 0;
    }
    m->write_pos = write_pos;
    m->fb_current = fb_curr;
    m->out_gain_current = out_curr;
}
