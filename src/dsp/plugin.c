/* schwung-ambiotica plugin entry — phase 1 passthrough.
 *
 * All eight knob params, six alt-state params, and the mode enum are present
 * and round-trip via set_param/get_param. Audio is passed through unchanged.
 * DSP stages (looper, granular, microloop, reverb) land in subsequent phases.
 */
#include "audio_fx_api_v2.h"

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
    return inst;
}

static void amb_destroy(void *vp) {
    free(vp);
}

/* --- Audio: pure passthrough for phase 1 --- */

static void amb_process(void *vp, int16_t *audio_inout, int frames) {
    (void)vp; (void)audio_inout; (void)frames;
    /* Phase 1: leave audio_inout untouched. */
}

/* --- set_param --- */

static void amb_set_state(amb_instance_t *inst, const char *val) {
    float f; int i;
    if (json_get_float(val, "mix",         &f) == 0) inst->mix = f;
    if (json_get_float(val, "loop_layer",  &f) == 0) inst->loop_layer = f;
    if (json_get_float(val, "grain_size",  &f) == 0) inst->grain_size = f;
    if (json_get_float(val, "scatter",     &f) == 0) inst->scatter = f;
    if (json_get_float(val, "micro_hold",  &f) == 0) inst->micro_hold = f;
    if (json_get_float(val, "decay",       &f) == 0) inst->decay = f;
    if (json_get_float(val, "mod_depth",   &f) == 0) inst->mod_depth = f;
    if (json_get_float(val, "mod_rate",    &f) == 0) inst->mod_rate = f;
    if (json_get_int  (val, "mix_kill_dry",   &i) == 0) inst->mix_kill_dry   = i ? 1 : 0;
    if (json_get_int  (val, "grain_glitchy",  &i) == 0) inst->grain_glitchy  = i ? 1 : 0;
    if (json_get_int  (val, "micro_freeze",   &i) == 0) inst->micro_freeze   = i ? 1 : 0;
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
    if (strcmp(key, "loop_layer") == 0)    { inst->loop_layer = (float)atof(val); return; }
    if (strcmp(key, "grain_size") == 0)    { inst->grain_size = (float)atof(val); return; }
    if (strcmp(key, "scatter") == 0)       { inst->scatter = (float)atof(val); return; }
    if (strcmp(key, "micro_hold") == 0)    { inst->micro_hold = (float)atof(val); return; }
    if (strcmp(key, "decay") == 0)         { inst->decay = (float)atof(val); return; }
    if (strcmp(key, "mod_depth") == 0)     { inst->mod_depth = (float)atof(val); return; }
    if (strcmp(key, "mod_rate") == 0)      { inst->mod_rate = (float)atof(val); return; }
    if (strcmp(key, "mix_kill_dry") == 0)   { inst->mix_kill_dry   = atoi(val) ? 1 : 0; return; }
    if (strcmp(key, "grain_glitchy") == 0)  { inst->grain_glitchy  = atoi(val) ? 1 : 0; return; }
    if (strcmp(key, "micro_freeze") == 0)   { inst->micro_freeze   = atoi(val) ? 1 : 0; return; }
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
