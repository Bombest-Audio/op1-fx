# Spring

A new take on the OP-1 Spring effect, as a plugin. Four controls, one shared DSP core, every format from one repo.

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+), Windows | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE |
| VST2 | Akai MPC OS standalone devices (Gen1 armv7, Gen2 aarch64) | `mpc/`, built with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) |

## Controls

Names and defaults come from the OP-1 (factory values 8000, 15000, 2000, 20000 of 32767). TURNS and DAMP are the labels on the
OP-1 screen; TONE and LEVEL are my names for the other two.

| Control | What it does |
|---|---|
| TONE | Corner of the low-pass in the spring loops, 1.2 kHz to 9 kHz. Darker or brighter tail. |
| TURNS | How dispersive the springs are, the strength of the chirp. |
| DAMP | How fast the tail dies, 4.5 s down to 0.4 s (bass RT60, cubic). |
| LEVEL | Drive into the springs. The dry signal always passes at unity. At 0 nothing enters and a finished tail leaves the audio bit-identical. |

## DSP

`core/spring_core.c` is plain C99 with no allocation. It is written from scratch as float DSP: three coupled spring loops,
each a chain of 32 dispersive all-pass sections plus a delay line, with a low-pass and a loss in the feedback. It is not a
bit-exact port of the OP-1 fixed-point code, and it carries no firmware code or tables. See `docs/DSP.md`.

## Build

macOS, all formats:

```sh
cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/mac
```

Set `-DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/JUCE` to reuse a local JUCE checkout.

Core tests (sanitizers on):

```sh
core/tests/run.sh
```

MPC port (from the mpc-vst-plugins checkout, needs Docker):

```sh
tools/test_port.sh <this folder>/mpc/vst.json            # offline host test
tools/test_port.sh <this folder>/mpc/vst.json aarch64    # same, in an arm64 container
tools/build_port.sh <this folder>/mpc/vst.json all       # armv7 and aarch64 .so
```

Not yet done: loading the `.so` on a device, Windows, AU validation with `auval`.

## Presets

12 factory presets, picked using Devvon Terrell's mixing guidance. Preset 0 (Init) is the OP-1 default. Source of truth: `mpc/presets.json` (feeds the MPC PRESET menu and the DAW program list). Details and sources: `docs/PRESETS.md`.
