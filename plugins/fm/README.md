# Fm

A new take on the OP-1 FM synth engine, as an instrument plugin. Four operators, eight voices, four screen knobs, an amp
envelope and a level control. One shared DSP core, every format from one repo.

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+), Windows | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE |
| VST2 | Akai MPC OS standalone devices (Gen1 armv7, Gen2 aarch64) | `mpc/`, built with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) |

## Controls

Names are mine; defaults come from the OP-1 factory preset (20000, 4500, 3000, 18000 of 32767).

| Control | What it does |
|---|---|
| RATIO | Modulator frequency over carrier frequency, 0.5 to 16, leaning toward whole numbers. |
| INDEX | Modulation depth, up to 16 rad. |
| FEEDBACK | Modulator self-feedback. |
| LAYER | Level of the second FM pair, an octave up. |
| ATK DEC SUS REL | Amp envelope. |
| LEVEL | Output level. |

MIDI: notes with velocity, pitch bend (range is a parameter, 2 semitones by default), mod wheel (more INDEX), sustain
pedal, all notes off.

## DSP

`core/fm_core.c` is plain C99 with no allocation after init. It is written from scratch as float DSP, not a bit-exact port of
the OP-1 fixed-point code, and the firmware decompile for this engine holds no signal path, so the knob behavior is my own
design. See `docs/DSP.md`.

## Build

macOS, all formats:

```sh
cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/mac
```

Set `-DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/JUCE` to reuse a local JUCE checkout and `-DOP1FX_ONLY=fm` to build only this
plugin.

Core tests (sanitizers on, then a timing run):

```sh
core/tests/run.sh
```

MPC VST2, with a checkout of mpc-vst-plugins next to this repo (needs Docker):

```sh
../mpc-re/mpc-vst-plugins/tools/test_port.sh mpc/vst.json
../mpc-re/mpc-vst-plugins/tools/build_port.sh mpc/vst.json all
```

The MPC port has two pages: FM (RATIO, INDEX, FEEDBACK, LAYER) and ENV (ATTACK, DECAY, SUSTAIN, RELEASE, LEVEL). Its time
controls use exponential curves, so values do not carry over by number from the plugin.

## Design

Faceplate layout is in the Claude Design project "Fm faceplate". The cube art is generated with Higgsfield and lives in
`design/art/`. `faceplate-bg-1280x630.png` is the shared background for the plugin window and the MPC skin. See `design/BRIEF.md`.
