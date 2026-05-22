# Ambiotica — Phase 1 Implementation Plan: Scaffold

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Produce a working `schwung-ambiotica` repo that builds to `dist/ambiotica-module.tar.gz`, deploys to the Move at `/data/UserData/schwung/modules/audio_fx/ambiotica/`, loads in a Chain slot as an Audio FX, and passes stereo audio through unchanged. All eight Ambiotica knob params, the six alt-state params, and the `mode` enum are present and announced by the screen reader. No DSP yet.

**Architecture:** Single-file C audio FX plugin implementing `audio_fx_api_v2_t` (the in-place stereo processor API used by `schwung-midiverb` / `schwung-cloudseed`). The plugin exposes all params via stringly-typed `set_param` / `get_param`, returns `chain_params` and `ui_hierarchy` JSON at runtime, supports `state` bulk save/restore, and `process_block` is a literal `memcpy` of input to output. The repo ships its own `Dockerfile` for ARM64 cross-compilation and a GitHub Actions workflow that releases on tag push.

**Tech Stack:** C, gcc-aarch64-linux-gnu, Docker, GitHub Actions, schwung host (`audio_fx_api_v2`).

**Working directory:** `/Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica/`

**Reference repo:** `/Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-midiverb/` — recent, uses v2 API, has chain_params + ui_hierarchy + state. Adapt patterns from there.

---

## Constraints to honor throughout

- **Realtime safety**: `process_block` runs in the SPI callback path (FIFO 90 on core 3). No `printf`, no `unified_log`, no `malloc/free`, no file I/O. Allocations only in `create_instance`.
- **Single-file DSP**: One `plugin.c` for now. Stages (looper, granular, microloop, reverb) split into separate files in later phases.
- **No `ui.js` needed**: Audio FX in chain context are driven by `ui_hierarchy` JSON returned via `get_param`. The chain host renders the standard FX block. See `schwung-midiverb` — also no `ui.js`.
- **Install path**: `/data/UserData/schwung/modules/audio_fx/ambiotica/`. NOT `/data/UserData/move-anything/...` (that path is legacy from before the rename).
- **help.json schema**: Hierarchical `{title, children: [{title, lines: [...]}]}` — NOT the flat `{sections: [...]}` format shown in the design doc. See `schwung-cloudseed/src/help.json` for the canonical shape; update as we go.
- **Don't push to GitHub in phase 1**: We're scaffolding locally. Tag/release is the last step and will be done manually after hardware verification.

---

## Task list (sequence, each independently testable)

### Task 1: Repo plumbing — `.gitignore`, `LICENSE`, `README.md`, `CLAUDE.md`

**Files:**
- Create: `schwung-ambiotica/.gitignore`
- Create: `schwung-ambiotica/LICENSE`
- Create: `schwung-ambiotica/README.md`
- Create: `schwung-ambiotica/CLAUDE.md`

**Step 1: Write `.gitignore`**

```
build/
dist/
*.so
.DS_Store
__pycache__/
*.pyc
```

**Step 2: Write `LICENSE`** — MIT, attribution to Charles Vestal. (Copy `schwung-midiverb/LICENSE` and update year/holder.)

**Step 3: Write `README.md`**

```markdown
# schwung-ambiotica

Ambient effect chain for Schwung / Move: rolling looper → granular → micro-looper
→ modulated reverb. Four mode presets (Loona, Mismember, NAPS, Flow), 8-knob
performance layout, capacitive double-tap gestures.

Ambient effect chain.

## Build

```bash
./scripts/build.sh        # cross-compile via Docker
./scripts/install.sh      # scp to ableton@move.local
```

## Status

Phase 1: scaffold (passthrough). DSP stages implemented in subsequent phases.

See `docs/plans/2026-05-22-ambiotica-design.md` for the design.
```

**Step 4: Write minimal `CLAUDE.md`** — points future-Claude at the design doc.

