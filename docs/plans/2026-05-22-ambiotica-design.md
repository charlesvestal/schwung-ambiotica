# Ambiotica — Design

**Date:** 2026-05-22
**Status:** Design validated, ready for implementation
**Repo:** `schwung-ambiotica` (external audio FX module, installed via Module Store)

## Inspiration

Ambiotica fuses the character of four ambient pedals into one unified DSP chain:

- **Chase Bliss Audio Blooper** — played looper with modifier/modulator section
- **Bananana Effects Mandala** — granular/chaotic textures
- **Chase Bliss Audio MOOD** — micro-looper layered under delay/reverb
- **Walrus Audio Slö** — multi-texture modulated reverb (Dark / Rise / Dream + sine/warp/sink mod)

Source pedals are inspiration only; their names do not appear in the UI. The four
mode presets are named **Loona**, **Mismember**, **NAPS**, **Flow**.

## Architecture — single continuous DSP chain

```
[stereo in] → STAGE 1: Looper → STAGE 2: Granular → STAGE 3: Micro-Looper → STAGE 4: Reverb → [mix] → [stereo out]
                                                                                       ↑
                                                                                    LFO (Mod)
```

One unified engine, four stages in series. The `Mix` knob lives at the very end (dry/wet
of the whole chain). Each stage is dry-throughable: when its key knob is at 0 the stage
is bypassed mathematically with no per-sample cost beyond a passthrough copy.

### Stage 1 — Looper (rolling capture)

~30s stereo ring buffer continuously records the input. `Loop Layer` blends a delayed
read of the buffer back into the chain. With `Layer = 0` the stage is silent / transparent.
A reverse flag flips playhead direction. A clear event (one-shot) wipes the buffer.

### Stage 2 — Granular

Pulls audio from Stage 1's output (so it grains the live signal, the rolling loop, or
both blended). `Grain Size` sets window length (~10ms–500ms, log scale). `Scatter`
randomizes per-grain pitch (±octave) and start-position (±buffer). A glitchy-window
toggle swaps the smooth Hann window for a hard rectangular one.

### Stage 3 — Micro-Looper

Short capture buffer (~50ms–4s). `Micro Hold` sweeps capture length and ramps into
infinite hold at full. Acts as a freeze layer over top of the granular smear.

### Stage 4 — Reverb

Lush stereo algorithmic reverb (long modal/diffusion design, Slö-spirited). `Decay`
controls tail length and ramps into self-oscillation at the top of its range. A shared
LFO (`Mod Depth` / `Mod Rate`) modulates **both** granular pitch and the reverb's
diffusion taps — this is what gives Ambiotica its coherent breathing character.

## Mode presets

