/* Ambiotica micro-loop — Stage 3 of the chain.
 *
 * 4-second stereo buffer that captures the post-Scatter layered signal and
 * lets the Micro Hold knob layer a delayed/looped copy on top, with smooth
 * transition into freeze.
 *
 *   hold = 0   → buffer still captures, but nothing blended (passthrough)
 *   hold low   → short stutter (~50–200 ms)
 *   hold mid   → ~1–2 s sustained layer
 *   hold high  → up to 4 s
 *   hold ≥ 0.95 OR freeze=1  → buffer locked, captured content keeps looping
 *
 * Output is `in + hold * delayed_read` so the upstream signal always flows
 * through cleanly — the micro-loop is an additive freeze layer.
 *
 * Realtime contract: process performs no allocation and no I/O.
 */
#ifndef AMBIOTICA_MICROLOOP_H
#define AMBIOTICA_MICROLOOP_H

typedef struct microloop_s microloop_t;

microloop_t* microloop_create(void);
void         microloop_destroy(microloop_t *m);

void         microloop_set_hold(microloop_t *m, float hold_0_1);
void         microloop_set_freeze(microloop_t *m, int freeze);

void         microloop_process(microloop_t *m,
                               const float *in_l, const float *in_r,
                               float *out_l, float *out_r,
                               int frames);

#endif
