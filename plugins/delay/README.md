# Delay

A new take on the OP-1 Delay effect, as a plugin. Four controls, one shared DSP core, every format from one repo.

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+), Windows | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE |
| VST2 | Akai MPC OS standalone devices (Gen1 armv7, Gen2 aarch64) | `mpc/`, built with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) |

## Controls

Names and defaults come from the OP-1 (factory values 8000, 8000, 8000, 8000 of 32767).

| Control | What it does |
|---|---|
| TIME | Loop length at full tape speed, 31 ms to 1 s. |
| SPEED | Tape speed. Delay time is loop length divided by speed, capped at 8 s. Slower tape is darker and bends the pitch of echoes while you turn it. |
| FDB | Feedback. At full the echoes do not decay. |
| INPUT | How much of the signal goes into the tape. The dry signal is always at unity. |

At the defaults the delay is about 0.3 s. The TIME readout shows the real delay, which also depends on SPEED.

## DSP

`core/delay_core.c` is plain C99 with no allocation (the caller supplies the delay memory). It is written from scratch as float
DSP. It is not a bit-exact port of the OP-1 fixed-point code, and it carries no firmware code or tables. See `docs/DSP.md`.

## Build

macOS, all formats (from the repo root):

```sh
cmake -S . -B build/delay -G Ninja -DCMAKE_BUILD_TYPE=Release -DOP1FX_ONLY=delay
cmake --build build/delay
```

Set `-DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/JUCE` to reuse a local JUCE 8.0.8 checkout.

Core tests (sanitizers on):

```sh
core/tests/run.sh
```

Editor snapshot (dev tool): `cmake --build build/delay --target DelaySnapshot`, then run `DelaySnapshot out.png 1280 0.8`.

MPC VST2, with a checkout of mpc-vst-plugins (needs Docker):

```sh
../../../mpc-re/mpc-vst-plugins/tools/test_port.sh mpc/vst.json
../../../mpc-re/mpc-vst-plugins/tools/build_port.sh mpc/vst.json all
```

## Design

Faceplate layout is in the Claude Design project "Delay faceplate". The tape machine art is generated with Higgsfield and lives
in `design/art/`. `faceplate-bg-1280x630.png` is the shared background for the plugin window and the MPC skin. See
`design/BRIEF.md`.
