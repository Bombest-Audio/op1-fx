# Spring DSP

## What the OP-1 does

The firmware codename is `Rymd` (the repo notes call it "spring, likely"). Its screen asset, `rymd.svg`, draws the words
TURNS and DAMP, a blue plus sign and a vertical level bar. The factory defaults are 8000, 15000, 2000 and 20000 out of 32767.
This is my reading of the decompiled routine, not something I checked against a running OP-1:

- It works on 64-sample blocks of 16-bit mono audio, in place. The reverb output is added to the input, so the dry
  signal always passes at unity.
- The memory is nine delay lines in three groups of three. Lengths in samples at 44.1 kHz are about 382, 1272 and 5330 in the
  first group, 3418, 1848 and 5902 in the second, and 596, 3490 and 6898 in the third (29136 samples in all). Each line also
  has a shorter tap, and the line is fed by its input plus a low-passed, scaled mix of its own two taps.
- Each group's output goes through a one-pole low-pass and is subtracted from the input of the next group, and the sum
  of all three outputs is fed back to the front. So the three groups are coupled, and the coupling is filtered.
- The four knobs act through curves. The first two (raw value shifted right by 7) index coefficient tables, with the first index
  clamped so that the two indices never add up to more than 220. The third is cubed and becomes a loss gain, `1 - knob^3`.
  The fourth is squared and scales the signal entering the network.

## What this version does

A new implementation inspired by that effect. It is not bit-exact and has no firmware code, tables or variable names in it.
From the RE I kept the three coupled groups, the filtered feedback between them, wet added to dry at unity, the cubed
loss and the squared input level, and the factory defaults. The rest I designed:

A spring reverb is dispersive: high frequencies run down the coil faster than low ones, so a click comes back as a short
downward chirp, and every trip around the tank smears it more. The multi-tap combs in the firmware do not do that, so the
delay network here is built around the chirp instead.

Per sample, mono sum in, three springs:

1. The mono sum is high-passed at 150 Hz (a spring pickup has no deep bass) and scaled by `0.9 * LEVEL^2`.
2. Each spring takes that plus its feedback and runs it through 32 stretched all-pass sections,
   `H(z) = (a + z^-M) / (1 + a z^-M)`, with `a` negative and `M = 4` samples at 44.1 kHz (scaled with the sample rate).
   Group delay of one section is `M (1 - a^2) / (1 + 2 a cos(M w) + a^2)`, so with `a = -0.6` the bass of the chain arrives
   about 10 ms after 2 kHz does (measured in the tests). That is the chirp. The chain is all-pass, so it adds no level change.
3. A delay line follows: 1103, 1459 and 1951 samples at 44.1 kHz for the three springs, scaled with the sample rate.
4. The feedback mixes 84% of a spring's own output with 16% of the next one (the coupling), goes through a one-pole low-pass,
   a 40 Hz DC blocker and the loop gain. The mix coefficients add to 1, so the coupling cannot add energy.
5. The wet output is picked up at different points per channel: `L = 0.70 s0 + 0.45 s1 + 0.15 s2`, `R = 0.15 s0 + 0.45 s1 + 0.70 s2`.
   It is added to the dry signal.

The controls:

| Control | Default | Effect |
|---|---|---|
| TONE | 0.244 | Corner of the loop low-pass, `1200 * 7.5^TONE` Hz (1.2 kHz to 9 kHz). Acts on the tail, not on the first return. |
| TURNS | 0.458 | All-pass coefficient `a = -(0.2 + 0.6 TURNS)`. More turns means a longer, deeper chirp. The label is from the OP-1 screen. |
| DAMP | 0.061 | Loop gain per trip, set from an RT60 of `0.45 / (DAMP^3 + 0.1)` seconds (4.5 s down to 0.41 s). Cubic, as on the OP-1. The label is from the OP-1 screen. |
| LEVEL | 0.610 | Drive into the springs, `0.9 * LEVEL^2`. At 0 nothing enters; the tail already ringing still decays. |

The DAMP time is the nominal RT60 for the bass. The low-pass and DC blocker in the loop shorten what you hear, and at the
defaults the wet signal falls about 30 dB per second.

Parameter smoothing is 10 ms one-pole on the four controls, with the derived coefficients refreshed every 16 samples.
Results do not depend on block size. Delay lines and all-pass state are sized for 192 kHz at compile time, and every length is
derived from the sample rate (clamped to 8 kHz to 192 kHz). An alternating 1e-24 offset keeps every tank state far above the
denormal range, so the loop never slows down on a CPU that handles denormals slowly. When LEVEL is 0 and the tail has died
below 1e-7 the audio is passed through untouched and the tank is not run.

## Cost

32 sections times 3 springs, about 12 flops per sample per section, so roughly 1200 flops per frame plus the loops.
Measured on this Mac (arm64, clang -O2): 82 ns per frame, 0.36% of real time at 44.1 kHz. The armv7 build was run under QEMU
only, which says nothing reliable about a device; see the README for what I could and could not measure.

## Tested (core/tests/test_core.c, ASan and UBSan, also run unsanitized for armv7)

- The chain is all-pass (impulse energy 1.00, magnitude 1 from 100 Hz to 8 kHz) and its measured group delay matches the formula
  at 300 Hz and 2 kHz within 5%. 300 Hz arrives 10.47 ms after the start, 2 kHz at 2.16 ms.
- The three-at-once path used by the tank returns the same samples as the single chain, bit for bit.
- LEVEL 0 leaves the buffers bit-identical, and so does LEVEL driven to 0 after the tail has died.
- No NaN or Inf at 22.05, 44.1, 48, 96 and 192 kHz across seven parameter corners with 0.9 full-scale noise, the tail dies
  out after the input stops, and the worst peak is 3.12 (wet adds to dry, so hot input at LEVEL 1 can exceed full scale).
- DAMP 0, 0.4, 0.7 and 1 give strictly shorter tails. TONE 1 has ten times the tail brightness of TONE 0.
  TURNS 1 spreads the first returns over 8.3 ms against 6.2 ms at TURNS 0.
- Block sizes 1, 8, 100 and 2000, with a parameter move halfway, give identical output.
- Flipping all four controls every 128 samples gives a largest sample step of 0.050 on a 220 Hz sine, against 0.102
  with the controls held at the louder setting.
- Wet channels are decorrelated (0.57) and balanced within 3 dB. The first return lands at 26.1 ms at every sample rate tested.
- Activity reads and clears. A reset leaves a silent tank.

Not tested: a listening comparison against a real OP-1. Treat this as a new effect that shares the OP-1's controls.
