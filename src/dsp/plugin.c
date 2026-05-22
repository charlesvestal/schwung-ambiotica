/* schwung-ambiotica plugin entry — phase 1 passthrough.
 *
 * All eight knob params, six alt-state params, and the mode enum are present
 * and round-trip via set_param/get_param. Audio is passed through unchanged.
 * DSP stages (looper, granular, microloop, reverb) land in subsequent phases.
 */
#include "audio_fx_api_v2.h"
#include "looper.h"
#include "granular.h"
#include "microloop.h"
#include "reverb.h"

#define AMB_SAMPLE_RATE       44100
#define AMB_BEATS_PER_BAR     4
#define AMB_DEFAULT_BPM       120.0f  /* fallback if host doesn't expose tempo */
#define AMB_LOOP_BARS_DEFAULT 1.5f    /* polyrhythmic — never lines up with bar grid */
#define AMB_LOOP_BARS_MIN     0.5f
#define AMB_LOOP_BARS_MAX     8.0f
#define AMB_LOOP_BUF_MAX_SECONDS 32   /* 8 bars at 60 BPM = max allocation needed */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static const host_api_v1_t *g_host = NULL;

#define AMB_MODE_COUNT 4
static const char *AMB_MODE_NAMES[AMB_MODE_COUNT] = {
    "Mismember", "Loona", "NAPS", "Flow"
};

typedef struct {
    float mix, loop_layer, grain_size, scatter;
    float micro_hold, decay, mod_depth, mod_rate;
} amb_preset_t;

/* Mode preset values — see docs/plans/2026-05-22-ambiotica-design.md §2.
 * Selecting a mode atomically overwrites these eight knob values. Capture
 * buffers (looper / granular / microloop) are intentionally preserved
 * across mode switches so audio doesn't dump. */
static const amb_preset_t AMB_PRESETS[AMB_MODE_COUNT] = {
    /* Mismember — chaotic glitch / pointillistic texture. */
    { .mix = 0.50f, .loop_layer = 0.87f, .grain_size = 0.25f, .scatter = 0.80f,
      .micro_hold = 0.18f, .decay = 0.80f, .mod_depth = 0.50f, .mod_rate = 0.60f },
    /* Loona — clean rolling-capture loops, short reverb. Loop_layer high so
     * a single phrase clearly returns at 6 s; decay low so it isn't washed. */
    { .mix = 0.50f, .loop_layer = 0.95f, .grain_size = 0.90f, .scatter = 0.05f,
      .micro_hold = 0.10f, .decay = 0.15f, .mod_depth = 0.15f, .mod_rate = 0.20f },
    /* NAPS — frozen-breath sound under lush tail. */
    { .mix = 0.50f, .loop_layer = 0.20f, .grain_size = 0.80f, .scatter = 0.20f,
      .micro_hold = 0.65f, .decay = 0.80f, .mod_depth = 0.25f, .mod_rate = 0.15f },
    /* Flow — pure modulated reverb, no capture/grain. */
    { .mix = 0.50f, .loop_layer = 0.00f, .grain_size = 0.10f, .scatter = 0.00f,
      .micro_hold = 0.00f, .decay = 0.95f, .mod_depth = 0.70f, .mod_rate = 0.10f },
};

typedef struct {
    float mix;
    float loop_layer;
    float grain_size;
    float scatter;
    float micro_hold;
    float decay;
    float mod_depth;
    float mod_rate;

    int   mod_sync;
    int   mod_shape;     /* 0=sine, 1=warp, 2=sink */

    int   mode;

    /* Setting (not part of mode presets) — loop length in bars (0.5..8.0). */
    float loop_length_bars;

    /* Lo-fi tails — reverb runs at half rate for time-stretched bitcrush. */
    int   lofi_tails_on;

    /* Live BPM tracking — re-applies loop_length and mod_rate when tempo
     * changes. Throttled to avoid recompute on every block. */
    float last_bpm;

    /* Smoothed final-mix value. plugin.c blends dry vs wet bus per sample
     * using mix_current ramping toward inst->mix so knob changes don't click. */
    float mix_current;

    /* Stage 1 — looper. */
    looper_t *looper;
    /* Stage 2 — granular. */
    granular_t *granular;
    /* Stage 3 — micro-loop (freeze layer). */
    microloop_t *microloop;
    /* Stage 4 — reverb. */
    reverb_t *reverb;
} amb_instance_t;

