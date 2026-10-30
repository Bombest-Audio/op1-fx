# Porting brief: one OP-1 synth or drum engine as an instrument plugin

Read `docs/PORTING-BRIEF.md` first. Everything there applies (folder ownership, no git, build/validate recipe, MPC toolchain,
Claude Design + Higgsfield for the faceplate, house rules, report format). This file lists what changes for an instrument.

## Differences from an effect

- Instrument: `op1fx_add_plugin(NAME <Name> ID <id> CODE <code> SYNTH CLAP_FEATURES instrument synthesizer stereo)`. The recipe
  then sets IS_SYNTH, MIDI input, AU type MusicDevice, AAX generator category and VST3 `Instrument Synth`.
- JUCE processor: output-only stereo bus (no input bus), `acceptsMidi()` true, polyphonic (8 voices, voice stealing that
  does not click), note on/off, velocity, pitch bend (+-2 semitones default), mod wheel if it maps to something sensible,
  sustain pedal, all-notes-off. Sample-accurate note timing inside the block.
- Params: the four engine knobs from the OP-1 screen (blue, green, white, orange order), plus an amp envelope (attack, decay,
  sustain, release) and output level. The factory defaults for the four knobs are in
  `~/bombest-audio/op1f-re/firmware/op1/op1_246/content/op1_factory.db`, table `synth_types` (or `drum_types`), `default_params`
  (0..32767 scale, signed values exist; the first four are the screen knobs, the rest are engine extras, use them if the engine
  needs them and say how). Decompile for the engine: `~/bombest-audio/op1f-re/work/decomp/dsp/<Codename>/`.
- DSP core (`core/<id>_core.c`): voice allocation lives in the core too so the MPC port shares it. API shape: `init(sr)`,
  `set_params`, `note_on(note, vel)`, `note_off(note)`, `pitch_bend`, `render(l, r, frames)`, `all_off`. No allocation after init.
- Tests (`core/tests/test_core.c`): pitch accuracy of the played note (measure the fundamental, within 5 cents), polyphony
  (8 notes, no NaN, bounded level), note-off releases to silence, retrigger and steal without clicks (step in the output under
  a threshold you justify), each of the four knobs audibly changes the spectrum or envelope, deterministic output for a fixed
  seed, level headroom (peak under 0 dBFS at max velocity, all voices).
- Animation: the editor reacts to the playing signal (output level and note activity from the core, read on a 60 Hz timer),
  restrained, like the effects.
- MPC port: an instrument port (no `"effect": true`), `midi()` for notes, bend, CC, `render()` in 128-frame blocks (or
  `SAMPLE_ACCURATE`), 44.1 kHz int16 stereo. Look at an existing synth port in `~/bombest-audio/mpc-re/mpc-vst-plugins/ports/`
  and `docs/PORTING.md`. CPU matters on the armv7 build: measure render time per block with `tools/bench.sh` or your own timer and
  report it as a fraction of the 2.9 ms block budget at 8 voices.
- Sample-based engines (Sampler, DrumSampler): the plugin plays a user sample. In JUCE, load a WAV by file chooser and drag and
  drop, store the path in state, ship one short built-in default sample (generate it in code or from your own recording, no
  third-party audio). In the MPC port, scan a `samples/` folder next to the `.so` and expose the choice as a whole-number parameter
  (`<key>_display` gives the name); ship a couple of generated samples there.

## Plugin codes

fm `OpFm`, cluster `OpCl`, pulse `OpPl`, phase `OpPh`, string `OpSt`, voltage `OpVl`, dsynth `OpDs`, digital `OpDg`, dna `OpDn`,
drwave `OpDw`, dbox `OpDb`, sampler `OpSm`, drumsampler `OpDm`. Bundle id and CLAP id are `audio.bombest.<id>`.
