# Punch DSP

## What the OP-1 does

The OP-1 effect is called Punch on screen and `Mllp` in the firmware. Its screen shows four knobs: FREQ, PUNCH, ROUNDS and
POWER, with defaults 6000, 15000, 20000 and 28000 out of 32767. The decompiled routine runs on 64-sample blocks of 16-bit
audio and shows these structural facts, which shaped the controls here:

- FREQ and ROUNDS together select a smoothing coefficient that is updated once per block through a one-pole filter.
- ROUNDS (raw value shifted right by 10, so 0 to 31) sets how many stages run, with state for up to 24.
- PUNCH is compared against a running sum of stage levels, so it behaves like a threshold on level.
- POWER scales the input of the processing path and is also the final dry/wet weight.

The arithmetic is Blackfin fixed point with table lookups. This repo does not reproduce it.

## What this version does

Per sample, for up to 24 rounds in series:

1. Mono sum of the signal entering the round, high-passed by a one-pole at the FREQ corner.
2. A fast and a slow envelope follower on that signal. Round `k` stretches both time constants by `1.18^k`, so round 0 sees
   clicks (0.3 ms attack window) and round 23 sees swells (about 16 ms).
3. Transient strength `t = (fast - slow) / (slow + 0.01)`, clamped to 0..3.
4. Gain `1 + PUNCH * 0.5 / activeRounds * t`, applied to both channels, then a soft ceiling (linear to 0.75, tanh knee to 1.0).

The per-round push is divided by the smoothed count of active rounds, so the cascade multiplies out to roughly the same total
boost at any ROUNDS setting. Rounds fade in and out over about 5 ms, so changing ROUNDS does not click.
POWER drives the input by `1 + 0.7 * power` and mixes `dry * (1 - power) + wet * power`.

## Tested (core/tests/test_core.c)

- POWER 0 leaves the buffers bit-identical.
- No NaN or Inf and no peak above 1.0 for full-scale noise at 44.1, 48 and 96 kHz across parameter corners.
- PUNCH raises the attack to body ratio of a synthetic drum loop (4.77 to 7.84 at full PUNCH).
- 1 round and 24 rounds produce clearly different output.
- Changing ROUNDS and FREQ every 128-sample block gives no sample step above 0.06 on a 220 Hz sine.

Not tested: listening comparison against a real OP-1. Treat the sound as a new effect that shares the OP-1's controls.
