# Pulse DSP

## What the OP-1 does

The OP-1 synth engine is called pulse on the screen and `Pls` in the firmware. Here is what I could take from the firmware,
and it is not much:

- The decompile of `dsp/Pls` holds only a constructor. It sets up an array of 6 objects of 96 bytes each (the bounds check
  names `src/dsp/Pls.cpp`, index 0 to 5), clears a few small fixed-size arrays in each and calls a memset helper. There is no
  oscillator, filter or envelope code in it. I could not read the sound out of the firmware, so the DSP is designed from what
  a pulse engine does and from the other facts below.
- Factory defaults, table `synth_types` id 6: `[20000, 16000, 4000, 3000, 0, 0, 0, 0]` of 32767. Only the first four are
  non-zero, so the four extra engine values are unused here. The first three are unsigned in every factory preset. The fourth
  goes negative in several presets.
- The 16 factory presets of type pulse (chord lr, fat bass, pulse lead, warm pad, and so on) put the first knob at 15200 to
  23168, the second at 6464 to 16384, the third at 0 to 16384 and the fourth at -9288 to +6141. I used these ranges to decide what
  each knob should cover and where the defaults sit. Preset names like "chord lr" and "fat bass" point to a stereo-spread
  voice and a thick low pulse.
- The screen art (`display/pls.svg`) is a blue pulse-wave train, two circles, a green dashed vertical sweep, white bars and a
  pink stepped bar. That is why the four knobs keep their blue, green, white and orange colours and why the picture on the
  faceplate is a pulse train.

The control meanings below are my reading of those facts, not decoded from the firmware.

## Controls

| Knob | Colour | Meaning here | Range | Default (raw / 32767) |
|---|---|---|---|---|
| SHAPE | blue | resting pulse width: 0 is a 50 percent square, 1 is a 4 percent pulse | 0..1 | 20000 -> 0.610 (22 percent duty) |
| SWEEP | green | depth of the pulse-width LFO | 0..1 | 16000 -> 0.488 |
| RATE | white | speed of the pulse-width LFO, 0.2 Hz to 25.6 Hz, exponential | 0..1 | 4000 -> 0.122 (0.36 Hz) |
| DETUNE | orange | spread between the voice's two oscillators, +-100 cents at the ends | -1..1 | 3000 -> 0.092 (9 cents) |

The amp envelope (attack 0.5 ms to 4 s, decay 5 ms to 4 s, sustain 0..1, release 5 ms to 6 s), output level and pitch bend
range (1 to 12 semitones, default 2) are not in the factory table for this engine. They are my own starting points.

## What this version does

Eight voices. Each voice has two band-limited pulse oscillators (A and B), one LFO and an ADSR.

1. A and B run at the played pitch moved by minus and plus half the DETUNE spread. A leans left in the stereo field (75/25),
   B leans right (25/75). With DETUNE at 0 and SWEEP at 0 the two channels are bit-identical.
2. Pulse width for A is `w = (1 - SHAPE) + SWEEP * lfo / 2`, and B sweeps the opposite way (`- lfo`), so the width moves in
   opposite directions in the two channels. `w` is clamped to 0..1 and mapped to a duty cycle of 4 percent to 50 percent. The LFO is
   a parabolic sine. Each voice starts it at a different phase (voice index / 8). The mod wheel pushes SWEEP toward 1.
3. The pulse is a polyBLEP-corrected band-limited pulse with the mean removed, and a gentle level drop for thin pulses so they do
   not jump out of a chord. The duty cycle is held above 1.5 times the phase increment so the two edge corrections never overlap.
4. Envelope: linear attack, exponential decay toward sustain, exponential release. Decay and release times are the time to fall
   60 dB.
5. Voice allocation: a note struck again takes over its own voice. Otherwise a free voice, then the quietest releasing voice,
   then the oldest held voice. A taken-over voice fades out over 2.5 ms and then starts the new note from zero, so a steal or a
   retrigger has no hard cut. Sustain pedal, pitch bend, mod wheel, all notes off and all sound off are handled in the core.
6. Output: sum of the voices, times the level, through a soft ceiling that is linear up to 0.7 and approaches 0.98.
7. Control values are smoothed per sample (about 5 ms). Pitch ratios and LFO speed are recomputed every 16 samples. The result
   does not depend on how the host splits the stream into blocks.

No firmware code, constants or tables are used. This is a new implementation inspired by the OP-1 engine, not bit-exact.

## Tested (core/tests/test_core.c, clang with ASan and UBSan)

- Pitch of the played note within 5 cents, 22.05 kHz to 192 kHz, notes 36, 60, 84, with and without the default detune. Pitch
  bend at full wheel gives plus and minus 200 cents. Pitch is measured with SWEEP at 0 because the PWM LFO amplitude-modulates the
  fundamental and skews a short-window spectral peak.
- Eight voices at velocity 127 and level 1.0: finite, peak under 1.0 in 36 knob-corner cases, with the soft ceiling holding it at 0.98.
- Release reaches silence (-30 dB at half the release time, digital silence afterwards), no stuck voices, note on with velocity 0
  releases, sustain pedal holds and releases, all notes off releases, all sound off is silent within 6 ms.
- Retrigger and steal: the fade follows the linear 2.5 ms ramp within 0.003 and the new note starts from near zero.
- Each knob changes something measurable: SHAPE (second harmonic against fundamental), SWEEP (second harmonic movement), RATE
  (LFO speed ratio), DETUNE (channel pitch split, identical channels at 0), mod wheel, attack, sustain, decay, level, velocity.
- Output is bit-identical for host blocks of 1, 7, 64, 100, 1000 and 60000 frames, and after `pulse_reset`.
- 2.4 million frames of random notes, controls and events at eight sample rates and blocks of 1 to 255: no NaN or Inf and no peak above 0.98.

Not tested: a listening comparison against a real OP-1. Treat the sound as a new synth that shares the OP-1's controls.
