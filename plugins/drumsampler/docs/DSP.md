# DrumSampler DSP notes

This is a new implementation inspired by the OP-1 drum sampler. It is not bit-exact and contains no firmware code or tables.

## From the reverse engineering
- 24 keyboard-mapped slots, one sample each, slot n on MIDI note 36 + n.
- Per slot: pitch, start (in), end (out), play mode, direction.
- Three play modes. The order GATE, ONE-SHOT, LOOP is inferred from the decompile, not confirmed on hardware.

## My own design
- Voices: 16 structs, 8 count as playing, up to 8 more are fading ghosts after a retrigger or steal. Slots are monophonic. A retrigger fades the old voice over 2 ms. A steal takes the quietest releasing voice, else the oldest.
- Catmull-Rom interpolation. Loops crossfade over at most 10 ms (and at most a quarter of the region). One-shots get a 1 ms end taper.
- ADSR is shared by all slots. Decay and release are times to -60 dB. Defaults are transparent (sustain 1).
- Output: level squared, then a soft ceiling (linear to 0.8, asymptote 0.99).
- `ds_set_sample` renders a short fade tail into the voice, so the host can free replaced audio immediately.
- Default kit: 24 sounds synthesized in code (`core/drumsampler_kit.c`), deterministic, no third-party audio.

## MPC port
Scans `samples/` in `create()` only (no I/O in `set_param`), falls back to the built-in kit. A `slot` parameter selects which slot the sample, pitch, start, end, mode, reverse and gain controls edit. State is saved by file name (about 930 bytes, under the 8192 limit).

## Measured
Core tests (ASan/UBSan): pitch error 0.000 cents over 25 rate/pitch combinations, peak 0.99 with 8 full-scale voices, no clicks on retrigger or steal. 8 looping voices cost 0.22% of a 2.9 ms block on this Mac. I did not measure on the MPC hardware.
