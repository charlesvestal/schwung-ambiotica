/* Ambiotica looper — Stage 1 of the chain.
 *
 * 30-second stereo ring buffer with always-on rolling capture. Loop Layer
 * knob maps to feedback gain (0..0.95), which controls both (a) how much
 * of the delayed read is fed back into the buffer and (b) how much is
 * blended into the output. At feedback = 0 the looper is transparent.
 *
 * Phase 4: forward playback only. Reverse direction (knob doubletap on
 * Grain Size in the design) arrives in phase 8.
 */
#ifndef AMBIOTICA_LOOPER_H
#define AMBIOTICA_LOOPER_H

typedef struct looper_s looper_t;

/* Create with maximum buffer capacity in samples. The active loop length
 * defaults to this capacity and can be reduced via looper_set_loop_len.
 * Returns NULL on allocation failure. */
looper_t* looper_create(int buf_capacity_samples);
void      looper_destroy(looper_t *l);

/* layer_0_1: 0 = no loop (transparent), 1 = ~unity feedback (long-tailed). */
void      looper_set_loop_len(looper_t *l, int loop_len_samples);
void      looper_set_layer(looper_t *l, float layer_0_1);

/* One-shot: zero the entire buffer. Safe to call from set_param. */
void      looper_clear(looper_t *l);

/* Process stereo block. Reads from in_l/in_r and writes them into the
 * capture ring (with feedback). out_l/out_r receive ONLY the loop signal
 * (fb × delayed_read) — no dry. The caller is responsible for mixing dry
 * back in. in and out may NOT alias. */
void      looper_process(looper_t *l,
                         const float *in_l, const float *in_r,
                         float *out_l, float *out_r,
                         int frames);

#endif
