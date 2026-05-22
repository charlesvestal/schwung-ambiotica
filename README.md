# schwung-ambiotica

Ambient effect chain for Schwung / Move: **rolling looper → granular shimmer →
parallel freeze layer → modulated reverb**. Four mode presets (Mismember,
Loona, NAPS, Flow), 8-knob performance layout, tempo-aware loop length.

## Build

```bash
./scripts/build.sh        # cross-compile via Docker
./scripts/install.sh      # scp to ableton@move.local
```

## What it does

A tempo-locked rolling looper, an octave-and-fifth granular shimmer, a
parallel auto-freeze layer, and an 8-comb async-modulated reverb with a
half-rate lo-fi tail option.

### Eight knobs

| Knob | Param |
|---|---|
| 1 | Mix |
| 2 | Loop Layer |
| 3 | Grain Size |
| 4 | Scatter |
| 5 | Micro Hold |
| 6 | Decay |
| 7 | Mod Depth |
| 8 | Mod Rate |

### Settings menu

- **Loop Length** — bars (0.5–8.0, default 1.5)
- **Mod Shape** — Sine / Warp / Sink
- **Tempo Sync** — lock mod rate to host BPM
- **Lo-Fi Tails** — reverb at half rate (time-stretched + bitcrush)

### Modes

| Mode | Character |
|---|---|
| Mismember | chaotic glitch / pointillistic |
| Loona | clean rolling-capture loops |
| NAPS | frozen pad under lush tail |
| Flow | pure modulated reverb |

## See

- `docs/plans/2026-05-22-ambiotica-design.md` — design + architecture