```markdown
# CLAUDE.md

Schwung audio FX module: rolling looper → granular → micro-looper → modulated reverb.

## Quick reference

- **Design doc:** `docs/plans/2026-05-22-ambiotica-design.md`
- **API:** `audio_fx_api_v2_t` (in-place stereo, single instance per chain slot)
- **DSP entry:** `src/dsp/plugin.c` → `move_audio_fx_init_v2`
- **Install path on device:** `/data/UserData/schwung/modules/audio_fx/ambiotica/`
- **Build:** `./scripts/build.sh` (Docker cross-compile to ARM64)
- **Deploy:** `./scripts/install.sh` (scp + chmod)

## Realtime safety

`process_block` is in the SPI callback path. No printf, no malloc, no file I/O.
Allocations only in `create_instance`. See parent `schwung/CLAUDE.md` §
"Realtime Safety".

## Phases

1. Scaffold (passthrough) ← current
2. Reverb stage alone
3. LFO + mod routing
4. Looper stage
5. Granular stage
6. Micro-Looper stage
7. Mode presets (atomic 8-knob overwrite)
8. Doubletap detection + alt-states + SR announcements
9. help.json + a11y polish
10. Catalog + release
```

**Step 5: Verify and commit**

```bash
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica
ls .gitignore LICENSE README.md CLAUDE.md
git add .gitignore LICENSE README.md CLAUDE.md
git commit -m "Add repo plumbing (license, readme, CLAUDE.md, gitignore)"
```

Expected: 4 files staged, commit succeeds.

---

### Task 2: Copy plugin API headers from schwung-midiverb

**Files:**
- Create: `schwung-ambiotica/src/dsp/audio_fx_api_v2.h` (copy from midiverb)
- Create: `schwung-ambiotica/src/dsp/plugin_api_v1.h` (copy from midiverb)

These headers are vendored from the schwung host. Both midiverb and cloudseed
keep their own copies; we do the same so the module is self-contained.

**Step 1: Copy headers**

```bash
mkdir -p /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica/src/dsp
cp /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-midiverb/src/dsp/audio_fx_api_v2.h \
   /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica/src/dsp/
cp /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-midiverb/src/dsp/plugin_api_v1.h \
   /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica/src/dsp/
```

**Step 2: Verify and commit**

```bash
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica
git add src/dsp/audio_fx_api_v2.h src/dsp/plugin_api_v1.h
git commit -m "Vendor audio_fx_api_v2.h and plugin_api_v1.h from schwung host"
```

Expected: 2 files staged, commit succeeds.

---

### Task 3: `src/module.json` — metadata

**Files:**
- Create: `schwung-ambiotica/src/module.json`

Minimal metadata only. `chain_params` and `ui_hierarchy` come from `get_param`
at runtime, NOT from module.json — that's the v2 pattern (see midiverb).

**Step 1: Write `src/module.json`**

```json
{
  "id": "ambiotica",
  "name": "Ambiotica",
  "abbrev": "AMB",
  "version": "0.1.0",
  "description": "Ambient chain: rolling looper → granular → micro-loop → modulated reverb. Four modes (Loona, Mismember, NAPS, Flow).",
  "author": "Charles Vestal",
  "license": "MIT",
  "dsp": "ambiotica.so",
  "api_version": 2,
  "capabilities": {
    "chainable": true,
    "component_type": "audio_fx"
  }
}
```

**Step 2: Verify and commit**

```bash
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica
git add src/module.json
git commit -m "Add module.json (audio_fx, chainable, api_version 2)"
```

Expected: 1 file staged, commit succeeds.

---

### Task 4: `src/help.json` — initial help text

**Files:**
- Create: `schwung-ambiotica/src/help.json`

Use the canonical hierarchical schema (children → children → lines). Reference:
`schwung-cloudseed/src/help.json`.

**Step 1: Write `src/help.json`**

