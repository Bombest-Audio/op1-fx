# Phase DSP notes

## What this is

I wrote Phase from scratch as a phase-distortion synth in the spirit of the OP-1 "phase" engine. It is not a bit-exact copy and contains no firmware code or tables.

The decompile for this engine (`op1f-re/work/decomp/dsp/Pd/`) is two stubs of about 1 KB total. It has no DSP content, so I could not port from it. I say this plainly because every claim of "like the OP-1" below rests on the screen graphic and the factory defaults only.

## What came from the OP-1 side

- The screen graphic `pd.svg`: four glyph letters A (blue 698eff), B (green 00ed95), R (white dfd9ff), W (red ff3a5d). The left graph shows blue and green warped sine curves. The right graph shows a red multi-cycle wave.
- Factory defaults (`synth_types` id 5): `[8000, 0, 0, 0, 0, 0, 0, 0]` of 32767. Only the first knob is non-zero (A = 0.2442). The other four raw values are 0 and I do not use them.

## My own design (everything else)

- Oscillator: the cycle is split in two halves. Each half runs a sine phase through `u + (u^g - u) * sin^2(pi u)`. The positive half uses g = 2^(3a), the negative half g = 2^(3b), both scaled by a pitch-dependent brightness. Slope is 1 at both ends, so the phase is C1 and aliasing stays low without oversampling.
- A and B are my reading of the blue and green curves: bend of each half.
- R: a resonant carrier `sin(2 pi n ph)` on the unwarped phase, under a smooth window, blended into the base wave. n grows with R and is capped below Nyquist.
- W: a window that fades the cycle out towards its end. This is my reading of the red wave.
- The R and W meanings are interpretations. I have not compared against a real OP-1.
- Voices: 8, plus 8 ghost voices. A stolen or retriggered voice is copied to a ghost that fades in 3 ms, so steals do not click.
- Amp envelope: linear attack (1 ms minimum), exponential decay toward sustain, exponential release.
- Output: voice gain 0.5, soft knee at 0.6, 8 Hz DC blocker. Controls are smoothed per sample. Render runs in 64-sample chunks and output is bit-identical for any host block size.
- MIDI: velocity, pitch bend (default +-2), mod wheel (adds up to 0.5 to both bends), sustain pedal, CC 120 and 123.

## Limits

- The warp is monotonic only up to g of about 2.1. Above that the phase dips back by at most 3.4e-4 of a half cycle.
- Bandlimiting is by shaping, not oversampling. With all knobs at maximum, notes near 4 kHz alias at about -37 dB (measured with a probe, not part of the test suite). For notes 60 to 96 the worst non-harmonic power is -55.0, -55.6, -49.2 and -43.2 dB across four knob sets.

## Tests (`core/tests/run.sh`, ASan and UBSan)

All pass. Key numbers: pitch worst error 0.254 cents over 112 rate, note and bend combinations; bend error under 0.02 cents; 8 voices at velocity 1 and level 1 peak at 1.0000 in the worst of seven knob corners; one note at defaults peaks at 0.347; steal and retrigger max step 0.0994 against a limit of 0.1302 (a hard cut moves 0.55); output bit-identical across block sizes 1 to 4096 and after reset; no subnormals.
