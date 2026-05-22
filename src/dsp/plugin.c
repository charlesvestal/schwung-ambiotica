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

#define AMB_SAMPLE_RATE 44100
#define AMB_LOOPER_SECONDS 6

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const host_api_v1_t *g_host = NULL;

#define AMB_MODE_COUNT 4
static const char *AMB_MODE_NAMES[AMB_MODE_COUNT] = {
    "Loona", "Mismember", "NAPS", "Flow"
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

    int   mix_kill_dry;
    int   grain_glitchy;
    int   micro_freeze;
    int   decay_infinite;
    int   mod_sync;
    int   mod_shape;     /* 0=sine, 1=warp, 2=sink */

    int   mode;

    /* Stage 1 — looper. */
    looper_t *looper;
    /* Stage 2 — granular. */
    granular_t *granular;
    /* Stage 3 — micro-loop (freeze layer). */
    microloop_t *microloop;
    /* Stage 4 — reverb. */
    reverb_t *reverb;
} amb_instance_t;

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
    /* Defaults match the Loona preset (mode 0) row of the design doc. */
    inst->mix = 0.50f;
    inst->loop_layer = 0.70f;
    inst->grain_size = 0.90f;
    inst->scatter = 0.05f;
    inst->micro_hold = 0.10f;
    inst->decay = 0.30f;
    inst->mod_depth = 0.15f;
    inst->mod_rate = 0.20f;
    inst->mode = 0;
    inst->mix_current = inst->mix;

    inst->looper = looper_create(AMB_LOOPER_SECONDS * AMB_SAMPLE_RATE);
    if (!inst->looper) { free(inst); return NULL; }
    looper_set_layer(inst->looper, inst->loop_layer);

    inst->granular = granular_create();
    if (!inst->granular) { looper_destroy(inst->looper); free(inst); return NULL; }
    granular_set_grain_size(inst->granular, inst->grain_size);
    granular_set_scatter(inst->granular, inst->scatter);
    granular_set_glitchy(inst->granular, inst->grain_glitchy);

    inst->microloop = microloop_create();
    if (!inst->microloop) {
        looper_destroy(inst->looper);
        granular_destroy(inst->granular);
        free(inst); return NULL;
    }
    microloop_set_hold(inst->microloop, inst->micro_hold);
    microloop_set_freeze(inst->microloop, inst->micro_freeze);

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
    static float rev_in_l[256], rev_in_r[256]; /* reverb input = dry + micro */
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
    if (json_get_float(val, "mod_depth",   &f) == 0) { inst->mod_depth = f; reverb_set_mod_depth(inst->reverb, f); }
    if (json_get_float(val, "mod_rate",    &f) == 0) { inst->mod_rate = f; reverb_set_mod_rate(inst->reverb, f); }
    if (json_get_int  (val, "mix_kill_dry",   &i) == 0) inst->mix_kill_dry   = i ? 1 : 0;
    if (json_get_int  (val, "grain_glitchy",  &i) == 0) { inst->grain_glitchy  = i ? 1 : 0; granular_set_glitchy(inst->granular, inst->grain_glitchy); }
    if (json_get_int  (val, "micro_freeze",   &i) == 0) { inst->micro_freeze   = i ? 1 : 0; microloop_set_freeze(inst->microloop, inst->micro_freeze); }
    if (json_get_int  (val, "decay_infinite", &i) == 0) inst->decay_infinite = i ? 1 : 0;
    if (json_get_int  (val, "mod_sync",       &i) == 0) inst->mod_sync       = i ? 1 : 0;
    if (json_get_int  (val, "mod_shape",      &i) == 0)
        inst->mod_shape = (i < 0 ? 0 : (i > 2 ? 2 : i));
    if (json_get_int  (val, "mode",           &i) == 0)
        inst->mode = (i < 0 ? 0 : (i >= AMB_MODE_COUNT ? AMB_MODE_COUNT - 1 : i));
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
        return;
    }
    if (strcmp(key, "mod_rate") == 0)      {
        inst->mod_rate = (float)atof(val);
        reverb_set_mod_rate(inst->reverb, inst->mod_rate);
        return;
    }
    if (strcmp(key, "mix_kill_dry") == 0)   { inst->mix_kill_dry   = atoi(val) ? 1 : 0; return; }
    if (strcmp(key, "grain_glitchy") == 0)  {
        inst->grain_glitchy = atoi(val) ? 1 : 0;
        granular_set_glitchy(inst->granular, inst->grain_glitchy);
        return;
    }
    if (strcmp(key, "micro_freeze") == 0)   {
        inst->micro_freeze = atoi(val) ? 1 : 0;
        microloop_set_freeze(inst->microloop, inst->micro_freeze);
        return;
    }
    if (strcmp(key, "decay_infinite") == 0) { inst->decay_infinite = atoi(val) ? 1 : 0; return; }
    if (strcmp(key, "mod_sync") == 0)       { inst->mod_sync       = atoi(val) ? 1 : 0; return; }
    if (strcmp(key, "mod_shape") == 0) {
        int s = atoi(val); inst->mod_shape = (s < 0 ? 0 : (s > 2 ? 2 : s)); return;
    }
    if (strcmp(key, "mode") == 0) {
        int m = atoi(val);
        inst->mode = (m < 0 ? 0 : (m >= AMB_MODE_COUNT ? AMB_MODE_COUNT - 1 : m));
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
    else if (strcmp(key, "mix_kill_dry") == 0)   n = snprintf(buf, buf_len, "%d", inst->mix_kill_dry);
    else if (strcmp(key, "grain_glitchy") == 0)  n = snprintf(buf, buf_len, "%d", inst->grain_glitchy);
    else if (strcmp(key, "micro_freeze") == 0)   n = snprintf(buf, buf_len, "%d", inst->micro_freeze);
    else if (strcmp(key, "decay_infinite") == 0) n = snprintf(buf, buf_len, "%d", inst->decay_infinite);
    else if (strcmp(key, "mod_sync") == 0)       n = snprintf(buf, buf_len, "%d", inst->mod_sync);
    else if (strcmp(key, "mod_shape") == 0)      n = snprintf(buf, buf_len, "%d", inst->mod_shape);
    else if (strcmp(key, "mode") == 0)        n = snprintf(buf, buf_len, "%d", inst->mode);
    else if (strcmp(key, "mode_count") == 0)  n = snprintf(buf, buf_len, "%d", AMB_MODE_COUNT);
    else if (strcmp(key, "mode_name") == 0)
        n = snprintf(buf, buf_len, "%s", AMB_MODE_NAMES[inst->mode]);
    else if (strcmp(key, "state") == 0) {
        n = snprintf(buf, buf_len,
            "{\"mode\":%d,"
            "\"mix\":%.4f,\"loop_layer\":%.4f,\"grain_size\":%.4f,\"scatter\":%.4f,"
            "\"micro_hold\":%.4f,\"decay\":%.4f,\"mod_depth\":%.4f,\"mod_rate\":%.4f,"
            "\"mix_kill_dry\":%d,\"grain_glitchy\":%d,\"micro_freeze\":%d,"
            "\"decay_infinite\":%d,\"mod_sync\":%d,\"mod_shape\":%d}",
            inst->mode,
            inst->mix, inst->loop_layer, inst->grain_size, inst->scatter,
            inst->micro_hold, inst->decay, inst->mod_depth, inst->mod_rate,
            inst->mix_kill_dry, inst->grain_glitchy, inst->micro_freeze,
            inst->decay_infinite, inst->mod_sync, inst->mod_shape);
    }
    else if (strcmp(key, "chain_params") == 0) {
        n = snprintf(buf, buf_len,
            "["
            "{\"key\":\"mode\",\"name\":\"Mode\",\"type\":\"enum\",\"options\":[\"Loona\",\"Mismember\",\"NAPS\",\"Flow\"]},"
            "{\"key\":\"mix\",\"name\":\"Mix\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"loop_layer\",\"name\":\"Loop Layer\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"grain_size\",\"name\":\"Grain Size\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"scatter\",\"name\":\"Scatter\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"micro_hold\",\"name\":\"Micro Hold\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"decay\",\"name\":\"Decay\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mod_depth\",\"name\":\"Mod Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mod_rate\",\"name\":\"Mod Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mix_kill_dry\",\"name\":\"Kill Dry\",\"type\":\"int\",\"min\":0,\"max\":1},"
            "{\"key\":\"grain_glitchy\",\"name\":\"Glitchy Grain\",\"type\":\"int\",\"min\":0,\"max\":1},"
            "{\"key\":\"micro_freeze\",\"name\":\"Infinite Hold\",\"type\":\"int\",\"min\":0,\"max\":1},"
            "{\"key\":\"decay_infinite\",\"name\":\"Infinite Decay\",\"type\":\"int\",\"min\":0,\"max\":1},"
            "{\"key\":\"mod_sync\",\"name\":\"Tempo Sync\",\"type\":\"int\",\"min\":0,\"max\":1},"
            "{\"key\":\"mod_shape\",\"name\":\"Mod Shape\",\"type\":\"enum\",\"options\":[\"Sine\",\"Warp\",\"Sink\"]}"
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
                    "{\"level\":\"alt\",\"label\":\"Alt States\"}"
                  "]"
                "},"
                "\"alt\":{"
                  "\"label\":\"Alt States\","
                  "\"knobs\":[],"
                  "\"params\":["
                    "{\"key\":\"mix_kill_dry\",\"label\":\"Kill Dry\"},"
                    "{\"key\":\"grain_glitchy\",\"label\":\"Glitchy Grain\"},"
                    "{\"key\":\"micro_freeze\",\"label\":\"Infinite Hold\"},"
                    "{\"key\":\"decay_infinite\",\"label\":\"Infinite Decay\"},"
                    "{\"key\":\"mod_sync\",\"label\":\"Tempo Sync\"},"
                    "{\"key\":\"mod_shape\",\"label\":\"Mod Shape\"}"
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
