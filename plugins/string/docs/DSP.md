# String DSP

This is a new implementation inspired by the OP-1 String synth engine. It is not bit-exact and contains no firmware code,
constants or variable names. I have not done a listening comparison against a real OP-1.

## What came from the reverse engineering

Only this, because the decompile of the engine is nearly empty:
- The engine validates an index in 0 to 5 and dispatches through a jump table I could not recover.
- Its callee initializes a six-word object.
- The factory defaults for the four knobs (synth_types id 8): 1516, 16704, 0, 15168 of 32767. The other four slots are 0.

Everything else (the string model, the mappings, the voice handling) is my design. The screen art has no text, so the knob
names are mine.

## Model

Each of 8 voices is a Karplus-Strong loop:
- A 16384-float delay line, read with a 4-point Lagrange fractional delay.
- A one-pole low-pass in the loop (DAMP). Its cutoff is a multiple of the note frequency, K = 2.5 * 40^damp.
- Three first-order all-pass stages (STIFF) with coefficient a = -0.85 * stiff^1.5. They spread the partials.
- Loop gain g = exp(-6.9078 / (f * T60)) divided by the low-pass gain at the fundamental, capped at 0.99995.
  T60 = 0.08 * 300^decay seconds.
- The delay is corrected for the phase delay of the low-pass and the all-passes, so the pitch stays on the note.
  Tuned frequency is clamped to 20 Hz up to sample rate / 8.

Excitation: velocity-colored noise, shaped by the loop low-pass, combed at the pick position (0.02 + 0.48 * pick of the
string, circular so the 2nd partial nulls at the middle), mean removed, RMS normalized.

Control values are smoothed (15 ms) and updated every 32 samples, counted in samples, so output is bit-identical for any
block size. The delay length is smoothed per sample (4 ms) so pitch bend does not step.

Voices: linear attack, exponential approach to sustain, exponential release. Allocation: a free voice, else the quietest
releasing voice, else the oldest. A stolen voice fades out over 2.5 ms, then the new note starts. A voice that stays under
1e-5 for 0.1 s is freed. Output is panned by note (+-0.35, equal power) and passes a soft ceiling (linear to 0.8, tanh knee
below 0.999).

## Limits

- Highest tuned pitch is sample rate / 8, lowest is 20 Hz.
- Strong STIFF is reduced on high notes so the delay stays positive.
- DECAY cannot reach its stated T60 when K is below about 2.5 (DAMP at the bottom), the loop gain would need to exceed 1.
- The sustained fundamental is much quieter than the pluck burst. That is how Karplus-Strong behaves.

## Tests

`core/tests/test_core.c`, run with `core/tests/run.sh` under ASan and UBSan. All pass:
- Pitch: 39 note and rate cases from 22050 to 192000 Hz, worst error 0.11 cents. With DAMP, STIFF and DECAY moved, 0.85 cents.
- 8 voices at velocity 127, DAMP and DECAY at maximum: peak 0.9990.
- Note-off ends at silence, decayed strings free their voice, pedal and all-notes-off work.
- Voice steal tested at 10 phases, largest mix step 0.00070. A mutation with an instant steal fails the test.
- PICK at the middle cuts the 2nd partial by 42 dB. DAMP raises high-frequency content from 0.033 to 0.315.
  STIFF moves the 12th partial by 60 cents. DECAY follows the formula (measured -27.0, -21.6 and -5.2 dB against -27.0, -21.7 and -5.2).
- Bit-identical output for block sizes 1, 7, 32, 33, 64, 128, 500 and 4096.
- Stable at 22050 to 192000 Hz, including block size 1. Parameter sweeps are click-free.

## Timing

`core/tests/bench.c` on the build Mac (not the device): 8 voices sounding, 128-frame blocks, mean about 20 us, p99 about
22 to 33 us, which is under 1.2% of the 2.9 ms block budget. The armv7 and aarch64 device cost is not measured.
