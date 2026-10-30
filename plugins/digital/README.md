# Digital

A new take on the OP-1 digital synth engine, as an instrument plugin. A feedback sine oscillator, a second sine oscillator
with octave steps, detune and hard sync, and a bit-flipping XOR stage. Eight voices, four screen knobs, an amp envelope and
a level control. One shared DSP core, every format from one repo.

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+), Windows | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE |
| VST2 | Akai MPC OS standalone devices (Gen1 armv7, Gen2 aarch64) | `mpc/`, built with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) |

## Controls

Names are mine; defaults come from the OP-1 factory preset (2000, 5000, -1000, 2000 of 32767).

| Control | What it does |
|---|---|
| FEEDBACK | Self phase modulation of the first oscillator, 0 to pi radians. |
| RATIO | Octave step of the second oscillator, -3 to 4 in 8 steps. The higher the step position, the quieter that oscillator. |
| DETUNE | Offset of the second oscillator, -1 to 1 (up to 1/16 of a ratio). Above 0 it also hard syncs it to the first. |
| CRUNCH | Depth of the XOR stage that flips the low bits of the mixed signal. |
| ATK DEC SUS REL | Amp envelope. |
| LEVEL | Output level. |

MIDI: notes with velocity, pitch bend (range is a parameter, 2 semitones by default), mod wheel, sustain pedal, all notes off.

## DSP

`core/digital_core.c` is plain C99 with no allocation after init. It is written from scratch as float DSP, not a bit-exact
port of the OP-1 fixed-point code, and it carries no firmware code or tables. See `docs/DSP.md`.

## Build

macOS, all formats:

```sh
cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/mac
```

Set `-DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/JUCE` to reuse a local JUCE checkout and `-DOP1FX_ONLY=digital` to build only this
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

The MPC port has two pages: DIGITAL (FEEDBACK, RATIO, DETUNE, CRUNCH) and ENV (ATTACK, DECAY, SUSTAIN, RELEASE, LEVEL). Its
time controls use exponential curves, so values do not carry over by number from the plugin. DETUNE is 0 to 1 there, with 0.5
meaning no detune.

## Design

Faceplate layout is in the Claude Design project "Digital faceplate" (https://claude.ai/design/p/71db94cd-4331-49cb-989e-f66e887bf7a9). The art is generated with Higgsfield and lives
in `design/art/`. `faceplate-bg-1280x630.png` is the shared background for the plugin window and the MPC skin, built by
`design/compose_faceplate.py`. See `design/BRIEF.md`.

## Voices and notes

Eight voices. A note reuses a voice already playing the same note, then a free one, then steals: the quietest voice that is
in its release, else the oldest note. A stolen or retriggered voice keeps its envelope and phases and slides to the new note,
so there is no step in the output. A fresh voice starts from clean phases.