/* Forward declarations. */
static void amb_apply_loop_length(amb_instance_t *inst);
static void amb_apply_mod_rate(amb_instance_t *inst);

/* Beat divisions for tempo-synced mod rate (slow → fast). */
static const float AMB_SYNC_BEATS[6] = {
    4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f  /* 1 bar, 1/2, 1/4, 1/8, 1/16, 1/32 */
};
#define AMB_SYNC_BEATS_COUNT 6

/* Compute mod rate: if mod_sync, use beat-division from knob × BPM; else
 * free-running log-mapped Hz from knob. Pushes the result into BOTH reverb
 * and granular so their LFOs share a tempo. */
static void amb_apply_mod_rate(amb_instance_t *inst) {
    if (!inst || !inst->reverb) return;
    float hz;
    if (inst->mod_sync) {
        float bpm = AMB_DEFAULT_BPM;
        if (g_host && g_host->get_bpm) {
            float b = g_host->get_bpm();
            if (b > 0.0f) bpm = b;
        }
        int idx = (int)(inst->mod_rate * (float)AMB_SYNC_BEATS_COUNT);
        if (idx < 0) idx = 0;
        if (idx >= AMB_SYNC_BEATS_COUNT) idx = AMB_SYNC_BEATS_COUNT - 1;
        float beats_per_cycle = AMB_SYNC_BEATS[idx];
        hz = bpm / (60.0f * beats_per_cycle);
        reverb_set_mod_rate_hz(inst->reverb, hz);
    } else {
        hz = 0.05f * expf(inst->mod_rate * 5.075f);
        reverb_set_mod_rate(inst->reverb, inst->mod_rate);
    }
    if (inst->granular) granular_set_mod_rate_hz(inst->granular, hz);
}

/* Apply loop_length_bars × current BPM to looper's active loop length. */
static void amb_apply_loop_length(amb_instance_t *inst) {
    if (!inst || !inst->looper) return;
    float bars = inst->loop_length_bars;
    if (bars < AMB_LOOP_BARS_MIN) bars = AMB_LOOP_BARS_MIN;
    if (bars > AMB_LOOP_BARS_MAX) bars = AMB_LOOP_BARS_MAX;
    float bpm = AMB_DEFAULT_BPM;
    if (g_host && g_host->get_bpm) {
        float b = g_host->get_bpm();
        if (b > 0.0f) bpm = b;
    }
    float loop_seconds = bars * (float)AMB_BEATS_PER_BAR * 60.0f / bpm;
    int samples = (int)(loop_seconds * (float)AMB_SAMPLE_RATE);
    looper_set_loop_len(inst->looper, samples);
}

/* --- Minimal JSON readers (same pattern as schwung-midiverb plugin.c) --- */

static int json_get_float(const char *json, const char *key, float *out) {
    if (!json || !key || !out) return -1;
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    while (*p == ' ' || *p == '\t') p++;
    *out = (float)atof(p);
    return 0;
}

static int json_get_int(const char *json, const char *key, int *out) {
    if (!json || !key || !out) return -1;
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    while (*p == ' ' || *p == '\t') p++;
    *out = atoi(p);
    return 0;
}

/* --- Lifecycle --- */

