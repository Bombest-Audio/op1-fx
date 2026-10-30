# Voltage DSP

## What the OP-1 does

The OP-1 engine is called Voltage on screen (the screen shows `volts`) and `Slump` in the firmware. Its factory defaults are
1516, 16704, 0 and 15168 out of 32767 for the four knobs (blue, green, white, orange). The decompiled routine is not a
string model, even though the defaults match the string engine's. These structural facts came from the decompile and shaped
this version:

- Four sine oscillators, read from a sine table. Each one feeds its own output back into its own phase.
- Two of the sines are rectified and shaped, then combined with an XOR of their integer values. This is the source of the harsh, chaotic part of the sound.
- A sync-reset ramp that restarts when the carrier wraps.
- A one-pole integrator low-pass on the summed oscillators. It clips its state before the feedback term is subtracted.
- The filter coefficient follows a curve of the FILTER knob that rises steeply and flattens near the top.
- Time-based constants are tuned for one sample rate.

The arithmetic is Blackfin fixed point with lookup tables. This repo does not reproduce it. I read the decompile only to learn
the structure and to fit curves, and I wrote the code and every table from scratch.

## What this version does

Per voice and per sample, in float:

1. Four sines. Oscillator 0 is the carrier at the played pitch. Oscillator 3 is a twin at the same pitch. Oscillators 1 and 2
   sit below and above the pitch by the DETUNE spread (up to 50 percent of the pitch at full, on a squared curve).
   Oscillators 1, 2 and 3 feed their own previous output back into their phase, scaled by FEEDBACK.
2. The XOR branch rectifies and shapes oscillators 2 and 3, turns them into 15-bit integers, XORs them and scales back.
   XOR sets how much of this branch is shaped again and mixed in.
3. A ramp is halved each time the carrier passes half a cycle, then falls by a step set by the FILTER knob and the level of
   the twin oscillator. It enters the low-pass at half weight.
4. Oscillator 1, the XOR branch and the ramp go through a one-pole integrator low-pass that clips its state before the
   feedback term is subtracted. The cutoff rises exponentially over the first 48 percent of the FILTER knob (about 21 Hz up
   to about 17 kHz, with a tanh keeping the pole stable). Above 48 percent the knob only changes the ramp.
5. The carrier is added at half level after the low-pass and the sum is clamped.
6. The voice gets an ADSR envelope and a velocity gain. After note-off the pitch drifts about 0.45 Hz per second. A one-pole
   smoother and a DC blocker sit on each voice. Eight voices sum into a soft limiter (linear to 0.6, tanh knee above, tops out at 0.99).

Time constants and the filter coefficient are scaled with the sample rate, so the plugin sounds the same at 44.1, 48 and 96 kHz.
A new note takes an idle voice, else the same note again, else the quietest releasing voice, else the oldest, and the stolen voice fades out first.

## Known differences from the OP-1

- The sine is a float table with linear interpolation, not a fixed-point lookup.
- Each voice uses a one-pole output smoother where the firmware averages two taps, and I added a DC blocker.
- The ramp and low-pass start from settled states instead of zero.
- With the filter fully open the firmware integrator hits a fixed-point limit and wraps. I did not copy that. The filter just stays open.
- The XOR branch is not sample-exact. It has the same character, not the same bits.
- The mapping of the four screen knobs to my XOR, FEEDBACK, FILTER and DETUNE controls is an assumption from the knob colors and from listening to the structure, not from the OP-1 UI strings.
- The sine phase polarity is an assumption.
- I checked a reference model of the decompiled routine against the core. Output agrees closely except for the XOR chaos and the open-filter wrap above.

## Tested (core/tests/test_core.c, ASan and UBSan on)

- Pitch of played notes within 5 cents.
- Eight notes at once: no NaN, bounded level, peak under 1.0 at full velocity.
- Note-off releases to silence.
- Retrigger and voice steal without clicks above a step threshold.
- Each of the four knobs changes the spectrum or the envelope.
- The filter curve is monotonic.
- Fixed input gives identical output on every run, and any block split gives the same samples.
- Moving the knobs every block gives no step above the threshold.

Not tested: listening comparison against a real OP-1, Windows, AAX, an armv7 run, and anything on the MPC device.
