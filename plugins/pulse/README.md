# Pulse

A new take on the OP-1 Pulse synth engine, as an instrument plugin. Eight voices, two PWM pulse oscillators per voice, one shared DSP core, every format from one source tree.

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+) | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE (built, auval not run) |
| VST2 | Akai MPC OS standalone devices (Gen1 armv7, Gen2 aarch64) | `mpc/`, built with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) |

AAX and Windows are not built yet.

## Controls

Names and defaults come from the OP-1 (factory values 20000, 16000, 4000, 3000 of 32767 for the first four).

| Control | What it does |
|---|---|
| SHAPE | Pulse duty cycle, about 4 to 50 percent. |
| SWEEP | Depth of the pulse-width modulation. |
| RATE | PWM LFO rate in Hz. |
| DETUNE | Cents between the two oscillators, -1 to 1 bipolar. |
| ATTACK, DECAY, SUSTAIN, RELEASE | Amp envelope. Decay and release are times to fall 60 dB. |
| LEVEL | Output level. |
| BEND | Pitch wheel range, 1 to 12 semitones, default 2. |

MIDI in: notes, pitch bend, mod wheel (CC1), sustain pedal (CC64), all sound off (CC120), all notes off (CC123). No audio input.

## DSP

`core/pulse_core.c` is plain C99 with no allocation after init. It is written from scratch as float DSP. The OP-1 decompile
only gave a constructor, so it is not a bit-exact port and carries no firmware code or tables. See `docs/DSP.md`.

## Build

macOS, all formats:

```sh
cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/mac
```

Core tests (sanitizers on, `PULSE_TIMING=1` adds a timing run):

```sh
core/tests/run.sh
```

MPC VST2, with a checkout of mpc-vst-plugins (needs Docker):

```sh
mpc-vst-plugins/tools/test_port.sh mpc/vst.json
mpc-vst-plugins/tools/build_port.sh mpc/vst.json all
```

## Design

Faceplate layout is in the Claude Design project "Pulse faceplate": https://claude.ai/design/p/991c54d1-c5c2-4a37-9283-ca1fc2e7370f
Neon art is generated with Higgsfield and lives in `design/art/`. `faceplate-bg-1280x630.png` is the shared background for
the plugin window and the MPC skin. The layout is also described in `design/BRIEF.md`.
