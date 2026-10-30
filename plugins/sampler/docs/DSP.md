# Sampler DSP

A new implementation inspired by the OP-1 sampler synth engine. It is not bit-exact and uses no firmware code, constants
or tables.

## What came from the firmware

The decompile of the engine (`op1f-re/work/decomp/dsp/SynthSampler/`) is thin. It has a voice-pool setup that makes eight
voices, a note-on entry that reads one parameter and scales it, a note-off entry that sets a release flag, and one large
render function in Blackfin fixed point that I did not try to turn back into an algorithm. What I took from it:

- eight voices
- one sample played across the keyboard
- note on and note off as separate entry points

The factory database has no `synth_types` row for the sampler (the table lists cluster, digital, drwave, fm, phase, pulse,
dna, string, dsynth and voltage), so there are no factory knob defaults to copy. Everything else, including the knob
meanings, is my design from how the OP-1 sampler is known to behave (a sample you can trim, tune and loop, played with an
envelope). I could not confirm the OP-1's own knob assignments from the firmware.

## Signal path

Per voice: sample read, envelope, velocity, voice gain, then the sum of the voices goes through a soft limiter and the
level control. The output is mono-in, mono-out and copied to both channels. Stereo files are mixed to mono on load.

- **Pitch.** Read increment = `2^((note - root) / 12) * sample_rate / output_rate`, then tune and bend as a multiplier
  that is smoothed over 5 ms and ramped per sample inside each 16-frame control tick. Mod wheel adds a 5.5 Hz vibrato of up to
  half a semitone. Interpolation is four-point cubic (Catmull-Rom). There is no band limiting, so a sample played several
  octaves above its root aliases, and one played below its root is clean.
- **START and END.** The played region. They are read at note on, so turning them affects the next note, not the one
  sounding. The region is never shorter than 8 frames. END below START is lifted to START plus 8 frames.
- **One-shot** (LOOP at 0). Plays START to END once. The last 32 source frames are faded to zero so the voice does not end on
  a step. When the region ends the voice stops, whatever the envelope is doing.
- **LOOP above 0.** Loops START to END. The value is the crossfade length, from 0 to half the region. Near the end the
  loop blends linearly with the material just after the loop start, and playback then jumps to START plus the crossfade
  length, so the seam is continuous. A linear fade suits correlated material (a loop cut at a zero crossing, a pad);
  uncorrelated noise would dip a little at the middle of the fade.
- **Envelope.** Linear attack, exponential decay toward the sustain level, exponential release. Decay and release are
  time to fall 60 dB. Release under 1e-5 ends the voice.
- **Voices.** Eight. A note that is already sounding is retriggered on its own voice. Otherwise a free voice, then the
  quietest releasing voice, then the oldest. When a voice is taken over or the sample is swapped, what the old voice was
  putting out carries on as a remainder that decays over 2 ms, so there is no step.
- **Level.** Output level is squared. Each voice peaks at 0.7 for a full-scale sample at full velocity. The sum goes through a
  soft limiter that does nothing below 0.75 and rounds off toward 1.0 above, so eight coherent full-scale voices stay under 0 dBFS.
- **Velocity** is linear gain.

## Parameters

| Control | Range | Default | Note |
|---|---|---|---|
| START | 0..1 | 0 | fraction of the sample |
| END | 0..1 | 1 | fraction of the sample |
| TUNE | -12..+12 semitones | 0 | added to the note |
| LOOP | 0..1 | 0 (off) | crossfade length when above 0 |
| ATK | 0.5 ms..5 s | 2 ms | |
| DEC | 5 ms..10 s | 1.5 s | |
| SUS | 0..1 | 1 | |
| REL | 5 ms..10 s | 250 ms | |
| LEVEL | 0..1 | 0.8 | squared |
| ROOT | note 0..127 | 60 | plugin only; follows the loaded file's smpl chunk when it has one |
| Bend range | 1..12 semitones | 2 | plugin only |

The MPC port has no ROOT parameter. It uses the root of each sample: the built-in sample's own root, or the file's smpl chunk
note, or 60.

## Built-in samples

Generated in code, no recordings: a struck bar (inharmonic partials, C4), a pad that repeats exactly once a second (whole-number
Hz partials, about C4, loops cleanly), and a plucked low string (harmonics that die faster the higher they are, C2). The
noise in the bar and pluck onsets is a fixed-seed generator, so the output is the same every time.

## Limits

- Samples are mono in memory, up to 30 seconds.
- No time stretching, no sample-rate-aware filtering, no multisampling.
- Pitch above the root aliases.
