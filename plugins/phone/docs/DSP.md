# Phone DSP

This is a new implementation inspired by the OP-1 Phone effect. It is not bit-exact and it contains no firmware code,
constants tables or variable names. The facts below came from reading the decompiled firmware routine (`Ptch`, the OP-1
screen file `ptch.svg`) and the factory defaults. Everything else is my own design.

## What the OP-1 does

The effect is called Phone on screen and `Ptch` in the firmware. Its screen is an exploded telephone handset with a chip, a
pot, a mic capsule, a speaker capsule and a patch of noise. The four knobs are labelled TONE (blue), PHONIC (green),
BAUD (white) and TELEMATIC (red). The factory defaults are 8000, 8000, 8000 and 16000 out of 32767.

The routine works on 64-sample blocks of 16-bit mono audio. From the decompile:

- The input is written into a capture buffer. A rising zero crossing is a rise through about +0.001 after the signal has
  been below -0.001. The capture ends once enough crossings are counted: the count is the raw PHONIC value shifted right by
  11 (0 to 15), and a crossing only counts after the capture holds at least 128 samples. The longest capture is 735 samples
  per counted cycle (one cycle of 60 Hz at 44.1 kHz). So a fragment is a few whole waveform cycles.
- Three buffers rotate between writing, "newest complete fragment" and playing, so the newest fragment always wins.
- The playing fragment loops. After the loop count, set by the raw BAUD value shifted right by 10 (0 to 31), playback moves
  to the newest fragment if there is one. Otherwise it keeps looping the old one.
- Playback reads the fragment through a polyphase interpolator at a rate set by TONE. The rate is 16 times the knob value
  squared, so the knob at a quarter turn plays at the original speed. The default 8000 gives 0.954.
- The wet signal is multiplied by a level follower of the input (about 10 ms, and it reaches full scale at about -48 dBFS).
  When the input stops, the loop fades out instead of ringing forever.
- TELEMATIC is a linear dry/wet crossfade.

Not verified on a device: which knob is which. I mapped them in the OP-1's usual knob order (blue, green, white, red), which
lines up with how the routine uses its four values and with the labels on the screen.

## What this version does

Per sample, for the stereo pair:

1. A 20 Hz one-pole high-pass on each channel, so DC offset does not stop the crossing detector and loops carry no offset.
   The crossing detector listens to the mono sum.
2. Capture starts on a crossing, so every fragment begins and ends at a crossing and the loop seam is nearly continuous.
   Crossings count once the fragment holds at least 2.9 ms (128 samples at 44.1 kHz, scaled with the sample rate). A
   fragment that finds no crossing within PHONIC x 1/60 s is dropped.
3. The newest complete fragment is played in a loop. After BAUD passes it moves on to the newest fragment, if a newer one
   exists. Both channels are stored, so the stereo image survives.
4. Playback at rate 16 x TONE^2, clamped to 1/16 to 16, smoothed over about 6 ms in the log domain. Source samples go
   through two one-pole low-passes whose corner falls as 0.45 / rate above unity rate, then an 8-tap Blackman-windowed sinc
   interpolator (64 phases with linear interpolation between them).
5. Wet is the interpolated signal times the level follower, with a soft ceiling above 0.8 so it never passes 1.0.
6. Output is `dry * (1 - TELEMATIC) + wet * TELEMATIC`. At TELEMATIC 0 the buffers are left untouched, bit for bit.

Controls, as the plugin shows them: TONE in semitones (-48 to +48), PHONIC 1 to 15 cycles, BAUD 1 to 31 loops, TELEMATIC
0 to 100. BAUD 1 and BAUD 0 would behave the same in the firmware, so the range starts at 1.

Memory is a fixed 1.2 MB per instance (three fragment buffers, two channels, sized for 192 kHz). Cost is small: about 0.1 to
0.6 percent of one Apple Silicon core for stereo at 44.1 kHz.

## Tested (core/tests/test_core.c, run with core/tests/run.sh, clang with ASan and UBSan)

- TELEMATIC 0 leaves the buffers bit-identical, also after a fade down from a wet setting.
- A 220 Hz sine at TONE ratios 0.5, 1, 2 and 4 comes out at 110.0, 220.0, 440.0 and 880.0 Hz. The same holds at 22.05, 32,
  48, 96 and 192 kHz (within 2 percent).
- Level at unity rate and full mix stays within 1.5 dB for 100 Hz, 440 Hz, 1 kHz and 4 kHz (measured -0.20, -0.06, -0.12,
  -1.27 dB).
- A 100 Hz sine gives 33.3 fragment changes per second at PHONIC 3 and BAUD 1, 8.7 at BAUD 4, 99.3 at PHONIC 1 and 20.0 at
  PHONIC 5.
- When the input stops the wet signal falls below 0.001 within 0.2 s. A DC input produces no wet signal and no fragment.
- Step and impulse responses are finite and settle: the step ends at its dry share, the impulse tail is below 1e-4.
- Noise and full-scale sines at every corner of the controls and five sample rates: finite, never above 1.0.
- Output is identical for block sizes 1, 7, 64 and 1000. Reset gives the same output as a fresh instance.
- Identical channels stay identical, different channels stay different. Changing every control every 128 samples gives no
  sample step above 0.08 on a 220 Hz sine.
- No denormals in the output or the filter state after loud input followed by silence. Each interpolator row sums to 1.

Not tested: a listening comparison against a real OP-1, and a perceptual check of the aliasing filter at extreme TONE.
Treat the sound as a new effect that shares the OP-1's controls.