Selecting a mode **overwrites the eight knob values** atomically (no crossfade).
Active capture buffers in the Looper and Micro-Looper are preserved across mode
switches (don't dump audio).

| Mode | Mix | Loop Layer | Grain Size | Scatter | Micro Hold | Decay | Mod Depth | Mod Rate | Character |
|------|----|----|----|----|----|----|----|----|----|
| **Loona** | 50% | 70% | 90% (≈bypass grain) | 5% | 10% | 30% | 15% | slow | Clean rolling-capture loops over a short tail. |
| **Mismember** | 50% | 40% | 25% (short) | **80%** | 20% | 50% | 50% | mid-fast | Chaotic glitch / pointillistic texture. |
| **NAPS** | 50% | 20% | 80% (long smear) | 20% | **65%** | 80% | 25% | slow | Frozen-breath sound — micro-loop holds a phrase under a long, lush tail. |
| **Flow** | 50% | 0% | bypass | 0% | 0% | **95%** | 70% | very slow | Pure modulated reverb. No capture, no grain. |

Mode selector lives at the root of `ui_hierarchy` as a `list_param` with `count_param: 4`,
`name_param: "mode_name"`.

## 8-knob performance layout

Knobs are mapped via `ui_hierarchy.knobs` (the standard Schwung array of param-key
strings). Each knob has a continuous param and a boolean/enum "alt-state" param
toggled by capacitive double-tap (notes 0–9, debounced through
`shared/input_filter.mjs`).

| # | Param key | Knob label | Range | Double-tap action | Alt-state param |
|---|---|---|---|---|---|
| 1 | `mix` | Mix | 0–1 | Kill dry (100% wet) ↔ restore | `mix_kill_dry` (bool) |
| 2 | `loop_layer` | Loop Layer | 0–1 | Clear rolling buffer (one-shot) | — |
| 3 | `grain_size` | Grain Size | 10ms–500ms (log) | Toggle grain window: smooth ↔ glitchy/rect | `grain_glitchy` (bool) |
| 4 | `scatter` | Scatter | 0–1 | Re-seed RNG (one-shot) | — |
| 5 | `micro_hold` | Micro Hold | 0–1 | ∞ hold ↔ release | `micro_freeze` (bool) |
| 6 | `decay` | Decay | 0–1 | ∞ decay (self-osc tail) ↔ restore | `decay_infinite` (bool) |
| 7 | `mod_depth` | Mod Depth | 0–1 | Tempo-sync rate on/off | `mod_sync` (bool) |
| 8 | `mod_rate` | Mod Rate | 0.05Hz–8Hz (or beat divisions when synced) | Cycle shape: sine → warp → sink → sine | `mod_shape` (0/1/2) |

**Detection window**: 350ms between touch events on the same knob counts as
double-tap. A single tap (touch without turn within 250ms) does nothing — turning is
the dominant gesture.

**Alt-state visibility**: doubletap-state params are exposed in `chain_params` as
boolean/enum types so they survive patch save/load. Shadow UI shows a small glyph
next to a knob whose alt is active (e.g. ∞ next to Decay when `decay_infinite=true`).

**One-shot doubletaps** (Loop Layer = clear, Scatter = re-seed) don't have alt-state —
they fire an internal event each time and immediately return.

## Chain integration

Ambiotica is a chainable audio FX (`component_type: "audio_fx"`,
`capabilities.chainable: true`). It loads into a Chain slot's FX position and exposes
itself through the shadow UI like any other audio FX module.

### `ui_hierarchy` (returned via `get_param`)

```json
{
  "modes": null,
  "levels": {
    "root": {
      "label": "Ambiotica",
      "list_param": "mode",
      "count_param": "mode_count",
      "name_param": "mode_name",
      "knobs": ["mix", "loop_layer", "grain_size", "scatter",
                "micro_hold", "decay", "mod_depth", "mod_rate"],
      "params": [
        {"key": "mix", "label": "Mix"},
        {"key": "loop_layer", "label": "Loop Layer"},
        {"key": "grain_size", "label": "Grain Size"},
        {"key": "scatter", "label": "Scatter"},
        {"key": "micro_hold", "label": "Micro Hold"},
        {"key": "decay", "label": "Decay"},
        {"key": "mod_depth", "label": "Mod Depth"},
        {"key": "mod_rate", "label": "Mod Rate"},
        {"level": "alt", "label": "Alt States"}
      ]
    },
    "alt": {
      "label": "Alt States",
      "params": [
        {"key": "mix_kill_dry", "label": "Kill Dry"},
        {"key": "grain_glitchy", "label": "Glitchy Grain"},
        {"key": "micro_freeze", "label": "∞ Hold"},
        {"key": "decay_infinite", "label": "∞ Decay"},
        {"key": "mod_sync", "label": "Tempo Sync"},
        {"key": "mod_shape", "label": "Mod Shape"}
      ]
    }
  }
}
```

### `chain_params`

Declares all knob params (float 0–1 with `min`/`max`/`step`), all alt-state params
(bool / enum), plus `mode` (enum with the four names). Without this, Shadow UI
doesn't know step sizes or enum labels.

### No `ui_chain.js`

Ambiotica is an audio FX, not a MIDI source — the chain host renders the standard FX
block in the chain UI. No custom fullscreen.

### Patch save/load

All knob and alt-state values are part of the slot patch JSON via `chain_host`'s
standard save mechanism. The mode name and the eight knob positions persist; raw
capture buffer audio does **not** persist (intentional — patches are configuration,
not content).

## help.json and accessibility

### `src/help.json`

Required in every Schwung module. Provides in-app help shown via the shadow UI /
chain UI help affordance.

```json
{
  "title": "Ambiotica",
  "summary": "Loop → granular → micro-loop → modulated reverb chain. Four mode presets: Loona, Mismember, NAPS, Flow.",
  "sections": [
    {"heading": "Modes", "body": "Loona — clean rolling capture..."},
    {"heading": "Knobs (1–8)", "body": "Mix | Loop Layer | Grain Size | Scatter | Micro Hold | Decay | Mod Depth | Mod Rate"},
    {"heading": "Doubletap gestures", "body": "Mix→Kill Dry. Loop Layer→Clear buffer. Grain Size→Glitchy window. Scatter→Re-seed. Micro Hold→∞ Hold. Decay→∞ Decay. Mod Depth→Tempo Sync. Mod Rate→Cycle shape."},
    {"heading": "Tips", "body": "Self-oscillation: doubletap Decay in Flow mode. For glitch beds, switch to Mismember and crank Scatter."}
  ]
}
```

### Accessibility / screen reader requirements

Every param label exposed via `chain_params` and `ui_hierarchy` is read aloud by
`shared/screen_reader.mjs` when navigating shadow UI. Requirements:

1. **No label collisions** — each `label` is unique within its hierarchy level.
2. **Alt-state announcement** — when a doubletap toggles an alt-state, emit a
   screen-reader event ("Kill Dry on", "Infinite Decay on", "Mod Shape Warp") so blind
   users hear the state change. Use the same SR hooks as other shadow chain modules
   (reference: `schwung-cloudseed` preset announcement pattern).
3. **Mode-switch announcement** — selecting a mode speaks "Mode Loona / Mismember
   / NAPS / Flow" plus a one-sentence character summary from `help.json`.
4. **Value readback** — turning a knob speaks the value with unit (e.g. "Grain Size
   120 milliseconds", "Mod Rate quarter note" when synced). Display formats declared
   via `chain_params[].display_format` and `unit`.
5. **Glyph parity** — any visual glyph in shadow UI (∞ next to Decay, etc.) has a
   textual equivalent in the SR stream. Visual indicators never carry information SR
   users can't reach.

**QA step**: manual screen-reader walkthrough on hardware as part of the release
checklist before tagging v0.1.

## Repo & build layout

Standard external module layout, modeled on `schwung-cloudseed` / `schwung-psxverb`.

```
schwung-ambiotica/
├── CLAUDE.md
├── README.md
├── release.json                          # version + download_url, auto-bumped by CI
├── src/
│   ├── module.json                       # id, name, component_type, capabilities, requires
│   ├── help.json                         # in-app help text (see above)
│   ├── ui.js                             # menu UI (boilerplate; not used in chain)
│   └── dsp/
│       ├── dsp.cpp                       # plugin_api_v2 entrypoint
│       ├── ambiotica.{cpp,h}             # top-level Engine: orchestrates stages + LFO + mix
│       ├── stage_looper.{cpp,h}          # ring buffer + reverse + clear
│       ├── stage_granular.{cpp,h}        # grain scheduler + Hann/rect window
│       ├── stage_microloop.{cpp,h}       # short-buffer freeze
│       ├── stage_reverb.{cpp,h}          # modal/diffusion reverb (Slö-spirited)
│       ├── lfo.{cpp,h}                   # sine/warp/sink shapes + tempo sync
│       └── params.{cpp,h}                # chain_params JSON + mode preset table
├── scripts/
│   ├── build.sh                          # Docker cross-compile → dist/ambiotica/ → dist/ambiotica-module.tar.gz
│   ├── install.sh                        # scp dist/ to /data/UserData/schwung/modules/audio_fxs/ambiotica/
│   └── Dockerfile                        # ARM64 toolchain
└── .github/workflows/release.yml         # tag v* → build → attach tarball → update release.json
```

### `src/module.json` key fields

```json
{
  "id": "ambiotica",
  "name": "Ambiotica",
  "component_type": "audio_fx",
  "capabilities": {
    "chainable": true,
    "audio_in": true,
    "default_forward_channel": -2
  }
}
```

### Tarball layout

`ambiotica-module.tar.gz` containing top-level `ambiotica/` directory — required by
`schwung-manager`'s `tar -xzf -C modules/audio_fx/` install path.

### Catalog entry

Added to `schwung/module-catalog.json` under `modules[]`:

```json
{
  "id": "ambiotica",
  "name": "Ambiotica",
  "description": "Loop → granular → micro-loop → modulated reverb. Four modes.",
  "author": "Charles Vestal",
  "component_type": "audio_fx",
  "github_repo": "charlesvestal/schwung-ambiotica",
  "default_branch": "main",
  "asset_name": "ambiotica-module.tar.gz",
  "min_host_version": "0.3.11"
}
```

No external assets needed (no ROMs, samples, soundfonts) — pure DSP. No `requires` field.

## Implementation phases

Incremental scaffolding — each phase is independently testable on hardware. Each
commits separately. No phase introduces silent failures (every stage should be
ear-testable when its key knob is up).

| # | Phase | What ships | How to verify |
|---|---|---|---|
| 1 | **Scaffold** | Repo + Docker + `release.yml` + `module.json` + `plugin_api_v2` skeleton (pure stereo passthrough). `chain_params` stub with the eight knob keys. | Loads in a chain slot, audio passes unmodified, screen reader announces all eight labels. |
| 2 | **Reverb stage (alone)** | Stage 4 — long modal/diffusion reverb. Decay wired. All other stages passthrough. | Mix + Decay produce a usable Slö-spirited tail. Verify CPU headroom under Link Audio rebuild mode. |
| 3 | **LFO + mod routing** | `lfo.cpp` with sine shape only; Mod Depth modulates reverb diffusion taps. | Mod Depth at 0 identical to phase 2; at full, audible movement on the tail. |
| 4 | **Looper stage** | Stage 1 — 30s stereo ring, Loop Layer feedback, reverse direction flag, clear buffer event. | Loop Layer audibly blends a delayed read. Doubletap-clear wipes audio cleanly. |
| 5 | **Granular stage** | Stage 2 — grain scheduler, Hann window, Scatter (pitch + position), glitchy window toggle. | Grain Size sweep transitions smear → pointillism. Scatter at full = chaotic. |
| 6 | **Micro-Looper stage** | Stage 3 — short capture, Micro Hold sweep, ∞ freeze alt-state. | Micro Hold ramps cleanly into freeze; ∞ Hold persists indefinitely. |
| 7 | **Mode presets** | `mode` enum param with four entries; atomic 8-knob overwrite on switch; capture buffers preserved. | Cycling Loona → Mismember → NAPS → Flow snaps to characterful starting points. |
| 8 | **Doubletap detection + alt-states** | Capacitive note 0–9 listener; 350ms window; eight alt-state params wired; one-shot vs toggle distinction; SR announcements. | Every doubletap action fires correctly and is spoken aloud. |
| 9 | **help.json + a11y polish** | Help text, screen-reader value readback with units, glyph/text parity audit, full SR walkthrough on hardware. | Manual SR pass with eyes closed — every knob/mode/alt-state reachable and announced. |
| 10 | **Catalog + release** | Add to `schwung/module-catalog.json`, tag `v0.1.0`, release notes. | `schwung-manager` installs cleanly, module appears in Audio FX list. |

### CPU concerns

Phases 2 + 5 + 6 stacked could be heavy under Link Audio rebuild mode. After phase 6,
profile on hardware and decide whether to downsample the granular stage internally
or limit reverb diffusion order. **Don't optimize earlier — measure first.**

### Host-side test harness (recommended, not blocking)

A tiny `tests/run_wav.cpp` that pipes a WAV through the engine for offline ear-testing.
Saves device round-trips during phases 2–6. Mirrors the pattern from prior DSP ports.

## Slö character backlog (post-phase-10)

Slö's three voices are Dark / Rise / Dream. Our Flow mode targets Dream
(modulated lush reverb + freeze, landing across phases 3/6/8). The other two
voices aren't in the current 10-phase scope but are worth considering once the
core chain is shipping:

- **Dark-style shimmer** — sub-octave or +1-oct injection into the reverb input.
  Implementation candidate: a tap that pitches the granular-stage output by ±12 semitones
  before it enters the reverb. Could be a mode-specific feature for Flow or a new
  alt-state on the Decay knob (e.g. triple-tap?). Adds harmonic depth that pure
  modulation can't.
- **Rise-style auto-swell** — envelope-follower on the wet bus that ramps up the
  wet level on new note attacks, then decays. Slö's Rise voice has this and it's a
  big part of why it sounds cinematic. Implementation: simple AR envelope keyed
  off input level threshold. Could live behind a Mod Depth alt-state.
- **Stretch / lo-fi tail** — Slöer's "Stretch" control runs the reverb at a
  reduced sample rate, time-stretching the algorithm and adding bitcrush
  artifacts. Implementation: internal downsampler around the reverb stage. Could
  be a future alt-state on Decay (e.g. doubletap Decay already does ∞-decay;
  a triple-tap or a separate "character" knob in a deeper menu).

None of these are blocking for v0.1.0 — Ambiotica's identity is the chain
(looper → granular → micro-loop → reverb), not Slö emulation. But each adds
character that pure modulation can't reach.
