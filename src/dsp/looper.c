/* Rolling-capture stereo looper with feedback. */
#include "looper.h"

#include <stdlib.h>
#include <string.h>

struct looper_s {
    float *buf_L;
    float *buf_R;
    int    buf_capacity;   /* allocated size — sets the max loop length */
    int    loop_len;       /* active read offset; <= buf_capacity */
    int    write_pos;
    /* Smoothed feedback — abrupt knob changes would otherwise inject a
     * step into the buffer and echo forever. */
    float  fb_target;
    float  fb_current;

    /* Loop-length crossfade state (same pattern as microloop). MIDI-clock
     * follower can re-anchor loop_len every loop pass; crossfade hides
     * the resulting read-pointer jump. */
    int    loop_len_pending;
    int    loop_len_queued;
    int    has_queued;
    int    crossfade_remaining;
};

#define LOOPER_SMOOTH_COEF   0.9989f  /* ~20 ms time constant @ 44.1 kHz */
#define LOOPER_CROSSFADE_LEN 512      /* ~11.6 ms linear crossfade */

/* Padé-3 tanh approximation — smooth soft-saturation for the feedback path.
 * Cheap (5 muls + 2 adds inside range) and bounded to ±1.0. Replaces hard
 * clip which produced clicky edges when sustained input + fb=1 saturated
 * the buffer. */
static inline float soft_sat(float x) {
    if (x >  3.0f) return  1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

looper_t* looper_create(int buf_capacity_samples) {
    if (buf_capacity_samples <= 0) return NULL;
    looper_t *l = (looper_t*)calloc(1, sizeof(looper_t));
    if (!l) return NULL;
    l->buf_capacity = buf_capacity_samples;
    l->loop_len = buf_capacity_samples;   /* default: full capacity */
    l->buf_L = (float*)calloc((size_t)buf_capacity_samples, sizeof(float));
    l->buf_R = (float*)calloc((size_t)buf_capacity_samples, sizeof(float));
    if (!l->buf_L || !l->buf_R) { looper_destroy(l); return NULL; }
    return l;
}

void looper_set_loop_len(looper_t *l, int loop_len_samples) {
    if (!l) return;
    if (loop_len_samples < 1) loop_len_samples = 1;
    if (loop_len_samples > l->buf_capacity) loop_len_samples = l->buf_capacity;

    /* If a crossfade is already running, queue the new value — don't
     * disrupt the in-flight transition. */
    if (l->crossfade_remaining > 0) {
        if (loop_len_samples != l->loop_len_pending) {
            l->loop_len_queued = loop_len_samples;
            l->has_queued = 1;
        }
        return;
    }
    if (loop_len_samples != l->loop_len) {
        l->loop_len_pending = loop_len_samples;
        l->crossfade_remaining = LOOPER_CROSSFADE_LEN;
    }
}

void looper_destroy(looper_t *l) {
    if (!l) return;
    free(l->buf_L);
    free(l->buf_R);
    free(l);
}

void looper_set_layer(looper_t *l, float layer_0_1) {
    if (!l) return;
    if (layer_0_1 < 0.0f) layer_0_1 = 0.0f;
    if (layer_0_1 > 1.0f) layer_0_1 = 1.0f;
    /* knob^2 curve for perceptual feel — soft start, lush mid, infinite top.
     * At knob = 1.0 fb = 1.0 (true looper, no decay). Soft-clip in process()
     * prevents amplitude runaway from sustained input + unity feedback. */
    l->fb_target = layer_0_1 * layer_0_1;
}

void looper_clear(looper_t *l) {
    if (!l) return;
    memset(l->buf_L, 0, (size_t)l->buf_capacity * sizeof(float));
    memset(l->buf_R, 0, (size_t)l->buf_capacity * sizeof(float));
}

void looper_process(looper_t *l,
                    const float *in_l, const float *in_r,
                    float *out_l, float *out_r,
                    int frames) {
    if (!l || frames <= 0) return;
    int pos = l->write_pos;
    const int cap = l->buf_capacity;

    float fb_curr      = l->fb_current;
    const float fb_t   = l->fb_target;
    const float c      = LOOPER_SMOOTH_COEF;
    const float ic     = 1.0f - c;

    for (int n = 0; n < frames; n++) {
        fb_curr = c * fb_curr + ic * fb_t;

        /* Read from active loop_len position. */
        int read_pos_a = pos - l->loop_len;
        if (read_pos_a < 0) read_pos_a += cap;
        float loopL = l->buf_L[read_pos_a];
        float loopR = l->buf_R[read_pos_a];

        /* During crossfade, blend with read at the pending loop_len.
         * Linear (gain-equal) crossfade — the two read positions are highly
         * correlated (same buffer, shifted by a few samples) so equal-power
         * cosine/sine causes a 3 dB hump mid-fade. Linear stays flat. */
        if (l->crossfade_remaining > 0) {
            int read_pos_b = pos - l->loop_len_pending;
            if (read_pos_b < 0) read_pos_b += cap;
            float loopL_b = l->buf_L[read_pos_b];
            float loopR_b = l->buf_R[read_pos_b];

            float gain_b = (float)(LOOPER_CROSSFADE_LEN - l->crossfade_remaining) *
                           (1.0f / (float)LOOPER_CROSSFADE_LEN);
            float gain_a = 1.0f - gain_b;
            loopL = gain_a * loopL + gain_b * loopL_b;
            loopR = gain_a * loopR + gain_b * loopR_b;

            l->crossfade_remaining--;
            if (l->crossfade_remaining == 0) {
                l->loop_len = l->loop_len_pending;
                if (l->has_queued && l->loop_len_queued != l->loop_len) {
                    l->loop_len_pending = l->loop_len_queued;
                    l->crossfade_remaining = LOOPER_CROSSFADE_LEN;
                }
                l->has_queued = 0;
            }
        }

        /* Write input + feedback back into the buffer at write_pos.
         * Normalized feedback formula: buf = (1-fb)*in + fb*old. At steady
         * state buffer content converges to input level — no buildup, no
         * runaway. At fb=1.0 the input term goes to zero, naturally freezing
         * the buffer (true looper). soft_sat kept as safety against
         * transient peaks. */
        float in_g = 1.0f - fb_curr;
        l->buf_L[pos] = soft_sat(in_g * in_l[n] + fb_curr * loopL);
        l->buf_R[pos] = soft_sat(in_g * in_r[n] + fb_curr * loopR);

        /* Output: ONLY the loop signal (no dry). Caller mixes dry separately. */
        out_l[n] = fb_curr * loopL;
        out_r[n] = fb_curr * loopR;

        pos++; if (pos >= cap) pos = 0;
    }
    l->write_pos = pos;
    l->fb_current = fb_curr;
}