static void* amb_create(const char *module_dir, const char *config_json) {
    (void)module_dir; (void)config_json;
    amb_instance_t *inst = (amb_instance_t*)calloc(1, sizeof(amb_instance_t));
    if (!inst) return NULL;
    /* Defaults match the Mismember preset (mode 0 — first in the list). */
    inst->mix = 0.50f;
    inst->loop_layer = 0.87f;
    inst->grain_size = 0.25f;
    inst->scatter = 0.80f;
    inst->micro_hold = 0.18f;
    inst->decay = 0.80f;
    inst->mod_depth = 0.50f;
    inst->mod_rate = 0.60f;
    inst->mode = 0;
    inst->mix_current = inst->mix;
    inst->lofi_tails_on = 0;
    inst->last_bpm = 0.0f;

    /* Allocate looper buffer for the WORST-case loop length so we can
     * resize the active loop_len later without realloc. 8 bars @ 60 BPM. */
    inst->looper = looper_create(AMB_LOOP_BUF_MAX_SECONDS * AMB_SAMPLE_RATE);
    if (!inst->looper) { free(inst); return NULL; }
    looper_set_layer(inst->looper, inst->loop_layer);

    /* Tempo-aware initial loop length using the default bar count. */
    inst->loop_length_bars = AMB_LOOP_BARS_DEFAULT;
    amb_apply_loop_length(inst);

    inst->granular = granular_create();
    if (!inst->granular) { looper_destroy(inst->looper); free(inst); return NULL; }
    granular_set_grain_size(inst->granular, inst->grain_size);
    granular_set_scatter(inst->granular, inst->scatter);

    inst->microloop = microloop_create();
    if (!inst->microloop) {
        looper_destroy(inst->looper);
        granular_destroy(inst->granular);
        free(inst); return NULL;
    }
    microloop_set_hold(inst->microloop, inst->micro_hold);

    inst->reverb = reverb_create();
    if (!inst->reverb) {
        looper_destroy(inst->looper);
        granular_destroy(inst->granular);
        microloop_destroy(inst->microloop);
        free(inst); return NULL;
    }
    reverb_set_decay(inst->reverb, inst->decay);
    reverb_set_mod_depth(inst->reverb, inst->mod_depth);
    reverb_set_mod_rate(inst->reverb, inst->mod_rate);
    return inst;
}

static void amb_destroy(void *vp) {
    amb_instance_t *inst = (amb_instance_t*)vp;
    if (!inst) return;
    looper_destroy(inst->looper);
    granular_destroy(inst->granular);
    microloop_destroy(inst->microloop);
    reverb_destroy(inst->reverb);
    free(inst);
}

/* --- Audio chain.
 *
 *   dry → looper → loop_signal (loop only, no dry)
 *                       ↓
 *                     granular → grained_loop
 *                                       ↓
 *   dry + grained_loop → reverb → reverb_tail
 *
 *   wet_bus = grained_loop + reverb_tail
 *   out     = (1-mix)*dry + mix*wet_bus
 *
 * Dry passes through clean. Scatter / pitch effects only affect the loop
 * layer, never the live signal. The reverb sees dry + grained loop so the
 * tail spans both the live note and any looped textures. */

