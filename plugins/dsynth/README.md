# DSynth

A new take on the OP-1 dsynth synth engine, as an instrument plugin. Two oscillators per voice, FM between them, a pitch
drop for the drum thump, an eight-mode filter, and eight voices.

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+) | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE |
| VST2 | Akai MPC OS standalone devices (Gen1 armv7, Gen2 aarch64) | `mpc/`, built with mpc-vst-plugins |

## Controls

Names and defaults come from the OP-1 (factory values 20664, 0, 8528, 6225 of 32767 for the first four).

| Control | What it does |
|---|---|
| MOD | FM depth, oscillator 2 into oscillator 1. The mod wheel adds to it. |
| WAVE | Oscillator 1 shape, sine to square to saw. |
| DECAY | Oscillator 1 decay, 15 ms to 2 s. Past the middle it stays long and an attack grows. |
| FILTER | Eight modes, 0 to 7. 0 is off, 1 is low-pass 12 dB. |
| ATK, DEC, SUS, REL | Amp envelope. |
| LEVEL | Output level. |

Bend range is a parameter in the plugin (1 to 12 semitones). It is not on the MPC screen.

## DSP

`core/dsynth_core.c` is plain C99 with no allocation after init. It is written from scratch as float DSP. It is not a
bit-exact port of the OP-1 code, and it carries no firmware code or tables. I play it as a pitched voice, not a drum kit
across notes. See `docs/DSP.md`.

## Build

macOS, all formats:

```sh
cmake -S . -B build/mac -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/mac
```

The target is `DSynth` in the op1-fx repo. Set `-DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/JUCE` to reuse a local JUCE checkout
if its version matches the pin.

Core tests (sanitizers on):

```sh
core/tests/run.sh
```

MPC VST2, with a checkout of mpc-vst-plugins (needs Docker):

```sh
../../../mpc-re/mpc-vst-plugins/tools/test_port.sh mpc/vst.json
../../../mpc-re/mpc-vst-plugins/tools/build_port.sh mpc/vst.json all
```

## Not verified

AU with auval, AAX, Windows, the MPC on a real device, listening against a real OP-1. See the report that came with this
folder.
