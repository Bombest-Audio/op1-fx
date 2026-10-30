# DrWave DSP

## What was recoverable from the OP-1 firmware, and what was not

The OP-1 engine is called "dr wave" on screen and `DrWave` in the firmware. I searched `op1f-re` for how it is implemented. The RE README
says "DrWave is direct" and there is no `DrWave` folder under `work/decomp/dsp/`. What exists:

- One assert string, `..\src\dsp\DrWave.cpp:592 i >= 0 && i < kSynthVoiceCount`, inside the producer factory's constructor for the engine
  (`producer_factory__callees/0197ada8.c`). It shows an engine object of 0x24 bytes per voice and six voices. No DSP.
- The factory DB row `synth_types` id 3: knob values `[24000, 16000, 8200, 24000, 32000, 0, 0, 0]` of 32767.
- Sixteen factory presets in the folder "dr wave" (`synth_presets`). Across them the first value ranges from 42 to 24154 (wide, used like a
  selector), the second from 204 to 12754, the third sits near 8200 in 12 of 16, the fourth is 0 in 13 of 16, and the fifth is 32000 or 32767
  in all of them.
- `display/drw.svg`, the screen asset: an empty 320 x 160 canvas, so no labels.
- `T10` and `Id` hold only a bounds check and a jump each, so nothing is recoverable there either.

So the sound design is mine. It is a wavetable-style oscillator with waveform selection, blend and modulation, which is what the engine is
known to be. It is not bit-exact and uses no firmware code or tables. Control names, the roles of the four knobs and the TONE extra are my
reading of the preset statistics, not facts from the firmware.

## Controls

| Control | OP-1 value (of 32767) | What it does here |
|---|---|---|
| WAVE (blue) | 24000 | Position along a bank of eight single-cycle waves: sine, triangle, organ, vowel, saw, square, 25% pulse, digital. Neighbouring waves are crossfaded, so the knob morphs. Default 5.13 is square sliding toward pulse. |
| BLEND (green) | 16000 | Equal-power crossfade from the wave to its mirror image in the bank (position 7 minus WAVE). The mirror oscillator runs 6 cents sharp, so the middle of the knob shimmers. |
| MOTION (white) | 8200 | Each note starts its wave position up to 4 positions above the WAVE setting and settles onto it, 60 dB in 1.2 s. At 0 the position stays put. |
| FOLD (orange) | 24000 | Sine wavefolder. The wave is multiplied by `1 + 6 * fold^2` and run through a sine, mixed with the dry wave by the fold amount, and scaled by `1/(1 + 1.4 mix^2)` to hold loudness. 0 is a bit-exact bypass. |
| TONE (extra, fifth value) | 32000 | One-pole low-pass per voice, 80 Hz to 20 kHz exponential, open at the top. It is in all presets at maximum, which is why I read it as a brightness control. |

The envelope (attack 3 ms, decay 1.2 s, sustain 0.6, release 0.25 s), output level and bend range are mine. The mod wheel pushes the wave
position up by 1.5 positions. The other three engine extras (all 0) are unused.

## Structure

- 8 voices, voice stealing prefers a voice in release, then the oldest note. A retrigger or steal keeps the voice's phase, envelope and slewed
  controls and slides to the new note, so it does not click.
- Wave bank: eight waves defined by harmonic spectra, built at init with an inverse FFT into 2048-sample tables. Each wave has 9 band-limited
  copies, one per octave of pitch (480, 240, 120, 60, 30, 15, 7, 3 and 1 harmonics). A note reads the level whose top partial stays under 0.4 of
  the sample rate, crossfading to the next level over the first 0.3 octave past its limit so the partials stay under Nyquist. All waves are scaled to
  the same RMS (0.30) with the peak capped at 1. The table is 585 KB per instance.
- Controls update every 16 samples and ramp linearly in between. Knob smoothing is 5 ms, per-voice slews 2 ms. Output does not depend on how
  a stretch of time is split into blocks (tested bit-exact for block sizes 1 to 4096).
- Output stage: voices summed, times 0.8, a soft ceiling linear to 0.7 and reaching 0.97, then level squared. The peak cannot pass 0.97.
- FOLD is a waveshaper, so it can alias. Its amount falls to a half at 2 kHz and a tenth at 18 kHz to limit that.

## Tested (core/tests/test_core.c, clang, ASan and UBSan)

Pitch within 5 cents (measured 0.09 cents for the sine over 22.05 to 192 kHz, 0.55 cents for saw, vowel and digital), full bend exactly 200
cents, 8 voices at velocity 127 under 0 dBFS in five parameter sets, release to exact silence, pedal and all-notes-off, steal and retrigger
steps no more than 1.2 times a plain note start, each knob changing the spectrum, MOTION settling to the WAVE spectrum, block-size independent
output, 22.05 to 192 kHz corners finite, parameter jumps without zipper steps, aliasing of the unfolded waves. See the report in the
repo notes for the printed numbers. Not tested: a listening comparison against a real OP-1.
