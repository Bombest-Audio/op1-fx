# DBox DSP

## What the OP-1 does, and what I could and could not read

The OP-1 drum engine is called dbox on screen and in the firmware (`dsp/dbox.cpp`). The decompile folder holds only two
stubs: a setter that stores a voice index 0..11 and jumps through a table, and a constructor with the same bounds check. No
synthesis code survived decompilation, so the sound design here is mine, built from what the firmware and screen assets show:

- The assert text reads `i >= 0 && i < kDrumVoiceCount` and the bound in the code is 12. I use 12 voices (the synth brief
  says 8; a kit needs more at once, so I kept the firmware number).
- `dbox.svg` (the OP-1 screen) shows two oscillator panels and one large dial, plus a grid of 24 key cells (ids k0 to k23).
  Panel 1 has a pitch knob (signed, 0 at 12 o'clock), a three-position wave slider (sine, square, saw-like ramp) and a DECAY
  knob whose far end is labelled ATTACK. Panel 2 has a mix knob (0 to full), its own wave slider and decay knob. The large
  dial has positions 0 to 7 with MODE and FILTER labels.
- `drum_types` id 1 holds eight defaults: 0, 0, 2917, 974, 7167, 0, 2592, 4607 of 32767.

From that I read the eight values as: PITCH 0 (centre), WAVE 0 (sine), DECAY 2917, MODE 974 (position 0), then the extras
osc 2 MIX 7167, osc 2 WAVE 0, osc 2 DECAY 2592, FILTER 4607. This mapping is an inference from the screen layout and the
order of the values, not something I read in code.

## Kit or pitched voice

A kit. The 24-key grid on the screen, the per-voice assert and the 12 producers say each key is its own drum voice. Key k
(MIDI note root + k, root 36 by default) plays drum k from a table of my own: four kicks, two snares, rim, clap, four toms,
four hats, crash, ride, cowbell, clave, two congas, shaker, zap. Notes outside the two octaves fold back in (note 60 plays
key 0). Closed, pedal and open hat choke each other, crash and ride choke each other. The four screen knobs are global and
move every drum together.

## What this version does

Per voice, per sample:

1. Oscillator 1 at the key's base pitch, dropped by a fast pitch sweep (kicks 30 semitones over 35 ms, zap rises), plus PITCH
   (+-12 semitones) and pitch bend. Wave morphs sine to square to saw (polyBLEP band limiting).
2. Oscillator 2 at a ratio set by MODE: 0 layer (1x), 1 octave, 2 fifth, 3 sub, 4 ring modulation, 5 FM, 6 hard FM,
   7 noise instead of oscillator 2. MIX is its level, or the FM depth.
3. A high-passed noise burst whose level and brightness belong to the key (snares, hats, clap).
4. Each part has its own exponential decay. DECAY maps 0 to 0.8 of the knob to 50 ms to 2.5 s (times a per-key scale), and
   the top fifth keeps the long decay and adds a slow attack up to 80 ms.
5. FILTER is a 2-pole low-pass from 20 kHz to 60 Hz. The mod wheel closes it further.
6. Velocity scales level and sweep depth. A gate envelope (attack, decay, sustain, release) sits on top. The defaults are
   inert (sustain 100%, release 3 s) so a drum rings out like a one-shot.
7. Output: sum, level squared, soft ceiling (linear to 0.75, tanh knee to 0.99) so the output never reaches 0 dBFS.

PITCH, WAVE, MIX, WAVE 2, FILTER and LEVEL move live (5 ms smoothing). DECAY, DECAY 2 and MODE are read when a note starts.

Stealing: a new key takes a free voice, else the quietest one. Retriggering a sounding key and stealing both fade the old
voice over 1 ms and start the new note after that, so there is no step. Block splitting never changes the output.

## What I did not use

Pitch bend range, root note and the envelope are mine, not firmware values. The plugin exposes root and bend range; the MPC
port keeps them at 36 and 2 semitones.

## Tested (core/tests/test_core.c, sanitizers on)

Pitch of key 10 within 5 cents of its table value, PITCH +12 and bend +2 semitones within 5 cents; 24 notes into 12 voices at
three loud settings with peak under 1.0; note off and sustain pedal behaviour; no sample step on retrigger or steal beyond
the single-hit step; every knob and each envelope stage changes the output; hat choke; bit-identical output for 1, 77 and
whole-block splits; reset equals fresh; 5 sample rates from 22050 to 192000 with random events and block sizes 1 to 512,
no NaN, peak at most 1.

Not tested: listening comparison with a real OP-1 (the sound is a new design), armv7 timing on the device.
