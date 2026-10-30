# DBox

A new take on the OP-1 dbox drum engine, as an instrument plugin: a 24 key synthesized drum kit, two oscillators and noise
per hit, 12 voices.

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+), Windows | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE |
| VST2 | Akai MPC OS standalone devices (Gen1 armv7, Gen2 aarch64) | `mpc/`, built with mpc-vst-plugins |

## Playing it

MIDI notes 36 to 59 (C1 to B2) play the 24 drums; other notes fold back into that range. Root note is a parameter in the
plugin. Hats choke each other, as do crash and ride. Velocity, pitch bend, mod wheel (closes the filter), sustain pedal and
all-notes-off work.

## Controls

| Control | What it does |
|---|---|
| PITCH | Oscillator pitch, +-12 semitones. |
| WAVE | Oscillator 1 shape, sine to square to saw. |
| DECAY | Oscillator 1 decay, 50 ms to 2.5 s (times a per-drum scale). The top fifth adds a slow attack. |
| MODE | 0 to 7: how oscillator 2 meets oscillator 1 (layer, octave, fifth, sub, ring, FM, hard FM, noise). |
| 2 MIX, 2 WAVE, 2 DECAY | Oscillator 2 level (or FM depth), shape and decay. |
| FILTER | Low-pass, 20 kHz down to 60 Hz. |
| ATK, DEC, SUS, REL, LEVEL | Gate envelope and output level. Defaults let drums ring out. |

Factory values (0, 0, 2917, 974, 7167, 0, 2592, 4607 of 32767) are the defaults. See `docs/DSP.md`.

## Build

```sh
cmake -S . -B build/dbox -G Ninja -DCMAKE_BUILD_TYPE=Release -DOP1FX_ONLY=dbox
cmake --build build/dbox
plugins/dbox/core/tests/run.sh          # core tests, sanitizers on; BENCH=1 adds a timing run
../mpc-re/mpc-vst-plugins/tools/test_port.sh mpc/vst.json
../mpc-re/mpc-vst-plugins/tools/build_port.sh mpc/vst.json all
```
