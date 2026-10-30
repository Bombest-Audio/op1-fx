# Digital DSP

A new implementation inspired by the OP-1 digital synth engine. It is not bit-exact, it carries no firmware code, constants,
variable names or tables, and I have not compared it by ear against a real OP-1.

## What came from the firmware, and what did not

The exported decompile for this engine (`op1f-re/work/decomp/dsp/T10/` and `T10__callees/`) holds only a constructor. I
decompiled the engine's vtable methods myself with Ghidra to read the render function. From that I took the structure of the
signal path and a few facts about the controls:

- Six voices. Each render call makes 128 samples from four signed knob values.
- Oscillator 1 is a sine with phase feedback from its own last output. Full knob is about pi radians.
- Oscillator 2 is a sine whose ratio is a power of two, chosen in 8 steps from -3 to 4 by the second knob, plus a fine offset
  from the third knob. Its level falls as the second knob rises. It is summed with its previous sample.
- Hard sync of oscillator 2 to oscillator 1 runs only when the third knob is above a small threshold.
- A third phase accumulator at oscillator 2's rate makes a rectified sine, scaled by the fourth knob squared. It is used as the
  XOR operand against the mixed 16-bit signal. The result is halved and averaged with the previous sample.

What I could not get from the firmware: the sine table contents (they are not in the decrypted image), so I assumed a plain
sine. I also did not take any scaling constant, state layout or fixed-point rounding.

The firmware screen `display/t10.svg` draws a line, a blue dot with echoes, three small green, white and red dots and a few
glitch squares. That is where the faceplate art idea comes from.

From `op1_factory.db`, table `synth_types`, id 2, `default_params` = 2000, 5000, -1000, 2000, 0, 0, 0, 0 of 32767. The first
four are the screen knobs and are the plugin defaults (0.061, 0.153, -0.031, 0.061).

Everything else is mine: knob names (FEEDBACK, RATIO, DETUNE, CRUNCH are my labels for the blue, green, white and orange knobs
in order, since no label strings for them are in the firmware assets), the float signal path, the amp envelope, the output
level, the pitch handling, the voice allocator. The brief asks for eight voices, so this plugin has eight, not the OP-1's six.

## Digital versus dna

T10 is digital and Id is dna. I do not think they are swapped. Evidence: the screen assets `t10.svg` and `id.svg` match the
two engines' constructors (T10 looks up the same named screen items as `t10.svg`: blue, green, white and red dots; Id's
constructor uses a table and buffer algorithm, which fits a sample-table style engine, and `id.svg` has shape groups); and the
order of the `synth_types` rows in the factory database agrees with that assignment. The caveat is that no string in the
firmware names either engine, so this is an inference from the assets and not a label.

## Signal path (per voice)

1. `y1 = sin(phase1 + 0.5 * FEEDBACK * pi * y1)` with the phase counted in cycles. The 0.5 factor is the averaged feedback.
2. `z = sin(phase2)`. When phase 1 wraps and sync is on, phase 2 resets to 0.
3. `m2 = clamp(zprev + z) * level2`, where level2 falls from 1 to 0 as RATIO rises.
4. `mix = clamp(0.5 * y1 + m2)`.
5. Convert to 16-bit, XOR with `int(|sin(phase3)| * crunch_depth * 32768)`, shift right by 1, convert back.
6. Output is the average of the last two results.

## Knobs

| Knob | Range | Effect |
|---|---|---|
| FEEDBACK | 0 to pi rad | Self phase modulation of oscillator 1. Default 0.061. |
| RATIO | 8 steps | Oscillator 2 runs at `2^step` times the note, step -3 to 4. Its level is `1 - ratio`. The ratio glides in the log domain over 8 ms so a step does not click. Default step -2. |
| DETUNE | -1 to 1 | Adds `detune / 16` to the oscillator 2 ratio. Above about 0.0005 hard sync is on. Default -0.031, so sync is off. |
| CRUNCH | 0 to 1 | XOR depth is `crunch^2` of full scale. Default 0.061, so about 0.4% of full scale. |

Added by me: attack (linear), decay and release (times to fall 60 dB), sustain, and an output level (squared). Mod wheel
(CC 1) adds up to 0.5 to FEEDBACK (clamped at 1); pitch bend range is 2 semitones by default. Sustain pedal (CC 64) and all-notes-off (CC 123, CC 120) are handled.

## Voices and notes

Eight voices. A note reuses a voice already playing the same note, then a free one, then steals the quietest releasing voice,
else the oldest. A stolen or retriggered voice keeps its phase and envelope and slides to the new note. Voices are spread
slightly in the stereo field, plus or minus 0.25.

## Implementation notes

- Sine from a 4096-point table with linear interpolation.
- Control rate of 16 samples. Frequencies and gains are computed per tick and ramped inside it as a start value plus a slope
  times the sample position, so the output does not depend on how time is split into `digital_render` calls.
- Knobs are smoothed with a 5 ms one-pole, gain with 2 ms.
- Output: voice sum into a soft ceiling that is linear to 0.7 and reaches 0.97, then the level control.
- No allocation after `digital_init`.
- No oversampling. The XOR stage and hard sync both fold at high notes and heavy settings. That is part of the sound, but it
  is not band limited.

## Tested (core/tests/test_core.c, run with core/tests/run.sh)

Clang, `-fsanitize=address,undefined`. 3249 checks, 0 failed.

- Factory defaults match the database values, and the knob mappings hit their end points.
- Idle synth outputs exact zeros.
- Pitch accuracy at 22.05 to 192 kHz, on the zero-cross measurement, the default voice by Goertzel, and pitch bend.
- Eight voices across five parameter sets stay finite and under the ceiling.
- Release, sustain pedal, all notes off and velocity 0.
- Voice stealing, retriggering and stealing a releasing voice step no more than 1.2 times the steady-state step.
- Each knob changes the spectrum. Small moves change it less than large ones.
- Hard sync makes the output periodic at the oscillator 1 rate. CRUNCH adds bit-flip energy.
- Envelope stages, velocity and mod wheel.
- Bit-identical output for block sizes 1, 7, 13, 64, 128, 1000, 4096, for a second instance, and after a reset.
- Seven sample rates times 16 knob corners times notes 0 to 127: finite and bounded.
- Each control jumping across its range every 128 samples steps no more than 1.5 times the largest static step.

Not tested: listening comparison against a real OP-1; AU validation with `auval` (the plugin is not installed from the build
tree); any timing on an MPC. The core timing run on this Mac is 0.57 percent of a 2.9 ms block at 8 voices; that is not an
ARM number.