```json
{
  "title": "Ambiotica",
  "children": [
    {
      "title": "Overview",
      "lines": [
        "Ambient chain:",
        "looper to granular",
        "to micro-loop to",
        "modulated reverb.",
        "",
        "Four mode presets",
        "(Loona, Mismember,",
        "NAPS, Flow) reset",
        "the 8 knobs to a",
        "character starting",
        "point."
      ]
    },
    {
      "title": "Knobs",
      "children": [
        {
          "title": "Knob Mapping",
          "lines": [
            "1 Mix",
            "2 Loop Layer",
            "3 Grain Size",
            "4 Scatter",
            "5 Micro Hold",
            "6 Decay",
            "7 Mod Depth",
            "8 Mod Rate"
          ]
        },
        {
          "title": "Double-tap",
          "lines": [
            "Mix: Kill Dry",
            "Loop Layer: Clear",
            "Grain Size: Glitch",
            "Scatter: Re-seed",
            "Micro Hold: Hold",
            "Decay: Self-osc",
            "Mod Depth: Sync",
            "Mod Rate: Shape"
          ]
        }
      ]
    },
    {
      "title": "Modes",
      "lines": [
        "Loona: rolling",
        " capture loops",
        "",
        "Mismember: glitch",
        " grain textures",
        "",
        "NAPS: frozen breath",
        " under lush tail",
        "",
        "Flow: pure modulated",
        " reverb (no capture)"
      ]
    }
  ]
}
```

**Step 2: Verify and commit**

```bash
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica
git add src/help.json
git commit -m "Add help.json (overview, knob map, double-tap, modes)"
```

Expected: 1 file staged, commit succeeds.

---

### Task 5: `src/dsp/plugin.c` — passthrough plugin with full param surface

**Files:**
- Create: `schwung-ambiotica/src/dsp/plugin.c`

This is the heart of phase 1. The plugin must:

1. Implement `audio_fx_api_v2_t` with `create_instance`, `destroy_instance`,
   `process_block`, `set_param`, `get_param`, `on_midi`.
2. Carry all 8 knob params, all 6 alt-state params, and a `mode` enum (0–3).
3. Return `chain_params` JSON listing every param with type/min/max/step/unit.
4. Return `ui_hierarchy` JSON with root + alt sublevel, knobs[8], mode list at root.
5. Return `state` JSON for slot autosave; accept `state` JSON for restore.
6. Return `mode_count`, `mode_name` for the root-level mode list (Schwung pattern).
7. `process_block` is a pure passthrough: `audio_inout` is left unchanged.

**Step 1: Write `src/dsp/plugin.c`**

```c
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
    /* Eight knob params (all 0–1 normalized). */
    float mix;
    float loop_layer;
    float grain_size;
    float scatter;
    float micro_hold;
    float decay;
    float mod_depth;
    float mod_rate;

    /* Six alt-state params. */
    int   mix_kill_dry;     /* bool 0/1 */
    int   grain_glitchy;    /* bool 0/1 */
    int   micro_freeze;     /* bool 0/1 */
    int   decay_infinite;   /* bool 0/1 */
    int   mod_sync;         /* bool 0/1 */
    int   mod_shape;        /* enum 0=sine, 1=warp, 2=sink */

    /* Mode preset (0–3). */
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
    /* Defaults match the Loona preset (mode 0) from the design doc.
     * Mode preset application is a phase-7 concern; for now we just seed
     * sensible 0.5/0 starting values matching the design's first row. */
    inst->mix = 0.50f;
    inst->loop_layer = 0.70f;
    inst->grain_size = 0.90f;
    inst->scatter = 0.05f;
    inst->micro_hold = 0.10f;
    inst->decay = 0.30f;
    inst->mod_depth = 0.15f;
    inst->mod_rate = 0.20f;  /* "slow" — 20% of range */
    inst->mode = 0;          /* Loona */
    return inst;
}

static void amb_destroy(void *vp) {
    free(vp);
}

/* --- Audio: pure passthrough for phase 1 --- */

static void amb_process(void *vp, int16_t *audio_inout, int frames) {
    (void)vp; (void)audio_inout; (void)frames;
    /* Phase 1: do nothing. Audio is already in place; leaving it alone IS
     * the passthrough. Future phases will read+write audio_inout in-place. */
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
        /* Phase 7 will overwrite the eight knob values here. Phase 1 just
         * records the selection so get_param("mode") round-trips. */
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
```

