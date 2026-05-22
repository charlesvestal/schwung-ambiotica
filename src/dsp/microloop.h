/* Ambiotica micro-loop — Stage 3 of the chain.
 *
 * 4-second stereo buffer. Runs parallel to the looper/granular path:
 * captures whatever input is fed in (typically the dry signal) and outputs
 * a delayed/looped copy. Always functional regardless of Loop Layer.
 *
 *   hold = 0   → silent output (no loop content)
 *   hold low   → short stutter (~50–200 ms)
 *   hold mid   → ~1–2 s sustained layer
 *   hold high  → up to 4 s
 *   hold ≥ 0.95 OR freeze=1  → buffer locked, captured content keeps looping
 *
 * Output is ONLY the loop content (hold × delayed read). The caller adds
 * dry passthrough separately so the micro-loop can be an additive layer
 * in the wet bus.
 *
 * Realtime contract: process performs no allocation and no I/O.
 */
#ifndef AMBIOTICA_MICROLOOP_H
#define AMBIOTICA_MICROLOOP_H

typedef struct microloop_s microloop_t;

microloop_t* microloop_create(void);
void         microloop_destroy(microloop_t *m);

void         microloop_set_hold(microloop_t *m, float hold_0_1);

void         microloop_process(microloop_t *m,
                               const float *in_l, const float *in_r,
                               float *out_l, float *out_r,
                               int frames);

#endif
