/* Ambiotica LFO — sine for phase 3; warp/sink shapes arrive with phase 8.
 *
 * lfo_t is a value type. Owners (currently reverb_t; later also granular)
 * embed it by value and advance it themselves. Phase 5+ may extract a shared
 * lfo_t into amb_instance_t so the same wobble bends grain pitch AND reverb
 * diffusion in lockstep.
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

#endif
