# DNA DSP

## What the OP-1 does

The OP-1 engine is called dna on screen. The factory database gives it four knobs with defaults -16000, 5000, 0 and 1024
out of 32767. The first value is negative, so knob 1 is signed. The factory presets keep knob 1 anywhere from -21299 to
32767, knob 2 mostly between 4300 and 12800, knob 3 mostly at 0 or 32767, and knob 4 mostly at 0.

I worked from the decompile in `op1f-re/work/decomp/dsp/Id/`. These are the only facts I took from it:

- The engine is the `Id` class, and the factory switch builds it as case 0xb (`T10` is case 1, `DrWave` is case 2).
- Its constructor is a pool of 6 small objects of 32 bytes each. None of them holds oscillator state the way `T10` does.
- It calls a routine that parses a 32-character hex string, a 128-bit ID, into four 32-bit words and builds sliding-window
  bit tables in circular buffers.
- The screen art `id.svg` is a set of tile glyphs.

So the decompile points at a 128-bit pattern read out through bit windows. That is where the genome idea comes from. No DSP
body survived in the decompile, only constructors. Everything below the next heading is my own design.

### Id or T10

Id is dna. `T10` builds three phasor-like sub-objects with the same init as `Iter`, `LfoSingle` and `Slump`, and its screen art
`t10.svg` is three blue dots plus white, green and red dots and a line, which fits digital. dna has no oscillator sub-objects
and an ID parser. They do not look swapped. This is inference from constructors, assets and factory defaults, not from DSP code,
so a listening check against a real OP-1 would be the real proof.

## What this version does

DNA is a new implementation inspired by the OP-1 engine. It is not bit-exact and contains no firmware code or tables.

1. **Genome.** 128 bits make a one-bit wave. Three genomes exist: the original and two mutants, all fixed pseudo-random bit
   strings from a seeded xorshift, so every instance sounds the same.
2. **MUTATE** (signed, -1 to 1). Every bit has its own threshold along the knob. Past its threshold a bit morphs, with a
   smoothstep, from the original toward the mutant on that side (mutant 1 for negative, mutant 2 for positive). So the wave
   changes a few bits at a time as the knob moves.
3. **BITS.** How many genes make one cycle, 2 to 128. Between two counts the neighbouring waves blend, so the knob is smooth.
4. **SUB.** The table holds two cycles. At 0 the second equals the first. As SUB rises the second cycle is negated and rotated
   by 43 slots, which drops a sub-octave-like component in.
5. **GRIT.** Each voice flips random genes with probability 0.5 times GRIT. Each voice has its own noise generator.
6. **Playback.** The wave is piecewise constant, so it is read with exact second-order antiderivative anti-aliasing: the table
   keeps its first and second integrals and a read is the second difference around the phase, which equals the wave filtered
   by a triangle two samples wide. It runs in doubles because at low notes the phase step squared is near 1e-8. A small
   square bias keeps a constant genome from turning the wave silent.
7. **Voices and amp.** 8 voices, linear attack, exponential decay and release, sustain level, velocity, a 1 ms steal and
   retrigger fade. Mod wheel is vibrato (5.5 Hz, up to 0.6 semitone), CC 64 is the sustain pedal, pitch bend range is 1 to 12
   semitones, CC 120 and 123 stop all notes. Controls update every 32 samples, smoothed, and ramp between ticks.
8. **Output.** The voice sum is scaled and passed through a soft ceiling, so the output cannot pass 0.97 before LEVEL.

Envelope time knobs map exponentially from 1 ms to 5 s (`dna_time_seconds`). Both the desktop plugin and the MPC port use
the same 0 to 1 knob position and the same mapping.

## Tested (core/tests/test_core.c, built with ASan and UBSan)

- 758 checks pass. The reader matches a brute-force triangle filter, and the table is zero mean and peaks at 1.
- The played note is within 5 cents of its pitch (measured worst case 0.036 cents). Pitch bend range and mod wheel vibrato
  are checked.
- 8 voices plus steals give no NaN and a peak under 0 dBFS. Note off, pedal, all-off and reset reach silence.
- Retrigger and voice steal fade out over about 1 ms instead of cutting.
- Each of MUTATE, BITS, SUB and GRIT changes the spectrum. ADSR and LEVEL behave as set.
- Output is deterministic and bit-identical for any block splitting, block size 1 included, and reset equals a fresh instance.
- Moving MUTATE, BITS and SUB while a note holds adds no clicks.
- The anti-aliased reader keeps off-grid energy at least 10.2 dB under a plain table read.
- Absurd parameters, notes, sample rates and block sizes stay finite and bounded.

Not tested: a listening comparison against a real OP-1. Treat the sound as a new synth that shares the OP-1's four controls.