**Step 2: Commit (just the source — build comes in next task)**

```bash
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica
git add src/dsp/plugin.c
git commit -m "Add plugin.c passthrough with full param surface"
```

Expected: 1 file staged, commit succeeds. No build yet.

---

### Task 6: `scripts/Dockerfile` + `scripts/build.sh`

**Files:**
- Create: `schwung-ambiotica/scripts/Dockerfile`
- Create: `schwung-ambiotica/scripts/build.sh` (executable)

Adapted from `schwung-midiverb/scripts/`. Simpler — single-file source, no
extra .c files yet.

**Step 1: Write `scripts/Dockerfile`** (identical pattern to midiverb)

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    make \
    && rm -rf /var/lib/apt/lists/*

ENV CROSS_PREFIX=aarch64-linux-gnu-

WORKDIR /build
```

**Step 2: Write `scripts/build.sh`**

```bash
#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="schwung-ambiotica-builder"

if [ -z "$CROSS_PREFIX" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== Building Ambiotica Module (via Docker) ==="
    if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
    fi
    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        "$IMAGE_NAME" \
        ./scripts/build.sh
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
cd "$REPO_ROOT"

mkdir -p build dist/ambiotica

${CROSS_PREFIX}gcc -O3 -shared -fPIC \
    -march=armv8-a -mtune=cortex-a72 \
    -ffast-math -fomit-frame-pointer -fno-stack-protector \
    -DNDEBUG \
    -Isrc/dsp \
    src/dsp/plugin.c \
    -o build/ambiotica.so \
    -lm

cp src/module.json dist/ambiotica/module.json
[ -f src/help.json ] && cp src/help.json dist/ambiotica/help.json
cp build/ambiotica.so dist/ambiotica/ambiotica.so
[ -f LICENSE ] && cp LICENSE dist/ambiotica/LICENSE
chmod +x dist/ambiotica/ambiotica.so

cd dist
tar -czvf ambiotica-module.tar.gz ambiotica/
echo "OK: dist/ambiotica-module.tar.gz"
```

**Step 3: Make build.sh executable**

```bash
chmod +x /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica/scripts/build.sh
```

**Step 4: Run the build to verify**

```bash
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica
./scripts/build.sh
```

Expected: Docker image builds (first run only, ~1–2 min), then compiles
`ambiotica.so` and produces `dist/ambiotica-module.tar.gz`. No warnings.

If gcc emits warnings, fix them before continuing — usually unused parameters
(`(void)param;` casts) or missing `<stdio.h>` includes.

**Step 5: Verify tarball contents**

```bash
tar -tzf /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica/dist/ambiotica-module.tar.gz
```

Expected output (order may vary):

```
ambiotica/
ambiotica/module.json
ambiotica/help.json
ambiotica/ambiotica.so
ambiotica/LICENSE
```

Top-level directory must be exactly `ambiotica/` — `schwung-manager` extracts
into `modules/audio_fx/` with `tar -xzf -C ...` and trusts the tarball
to provide the right top-level dir name.

**Step 6: Commit**

```bash
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica
git add scripts/Dockerfile scripts/build.sh
git commit -m "Add cross-compile build (Docker + ARM64 gcc)"
```

---

### Task 7: `scripts/install.sh`

**Files:**
- Create: `schwung-ambiotica/scripts/install.sh` (executable)

**Step 1: Write `scripts/install.sh`**

```bash
#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$REPO_ROOT"

if [ ! -d "dist/ambiotica" ]; then
    echo "Run ./scripts/build.sh first."
    exit 1
fi

ssh ableton@move.local "mkdir -p /data/UserData/schwung/modules/audio_fx/ambiotica"
scp -r dist/ambiotica/* ableton@move.local:/data/UserData/schwung/modules/audio_fx/ambiotica/
ssh ableton@move.local "chmod -R a+rw /data/UserData/schwung/modules/audio_fx/ambiotica"

echo "Installed to /data/UserData/schwung/modules/audio_fx/ambiotica/"
echo "Trigger a host rescan (Module Store > Refresh, or restart Schwung) to load."
```

**Step 2: Make executable and commit**

```bash
chmod +x /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica/scripts/install.sh
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica
git add scripts/install.sh
git commit -m "Add install.sh (scp to /data/UserData/schwung/modules/audio_fx/)"
```

---

### Task 8: `release.json` + `.github/workflows/release.yml`

**Files:**
- Create: `schwung-ambiotica/release.json`
- Create: `schwung-ambiotica/.github/workflows/release.yml`

Use the same workflow pattern as schwung-cloudseed. The workflow:
1. Verifies tag version matches `src/module.json` version.
2. Builds the tarball via Docker.
3. Attaches the tarball to the GitHub release.
4. Rewrites `release.json` on `main` with the new version + download URL.

**Step 1: Write `release.json`** (placeholder — workflow rewrites on tag push)

```json
{
  "version": "0.1.0",
  "download_url": "https://github.com/charlesvestal/schwung-ambiotica/releases/download/v0.1.0/ambiotica-module.tar.gz"
}
```

**Step 2: Write `.github/workflows/release.yml`**

```bash
mkdir -p /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica/.github/workflows
```

```yaml
name: Release

on:
  push:
    tags:
      - 'v*'

permissions:
  contents: write

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Verify version match
        run: |
          TAG_VERSION="${GITHUB_REF_NAME#v}"
          MODULE_VERSION=$(grep '"version"' src/module.json | head -1 | sed 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/')
          if [ "$TAG_VERSION" != "$MODULE_VERSION" ]; then
            echo "ERROR: Tag version ($TAG_VERSION) does not match module.json version ($MODULE_VERSION)"
            exit 1
          fi
          echo "Version check passed: $TAG_VERSION"

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3

      - name: Build with Docker
        run: |
          docker build -t module-builder -f scripts/Dockerfile .
          docker run --rm -v "$PWD:/build" -w /build module-builder ./scripts/build.sh

      - name: Create Release
        uses: softprops/action-gh-release@v1
        with:
          files: dist/ambiotica-module.tar.gz
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}

      - name: Commit release.json
        run: |
          VERSION="${GITHUB_REF_NAME#v}"
          git fetch origin main
          git checkout -f main
          git config user.name "github-actions[bot]"
          git config user.email "github-actions[bot]@users.noreply.github.com"
          cat > release.json <<EOF
          {
            "version": "${VERSION}",
            "download_url": "https://github.com/${{ github.repository }}/releases/download/${{ github.ref_name }}/ambiotica-module.tar.gz"
          }
          EOF
          git add release.json
          git commit -m "chore: update release.json for ${{ github.ref_name }}" || echo "No changes to commit"
          git push origin main
```

**Step 3: Commit**

```bash
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica
git add release.json .github/workflows/release.yml
git commit -m "Add release workflow + initial release.json (v0.1.0 placeholder)"
```

---

### Task 9: Hardware verification — load passthrough in chain slot

This is the gate that closes phase 1. Without this, nothing later matters.

**Step 1: Deploy**

```bash
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica
./scripts/install.sh
```

Expected: scp succeeds, install completes.

**Step 2: Trigger Schwung host to rescan modules**

Either restart Schwung on the Move (the heavier option), or trigger a rescan
through the on-device UI (Module Store → Refresh).

From host machine, the easy path is restart:

```bash
ssh ableton@move.local "systemctl --user restart schwung || true"
```

Wait 5–10 seconds.

**Step 3: Verify the module is discovered**

```bash
ssh ableton@move.local "ls -la /data/UserData/schwung/modules/audio_fx/ambiotica/"
```

Expected: `module.json`, `help.json`, `ambiotica.so`, `LICENSE`. The `.so` is
executable and ~tens of KB.

**Step 4: Load Ambiotica into a chain slot on hardware**

On the Move:

1. Long-press Track 1 (or Shift+Vol+Track 1) to open slot editor.
2. Pick a sound generator (any built-in, e.g. SF2) so the chain has audio to process.
3. Navigate to FX slot 2 (or wherever an audio FX goes — slot editor's FX position).
4. Choose **Ambiotica** from the audio FX list.
5. Hit a pad — does audio play?

Expected: **Audio plays unchanged.** The synth's output reaches the speakers
exactly as if Ambiotica weren't there. Mix knob has no audible effect (because
the engine is passthrough, but the mix value is being received and stored).

**Step 5: Verify knob layout in shadow UI**

While the chain slot is focused with Ambiotica active:

- The eight knobs should map to: Mix, Loop Layer, Grain Size, Scatter,
  Micro Hold, Decay, Mod Depth, Mod Rate.
- Turning each knob displays the param name and a value 0.00–1.00.
- Pressing jog click to descend into "Alt States" should reveal the six
  alt-state params (Kill Dry, Glitchy Grain, Infinite Hold, Infinite Decay,
  Tempo Sync, Mod Shape).
- At the root level, scrolling the mode list should show Loona / Mismember /
  NAPS / Flow.

If any of these are missing or wrong — the `chain_params` or `ui_hierarchy`
JSON strings have a typo. Tail the host log to see what Schwung is parsing:

```bash
ssh ableton@move.local "touch /data/UserData/schwung/debug_log_on && tail -f /data/UserData/schwung/debug.log"
```

**Step 6: Verify screen reader announces every label**

If screen reader is enabled in Global Settings → Accessibility (or whatever
the toggle is called), turning each of the eight knobs and entering each
hierarchy level should produce a spoken label. Every name should be unique
and matching the design.

**Step 7: Verify state save/restore round-trip**

1. Set Mix to ~0.42 by turning knob 1.
2. Switch the slot's synth to something else (forces a re-save).
3. Switch back. Mix should still be ~0.42.

If state doesn't round-trip, autosave isn't picking up `get_param("state")` —
inspect the returned JSON manually:

```bash
# On the host machine, you can also test the .so locally by writing a
# tiny C harness, but that's overkill for phase 1. The state JSON is
# inspectable in /data/UserData/schwung/patches/slot_*.json after autosave.
```

**Step 8: Document the verification**

Add a single sentence to README confirming phase 1 verified on hardware:

```bash
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-ambiotica
# Append to README.md under Status:
# "Phase 1 verified on hardware YYYY-MM-DD."
git add README.md
git commit -m "Note phase 1 verified on hardware"
```

---

## Done criteria for phase 1

All of:

- [x] `dist/ambiotica-module.tar.gz` builds without warnings.
- [x] Tarball has the right top-level `ambiotica/` directory.
- [x] Installs to `/data/UserData/schwung/modules/audio_fx/ambiotica/` cleanly.
- [x] Loads in a Chain slot's audio FX position.
- [x] Audio passes through unchanged (synth audible, Mix knob has no audible effect yet — that's correct for passthrough).
- [x] Shadow UI exposes the eight knobs in the right order.
- [x] Alt-state sublevel shows six items.
- [x] Mode list shows Loona / Mismember / NAPS / Flow.
- [x] State round-trips across a slot switch.
- [x] Screen reader announces every label.

Once all green, phase 2 (reverb stage alone) starts in a fresh planning pass.

## What we are NOT doing in phase 1

- No DSP whatsoever. process_block is a no-op.
- No mode-switching behavior (just stores the mode index).
- No double-tap detection (alt-states are settable via shadow UI menu only).
- No tempo sync logic.
- No CPU profiling — passthrough has effectively zero cost.
- No host-side WAV test harness — useful in phase 2+ when DSP exists, premature now.
- No GitHub release / tag push — we're scaffolding locally.
- No catalog entry in `schwung/module-catalog.json` — that's phase 10.

## Why a phase-1 plan looks small but isn't

The deceptive part is task 5 (`plugin.c`). It's one file but it contains the
full param + JSON surface (chain_params, ui_hierarchy, state) that everything
downstream relies on. Get the JSON strings right here and phases 2–8 just fill
in DSP behavior. Get them wrong and you'll be chasing UI bugs through every
later phase.

If `chain_params` types/ranges drift from what the eventual DSP expects, fix
them HERE rather than carrying tech debt forward.
