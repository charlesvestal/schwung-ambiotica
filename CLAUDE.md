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
