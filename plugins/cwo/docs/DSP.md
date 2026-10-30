# Cwo DSP

## What the OP-1 does

The OP-1 effect is called CWO on screen (a cow) and `BodeAudioEngine` in the firmware. Its screen shows the labels FREQ, DELAY and
FDBACK, and a fourth control. Factory defaults are 5000, 8192, 16384 and 32767 out of 32767. Facts I took from the firmware strings and
the other material, and what each one is worth:

- The assert strings in `BodeAudioEngine.cpp` name a sine table, a rate table, a delay table, a table of interpolation coefficients
  for fractional delay reads, a delay line, and an oscillator phase and total rate for each of the left and right channels. Phase and
  rate are asserted to stay inside half a turn, which is how a frequency shifter keeps its oscillator below Nyquist. Together that says
  delay line plus oscillator, which is a Bode frequency shifter. This is the strongest evidence, and the repo README reached the same
  guess.
- The delay time is asserted to be positive and shorter than the line, and is read with an interpolation table, so the OP-1 reads
  fractional delays. I did not recover the line length or the table sizes, because they are compile-time symbols, not values in the
  strings.
- Each channel has its own oscillator phase, so the two channels can sit at different points of the shift.
- The control order FREQ, DELAY, FDBACK, SIDEBAND follows the screen labels and the order the controls reach the code. I did not
  confirm the fourth control's name on a running unit.
- A Max for Live device (`op1-docs/CWO.zip`, "CWO" by Johny Maandag, CC BY 4.0) is built on the same effect. It has four dials,
  Frequency, Delay, Feedback and Sideband, and its patch uses `tapin~`, `tapout~` and `freqshift~`. That confirms the control set and
  the delay-plus-shifter pairing. I did not copy the patch, its ranges or its layout.

The firmware arithmetic is Blackfin fixed point with lookup tables. I did not recover the table contents, so every curve below is my own
choice. This is a new implementation inspired by the OP-1 effect, not bit-exact, and it contains no firmware code or tables.

## What this version does

Per channel, per sample:

1. Read the delay line `D` samples back with a four point Catmull-Rom interpolator.
2. Run the delayed sample through a Hilbert pair: two paths of four first-order allpass sections in z^2 (Olli Niemitalo's coefficients),
   one path delayed by a sample. The two outputs keep unit gain and sit about 90 degrees apart from roughly 40 Hz to just under Nyquist.
3. Combine with the oscillator: `shifted = Q*cos(w) + (1 - 2*sideband) * I*sin(w)`. Sideband 1 is the upper sideband (shift up), 0 the
   lower, 0.5 is ring modulation.
4. Write `softclip(input + feedback * shifted)` into the delay line. The soft clip is linear to 1.0 and reaches 2.0, so it only acts when
   the loop is driven hard.
5. Compute the effect output `wet = softclip(input + shifted)`, linear to 0.9 and capped at 1.0.
6. Output `input + mix * (wet - input)`.

So every echo is shifted once more than the one before, and the first echo is already shifted. That is my choice: with the output taken
after the shifter, FREQ and SIDEBAND do something at zero feedback too. I did not check where the OP-1 or the Max for Live device takes
its output.

The left and right oscillators start a quarter turn apart, which spreads the repeats across the stereo field. The firmware has separate
phases per channel (from the asserts); the quarter turn offset is mine.

### Controls

| Control | Mapping | Default |
|---|---|---|
| FREQ | shift in Hz = 0.5 * (8000^x - 1), so 0 Hz to 4 kHz, exponential | 0.1526, 1.5 Hz |
| DELAY | 1 ms + 599 ms * x^2 | 0.25, 38 ms |
| FDBACK | gain = 0.92 * x | 0.5, gain 0.46 |
| SIDEBAND | 0 lower, 0.5 ring modulation, 1 upper | 1.0, upper |
| MIX | dry/wet, 0 is the input untouched, 1 is the effect output | 1.0, 100% wet |

FREQ, FDBACK, SIDEBAND and MIX are smoothed with a 20 ms one-pole. DELAY glides with a 60 ms one-pole and the speed of the read point is held
between 0.1x and 1.9x, so the read point is continuous and never passes the write point. A delay change bends the pitch of the echoes, like
tape. Delay memory is sized from the sample rate (600 ms, capped at 131056 samples per channel, so the cap bites above about 218 kHz).
The shortest delay is 1 ms or 8 samples, whichever is longer.

### Mix

MIX is a parallel dry/wet blend. The wet signal is the output the effect had before MIX existed, so the default is 1.0 (100% wet), which
keeps every old setting and saved session sounding the same. I did not pick 50% because that would change the sound of existing
sessions. Two things follow from how the effect works:

- The wet output already contains the dry input (the first term of `input + shifted`). So MIX does not fade the dry signal out. It
  scales the shifted echoes in: at 0 you hear the input only, at 100% you hear the input plus the full echoes, and in between the
  echoes are proportionally quieter. That is what makes it a parallel control.
- There is no latency to line up. The dry sample is in the wet output on the same sample, and the delayed echoes are meant to be late.

At exactly 0 the code copies the input sample, so the output is bit-identical to the input. The smoothed mix snaps to its target when it
is within 1e-5, so 0 and 1 are exact once the smoothing settles (about 230 ms after a jump from the other end). Activity for the
animation is still the level of the shifted echoes and does not depend on MIX.

## Tested (core/tests/test_core.c, clang with AddressSanitizer and UBSan)

- Silence in gives silence out, and `cwo_reset` clears every echo.
- Before the first echo arrives, the output is bit-identical to the input.
- The echo of an impulse arrives within 2 ms of the commanded delay at 22.05, 44.1, 48, 96 and 192 kHz.
- A 1 kHz sine shifted by 200 Hz: upper sideband puts 65 dB more energy at 1200 Hz than at 800 Hz, lower sideband the reverse, both at unit
  gain within 1 dB. At sideband 0.5 both sidebands are equal and 6 dB down.
- Feedback 0.5 decays each repeat, feedback 0 gives one echo, full feedback at a 1 ms delay decays to nothing.
- No NaN or Inf and no peak above 1.0 for full-scale noise and for full-scale DC, over 240 corner combinations of the four controls at five
  sample rates from 22.05 to 192 kHz.
- Changing all four controls every 128 samples gives no sample step above 0.15 on a 220 Hz sine (measured 0.062).
- MIX 0 is bit-exact dry (memcmp against the input, block size 37, with the other controls at hard settings). MIX 0.5 matches the
  average of dry and wet to 6e-8. A dry impulse stays on its own sample. Full-scale noise over MIX 0 to 1 peaks at 1.0000. Jumping MIX
  between 0 and 1 every block while a 220 Hz sine plays gives a largest sample step of 0.0311, and MIX 0 settles back to exact dry.
- A silent channel stays silent (no crosstalk), and block size 1 matches block size 128 bit for bit.

Not tested: a listening comparison against a real OP-1. Treat the sound as a new effect that shares the OP-1's controls.
