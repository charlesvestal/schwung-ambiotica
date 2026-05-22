/* Rolling-capture stereo looper with feedback. */
#include "looper.h"

#include <stdlib.h>
#include <string.h>

struct looper_s {
    float *buf_L;
    float *buf_R;
    int    buf_len;
    int    write_pos;
    /* Smoothed feedback — abrupt knob changes would otherwise inject a
     * step into the buffer and echo forever. */
    float  fb_target;
    float  fb_current;
};

#define LOOPER_SMOOTH_COEF 0.9989f  /* ~20 ms time constant @ 44.1 kHz */

looper_t* looper_create(int buf_len_samples) {
    if (buf_len_samples <= 0) return NULL;
    looper_t *l = (looper_t*)calloc(1, sizeof(looper_t));
    if (!l) return NULL;
    l->buf_len = buf_len_samples;
    l->buf_L = (float*)calloc((size_t)buf_len_samples, sizeof(float));
    l->buf_R = (float*)calloc((size_t)buf_len_samples, sizeof(float));
    if (!l->buf_L || !l->buf_R) { looper_destroy(l); return NULL; }
    return l;
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
    /* knob^2 curve so the bottom half is much quieter (perceptually useful
     * range) and only the top quarter gets into "lush layered loop" territory.
     * Cap at 0.95 prevents uncontrolled growth on sustained input. */
    l->fb_target = layer_0_1 * layer_0_1 * 0.95f;
}

void looper_clear(looper_t *l) {
    if (!l) return;
    memset(l->buf_L, 0, (size_t)l->buf_len * sizeof(float));
    memset(l->buf_R, 0, (size_t)l->buf_len * sizeof(float));
}

void looper_process(looper_t *l,
                    const float *in_l, const float *in_r,
                    float *out_l, float *out_r,
                    int frames) {
    if (!l || frames <= 0) return;
    int pos = l->write_pos;
    const int len = l->buf_len;

    float fb_curr      = l->fb_current;
    const float fb_t   = l->fb_target;
    const float c      = LOOPER_SMOOTH_COEF;
    const float ic     = 1.0f - c;

    for (int n = 0; n < frames; n++) {
        fb_curr = c * fb_curr + ic * fb_t;

        /* Read the sample that's about to be overwritten — this is the
         * oldest sample in the buffer, exactly buf_len samples old. */
        float loopL = l->buf_L[pos];
        float loopR = l->buf_R[pos];

        /* Write input + feedback back into the buffer. Soft-clip so the
         * loop can't blow up under sustained input + near-unity feedback. */
        float newL = in_l[n] + fb_curr * loopL;
        float newR = in_r[n] + fb_curr * loopR;
        if (newL >  1.0f) newL =  1.0f; else if (newL < -1.0f) newL = -1.0f;
        if (newR >  1.0f) newR =  1.0f; else if (newR < -1.0f) newR = -1.0f;
        l->buf_L[pos] = newL;
        l->buf_R[pos] = newR;

        /* Output: ONLY the loop signal (no dry). Caller mixes dry separately. */
        out_l[n] = fb_curr * loopL;
        out_r[n] = fb_curr * loopR;

        pos++; if (pos >= len) pos = 0;
    }
    l->write_pos = pos;
    l->fb_current = fb_curr;
}