static void amb_process(void *vp, int16_t *audio_inout, int frames) {
    amb_instance_t *inst = (amb_instance_t*)vp;
    if (!inst || !inst->reverb || !inst->looper || !inst->granular || frames <= 0) return;

    static float dry_l[256],   dry_r[256];
    static float loop_l[256],  loop_r[256];   /* looper output: loop only */
    static float gran_l[256],  gran_r[256];   /* granular(loop) */
    static float micro_l[256], micro_r[256];  /* microloop(layered) */
    static float rev_in_l[256], rev_in_r[256]; /* reverb input = dry + layered + micro */
    static float wet_l[256],   wet_r[256];    /* reverb tail */
    if (frames > 256) frames = 256;

    /* Int16 → float. */
    for (int i = 0; i < frames; i++) {
        dry_l[i] = audio_inout[2*i + 0] * (1.0f / 32768.0f);
        dry_r[i] = audio_inout[2*i + 1] * (1.0f / 32768.0f);
    }

    /* Stage 1: Looper. Captures dry, outputs only the loop signal. */
    looper_process(inst->looper, dry_l, dry_r, loop_l, loop_r, frames);

    /* Stage 2: Granular processes the loop signal only (live signal stays clean). */
    granular_process(inst->granular, loop_l, loop_r, gran_l, gran_r, frames);

    /* Scatter knob is dual-purpose:
     *   0..0.5  : clean loop at full + shimmer ramping in (layered shimmer)
     *   0.5..1  : clean loop fades out + shimmer stays full (replaces into destruction)
     * At 0   : pure clean loop (gran is near-identity anyway, but explicit zero is silent)
     * At 0.5 : loop + half-shimmer (classic shimmer-on-top)
     * At 1   : only the heavily scattered version (clean gone, modulated to destruction) */
    const float scatter   = inst->scatter;
    const float clean_g   = (scatter <= 0.5f) ? 1.0f : (1.0f - 2.0f * (scatter - 0.5f));
    const float shimmer_g = scatter;

    static float layered_l[256], layered_r[256];
    for (int i = 0; i < frames; i++) {
        layered_l[i] = clean_g * loop_l[i] + shimmer_g * gran_l[i];
        layered_r[i] = clean_g * loop_r[i] + shimmer_g * gran_r[i];
    }

    /* Stage 3: Micro-loop captures the DRY signal in parallel — independent
     * freeze layer that doesn't depend on Loop Layer being on. Output is
     * just the loop content (no dry passthrough); caller adds dry separately. */
    microloop_process(inst->microloop, dry_l, dry_r, micro_l, micro_r, frames);

    /* Reverb input = dry + layered (loop/shimmer) + micro (freeze). */
    for (int i = 0; i < frames; i++) {
        rev_in_l[i] = dry_l[i] + layered_l[i] + micro_l[i];
        rev_in_r[i] = dry_r[i] + layered_r[i] + micro_r[i];
    }

    /* Stage 4: Reverb. */
    reverb_process(inst->reverb, rev_in_l, rev_in_r, wet_l, wet_r, frames);

    /* Final mix: wet bus = layered + micro + reverb tail.
     * Smooth the Mix knob per-sample so abrupt knob changes don't click. */
    const float mix_target = inst->mix;
    float mix_curr = inst->mix_current;
    const float c = 0.9989f;  /* ~20 ms time constant */
    const float ic = 1.0f - c;
    for (int i = 0; i < frames; i++) {
        mix_curr = c * mix_curr + ic * mix_target;
        float dry_g = 1.0f - mix_curr;

        float wet_bus_l = layered_l[i] + micro_l[i] + wet_l[i];
        float wet_bus_r = layered_r[i] + micro_r[i] + wet_r[i];
        float l = dry_g * dry_l[i] + mix_curr * wet_bus_l;
        float r = dry_g * dry_r[i] + mix_curr * wet_bus_r;
        if (l >  1.0f) l =  1.0f; else if (l < -1.0f) l = -1.0f;
        if (r >  1.0f) r =  1.0f; else if (r < -1.0f) r = -1.0f;
        audio_inout[2*i + 0] = (int16_t)(l * 32767.0f);
        audio_inout[2*i + 1] = (int16_t)(r * 32767.0f);
    }
    inst->mix_current = mix_curr;
}

/* --- set_param --- */

static void amb_set_state(amb_instance_t *inst, const char *val) {
    float f; int i;
    if (json_get_float(val, "mix",         &f) == 0) inst->mix = f;
    if (json_get_float(val, "loop_layer",  &f) == 0) { inst->loop_layer = f; looper_set_layer(inst->looper, f); }
    if (json_get_float(val, "grain_size",  &f) == 0) { inst->grain_size = f; granular_set_grain_size(inst->granular, f); }
    if (json_get_float(val, "scatter",     &f) == 0) { inst->scatter = f; granular_set_scatter(inst->granular, f); }
    if (json_get_float(val, "micro_hold",  &f) == 0) { inst->micro_hold = f; microloop_set_hold(inst->microloop, f); }
    if (json_get_float(val, "decay",       &f) == 0) { inst->decay = f; reverb_set_decay(inst->reverb, f); }
    if (json_get_float(val, "mod_depth",   &f) == 0) { inst->mod_depth = f; reverb_set_mod_depth(inst->reverb, f); granular_set_mod_depth(inst->granular, f); }
    if (json_get_float(val, "mod_rate",    &f) == 0) { inst->mod_rate = f; reverb_set_mod_rate(inst->reverb, f); }
    if (json_get_int  (val, "mod_sync",       &i) == 0) {
        inst->mod_sync = i ? 1 : 0;
        amb_apply_mod_rate(inst);
    }
    if (json_get_int  (val, "mod_shape",      &i) == 0) {
        inst->mod_shape = (i < 0 ? 0 : (i > 2 ? 2 : i));
        reverb_set_mod_shape(inst->reverb, inst->mod_shape);
    }
    if (json_get_int  (val, "lofi_tails",     &i) == 0) {
        inst->lofi_tails_on = i ? 1 : 0;
        reverb_set_stretch(inst->reverb, inst->lofi_tails_on);
    }
    if (json_get_int  (val, "mode",           &i) == 0)
        inst->mode = (i < 0 ? 0 : (i >= AMB_MODE_COUNT ? AMB_MODE_COUNT - 1 : i));
    if (json_get_float(val, "loop_length",    &f) == 0) {
        if (f < AMB_LOOP_BARS_MIN) f = AMB_LOOP_BARS_MIN;
        if (f > AMB_LOOP_BARS_MAX) f = AMB_LOOP_BARS_MAX;
        inst->loop_length_bars = f;
        amb_apply_loop_length(inst);
    }
}

