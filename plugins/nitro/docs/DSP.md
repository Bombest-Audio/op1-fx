# Nitro DSP

## Where this comes from

The OP-1 effect is called nitro on the unit and `Mmmf` in the firmware, which is my best read from the README codename
("nitro, likely"). There is no tagged decompile for it. I found the real routine by decompiling untagged functions near
`0x19e7ed4` in the firmware image, so the structure below is read from code and not from a spec.

This is a new implementation inspired by the OP-1 effect. It is not bit-exact. It carries no firmware code, tables or names.

## Facts taken from the RE

- The screen has a FREQ dial, a RES label, the word "drive" and two 10-bar meters.
- The factory defaults are 500, 0, 10500 and 16000 of 32767.
- The routine is a cascade of 4 stages. A 12 by 5 mode-mix table picks how the input and the four stage outputs are combined.
- The cutoff comes from a 256 entry table, clamped to a coefficient range of about 0.002 to 0.9 in 1.31 fixed point.
- A peak follower with a decay of 0.9922 per 64 samples feeds the feedback path.
- Feedback gain grows with the RES knob squared times the fourth knob, plus RES times the follower peak.

## What I could not resolve

- How the firmware maps the SHAPE value to a row. Shifting 0..32767 right by 11 gives 16 rows for a 12 row table. I map the
  full knob over the 12 rows and chose the plugin default so it lands on the same row the factory 10500 does. This is a
  deviation. The knob travel is not the same as on the unit.
- The cutoff table is filled at run time and is zero in the ELF image, so I could not read it. I designed an exponential
  14 Hz to 16 kHz map. It fits the coefficient clamp range above.
- What the fourth knob does inside the loop, and the sine-table shaping. I treat the fourth knob as DRIVE because the screen
  says "drive". The shaping is my own soft ceiling.

## What this version does

A 4-stage cascade of trapezoidal (TPT) one-pole low-passes, with the feedback from stage 4 solved without a unit delay.

1. DRIVE is a pre-gain of `10^(1.2 d^2)` into the loop, then the soft ceiling (linear to 0.75, tanh knee to 1.0).
2. Feedback is `4.4 * clamp(res^2 * (0.8 + 0.2 d) + 0.2 * res * env, 0, 1)`. `env` is a peak follower with a 186 ms decay.
   At RES 1 and DRIVE 1 the loop passes 4.0 and rings on its own. The input gain is raised by `1 + 0.35 fb` so the level holds.
3. SHAPE crossfades linearly between 12 rows of stage-output mixes: LP 24, LP 18, LP 12, LP 6, THRU, HP 6+, NOTCH, BAND,
   HP 6, HP 12, HP 18, HP 24. The mixes are plain sums, such as the binomial rows for the high-pass slopes.
MIX is mine, not an OP-1 control. It is a linear blend of the untouched input and the filtered output, `dry*(1-m) + wet*m`,
   smoothed over 5 ms like the others. The filter keeps running at MIX 0 so its state is continuous when MIX moves. MIX 1 is
   bit-identical to the fully wet signal and MIX 0 is a bit-exact bypass (`test_mix` checks both). The default is 1.
4. FREQ is exponential, 14 Hz to 16 kHz. All five parameters are smoothed over 5 ms. The filter coefficient is recomputed
   per sample from `tan`, so sweeps do not click.
5. The activity value for the editor is the input peak follower.

## Tested (core/tests/test_core.c)

- Output is finite and bounded at 22.05, 44.1, 48, 96 and 192 kHz across parameter corners.
- LP, HP, NOTCH and BAND responses match the analytic response. THRU is transparent.
- Step response, resonance at high RES, and ringing at RES 1 with DRIVE 1.
- DRIVE adds harmonics. Level stays in range.
- Sweeping every parameter gives no click.
- Block size 1 gives the same output as one large block. Mono equals the left channel of stereo.
- Reset clears all state. No subnormals appear in a long decay.
- Activity reports the input level. The FREQ and SHAPE maps round-trip.

Not tested: a listening comparison against a real OP-1. Treat the sound as a new effect that shares the OP-1's controls.
