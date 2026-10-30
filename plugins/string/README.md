# String

A new take on the OP-1 String synth engine, as an instrument plugin. A plucked string with eight voices, four engine
controls and an amp envelope. One shared DSP core, every format from one repo.

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+), Windows | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE |
| VST2 | Akai MPC OS standalone devices (Gen1 armv7, Gen2 aarch64) | `mpc/`, built with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) |

## Controls

The four engine knobs follow the OP-1 screen order (blue, green, white, orange). Defaults are the OP-1 factory values
1516, 16704, 0 and 15168 of 32767. The OP-1 screen shows no text, so the names are mine, taken from what the engine does.

| Control | What it does |
|---|---|
| PICK | Where the string is plucked. Near the bridge is bright, the middle is hollow. Shown as a percent of the string. |
| DAMP | Brightness of the loop. Low is dark, high keeps the upper partials ringing. |
| STIFF | String stiffness. At 0 the partials are exact multiples, higher stretches them toward a bell. |
| DECAY | Ring time of the string, 0.08 s to 24 s to reach -60 dB. |
| ATTACK, DECAY, SUSTAIN, RELEASE | Amp envelope. Times are on an exponential scale. |
| LEVEL | Output gain in dB. |

Playing: velocity, pitch bend (range is a plugin parameter, 2 semitones default), mod wheel for vibrato, sustain pedal,
all notes off. 8 voices, a stolen voice fades out in 2.5 ms before the new note starts.

## DSP

`core/string_core.c` is plain C99 with no allocation. It is written from scratch as float DSP. It is not a bit-exact
port of the OP-1 fixed-point code, and it carries no firmware code or tables. See `docs/DSP.md`.

## Build

macOS, all formats:

```sh
cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DOP1FX_ONLY=string
cmake --build build/mac
```

Set `-DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/JUCE` to reuse a local JUCE checkout.

Core tests (sanitizers on) and the host timing bench:

```sh
core/tests/run.sh
clang -std=c99 -O2 -D_POSIX_C_SOURCE=199309L core/tests/bench.c core/string_core.c -lm -o build/bench && build/bench
```

MPC VST2, with a checkout of mpc-vst-plugins (needs Docker):

```sh
~/bombest-audio/mpc-re/mpc-vst-plugins/tools/test_port.sh mpc/vst.json
~/bombest-audio/mpc-re/mpc-vst-plugins/tools/build_port.sh mpc/vst.json all
```

Editor snapshot without a host: build the `StringSnapshot` target and run `StringSnapshot out.png 1280 0.8`.

## Design

Faceplate layout is in the Claude Design project "String faceplate". The art is generated with Higgsfield and lives in
`design/art/`. `faceplate-bg-1280x630.png` is the shared background for the plugin window and the MPC skin.
Claude Design's standalone MCP server stops working on 2026-12-14, so the layout is also described in `design/BRIEF.md`.