static void amb_set_param(void *vp, const char *key, const char *val) {
    amb_instance_t *inst = (amb_instance_t*)vp;
    if (!inst || !key || !val) return;
    if (strcmp(key, "state") == 0)         { amb_set_state(inst, val); return; }
    if (strcmp(key, "mix") == 0)           { inst->mix = (float)atof(val); return; }
    if (strcmp(key, "loop_layer") == 0)    {
        inst->loop_layer = (float)atof(val);
        looper_set_layer(inst->looper, inst->loop_layer);
        return;
    }
    if (strcmp(key, "loop_clear") == 0)    {
        /* One-shot: any truthy value clears the loop buffer. Wired to the
         * Loop Layer knob's double-tap by phase 8. */
        if (atoi(val) != 0) looper_clear(inst->looper);
        return;
    }
    if (strcmp(key, "loop_length") == 0) {
        /* Bars, 0.5..8.0 step 0.5. Triggers a recompute of looper.loop_len. */
        float bars = (float)atof(val);
        if (bars < AMB_LOOP_BARS_MIN) bars = AMB_LOOP_BARS_MIN;
        if (bars > AMB_LOOP_BARS_MAX) bars = AMB_LOOP_BARS_MAX;
        inst->loop_length_bars = bars;
        amb_apply_loop_length(inst);
        return;
    }
    if (strcmp(key, "grain_size") == 0)    {
        inst->grain_size = (float)atof(val);
        granular_set_grain_size(inst->granular, inst->grain_size);
        return;
    }
    if (strcmp(key, "scatter") == 0)       {
        inst->scatter = (float)atof(val);
        granular_set_scatter(inst->granular, inst->scatter);
        return;
    }
    if (strcmp(key, "micro_hold") == 0)    {
        inst->micro_hold = (float)atof(val);
        microloop_set_hold(inst->microloop, inst->micro_hold);
        return;
    }
    if (strcmp(key, "decay") == 0)         {
        inst->decay = (float)atof(val);
        reverb_set_decay(inst->reverb, inst->decay);
        return;
    }
    if (strcmp(key, "mod_depth") == 0)     {
        inst->mod_depth = (float)atof(val);
        reverb_set_mod_depth(inst->reverb, inst->mod_depth);
        granular_set_mod_depth(inst->granular, inst->mod_depth);
        return;
    }
    if (strcmp(key, "mod_rate") == 0)      {
        inst->mod_rate = (float)atof(val);
        amb_apply_mod_rate(inst);
        return;
    }
    if (strcmp(key, "mod_sync") == 0)       {
        inst->mod_sync = atoi(val) ? 1 : 0;
        amb_apply_mod_rate(inst);
        return;
    }
    if (strcmp(key, "lofi_tails") == 0) {
        inst->lofi_tails_on = atoi(val) ? 1 : 0;
        reverb_set_stretch(inst->reverb, inst->lofi_tails_on);
        return;
    }
    if (strcmp(key, "mod_shape") == 0) {
        int s = atoi(val);
        inst->mod_shape = (s < 0 ? 0 : (s > 2 ? 2 : s));
        reverb_set_mod_shape(inst->reverb, inst->mod_shape);
        return;
    }
    if (strcmp(key, "mode") == 0) {
        int m = atoi(val);
        m = (m < 0 ? 0 : (m >= AMB_MODE_COUNT ? AMB_MODE_COUNT - 1 : m));
        inst->mode = m;
        /* Atomic 8-knob preset — overwrite values AND push to each stage.
         * Capture buffers are preserved so the loop / freeze / grain
         * material in flight survives the mode change. */
        const amb_preset_t *p = &AMB_PRESETS[m];
        inst->mix        = p->mix;
        inst->loop_layer = p->loop_layer;
        inst->grain_size = p->grain_size;
        inst->scatter    = p->scatter;
        inst->micro_hold = p->micro_hold;
        inst->decay      = p->decay;
        inst->mod_depth  = p->mod_depth;
        inst->mod_rate   = p->mod_rate;
        looper_set_layer(inst->looper, inst->loop_layer);
        granular_set_grain_size(inst->granular, inst->grain_size);
        granular_set_scatter(inst->granular, inst->scatter);
        microloop_set_hold(inst->microloop, inst->micro_hold);
        reverb_set_decay(inst->reverb, inst->decay);
        reverb_set_mod_depth(inst->reverb, inst->mod_depth);
        granular_set_mod_depth(inst->granular, inst->mod_depth);
        amb_apply_mod_rate(inst);
        return;
    }
}

