/* Ambiotica sub-octave pitch shifter — used by the "Dark" Slö voice.
 *
 * Two crossfading read heads at 0.5× rate over a ~93 ms ring buffer.
 * Output is always −1 octave of the input; level is controlled by the
 * caller's mix. Standard tape-style implementation: pitch shift up/down
 * via variable-rate read with cosine crossfade between two offset heads
 * to hide the read-pointer wrap.
 *
 * Realtime contract: process performs no allocation and no I/O.
 */
#ifndef AMBIOTICA_PSHIFT_H
#define AMBIOTICA_PSHIFT_H

typedef struct pshift_s pshift_t;

pshift_t* pshift_create(void);
void      pshift_destroy(pshift_t *p);

/* Process stereo: out = sub-octave of in. */
void      pshift_process(pshift_t *p,
                         const float *in_l, const float *in_r,
                         float *out_l, float *out_r,
                         int frames);

#endif
