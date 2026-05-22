# Ambiotica — Design

**Date:** 2026-05-22 (initial), updated through v0.1.0 ship
**Status:** v0.1.0 shipped
**Repo:** `schwung-ambiotica` (external audio FX module, installed via Module Store)

## Concept

Ambient-texture multi-effect for Schwung / Move: **rolling looper → granular
shimmer → parallel freeze layer → modulated reverb** in a single continuous
chain, with 8 performance knobs, 4 mode presets, and a small Settings menu.

## Architecture

```
[stereo in] ─┬─→ STAGE 1: Looper ──→ loop_l ─┐
             │                                │
             │   STAGE 2: Granular(loop_l) ──→ gran_l
             │                                │
             │   layered = clean·loop + shimmer·gran   (Scatter crossfade)
             │                                │
             ├─→ STAGE 3: Microloop(dry) ──→ micro_l  (parallel freeze)
             │                                │
             ↓                                ↓
       [reverb_in = dry + layered + micro] → STAGE 4: Reverb → wet_l
                                                              │
       wet_bus = layered + micro + wet_l                      │
       out     = (1-mix)·dry + mix·wet_bus ───────────────────┘
```

Each stage is mathematically transparent when its key knob is at 0.

### Stage 1 — Looper

Tempo-aware ring buffer (sized by `loop_length_bars × beats × 60 / BPM`,
default 1.5 bars). `Loop Layer` (knob² curve, cap 0.95) controls feedback
between buffer and chain — sets both the per-pass loop volume and how long
the loop persists. Output is the loop signal only; dry passes through
separately in the final mix. Live BPM tracking adjusts the loop length when
the host tempo changes.

### Stage 2 — Granular

8-grain scheduler over a 2-second capture of the loop signal. `Grain Size`
sets window length (log scale, 10ms–500ms). `Scatter` is dual-purpose:

- 0 → 0.5: clean loop signal at full, granular layered on top (shimmer
  amount = scatter)
- 0.5 → 1.0: clean loop fades out, granular replaces it (scatter "into
  destruction")

Per-grain pitch is quantized to a power-chord set: unison, ±octave, ±perfect
fifth. Probability of leaving unison scales with Scatter — always musical
regardless of source key. A shared LFO (rate matched to reverb's) bends grain
pitch up to ±100 cents at `Mod Depth = 1`.

### Stage 3 — Micro-loop

Runs **parallel** to Stages 1–2 — captures the dry signal directly so freeze
works even with Loop Layer at 0. `Micro Hold` sweeps loop length log-scale
(100ms–4s) and at ≥ 95% auto-engages freeze (buffer stops updating,
captured content keeps looping). Crossfade (~12ms equal-power) hides
loop-length changes. Output is loop content only; dry passes through.

### Stage 4 — Reverb

8 parallel damped combs (delays ~50–74 ms) → 4 serial allpasses, with true
stereo via +37-sample spread. Input HPF at ~80 Hz keeps low-end from
piling up in the comb feedback. Each comb has its own LFO at a slightly
different rate (decorrelated async modulation = continuously evolving
tail). `Decay` maps perceptually (knob^0.4) from ~250 ms to 15 s+.
`Mod Depth` modulates comb read positions; `Mod Rate` sets LFO speed
(log 0.05–8 Hz, or beat-locked via Tempo Sync setting).

## Performance knobs (8)

| # | Knob | Range |
|---|---|---|
| 1 | Mix | dry ↔ wet |
| 2 | Loop Layer | feedback (knob² → 0..0.95) |
| 3 | Grain Size | 10ms..500ms (log) |
| 4 | Scatter | crossfade clean↔shimmer + pitch-quant probability |
| 5 | Micro Hold | 100ms..4s + auto-freeze ≥ 95% |
| 6 | Decay | ~250ms..15s+ (perceptual curve) |
| 7 | Mod Depth | reverb wobble + grain pitch |
| 8 | Mod Rate | 0.05..8 Hz (free) or beat-locked |

All eight knobs are smoothed per-sample (~20 ms time constant) to prevent
abrupt-change clicks from getting baked into the looper/reverb feedback.

## Mode presets

Mode selector lives at the root of `ui_hierarchy`. Selecting overwrites the
eight knob values atomically; capture buffers are preserved across switches.

| Mode | Mix | LL | GS | Sc | MH | Dy | MD | MR | Character |
|------|----|----|----|----|----|----|----|----|---|
| **Mismember** | 0.50 | 0.87 | 0.25 | 0.80 | 0.18 | 0.80 | 0.50 | 0.60 | chaotic glitch / pointillistic |
| **Loona** | 0.50 | 0.95 | 0.90 | 0.05 | 0.10 | 0.15 | 0.15 | 0.20 | clean rolling-capture loops |
| **NAPS** | 0.50 | 0.20 | 0.80 | 0.20 | 0.65 | 0.80 | 0.25 | 0.15 | frozen pad under lush tail |
| **Flow** | 0.50 | 0.00 | 0.10 | 0.00 | 0.00 | 0.95 | 0.70 | 0.10 | pure modulated reverb |

## Settings menu

Persistent settings — not touched by mode preset application.

| Setting | Range | Default | Note |
|---|---|---|---|
| Loop Length | 0.5 – 8.0 bars (step 0.5) | 1.5 bars | Tempo-aware; live-tracks BPM |
| Mod Shape | Sine / Warp / Sink | Sine | Warp = DC-biased up, Sink = DC-biased down |
| Tempo Sync | on/off | off | Lock mod rate to beat divisions via host BPM |
| Lo-Fi Tails | on/off | off | Reverb at half rate → doubled delays + bitcrush |

## Implementation summary

- **Plugin API:** `audio_fx_api_v2_t` (in-place stereo, single-instance per
  chain slot). Entry `move_audio_fx_init_v2`.
- **Files:** `src/dsp/{plugin,looper,granular,microloop,reverb,lfo}.c`
- **Smoothing:** every knob param uses a ~20 ms one-pole ramp; loop_length
  uses a ~12 ms equal-power crossfade between read positions.
- **Realtime contract:** all allocations in `create_instance`; `process_block`
  has no malloc, no I/O.
- **Capture buffer sizes:** looper up to 32s (8 bars @ 60 BPM), granular 2s,
  microloop 4s. Total ~14 MB per instance worst case.

## Chain integration

- `component_type: "audio_fx"`, `capabilities.chainable: true`
- `ui_hierarchy` root has `list_param: "mode"` (the 4 mode names).
- Knob array: `[mix, loop_layer, grain_size, scatter, micro_hold, decay, mod_depth, mod_rate]`
- `settings` sub-level: Loop Length, Mod Shape, Tempo Sync, Lo-Fi Tails

## Known limitations / future work

- **Looper silence-skip bug** — schwung's shim skips FX after ~1s of
  silent output, causing looper.pos to freeze. Fix needs a `requires_
  continuous_processing` capability flag respected by the shim's idle
  detection. See `prompts/` (in parent repo) for the host-side fix prompt.
- **Knob value drift on mode switch** — shadow UI caches knob positions
  and doesn't re-read after `set_param("mode")`. Host-side fix needed.
- **Sub-octave shimmer** (a "Dark" texture) — was prototyped, removed in
  v0.1 because the contribution didn't feel musically distinctive enough.
  Could revisit as a separate "Shimmer" feature in v0.2.
- **Auto-swell envelope on wet bus** — same — prototyped and removed for
  same reason.
- **Daisy Seed port** — feasible (32-bit FPU, 64 MB SDRAM). Needs static
  buffer allocation, possibly reduce max looper to 16s.
