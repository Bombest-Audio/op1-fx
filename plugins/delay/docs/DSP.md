# Delay DSP

## What the OP-1 does

The effect is called Delay on the OP-1 (row 5, `delay`, in the factory `fx_types` table, defaults 8000, 8000, 8000, 8000 out of
32767). Its screen (`delay.svg`) has the labels INPUT, FDB and SPEED, a square field of concentric rings, and a line from the
left edge into the middle of the rings.

The firmware codename is not stated anywhere. The RE notes list `Dssd` as "delay, unconfirmed", and I checked that before
using it. Three things point the same way: the Dssd routine is a single ring buffer with a feedback path, its four
parameters line up with the three screen labels plus the effect's fourth knob, and it keeps a peak follower of the input that
looks like the INPUT meter on the screen. I also looked at `Ftwo`: it has its own screen (`ftwo.svg`), no ring buffer, and uses
the filter tables, so it is a separate filter effect and I did not use it. I call Dssd the delay with good confidence, not proof.

Facts read from the Dssd routine (it works on 64-sample blocks of 16-bit audio with 32-bit internal values):

- One ring buffer per effect. Its length is 44100 shifted right by a small integer, 0 to 5, taken from a six entry table
  indexed by the top bits of the first parameter. So the buffer holds between 1 s and 1/32 s of slots at 44.1 kHz.
- Three pointers walk that ring: a read pointer, a clear pointer and a write pointer. They all advance by the same
  fraction of a slot per sample. That fraction is the second parameter divided by 32768, never below 33/32768. At full
  value the ring is clocked at the audio rate, at lower values it is clocked slower.
- Reads and writes go through a 7 tap polyphase interpolator (32 phases, blended between phases). A write adds a small
  kernel into the ring at a fractional position, which is why there is a pointer that clears slots after they are read.
- The output is the dry input plus the value read from the ring at unity gain.
- The value written into the ring is the input times the fourth parameter plus the third parameter times the value just
  read. That is the feedback loop. A factor of 2 appears around the feedback multiply, which I read as a fixed point format
  artifact, not a gain of 2.
- The ring and the sums saturate at full scale.

So the delay time is ring length divided by the clock rate, and a slower clock also lowers the bandwidth of what goes through
the ring (the write interpolator is a low-pass at the ring's own Nyquist rate). That is a tape delay: slow the tape and the
repeats get longer and darker, and changing the speed while audio is in flight bends its pitch.

Not recovered: how the first knob maps onto the shift (the table only has six entries, the knob has 32768 values), the
interpolator table itself (it is not in the image I read), and the starting offsets of the three pointers, so the exact delay
time at a given setting is not known.

## What this version does

This is a new implementation inspired by the OP-1 effect. It is not bit-exact and carries no firmware code or tables. The
controls and their order (blue, green, white, orange on the OP-1) are:

| Control | Value 0 to 1 means |
|---|---|
| TIME | Loop length at full tape speed: 1/32 s at 0, 1 s at 1, exponential. The name is mine, the screen has no label for it. |
| SPEED | Tape speed, linear, floored at 1/32. Delay time is loop length divided by speed. |
| FDB | Feedback gain, 0 to 1. At 1 the repeats do not decay. |
| INPUT | Level of the signal sent into the tape. The dry signal always passes at unity. |

Factory defaults (0.244 each) give a delay of 0.298 s with about 4.3 kHz of echo bandwidth (-3 dB) and echoes about 12 dB down.
The delay is capped at 8 s: if TIME and SPEED would ask for more, the speed is raised to hit 8 s.

Per channel (left and right are two independent lines with shared controls):

1. Read the line `D` samples behind the write point with a 4 point cubic Hermite interpolator.
2. `wet` is that read. The output is `dry + wet`.
3. The write value is `input_gain * dry + feedback * wet`, sent through two one-pole low-passes, then a soft ceiling
   (linear to 0.75, tanh knee to 1.0), then stored. The low-pass corners follow the tape speed: each pole sits at
   `27.5 kHz * speed`, which puts the combined -3 dB point near 0.4 of the Nyquist rate of a tape clocked at
   `speed * 44.1 kHz`. The corner is in Hz and does not depend on the host sample rate (capped at 0.45 of it).
4. `D` is `loop length / speed` and glides toward its target. The glide is limited to 0.5 samples per sample when the delay
   grows and 1 sample per sample when it shrinks, so the pitch of what is in flight moves by at most one octave down or up.
   Turning TIME or SPEED therefore sweeps the repeats like a tape speed change and never jumps.

I model the tape as a delay line at the host rate plus a speed-dependent low-pass, not as a ring clocked at a variable
rate with sinc writes like the firmware. The two behave the same for what you hear (delay time, darkening, pitch bend on a
change). The difference is that a clocked ring keeps memory fixed while the delay grows, and mine uses up to 8 s of samples.

Housekeeping: the core never allocates, the caller passes two buffers of `delay_buffer_frames(sr)` floats. Controls are
smoothed per sample (30 ms). Control-rate work (exp, filter coefficients) runs every 8 samples from a counter inside the
state, so the output does not depend on the host block size (tested bit-exact for block sizes 1, 7, 128 and 5000). Stored
values are flushed to zero below 1e-20, input that is not finite or above 16 in magnitude is not fed to the tape, and
`delay_reset` clears the tape and all filter state.

Level: the tape is bounded by 1.0 and the cubic read can overshoot that by about 10 percent on noise, so `wet` stays under
1.3 in the tests. `dry + wet` can of course exceed 1.0, as with any delay.

## Tested (core/tests/test_core.c, clang, ASan and UBSan)

- Parameter maps: TIME 0 and 1 give 1/32 s and 1 s, monotonic; the delay never exceeds 8 s and does reach it.
- INPUT 0 on an empty tape leaves the buffers bit-identical (22.05, 44.1, 48, 96, 192 kHz).
- Impulse response at 0.5 s, FDB 0.5: echoes land at 22050, 44100, 66150, 88200 samples with areas 0.5, 0.25, 0.125, 0.0625
  (each exactly FDB times the one before), nothing before the first echo, left equals right.
- Step response settles to dry plus INPUT times dry (0.45 for 0.3 and INPUT 0.5).
- Slow tape is darker: a 500 Hz echo passes at 1.00 at full speed and 0.996 at speed 0.3; a 10 kHz echo is 0.824 at full speed
  and 0.477 at speed 0.3.
- Delay time law: measured echo position matches `delay_time_seconds` within 12 samples at nine TIME, SPEED, rate combinations
  (22.05 to 192 kHz, 31 ms to 8 s).
- No NaN or Inf, and echo level at or under 1.3, for noise at all 16 corners of the four controls at five sample rates.
- FDB 1 stays bounded and keeps ringing for 10 s; FDB 0.5 decays to exact zero (no denormal tail).
- Output is bit-identical across block sizes 1, 7, 128 and 5000. The left output does not depend on the right input (so the
  mono path through a scratch channel is exact).
- TIME and SPEED jumping every 0.12 s on a 220 Hz sine: largest sample step 0.0525. With the glide removed the same test gives
  0.99, so the test does catch a click.
- The delay glide stays inside -1.0 to +0.5 samples per sample for a 31 ms to 1 s jump and settles on the target.
- `delay_reset` clears the tape. NaN, infinity and 1e30 on the input never reach the tape.
- Activity reads 0 for silence, 0.7 for a 0.8 impulse and its echoes, and clears when taken.
- Tail length: one delay with no feedback, several repeats at FDB 0.5, 30 s cap at FDB 1.

Not tested: a listening comparison against a real OP-1, and the real OP-1 delay times.
