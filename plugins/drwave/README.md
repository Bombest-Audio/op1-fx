# DrWave

A new take on the OP-1 "dr wave" synth engine, as an instrument plugin. Four knobs (WAVE, BLEND, MOTION, FOLD), a tone control, an amp
envelope and a level. Eight voices. One shared C core.

| Format | Platform | Built from |
|---|---|---|
| VST3, CLAP, Standalone | macOS (universal, 11.0+), Windows | JUCE, `CMakeLists.txt` |
| AU | macOS | JUCE |
| VST2 | Akai MPC OS standalone devices (armv7, aarch64) | `mpc/`, built with mpc-vst-plugins |

The firmware has no recoverable DSP for this engine, so the sound is my own design from what dr wave is known to do. `docs/DSP.md` says what
came from the firmware (the defaults, the preset statistics, the voice count) and what is mine.

Build and test:

```sh
core/tests/run.sh
cmake -S ../.. -B ../../build/drwave -G Ninja -DCMAKE_BUILD_TYPE=Release -DOP1FX_ONLY=drwave
```

MPC: `mpc/vst.json`, uid `BbDw`. Faceplate notes are in `design/BRIEF.md`; `design/compose_faceplate.py` rebuilds the background.
