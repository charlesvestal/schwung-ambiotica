/* Ambiotica LFO — sine, warp, sink shapes.
 *
 * lfo_t is a value type. Owners embed it by value and advance it themselves.
 *
 * Shape 0 (Sine): symmetric ±1, equal up and down.
 * Shape 1 (Warp): DC-biased positive sine — pitch always biased up, like a
 *                 warped record where the pitch trends upward over the cycle.
 * Shape 2 (Sink): DC-biased negative sine — pitch always biased down, like
 *                 a slow Bigsby drop.
 */
#ifndef AMBIOTICA_LFO_H
#define AMBIOTICA_LFO_H

typedef struct {
    float phase;       /* radians [0, 2pi) */
    float increment;   /* radians per sample */
    int   sample_rate;
} lfo_t;

void  lfo_init(lfo_t *lfo, int sample_rate);
void  lfo_set_rate_hz(lfo_t *lfo, float hz);
void  lfo_set_phase(lfo_t *lfo, float radians);

/* Advance one sample, return current sine value in [-1, +1]. */
float lfo_tick_sine(lfo_t *lfo);

/* Read sine without advancing — useful when multiple consumers want to
 * read offsets from the same per-sample phase. */
float lfo_sine_at_offset(const lfo_t *lfo, float radian_offset);

/* Apply a shape to a raw sine value: 0=sine, 1=warp, 2=sink.
 * Used to share one shared LFO across consumers with different shape needs. */
float lfo_shape_apply(int shape, float sine);

#endif
