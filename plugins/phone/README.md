# Phone

A new take on the OP-1 Phone effect, as a plugin. Four controls, one shared DSP core, every format from one repo.

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+), Windows | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE |
| VST2 | Akai MPC OS standalone devices (Gen1 armv7, Gen2 aarch64) | `mpc/`, built with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) |

## Controls

Names and defaults come from the OP-1 (factory values 8000, 8000, 8000, 16000 of 32767).

| Control | What it does |
|---|---|
| TONE | Playback speed of the loop, shown in semitones from -48 to +48. A quarter turn is the original pitch. |
| PHONIC | Whole waveform cycles in each fragment, 1 to 15. |
| BAUD | How many times a fragment plays before the next one is taken, 1 to 31. |
| TELEMATIC | Dry/wet mix. At 0 the audio passes through bit for bit. |

The effect chops the input into fragments at rising zero crossings, loops the newest fragment at the TONE speed and fades it
with the input level. Try it on a voice or a held note.

## DSP

`core/phone_core.c` is plain C99 with no allocation. It is written from scratch as float DSP. It is not a bit-exact port of
the OP-1 fixed-point code, and it carries no firmware code or tables. See `docs/DSP.md`.

## Build

From the repo root, macOS, this plugin only:

```sh
cmake -S . -B build/phone -G Ninja -DCMAKE_BUILD_TYPE=Release -DOP1FX_ONLY=phone
cmake --build build/phone
```

Set `-DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/JUCE` to reuse a local JUCE checkout.

Core tests (sanitizers on):

```sh
core/tests/run.sh
```

MPC VST2, with a checkout of mpc-vst-plugins next to this repo (needs Docker):

```sh
../../../mpc-re/mpc-vst-plugins/tools/test_port.sh mpc/vst.json
../../../mpc-re/mpc-vst-plugins/tools/build_port.sh mpc/vst.json all
```

## Design

Faceplate layout is in the Claude Design project "Phone faceplate". The handset art is generated with Higgsfield and lives in
`design/art/`. `faceplate-bg-1280x630.png` is the shared background for the plugin window and the MPC skin. The editor
animates the capsules and the chip with the signal, see `design/BRIEF.md`.
Claude Design's standalone MCP server stops working on 2026-12-14, so the layout is also described in `design/BRIEF.md`.

## Presets

12 factory presets, picked using Devvon Terrell's mixing guidance. Preset 0 (Init) is the OP-1 default. Source of truth: `mpc/presets.json` (feeds the MPC PRESET menu and the DAW program list). Details and sources: `docs/PRESETS.md`.