/* --- get_param --- */

static int amb_get_param(void *vp, const char *key, char *buf, int buf_len) {
    amb_instance_t *inst = (amb_instance_t*)vp;
    if (!inst || !key || !buf || buf_len <= 0) return -1;
    int n = -1;

    if      (strcmp(key, "mix") == 0)         n = snprintf(buf, buf_len, "%.3f", inst->mix);
    else if (strcmp(key, "loop_layer") == 0)  n = snprintf(buf, buf_len, "%.3f", inst->loop_layer);
    else if (strcmp(key, "grain_size") == 0)  n = snprintf(buf, buf_len, "%.3f", inst->grain_size);
    else if (strcmp(key, "scatter") == 0)     n = snprintf(buf, buf_len, "%.3f", inst->scatter);
    else if (strcmp(key, "micro_hold") == 0)  n = snprintf(buf, buf_len, "%.3f", inst->micro_hold);
    else if (strcmp(key, "decay") == 0)       n = snprintf(buf, buf_len, "%.3f", inst->decay);
    else if (strcmp(key, "mod_depth") == 0)   n = snprintf(buf, buf_len, "%.3f", inst->mod_depth);
    else if (strcmp(key, "mod_rate") == 0)    n = snprintf(buf, buf_len, "%.3f", inst->mod_rate);
    else if (strcmp(key, "mod_sync") == 0)       n = snprintf(buf, buf_len, "%d", inst->mod_sync);
    else if (strcmp(key, "mod_shape") == 0)      n = snprintf(buf, buf_len, "%d", inst->mod_shape);
    else if (strcmp(key, "lofi_tails") == 0)     n = snprintf(buf, buf_len, "%d", inst->lofi_tails_on);
    else if (strcmp(key, "mode") == 0)        n = snprintf(buf, buf_len, "%d", inst->mode);
    else if (strcmp(key, "mode_count") == 0)  n = snprintf(buf, buf_len, "%d", AMB_MODE_COUNT);
    else if (strcmp(key, "mode_name") == 0)
        n = snprintf(buf, buf_len, "%s", AMB_MODE_NAMES[inst->mode]);
    else if (strcmp(key, "loop_length") == 0)
        n = snprintf(buf, buf_len, "%.1f", inst->loop_length_bars);
    else if (strcmp(key, "state") == 0) {
        n = snprintf(buf, buf_len,
            "{\"mode\":%d,"
            "\"mix\":%.4f,\"loop_layer\":%.4f,\"grain_size\":%.4f,\"scatter\":%.4f,"
            "\"micro_hold\":%.4f,\"decay\":%.4f,\"mod_depth\":%.4f,\"mod_rate\":%.4f,"
            "\"mod_sync\":%d,\"mod_shape\":%d,"
            "\"lofi_tails\":%d,"
            "\"loop_length\":%.2f}",
            inst->mode,
            inst->mix, inst->loop_layer, inst->grain_size, inst->scatter,
            inst->micro_hold, inst->decay, inst->mod_depth, inst->mod_rate,
            inst->mod_sync, inst->mod_shape,
            inst->lofi_tails_on,
            inst->loop_length_bars);
    }
    else if (strcmp(key, "chain_params") == 0) {
        n = snprintf(buf, buf_len,
            "["
            "{\"key\":\"mode\",\"name\":\"Mode\",\"type\":\"enum\",\"options\":[\"Mismember\",\"Loona\",\"NAPS\",\"Flow\"]},"
            "{\"key\":\"mix\",\"name\":\"Mix\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"loop_layer\",\"name\":\"Loop Layer\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"grain_size\",\"name\":\"Grain Size\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"scatter\",\"name\":\"Scatter\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"micro_hold\",\"name\":\"Micro Hold\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"decay\",\"name\":\"Decay\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mod_depth\",\"name\":\"Mod Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mod_rate\",\"name\":\"Mod Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mod_sync\",\"name\":\"Tempo Sync\",\"type\":\"int\",\"min\":0,\"max\":1},"
            "{\"key\":\"mod_shape\",\"name\":\"Mod Shape\",\"type\":\"enum\",\"options\":[\"Sine\",\"Warp\",\"Sink\"]},"
            "{\"key\":\"lofi_tails\",\"name\":\"Lo-Fi Tails\",\"type\":\"int\",\"min\":0,\"max\":1},"
            "{\"key\":\"loop_length\",\"name\":\"Loop Length\",\"type\":\"float\",\"min\":0.5,\"max\":8,\"step\":0.5,\"unit\":\"bars\"}"
            "]");
    }
    else if (strcmp(key, "ui_hierarchy") == 0) {
        n = snprintf(buf, buf_len,
            "{"
              "\"modes\":null,"
              "\"levels\":{"
                "\"root\":{"
                  "\"label\":\"Ambiotica\","
                  "\"list_param\":\"mode\","
                  "\"count_param\":\"mode_count\","
                  "\"name_param\":\"mode_name\","
                  "\"knobs\":[\"mix\",\"loop_layer\",\"grain_size\",\"scatter\","
                             "\"micro_hold\",\"decay\",\"mod_depth\",\"mod_rate\"],"
                  "\"params\":["
                    "{\"key\":\"mix\",\"label\":\"Mix\"},"
                    "{\"key\":\"loop_layer\",\"label\":\"Loop Layer\"},"
                    "{\"key\":\"grain_size\",\"label\":\"Grain Size\"},"
                    "{\"key\":\"scatter\",\"label\":\"Scatter\"},"
                    "{\"key\":\"micro_hold\",\"label\":\"Micro Hold\"},"
                    "{\"key\":\"decay\",\"label\":\"Decay\"},"
                    "{\"key\":\"mod_depth\",\"label\":\"Mod Depth\"},"
                    "{\"key\":\"mod_rate\",\"label\":\"Mod Rate\"},"
                    "{\"level\":\"settings\",\"label\":\"Settings\"}"
                  "]"
                "},"
                "\"settings\":{"
                  "\"label\":\"Settings\","
                  "\"knobs\":[],"
                  "\"params\":["
                    "{\"key\":\"loop_length\",\"label\":\"Loop Length\"},"
                    "{\"key\":\"mod_shape\",\"label\":\"Mod Shape\"},"
                    "{\"key\":\"mod_sync\",\"label\":\"Tempo Sync\"},"
                    "{\"key\":\"lofi_tails\",\"label\":\"Lo-Fi Tails\"}"
                  "]"
                "}"
              "}"
            "}");
    }

    if (n < 0) return -1;
    if (n >= buf_len) return buf_len - 1;
    return n;
}

/* --- MIDI: ignored in phase 1 (doubletap detection is phase 8) --- */

static void amb_on_midi(void *vp, const uint8_t *msg, int len, int source) {
    (void)vp; (void)msg; (void)len; (void)source;
}

/* --- API surface --- */

static audio_fx_api_v2_t API = {
    .api_version     = AUDIO_FX_API_VERSION_2,
    .create_instance = amb_create,
    .destroy_instance = amb_destroy,
    .process_block   = amb_process,
    .set_param       = amb_set_param,
    .get_param       = amb_get_param,
    .on_midi         = amb_on_midi,
};

audio_fx_api_v2_t* move_audio_fx_init_v2(const host_api_v1_t *host) {
    g_host = host;
    return &API;
}
