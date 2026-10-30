# Nitro

A new take on the OP-1 nitro effect, as a plugin. Four controls, one shared DSP core, every format from one repo.

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+), Windows | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE |
| AAX | macOS, Windows | JUCE, only when an AAX SDK path is set |
| VST2 | Akai MPC OS standalone devices (Gen1 armv7, Gen2 aarch64) | `mpc/`, built with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) |

## Controls

Names come from the OP-1 screen. Factory values are 500, 0, 10500 and 16000 of 32767.

| Control | What it does |
|---|---|
| FREQ | Cutoff of the filter, 14 Hz to 16 kHz. |
| RES | Feedback around the four stages. At full RES with full DRIVE it rings on its own. |
| SHAPE | Morphs from low pass 24 through pass-through and notch or band to high pass 24. |
| DRIVE | Gain into the filter, with a soft ceiling. |

## DSP

`core/nitro_core.c` is plain C99 with no allocation. It is written from scratch as float DSP. It is not a bit-exact
port of the OP-1 fixed-point code, and it carries no firmware code or tables. See `docs/DSP.md`.

## Build

From the op1-fx root:

```sh
cmake -S . -B build/nitro -G Ninja -DCMAKE_BUILD_TYPE=Release -DOP1FX_ONLY=nitro
cmake --build build/nitro
```

Set `-DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/JUCE` to reuse a local JUCE 8.0.8 checkout.

Core tests (sanitizers on):

```sh
core/tests/run.sh
```

MPC VST2, with a checkout of mpc-vst-plugins (needs Docker):

```sh
../mpc-re/mpc-vst-plugins/tools/test_port.sh mpc/vst.json
../mpc-re/mpc-vst-plugins/tools/build_port.sh mpc/vst.json all
```

Windows builds run in GitHub Actions at the repo root.

## Design

Faceplate layout is in the Claude Design project "Nitro faceplate". The bottle art is generated with Higgsfield and lives in
`design/art/`. `faceplate-bg-1280x630.png` is the shared background for the plugin window and the MPC skin.
The layout is also described in `design/BRIEF.md`.
