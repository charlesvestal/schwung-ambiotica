/* Micro-loop — short feedback delay that locks into freeze near max hold. */
#include "microloop.h"

#include <stdlib.h>
#include <string.h>

#define M_SAMPLE_RATE     44100
#define M_MIN_LEN_SAMPLES (M_SAMPLE_RATE / 20)         /* 50 ms */
#define M_MAX_LEN_SAMPLES (4 * M_SAMPLE_RATE)          /* 4 s */
#define M_AUTO_FREEZE     0.95f                        /* knob threshold */

struct microloop_s {
    float *buf_L;
    float *buf_R;
    int    buf_capacity;
    int    write_pos;
    int    loop_len;
    float  hold;       /* 0..1 — both loop length and blend amount */
    int    freeze;     /* external freeze flag (alt-state) */
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
    /* Linear mapping for now — easy to tune by ear. */
    m->loop_len = M_MIN_LEN_SAMPLES +
                  (int)((float)(M_MAX_LEN_SAMPLES - M_MIN_LEN_SAMPLES) * hold_0_1);
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
    const float hold = m->hold;
    const float fb   = hold * 0.95f;
    /* Auto-engage freeze when knob approaches max, OR if alt-state demands it. */
    const int frozen = m->freeze || (hold >= M_AUTO_FREEZE);
    const int loop_len = m->loop_len;
    const int buf_capacity = m->buf_capacity;
    int write_pos = m->write_pos;

    for (int n = 0; n < frames; n++) {
        int read_pos = write_pos - loop_len;
        if (read_pos < 0) read_pos += buf_capacity;
        float read_L = m->buf_L[read_pos];
        float read_R = m->buf_R[read_pos];

        /* Output: input + hold × delayed read. */
        out_l[n] = in_l[n] + hold * read_L;
        out_r[n] = in_r[n] + hold * read_R;

        /* Write input + feedback into buffer unless frozen. */
        if (!frozen) {
            float new_L = in_l[n] + fb * read_L;
            float new_R = in_r[n] + fb * read_R;
            if (new_L >  1.0f) new_L =  1.0f; else if (new_L < -1.0f) new_L = -1.0f;
            if (new_R >  1.0f) new_R =  1.0f; else if (new_R < -1.0f) new_R = -1.0f;
            m->buf_L[write_pos] = new_L;
            m->buf_R[write_pos] = new_R;
        }

        write_pos++;
        if (write_pos >= buf_capacity) write_pos = 0;
    }
    m->write_pos = write_pos;
}
