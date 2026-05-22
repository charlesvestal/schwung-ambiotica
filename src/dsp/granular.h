/* Ambiotica granular — Stage 2 of the chain.
 *
 * 2-second stereo capture ring + 8-grain pool. Each grain reads a windowed
 * chunk of the captured audio at a position and pitch determined by Scatter
 * (with the random seeded fresh per grain). Hann window by default;
 * rectangular when glitchy alt-state is on.
 *
 * Scatter = 0 + unity pitch ratio gives near-passthrough (small phase delay).
 * Scatter = 1 randomizes both position (±half buffer) and pitch (±1 octave).
 *
 * Realtime contract: granular_process performs no allocation and no I/O.
 */
#ifndef AMBIOTICA_GRANULAR_H
#define AMBIOTICA_GRANULAR_H

typedef struct granular_s granular_t;

granular_t* granular_create(void);
void        granular_destroy(granular_t *g);

/* size_0_1: maps to grain length 10 ms .. 500 ms (linear). */
void        granular_set_grain_size(granular_t *g, float size_0_1);

/* scatter_0_1: 0 = sequential / unity pitch, 1 = full random pitch ±oct +
 * full buffer-wide position randomness. */
void        granular_set_scatter(granular_t *g, float scatter_0_1);

void        granular_process(granular_t *g,
                             const float *in_l, const float *in_r,
                             float *out_l, float *out_r,
                             int frames);

#endif
