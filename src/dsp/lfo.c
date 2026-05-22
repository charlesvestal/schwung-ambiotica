#include "lfo.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TWO_PI (2.0f * (float)M_PI)

void lfo_init(lfo_t *lfo, int sample_rate) {
    if (!lfo) return;
    lfo->phase = 0.0f;
    lfo->increment = 0.0f;
    lfo->sample_rate = sample_rate > 0 ? sample_rate : 44100;
}

void lfo_set_rate_hz(lfo_t *lfo, float hz) {
    if (!lfo) return;
    if (hz < 0.0f) hz = 0.0f;
    lfo->increment = TWO_PI * hz / (float)lfo->sample_rate;
}

void lfo_set_phase(lfo_t *lfo, float radians) {
    if (!lfo) return;
    while (radians < 0.0f)    radians += TWO_PI;
    while (radians >= TWO_PI) radians -= TWO_PI;
    lfo->phase = radians;
}

float lfo_tick_sine(lfo_t *lfo) {
    if (!lfo) return 0.0f;
    float y = sinf(lfo->phase);
    lfo->phase += lfo->increment;
    if (lfo->phase >= TWO_PI) lfo->phase -= TWO_PI;
    return y;
}

float lfo_sine_at_offset(const lfo_t *lfo, float radian_offset) {
    if (!lfo) return 0.0f;
    return sinf(lfo->phase + radian_offset);
}

float lfo_shape_apply(int shape, float sine) {
    /* sine ∈ [-1, +1]. Output ∈ [-1, +1] for sine; warp & sink remap to a
     * unipolar range centered on a DC offset that biases the modulation in
     * one direction. */
    switch (shape) {
        case 1:  /* Warp: 0..+1, pitch always biased up */
            return 0.5f * (1.0f + sine);
        case 2:  /* Sink: -1..0, pitch always biased down */
            return 0.5f * (sine - 1.0f);
        case 0:
        default:
            return sine;
    }
}
