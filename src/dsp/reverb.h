/* Ambiotica reverb — 4-delay Hadamard FDN with pre-diffusion.
 *
 * Slö-spirited lush algorithmic reverb. Stage 4 of the Ambiotica chain.
 * Phase 2: just the reverb. LFO modulation arrives in phase 3 via
 * reverb_set_mod() (not exposed yet).
 *
 * Realtime contract: reverb_process performs no allocation and no I/O.
 * All buffers and state are allocated in reverb_create.
 */
#ifndef AMBIOTICA_REVERB_H
#define AMBIOTICA_REVERB_H

typedef struct reverb_s reverb_t;

reverb_t* reverb_create(void);
void      reverb_destroy(reverb_t *r);

/* decay_0_1: 0 = short tail, 1 = very long tail. Maps to feedback gain. */
void      reverb_set_decay(reverb_t *r, float decay_0_1);

/* depth_0_1: 0 = no modulation, 1 = ~30 samples of comb delay modulation
 * (about ±0.68 ms swing at 44.1 kHz). */
void      reverb_set_mod_depth(reverb_t *r, float depth_0_1);

/* rate_0_1: 0 = 0.05 Hz (very slow drift), 1 = 8 Hz (chorus-fast). Log-mapped. */
void      reverb_set_mod_rate(reverb_t *r, float rate_0_1);

/* Process stereo float buffers. in and out may NOT alias (in is read first
 * for the whole block via mono-sum; safest to use separate buffers). */
void      reverb_process(reverb_t *r,
                         const float *in_l, const float *in_r,
                         float *out_l, float *out_r,
                         int frames);

#endif
