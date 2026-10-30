# Cluster DSP

Cluster is a new implementation inspired by the OP-1 "cluster" synth engine. It is not bit-exact. It carries no firmware
code, tables or variable names. I wrote it as float DSP from a description of what the original does.

## What came from the firmware decompile

I read the Blackfin decompile of the engine and took these facts from it:

- The engine runs 8 sine oscillators. Each one phase-modulates itself with its own previous output, per sample.
- The number of active oscillators comes from the first knob, one oscillator per 2048 steps of the 15-bit value, up to 8.
- The second knob draws a path through two values, A and B. It runs through four segments: A rises while B is -1, then B rises
  while A is 1, then A falls while B is 1, then A is 0 while B falls. A feedback accumulator integrates a cubic of B each
  block and saturates at plus or minus 1. The feedback amount is the saturated sum of A and the accumulator, scaled by about 0.5.
- The third knob picks a decay time for a per-oscillator detune that does a random walk.
- The fourth knob sets the size of the random kick that drives that walk.
- All oscillators start in phase at a zero crossing.
- The factory defaults for the four knobs are 4000, 4000, 8000 and 100 of 32767.

## What I could not recover, so I chose it myself

The firmware sine, decay and normalization tables are zeros in the image I have. So these are my choices and I did not
measure them against a real OP-1:

- The sine table: 4096 points with linear interpolation.
- The decay mapping for SPREAD (`cluster_spread_tau`).
- The size of the random kick for WANDER, and the detune clamp (plus or minus 0.25).
- The level normalization with the oscillator count (N to the power -0.75), the voice gain, the output gain, the pan.
- Oscillator 0 is pinned to the played pitch, so the note always reads as its pitch. Others start up to 1.5 percent off.
- The envelope, voice allocation, pitch bend, mod wheel and sustain pedal. The OP-1 envelope is not part of this engine.

I also ignored the display-only state that the original keeps for its screen.

## Signal flow

For each voice, every sample of every active oscillator:

    y = sin(phase + fb * y_previous)
    phase += 2 pi f (1 + detune) / sr

The phase is a 32-bit accumulator, so it wraps exactly. The oscillators are summed with a fixed constant-power pan each,
then the voice amp envelope (ADSR) and velocity are applied, then the voice sum goes through output gain and a soft ceiling
(linear up to 0.75, a tanh knee to 1.0). Peak output stays at or under 1.0 in the worst case with 8 voices.

The core works on a tick of 128/44100 s (scaled with the sample rate). Once per tick it updates the feedback accumulator,
the detune random walk and the amplitudes. Detune, amplitude and feedback are ramped linearly per sample between ticks, so
there are no steps. Output does not depend on the block size (checked bit-exact for 1, 37, 128 and 1000 frames).

## Controls

| Control | Meaning |
|---|---|
| OSCS | 1 to 8 oscillators. |
| FEEDBACK | Position on the A/B path above. It sets the feedback amount and how it drifts over the note. |
| SPREAD | How long detune lasts before it settles back. |
| WANDER | Size of the random kick. |
| ATK, DEC, SUS, REL | Amp envelope. Times map 1 ms to 5 s on an exponential curve. |
| LEVEL | Output gain, 2 * level squared, so 0.707 is unity. |
| BEND | Pitch bend range in semitones, 0 to 12. |

MIDI: note on and off with velocity, pitch bend, CC1 adds feedback, CC64 sustain, CC120 and CC123 stop all notes, CC121
resets controllers.

## Voices

8 voices. A new note takes an idle voice, else the quietest releasing voice, else the oldest held voice. A stolen voice
fades out over 3 ms before the new note starts. A repeated note releases the old voice. The random generator is a seeded
LCG, so a fixed seed gives identical output.

## Tests

`core/tests/run.sh` builds `core/tests/test_core.c` with clang under ASan and UBSan. It checks the feedback path, pitch
accuracy (within 5 cents from 22.05 kHz to 192 kHz, notes 28 to 96), pitch bend, polyphony and headroom, release and pedal,
clicks on retrigger, steal and oscillator count changes, that each knob changes the spectrum or envelope, determinism, a
random stress run at 7 sample rates, and reset.
