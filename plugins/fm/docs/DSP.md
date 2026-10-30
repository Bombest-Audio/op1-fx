# Fm DSP

A new implementation inspired by the OP-1 FM synth engine. It is not bit-exact, it carries no firmware code, constants or
tables, and the knob behavior below is my own design. I have not compared it by ear against a real OP-1.

## What came from the firmware, and what did not

The decompile for this engine (`op1f-re/work/decomp/dsp/Fm/` and `Fm__callees/`) is only two functions: a class
constructor and a dispatcher stub. There is no signal path in it. What it does show:

- The engine object holds four operator sub-objects (four calls to the same init routine) and two small arrays sized for
  six voices. So four operators, six voices on the OP-1.
- An assert on a voice index in the range 0 to 5.

The firmware screen `display/fm.svg` draws the engine as groups of four isometric cubes, one per operator. That is where
the faceplate art idea comes from.

From `op1_factory.db`, table `synth_types`, id 4 (`fm`), `default_params` = 20000, 4500, 3000, 18000, 15000, 0, 100,
1500 of 32767. The first four are the screen knobs and are the plugin defaults (0.610, 0.137, 0.092, 0.549).

Everything else is mine: what each knob does, the operator wiring, the envelopes, the voice allocator. The brief asks for
eight voices, so this plugin has eight, not the OP-1's six. The names RATIO, INDEX, FEEDBACK and LAYER are my labels
for the blue, green, white and orange knobs in order; I found no label strings for them in the firmware assets.

## Structure

Two FM pairs per voice, four sine operators in all.

- Pair A: modulator at `f * RATIO` phase-modulates a carrier at `f`.
- Pair B: the same arrangement an octave up, modulator at `2f * RATIO`, carrier at `2f`.
- Output is `(A + LAYER * B) / (1 + LAYER)`, so LAYER moves the mix without changing the peak level.
- Each modulator feeds back on itself through the average of its last two outputs.

The four cubes on the faceplate are these four operators.

## Knobs

| Knob | Range | Effect |
|---|---|---|
| RATIO | 0.5 to 16 | Modulator frequency over carrier frequency, exponential: `0.5 * 2^(5 x)`. Above 1 it is pulled up to 75% of the way to the nearest whole number, so most positions give a near-harmonic spectrum with a slow beat instead of a clangorous one. Default 4.04. |
| INDEX | 0 to 16 rad | Peak phase modulation, `16 * x^1.5`. Default 0.81 rad. |
| FEEDBACK | 0 to 2 rad | Modulator self-feedback depth. Default 0.18 rad. |
| LAYER | 0 to 1 | Level of pair B against pair A. Default 0.55. |

Added by me: attack, decay, sustain, release for the amp envelope (decay and release are times to fall 60 dB), and an
output level (squared). Mod wheel (CC 1) multiplies INDEX by up to 3. Pitch bend range is 2 semitones by default.
Sustain pedal (CC 64) and all-notes-off (CC 123, CC 120) are handled.

## The four extras in the factory preset

Raw 15000, 0, 100, 1500 of 32767. The firmware does not say what they do, so I assigned them, and the plugin and the MPC
port keep them fixed at those factory values:

| Raw | Used as | Value |
|---|---|---|
| 15000 | Mod decay: how long INDEX takes to relax after a note starts, 0.04 to 4 s, floor at 25% | 0.33 s |
| 0 | Detune of pair B, plus or minus 30 cents | none |
| 100 | How much velocity scales INDEX | about 0.3% |
| 1500 | Key tracking: INDEX is scaled by `2^(-3 * amount * octaves above middle C)`, clamped to 0.1 to 2 | 4.6% |

## Voices and notes

Eight voices. A note reuses a voice already playing the same note, then a free one, then steals: the quietest voice that is
in its release, else the oldest note. A stolen or retriggered voice keeps its envelope and phases and slides to the new
note (index and velocity gain move through a 2 ms one-pole), so there is no step in the output. A fresh voice starts at
zero from clean phases.

## Anti-aliasing

Every note limits its modulation index so that carrier plus modulator times (index + 1) stays under Nyquist. A note that
is too high for that plays as a plain sine. Pair B fades out as its carrier approaches Nyquist. There is no oversampling.
Heavy settings on high notes will still fold a little at 22.05 kHz.

## Implementation notes

- Sine from a 2048-point table with linear interpolation (about -118 dB error).
- Control rate of 16 samples. Frequencies, index and gains are computed per tick and ramped linearly inside it. Knobs are
  smoothed with a 5 ms one-pole, so knob moves do not click.
- Output: voice sum times 0.5 into a soft ceiling that is linear to 0.7 and reaches 0.97, then the level control. Eight
  voices at full velocity cannot reach 0 dBFS.
- Sample accurate: the output does not depend on how time is split into `fm_render` calls. The tests check it is bit-identical
  for block sizes 1 to 4096.
- No allocation after `fm_init`. About 8 KB for the sine table plus voices, per instance.

## Tested (core/tests/test_core.c, run with core/tests/run.sh)

Clang, `-fsanitize=address,undefined`.

- Factory defaults match the database values.
- Knob mappings are monotone and hit their end points.
- Idle synth outputs exact zeros.
- Pitch of a pure sine within 0.09 cents over notes 24 to 96 at 22.05 to 192 kHz. Pitch of the FM voice (ratio 2, layer
  on) within 0.35 cents by autocorrelation. Full bend with range 2 is +200.00 cents.
- Eight voices at velocity 127 across four parameter sets: finite, peak 0.970 (-0.26 dBFS).
- Release reaches exact zero and frees the voices. Sustain pedal holds, lifting it releases, all-notes-off ends everything.
- Stealing the oldest voice, stealing a releasing voice, retriggering, and stealing with a 0.5 ms attack each step no more
  than 1.07 times as hard as the same note arriving on a free voice.
- Each of RATIO, INDEX, FEEDBACK and LAYER changes the spectrum by 30 to 89 dB mean difference between 0 and 1. INDEX and
  FEEDBACK raise the spectral centroid. Mod wheel and velocity-to-index change the spectrum.
- Bit-identical output for block sizes 1, 7, 13, 64, 128, 1000, 4096 and after `fm_reset`.
- 22.05 to 192 kHz with extreme parameter corners and 10 notes up to 127: finite, under 0 dBFS.
- Each control jumping across its range every 128 samples steps no more than 1.3 times the largest step the static settings
  make by themselves.

Not tested: listening comparison against a real OP-1; AU validation with `auval` (the plugin is not installed from the
build tree); any timing on an MPC.
