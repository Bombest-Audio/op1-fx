# DrWave faceplate brief

Canvas 1280 x 630 (the MPC plugin view size; the desktop window scales it at the same aspect ratio).

- Plate color `#07080c`, ink `#e9ece8`, muted `#6c7370`, track `#23262a`.
- Accents are the OP-1 screen colours: blue `#698eff` (WAVE), green `#2ef0a0` (BLEND), white `#e9ece8` (MOTION), orange `#ff9a3d` (FOLD).
  The envelope, level and tone knobs use muted grey.
- Type: Barlow Condensed for labels and readouts, a small mono caption under each big knob.
- Centre: a vintage doctor's head mirror in front of eight stacked neon waveforms, generated with Higgsfield (`design/art/mirror-a.png`, model
  gpt_image_2_5, one generation). The idea is the name: "dr wave" is a doctor plus waves. `compose_faceplate.py` places the picture at 540 px
  square, x 370, y 20, lightened onto the plate and feathered at its edges, and writes `art/faceplate-bg-1280x630.png`. No text, logo or
  person in the art. The OP-1 drwave screen asset (`drw.svg`) is an empty 320x160 canvas, so there was nothing there to follow.
- Left column: WAVE above BLEND. Right column: MOTION above FOLD. Each is a 240 x 250 cell: label (36 px), 270 degree ring
  (radius 74, stroke 6), readout (76 px), caption (12 px).
- Small knobs (150 x 130): ATK, DEC, LEVEL at x 335, SUS, REL, TONE at x 795.
- Wordmark DR WAVE top left at (48, 24) with WAVETABLE VOICE under it.
- Animation: a live trace of the current wave (the two bank waves around the WAVE setting, morphed) is drawn at y 470 under the mirror,
  with the BLEND mirror wave in green behind it. The trace swells and brightens with output level (rises at once, falls over about
  200 ms) and each note start sends a soft spot along it. Nothing moves when the synth is silent.

Layout source: the Claude Design project "DrWave faceplate" (`DrWave.dc.html`, https://claude.ai/design/p/8500be8c-5d92-4526-b1de-8c45bdeaa6a3).
It shows the layout with a striped placeholder where the Higgsfield picture goes. Interactions: drag a knob vertically, 220 px for the
full range, double-click for the default.
