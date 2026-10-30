# DrumSampler

24-slot drum sampler instrument. VST3, AU, CLAP, Standalone, plus an MPC Live III port (uid BbDm).

Slot n answers to MIDI note 36 + n. Load a WAV per slot with the file chooser or by dropping it on a pad. Paths are saved in the project. A synthesized default kit plays until you load something.

- Core: `core/` (portable C99, tests in `core/tests/run.sh`)
- Plugin: `plugin/` (JUCE)
- MPC: `mpc/` (put WAVs in `samples/` next to the .so)
- Notes: `docs/DSP.md`, `design/BRIEF.md`

This is a new implementation inspired by the OP-1, not a copy of its firmware.
