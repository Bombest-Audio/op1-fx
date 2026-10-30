# DSynth DSP

## What the OP-1 gives me

The OP-1 synth engine is called dsynth in the firmware. The decompile has no readable DSP for it, so this plugin is a new
design built from what the machine shows on screen and a few structural facts. It is not a bit-exact port.

What I used:

- The screen: four knobs (MOD, WAVE, DECAY, FILTER), two oscillator panels, and a filter dial with eight positions.
- The factory preset (synth_types id 9): 20664, 0, 8528, 6225, 328, 0, 12130, 11465 of 32767.
- The class layout: an object index range of 0 to 5, 0x48-byte objects and two sub-objects per voice. That points to two
  oscillators per voice. The 0 to 5 range suggests six voices on the device. I use 8, as the brief asked.
- dsynth sits in the chromatic synth list, not in the drum kit. So it plays pitched across the keyboard.

## Kit or pitched voice

Pitched percussive voice. All eight voices share one sound, and the played note sets the pitch. A kit across MIDI notes
would need a per-note parameter set, and I found none. So I did not build one. Each note starts with a short pitch drop and
a filter that opens and closes, which gives the thump of a drum, and the pitch still follows the key.

## Signal path, per voice

1. Oscillator 2 runs at the note pitch times a ratio (1 to 8, from `osc2_pitch`). Its shape is sine to saw (`osc2_wave`).
2. Envelope 2 (exponential decay, from `osc2_decay`) scales oscillator 2. That sets the modulation depth over time.
3. Oscillator 2 phase-modulates oscillator 1. Depth is up to 9 radians (`MOD`, squared). FM here is phase modulation.
4. Oscillator 1 is sine at `WAVE` 0, square at 0.5, saw at 1, blended between. Square and saw use PolyBLEP.
5. Envelope 1 (from `DECAY`) scales oscillator 1. Past the middle of the knob the decay stays long and an attack grows.
6. Envelope 2 also drops the pitch at the start of the note, up to 7 semitones, scaled by the envelope to the sixth power.
7. The amp ADSR and velocity scale the voice. The amp envelope is exponential, 60 dB per time setting.
8. A TPT state-variable filter, two stages, with eight modes. Envelope 1 opens the cutoff up to 2 octaves.
9. Pan, sum of voices, output gain (`LEVEL` squared), then a soft limiter (knee 0.6, ceiling 0.98).

## Filter modes

| Dial | Mode |
|---|---|
| 0 | off |
| 1 | low-pass 12 dB (factory) |
| 2 | low-pass 24 dB |
| 3 | band-pass |
| 4 | narrow band-pass |
| 5 | high-pass 12 dB |
| 6 | notch |
| 7 | resonant low-pass |

Mix weights are smoothed per tick, so switching modes does not click.

## The eight factory values

| Raw value | Normalized | Used as |
|---|---|---|
| 20664 (MOD) | 0.6306 | FM depth |
| 0 (WAVE) | 0 | oscillator 1 shape, sine |
| 8528 (DECAY) | 0.2603 | oscillator 1 decay, 192 ms |
| 6225 (FILTER) | dial 1 | low-pass 12 dB |
| 328 | 0.0100 | `osc2_pitch`: oscillator 2 ratio, 1.02 (about unison) |
| 0 | 0 | `osc2_wave`: oscillator 2 shape, sine |
| 12130 | 0.3702 | `osc2_decay`: envelope 2, shapes modulation and pitch drop |
| 11465 | 0.3499 | `cutoff`: filter corner above the note, about 6.7 times its frequency |

I guessed which extra is which from the order and from the screen. I did not confirm it from the firmware. The plugin and
the MPC port keep these four at the factory value and do not expose them as controls. The core still has them as fields.

## Voices and timing

- 8 voices. A new note reuses the same key if sounding, then a free voice, then steals the quietest.
- A steal fades the old sound over 2 ms, then starts the new note. This avoids a click.
- Control values update every 16 samples (a tick). Ramps come from the tick position, never accumulated, so the output is
  bit-identical however the audio is split into render calls.
- Smoothing: one-pole at tick rate, 5 ms.
- Mod wheel (CC1) adds to MOD. Pitch bend range is a parameter (1 to 12 semitones, default 2). CC64 holds notes. CC120 and
  CC123 release everything.

## Caveats

- Aliasing: PolyBLEP only tames the square and saw edges. Phase modulation with a bright modulator and a high ratio makes
  partials above Nyquist that fold back. I did not oversample. It is audible at high notes with MOD near full.
- Not tested by ear against a real OP-1. I only have the numbers.

## Tested (core/tests/test_core.c)

53 checks under ASan and UBSan: knob mappings and defaults, pitch accuracy, bend, release and pedal, all-off, natural decay,
polyphony and steals, headroom, click thresholds, determinism and split invariance, reset, each knob, velocity, pitch drop,
sample rates and block size 1, subnormals, parameter abuse, and the activity API.
