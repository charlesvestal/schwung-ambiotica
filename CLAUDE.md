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

## Status

v0.1.0 shipped. See `docs/plans/2026-05-22-ambiotica-design.md` for the
implementation summary and known limitations.

## Stages (all shipped)

- Looper (tempo-aware, ring buffer, ~14 MB worst case)
- Granular (8-grain scheduler, octave+fifth pitch quantization, LFO-modulated)
- Micro-loop (parallel freeze layer, auto-freeze at ≥95% hold)
- Reverb (8 combs + 4 allpasses, per-comb async LFO, HPF input, lo-fi half-rate option)

## Mode presets (root list)

Mismember (glitch) → Loona (loops) → NAPS (frozen pad) → Flow (pure reverb)
