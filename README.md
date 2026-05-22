# schwung-ambiotica

Ambient effect chain for Schwung / Move: rolling looper → granular → micro-looper
→ modulated reverb. Four mode presets (Loona, Mismember, NAPS, Flow), 8-knob
performance layout, capacitive double-tap gestures.

Inspired by Chase Bliss Audio Blooper, Bananana Effects Mandala, Chase Bliss
Audio MOOD, and Walrus Audio Slö.

## Build

```bash
./scripts/build.sh        # cross-compile via Docker
./scripts/install.sh      # scp to ableton@move.local
```

## Status

Phase 1 (scaffold / passthrough) verified on hardware 2026-05-22. DSP stages
implemented in subsequent phases.

See `docs/plans/2026-05-22-ambiotica-design.md` for the design.
