# Phase

An 8-voice phase-distortion synth plugin, a new take on the OP-1 "phase" engine. VST3, AU, CLAP and Standalone on macOS, plus an MPC OS VST2 port.

I built it from scratch. The OP-1 decompile for this engine was empty, so the sound is my design from the screen graphic and the factory defaults. See `docs/DSP.md`.

## Controls

A and B bend the two halves of the cycle. R adds a resonant partial. W fades the cycle out towards its end. Then attack, decay, sustain, release, level and pitch bend range.

## Layout

- `core/` portable C99 DSP and tests (`core/tests/run.sh`)
- `plugin/` JUCE processor, editor and Snapshot tool
- `mpc/` MPC OS port (`vst.json`, `engine.c`, `params.json`, `layout.conf`)
- `design/` brief and art
- `docs/DSP.md` what is from the OP-1 and what is mine

## Build

From the op1-fx root: `cmake --build build/phase`. Plugin code `OpPh`, manufacturer `Bomb`, id `audio.bombest.phase`.
